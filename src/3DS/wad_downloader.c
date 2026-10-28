#include <time.h>
#include <stdlib.h>
#include "wad_downloader.h"
#include "wad_picker.h"
#include "trinity_version.h"
#include <png.h>
#include <3ds.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <minizip/unzip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <malloc.h>
#include <ctype.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *s_soc_buffer = NULL;
static bool s_net_inited = false;

static bool Downloader_InitNetwork(void)
{
    if (s_net_inited) return true;
    s_soc_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!s_soc_buffer) return false;

    Result res = socInit(s_soc_buffer, SOC_BUFFERSIZE);
    if (R_FAILED(res)) {
        free(s_soc_buffer);
        s_soc_buffer = NULL;
        return false;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    s_net_inited = true;
    return true;
}

static void Downloader_ExitNetwork(void)
{
    if (s_net_inited) {
        curl_global_cleanup();
        socExit();
        if (s_soc_buffer) {
            free(s_soc_buffer);
            s_soc_buffer = NULL;
        }
        s_net_inited = false;
    }
}

#include "wad_catalog.h"
// In-memory cache for installed status & cover availability (zero SD I/O during rendering)
static bool s_installed[CATALOG_COUNT];
static bool s_has_cover[CATALOG_COUNT];
static bool s_store_cache_initialized = false;

static void Downloader_RefreshInstalledCache(void)
{
    memset(s_installed, 0, sizeof(s_installed));
    memset(s_has_cover, 0, sizeof(s_has_cover));

    // Read directory entries into memory ONCE (approx 1-2 milliseconds on FAT32)
    #define MAX_DIR_FILES 1024
    static char local_files[MAX_DIR_FILES][64];
    int local_count = 0;

    DIR *d = opendir("sdmc:/3ds/Trinity3DS/wads");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && local_count < MAX_DIR_FILES) {
            if (ent->d_name[0] == '.') continue;
            strncpy(local_files[local_count], ent->d_name, sizeof(local_files[local_count]) - 1);
            local_files[local_count][sizeof(local_files[local_count]) - 1] = '\0';
            local_count++;
        }
        closedir(d);
    }

    if (local_count == 0) return;

    // Fast in-memory match (0.001 ms)
    for (int i = 0; i < CATALOG_COUNT; i++) {
        const char *target = s_catalog[i].target_file;
        char base[64];
        strncpy(base, target, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';

        char png_name[64];
        snprintf(png_name, sizeof(png_name), "%s.png", base);

        for (int j = 0; j < local_count; j++) {
            if (!s_installed[i] && !strcasecmp(target, local_files[j])) {
                s_installed[i] = true;
            }
            if (!s_has_cover[i] && !strcasecmp(png_name, local_files[j])) {
                s_has_cover[i] = true;
            }
        }
    }
}

static char *Downloader_strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

const repo_item_t* Downloader_FindCatalogItem(const char *filename)
{
    if (!filename || !filename[0]) return NULL;

    char base[64];
    strncpy(base, filename, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    // 1. Check official IWADs & expansions
    int iwad_count = sizeof(s_iwad_metadata) / sizeof(s_iwad_metadata[0]);
    for (int i = 0; i < iwad_count; i++) {
        if (!strcasecmp(filename, s_iwad_metadata[i].target_file) ||
            !strcasecmp(base, s_iwad_metadata[i].target_file)) {
            return &s_iwad_metadata[i];
        }
        char ibase[64];
        strncpy(ibase, s_iwad_metadata[i].target_file, sizeof(ibase) - 1);
        ibase[sizeof(ibase) - 1] = '\0';
        char *idot = strrchr(ibase, '.');
        if (idot) *idot = '\0';
        if (!strcasecmp(base, ibase)) {
            return &s_iwad_metadata[i];
        }
    }

    // 2. Exact match on target_file in s_catalog
    for (int i = 0; i < CATALOG_COUNT; i++) {
        if (!strcasecmp(filename, s_catalog[i].target_file)) {
            return &s_catalog[i];
        }
    }

    // 3. Basename match on target_file
    for (int i = 0; i < CATALOG_COUNT; i++) {
        char cat_base[64];
        strncpy(cat_base, s_catalog[i].target_file, sizeof(cat_base) - 1);
        cat_base[sizeof(cat_base) - 1] = '\0';
        char *cat_dot = strrchr(cat_base, '.');
        if (cat_dot) *cat_dot = '\0';

        if (!strcasecmp(base, cat_base)) {
            return &s_catalog[i];
        }
    }

    // 4. Partial match against title
    for (int i = 0; i < CATALOG_COUNT; i++) {
        if (Downloader_strcasestr(s_catalog[i].title, base) || Downloader_strcasestr(base, s_catalog[i].title)) {
            return &s_catalog[i];
        }
    }

    return NULL;
}

// SSL configuration: uses real Mozilla CA bundle if present on SD card
static void Downloader_SetupSSL(CURL *curl)
{
    const char *ca_path = "sdmc:/3ds/Trinity3DS/cacert.pem";
    if (access(ca_path, F_OK) == 0) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
}

// Download progress state
typedef struct {
    curl_off_t dltotal;
    curl_off_t dlnow;
    const char *title;
    bool abort_requested;
    u64 last_render_tick;
} dl_progress_t;

// Background download thread context
typedef struct {
    const char *url;
    FILE *fp;
    volatile curl_off_t dltotal;
    volatile curl_off_t dlnow;
    volatile bool abort_requested;
    volatile bool finished;
    CURLcode res;
} dl_thread_ctx_t;

static void Downloader_RenderProgress(dl_progress_t *prog)
{
    u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!bot_fb) return;

    // Background
    WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 16, 16, 20);

    // Modal Card
    WP_FillRect(bot_fb, 320, 15, 30, 290, 180, 28, 28, 35);
    WP_DrawRect(bot_fb, 320, 15, 30, 290, 180, 80, 160, 255);

    // Title
    WP_FillRect(bot_fb, 320, 15, 30, 290, 26, 35, 75, 140);
    WP_DrawCenteredText(bot_fb, 320, 15, 30, 290, 26, "DOWNLOADING...", 255, 255, 255, 1);

    // WAD Title
    if (strlen(prog->title) <= 24) {
        WP_DrawCenteredText(bot_fb, 320, 20, 68, 280, 20, prog->title, 255, 215, 0, 1);
    } else {
        char trunc[25];
        strncpy(trunc, prog->title, 24);
        trunc[24] = '\0';
        WP_DrawCenteredText(bot_fb, 320, 20, 68, 280, 20, trunc, 255, 215, 0, 1);
    }

    // Progress Bar Frame
    int bar_x = 25;
    int bar_y = 100;
    int bar_w = 270;
    int bar_h = 22;

    WP_FillRect(bot_fb, 320, bar_x, bar_y, bar_w, bar_h, 10, 10, 15);
    WP_DrawRect(bot_fb, 320, bar_x, bar_y, bar_w, bar_h, 120, 120, 140);

    // Fill progress
    float pct = 0.0f;
    if (prog->dltotal > 0) {
        pct = (float)prog->dlnow / (float)prog->dltotal;
        if (pct > 1.0f) pct = 1.0f;
        int fill_w = (int)(pct * (bar_w - 4));
        if (fill_w > 0) {
            WP_FillRect(bot_fb, 320, bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 30, 180, 80);
        }
    }

    // Percentage & Size Text
    char status_str[64];
    float cur_mb = (float)prog->dlnow / (1024.0f * 1024.0f);
    float tot_mb = (float)prog->dltotal / (1024.0f * 1024.0f);

    if (prog->dltotal > 0) {
        snprintf(status_str, sizeof(status_str), "%.1f%%  (%.2f / %.2f MB)", pct * 100.0f, cur_mb, tot_mb);
    } else {
        snprintf(status_str, sizeof(status_str), "Downloaded: %.2f MB", cur_mb);
    }
    WP_DrawCenteredText(bot_fb, 320, 20, 134, 280, 16, status_str, 200, 230, 255, 1);

    // Cancel hint
    WP_FillRect(bot_fb, 320, 25, 165, 270, 30, 60, 20, 20);
    WP_DrawRect(bot_fb, 320, 25, 165, 270, 30, 200, 50, 50);
    WP_DrawCenteredText(bot_fb, 320, 25, 165, 270, 30, "[B]  CANCEL DOWNLOAD", 255, 255, 255, 1);

    gfxFlushBuffers();
    gfxSwapBuffers();
}

static int Downloader_XferInfo(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    dl_thread_ctx_t *ctx = (dl_thread_ctx_t*)clientp;
    ctx->dltotal = dltotal;
    ctx->dlnow = dlnow;

    if (ctx->abort_requested) {
        return 1;
    }
    return 0;
}

static void Downloader_ThreadFunc(void *arg)
{
    dl_thread_ctx_t *ctx = (dl_thread_ctx_t*)arg;

    CURL *curl = curl_easy_init();
    if (!curl) {
        ctx->res = CURLE_FAILED_INIT;
        ctx->finished = true;
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, ctx->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx->fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Downloader_XferInfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    Downloader_SetupSSL(curl);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Trinity3DS/" TRINITY3DS_VERSION " (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 64 * 1024L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);

    ctx->res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    ctx->finished = true;
}

static bool Downloader_ExtractZip(const char *zip_path, const char *dest_dir, char *err_msg, size_t err_len)
{
    unzFile uf = unzOpen(zip_path);
    if (!uf) {
        snprintf(err_msg, err_len, "Could not open ZIP file!");
        return false;
    }

    if (unzGoToFirstFile(uf) != UNZ_OK) {
        unzClose(uf);
        snprintf(err_msg, err_len, "ZIP archive is empty!");
        return false;
    }

    char filename[256];
    unz_file_info file_info;
    char buffer[16384];
    int extracted = 0;

    do {
        if (unzGetCurrentFileInfo(uf, &file_info, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK) {
            continue;
        }

        if (strstr(filename, "__MACOSX") || strstr(filename, "/.")) {
            continue;
        }

        const char *dot = strrchr(filename, '.');
        if (!dot) continue;

        bool is_wad_deh = (strcasecmp(dot, ".wad") == 0 || strcasecmp(dot, ".deh") == 0);
        bool is_image = (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0);
        if (!is_wad_deh && !is_image) {
            continue;
        }

        const char *basename = strrchr(filename, '/');
        if (basename) basename++;
        else basename = filename;

        if (!basename[0] || basename[0] == '.') continue;

        if (unzOpenCurrentFile(uf) != UNZ_OK) continue;

        char out_path[512];
        snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, basename);

        FILE *out_fp = fopen(out_path, "wb");
        if (!out_fp) {
            unzCloseCurrentFile(uf);
            continue;
        }

        int read_bytes = 0;
        while ((read_bytes = unzReadCurrentFile(uf, buffer, sizeof(buffer))) > 0) {
            fwrite(buffer, 1, read_bytes, out_fp);
        }

        fclose(out_fp);
        unzCloseCurrentFile(uf);
        if (is_wad_deh && strcasecmp(dot, ".wad") == 0) {
            extracted++;
        }
    } while (unzGoToNextFile(uf) == UNZ_OK);

    unzClose(uf);

    if (extracted == 0) {
        snprintf(err_msg, err_len, "No .WAD file found in ZIP!");
        return false;
    }

    return true;
}

static const u8 s_doom_palette[768] = {
    0x00, 0x00, 0x00, 0x1F, 0x17, 0x0B, 0x17, 0x0F, 0x07, 0x4B, 0x4B, 0x4B,
    0xFF, 0xFF, 0xFF, 0x1B, 0x1B, 0x1B, 0x13, 0x13, 0x13, 0x0B, 0x0B, 0x0B,
    0x07, 0x07, 0x07, 0x2F, 0x37, 0x1F, 0x23, 0x2B, 0x0F, 0x17, 0x1F, 0x07,
    0x0F, 0x17, 0x00, 0x4F, 0x3B, 0x2B, 0x47, 0x33, 0x23, 0x3F, 0x2B, 0x1B,
    0xFF, 0xB7, 0xB7, 0xF7, 0xAB, 0xAB, 0xF3, 0xA3, 0xA3, 0xEB, 0x97, 0x97,
    0xE7, 0x8F, 0x8F, 0xDF, 0x87, 0x87, 0xDB, 0x7B, 0x7B, 0xD3, 0x73, 0x73,
    0xCB, 0x6B, 0x6B, 0xC7, 0x63, 0x63, 0xBF, 0x5B, 0x5B, 0xBB, 0x57, 0x57,
    0xB3, 0x4F, 0x4F, 0xAF, 0x47, 0x47, 0xA7, 0x3F, 0x3F, 0xA3, 0x3B, 0x3B,
    0x9B, 0x33, 0x33, 0x97, 0x2F, 0x2F, 0x8F, 0x2B, 0x2B, 0x8B, 0x23, 0x23,
    0x83, 0x1F, 0x1F, 0x7F, 0x1B, 0x1B, 0x77, 0x17, 0x17, 0x73, 0x13, 0x13,
    0x6B, 0x0F, 0x0F, 0x67, 0x0B, 0x0B, 0x5F, 0x07, 0x07, 0x5B, 0x07, 0x07,
    0x53, 0x07, 0x07, 0x4F, 0x00, 0x00, 0x47, 0x00, 0x00, 0x43, 0x00, 0x00,
    0xFF, 0xEB, 0xDF, 0xFF, 0xE3, 0xD3, 0xFF, 0xDB, 0xC7, 0xFF, 0xD3, 0xBB,
    0xFF, 0xCF, 0xB3, 0xFF, 0xC7, 0xA7, 0xFF, 0xBF, 0x9B, 0xFF, 0xBB, 0x93,
    0xFF, 0xB3, 0x83, 0xF7, 0xAB, 0x7B, 0xEF, 0xA3, 0x73, 0xE7, 0x9B, 0x6B,
    0xDF, 0x93, 0x63, 0xD7, 0x8B, 0x5B, 0xCF, 0x83, 0x53, 0xCB, 0x7F, 0x4F,
    0xBF, 0x7B, 0x4B, 0xB3, 0x73, 0x47, 0xAB, 0x6F, 0x43, 0xA3, 0x6B, 0x3F,
    0x9B, 0x63, 0x3B, 0x8F, 0x5F, 0x37, 0x87, 0x57, 0x33, 0x7F, 0x53, 0x2F,
    0x77, 0x4F, 0x2B, 0x6B, 0x47, 0x27, 0x5F, 0x43, 0x23, 0x53, 0x3F, 0x1F,
    0x4B, 0x37, 0x1B, 0x3F, 0x2F, 0x17, 0x33, 0x2B, 0x13, 0x2B, 0x23, 0x0F,
    0xEF, 0xEF, 0xEF, 0xE7, 0xE7, 0xE7, 0xDF, 0xDF, 0xDF, 0xDB, 0xDB, 0xDB,
    0xD3, 0xD3, 0xD3, 0xCB, 0xCB, 0xCB, 0xC7, 0xC7, 0xC7, 0xBF, 0xBF, 0xBF,
    0xB7, 0xB7, 0xB7, 0xB3, 0xB3, 0xB3, 0xAB, 0xAB, 0xAB, 0xA7, 0xA7, 0xA7,
    0x9F, 0x9F, 0x9F, 0x97, 0x97, 0x97, 0x93, 0x93, 0x93, 0x8B, 0x8B, 0x8B,
    0x83, 0x83, 0x83, 0x7F, 0x7F, 0x7F, 0x77, 0x77, 0x77, 0x6F, 0x6F, 0x6F,
    0x6B, 0x6B, 0x6B, 0x63, 0x63, 0x63, 0x5B, 0x5B, 0x5B, 0x57, 0x57, 0x57,
    0x4F, 0x4F, 0x4F, 0x47, 0x47, 0x47, 0x43, 0x43, 0x43, 0x3B, 0x3B, 0x3B,
    0x37, 0x37, 0x37, 0x2F, 0x2F, 0x2F, 0x27, 0x27, 0x27, 0x23, 0x23, 0x23,
    0x77, 0xFF, 0x6F, 0x6F, 0xEF, 0x67, 0x67, 0xDF, 0x5F, 0x5F, 0xCF, 0x57,
    0x5B, 0xBF, 0x4F, 0x53, 0xAF, 0x47, 0x4B, 0x9F, 0x3F, 0x43, 0x93, 0x37,
    0x3F, 0x83, 0x2F, 0x37, 0x73, 0x2B, 0x2F, 0x63, 0x23, 0x27, 0x53, 0x1B,
    0x1F, 0x43, 0x17, 0x17, 0x33, 0x0F, 0x13, 0x23, 0x0B, 0x0B, 0x17, 0x07,
    0xBF, 0xA7, 0x8F, 0xB7, 0x9F, 0x87, 0xAF, 0x97, 0x7F, 0xA7, 0x8F, 0x77,
    0x9F, 0x87, 0x6F, 0x9B, 0x7F, 0x6B, 0x93, 0x7B, 0x63, 0x8B, 0x73, 0x5B,
    0x83, 0x6B, 0x57, 0x7B, 0x63, 0x4F, 0x77, 0x5F, 0x4B, 0x6F, 0x57, 0x43,
    0x67, 0x53, 0x3F, 0x5F, 0x4B, 0x37, 0x57, 0x43, 0x33, 0x53, 0x3F, 0x2F,
    0x9F, 0x83, 0x63, 0x8F, 0x77, 0x53, 0x83, 0x6B, 0x4B, 0x77, 0x5F, 0x3F,
    0x67, 0x53, 0x33, 0x5B, 0x47, 0x2B, 0x4F, 0x3B, 0x23, 0x43, 0x33, 0x1B,
    0x7B, 0x7F, 0x63, 0x6F, 0x73, 0x57, 0x67, 0x6B, 0x4F, 0x5B, 0x63, 0x47,
    0x53, 0x57, 0x3B, 0x47, 0x4F, 0x33, 0x3F, 0x47, 0x2B, 0x37, 0x3F, 0x27,
    0xFE, 0xFF, 0x73, 0xFF, 0xEA, 0x43, 0xF6, 0xCE, 0x24, 0xE2, 0xA9, 0x10,
    0xCD, 0x83, 0x00, 0xAE, 0x5C, 0x00, 0x8E, 0x43, 0x00, 0x73, 0x2B, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xDB, 0xDB, 0xFF, 0xBB, 0xBB, 0xFF, 0x9B, 0x9B,
    0xFF, 0x7B, 0x7B, 0xFF, 0x5F, 0x5F, 0xFF, 0x3F, 0x3F, 0xFF, 0x1F, 0x1F,
    0xFF, 0x00, 0x00, 0xEF, 0x00, 0x00, 0xE3, 0x00, 0x00, 0xD7, 0x00, 0x00,
    0xCB, 0x00, 0x00, 0xBF, 0x00, 0x00, 0xB3, 0x00, 0x00, 0xA7, 0x00, 0x00,
    0x9B, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x73, 0x00, 0x00,
    0x67, 0x00, 0x00, 0x5B, 0x00, 0x00, 0x4F, 0x00, 0x00, 0x43, 0x00, 0x00,
    0xE7, 0xEF, 0xFF, 0xC7, 0xD9, 0xFF, 0xAB, 0xC6, 0xFF, 0x8F, 0xB2, 0xFF,
    0x73, 0x9F, 0xFF, 0x53, 0x89, 0xFF, 0x37, 0x76, 0xFF, 0x1B, 0x63, 0xFF,
    0x00, 0x51, 0xFF, 0x00, 0x48, 0xE3, 0x00, 0x40, 0xCB, 0x00, 0x39, 0xB3,
    0x00, 0x31, 0x9B, 0x00, 0x29, 0x83, 0x00, 0x22, 0x6B, 0x00, 0x1A, 0x53,
    0xFF, 0xFF, 0xFF, 0xFF, 0xEB, 0xDB, 0xFF, 0xD7, 0xBB, 0xFF, 0xC7, 0x9B,
    0xFF, 0xB3, 0x7B, 0xFF, 0xA3, 0x5B, 0xFF, 0x8F, 0x3B, 0xFF, 0x7F, 0x1B,
    0xF3, 0x73, 0x17, 0xEB, 0x6F, 0x0F, 0xDF, 0x67, 0x0F, 0xD7, 0x5F, 0x0B,
    0xCB, 0x57, 0x07, 0xC3, 0x4F, 0x00, 0xB7, 0x47, 0x00, 0xAF, 0x43, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xF6, 0xD7, 0xFF, 0xED, 0xB3, 0xFF, 0xE5, 0x8F,
    0xFF, 0xDC, 0x6B, 0xFF, 0xD4, 0x47, 0xFF, 0xCB, 0x23, 0xFF, 0xC3, 0x00,
    0xA7, 0x3F, 0x00, 0x9F, 0x37, 0x00, 0x93, 0x2F, 0x00, 0x87, 0x23, 0x00,
    0x4F, 0x3B, 0x27, 0x43, 0x2F, 0x1B, 0x37, 0x23, 0x13, 0x2F, 0x1B, 0x0B,
    0x00, 0x1A, 0x53, 0x00, 0x16, 0x47, 0x00, 0x13, 0x3B, 0x00, 0x0F, 0x2F,
    0x00, 0x0B, 0x23, 0x00, 0x07, 0x17, 0x00, 0x03, 0x0B, 0x00, 0x00, 0x00,
    0xFF, 0x9F, 0x43, 0xFF, 0xBD, 0x4B, 0xFF, 0x7B, 0xFF, 0xFF, 0x00, 0xFF,
    0xCF, 0x00, 0xCF, 0x9F, 0x00, 0x9B, 0x6F, 0x00, 0x6B, 0xA7, 0x6B, 0x6B,
};

static bool Downloader_SaveRGBToPNG(const char *filename, const u8 *rgb_data, int width, int height)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) return false;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { fclose(fp); return false; }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_write_struct(&png_ptr, NULL); fclose(fp); return false; }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    for (int y = 0; y < height; y++) {
        png_bytep row = (png_bytep)(rgb_data + y * width * 3);
        png_write_row(png_ptr, row);
    }

    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

static bool Downloader_ExtractTitlepicToPng(const char *wad_path, const char *out_png_path)
{
    FILE *f = fopen(wad_path, "rb");
    if (!f) return false;

    char hdr[12];
    if (fread(hdr, 1, 12, f) != 12) { fclose(f); return false; }

    int32_t numlumps = *(int32_t*)(hdr + 4);
    int32_t infotableofs = *(int32_t*)(hdr + 8);

    if (numlumps <= 0 || numlumps > 65536 || infotableofs <= 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, infotableofs, SEEK_SET) != 0) { fclose(f); return false; }

    int32_t target_pos = 0;
    int32_t target_size = 0;

    for (int i = 0; i < numlumps; i++) {
        uint8_t entry[16];
        if (fread(entry, 1, 16, f) != 16) break;

        char name[9] = {0};
        memcpy(name, entry + 8, 8);
        for (int c = 0; c < 8; c++) name[c] = toupper((unsigned char)name[c]);

        if (!strncmp(name, "TITLEPIC", 8)) {
            target_pos = *(int32_t*)(entry);
            target_size = *(int32_t*)(entry + 4);
            break; // Found primary title graphic!
        } else if (!strncmp(name, "INTERPIC", 8) || !strncmp(name, "DMENUPIC", 8) || !strncmp(name, "BOSSBACK", 8)) {
            if (target_pos == 0) {
                target_pos = *(int32_t*)(entry);
                target_size = *(int32_t*)(entry + 4);
            }
        }
    }

    if (target_pos <= 0 || target_size <= 8 || target_size > 2000000) {
        fclose(f);
        return false;
    }

    if (fseek(f, target_pos, SEEK_SET) != 0) { fclose(f); return false; }

    uint8_t *data = (uint8_t*)malloc(target_size);
    if (!data) { fclose(f); return false; }

    if (fread(data, 1, target_size, f) != (size_t)target_size) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);

    // Decode Doom Picture format
    int16_t width = *(int16_t*)(data);
    int16_t height = *(int16_t*)(data + 2);

    bool ok = false;
    if (width > 0 && width <= 640 && height > 0 && height <= 480 && (8 + width * 4) <= target_size) {
        uint32_t *col_ptrs = (uint32_t*)(data + 8);
        u8 *rgb = (u8*)calloc(width * height * 3, 1);
        if (rgb) {
            for (int x = 0; x < width; x++) {
                uint32_t ofs = col_ptrs[x];
                while (ofs < (uint32_t)target_size) {
                    uint8_t topdelta = data[ofs];
                    if (topdelta == 0xFF) break;
                    uint8_t length = data[ofs + 1];
                    ofs += 3;
                    for (int r = 0; r < length; r++) {
                        int y = topdelta + r;
                        if (y >= 0 && y < height && x >= 0 && x < width && (ofs + r) < (uint32_t)target_size) {
                            uint8_t pal_idx = data[ofs + r];
                            int idx = (y * width + x) * 3;
                            rgb[idx + 0] = s_doom_palette[pal_idx * 3 + 0];
                            rgb[idx + 1] = s_doom_palette[pal_idx * 3 + 1];
                            rgb[idx + 2] = s_doom_palette[pal_idx * 3 + 2];
                        }
                    }
                    ofs += length + 1;
                }
            }
            ok = Downloader_SaveRGBToPNG(out_png_path, rgb, width, height);
            free(rgb);
        }
    } else if (target_size == 64000) {
        u8 *rgb = (u8*)malloc(320 * 200 * 3);
        if (rgb) {
            for (int i = 0; i < 64000; i++) {
                uint8_t pal_idx = data[i];
                rgb[i * 3 + 0] = s_doom_palette[pal_idx * 3 + 0];
                rgb[i * 3 + 1] = s_doom_palette[pal_idx * 3 + 1];
                rgb[i * 3 + 2] = s_doom_palette[pal_idx * 3 + 2];
            }
            ok = Downloader_SaveRGBToPNG(out_png_path, rgb, 320, 200);
            free(rgb);
        }
    }

    free(data);
    return ok;
}

bool Downloader_FetchOnlineCover(const char *target_filename, const char *out_png_path)
{
    if (!target_filename || !out_png_path) return false;
    if (osGetWifiStrength() == 0) return false;
    if (!Downloader_InitNetwork()) return false;

    char base_name[64];
    strncpy(base_name, target_filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char lower_base[64];
    strncpy(lower_base, base_name, sizeof(lower_base) - 1);
    lower_base[sizeof(lower_base) - 1] = '\0';
    for (int i = 0; lower_base[i]; i++) lower_base[i] = tolower((unsigned char)lower_base[i]);

    const char *url_patterns[] = {
        "https://raw.githubusercontent.com/mcl5500/trinity3ds-covers/main/%s.png",
        "https://archive.org/download/doom-wads-covers/%s.png",
        "https://raw.githubusercontent.com/chocolatiers/doom-cover-art/master/%s.png"
    };

    char temp_png[256];
    snprintf(temp_png, sizeof(temp_png), "sdmc:/3ds/Trinity3DS/downloads/temp_cover.png");
    mkdir("sdmc:/3ds/Trinity3DS/downloads", 0777);

    for (size_t p = 0; p < sizeof(url_patterns) / sizeof(url_patterns[0]); p++) {
        char url[384];
        snprintf(url, sizeof(url), url_patterns[p], lower_base);

        FILE *fp = fopen(temp_png, "wb");
        if (!fp) continue;

        CURL *curl = curl_easy_init();
        if (!curl) { fclose(fp); unlink(temp_png); continue; }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        Downloader_SetupSSL(curl);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Trinity3DS/" TRINITY3DS_VERSION " (Nintendo 3DS)");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        fclose(fp);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && http_code == 200) {
            FILE *chk = fopen(temp_png, "rb");
            if (chk) {
                u8 sig[8] = {0};
                size_t read_bytes = fread(sig, 1, 8, chk);
                fseek(chk, 0, SEEK_END);
                long fsz = ftell(chk);
                fclose(chk);

                if (read_bytes >= 8 && sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G' && fsz > 100) {
                    rename(temp_png, out_png_path);
                    return true;
                }
            }
        }
        unlink(temp_png);
    }
    return false;
}

static bool Downloader_Download(const repo_item_t *item, char *err_msg, size_t err_len)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/Trinity3DS", 0777);
    mkdir("sdmc:/3ds/Trinity3DS/wads", 0777);
    mkdir("sdmc:/3ds/Trinity3DS/downloads", 0777);

    char temp_file[256];
    if (item->is_zip) {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Trinity3DS/downloads/temp_dl.zip");
    } else {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Trinity3DS/wads/%s", item->target_file);
    }

    FILE *fp = fopen(temp_file, "wb");
    if (!fp) {
        snprintf(err_msg, err_len, "Failed to create destination file on SD!");
        return false;
    }

    // Set 64 KB write buffer to eliminate SD FAT write stutter
    setvbuf(fp, NULL, _IOFBF, 64 * 1024);

    dl_thread_ctx_t ctx = {0};
    ctx.url = item->url;
    ctx.fp = fp;

    s32 prio = 0x31;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s32 thread_prio = (prio + 1 < 0x3f) ? prio + 1 : prio;

    Thread dl_thread = threadCreate(Downloader_ThreadFunc, &ctx, 32 * 1024, thread_prio, -1, false);
    if (!dl_thread) {
        // Fallback: synchronous download if thread creation fails
        Downloader_ThreadFunc(&ctx);
    } else {
        dl_progress_t prog = {0};
        prog.title = item->title;

        while (!ctx.finished) {
            hidScanInput();
            u32 kDown = hidKeysDown();
            touchPosition touch;
            hidTouchRead(&touch);

            if (kDown & KEY_B) {
                ctx.abort_requested = true;
            }
            if ((kDown & KEY_TOUCH) && touch.py >= 165 && touch.py <= 195 && touch.px >= 25 && touch.px <= 295) {
                ctx.abort_requested = true;
            }

            prog.dltotal = ctx.dltotal;
            prog.dlnow = ctx.dlnow;
            Downloader_RenderProgress(&prog);

            gspWaitForVBlank();
        }

        threadJoin(dl_thread, U64_MAX);
        threadFree(dl_thread);
    }

    fclose(fp);

    if (ctx.abort_requested) {
        unlink(temp_file);
        snprintf(err_msg, err_len, "Download cancelled by user.");
        return false;
    }

    if (ctx.res != CURLE_OK) {
        unlink(temp_file);
        snprintf(err_msg, err_len, "Network error: %s", curl_easy_strerror(ctx.res));
        return false;
    }

    if (item->is_zip) {
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 15, 70, 290, 100, 28, 28, 35);
            WP_DrawRect(bot_fb, 320, 15, 70, 290, 100, 255, 215, 0);
            WP_DrawCenteredText(bot_fb, 320, 20, 90, 280, 20, "EXTRACTING WAD FILES...", 255, 255, 255, 1);
            WP_DrawCenteredText(bot_fb, 320, 20, 120, 280, 20, "Please wait a moment...", 200, 200, 200, 1);
            gfxFlushBuffers();
            gfxSwapBuffers();
        }

        bool ext_ok = Downloader_ExtractZip(temp_file, "sdmc:/3ds/Trinity3DS/wads", err_msg, err_len);
        unlink(temp_file);
        if (!ext_ok) return false;
    }

    // Cover Import Priority 1: Check if ZIP contained an image file
    char base_name[64];
    strncpy(base_name, item->target_file, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char png_path[256];
    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);

    if (access(png_path, F_OK) != 0) {
        const char *alt_names[] = {
            "cover.png", "title.png", "titlepic.png", "preview.png", "boxart.png",
            "COVER.PNG", "TITLE.PNG", "TITLEPIC.PNG"
        };
        for (size_t a = 0; a < sizeof(alt_names) / sizeof(alt_names[0]); a++) {
            char alt_path[256];
            snprintf(alt_path, sizeof(alt_path), "sdmc:/3ds/Trinity3DS/wads/%s", alt_names[a]);
            if (access(alt_path, F_OK) == 0) {
                rename(alt_path, png_path);
                break;
            }
        }
    }

    // Cover Import Priority 2: Extract TITLEPIC directly from WAD if no PNG exists yet
    if (access(png_path, F_OK) != 0) {
        char target_wad_path[256];
        snprintf(target_wad_path, sizeof(target_wad_path), "sdmc:/3ds/Trinity3DS/wads/%s", item->target_file);
        Downloader_ExtractTitlepicToPng(target_wad_path, png_path);
    }

    // Cover Import Priority 3: Fallback online download if still no cover
    if (access(png_path, F_OK) != 0) {
        Downloader_FetchOnlineCover(item->target_file, png_path);
    }

    // Reload cover immediately so store and launcher display it!
    WP_UpdateCoverByFilename(item->target_file);

    // Automatically save local .txt metadata file
    char meta_path[256];
    snprintf(meta_path, sizeof(meta_path), "sdmc:/3ds/Trinity3DS/wads/%s.txt", base_name);
    FILE *mfp = fopen(meta_path, "w");
    if (mfp) {
        fprintf(mfp, "AUTHOR: %s\nBASE: %s\nDESCRIPTION:\n%s\n",
                item->author[0] ? item->author : "Unknown",
                item->base_iwad[0] ? item->base_iwad : "DOOM2.WAD",
                item->desc);
        fclose(mfp);
    }

    return true;
}

int Downloader_WordWrap(const char *text, char lines[][64], int max_lines, int max_chars)
{
    if (!text || !text[0]) return 0;
    int line_count = 0;
    const char *p = text;

    while (*p && line_count < max_lines) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        int len = strlen(p);
        if (len <= max_chars) {
            strncpy(lines[line_count], p, sizeof(lines[line_count]) - 1);
            lines[line_count][sizeof(lines[line_count]) - 1] = '\0';
            line_count++;
            break;
        }

        int break_pos = max_chars;
        while (break_pos > 0 && p[break_pos] != ' ' && p[break_pos] != '\n') {
            break_pos--;
        }
        if (break_pos == 0) {
            break_pos = max_chars;
        }

        int copy_len = break_pos;
        if (copy_len >= 64) copy_len = 63;
        strncpy(lines[line_count], p, copy_len);
        lines[line_count][copy_len] = '\0';
        line_count++;

        p += break_pos;
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    }

    return line_count;
}

void Downloader_ShowDescriptionCustom(const char *title, const char *author, const char *base_iwad, const char *size_str, const char *desc)
{
    if (!desc || !desc[0]) return;

    char lines[64][64];
    int line_count = Downloader_WordWrap(desc, lines, 64, 56);
    if (line_count == 0) {
        strcpy(lines[0], "No description available.");
        line_count = 1;
    }

    int visible_lines = 11;
    int scroll_line = 0;
    int max_scroll = (line_count > visible_lines) ? (line_count - visible_lines) : 0;
    int debounce = 10;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();
        u32 kh = hidKeysHeld();
        touchPosition touch;
        hidTouchRead(&touch);

        if (debounce > 0) {
            debounce--;
        } else {
            if (kd & (KEY_B | KEY_X | KEY_A | KEY_START)) {
                break;
            }

            if ((kd & KEY_UP) || (kh & KEY_UP)) {
                if (scroll_line > 0) scroll_line--;
            }
            if ((kd & KEY_DOWN) || (kh & KEY_DOWN)) {
                if (scroll_line < max_scroll) scroll_line++;
            }

            if (kd & KEY_TOUCH) {
                // Button Up: py: 36..80
                if (touch.py >= 36 && touch.py <= 80 && touch.px >= 20 && touch.px <= 300) {
                    if (scroll_line > 0) scroll_line--;
                }
                // Button Down: py: 130..174
                else if (touch.py >= 130 && touch.py <= 174 && touch.px >= 20 && touch.px <= 300) {
                    if (scroll_line < max_scroll) scroll_line++;
                }
                // Button Back: py: 190..235
                else if (touch.py >= 190 && touch.py <= 235 && touch.px >= 20 && touch.px <= 300) {
                    break;
                }
            }
        }

        // Top Screen: Description Text
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            WP_FillRect(top_fb, 400, 0, 0, 400, 240, 18, 20, 24);

            // Crimson Header
            WP_FillRect(top_fb, 400, 0, 0, 400, 32, 130, 20, 20);
            WP_FillRect(top_fb, 400, 0, 30, 400, 2, 210, 40, 40);
            WP_DrawText(top_fb, 400, 16, 8, title ? title : "WAD DETAILS", 255, 255, 255, 1);
            WP_DrawText(top_fb, 400, 280, 10, "[DESCRIPTION]", 255, 215, 0, 1);

            // Sub-header metadata
            char sub_str[96];
            snprintf(sub_str, sizeof(sub_str), "AUTHOR: %s  |  BASE: %s  |  SIZE: %s",
                     (author && author[0]) ? author : "Unknown",
                     (base_iwad && base_iwad[0]) ? base_iwad : "DOOM2.WAD",
                     (size_str && size_str[0]) ? size_str : "-");
            WP_DrawText(top_fb, 400, 16, 36, sub_str, 180, 210, 240, 1);

            // Viewport Box (Y: 50 to 208, H: 158)
            WP_FillRect(top_fb, 400, 12, 50, 376, 158, 26, 28, 34);
            WP_DrawRect(top_fb, 400, 12, 50, 376, 158, 65, 115, 180);

            for (int i = 0; i < visible_lines; i++) {
                int line_idx = scroll_line + i;
                if (line_idx < line_count) {
                    int line_y = 56 + i * 13;
                    WP_DrawText(top_fb, 400, 20, line_y, lines[line_idx], 230, 235, 245, 1);
                }
            }

            // Scrollbar
            if (max_scroll > 0) {
                int bar_total_h = 154;
                int thumb_h = (visible_lines * bar_total_h) / line_count;
                if (thumb_h < 16) thumb_h = 16;
                int thumb_y = 52 + (scroll_line * (bar_total_h - thumb_h)) / max_scroll;

                WP_FillRect(top_fb, 400, 381, 52, 5, bar_total_h, 35, 38, 45);
                WP_FillRect(top_fb, 400, 381, thumb_y, 5, thumb_h, 100, 180, 255);
            }

            // Footer
            WP_FillRect(top_fb, 400, 12, 214, 376, 20, 22, 24, 28);
            WP_DrawRect(top_fb, 400, 12, 214, 376, 20, 50, 50, 60);
            WP_DrawText(top_fb, 400, 18, 220, "D-PAD: Scroll   [B] / [X] / [A]: Close", 255, 215, 0, 1);
            if (max_scroll > 0) {
                char page_buf[32];
                snprintf(page_buf, sizeof(page_buf), "%d / %d", scroll_line + 1, max_scroll + 1);
                WP_DrawText(top_fb, 400, 340, 220, page_buf, 180, 180, 200, 1);
            }
        }

        // Bottom Screen: Touch controls
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 16, 16, 20);

            // Header bar
            WP_FillRect(bot_fb, 320, 0, 0, 320, 28, 30, 35, 45);
            WP_DrawCenteredText(bot_fb, 320, 0, 0, 320, 28, "DESCRIPTION & INFO", 255, 215, 0, 1);

            bool can_up = (scroll_line > 0);
            WP_FillRect(bot_fb, 320, 20, 36, 280, 44, can_up ? 25 : 18, can_up ? 70 : 25, can_up ? 120 : 35);
            WP_DrawRect(bot_fb, 320, 20, 36, 280, 44, can_up ? 80 : 40, can_up ? 160 : 50, can_up ? 255 : 60);
            WP_DrawCenteredText(bot_fb, 320, 20, 36, 280, 44, "[ ▲ ]  SCROLL UP", can_up ? 255 : 120, can_up ? 255 : 120, can_up ? 255 : 120, 1);

            char status_str[64];
            snprintf(status_str, sizeof(status_str), "Lines %d to %d of %d",
                     scroll_line + 1, (scroll_line + visible_lines > line_count) ? line_count : scroll_line + visible_lines, line_count);
            WP_FillRect(bot_fb, 320, 20, 86, 280, 38, 22, 24, 30);
            WP_DrawCenteredText(bot_fb, 320, 20, 86, 280, 38, status_str, 200, 220, 255, 1);

            bool can_down = (scroll_line < max_scroll);
            WP_FillRect(bot_fb, 320, 20, 130, 280, 44, can_down ? 25 : 18, can_down ? 70 : 25, can_down ? 120 : 35);
            WP_DrawRect(bot_fb, 320, 20, 130, 280, 44, can_down ? 80 : 40, can_down ? 160 : 50, can_down ? 255 : 60);
            WP_DrawCenteredText(bot_fb, 320, 20, 130, 280, 44, "[ ▼ ]  SCROLL DOWN", can_down ? 255 : 120, can_down ? 255 : 120, can_down ? 255 : 120, 1);

            WP_FillRect(bot_fb, 320, 20, 188, 280, 42, 120, 30, 30);
            WP_DrawRect(bot_fb, 320, 20, 188, 280, 42, 220, 60, 60);
            WP_DrawCenteredText(bot_fb, 320, 20, 188, 280, 42, "[ B / X ]  BACK TO LIST", 255, 255, 255, 1);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

void Downloader_ShowDescriptionModal(const repo_item_t *item)
{
    if (!item) return;
    Downloader_ShowDescriptionCustom(item->title, item->author, item->base_iwad, item->size_str, item->desc);
}

typedef struct {
    char *data;
    size_t size;
} curl_buf_t;

static size_t CurlWriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    curl_buf_t *mem = (curl_buf_t *)userp;
    if (mem->size + realsize > 65535) return 0;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

bool Downloader_FetchMetadataForWad(const char *filename, char *out_author, size_t author_len, char *out_base, size_t base_len, char *out_desc, size_t desc_len)
{
    if (!filename || !filename[0]) return false;

    char base_name[64];
    strncpy(base_name, filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    // 1. Check local catalog first (instant, 300+ entries)
    const repo_item_t *item = Downloader_FindCatalogItem(filename);
    if (item) {
        if (out_author && author_len > 0) {
            strncpy(out_author, item->author[0] ? item->author : "Unbekannt", author_len - 1);
            out_author[author_len - 1] = '\0';
        }
        if (out_base && base_len > 0) {
            strncpy(out_base, item->base_iwad[0] ? item->base_iwad : "DOOM2.WAD", base_len - 1);
            out_base[base_len - 1] = '\0';
        }
        if (out_desc && desc_len > 0) {
            strncpy(out_desc, item->desc, desc_len - 1);
            out_desc[desc_len - 1] = '\0';
        }

        // Save to .txt file
        char meta_path[256];
        snprintf(meta_path, sizeof(meta_path), "sdmc:/3ds/Trinity3DS/wads/%s.txt", base_name);
        FILE *fp = fopen(meta_path, "w");
        if (fp) {
            fprintf(fp, "AUTHOR: %s\nBASE: %s\nDESCRIPTION:\n%s\n",
                    item->author[0] ? item->author : "Unknown",
                    item->base_iwad[0] ? item->base_iwad : "DOOM2.WAD",
                    item->desc);
            fclose(fp);
        }

        // Try cover download if missing
        char png_path[256];
        snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);
        if (access(png_path, F_OK) != 0) {
            Downloader_InitNetwork();
            Downloader_FetchOnlineCover(filename, png_path);
        }

        return true;
    }

    // 2. Not in catalog: Query idgames API online
    if (!Downloader_InitNetwork()) return false;

    char url[256];
    snprintf(url, sizeof(url), "http://www.doomworld.com/idgames/api/api.php?action=search&query=%s&type=filename&out=json", base_name);

    curl_buf_t chunk = { malloc(1), 0 };
    if (!chunk.data) return false;
    chunk.data[0] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(chunk.data);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    Downloader_SetupSSL(curl);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Trinity3DS/" TRINITY3DS_VERSION " (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    bool found = false;
    if (res == CURLE_OK && http_code == 200 && chunk.data && chunk.size > 0) {
        struct json_object *root = json_tokener_parse(chunk.data);
        if (root) {
            struct json_object *content = NULL;
            if (json_object_object_get_ex(root, "content", &content)) {
                struct json_object *file = NULL;
                if (json_object_object_get_ex(content, "file", &file)) {
                    struct json_object *entry = file;
                    if (json_object_is_type(file, json_type_array) && json_object_array_length(file) > 0) {
                        entry = json_object_array_get_idx(file, 0);
                    }
                    if (entry) {
                        struct json_object *auth_obj = NULL;
                        struct json_object *desc_obj = NULL;
                        char a_str[64] = "Unknown";
                        char d_str[512] = "";

                        if (json_object_object_get_ex(entry, "author", &auth_obj)) {
                            strncpy(a_str, json_object_get_string(auth_obj), sizeof(a_str) - 1);
                        }
                        if (json_object_object_get_ex(entry, "description", &desc_obj)) {
                            strncpy(d_str, json_object_get_string(desc_obj), sizeof(d_str) - 1);
                        }

                        if (d_str[0]) {
                            found = true;
                            if (out_author && author_len > 0) strncpy(out_author, a_str, author_len - 1);
                            if (out_base && base_len > 0) strncpy(out_base, "DOOM2.WAD", base_len - 1);
                            if (out_desc && desc_len > 0) strncpy(out_desc, d_str, desc_len - 1);

                            // Save to .txt
                            char meta_path[256];
                            snprintf(meta_path, sizeof(meta_path), "sdmc:/3ds/Trinity3DS/wads/%s.txt", base_name);
                            FILE *fp = fopen(meta_path, "w");
                            if (fp) {
                                fprintf(fp, "AUTHOR: %s\nBASE: DOOM2.WAD\nDESCRIPTION:\n%s\n", a_str, d_str);
                                fclose(fp);
                            }
                        }
                    }
                }
            }
            json_object_put(root);
        }
    }
    free(chunk.data);

    // Try cover download if missing
    char png_path[256];
    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);
    if (access(png_path, F_OK) != 0) {
        Downloader_FetchOnlineCover(filename, png_path);
    }

    return found;
}

static void Downloader_RenderTopScreen(u8 *fb, const repo_item_t *cur, int sel_idx, int total_count, bool installed)
{
    // Background: Dark gunmetal gray
    WP_FillRect(fb, 400, 0, 0, 400, 240, 20, 20, 22);

    // Header banner: Deep Crimson
    WP_FillRect(fb, 400, 0, 0, 400, 32, 130, 20, 20);
    WP_FillRect(fb, 400, 0, 30, 400, 2, 210, 40, 40);
    WP_DrawText(fb, 400, 16, 8, "TRINITY3DS", 255, 255, 255, 2);
    WP_DrawText(fb, 400, 280, 12, TRINITY3DS_VERSION_STR " WAD STORE", 255, 215, 0, 1);

    // WiFi signal
    int wifi = osGetWifiStrength();
    int wx = 190;
    int wy = 10;

    // Draw signal bars (green if active, dark grey if inactive)
    WP_FillRect(fb, 400, wx,     wy + 8, 3, 4, wifi >= 1 ? 80 : 50, wifi >= 1 ? 255 : 50, wifi >= 1 ? 100 : 55);
    WP_FillRect(fb, 400, wx + 5, wy + 4, 3, 8, wifi >= 2 ? 80 : 50, wifi >= 2 ? 255 : 50, wifi >= 2 ? 100 : 55);
    WP_FillRect(fb, 400, wx + 10, wy,    3, 12, wifi >= 3 ? 80 : 50, wifi >= 3 ? 255 : 50, wifi >= 3 ? 100 : 55);

    if (wifi > 0) {
        WP_DrawText(fb, 400, wx + 18, wy + 3, "WIFI: ON", 100, 255, 120, 1);
    } else {
        WP_DrawText(fb, 400, wx + 18, wy + 3, "WIFI: OFF", 255, 80, 80, 1);
    }

    if (!cur || total_count == 0) {
        WP_DrawText(fb, 400, 20, 80, "NO ENTRIES FOUND", 255, 80, 80, 2);
        return;
    }

    // Card Box (Total width: 376, Left: X=12, Right: X=388, Top: Y=40, Bottom: Y=176)
    WP_FillRect(fb, 400, 12, 40, 376, 136, 30, 32, 38);
    WP_DrawRect(fb, 400, 12, 40, 376, 136, 70, 130, 200);

    // LEFT SIDE: Metadata Info (X: 20 to 266)
    // 1. Name
    if (strlen(cur->title) <= 17) {
        WP_DrawText(fb, 400, 20, 46, cur->title, 255, 255, 255, 2);
    } else {
        WP_DrawText(fb, 400, 20, 48, cur->title, 255, 255, 255, 1);
    }

    // 2. Author
    char author_str[64];
    snprintf(author_str, sizeof(author_str), "AUTHOR: %s", cur->author[0] ? cur->author : "Unknown");
    WP_DrawText(fb, 400, 20, 66, author_str, 255, 215, 0, 1);

    // 3. Category
    if (cur->is_iwad) {
        WP_FillRect(fb, 400, 20, 80, 125, 15, 20, 120, 40);
        WP_DrawCenteredText(fb, 400, 20, 80, 125, 15, "[STANDALONE IWAD]", 255, 255, 255, 1);
    } else {
        WP_FillRect(fb, 400, 20, 80, 140, 15, 20, 70, 130);
        WP_DrawCenteredText(fb, 400, 20, 80, 140, 15, "[COMMUNITY MOD/WAD]", 180, 230, 255, 1);
    }

    // 4. Base
    char req_str[64];
    snprintf(req_str, sizeof(req_str), "BASE: %s", cur->base_iwad);
    WP_DrawText(fb, 400, 20, 98, req_str, 180, 215, 245, 1);

    // 5. Size
    char sz_str[64];
    snprintf(sz_str, sizeof(sz_str), "SIZE: %s", cur->size_str);
    WP_DrawText(fb, 400, 20, 112, sz_str, 200, 200, 200, 1);

    // 6. Installation Status
    if (installed) {
        WP_FillRect(fb, 400, 20, 128, 145, 16, 20, 105, 30);
        WP_DrawCenteredText(fb, 400, 20, 128, 145, 16, "ALREADY INSTALLED", 100, 255, 120, 1);
    } else {
        WP_FillRect(fb, 400, 20, 128, 145, 16, 90, 70, 15);
        WP_DrawCenteredText(fb, 400, 20, 128, 145, 16, "NOT INSTALLED", 255, 215, 0, 1);
    }

    // 7. [X] DESCRIPTION Button & Position
    WP_FillRect(fb, 400, 20, 150, 145, 18, 40, 60, 95);
    WP_DrawRect(fb, 400, 20, 150, 145, 18, 80, 140, 220);
    WP_DrawCenteredText(fb, 400, 20, 150, 145, 18, "[X] DESCRIPTION", 255, 255, 255, 1);

    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "#%d/%d", sel_idx + 1, total_count);
    WP_DrawText(fb, 400, 175, 154, pos_buf, 160, 160, 180, 1);

    // RIGHT SIDE: Cover Art Box (X: 274, Y: 46, W: 106, H: 124)
    int cover_x = 274;
    int cover_y = 46;
    int cover_w = 106;
    int cover_h = 124;

    WP_FillRect(fb, 400, cover_x, cover_y, cover_w, cover_h, 16, 18, 22);
    WP_DrawRect(fb, 400, cover_x, cover_y, cover_w, cover_h, 70, 95, 130);

    if (WP_HasCover()) {
        WP_DrawCover(fb, 400, cover_x + 1, cover_y + 1, cover_w - 2, cover_h - 2);
    } else {
        WP_DrawCenteredText(fb, 400, cover_x, cover_y + 35, cover_w, 18, "NO", 110, 120, 135, 2);
        WP_DrawCenteredText(fb, 400, cover_x, cover_y + 57, cover_w, 18, "COVER", 110, 120, 135, 2);
        WP_DrawCenteredText(fb, 400, cover_x, cover_y + 85, cover_w, 14, "[Y] GET", 255, 215, 0, 1);
    }

    // Controls Help Box (Bottom)
    WP_FillRect(fb, 400, 12, 182, 376, 50, 24, 24, 28);
    WP_DrawRect(fb, 400, 12, 182, 376, 50, 70, 70, 80);
    WP_DrawText(fb, 400, 20, 189, "CONTROLS:", 255, 215, 0, 1);
    WP_DrawText(fb, 400, 100, 189, "D-PAD: Scroll   L / R: A-Z Jump", 200, 200, 200, 1);
    WP_DrawText(fb, 400, 20, 209, "A: Download  X: Details  Y: Fetch Cover  B: Back", 100, 255, 120, 1);
}
static void Downloader_RenderBottomScreen(u8 *fb, const repo_item_t *items, const bool *installed_flags, int count, int sel_idx, int scroll_offset)
{
    // Background
    WP_FillRect(fb, 320, 0, 0, 320, 240, 16, 16, 20);

    // Header bar
    WP_FillRect(fb, 320, 0, 0, 320, 24, 30, 35, 45);
    WP_DrawText(fb, 320, 8, 8, "AVAILABLE WADS & MEGAWADS:", 255, 215, 0, 1);

    // List of 5 items
    int visible_items = 5;
    int item_y_start = 28;
    int item_h = 32;

    for (int i = 0; i < visible_items; i++) {
        int idx = scroll_offset + i;
        int y = item_y_start + i * (item_h + 2);

        if (idx < count) {
            const repo_item_t *item = &items[idx];
            bool sel = (idx == sel_idx);
            bool installed = installed_flags ? installed_flags[idx] : false;

            // Row background
            if (sel) {
                WP_FillRect(fb, 320, 6, y, 308, item_h, 20, 60, 100);
                WP_DrawRect(fb, 320, 6, y, 308, item_h, 80, 180, 255);
            } else {
                WP_FillRect(fb, 320, 6, y, 308, item_h, 26, 26, 30);
                WP_DrawRect(fb, 320, 6, y, 308, item_h, 45, 45, 52);
            }

            // Indicator
            if (sel) {
                WP_DrawText(fb, 320, 12, y + 11, ">", 255, 255, 80, 2);
            }

            // Install status badge
            if (installed) {
                WP_FillRect(fb, 320, 26, y + 8, 28, 16, 20, 110, 30);
                WP_DrawCenteredText(fb, 320, 26, y + 8, 28, 16, "[OK]", 100, 255, 120, 1);
            } else {
                WP_FillRect(fb, 320, 26, y + 8, 28, 16, 70, 70, 80);
                WP_DrawCenteredText(fb, 320, 26, y + 8, 28, 16, "[--]", 200, 200, 200, 1);
            }

            // Name
            WP_DrawText(fb, 320, 62, y + 12, item->title, sel ? 255 : 220, sel ? 255 : 220, sel ? 100 : 220, 1);

            // Size badge
            WP_DrawText(fb, 320, 260, y + 12, item->size_str, 160, 180, 200, 1);
        }
    }

    // Bottom Action Buttons:
    // Left: [A] LADEN (X: 6, Y: 202, W: 96, H: 34)
    // Mid-Left: [Y] COVER (X: 106, Y: 202, W: 66, H: 34)
    // Mid-Right: [X] DETAILS (X: 176, Y: 202, W: 72, H: 34)
    // Right: [B] ZURUECK (X: 252, Y: 202, W: 62, H: 34)
    bool already_inst = (count > 0 && sel_idx < count && installed_flags) ? installed_flags[sel_idx] : false;

    if (already_inst) {
        WP_FillRect(fb, 320, 6, 202, 96, 34, 30, 70, 40);
        WP_DrawRect(fb, 320, 6, 202, 96, 34, 60, 150, 70);
        WP_DrawCenteredText(fb, 320, 6, 202, 96, 34, "RE-DL [A]", 200, 255, 200, 1);
    } else {
        WP_FillRect(fb, 320, 6, 202, 96, 34, 20, 120, 45);
        WP_DrawRect(fb, 320, 6, 202, 96, 34, 60, 230, 90);
        WP_DrawCenteredText(fb, 320, 6, 202, 96, 34, "[A] DOWNLOAD", 255, 255, 255, 1);
    }

    // Cover Button [Y]
    WP_FillRect(fb, 320, 106, 202, 66, 34, 40, 50, 30);
    WP_DrawRect(fb, 320, 106, 202, 66, 34, 180, 190, 60);
    WP_DrawCenteredText(fb, 320, 106, 202, 66, 34, "[Y] COVER", 255, 235, 120, 1);

    // Info / Description Button [X]
    WP_FillRect(fb, 320, 176, 202, 72, 34, 35, 55, 85);
    WP_DrawRect(fb, 320, 176, 202, 72, 34, 70, 120, 210);
    WP_DrawCenteredText(fb, 320, 176, 202, 72, 34, "[X] INFO", 255, 255, 255, 1);

    // Back button [B]
    WP_FillRect(fb, 320, 252, 202, 62, 34, 30, 30, 36);
    WP_DrawRect(fb, 320, 252, 202, 62, 34, 80, 80, 95);
    WP_DrawCenteredText(fb, 320, 252, 202, 62, 34, "[B]", 255, 255, 255, 1);
}

static void Downloader_ShowModal(const char *title, const char *msg1, const char *msg2, u8 r, u8 g, u8 b)
{
    // Render loop for modal dialog (handles double buffering and proper input debounce)
    for (int t = 0; t < 300 && aptMainLoop(); t++) {
        hidScanInput();
        u32 kd = hidKeysDown();
        if (t > 15 && (kd & (KEY_A | KEY_B | KEY_START | KEY_TOUCH))) {
            break;
        }

        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 20, 50, 280, 135, 28, 28, 35);
            WP_DrawRect(bot_fb, 320, 20, 50, 280, 135, r, g, b);

            WP_FillRect(bot_fb, 320, 20, 50, 280, 26, r / 2, g / 2, b / 2);
            WP_DrawCenteredText(bot_fb, 320, 20, 50, 280, 26, title, 255, 255, 255, 1);

            if (msg1) WP_DrawCenteredText(bot_fb, 320, 25, 85, 270, 18, msg1, 255, 255, 255, 1);
            if (msg2) WP_DrawCenteredText(bot_fb, 320, 25, 110, 270, 18, msg2, 220, 220, 220, 1);

            WP_DrawCenteredText(bot_fb, 320, 25, 150, 270, 18, "Press [A] or tap...", 255, 215, 0, 1);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

static void Downloader_RenderCacheFrame(int progress_pct, const char *step_title, const char *log_lines[], int log_count, int spinner_frame)
{
    // 1. Top Screen (400x240)
    u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    if (top_fb) {
        // Background: Dark gunmetal
        WP_FillRect(top_fb, 400, 0, 0, 400, 240, 20, 20, 24);

        // Header banner: Deep Crimson
        WP_FillRect(top_fb, 400, 0, 0, 400, 32, 130, 20, 20);
        WP_FillRect(top_fb, 400, 0, 30, 400, 2, 210, 40, 40);
        WP_DrawText(top_fb, 400, 16, 8, "TRINITY3DS", 255, 255, 255, 2);
        WP_DrawText(top_fb, 400, 270, 12, TRINITY3DS_VERSION_STR " WAD STORE", 255, 215, 0, 1);

        // Central Card Frame
        WP_FillRect(top_fb, 400, 20, 46, 360, 172, 30, 32, 40);
        WP_DrawRect(top_fb, 400, 20, 46, 360, 172, 70, 140, 220);

        // Title Box
        WP_FillRect(top_fb, 400, 20, 46, 360, 28, 40, 60, 95);
        WP_DrawCenteredText(top_fb, 400, 20, 46, 360, 28, "INITIALIZING WAD STORE CACHE", 255, 255, 255, 1);

        // Spinner / Animation Icon
        static const char *spinner_chars[] = {"[ | ]", "[ / ]", "[ - ]", "[ \\ ]"};
        const char *spin = spinner_chars[(spinner_frame / 4) % 4];
        WP_DrawCenteredText(top_fb, 400, 20, 84, 360, 16, spin, 255, 215, 0, 2);

        // Step description
        if (step_title) {
            WP_DrawCenteredText(top_fb, 400, 30, 114, 340, 16, step_title, 200, 230, 255, 1);
        }

        // Progress Bar
        int bar_x = 40;
        int bar_y = 138;
        int bar_w = 320;
        int bar_h = 22;

        WP_FillRect(top_fb, 400, bar_x, bar_y, bar_w, bar_h, 12, 14, 18);
        WP_DrawRect(top_fb, 400, bar_x, bar_y, bar_w, bar_h, 120, 150, 190);

        if (progress_pct > 0) {
            int fill_w = (int)((progress_pct / 100.0f) * (bar_w - 4));
            if (fill_w > bar_w - 4) fill_w = bar_w - 4;
            u8 bar_r = (progress_pct == 100) ? 50 : 30 + (progress_pct % 20);
            u8 bar_g = (progress_pct == 100) ? 220 : 180;
            u8 bar_b = (progress_pct == 100) ? 70 : 80;
            WP_FillRect(top_fb, 400, bar_x + 2, bar_y + 2, fill_w, bar_h - 4, bar_r, bar_g, bar_b);
        }

        char pct_str[32];
        snprintf(pct_str, sizeof(pct_str), "%d %%", progress_pct);
        WP_DrawCenteredText(top_fb, 400, bar_x, bar_y + 4, bar_w, 16, pct_str, 255, 255, 255, 1);

        WP_DrawCenteredText(top_fb, 400, 20, 175, 360, 16, "One-time setup for maximum performance", 160, 170, 190, 1);
    }

    // 2. Bottom Screen (320x240)
    u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (bot_fb) {
        WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 14, 16, 20);

        WP_FillRect(bot_fb, 320, 10, 12, 300, 24, 25, 35, 48);
        WP_DrawRect(bot_fb, 320, 10, 12, 300, 24, 60, 120, 180);
        WP_DrawText(bot_fb, 320, 20, 18, "DIAGNOSTICS & SYSTEM STATUS", 255, 215, 0, 1);

        WP_FillRect(bot_fb, 320, 10, 40, 300, 186, 18, 22, 28);
        WP_DrawRect(bot_fb, 320, 10, 40, 300, 186, 50, 70, 90);

        for (int i = 0; i < log_count && i < 8; i++) {
            if (log_lines[i]) {
                WP_DrawText(bot_fb, 320, 20, 52 + i * 20, log_lines[i], 120, 230, 160, 1);
            }
        }

        if (progress_pct < 100) {
            WP_DrawText(bot_fb, 320, 20, 204, "> Please wait a moment...", 220, 220, 220, 1);
        } else {
            WP_DrawText(bot_fb, 320, 20, 204, "> Ready! Launching WAD Store...", 100, 255, 120, 1);
        }
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static void Downloader_InitStoreCacheWithAnimation(void)
{
    if (s_store_cache_initialized) return;

    const char *logs[8] = {0};
    int log_cnt = 0;
    int anim_frame = 0;

    static char log_buf0[64], log_buf1[64], log_buf2[64], log_buf3[64], log_buf4[64];

    snprintf(log_buf0, sizeof(log_buf0), "[..] SD Card: /3ds/Trinity3DS/wads/");
    logs[log_cnt++] = log_buf0;

    for (int f = 0; f < 10; f++) {
        Downloader_RenderCacheFrame(10 + f, "Scanning SD card folders...", logs, log_cnt, anim_frame++);
    }

    // Step 1: Scan local files
    #define MAX_CACHE_FILES 1024
    static char cache_files[MAX_CACHE_FILES][64];
    int cache_count = 0;

    DIR *d = opendir("sdmc:/3ds/Trinity3DS/wads");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && cache_count < MAX_CACHE_FILES) {
            if (ent->d_name[0] == '.') continue;
            strncpy(cache_files[cache_count], ent->d_name, sizeof(cache_files[cache_count]) - 1);
            cache_files[cache_count][sizeof(cache_files[cache_count]) - 1] = '\0';
            cache_count++;
        }
        closedir(d);
    }

    snprintf(log_buf0, sizeof(log_buf0), "[OK] Directory scanned: %d files", cache_count);

    int installed_count = 0;
    int covers_count = 0;

    // Step 2: Match catalog items
    snprintf(log_buf1, sizeof(log_buf1), "[..] Indexing %d WADs...", CATALOG_COUNT);
    logs[log_cnt++] = log_buf1;

    for (int f = 0; f < 10; f++) {
        Downloader_RenderCacheFrame(25 + f * 2, "Indexing catalog & install status...", logs, log_cnt, anim_frame++);
    }

    memset(s_installed, 0, sizeof(s_installed));
    memset(s_has_cover, 0, sizeof(s_has_cover));

    for (int i = 0; i < CATALOG_COUNT; i++) {
        const char *target = s_catalog[i].target_file;
        char base[64];
        strncpy(base, target, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';

        char png_name[64];
        snprintf(png_name, sizeof(png_name), "%s.png", base);

        for (int j = 0; j < cache_count; j++) {
            if (!s_installed[i] && !strcasecmp(target, cache_files[j])) {
                s_installed[i] = true;
                installed_count++;
            }
            if (!s_has_cover[i] && !strcasecmp(png_name, cache_files[j])) {
                s_has_cover[i] = true;
                covers_count++;
            }
        }
    }

    snprintf(log_buf1, sizeof(log_buf1), "[OK] Installed: %d | Covers: %d", installed_count, covers_count);

    // Step 3: Check TITLEPIC extraction for installed WADs without cover
    snprintf(log_buf2, sizeof(log_buf2), "[..] Optimizing cover cache...");
    logs[log_cnt++] = log_buf2;

    for (int f = 0; f < 8; f++) {
        Downloader_RenderCacheFrame(50 + f * 3, "Checking TITLEPICs of installed WADs...", logs, log_cnt, anim_frame++);
    }

    int extracted = 0;
    for (int i = 0; i < CATALOG_COUNT; i++) {
        if (s_installed[i] && !s_has_cover[i]) {
            char base[64];
            strncpy(base, s_catalog[i].target_file, sizeof(base) - 1);
            base[sizeof(base) - 1] = '\0';
            char *dot = strrchr(base, '.');
            if (dot) *dot = '\0';

            char wad_path[256];
            snprintf(wad_path, sizeof(wad_path), "sdmc:/3ds/Trinity3DS/wads/%s", s_catalog[i].target_file);
            char png_path[256];
            snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base);

            if (Downloader_ExtractTitlepicToPng(wad_path, png_path)) {
                s_has_cover[i] = true;
                extracted++;
            }
        }
    }

    if (extracted > 0) {
        snprintf(log_buf2, sizeof(log_buf2), "[OK] %d TITLEPICs extracted automatically", extracted);
    } else {
        snprintf(log_buf2, sizeof(log_buf2), "[OK] Cover cache is up to date");
    }

    // Step 4: Finalize
    snprintf(log_buf3, sizeof(log_buf3), "[OK] %d Megawads ready in RAM", CATALOG_COUNT);
    logs[log_cnt++] = log_buf3;

    snprintf(log_buf4, sizeof(log_buf4), "[OK] In-memory cache active!");
    logs[log_cnt++] = log_buf4;

    for (int pct = 80; pct <= 100; pct += 2) {
        Downloader_RenderCacheFrame(pct, "Initialization complete!", logs, log_cnt, anim_frame++);
    }

    // Hold 100% frame briefly for satisfying feedback (~15 frames = 250ms)
    for (int f = 0; f < 15; f++) {
        Downloader_RenderCacheFrame(100, "Enjoy the WAD Store!", logs, log_cnt, anim_frame++);
    }

    s_store_cache_initialized = true;
}

bool WAD_Downloader_Run(void)
{
    int selected_idx = 0;
    int scroll_offset = 0;
    bool any_installed = false;

    // Check if initial cache build is needed (with animated loading screen)
    if (!s_store_cache_initialized) {
        Downloader_InitStoreCacheWithAnimation();
    } else {
        Downloader_RefreshInstalledCache();
    }

    WP_InvalidateCoverCache();
    if (CATALOG_COUNT > 0) {
        WP_UpdateCoverByFilename(s_catalog[0].target_file);
    }

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        int count = CATALOG_COUNT;

        // Keep selected in bounds
        if (selected_idx >= count) selected_idx = count - 1;
        if (selected_idx < 0) selected_idx = 0;

        // Adjust scroll offset
        if (selected_idx < scroll_offset) {
            scroll_offset = selected_idx;
        }
        if (selected_idx >= scroll_offset + 5) {
            scroll_offset = selected_idx - 4;
        }

        // Exit / Back: instantaneous return!
        if (kDown & KEY_B) {
            break;
        }

        // Navigation
        if (kDown & (KEY_DUP | KEY_CPAD_UP)) {
            if (selected_idx > 0) selected_idx--;
        }
        if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) {
            if (selected_idx < count - 1) selected_idx++;
        }
        if (kDown & (KEY_DLEFT | KEY_CPAD_LEFT)) {
            selected_idx -= 5;
            if (selected_idx < 0) selected_idx = 0;
        }
        if (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) {
            selected_idx += 5;
            if (selected_idx >= count) selected_idx = count - 1;
        }

        // Alphabetical jumping with [L] and [R]!
        if (kDown & KEY_R) {
            char cur_ch = toupper((unsigned char)s_catalog[selected_idx].title[0]);
            int target = -1;
            for (int i = selected_idx + 1; i < count; i++) {
                char next_ch = toupper((unsigned char)s_catalog[i].title[0]);
                if (next_ch > cur_ch) {
                    target = i;
                    break;
                }
            }
            if (target >= 0) selected_idx = target;
        }
        if (kDown & KEY_L) {
            char cur_ch = toupper((unsigned char)s_catalog[selected_idx].title[0]);
            int cur_first = selected_idx;
            while (cur_first > 0 && toupper((unsigned char)s_catalog[cur_first - 1].title[0]) == cur_ch) {
                cur_first--;
            }
            if (cur_first < selected_idx) {
                selected_idx = cur_first;
            } else if (cur_first > 0) {
                char prev_ch = toupper((unsigned char)s_catalog[cur_first - 1].title[0]);
                int target = cur_first - 1;
                while (target > 0 && toupper((unsigned char)s_catalog[target - 1].title[0]) == prev_ch) {
                    target--;
                }
                selected_idx = target;
            }
        }

        // Touch Input
        if (kDown & KEY_TOUCH) {
            // Check list rows (Y: 28 to 198)
            int item_y_start = 28;
            int item_h = 32;

            for (int i = 0; i < 5; i++) {
                int y = item_y_start + i * (item_h + 2);
                if (touch.py >= y && touch.py < y + item_h && touch.px >= 6 && touch.px <= 314) {
                    int clicked_idx = scroll_offset + i;
                    if (clicked_idx < count) {
                        selected_idx = clicked_idx;
                    }
                    break;
                }
            }

            // Touch on Bottom Action Buttons:
            // [A] LADEN (6 to 102)
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 6 && touch.px <= 102) {
                kDown |= KEY_A;
            }
            // [Y] COVER (106 to 172)
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 106 && touch.px <= 172) {
                kDown |= KEY_Y;
            }
            // [X] DETAILS (176 to 248)
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 176 && touch.px <= 248) {
                kDown |= KEY_X;
            }
            // [B] ZURUECK (252 to 314)
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 252 && touch.px <= 314) {
                kDown |= KEY_B;
            }
        }

        // Show Description: [X] or [SELECT]
        if ((kDown & KEY_X) || (kDown & KEY_SELECT)) {
            if (count > 0 && selected_idx < count) {
                Downloader_ShowDescriptionModal(&s_catalog[selected_idx]);
            }
        }

        // Manual Online Cover Download: [Y]
        if (kDown & KEY_Y) {
            if (count > 0 && selected_idx < count) {
                const repo_item_t *cur = &s_catalog[selected_idx];
                if (osGetWifiStrength() == 0) {
                    Downloader_ShowModal("NO WIFI CONNECTION", "Please connect your 3DS to the internet!", "Required to download covers online.", 255, 80, 80);
                    continue;
                }
                if (!Downloader_InitNetwork()) {
                    Downloader_ShowModal("NETWORK ERROR", "Socket initialization failed!", NULL, 255, 50, 50);
                    continue;
                }

                char base_name[64];
                strncpy(base_name, cur->target_file, sizeof(base_name) - 1);
                base_name[sizeof(base_name) - 1] = '\0';
                char *dot = strrchr(base_name, '.');
                if (dot) *dot = '\0';

                char png_path[256];
                snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);

                Downloader_ShowModal("DOWNLOADING COVER...", cur->title, "Connecting to online repository...", 80, 160, 255);

                bool ok = Downloader_FetchOnlineCover(cur->target_file, png_path);
                if (ok) {
                    s_has_cover[selected_idx] = true;
                    WP_InvalidateCoverCache();
                    WP_UpdateCoverByFilename(cur->target_file);
                    Downloader_ShowModal("COVER DOWNLOADED!", cur->title, "Cover saved to SD card.", 50, 220, 80);
                } else {
                    Downloader_ShowModal("NO COVER FOUND", "No matching cover found in", "online repositories.", 255, 120, 80);
                }
            }
        }

        // Download Action: [A]
        if (kDown & KEY_A) {
            if (count > 0 && selected_idx < count) {
                if (osGetWifiStrength() == 0) {
                    Downloader_ShowModal("NO WIFI CONNECTION", "Please connect your 3DS to the internet!", "Check system settings.", 255, 80, 80);
                    continue;
                }

                if (!Downloader_InitNetwork()) {
                    Downloader_ShowModal("NETWORK ERROR", "Socket initialization failed!", "Check WiFi settings.", 255, 50, 50);
                    continue;
                }

                const repo_item_t *sel = &s_catalog[selected_idx];
                char err_msg[128] = {0};

                bool ok = Downloader_Download(sel, err_msg, sizeof(err_msg));
                if (ok) {
                    any_installed = true;
                    Downloader_RefreshInstalledCache();
                    WP_InvalidateCoverCache();
                    WP_UpdateCoverByFilename(sel->target_file);
                    Downloader_ShowModal("DOWNLOAD COMPLETE!", sel->title, "Successfully installed to /wads/!", 50, 220, 80);
                } else {
                    Downloader_ShowModal("DOWNLOAD FAILED", err_msg, "Please try again later.", 255, 60, 60);
                }
            }
        }

        // Update cover on selection change (instant 60 FPS, zero SD thrashing)
        static int s_last_sel = -1;
        if (selected_idx != s_last_sel) {
            s_last_sel = selected_idx;
            const repo_item_t *cur = (count > 0) ? &s_catalog[selected_idx] : NULL;
            if (cur) {
                WP_UpdateCoverByFilename(cur->target_file);
            } else {
                WP_FreeCover();
            }
        }

        // Render Frame
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            const repo_item_t *cur = (count > 0) ? &s_catalog[selected_idx] : NULL;
            bool inst = (count > 0) ? s_installed[selected_idx] : false;
            Downloader_RenderTopScreen(top_fb, cur, selected_idx, count, inst);
        }

        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            Downloader_RenderBottomScreen(bot_fb, s_catalog, s_installed, count, selected_idx, scroll_offset);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    WP_InvalidateCoverCache();
    return any_installed;
}

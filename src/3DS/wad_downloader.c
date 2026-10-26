#include "wad_downloader.h"
#include "wad_picker.h"
#include <3ds.h>
#include <curl/curl.h>
#include <minizip/unzip.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
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

typedef struct {
    char title[64];
    char author[64];
    char category[32];   // "STANDALONE", "MEGAWAD", "EPISODE"
    char base_iwad[32];  // e.g. "DOOM2.WAD", "DOOM.WAD", "KEINE"
    char desc1[64];
    char desc2[64];
    char url[256];
    char target_file[64];// Primary wad to check for installation
    char size_str[32];
    bool is_zip;
    bool is_iwad;
} repo_item_t;

// Curated verified Doom megawads & standalone games
static const repo_item_t s_curated_wads[] = {
    {
        "SIGIL (EPISODE 5)",
        "John Romero",
        "EPISODE",
        "DOOM.WAD",
        "Offizielle 5. Episode von Doom-Schoepfer John Romero.",
        "9 bockschwere Level im klassischen 1993-Stil.",
        "https://romero.com/s/SIGIL_v1_21.zip",
        "SIGIL_v1_21.wad",
        "3.25 MB",
        true,
        false
    },
    {
        "SIGIL II (EPISODE 6)",
        "John Romero",
        "EPISODE",
        "DOOM.WAD",
        "Offizielle 6. Episode zum 30. Doom-Jubilaeum.",
        "9 brandneue Level voller Herausforderungen.",
        "https://romero.com/s/SIGIL_II_V1_0.zip",
        "SIGIL_II_V1_0.WAD",
        "2.01 MB",
        true,
        false
    },
    {
        "FREEDOOM: PHASE 1 & 2",
        "Freedoom Project",
        "STANDALONE IWAD",
        "KEINE (STANDALONE)",
        "Komplett freie Standalone-Spiele (Phase 1 & 2)!",
        "68 Level insgesamt. Kein Original-Doom erforderlich.",
        "https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip",
        "freedoom2.wad",
        "24.1 MB",
        true,
        true
    },
    {
        "SCYTHE",
        "Erik Alm",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres 32-Level Megawad. Schnelle Action,",
        "fantastische Musik und purer Spielspass.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/scythe.zip",
        "SCYTHE.WAD",
        "2.08 MB",
        true,
        false
    },
    {
        "SCYTHE 2",
        "Erik Alm",
        "MEGAWAD",
        "DOOM2.WAD",
        "Die beruehmte Fortsetzung von Scythe (32 Level).",
        "Wunderschoene Texturen & geniales Leveldesign.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/scythe2.zip",
        "scythe2.wad",
        "10.5 MB",
        true,
        false
    },
    {
        "ALIEN VENDETTA",
        "Classic Doom Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "Einer der groessten Klassiker aller Zeiten (32 Level).",
        "Atemberaubende Kulissen und epischer Soundtrack.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/av.zip",
        "AV.WAD",
        "9.04 MB",
        true,
        false
    },
    {
        "PLUTONIA 2",
        "Plutonia 2 Team",
        "MEGAWAD",
        "PLUTONIA / DOOM2",
        "Der offizielle Community-Nachfolger zu Plutonia.",
        "32 knallharte Dschungel- & Tempel-Level.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/pl2.zip",
        "PL2.WAD",
        "5.37 MB",
        true,
        false
    },
    {
        "ANCIENT ALIENS",
        "skillsaw",
        "MEGAWAD",
        "DOOM2.WAD",
        "Buntes Meisterwerk mit Maya-Tempeln & Aliens.",
        "32 Level mit eigens komponiertem Soundtrack von stewboy.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/aaliens.zip",
        "aaliens_v1_2.wad",
        "39.8 MB",
        true,
        false
    },
    {
        "VALIANT",
        "skillsaw",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 rasante Level mit neuen Monster-Varianten,",
        "schnelleren Waffen und brillanter Action.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/valiant.zip",
        "Valiant.wad",
        "25.6 MB",
        true,
        false
    },
    {
        "REQUIEM",
        "The Innocent Crew & Friends",
        "MEGAWAD",
        "DOOM2.WAD",
        "32-Level Meilenstein von 1997.",
        "Duestere Atmosphaere und grandiose Architektur.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/requiem.zip",
        "REQUIEM.WAD",
        "3.98 MB",
        true,
        false
    },
    {
        "BACK TO SATURN X (EP 1)",
        "BTSX Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "Get Out of My Stations! Riesiges Sci-Fi Megawad.",
        "Eigene Texturen, Sounds & fantastische Musik.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/btsx_e1.zip",
        "btsx_e1a.wad",
        "17.9 MB",
        true,
        false
    }
};

#define CURATED_COUNT ((int)(sizeof(s_curated_wads) / sizeof(s_curated_wads[0])))

#define MAX_SEARCH_RESULTS 64
static repo_item_t s_search_results[MAX_SEARCH_RESULTS];
static int s_search_count = 0;
static bool s_in_search_mode = false;
static char s_last_search_query[64] = {0};

// In-memory cache for installed status (avoids hammering the slow SD card every frame)
static bool s_curated_installed[CURATED_COUNT];
static bool s_search_installed[MAX_SEARCH_RESULTS];

static bool Downloader_CheckFileOnDisk(const char *target_filename)
{
    if (!target_filename || !target_filename[0]) return false;

    char path[256];
    snprintf(path, sizeof(path), "sdmc:/3ds/Boom3DS/wads/%s", target_filename);
    if (access(path, R_OK) == 0) return true;

    snprintf(path, sizeof(path), "/3ds/Boom3DS/wads/%s", target_filename);
    if (access(path, R_OK) == 0) return true;

    return false;
}

static void Downloader_RefreshInstalledCache(void)
{
    for (int i = 0; i < CURATED_COUNT; i++) {
        s_curated_installed[i] = Downloader_CheckFileOnDisk(s_curated_wads[i].target_file);
    }
    for (int i = 0; i < s_search_count; i++) {
        s_search_installed[i] = Downloader_CheckFileOnDisk(s_search_results[i].target_file);
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
    WP_DrawCenteredText(bot_fb, 320, 15, 30, 290, 26, "DOWNLOAD LAEUFT...", 255, 255, 255, 1);

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
        snprintf(status_str, sizeof(status_str), "Geladen: %.2f MB", cur_mb);
    }
    WP_DrawCenteredText(bot_fb, 320, 20, 134, 280, 16, status_str, 200, 230, 255, 1);

    // Cancel hint
    WP_FillRect(bot_fb, 320, 25, 165, 270, 30, 60, 20, 20);
    WP_DrawRect(bot_fb, 320, 25, 165, 270, 30, 200, 50, 50);
    WP_DrawCenteredText(bot_fb, 320, 25, 165, 270, 30, "[B]  DOWNLOAD ABBRECHEN", 255, 255, 255, 1);

    gfxFlushBuffers();
    gfxSwapBuffers();
}

static int Downloader_XferInfo(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    dl_progress_t *prog = (dl_progress_t*)clientp;
    prog->dltotal = dltotal;
    prog->dlnow = dlnow;

    hidScanInput();
    u32 kDown = hidKeysDown();
    touchPosition touch;
    hidTouchRead(&touch);

    if (kDown & KEY_B) {
        prog->abort_requested = true;
        return 1;
    }
    if ((kDown & KEY_TOUCH) && touch.py >= 165 && touch.py <= 195 && touch.px >= 25 && touch.px <= 295) {
        prog->abort_requested = true;
        return 1;
    }

    u64 now = osGetTime();
    if (now - prog->last_render_tick >= 100) {
        prog->last_render_tick = now;
        Downloader_RenderProgress(prog);
    }

    return 0;
}

typedef struct {
    char *data;
    size_t size;
} mem_buffer_t;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    mem_buffer_t *mem = (mem_buffer_t *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

static bool Downloader_ExtractZip(const char *zip_path, const char *dest_dir, char *err_msg, size_t err_len)
{
    unzFile uf = unzOpen(zip_path);
    if (!uf) {
        snprintf(err_msg, err_len, "ZIP-Datei konnte nicht geoeffnet werden!");
        return false;
    }

    if (unzGoToFirstFile(uf) != UNZ_OK) {
        unzClose(uf);
        snprintf(err_msg, err_len, "ZIP-Archiv ist leer!");
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

        if (strcasecmp(dot, ".wad") != 0 && strcasecmp(dot, ".deh") != 0) {
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
        extracted++;
    } while (unzGoToNextFile(uf) == UNZ_OK);

    unzClose(uf);

    if (extracted == 0) {
        snprintf(err_msg, err_len, "Keine .WAD Datei im ZIP gefunden!");
        return false;
    }

    return true;
}

static bool Downloader_Download(const repo_item_t *item, char *err_msg, size_t err_len)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/Boom3DS", 0777);
    mkdir("sdmc:/3ds/Boom3DS/wads", 0777);
    mkdir("sdmc:/3ds/Boom3DS/downloads", 0777);

    char temp_file[256];
    if (item->is_zip) {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Boom3DS/downloads/temp_dl.zip");
    } else {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Boom3DS/wads/%s", item->target_file);
    }

    FILE *fp = fopen(temp_file, "wb");
    if (!fp) {
        snprintf(err_msg, err_len, "Konnte Zieldatei auf SD nicht erstellen!");
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        unlink(temp_file);
        snprintf(err_msg, err_len, "Curl-Initialisierung fehlgeschlagen!");
        return false;
    }

    dl_progress_t prog = {0};
    prog.title = item->title;
    prog.last_render_tick = osGetTime();

    curl_easy_setopt(curl, CURLOPT_URL, item->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Downloader_XferInfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Boom3DS/0.9.5 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (prog.abort_requested) {
        unlink(temp_file);
        snprintf(err_msg, err_len, "Download vom Benutzer abgebrochen.");
        return false;
    }

    if (res != CURLE_OK) {
        unlink(temp_file);
        snprintf(err_msg, err_len, "Netzwerkfehler: %s", curl_easy_strerror(res));
        return false;
    }

    if (item->is_zip) {
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 15, 70, 290, 100, 28, 28, 35);
            WP_DrawRect(bot_fb, 320, 15, 70, 290, 100, 255, 215, 0);
            WP_DrawCenteredText(bot_fb, 320, 20, 90, 280, 20, "ENTPACKE WAD-DATEIEN...", 255, 255, 255, 1);
            WP_DrawCenteredText(bot_fb, 320, 20, 120, 280, 20, "Bitte einen Moment Geduld...", 200, 200, 200, 1);
            gfxFlushBuffers();
            gfxSwapBuffers();
        }

        bool ext_ok = Downloader_ExtractZip(temp_file, "sdmc:/3ds/Boom3DS/wads", err_msg, err_len);
        unlink(temp_file);
        if (!ext_ok) return false;
    }

    return true;
}

static bool Downloader_SearchIdgames(const char *query, char *err_msg, size_t err_len)
{
    if (!query || !query[0]) return false;

    char encoded_query[128] = {0};
    int eq_idx = 0;
    for (int i = 0; query[i] && eq_idx < 120; i++) {
        if (query[i] == ' ') {
            strcat(encoded_query, "%20");
            eq_idx += 3;
        } else if (isalnum((unsigned char)query[i]) || query[i] == '-' || query[i] == '_') {
            encoded_query[eq_idx++] = query[i];
            encoded_query[eq_idx] = '\0';
        }
    }

    char api_url[256];
    snprintf(api_url, sizeof(api_url), "https://www.doomworld.com/idgames/api/api.php?action=search&query=%s&out=json", encoded_query);

    u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (bot_fb) {
        WP_FillRect(bot_fb, 320, 15, 70, 290, 100, 28, 28, 35);
        WP_DrawRect(bot_fb, 320, 15, 70, 290, 100, 80, 180, 255);
        WP_DrawCenteredText(bot_fb, 320, 20, 90, 280, 20, "DURCHSUCHE IDGAMES ARCHIV...", 255, 255, 255, 1);
        WP_DrawCenteredText(bot_fb, 320, 20, 120, 280, 20, query, 255, 215, 0, 1);
        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err_msg, err_len, "Curl konnte nicht initialisiert werden!");
        return false;
    }

    mem_buffer_t chunk = {0};
    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Boom3DS/0.9.5 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || !chunk.data) {
        if (chunk.data) free(chunk.data);
        snprintf(err_msg, err_len, "API-Anfrage fehlgeschlagen: %s", curl_easy_strerror(res));
        return false;
    }

    struct json_object *root = json_tokener_parse(chunk.data);
    free(chunk.data);

    if (!root) {
        snprintf(err_msg, err_len, "Ungueltige Antwort vom Doomworld-Server!");
        return false;
    }

    struct json_object *content_obj = NULL;
    if (!json_object_object_get_ex(root, "content", &content_obj)) {
        json_object_put(root);
        snprintf(err_msg, err_len, "Keine Treffer fuer '%s' im Archiv gefunden.", query);
        return false;
    }

    struct json_object *file_obj = NULL;
    if (!json_object_object_get_ex(content_obj, "file", &file_obj)) {
        json_object_put(root);
        snprintf(err_msg, err_len, "Keine Treffer fuer '%s' im Archiv gefunden.", query);
        return false;
    }

    s_search_count = 0;

    #define PARSE_FILE_ITEM(f_item) do { \
        if (s_search_count >= MAX_SEARCH_RESULTS) break; \
        repo_item_t *it = &s_search_results[s_search_count]; \
        memset(it, 0, sizeof(repo_item_t)); \
        struct json_object *val; \
        if (json_object_object_get_ex(f_item, "title", &val)) \
            strncpy(it->title, json_object_get_string(val), sizeof(it->title) - 1); \
        if (json_object_object_get_ex(f_item, "author", &val)) \
            strncpy(it->author, json_object_get_string(val), sizeof(it->author) - 1); \
        if (json_object_object_get_ex(f_item, "description", &val)) { \
            const char *desc = json_object_get_string(val); \
            strncpy(it->desc1, desc, sizeof(it->desc1) - 1); \
        } \
        const char *dir = ""; \
        const char *fn = ""; \
        if (json_object_object_get_ex(f_item, "dir", &val)) dir = json_object_get_string(val); \
        if (json_object_object_get_ex(f_item, "filename", &val)) fn = json_object_get_string(val); \
        snprintf(it->url, sizeof(it->url), "https://youfailit.net/pub/idgames/%s%s", dir, fn); \
        strncpy(it->target_file, fn, sizeof(it->target_file) - 1); \
        char *dot = strrchr(it->target_file, '.'); \
        if (dot && !strcasecmp(dot, ".zip")) { \
            strcpy(dot, ".wad"); \
            it->is_zip = true; \
        } \
        if (json_object_object_get_ex(f_item, "size", &val)) { \
            long sz = json_object_get_int64(val); \
            snprintf(it->size_str, sizeof(it->size_str), "%.2f MB", (float)sz / (1024.0f * 1024.0f)); \
        } \
        strncpy(it->category, "IDGAMES", sizeof(it->category) - 1); \
        strncpy(it->base_iwad, "DOOM2 / DOOM", sizeof(it->base_iwad) - 1); \
        s_search_count++; \
    } while(0)

    if (json_object_is_type(file_obj, json_type_array)) {
        int len = json_object_array_length(file_obj);
        for (int i = 0; i < len; i++) {
            struct json_object *item = json_object_array_get_idx(file_obj, i);
            if (item) PARSE_FILE_ITEM(item);
        }
    } else if (json_object_is_type(file_obj, json_type_object)) {
        PARSE_FILE_ITEM(file_obj);
    }

    json_object_put(root);

    if (s_search_count == 0) {
        snprintf(err_msg, err_len, "Keine passenden WADs fuer '%s' gefunden.", query);
        return false;
    }

    strncpy(s_last_search_query, query, sizeof(s_last_search_query) - 1);
    s_in_search_mode = true;
    Downloader_RefreshInstalledCache();
    return true;
}

static void Downloader_RenderTopScreen(u8 *fb, const repo_item_t *cur, int sel_idx, int total_count, bool installed)
{
    // Background
    WP_FillRect(fb, 400, 0, 0, 400, 240, 20, 20, 24);

    // Header bar
    WP_FillRect(fb, 400, 0, 0, 400, 32, 20, 60, 120);
    WP_FillRect(fb, 400, 0, 30, 400, 2, 80, 160, 255);
    WP_DrawText(fb, 400, 16, 8, "BOOM3DS WAD REPOSITORY", 255, 255, 255, 2);

    // WiFi Indicator
    u8 wifi = osGetWifiStrength();
    if (wifi == 0) {
        WP_DrawText(fb, 400, 310, 11, "WLAN: AUS", 255, 80, 80, 1);
    } else if (wifi == 1) {
        WP_DrawText(fb, 400, 310, 11, "WLAN: .| ", 255, 215, 0, 1);
    } else if (wifi == 2) {
        WP_DrawText(fb, 400, 310, 11, "WLAN: .||", 150, 255, 100, 1);
    } else {
        WP_DrawText(fb, 400, 310, 11, "WLAN: |||", 100, 255, 120, 1);
    }

    if (!cur || total_count == 0) {
        WP_DrawText(fb, 400, 20, 80, "KEINE EINTRÄGE GEFUNDEN", 255, 80, 80, 2);
        return;
    }

    // Card Box
    WP_FillRect(fb, 400, 12, 40, 376, 136, 30, 32, 38);
    WP_DrawRect(fb, 400, 12, 40, 376, 136, 70, 130, 200);

    // Title
    if (strlen(cur->title) <= 22) {
        WP_DrawText(fb, 400, 22, 48, cur->title, 255, 255, 255, 2);
    } else {
        WP_DrawText(fb, 400, 22, 50, cur->title, 255, 255, 255, 1);
    }

    // Author
    char author_str[96];
    snprintf(author_str, sizeof(author_str), "AUTOR: %s", cur->author[0] ? cur->author : "Unbekannt");
    WP_DrawText(fb, 400, 22, 70, author_str, 255, 215, 0, 1);

    // Category Badge
    if (cur->is_iwad) {
        WP_FillRect(fb, 400, 22, 88, 140, 16, 20, 120, 40);
        WP_DrawCenteredText(fb, 400, 22, 88, 140, 16, "[STANDALONE IWAD]", 255, 255, 255, 1);
    } else {
        WP_FillRect(fb, 400, 22, 88, 140, 16, 20, 70, 130);
        WP_DrawCenteredText(fb, 400, 22, 88, 140, 16, "[COMMUNITY MOD/WAD]", 180, 230, 255, 1);
    }

    // Requirement & Size
    char req_str[96];
    snprintf(req_str, sizeof(req_str), "BASIS: %s   GROESSE: %s", cur->base_iwad, cur->size_str);
    WP_DrawText(fb, 400, 22, 110, req_str, 200, 200, 200, 1);

    // Description
    if (cur->desc1[0]) {
        WP_DrawText(fb, 400, 22, 128, cur->desc1, 180, 210, 240, 1);
    }
    if (cur->desc2[0]) {
        WP_DrawText(fb, 400, 22, 142, cur->desc2, 180, 210, 240, 1);
    }

    // Install status on card
    if (installed) {
        WP_FillRect(fb, 400, 240, 88, 140, 16, 20, 100, 30);
        WP_DrawCenteredText(fb, 400, 240, 88, 140, 16, "BEREITS INSTALLIERT", 100, 255, 120, 1);
    } else {
        WP_FillRect(fb, 400, 240, 88, 140, 16, 90, 70, 15);
        WP_DrawCenteredText(fb, 400, 240, 88, 140, 16, "NICHT INSTALLIERT", 255, 215, 0, 1);
    }

    // Position indicator
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "#%d / %d", sel_idx + 1, total_count);
    WP_DrawText(fb, 400, 330, 158, pos_buf, 160, 160, 180, 1);

    // Controls Help Box (Bottom)
    WP_FillRect(fb, 400, 12, 182, 376, 50, 24, 24, 28);
    WP_DrawRect(fb, 400, 12, 182, 376, 50, 70, 70, 80);
    WP_DrawText(fb, 400, 20, 189, "STEUERUNG:", 255, 215, 0, 1);
    WP_DrawText(fb, 400, 100, 189, "D-PAD: Auswaehlen   A/TOUCH: Herunterladen", 200, 200, 200, 1);
    WP_DrawText(fb, 400, 20, 209, "X: idgames Suche    B: Zurueck zum WAD-Browser", 100, 255, 120, 1);
}

static void Downloader_RenderBottomScreen(u8 *fb, const repo_item_t *items, const bool *installed_flags, int count, int sel_idx, int scroll_offset)
{
    // Background
    WP_FillRect(fb, 320, 0, 0, 320, 240, 16, 16, 20);

    // Header bar
    WP_FillRect(fb, 320, 0, 0, 320, 24, 30, 35, 45);
    if (s_in_search_mode) {
        char s_hdr[64];
        snprintf(s_hdr, sizeof(s_hdr), "SUCHERGEBNISSE: '%s'", s_last_search_query);
        WP_DrawText(fb, 320, 8, 8, s_hdr, 255, 215, 0, 1);
    } else {
        WP_DrawText(fb, 320, 8, 8, "EMPFEHLUNGEN (KURATIERTER KATALOG):", 255, 215, 0, 1);
    }

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
    // Left: [HERUNTERLADEN] (X: 6, Y: 202, W: 194, H: 34)
    // Right: [SUCHE / KATALOG] (X: 206, Y: 202, W: 108, H: 34)
    bool already_inst = (count > 0 && sel_idx < count && installed_flags) ? installed_flags[sel_idx] : false;

    if (already_inst) {
        WP_FillRect(fb, 320, 6, 202, 194, 34, 30, 70, 40);
        WP_DrawRect(fb, 320, 6, 202, 194, 34, 60, 150, 70);
        WP_DrawCenteredText(fb, 320, 6, 202, 194, 34, "NEU LADEN [A]", 200, 255, 200, 1);
    } else {
        WP_FillRect(fb, 320, 6, 202, 194, 34, 20, 120, 45);
        WP_DrawRect(fb, 320, 6, 202, 194, 34, 60, 230, 90);
        WP_DrawCenteredText(fb, 320, 6, 202, 194, 34, "HERUNTERLADEN", 255, 255, 255, 2);
    }

    // Search / Back button
    WP_FillRect(fb, 320, 206, 202, 108, 34, 25, 60, 115);
    WP_DrawRect(fb, 320, 206, 202, 108, 34, 80, 160, 255);
    if (s_in_search_mode) {
        WP_DrawCenteredText(fb, 320, 206, 202, 108, 34, "[B] KATALOG", 255, 255, 255, 1);
    } else {
        WP_DrawCenteredText(fb, 320, 206, 202, 108, 34, "[X] SUCHE", 255, 255, 255, 1);
    }
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

            WP_DrawCenteredText(bot_fb, 320, 25, 150, 270, 18, "Druecke [A] oder tippe...", 255, 215, 0, 1);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

bool WAD_Downloader_Run(void)
{
    int selected_idx = 0;
    int scroll_offset = 0;
    bool any_installed = false;

    s_in_search_mode = false;
    s_search_count = 0;

    // Check installed status ONCE upfront in memory (zero SD I/O during render loop!)
    Downloader_RefreshInstalledCache();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        const repo_item_t *items = s_in_search_mode ? s_search_results : s_curated_wads;
        const bool *installed_flags = s_in_search_mode ? s_search_installed : s_curated_installed;
        int count = s_in_search_mode ? s_search_count : CURATED_COUNT;

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

        // Exit / Back
        if (kDown & KEY_B) {
            if (s_in_search_mode) {
                s_in_search_mode = false;
                selected_idx = 0;
                scroll_offset = 0;
                continue;
            } else {
                break;
            }
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

        // Search trigger: [X] button
        if (kDown & KEY_X) {
            if (osGetWifiStrength() == 0) {
                Downloader_ShowModal("KEIN WLAN", "Bitte 3DS mit dem Internet verbinden!", "Systemeinstellungen pruefen.", 255, 80, 80);
                continue;
            }

            if (!Downloader_InitNetwork()) {
                Downloader_ShowModal("NETZWERKFEHLER", "Socket-Initialisierung fehlgeschlagen!", "3DS unterstuetzt kein WiFi?", 255, 50, 50);
                continue;
            }

            SwkbdState swkbd;
            char query_buf[64] = {0};
            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(query_buf) - 1);
            swkbdSetHintText(&swkbd, "WAD-Name eingeben (z.B. scythe, plutonia)");

            SwkbdButton button = swkbdInputText(&swkbd, query_buf, sizeof(query_buf));
            if (button == SWKBD_BUTTON_CONFIRM && query_buf[0]) {
                char err[128];
                if (Downloader_SearchIdgames(query_buf, err, sizeof(err))) {
                    selected_idx = 0;
                    scroll_offset = 0;
                } else {
                    Downloader_ShowModal("SUCHE FEHLGESCHLAGEN", err, NULL, 255, 80, 80);
                }
            }
            continue;
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

            // Touch on Bottom Action Button: [HERUNTERLADEN]
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 6 && touch.px <= 200) {
                kDown |= KEY_A;
            }

            // Touch on Right Button: [SUCHE] / [KATALOG]
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 206 && touch.px <= 314) {
                if (s_in_search_mode) {
                    kDown |= KEY_B;
                } else {
                    kDown |= KEY_X;
                }
            }
        }

        // Download Action: [A]
        if (kDown & KEY_A) {
            if (count > 0 && selected_idx < count) {
                if (osGetWifiStrength() == 0) {
                    Downloader_ShowModal("KEIN WLAN", "Bitte 3DS mit dem Internet verbinden!", "Systemeinstellungen pruefen.", 255, 80, 80);
                    continue;
                }

                if (!Downloader_InitNetwork()) {
                    Downloader_ShowModal("NETZWERKFEHLER", "Socket-Initialisierung fehlgeschlagen!", "3DS unterstuetzt kein WiFi?", 255, 50, 50);
                    continue;
                }

                const repo_item_t *sel = &items[selected_idx];
                char err_msg[128] = {0};

                bool ok = Downloader_Download(sel, err_msg, sizeof(err_msg));
                if (ok) {
                    any_installed = true;
                    Downloader_RefreshInstalledCache();
                    Downloader_ShowModal("DOWNLOAD ERFOLGREICH!", sel->title, "Wurde erfolgreich nach /wads/ installiert!", 50, 220, 80);
                } else {
                    Downloader_ShowModal("FEHLER BEIM DOWNLOAD", err_msg, "Bitte spaeter erneut versuchen.", 255, 60, 60);
                }
            }
        }

        // Render Frame
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            const repo_item_t *cur = (count > 0) ? &items[selected_idx] : NULL;
            bool inst = (count > 0 && installed_flags) ? installed_flags[selected_idx] : false;
            Downloader_RenderTopScreen(top_fb, cur, selected_idx, count, inst);
        }

        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            Downloader_RenderBottomScreen(bot_fb, items, installed_flags, count, selected_idx, scroll_offset);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    Downloader_ExitNetwork();
    return any_installed;
}

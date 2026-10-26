#include "wad_picker.h"
#include "wad_downloader.h"
#include "i_main.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>
#include <png.h>
#include <SDL/SDL.h>
#include <SDL/SDL_mixer.h>

static Mix_Music *s_launcher_music = NULL;
static bool s_music_inited = false;

static void WP_StartBGM(void)
{
    if (s_music_inited) return;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        return;
    }
    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 1024) < 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    s_music_inited = true;

    const char *paths[] = {
        "sdmc:/3ds/Trinity3DS/launcher.ogg",
        "/3ds/Trinity3DS/launcher.ogg",
        "sdmc:/3ds/Trinity3DS/wads/launcher.ogg",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], R_OK) == 0) {
            s_launcher_music = Mix_LoadMUS(paths[i]);
            if (s_launcher_music) {
                Mix_VolumeMusic(85);
                Mix_PlayMusic(s_launcher_music, -1);
                break;
            }
        }
    }
}

static void WP_StopBGM(void)
{
    if (s_music_inited) {
        if (s_launcher_music) {
            Mix_HaltMusic();
            Mix_FreeMusic(s_launcher_music);
            s_launcher_music = NULL;
        }
        Mix_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        s_music_inited = false;
    }
}

#define MAX_WADS 512
#define LAST_WAD_FILE "sdmc:/3ds/Trinity3DS/last_wad.txt"
#define WAD_CACHE_FILE "sdmc:/3ds/Trinity3DS/wads_cache.bin"
#define CACHE_MAGIC 0x44415742 // 'BWAD'
#define CACHE_VERSION 2

typedef struct {
    char path[256];
    char filename[64];
    char display_name[64];
    bool is_iwad;
    long file_size;
} wad_info_t;

typedef struct {
    u32 magic;
    u32 version;
    int wad_count;
} cache_header_t;

typedef struct {
    int width;
    int height;
    u32 *pixels; // 0xRRGGBB
} wp_image_t;

static wad_info_t s_wads[MAX_WADS];
static int s_wad_count = 0;
static int s_selected_idx = 0;
static int s_scroll_offset = 0;

static wp_image_t *s_cur_cover = NULL;
static int s_cur_cover_idx = -1;

// Standard 5x7 Font data for ASCII 32 (' ') to 90 ('Z')
static const u8 s_font_5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' (32)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A (65)
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z (90)
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // [ (91)
    {0x02, 0x04, 0x08, 0x10, 0x20}, // \ (92)
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ] (93)
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^ (94)
    {0x40, 0x40, 0x40, 0x40, 0x40}  // _ (95)
};

void WP_DrawPixel(u8 *fb, int width, int x, int y, u8 r, u8 g, u8 b)
{
    if (x < 0 || x >= width || y < 0 || y >= 240) return;
    int offset = (x * 240 + (239 - y)) * 3;
    fb[offset + 0] = b;
    fb[offset + 1] = g;
    fb[offset + 2] = r;
}

void WP_FillRect(u8 *fb, int width, int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    for (int cy = y; cy < y + h; cy++) {
        for (int cx = x; cx < x + w; cx++) {
            WP_DrawPixel(fb, width, cx, cy, r, g, b);
        }
    }
}

void WP_DrawRect(u8 *fb, int width, int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    for (int cx = x; cx < x + w; cx++) {
        WP_DrawPixel(fb, width, cx, y, r, g, b);
        WP_DrawPixel(fb, width, cx, y + h - 1, r, g, b);
    }
    for (int cy = y; cy < y + h; cy++) {
        WP_DrawPixel(fb, width, x, cy, r, g, b);
        WP_DrawPixel(fb, width, x + w - 1, cy, r, g, b);
    }
}

static void WP_DrawChar(u8 *fb, int width, int x, int y, char c, u8 r, u8 g, u8 b, int scale)
{
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c < 32 || c > 95) c = ' ';
    int idx = c - 32;

    for (int col = 0; col < 5; col++) {
        u8 line = s_font_5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                if (scale == 1) {
                    WP_DrawPixel(fb, width, x + col, y + row, r, g, b);
                } else {
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            WP_DrawPixel(fb, width, x + col * scale + dx, y + row * scale + dy, r, g, b);
                        }
                    }
                }
            }
        }
    }
}

void WP_DrawText(u8 *fb, int width, int x, int y, const char *str, u8 r, u8 g, u8 b, int scale)
{
    int cur_x = x;
    int spacing = (scale == 1) ? 6 : (scale * 6);
    while (*str) {
        if (*str == '\n') {
            y += (scale == 1) ? 9 : (scale * 9);
            cur_x = x;
        } else {
            WP_DrawChar(fb, width, cur_x, y, *str, r, g, b, scale);
            cur_x += spacing;
        }
        str++;
    }
}

void WP_DrawCenteredText(u8 *fb, int width, int box_x, int box_y, int box_w, int box_h, const char *text, u8 r, u8 g, u8 b, int scale)
{
    if (!text || !text[0]) return;
    int len = strlen(text);
    int char_w = (scale == 1) ? 6 : (scale * 6);
    int text_w = len * char_w - (scale == 1 ? 1 : scale);
    int text_h = 7 * scale;

    int x = box_x + (box_w - text_w) / 2;
    int y = box_y + (box_h - text_h) / 2;

    WP_DrawText(fb, width, x, y, text, r, g, b, scale);
}

static void WP_FreeCover(void)
{
    if (s_cur_cover) {
        if (s_cur_cover->pixels) free(s_cur_cover->pixels);
        free(s_cur_cover);
        s_cur_cover = NULL;
    }
    s_cur_cover_idx = -1;
}

static wp_image_t* WP_LoadPNG(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { fclose(fp); return NULL; }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_read_struct(&png_ptr, NULL, NULL); fclose(fp); return NULL; }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    wp_image_t *img = (wp_image_t*)malloc(sizeof(wp_image_t));
    if (!img) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }
    img->width = width;
    img->height = height;
    img->pixels = (u32*)malloc(width * height * sizeof(u32));
    if (!img->pixels) {
        free(img);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        free(img->pixels);
        free(img);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_byte*)(img->pixels + y * width);
    }
    png_read_image(png_ptr, row_pointers);
    free(row_pointers);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
    return img;
}

static void WP_UpdateCurrentCover(int idx)
{
    if (idx == s_cur_cover_idx) return;
    WP_FreeCover();
    s_cur_cover_idx = idx;
    if (idx < 0 || idx >= s_wad_count) return;

    wad_info_t *wad = &s_wads[idx];
    char png_path[256];

    // 1. Direct path in /wads/<filename_without_ext>.png
    char base_name[64];
    strncpy(base_name, wad->filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);
    s_cur_cover = WP_LoadPNG(png_path);
    if (s_cur_cover) return;

    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Boom3DS/wads/%s.png", base_name);
    s_cur_cover = WP_LoadPNG(png_path);
    if (s_cur_cover) return;

    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.PNG", base_name);
    s_cur_cover = WP_LoadPNG(png_path);
    if (s_cur_cover) return;

    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Boom3DS/wads/%s.PNG", base_name);
    s_cur_cover = WP_LoadPNG(png_path);
    if (s_cur_cover) return;

    // 2. Try relative to wad path
    strncpy(png_path, wad->path, sizeof(png_path) - 1);
    png_path[sizeof(png_path) - 1] = '\0';
    dot = strrchr(png_path, '.');
    if (dot) {
        strcpy(dot, ".png");
        s_cur_cover = WP_LoadPNG(png_path);
    }
}

// Draw scaled cover image with aspect ratio preservation (centered inside dest box)
static void WP_DrawCover(u8 *fb, int fb_width, int dest_x, int dest_y, int dest_w, int dest_h)
{
    if (!s_cur_cover || !s_cur_cover->pixels) return;

    int src_w = s_cur_cover->width;
    int src_h = s_cur_cover->height;
    if (src_w <= 0 || src_h <= 0) return;

    // Calculate fitted dimensions preserving aspect ratio
    int fit_w = dest_w;
    int fit_h = (src_h * dest_w) / src_w;

    if (fit_h > dest_h) {
        fit_h = dest_h;
        fit_w = (src_w * dest_h) / src_h;
    }

    int offset_x = dest_x + (dest_w - fit_w) / 2;
    int offset_y = dest_y + (dest_h - fit_h) / 2;

    u32 *src = s_cur_cover->pixels;

    for (int dy = 0; dy < fit_h; dy++) {
        int sy = (dy * src_h) / fit_h;
        if (sy >= src_h) sy = src_h - 1;
        u32 *src_row = src + sy * src_w;

        for (int dx = 0; dx < fit_w; dx++) {
            int sx = (dx * src_w) / fit_w;
            if (sx >= src_w) sx = src_w - 1;

            u32 pixel = src_row[sx];
            // Format in memory from png: RGBA (little endian: R, G, B, A)
            u8 r = (pixel & 0x000000FF);
            u8 g = (pixel & 0x0000FF00) >> 8;
            u8 b = (pixel & 0x00FF0000) >> 16;
            u8 a = (pixel & 0xFF000000) >> 24;

            // Only draw non-transparent pixels
            if (a > 32) {
                WP_DrawPixel(fb, fb_width, offset_x + dx, offset_y + dy, r, g, b);
            }
        }
    }
}

// Clean up display names for common WADs
static void WP_FormatDisplayName(wad_info_t *wad)
{
    const char *fn = wad->filename;

    if (!strcasecmp(fn, "doom.wad") || !strcasecmp(fn, "doomu.wad")) {
        strcpy(wad->display_name, "DOOM 1: The Ultimate DOOM");
    } else if (!strcasecmp(fn, "doom2.wad")) {
        strcpy(wad->display_name, "DOOM II: Hell on Earth");
    } else if (!strcasecmp(fn, "tnt.wad")) {
        strcpy(wad->display_name, "Final Doom: TNT - Evilution");
    } else if (!strcasecmp(fn, "plutonia.wad")) {
        strcpy(wad->display_name, "Final Doom: The Plutonia Experiment");
    } else if (!strcasecmp(fn, "doom1.wad")) {
        strcpy(wad->display_name, "DOOM: Shareware Episode 1");
    } else if (!strcasecmp(fn, "freedoom1.wad")) {
        strcpy(wad->display_name, "Freedoom: Phase 1");
    } else if (!strcasecmp(fn, "freedoom2.wad")) {
        strcpy(wad->display_name, "Freedoom: Phase 2");
    } else if (!strcasecmp(fn, "sigil.wad")) {
        strcpy(wad->display_name, "SIGIL (John Romero)");
    } else if (!strcasecmp(fn, "sigil_ii.wad") || !strcasecmp(fn, "sigil2.wad")) {
        strcpy(wad->display_name, "SIGIL II (John Romero)");
    } else if (!strcasecmp(fn, "hacx.wad")) {
        strcpy(wad->display_name, "HACX: Twitch 'n Kill");
    } else if (!strcasecmp(fn, "chex.wad")) {
        strcpy(wad->display_name, "Chex Quest");
    } else if (!strcasecmp(fn, "scythe.wad")) {
        strcpy(wad->display_name, "Scythe (Erik Alm)");
    } else if (!strcasecmp(fn, "scythe2.wad")) {
        strcpy(wad->display_name, "Scythe 2 (Erik Alm)");
    } else if (!strcasecmp(fn, "av.wad")) {
        strcpy(wad->display_name, "Alien Vendetta");
    } else if (!strcasecmp(fn, "hr.wad")) {
        strcpy(wad->display_name, "Hell Revealed");
    } else if (!strcasecmp(fn, "hr2final.wad") || !strcasecmp(fn, "hr2.wad")) {
        strcpy(wad->display_name, "Hell Revealed II");
    } else if (!strcasecmp(fn, "mm.wad")) {
        strcpy(wad->display_name, "Memento Mori");
    } else if (!strcasecmp(fn, "mm2.wad")) {
        strcpy(wad->display_name, "Memento Mori II");
    } else if (!strcasecmp(fn, "1klinecp.wad")) {
        strcpy(wad->display_name, "1000 Lines Community Project");
    } else if (!strcasecmp(fn, "pl2.wad")) {
        strcpy(wad->display_name, "Plutonia 2");
    } else if (!strcasecmp(fn, "tntr.wad")) {
        strcpy(wad->display_name, "TNT: Revilution");
    } else {
        // Strip .wad extension and clean underscores
        char buf[64];
        strncpy(buf, fn, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *dot = strrchr(buf, '.');
        if (dot) *dot = '\0';

        for (int i = 0; buf[i]; i++) {
            if (buf[i] == '_' || buf[i] == '-') buf[i] = ' ';
        }
        strncpy(wad->display_name, buf, sizeof(wad->display_name) - 1);
        wad->display_name[sizeof(wad->display_name) - 1] = '\0';
    }
}

static void WP_ScanDirectory(const char *dir_path, int depth)
{
    if (depth > 2 || s_wad_count >= MAX_WADS) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (s_wad_count >= MAX_WADS) break;
        if (entry->d_name[0] == '.') continue;

        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // Don't recurse into subdirectories
            continue;
        } else if (S_ISREG(st.st_mode)) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && !strcasecmp(ext, ".wad")) {
                // Ignore prboom-plus.wad system wad
                if (!strcasecmp(entry->d_name, "prboom-plus.wad")) continue;

                // Check for duplicates by filename (case-insensitive)
                bool dup = false;
                for (int i = 0; i < s_wad_count; i++) {
                    if (!strcasecmp(s_wads[i].filename, entry->d_name)) {
                        dup = true;
                        break;
                    }
                }
                if (dup) continue;

                // Check IWAD vs PWAD header
                FILE *fp = fopen(full_path, "rb");
                if (!fp) continue;

                char magic[5] = {0};
                size_t read_bytes = fread(magic, 1, 4, fp);
                fclose(fp);

                if (read_bytes < 4) continue;
                bool is_iwad = (strncmp(magic, "IWAD", 4) == 0);
                bool is_pwad = (strncmp(magic, "PWAD", 4) == 0);

                if (!is_iwad && !is_pwad) continue;

                wad_info_t *w = &s_wads[s_wad_count++];
                strncpy(w->path, full_path, sizeof(w->path) - 1);
                strncpy(w->filename, entry->d_name, sizeof(w->filename) - 1);
                w->is_iwad = is_iwad;
                w->file_size = st.st_size;
                WP_FormatDisplayName(w);
            }
        }
    }
    closedir(dir);
}

// Load or Save WAD Cache for instant startup
static bool WP_LoadCache(void)
{
    FILE *fp = fopen(WAD_CACHE_FILE, "rb");
    if (!fp) return false;

    cache_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    if (hdr.magic != CACHE_MAGIC || hdr.version != CACHE_VERSION || hdr.wad_count <= 0 || hdr.wad_count > MAX_WADS) {
        fclose(fp);
        return false;
    }

    if (fread(s_wads, sizeof(wad_info_t), hdr.wad_count, fp) != (size_t)hdr.wad_count) {
        fclose(fp);
        return false;
    }

    fclose(fp);
    s_wad_count = hdr.wad_count;
    return true;
}

static void WP_SaveCache(void)
{
    if (s_wad_count <= 0) return;
    FILE *fp = fopen(WAD_CACHE_FILE, "wb");
    if (!fp) return;

    cache_header_t hdr;
    hdr.magic = CACHE_MAGIC;
    hdr.version = CACHE_VERSION;
    hdr.wad_count = s_wad_count;

    fwrite(&hdr, sizeof(hdr), 1, fp);
    fwrite(s_wads, sizeof(wad_info_t), s_wad_count, fp);
    fclose(fp);
}

// Compare function: IWADs first, then alphabetical
static int WP_CompareWads(const void *a, const void *b)
{
    const wad_info_t *wa = (const wad_info_t *)a;
    const wad_info_t *wb = (const wad_info_t *)b;

    if (wa->is_iwad && !wb->is_iwad) return -1;
    if (!wa->is_iwad && wb->is_iwad) return 1;

    return strcasecmp(wa->display_name, wb->display_name);
}

void WP_RescanWads(void)
{
    s_wad_count = 0;
    unlink(WAD_CACHE_FILE);
    WP_ScanDirectory("sdmc:/3ds/Trinity3DS/wads", 0);
    if (s_wad_count == 0) {
        WP_ScanDirectory("/3ds/Trinity3DS/wads", 0);
    }
    if (s_wad_count > 0) {
        qsort(s_wads, s_wad_count, sizeof(wad_info_t), WP_CompareWads);
        WP_SaveCache();
    }
}

static void WP_RenderTopScreen(u8 *fb)
{
    // Background: Dark gunmetal gray
    WP_FillRect(fb, 400, 0, 0, 400, 240, 20, 20, 22);

    // Header banner: Deep Crimson
    WP_FillRect(fb, 400, 0, 0, 400, 32, 130, 20, 20);
    WP_FillRect(fb, 400, 0, 30, 400, 2, 210, 40, 40);
    WP_DrawText(fb, 400, 16, 8, "TRINITY3DS", 255, 255, 255, 2);
    WP_DrawText(fb, 400, 280, 12, "v0.9.6 WAD MANAGER", 255, 215, 0, 1);

    if (s_wad_count == 0) {
        WP_DrawText(fb, 400, 20, 100, "KEINE WAD-DATEIEN GEFUNDEN!", 255, 50, 50, 2);
        WP_DrawText(fb, 400, 20, 130, "Bitte kopiere .wad Dateien nach:", 200, 200, 200, 1);
        WP_DrawText(fb, 400, 20, 145, "/3ds/Trinity3DS/wads/ oder /3ds/Trinity3DS/", 100, 220, 255, 1);
        return;
    }

    wad_info_t *cur = &s_wads[s_selected_idx];

    // Ensure cover image for current WAD is loaded
    WP_UpdateCurrentCover(s_selected_idx);

    // Card Box for current WAD
    WP_FillRect(fb, 400, 12, 40, 376, 136, 30, 30, 35);
    WP_DrawRect(fb, 400, 12, 40, 376, 136, 180, 150, 40);

    // Cover Art Box (Left: X=18, Y=46, W=100, H=124)
    int cover_x = 18;
    int cover_y = 46;
    int cover_w = 98;
    int cover_h = 124;

    WP_FillRect(fb, 400, cover_x, cover_y, cover_w, cover_h, 15, 15, 18);
    WP_DrawRect(fb, 400, cover_x, cover_y, cover_w, cover_h, 80, 80, 90);

    if (s_cur_cover && s_cur_cover->pixels) {
        WP_DrawCover(fb, 400, cover_x + 1, cover_y + 1, cover_w - 2, cover_h - 2);
    } else {
        // Fallback placeholder when no cover is present
        WP_DrawText(fb, 400, cover_x + 16, cover_y + 45, "KEIN", 120, 120, 130, 2);
        WP_DrawText(fb, 400, cover_x + 12, cover_y + 65, "COVER", 120, 120, 130, 2);
    }

    // Info Section (Right of Cover: X=126)
    int info_x = 126;

    // Title (Scale 2 if short, Scale 1 if long)
    if (strlen(cur->display_name) <= 20) {
        WP_DrawText(fb, 400, info_x, 48, cur->display_name, 255, 255, 255, 2);
    } else {
        WP_DrawText(fb, 400, info_x, 48, cur->display_name, 255, 255, 255, 1);
    }

    // Type Badge
    if (cur->is_iwad) {
        WP_FillRect(fb, 400, info_x, 68, 140, 16, 20, 120, 40);
        WP_DrawText(fb, 400, info_x + 4, 72, "[IWAD - VOLLVERSION]", 255, 255, 255, 1);
    } else {
        WP_FillRect(fb, 400, info_x, 68, 145, 16, 20, 70, 130);
        WP_DrawText(fb, 400, info_x + 4, 72, "[PWAD - MEGAWAD / MOD]", 180, 230, 255, 1);
    }

    // Size & Count
    char size_buf[64];
    float mb = (float)cur->file_size / (1024.0f * 1024.0f);
    snprintf(size_buf, sizeof(size_buf), "GROESSE: %.2f MB  (#%d/%d)", mb, s_selected_idx + 1, s_wad_count);
    WP_DrawText(fb, 400, info_x, 92, size_buf, 200, 200, 200, 1);

    // Path
    char path_buf[128];
    snprintf(path_buf, sizeof(path_buf), "DATEI: %s", cur->filename);
    WP_DrawText(fb, 400, info_x, 110, path_buf, 160, 210, 255, 1);

    // Status hint
    if (cur->is_iwad) {
        WP_DrawText(fb, 400, info_x, 134, "* EIGENSTAENDIGES SPIEL", 100, 255, 120, 1);
        WP_DrawText(fb, 400, info_x, 148, "* STARTET DIREKT", 100, 255, 120, 1);
    } else {
        WP_DrawText(fb, 400, info_x, 134, "* MEGAWAD / COMMUNITY MOD", 255, 200, 80, 1);
        WP_DrawText(fb, 400, info_x, 148, "* LAEUFT MIT DOOM II BASIS", 255, 200, 80, 1);
    }

    // Controls Help Box (Bottom)
    WP_FillRect(fb, 400, 12, 182, 376, 50, 24, 24, 28);
    WP_DrawRect(fb, 400, 12, 182, 376, 50, 70, 70, 80);
    WP_DrawText(fb, 400, 20, 189, "STEUERUNG:", 255, 215, 0, 1);
    WP_DrawText(fb, 400, 100, 189, "D-PAD: Scrollen   L / R: A-Z Buchstabensprung", 200, 200, 200, 1);
    WP_DrawText(fb, 400, 20, 209, "A/TOUCH: Starten   START: Zuletzt   Y: WAD Downloader", 100, 255, 120, 1);
}

static void WP_RenderBottomScreen(u8 *fb)
{
    // Background
    WP_FillRect(fb, 320, 0, 0, 320, 240, 16, 16, 18);

    // Touch list title
    WP_FillRect(fb, 320, 0, 0, 320, 24, 40, 40, 45);
    WP_DrawText(fb, 320, 8, 8, "WAEHLE EIN SPIEL ODER MEGAWAD:", 255, 215, 0, 1);

    // List of 5 items
    int visible_items = 5;
    int item_y_start = 28;
    int item_h = 32;

    for (int i = 0; i < visible_items; i++) {
        int idx = s_scroll_offset + i;
        int y = item_y_start + i * (item_h + 2);

        if (idx < s_wad_count) {
            wad_info_t *w = &s_wads[idx];
            bool sel = (idx == s_selected_idx);

            // Row background
            if (sel) {
                WP_FillRect(fb, 320, 6, y, 308, item_h, 70, 50, 15);
                WP_DrawRect(fb, 320, 6, y, 308, item_h, 255, 215, 0);
            } else {
                WP_FillRect(fb, 320, 6, y, 308, item_h, 28, 28, 32);
                WP_DrawRect(fb, 320, 6, y, 308, item_h, 50, 50, 55);
            }

            // Indicator
            if (sel) {
                WP_DrawText(fb, 320, 12, y + 11, ">", 255, 255, 50, 2);
            }

            // Type badge
            if (w->is_iwad) {
                WP_FillRect(fb, 320, 26, y + 8, 48, 16, 20, 120, 40);
                WP_DrawText(fb, 320, 30, y + 12, "IWAD", 255, 255, 255, 1);
            } else {
                WP_FillRect(fb, 320, 26, y + 8, 48, 16, 20, 70, 130);
                WP_DrawText(fb, 320, 30, y + 12, "PWAD", 180, 230, 255, 1);
            }

            // Name
            WP_DrawText(fb, 320, 80, y + 12, w->display_name, sel ? 255 : 220, sel ? 255 : 220, sel ? 100 : 220, 1);
        }
    }

    // Bottom Action Buttons
    // Left: [SPIEL STARTEN] (X=6, Y=202, W=194, H=34)
    WP_FillRect(fb, 320, 6, 202, 194, 34, 20, 130, 40);
    WP_DrawRect(fb, 320, 6, 202, 194, 34, 80, 255, 100);
    WP_DrawCenteredText(fb, 320, 6, 202, 194, 34, "SPIEL STARTEN", 255, 255, 255, 2);

    // Right: [[Y] STORE] (X=206, Y=202, W=108, H=34)
    WP_FillRect(fb, 320, 206, 202, 108, 34, 25, 60, 120);
    WP_DrawRect(fb, 320, 206, 202, 108, 34, 80, 160, 255);
    WP_DrawCenteredText(fb, 320, 206, 202, 108, 34, "[Y] STORE", 255, 255, 255, 1);
}

// Find base IWAD for PWADs
static const char* WP_FindBaseIWAD(void)
{
    // Look for doom2.wad first
    for (int i = 0; i < s_wad_count; i++) {
        if (!strcasecmp(s_wads[i].filename, "doom2.wad")) {
            return s_wads[i].path;
        }
    }
    // Look for doom.wad / doomu.wad
    for (int i = 0; i < s_wad_count; i++) {
        if (!strcasecmp(s_wads[i].filename, "doom.wad") || !strcasecmp(s_wads[i].filename, "doomu.wad")) {
            return s_wads[i].path;
        }
    }
    // Any IWAD available
    for (int i = 0; i < s_wad_count; i++) {
        if (s_wads[i].is_iwad) {
            return s_wads[i].path;
        }
    }
    return NULL;
}

bool WAD_Picker_Run(char *out_iwad, size_t iwad_len, char *out_pwad, size_t pwad_len)
{
    s_wad_count = 0;

    // Fast Path: Try loading pre-built cache first
    hidScanInput();
    bool force_rescan = (hidKeysHeld() & KEY_X); // Hold X on boot to force full re-scan

    if (!force_rescan && WP_LoadCache()) {
        // Cache loaded instantly!
    } else {
        // Show "Scanne WADs..." notification while scanning
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            WP_FillRect(top_fb, 400, 0, 0, 400, 240, 20, 20, 22);
            WP_FillRect(top_fb, 400, 0, 0, 400, 32, 130, 20, 20);
            WP_DrawText(top_fb, 400, 16, 8, "TRINITY3DS", 255, 255, 255, 2);
            WP_DrawText(top_fb, 400, 20, 100, "SCANNE WAD-DATEIEN...", 255, 215, 0, 2);
            WP_DrawText(top_fb, 400, 20, 130, "Bitte einen kurzen Moment Geduld...", 200, 200, 200, 1);
            gfxFlushBuffers();
            gfxSwapBuffers();
        }

        // Scan ONLY /3ds/Trinity3DS/wads/
        WP_ScanDirectory("sdmc:/3ds/Trinity3DS/wads", 0);
        if (s_wad_count == 0) {
            WP_ScanDirectory("/3ds/Trinity3DS/wads", 0);
        }

        if (s_wad_count > 0) {
            // Sort WADs
            qsort(s_wads, s_wad_count, sizeof(wad_info_t), WP_CompareWads);
            // Save cache for next instantaneous boot
            WP_SaveCache();
        }
    }

    if (s_wad_count == 0) {
        // Render informational notice if no WADs found
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            WP_FillRect(top_fb, 400, 0, 0, 400, 240, 20, 20, 22);
            WP_FillRect(top_fb, 400, 0, 0, 400, 32, 130, 20, 20);
            WP_DrawText(top_fb, 400, 16, 8, "TRINITY3DS WAD MANAGER", 255, 255, 255, 2);
            WP_DrawText(top_fb, 400, 20, 60, "KEINE WADS IM SCAN GEFUNDEN!", 255, 80, 80, 2);
            WP_DrawText(top_fb, 400, 20, 95, "Bitte kopiere WADs in einen dieser Ordner:", 200, 200, 200, 1);
            WP_DrawText(top_fb, 400, 20, 115, "* sdmc:/3ds/Trinity3DS/wads/", 100, 220, 255, 1);
            WP_DrawText(top_fb, 400, 20, 130, "* sdmc:/3ds/Trinity3DS/", 100, 220, 255, 1);
            WP_DrawText(top_fb, 400, 20, 160, "Druecke [A] um mit Standard-IWAD zu starten", 255, 215, 0, 1);
        }
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 16, 16, 18);
            WP_DrawText(bot_fb, 320, 20, 100, "Tippe hier oder druecke [A]...", 200, 200, 200, 1);
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();

        for (int timer = 0; timer < 180 && aptMainLoop(); timer++) {
            hidScanInput();
            if (hidKeysDown() & (KEY_A | KEY_START | KEY_TOUCH)) break;
            gspWaitForVBlank();
        }
        return false;
    }

    // Sort WADs
    qsort(s_wads, s_wad_count, sizeof(wad_info_t), WP_CompareWads);

    // Load last wad
    char last_path[256] = {0};
    FILE *fp = fopen(LAST_WAD_FILE, "r");
    if (fp) {
        if (fgets(last_path, sizeof(last_path), fp)) {
            char *nl = strchr(last_path, '\n');
            if (nl) *nl = '\0';
        }
        fclose(fp);
    }

    s_selected_idx = 0;
    if (last_path[0]) {
        for (int i = 0; i < s_wad_count; i++) {
            if (!strcmp(s_wads[i].path, last_path)) {
                s_selected_idx = i;
                break;
            }
        }
    }

    // Check if START is held for instant launch of last played WAD
    hidScanInput();
    if ((hidKeysHeld() & KEY_START) && last_path[0]) {
        wad_info_t *chosen = &s_wads[s_selected_idx];
        if (chosen->is_iwad) {
            strncpy(out_iwad, chosen->path, iwad_len - 1);
            if (out_pwad) out_pwad[0] = '\0';
        } else {
            const char *base = WP_FindBaseIWAD();
            if (base) strncpy(out_iwad, base, iwad_len - 1);
            if (out_pwad) strncpy(out_pwad, chosen->path, pwad_len - 1);
        }
        return true;
    }

    // Interactive Loop
    bool confirmed = false;
    WP_StartBGM();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        touchPosition touch;
        hidTouchRead(&touch);

        // Adjust scroll offset to keep selected in view
        if (s_selected_idx < s_scroll_offset) {
            s_scroll_offset = s_selected_idx;
        }
        if (s_selected_idx >= s_scroll_offset + 5) {
            s_scroll_offset = s_selected_idx - 4;
        }

        // Navigation
        if (kDown & (KEY_DUP | KEY_CPAD_UP)) {
            if (s_selected_idx > 0) s_selected_idx--;
        }
        if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) {
            if (s_selected_idx < s_wad_count - 1) s_selected_idx++;
        }
        if (kDown & (KEY_DLEFT | KEY_CPAD_LEFT)) {
            s_selected_idx -= 5;
            if (s_selected_idx < 0) s_selected_idx = 0;
        }
        if (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) {
            s_selected_idx += 5;
            if (s_selected_idx >= s_wad_count) s_selected_idx = s_wad_count - 1;
        }
        if (kDown & KEY_R) {
            // Jump to next letter (A -> B -> C ...)
            char cur_ch = toupper((unsigned char)s_wads[s_selected_idx].display_name[0]);
            int target = -1;
            for (int i = s_selected_idx + 1; i < s_wad_count; i++) {
                char next_ch = toupper((unsigned char)s_wads[i].display_name[0]);
                if (next_ch > cur_ch) {
                    target = i;
                    break;
                }
            }
            if (target >= 0) s_selected_idx = target;
        }
        if (kDown & KEY_L) {
            // Jump to previous letter
            char cur_ch = toupper((unsigned char)s_wads[s_selected_idx].display_name[0]);
            int cur_first = s_selected_idx;
            while (cur_first > 0 && toupper((unsigned char)s_wads[cur_first - 1].display_name[0]) == cur_ch) {
                cur_first--;
            }
            if (cur_first < s_selected_idx) {
                s_selected_idx = cur_first;
            } else if (cur_first > 0) {
                char prev_ch = toupper((unsigned char)s_wads[cur_first - 1].display_name[0]);
                int target = cur_first - 1;
                while (target > 0 && toupper((unsigned char)s_wads[target - 1].display_name[0]) == prev_ch) {
                    target--;
                }
                s_selected_idx = target;
            }
        }

        // Touch Input
        if (kDown & KEY_TOUCH) {
            // Check list rows (Y: 28 to 198)
            if (touch.py >= 28 && touch.py <= 198) {
                int clicked_row = (touch.py - 28) / 34;
                int clicked_idx = s_scroll_offset + clicked_row;
                if (clicked_idx < s_wad_count) {
                    if (s_selected_idx == clicked_idx) {
                        // Double tap to launch!
                        confirmed = true;
                    } else {
                        s_selected_idx = clicked_idx;
                    }
                }
            }
            // Check Start Button (X: 6 to 200, Y: 202 to 238)
            else if (touch.py >= 202 && touch.py <= 238 && touch.px >= 6 && touch.px <= 200) {
                confirmed = true;
            }
            // Check WAD Store Button (X: 206 to 314, Y: 202 to 238)
            else if (touch.py >= 202 && touch.py <= 238 && touch.px >= 206 && touch.px <= 314) {
                kDown |= KEY_Y;
            }
        }

        // Open WAD Downloader / Repository
        if (kDown & KEY_Y) {
            if (WAD_Downloader_Run()) {
                // If any WAD was downloaded, rescan and update
                WP_RescanWads();
                if (s_selected_idx >= s_wad_count) s_selected_idx = s_wad_count - 1;
                if (s_selected_idx < 0) s_selected_idx = 0;
            }
            continue;
        }

        // Button Confirm
        if (kDown & (KEY_A | KEY_START)) {
            confirmed = true;
        }

        if (confirmed) {
            break;
        }

        // Render Frame
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

        if (top_fb) WP_RenderTopScreen(top_fb);
        if (bot_fb) WP_RenderBottomScreen(bot_fb);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    WP_FreeCover();
    WP_StopBGM();
    if (!confirmed || s_wad_count == 0) {
        // User exited via Home menu without confirming
        I_SafeExit(0);
        return false;
    }

    wad_info_t *chosen = &s_wads[s_selected_idx];

    // Save choice for next time
    FILE *out_fp = fopen(LAST_WAD_FILE, "w");
    if (out_fp) {
        fprintf(out_fp, "%s\n", chosen->path);
        fclose(out_fp);
    }

    if (chosen->is_iwad) {
        strncpy(out_iwad, chosen->path, iwad_len - 1);
        if (out_pwad) out_pwad[0] = '\0';
    } else {
        // PWAD/Megawad requires an IWAD base
        const char *base = WP_FindBaseIWAD();
        if (base) {
            strncpy(out_iwad, base, iwad_len - 1);
        } else {
            // Fallback to chosen path if no IWAD found
            strncpy(out_iwad, chosen->path, iwad_len - 1);
        }
        if (out_pwad) {
            strncpy(out_pwad, chosen->path, pwad_len - 1);
        }
    }

    return true;
}

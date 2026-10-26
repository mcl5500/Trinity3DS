#include "wad_picker.h"
#include "wad_downloader.h"
#include "i_main.h"
#include "version.h"

void I_SetupTimidity(void);
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

    I_SetupTimidity();

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

void WP_DrawClippedText(u8 *fb, int width, int clip_x1, int clip_x2, int x, int y, const char *str, u8 r, u8 g, u8 b, int scale)
{
    if (!str || !str[0]) return;
    int cur_x = x;
    int spacing = (scale == 1) ? 6 : (scale * 6);
    int char_w = 5 * scale;

    while (*str) {
        if (*str == '\n') {
            y += (scale == 1) ? 9 : (scale * 9);
            cur_x = x;
        } else {
            if (cur_x >= clip_x2) break; // Past the right edge
            if (cur_x + char_w >= clip_x1) {
                char c = *str;
                if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
                if (c < 32 || c > 95) c = ' ';
                int idx = c - 32;

                for (int col = 0; col < 5; col++) {
                    int px = cur_x + col * scale;
                    for (int dx = 0; dx < scale; dx++) {
                        int final_x = px + dx;
                        if (final_x >= clip_x1 && final_x < clip_x2) {
                            u8 line = s_font_5x7[idx][col];
                            for (int row = 0; row < 7; row++) {
                                if (line & (1 << row)) {
                                    if (scale == 1) {
                                        WP_DrawPixel(fb, width, final_x, y + row, r, g, b);
                                    } else {
                                        for (int dy = 0; dy < scale; dy++) {
                                            WP_DrawPixel(fb, width, final_x, y + row * scale + dy, r, g, b);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
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

void WP_FreeCover(void)
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

static char s_last_filename[64] = "";
static int s_cached_meta_idx = -1;

void WP_InvalidateCoverCache(void)
{
    s_last_filename[0] = '\0';
    s_cached_meta_idx = -1;
    WP_FreeCover();
}

void WP_UpdateCoverByFilename(const char *target_filename)
{
    if (target_filename && s_last_filename[0] && !strcasecmp(s_last_filename, target_filename)) {
        return; // Already checked/loaded!
    }
    WP_FreeCover();
    if (!target_filename || !target_filename[0]) {
        s_last_filename[0] = '\0';
        return;
    }

    strncpy(s_last_filename, target_filename, sizeof(s_last_filename) - 1);
    s_last_filename[sizeof(s_last_filename) - 1] = '\0';

    char base_name[64];
    strncpy(base_name, target_filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char png_path[256];
    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);
    s_cur_cover = WP_LoadPNG(png_path);
    if (!s_cur_cover) {
        snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.PNG", base_name);
        s_cur_cover = WP_LoadPNG(png_path);
    }
    if (!s_cur_cover) {
        // Check lowercase
        char lower[64];
        strncpy(lower, base_name, sizeof(lower) - 1);
        lower[sizeof(lower) - 1] = '\0';
        for (int i = 0; lower[i]; i++) lower[i] = tolower((unsigned char)lower[i]);
        snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", lower);
        s_cur_cover = WP_LoadPNG(png_path);
    }
}

bool WP_HasCover(void)
{
    return (s_cur_cover && s_cur_cover->pixels);
}

static void WP_UpdateCurrentCover(int idx)
{
    if (idx == s_cur_cover_idx) return;
    s_cur_cover_idx = idx;
    if (idx < 0 || idx >= s_wad_count) {
        WP_FreeCover();
        return;
    }
    WP_UpdateCoverByFilename(s_wads[idx].filename);
}

// Draw scaled cover image with aspect ratio preservation (centered inside dest box)
void WP_DrawCover(u8 *fb, int fb_width, int dest_x, int dest_y, int dest_w, int dest_h)
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

static bool WP_LoadWadMetadata(const char *filename, char *out_author, size_t author_len, char *out_base, size_t base_len, char *out_desc, size_t desc_len)
{
    if (!filename || !filename[0]) return false;

    char base_name[64];
    strncpy(base_name, filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char meta_path[256];
    snprintf(meta_path, sizeof(meta_path), "sdmc:/3ds/Trinity3DS/wads/%s.txt", base_name);

    if (out_author && author_len > 0) out_author[0] = '\0';
    if (out_base && base_len > 0) out_base[0] = '\0';
    if (out_desc && desc_len > 0) out_desc[0] = '\0';

    FILE *mfp = fopen(meta_path, "r");
    if (mfp) {
        char line[512];
        bool in_desc = false;

        while (fgets(line, sizeof(line), mfp)) {
            char *nl = strchr(line, '\r');
            if (nl) *nl = '\0';
            nl = strchr(line, '\n');
            if (nl) *nl = '\0';

            if (!strncasecmp(line, "AUTOR:", 6) || !strncasecmp(line, "AUTHOR:", 7)) {
                char *val = strchr(line, ':') + 1;
                while (*val == ' ') val++;
                if (out_author && author_len > 0) {
                    strncpy(out_author, val, author_len - 1);
                    out_author[author_len - 1] = '\0';
                }
            } else if (!strncasecmp(line, "BASIS:", 6) || !strncasecmp(line, "BASE:", 5)) {
                char *val = strchr(line, ':') + 1;
                while (*val == ' ') val++;
                if (out_base && base_len > 0) {
                    strncpy(out_base, val, base_len - 1);
                    out_base[base_len - 1] = '\0';
                }
            } else if (!strncasecmp(line, "BESCHREIBUNG:", 13) || !strncasecmp(line, "DESCRIPTION:", 12)) {
                in_desc = true;
            } else {
                if (out_desc && desc_len > 0 && (in_desc || out_desc[0] == '\0')) {
                    if (out_desc[0] != '\0') strncat(out_desc, " ", desc_len - strlen(out_desc) - 1);
                    strncat(out_desc, line, desc_len - strlen(out_desc) - 1);
                }
            }
        }
        fclose(mfp);
        if (out_desc && out_desc[0] != '\0') return true;
    }

    // Auto-resolve from catalog if not present on SD card yet
    const repo_item_t *item = Downloader_FindCatalogItem(filename);
    if (item) {
        if (out_author && author_len > 0) strncpy(out_author, item->author[0] ? item->author : "Unknown", author_len - 1);
        if (out_base && base_len > 0) strncpy(out_base, item->base_iwad[0] ? item->base_iwad : "DOOM2.WAD", base_len - 1);
        if (out_desc && desc_len > 0) strncpy(out_desc, item->desc, desc_len - 1);

        // Auto-cache to .txt so subsequent loads are fast
        FILE *wfp = fopen(meta_path, "w");
        if (wfp) {
            fprintf(wfp, "AUTHOR: %s\nBASE: %s\nDESCRIPTION:\n%s\n",
                    item->author[0] ? item->author : "Unknown",
                    item->base_iwad[0] ? item->base_iwad : "DOOM2.WAD",
                    item->desc);
            fclose(wfp);
        }
        return true;
    }

    if (out_author && author_len > 0) strncpy(out_author, "Unknown", author_len - 1);
    if (out_base && base_len > 0) strncpy(out_base, "DOOM2.WAD", base_len - 1);
    if (out_desc && desc_len > 0) strncpy(out_desc, "No description available. Press [Y] to fetch metadata.", desc_len - 1);
    return false;
}

static void WP_RenderTopScreen(u8 *fb)
{
    // Background: Dark gunmetal gray
    WP_FillRect(fb, 400, 0, 0, 400, 240, 20, 20, 22);

    // Header banner: Deep Crimson
    WP_FillRect(fb, 400, 0, 0, 400, 32, 130, 20, 20);
    WP_FillRect(fb, 400, 0, 30, 400, 2, 210, 40, 40);
    WP_DrawText(fb, 400, 16, 8, "TRINITY3DS", 255, 255, 255, 2);
    WP_DrawText(fb, 400, 280, 12, TRINITY3DS_VERSION_STR " WAD MANAGER", 255, 215, 0, 1);

    if (s_wad_count == 0) {
        WP_DrawText(fb, 400, 20, 100, "NO WAD FILES FOUND!", 255, 50, 50, 2);
        WP_DrawText(fb, 400, 20, 130, "Please copy .wad files to:", 200, 200, 200, 1);
        WP_DrawText(fb, 400, 20, 145, "/3ds/Trinity3DS/wads/ or /3ds/Trinity3DS/", 100, 220, 255, 1);
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
        WP_DrawText(fb, 400, cover_x + 16, cover_y + 45, "NO", 120, 120, 130, 2);
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
        WP_DrawText(fb, 400, info_x + 4, 72, "[IWAD - FULL GAME]", 255, 255, 255, 1);
    } else {
        WP_FillRect(fb, 400, info_x, 68, 145, 16, 20, 70, 130);
        WP_DrawText(fb, 400, info_x + 4, 72, "[PWAD - MEGAWAD / MOD]", 180, 230, 255, 1);
    }

    // Size & Count
    char size_buf[64];
    float mb = (float)cur->file_size / (1024.0f * 1024.0f);
    snprintf(size_buf, sizeof(size_buf), "SIZE: %.2f MB  (#%d/%d)", mb, s_selected_idx + 1, s_wad_count);
    WP_DrawText(fb, 400, info_x, 92, size_buf, 200, 200, 200, 1);

    // Path
    char path_buf[128];
    snprintf(path_buf, sizeof(path_buf), "FILE: %s", cur->filename);
    WP_DrawText(fb, 400, info_x, 110, path_buf, 160, 210, 255, 1);

    // Metadata snippet (Author & Description preview) - cached by index to avoid 60 FPS SD thrashing
    static char s_cached_auth[64] = "";
    static char s_cached_base[32] = "";
    static char s_cached_desc[256] = "";
    static bool s_cached_has_meta = false;

    if (s_selected_idx != s_cached_meta_idx) {
        s_cached_meta_idx = s_selected_idx;
        s_cached_has_meta = WP_LoadWadMetadata(cur->filename, s_cached_auth, sizeof(s_cached_auth),
                                               s_cached_base, sizeof(s_cached_base),
                                               s_cached_desc, sizeof(s_cached_desc));
    }

    if (s_cached_has_meta && s_cached_auth[0] && strcasecmp(s_cached_auth, "Unknown") != 0 && strcasecmp(s_cached_auth, "Unbekannt") != 0) {
        char auth_buf[64];
        snprintf(auth_buf, sizeof(auth_buf), "AUTHOR: %s", s_cached_auth);
        WP_DrawText(fb, 400, info_x, 130, auth_buf, 255, 215, 0, 1);
    } else {
        WP_DrawText(fb, 400, info_x, 130, cur->is_iwad ? "* STANDALONE GAME" : "* MEGAWAD / MOD", 100, 255, 120, 1);
    }

    if (s_cached_has_meta && s_cached_desc[0]) {
        char short_desc[48];
        strncpy(short_desc, s_cached_desc, sizeof(short_desc) - 1);
        short_desc[sizeof(short_desc) - 1] = '\0';
        WP_DrawText(fb, 400, info_x, 146, short_desc, 200, 200, 210, 1);
    } else {
        WP_DrawText(fb, 400, info_x, 146, cur->is_iwad ? "* DIRECT LAUNCH" : "* RUNS WITH DOOM II BASE", 200, 200, 200, 1);
    }

    // Controls Help Box (Bottom)
    WP_FillRect(fb, 400, 12, 182, 376, 50, 24, 24, 28);
    WP_DrawRect(fb, 400, 12, 182, 376, 50, 70, 70, 80);
    WP_DrawText(fb, 400, 20, 189, "CONTROLS:", 255, 215, 0, 1);
    WP_DrawText(fb, 400, 100, 189, "D-PAD: Scroll   L / R: A-Z Jump", 200, 200, 200, 1);
    WP_DrawText(fb, 400, 20, 209, "A: Launch   X: Info & Options   Y: Store   START: Quick Resume", 100, 255, 120, 1);
}

static void WP_RenderBottomScreen(u8 *fb)
{
    // Background
    WP_FillRect(fb, 320, 0, 0, 320, 240, 16, 16, 18);

    // Touch list title
    WP_FillRect(fb, 320, 0, 0, 320, 24, 40, 40, 45);
    WP_DrawText(fb, 320, 8, 8, "SELECT GAME OR MEGAWAD:", 255, 215, 0, 1);

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

    // Bottom Action Buttons (3 Buttons: Launch, Info [X], Store [Y])
    // Left: [LAUNCH GAME] (X=6, Y=202, W=130, H=34)
    WP_FillRect(fb, 320, 6, 202, 130, 34, 20, 130, 40);
    WP_DrawRect(fb, 320, 6, 202, 130, 34, 80, 255, 100);
    WP_DrawCenteredText(fb, 320, 6, 202, 130, 34, "LAUNCH", 255, 255, 255, 2);

    // Center: [[X] OPTIONS] (X=142, Y=202, W=92, H=34)
    WP_FillRect(fb, 320, 142, 202, 92, 34, 110, 40, 20);
    WP_DrawRect(fb, 320, 142, 202, 92, 34, 230, 90, 50);
    WP_DrawCenteredText(fb, 320, 142, 202, 92, 34, "[X] INFO", 255, 255, 255, 1);

    // Right: [[Y] STORE] (X=240, Y=202, W=74, H=34)
    WP_FillRect(fb, 320, 240, 202, 74, 34, 25, 60, 120);
    WP_DrawRect(fb, 320, 240, 202, 74, 34, 80, 160, 255);
    WP_DrawCenteredText(fb, 320, 240, 202, 74, 34, "[Y] STORE", 255, 255, 255, 1);
}

// WAD Management Modal: Delete WAD or Show/Fetch Metadata
static bool WP_ManageWadModal(int wad_idx)
{
    if (wad_idx < 0 || wad_idx >= s_wad_count) return false;
    wad_info_t *wad = &s_wads[wad_idx];

    char base_name[64];
    strncpy(base_name, wad->filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char meta_author[64] = "Unknown";
    char meta_base[32] = "DOOM2.WAD";
    char meta_desc[1024] = "";

    WP_LoadWadMetadata(wad->filename, meta_author, sizeof(meta_author), meta_base, sizeof(meta_base), meta_desc, sizeof(meta_desc));

    char status_msg[128] = "Select an action below:";
    u8 status_r = 200, status_g = 220, status_b = 255;

    bool delete_requested = false;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        if (kDown & KEY_B) break;

        // Touch handling
        if (kDown & KEY_TOUCH) {
            // Button 1: [A] BESCHREIBUNG (X: 16, Y: 34, W: 138, H: 44)
            if (touch.py >= 34 && touch.py <= 78 && touch.px >= 16 && touch.px <= 154) {
                kDown |= KEY_A;
            }
            // Button 2: [Y] INFO ABRUFEN (X: 166, Y: 34, W: 138, H: 44)
            if (touch.py >= 34 && touch.py <= 78 && touch.px >= 166 && touch.px <= 304) {
                kDown |= KEY_Y;
            }
            // Button 3: [X] LOESCHEN (X: 16, Y: 86, W: 138, H: 44)
            if (touch.py >= 86 && touch.py <= 130 && touch.px >= 16 && touch.px <= 154) {
                kDown |= KEY_X;
            }
            // Button 4: [B] ZURUECK (X: 166, Y: 86, W: 138, H: 44)
            if (touch.py >= 86 && touch.py <= 130 && touch.px >= 166 && touch.px <= 304) {
                kDown |= KEY_B;
            }
        }

        // Action: [A] Show Full Description Screen
        if (kDown & KEY_A) {
            char size_str[32];
            float mb = (float)wad->file_size / (1024.0f * 1024.0f);
            snprintf(size_str, sizeof(size_str), "%.2f MB", mb);
            Downloader_ShowDescriptionCustom(wad->display_name, meta_author, meta_base, size_str, meta_desc);
            continue;
        }

        // Action: [Y] Fetch / Update Metadata & Cover
        if (kDown & KEY_Y) {
            // Render "Fetching..." message on bottom screen first
            u8 *bot_loading = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
            if (bot_loading) {
                WP_FillRect(bot_loading, 320, 16, 140, 288, 88, 25, 30, 45);
                WP_DrawRect(bot_loading, 320, 16, 140, 288, 88, 80, 160, 255);
                WP_DrawCenteredText(bot_loading, 320, 16, 155, 288, 20, "FETCHING METADATA...", 255, 255, 255, 1);
                WP_DrawCenteredText(bot_loading, 320, 16, 180, 288, 20, "Searching catalog & online...", 200, 220, 255, 1);
                gfxFlushBuffers();
                gfxSwapBuffers();
            }

            bool ok = Downloader_FetchMetadataForWad(wad->filename, meta_author, sizeof(meta_author), meta_base, sizeof(meta_base), meta_desc, sizeof(meta_desc));
            WP_UpdateCoverByFilename(wad->filename);
            s_cached_meta_idx = -1;

            if (ok) {
                snprintf(status_msg, sizeof(status_msg), "Metadata & cover updated!");
                status_r = 80; status_g = 255; status_b = 100;
            } else {
                snprintf(status_msg, sizeof(status_msg), "No new data found.");
                status_r = 255; status_g = 200; status_b = 80;
            }
            continue;
        }

        // Action: [X] Delete WAD
        if (kDown & KEY_X) {
            // Confirm deletion dialog
            bool confirm_del = false;
            while (aptMainLoop()) {
                hidScanInput();
                u32 subDown = hidKeysDown();
                touchPosition subTouch;
                hidTouchRead(&subTouch);

                if (subDown & KEY_B) break;
                if (subDown & KEY_A) { confirm_del = true; break; }

                if (subDown & KEY_TOUCH) {
                    if (subTouch.py >= 170 && subTouch.py <= 204 && subTouch.px >= 20 && subTouch.px <= 140) {
                        confirm_del = true;
                        break;
                    }
                    if (subTouch.py >= 170 && subTouch.py <= 204 && subTouch.px >= 180 && subTouch.px <= 300) {
                        break;
                    }
                }

                u8 *bot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
                if (bot) {
                    WP_FillRect(bot, 320, 0, 0, 320, 240, 15, 15, 18);
                    WP_FillRect(bot, 320, 10, 30, 300, 180, 45, 15, 15);
                    WP_DrawRect(bot, 320, 10, 30, 300, 180, 255, 60, 60);

                    WP_DrawCenteredText(bot, 320, 10, 45, 300, 20, "CONFIRM DELETE?", 255, 80, 80, 2);
                    WP_DrawCenteredText(bot, 320, 10, 75, 300, 16, wad->filename, 255, 255, 255, 1);
                    WP_DrawCenteredText(bot, 320, 10, 100, 300, 16, "File & cover will be permanently", 200, 200, 200, 1);
                    WP_DrawCenteredText(bot, 320, 10, 116, 300, 16, "deleted from SD card!", 200, 200, 200, 1);

                    // [JA, LOESCHEN]
                    WP_FillRect(bot, 320, 20, 170, 120, 34, 160, 20, 20);
                    WP_DrawRect(bot, 320, 20, 170, 120, 34, 255, 80, 80);
                    WP_DrawCenteredText(bot, 320, 20, 170, 120, 34, "[A] YES", 255, 255, 255, 2);

                    // [NEIN, ABBRUCH]
                    WP_FillRect(bot, 320, 180, 170, 120, 34, 40, 40, 45);
                    WP_DrawRect(bot, 320, 180, 170, 120, 34, 100, 100, 110);
                    WP_DrawCenteredText(bot, 320, 180, 170, 120, 34, "[B] NO", 255, 255, 255, 2);
                }
                gfxFlushBuffers();
                gfxSwapBuffers();
                gspWaitForVBlank();
            }

            if (confirm_del) {
                // Delete .wad file
                unlink(wad->path);
                // Delete companion png
                char png_del[256];
                snprintf(png_del, sizeof(png_del), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);
                unlink(png_del);
                snprintf(png_del, sizeof(png_del), "sdmc:/3ds/Trinity3DS/wads/%s.PNG", base_name);
                unlink(png_del);
                char meta_del[256];
                snprintf(meta_del, sizeof(meta_del), "sdmc:/3ds/Trinity3DS/wads/%s.txt", base_name);
                unlink(meta_del);

                delete_requested = true;
                break;
            }
        }

        // Render Frame
        u8 *top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top) {
            WP_FillRect(top, 400, 0, 0, 400, 240, 20, 20, 22);
            WP_FillRect(top, 400, 0, 0, 400, 30, 110, 30, 20);
            WP_DrawText(top, 400, 16, 8, "WAD INFO & OPTIONS", 255, 255, 255, 2);

            // Card Box: Y: 36, H: 84
            WP_FillRect(top, 400, 12, 36, 376, 84, 28, 28, 32);
            WP_DrawRect(top, 400, 12, 36, 376, 84, 180, 150, 40);

            // Cover Art Box (Left: X=16, Y=40, W=60, H=76)
            WP_FillRect(top, 400, 16, 40, 60, 76, 15, 15, 18);
            WP_DrawRect(top, 400, 16, 40, 60, 76, 70, 70, 80);
            if (s_cur_cover && s_cur_cover->pixels) {
                WP_DrawCover(top, 400, 17, 41, 58, 74);
            } else {
                WP_DrawText(top, 400, 26, 68, "COVER", 120, 120, 130, 1);
            }

            // Info Section (Right of cover: X=84)
            int info_x = 84;
            if (strlen(wad->display_name) <= 24) {
                WP_DrawText(top, 400, info_x, 42, wad->display_name, 255, 215, 0, 2);
            } else {
                WP_DrawText(top, 400, info_x, 44, wad->display_name, 255, 215, 0, 1);
            }

            char auth_buf[64];
            snprintf(auth_buf, sizeof(auth_buf), "AUTHOR: %s", meta_author);
            WP_DrawText(top, 400, info_x, 66, auth_buf, 255, 255, 255, 1);

            char sub_buf[96];
            float mb = (float)wad->file_size / (1024.0f * 1024.0f);
            snprintf(sub_buf, sizeof(sub_buf), "BASE: %s  |  %.2f MB", meta_base, mb);
            WP_DrawText(top, 400, info_x, 82, sub_buf, 180, 220, 255, 1);

            char file_buf[96];
            snprintf(file_buf, sizeof(file_buf), "FILE: %s", wad->filename);
            WP_DrawText(top, 400, info_x, 98, file_buf, 160, 180, 200, 1);

            // Description Viewport Box: Y: 124, H: 108
            WP_FillRect(top, 400, 12, 124, 376, 108, 24, 26, 32);
            WP_DrawRect(top, 400, 12, 124, 376, 108, 80, 140, 220);
            WP_DrawText(top, 400, 18, 128, "DESCRIPTION:", 255, 215, 0, 1);

            // Word wrap description up to 5 visible lines
            char d_lines[16][64];
            int d_count = Downloader_WordWrap(meta_desc, d_lines, 16, 56);
            for (int l = 0; l < 5 && l < d_count; l++) {
                WP_DrawText(top, 400, 20, 144 + l * 14, d_lines[l], 220, 225, 235, 1);
            }
            if (d_count > 5) {
                WP_DrawText(top, 400, 220, 216, "[A] READ FULLSCREEN...", 100, 255, 120, 1);
            }
        }

        u8 *bot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot) {
            WP_FillRect(bot, 320, 0, 0, 320, 240, 16, 16, 18);
            WP_FillRect(bot, 320, 0, 0, 320, 26, 45, 25, 20);
            WP_DrawText(bot, 320, 12, 8, "WAD MANAGEMENT & INFO:", 255, 215, 0, 1);

            // Row 1 Buttons (Y: 34, H: 44)
            // Button 1: [A] BESCHREIBUNG (Blau)
            WP_FillRect(bot, 320, 16, 34, 138, 44, 25, 60, 110);
            WP_DrawRect(bot, 320, 16, 34, 138, 44, 80, 170, 255);
            WP_DrawCenteredText(bot, 320, 16, 34, 138, 44, "[A] READ TEXT", 255, 255, 255, 1);

            // Button 2: [Y] METADATEN ABRUFEN (Gruen/Cyan)
            WP_FillRect(bot, 320, 166, 34, 138, 44, 20, 95, 80);
            WP_DrawRect(bot, 320, 166, 34, 138, 44, 50, 220, 180);
            WP_DrawCenteredText(bot, 320, 166, 34, 138, 44, "[Y] FETCH INFO", 255, 255, 255, 1);

            // Row 2 Buttons (Y: 86, H: 44)
            // Button 3: [X] LOESCHEN (Rot)
            WP_FillRect(bot, 320, 16, 86, 138, 44, 130, 25, 25);
            WP_DrawRect(bot, 320, 16, 86, 138, 44, 255, 70, 70);
            WP_DrawCenteredText(bot, 320, 16, 86, 138, 44, "[X] DELETE", 255, 255, 255, 1);

            // Button 4: [B] ZURUECK (Grau)
            WP_FillRect(bot, 320, 166, 86, 138, 44, 38, 38, 45);
            WP_DrawRect(bot, 320, 166, 86, 138, 44, 100, 100, 110);
            WP_DrawCenteredText(bot, 320, 166, 86, 138, 44, "[B] BACK", 255, 255, 255, 1);

            // Status Area (Y: 138 to 230)
            WP_FillRect(bot, 320, 16, 140, 288, 88, 24, 25, 30);
            WP_DrawRect(bot, 320, 16, 140, 288, 88, 60, 65, 80);

            WP_DrawCenteredText(bot, 320, 16, 148, 288, 16, status_msg, status_r, status_g, status_b, 1);

            WP_DrawText(bot, 320, 24, 172, "* [A]: Full description", 200, 200, 210, 1);
            WP_DrawText(bot, 320, 24, 188, "* [Y]: Fetch metadata & cover online", 100, 220, 255, 1);
            WP_DrawText(bot, 320, 24, 204, "* [X]: Delete WAD file from SD card", 255, 120, 120, 1);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    return delete_requested;
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
        // Show "Scanning WADs..." notification while scanning
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            WP_FillRect(top_fb, 400, 0, 0, 400, 240, 20, 20, 22);
            WP_FillRect(top_fb, 400, 0, 0, 400, 32, 130, 20, 20);
            WP_DrawText(top_fb, 400, 16, 8, "TRINITY3DS", 255, 255, 255, 2);
            WP_DrawText(top_fb, 400, 20, 100, "SCANNING WAD FILES...", 255, 215, 0, 2);
            WP_DrawText(top_fb, 400, 20, 130, "Please wait a moment...", 200, 200, 200, 1);
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
            WP_DrawText(top_fb, 400, 20, 60, "NO WADS FOUND IN SCAN!", 255, 80, 80, 2);
            WP_DrawText(top_fb, 400, 20, 95, "Please copy WADs into one of these folders:", 200, 200, 200, 1);
            WP_DrawText(top_fb, 400, 20, 115, "* sdmc:/3ds/Trinity3DS/wads/", 100, 220, 255, 1);
            WP_DrawText(top_fb, 400, 20, 130, "* sdmc:/3ds/Trinity3DS/", 100, 220, 255, 1);
            WP_DrawText(top_fb, 400, 20, 160, "Press [A] to launch with default IWAD", 255, 215, 0, 1);
        }
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 16, 16, 18);
            WP_DrawText(bot_fb, 320, 20, 100, "Tap here or press [A]...", 200, 200, 200, 1);
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
            // Check Start Button (X: 6 to 136, Y: 202 to 238)
            else if (touch.py >= 202 && touch.py <= 238 && touch.px >= 6 && touch.px <= 136) {
                confirmed = true;
            }
            // Check WAD Options Button (X: 142 to 234, Y: 202 to 238)
            else if (touch.py >= 202 && touch.py <= 238 && touch.px >= 142 && touch.px <= 234) {
                kDown |= KEY_X;
            }
            // Check WAD Store Button (X: 240 to 314, Y: 202 to 238)
            else if (touch.py >= 202 && touch.py <= 238 && touch.px >= 240 && touch.px <= 314) {
                kDown |= KEY_Y;
            }
        }

        // Open WAD Options / Management Dialog [X]
        if (kDown & KEY_X) {
            if (s_wad_count > 0 && s_selected_idx < s_wad_count) {
                if (WP_ManageWadModal(s_selected_idx)) {
                    // WAD was deleted, rescan and keep index valid
                    WP_RescanWads();
                    if (s_selected_idx >= s_wad_count) s_selected_idx = s_wad_count - 1;
                    if (s_selected_idx < 0) s_selected_idx = 0;
                }
            }
            continue;
        }

        // Open WAD Downloader / Repository [Y]
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

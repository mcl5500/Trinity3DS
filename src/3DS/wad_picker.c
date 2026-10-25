#include "wad_picker.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>

#define MAX_WADS 512
#define LAST_WAD_FILE "sdmc:/3ds/PrBoom-Plus/last_wad.txt"

typedef struct {
    char path[256];
    char filename[64];
    char display_name[64];
    bool is_iwad;
    long file_size;
} wad_info_t;

static wad_info_t s_wads[MAX_WADS];
static int s_wad_count = 0;
static int s_selected_idx = 0;
static int s_scroll_offset = 0;

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

static inline void WP_DrawPixel(u8 *fb, int width, int x, int y, u8 r, u8 g, u8 b)
{
    if (x < 0 || x >= width || y < 0 || y >= 240) return;
    int offset = (x * 240 + (239 - y)) * 3;
    fb[offset + 0] = b;
    fb[offset + 1] = g;
    fb[offset + 2] = r;
}

static void WP_FillRect(u8 *fb, int width, int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    for (int cy = y; cy < y + h; cy++) {
        for (int cx = x; cx < x + w; cx++) {
            WP_DrawPixel(fb, width, cx, cy, r, g, b);
        }
    }
}

static void WP_DrawRect(u8 *fb, int width, int x, int y, int w, int h, u8 r, u8 g, u8 b)
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

static void WP_DrawText(u8 *fb, int width, int x, int y, const char *str, u8 r, u8 g, u8 b, int scale)
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

// Clean up display names for common WADs
static void WP_FormatDisplayName(wad_info_t *wad)
{
    const char *fn = wad->filename;

    if (!strcasecmp(fn, "doom.wad") || !strcasecmp(fn, "doomu.wad")) {
        strcpy(wad->display_name, "The Ultimate DOOM");
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
            WP_ScanDirectory(full_path, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && !strcasecmp(ext, ".wad")) {
                // Ignore prboom-plus.wad system wad
                if (!strcasecmp(entry->d_name, "prboom-plus.wad")) continue;

                // Check for duplicates
                bool dup = false;
                for (int i = 0; i < s_wad_count; i++) {
                    if (!strcasecmp(s_wads[i].path, full_path)) {
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

// Compare function: IWADs first, then alphabetical
static int WP_CompareWads(const void *a, const void *b)
{
    const wad_info_t *wa = (const wad_info_t *)a;
    const wad_info_t *wb = (const wad_info_t *)b;

    if (wa->is_iwad && !wb->is_iwad) return -1;
    if (!wa->is_iwad && wb->is_iwad) return 1;

    return strcasecmp(wa->display_name, wb->display_name);
}

static void WP_RenderTopScreen(u8 *fb)
{
    // Background: Dark gunmetal gray
    WP_FillRect(fb, 400, 0, 0, 400, 240, 20, 20, 22);

    // Header banner: Deep Crimson
    WP_FillRect(fb, 400, 0, 0, 400, 32, 130, 20, 20);
    WP_FillRect(fb, 400, 0, 30, 400, 2, 210, 40, 40);
    WP_DrawText(fb, 400, 16, 8, "BOOM3DS DUALSCREEN", 255, 255, 255, 2);
    WP_DrawText(fb, 400, 280, 12, "v0.8.0 WAD BROWSER", 255, 215, 0, 1);

    if (s_wad_count == 0) {
        WP_DrawText(fb, 400, 20, 100, "KEINE WAD-DATEIEN GEFUNDEN!", 255, 50, 50, 2);
        WP_DrawText(fb, 400, 20, 130, "Bitte kopiere .wad Dateien nach:", 200, 200, 200, 1);
        WP_DrawText(fb, 400, 20, 145, "/3ds/PrBoom-Plus/ oder /3ds/doom/wads/", 100, 220, 255, 1);
        return;
    }

    wad_info_t *cur = &s_wads[s_selected_idx];

    // Card Box for current WAD
    WP_FillRect(fb, 400, 12, 42, 376, 128, 30, 30, 35);
    WP_DrawRect(fb, 400, 12, 42, 376, 128, 180, 150, 40);

    // Title
    WP_DrawText(fb, 400, 22, 52, cur->display_name, 255, 255, 255, 2);

    // Type Badge
    if (cur->is_iwad) {
        WP_FillRect(fb, 400, 22, 78, 140, 16, 20, 120, 40);
        WP_DrawText(fb, 400, 26, 82, "[IWAD - VOLLVERSION]", 255, 255, 255, 1);
    } else {
        WP_FillRect(fb, 400, 22, 78, 145, 16, 20, 70, 130);
        WP_DrawText(fb, 400, 26, 82, "[PWAD - MEGAWAD / MOD]", 180, 230, 255, 1);
    }

    // Size & Count
    char size_buf[64];
    float mb = (float)cur->file_size / (1024.0f * 1024.0f);
    snprintf(size_buf, sizeof(size_buf), "GROESSE: %.2f MB   (WAD %d VON %d)", mb, s_selected_idx + 1, s_wad_count);
    WP_DrawText(fb, 400, 22, 104, size_buf, 200, 200, 200, 1);

    // Path
    char path_buf[128];
    snprintf(path_buf, sizeof(path_buf), "PFAD: %s", cur->path);
    WP_DrawText(fb, 400, 22, 122, path_buf, 150, 150, 160, 1);

    // Status hint
    if (cur->is_iwad) {
        WP_DrawText(fb, 400, 22, 142, "* EIGENSTAENDIGES SPIEL - STARTET DIREKT", 100, 255, 120, 1);
    } else {
        WP_DrawText(fb, 400, 22, 142, "* MEGAWAD - LAEUFT MIT DOOM II ALS BASIS", 255, 200, 80, 1);
    }

    // Controls Help Box
    WP_FillRect(fb, 400, 12, 180, 376, 52, 24, 24, 28);
    WP_DrawRect(fb, 400, 12, 180, 376, 52, 70, 70, 80);
    WP_DrawText(fb, 400, 20, 188, "STEUERUNG:", 255, 215, 0, 1);
    WP_DrawText(fb, 400, 100, 188, "D-PAD / CPAD: Navigieren   L / R: Seite blaettern", 200, 200, 200, 1);
    WP_DrawText(fb, 400, 20, 208, "A / TOUCH : Spiel starten   START : Zuletzt gespielt", 100, 255, 120, 1);
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

    // Big Action Button at bottom
    WP_FillRect(fb, 320, 6, 202, 308, 34, 20, 130, 40);
    WP_DrawRect(fb, 320, 6, 202, 308, 34, 80, 255, 100);
    WP_DrawText(fb, 320, 75, 212, "▶  SPIEL STARTEN", 255, 255, 255, 2);
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

    // Scan standard directories (both with and without sdmc: prefix)
    WP_ScanDirectory("/3ds/PrBoom-Plus", 0);
    WP_ScanDirectory("sdmc:/3ds/PrBoom-Plus", 0);
    WP_ScanDirectory("/3ds/doom", 0);
    WP_ScanDirectory("sdmc:/3ds/doom", 0);
    WP_ScanDirectory("/3ds/doom/wads", 0);
    WP_ScanDirectory("sdmc:/3ds/doom/wads", 0);
    WP_ScanDirectory("/roms/doom", 0);
    WP_ScanDirectory("sdmc:/roms/doom", 0);
    WP_ScanDirectory(".", 0);

    if (s_wad_count == 0) {
        // Render informational notice if no WADs found
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            WP_FillRect(top_fb, 400, 0, 0, 400, 240, 20, 20, 22);
            WP_FillRect(top_fb, 400, 0, 0, 400, 32, 130, 20, 20);
            WP_DrawText(top_fb, 400, 16, 8, "BOOM3DS WAD BROWSER", 255, 255, 255, 2);
            WP_DrawText(top_fb, 400, 20, 60, "KEINE WADS IM SCAN GEFUNDEN!", 255, 80, 80, 2);
            WP_DrawText(top_fb, 400, 20, 95, "Bitte kopiere WADs in einen dieser Ordner:", 200, 200, 200, 1);
            WP_DrawText(top_fb, 400, 20, 115, "* sdmc:/3ds/PrBoom-Plus/", 100, 220, 255, 1);
            WP_DrawText(top_fb, 400, 20, 130, "* sdmc:/3ds/doom/wads/", 100, 220, 255, 1);
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
        if (kDown & (KEY_L | KEY_DLEFT)) {
            s_selected_idx -= 5;
            if (s_selected_idx < 0) s_selected_idx = 0;
        }
        if (kDown & (KEY_R | KEY_DRIGHT)) {
            s_selected_idx += 5;
            if (s_selected_idx >= s_wad_count) s_selected_idx = s_wad_count - 1;
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
            // Check Start Button (Y: 202 to 238)
            else if (touch.py >= 202 && touch.py <= 238) {
                confirmed = true;
            }
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

    if (s_wad_count == 0) return false;

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

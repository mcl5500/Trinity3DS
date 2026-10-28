#include "ds_screen.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_state.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_tick.h"
#include "p_mobj.h"
#include "p_map.h"
#include "p_spec.h"
#include "trinity_version.h"
#include "g_game.h"
#include "s_sound.h"
#include "sounds.h"
#include "i_system.h"
#include "i_main.h"

void I_ResetToLauncher(void);
#include "dsda/mapinfo.h"
const char *G_GetCurrentMap(void) {
    return dsda_MapLumpName(gameepisode, gamemap);
}
char *dsda_SaveGameName(int slot, dboolean temp);
void doom_printf(const char *s, ...);

#include <3ds.h>
#include <3ds/applets/swkbd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

int ds_touch_weapon = wp_nochange;
int ds_touch_artifact = 0;

static int ds_map_zoom = 2; // Default 3x tactical zoom (much larger & clearer)
static int ds_save_flash = 0;
static int ds_load_flash = 0;

// Cached 256-color palette (768 bytes: R, G, B)
static u8 ds_palette[768];
static bool ds_pal_loaded = false;

// 5x7 mini font for stats overlay
static const unsigned char mini_font[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space (32)
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x14, 0x08, 0x3E, 0x08, 0x14, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0 (48)
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x08, 0x14, 0x22, 0x41, 0x00, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x00, 0x41, 0x22, 0x14, 0x08, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A (65)
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x09, 0x01, // F
    0x3E, 0x41, 0x49, 0x49, 0x7A, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x07, 0x08, 0x70, 0x08, 0x07, // Y
    0x61, 0x51, 0x49, 0x45, 0x43  // Z (90)
};

static inline void DS_DrawPixel(u8 *fb, int x, int y, u8 r, u8 g, u8 b)
{
    if (x < 0 || x >= 320 || y < 0 || y >= 240) return;
    int offset = (x * 240 + (239 - y)) * 3;
    fb[offset + 0] = b;
    fb[offset + 1] = g;
    fb[offset + 2] = r;
}

static void DS_FillRect(u8 *fb, int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    for (int cy = y; cy < y + h; cy++) {
        for (int cx = x; cx < x + w; cx++) {
            DS_DrawPixel(fb, cx, cy, r, g, b);
        }
    }
}

static void DS_DrawRect(u8 *fb, int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    for (int cx = x; cx < x + w; cx++) {
        DS_DrawPixel(fb, cx, y, r, g, b);
        DS_DrawPixel(fb, cx, y + h - 1, r, g, b);
    }
    for (int cy = y; cy < y + h; cy++) {
        DS_DrawPixel(fb, x, cy, r, g, b);
        DS_DrawPixel(fb, x + w - 1, cy, r, g, b);
    }
}

void DS_QuickSave(void)
{
    if (gamestate != GS_LEVEL) return;
    if (players[consoleplayer].playerstate == PST_DEAD) {
        S_StartSound(NULL, sfx_oof);
        doom_printf("CAN'T SAVE WHILE DEAD!");
        return;
    }

    char desc[24];
    snprintf(desc, sizeof(desc), "QUICKSAVE %s", G_GetCurrentMap());
    G_SaveGame(0, desc);
    S_StartSound(NULL, sfx_swtchx);
    doom_printf("GAME SAVED (QUICKSAVE)");
    ds_save_flash = 12;
}

void DS_QuickLoad(void)
{
    if (gamestate != GS_LEVEL) return;

    char *path = dsda_SaveGameName(0, false);
    if (path && access(path, R_OK) == 0) {
        G_LoadGame(0, false);
        S_StartSound(NULL, sfx_swtchx);
        doom_printf("GAME LOADED (QUICKSAVE)");
        ds_load_flash = 12;
    } else {
        S_StartSound(NULL, sfx_oof);
        doom_printf("NO QUICKSAVE FOUND!");
        ds_load_flash = -12;
    }
    if (path) Z_Free(path);
}

// Draw a line with configurable thickness (1 or 2 pixels)
static void DS_DrawLine(u8 *fb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b, int min_y, int max_y, int thick)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        if (y0 >= min_y && y0 <= max_y && x0 >= 2 && x0 < 266) {
            DS_DrawPixel(fb, x0, y0, r, g, b);
            if (thick == 2) {
                if (x0 + 1 < 266) DS_DrawPixel(fb, x0 + 1, y0, r, g, b);
                if (y0 + 1 <= max_y) DS_DrawPixel(fb, x0, y0 + 1, r, g, b);
            }
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void DS_DrawChar(u8 *fb, int x, int y, char c, u8 r, u8 g, u8 b, int scale)
{
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c < 32 || c > 90) c = ' ';

    int idx = (c - 32) * 5;
    for (int col = 0; col < 5; col++) {
        unsigned char bits = mini_font[idx + col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                if (scale == 1) {
                    DS_DrawPixel(fb, x + col, y + row, r, g, b);
                } else {
                    DS_FillRect(fb, x + col * scale, y + row * scale, scale, scale, r, g, b);
                }
            }
        }
    }
}

static void DS_DrawString(u8 *fb, int x, int y, const char *str, u8 r, u8 g, u8 b, int scale)
{
    int cx = x;
    while (*str) {
        DS_DrawChar(fb, cx, y, *str, r, g, b, scale);
        cx += (5 + 1) * scale;
        str++;
    }
}

// Load the 256-color palette from PLAYPAL
static void DS_LoadPalette(void)
{
    if (ds_pal_loaded) return;
    int lump = W_CheckNumForName("PLAYPAL");
    if (lump >= 0) {
        const u8 *data = (const u8 *)W_LockLumpNum(lump);
        if (data) {
            memcpy(ds_palette, data, 768);
            ds_pal_loaded = true;
        }
    }
}

// Draw an authentic patch lump directly into the bottom framebuffer
static void DS_DrawLumpPatch(u8 *fb, int dest_x, int dest_y, int lump)
{
    if (lump < 0) return;
    if (!ds_pal_loaded) DS_LoadPalette();
    if (!ds_pal_loaded) return;

    const u8 *data = (const u8 *)W_LockLumpNum(lump);
    if (!data) return;

    short w = *(const short *)(data + 0);
    short h = *(const short *)(data + 2);
    short leftoffset = *(const short *)(data + 4);
    short topoffset = *(const short *)(data + 6);
    const int *columnofs = (const int *)(data + 8);

    dest_x -= leftoffset;
    dest_y -= topoffset;

    for (int col = 0; col < w; col++) {
        int sx = dest_x + col;
        if (sx < 0 || sx >= 320) continue;

        int ofs = columnofs[col];
        const u8 *post = data + ofs;
        while (1) {
            u8 topdelta = *post++;
            if (topdelta == 0xFF) break;
            u8 length = *post++;
            post++; // dummy byte
            int sy = dest_y + topdelta;
            for (int row = 0; row < length; row++) {
                int py = sy + row;
                u8 pal_idx = *post++;
                if (py >= 0 && py < 240) {
                    u8 r = ds_palette[pal_idx * 3 + 0];
                    u8 g = ds_palette[pal_idx * 3 + 1];
                    u8 b = ds_palette[pal_idx * 3 + 2];
                    DS_DrawPixel(fb, sx, py, r, g, b);
                }
            }
            post++; // dummy byte
        }
    }
}

// Draw a patch centered inside a bounding box (ignores internal sprite offsets)
static void DS_DrawPatchCentered(u8 *fb, int box_x, int box_y, int box_w, int box_h, int lump)
{
    if (lump < 0) return;
    if (!ds_pal_loaded) DS_LoadPalette();
    if (!ds_pal_loaded) return;

    const u8 *data = (const u8 *)W_LockLumpNum(lump);
    if (!data) return;

    short w = *(const short *)(data + 0);
    short h = *(const short *)(data + 2);
    if (w <= 0 || h <= 0 || w > 320 || h > 240) return;

    const int *columnofs = (const int *)(data + 8);

    int dest_x = box_x + (box_w - w) / 2;
    int dest_y = box_y + (box_h - h) / 2;

    for (int col = 0; col < w; col++) {
        int sx = dest_x + col;
        if (sx < box_x || sx >= box_x + box_w || sx < 0 || sx >= 320) continue;

        int ofs = columnofs[col];
        const u8 *post = data + ofs;
        while (1) {
            u8 topdelta = *post++;
            if (topdelta == 0xFF) break;
            u8 length = *post++;
            post++; // dummy byte
            int sy = dest_y + topdelta;
            for (int row = 0; row < length; row++) {
                int py = sy + row;
                u8 pal_idx = *post++;
                if (py >= box_y && py < box_y + box_h && py >= 0 && py < 240) {
                    u8 r = ds_palette[pal_idx * 3 + 0];
                    u8 g = ds_palette[pal_idx * 3 + 1];
                    u8 b = ds_palette[pal_idx * 3 + 2];
                    DS_DrawPixel(fb, sx, py, r, g, b);
                }
            }
            post++; // dummy byte
        }
    }
}

// Draw an authentic patch by name
static void DS_DrawWadPatch(u8 *fb, int dest_x, int dest_y, const char *patch_name)
{
    int lump = W_CheckNumForName(patch_name);
    if (lump >= 0) {
        DS_DrawLumpPatch(fb, dest_x, dest_y, lump);
    }
}

// Draw a sub-region (slice of columns) of a patch directly into the framebuffer
static void DS_DrawWadPatchSub(u8 *fb, int dest_x, int dest_y, const char *patch_name, int src_col_start, int num_cols)
{
    if (!ds_pal_loaded) DS_LoadPalette();
    if (!ds_pal_loaded) return;

    int lump = W_CheckNumForName(patch_name);
    if (lump < 0) return;

    const u8 *data = (const u8 *)W_LockLumpNum(lump);
    if (!data) return;

    short w = *(const short *)(data + 0);
    const int *columnofs = (const int *)(data + 8);

    for (int i = 0; i < num_cols; i++) {
        int col = src_col_start + i;
        if (col < 0 || col >= w) continue;
        int sx = dest_x + i;
        if (sx < 0 || sx >= 320) continue;

        int ofs = columnofs[col];
        const u8 *post = data + ofs;
        while (1) {
            u8 topdelta = *post++;
            if (topdelta == 0xFF) break;
            u8 length = *post++;
            post++; // dummy byte
            int sy = dest_y + topdelta;
            for (int row = 0; row < length; row++) {
                int py = sy + row;
                u8 pal_idx = *post++;
                if (py >= 0 && py < 240) {
                    u8 r = ds_palette[pal_idx * 3 + 0];
                    u8 g = ds_palette[pal_idx * 3 + 1];
                    u8 b = ds_palette[pal_idx * 3 + 2];
                    DS_DrawPixel(fb, sx, py, r, g, b);
                }
            }
            post++; // dummy byte
        }
    }
}

// Draw an authentic Doom patch with configurable brightness percentage (for dimmed unowned weapons)
static void DS_DrawWadPatchDimmed(u8 *fb, int dest_x, int dest_y, const char *patch_name, int brightness_percent)
{
    if (!ds_pal_loaded) DS_LoadPalette();
    if (!ds_pal_loaded) return;

    int lump = W_CheckNumForName(patch_name);
    if (lump < 0) return;

    const u8 *data = (const u8 *)W_LockLumpNum(lump);
    if (!data) return;

    short w = *(const short *)(data + 0);
    short leftoffset = *(const short *)(data + 4);
    short topoffset = *(const short *)(data + 6);
    const int *columnofs = (const int *)(data + 8);

    dest_x -= leftoffset;
    dest_y -= topoffset;

    for (int col = 0; col < w; col++) {
        int sx = dest_x + col;
        if (sx < 0 || sx >= 320) continue;

        int ofs = columnofs[col];
        const u8 *post = data + ofs;
        while (1) {
            u8 topdelta = *post++;
            if (topdelta == 0xFF) break;
            u8 length = *post++;
            post++; // dummy byte
            int sy = dest_y + topdelta;
            for (int row = 0; row < length; row++) {
                int py = sy + row;
                u8 pal_idx = *post++;
                if (py >= 0 && py < 240) {
                    u8 r = (ds_palette[pal_idx * 3 + 0] * brightness_percent) / 100;
                    u8 g = (ds_palette[pal_idx * 3 + 1] * brightness_percent) / 100;
                    u8 b = (ds_palette[pal_idx * 3 + 2] * brightness_percent) / 100;
                    DS_DrawPixel(fb, sx, py, r, g, b);
                }
            }
            post++; // dummy byte
        }
    }
}

// Draw authentic Doom numbers right-aligned
static void DS_DrawDoomNumber(u8 *fb, int right_x, int y, int val, int max_digits)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", val);
    int len = strlen(buf);

    int cur_x = right_x;
    for (int i = len - 1; i >= 0; i--) {
        char patch_name[9];
        snprintf(patch_name, sizeof(patch_name), "STTNUM%c", buf[i]);
        cur_x -= 14; // Standard STTNUM width
        DS_DrawWadPatch(fb, cur_x, y, patch_name);
    }
}

// Render the authentic Doom Status Bar at the bottom (Y: 208 to 239)
// Seamless gap-free design: Arms block removed (now beside automap).
// 100% authentic original Doom designations for AMMO, HEALTH, ARMOR, etc. are preserved!
static void DS_DrawAuthenticStatusBar(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    const int ST_Y = 208;

    // 1. Draw base STBAR background across the entire 320x32 width
    // Retains original Doom graphics for AMMO, HEALTH, FACE, ARMOR, KEYS, and AMMO TABLE!
    DS_DrawWadPatch(fb, 0, ST_Y, "STBAR");

    // 2. Seamlessly cover ONLY the "ARMS" label (Y: ST_Y + 23 to ST_Y + 28) in columns 104..142.
    // Columns 104..142 in STBAR are ALREADY 39 unique, natural, continuous Doom stone texture columns!
    // By copying the natural stone rows 12..17 from the exact same column over rows 23..28,
    // the label is gone and there is ZERO texture repetition across the entire status bar!
    for (int x = 104; x < 143; x++) {
        for (int r = 0; r < 6; r++) {
            int src_y = ST_Y + 12 + r;
            int dst_y = ST_Y + 23 + r;
            int src_ofs = (x * 240 + (239 - src_y)) * 3;
            int dst_ofs = (x * 240 + (239 - dst_y)) * 3;
            fb[dst_ofs + 0] = fb[src_ofs + 0];
            fb[dst_ofs + 1] = fb[src_ofs + 1];
            fb[dst_ofs + 2] = fb[src_ofs + 2];
        }
    }

    // 3. Ammo count (Right-aligned at original position X: 44, Y: ST_Y + 3)
    ammotype_t amtype = weaponinfo[plr->readyweapon].ammo;
    if (amtype != am_noammo) {
        DS_DrawDoomNumber(fb, 44, ST_Y + 3, plr->ammo[amtype], 3);
    }

    // 4. Health count & % symbol (Right-aligned at original position X: 90, Y: ST_Y + 3)
    DS_DrawDoomNumber(fb, 90, ST_Y + 3, plr->health, 3);
    DS_DrawWadPatch(fb, 90, ST_Y + 3, "STTPRCNT");

    // 5. Doomguy Face (Original position X: 143, Y: ST_Y)
    char face_name[9] = "STFST01";
    int hp = plr->health;
    int pain_stage = 0;
    if (hp <= 0) {
        strcpy(face_name, "STFDEAD0");
    } else {
        if (plr->cheats & CF_GODMODE) {
            strcpy(face_name, "STFGOD0");
        } else if (plr->damagecount > 0) {
            strcpy(face_name, "STFEVL0");
        } else {
            if (hp < 20) pain_stage = 4;
            else if (hp < 40) pain_stage = 3;
            else if (hp < 60) pain_stage = 2;
            else if (hp < 80) pain_stage = 1;
            else pain_stage = 0;

            static int face_timer = 0;
            face_timer++;
            int look = (face_timer / 35) % 3; // Look left, straight, right
            snprintf(face_name, sizeof(face_name), "STFST%d%d", pain_stage, look);
        }
    }
    DS_DrawWadPatch(fb, 143, ST_Y, face_name);

    // 6. Armor (Right-aligned at original position X: 221, Y: ST_Y + 3)
    DS_DrawDoomNumber(fb, 221, ST_Y + 3, plr->armorpoints[ARMOR_ARMOR], 3);
    DS_DrawWadPatch(fb, 221, ST_Y + 3, "STTPRCNT");

    // 7. Keycards / Skulls (Original position X: 239)
    if (plr->cards[it_blueskull]) DS_DrawWadPatch(fb, 239, ST_Y + 3, "STKEYS3");
    else if (plr->cards[it_bluecard]) DS_DrawWadPatch(fb, 239, ST_Y + 3, "STKEYS0");

    if (plr->cards[it_yellowskull]) DS_DrawWadPatch(fb, 239, ST_Y + 13, "STKEYS4");
    else if (plr->cards[it_yellowcard]) DS_DrawWadPatch(fb, 239, ST_Y + 13, "STKEYS1");

    if (plr->cards[it_redskull]) DS_DrawWadPatch(fb, 239, ST_Y + 23, "STKEYS5");
    else if (plr->cards[it_redcard]) DS_DrawWadPatch(fb, 239, ST_Y + 23, "STKEYS2");
}

// Draw a sleek 3D filled progress bar (Balken) with beveled border, recessed cavity, and fluid highlight
static void DS_DrawGaugeBar(u8 *fb, int x, int y, int w, int h, int cur, int max,
                            u8 r, u8 g, u8 b, u8 border_r, u8 border_g, u8 border_b)
{
    // 1. Dark recessed cavity
    DS_FillRect(fb, x, y, w, h, 14, 16, 20);

    // 2. Beveled outer border
    DS_DrawRect(fb, x - 1, y - 1, w + 2, h + 2, border_r, border_g, border_b);

    if (max <= 0) return;
    int clamped = cur < 0 ? 0 : (cur > max ? max : cur);
    int fill_w = (clamped * (w - 2)) / max;
    if (fill_w <= 0) return;

    int inner_x = x + 1;
    int inner_y = y + 1;
    int inner_h = h - 2;

    u8 hi_r = (r > 200) ? 255 : (r + 55);
    u8 hi_g = (g > 200) ? 255 : (g + 55);
    u8 hi_b = (b > 200) ? 255 : (b + 55);

    u8 sh_r = (r * 2) / 3;
    u8 sh_g = (g * 2) / 3;
    u8 sh_b = (b * 2) / 3;

    // Top highlight row (1-2px)
    DS_FillRect(fb, inner_x, inner_y, fill_w, 2, hi_r, hi_g, hi_b);

    // Main vibrant body
    if (inner_h > 4) {
        DS_FillRect(fb, inner_x, inner_y + 2, fill_w, inner_h - 4, r, g, b);
    }

    // Bottom shadow row (2px)
    if (inner_h > 2) {
        DS_FillRect(fb, inner_x, inner_y + inner_h - 2, fill_w, 2, sh_r, sh_g, sh_b);
    }

    // Right-edge tip cap
    if (fill_w < w - 2) {
        DS_FillRect(fb, inner_x + fill_w - 1, inner_y, 1, inner_h, hi_r, hi_g, hi_b);
    }
}

// Render the authentic Heretic Status Bar at the bottom (Y: 208 to 239)
static void DS_DrawHereticStatusBar(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    const int ST_Y = 208;

    // 1. Base wood/stone bar
    DS_DrawWadPatch(fb, 0, ST_Y, "BARBACK");

    // 2. Gargoyle faces: left (LTFACE) and right (RTFACE)
    DS_DrawWadPatch(fb, 0, ST_Y, "LTFACE");
    DS_DrawWadPatch(fb, 290, ST_Y, "RTFACE");
    if (plr->cheats & CF_GODMODE) {
        DS_DrawWadPatch(fb, 16, ST_Y + 9, "GOD1");
        DS_DrawWadPatch(fb, 287, ST_Y + 9, "GOD2");
    }

    // 3. Health Bar (Lebensbalken): X: 30 to 134
    int hp = plr->health;
    u8 hr = hp < 25 ? 255 : 255;
    u8 hg = hp < 25 ? 40 : 215;
    u8 hb = hp < 25 ? 40 : 0;
    u8 bar_r = hp < 25 ? 255 : 220;
    u8 bar_g = hp < 25 ? 30 : 35;
    u8 bar_b = hp < 25 ? 30 : 45;

    DS_DrawGaugeBar(fb, 30, ST_Y + 9, 72, 14, hp, 100,
                    bar_r, bar_g, bar_b, 75, 55, 40);

    char hp_str[16];
    snprintf(hp_str, sizeof(hp_str), "%d", hp);
    DS_DrawString(fb, 106, ST_Y + 7, hp_str, hr, hg, hb, 1);
    DS_DrawString(fb, 106, ST_Y + 18, "HP", 180, 150, 60, 1);

    // 4. Center Buffs (X: 134 to 156)
    if (plr->powers[pw_weaponlevel2]) {
        DS_DrawString(fb, 132, ST_Y + 6, "[TOME]", 255, 215, 40, 1);
    }
    if (plr->powers[pw_flight]) {
        DS_DrawString(fb, 132, ST_Y + 18, "[WINGS]", 120, 220, 255, 1);
    }

    // 5. Ammo Bar (Munitionsbalken - matches Health Bar completely): X: 160 to 264
    ammotype_t amtype = weaponinfo[plr->readyweapon].ammo;
    int cur_ammo = (amtype != am_noammo) ? plr->ammo[amtype] : 0;
    int max_am = (amtype != am_noammo) ? plr->maxammo[amtype] : 100;
    if (max_am <= 0) max_am = 100;

    if (amtype != am_noammo) {
        DS_DrawGaugeBar(fb, 160, ST_Y + 9, 72, 14, cur_ammo, max_am,
                        235, 175, 25, 75, 55, 40);
        char ammo_str[16];
        snprintf(ammo_str, sizeof(ammo_str), "%d", cur_ammo);
        DS_DrawString(fb, 236, ST_Y + 7, ammo_str, 255, 215, 0, 1);
        DS_DrawString(fb, 236, ST_Y + 18, "AMMO", 180, 150, 60, 1);
    } else {
        DS_DrawGaugeBar(fb, 160, ST_Y + 9, 72, 14, 0, 100,
                        80, 80, 80, 50, 50, 50);
        DS_DrawString(fb, 236, ST_Y + 12, "--", 140, 130, 110, 1);
    }

    // 6. Keys: Yellow (KEY1), Green (KEY2), Blue (KEY3)
    if (plr->cards[it_yellowcard]) DS_DrawWadPatch(fb, 272, ST_Y + 3, "KEY1");
    if (plr->cards[it_redcard])    DS_DrawWadPatch(fb, 272, ST_Y + 12, "KEY2");
    if (plr->cards[it_bluecard])   DS_DrawWadPatch(fb, 272, ST_Y + 21, "KEY3");
}

// Render the authentic Hexen Status Bar at the bottom (Y: 208 to 239)
static void DS_DrawHexenStatusBar(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    const int ST_Y = 208;

    // 1. Base stone bar
    DS_DrawWadPatch(fb, 0, ST_Y, "H2BAR");

    // 2. Class title & health (X: 8 to 108)
    int pclass = PlayerClass[consoleplayer];
    const char *c_title = "FIGHTER";
    if (pclass == 2) c_title = "CLERIC";
    else if (pclass == 3) c_title = "MAGE";

    DS_DrawString(fb, 8, ST_Y + 3, c_title, 255, 215, 0, 1);

    // Health Bar (Lebensbalken):
    int hp = plr->health;
    u8 bar_r = hp < 25 ? 255 : 220;
    u8 bar_g = hp < 25 ? 30 : 35;
    u8 bar_b = hp < 25 ? 30 : 45;
    DS_DrawGaugeBar(fb, 8, ST_Y + 15, 64, 13, hp, 100,
                    bar_r, bar_g, bar_b, 60, 65, 78);

    char hp_str[16];
    snprintf(hp_str, sizeof(hp_str), "%d", hp);
    u8 hr = hp < 25 ? 255 : 240;
    u8 hg = hp < 25 ? 60 : 220;
    u8 hb = hp < 25 ? 60 : 180;
    DS_DrawString(fb, 76, ST_Y + 16, hp_str, hr, hg, hb, 1);

    // 3. Armor & active powerups (X: 110 to 142)
    int tot_armor = (plr->armorpoints[ARMOR_ARMOR]) / 5;
    char arm_str[16];
    snprintf(arm_str, sizeof(arm_str), "ARM:%d", tot_armor);
    DS_DrawString(fb, 110, ST_Y + 4, arm_str, 200, 210, 225, 1);

    if (plr->powers[pw_flight]) {
        DS_DrawString(fb, 110, ST_Y + 16, "[FLY]", 120, 220, 255, 1);
    } else if (plr->powers[pw_speed]) {
        DS_DrawString(fb, 110, ST_Y + 16, "[BOOTS]", 255, 200, 50, 1);
    }

    // 4. Dual Interactive Mana Bars (X: 146 to 316)
    weapontype_t ready = plr->readyweapon;

    // --- Blue Mana Bar (X: 146 to 228) ---
    int m1 = plr->ammo[0];
    DS_DrawWadPatch(fb, 146, ST_Y + 5, m1 > 0 ? "MANABRT1" : "MANADIM1");

    bool b_active = (ready == wp_second || ready == wp_fourth);
    u8 b_border_r = b_active ? 255 : 40;
    u8 b_border_g = b_active ? 255 : 90;
    u8 b_border_b = b_active ? 255 : 180;

    DS_DrawGaugeBar(fb, 162, ST_Y + 9, 44, 13, m1, 200,
                    35, 130, 245, b_border_r, b_border_g, b_border_b);

    char m1_str[16];
    snprintf(m1_str, sizeof(m1_str), "%d", m1);
    DS_DrawString(fb, 210, ST_Y + 11, m1_str, 100, 200, 255, 1);

    // --- Green Mana Bar (X: 232 to 314) ---
    int m2 = plr->ammo[1];
    DS_DrawWadPatch(fb, 232, ST_Y + 5, m2 > 0 ? "MANABRT2" : "MANADIM2");

    bool g_active = (ready == wp_third || ready == wp_fourth);
    u8 g_border_r = g_active ? 255 : 35;
    u8 g_border_g = g_active ? 255 : 140;
    u8 g_border_b = g_active ? 255 : 60;

    DS_DrawGaugeBar(fb, 248, ST_Y + 9, 44, 13, m2, 200,
                    40, 200, 70, g_border_r, g_border_g, g_border_b);

    char m2_str[16];
    snprintf(m2_str, sizeof(m2_str), "%d", m2);
    DS_DrawString(fb, 296, ST_Y + 11, m2_str, 90, 255, 120, 1);
}

// Render the Wolfenstein-style Tactical Live-Automap (Y: 0 to 207, X: 0 to 267)
static void DS_DrawTacticalAutomap(u8 *fb)
{
    int max_map_y = raven ? 174 : 205;

    // 1. Pure Black Background (authentic Wolfenstein style)
    DS_FillRect(fb, 0, 0, 268, max_map_y + 3, 0, 0, 0);

    // 2. Crisp Outer Border (Dark Slate Gray)
    DS_FillRect(fb, 0, 0, 268, 2, 50, 55, 65);
    DS_FillRect(fb, 0, max_map_y + 1, 268, 2, 50, 55, 65);
    DS_FillRect(fb, 0, 0, 2, max_map_y + 3, 50, 55, 65);
    DS_FillRect(fb, 266, 0, 2, max_map_y + 3, 50, 55, 65);

    player_t *plr = &players[consoleplayer];
    if (!plr || !plr->mo) return;

    fixed_t px = plr->mo->x;
    fixed_t py = plr->mo->y;

    const int center_x = 134;
    const int center_y = raven ? 88 : 104;
    const int shift = FRACBITS + ds_map_zoom;

    // 3. Draw all visible map walls with authentic Wolfenstein styling
    for (int i = 0; i < numlines; i++) {
        line_t *ld = &lines[i];

        if (!(ld->flags & ML_MAPPED) && !(plr->powers[pw_allmap]))
            continue;
        if ((ld->flags & ML_DONTDRAW) && !(plr->powers[pw_allmap]))
            continue;

        int x0 = center_x + ((ld->v1->x - px) >> shift);
        int y0 = center_y - ((ld->v1->y - py) >> shift);
        int x1 = center_x + ((ld->v2->x - px) >> shift);
        int y1 = center_y - ((ld->v2->y - py) >> shift);

        // Bounding box test against 268x208
        if ((x0 < -60 && x1 < -60) || (x0 > 330 && x1 > 330) ||
            (y0 < -60 && y1 < -60) || (y0 > 260 && y1 > 260))
            continue;

        // Calculate distance from player to line midpoint
        fixed_t lmx = (ld->v1->x + ld->v2->x) >> 1;
        fixed_t lmy = (ld->v1->y + ld->v2->y) >> 1;
        int dx = (lmx - px) >> FRACBITS;
        int dy = (lmy - py) >> FRACBITS;
        bool is_nearby = (dx * dx + dy * dy) <= (550 * 550); // ~550 map units

        bool is_secret = (ld->flags & ML_SECRET) ||
                         (ld->frontsector && P_IsSecret(ld->frontsector)) ||
                         (ld->backsector && P_IsSecret(ld->backsector));
        bool is_discovered = (ld->special == 0 && ld->backsector && (ld->flags & ML_SECRET)) ||
                             (ld->frontsector && P_WasSecret(ld->frontsector) && !P_IsSecret(ld->frontsector)) ||
                             (ld->backsector && P_WasSecret(ld->backsector) && !P_IsSecret(ld->backsector));

        u8 r = 175, g = 180, b = 195; // Wolfenstein Solid Wall: Crisp Slate Gray
        int thick = 2;

        if (is_secret) {
            // Secret wall / door:
            // Highlighted distinctly when nearby, discovered, or with allmap powerup!
            if (is_nearby || is_discovered || plr->powers[pw_allmap]) {
                r = 235; g = 60; b = 255; // Distinct Bright Magenta/Purple Secret
                thick = 2;
            } else {
                // Far away: disguised as regular solid wall until player approaches
                r = 175; g = 180; b = 195;
                thick = 2;
            }
        } else if (ld->special == 11 || ld->special == 51 || ld->special == 52 ||
                   ld->special == 124 || ld->special == 197 || ld->special == 198) {
            // Exit switch / line: Light Green
            r = 50; g = 220; b = 80;
            thick = 2;
        } else if (ld->special) {
            // Interactive doors & switches
            if (ld->special == 26 || ld->special == 32 || ld->special == 99 || ld->special == 133) {
                r = 60; g = 140; b = 255;  // Blue key door
            } else if (ld->special == 27 || ld->special == 34 || ld->special == 135 || ld->special == 137) {
                r = 255; g = 215; b = 0;   // Yellow key door
            } else if (ld->special == 28 || ld->special == 33 || ld->special == 134 || ld->special == 136) {
                r = 255; g = 50; b = 50;   // Red key door
            } else {
                r = 70; g = 150; b = 255;  // Standard door: Light Blue
            }
            thick = 2;
        } else if (ld->backsector != NULL) {
            // 2-sided open ledge / window / floor step: Subtle Dark Slate
            r = 90; g = 95; b = 110;
            thick = 1;
        }

        DS_DrawLine(fb, x0, y0, x1, y1, r, g, b, 2, max_map_y, thick);
    }

    // 4. Draw active enemies & pickups ONLY when in visual range (Wolfenstein-style line of sight)
    if (thinkercap.next) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function == P_MobjThinker) {
                mobj_t *mo = (mobj_t *)th;
                if (!mo) continue;

                if (!plr->powers[pw_allmap] && !P_CheckSight(plr->mo, mo)) {
                    continue;
                }

                int mx = center_x + ((mo->x - px) >> shift);
                int my = center_y - ((mo->y - py) >> shift);

                if (mx >= 4 && mx < 264 && my >= 4 && my < max_map_y) {
                    // 1. Active Alive Enemy: Bright 3x3 Red Blip
                    if ((mo->flags & MF_COUNTKILL) && mo->health > 0) {
                        DS_FillRect(fb, mx - 1, my - 1, 3, 3, 240, 30, 30);
                    }
                    // 2. Pickups / Items: Bright 2x2 Yellow Blip
                    else if (mo->flags & MF_SPECIAL) {
                        DS_FillRect(fb, mx, my, 2, 2, 255, 220, 40);
                    }
                }
            }
        }
    }

    // 5. Draw Player Marker: Exact 8-direction Wolf4SDL Arrow (3x3 core + pointer nose)
    int dir = (int)(((unsigned int)(plr->mo->angle + (ANG45 / 2))) >> 29) & 7;

    // 3x3 pure white center core
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            DS_DrawPixel(fb, center_x + dx, center_y + dy, 255, 255, 255);
        }
    }

    // Pointer tip based on direction (0=East, 1=NE, 2=North, 3=NW, 4=West, 5=SW, 6=South, 7=SE)
    switch (dir) {
        case 0: // East (+X)
            DS_DrawPixel(fb, center_x + 2, center_y, 255, 255, 255);
            DS_DrawPixel(fb, center_x + 3, center_y, 255, 255, 255);
            break;
        case 1: // North-East (+X, -Y)
            DS_DrawPixel(fb, center_x + 2, center_y - 2, 255, 255, 255);
            break;
        case 2: // North (-Y)
            DS_DrawPixel(fb, center_x, center_y - 2, 255, 255, 255);
            DS_DrawPixel(fb, center_x, center_y - 3, 255, 255, 255);
            break;
        case 3: // North-West (-X, -Y)
            DS_DrawPixel(fb, center_x - 2, center_y - 2, 255, 255, 255);
            break;
        case 4: // West (-X)
            DS_DrawPixel(fb, center_x - 2, center_y, 255, 255, 255);
            DS_DrawPixel(fb, center_x - 3, center_y, 255, 255, 255);
            break;
        case 5: // South-West (-X, +Y)
            DS_DrawPixel(fb, center_x - 2, center_y + 2, 255, 255, 255);
            break;
        case 6: // South (+Y)
            DS_DrawPixel(fb, center_x, center_y + 2, 255, 255, 255);
            DS_DrawPixel(fb, center_x, center_y + 3, 255, 255, 255);
            break;
        case 7: // South-East (+X, +Y)
            DS_DrawPixel(fb, center_x + 2, center_y + 2, 255, 255, 255);
            break;
    }

    // 6. Tactical Header Bar (Kills/Items/Secrets stats + [RESET] + [SAVE] + [LOAD] + [ZOOM])
    DS_FillRect(fb, 0, 0, 268, 20, 18, 20, 26);
    DS_FillRect(fb, 0, 19, 268, 1, 40, 45, 55);

    // [RESET] Button (X: 2 to 44, width: 42, height: 16) - Soft-reset back to WAD Manager
    DS_FillRect(fb, 2, 2, 42, 16, 75, 20, 20);
    DS_DrawRect(fb, 2, 2, 42, 16, 220, 60, 60);
    DS_DrawString(fb, 6, 6, "WADS", 255, 200, 200, 1);

    // Stats string (compact format: K:12/34 S:1/3)
    char stats_buf[64];
    snprintf(stats_buf, sizeof(stats_buf), "K:%d/%d S:%d/%d",
             plr->killcount, totalkills, plr->secretcount, totalsecret);
    DS_DrawString(fb, 48, 6, stats_buf, 180, 210, 245, 1);

    // [SAVE] Button (X: 138 to 178, width: 40, height: 16)
    if (ds_save_flash > 0) {
        DS_FillRect(fb, 138, 2, 40, 16, 60, 220, 80);
        DS_DrawRect(fb, 138, 2, 40, 16, 255, 255, 255);
        DS_DrawString(fb, 142, 6, "SAVED", 10, 50, 10, 1);
        ds_save_flash--;
    } else {
        DS_FillRect(fb, 138, 2, 40, 16, 25, 65, 30);
        DS_DrawRect(fb, 138, 2, 40, 16, 45, 140, 55);
        DS_DrawString(fb, 146, 6, "SAVE", 160, 255, 170, 1);
    }

    // [LOAD] Button (X: 182 to 222, width: 40, height: 16)
    if (ds_load_flash > 0) {
        DS_FillRect(fb, 182, 2, 40, 16, 60, 160, 240);
        DS_DrawRect(fb, 182, 2, 40, 16, 255, 255, 255);
        DS_DrawString(fb, 184, 6, "LOADED", 10, 20, 60, 1);
        ds_load_flash--;
    } else if (ds_load_flash < 0) {
        DS_FillRect(fb, 182, 2, 40, 16, 180, 40, 40);
        DS_DrawRect(fb, 182, 2, 40, 16, 255, 100, 100);
        DS_DrawString(fb, 186, 6, "EMPTY", 255, 255, 255, 1);
        ds_load_flash++;
    } else {
        DS_FillRect(fb, 182, 2, 40, 16, 25, 45, 75);
        DS_DrawRect(fb, 182, 2, 40, 16, 50, 100, 170);
        DS_DrawString(fb, 188, 6, "LOAD", 170, 220, 255, 1);
    }

    // [ZOOM] Button (X: 226 to 264, width: 38, height: 16)
    DS_FillRect(fb, 226, 2, 38, 16, 35, 38, 46);
    DS_DrawRect(fb, 226, 2, 38, 16, 80, 90, 110);
    const char *zoom_str = "3x";
    if (ds_map_zoom == 1) zoom_str = "4x";
    else if (ds_map_zoom == 2) zoom_str = "3x";
    else if (ds_map_zoom == 3) zoom_str = "2x";
    else if (ds_map_zoom == 4) zoom_str = "1x";
    char zoom_buf[16];
    snprintf(zoom_buf, sizeof(zoom_buf), "Z:%s", zoom_str);
    DS_DrawString(fb, 230, 6, zoom_buf, 200, 210, 225, 1);
}

// Render the interactive 7-slot weapon selection panel on the right side next to automap (X: 268 to 319, Y: 0 to 207)
// Uses authentic Doom design: STBAR metal plate texture, authentic STYSNUM/STGNUM numbers from WAD (2x),
// Boom-style white highlight box for the active weapon, and authentic Doom colors (gold/gray). No ammo numbers.
static void DS_DrawWeaponSelector(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    // Divider border between map and weapon selector
    DS_FillRect(fb, 267, 0, 1, 208, 50, 55, 65);

    static const struct {
        int slot;
        weapontype_t wp;
        weapontype_t alt_wp;
        const char *name;
    } slots[7] = {
        { 1, wp_fist,     wp_chainsaw,     "FIST" },
        { 2, wp_pistol,   wp_nochange,     "PIST" },
        { 3, wp_shotgun,  wp_supershotgun, "SHOT" },
        { 4, wp_chaingun, wp_nochange,     "CHGN" },
        { 5, wp_missile,  wp_nochange,     "RCKT" },
        { 6, wp_plasma,   wp_nochange,     "PLAS" },
        { 7, wp_bfg,      wp_nochange,     "BFG"  },
    };

    static const int slot_y[7] = { 0, 29, 59, 89, 119, 148, 178 };
    static const int slot_h[7] = { 29, 30, 30, 30, 29, 30, 30 };

    for (int i = 0; i < 7; i++) {
        int sy = slot_y[i];
        int sh = slot_h[i];
        int by = sy;
        int bh = sh - 1;
        int bx = 268;
        int bw = 52;

        bool owned = false;
        bool active = false;
        const char *label = slots[i].name;

        if (i == 0) {
            // Slot 1: Fist / Chainsaw
            owned = true;
            if (plr->weaponowned[wp_chainsaw]) {
                label = (plr->readyweapon == wp_chainsaw) ? "SAW" : "FIST";
            }
            active = (plr->readyweapon == wp_fist || plr->readyweapon == wp_chainsaw);
        } else if (i == 2) {
            // Slot 3: Shotgun / SSG
            owned = plr->weaponowned[wp_shotgun] || plr->weaponowned[wp_supershotgun];
            if (plr->weaponowned[wp_supershotgun] && plr->readyweapon == wp_supershotgun) {
                label = "SSG";
            } else if (plr->weaponowned[wp_supershotgun] && plr->weaponowned[wp_shotgun]) {
                label = (plr->readyweapon == wp_shotgun) ? "SHOT" : "SSG";
            } else if (plr->weaponowned[wp_supershotgun]) {
                label = "SSG";
            } else {
                label = "SHOT";
            }
            active = (plr->readyweapon == wp_shotgun || plr->readyweapon == wp_supershotgun);
        } else {
            owned = plr->weaponowned[slots[i].wp];
            if ((i == 5 || i == 6) && gamemode == shareware) owned = false;
            active = (plr->readyweapon == slots[i].wp);
        }

        // 52 completely clean, unique stone columns from STBAR with zero text or artifacts
        static const u8 clean_stone_cols[52] = {
              1,   2,   3,   4,   5,   6,   7,  39,  40,  41,  42,  43,  44,
             45,  49,  50,  51,  52,  53,  54,  96,  97,  98,  99, 100, 101,
            102, 103, 179, 180, 181, 184, 185, 225, 226, 227, 228, 229, 231,
            232, 233, 234, 241, 242, 243, 244, 245, 246, 107, 108, 139, 140
        };

        // 1. Tile authentic Doom metallic stone background using 52 unique non-repeating columns
        int col_shift = (i * 13) % 52;
        for (int x = bx; x < bx + bw; x++) {
            int src_col = clean_stone_cols[(col_shift + (x - bx)) % 52];
            DS_DrawWadPatchSub(fb, x, by, "STBAR", src_col, 1);
        }

        // 2. Beveled borders (authentic Doom style)
        DS_FillRect(fb, bx, by, bw, 1, 107, 107, 107);
        DS_FillRect(fb, bx, by + 1, 1, bh - 1, 107, 107, 107);
        DS_FillRect(fb, bx, by + bh - 1, bw, 1, 27, 27, 27);
        DS_FillRect(fb, bx + bw - 1, by + 1, 1, bh - 1, 27, 27, 27);

        // 3. If ACTIVE: Draw crisp Boom/Doom pure white highlight bracket around active weapon slot!
        if (active) {
            DS_DrawRect(fb, bx, by, bw, bh, 255, 255, 255);
            DS_DrawRect(fb, bx + 1, by + 1, bw - 2, bh - 2, 255, 255, 255);
        }

        // 4. Draw authentic Doom RED number (STTNUM)
        char num_patch[12];
        snprintf(num_patch, sizeof(num_patch), "STTNUM%d", slots[i].slot);
        int num_x = bx + 5;
        int num_y = by + (bh - 16) / 2;
        if (owned) {
            DS_DrawWadPatch(fb, num_x, num_y, num_patch); // Bright authentic Doom red number
        } else {
            DS_DrawWadPatchDimmed(fb, num_x, num_y, num_patch, 30); // Dim dark red number
        }

        // 5. Draw weapon name behind the number in pure white (if owned) or dim gray (if unowned)
        int tx = bx + 22;
        int ty = by + (bh - 7) / 2;
        if (owned || active) {
            DS_DrawString(fb, tx, ty, label, 255, 255, 255, 1); // Pure white
        } else {
            DS_DrawString(fb, tx, ty, label, 90, 90, 95, 1);   // Dim gray
        }
    }
}

// Render the interactive 7-slot weapon selection panel for Heretic (X: 268 to 319, Y: 0 to 207)
static void DS_DrawHereticWeaponSelector(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    // Divider border between map and weapon selector
    DS_FillRect(fb, 267, 0, 1, 208, 50, 55, 65);

    static const struct {
        int slot;
        weapontype_t wp;
        weapontype_t alt_wp;
        const char *name;
    } slots[7] = {
        { 1, wp_staff,      wp_gauntlets, "STAF" },
        { 2, wp_goldwand,   wp_nochange,  "WAND" },
        { 3, wp_crossbow,   wp_nochange,  "XBOW" },
        { 4, wp_blaster,    wp_nochange,  "CLAW" },
        { 5, wp_skullrod,   wp_nochange,  "HELL" },
        { 6, wp_phoenixrod, wp_nochange,  "PHNX" },
        { 7, wp_mace,       wp_nochange,  "MACE" },
    };

    static const int slot_y[7] = { 0, 29, 59, 89, 119, 148, 178 };
    static const int slot_h[7] = { 29, 30, 30, 30, 29, 30, 30 };

    for (int i = 0; i < 7; i++) {
        int sy = slot_y[i];
        int sh = slot_h[i];
        int by = sy;
        int bh = sh - 1;
        int bx = 268;
        int bw = 52;

        bool owned = false;
        bool active = false;
        const char *label = slots[i].name;

        if (i == 0) {
            owned = true;
            if (plr->weaponowned[wp_gauntlets]) {
                label = (plr->readyweapon == wp_gauntlets) ? "GAUN" : "STAF";
            }
            active = (plr->readyweapon == wp_staff || plr->readyweapon == wp_gauntlets);
        } else {
            owned = plr->weaponowned[slots[i].wp];
            active = (plr->readyweapon == slots[i].wp);
        }

        // Dark fantasy wood/stone background
        DS_FillRect(fb, bx, by, bw, bh, 35, 26, 20);

        // Beveled borders
        DS_FillRect(fb, bx, by, bw, 1, 75, 55, 45);
        DS_FillRect(fb, bx, by + 1, 1, bh - 1, 75, 55, 45);
        DS_FillRect(fb, bx, by + bh - 1, bw, 1, 18, 12, 10);
        DS_FillRect(fb, bx + bw - 1, by + 1, 1, bh - 1, 18, 12, 10);

        // Active highlight: gold double border
        if (active) {
            DS_DrawRect(fb, bx, by, bw, bh, 255, 215, 0);
            DS_DrawRect(fb, bx + 1, by + 1, bw - 2, bh - 2, 255, 215, 0);
        }

        // Slot number "1".."7"
        char num_str[4];
        snprintf(num_str, sizeof(num_str), "%d", slots[i].slot);
        int num_x = bx + 6;
        int num_y = by + (bh - 7) / 2;
        if (owned) {
            DS_DrawString(fb, num_x, num_y, num_str, 255, 215, 0, 1);
        } else {
            DS_DrawString(fb, num_x, num_y, num_str, 80, 70, 60, 1);
        }

        // Weapon name label
        int tx = bx + 18;
        int ty = by + (bh - 7) / 2;
        if (active) {
            DS_DrawString(fb, tx, ty, label, 255, 255, 255, 1);
        } else if (owned) {
            DS_DrawString(fb, tx, ty, label, 220, 200, 160, 1);
        } else {
            DS_DrawString(fb, tx, ty, label, 75, 70, 65, 1);
        }
    }
}

// Render the interactive 4-slot weapon selection panel for Hexen (X: 268 to 319, Y: 0 to 207)
static void DS_DrawHexenWeaponSelector(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    // Divider border between map and weapon selector
    DS_FillRect(fb, 267, 0, 1, 208, 50, 55, 65);

    int pclass = PlayerClass[consoleplayer];
    static const char *f_names[4] = { "FIST", "AXE",  "HAMR", "QUIET" };
    static const char *c_names[4] = { "MACE", "STAF", "FIRE", "WRTH"  };
    static const char *m_names[4] = { "WAND", "FRST", "ARC",  "BLOD"  };

    const char **names = f_names;
    if (pclass == 2) names = c_names;
    else if (pclass == 3) names = m_names;

    for (int i = 0; i < 4; i++) {
        int by = i * 52;
        int bh = 51;
        int bx = 268;
        int bw = 52;

        bool owned = plr->weaponowned[i];
        bool active = (plr->readyweapon == i);
        const char *label = names[i];

        // Dark gothic stone background
        DS_FillRect(fb, bx, by, bw, bh, 24, 26, 32);

        // Beveled borders
        DS_FillRect(fb, bx, by, bw, 1, 60, 65, 75);
        DS_FillRect(fb, bx, by + 1, 1, bh - 1, 60, 65, 75);
        DS_FillRect(fb, bx, by + bh - 1, bw, 1, 14, 15, 18);
        DS_FillRect(fb, bx + bw - 1, by + 1, 1, bh - 1, 14, 15, 18);

        // Active highlight: pure white double border
        if (active) {
            DS_DrawRect(fb, bx, by, bw, bh, 255, 255, 255);
            DS_DrawRect(fb, bx + 1, by + 1, bw - 2, bh - 2, 255, 255, 255);
        }

        // Slot number "1".."4"
        char num_str[4];
        snprintf(num_str, sizeof(num_str), "%d", i + 1);
        if (owned) {
            DS_DrawString(fb, bx + 6, by + 8, num_str, 100, 180, 255, 2);
        } else {
            DS_DrawString(fb, bx + 6, by + 8, num_str, 60, 70, 85, 2);
        }

        // Weapon name label
        if (active) {
            DS_DrawString(fb, bx + 20, by + 12, label, 255, 255, 255, 1);
        } else if (owned) {
            DS_DrawString(fb, bx + 20, by + 12, label, 180, 210, 240, 1);
        } else {
            DS_DrawString(fb, bx + 20, by + 12, label, 70, 75, 90, 1);
        }

        // For Slot 4: Weapon pieces indicator
        if (i == 3) {
            if (owned) {
                DS_DrawString(fb, bx + 8, by + 34, "[READY]", 80, 240, 100, 1);
            } else {
                int pieces = plr->pieces;
                char piece_buf[16];
                snprintf(piece_buf, sizeof(piece_buf), "P:%s%s%s",
                         (pieces & WPIECE1) ? "1" : "-",
                         (pieces & WPIECE2) ? "2" : "-",
                         (pieces & WPIECE3) ? "3" : "-");
                DS_DrawString(fb, bx + 6, by + 34, piece_buf, 240, 180, 60, 1);
            }
        }
    }
}

// Render the interactive 8-slot Artifact Tray for Heretic & Hexen (X: 0 to 267, Y: 176 to 207)
static void DS_DrawArtifactTray(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    // Background tray bar
    DS_FillRect(fb, 0, 176, 268, 32, 14, 16, 20);
    DS_FillRect(fb, 0, 175, 268, 1, 50, 55, 65);

    static const char *heretic_arti_sprites[] = {
        "ARTIBOX", "ARTIINVU", "ARTIINVS", "ARTIPTN2", "ARTISPHL",
        "ARTIPWBK", "ARTITRCH", "ARTIFBMB", "ARTIEGGC", "ARTISOAR", "ARTIATLP"
    };

    static const char *hexen_arti_sprites[] = {
        "ARTIBOX", "ARTIINVU", "ARTIPTN2", "ARTISPHL", "ARTIHRAD",
        "ARTISUMN", "ARTITRCH", "ARTIPORK", "ARTISOAR", "ARTIBLST",
        "ARTIPSBG", "ARTITELO", "ARTISPED", "ARTIBMAN", "ARTIBRAC",
        "ARTIATLP", "ARTISKLL", "ARTIBGEM", "ARTIGEMR", "ARTIGEMG",
        "ARTIGMG2", "ARTIGEMB", "ARTIGMB2", "ARTIBOK1", "ARTIBOK2",
        "ARTISKL2", "ARTIFWEP", "ARTICWEP", "ARTIMWEP", "ARTIGEAR",
        "ARTIGER2", "ARTIGER3", "ARTIGER4"
    };

    for (int i = 0; i < 8; i++) {
        int bx = 2 + i * 33;
        int by = 177;
        int bw = 32;
        int bh = 30;

        // Slot frame
        DS_FillRect(fb, bx, by, bw, bh, 22, 25, 32);
        DS_DrawRect(fb, bx, by, bw, bh, 45, 50, 62);

        if (i < plr->inventorySlotNum && plr->inventory[i].type != arti_none && plr->inventory[i].count > 0) {
            int type = plr->inventory[i].type;
            int count = plr->inventory[i].count;

            // Active/Ready artifact highlight
            if (type == plr->readyArtifact) {
                DS_DrawRect(fb, bx, by, bw, bh, 255, 215, 0);
                DS_DrawRect(fb, bx + 1, by + 1, bw - 2, bh - 2, 255, 215, 0);
            }

            // Sprite lump lookup
            const char *arti_name = NULL;
            if (hexen) {
                if (type >= 0 && type < (int)(sizeof(hexen_arti_sprites) / sizeof(hexen_arti_sprites[0]))) {
                    arti_name = hexen_arti_sprites[type];
                }
            } else {
                if (type >= 0 && type < (int)(sizeof(heretic_arti_sprites) / sizeof(heretic_arti_sprites[0]))) {
                    arti_name = heretic_arti_sprites[type];
                }
            }

            if (arti_name) {
                int lump = W_CheckNumForName2(arti_name, ns_sprites);
                if (lump >= 0) {
                    DS_DrawPatchCentered(fb, bx + 1, by + 1, bw - 2, bh - 2, lump);
                }
            }

            // Stack count
            if (count > 1) {
                char cnt_str[8];
                snprintf(cnt_str, sizeof(cnt_str), "%d", count);
                DS_DrawString(fb, bx + bw - 8, by + bh - 8, cnt_str, 255, 255, 255, 1);
            }
        }
    }
}

// Draw Dashboard when in Menu or Title screen
static void DS_DrawDashboard(u8 *fb)
{
    DS_FillRect(fb, 0, 0, 320, 240, 14, 16, 22);

    // Title banner
    DS_FillRect(fb, 0, 0, 320, 36, 28, 32, 42);
    DS_DrawString(fb, 45, 10, "TRINITY3DS", 240, 60, 50, 2);
    DS_DrawString(fb, 120, 26, TRINITY3DS_VERSION_STR, 100, 180, 255, 1);

    // Info card
    DS_FillRect(fb, 16, 48, 288, 55, 22, 26, 36);
    DS_DrawString(fb, 26, 56, "SYSTEM STATUS", 140, 160, 190, 1);
    DS_DrawString(fb, 26, 70, "New 3DS Speedup : Active (804 MHz)", 60, 220, 80, 1);
    DS_DrawString(fb, 26, 84, "Renderer Engine : GPU-OpenGL / Citro3D", 220, 220, 220, 1);

    // Controls guide
    DS_FillRect(fb, 16, 112, 288, 114, 22, 26, 36);
    DS_DrawString(fb, 26, 120, "CONTROLS GUIDE", 140, 160, 190, 1);
    DS_DrawString(fb, 26, 134, "Circle Pad : WASD Move & Strafe", 240, 200, 40, 1);
    DS_DrawString(fb, 26, 148, "C-Stick    : Camera Turn Left/Right", 240, 200, 40, 1);
    DS_DrawString(fb, 26, 162, "A / R / ZR : Attack / Fire", 220, 220, 220, 1);
    DS_DrawString(fb, 26, 176, "B          : Use / Open Doors", 220, 220, 220, 1);
    DS_DrawString(fb, 26, 190, "Touchscreen: Tap Automap to Zoom", 80, 180, 255, 1);
    DS_DrawString(fb, 26, 204, "mcl5500", 120, 130, 150, 1);
}

void DS_Init(void)
{
    ds_map_zoom = 2;
    ds_pal_loaded = false;
}

// Interactive Touch Weapon Selection
void DS_SelectWeaponSlot(int slot)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    weapontype_t target = wp_nochange;

    if (hexen) {
        if (slot >= 1 && slot <= 4) {
            int wp = slot - 1;
            if (plr->weaponowned[wp]) {
                target = (weapontype_t)wp;
            }
        }
    } else if (heretic) {
        switch (slot) {
            case 1:
                // Staff / Gauntlets toggle
                if (plr->weaponowned[wp_gauntlets] && plr->weaponowned[wp_staff]) {
                    target = (plr->readyweapon == wp_gauntlets) ? wp_staff : wp_gauntlets;
                } else if (plr->weaponowned[wp_gauntlets]) {
                    target = wp_gauntlets;
                } else if (plr->weaponowned[wp_staff]) {
                    target = wp_staff;
                }
                break;
            case 2:
                if (plr->weaponowned[wp_goldwand]) target = wp_goldwand;
                break;
            case 3:
                if (plr->weaponowned[wp_crossbow]) target = wp_crossbow;
                break;
            case 4:
                if (plr->weaponowned[wp_blaster]) target = wp_blaster;
                break;
            case 5:
                if (plr->weaponowned[wp_skullrod]) target = wp_skullrod;
                break;
            case 6:
                if (plr->weaponowned[wp_phoenixrod]) target = wp_phoenixrod;
                break;
            case 7:
                if (plr->weaponowned[wp_mace]) target = wp_mace;
                break;
        }
    } else {
        switch (slot) {
            case 1:
                // Slot 1: Fist / Chainsaw toggle
                if (plr->weaponowned[wp_chainsaw] && plr->weaponowned[wp_fist]) {
                    target = (plr->readyweapon == wp_chainsaw) ? wp_fist : wp_chainsaw;
                } else if (plr->weaponowned[wp_chainsaw]) {
                    target = wp_chainsaw;
                } else if (plr->weaponowned[wp_fist]) {
                    target = wp_fist;
                }
                break;
            case 2:
                // Slot 2: Pistol
                if (plr->weaponowned[wp_pistol]) target = wp_pistol;
                break;
            case 3:
                // Slot 3: Shotgun / Super Shotgun toggle
                if (plr->weaponowned[wp_supershotgun] && plr->weaponowned[wp_shotgun]) {
                    target = (plr->readyweapon == wp_supershotgun) ? wp_shotgun : wp_supershotgun;
                } else if (plr->weaponowned[wp_supershotgun]) {
                    target = wp_supershotgun;
                } else if (plr->weaponowned[wp_shotgun]) {
                    target = wp_shotgun;
                }
                break;
            case 4:
                // Slot 4: Chaingun
                if (plr->weaponowned[wp_chaingun]) target = wp_chaingun;
                break;
            case 5:
                // Slot 5: Rocket Launcher
                if (plr->weaponowned[wp_missile]) target = wp_missile;
                break;
            case 6:
                // Slot 6: Plasma Rifle
                if (plr->weaponowned[wp_plasma] && gamemode != shareware) target = wp_plasma;
                break;
            case 7:
                // Slot 7: BFG 9000
                if (plr->weaponowned[wp_bfg] && gamemode != shareware) target = wp_bfg;
                break;
        }
    }

    if (target != wp_nochange && plr->weaponowned[target]) {
        plr->pendingweapon = target;
        ds_touch_weapon = target;
    }
}

void DS_HandleTouch(int touch_x, int touch_y)
{
    if (gamestate == GS_LEVEL) {
        player_t *plr = &players[consoleplayer];

        // 1. Right-side Weapon Selector touch (Slots 1 to 4 in Hexen, 1 to 7 in Heretic/Doom)
        if (touch_x >= 268 && touch_y < 208) {
            int slot = 1;
            if (hexen) {
                slot = (touch_y / 52) + 1;
                if (slot > 4) slot = 4;
            } else {
                if (touch_y < 29) slot = 1;
                else if (touch_y < 59) slot = 2;
                else if (touch_y < 89) slot = 3;
                else if (touch_y < 119) slot = 4;
                else if (touch_y < 148) slot = 5;
                else if (touch_y < 178) slot = 6;
                else slot = 7;
            }
            DS_SelectWeaponSlot(slot);
        }
        // 2. Raven games: Artifact Tray touch (X: 0 to 267, Y: 176 to 207)
        else if (raven && touch_y >= 176 && touch_y < 208 && touch_x < 268) {
            int slot = (touch_x - 2) / 33;
            if (plr && slot >= 0 && slot < plr->inventorySlotNum && plr->inventory[slot].count > 0) {
                ds_touch_artifact = plr->inventory[slot].type;
            }
        }
        // 3. Header bar action buttons (WADS / RESET, SAVE, LOAD, ZOOM)
        else if (touch_y < 24) {
            if (touch_x >= 2 && touch_x <= 44) {
                // Soft-reset back to WAD Manager
                I_ResetToLauncher();
                return;
            } else if (touch_x >= 138 && touch_x <= 178) {
                DS_QuickSave();
            } else if (touch_x >= 182 && touch_x <= 222) {
                DS_QuickLoad();
            } else if (touch_x >= 226 && touch_x <= 266) {
                // Zoom button
                ds_map_zoom++;
                if (ds_map_zoom > 4) ds_map_zoom = 1;
            }
        }
        // 4. Automap area: toggle zoom
        else if (touch_y < (raven ? 176 : 208)) {
            ds_map_zoom++;
            if (ds_map_zoom > 4) {
                ds_map_zoom = 1;
            }
        }
        // 5. Status bar touch (Y: 208..239) - Interactive for health, mana, and ammo!
        else {
            if (hexen) {
                if (touch_x < 130) {
                    // Health bar tapped: try healing with Quartz Flask or Mystic Urn if injured
                    bool healed = false;
                    if (plr->health < 100) {
                        for (int i = 0; i < plr->inventorySlotNum; i++) {
                            if ((plr->inventory[i].type == hexen_arti_health || plr->inventory[i].type == hexen_arti_superhealth) && plr->inventory[i].count > 0) {
                                ds_touch_artifact = plr->inventory[i].type;
                                healed = true;
                                break;
                            }
                        }
                    }
                    if (!healed) {
                        DS_SelectWeaponSlot(1); // Fist/Punch
                    }
                } else if (touch_x >= 144 && touch_x < 232) {
                    // Blue Mana tapped: select Weapon 2 (or W4 if already on W2)
                    if (plr->readyweapon != wp_second && plr->weaponowned[wp_second]) {
                        plr->pendingweapon = wp_second;
                        ds_touch_weapon = wp_second;
                    } else if (plr->weaponowned[wp_fourth] && plr->ammo[1] > 0) {
                        plr->pendingweapon = wp_fourth;
                        ds_touch_weapon = wp_fourth;
                    }
                } else if (touch_x >= 232) {
                    // Green Mana tapped: select Weapon 3 (or W4 if already on W3)
                    if (plr->readyweapon != wp_third && plr->weaponowned[wp_third]) {
                        plr->pendingweapon = wp_third;
                        ds_touch_weapon = wp_third;
                    } else if (plr->weaponowned[wp_fourth] && plr->ammo[0] > 0) {
                        plr->pendingweapon = wp_fourth;
                        ds_touch_weapon = wp_fourth;
                    }
                }
            } else if (heretic) {
                if (touch_x < 144) {
                    // Health bar tapped: try healing with Quartz Flask or Mystic Urn if injured
                    bool healed = false;
                    if (plr->health < 100) {
                        for (int i = 0; i < plr->inventorySlotNum; i++) {
                            if ((plr->inventory[i].type == arti_health || plr->inventory[i].type == arti_superhealth) && plr->inventory[i].count > 0) {
                                ds_touch_artifact = plr->inventory[i].type;
                                healed = true;
                                break;
                            }
                        }
                    }
                    if (!healed) {
                        DS_SelectWeaponSlot(1); // Staff / Gauntlets toggle
                    }
                } else if (touch_x >= 144 && touch_x <= 284) {
                    // Ammo bar tapped: cycle through weapons that use ammo
                    int cur = plr->readyweapon;
                    for (int step = 1; step <= 6; step++) {
                        int wp = (cur + step) % 7;
                        if (wp == wp_staff) continue;
                        if (plr->weaponowned[wp]) {
                            ammotype_t am = weaponinfo[wp].ammo;
                            if (am != am_noammo && plr->ammo[am] > 0) {
                                plr->pendingweapon = (weapontype_t)wp;
                                ds_touch_weapon = (weapontype_t)wp;
                                break;
                            }
                        }
                    }
                }
            } else {
                // Doom:
                if (touch_x <= 54) {
                    // Ammo tapped: cycle through weapons with ammo
                    static const weapontype_t wp_order[6] = {
                        wp_shotgun, wp_supershotgun, wp_chaingun, wp_missile, wp_plasma, wp_bfg
                    };
                    int cur = plr->readyweapon;
                    for (int step = 0; step < 6; step++) {
                        weapontype_t wp = wp_order[step];
                        if (wp != cur && plr->weaponowned[wp]) {
                            ammotype_t am = weaponinfo[wp].ammo;
                            if (am != am_noammo && plr->ammo[am] > 0) {
                                plr->pendingweapon = wp;
                                ds_touch_weapon = wp;
                                break;
                            }
                        }
                    }
                } else if (touch_x <= 180) {
                    // Tap Health or Face -> Quick Melee / Fist / Chainsaw toggle
                    DS_SelectWeaponSlot(1);
                }
            }
        }
    }
}

void DS_UpdateBottomScreen(void)
{
    u8 *framebuf = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!framebuf) return;

    if (gamestate == GS_LEVEL) {
        DS_DrawTacticalAutomap(framebuf);
        if (hexen) {
            DS_DrawHexenWeaponSelector(framebuf);
            DS_DrawArtifactTray(framebuf);
            DS_DrawHexenStatusBar(framebuf);
        } else if (heretic) {
            DS_DrawHereticWeaponSelector(framebuf);
            DS_DrawArtifactTray(framebuf);
            DS_DrawHereticStatusBar(framebuf);
        } else {
            DS_DrawWeaponSelector(framebuf);
            DS_DrawAuthenticStatusBar(framebuf);
        }
    } else {
        DS_DrawDashboard(framebuf);
    }
}

#ifdef __3DS__
dboolean I_SoftwareKeyboard(const char *hint, const char *initial, char *out, int max_len)
{
    if (!out || max_len <= 0) return false;

    SwkbdState swkbd;
    char myTypedString[256];
    memset(myTypedString, 0, sizeof(myTypedString));

    int limit = max_len;
    if (limit > 255) limit = 255;

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, limit);
    swkbdSetFeatures(&swkbd, SWKBD_DEFAULT_QWERTY | SWKBD_PREDICTIVE_INPUT | SWKBD_ALLOW_HOME);
    swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    if (hint && *hint) {
        swkbdSetHintText(&swkbd, hint);
    }
    if (initial && *initial) {
        swkbdSetInitialText(&swkbd, initial);
    }

    SwkbdButton button = swkbdInputText(&swkbd, myTypedString, sizeof(myTypedString));

    // Clear any input events generated while tapping the keyboard
    hidScanInput();

    if (button == SWKBD_BUTTON_CONFIRM || button == SWKBD_BUTTON_RIGHT) {
        strncpy(out, myTypedString, limit);
        out[limit] = '\0';
        return true;
    }

    return false;
}

dboolean I_SoftwareKeyboardNumeric(const char *hint, const char *initial, char *out, int max_len)
{
    if (!out || max_len <= 0) return false;

    SwkbdState swkbd;
    char myTypedString[256];
    memset(myTypedString, 0, sizeof(myTypedString));

    int limit = max_len;
    if (limit > 255) limit = 255;

    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, limit);
    swkbdSetFeatures(&swkbd, SWKBD_ALLOW_HOME);
    swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    if (hint && *hint) {
        swkbdSetHintText(&swkbd, hint);
    }
    if (initial && *initial) {
        swkbdSetInitialText(&swkbd, initial);
    }

    SwkbdButton button = swkbdInputText(&swkbd, myTypedString, sizeof(myTypedString));

    // Clear any input events generated while tapping the keyboard
    hidScanInput();

    if (button == SWKBD_BUTTON_CONFIRM || button == SWKBD_BUTTON_RIGHT) {
        strncpy(out, myTypedString, limit);
        out[limit] = '\0';
        return true;
    }

    return false;
}
#endif

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
#include "version.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static int ds_map_zoom = 5; // Zoom scale shift

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

// Draw a line with configurable thickness (1 or 2 pixels)
static void DS_DrawLine(u8 *fb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b, int min_y, int max_y, int thick)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        if (y0 >= min_y && y0 <= max_y && x0 >= 0 && x0 < 320) {
            DS_DrawPixel(fb, x0, y0, r, g, b);
            if (thick == 2) {
                DS_DrawPixel(fb, x0 + 1, y0, r, g, b);
                DS_DrawPixel(fb, x0, y0 + 1, r, g, b);
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
        const u8 *data = (const u8 *)W_CacheLumpNum(lump);
        if (data) {
            memcpy(ds_palette, data, 768);
            ds_pal_loaded = true;
        }
    }
}

// Draw an authentic Doom patch lump directly into the bottom framebuffer
static void DS_DrawWadPatch(u8 *fb, int dest_x, int dest_y, const char *patch_name)
{
    if (!ds_pal_loaded) DS_LoadPalette();
    if (!ds_pal_loaded) return;

    int lump = W_CheckNumForName(patch_name);
    if (lump < 0) return;

    const u8 *data = (const u8 *)W_CacheLumpNum(lump);
    if (!data) return;

    short w = *(const short *)(data + 0);
    short h = *(const short *)(data + 2);
    short leftoffset = *(const short *)(data + 4);
    short topoffset = *(const short *)(data + 6);
    const int *columnofs = (const int *)(data + 8);

    // Apply Doom patch offsets so elements (like Doomguy face) center correctly
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
static void DS_DrawAuthenticStatusBar(u8 *fb)
{
    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    const int ST_Y = 208;

    // 1. Draw base STBAR background (320x32 pixels)
    DS_DrawWadPatch(fb, 0, ST_Y, "STBAR");

    // 2. Ammo count (Right-aligned at X: 44, Y: ST_Y + 3)
    ammotype_t amtype = weaponinfo[plr->readyweapon].ammo;
    if (amtype != am_noammo) {
        DS_DrawDoomNumber(fb, 44, ST_Y + 3, plr->ammo[amtype], 3);
    }

    // 3. Health (Right-aligned at X: 90, Y: ST_Y + 3)
    DS_DrawDoomNumber(fb, 90, ST_Y + 3, plr->health, 3);
    DS_DrawWadPatch(fb, 90, ST_Y + 3, "STTPRCNT"); // % symbol

    // 4. Arms background (X: 104, Y: ST_Y)
    DS_DrawWadPatch(fb, 104, ST_Y, "STARMS");

    // 5. Doomguy Face (X: 143, Y: ST_Y)
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

    // 6. Armor (Right-aligned at X: 221, Y: ST_Y + 3)
    DS_DrawDoomNumber(fb, 221, ST_Y + 3, plr->armorpoints, 3);
    DS_DrawWadPatch(fb, 221, ST_Y + 3, "STTPRCNT");

    // 7. Keycards / Skulls (X: 239)
    // Blue key
    if (plr->cards[it_blueskull]) DS_DrawWadPatch(fb, 239, ST_Y + 3, "STKEYS3");
    else if (plr->cards[it_bluecard]) DS_DrawWadPatch(fb, 239, ST_Y + 3, "STKEYS0");

    // Yellow key
    if (plr->cards[it_yellowskull]) DS_DrawWadPatch(fb, 239, ST_Y + 13, "STKEYS4");
    else if (plr->cards[it_yellowcard]) DS_DrawWadPatch(fb, 239, ST_Y + 13, "STKEYS1");

    // Red key
    if (plr->cards[it_redskull]) DS_DrawWadPatch(fb, 239, ST_Y + 23, "STKEYS5");
    else if (plr->cards[it_redcard]) DS_DrawWadPatch(fb, 239, ST_Y + 23, "STKEYS2");
}

// Render the Wolfenstein-style Tactical Live-Automap (Y: 0 to 207)
static void DS_DrawTacticalAutomap(u8 *fb)
{
    // 1. Pure Black Background (authentic Wolfenstein style)
    DS_FillRect(fb, 0, 0, 320, 208, 0, 0, 0);

    // 2. Crisp Outer Border (Dark Slate Gray)
    DS_FillRect(fb, 0, 0, 320, 2, 50, 55, 65);
    DS_FillRect(fb, 0, 206, 320, 2, 50, 55, 65);
    DS_FillRect(fb, 0, 0, 2, 208, 50, 55, 65);
    DS_FillRect(fb, 318, 0, 2, 208, 50, 55, 65);

    player_t *plr = &players[consoleplayer];
    if (!plr || !plr->mo) return;

    fixed_t px = plr->mo->x;
    fixed_t py = plr->mo->y;

    const int center_x = 160;
    const int center_y = 104;
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

        // Bounding box test against 320x208
        if ((x0 < -60 && x1 < -60) || (x0 > 380 && x1 > 380) ||
            (y0 < -60 && y1 < -60) || (y0 > 260 && y1 > 260))
            continue;

        u8 r = 175, g = 180, b = 195; // Wolfenstein Solid Wall: Crisp Slate Gray
        int thick = 2;

        if (ld->flags & ML_SECRET) {
            // Secret Wall / Door:
            // Must stay DISGUISED as a normal wall until discovered or opened!
            if ((ld->backsector && ld->special == 0) ||
                (ld->frontsector && P_WasSecret(ld->frontsector) && !P_IsSecret(ld->frontsector)) ||
                (ld->backsector && P_WasSecret(ld->backsector) && !P_IsSecret(ld->backsector))) {
                // Discovered secret passage: subtle gold portal
                r = 240; g = 200; b = 60;
                thick = 1;
            } else {
                // Unopened secret: identical to standard solid wall (no spoilers!)
                r = 175; g = 180; b = 195;
                thick = 2;
            }
        } else if (ld->special == 11 || ld->special == 51 || ld->special == 52 ||
                   ld->special == 124 || ld->special == 197 || ld->special == 198) {
            // Exit switch / line: Light Green (like elevator in Wolfenstein)
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
                r = 70; g = 150; b = 255;  // Standard door: Light Blue (Wolfenstein style)
            }
            thick = 2;
        } else if (ld->backsector != NULL) {
            // 2-sided open ledge / window / floor step: Subtle Dark Slate
            r = 90; g = 95; b = 110;
            thick = 1;
        }

        DS_DrawLine(fb, x0, y0, x1, y1, r, g, b, 2, 205, thick);
    }

    // 4. Draw active enemies & pickups ONLY when in visual range (Wolfenstein-style line of sight)
    if (thinkercap.next) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function == P_MobjThinker) {
                mobj_t *mo = (mobj_t *)th;
                if (!mo) continue;

                // Only reveal if within line of sight (im sichtbaren Bereich) or allmap cheat
                if (!plr->powers[pw_allmap] && !P_CheckSight(plr->mo, mo)) {
                    continue;
                }

                int mx = center_x + ((mo->x - px) >> shift);
                int my = center_y - ((mo->y - py) >> shift);

                if (mx >= 4 && mx < 316 && my >= 4 && my < 204) {
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

    // 5. Draw Player Arrow (Wolfenstein-style White / Green arrow)
    angle_t angle = plr->mo->angle;
    double rad = (double)angle * (3.14159265358979323846 / (double)ANG180);

    int tip_x = center_x + (int)(11.0 * cos(rad));
    int tip_y = center_y - (int)(11.0 * sin(rad));

    int l_x = center_x + (int)(7.0 * cos(rad + 2.5));
    int l_y = center_y - (int)(7.0 * sin(rad + 2.5));

    int r_x = center_x + (int)(7.0 * cos(rad - 2.5));
    int r_y = center_y - (int)(7.0 * sin(rad - 2.5));

    // Player arrow (Bright White core, Green outline)
    DS_DrawLine(fb, center_x, center_y, tip_x, tip_y, 255, 255, 255, 2, 205, 1);
    DS_DrawLine(fb, tip_x, tip_y, l_x, l_y, 40, 255, 60, 2, 205, 1);
    DS_DrawLine(fb, tip_x, tip_y, r_x, r_y, 40, 255, 60, 2, 205, 1);
    DS_DrawLine(fb, l_x, l_y, center_x, center_y, 40, 255, 60, 2, 205, 1);

    // 6. Tactical Header Bar (Kills, Items, Secrets, Zoom)
    char stats_buf[64];
    snprintf(stats_buf, sizeof(stats_buf), "KILLS: %d/%d  ITEMS: %d/%d  SECRETS: %d/%d",
             plr->killcount, totalkills, plr->itemcount, totalitems, plr->secretcount, totalsecret);
    DS_DrawString(fb, 8, 6, stats_buf, 170, 205, 240, 1);

    char zoom_buf[16];
    snprintf(zoom_buf, sizeof(zoom_buf), "ZOOM %dx", 10 - ds_map_zoom);
    DS_DrawString(fb, 264, 6, zoom_buf, 170, 205, 240, 1);
}

// Draw Dashboard when in Menu or Title screen
static void DS_DrawDashboard(u8 *fb)
{
    DS_FillRect(fb, 0, 0, 320, 240, 14, 16, 22);

    // Title banner
    DS_FillRect(fb, 0, 0, 320, 36, 28, 32, 42);
    DS_DrawString(fb, 45, 10, "BOOM3DS DUALSCREEN", 240, 60, 50, 2);
    DS_DrawString(fb, 120, 26, BOOM3DS_DUALSCREEN_VERSION_STR, 100, 180, 255, 1);

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
    DS_DrawString(fb, 26, 204, "Created by : Sheldor & Team", 120, 130, 150, 1);
}

void DS_Init(void)
{
    ds_map_zoom = 5;
    ds_pal_loaded = false;
}

void DS_HandleTouch(int touch_x, int touch_y)
{
    if (gamestate == GS_LEVEL) {
        if (touch_y < 208) {
            ds_map_zoom--;
            if (ds_map_zoom < 3) {
                ds_map_zoom = 7;
            }
        }
    }
}

void DS_UpdateBottomScreen(void)
{
    u8 *framebuf = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!framebuf) return;

    touchPosition touch;
    hidTouchRead(&touch);
    if (touch.px > 0 || touch.py > 0) {
        static bool was_touched = false;
        if (!was_touched) {
            DS_HandleTouch(touch.px, touch.py);
            was_touched = true;
        }
    }

    if (gamestate == GS_LEVEL) {
        DS_DrawTacticalAutomap(framebuf);
        DS_DrawAuthenticStatusBar(framebuf);
    } else {
        DS_DrawDashboard(framebuf);
    }
}

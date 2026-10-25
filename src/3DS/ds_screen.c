#include "ds_screen.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_state.h"
#include "p_mobj.h"
#include "version.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Current zoom level for automap: shift amount
static int ds_map_zoom = 5; // Fixed point shift: FRACBITS + ds_map_zoom
static bool ds_map_follow = true;

// Custom 5x7 ASCII bitmap font (characters 32 to 90)
static const unsigned char font5x7[] = {
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
    if (x < 0 || x >= 320 || y < 0 || y >= 240)
        return;
    // 3DS Bottom screen is rotated: X -> column, (239 - Y) -> row
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

static void DS_DrawLine(u8 *fb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b, int min_y, int max_y)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        if (y0 >= min_y && y0 <= max_y && x0 >= 0 && x0 < 320) {
            DS_DrawPixel(fb, x0, y0, r, g, b);
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
        unsigned char bits = font5x7[idx + col];
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

static const char* DS_GetWeaponName(weapontype_t wp)
{
    switch (wp) {
        case wp_fist: return "FIST";
        case wp_pistol: return "PISTOL";
        case wp_shotgun: return "SHOTGUN";
        case wp_chaingun: return "CHAINGUN";
        case wp_missile: return "ROCKET";
        case wp_plasma: return "PLASMA";
        case wp_bfg: return "BFG 9000";
        case wp_chainsaw: return "CHAINSAW";
        case wp_supershotgun: return "SUPER SHOTGUN";
        default: return "WEAPON";
    }
}

// Procedural Doomguy Mugshot face
static void DS_DrawMugshot(u8 *fb, int cx, int cy, int health, int damagecount, bool godmode)
{
    // Face box outline (36x36)
    int fx = cx - 18;
    int fy = cy - 18;
    DS_FillRect(fb, fx - 1, fy - 1, 38, 38, 50, 50, 55);
    DS_FillRect(fb, fx, fy, 36, 36, 18, 20, 26);

    // Skin base color
    u8 skin_r = 215, skin_g = 160, skin_b = 130;
    if (godmode) {
        skin_r = 240; skin_g = 220; skin_b = 160;
    } else if (damagecount > 0) {
        skin_r = 230; skin_g = 120; skin_b = 110; // Pain flash
    }

    // Hair
    DS_FillRect(fb, fx + 8, fy + 4, 20, 6, 80, 50, 25);
    DS_FillRect(fb, fx + 6, fy + 6, 4, 8, 80, 50, 25);
    DS_FillRect(fb, fx + 26, fy + 6, 4, 8, 80, 50, 25);

    // Face skin
    DS_FillRect(fb, fx + 8, fy + 8, 20, 20, skin_r, skin_g, skin_b);
    DS_FillRect(fb, fx + 10, fy + 26, 16, 4, skin_r, skin_g, skin_b);

    // Eyes
    u8 eye_r = 40, eye_g = 40, eye_b = 40;
    if (godmode) {
        eye_r = 255; eye_g = 255; eye_b = 50; // Golden eyes!
    }
    DS_FillRect(fb, fx + 11, fy + 14, 4, 3, 240, 240, 240);
    DS_FillRect(fb, fx + 21, fy + 14, 4, 3, 240, 240, 240);
    DS_FillRect(fb, fx + 12, fy + 14, 2, 3, eye_r, eye_g, eye_b);
    DS_FillRect(fb, fx + 22, fy + 14, 2, 3, eye_r, eye_g, eye_b);

    // Eyebrows (angry/determined)
    DS_DrawLine(fb, fx + 10, fy + 12, fx + 15, fy + 13, 60, 35, 15, fy, fy + 36);
    DS_DrawLine(fb, fx + 26, fy + 12, fx + 21, fy + 13, 60, 35, 15, fy, fy + 36);

    // Nose
    DS_FillRect(fb, fx + 17, fy + 18, 2, 4, skin_r - 35, skin_g - 35, skin_b - 35);

    // Mouth / Expression
    if (health <= 0) {
        // Dead: X eyes & flat mouth
        DS_FillRect(fb, fx + 11, fy + 14, 4, 3, 140, 20, 20);
        DS_FillRect(fb, fx + 21, fy + 14, 4, 3, 140, 20, 20);
        DS_DrawLine(fb, fx + 14, fy + 24, fx + 22, fy + 24, 70, 30, 30, fy, fy + 36);
    } else if (damagecount > 0 || health < 25) {
        // Grimace / open bloody teeth
        DS_FillRect(fb, fx + 14, fy + 24, 8, 3, 160, 20, 20);
        DS_DrawLine(fb, fx + 15, fy + 25, fx + 21, fy + 25, 230, 230, 230, fy, fy + 36);
        // Blood drops
        DS_FillRect(fb, fx + 10, fy + 10, 2, 4, 180, 20, 20);
        DS_FillRect(fb, fx + 20, fy + 27, 2, 4, 180, 20, 20);
    } else if (health < 60) {
        // Scratched smirk
        DS_DrawLine(fb, fx + 14, fy + 24, fx + 22, fy + 23, 110, 40, 40, fy, fy + 36);
        DS_FillRect(fb, fx + 23, fy + 11, 2, 3, 170, 30, 30);
    } else {
        // Determined Doom smirk
        DS_DrawLine(fb, fx + 13, fy + 24, fx + 23, fy + 23, 100, 40, 40, fy, fy + 36);
    }
}

// Render the Live Automap onto the top portion of the bottom screen (Y: 0 to 175)
static void DS_DrawLiveAutomap(u8 *fb)
{
    // Clear Automap area (Y: 0 to 175) to deep radar blue/black
    DS_FillRect(fb, 0, 0, 320, 176, 12, 14, 18);

    player_t *plr = &players[consoleplayer];
    if (!plr || !plr->mo) return;

    fixed_t px = plr->mo->x;
    fixed_t py = plr->mo->y;

    int cx = 160;
    int cy = 88;

    // Grid markings (subtle)
    for (int gx = 40; gx < 320; gx += 40) {
        for (int gy = 20; gy < 176; gy += 40) {
            DS_DrawPixel(fb, gx, gy, 28, 32, 40);
        }
    }

    int shift = FRACBITS + ds_map_zoom;

    // Draw all mapped level lines
    for (int i = 0; i < numlines; i++) {
        line_t *ld = &lines[i];

        // Only draw seen lines (or if cheat/allmap enabled)
        if (!(ld->flags & ML_MAPPED) && !(plr->powers[pw_allmap]))
            continue;
        if (ld->flags & ML_DONTDRAW)
            continue;

        int x0 = cx + ((ld->v1->x - px) >> shift);
        int y0 = cy - ((ld->v1->y - py) >> shift);
        int x1 = cx + ((ld->v2->x - px) >> shift);
        int y1 = cy - ((ld->v2->y - py) >> shift);

        // Quick bounding box check against 320x176
        if ((x0 < -50 && x1 < -50) || (x0 > 370 && x1 > 370) ||
            (y0 < -50 && y1 < -50) || (y0 > 220 && y1 > 220))
            continue;

        u8 r = 180, g = 60, b = 60; // Default solid wall: Red

        if (ld->flags & ML_SECRET) {
            r = 200; g = 80; b = 240; // Secret pushwall: Purple
        } else if (ld->special) {
            // Door or interactive switch
            if (ld->special == 26 || ld->special == 32 || ld->special == 99 || ld->special == 133) {
                r = 50; g = 140; b = 255; // Blue key door
            } else if (ld->special == 27 || ld->special == 34 || ld->special == 135 || ld->special == 137) {
                r = 255; g = 215; b = 0;   // Yellow key door
            } else if (ld->special == 28 || ld->special == 33 || ld->special == 134 || ld->special == 136) {
                r = 255; g = 50; b = 50;   // Red key door
            } else {
                r = 255; g = 150; b = 40;  // Standard door / switch: Orange
            }
        } else if (ld->backsector != NULL) {
            r = 130; g = 110; b = 60; // Step / ledge / window: Yellow-Brown
        }

        DS_DrawLine(fb, x0, y0, x1, y1, r, g, b, 0, 175);
    }

    // Draw Player Arrow
    angle_t angle = plr->mo->angle;
    double rad = (double)angle * (3.14159265358979323846 / (double)ANG180);

    int tip_x = cx + (int)(10.0 * cos(rad));
    int tip_y = cy - (int)(10.0 * sin(rad));

    int l_x = cx + (int)(6.0 * cos(rad + 2.5));
    int l_y = cy - (int)(6.0 * sin(rad + 2.5));

    int r_x = cx + (int)(6.0 * cos(rad - 2.5));
    int r_y = cy - (int)(6.0 * sin(rad - 2.5));

    // Green player arrow
    DS_DrawLine(fb, cx, cy, tip_x, tip_y, 40, 255, 60, 0, 175);
    DS_DrawLine(fb, tip_x, tip_y, l_x, l_y, 40, 255, 60, 0, 175);
    DS_DrawLine(fb, tip_x, tip_y, r_x, r_y, 40, 255, 60, 0, 175);
    DS_DrawLine(fb, l_x, l_y, cx, cy, 40, 255, 60, 0, 175);

    // Zoom indicator on top right
    char zoom_buf[16];
    snprintf(zoom_buf, sizeof(zoom_buf), "ZOOM %dx", 10 - ds_map_zoom);
    DS_DrawString(fb, 260, 6, zoom_buf, 90, 120, 160, 1);
}

// Render the DualScreen Status Bar at the bottom (Y: 176 to 239)
static void DS_DrawStatusBar(u8 *fb)
{
    // High-tech metallic divider bar (Y: 176 to 178)
    DS_FillRect(fb, 0, 176, 320, 1, 45, 52, 65);
    DS_FillRect(fb, 0, 177, 320, 1, 95, 115, 140);
    DS_FillRect(fb, 0, 178, 320, 1, 25, 30, 40);

    // HUD base plate (Y: 179 to 239)
    DS_FillRect(fb, 0, 179, 320, 61, 20, 22, 28);

    player_t *plr = &players[consoleplayer];
    if (!plr) return;

    // === 1. HEALTH (Left box, X: 6 .. 68) ===
    DS_FillRect(fb, 6, 183, 64, 52, 28, 32, 40);
    DS_DrawString(fb, 12, 187, "HEALTH", 130, 145, 165, 1);

    int hp = plr->health;
    char hp_str[16];
    snprintf(hp_str, sizeof(hp_str), "%d%%", hp);

    u8 hr = 60, hg = 230, hb = 80; // Green
    if (hp <= 25) { hr = 240; hg = 40; hb = 40; } // Red
    else if (hp <= 50) { hr = 240; hg = 180; hb = 30; } // Yellow

    DS_DrawString(fb, 12, 200, hp_str, hr, hg, hb, 2);

    // Small health meter bar
    int hp_bar = (hp > 100 ? 100 : hp) * 52 / 100;
    if (hp_bar < 0) hp_bar = 0;
    DS_FillRect(fb, 12, 224, 52, 4, 35, 40, 50);
    DS_FillRect(fb, 12, 224, hp_bar, 4, hr, hg, hb);

    // === 2. ARMOR (Box X: 74 .. 136) ===
    DS_FillRect(fb, 74, 183, 64, 52, 28, 32, 40);
    DS_DrawString(fb, 82, 187, "ARMOR", 130, 145, 165, 1);

    int armor = plr->armorpoints;
    char arm_str[16];
    snprintf(arm_str, sizeof(arm_str), "%d%%", armor);

    u8 ar = 80, ag = 170, ab = 255; // Blue
    if (armor == 0) { ar = 100; ag = 105; ab = 115; }

    DS_DrawString(fb, 80, 200, arm_str, ar, ag, ab, 2);

    int arm_bar = (armor > 100 ? 100 : armor) * 52 / 100;
    if (arm_bar < 0) arm_bar = 0;
    DS_FillRect(fb, 80, 224, 52, 4, 35, 40, 50);
    DS_FillRect(fb, 80, 224, arm_bar, 4, ar, ag, ab);

    // === 3. DOOMGUY MUGSHOT (Center, X: 142 .. 178) ===
    bool godmode = (plr->cheats & CF_GODMODE) != 0;
    DS_DrawMugshot(fb, 160, 209, plr->health, plr->damagecount, godmode);

    // === 4. AMMO & WEAPON (Box X: 184 .. 268) ===
    DS_FillRect(fb, 184, 183, 86, 52, 28, 32, 40);

    const char *wname = DS_GetWeaponName(plr->readyweapon);
    DS_DrawString(fb, 190, 187, wname, 230, 180, 40, 1);

    ammotype_t amtype = weaponinfo[plr->readyweapon].ammo;
    if (amtype != am_noammo) {
        int cur_ammo = plr->ammo[amtype];
        int max_ammo = plr->maxammo[amtype];
        char ammo_str[16];
        snprintf(ammo_str, sizeof(ammo_str), "%d", cur_ammo);
        DS_DrawString(fb, 190, 200, ammo_str, 240, 210, 40, 2);

        char max_str[16];
        snprintf(max_str, sizeof(max_str), "/%d", max_ammo);
        DS_DrawString(fb, 234, 207, max_str, 120, 125, 135, 1);
    } else {
        DS_DrawString(fb, 190, 200, "---", 120, 125, 135, 2);
    }

    // === 5. KEYS (Right box, X: 274 .. 314) ===
    DS_FillRect(fb, 274, 183, 40, 52, 28, 32, 40);
    DS_DrawString(fb, 280, 187, "KEYS", 130, 145, 165, 1);

    // Blue card / skull
    if (plr->cards[it_bluecard] || plr->cards[it_blueskull]) {
        DS_FillRect(fb, 280, 200, 8, 8, 40, 120, 255);
        DS_DrawChar(fb, 282, 201, 'B', 255, 255, 255, 1);
    } else {
        DS_FillRect(fb, 280, 200, 8, 8, 40, 45, 55);
    }

    // Yellow card / skull
    if (plr->cards[it_yellowcard] || plr->cards[it_yellowskull]) {
        DS_FillRect(fb, 290, 200, 8, 8, 240, 200, 20);
        DS_DrawChar(fb, 292, 201, 'Y', 20, 20, 20, 1);
    } else {
        DS_FillRect(fb, 290, 200, 8, 8, 40, 45, 55);
    }

    // Red card / skull
    if (plr->cards[it_redcard] || plr->cards[it_redskull]) {
        DS_FillRect(fb, 300, 200, 8, 8, 240, 40, 40);
        DS_DrawChar(fb, 302, 201, 'R', 255, 255, 255, 1);
    } else {
        DS_FillRect(fb, 300, 200, 8, 8, 40, 45, 55);
    }
}

// Draw Dashboard when not in game level (Menus, Title, Intermission)
static void DS_DrawDashboard(u8 *fb)
{
    // Clean modern dashboard
    DS_FillRect(fb, 0, 0, 320, 240, 16, 18, 24);

    // Title banner
    DS_FillRect(fb, 0, 0, 320, 38, 30, 35, 46);
    DS_DrawString(fb, 45, 10, "BOOM3DS DUALSCREEN", 240, 60, 50, 2);
    DS_DrawString(fb, 120, 26, BOOM3DS_DUALSCREEN_VERSION_STR, 100, 180, 255, 1);

    // Status cards
    DS_FillRect(fb, 16, 50, 288, 55, 24, 28, 38);
    DS_DrawString(fb, 26, 58, "SYSTEM STATUS", 140, 160, 190, 1);
    DS_DrawString(fb, 26, 72, "New 3DS Speedup : Active (804 MHz)", 60, 220, 80, 1);
    DS_DrawString(fb, 26, 86, "Renderer Engine : GPU-OpenGL / Citro3D", 220, 220, 220, 1);

    // Controls overview
    DS_FillRect(fb, 16, 115, 288, 110, 24, 28, 38);
    DS_DrawString(fb, 26, 123, "CONTROLS GUIDE", 140, 160, 190, 1);
    DS_DrawString(fb, 26, 137, "Circle Pad : WASD Move & Strafe", 240, 200, 40, 1);
    DS_DrawString(fb, 26, 151, "C-Stick    : Camera Turn Left/Right", 240, 200, 40, 1);
    DS_DrawString(fb, 26, 165, "A / R / ZR : Attack / Fire", 220, 220, 220, 1);
    DS_DrawString(fb, 26, 179, "B          : Use / Open Doors", 220, 220, 220, 1);
    DS_DrawString(fb, 26, 193, "Touchscreen: Tap Automap to Zoom", 80, 180, 255, 1);
    DS_DrawString(fb, 26, 207, "Created by : Sheldor & Team", 120, 130, 150, 1);
}

void DS_Init(void)
{
    ds_map_zoom = 5;
    ds_map_follow = true;
}

void DS_HandleTouch(int touch_x, int touch_y)
{
    if (gamestate == GS_LEVEL) {
        // Tapping on the automap toggles zoom
        if (touch_y < 176) {
            ds_map_zoom--;
            if (ds_map_zoom < 3) {
                ds_map_zoom = 7; // Cycle zoom: 7 (far) -> 3 (close)
            }
        }
    }
}

void DS_UpdateBottomScreen(void)
{
    u8 *framebuf = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!framebuf) return;

    // Check touch input
    touchPosition touch;
    hidTouchRead(&touch);
    if (touch.px > 0 || touch.py > 0) {
        static bool was_touched = false;
        if (!was_touched) {
            DS_HandleTouch(touch.px, touch.py);
            was_touched = true;
        }
    } else {
        // Reset touch flag when released
    }

    if (gamestate == GS_LEVEL) {
        DS_DrawLiveAutomap(framebuf);
        DS_DrawStatusBar(framebuf);
    } else {
        DS_DrawDashboard(framebuf);
    }
}

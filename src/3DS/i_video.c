/* Emacs style mode select   -*- C -*-
 *-----------------------------------------------------------------------------
 *
 *  Trinity3DS: 3DS Video Subsystem for dsda-doom
 *
 *-----------------------------------------------------------------------------
 */

#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"
#include "d_event.h"
#include "d_main.h"
#include "v_video.h"
#include "i_video.h"
#include "i_system.h"
#include "i_main.h"
#include "lprintf.h"
#include "w_wad.h"
#include "r_draw.h"
#include "r_things.h"
#include "r_plane.h"
#include "r_main.h"
#include "r_state.h"
#include "m_fixed.h"
#include "f_wipe.h"
#include "st_stuff.h"
#include "am_map.h"
#include "3DS/ds_screen.h"
#include "3DS/i_joy.h"
#include "3DS/gl_wrapper.h"
#include "dsda/signal_context.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_Window *sdl_window = NULL;
SDL_Renderer *sdl_renderer = NULL;
SDL_Rect renderer_rect = {0, 0, 400, 240};
SDL_Rect window_rect = {0, 0, 400, 240};
SDL_Rect viewport_rect = {0, 0, 400, 240};
const char *screen_resolutions_list[] = {"400x240", NULL};
const char *sdl_video_window_pos = "";
int desired_fullscreen = 1;
int exclusive_fullscreen = 0;
int renderW = 400;
int renderH = 240;
dboolean window_focused = true;

u8 *s_right_eye_buffer = NULL;
bool s_stereo_rendered_this_frame = false;

static u8 s_current_palette[768];
static int s_active_palette_idx = -1;
static const u8 *s_playpal_data = NULL;

void I_SetPalette(int pal)
{
    if (pal >= 0) {
        s_active_palette_idx = pal;
    } else if (s_active_palette_idx >= 0) {
        pal = s_active_palette_idx;
    } else {
        pal = 0;
    }

    int lump = W_CheckNumForName("PLAYPAL");
    if (lump >= 0) {
        s_playpal_data = (const u8 *)W_LockLumpNum(lump);
        int max_len = W_LumpLength(lump);
        if (s_playpal_data && (pal * 768 + 768) <= max_len) {
            const u8 *raw_pal = s_playpal_data + (pal * 768);
            int g_lvl = usegamma;
            if (g_lvl < 0) g_lvl = 0;
            if (g_lvl > 4) g_lvl = 4;

            int gtlump = W_CheckNumForName2("GAMMATBL", ns_prboom);
            if (gtlump < 0)
                gtlump = W_CheckNumForName("GAMMATBL");

            if (gtlump >= 0 && g_lvl > 0) {
                const u8 *gtbl = (const u8 *)W_LockLumpNum(gtlump) + (g_lvl * 256);
                for (int i = 0; i < 768; i++) {
                    s_current_palette[i] = gtbl[raw_pal[i]];
                }
            } else {
                memcpy(s_current_palette, raw_pal, 768);
            }
        }
    }
}

void I_PreInitGraphics(void)
{
    gfxInitDefault();
    gfxSet3D(true);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);

    I_InitJoystick();

    I_AtExit(I_ShutdownGraphics, true, "I_ShutdownGraphics", exit_priority_normal);
    I_AtExit(I_ShutdownJoystick, true, "I_ShutdownJoystick", exit_priority_normal);
}

static void I_InitBuffersRes(void)
{
    R_InitMeltRes();
    R_InitSpritesRes();
    R_InitBuffersRes();
    R_InitPlanesRes();
    R_InitVisplanesRes();
}

void I_InitScreenResolution(void)
{
    SCREENWIDTH = 400;
    SCREENHEIGHT = 240;
    SCREENPITCH = 400;

    V_InitMode(VID_MODESW);

    for (int i = 0; i < NUM_SCREENS; i++) {
        screens[i].width = 400;
        screens[i].height = 240;
        screens[i].pitch = 400;
        screens[i].not_on_heap = false;
    }

    I_InitBuffersRes();

    if (!screens[0].data) {
        V_AllocScreens();
    }
    if (!s_right_eye_buffer) {
        s_right_eye_buffer = malloc(400 * 240);
    }
}

void I_UpdateVideoMode(void)
{
    I_InitScreenResolution();

    R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);

    R_ExecuteSetViewSize();

    I_SetPalette(0);

    ST_SetResolution();
    AM_SetResolution();
}

void I_InitGraphics(void)
{
    static int firsttime = 1;

    if (firsttime) {
        firsttime = 0;
        I_UpdateVideoMode();
    }
}

void I_ShutdownGraphics(void)
{
    if (s_right_eye_buffer) {
        free(s_right_eye_buffer);
        s_right_eye_buffer = NULL;
    }
    gfxExit();
}

void *I_GetSDLWindow(void) { return NULL; }
void *I_GetSDLRenderer(void) { return NULL; }
void I_SetWindowCaption(void) {}
void I_SetWindowIcon(void) {}
void I_QueueFrameCapture(void) {}
void I_QueueScreenshot(void) {}
void I_HandleCapture(void) {}
int I_ScreenShot(const char *fname) { (void)fname; return 0; }
unsigned char *I_GrabScreen(void) { return screens[0].data; }
dboolean I_WindowFocused(void) { return true; }
void UpdateGrab(void) {}
void I_SetWindowRect(void) {}
void I_SetViewportRect(void) {}
void I_UpdateRenderSize(void) {}
void I_StartFrame(void) {}

void I_StartTic(void)
{
    I_PollJoystick();

    // Check touch on bottom screen
    touchPosition touch;
    hidTouchRead(&touch);
    u32 kHeld = hidKeysHeld();

    if (gamestate == GS_LEVEL) {
        static bool was_touched = false;
        if (kHeld & KEY_TOUCH) {
            if (!was_touched && (touch.px > 0 || touch.py > 0)) {
                DS_HandleTouch(touch.px, touch.py);
                was_touched = true;
            }
        } else {
            was_touched = false;
        }
    }
}

static void I_BlitTopEye(u8 *dst, const u8 *src)
{
    if (!dst || !src) return;

    // 3DS top screen framebuffer is rotated 90 degrees counter-clockwise
    // Framebuffer: 240 columns of 400 pixels each, format BGR8 (3 bytes/pixel)
    for (int y = 0; y < 240; y++) {
        const u8 *src_row = &src[y * 400];
        int dst_col_offset = (239 - y) * 3;
        for (int x = 0; x < 400; x++) {
            u8 pal_idx = src_row[x];
            const u8 *rgb = &s_current_palette[pal_idx * 3];

            int dst_idx = x * (240 * 3) + dst_col_offset;
            dst[dst_idx + 0] = rgb[2]; // Blue
            dst[dst_idx + 1] = rgb[1]; // Green
            dst[dst_idx + 2] = rgb[0]; // Red
        }
    }
}

void I_3D_RenderPlayerView(player_t *player, float slider)
{
    if (!s_right_eye_buffer)
        return;

    fixed_t orig_viewx = viewx;
    fixed_t orig_viewy = viewy;
    angle_t orig_viewangle = viewangle;

    // Eye separation in Doom map units:
    // 2.2 map units per eye gives a clear, immersive, and vivid 3D depth effect
    // with parallel cameras (NO divergence, NO keystone distortion).
    // The player's weapon and HUD stay rock-solid at disparity 0 with zero flicker!
    fixed_t eye_offset = (fixed_t)(FRACUNIT * (2.2f * slider));

    // Perpendicular vector pointing Right in Doom's counter-clockwise coordinate system:
    // viewangle = 0 (East, +X) -> Right is South (-Y): dx = 0, dy = -FRACUNIT
    // viewangle = 90 (North, +Y) -> Right is East (+X): dx = +FRACUNIT, dy = 0
    // Vector pointing Right: (sin(viewangle), -cos(viewangle))
    angle_t ang = orig_viewangle >> ANGLETOFINESHIFT;
    fixed_t dx = FixedMul(eye_offset, finesine[ang]);
    fixed_t dy = FixedMul(eye_offset, finecosine[ang]);

    // === PASS 1: LEFT EYE ===
    // Left eye is shifted LEFT (-dx, +dy).
    // Parallel cameras: viewangle is identical to player angle (NO ROTATION/TOE-IN!)
    viewx = orig_viewx - dx;
    viewy = orig_viewy + dy;
    viewangle = orig_viewangle;

    DSDA_ADD_CONTEXT(sf_player_view);
    R_RenderPlayerView(player);
    DSDA_REMOVE_CONTEXT(sf_player_view);

    // === PASS 2: RIGHT EYE ===
    // Right eye is shifted RIGHT (+dx, -dy).
    u8 *orig_screen = screens[0].data;
    screens[0].data = s_right_eye_buffer;
    drawvars.topleft = s_right_eye_buffer;

    viewx = orig_viewx + dx;
    viewy = orig_viewy - dy;
    viewangle = orig_viewangle;

    DSDA_ADD_CONTEXT(sf_player_view);
    R_RenderPlayerView(player);
    DSDA_REMOVE_CONTEXT(sf_player_view);

    // Restore original camera & screen
    screens[0].data = orig_screen;
    drawvars.topleft = orig_screen;
    viewx = orig_viewx;
    viewy = orig_viewy;
    viewangle = orig_viewangle;

    s_stereo_rendered_this_frame = true;
}

void I_FinishUpdate(void)
{
    // 1. Draw bottom tactical dashboard
    DS_UpdateBottomScreen();

    // 2. Render top screen (400x240) in Software mode
    u8 *dst_l = (u8 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    u8 *dst_r = (u8 *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);

    float slider = osGet3DSliderState();

    if (slider > 0.01f && s_stereo_rendered_this_frame && s_right_eye_buffer) {
        // True stereoscopic 3D: Left eye gets screens[0], Right eye gets s_right_eye_buffer
        I_BlitTopEye(dst_l, screens[0].data);
        I_BlitTopEye(dst_r, s_right_eye_buffer);
    } else {
        // 2D mode (or menus / intermission): both eyes get screens[0]
        I_BlitTopEye(dst_l, screens[0].data);
        if (dst_r) {
            I_BlitTopEye(dst_r, screens[0].data);
        }
    }

    s_stereo_rendered_this_frame = false;

    // 3. Flush buffers, swap and wait for VBlank
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

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
#include "3DS/ds_screen.h"
#include "3DS/i_joy.h"
#include "3DS/gl_wrapper.h"

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

static u8 s_current_palette[768];
static int s_active_palette_idx = -1;
static const u8 *s_playpal_data = NULL;

void I_SetPalette(int pal)
{
    s_active_palette_idx = pal;

    int lump = W_CheckNumForName("PLAYPAL");
    if (lump >= 0) {
        s_playpal_data = (const u8 *)W_LockLumpNum(lump);
        int max_len = W_LumpLength(lump);
        if (s_playpal_data && pal >= 0 && (pal * 768 + 768) <= max_len) {
            memcpy(s_current_palette, s_playpal_data + (pal * 768), 768);
        }
    }
}

void I_PreInitGraphics(void)
{
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);

    I_AtExit(I_ShutdownGraphics, true, "I_ShutdownGraphics", exit_priority_normal);
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
    }
}

void I_InitGraphics(void)
{
    I_SetPalette(0);
}

void I_UpdateVideoMode(void)
{
    I_InitScreenResolution();
}

void I_ShutdownGraphics(void)
{
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

void I_FinishUpdate(void)
{
    // 1. Draw bottom tactical dashboard
    DS_UpdateBottomScreen();

    // 2. Render top screen (400x240) in Software mode
    u8 *dst = (u8 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    const u8 *src = screens[0].data;

    if (dst && src) {
        // 3DS top screen framebuffer is rotated 90 degrees counter-clockwise
        // Framebuffer: 240 columns of 400 pixels each, format BGR8 (3 bytes/pixel)
        for (int y = 0; y < 240; y++) {
            for (int x = 0; x < 400; x++) {
                u8 pal_idx = src[y * 400 + x];
                const u8 *rgb = &s_current_palette[pal_idx * 3];

                int dst_idx = (x * 240 + (239 - y)) * 3;
                dst[dst_idx + 0] = rgb[2]; // Blue
                dst[dst_idx + 1] = rgb[1]; // Green
                dst[dst_idx + 2] = rgb[0]; // Red
            }
        }
    }

    // 3. Flush buffers, swap and wait for VBlank
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

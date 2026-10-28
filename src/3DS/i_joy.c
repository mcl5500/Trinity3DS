/* Emacs style mode select   -*- C -*-
 *-----------------------------------------------------------------------------
 *
 *  Trinity3DS: Native 3DS Input Subsystem
 *
 *-----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"
#include "d_event.h"
#include "d_main.h"
#include "i_joy.h"
#include "lprintf.h"
#include "i_system.h"
#include "i_main.h"

#include <3ds.h>

int joyxmove = 0;
int joyymove = 0;
int joyrxmove = 0;
int joyrymove = 0;
u32 ctr_keys_held = 0;
u32 ctr_keys_down = 0;

static bool irrst_initialized = false;

void I_InitJoystick(void)
{
  if (!irrst_initialized) {
    Result res = irrstInit();
    if (R_SUCCEEDED(res)) {
      irrst_initialized = true;
      lprintf(LO_INFO, "Trinity3DS: IRRST (C-Stick) initialized successfully.\n");
    } else {
      lprintf(LO_WARN, "Trinity3DS: irrstInit failed (code 0x%08lX)\n", res);
    }
  }
}

void I_ShutdownJoystick(void)
{
  if (irrst_initialized) {
    irrstExit();
    irrst_initialized = false;
  }
}

void I_ResetToLauncher(void);

void I_PollJoystick(void)
{
  if (!irrst_initialized) {
    I_InitJoystick();
  }

  hidScanInput();

  ctr_keys_held = hidKeysHeld();
  ctr_keys_down = hidKeysDown();

  // Soft-reset hotkey: Hold L + R + START + SELECT for ~1.5s (90 ticks) -> Soft-reset to WAD manager
  const unsigned int reset_combo = KEY_L | KEY_R | KEY_START | KEY_SELECT;
  static int reset_counter = 0;
  if ((ctr_keys_held & reset_combo) == reset_combo) {
    reset_counter++;
    if (reset_counter >= 90) {
      I_ResetToLauncher();
      return;
    }
  } else {
    reset_counter = 0;
  }

  // Circle Pad: Read analog stick
  circlePosition cpos;
  hidCircleRead(&cpos);

  // Circle Pad X (Strafe) - normalized to -10 .. +10
  if (abs(cpos.dx) > 15) {
    joyxmove = (cpos.dx * 192) / 3000;
    if (joyxmove == 0) joyxmove = (cpos.dx > 0) ? 1 : -1;
  } else {
    joyxmove = 0;
  }

  // Circle Pad Y (Walk forward/backward) - normalized to -10 .. +10
  if (abs(cpos.dy) > 15) {
    joyymove = (cpos.dy * 192) / 3000;
    if (joyymove == 0) joyymove = (cpos.dy > 0) ? 1 : -1;
  } else {
    joyymove = 0;
  }

  // C-Stick (New 3DS / CPP): Read analog camera turning & freelook
  if (irrst_initialized) {
    irrstScanInput();

    circlePosition cstickpos = {0, 0};
    irrstCstickRead(&cstickpos);

    if (abs(cstickpos.dx) > 12) {
      joyrxmove = (cstickpos.dx * 192) / 3000;
      if (joyrxmove == 0) joyrxmove = (cstickpos.dx > 0) ? 1 : -1;
    } else {
      joyrxmove = 0;
    }

    if (abs(cstickpos.dy) > 12) {
      joyrymove = (cstickpos.dy * 192) / 3000;
      if (joyrymove == 0) joyrymove = (cstickpos.dy > 0) ? 1 : -1;
    } else {
      joyrymove = 0;
    }
  } else {
    joyrxmove = joyrymove = 0;
  }

  // START: Toggle Menu / Pause (KEYD_ESCAPE)
  if (ctr_keys_down & KEY_START) {
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ev_keydown;
    ev.data1.i = KEYD_ESCAPE;
    D_PostEvent(&ev);
    ev.type = ev_keyup;
    D_PostEvent(&ev);
  }

  // SELECT: Toggle Automap (KEYD_TAB)
  if (ctr_keys_down & KEY_SELECT) {
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ev_keydown;
    ev.data1.i = KEYD_TAB;
    D_PostEvent(&ev);
    ev.type = ev_keyup;
    D_PostEvent(&ev);
  }

  // Menu navigation when menu is open or on title/intermission screens
  extern dboolean menuactive;
  if (menuactive || gamestate != GS_LEVEL) {
    static u64 nav_wait = 0;
    u64 now = osGetTime();

    if (now > nav_wait) {
      int nav_key = 0;
      if ((ctr_keys_held & KEY_DUP) || joyymove > 4) {
        nav_key = KEYD_UPARROW;
      } else if ((ctr_keys_held & KEY_DDOWN) || joyymove < -4) {
        nav_key = KEYD_DOWNARROW;
      } else if ((ctr_keys_held & KEY_DLEFT) || joyxmove < -4) {
        nav_key = KEYD_LEFTARROW;
      } else if ((ctr_keys_held & KEY_DRIGHT) || joyxmove > 4) {
        nav_key = KEYD_RIGHTARROW;
      }

      if (nav_key) {
        event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = ev_keydown;
        ev.data1.i = nav_key;
        D_PostEvent(&ev);
        ev.type = ev_keyup;
        D_PostEvent(&ev);
        nav_wait = now + 160;
      }
    }

    if (ctr_keys_down & KEY_A) {
      event_t ev;
      memset(&ev, 0, sizeof(ev));
      ev.type = ev_keydown;
      ev.data1.i = KEYD_ENTER;
      D_PostEvent(&ev);
      ev.type = ev_keyup;
      D_PostEvent(&ev);
    }
    if (ctr_keys_down & KEY_B) {
      event_t ev;
      memset(&ev, 0, sizeof(ev));
      ev.type = ev_keydown;
      ev.data1.i = KEYD_BACKSPACE;
      D_PostEvent(&ev);
      ev.type = ev_keyup;
      D_PostEvent(&ev);
    }
  }
}

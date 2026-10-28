/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *   Joystick handling for Linux
 *
 *-----------------------------------------------------------------------------
 */

#ifndef lint
#endif /* lint */

#include <stdlib.h>

#include "doomdef.h"
#include "doomtype.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_joy.h"
#include "lprintf.h"
#include "i_system.h"
#include "i_main.h"

#include <3ds.h>

extern int joybfire;
extern int joybuse;
extern int joybspeed;
extern int joybnextweapon;

static bool irrst_initialized = false;

void I_InitJoystick(void)
{
  joybfire = 0;
  joybuse = 1;
  joybnextweapon = 2;
  joybspeed = 3;

  if (!irrst_initialized) {
    if (R_SUCCEEDED(irrstInit())) {
      irrst_initialized = true;
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
  event_t ev;
  memset(&ev, 0, sizeof(ev));

  ev.type = ev_joystick;
  ev.data1.i = 0;
  ev.data2.i = 0;

  hidScanInput();

  unsigned int kHeld = hidKeysHeld();

  // Soft-reset hotkey: Hold L + R + START + SELECT for ~1.5s (90 ticks) -> Soft-reset to WAD manager
  const unsigned int reset_combo = KEY_L | KEY_R | KEY_START | KEY_SELECT;
  static int reset_counter = 0;
  if ((kHeld & reset_combo) == reset_combo) {
    reset_counter++;
    if (reset_counter >= 90) {
      I_ResetToLauncher();
      return;
    }
  } else {
    reset_counter = 0;
  }

  // A, R, ZR: Fire (Button 0)
  if (kHeld & (KEY_A | KEY_R | KEY_ZR))
    ev.data1.i |= (1 << 0);
  // B, Y: Use / Open Doors & Switches (Button 1)
  if (kHeld & (KEY_B | KEY_Y))
    ev.data1.i |= (1 << 1);
  // X: Next Weapon (Button 2)
  if (kHeld & KEY_X)
    ev.data1.i |= (1 << 2);
  // L, ZL: Sprint / Speed (Button 3)
  if (kHeld & (KEY_L | KEY_ZL))
    ev.data1.i |= (1 << 3);
  if (kHeld & KEY_START)
    ev.data1.i |= (1 << 6);
  if (kHeld & KEY_SELECT)
    ev.data1.i |= (1 << 7);
  if (kHeld & KEY_DUP)
    ev.data1.i |= (1 << 8);
  if (kHeld & KEY_DDOWN)
    ev.data1.i |= (1 << 9);
  if (kHeld & KEY_DLEFT)
    ev.data1.i |= (1 << 10);
  if (kHeld & KEY_DRIGHT)
    ev.data1.i |= (1 << 11);

  D_PostEvent(&ev);

  // Circle Pad (Analog Movement)
  circlePosition cpos;
  hidCircleRead(&cpos);
  if (abs(cpos.dx) > 15 || abs(cpos.dy) > 15) {
    event_t move_ev;
    memset(&move_ev, 0, sizeof(move_ev));
    move_ev.type = ev_move_analog;
    move_ev.data1.f = (float)cpos.dx / 156.0f;
    move_ev.data2.f = (float)cpos.dy / 156.0f;
    D_PostEvent(&move_ev);
  } else {
    event_t move_ev;
    memset(&move_ev, 0, sizeof(move_ev));
    move_ev.type = ev_move_analog;
    move_ev.data1.f = 0.0f;
    move_ev.data2.f = 0.0f;
    D_PostEvent(&move_ev);
  }

  // C-Stick (Camera Turning / Free Look)
  if (irrst_initialized) {
    circlePosition cstickpos = {0, 0};
    irrstCstickRead(&cstickpos);
    if (abs(cstickpos.dx) > 15 || abs(cstickpos.dy) > 15) {
      event_t look_ev;
      memset(&look_ev, 0, sizeof(look_ev));
      look_ev.type = ev_look_analog;
      look_ev.data1.f = (float)cstickpos.dx / 156.0f;
      look_ev.data2.f = (float)cstickpos.dy / 156.0f;
      D_PostEvent(&look_ev);
    }
  }
}

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
 *  Provides a logical console output routine that allows what is
 *  output to console normally and when output is redirected to
 *  be controlled..
 *
 *-----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "doomtype.h"
#include "lprintf.h"
#include "i_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "e6y.h"//e6y

int cons_error_mask = -1-LO_INFO; /* all but LO_INFO when redir'd */
int cons_output_mask = -1;        /* all output enabled */

/* cphipps - enlarged message buffer and made non-static
 * We still have to be careful here, this function can be called after exit
 */
#define MAX_MESSAGE_SIZE 2048

int lprintf(OutputLevels pri, const char *s, ...)
{
  int r=0;
  char msg[MAX_MESSAGE_SIZE];
  int lvl=pri;

  va_list v;
  va_start(v,s);
  doom_vsnprintf(msg,sizeof(msg),s,v);    /* print message in buffer  */
  va_end(v);

#ifdef __3DS__
  FILE *logfile = fopen("sdmc:/3ds/Trinity3DS/log.txt", "a");
  if (!logfile) {
    logfile = fopen("/3ds/Trinity3DS/log.txt", "a");
  }

  if (logfile)
  {
    if (lvl&cons_output_mask)               /* mask output as specified */
    {
      r=fprintf(logfile,"%s",msg);
    }
    if (!isatty(1) && lvl&cons_error_mask)  /* if stdout redirected     */
      r=fprintf(logfile,"%s",msg);           /* select output at console */

    fclose(logfile);
  }
#else
  if (lvl&cons_output_mask)               /* mask output as specified */
  {
    // do not crash with unicode dirs
    if (fileno(stdout) != -1)

    r=fprintf(stdout,"%s",msg);
  }
  if (!isatty(1) && lvl&cons_error_mask)  /* if stdout redirected     */
    r=fprintf(stderr,"%s",msg);           /* select output at console */
#endif

  return r;
}

/*
 * I_Error
 *
 * cphipps - moved out of i_* headers, to minimise source files that depend on
 * the low-level headers. All this does is print the error, then call the
 * low-level safe exit function.
 * killough 3/20/98: add const
 */

#ifdef __3DS__
#include <3ds.h>
#include "3DS/wad_picker.h"

static void CTR_DisplayFatalError(const char *msg)
{
  for (int t = 0; t < 600 && aptMainLoop(); t++) {
    hidScanInput();
    u32 kd = hidKeysDown();
    if (t > 30 && (kd & (KEY_A | KEY_B | KEY_START | KEY_TOUCH))) {
      break;
    }

    u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (bot_fb) {
      WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 16, 16, 20);
      WP_FillRect(bot_fb, 320, 10, 20, 300, 200, 35, 15, 18);
      WP_DrawRect(bot_fb, 320, 10, 20, 300, 200, 255, 60, 60);

      WP_FillRect(bot_fb, 320, 10, 20, 300, 28, 140, 20, 25);
      WP_DrawCenteredText(bot_fb, 320, 10, 20, 300, 28, "KRITISCHER DOOM-FEHLER", 255, 255, 255, 1);

      char lines[8][64];
      int nlines = 0;
      if (msg) {
        const char *p = msg;
        while (*p && nlines < 6) {
          while (*p == ' ' || *p == '\n' || *p == '\r') p++;
          if (!*p) break;
          int len = 0;
          while (p[len] && p[len] != '\n' && p[len] != '\r' && len < 38) len++;
          strncpy(lines[nlines], p, len);
          lines[nlines][len] = '\0';
          nlines++;
          p += len;
        }
      }

      for (int i = 0; i < nlines; i++) {
        WP_DrawText(bot_fb, 320, 20, 60 + i * 18, lines[i], 255, 220, 220, 1);
      }

      WP_DrawCenteredText(bot_fb, 320, 10, 185, 300, 20, "Druecke [A] oder [B] zum Beenden...", 255, 215, 0, 1);
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
  }
}
#endif

void I_Error(const char *error, ...)
{
  char errmsg[MAX_MESSAGE_SIZE];
  va_list argptr;
  va_start(argptr,error);
  doom_vsnprintf(errmsg,sizeof(errmsg),error,argptr);
  va_end(argptr);
  lprintf(LO_ERROR, "%s\n", errmsg);

#ifdef __3DS__
  CTR_DisplayFatalError(errmsg);
#endif

  if (!M_CheckParm ("-nodraw")) {
    I_MessageBox(errmsg, PRB_MB_OK);
  }

  I_SafeExit(-1);
}

// Attempt to compensate for lack of va_copy

#ifndef va_copy
# ifdef __va_copy
#  define va_copy(a,b) __va_copy(a,b)
# else
#  define va_copy(a,b) ((a)=(b))
# endif
#endif

// Wrapper to handle non-standard stdio implementations

int doom_vsnprintf(char *buf, size_t max, const char *fmt, va_list va)
{
  int rv;
  va_list vc;

  assert((max == 0 && buf == NULL) || (max != 0 && buf != NULL));
  assert(fmt != NULL);

  va_copy(vc, va);
  rv = vsnprintf(buf, max, fmt, vc);
  va_end(vc);

  if (rv < 0) // Handle an unhelpful return value.
  {
    // write into a scratch buffer that keeps growing until the output fits
    static char *backbuffer;
    static size_t backsize = 1024;

    for (; rv < 0; backsize *= 2)
    {
      if (backsize <= max) continue;

      backbuffer = (realloc)(backbuffer, backsize);
      assert(backbuffer != NULL);

      va_copy(vc, va);
      rv = vsnprintf(backbuffer, backsize, fmt, vc);
      va_end(vc);
    }

    if (buf)
    {
      size_t end = (size_t) rv >= max ? max-1 : rv;
      memmove(buf, backbuffer, end);
      buf[end] = '\0';
    }
  }

  if (buf && (size_t) rv >= max && buf[max-1]) // ensure null-termination
    buf[max-1] = '\0';

  return rv;
}

int doom_snprintf(char *buf, size_t max, const char *fmt, ...)
{
  int rv;
  va_list va;

  va_start(va, fmt);
  rv = doom_vsnprintf(buf, max, fmt, va);
  va_end(va);

  return rv;
}

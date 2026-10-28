/* Emacs style mode select   -*- C -*-
 *-----------------------------------------------------------------------------
 *
 *  Trinity3DS: Port of dsda-doom for Nintendo 3DS
 *
 *-----------------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef __3DS__
#include <3ds.h>
#include "3DS/ds_screen.h"
#include "3DS/wad_picker.h"

// Override libctru's default 50/50 linear heap split.
// Trinity3DS uses software rendering and only requires ~2 MB for display and audio buffers.
// Capping the linear heap at 6 MB maximizes the standard application heap (malloc),
// providing ~60 MB on Old 3DS and ~110 MB on New 3DS, which prevents out-of-memory
// crashes (Z_Malloc failure) in large games like Hexen.
u32 __ctru_linear_heap_size = 6 * 1024 * 1024;
#endif

#include <errno.h>

#include "doomdef.h"
#include "d_main.h"
#include "m_fixed.h"
#include "i_system.h"
#include "i_video.h"
#include "z_zone.h"
#include "lprintf.h"
#include "m_random.h"
#include "doomstat.h"
#include "g_game.h"
#include "m_misc.h"
#include "i_sound.h"
#include "i_main.h"
#include "r_fps.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include "e6y.h"

#include "dsda.h"
#include "dsda/args.h"
#include "dsda/analysis.h"
#include "dsda/endoom.h"
#include "dsda/settings.h"
#include "dsda/signal_context.h"
#include "dsda/split_tracker.h"
#include "dsda/text_file.h"
#include "dsda/time.h"
#include "dsda/wad_stats.h"
#include "dsda/zipfile.h"

void I_Init(void)
{
  dsda_ResetTimeFunctions(fastdemo);
  I_InitSound();
}

void I_Init2(void)
{
  dsda_ResetTimeFunctions(fastdemo);
  force_singletics_to = gametic + BACKUPTICS;
}

int signal_context;

static volatile sig_atomic_t interrupted = 0;

dboolean I_Interrupted(void)
{
  return interrupted;
}

static void I_SignalHandler(int s)
{
  char buf[2048];
  signal(s, SIG_IGN);
  I_SigString(buf, sizeof(buf), s);
  I_Error("The game has crashed!\n"
          "Please report the following information: %s (0x%04x)",
          buf, signal_context);
}

static void I_IntHandler(int s)
{
  (void)s;
  interrupted = 1;
}

static void PrintVer(void)
{
  char vbuf[200];
  lprintf(LO_INFO, "%s\n", I_GetVersionString(vbuf, 200));
}

typedef struct atexit_listentry_s atexit_listentry_t;

struct atexit_listentry_s
{
    atexit_func_t func;
    dboolean run_on_error;
    atexit_listentry_t *next;
    const char* name;
};

static atexit_listentry_t *exit_funcs[exit_priority_max];
static int exit_priority;

void I_AtExit(atexit_func_t func, dboolean run_on_error,
              const char* name, exit_priority_t priority)
{
    atexit_listentry_t *entry;

    entry = Z_Malloc(sizeof(*entry));

    entry->func = func;
    entry->run_on_error = run_on_error;
    entry->next = exit_funcs[priority];
    entry->name = name;
    exit_funcs[priority] = entry;
}

void I_SafeExit(int rc)
{
  atexit_listentry_t *entry;

  lprintf(LO_DEBUG, "\n");

  for (; exit_priority < exit_priority_max; ++exit_priority)
  {
    while ((entry = exit_funcs[exit_priority]))
    {
      exit_funcs[exit_priority] = exit_funcs[exit_priority]->next;

      if (rc == 0 || entry->run_on_error)
      {
        lprintf(LO_DEBUG, "Exit Sequence[%d]: %s (%d)\n", exit_priority, entry->name, rc);
        entry->func();
      }
    }
  }

  exit(rc);
}

#ifdef __3DS__
void I_ResetToLauncher(void)
{
  unlink("sdmc:/3ds/Trinity3DS/autostart.flag");
  aptSetChainloaderToSelf();
  I_SafeExit(0);
}

static void CTR_CleanShutdown(void)
{
  I_SafeExit(0);
}
#endif

static void I_EssentialQuit (void)
{
  if (demorecording)
  {
    G_CheckDemoStatus();
  }
  dsda_ExportTextFile();
  dsda_WriteAnalysis();
  dsda_WriteSplits();
  dsda_SaveWadStats();
  dsda_CacheEndoom();
  W_Shutdown();
  dsda_CleanZipTempDirs();
}

static void I_Quit (void)
{
  M_SaveDefaults ();
  dsda_DumpEndoom();
}

void I_SetProcessPriority(void) {}

void I_InitMouse(void) {}
int mouse_hide_timer = 0;

int main(int argc, char **argv)
{
#ifdef __3DS__
  static char *dummy_argv[] = {"sdmc:/3ds/Trinity3DS/Trinity3DS.3dsx", NULL};
  if (argc == 0 || !argv || !argv[0]) {
    argc = 1;
    argv = dummy_argv;
  }

  bool isN3DS = false;
  APT_CheckNew3DS(&isN3DS);
  if (isN3DS)
    osSetSpeedupEnable(true);

  lprintf(LO_INFO, "3DS Memory: Linear Heap = %lu MB, App Heap = %lu MB, Free Region = %lu MB\n",
          (unsigned long)(__ctru_linear_heap_size / (1024 * 1024)),
          (unsigned long)(envGetHeapSize() / (1024 * 1024)),
          (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / (1024 * 1024)));
#endif

  dsda_ParseCommandLineArgs(argc, argv);

  if (dsda_Flag(dsda_arg_verbose))
    I_EnableVerboseLogging();

  if (dsda_Flag(dsda_arg_quiet))
    I_DisableAllLogging();

  if (dsda_Flag(dsda_arg_v))
  {
    PrintVer();
    return 0;
  }

  ParamsMatchingCheck();

  lprintf(LO_DEBUG, "M_LoadDefaults: Load system defaults.\n");
  M_LoadDefaults();
  lprintf(LO_DEBUG, "\n");

  setlocale(LC_TIME, "");
  PrintVer();

  I_AtExit(I_EssentialQuit, true, "I_EssentialQuit", exit_priority_first);
  I_AtExit(I_Quit, false, "I_Quit", exit_priority_quit);

#ifndef PRBOOM_DEBUG
  if (!dsda_Flag(dsda_arg_sigsegv))
  {
    signal(SIGSEGV, I_SignalHandler);
  }
  signal(SIGFPE,  I_SignalHandler);
  signal(SIGILL,  I_SignalHandler);
  signal(SIGABRT, I_SignalHandler);

  signal(SIGTERM, I_IntHandler);
  signal(SIGINT,  I_IntHandler);
#endif

#ifdef __3DS__
  atexit(CTR_CleanShutdown);
#endif

  I_PreInitGraphics();

  D_DoomMain ();
  return 0;
}

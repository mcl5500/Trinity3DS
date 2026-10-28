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

extern char* fake_heap_start;
extern char* fake_heap_end;
extern void mappableInit(u32 start, u32 end);

u32 __ctru_heap = 0;
u32 __ctru_heap_size = 0;
u32 __ctru_linear_heap = 0;
u32 __ctru_linear_heap_size = 0;

void __system_allocateHeaps(void)
{
    // Query available memory from kernel resource limit
    Handle resLimit = 0;
    if (R_FAILED(svcGetResourceLimit(&resLimit, 0xFFFF8001))) {
        svcBreak(USERBREAK_PANIC);
        return;
    }

    s64 maxCommit = 0, currentCommit = 0;
    ResourceLimitType resType = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&maxCommit, resLimit, &resType, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, resLimit, &resType, 1);
    svcCloseHandle(resLimit);

    u32 total_free = (u32)(maxCommit - currentCommit);
    total_free &= ~0xFFF; // 4KB page align

    // Ensure at least 8 MB for linear heap (framebuffers, sound, etc.)
    u32 min_linear = 8 * 1024 * 1024;
    // 56 MB cap for app heap fits safely within 0x08000000 virtual address range (max 64 MB)
    u32 max_app_heap = 56 * 1024 * 1024;

    u32 app_heap = (total_free > min_linear) ? (total_free - min_linear) : (total_free / 2);
    if (app_heap > max_app_heap)
        app_heap = max_app_heap;

    u32 linear_heap = total_free - app_heap;

    __ctru_heap_size = app_heap;
    __ctru_linear_heap_size = linear_heap;

    // Allocate application heap (0x08000000)
    Result res = svcControlMemory(&__ctru_heap, 0x08000000, 0x0, __ctru_heap_size, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE);
    if (R_FAILED(res)) {
        svcBreak(USERBREAK_PANIC);
        return;
    }

    // Allocate linear heap
    res = svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE);
    if (R_FAILED(res)) {
        svcBreak(USERBREAK_PANIC);
        return;
    }

    // Set up newlib sbrk heap
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;

    // Initialize mappable address range
    mappableInit(0x10000000, 0x14000000);
}
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

#include "wad_downloader.h"
#include "wad_picker.h"
#include <3ds.h>
#include <curl/curl.h>
#include <minizip/unzip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>
#include <ctype.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *s_soc_buffer = NULL;
static bool s_net_inited = false;

static bool Downloader_InitNetwork(void)
{
    if (s_net_inited) return true;
    s_soc_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!s_soc_buffer) return false;

    Result res = socInit(s_soc_buffer, SOC_BUFFERSIZE);
    if (R_FAILED(res)) {
        free(s_soc_buffer);
        s_soc_buffer = NULL;
        return false;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    s_net_inited = true;
    return true;
}

static void Downloader_ExitNetwork(void)
{
    if (s_net_inited) {
        curl_global_cleanup();
        socExit();
        if (s_soc_buffer) {
            free(s_soc_buffer);
            s_soc_buffer = NULL;
        }
        s_net_inited = false;
    }
}

typedef struct {
    char title[64];
    char author[64];
    char category[32];   // "STANDALONE", "MEGAWAD", "EPISODE"
    char base_iwad[32];  // e.g. "DOOM2.WAD", "DOOM.WAD", "KEINE"
    char desc1[64];
    char desc2[64];
    char url[256];
    char target_file[64];// Primary wad to check for installation
    char size_str[32];
    bool is_zip;
    bool is_iwad;
} repo_item_t;

// Comprehensive alphabetical catalog of Doom community masterpieces & standalone games
static const repo_item_t s_catalog[] = {
    {
        "2002 A DOOM ODYSSEY",
        "Paul Corfiatis",
        "MEGAWAD",
        "DOOM.WAD",
        "Meisterhafte 4-Episoden Hommage an Ultimate Doom.",
        "Klassischer 1993-Stil mit fantastischer Atmosphaere.",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/2002ado.zip",
        "2002ado.wad",
        "4.2 MB",
        true,
        false
    },
    {
        "ALIEN VENDETTA",
        "Classic Doom Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "Einer der groessten Klassiker aller Zeiten (32 Level).",
        "Atemberaubende Kulissen und epischer Soundtrack.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/av.zip",
        "AV.WAD",
        "8.6 MB",
        true,
        false
    },
    {
        "ANCIENT ALIENS",
        "skillsaw",
        "MEGAWAD",
        "DOOM2.WAD",
        "Buntes Meisterwerk mit Maya-Tempeln & Aliens.",
        "32 Level mit eigens komponiertem Soundtrack von stewboy.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/aaliens.zip",
        "aaliens_v1_2.wad",
        "38.0 MB",
        true,
        false
    },
    {
        "BACK TO SATURN X (EP 1)",
        "BTSX Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "Get Out of My Stations! Riesiges Sci-Fi Megawad.",
        "Eigene Texturen, Sounds & fantastische Musik.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/btsx_e1.zip",
        "btsx_e1a.wad",
        "17.8 MB",
        true,
        false
    },
    {
        "BACK TO SATURN X (EP 2)",
        "BTSX Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "Tower in the Fountain of Sparks! Teil 2 des Sci-Fi Epos.",
        "Noch groessere Level, neue Atmosphaere und Musik.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/btsx_e2.zip",
        "btsx_e2a.wad",
        "26.7 MB",
        true,
        false
    },
    {
        "COMMUNITY CHEST",
        "Doomworld Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "Der erste Teil der legendaeren Community Chest Reihe.",
        "32 abwechslungsreiche Level vieler bekannter Mapper.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/cchest.zip",
        "cchest.wad",
        "4.4 MB",
        true,
        false
    },
    {
        "COMMUNITY CHEST 2",
        "Doomworld Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 riesige Level mit neuen Texturen und Sounds.",
        "Deutlich hoehere Detailtreue und brillante Musik.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/cchest2.zip",
        "cchest2.wad",
        "8.5 MB",
        true,
        false
    },
    {
        "COMMUNITY CHEST 3",
        "Doomworld Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "Teil 3 der beliebten Megawad-Reihe (32 Level).",
        "Wunderschoene Architektur und packende Kaempfe.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/cchest3.zip",
        "cchest3.wad",
        "11.8 MB",
        true,
        false
    },
    {
        "COMMUNITY CHEST 4",
        "Doomworld Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "Der vierte Teil mit atemberaubender Grafik.",
        "Modernes Boom-Leveldesign und Soundtrack.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/cchest4.zip",
        "cchest4.wad",
        "22.4 MB",
        true,
        false
    },
    {
        "DEATHLESS",
        "James Paddock (Jimmy)",
        "MEGAWAD",
        "DOOM.WAD",
        "Rasantes 4-Episoden Megawad fuer Ultimate Doom.",
        "Vollstaendig solo von James Paddock komponiert & gemappt.",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/deathless.zip",
        "deathless.wad",
        "1.7 MB",
        true,
        false
    },
    {
        "DEUS VULT",
        "Huy Pham (DoomHero85)",
        "EPISODE",
        "DOOM2.WAD",
        "Monumentale gigantische Kathedralen und Tempel.",
        "Eines der optisch beeindruckendsten WADs ueberhaupt.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/d-f/dv.zip",
        "dv.wad",
        "6.1 MB",
        true,
        false
    },
    {
        "DOOM 2 THE WAY ID DID",
        "D2TWID Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 Level, die sich anfuehlen wie original von id Software 1994.",
        "Authentisches Retro-Feeling pur.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/d2twid.zip",
        "d2twid.wad",
        "1.9 MB",
        true,
        false
    },
    {
        "EPIC 2",
        "Alexander S. (Eternal)",
        "MEGAWAD",
        "DOOM2.WAD",
        "32-Level Meisterwerk im alten Aegypten & Alien-Welten.",
        "Einzigartige Texturen, Architektur und Raetsel.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/epic2.zip",
        "epic2.wad",
        "16.5 MB",
        true,
        false
    },
    {
        "ETERNAL DOOM III",
        "TeamTNT",
        "MEGAWAD",
        "DOOM2.WAD",
        "Riesige mittelalterliche Burgen und Raetsel-Level.",
        "32 massive Schlachten von den Machern von Final Doom.",
        "https://youfailit.net/pub/idgames/themes/TeamTNT/eternal/eternal.zip",
        "eternal.wad",
        "9.0 MB",
        true,
        false
    },
    {
        "EVITERNITY",
        "Dragonfly & Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "Modernes OTEX-Meisterwerk mit 6 Episoden (32 Level).",
        "Traumhafte Optik und epischer Soundtrack.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/eviternity.zip",
        "eviternity.wad",
        "47.6 MB",
        true,
        false
    },
    {
        "FREEDOOM: PHASE 1 & 2",
        "Freedoom Project",
        "STANDALONE IWAD",
        "KEINE (STANDALONE)",
        "Komplett freie Standalone-Spiele (Phase 1 & 2)!",
        "68 Level insgesamt. Kein Original-Doom erforderlich.",
        "https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip",
        "freedoom2.wad",
        "23.0 MB",
        true,
        true
    },
    {
        "GOING DOWN",
        "Cyriak Harris (mouldy)",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 Level in einem zusammenbrechenden Hochhaus-Aufzug.",
        "Verrueckte Kaempfe, geniales Pacing und voller Humor.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/gd.zip",
        "gd.wad",
        "8.4 MB",
        true,
        false
    },
    {
        "HELL REVEALED",
        "Yonatan Donner & Haggay Niv",
        "MEGAWAD",
        "DOOM2.WAD",
        "Der Urvater aller 'Slaughter-Maps' von 1997.",
        "32 anspruchsvolle, hochintensive Level.",
        "https://youfailit.net/pub/idgames/themes/hr/hr.zip",
        "hr.wad",
        "2.2 MB",
        true,
        false
    },
    {
        "HELL REVEALED II",
        "Jonas Feragen & HR2 Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "Die offizielle Fortsetzung mit 32 noch haerteren Maps.",
        "Komplett neuer Soundtrack und duestere Kulissen.",
        "https://youfailit.net/pub/idgames/themes/hr/hr2final.zip",
        "hr2final.wad",
        "2.4 MB",
        true,
        false
    },
    {
        "ICARUS: ALIEN VANGUARD",
        "TeamTNT",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Sci-Fi Megawad von 1996 auf einem Raumschiff.",
        "Eigene Texturen und toller 90er-Jahre Soundtrack.",
        "https://youfailit.net/pub/idgames/themes/TeamTNT/icarus/icarus.zip",
        "icarus.wad",
        "2.7 MB",
        true,
        false
    },
    {
        "MEMENTO MORI",
        "The Innocent Crew",
        "MEGAWAD",
        "DOOM2.WAD",
        "Der erste grosse Community-Megawad-Klassiker von 1995.",
        "32 legendaere Level voller Doom-Historie.",
        "https://youfailit.net/pub/idgames/themes/mm/mm_allup.zip",
        "MM.WAD",
        "2.9 MB",
        true,
        false
    },
    {
        "MEMENTO MORI II",
        "The Innocent Crew",
        "MEGAWAD",
        "DOOM2.WAD",
        "Der noch groessere und detailliertere Nachfolger von 1996.",
        "Ein absoluter Meilenstein der Modding-Geschichte.",
        "https://youfailit.net/pub/idgames/themes/mm/mm2.zip",
        "MM2.WAD",
        "3.9 MB",
        true,
        false
    },
    {
        "NO END IN SIGHT",
        "Emil Brundage & Xaser",
        "MEGAWAD",
        "DOOM.WAD",
        "4 Episoden fuer Ultimate Doom im duesteren Romero-Stil.",
        "Grossartige Erkundung, Geheimnisse und Kaempfe.",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/neis.zip",
        "neis.wad",
        "6.1 MB",
        true,
        false
    },
    {
        "PLUTONIA 2",
        "Plutonia 2 Team",
        "MEGAWAD",
        "PLUTONIA / DOOM2",
        "Der offizielle Community-Nachfolger zu Plutonia.",
        "32 knallharte Dschungel- & Tempel-Level.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/pl2.zip",
        "PL2.WAD",
        "5.1 MB",
        true,
        false
    },
    {
        "PLUTONIA REVISITED (PRCP)",
        "Doomworld Community",
        "MEGAWAD",
        "PLUTONIA / DOOM2",
        "Plutonia Revisited Community Project: 32 Level.",
        "Eigene Texturen, tropische Kulissen und Herausforderung.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/prcp.zip",
        "prcp.wad",
        "4.9 MB",
        true,
        false
    },
    {
        "REQUIEM",
        "The Innocent Crew & Friends",
        "MEGAWAD",
        "DOOM2.WAD",
        "32-Level Meilenstein von 1997.",
        "Duestere Atmosphaere und grandiose Architektur.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/requiem.zip",
        "REQUIEM.WAD",
        "3.8 MB",
        true,
        false
    },
    {
        "ROWDY RUDY II: POWERTRIP",
        "Doomkid",
        "MEGAWAD",
        "DOOM2.WAD",
        "Action pur! Modifizierte Waffen, neue Gegner & Sounds.",
        "Unglaublich unterhaltsames Oldschool-Megawad.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/rudy2.zip",
        "rudy2.wad",
        "6.3 MB",
        true,
        false
    },
    {
        "SCYTHE",
        "Erik Alm",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres 32-Level Megawad. Schnelle Action,",
        "fantastische Musik und purer Spielspass.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/scythe.zip",
        "SCYTHE.WAD",
        "2.0 MB",
        true,
        false
    },
    {
        "SCYTHE 2",
        "Erik Alm",
        "MEGAWAD",
        "DOOM2.WAD",
        "Die beruehmte Fortsetzung von Scythe (32 Level).",
        "Wunderschoene Texturen & geniales Leveldesign.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/scythe2.zip",
        "scythe2.wad",
        "10.1 MB",
        true,
        false
    },
    {
        "SIGIL (EPISODE 5)",
        "John Romero",
        "EPISODE",
        "DOOM.WAD",
        "Offizielle 5. Episode von Doom-Schoepfer John Romero.",
        "9 bockschwere Level im klassischen 1993-Stil.",
        "https://romero.com/s/SIGIL_v1_21.zip",
        "SIGIL_v1_21.wad",
        "3.1 MB",
        true,
        false
    },
    {
        "SIGIL II (EPISODE 6)",
        "John Romero",
        "EPISODE",
        "DOOM.WAD",
        "Offizielle 6. Episode zum 30. Doom-Jubilaeum.",
        "9 brandneue Level voller Herausforderungen.",
        "https://romero.com/s/SIGIL_II_V1_0.zip",
        "SIGIL_II_V1_0.WAD",
        "1.9 MB",
        true,
        false
    },
    {
        "SPEED OF DOOM",
        "Josh (Joshy) & Darkwave062",
        "MEGAWAD",
        "DOOM2.WAD",
        "33 rasante Level mit hohem Tempo und tollem Flow.",
        "Extrem beliebter Community-Klassiker.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/sodfinal.zip",
        "sodfinal.wad",
        "8.8 MB",
        true,
        false
    },
    {
        "SUNLUST",
        "Ribbiks & Dannebubinga",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 wunderschoene, extrem fordernde Level.",
        "Unglaubliche Architektur und fantastischer Soundtrack.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/sunlust.zip",
        "sunlust.wad",
        "23.3 MB",
        true,
        false
    },
    {
        "ULTIMATE DOOM THE WAY ID DID",
        "UDTWID Team",
        "MEGAWAD",
        "DOOM.WAD",
        "Wie haette Ultimate Doom 1993 noch aussehen koennen?",
        "4 Episoden voller Nostalgie und Charme.",
        "https://youfailit.net/pub/idgames/levels/doom/s-u/udtwid.zip",
        "udtwid.wad",
        "0.7 MB",
        true,
        false
    },
    {
        "VALIANT",
        "skillsaw",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 rasante Level mit neuen Monster-Varianten,",
        "schnelleren Waffen und brillanter Action.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/valiant.zip",
        "Valiant.wad",
        "24.4 MB",
        true,
        false
    },
    {
        "WHISPERS OF SATAN",
        "Paul Corfiatis & Kristian Aro",
        "MEGAWAD",
        "DOOM2.WAD",
        "35 riesige Level mit duesterem gotischem Flair.",
        "Grossartige Raetsel, Geheimgänge und Soundtrack.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/wos.zip",
        "wos.wad",
        "10.9 MB",
        true,
        false
    }
};

#define CATALOG_COUNT ((int)(sizeof(s_catalog) / sizeof(s_catalog[0])))

// In-memory cache for installed status (avoids hammering the slow SD card every frame)
static bool s_installed[CATALOG_COUNT];

static bool Downloader_CheckFileOnDisk(const char *target_filename)
{
    if (!target_filename || !target_filename[0]) return false;

    char path[256];
    // Check exact
    snprintf(path, sizeof(path), "sdmc:/3ds/Boom3DS/wads/%s", target_filename);
    if (access(path, R_OK) == 0) return true;

    // Check lowercase
    char lower[64];
    strncpy(lower, target_filename, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    for (int i = 0; lower[i]; i++) lower[i] = tolower((unsigned char)lower[i]);
    snprintf(path, sizeof(path), "sdmc:/3ds/Boom3DS/wads/%s", lower);
    if (access(path, R_OK) == 0) return true;

    // Check uppercase
    char upper[64];
    strncpy(upper, target_filename, sizeof(upper) - 1);
    upper[sizeof(upper) - 1] = '\0';
    for (int i = 0; upper[i]; i++) upper[i] = toupper((unsigned char)upper[i]);
    snprintf(path, sizeof(path), "sdmc:/3ds/Boom3DS/wads/%s", upper);
    if (access(path, R_OK) == 0) return true;

    return false;
}

static void Downloader_RefreshInstalledCache(void)
{
    for (int i = 0; i < CATALOG_COUNT; i++) {
        s_installed[i] = Downloader_CheckFileOnDisk(s_catalog[i].target_file);
    }
}

// Download progress state
typedef struct {
    curl_off_t dltotal;
    curl_off_t dlnow;
    const char *title;
    bool abort_requested;
    u64 last_render_tick;
} dl_progress_t;

static void Downloader_RenderProgress(dl_progress_t *prog)
{
    u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!bot_fb) return;

    // Background
    WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 16, 16, 20);

    // Modal Card
    WP_FillRect(bot_fb, 320, 15, 30, 290, 180, 28, 28, 35);
    WP_DrawRect(bot_fb, 320, 15, 30, 290, 180, 80, 160, 255);

    // Title
    WP_FillRect(bot_fb, 320, 15, 30, 290, 26, 35, 75, 140);
    WP_DrawCenteredText(bot_fb, 320, 15, 30, 290, 26, "DOWNLOAD LAEUFT...", 255, 255, 255, 1);

    // WAD Title
    if (strlen(prog->title) <= 24) {
        WP_DrawCenteredText(bot_fb, 320, 20, 68, 280, 20, prog->title, 255, 215, 0, 1);
    } else {
        char trunc[25];
        strncpy(trunc, prog->title, 24);
        trunc[24] = '\0';
        WP_DrawCenteredText(bot_fb, 320, 20, 68, 280, 20, trunc, 255, 215, 0, 1);
    }

    // Progress Bar Frame
    int bar_x = 25;
    int bar_y = 100;
    int bar_w = 270;
    int bar_h = 22;

    WP_FillRect(bot_fb, 320, bar_x, bar_y, bar_w, bar_h, 10, 10, 15);
    WP_DrawRect(bot_fb, 320, bar_x, bar_y, bar_w, bar_h, 120, 120, 140);

    // Fill progress
    float pct = 0.0f;
    if (prog->dltotal > 0) {
        pct = (float)prog->dlnow / (float)prog->dltotal;
        if (pct > 1.0f) pct = 1.0f;
        int fill_w = (int)(pct * (bar_w - 4));
        if (fill_w > 0) {
            WP_FillRect(bot_fb, 320, bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 30, 180, 80);
        }
    }

    // Percentage & Size Text
    char status_str[64];
    float cur_mb = (float)prog->dlnow / (1024.0f * 1024.0f);
    float tot_mb = (float)prog->dltotal / (1024.0f * 1024.0f);

    if (prog->dltotal > 0) {
        snprintf(status_str, sizeof(status_str), "%.1f%%  (%.2f / %.2f MB)", pct * 100.0f, cur_mb, tot_mb);
    } else {
        snprintf(status_str, sizeof(status_str), "Geladen: %.2f MB", cur_mb);
    }
    WP_DrawCenteredText(bot_fb, 320, 20, 134, 280, 16, status_str, 200, 230, 255, 1);

    // Cancel hint
    WP_FillRect(bot_fb, 320, 25, 165, 270, 30, 60, 20, 20);
    WP_DrawRect(bot_fb, 320, 25, 165, 270, 30, 200, 50, 50);
    WP_DrawCenteredText(bot_fb, 320, 25, 165, 270, 30, "[B]  DOWNLOAD ABBRECHEN", 255, 255, 255, 1);

    gfxFlushBuffers();
    gfxSwapBuffers();
}

static int Downloader_XferInfo(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    dl_progress_t *prog = (dl_progress_t*)clientp;
    prog->dltotal = dltotal;
    prog->dlnow = dlnow;

    hidScanInput();
    u32 kDown = hidKeysDown();
    touchPosition touch;
    hidTouchRead(&touch);

    if (kDown & KEY_B) {
        prog->abort_requested = true;
        return 1;
    }
    if ((kDown & KEY_TOUCH) && touch.py >= 165 && touch.py <= 195 && touch.px >= 25 && touch.px <= 295) {
        prog->abort_requested = true;
        return 1;
    }

    u64 now = osGetTime();
    // Only refresh UI every 400ms (2.5 fps) during download to leave 99% CPU for WiFi throughput & disk I/O!
    if (now - prog->last_render_tick >= 400) {
        prog->last_render_tick = now;
        Downloader_RenderProgress(prog);
    }

    return 0;
}

static bool Downloader_ExtractZip(const char *zip_path, const char *dest_dir, char *err_msg, size_t err_len)
{
    unzFile uf = unzOpen(zip_path);
    if (!uf) {
        snprintf(err_msg, err_len, "ZIP-Datei konnte nicht geoeffnet werden!");
        return false;
    }

    if (unzGoToFirstFile(uf) != UNZ_OK) {
        unzClose(uf);
        snprintf(err_msg, err_len, "ZIP-Archiv ist leer!");
        return false;
    }

    char filename[256];
    unz_file_info file_info;
    char buffer[16384];
    int extracted = 0;

    do {
        if (unzGetCurrentFileInfo(uf, &file_info, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK) {
            continue;
        }

        if (strstr(filename, "__MACOSX") || strstr(filename, "/.")) {
            continue;
        }

        const char *dot = strrchr(filename, '.');
        if (!dot) continue;

        if (strcasecmp(dot, ".wad") != 0 && strcasecmp(dot, ".deh") != 0) {
            continue;
        }

        const char *basename = strrchr(filename, '/');
        if (basename) basename++;
        else basename = filename;

        if (!basename[0] || basename[0] == '.') continue;

        if (unzOpenCurrentFile(uf) != UNZ_OK) continue;

        char out_path[512];
        snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, basename);

        FILE *out_fp = fopen(out_path, "wb");
        if (!out_fp) {
            unzCloseCurrentFile(uf);
            continue;
        }

        int read_bytes = 0;
        while ((read_bytes = unzReadCurrentFile(uf, buffer, sizeof(buffer))) > 0) {
            fwrite(buffer, 1, read_bytes, out_fp);
        }

        fclose(out_fp);
        unzCloseCurrentFile(uf);
        extracted++;
    } while (unzGoToNextFile(uf) == UNZ_OK);

    unzClose(uf);

    if (extracted == 0) {
        snprintf(err_msg, err_len, "Keine .WAD Datei im ZIP gefunden!");
        return false;
    }

    return true;
}

static bool Downloader_Download(const repo_item_t *item, char *err_msg, size_t err_len)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/Boom3DS", 0777);
    mkdir("sdmc:/3ds/Boom3DS/wads", 0777);
    mkdir("sdmc:/3ds/Boom3DS/downloads", 0777);

    char temp_file[256];
    if (item->is_zip) {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Boom3DS/downloads/temp_dl.zip");
    } else {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Boom3DS/wads/%s", item->target_file);
    }

    FILE *fp = fopen(temp_file, "wb");
    if (!fp) {
        snprintf(err_msg, err_len, "Konnte Zieldatei auf SD nicht erstellen!");
        return false;
    }

    // Set 64 KB write buffer to eliminate SD FAT write stutter
    setvbuf(fp, NULL, _IOFBF, 64 * 1024);

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        unlink(temp_file);
        snprintf(err_msg, err_len, "Curl-Initialisierung fehlgeschlagen!");
        return false;
    }

    dl_progress_t prog = {0};
    prog.title = item->title;
    prog.last_render_tick = osGetTime();

    curl_easy_setopt(curl, CURLOPT_URL, item->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Downloader_XferInfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Boom3DS/0.9.5 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    // Optimized buffer sizes for faster 3DS network throughput
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 64 * 1024L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (prog.abort_requested) {
        unlink(temp_file);
        snprintf(err_msg, err_len, "Download vom Benutzer abgebrochen.");
        return false;
    }

    if (res != CURLE_OK) {
        unlink(temp_file);
        snprintf(err_msg, err_len, "Netzwerkfehler: %s", curl_easy_strerror(res));
        return false;
    }

    if (item->is_zip) {
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 15, 70, 290, 100, 28, 28, 35);
            WP_DrawRect(bot_fb, 320, 15, 70, 290, 100, 255, 215, 0);
            WP_DrawCenteredText(bot_fb, 320, 20, 90, 280, 20, "ENTPACKE WAD-DATEIEN...", 255, 255, 255, 1);
            WP_DrawCenteredText(bot_fb, 320, 20, 120, 280, 20, "Bitte einen Moment Geduld...", 200, 200, 200, 1);
            gfxFlushBuffers();
            gfxSwapBuffers();
        }

        bool ext_ok = Downloader_ExtractZip(temp_file, "sdmc:/3ds/Boom3DS/wads", err_msg, err_len);
        unlink(temp_file);
        if (!ext_ok) return false;
    }

    return true;
}

static void Downloader_RenderTopScreen(u8 *fb, const repo_item_t *cur, int sel_idx, int total_count, bool installed)
{
    // Background
    WP_FillRect(fb, 400, 0, 0, 400, 240, 20, 20, 24);

    // Header bar
    WP_FillRect(fb, 400, 0, 0, 400, 32, 20, 60, 120);
    WP_FillRect(fb, 400, 0, 30, 400, 2, 80, 160, 255);
    WP_DrawText(fb, 400, 16, 8, "BOOM3DS WAD REPOSITORY", 255, 255, 255, 2);

    // Graphical 3-bar WiFi Indicator
    u8 wifi = osGetWifiStrength();
    int wx = 300;
    int wy = 10;

    // Draw signal bars (green if active, dark grey if inactive)
    WP_FillRect(fb, 400, wx,     wy + 8, 3, 4, wifi >= 1 ? 80 : 50, wifi >= 1 ? 255 : 50, wifi >= 1 ? 100 : 55);
    WP_FillRect(fb, 400, wx + 5, wy + 4, 3, 8, wifi >= 2 ? 80 : 50, wifi >= 2 ? 255 : 50, wifi >= 2 ? 100 : 55);
    WP_FillRect(fb, 400, wx + 10, wy,    3, 12, wifi >= 3 ? 80 : 50, wifi >= 3 ? 255 : 50, wifi >= 3 ? 100 : 55);

    if (wifi > 0) {
        WP_DrawText(fb, 400, wx + 18, wy + 3, "WLAN: OK", 100, 255, 120, 1);
    } else {
        WP_DrawText(fb, 400, wx + 18, wy + 3, "WLAN: AUS", 255, 80, 80, 1);
    }

    if (!cur || total_count == 0) {
        WP_DrawText(fb, 400, 20, 80, "KEINE EINTRÄGE GEFUNDEN", 255, 80, 80, 2);
        return;
    }

    // Card Box
    WP_FillRect(fb, 400, 12, 40, 376, 136, 30, 32, 38);
    WP_DrawRect(fb, 400, 12, 40, 376, 136, 70, 130, 200);

    // Title
    if (strlen(cur->title) <= 22) {
        WP_DrawText(fb, 400, 22, 48, cur->title, 255, 255, 255, 2);
    } else {
        WP_DrawText(fb, 400, 22, 50, cur->title, 255, 255, 255, 1);
    }

    // Author
    char author_str[96];
    snprintf(author_str, sizeof(author_str), "AUTOR: %s", cur->author[0] ? cur->author : "Unbekannt");
    WP_DrawText(fb, 400, 22, 70, author_str, 255, 215, 0, 1);

    // Category Badge
    if (cur->is_iwad) {
        WP_FillRect(fb, 400, 22, 88, 140, 16, 20, 120, 40);
        WP_DrawCenteredText(fb, 400, 22, 88, 140, 16, "[STANDALONE IWAD]", 255, 255, 255, 1);
    } else {
        WP_FillRect(fb, 400, 22, 88, 140, 16, 20, 70, 130);
        WP_DrawCenteredText(fb, 400, 22, 88, 140, 16, "[COMMUNITY MOD/WAD]", 180, 230, 255, 1);
    }

    // Requirement & Size
    char req_str[96];
    snprintf(req_str, sizeof(req_str), "BASIS: %s   GROESSE: %s", cur->base_iwad, cur->size_str);
    WP_DrawText(fb, 400, 22, 110, req_str, 200, 200, 200, 1);

    // Description
    if (cur->desc1[0]) {
        WP_DrawText(fb, 400, 22, 128, cur->desc1, 180, 210, 240, 1);
    }
    if (cur->desc2[0]) {
        WP_DrawText(fb, 400, 22, 142, cur->desc2, 180, 210, 240, 1);
    }

    // Install status on card
    if (installed) {
        WP_FillRect(fb, 400, 240, 88, 140, 16, 20, 100, 30);
        WP_DrawCenteredText(fb, 400, 240, 88, 140, 16, "BEREITS INSTALLIERT", 100, 255, 120, 1);
    } else {
        WP_FillRect(fb, 400, 240, 88, 140, 16, 90, 70, 15);
        WP_DrawCenteredText(fb, 400, 240, 88, 140, 16, "NICHT INSTALLIERT", 255, 215, 0, 1);
    }

    // Position indicator
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "#%d / %d", sel_idx + 1, total_count);
    WP_DrawText(fb, 400, 330, 158, pos_buf, 160, 160, 180, 1);

    // Controls Help Box (Bottom)
    WP_FillRect(fb, 400, 12, 182, 376, 50, 24, 24, 28);
    WP_DrawRect(fb, 400, 12, 182, 376, 50, 70, 70, 80);
    WP_DrawText(fb, 400, 20, 189, "STEUERUNG:", 255, 215, 0, 1);
    WP_DrawText(fb, 400, 100, 189, "D-PAD: Scrollen   L / R: A-Z Buchstabensprung", 200, 200, 200, 1);
    WP_DrawText(fb, 400, 20, 209, "A / TOUCH: Herunterladen   B: Zurueck zum WAD-Browser", 100, 255, 120, 1);
}

static void Downloader_RenderBottomScreen(u8 *fb, const repo_item_t *items, const bool *installed_flags, int count, int sel_idx, int scroll_offset)
{
    // Background
    WP_FillRect(fb, 320, 0, 0, 320, 240, 16, 16, 20);

    // Header bar
    WP_FillRect(fb, 320, 0, 0, 320, 24, 30, 35, 45);
    WP_DrawText(fb, 320, 8, 8, "VERFUEGBARE WADS & MEGAWADS:", 255, 215, 0, 1);

    // List of 5 items
    int visible_items = 5;
    int item_y_start = 28;
    int item_h = 32;

    for (int i = 0; i < visible_items; i++) {
        int idx = scroll_offset + i;
        int y = item_y_start + i * (item_h + 2);

        if (idx < count) {
            const repo_item_t *item = &items[idx];
            bool sel = (idx == sel_idx);
            bool installed = installed_flags ? installed_flags[idx] : false;

            // Row background
            if (sel) {
                WP_FillRect(fb, 320, 6, y, 308, item_h, 20, 60, 100);
                WP_DrawRect(fb, 320, 6, y, 308, item_h, 80, 180, 255);
            } else {
                WP_FillRect(fb, 320, 6, y, 308, item_h, 26, 26, 30);
                WP_DrawRect(fb, 320, 6, y, 308, item_h, 45, 45, 52);
            }

            // Indicator
            if (sel) {
                WP_DrawText(fb, 320, 12, y + 11, ">", 255, 255, 80, 2);
            }

            // Install status badge
            if (installed) {
                WP_FillRect(fb, 320, 26, y + 8, 28, 16, 20, 110, 30);
                WP_DrawCenteredText(fb, 320, 26, y + 8, 28, 16, "[OK]", 100, 255, 120, 1);
            } else {
                WP_FillRect(fb, 320, 26, y + 8, 28, 16, 70, 70, 80);
                WP_DrawCenteredText(fb, 320, 26, y + 8, 28, 16, "[--]", 200, 200, 200, 1);
            }

            // Name
            WP_DrawText(fb, 320, 62, y + 12, item->title, sel ? 255 : 220, sel ? 255 : 220, sel ? 100 : 220, 1);

            // Size badge
            WP_DrawText(fb, 320, 260, y + 12, item->size_str, 160, 180, 200, 1);
        }
    }

    // Bottom Action Buttons:
    // Left: [HERUNTERLADEN] (X: 6, Y: 202, W: 200, H: 34)
    // Right: [ZURUECK] (X: 212, Y: 202, W: 102, H: 34)
    bool already_inst = (count > 0 && sel_idx < count && installed_flags) ? installed_flags[sel_idx] : false;

    if (already_inst) {
        WP_FillRect(fb, 320, 6, 202, 200, 34, 30, 70, 40);
        WP_DrawRect(fb, 320, 6, 202, 200, 34, 60, 150, 70);
        WP_DrawCenteredText(fb, 320, 6, 202, 200, 34, "NEU LADEN [A]", 200, 255, 200, 1);
    } else {
        WP_FillRect(fb, 320, 6, 202, 200, 34, 20, 120, 45);
        WP_DrawRect(fb, 320, 6, 202, 200, 34, 60, 230, 90);
        WP_DrawCenteredText(fb, 320, 6, 202, 200, 34, "HERUNTERLADEN", 255, 255, 255, 2);
    }

    // Back button
    WP_FillRect(fb, 320, 212, 202, 102, 34, 30, 30, 36);
    WP_DrawRect(fb, 320, 212, 202, 102, 34, 80, 80, 95);
    WP_DrawCenteredText(fb, 320, 212, 202, 102, 34, "[B] ZURUECK", 255, 255, 255, 1);
}

static void Downloader_ShowModal(const char *title, const char *msg1, const char *msg2, u8 r, u8 g, u8 b)
{
    // Render loop for modal dialog (handles double buffering and proper input debounce)
    for (int t = 0; t < 300 && aptMainLoop(); t++) {
        hidScanInput();
        u32 kd = hidKeysDown();
        if (t > 15 && (kd & (KEY_A | KEY_B | KEY_START | KEY_TOUCH))) {
            break;
        }

        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 20, 50, 280, 135, 28, 28, 35);
            WP_DrawRect(bot_fb, 320, 20, 50, 280, 135, r, g, b);

            WP_FillRect(bot_fb, 320, 20, 50, 280, 26, r / 2, g / 2, b / 2);
            WP_DrawCenteredText(bot_fb, 320, 20, 50, 280, 26, title, 255, 255, 255, 1);

            if (msg1) WP_DrawCenteredText(bot_fb, 320, 25, 85, 270, 18, msg1, 255, 255, 255, 1);
            if (msg2) WP_DrawCenteredText(bot_fb, 320, 25, 110, 270, 18, msg2, 220, 220, 220, 1);

            WP_DrawCenteredText(bot_fb, 320, 25, 150, 270, 18, "Druecke [A] oder tippe...", 255, 215, 0, 1);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

bool WAD_Downloader_Run(void)
{
    int selected_idx = 0;
    int scroll_offset = 0;
    bool any_installed = false;

    // Check installed status ONCE upfront in memory (zero SD I/O during render loop!)
    Downloader_RefreshInstalledCache();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        int count = CATALOG_COUNT;

        // Keep selected in bounds
        if (selected_idx >= count) selected_idx = count - 1;
        if (selected_idx < 0) selected_idx = 0;

        // Adjust scroll offset
        if (selected_idx < scroll_offset) {
            scroll_offset = selected_idx;
        }
        if (selected_idx >= scroll_offset + 5) {
            scroll_offset = selected_idx - 4;
        }

        // Exit / Back: instantaneous return!
        if (kDown & KEY_B) {
            break;
        }

        // Navigation
        if (kDown & (KEY_DUP | KEY_CPAD_UP)) {
            if (selected_idx > 0) selected_idx--;
        }
        if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) {
            if (selected_idx < count - 1) selected_idx++;
        }
        if (kDown & (KEY_DLEFT | KEY_CPAD_LEFT)) {
            selected_idx -= 5;
            if (selected_idx < 0) selected_idx = 0;
        }
        if (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) {
            selected_idx += 5;
            if (selected_idx >= count) selected_idx = count - 1;
        }

        // Alphabetical jumping with [L] and [R]!
        if (kDown & KEY_R) {
            char cur_ch = toupper((unsigned char)s_catalog[selected_idx].title[0]);
            int target = -1;
            for (int i = selected_idx + 1; i < count; i++) {
                char next_ch = toupper((unsigned char)s_catalog[i].title[0]);
                if (next_ch > cur_ch) {
                    target = i;
                    break;
                }
            }
            if (target >= 0) selected_idx = target;
        }
        if (kDown & KEY_L) {
            char cur_ch = toupper((unsigned char)s_catalog[selected_idx].title[0]);
            int cur_first = selected_idx;
            while (cur_first > 0 && toupper((unsigned char)s_catalog[cur_first - 1].title[0]) == cur_ch) {
                cur_first--;
            }
            if (cur_first < selected_idx) {
                selected_idx = cur_first;
            } else if (cur_first > 0) {
                char prev_ch = toupper((unsigned char)s_catalog[cur_first - 1].title[0]);
                int target = cur_first - 1;
                while (target > 0 && toupper((unsigned char)s_catalog[target - 1].title[0]) == prev_ch) {
                    target--;
                }
                selected_idx = target;
            }
        }

        // Touch Input
        if (kDown & KEY_TOUCH) {
            // Check list rows (Y: 28 to 198)
            int item_y_start = 28;
            int item_h = 32;

            for (int i = 0; i < 5; i++) {
                int y = item_y_start + i * (item_h + 2);
                if (touch.py >= y && touch.py < y + item_h && touch.px >= 6 && touch.px <= 314) {
                    int clicked_idx = scroll_offset + i;
                    if (clicked_idx < count) {
                        selected_idx = clicked_idx;
                    }
                    break;
                }
            }

            // Touch on Bottom Action Button: [HERUNTERLADEN]
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 6 && touch.px <= 206) {
                kDown |= KEY_A;
            }

            // Touch on Right Button: [ZURUECK]
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 212 && touch.px <= 314) {
                kDown |= KEY_B;
            }
        }

        // Download Action: [A]
        if (kDown & KEY_A) {
            if (count > 0 && selected_idx < count) {
                if (osGetWifiStrength() == 0) {
                    Downloader_ShowModal("KEIN WLAN", "Bitte 3DS mit dem Internet verbinden!", "Systemeinstellungen pruefen.", 255, 80, 80);
                    continue;
                }

                if (!Downloader_InitNetwork()) {
                    Downloader_ShowModal("NETZWERKFEHLER", "Socket-Initialisierung fehlgeschlagen!", "3DS unterstuetzt kein WiFi?", 255, 50, 50);
                    continue;
                }

                const repo_item_t *sel = &s_catalog[selected_idx];
                char err_msg[128] = {0};

                bool ok = Downloader_Download(sel, err_msg, sizeof(err_msg));
                if (ok) {
                    any_installed = true;
                    Downloader_RefreshInstalledCache();
                    Downloader_ShowModal("DOWNLOAD ERFOLGREICH!", sel->title, "Wurde erfolgreich nach /wads/ installiert!", 50, 220, 80);
                } else {
                    Downloader_ShowModal("FEHLER BEIM DOWNLOAD", err_msg, "Bitte spaeter erneut versuchen.", 255, 60, 60);
                }
            }
        }

        // Render Frame
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            const repo_item_t *cur = (count > 0) ? &s_catalog[selected_idx] : NULL;
            bool inst = (count > 0) ? s_installed[selected_idx] : false;
            Downloader_RenderTopScreen(top_fb, cur, selected_idx, count, inst);
        }

        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            Downloader_RenderBottomScreen(bot_fb, s_catalog, s_installed, count, selected_idx, scroll_offset);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    // Do NOT shutdown network here if it wasn't even needed, or if user returns to browser
    // This makes pressing [B] instant without any lag!
    return any_installed;
}

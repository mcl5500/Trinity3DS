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
#include <dirent.h>
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
        "'DEATH IN THE ASS OF THE UNIVERSE'......",
        "Eye del Cul",
        "MEGAWAD",
        "DOOM.WAD",
        "A compilation of 27 levels (the three series from th...",
        "But they weren't conceived as a complete and coheren...",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/niveles.zip",
        "niveles.wad",
        "1.0 MB",
        true,
        false
    },
    {
        "1 MONSTER MEGAWAD",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "In the spirit of 1024 Congestion and 10Sector, 25 ne...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/1monster.zip",
        "1monster.wad",
        "7.2 MB",
        true,
        false
    },
    {
        "10 SECTORS",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "From mid-August to mid-September, Doomworld",
        "com held a contest with a simple premise: design the...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/10sector.zip",
        "10sector.wad",
        "3.2 MB",
        true,
        false
    },
    {
        "10 SECTORS PART 2",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "From mid-August to mid-September, Doomworld",
        "com held a contest with a simple premise: design the...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/10secto2.zip",
        "10secto2.wad",
        "1.5 MB",
        true,
        false
    },
    {
        "100 LINE MASSACRE",
        "NinjaDelphox and Arsinikk",
        "MEGAWAD",
        "DOOM2.WAD",
        "100 Line Massacre is a small Megawad project (Full 3...",
        " All maps were designed with pistol start in mind, t...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/100lnm.zip",
        "100lnm.wad",
        "0.9 MB",
        true,
        false
    },
    {
        "1000 LINE COMMUNITY PROJECT V1.0",
        "Liberation and others",
        "MEGAWAD",
        "DOOM2.WAD",
        "A 34 map megawad made for vanilla Doom 2, made by th...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/1klinecp.zip",
        "1klinecp.wad",
        "2.1 MB",
        true,
        false
    },
    {
        "1000 LINES 2 - COMMUNITY PROJECT 1.0",
        "Liberation and others",
        "MEGAWAD",
        "DOOM2.WAD",
        "A 34 map megawad made for vanilla Doom 2, made by th...",
        "Using the power of Votex, a vanilla version of the O...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/1klinecp2.zip",
        "1klinecp2.wad",
        "23.1 MB",
        true,
        false
    },
    {
        "180 MINUTES POUR VIVRE",
        "The French Doom Community &...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A collection of 32 new 'speedmaps'",
        "See 180mpv_info",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/180mpv.zip",
        "180mpv.wad",
        "9.6 MB",
        true,
        false
    },
    {
        "1994 TUNE-UP COMMUNITY PROJECT",
        "Snarboo",
        "MEGAWAD",
        "DOOM2.WAD",
        "Everybody knows that most maps from 1994 are pretty ...",
        " The purpose of this community wad is to improve on ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/1994tu.zip",
        "1994tu.wad",
        "10.5 MB",
        true,
        false
    },
    {
        "200 MINUTES OF /VR/",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "A megawad of vanilla maps, each made in 200 minutes",
        "https://archive",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/200minvr.zip",
        "200minvr.wad",
        "1.2 MB",
        true,
        false
    },
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
        "2002 A DOOM ODYSSEY - 10TH ANNIVERSAR...",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "The 10th anniversary edition of 2002 A Doom Odyssey",
        "Featuring many improvements and enhancements over th...",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/2002ad10.zip",
        "2002ad10.wad",
        "4.2 MB",
        true,
        false
    },
    {
        "2048 UNLEASHED (PCORF COMMUNITY PROJE...",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "Welcome to 2048 UNLEASHED - An all new 32 level sing...",
        "Known as the Pcorf Community Project 2 at the Doomwo...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/pc_cp2.zip",
        "pc_cp2.wad",
        "6.1 MB",
        true,
        false
    },
    {
        "3 HEURES D'AGONIE",
        "JC Zyklonb Darkwave0000 Nec...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Doom Community Meisterwerk",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/3hafinal.zip",
        "3hafinal.wad",
        "2.5 MB",
        true,
        false
    },
    {
        "3 HEURES D'AGONIE 2",
        "3ha2 Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "3 Heures d'agonie 2 is a collection of 32 vanilla sp...",
        "The general rule was that each map layout was to be ...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/3ha2.zip",
        "3ha2.wad",
        "2.9 MB",
        true,
        false
    },
    {
        "3 HEURES D'AGONIE 3",
        "3ha3 Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Doom Community Meisterwerk",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/3ha3.zip",
        "3ha3.wad",
        "3.2 MB",
        true,
        false
    },
    {
        "30 LEVELS",
        "Stephen Huff",
        "MEGAWAD",
        "DOOM2.WAD",
        "After destroying the big boss in doom2, you settled ...",
        "Realizing the drill, you grabbed your sidearm and wi...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/30lvls.zip",
        "30lvls.wad",
        "3.6 MB",
        true,
        false
    },
    {
        "300 MINUTES OF /VR/",
        "Various users of 4chan's /v...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A megawad compilation of maps made by /vr/ users",
        "Participants were asked to make a map in 300 minutes...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/300minvr.zip",
        "300minvr.wad",
        "2.0 MB",
        true,
        false
    },
    {
        "32 HOURS IN PAIN",
        "Kristian Aro",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is the second episode of the ' 20 Days in Hell ...",
        "Send me feedback, please",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/32hnpain.zip",
        "32hnpain.wad",
        "1.5 MB",
        true,
        false
    },
    {
        "32 OVER 32",
        "Donnel 'Jazzmaster9' Enriquez",
        "MEGAWAD",
        "DOOM2.WAD",
        "Not your average mapset",
        "32 over 32 contains 32 standalone levels separated i...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/32over32.zip",
        "32over32.wad",
        "5.2 MB",
        true,
        false
    },
    {
        "32IN24-14: HOW THE HAMBURGLAR STOLE C...",
        "The 32in24 Team!",
        "MEGAWAD",
        "DOOM2.WAD",
        "It was the pinnacle of burgerdom",
        "Finally, the long summer's study in Aberdonian cuisi...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/32in2414.zip",
        "32in2414.wad",
        "13.5 MB",
        true,
        false
    },
    {
        "50 MONSTERS",
        "Brayden 'AD_79' Hart et al.",
        "MEGAWAD",
        "DOOM2.WAD",
        "50 Monsters is a project I started back in 2014 when...",
        "It got a bit out of hand in the second half of devel...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/50monstr.zip",
        "50monstr.wad",
        "6.4 MB",
        true,
        false
    },
    {
        "50 SHADES OF GRAYTALL",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "50 Shades of Graytall is a project designed around c...",
        "Started in response to a silly post made in the Edit...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/50shades.zip",
        "50shades.wad",
        "4.9 MB",
        true,
        false
    },
    {
        "A FISTFUL OF DOOM",
        "Gali editor for 'The WadFac...",
        "MEGAWAD",
        "DOOM2.WAD",
        "'A Fistful of Doom' is a complete Western conversion...",
        "Every effort has been made to make this the ultimate...",
        "https://youfailit.net/pub/idgames/levels/doom2/d-f/fistful3.zip",
        "FISTFUL.WAD",
        "1.7 MB",
        true,
        false
    },
    {
        "A.L.T.",
        "[B0S] Clan",
        "MEGAWAD",
        "DOOM2.WAD",
        "Third WAD by [B0S] clan",
        "Now it's 32 levels",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/alt.zip",
        "alt.wad",
        "11.6 MB",
        true,
        false
    },
    {
        "ABANDONED MEGAWAD",
        "yylow30",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is just a collection of maps I've abandoned in ...",
        "=''' So I make those into a Megawad",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/abmega.zip",
        "abmega.wad",
        "3.2 MB",
        true,
        false
    },
    {
        "ABSOLUTE DISHONOR",
        "Michael Jan Krizik (valkiri...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Absolute Dishonor is a brand-new 8-level WAD created...",
        "The levels are inspired from old-school Doom WADs fr...",
        "https://youfailit.net/pub/idgames/levels/doom2/a-c/absoldis.zip",
        "ABSOLDIS.wad",
        "1.5 MB",
        true,
        false
    },
    {
        "ABYSS",
        "Avery Lodato",
        "MEGAWAD",
        "DOOM2.WAD",
        "This pwad was written by Avery Lodato (alodato@ucdavis",
        "edu) using deu5",
        "https://youfailit.net/pub/idgames/levels/doom/a-c/abyss.zip",
        "ABYSS.WAD",
        "0.0 MB",
        true,
        false
    },
    {
        "AKELDAMA",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "Get ready to blaze through 32 action-packed levels f...",
        "Akeldama continues in the spirit of collecting a gro...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/akeldama.zip",
        "akeldama.wad",
        "7.3 MB",
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
        "ALTITUDE",
        "Thomas 'tourniquet' Seifert",
        "MEGAWAD",
        "DOOM2.WAD",
        "One large City themed map designed for Vanilla/Choco...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/a-c/altitude.zip",
        "ALTITUDE.WAD",
        "0.5 MB",
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
        "ANCIENT ALIENS",
        "Paul 'skillsaw' DeBruyne",
        "MEGAWAD",
        "DOOM2.WAD",
        "Demos recorded with the original release of Ancient ...",
        "1 includes updates to textures",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/aaliens.zip",
        "aaliens.wad",
        "35.9 MB",
        true,
        false
    },
    {
        "AND THE BLOODSHED BEGAN",
        "Maxime Bisiaux 'Datacore' /...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/atbb_f2.zip",
        "atbb_f2.wad",
        "2.3 MB",
        true,
        false
    },
    {
        "AREA 51",
        "Keith Hickman, Garth Donovan",
        "MEGAWAD",
        "DOOM2.WAD",
        "Area 51, The militarys not so top secret base in Nev...",
        " We are sending you in alone to investigate armed wi...",
        "https://youfailit.net/pub/idgames/levels/doom2/a-c/area51.zip",
        "AREA51.WAD",
        "1.1 MB",
        true,
        false
    },
    {
        "ARMADOSIA THE MAD CORRIDOR V2.1",
        "arma kero",
        "MEGAWAD",
        "DOOM2.WAD",
        "Large, complex levels, with a focus on exploration",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/amdosia.zip",
        "amdosia.wad",
        "9.9 MB",
        true,
        false
    },
    {
        "ASTRO.WAD FOR DOOM2",
        "Brandon Phillips",
        "MEGAWAD",
        "DOOM2.WAD",
        "8 levels of pure fun and carnage",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/a-c/astro.zip",
        "astro.wad",
        "0.1 MB",
        true,
        false
    },
    {
        "ASTROSTEIN 2",
        "Laz Rojas",
        "MEGAWAD",
        "DOOM2.WAD",
        "10-level WolfenDOOM adventure, featuring new",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/themes/wolf3d/astro2.zip",
        "astro2.wad",
        "2.6 MB",
        true,
        false
    },
    {
        "AWESOMUS.WAD FOR DOOM 2",
        "Greg Kegel",
        "MEGAWAD",
        "DOOM2.WAD",
        "I just thought I tell you that this is my very first...",
        "It is, I'll admit, a very simple, music only addition",
        "https://youfailit.net/pub/idgames/music/awesomus.zip",
        "AWESOMUS.WAD",
        "0.2 MB",
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
        "BACK TO SATURN X E1: GET OUT OF MY ST...",
        "Esselfortium and a lot of w...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Get Out Of My Stations is the first episode of Back ...",
        " Back to Saturn X is fully compatible with Doom2",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/btsx_e1.zip",
        "btsx_e1.wad",
        "17.5 MB",
        true,
        false
    },
    {
        "BARBIE GIRL",
        "Adam Windsor / Capellan",
        "MEGAWAD",
        "DOOM2.WAD",
        "The UAC has been conducting experiments on demons, a...",
        " They've lost contact with the base where they were ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/a-c/barbie.zip",
        "barbie.wad",
        "0.2 MB",
        true,
        false
    },
    {
        "BASE GANYMEDE: COMPLETE",
        "Adam 'Khorus' Woodmansey",
        "MEGAWAD",
        "DOOM2.WAD",
        "A 3 episode vanilla megawad for Doom 1 containing a ...",
        "Set on one of Jupiter's moons called Ganymede, this ...",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/bgcomp.zip",
        "BGComp.wad",
        "2.1 MB",
        true,
        false
    },
    {
        "BATMAN.WAD",
        "Greg Wood, aka 'Woody'",
        "MEGAWAD",
        "DOOM2.WAD",
        "A Deathmatch level, but fully implemented for the we...",
        "It has 2 ways to exit, one of them being cooperative",
        "https://youfailit.net/pub/idgames/levels/doom/deathmatch/a-c/batman.zip",
        "batman.wad",
        "0.0 MB",
        true,
        false
    },
    {
        "BELLA II",
        "Paul Corfiatis and Chris Ha...",
        "MEGAWAD",
        "DOOM2.WAD",
        "35 Levels for Zdoom, Plus 5 secret levels",
        "Each level has all new challenges awaiting you",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/bella2.zip",
        "bella2.wad",
        "6.8 MB",
        true,
        false
    },
    {
        "BETRAY (XBOX MAP33)",
        "Brendon Chung",
        "SECRET LEVEL",
        "DOOM2.WAD",
        "Das geheime 33. Bonuslevel der Xbox Doom II Version.",
        "Spannendes raetsellastiges Leveldesign.",
        "https://archive.org/download/doom-wads/Betray%20%28Xbox%20Doom%20II%29%20%5BFixed%5D.zip",
        "BETRAY.WAD",
        "0.1 MB",
        true,
        false
    },
    {
        "BEYOND THE HELL",
        "Petr Machata, Milan Kuchynk...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Megawad created by four Czech Doomers See the origin...",
        "TXT) for further descriptions and information",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/bth.zip",
        "bth.wad",
        "1.3 MB",
        true,
        false
    },
    {
        "BLASPHEMER",
        "Blasphemer Community Project",
        "TOTAL CONVERSION",
        "DOOM.WAD",
        "Freies Dark-Fantasy Spiel im Geiste von Heretic.",
        "Duestere Kathedralen und eigene Monster.",
        "https://archive.org/download/doom-wads/Blasphemer%20%28v0.1.5%29.zip",
        "BLASPHEM.WAD",
        "2.5 MB",
        true,
        true
    },
    {
        "BLOOD.WAD",
        "Eric",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a simple Dehacked 1",
        "3 patch which leaves blood marks on the ground where...",
        "https://youfailit.net/pub/idgames/utils/exe_edit/patches/blood.zip",
        "blood.wad",
        "0.0 MB",
        true,
        false
    },
    {
        "BLOODSTAIN",
        "Pavel 'Pipicz' Tvrzník",
        "MEGAWAD",
        "DOOM2.WAD",
        "Full 32-map megawad",
        "See BStainfo",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/bstain.zip",
        "bstain.wad",
        "11.1 MB",
        true,
        false
    },
    {
        "BURY MY HEART KNEE DEEP",
        "Ryath/scwiba",
        "MEGAWAD",
        "DOOM2.WAD",
        "*** IMPORTANT: Saving in vanilla (or Chocolate) Doom...",
        "If you want to save, you'll need a limit-removing port",
        "https://youfailit.net/pub/idgames/levels/doom2/j-l/kneedeep.zip",
        "kneedeep.wad",
        "0.8 MB",
        true,
        false
    },
    {
        "CABIN - AN APOCALYPTIC NIGHTMARE",
        "UndeadRyker",
        "MEGAWAD",
        "DOOM2.WAD",
        "The events of CABIN occur directly after DOOM II, se...",
        "---- After you defeated the Icon of Sin, a portal ba...",
        "https://youfailit.net/pub/idgames/levels/doom2/a-c/cabin.zip",
        "cabin.wad",
        "0.8 MB",
        true,
        false
    },
    {
        "CABRO'S LEGACY",
        "Various (List of maps at th...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Cabro's Legacy is a compilatory megawad, created in ...",
        "It contains maps from the top three winners from eac...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/clegacy.zip",
        "clegacy.wad",
        "16.5 MB",
        true,
        false
    },
    {
        "CASTLE DOOM",
        "Chris Weathers",
        "MEGAWAD",
        "DOOM2.WAD",
        "Single-player megawad that pits the player against t...",
        "I tried to keep it generic so that the various advan...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/cstldoom.zip",
        "cstldoom.wad",
        "35.5 MB",
        true,
        false
    },
    {
        "CHAINGUNNER'S WAD PACK",
        "Chainguner",
        "MEGAWAD",
        "DOOM2.WAD",
        "Levels by Chaingunner",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/chain.zip",
        "chain.wad",
        "2.2 MB",
        true,
        false
    },
    {
        "CHEX QUEST",
        "Digital Cafe",
        "TOTAL CONVERSION",
        "DOOM.WAD",
        "Legendaere Total Conversion mit dem Zorcher.",
        "Vollstaendig familienfreundlich mit eigenen Texturen.",
        "https://archive.org/download/doom-wads/Chex%20Quest.zip",
        "CHEX.WAD",
        "3.9 MB",
        true,
        true
    },
    {
        "CHEX QUEST 2: FLEMOIDS CHEK",
        "Digital Cafe",
        "TOTAL CONVERSION",
        "DOOM.WAD",
        "Offizieller zweiter Teil auf Chex City.",
        "5 neue Level mit neuen Flem-Gegnern.",
        "https://archive.org/download/doom-wads/Chex%20Quest%202.zip",
        "CHEX2.WAD",
        "2.1 MB",
        true,
        false
    },
    {
        "CHEX QUEST 3",
        "Charles Jacobi",
        "TOTAL CONVERSION",
        "DOOM.WAD",
        "Teil 3 der beliebten Chex Quest Reihe.",
        "Entwickelt vom originalen Chex-Kuenstler.",
        "https://archive.org/download/doom-wads/Chex%20Quest%203%20%28v1.4%29.zip",
        "CHEX3.WAD",
        "6.3 MB",
        true,
        true
    },
    {
        "CHRISWAD V6.1",
        "Chris Polan",
        "MEGAWAD",
        "DOOM2.WAD",
        "30 levels for Doom II v1",
        "9 - 20 levels support all play modes, and there's 10...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/chris61.zip",
        "chris61.wad",
        "1.8 MB",
        true,
        false
    },
    {
        "CITY OF DOOM (PART I) BETA 1-11",
        "Paul Maurone (a.k.a. `Nowot...",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a 'plain vanilla DooM ][' PWAD of maps 1-11",
        " I tried to put the player in the city as best as po...",
        "https://youfailit.net/pub/idgames/levels/doom2/a-c/cod1-11.zip",
        "Cod1-11.wad",
        "3.0 MB",
        true,
        false
    },
    {
        "CLAUSTROPHOBIA 1024",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "Claustrophobia 1024 is the community-made, inspirati...",
        "It's comprised of 32 brand-new maps restricting the ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/1024clau.zip",
        "1024clau.wad",
        "7.6 MB",
        true,
        false
    },
    {
        "CLAUSTROPHOBIA 1024 2: THE MYSTERY OF...",
        "Jodwin",
        "MEGAWAD",
        "DOOM2.WAD",
        "Claustrophobia 1024 2 is the community-made sequel t...",
        "It's comprised of 34 brand-new maps restricting the ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/1024cla2.zip",
        "1024cla2.wad",
        "8.7 MB",
        true,
        false
    },
    {
        "CODENAME HYENA KILLERMACHINE",
        "Unholy Software / http://ww...",
        "MEGAWAD",
        "DOOM.WAD",
        "This is the rerelease of our old old old TC Hyena, w...",
        "It's not testted this time around and it probaly got...",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/hyena.zip",
        "hyena.wad",
        "3.8 MB",
        true,
        false
    },
    {
        "COLD FRONT",
        "Egg Boy",
        "MEGAWAD",
        "DOOM2.WAD",
        "After 4 months of on and off work, Cold Front is fin...",
        "Really, I designed around 7 or 8 maps, but a number ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/a-c/cldfrnt.zip",
        "CLDFRNT.WAD",
        "1.6 MB",
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
        "COMMUNITY IS FALLING 3",
        "Samuel Villarreal (svkaiser...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Community is Falling 3 is the third sequal to the 'C...",
        "Lots of inside jokes are involved so those who are o...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/cif3.zip",
        "cif3.wad",
        "45.5 MB",
        true,
        false
    },
    {
        "CONC.E.R.NED",
        "Matt 'cannonball' Powell",
        "MEGAWAD",
        "DOOM.WAD",
        "3 episode wad for ultimate doom with the usual tech/...",
        "This is an expansion from my cbspeed",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/concern.zip",
        "concern.wad",
        "3.5 MB",
        true,
        false
    },
    {
        "CONGESTION 1024",
        "Various mappers",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 maps no bigger than 1024 X 1024",
        "Some maps have areas outside the 1024 X 1024 block b...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/1024.zip",
        "1024.wad",
        "3.6 MB",
        true,
        false
    },
    {
        "COOP BUILD",
        "Toke (Dylan McIntosh)",
        "MEGAWAD",
        "DOOM2.WAD",
        "Toke's coop wad",
        "Have fun playing it",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/cbuildlm.zip",
        "cbuildlm.wad",
        "1.4 MB",
        true,
        false
    },
    {
        "CORE",
        "Jake 'The Nutcracker' Stevens",
        "MEGAWAD",
        "DOOM2.WAD",
        "A (almost) TC that follows the idea of making a seri...",
        "It was supposed to follow the 'classic' doom2 style ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/core.zip",
        "core.wad",
        "5.2 MB",
        true,
        false
    },
    {
        "COUNTDOWN TO EXTINCTION",
        "Obsidian",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 maps for vanilla Doom, each one made in one hour ...",
        "Bear in mind that all of these maps were designed fo...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/ticktock.zip",
        "ticktock.wad",
        "1.1 MB",
        true,
        false
    },
    {
        "DANCE",
        "Sasha",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dance.zip",
        "dance.wad",
        "7.3 MB",
        true,
        false
    },
    {
        "DANIEL LEVEL SERIE 02",
        "Daniel",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a 30 level wad with small maps, made for use...",
        "The ZDoom bots are a little 'stupid', so some pickup...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/dal02.zip",
        "dal02.wad",
        "1.0 MB",
        true,
        false
    },
    {
        "DARK TARTARUS: DEAD TONIGHT",
        "lihanchi",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tartaru3.zip",
        "tartaru3.wad",
        "56.1 MB",
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
        "DEATHZ0R ONLINE: THE INTERNET MADE ST...",
        "Dean 'deathz0r' Joseph",
        "MEGAWAD",
        "DOOM2.WAD",
        "This wad is better than your pile of crap",
        " Read the story below for a real description",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/do-tims.zip",
        "do-tims.wad",
        "3.9 MB",
        true,
        false
    },
    {
        "DEMISE",
        "Nathan Pagliaro",
        "MEGAWAD",
        "DOOM.WAD",
        "One man, one difficulty and a lot of demons",
        "Battle you way through against demons in order to es...",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/demise1.zip",
        "demise1.wad",
        "2.4 MB",
        true,
        false
    },
    {
        "DEMONFEAR",
        "Adam Windsor",
        "MEGAWAD",
        "DOOM2.WAD",
        "This WAD replaces all 32 levels of Doom2",
        "All levels are playable solo, co-op or DM, to give y...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/dmonfear.zip",
        "Dmonfear.wad",
        "0.7 MB",
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
        "DMP2014",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "27 maps by 27 different mappers",
        "There were no rules and all maps were accepted",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dmp2014.zip",
        "dmp2014.wad",
        "5.4 MB",
        true,
        false
    },
    {
        "DOOM 2 IN A NUTSHELL",
        "Invertcube and dpImagine",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/smdoom2c.zip",
        "smdoom2c.wad",
        "1.4 MB",
        true,
        false
    },
    {
        "DOOM 2 IN NAME ONLY",
        "Various, see below",
        "MEGAWAD",
        "DOOM2.WAD",
        "Imagine a world where Doom II was never released, an...",
        "Then imagine that nearly 20 years later, some people...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/d2ino.zip",
        "D2INO.wad",
        "6.4 MB",
        true,
        false
    },
    {
        "DOOM 2 IN SPAIN ONLY",
        "AD-79, Alfonzo, Breezeep, E...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Vanilla-compatable megawad",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/d2isov2.zip",
        "D2ISOv2.wad",
        "3.8 MB",
        true,
        false
    },
    {
        "DOOM 2 RELOADED",
        "Andy Stewart",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 Level Megawad for use with Doom2",
        "Hopefully a fun packed collection of mainly small to...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/d2reload.zip",
        "D2RELOAD.WAD",
        "4.2 MB",
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
        "DOOM 2 UNLEASHED (PCORF COMMUNITY PRO...",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "Welcome to DOOM 2: UNLEASHED - An all new 32 level m...",
        "Known as the Pcorf Community Project at the Doomworl...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/pc_cp.zip",
        "pc_cp.wad",
        "9.0 MB",
        true,
        false
    },
    {
        "DOOM 2099 ADDON",
        "Levels: MAP01-24 Justin Mad...",
        "MEGAWAD",
        "DOOM2.WAD",
        "STORY SO FAR",
        " The year is 2099",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/doom2099.zip",
        "doom2099.wad",
        "3.0 MB",
        true,
        false
    },
    {
        "DOOM 32 (RELEASE II)",
        "Michel Remacle, Jeremy Wagn...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A set of 32 carefully collected WADfiles for your en...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/doom32r2.zip",
        "doom32r2.wad",
        "2.9 MB",
        true,
        false
    },
    {
        "DOOM 404",
        "Adam Windsor / Capellan",
        "MEGAWAD",
        "DOOM2.WAD",
        "It's a sad life on the Space Marine IT Help Desk",
        "Especially when you're stuck on the graveyard shift",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/doom404.zip",
        "doom404.wad",
        "1.0 MB",
        true,
        false
    },
    {
        "DOOM CITY",
        "Shams Young",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a one level WAD for DOOM II that is designed...",
        "This level is packed with new graphics, and is desig...",
        "https://youfailit.net/pub/idgames/levels/doom2/d-f/doomcity.zip",
        "DOOMCITY.WAD",
        "0.1 MB",
        true,
        false
    },
    {
        "DOOM CORE",
        "Michael Jan Krizk (Valkirif...",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a collection of 32 levels previously unrelea...",
        "WAD) this is a collection of levels I made from Nove...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/dcv.zip",
        "DCV.wad",
        "3.4 MB",
        true,
        false
    },
    {
        "DOOM II: NO REST FOR THE LIVING",
        "Nerve Software / id Software",
        "EPISODE",
        "DOOM2.WAD",
        "Offizielle 9-Level Erweiterung von Nerve Software.",
        "Fantastisches Leveldesign der BFG Edition.",
        "https://archive.org/download/doom-wads/Doom%20II%20-%20No%20Rest%20for%20the%20Living%20%28BFG%20Edition%29.zip",
        "NERVE.WAD",
        "1.3 MB",
        true,
        false
    },
    {
        "DOOM MONSTER REPLACEMENT - ZOMBIES",
        "Daniel",
        "MEGAWAD",
        "DOOM2.WAD",
        "This wad replaces most of the Doom monsters with Zom...",
        "Very funny if you're tired of Imps",
        "https://youfailit.net/pub/idgames/combos/zombie.zip",
        "ZOMBIE.WAD",
        "0.4 MB",
        true,
        false
    },
    {
        "DOOM THE WAY ID DID",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "'At some point in time I got the idea that it would ...",
        "He once replied to an email saying 'maybe I will",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/dtwid.zip",
        "DTWID.wad",
        "1.9 MB",
        true,
        false
    },
    {
        "DOOM THE WAY ID DID - THE LOST EPISODES",
        "Various",
        "MEGAWAD",
        "DOOM.WAD",
        "Six episodes of classic Doom maps, originally candid...",
        " DTWID was a long but fruitful journey, with somewhe...",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/dtwid-le.zip",
        "dtwid-le.wad",
        "3.7 MB",
        true,
        false
    },
    {
        "DOOM: A MEGAWAD IN TWO WEEKS",
        "Various, in no particular o...",
        "MEGAWAD",
        "DOOM2.WAD",
        "The idea behind this was to create a classic DOOM Me...",
        "You can follow the development of the project at thi...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/megaw2.zip",
        "megaw2.wad",
        "2.3 MB",
        true,
        false
    },
    {
        "DOOM: DAMNATION",
        "Ofisil",
        "MEGAWAD",
        "DOOM2.WAD",
        "MegaWad for The Ultimate Doom, with the purpose to b...",
        "Meant to be experienced in pure vanilla fashion",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/ddamn10.zip",
        "DDAMN10.wad",
        "2.4 MB",
        true,
        false
    },
    {
        "DOOM: UNCUT",
        "CheapAlert",
        "MEGAWAD",
        "DOOM2.WAD",
        "Makes the monsters anatomically correct again, with ...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/themes/x-rated/i_am_old_enough_to_look_at_this/doomuc.zip",
        "doomu.wad",
        "0.1 MB",
        true,
        false
    },
    {
        "DOOMII DEMO SCREENSHOTS",
        "id Software",
        "MEGAWAD",
        "DOOM2.WAD",
        "These are the",
        "PCX files from John Romero's DOOMII demo during the ...",
        "https://youfailit.net/pub/idgames/graphics/doom2pcx.zip",
        "doom2.wad",
        "0.7 MB",
        true,
        false
    },
    {
        "DOOMWORLD MEGA PROJECT 2012",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "Harmata said it best: 'I could've picked random wads...",
        "' The goal of this project was to get as many mapper...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dmp2012.zip",
        "dmp2012.wad",
        "4.2 MB",
        true,
        false
    },
    {
        "DOOMWORLD MEGA PROJECT 2013",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "54 maps by 55 different mappers in 3 wads",
        "com/vb/wads-mods/64899-",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dmp2013.zip",
        "dmp2013.wad",
        "26.5 MB",
        true,
        false
    },
    {
        "DOOMWORLD MEGA PROJECT 2015",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "The idea and goal of the project was to get maps fro...",
        "Anyone could participate, and there were no restrict...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dmp2015.zip",
        "dmp2015.wad",
        "17.6 MB",
        true,
        false
    },
    {
        "DOUBLE IMPACT",
        "RottKing & Ralphis",
        "MEGAWAD",
        "DOOM2.WAD",
        "A nine map episode 1 replacement",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/d-f/dbimpact.zip",
        "dbimpact.wad",
        "1.3 MB",
        true,
        false
    },
    {
        "DSV EPISODE 4: THE REVOLUTION",
        "Samuel Villarreal, Yves 'ev...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dsv4_rvltinL.zip",
        "dsv4_rvltinl.wad",
        "6.7 MB",
        true,
        false
    },
    {
        "DSV EPISODE 4: THE REVOLUTION",
        "Samuel Villarreal, Yves 'ev...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dsv4_rvltinE.zip",
        "dsv4_rvltine.wad",
        "6.6 MB",
        true,
        false
    },
    {
        "DSV3-WAR",
        "Samuel A. Villarreal",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 tough levels that were inspired by Plutonia, Mome...",
        "The levels aren't super detailed like Darkening or E...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/war_3.zip",
        "war_3.wad",
        "5.5 MB",
        true,
        false
    },
    {
        "DTS-T",
        "Ingmar Derks ('Deathmatcher')",
        "MEGAWAD",
        "DOOM2.WAD",
        "A hidden UAC base on Phobos: Not satisfied with thei...",
        "There were two prototypes of this machine but one mo...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dts-t.zip",
        "dts-t.wad",
        "45.2 MB",
        true,
        false
    },
    {
        "DUMP EPISODE 1: FUCK TIME LIMITS",
        "TerminusEst13 (et al)",
        "MEGAWAD",
        "DOOM2.WAD",
        "Mapping is hard",
        " A lot of us have been wanting to map for a while, b...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dump-1.zip",
        "dump-1.wad",
        "14.1 MB",
        true,
        false
    },
    {
        "ECHELON",
        "Sverre Andre Kvernmo",
        "MEGAWAD",
        "DOOM2.WAD",
        "A megawad full of small & fast levels",
        "A reimagining of the Doom II storyline",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/echelon.zip",
        "echelon.wad",
        "7.3 MB",
        true,
        false
    },
    {
        "ENDLESS TORTURE",
        "Maxime Bisiaux 'Datacore'",
        "MEGAWAD",
        "DOOM.WAD",
        "36 maps with various difficulties",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/et.zip",
        "et.wad",
        "1.6 MB",
        true,
        false
    },
    {
        "ENJAY ZDOOM 2001",
        "Nigel Rowand (Enjay)",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 Levels replacing Doom2, basically following the D...",
        "A reworking of my old Enjay Doom project (originally...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/njzd2001.zip",
        "njzd2001.wad",
        "3.5 MB",
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
        "ESP",
        "TimeOfDeath",
        "MEGAWAD",
        "DOOM2.WAD",
        "33 hard maps that I tested myself",
        "Max demos for all maps included (speed demo for map30)",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/esp.zip",
        "esp.wad",
        "9.8 MB",
        true,
        false
    },
    {
        "ESTRANGED",
        "Foodles",
        "MEGAWAD",
        "DOOM2.WAD",
        "Estranged is a 32 level megawad that uses stock Doom...",
        "Levels are playable from pistol start",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/estrangd.zip",
        "estrangd.wad",
        "4.9 MB",
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
        "ETERNALLY YOURS",
        "Michael Jan Krizik (valkiri...",
        "MEGAWAD",
        "DOOM2.WAD",
        "This concludes the Doom Core trilogy",
        "Eternally Yours is an 11-map episode that should wor...",
        "https://youfailit.net/pub/idgames/levels/doom2/d-f/etrnyour.zip",
        "ETRNYOUR.wad",
        "1.3 MB",
        true,
        false
    },
    {
        "EVILTECH: SOUL OF MEGAWAD (COMPATIBIL...",
        "Nicolás Monti",
        "MEGAWAD",
        "DOOM2.WAD",
        "classic doom megawad with 34 maps",
        " I started this megawad in 2002",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/eviltecc.zip",
        "eviltecc.wad",
        "6.1 MB",
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
        "FAVA BEANS",
        "Sean Birkel",
        "MEGAWAD",
        "DOOM2.WAD",
        "Well, this is a very impressive episode including 9 ...",
        "All the levels in this episode were created by mysel...",
        "https://youfailit.net/pub/idgames/levels/doom/0-9/01fava.zip",
        "FAVA.WAD",
        "0.4 MB",
        true,
        false
    },
    {
        "FINAL FANTASY DOOM (VERSION 2)",
        "Original version Ramon Muji...",
        "MEGAWAD",
        "DOOM.WAD",
        "Final Fantasy 'the best Japanese RPG ever' meets Doo...",
        " Final Fantasy Doom is a Total Conversion for Ultima...",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/ffdoom2.zip",
        "ffdoom2.wad",
        "2.3 MB",
        true,
        false
    },
    {
        "FIVE ROOMS OF DOOM",
        "Various (Project leader: Ke...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A Doomworld community project, startes by Keeper of ...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/5room.zip",
        "5room.wad",
        "11.0 MB",
        true,
        false
    },
    {
        "FRAGPORT",
        "Stephen Clark (The Ultimate...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A 21st century DooM 2 episode that is big, innovativ...",
        "This wad features desert and mountain levels, and th...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/fragport.zip",
        "fragport.wad",
        "3.3 MB",
        true,
        false
    },
    {
        "FREEDOOM: FREEDM",
        "Freedoom Project",
        "STANDALONE",
        "DOOM2.WAD",
        "Freie Deathmatch-orientierte Arena-Kampagne (32 Level).",
        "Fokussiert auf schnelle Schusswechsel & Layouts.",
        "https://archive.org/download/doom-wads/Freedoom%20-%20FreeDM%20%28v0.11.3%29.zip",
        "FREEDM.WAD",
        "7.4 MB",
        true,
        true
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
        "FREUDIAN SLIPGATE",
        "Alter, Obsidian, Pinchy, Ge...",
        "MEGAWAD",
        "DOOM2.WAD",
        "You are playing Quake, you walk into the Slipgate on...",
        "#doomtwid realm, you have angered the #doomtwid gods...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/freudslp.zip",
        "freudslp.wad",
        "9.5 MB",
        true,
        false
    },
    {
        "GARRULISMO (SPANISH SLANG THAT I CAN'...",
        "Eye del Cul",
        "MEGAWAD",
        "DOOM2.WAD",
        "A complete episode for Doom2, with all 32 maps replaced",
        "I think that these are quality levels: cool architec...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/garrulo.zip",
        "garrulo.wad",
        "2.3 MB",
        true,
        false
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
        "GOLDENEYEDOOM2 TC (FOR DOOM2 V1.9 AND...",
        "GoldenEye Doom2 Team",
        "MEGAWAD",
        "DOOM2.WAD",
        "(brief) GoldenEye Doom2 TC",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/gedoom2.zip",
        "gedoom2.wad",
        "3.6 MB",
        true,
        false
    },
    {
        "GZDOOM SPEEDMAPPING SESSION #4",
        "Cyberdemon531",
        "MEGAWAD",
        "DOOM2.WAD",
        "30 maps made in under FOUR hours",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/gzspd4.zip",
        "gzspd4.wad",
        "4.1 MB",
        true,
        false
    },
    {
        "HACX: TWITCH 'N KILL (V1.2)",
        "Banjo Software",
        "TOTAL CONVERSION",
        "DOOM2.WAD",
        "Cyberpunk Total Conversion mit 21 neuen Leveln.",
        "Eigene Cyber-Waffen, Soundtracks & Gegner.",
        "https://archive.org/download/doom-wads/HACX%20%28v1.2%29.zip",
        "HACX.WAD",
        "7.3 MB",
        true,
        true
    },
    {
        "HADEPHOBIA",
        "Team Progfic (Various)",
        "MEGAWAD",
        "DOOM2.WAD",
        "A PRBoom+ complevel 9 megawad resulting from the Pro...",
        "The project's aim was to create a community project ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/h_phobia.zip",
        "h_phobia.wad",
        "7.0 MB",
        true,
        false
    },
    {
        "HARMONY",
        "Thomas van der Velden",
        "TOTAL CONVERSION",
        "DOOM2.WAD",
        "Vollstaendig handgezeichnete Sci-Fi TC (13 Level).",
        "Eigene Welt, Waffen und Soundtrack.",
        "https://archive.org/download/doom-wads/Harmony%20%28v1.1%29.zip",
        "HARM1.WAD",
        "39.8 MB",
        true,
        true
    },
    {
        "HELL AWAKENED...",
        "Death Dealer, insanoflex312",
        "MEGAWAD",
        "DOOM2.WAD",
        "Read the read me for full story 32 levels of difficu...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/ha.zip",
        "ha.wad",
        "4.8 MB",
        true,
        false
    },
    {
        "HELL AWAKENED...",
        "Death Dealer",
        "MEGAWAD",
        "DOOM2.WAD",
        "Read the read me for full story 32 levels of difficu...",
        "This version is a patched version of the origional",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/hav2.zip",
        "hav2.wad",
        "8.2 MB",
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
        "HELL RUN - ANOTHER RELEASE FROM RUN L...",
        "Jonathan Jordan",
        "MEGAWAD",
        "DOOM2.WAD",
        "* Full replacement for Doom Epesode Two * Support fo...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom/g-i/hellrun.zip",
        "HELLRUN.WAD",
        "0.5 MB",
        true,
        false
    },
    {
        "HELL TO PAY",
        "Wraith Corporation",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 Level Sci-Fi Megawad von Wraith Corporation.",
        "Zukunftskulisse, neue Sprites & Texturen.",
        "https://archive.org/download/doom-wads/Hell%20to%20Pay.zip",
        "HTP-RAW.WAD",
        "4.0 MB",
        true,
        false
    },
    {
        "HELLBOUND",
        "Zoltán Sófalvi (Z86)",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 map megawad with collections of maps made from di...",
        "All old maps are re-polished to make them consistent...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/hellbnd.zip",
        "hellbnd.wad",
        "16.7 MB",
        true,
        false
    },
    {
        "HELLCORE",
        "Robert Babor (Ship by Devon...",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 levels from scratch",
        "All varied in year and quality, but what can ya do",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/hellcore.zip",
        "hellcore.wad",
        "22.5 MB",
        true,
        false
    },
    {
        "HELLISH ROGUES",
        "Rymante",
        "MEGAWAD",
        "DOOM2.WAD",
        "Your squad was sent to investigate a rogue research ...",
        " Shortly after landing, your squad is ambushed by an...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/g-i/helrogues.zip",
        "HELROGUES.WAD",
        "3.9 MB",
        true,
        false
    },
    {
        "HERIAN (REVISION 5, 04/06/1998)",
        "Ian Wilson",
        "MEGAWAD",
        "DOOM2.WAD",
        "Herian has no story it is simply a 32 level TC made ...",
        "The infuence for it came from my favourite games men...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/herian.zip",
        "herian.wad",
        "3.3 MB",
        true,
        false
    },
    {
        "HERIAN2 (REVISION 0, 24/01/1999)",
        "Ian Wilson",
        "MEGAWAD",
        "DOOM2.WAD",
        "Herian2 is fully compatible with only ZDoom version 1",
        "17 or later",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/herian2.zip",
        "herian2.wad",
        "7.3 MB",
        true,
        false
    },
    {
        "HEROES",
        "AL",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Doom Community Meisterwerk",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/heroes.zip",
        "HEROES.WAD",
        "2.3 MB",
        true,
        false
    },
    {
        "HEROES 2",
        "AL",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Doom Community Meisterwerk",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/heroes2.zip",
        "HEROES2.WAD",
        "2.5 MB",
        true,
        false
    },
    {
        "HEROES' TALES",
        "Shadowman",
        "MEGAWAD",
        "DOOM2.WAD",
        "The project from iddqd",
        "ru the community, basis of which are maps, made in a...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/ht.zip",
        "ht.wad",
        "4.5 MB",
        true,
        false
    },
    {
        "HOOVER DAM",
        "Michael Reed",
        "MEGAWAD",
        "DOOM2.WAD",
        "A large new level for Doom 2",
        "I've tried to create something really unique with th...",
        "https://youfailit.net/pub/idgames/levels/doom2/g-i/hoover.zip",
        "HOOVER.WAD",
        "0.1 MB",
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
        "INFECTED TELECOM STATIONS V1.2",
        "Karoly (Charles) Horvath",
        "MEGAWAD",
        "DOOM2.WAD",
        "A 16+2 level wad, technical reality + tactical fight",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/telekom.zip",
        "telekom.wad",
        "3.8 MB",
        true,
        false
    },
    {
        "INTERCEPTION",
        "The Interceptors",
        "MEGAWAD",
        "DOOM2.WAD",
        "Interception is a vanilla-compatible megawad for DOO...",
        " In the far distant future, you are part of a team o...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/intercep.zip",
        "intercep.wad",
        "3.8 MB",
        true,
        false
    },
    {
        "INVASION 1.9",
        "Paul Fleschute",
        "MEGAWAD",
        "DOOM.WAD",
        "Alot of people seemed to be blown away by Invasion V1",
        "Well, you people ain't seen nothin' yet",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/inva_19.zip",
        "inva_19.wad",
        "2.6 MB",
        true,
        false
    },
    {
        "INVASION II V.1.0B",
        "Paul Fleschute",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is sorta a BETA version of Invasion II, the nex...",
        "And I will make a full attempt to kill ALL the bugs",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/inva2_09b.zip",
        "inva2_09b.wad",
        "3.4 MB",
        true,
        false
    },
    {
        "ITALO-DOOM",
        "Dannebubinga & Ribbiks",
        "MEGAWAD",
        "DOOM2.WAD",
        "Smoking hard inside a bar, I'm drinking like a fish",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/italo.zip",
        "italo.wad",
        "1.6 MB",
        true,
        false
    },
    {
        "JADE EARTH",
        "Jodwin",
        "MEGAWAD",
        "DOOM2.WAD",
        "A massive UAC base hidden deep underground",
        "Save, soldier, because you're going to need it (par ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/j-l/jadearth.zip",
        "jadearth.wad",
        "1.4 MB",
        true,
        false
    },
    {
        "JAPANESE COMMUNITY PROJECT",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "Japanese Community Project is a 32 level megawad mad...",
        " Doom is a minor game in Japan, so Doom community in...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/jpcp.zip",
        "jpcp.wad",
        "12.7 MB",
        true,
        false
    },
    {
        "JENESIS",
        "James 'Jimmy' Paddock",
        "MEGAWAD",
        "DOOM2.WAD",
        "The third and final part of my solo megawad",
        "Part 3 of 'Jenesis' adds 12 extra maps to the first ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/jenesis.zip",
        "jenesis.wad",
        "6.4 MB",
        true,
        false
    },
    {
        "JOW: JASON WAINMAN'S 30 LEVELS OF DOO...",
        "Jason Wainman",
        "MEGAWAD",
        "DOOM2.WAD",
        "These levels, which were the results of four months ...",
        " However, I did notice a few things that I felt need...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/jow.zip",
        "jow.wad",
        "1.0 MB",
        true,
        false
    },
    {
        "KAMA SUTRA",
        "Adolf 'Gusta' Vojta, Jakub ...",
        "MEGAWAD",
        "DOOM2.WAD",
        "we made 32 maps, finally, so you all 'HR-skilled' pl...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/ksutra.zip",
        "KSUTRA.WAD",
        "4.8 MB",
        true,
        false
    },
    {
        "KHORUS' SPEEDY SHIT",
        "Adam 'Khorus' Woodmansey",
        "MEGAWAD",
        "DOOM2.WAD",
        "A vanilla megawad for Doom 2 containing 32 short, ch...",
        "The wad's premise was to create at least one speedma...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/kssht.zip",
        "kssht.wad",
        "1.1 MB",
        true,
        false
    },
    {
        "KILLING ADVENTURE",
        "Ruba",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is an all new a 32 level episode for doom2",
        "You must adventure through many unique worlds killin...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/killadve.zip",
        "killadve.wad",
        "1.2 MB",
        true,
        false
    },
    {
        "KNEE-DEEP IN ZDOOM",
        "KDIZD Team (See CREDITS.TXT...",
        "MEGAWAD",
        "DOOM.WAD",
        "The experience of playing through Knee-Deep in the D...",
        " Knee-Deep in ZDoom is an interpretation of Id Softw...",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/megawads/kdizd_12.zip",
        "kdizd_12.wad",
        "23.1 MB",
        true,
        false
    },
    {
        "LAST DAY ON EARTH EPISODE I",
        "Doom Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/lastep1.zip",
        "lastep1.wad",
        "1.0 MB",
        true,
        false
    },
    {
        "LICENSE TO ILYA",
        "joe-ilya and friends",
        "MEGAWAD",
        "DOOM2.WAD",
        "A tune-up community project led, compiled and with n...",
        "Each map is a tune-up of joe-ilya's earlier works su...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/joetune.zip",
        "joetune.wad",
        "5.4 MB",
        true,
        false
    },
    {
        "LUNAR BASE",
        "Mark Barry",
        "MEGAWAD",
        "DOOM2.WAD",
        "Designed for Deathmatch 2 for DOOM II Weapons: -----...",
        "Lots of hidden areas and traps/tricks 2",
        "https://youfailit.net/pub/idgames/levels/doom2/j-l/lunar.zip",
        "Lunar.Wad",
        "0.1 MB",
        true,
        false
    },
    {
        "MAGIC DOOM",
        "Magicsofa",
        "MEGAWAD",
        "DOOM2.WAD",
        "Staring into the glowing teleporter, you vaguely rem...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/magicsof.zip",
        "Magicsof.wad",
        "2.5 MB",
        true,
        false
    },
    {
        "MANO LAIKAS: A ROAD TO GAMZATTI",
        "Nicolas Monti",
        "MEGAWAD",
        "DOOM2.WAD",
        "Well, I'm back",
        "I thought Erkattäññe was my last shot but I wanted t...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/manolaik.zip",
        "manolaik.wad",
        "3.5 MB",
        true,
        false
    },
    {
        "MARSWAR V3.01",
        "Nathan Lineback",
        "MEGAWAD",
        "DOOM2.WAD",
        "You are a Martian terraformer stuck in a small stati...",
        " Since the beginning of the terraforming project, th...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/marsw301.zip",
        "marsw301.wad",
        "4.2 MB",
        true,
        false
    },
    {
        "MASS EXTINCTION",
        "Nicolas Monti",
        "MEGAWAD",
        "DOOM2.WAD",
        "Full 32-map megawad done in the styles I like the mo...",
        "the mapset is divided into 4 episodes and it covers ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/massexe.zip",
        "massexe.wad",
        "3.1 MB",
        true,
        false
    },
    {
        "MAYAN REYNOLDS",
        "AD_79, Bobby, DCG Retrowave...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Mayan Reynolds is a small vanilla compatible episode...",
        "The WAD was speed-map project made over the course o...",
        "https://youfailit.net/pub/idgames/levels/doom2/m-o/mynrnlds.zip",
        "MYNRNLDS.WAD",
        "1.5 MB",
        true,
        false
    },
    {
        "MAYHEM 1500 AKA MAYHEM2015 (V1.50)",
        "A buncha schmucks",
        "MEGAWAD",
        "DOOM2.WAD",
        "After three successful(ish) endeavours and a year of...",
        "This time, it's a very gothic edition of MAYhem wher...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/mayhem15.zip",
        "mayhem15.wad",
        "4.2 MB",
        true,
        false
    },
    {
        "MAYHEM 2048 AKA MAYHEM2014",
        "A buncha schmucks",
        "MEGAWAD",
        "DOOM2.WAD",
        "After Two successful",
        "ish endeavors, the MAYhem crew is back for its bigge...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/mayhem14.zip",
        "mayhem14.wad",
        "7.0 MB",
        true,
        false
    },
    {
        "MEGA TRICKSTER'S MISTAKE UNREALITY WI...",
        "The Tricksters - Mega Trick...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Unreality has been struck with extreme stupidness an...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/mtmstk.zip",
        "mtmstk.wad",
        "3.7 MB",
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
        "MINI-LEVEL MEGAWAD",
        "Wim Vanrie",
        "MEGAWAD",
        "DOOM2.WAD",
        "A megawad which was 95% finished two years ago",
        "Never got around to adding the final touches until now",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/mlm12.zip",
        "mlm12.wad",
        "1.3 MB",
        true,
        false
    },
    {
        "MOCK 2: THE SPEED OF STUPID",
        "Lots",
        "MEGAWAD",
        "DOOM2.WAD",
        "It is the end of the clone wars of the 52nd century",
        "All of humans is happy but the evil monsters of Hell...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/mock2.zip",
        "mock2.wad",
        "2.9 MB",
        true,
        false
    },
    {
        "MONOCHROME MAPPING PROJECT",
        "Various (See map list below)",
        "MEGAWAD",
        "DOOM2.WAD",
        "A collection of maps experimenting with the concept ...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dwmmp1.zip",
        "dwmmp1.wad",
        "5.6 MB",
        true,
        false
    },
    {
        "MOONBLOOD",
        "Deadwing",
        "MEGAWAD",
        "DOOM2.WAD",
        "Moonblood is a megawad (32 levels) for Doom 2",
        "The Project has started with remastering the levels ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/moonbld.zip",
        "moonbld.wad",
        "3.9 MB",
        true,
        false
    },
    {
        "MOUNTAIN GORGE",
        "Michael Jan Krizik (valkiri...",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a large map that replaces MAP01",
        "An older level I decided to finish in a sort of slau...",
        "https://youfailit.net/pub/idgames/levels/doom2/m-o/mtngorge.zip",
        "MTNGORGE.WAD",
        "0.2 MB",
        true,
        false
    },
    {
        "MUTINY (A DOOMWORLD COMMUNITY PROJECT)",
        "Various (maplist below)",
        "MEGAWAD",
        "DOOM2.WAD",
        "Mutiny is a 16-level, Limit Removing Community Proje...",
        "It features new colorful, high contrast, (intentiona...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/mutiny.zip",
        "mutiny.wad",
        "5.3 MB",
        true,
        false
    },
    {
        "NANOPINEAPPLE 2018",
        "Sophie Kirschner",
        "MEGAWAD",
        "DOOM2.WAD",
        "Thirty-two maps, all first made during November and ...",
        "Wow, talk about an intense month",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/pnano18.zip",
        "pnano18.wad",
        "1.6 MB",
        true,
        false
    },
    {
        "NECROPIA ZONE (2010 FINAL VERSION) FO...",
        "Omy",
        "MEGAWAD",
        "DOOM2.WAD",
        "Necropia 2010 is new megawad with 32 new levels for ...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/necropia.zip",
        "necropia.wad",
        "4.3 MB",
        true,
        false
    },
    {
        "NEWDOOM COMMUNITY PROJECT",
        "various Doom Community members",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/ndcp.zip",
        "ndcp.wad",
        "7.0 MB",
        true,
        false
    },
    {
        "NEWDOOM COMMUNITY PROJECT II",
        "Various Authors",
        "MEGAWAD",
        "DOOM2.WAD",
        "This project started on January 10, 2006",
        "The first NDCP had been completed and released",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/ndcp2.zip",
        "ndcp2.wad",
        "19.0 MB",
        true,
        false
    },
    {
        "NIGHT.WAD",
        "Doom Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "Supplementary graphics for doom with dmgraph",
        "exe and a batch file to replace the pinkies with a b...",
        "https://youfailit.net/pub/idgames/graphics/knight.zip",
        "NIGHT.WAD",
        "0.1 MB",
        true,
        false
    },
    {
        "NIGHTMARES OF LOKI 1999",
        "Jerry Lehr, Jr",
        "MEGAWAD",
        "DOOM.WAD",
        "Nightmares of Loki is a cryptic hallucinagenic Night...",
        "This version contains 3 episodes and level starts to...",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/loki1999.zip",
        "loki1999.wad",
        "4.7 MB",
        true,
        false
    },
    {
        "NJ DOOM",
        "Nigel 'Enjay' Rowand",
        "MEGAWAD",
        "DOOM.WAD",
        "Olde replacement for the original Doom 3 Episodes",
        "Released purely because of interest in old, cheesy 1...",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/njdoom.zip",
        "njdoom.wad",
        "1.2 MB",
        true,
        false
    },
    {
        "NJ DOOM2",
        "Nigel 'Enjay' Rowand",
        "MEGAWAD",
        "DOOM2.WAD",
        "Olde replacement for Doom2",
        "Released purely because of interest in old, cheesy 1...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/njdoom2.zip",
        "njdoom2.wad",
        "1.5 MB",
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
        "NOVA II: NEW DAWN",
        "TeamNOVA 2",
        "MEGAWAD",
        "DOOM2.WAD",
        "/\/\/\FILE UPDATED (Ver 1",
        "2) 3/27/16/\/\/\ 32 maps for any limit removing port",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/nova2.zip",
        "nova2.wad",
        "13.8 MB",
        true,
        false
    },
    {
        "NOVA: THE BIRTH",
        "TeamNOVA",
        "MEGAWAD",
        "DOOM2.WAD",
        "/\/\/\FILE UPDATED (Ver 1",
        "1) 8/31/15/\/\/\ Here we worked on creating a 32 map...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/nova.zip",
        "nova.wad",
        "7.2 MB",
        true,
        false
    },
    {
        "NUMBER ONE KILL THE NEXT GENERATION",
        "Angelo Jefferson",
        "MEGAWAD",
        "DOOM2.WAD",
        "There was hexen and hexen2, startrek and startrek th...",
        "What do they all have in common",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/1killtng.zip",
        "1killtng.wad",
        "2.0 MB",
        true,
        false
    },
    {
        "OOPS: ALL TECHBASE",
        "Various (see credits)",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Doom Community Meisterwerk",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/oatb.zip",
        "OATB.WAD",
        "4.0 MB",
        true,
        false
    },
    {
        "OPERATION BIOWAR",
        "Chris Harbin, Paul Corfiati...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Biowar is a doom2 megawad in the spirit of ICARUS an...",
        "It consists of 18 regular levels and 1 secret level",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/biowar.zip",
        "biowar.wad",
        "1.8 MB",
        true,
        false
    },
    {
        "PAGODIA",
        "Aurelius, Bobby, Egg Boy, J...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Pagodia is a vanilla compatible episode for Doom 2 f...",
        "The wad is split into 3 distinct episodes: Village, ...",
        "https://youfailit.net/pub/idgames/levels/doom2/p-r/pagodia.zip",
        "pagodia.wad",
        "4.4 MB",
        true,
        false
    },
    {
        "PANDORA",
        "Adam Bilbrough (Gibbon)",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a megawad comprised on shovelware-esque leve...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/pandora_22.zip",
        "pandora_22.wad",
        "2.2 MB",
        true,
        false
    },
    {
        "PERDITION'S GATE",
        "Wraith Corporation",
        "MEGAWAD",
        "DOOM2.WAD",
        "Kommerzieller 32-Level Megawad-Klassiker.",
        "Schnelle Kaempfe und intelligentes Layout.",
        "https://archive.org/download/doom-wads/Perdition%27s%20Gate.zip",
        "PG-RAW-X.WAD",
        "4.2 MB",
        true,
        false
    },
    {
        "PINOCHESTEIN 3D EDICION GL (GZDOOM)",
        "Gabriel Valderrama (Kotomet...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Pinochestein 3D edicion GL (GL edition) is a total c...",
        "This version not only improves lighting decorations,...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/gzp8glv2.zip",
        "gzp8glv2.wad",
        "33.1 MB",
        true,
        false
    },
    {
        "PINOCHESTEIN 3D EDICION GL (ZANDRONUM)",
        "Gabriel Valderrama (Kotomet...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Pinochestein 3D edicion GL (GL edition) is a total c...",
        "This version not only improves lighting decorations,...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/znp8glv2.zip",
        "znp8glv2.wad",
        "19.8 MB",
        true,
        false
    },
    {
        "PIR WAD",
        "See list below",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/pir.zip",
        "pir.wad",
        "1.8 MB",
        true,
        false
    },
    {
        "PIZZA STEVE",
        "Fernando Sanchez M.",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 Doom2 Levels",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/pizzas.zip",
        "pizzas.wad",
        "2.9 MB",
        true,
        false
    },
    {
        "PLUTONIA 1024: THE PLUTINYA EXERIMENT",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 single player Plutonia-themed 1024 maps designed ...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/pl1024.zip",
        "pl1024.wad",
        "2.1 MB",
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
        "PLUTONIA MIDI PACK",
        "Jimmy & various",
        "MEGAWAD",
        "DOOM2.WAD",
        "One thing that separates the two halves of Final Doo...",
        "So, what if Dario and Milo Casali were musicians the...",
        "https://youfailit.net/pub/idgames/music/plutmidi.zip",
        "plutmidi.wad",
        "0.2 MB",
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
        "PROJECT X",
        "C¥ßerÐemºn",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 new levels for Doom][(see P-XMAN",
        "TXT for more)",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/proj-x.zip",
        "proj-x.wad",
        "1.0 MB",
        true,
        false
    },
    {
        "PUNISHER.WAD",
        "Daniel 'Demonlord' Lindgren",
        "MEGAWAD",
        "DOOM2.WAD",
        "Have changed the route since last recording",
        "Thisone is a bit faster",
        "https://youfailit.net/pub/idgames/lmps/doom2/1.9pwad/punisher.zip",
        "punisher.wad",
        "0.0 MB",
        true,
        false
    },
    {
        "RAY MOHAWK 2: RAY WREAKS HAVOC! 20 VA...",
        "Doomkid & Co.",
        "MEGAWAD",
        "DOOM2.WAD",
        "Ray is BACK and ready for more vanilla-compatible ac...",
        "This is a community project and sequel to my older w...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/mohawks2.zip",
        "mohawks2.wad",
        "6.7 MB",
        true,
        false
    },
    {
        "RAY MOHAWK'S MANIC MONDAY! (6 VANILLA...",
        "Doomkid",
        "MEGAWAD",
        "DOOM2.WAD",
        "Feeling a little sick of the winter chill",
        "Step out into the sunshine with Ray Mohawk on a ques...",
        "https://youfailit.net/pub/idgames/levels/doom2/m-o/mohawks.zip",
        "mohawks.wad",
        "0.9 MB",
        true,
        false
    },
    {
        "REALM OF CHAOS",
        "The MacIntosh Team (TMT)",
        "MEGAWAD",
        "DOOM2.WAD",
        "This 32-level Doom2 WAD is the first (and only) Mac-...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/rlmchaos.zip",
        "RLMCHAOS.WAD",
        "1.6 MB",
        true,
        false
    },
    {
        "REKKR",
        "Revae",
        "MEGAWAD",
        "DOOM2.WAD",
        "REKKR is a total conversion for Ultimate Doom v1",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/rekkr.zip",
        "REKKR.wad",
        "12.8 MB",
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
        "RESTORING DEIMOS",
        "Whymes",
        "MEGAWAD",
        "DOOM2.WAD",
        "A 32-map single player/cooperative wad",
        "Contains basic support for deathmatch, as well as di...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/rdeimosa.zip",
        "rdeimosa.wad",
        "20.9 MB",
        true,
        false
    },
    {
        "RESURGENCE (FINAL VERSION)",
        "Josh Sealy with Guest Mappe...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Resurgence is a 32 maps megawad project, an upgrade ...",
        "The maps are similar to Speed of Doom, ranging from ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/resurge.zip",
        "resurge.wad",
        "13.2 MB",
        true,
        false
    },
    {
        "REVENGE OF THE 90'S",
        "Faceplant92 / Doomkid",
        "MEGAWAD",
        "DOOM2.WAD",
        "20 levels, designed for Single Player / CO-OP",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/rot90fix.zip",
        "rot90fix.wad",
        "3.4 MB",
        true,
        false
    },
    {
        "REVERIE",
        "Michael Jan Krizik",
        "MEGAWAD",
        "DOOM2.WAD",
        "A brand new, 32-level megawad for Vanilla Doom 2",
        "Inspired from and made in the style of 90's megawads",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/reverie.zip",
        "REVERIE.WAD",
        "4.1 MB",
        true,
        false
    },
    {
        "REVOLUTION!",
        "Thomas van der Velden",
        "MEGAWAD",
        "DOOM2.WAD",
        "is an unofficial add-on for id Softwares DOOM2 It of...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/tvr.zip",
        "tvr.wad",
        "2.2 MB",
        true,
        false
    },
    {
        "RISE OF THE WOOL BALL",
        "MSP",
        "TOTAL CONVERSION",
        "DOOM2.WAD",
        "Bunte humorvolle TC im Stil klassischer Retro-Shooter.",
        "5 Episoden voller Spass, Musik & neuer Grafiken.",
        "https://archive.org/download/doom-wads/Rise%20Of%20The%20Wool%20Ball%20%28v1.3%29.zip",
        "ROTWB.WAD",
        "7.6 MB",
        true,
        true
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
        "ROWDY RUDY'S REVENGE! (A 10 MAP PARTI...",
        "Doomkid",
        "MEGAWAD",
        "DOOM2.WAD",
        "Rowdy Rudy's Revenge: a 10 map partial conversion",
        "100% vanilla, heavy on the dehacked",
        "https://youfailit.net/pub/idgames/levels/doom2/p-r/rwdyrudy.zip",
        "rwdyrudy.wad",
        "3.5 MB",
        true,
        false
    },
    {
        "SCIENTIST 2",
        "Scientist",
        "MEGAWAD",
        "DOOM2.WAD",
        "Made for 'vanilla' doom2",
        "exe and includes: -20 single player maps (map01 upto...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/sci2.zip",
        "sci2.wad",
        "4.1 MB",
        true,
        false
    },
    {
        "SCIMITAR",
        "Cycloid",
        "MEGAWAD",
        "DOOM2.WAD",
        "20 shortish levels split into three episodes: Rear E...",
        "No one quite knows what it is, apart from the birthi...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/scimitar.zip",
        "scimitar.wad",
        "1.9 MB",
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
        "SEWERS (XBOX E1M10)",
        "David Robinson",
        "SECRET LEVEL",
        "DOOM.WAD",
        "Das legendaere geheime Bonuslevel der Xbox-Version.",
        "Wird als E1M10 im ersten Doom-Kapitel gespielt.",
        "https://archive.org/download/doom-wads/Sewers%20%28Xbox%20Ultimate%20Doom%29%20%5BFixed%5D.zip",
        "SEWERS.WAD",
        "0.1 MB",
        true,
        false
    },
    {
        "SIGIL",
        "John Romero",
        "MEGAWAD",
        "DOOM2.WAD",
        "Baphomet was only doing Satan's bidding by placing a...",
        "Somehow they didn't understand that you're the reaso...",
        "https://youfailit.net/pub/idgames/levels/doom/Ports/s-u/sigil_v1_23.zip",
        "SIGIL.wad",
        "3.1 MB",
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
        "SINISTER SEVEN - VANILLA MAPS BY DOOMKID",
        "Doomkid [Adam P.]",
        "MEGAWAD",
        "DOOM2.WAD",
        "7 sp/coop maps, vanilla compatible (besides map07), ...",
        "Some of these maps are from submissions to communtiy...",
        "https://youfailit.net/pub/idgames/levels/doom2/s-u/sinseven.zip",
        "SinSeven.wad",
        "2.9 MB",
        true,
        false
    },
    {
        "SKULLDASH (VERSION 1.0)",
        "Joshua 'Dragonfly' O'Sullivan",
        "MEGAWAD",
        "DOOM2.WAD",
        "SKULLDASH OVERVIEW: Welcome to Skulldash, the fast-p...",
        "The objective in skulldash is to collect the Skullto...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/skuldash.zip",
        "skuldash.wad",
        "18.2 MB",
        true,
        false
    },
    {
        "SLAUGHTERFEST 2011",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "A compilation of 32 slaughter maps, each designed to...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/sf2011.zip",
        "sf2011.wad",
        "5.8 MB",
        true,
        false
    },
    {
        "SLAUGHTERFEST 2012",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "Another compilation of slaughter maps, 35 to be precise",
        "Designed to be played from pistol start",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/sf2012.zip",
        "sf2012.wad",
        "12.8 MB",
        true,
        false
    },
    {
        "SLITH",
        "Pistoolkip",
        "MEGAWAD",
        "DOOM2.WAD",
        "A five-map episode for Doom II with Thy Flesh Consum...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/s-u/slith.zip",
        "SLITH.WAD",
        "0.8 MB",
        true,
        false
    },
    {
        "SPEED MASTER",
        "Russian Doom Community (see...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Megawad, based on the results of three speedmapping ...",
        "ru in 2014-2016, with next themes: 1",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/spmaster.zip",
        "spmaster.wad",
        "23.8 MB",
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
        "SQUARES",
        "Costa Lappas",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is extremly hard if not impossible to complete ...",
        "the only bug I know of is that you can't save it on ...",
        "https://youfailit.net/pub/idgames/levels/doom2/0-9/1squares.zip",
        "1squares.wad",
        "0.0 MB",
        true,
        false
    },
    {
        "STONE STRATAGEMS V1",
        "Alias Zeratul 982",
        "MEGAWAD",
        "DOOM2.WAD",
        "20 levels: Some short, some deadly, most with stone-...",
        "(Look under 'Level Information' for more info) Be su...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/stone.zip",
        "stone.wad",
        "1.0 MB",
        true,
        false
    },
    {
        "STRAIN 1.0",
        "The Alpha Dog Alliance",
        "MEGAWAD",
        "DOOM2.WAD",
        "STRAIN is THE largest DOOM II Total Conversion",
        "And it's FREE",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/strain10.zip",
        "strain10.wad",
        "4.8 MB",
        true,
        false
    },
    {
        "STRAIN 1.0 (REPACKAGED EDITION)",
        "Alpha Dog Alliance",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a reupload of the legendary 1997 total conve...",
        "The original version uploaded to /idgames on Septemb...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/strain.zip",
        "STRAIN.WAD",
        "9.4 MB",
        true,
        false
    },
    {
        "SUITCASE OF GOR (FORMERLY D2REDUX)",
        "The Tarnsman of Christmas Past",
        "MEGAWAD",
        "DOOM2.WAD",
        "The first maps I ever made, they suck, look terrible...",
        "A reimaging/remake/restyling of DOOM II",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/suitcgor.zip",
        "suitcgor.wad",
        "9.9 MB",
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
        "SUPER SONIC DOOM",
        "Stephen Clark (The Ultimate...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A DooM 2 episode with a Sonic influence throughout, ...",
        "It has the Sonic layout (zones, acts, bosses) and al...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/sonic.zip",
        "sonic.wad",
        "17.9 MB",
        true,
        false
    },
    {
        "SURVIVE IN HELL PUBLIC RELEASE 2",
        "JC",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/sihr2fix.zip",
        "sihr2fix.wad",
        "3.9 MB",
        true,
        false
    },
    {
        "SWIFT DEATH",
        "franckFRAG (guest mappers J...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Swift Death is an extremely hard megawad, calibrated...",
        "I recommanded for novice players to play in 'easy' (...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/swideath.zip",
        "swideath.wad",
        "1.5 MB",
        true,
        false
    },
    {
        "SWITCHEROOM",
        "Various",
        "MEGAWAD",
        "DOOM.WAD",
        "The idea was to remake a map from one episode, but a...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/swtchrm.zip",
        "swtchrm.wad",
        "1.9 MB",
        true,
        false
    },
    {
        "TARAKANNIK",
        "Sasha",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tarakans.zip",
        "tarakans.wad",
        "2.9 MB",
        true,
        false
    },
    {
        "TARAKANNIK 2",
        "Sasha",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tarakan2.zip",
        "tarakan2.wad",
        "10.3 MB",
        true,
        false
    },
    {
        "TEN COMMUNITY PROJECT",
        "Various members of the ZDoo...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Fuzzball Fox came up with the concept of using only ...",
        "The resulting mapset is split into two episodes, Sin...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tcp.zip",
        "tcp.wad",
        "15.1 MB",
        true,
        false
    },
    {
        "THE 64 KB CHALLENGE: VANILLA SELECTION",
        "Various, see authors-v.txt",
        "MEGAWAD",
        "DOOM2.WAD",
        "34 maps made under or equal the file size limitation...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/32of64-v.zip",
        "32OF64-V.WAD",
        "1.1 MB",
        true,
        false
    },
    {
        "THE ABYSS",
        "H2SO4 & MrElusive",
        "MEGAWAD",
        "DOOM2.WAD",
        "Who cares about some corny story line, who reads it ...",
        "Just play the f#%$&&*",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/abyss24a.zip",
        "abyss24a.wad",
        "1.7 MB",
        true,
        false
    },
    {
        "THE ARCHIVES ARE DOOMED: GOLD EDITION!",
        "Wills",
        "MEGAWAD",
        "DOOM2.WAD",
        "Legendaeres Levelset der Doom-Community.",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tardgold.zip",
        "tardgold.wad",
        "1.2 MB",
        true,
        false
    },
    {
        "THE DARKENING",
        "Jan Van der Veken",
        "MEGAWAD",
        "DOOM2.WAD",
        "It has been 3 years since the destruction of the hug...",
        "Your journey back home was almost as difficult and b...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/darken.zip",
        "darken.wad",
        "1.6 MB",
        true,
        false
    },
    {
        "THE DARKENING EPISODE 2",
        "Ola Björling",
        "MEGAWAD",
        "DOOM2.WAD",
        "After a hard nights driving about in the galaxy, you...",
        " Uhm",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/darken2.zip",
        "Darken2.wad",
        "4.3 MB",
        true,
        false
    },
    {
        "THE DEVILZ WORK",
        "DoomKid",
        "MEGAWAD",
        "DOOM2.WAD",
        "Hello fellow Doomers",
        "Here are 7 SP/coop maps designed for DOS Doom2",
        "https://youfailit.net/pub/idgames/levels/doom2/s-u/tdevil2.zip",
        "TDEVIL2.WAD",
        "1.2 MB",
        true,
        false
    },
    {
        "THE DOOM TRIBUTE PROJECT",
        "Alando1 (Alan)",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is a re-interpretation of the original DOOM & D...",
        "It pretty much is a retelling of the DOOM story in a...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/doomtrib.zip",
        "doomtrib.wad",
        "47.0 MB",
        true,
        false
    },
    {
        "THE GREAT BICYCLE MYSTERY 2",
        "TheCupboard & Worst-vd-Plas",
        "MEGAWAD",
        "DOOM2.WAD",
        "The Great Bicycle Mystery 2 is a 32 level journey th...",
        "Along the way, you'll encounter 9 new monsters, new ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/bm2surv.zip",
        "bm2surv.wad",
        "7.2 MB",
        true,
        false
    },
    {
        "THE JOURNEY VER. 1.1",
        "hervoheebo",
        "MEGAWAD",
        "DOOM2.WAD",
        "A new 32-map megawad for DOOM II, focusing on short ...",
        " THE JOURNEY takes you across a variety of locales, ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tj.zip",
        "tj.wad",
        "3.2 MB",
        true,
        false
    },
    {
        "THE LAST JOKE!",
        "Walter 'Daimon' Confalonieri",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is my last jokewad, as the title suggest",
        "after this i will made only fuckin' cool things",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/dai_lj.zip",
        "dai_lj.wad",
        "2.3 MB",
        true,
        false
    },
    {
        "THE LAST REVENGER",
        "«¥£ ãçª®¢ (Oleg Puchkov)",
        "MEGAWAD",
        "DOOM2.WAD",
        "Single player DOOM II levels",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/revenger.zip",
        "revenger.wad",
        "4.9 MB",
        true,
        false
    },
    {
        "THE LONG TREK BACK HOME V. 1.1",
        "hervoheebo",
        "MEGAWAD",
        "DOOM2.WAD",
        "Presented as an immediate sequel to Doom ][, The Lon...",
        "Though the respective areas are familiar, great uphe...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/longtrek.zip",
        "LONGTREK.WAD",
        "3.5 MB",
        true,
        false
    },
    {
        "THE LOST EPISODES OF DOOM",
        "Jonathan Mendoza & Chris Klie",
        "MEGAWAD",
        "DOOM.WAD",
        "Die beruehmte 24-Level Buch-Kampagne von 1995.",
        "Einer der allerersten professionellen Megawads.",
        "https://archive.org/download/doom-wads/Lost%20Episodes%20of%20Doom%2C%20The.zip",
        "JPTR_FIX.WAD",
        "0.8 MB",
        true,
        false
    },
    {
        "THE MODEST MAPPING CHALLENGE",
        "Jark, Pegleg, LowGcifer, Ka...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A community project, each Mapper had 40 sectors (In ...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tmmc.zip",
        "TMMC.WAD",
        "3.2 MB",
        true,
        false
    },
    {
        "THE NEW ADVENTURE",
        "Paul Corfiatis",
        "MEGAWAD",
        "DOOM2.WAD",
        "Its the 43rd century, year 662, month 3 day 25",
        "The universe is a wreck and you must sort out all th...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/0cb.zip",
        "0cb.wad",
        "1.2 MB",
        true,
        false
    },
    {
        "THE NEW DOOM V1.0",
        "Ernie Stockham, Brian Davis...",
        "MEGAWAD",
        "DOOM2.WAD",
        "after about 34 months of hard work and late nights u...",
        "are finally finished",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/1337.zip",
        "1337.wad",
        "1.2 MB",
        true,
        false
    },
    {
        "THE REBIRTH",
        "Bobjet (Björn Ostmann)",
        "MEGAWAD",
        "DOOM2.WAD",
        "Total Conversation for Doom2, replaces all of the 32...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/rebirth.zip",
        "rebirth.wad",
        "4.7 MB",
        true,
        false
    },
    {
        "THE TECHWARS",
        "Moti",
        "MEGAWAD",
        "DOOM2.WAD",
        "A Tech-styled (mostly) megawad for ZDoom",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/techwars.zip",
        "techwars.wad",
        "3.2 MB",
        true,
        false
    },
    {
        "THE TWILIGHT ZONE (VERSION 5 RELEASED...",
        "Paul Corfiatis",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 all new classic levels for Doom 2",
        "The twilight zone dosen't really have a story to it",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/twzone.zip",
        "TWZONE.WAD",
        "1.3 MB",
        true,
        false
    },
    {
        "THE TWILIGHT ZONE II: FINAL DREAMS",
        "Paul Corfiatis",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Doom Community Meisterwerk",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/twzone2.zip",
        "TWZONE2.WAD",
        "3.8 MB",
        true,
        false
    },
    {
        "THE VILECORE",
        "Darren Finch A.K.A: Doom_Dude",
        "MEGAWAD",
        "DOOM2.WAD",
        "More DOOM II:",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/vilcore.zip",
        "vilcore.wad",
        "1.8 MB",
        true,
        false
    },
    {
        "THE WORLD OF DETH -FROM HEAVEN TO HEL...",
        "Morten Stromsvold (morten_s...",
        "MEGAWAD",
        "DOOM2.WAD",
        "At last I am done with my first and last MegaWad",
        "After years of building",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/world.zip",
        "world.wad",
        "2.2 MB",
        true,
        false
    },
    {
        "THREE'S A CROWD (V1.2.2)",
        "Scypek2",
        "MEGAWAD",
        "DOOM2.WAD",
        "A vanilla-compatible mapset created for NaNoWadMo 2019",
        "It tells the story of three people and the different...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/3iac.zip",
        "3IAC.wad",
        "2.8 MB",
        true,
        false
    },
    {
        "THT: THRENODY",
        "Various (see credits below)",
        "MEGAWAD",
        "DOOM2.WAD",
        "Ty Halderman, the essential pillar of the Doom commu...",
        "He is greatly missed and his contributions to the Do...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/thtthren.zip",
        "thtthren.wad",
        "13.0 MB",
        true,
        false
    },
    {
        "TIME OF MAPS - PART 1",
        "TimeOfDeath (Chris Balch)",
        "MEGAWAD",
        "DOOM2.WAD",
        "TOM is a boom compatible wad that uses only doom2 fo...",
        "-complevel 9 in PrBoom-Plus is required for some ris...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/tom19.zip",
        "tom19.wad",
        "5.6 MB",
        true,
        false
    },
    {
        "TNT MIDI PACK",
        "Cammy et al.",
        "MEGAWAD",
        "DOOM2.WAD",
        "Eight years since the Plutonia MIDI pack and twenty-...",
        "I always wondered where the love for the elder siste...",
        "https://youfailit.net/pub/idgames/music/tntmidi.zip",
        "TNTmidi.wad",
        "0.6 MB",
        true,
        false
    },
    {
        "TNT: REVILUTION",
        "Various, see below.",
        "MEGAWAD",
        "DOOM2.WAD",
        "TNT: Revilution is a fan made community project that...",
        "The goal of this project was to capture the spirit o...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/tntr.zip",
        "TNTR.WAD",
        "8.3 MB",
        true,
        false
    },
    {
        "TOONS FOR DOOM 2",
        "David (Pepe) Hopkins 2",
        "MEGAWAD",
        "DOOM2.WAD",
        "Sequel to TOONS FOR DOOM",
        "More latter",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/toon2b.zip",
        "toon2b.wad",
        "2.6 MB",
        true,
        false
    },
    {
        "ULTIMATE DOOM 2",
        "Doom Community",
        "MEGAWAD",
        "DOOM2.WAD",
        "You're one tough marine",
        "You know it, and everyone that knows you knows it",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/ultimate.zip",
        "ultimate.wad",
        "2.5 MB",
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
        "ULTIMATE SIMPLICITY",
        "Agent Spork",
        "MEGAWAD",
        "DOOM2.WAD",
        "Ultimate Simplicity is a megawad comprised of 3 epis...",
        "It is essentially an overhauled version of the origi...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/sp_usimp.zip",
        "sp_usimp.wad",
        "15.2 MB",
        true,
        false
    },
    {
        "UNHOLY REALMS",
        "Brian Knox",
        "MEGAWAD",
        "DOOM2.WAD",
        "32 new limit-removing maps created by one author (an...",
        "Gameplay-oriented",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/ur.zip",
        "ur.wad",
        "8.9 MB",
        true,
        false
    },
    {
        "UPRISING",
        "Cheesewheel",
        "MEGAWAD",
        "DOOM2.WAD",
        "Uprising is a narrative driven DOOM2 megawad for lim...",
        "It features an extensive dehacked patch with changes...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/uprising.zip",
        "UPRISING.WAD",
        "9.0 MB",
        true,
        false
    },
    {
        "URANIA",
        "riderr3",
        "MEGAWAD",
        "DOOM2.WAD",
        "You need Plutonia IWAD for this",
        "Plot: Urania - a planet that suddenly appeared in th...",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/urania.zip",
        "URANIA.WAD",
        "3.7 MB",
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
        "VALIANT: VACCINATED EDITION",
        "Paul 'skillsaw' DeBruyne",
        "MEGAWAD",
        "DOOM2.WAD",
        "This edition of Valiant strips out all of the MBF De...",
        " If you really disliked the additional monsters and ...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/valve.zip",
        "valve.wad",
        "26.9 MB",
        true,
        false
    },
    {
        "VIGOR",
        "Bobby (lolmcswagger), DCG R...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Vigor is a vanilla compatible megawad with a focus o...",
        "Inspired by E",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/vigor.zip",
        "VIGOR.WAD",
        "1.9 MB",
        true,
        false
    },
    {
        "VILE FLESH",
        "Gwyn Williams",
        "MEGAWAD",
        "DOOM2.WAD",
        "Klassisches Doom Community Meisterwerk",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/vile.zip",
        "VILE.WAD",
        "5.3 MB",
        true,
        false
    },
    {
        "VISPIRE",
        "Michael Jan Krizik (valkiri...",
        "MEGAWAD",
        "DOOM2.WAD",
        "A collection of fast, fun old-school maps with varyi...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/megawads/vispire.zip",
        "VISPIRE.WAD",
        "1.1 MB",
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
    },
    {
        "WHITEMARE 2",
        "Russian Doom Community (see...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Megawad, based on the results of a four speedmapping...",
        "ru in 2013, with such themes, as winter, thaw, preci...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/white2.zip",
        "white2.wad",
        "41.0 MB",
        true,
        false
    },
    {
        "WONDERFUL DOOM, V.1.3",
        "Wraith",
        "MEGAWAD",
        "DOOM2.WAD",
        "I just wanted all doomers to play Orginal Doom one m...",
        "All maps are similar in design and difficulty to the...",
        "https://youfailit.net/pub/idgames/levels/doom/megawads/wd13.zip",
        "WD13.WAD",
        "1.6 MB",
        true,
        false
    },
    {
        "WOOO",
        "mrthejoshmon, cannonball, S...",
        "MEGAWAD",
        "DOOM2.WAD",
        "Before I say a thing, this is a jokewad for ZDoom (Y...",
        ") so if you do not want to play some of the worst ma...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/wooo.zip",
        "wooo.wad",
        "6.6 MB",
        true,
        false
    },
    {
        "WOOO 2",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "This is the highly anticipated sequel to WOOO",
        "(The ZDoom orientated jokewad that nobody asked for)...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/wooo2.zip",
        "wooo2.wad",
        "12.8 MB",
        true,
        false
    },
    {
        "WOOO 3: TOO GOOD FOR THE MAPPERS WHO ...",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "'Here it FINALLY is",
        "The EXCITING (citation needed) CONCLUSION (please go...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/wooo3.zip",
        "wooo3.wad",
        "48.4 MB",
        true,
        false
    },
    {
        "ZDOOM COMMUNITY MAP PROJECT II",
        "Various authors",
        "MEGAWAD",
        "DOOM2.WAD",
        "As the successor to the ZDCMP1, the second take foll...",
        "Expect a high quality Doom experience with countless...",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/zdcmp2.zip",
        "zdcmp2.wad",
        "51.4 MB",
        true,
        false
    },
    {
        "ZONES OF FEAR",
        "Damned, Des_arthes, enkeli/...",
        "MEGAWAD",
        "DOOM2.WAD",
        "After the first czech community project we were deci...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/zof.zip",
        "zof.wad",
        "11.9 MB",
        true,
        false
    },
    {
        "ZPACK - RANDOM MAPS FOR ZDOOM",
        "Various",
        "MEGAWAD",
        "DOOM2.WAD",
        "A megawad of 3 themed episodes, each of them with nu...",
        "Fantastisches Leveldesign und Gameplay.",
        "https://youfailit.net/pub/idgames/levels/doom2/Ports/megawads/zpack.zip",
        "zpack.wad",
        "35.3 MB",
        true,
        false
    }
};
#define CATALOG_COUNT ((int)(sizeof(s_catalog) / sizeof(s_catalog[0])))

// In-memory cache for installed status (avoids hammering the slow SD card every frame)
static bool s_installed[CATALOG_COUNT];

static void Downloader_RefreshInstalledCache(void)
{
    memset(s_installed, 0, sizeof(s_installed));

    // Read directory entries into memory ONCE (approx 1-2 milliseconds on FAT32)
    // instead of calling access() 900+ times over slow SD card IPC!
    #define MAX_DIR_FILES 1024
    static char local_files[MAX_DIR_FILES][64];
    int local_count = 0;

    DIR *d = opendir("sdmc:/3ds/Trinity3DS/wads");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && local_count < MAX_DIR_FILES) {
            if (ent->d_name[0] == '.') continue;
            strncpy(local_files[local_count], ent->d_name, sizeof(local_files[local_count]) - 1);
            local_files[local_count][sizeof(local_files[local_count]) - 1] = '\0';
            local_count++;
        }
        closedir(d);
    }

    if (local_count == 0) return;

    // Fast in-memory match (0.001 ms)
    for (int i = 0; i < CATALOG_COUNT; i++) {
        const char *target = s_catalog[i].target_file;
        for (int j = 0; j < local_count; j++) {
            if (!strcasecmp(target, local_files[j])) {
                s_installed[i] = true;
                break;
            }
        }
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
    mkdir("sdmc:/3ds/Trinity3DS", 0777);
    mkdir("sdmc:/3ds/Trinity3DS/wads", 0777);
    mkdir("sdmc:/3ds/Trinity3DS/downloads", 0777);

    char temp_file[256];
    if (item->is_zip) {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Trinity3DS/downloads/temp_dl.zip");
    } else {
        snprintf(temp_file, sizeof(temp_file), "sdmc:/3ds/Trinity3DS/wads/%s", item->target_file);
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Trinity3DS/0.9.8 (Nintendo 3DS)");
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

        bool ext_ok = Downloader_ExtractZip(temp_file, "sdmc:/3ds/Trinity3DS/wads", err_msg, err_len);
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
    WP_DrawText(fb, 400, 16, 8, "TRINITY3DS WAD STORE", 255, 255, 255, 2);

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
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 206 && touch.px <= 314) {
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

#include "wad_downloader.h"
#include "wad_picker.h"
#include <png.h>
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
    char desc[512];
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
        "A compilation of 27 levels (the three series from the original Doom), that I made between 1994 and 2000. But they weren't conceived as a complete and coherent episode. There isn't storyline or something similar, and most of these maps were constructed without any previous planning. In any case, they're grouped in three series, in the same way that the original episodes from Doom. The usual; my first episode has a futurist ambientation, the second introduces some bloody...",
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
        "In the spirit of 1024 Congestion and 10Sector, 25 new maps from the Doomworld community, each limited to only one monster type.",
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
        "From mid-August to mid-September, Doomworld.com held a contest with a simple premise: design the best Boom level you can with only 10 sectors. This is the result.",
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
        "From mid-August to mid-September, Doomworld.com held a contest with a simple premise: design the best Boom level you can with only 10 sectors. This is the second level compilation from that contest.",
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
        "100 Line Massacre is a small Megawad project (Full 32 maps + 3 Bonus) by NinjaDelphox and Arsinikk with the goal of making Vanilla- Compatible maps with the limit of 100 lines. All maps were designed with pistol start in mind, though continuous play is perfectly fine. Each map should take only around 2-5 minutes to finish. In addition to the 100 line limit, the other goal was to add variety to the megawad by alternating authorship with each map, combining Ninja's heavy...",
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
        "A 34 map megawad made for vanilla Doom 2, made by the community with a strict line limit of 1000 lines.",
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
        "A 34 map megawad made for vanilla Doom 2, made by the community with a strict line limit of 1000 lines. Using the power of Votex, a vanilla version of the OTEX texture pack by Ola Bj rling (ukiro). Carrying the story on from the last time, the player must fight through 3 new, exciting and totally infested worlds.",
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
        "A collection of 32 new 'speedmaps'. See 180mpv_info.txt for details. Also included is 180mpv_b.wad, a bonus wad containing an extra map 30, as well as story elements for the whole wad. See 180mpv_b.txt for more info.",
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
        "Everybody knows that most maps from 1994 are pretty terrible, but what if we took maps from the archive that permit modification and then redetailed them? The purpose of this community wad is to improve on maps from 1994. Most maps from that time had strong layouts, but poor design decisions such as misaligned textures, bad texture choices, hidden doors and mandatory secrets to progress. The aim of this project is to fix those problems and take what was already strong about...",
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
        "A megawad of vanilla maps, each made in 200 minutes. https://archive.foolz.us/vr/thread/1614552/#1620243",
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
        "A 4 episode megawad for the Ultimate Doom containing 4 all new challenging episodes for you to test you're skill on, great architecture and lots of hard work, put into every level. A recomended download.",
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
        "The 10th anniversary edition of 2002 A Doom Odyssey. Featuring many improvements and enhancements over the original release including some new maps with improved gameplay and musis. Contains 4 new episodes of 9 maps each.",
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
        "Welcome to 2048 UNLEASHED - An all new 32 level single player megawad for DOOM 2 put together by various authors in the community. Known as the Pcorf Community Project 2 at the Doomworld forums. The aim of this project was simple and it was to create a 2048x2048 Boom compatible level for DOOM 2 using the Whispers of Satan texture wad. 10 months later I received 49 levels divided into two episodes, the first 2048 UNLEASHED which contains the 32 main levels and the second ALL...",
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
        "Klassisches Doom Community Meisterwerk Fantastisches Leveldesign und Gameplay.",
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
        "3 Heures d'agonie 2 is a collection of 32 vanilla speedmaps designed by members of the French Doom Community. The general rule was that each map layout was to be made within a time limit of 3 hours.",
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
        "Klassisches Doom Community Meisterwerk Fantastisches Leveldesign und Gameplay.",
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
        "After destroying the big boss in doom2, you settled down for some r&r, when, suddenly, the cold dead faces of the undead greeted you as you transported in. Realizing the drill, you grabbed your sidearm and wished for some better armament. As you fight your way from station to station you realize that overthrowing the big boss has let different factions in hell struggle for power. You have a bad feeling about who is winning that struggle to rule in hell. Your apprehensions...",
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
        "A megawad compilation of maps made by /vr/ users. Participants were asked to make a map in 300 minutes, with an initial two-week deadline period for submission. There was no particular theme beyond vanilla compatibility / no custom textures. At deadline 25 maps had been submitted, so it was decided to allow extra time to complete the set; also the previously submitted maps were bugfixed and polished, and finally the wad released to /idgames. Post-release update: the initial...",
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
        "This is the second episode of the ' 20 Days in Hell ' trilogy. Send me feedback, please. Requires TNT Evilution.",
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
        "Not your average mapset. 32 over 32 contains 32 standalone levels separated into 32 individual wad files. The levels were made with pistol start in mind and each map will vary in terms of Theme, Difficulty and Gameplay style.",
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
        "'...It was the pinnacle of burgerdom. Finally, the long summer's study in Aberdonian cuisine and bovine mating rituals had yielded of a supple, juicy tower clad with buns too good to slap and the fattest sow the YEDS had ever sewn. The chief engineer-turned-overlord of foodstuffs, Percival Mechacook, deemed it necessary to awaken Shaitoken from his long winter's slumber to celebrate this yuletide achievement. Together, they would drive the Yeoman Engineers of Software and...",
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
        "50 Monsters is a project I started back in 2014 when I was just really getting into mapping. It got a bit out of hand in the second half of development, and as such the final product is fairly inconsistent in terms of overall quality. Still, though, it was a good learning experience and I enjoyed creating it.",
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
        "50 Shades of Graytall is a project designed around creating maps with interesting architecture and gameplay despite being limited to a selection of arguably hideous textures. Started in response to a silly post made in the Editing forum which led I and several others to attempt maps using the (slightly edited) rules in the post, hoping the result would be decent. Eventually opened up to the community and here we are! Hope you enjoy the maps and if you don't hey Doom projects...",
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
        "'A Fistful of Doom' is a complete Western conversion Every effort has been made to make this the ultimate...",
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
        "Third WAD by [B0S] clan. Now it's 32 levels. Plot: 'It all happened because of that strange portal in the cargo compartment of this falling plane. After we loaded the object we had found under kilometers of ice in the cargo hold, the commander appointed me to escort the valuable cargo. Who could've known that the mysterious find was going to suddenly come to life? A bright flash is the last thing I remember after liftoff.'",
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
        "This is just a collection of maps I've abandoned in my attempt to make a Megawad. =.=''' So I make those into a Megawad. Playable, but expect absolute random themes, horrible textures, random monsters, random stuffs all over the random maps. But do expect some interesting maps that might be better than a few other maps around. Winks! Oh! And expect maps with repeated sections as I might have copied a section or two from maps to maps, but no 2 maps are identical. Expect a few...",
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
        "Absolute Dishonor is a brand-new 8-level WAD created The levels are inspired from old-school Doom WADs fr...",
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
        "This pwad was written by Avery Lodato (alodato@ucdavis edu) using deu5",
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
        "Get ready to blaze through 32 action-packed levels fit to play in any port of choice - echoes of past megawad affairs such as Memento Mori and Alien Vendetta helped to shape the inspiration for this project. Akeldama continues in the spirit of collecting a group of talented mappers and creating a brand new experience for regular doom2.exe.",
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
        "Alien Vendetta offers 32 all new levels, ranging from small fast-paced tech-styled ones, to complex medieval- and hellish-themed maps. Variety was to be expected considering several authors had a hand in the making of these maps and as no specific criteria were given to them. AV originated in '96 as a one-man project, but saw many transitions and was eventually made a team effort in August 1999. What started out as a pure 'Hell Revealed' follower balanced out to be something...",
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
        "One large City themed map designed for Vanilla/Choco Fantastisches Leveldesign und Gameplay.",
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
        "Demos recorded with the original release of Ancient Aliens are compatible with this update. Version 1.1 includes updates to textures. Version 1.2 includes an update to fix a MAP24 crash in Eternity caused by mistakenly rebuilding the nodes in MAP24. Ancient Aliens is a megawad featuring 32 colorful, action-packed maps for Boom compatible ports. Go on a journey to discover the truth about the enigmatic origins of human civilization... I'm not saying it was aliens, but... the...",
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
        "Demos recorded with the original release of Ancient Aliens are compatible with this update. Version 1.1 includes updates to textures. Version 1.2 includes an update to fix a MAP24 crash in Eternity caused by mistakenly rebuilding the nodes in MAP24. Ancient Aliens is a megawad featuring 32 colorful, action-packed maps for Boom compatible ports. Go on a journey to discover the truth about the enigmatic origins of human civilization... I'm not saying it was aliens, but... the...",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "Area 51, The militarys not so top secret base in Nev We are sending you in alone to investigate armed wi...",
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
        "Large, complex levels, with a focus on exploration Fantastisches Leveldesign und Gameplay.",
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
        "8 levels of pure fun and carnage Fantastisches Leveldesign und Gameplay.",
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
        "10-level WolfenDOOM adventure, featuring new Fantastisches Leveldesign und Gameplay.",
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
        "I just thought I tell you that this is my very first It is, I'll admit, a very simple, music only addition",
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
        "Get Out Of My Stations is the first episode of Back to Saturn X, a three-episode partial conversion for Doom II. Back to Saturn X is fully compatible with Doom2.exe and Chocolate Doom. It has also been tested with Eternity Engine, PrBoom-Plus, ZDoom, GZDoom, and Risen3D.",
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
        "Tower in the Fountain of Sparks is the second episode of Back to Saturn X, a three-episode partial conversion for Doom II. Back to Saturn X is fully compatible with Doom2.exe and Chocolate Doom. It has also been tested with Eternity Engine, PrBoom-Plus, ZDoom, GZDoom, and Risen3D.",
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
        "Get Out Of My Stations is the first episode of Back to Saturn X, a three-episode partial conversion for Doom II. Back to Saturn X is fully compatible with Doom2.exe and Chocolate Doom. It has also been tested with Eternity Engine, PrBoom-Plus, ZDoom, GZDoom, and Risen3D.",
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
        "The UAC has been conducting experiments on demons, a They've lost contact with the base where they were ...",
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
        "A 3 episode vanilla megawad for Doom 1 containing a total of 27 maps. Set on one of Jupiter's moons called Ganymede, this wad should provide a challenge for a wide variety of skill levels ranging from the humble newbie to veteran speedrunner. Each episode has it's own progression rather than the usual base -> hellbase -> hell.",
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
        "A Deathmatch level, but fully implemented for the we It has 2 ways to exit, one of them being cooperative",
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
        "35 Levels for Zdoom, Plus 5 secret levels. Each level has all new challenges awaiting you. Plenty of use of the features in Zdoom like stealth enemies. There are also some new enemies capable of peforming the most unexpected tricks. There is no scripting this wad because I havent figured it out. The new enemies are a Spectre in one of the secret levels and a deadly floating Romero head. The five Secret levels like the two secret levels in the original Bella are all stunning....",
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
        "Das geheime 33. Bonuslevel der Xbox Doom II Version. Spannendes raetsellastiges Leveldesign.",
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
        "Megawad created by four Czech Doomers See the original textfile (!AA_BTH.TXT) for further descriptions and information.",
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
        "Freies Dark-Fantasy Spiel im Geiste von Heretic. Duestere Kathedralen und eigene Monster.",
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
        "This is a simple Dehacked 1 3 patch which leaves blood marks on the ground where...",
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
        "Full 32-map megawad. See BStainfo.txt for further information.",
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
        "*** IMPORTANT: Saving in vanilla (or Chocolate) Doom If you want to save, you'll need a limit-removing port",
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
        "The events of CABIN occur directly after DOOM II, se ---- After you defeated the Icon of Sin, a portal ba...",
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
        "Cabro's Legacy is a compilatory megawad, created in the year 2014 as an homage to the Cabro's mapping tournaments. It contains maps from the top three winners from each year. These tournaments started in the now extinct Cabro's website in the Y2K, and were later continued in the Arcades3D website until the year 2015. They were organized by Cabro in their first entries; and later continued by the Arcades3D users Mojon Man and Wraith, respectively. Rules has changed over the...",
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
        "Single-player megawad that pits the player against the forces of Castle Doom. I tried to keep it generic so that the various advanced ports can all run it the same way.",
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
        "Levels by Chaingunner.",
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
        "Legendaere Total Conversion mit dem Zorcher. Vollstaendig familienfreundlich mit eigenen Texturen.",
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
        "Offizieller zweiter Teil auf Chex City. 5 neue Level mit neuen Flem-Gegnern.",
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
        "Teil 3 der beliebten Chex Quest Reihe. Entwickelt vom originalen Chex-Kuenstler.",
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
        "30 levels for Doom II v1.9 - 20 levels support all play modes, and there's 10 Deathmatch-only levels as well. ChrisWAD also includes new sounds, music, graphics, a new monster (The Blob), and several changes to the existing monsters.",
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
        "This is a 'plain vanilla DooM ][' PWAD of maps 1-11 I tried to put the player in the city as best as po...",
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
        "Claustrophobia 1024 is the community-made, inspirational sequel to Congestion 1024 which was released back in 2005. It's comprised of 32 brand-new maps restricting the player's movements to a 1024x1024 space. Decoration and monsters were allowed outside of this 1024 squared area. The player is allowed outside of the 1024 boundary given he is killed instantly. The focus of the project was to create a set of fun, quick & good looking maps while being able to work within the...",
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
        "Claustrophobia 1024 2 is the community-made sequel to Claustrophobia 1024 which was released in June 2009. It's comprised of 34 brand-new maps restricting the player's movements to a 1024x1024 space. Decoration and monsters were allowed outside of this 1024 squared area. The player is allowed outside of the 1024 boundary given he is killed instantly. The focus of the project was to create a set of fun, quick & good looking maps while being able to work within the boundaries...",
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
        "This is the rerelease of our old old old TC Hyena, wich was made between 1994 and 1998. It's not testted this time around and it probaly got hundreds of bugs. But it was rereleased by popular demand. (from people who hadn't played it yet. :p) I don't claim it's good. But I as the head developer of it of course love it as it was my baby for 4 years (before I got online and saw what other people did with the engine). Please don't mail me and tell how much you think it suck (if...",
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
        "After 4 months of on and off work, Cold Front is fin Really, I designed around 7 or 8 maps, but a number ...",
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
        "This is a compilation of levels from the Doom Community",
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
        "Following the success of the original Community Chest, Community Chest 2 is a 32 map megawad including levels made by 26 different authors from the Doom Community. Originally announced almost one year ago in December 2003, it is now complete and ready for your enjoyment.",
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
        "The third installment of the series, Community Chest 3 boasts 32 maps made by 20 different authors from the Doom community. After one year of work, they have been made available for your enjoyment.",
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
        "The fourth installment of the series, Community Chest 4 boasts 32 maps made by 20 different authors from the Doom community. After four years of work, they have been made available for your enjoyment. Included in this package is the base resource WAD compiled for the project, which can be found in cc4-tex.zip. It may be a good starting point if you need a large, varied, and meticulously organized texture WAD for your project!",
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
        "Community is Falling 3 is the third sequal to the 'Community Is Falling' series, which are jokewads based on the drama and comedy inside the Doom community. Lots of inside jokes are involved so those who are outside the community will probably not understand most of whats going on. Unlike the previous two jokewads, this one is a full fledged 36 - level megawad/TC that abuses as much Zdoom features as possible. The design of this jokewad is not like that of Mock2, or DoTims,...",
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
        "3 episode wad for ultimate doom with the usual tech/hell tech/hell setup with a few exceptions from time to time. This is an expansion from my cbspeed.wad I realeased last August, so the first episode is an updated version of cbspeed with hopefully improved gameplay and layout flow. Episodes 2 and 3 were solely built from scratch. The wad is designed to hopefully give a varied and at times challenging gameplay experience.",
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
        "32 maps no bigger than 1024 X 1024. Some maps have areas outside the 1024 X 1024 block but they are not part of the playing area. The idea was to focus on quick and fun gameplay.",
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
        "Toke's coop wad! Have fun playing it. R.I.P. Toke!",
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
        "A (almost) TC that follows the idea of making a series of small maps with a storyline about hellspawn and such (similar to doom2). It was supposed to follow the 'classic' doom2 style gameplay, so it should feel like doom2.",
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
        "32 maps for vanilla Doom, each one made in one hour over a period of 3 weeks. Bear in mind that all of these maps were designed for pistol start, so be prepared for a rough ride on continuous play.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "This is a 30 level wad with small maps, made for use with ZDoom bots, on cooperative or deathmatch modes. The ZDoom bots are a little 'stupid', so some pickups were placed strategically on the maps. Note: you can play it on any source port.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "A speedily-done Ultimate Doom megawad. The 27 layouts for the three new episodes were first blocked out starting midnight 1st Nov. The 27th map was thingplaced and balanced at about midnight on 10th Nov, so the total build time for this is 9 days. A number of days after release were spent on finding bugs and balancing the maps for single and co-op play. No jumping or crouching (enforced). Freelook allowed. As with this mapset's predecessor Griefless, ***A LIMIT-REMOVING PORT...",
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
        "This wad is better than your pile of crap. Read the story below for a real description.",
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
        "One man, one difficulty and a lot of demons. Battle you way through against demons in order to escape Mars.",
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
        "This WAD replaces all 32 levels of Doom2. All levels are playable solo, co-op or DM, to give you maximum playing opportunity! Short descriptions of each level are included in DFEARLVL.TXT (included with DMONFEAR.ZIP), but the basic ethic behind the design is simple: I aimed to provide 32 short, sharp shocks for Doom 2 that were both highly playable and visually impressive. I hope I've succeeded.",
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
        "Monumentale gigantische Kathedralen und Tempel. Eines der optisch beeindruckendsten WADs ueberhaupt.",
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
        "27 maps by 27 different mappers. There were no rules and all maps were accepted.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "Imagine a world where Doom II was never released, and the only information that ever came to light was the level titles. Then imagine that nearly 20 years later, some people decided to make some levels based on how they interpreted the level name. Those very levels are in this WAD file.",
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
        "Vanilla-compatable megawad Fantastisches Leveldesign und Gameplay.",
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
        "32 Level Megawad for use with Doom2. Hopefully a fun packed collection of mainly small to medium sized maps.",
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
        "Doom II The Way id Did is the long awaited sequel to 2011's Doom The Way id Did. It comprises 32 levels that endeavour to both look and play as though they've come from Dallas, 1994, from the very toiling hands of Sandy, John and American. The idea was originally kicked about by Jason Root aka Hellbent, whose passion for the game and knack for growing enthusiasm resulted in one of the most successful community collaborations in Doomworld history not to mention an impressive...",
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
        "Welcome to DOOM 2: UNLEASHED - An all new 32 level megawad for DOOM 2 put together by various authors in the community. Known as the Pcorf Community Project at the Doomworld forums. After the release of Whispers of Satan I wanted to make a community megawad. The aim of this project was simple and it was to create a level for DOOM 2 using only stock DOOM 2 textures. 15 months later I received 40 levels divided into two episodes, the first DOOM 2 UNLEASHED which contains the...",
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
        "STORY SO FAR... The year is 2099. You are half-cyborg. Most humans are in this year, with",
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
        "A set of 32 carefully collected WADfiles for your entertainment. (What else?)",
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
        "It's a sad life on the Space Marine IT Help Desk. Especially when you're stuck on the graveyard shift. Night after night of 'Did you try rebooting?' Not exactly what you expected when you signed up. You figured this call would be more of the same: some garbled voice mail about 'gremlins in the system'. Gremlins? Seriously, how old is the guy who left that? Ninety five? He probably remembers vacuum tubes. Now you're here though, something isn't right. This facility should be...",
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
        "This is a one level WAD for DOOM II that is designed This level is packed with new graphics, and is desig...",
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
        "This is a collection of 32 levels previously unreleased (with the exception of REJECTS.WAD) this is a collection of levels I made from November 2009 - December 2010. Hopefully, you'll find something in here worthwhile and fun to play.",
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
        "Offizielle 9-Level Erweiterung von Nerve Software. Fantastisches Leveldesign der BFG Edition.",
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
        "This wad replaces most of the Doom monsters with Zom Very funny if you're tired of Imps",
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
        "'At some point in time I got the idea that it would be really fun if John Romero dusted off the ol' doom editor and made another map for Doom episode 1 for old time's sake. He once replied to an email saying 'maybe I will!' but never did get around to it as far as I know. This project asks the contributors to carefully study the original maps and try to make them as John Romero and Sandy Petersen did. The guiding principle in the project is to pretend that id Software...",
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
        "Six episodes of classic Doom maps, originally candidates for last year's 'Doom the Way Id Did', arranged in an episodic format. DTWID was a long but fruitful journey, with somewhere around a hundred map submissions in total. Given the strictness of the rules, most of the maps cut were not done so for reasons of quality. On their own they're quite solid maps, deserving their own showcase. This collection, while not 100% complete, contains the vast majority of submissions to...",
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
        "The idea behind this was to create a classic DOOM Megawad in two weeks. You can follow the development of the project at this thread in the doomworld.com forums http://www.doomworld.com/vb/showthread.php?s=&threadid=39487 As calling the project DOOM 3 should be way too ambitous, we droped the idea, but the mod follows the plot right where Final DOOM lefts out. There are multiple references and alusion to elements from other chapters in the DOOM series, I'll send a special...",
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
        "MegaWad for The Ultimate Doom, with the purpose to be a reboot of the original game. Meant to be experienced in pure vanilla fashion. Some novel ideas here and there, and an attempt to push the engine to its limits at times, but mostly a 'traditional' wad, with a 'gameplay first' mindset, and without any of the modern trends of wad design, like ultra-realistic visuals, walking sims, or slaughtermaps.",
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
        "Makes the monsters anatomically correct again, with Fantastisches Leveldesign und Gameplay.",
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
        "These are the PCX files from John Romero's DOOMII demo during the ...",
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
        "Harmata said it best: 'I could've picked random wads from idgames and then combine them in a megawad and it would be exactly the same thing as this project.' The goal of this project was to get as many mappers as possible to contribute to a single project. All maps were accepted and nobody was rejected. This megawad contains 40 maps from 40 different mappers from the Doomworld forums. VANILLA / LIMIT-REMOVING 01 Bean, by cannonball 02 Daemonum Malum, by DooMAD 03...",
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
        "54 maps by 55 different mappers in 3 wads. http://www.doomworld.com/vb/wads-mods/64899-",
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
        "The idea and goal of the project was to get maps from as many mappers as possible, one map per person, during the course of the year 2015. Anyone could participate, and there were no restrictions regarding the theme or the quality of the maps, other than 'no abusive maps that use scripts to change people's settings or are 1 GB while unzipped, etc.'. The maps are split into 3 wads: DMP15A for vanilla and limit-removing compatible maps, DMP15B for Boom compatible maps, and...",
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
        "A nine map episode 1 replacement Fantastisches Leveldesign und Gameplay.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "32 tough levels that were inspired by Plutonia, Momento Mori and Icraus. The levels aren't super detailed like Darkening or Eternal but just as good as Doom and Doom 2's levels. This is the third and final installment of the Dsv series with better detail from my previous two wads. Like my previous work, I like to add new weapon graphics from Unreal, Quake2, and Blood to replace the boring ones. There are some deh modified stuff like the Big Brain (which takes more damage)....",
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
        "A hidden UAC base on Phobos: Not satisfied with their achievements, scientists have developed a new kind of teleporter, capable of not only sending things through space, but also through time and into different dimensions. There were two prototypes of this machine but one morning, one of them was found missing! Needless to say that one lone marine is sent through the second device, to find out where the missing prototype might be and who is behind the theft...",
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
        "Mapping is hard. A lot of us have been wanting to map for a while, but can't really get over that initial hump. There's only one way to improve, but oh man is it painful to just grind, grind, grind until you get a clue and figure out what to do. We get awesome ideas! Oh, man, this would be so cool! Then we jump in and...stare at the grid for hours. For 2016, I really want to better my mapping abilities and put out some good stuff. Two friends, Mystical and Gardevoir, said...",
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
        "A megawad full of small & fast levels. A reimagining of the Doom II storyline.",
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
        "36 maps with various difficulties Fantastisches Leveldesign und Gameplay.",
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
        "32 Levels replacing Doom2, basically following the Doom2 'story'. A reworking of my old Enjay Doom project (originally from 1994 for some of the older levels). Therefore, the look is a little primative in places.",
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
        "All levels are designed for vanilla Doom2. Recommended engines: prBoom-plus with '-complevel 2' and (G)ZDoom in 'DOOM (strict)' compatibility mode. Jumping should be disabled. For OpenGL ports like GZDoom and GLBoom-Plus you can add EPIC2GL.WAD after EPIC2.WAD for HI-RES skies: glboom-plus -file epic2.wad epic2gl.wad",
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
        "33 hard maps that I tested myself. Max demos for all maps included (speed demo for map30).",
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
        "Estranged is a 32 level megawad that uses stock Doom & Doom2 textures, I would describe the levels as cleanly detailed with a variety of environments. Levels are playable from pistol start. Story: You are a marine stationed on a UAC mining facility on a desolate world on the edge of the known galaxy. Whilst performing routine operations you lose communication with the main base, upon your return you find that demons have overrun the facilities. You send a distress signal but...",
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
        "Riesige mittelalterliche Burgen und Raetsel-Level. 32 massive Schlachten von den Machern von Final Doom.",
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
        "This concludes the Doom Core trilogy Eternally Yours is an 11-map episode that should wor...",
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
        "classic doom megawad with 34 maps. I started this megawad in 2002! then I abandonned the project and reassumed it in 2011. back in 2002 I made the following maps: 01-11, 21, 23, 26, 70% 0f 24 and the drawing for 25. at that time my computer sucked and I used doome42 for editing, doomed42 is by far the crappiest doom editor ever, it's buggy as hell and too way primitive. it seems that my maps 24 and 25 could'nt be built with it because of its limitations, they crashed every...",
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
        "Eviternity is a megawad comprised of six 5-map episodes (called Chapters) plus two secret maps. This project exclusively uses OTEX, a brand new high quality texture pack by ukiro. Eviternity's six chapters explore a series of unique and varied themes, each featuring classic gameplay with an interest in making each map hold its own unique identity and personality. The themes are 'Medieval', 'Techbase', 'Icy Castles', 'Industrial / Brutalism', 'Hell / Gore / Alien' and...",
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
        "Well, this is a very impressive episode including 9 All the levels in this episode were created by mysel...",
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
        "Final Fantasy 'the best Japanese RPG ever' meets Doom 'the best FPS ever'. Final Fantasy Doom is a Total Conversion for Ultimate Doom with 27 single-player levels (3 complete episodes) and 7 Deathmatch levels (Episode 4). Final Fantasy Doom features new music, sound, monsters, weapons and graphics, all based on the game Final Fantasy 3 by SquareSoft. In Final Fantasy Doom you are MOG, FF3's cutest hero, and you must fight your way through forests, caves, mountains, castles,...",
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
        "A Doomworld community project, startes by Keeper of Jericho, in which the goal was to make the best map each author could make without exceeding the five room limit.",
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
        "A 21st century DooM 2 episode that is big, innovative, good-looking, and of course, HARD! This wad features desert and mountain levels, and there are plenty of levels beside the sea. There are also city levels and hi-tech levels. But - every style is represented here, even if it is only 1 level! (except the Spirit-World style) For those of you who like secrets, there are at least 3 in each level (except 25-27 and 29-32 where there are none). And you'll be pleased to know...",
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
        "Freie Deathmatch-orientierte Arena-Kampagne (32 Level). Fokussiert auf schnelle Schusswechsel & Layouts.",
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
        "Komplett freie Standalone-Spiele (Phase 1 & 2)! 68 Level insgesamt. Kein Original-Doom erforderlich.",
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
        "You are playing Quake, you walk into the Slipgate only to find out it's Freudian, suddenly, you are in Doom.. #doomtwid realm, you have angered the #doomtwid gods and they throw you into the random maps for invading their privileges on internet. Are you agitated enough to survive the Bull#$@$ maps?",
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
        "A complete episode for Doom2, with all 32 maps replaced... I think that these are quality levels: cool architecture, good light effects, studied use of textures... And I hope, with a exciting gameplay! The two guys that appears in the main screen are Chiquito de la Calzada -'Child of the Road' (!!!), spanish humorist-, and El Fary -'The Fary', matchless spanish singer, like Frank Sinatra, Bob Dylan or other international stars (!!!)-.",
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
        "Something sinister is lurking in UAC headquarters. Grab your gun, head to the roof and shoot your way down through the building to get to the bottom of the mystery. This is a 32 map Doom 2 megawad - boom format, complevel 9. Each map is a floor, each floor has a lift, each lift needs a key. You'll work it out I'm sure, just keep going down. These are small, fairly hard maps with high body-counts, and gameplay I would describe as 'chaotic-evil'. The music tracks are my own...",
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
        "(brief) GoldenEye Doom2 TC Fantastisches Leveldesign und Gameplay.",
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
        "30 maps made in under FOUR hours.",
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
        "Cyberpunk Total Conversion mit 21 neuen Leveln. Eigene Cyber-Waffen, Soundtracks & Gegner.",
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
        "A PRBoom+ complevel 9 megawad resulting from the Progressive Fiction Community Project. The project's aim was to create a community project with a running story, thematic continuity and gameplay gradient. See story.txt for story info.",
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
        "Vollstaendig handgezeichnete Sci-Fi TC (13 Level). Eigene Welt, Waffen und Soundtrack.",
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
        "Read the read me for full story 32 levels of difficult gameplay and decent deco.",
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
        "Read the read me for full story 32 levels of difficult gameplay and decent deco. This version is a patched version of the origional. Several fixes have been made, and it was long overdue for release. Enjoy.",
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
        "Der Urvater aller 'Slaughter-Maps' von 1997. 32 anspruchsvolle, hochintensive Level.",
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
        "Die offizielle Fortsetzung mit 32 noch haerteren Maps. Komplett neuer Soundtrack und duestere Kulissen.",
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
        "* Full replacement for Doom Epesode Two * Support fo Fantastisches Leveldesign und Gameplay.",
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
        "32 Level Sci-Fi Megawad von Wraith Corporation. Zukunftskulisse, neue Sprites & Texturen.",
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
        "32 map megawad with collections of maps made from different periods of my doom mapping hobby, along with new ones. All old maps are re-polished to make them consistent in look with the newer ones. Maps are 'progressively' chained together (so the previous map's end is more or less linked to the next one's start). Difficulty is medium, sometimes hard, but not insane. Experienced players can do it in UV without any problem. HBFM29.wad: the full version of MAP29. Load this if...",
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
        "32 levels from scratch. All varied in year and quality, but what can ya do? This text is unfinished, I cannot remember where all these midis are from, frankly I don't care too much, I just want to get this damn thing off my comp, and I apologize if I cannot give credit where it is due.",
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
        "Your squad was sent to investigate a rogue research Shortly after landing, your squad is ambushed by an...",
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
        "Herian has no story it is simply a 32 level TC made primarily for the single player. The infuence for it came from my favourite games mentioned below. 32 NEW levels, NEW textures, NEW sounds & NEW monsters All levels were made for single play. Deathmatch and co-operative play is possible, but not optimized. Many of the levels are too large for deathmatch.",
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
        "Herian2 is fully compatible with only ZDoom version 1.17 or later. Herian2 has a simple story line. The setting, time-wise, is prior to the fearsome adventures that were seen in Doom and Doom2 the likes of which has never been see again. Many tales have been told since but, with few exceptions, most although.... perhaps interesting, do not have you quaking with fear like the originals. Before the days of Doom many of the characters that we came to know in these tales and...",
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
        "Klassisches Doom Community Meisterwerk Fantastisches Leveldesign und Gameplay.",
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
        "Klassisches Doom Community Meisterwerk Fantastisches Leveldesign und Gameplay.",
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
        "The project from iddqd.ru the community, basis of which are maps, made in a speed- mapping course, on which only 4 hours were allowed (each speedmapping contest - 4 hours length). The first speedmapping contest ('Nature'), most maps from which are in this megawad, was in 2008.26.01. The second speedmapping contest ('Hell') has taken place on 2008.02.02. The third ('City') and the fourth ('Boss-level') speed- mapping contests were onto 2009.08.03 and 2009.18.07. Not all maps,...",
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
        "A large new level for Doom 2 I've tried to create something really unique with th...",
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
        "Klassisches Sci-Fi Megawad von 1996 auf einem Raumschiff. Eigene Texturen und toller 90er-Jahre Soundtrack.",
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
        "A 16+2 level wad, technical reality + tactical fight Fantastisches Leveldesign und Gameplay.",
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
        "Interception is a vanilla-compatible megawad for DOOM II. In the far distant future, you are part of a team of marines, who are sent a distress signal from an archaeology site. Once you arrive, the rest of your team is killed by some nasty hell spawns. Now it's your job to stop the invasion, or die trying. Along the way, you explore ancient military bases, an underground necropolis, and Hell itself. Final Words from the Author: So, I've finally had a chance to sit down and...",
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
        "Alot of people seemed to be blown away by Invasion V1.7. Well, you people ain't seen nothin' yet. Whats New? even more quality, more NEW ORIGINAL textures, and more flats... Oh, and more music.",
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
        "This is sorta a BETA version of Invasion II, the next version will be called Invasion 98 (for TeamTNT's BOOM engine). And I will make a full attempt to kill ALL the bugs. Which should't be that hard, anyway.",
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
        "Smoking hard inside a bar, I'm drinking like a fish.",
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
        "A massive UAC base hidden deep underground Save, soldier, because you're going to need it (par ...",
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
        "Japanese Community Project is a 32 level megawad made by Japanese Doomers. Doom is a minor game in Japan, so Doom community in Japan is very small. The community having been weakening year by year, we started this project for strengthening our solidarity, improving mapping skill, and so on. We aimed a low-threshold project which anyone could participate. Some doomers were inexperience mappers or beginners, so Tatsurd-Cacocaco, a project leader, supported map's detail and...",
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
        "The third and final part of my solo megawad. Part 3 of 'Jenesis' adds 12 extra maps to the first twenty (which have all also been modified for the better in some way or another) and replaces the final map, as its predecessor did. 'Jenesis' is a 32-level megawad for Boom, complete with a new final boss, a new monster, new status bar graphics and a recolored message font. The aim of this project was mainly to hone my skills in traditional Doom level design, but it was also to...",
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
        "These levels, which were the results of four months of work from October 1995 to January 1996, are some of the hardest ones you'll ever experience. However, I did notice a few things that I felt needed improvement. I made the textures more realistic to the surroundings of the boards, eliminated some unwanted items, and installing walls where open space once was -- this would sometimes make the levels too easy to play. Also, on some levels, I changed the passageways from the...",
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
        "Heh..we made 32 maps, finally, so you all 'HR-skilled' players let's go and beat them :-)",
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
        "A vanilla megawad for Doom 2 containing 32 short, chaotic and challenging maps. The wad's premise was to create at least one speedmap a day. There was no set time limit, times ranged from 1.5 to 3.5 hours. Don't let the word 'speedmap' scare you off though! In the author's opinion, this wad contains many of his best maps to date.",
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
        "This is an all new a 32 level episode for doom2. You must adventure through many unique worlds killing awful monsters along the way. When you play this in Zdoom you get all the level names. I learnt how to use mapinfo, add new textures and add new music. The first 11 levels are on the ground and then you go into space before heading into a vivid colorful world of beauty. Many arch guys and barons and blue enemies and demons will try and stop you but you have plenty of...",
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
        "The experience of playing through Knee-Deep in the Dead is one that many gamers will never forget, but through the years the episode has become too easy for many to play through and enjoy the way they did the first time. Knee-Deep in ZDoom is an interpretation of Id Software's original groundbreaking production, Knee-Deep in the Dead. The team has focused on expanding the original episode by utilizing the power of Randy Heit's ZDoom port; adding new areas, features, and...",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "A tune-up community project led, compiled and with new graphics by joe-ilya. Each map is a tune-up of joe-ilya's earlier works such as 'lost maps', '1024 abc' or other contributions.",
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
        "Designed for Deathmatch 2 for DOOM II Weapons: ----- Lots of hidden areas and traps/tricks 2",
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
        "Staring into the glowing teleporter, you vaguely remember an",
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
        "Well, I'm back. I thought Erkattae e was my last shot but I wanted to make more maps so here we are. Mano Laikas is a full 'classic' Doom2 megawad. Aesthetics are pretty simple, the themes vary from E1, E2, alpha and what I call 'heteroclites' and my goal as for Erkattae e was to make mid sized maps with moderate monster count so they can be completed in 10-15 minutes at best. Also I seem to have developed a style that some praise and some others hate (those who rate my...",
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
        "You are a Martian terraformer stuck in a small station in the middle of nowhere. Since the beginning of the terraforming project, the United Organizations of Earth have been threatening to take over. Recent events have prompted an invasion from Earth, only something went wrong with their new 'space-warp' capable ships... that were built by some company with the initials 'MS'.",
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
        "Full 32-map megawad done in the styles I like the most, E1, E2, Alpha and Doom2 Techbase. the mapset is divided into 4 episodes and it covers 4 geological periods of time, from devonian to triassic, and each map represents an age.",
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
        "Mayan Reynolds is a small vanilla compatible episode The WAD was speed-map project made over the course o...",
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
        "After three successful(ish) endeavours and a year of slow-cooking, the MAYhem crew is back with 34 more maps made in May! This time, it's a very gothic edition of MAYhem where we decided to restrict every level to exactly 1,500 lines. These maps may be black like our souls, but we hope this WAD brings joy and laughter to those who play it! The Maplist: 01 - No Mayhem! by Walter Confalonieri 02 - The Silencer by Impboy 03 - Venom Canyon by jmickle66666666 04 - Prayers in...",
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
        "After Two successful...ish endeavors, the MAYhem crew is back for its biggest, most ambitious, and closest to on time campaign yet! (Everything said and done before July!) As usual the maps were made in a one month time span (the month of May). Only this time, to commemorate the cultural zeitgeist of the era, all the maps were confined to a 2048x2048 playing space, so years from now, we can look back on one of the definitve passtimes of the early months of 2014! While...",
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
        "Unreality has been struck with extreme stupidness and you have to kill all the demons and stop John Romero!!",
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
        "Der erste grosse Community-Megawad-Klassiker von 1995. 32 legendaere Level voller Doom-Historie.",
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
        "Der noch groessere und detailliertere Nachfolger von 1996. Ein absoluter Meilenstein der Modding-Geschichte.",
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
        "A megawad which was 95% finished two years ago. Never got around to adding the final touches until now. The levels are relatively small, hence the name. Other than that, there's no link between them. Some are horrible, some are good. I had planned to add customized level names, rearrange the soundtracks and some other tidbits but have never done so.",
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
        "It is the end of the clone wars of the 52nd century. All of humans is happy but the evil monsters of Hell come to kill everyone. Uh oh. Look like Doom guy need to save the day again.",
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
        "A collection of maps experimenting with the concept of simple textures in order to emphasize the fundamentals of level design (lighting, elevation, thing placement, simple contrast, etc.)",
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
        "Moonblood is a megawad (32 levels) for Doom 2. The Project has started with remastering the levels from my first wad, Eclipse, but the changes became quite big and I decided to turn it into Moonblood (it was initially called Revclipse, then after Reclipse). The levels were most inspired by early Scythe 1 levels, Jenesis, Mano Laikas and DTWID series.",
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
        "This is a large map that replaces MAP01 An older level I decided to finish in a sort of slau...",
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
        "Mutiny is a 16-level, Limit Removing Community Project Episode for Doom 2 intended to be the spiritual successor of late 90's cyberpunk themed wads such as STRAIN, Hacx, Nimrod, Dystopia 3, and Perdition's Gate. It features new colorful, high contrast, (intentionally retro) textures inspired by the works of Iika Keranen and Anthony Czerwonka, complete with plenty radioactive signs, warning stripes, computers, slime, and rust.",
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
        "Thirty-two maps, all first made during November and then further refined in December 2018. Wow, talk about an intense month... Nanopineapple was designed for Boom ports and pistol starts. No jumping, crouching, or free aim! This breaks the WAD. Infinite height is encouraged for a more challenging experience, but turning it off won't break the game. Enjoy!",
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
        "Necropia 2010 is new megawad with 32 new levels for use with Doom2",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "This project started on January 10, 2006. The first NDCP had been completed and released. Towards the end of that project talk had been going on about a sequel, and thus this project was formed. PumpkinSmasher took the reigns as project leader and started map sign-ups. The goal was not to take near as long as is took to finish the first NDCP. This goal was not achieved.",
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
        "Supplementary graphics for doom with dmgraph exe and a batch file to replace the pinkies with a b...",
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
        "Nightmares of Loki is a cryptic hallucinagenic Nightmare. This version contains 3 episodes and level starts to the 4th This game was started in 1995 under the name 'doom legacy' After a source port was discovered with the same name, it has been renamed in 1999 to 'Nightmares of Loki' The story is little to nothing but the levels are the focus. These levels are HUGE and they'll piss you off, so SAVEGAME! works great in zdoom, 2 levels crash in jdoom. made for the original...",
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
        "Olde replacement for the original Doom 3 Episodes. Released purely because of interest in old, cheesy 1994 WADS. This is possibly not the first version of this file, but an early revision. I no longer have the original The file date may or may not be accurate. The original was originally uploaded to the Compuserve action games forum in 1994.",
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
        "Olde replacement for Doom2. Released purely because of interest in old, cheesy 1994 WADS. This is not the first version of this file. It may be based on an early revision but this one has been 'legalised' by removing maps 31 and 32 which were just edited version of the Id originals to make them a little more like the Wolf originals. It is likely that this is not even based on the original version of NJDoom but an early revision. The file date indicated a December 1994...",
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
        "Oldschool 4-episode adventure for The Ultimate Doom, featuring some of the most devilish map design this side of 1994. 'Nuff said... ish. Similar to DTWID, the spirit of id is ever-present, albeit cranked up to 11 with a devious new spin on the classic episodes' themes. Along the way, you'll enter parallel worlds, explore derelict spaceships still floating in the sky, and return to some eerily familiar locales on the far side of Phobos and beyond.",
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
        "//////FILE UPDATED (Ver 1.2) 3/27/16////// 32 maps for any limit removing port. Like its predecessor, NOVA II is a megawad designed to give new mappers a chance to flex their mapping muscles and develop their abilities with the help of veteran mappers. Not all the maps in here are from newbies, but for the most part what you'll see in here are people's first forays into the world of Doom mapping.",
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
        "//////FILE UPDATED (Ver 1.1) 8/31/15////// Here we worked on creating a 32 map megawad consisting of efforts by those who are new to the Doom mapping experience. The restrictions are as follows: - All maps must be in limit removing format (-complevel 2 if that definition means anything to you) - All maps are to be made entirely from stock Doom II textures.",
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
        "There was hexen and hexen2, startrek and startrek the next generation, quake and quake2. What do they all have in common? The sequel was a hell of a lot better than the original which was good any ways. Well now theirs #1kill the next generation which follows the tradition. It features 23 never before seen levels with new textures and music from all off doom ever created under Id. It was built with the experience of building 13 levels for doom/doom2 and the experience of...",
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
        "Klassisches Doom Community Meisterwerk Fantastisches Leveldesign und Gameplay.",
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
        "Biowar is a doom2 megawad in the spirit of ICARUS and others. It consists of 18 regular levels and 1 secret level. The levels range from moderate to hard and are all very well done. Enjoy!",
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
        "Pagodia is a vanilla compatible episode for Doom 2 f The wad is split into 3 distinct episodes: Village, ...",
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
        "This is a megawad comprised on shovelware-esque levels by the Doom Community",
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
        "Kommerzieller 32-Level Megawad-Klassiker. Schnelle Kaempfe und intelligentes Layout.",
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
        "Pinochestein 3D edicion GL (GL edition) is a total conversion of the original pinochestein 3D that tries to take the best advantage of the new features of ZDoom based ports through UDMF format mapping and lighting implemented by hardware acceleration through OpenGL (hence the name). This version not only improves lighting decorations, also fixes all of the maps to give a more Doom-ish style and a more futuristic touch according to the previous history of the game.",
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
        "Pinochestein 3D edicion GL (GL edition) is a total conversion of the original pinochestein 3D that tries to take the best advantage of the new features of ZDoom based ports through UDMF format mapping and lighting implemented by hardware acceleration through OpenGL (hence the name). This version not only improves lighting decorations, also fixes all of the maps to give a more Doom-ish style and a more futuristic touch according to the previous history of the game.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "32 Doom2 Levels.",
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
        "32 single player Plutonia-themed 1024 maps designed by various Doomworld community members.",
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
        "An add-on sequel for Final DOOM's 'The Plutonia Experiment'. It emulates Plutonia's design and gameplay styles and follows from where it left off.",
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
        "One thing that separates the two halves of Final Doo So, what if Dario and Milo Casali were musicians the...",
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
        "Plutonia Revisited Community Project: 32 Level. Eigene Texturen, tropische Kulissen und Herausforderung.",
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
        "32 new levels for Doom][(see P-XMAN.TXT for more). Not just shoot 'em up, not just puzzle solving. Two levels even make you search for something before you can exit.",
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
        "Have changed the route since last recording Thisone is a bit faster",
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
        "Ray is BACK and ready for more vanilla-compatible action in the tropics! This is a community project and sequel to my older wad, Ray Mohawk's Manic Monday. --- STORY: A year has passed since Ray successfully destroyed the UAC and several of their Nukage production compounds. Soon after Ray completed his mission, the prestigious General Kawasaki, one of the last surviving military leaders on Earth, sought him out and rewarded him with a Medal of Courage for his heroic effort....",
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
        "Feeling a little sick of the winter chill Step out into the sunshine with Ray Mohawk on a ques...",
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
        "This 32-level Doom2 WAD is the first (and only) Mac-made conversion WAD.",
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
        "REKKR is a total conversion for Ultimate Doom v1.9",
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
        "32-Level Meilenstein von 1997. Duestere Atmosphaere und grandiose Architektur.",
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
        "A 32-map single player/cooperative wad. Contains basic support for deathmatch, as well as difficulty levels. It entails two episodes, with episode 1 being map 1-15, and episode 2 being map16-30. There is a secret level on map 15 as well as a credits map after map 30.",
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
        "Resurgence is a 32 maps megawad project, an upgrade from the unfinished Surge project. The maps are similar to Speed of Doom, ranging from Scythe/Plutonia-like maps to Speed of Doom to Resurgence-unique. The difficulty is considerably less in places but there are several challenging maps to plow through with the occasional slaughtermaps. (For those who don't like slaughter, avoid Map11,25,27,28,30) Sizes vary greatly (for the sake of variety), ranging from quick paced maps...",
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
        "20 levels, designed for Single Player / CO-OP Fantastisches Leveldesign und Gameplay.",
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
        "A brand new, 32-level megawad for Vanilla Doom 2.exe. Inspired from and made in the style of 90's megawads.",
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
        "REVOLUTION! is an unofficial add-on for id Softwares DOOM2 It offers 32 NEW levels with lots of new textures, sprites and flats.",
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
        "Bunte humorvolle TC im Stil klassischer Retro-Shooter. 5 Episoden voller Spass, Musik & neuer Grafiken.",
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
        "Rowdy Rudy is back and he's madder than ever! THE BACKSTORY: 8 months have passed since Rudy took out the evil cult known as the Protectors, avenged his brother and saved his home city. Impressed by his efforts, many different entities reached out to him and offered him money for mercenary work. Rudy was not interested, he just wanted to live a somewhat normal life and continually refused offers. Having realized it wouldn't stop and could potentially lead to confrontation,...",
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
        "Rowdy Rudy's Revenge: a 10 map partial conversion 100% vanilla, heavy on the dehacked",
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
        "Made for 'vanilla' doom2.exe and includes: -20 single player maps (map01 upto 20 = all 11 maps from sci.wad + 9 new maps), -10 bonus deathmatch maps (map21-30) -some new textures and sprites including some weapons and a renewed scientist sprite.",
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
        "20 shortish levels split into three episodes: Rear Entry, Knee Deep in the Sludge and Into the Blood 'The Source' - That's what they call it. No one quite knows what it is, apart from the birthing grounds of the monstrous army that invaded your planet. Your team was sent to destroy it, but the way in was heavily guarded, and now only you survive. You'll have to find another way in... The maps are all relatively small with 2 min par times as average. all are designed to be...",
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
        "A 32 level megawad for doom2. This megawad doesn't have perfect texture alignment, insane detail and fantasticly hard gameplay. Instead it focuses on small fun maps to blast through without much thought about defense. The difficulty rises steadily as you go through the maps and the last few should challenge even the best.",
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
        "This is the sequel to my old megawad 'Scythe'. Expect better looking and bigger levels. unfortunately, the last 3 levels are missing, because I am so lazy and I haven't been satisfied with my attempts at making map28. I _might_ release the last 3 maps some time in the future, but do not count on it. (Update: the last 3 maps are now completed!)",
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
        "Das legendaere geheime Bonuslevel der Xbox-Version. Wird als E1M10 im ersten Doom-Kapitel gespielt.",
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
        "Baphomet was only doing Satan's bidding by placing a Somehow they didn't understand that you're the reaso...",
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
        "Offizielle 5. Episode von Doom-Schoepfer John Romero. 9 bockschwere Level im klassischen 1993-Stil.",
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
        "Offizielle 6. Episode zum 30. Doom-Jubilaeum. 9 brandneue Level voller Herausforderungen.",
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
        "7 sp/coop maps, vanilla compatible (besides map07), Some of these maps are from submissions to communtiy...",
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
        "SKULLDASH OVERVIEW: Welcome to Skulldash, the fast-paced speedrun arcade mod For Doom II. The objective in skulldash is to collect the Skulltokens scattered around each level to unlock the exit. While collecting the Skulltokens you have to race against the clock, as there is a time limit to beat all while defending yourself from the hordes of monsters from hell. FAST PACED GAMEPLAY: While on the hunt for the hundreds of Skulltokens scattered across the various realms, you re...",
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
        "A compilation of 32 slaughter maps, each designed to be played from a pistol start.",
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
        "Another compilation of slaughter maps, 35 to be precise. Designed to be played from pistol start.",
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
        "A five-map episode for Doom II with Thy Flesh Consum Fantastisches Leveldesign und Gameplay.",
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
        "Megawad, based on the results of three speedmapping contests, held on www.iddqd.ru in 2014-2016, with next themes: 1. Industrial 2. Ancient city 3. Free disigned style, but there was limit of 3-colours of textures/floors on map All maps were built in limit of 4 hours, than were sent to a contest judge and there was voting for best maps. Later, maps we not be able to upgraded, but with fix of some major bugs and gameplay balance.",
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
        "33 rasante Level mit hohem Tempo und tollem Flow. Extrem beliebter Community-Klassiker.",
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
        "This is extremly hard if not impossible to complete the only bug I know of is that you can't save it on ...",
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
        "20 levels: Some short, some deadly, most with stone-like textures, and each with midis from Doom, Doom 2, Requiem, Momento Mori 2, etc. (Look under 'Level Information' for more info) Be sure to look for the 'inspiration points' in certain maps.",
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
        "STRAIN is THE largest DOOM II Total Conversion. And it's FREE! This is the product of our painstaking effort at a true successor to DOOM II. We hope you'll agree that it is much better than 90% of all those $50 shovelware first person shooters that wish they were DOOM! STRAIN includes 32 new levels, all new music, and a complete story which carries through the levels. It also contains 100+ new textures, and many new baddies, weapons, graphics, and sound effects. Yes, that's...",
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
        "This is a reupload of the legendary 1997 total conversion STRAIN to repackage it in a manner suitable for modern users. The original version uploaded to /idgames on September 14, 1997 was distributed as a self-extracting LZH file, which one could run only in DOS, split into four pieces so the user could copy them to four 3 -inch floppy disks and install the WAD that way if they wanted. Because both DOS and floppy disks have been obsolete for decades, this method now serves...",
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
        "The first maps I ever made, they suck, look terrible and are cramped as shit. A reimaging/remake/restyling of DOOM II. Inspired by a ton of stuff.",
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
        "Sunlust is a set of 32 boom-compatible maps for Doom II, designed to be played from pistol start. The maps meander through a range of themes, from traditional bases and temples to abstract hellish, void, and tech aesthetics. UV is designed primarily for ubermensch doom-gods, thus we encourage most players to start off on HMP or lower.",
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
        "A DooM 2 episode with a Sonic influence throughout, courtesy of a brainwave I had during the map drawing phase of Licence to Spell DooM. It has the Sonic layout (zones, acts, bosses) and also has some Sonic features (monitors, springs, water). It also uses the health system seen in map 09 of Fragport (which was also Sonic-inspired) but will still look and play like a normal DooM 2 megawad.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "Swift Death is an extremely hard megawad, calibrated for speedrunners. I recommanded for novice players to play in 'easy' ('Toilet Break', or 'Soft Event'). SD is realized in speedmapping sessions, for the most parts of maps. These maps are generally small, and contain on average 75 monsters. They are also compatible for the duel / deathmatch.",
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
        "The idea was to remake a map from one episode, but as if it had appeared in another episode.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "Fuzzball Fox came up with the concept of using only 10 textures for a map, and challenged the ZDoom forum members to create 15 single player maps and 5 multiplayer maps that used this limit, and also limit the player to a 2048x2048 area. The resulting mapset is split into two episodes, SinglePlayer (starting MAP01 and ending on MAP15) and Multiplayer, which can be played either as a second single player episode, or with up to 3 other players co-operatively (starting on MAP16...",
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
        "34 maps made under or equal the file size limitation of 64 kylobites, all these levels are vanilla doom / DOS executable friendly.",
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
        "Who cares about some corny story line, who reads it anyway but the geeks who write it. SCREW IT!! Just play the f#%$&&*! thing. These are still the best maps ever made...4 DOOM.",
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
        "Legendaeres Levelset der Doom-Community. Fantastisches Leveldesign und Gameplay.",
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
        "It has been 3 years since the destruction of the huge demon that masterminded the invasion of Earth. Your journey back home was almost as difficult and bloodthirsty as your journey into hell, but you made it. You had expected to be made a hero; after all, you had saved Earth and all it's worlds from a fate worse than death. But it was not to be that way. Upon your return, you discovered that things had begun to change. The UAC, deprived of some of it's key personnel, had...",
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
        "After a hard nights driving about in the galaxy, your trusted space vessel 'Margie' runs out of fuel and you crash on some ugly planet. Uhm... thats it I think. Oh yeah; You have to kill stuff too. (If you depserately need a storyline, go play something else or think of one yourself.)",
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
        "Hello fellow Doomers Here are 7 SP/coop maps designed for DOS Doom2",
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
        "This is a re-interpretation of the original DOOM & DOOM II levels. It pretty much is a retelling of the DOOM story in a more up-to-date format. This wad contains 34 levels and it is an attempt to recreate the original DOOM maps while still keeping the classic DOOM experience.This wad includes cut-scenes (that are skipable), smooth DOOM weapon animations, a couple of new weapons including the railgun with sniper scope, and a second pistol you can pick up and go Lara Croft on...",
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
        "The Great Bicycle Mystery 2 is a 32 level journey that will take you to the lowest pits of hell to the brownest of techbases to the highest of mountain teleportation testing facilities to the greasiest of restaurants to the most obligatory of sewer maps. Along the way, you'll encounter 9 new monsters, new textures, catchy music, and stuff.",
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
        "A new 32-map megawad for DOOM II, focusing on short to medium length levels, played in a series or from pistol start, in single player or cooperative. THE JOURNEY takes you across a variety of locales, challenging you along the way with insidious traps and vicious monsters. The levels are easy to understand, but don't underestimate them because of the lack of massive hordes! Close quarters and strategical monster and item placement will test your skills. The average time to...",
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
        "This is my last jokewad, as the title suggest. after this i will made only fuckin' cool things. Maybe (and i repeat MAYBE) i'll plan to made a sequel of the bleha saga, but it will a semiserious thing like in bella 2 of paul corfiatis. Oh and don't forget that mordeth thing of the community joke!!!!!!! btw, lets start with the description of this wad! This wad is based upon joke megawads like 1337.wad, mock2.wad, white0.wad, ocb.wad and awsome.wad SO EXPECT SOMENTHING OF...",
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
        "Single player DOOM II levels Fantastisches Leveldesign und Gameplay.",
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
        "Presented as an immediate sequel to Doom ][, The Long Trek Back Home sees you through the old levels in reverse order; Icon of Sin is the first and Entryway the last. Though the respective areas are familiar, great upheavals have taken place: destruction and Hellish subversion ensure that there is as much new as there is old. From the caved-in ruins of Hell to the corrupted cityscape to the former spaceport suffused in demonic magic, the road home is long... This is the...",
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
        "Die beruehmte 24-Level Buch-Kampagne von 1995. Einer der allerersten professionellen Megawads.",
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
        "A community project, each Mapper had 40 sectors (In editor, joining of sectors was allowed), 4 monster types and No SSG to work with in Doom: Doom 2 (Doom) Format.",
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
        "Its the 43rd century, year 662, month 3 day 25. The universe is a wreck and you must sort out all this mess. So you have to go on a new adventure and kill Paul Corfiatis. Once he is beaten you will be a proud champion. This Wod requires ZDOOM!!",
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
        "HI!! after about 34 months of hard work and late nights up at the computer, our levels for THE NEW DOOM! are finally finished! We have named it THE NEW DOOM because we replaced all of the OLD DOOM! We hope you like these levels, because I have had to get a thicker glasses perscription twice this year from all the late nights with no rest. Our grades have also dropped approximately two letters each class because of the work we put into this. Anyway, let's not use this whole...",
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
        "Total Conversation for Doom2, replaces all of the 32 Maps",
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
        "A Tech-styled (mostly) megawad for ZDoom. My first",
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
        "32 all new classic levels for Doom 2. The twilight zone dosen't really have a story to it. The Twilight Zone was finished in Janurary 1998 where Level 17 and 18 were Crossing Acheron and Dante's gate. But I did a new level 17 and 18 myself so Dr Sleep wouldn't get angry.",
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
        "Klassisches Doom Community Meisterwerk Fantastisches Leveldesign und Gameplay.",
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
        "More DOOM II: Fantastisches Leveldesign und Gameplay.",
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
        "At last I am done with my first and last MegaWad. After years of building. But I admit I haven't dedicated my entire life to Doom.....:) If I had I would probably had launched the wad much earlier in my dooming career. By the way I havn't introduced myself. My name is Morten Stromsvold. Also known as The Big One among friends. (TBO for shortening). Ok...enough of the chit-chat. Her is some information about The World Of Deth. The World Of Death is an original Doom 2 MegaWad....",
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
        "A vanilla-compatible mapset created for NaNoWadMo 2019. It tells the story of three people and the different challenges they face trying to survive and save each other after the research facility they're stationed at is struck by a hellish invasion...",
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
        "Ty Halderman, the essential pillar of the Doom community, passed away in 2015. He is greatly missed and his contributions to the Doom scene have helped keep this old game alive and kicking. This project tries to honor Ty with levels that pay tribute to his work be it in the form of his levels or contribution to the Boom sourceport. For more information on Ty go to this website, which has links to everything you need to know about him as well as the community's condolences:...",
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
        "TOM is a boom compatible wad that uses only doom2 format line actions (except some boom light transfers) and sector effects. -complevel 9 in PrBoom-Plus is required for some rising stairs and 'floor lower to 8 above highest floor' behaviour. Some maps don't work properly in ZDoom. This isn't intended to be a traditional map set. Instead, it's a compilation of random maps and mapping experiments (for me) with random themes that are sorted by date (mostly), rather than...",
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
        "Eight years since the Plutonia MIDI pack and twenty- I always wondered where the love for the elder siste...",
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
        "TNT: Revilution is a fan made community project that acts as a sequel to TeamTNT's Evilution. The goal of this project was to capture the spirit of what made Evilution so great, while at the same time bringing the quality of the visuals & game play up to modern standards.",
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
        "Sequel to TOONS FOR DOOM. More latter.",
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
        "You're one tough marine. You know it, and everyone that knows you knows it. Once stationed on Mars, you fought alone through the bloody cooridors of UACs science facilities and then into hell itself...just to stop an alien invasion. Next you witnessed your home planet, Earth, be overrun by the demons you fought so bravely to destroy. They wrecked havoc, they took countless lives, and they really stank up the place. The remaining citizens of Earth took to the escape ships and...",
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
        "Wie haette Ultimate Doom 1993 noch aussehen koennen? 4 Episoden voller Nostalgie und Charme.",
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
        "Ultimate Simplicity is a megawad comprised of 3 episodes, including 30 maps. It is essentially an overhauled version of the original Simplicity, with vast improvements made overall. See the NOTES section for more detailed information.",
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
        "32 new limit-removing maps created by one author (and 5 guest mappers). Gameplay-oriented. See urinfo.txt for more info.",
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
        "Uprising is a narrative driven DOOM2 megawad for limit removing ports. It features an extensive dehacked patch with changes to weapons and monsters, alongside varied maps and gameplay. You'll be fighting through wastelands, abandoned facilities, hellish installations, ruined cities, frozen techbases and beyond...",
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
        "You need Plutonia IWAD for this. Plot: Urania - a planet that suddenly appeared in the solar system. Many years ago there lived advanced civilizations, and now it is teeming with monsters. You were sent to Urania as part of the international battalion, in order to eliminate the cause of the monsters spawning from portals which terrorize the population and prevent us us excavate on the nearby planets. But now your comrades are killed and you are left alone face to face with...",
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
        "Valiant is a MBF megawad for Doom II featuring 32 new maps spanning 5 themed episodes. Each map is designed to be played from a pistol start, but some effort has been made to support continuous play as well. Additionally, Valiant includes a number of new monsters and weapons implemented through an extensive MBF format DeHackEd patch. As a result, Valiant is not compatible with gameplay mods, and can only be played in ports with full MBF DeHackEd support. At present, these...",
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
        "This edition of Valiant strips out all of the MBF DeHackEd work from the original release, which means this version is compatible with gameplay mods! If you really disliked the additional monsters and weapons in Valiant, you could also play this version as an alternative, since none of that content is present. Have fun! It's probably going to be a bit easy, though.",
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
        "Vigor is a vanilla compatible megawad with a focus on fast and frantic gameplay. Inspired by E.M's 'Gotta Go Fast,' Vigor is Scythe styled by design, a good number of the maps on display were made within a short period of time and feature tight, punchy gameplay.",
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
        "Klassisches Doom Community Meisterwerk Fantastisches Leveldesign und Gameplay.",
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
        "A collection of fast, fun old-school maps with varying difficulty levels.",
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
        "Whispers of Satan is a 30 level add-on for id software's game Doom 2: Hell on Earth requiring a Boom compatible source port.",
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
        "Megawad, based on the results of a four speedmapping contests, held on www.iddqd.ru in 2013, with such themes, as winter, thaw, precipices, and ancient civilizations. Maps were finalized some time later (some - very significantly), and several maps were built from scratch. This megawad tells us the fate of the Marine, who was going to celebrate the New Year. Suddenly, however, the monsters changed seasons' offset, and as a result - they stole the winter. We have to go...",
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
        "I just wanted all doomers to play Orginal Doom one more time. All maps are similar in design and difficulty to their originals, but they are build from scratch. I know, they simply look like originals, but they are still better than just modified ID`s levels..",
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
        "Before I say a thing, this is a jokewad for ZDoom (YAY JUST WHAT WE NEED MORE OF!) so if you do not want to play some of the worst maps available (as was the projects intention) then skip this. This is WOOO, an exhilarating experience into mapping... If it was done by monkeys. You can't just blame me for this mess, you have these guys to blame: Mappers, project manegers, 'artwork' providers, reviewers, anybody unlucky enough to post in the thread and awareness providers...",
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
        "This is the highly anticipated sequel to WOOO! (The ZDoom orientated jokewad that nobody asked for) This is a jokewad which is meant to be awful, silly and just plain weird. This time we have more than 32 maps of pure unbridled crap! We have maps that will crush your soul, maps that will make you question the world as we know it, maps that will make you fear pizza, maps that will challenge you and maps that will make you think... For all the wrong reasons! People responsible...",
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
        "'Here it FINALLY is! The EXCITING (citation needed) CONCLUSION (please god) to the CRITICALLY PANNED WOOO series! In this EXCITING (citation needed) installment you will be forced into MULTIPLE MAPS on a quest to AVENGE A CLAYMATION demon guy (or some shit) with FULLY VOICED FMV CUTSCENES and STUNNING VISUALS that PUSH THE ENGINE TO ITS LIMIT (and you) as the GRIPPING STORY continues. Only YOU can save SOMEBODY by doing SOMETHING, I'm sure of it!' WOOO 3 is... Well, tries to...",
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
        "As the successor to the ZDCMP1, the second take follows the same rules: Various mappers and modders are working on fixed dates/slots on the map and continueing where the prior person has stopped. Expect a high quality Doom experience with countless surprises and numerous things that push ZDoom to its limits.",
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
        "After the first czech community project we were decided to make something larger hance Zones of Fear were born.",
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
        "A megawad of 3 themed episodes, each of them with numerous maps fully taking the advantage of ZDoom's features!",
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

static const u8 s_doom_palette[768] = {
    0x00, 0x00, 0x00, 0x1F, 0x17, 0x0B, 0x17, 0x0F, 0x07, 0x4B, 0x4B, 0x4B,
    0xFF, 0xFF, 0xFF, 0x1B, 0x1B, 0x1B, 0x13, 0x13, 0x13, 0x0B, 0x0B, 0x0B,
    0x07, 0x07, 0x07, 0x2F, 0x37, 0x1F, 0x23, 0x2B, 0x0F, 0x17, 0x1F, 0x07,
    0x0F, 0x17, 0x00, 0x4F, 0x3B, 0x2B, 0x47, 0x33, 0x23, 0x3F, 0x2B, 0x1B,
    0xFF, 0xB7, 0xB7, 0xF7, 0xAB, 0xAB, 0xF3, 0xA3, 0xA3, 0xEB, 0x97, 0x97,
    0xE7, 0x8F, 0x8F, 0xDF, 0x87, 0x87, 0xDB, 0x7B, 0x7B, 0xD3, 0x73, 0x73,
    0xCB, 0x6B, 0x6B, 0xC7, 0x63, 0x63, 0xBF, 0x5B, 0x5B, 0xBB, 0x57, 0x57,
    0xB3, 0x4F, 0x4F, 0xAF, 0x47, 0x47, 0xA7, 0x3F, 0x3F, 0xA3, 0x3B, 0x3B,
    0x9B, 0x33, 0x33, 0x97, 0x2F, 0x2F, 0x8F, 0x2B, 0x2B, 0x8B, 0x23, 0x23,
    0x83, 0x1F, 0x1F, 0x7F, 0x1B, 0x1B, 0x77, 0x17, 0x17, 0x73, 0x13, 0x13,
    0x6B, 0x0F, 0x0F, 0x67, 0x0B, 0x0B, 0x5F, 0x07, 0x07, 0x5B, 0x07, 0x07,
    0x53, 0x07, 0x07, 0x4F, 0x00, 0x00, 0x47, 0x00, 0x00, 0x43, 0x00, 0x00,
    0xFF, 0xEB, 0xDF, 0xFF, 0xE3, 0xD3, 0xFF, 0xDB, 0xC7, 0xFF, 0xD3, 0xBB,
    0xFF, 0xCF, 0xB3, 0xFF, 0xC7, 0xA7, 0xFF, 0xBF, 0x9B, 0xFF, 0xBB, 0x93,
    0xFF, 0xB3, 0x83, 0xF7, 0xAB, 0x7B, 0xEF, 0xA3, 0x73, 0xE7, 0x9B, 0x6B,
    0xDF, 0x93, 0x63, 0xD7, 0x8B, 0x5B, 0xCF, 0x83, 0x53, 0xCB, 0x7F, 0x4F,
    0xBF, 0x7B, 0x4B, 0xB3, 0x73, 0x47, 0xAB, 0x6F, 0x43, 0xA3, 0x6B, 0x3F,
    0x9B, 0x63, 0x3B, 0x8F, 0x5F, 0x37, 0x87, 0x57, 0x33, 0x7F, 0x53, 0x2F,
    0x77, 0x4F, 0x2B, 0x6B, 0x47, 0x27, 0x5F, 0x43, 0x23, 0x53, 0x3F, 0x1F,
    0x4B, 0x37, 0x1B, 0x3F, 0x2F, 0x17, 0x33, 0x2B, 0x13, 0x2B, 0x23, 0x0F,
    0xEF, 0xEF, 0xEF, 0xE7, 0xE7, 0xE7, 0xDF, 0xDF, 0xDF, 0xDB, 0xDB, 0xDB,
    0xD3, 0xD3, 0xD3, 0xCB, 0xCB, 0xCB, 0xC7, 0xC7, 0xC7, 0xBF, 0xBF, 0xBF,
    0xB7, 0xB7, 0xB7, 0xB3, 0xB3, 0xB3, 0xAB, 0xAB, 0xAB, 0xA7, 0xA7, 0xA7,
    0x9F, 0x9F, 0x9F, 0x97, 0x97, 0x97, 0x93, 0x93, 0x93, 0x8B, 0x8B, 0x8B,
    0x83, 0x83, 0x83, 0x7F, 0x7F, 0x7F, 0x77, 0x77, 0x77, 0x6F, 0x6F, 0x6F,
    0x6B, 0x6B, 0x6B, 0x63, 0x63, 0x63, 0x5B, 0x5B, 0x5B, 0x57, 0x57, 0x57,
    0x4F, 0x4F, 0x4F, 0x47, 0x47, 0x47, 0x43, 0x43, 0x43, 0x3B, 0x3B, 0x3B,
    0x37, 0x37, 0x37, 0x2F, 0x2F, 0x2F, 0x27, 0x27, 0x27, 0x23, 0x23, 0x23,
    0x77, 0xFF, 0x6F, 0x6F, 0xEF, 0x67, 0x67, 0xDF, 0x5F, 0x5F, 0xCF, 0x57,
    0x5B, 0xBF, 0x4F, 0x53, 0xAF, 0x47, 0x4B, 0x9F, 0x3F, 0x43, 0x93, 0x37,
    0x3F, 0x83, 0x2F, 0x37, 0x73, 0x2B, 0x2F, 0x63, 0x23, 0x27, 0x53, 0x1B,
    0x1F, 0x43, 0x17, 0x17, 0x33, 0x0F, 0x13, 0x23, 0x0B, 0x0B, 0x17, 0x07,
    0xBF, 0xA7, 0x8F, 0xB7, 0x9F, 0x87, 0xAF, 0x97, 0x7F, 0xA7, 0x8F, 0x77,
    0x9F, 0x87, 0x6F, 0x9B, 0x7F, 0x6B, 0x93, 0x7B, 0x63, 0x8B, 0x73, 0x5B,
    0x83, 0x6B, 0x57, 0x7B, 0x63, 0x4F, 0x77, 0x5F, 0x4B, 0x6F, 0x57, 0x43,
    0x67, 0x53, 0x3F, 0x5F, 0x4B, 0x37, 0x57, 0x43, 0x33, 0x53, 0x3F, 0x2F,
    0x9F, 0x83, 0x63, 0x8F, 0x77, 0x53, 0x83, 0x6B, 0x4B, 0x77, 0x5F, 0x3F,
    0x67, 0x53, 0x33, 0x5B, 0x47, 0x2B, 0x4F, 0x3B, 0x23, 0x43, 0x33, 0x1B,
    0x7B, 0x7F, 0x63, 0x6F, 0x73, 0x57, 0x67, 0x6B, 0x4F, 0x5B, 0x63, 0x47,
    0x53, 0x57, 0x3B, 0x47, 0x4F, 0x33, 0x3F, 0x47, 0x2B, 0x37, 0x3F, 0x27,
    0xFE, 0xFF, 0x73, 0xFF, 0xEA, 0x43, 0xF6, 0xCE, 0x24, 0xE2, 0xA9, 0x10,
    0xCD, 0x83, 0x00, 0xAE, 0x5C, 0x00, 0x8E, 0x43, 0x00, 0x73, 0x2B, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xDB, 0xDB, 0xFF, 0xBB, 0xBB, 0xFF, 0x9B, 0x9B,
    0xFF, 0x7B, 0x7B, 0xFF, 0x5F, 0x5F, 0xFF, 0x3F, 0x3F, 0xFF, 0x1F, 0x1F,
    0xFF, 0x00, 0x00, 0xEF, 0x00, 0x00, 0xE3, 0x00, 0x00, 0xD7, 0x00, 0x00,
    0xCB, 0x00, 0x00, 0xBF, 0x00, 0x00, 0xB3, 0x00, 0x00, 0xA7, 0x00, 0x00,
    0x9B, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x73, 0x00, 0x00,
    0x67, 0x00, 0x00, 0x5B, 0x00, 0x00, 0x4F, 0x00, 0x00, 0x43, 0x00, 0x00,
    0xE7, 0xEF, 0xFF, 0xC7, 0xD9, 0xFF, 0xAB, 0xC6, 0xFF, 0x8F, 0xB2, 0xFF,
    0x73, 0x9F, 0xFF, 0x53, 0x89, 0xFF, 0x37, 0x76, 0xFF, 0x1B, 0x63, 0xFF,
    0x00, 0x51, 0xFF, 0x00, 0x48, 0xE3, 0x00, 0x40, 0xCB, 0x00, 0x39, 0xB3,
    0x00, 0x31, 0x9B, 0x00, 0x29, 0x83, 0x00, 0x22, 0x6B, 0x00, 0x1A, 0x53,
    0xFF, 0xFF, 0xFF, 0xFF, 0xEB, 0xDB, 0xFF, 0xD7, 0xBB, 0xFF, 0xC7, 0x9B,
    0xFF, 0xB3, 0x7B, 0xFF, 0xA3, 0x5B, 0xFF, 0x8F, 0x3B, 0xFF, 0x7F, 0x1B,
    0xF3, 0x73, 0x17, 0xEB, 0x6F, 0x0F, 0xDF, 0x67, 0x0F, 0xD7, 0x5F, 0x0B,
    0xCB, 0x57, 0x07, 0xC3, 0x4F, 0x00, 0xB7, 0x47, 0x00, 0xAF, 0x43, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xF6, 0xD7, 0xFF, 0xED, 0xB3, 0xFF, 0xE5, 0x8F,
    0xFF, 0xDC, 0x6B, 0xFF, 0xD4, 0x47, 0xFF, 0xCB, 0x23, 0xFF, 0xC3, 0x00,
    0xA7, 0x3F, 0x00, 0x9F, 0x37, 0x00, 0x93, 0x2F, 0x00, 0x87, 0x23, 0x00,
    0x4F, 0x3B, 0x27, 0x43, 0x2F, 0x1B, 0x37, 0x23, 0x13, 0x2F, 0x1B, 0x0B,
    0x00, 0x1A, 0x53, 0x00, 0x16, 0x47, 0x00, 0x13, 0x3B, 0x00, 0x0F, 0x2F,
    0x00, 0x0B, 0x23, 0x00, 0x07, 0x17, 0x00, 0x03, 0x0B, 0x00, 0x00, 0x00,
    0xFF, 0x9F, 0x43, 0xFF, 0xBD, 0x4B, 0xFF, 0x7B, 0xFF, 0xFF, 0x00, 0xFF,
    0xCF, 0x00, 0xCF, 0x9F, 0x00, 0x9B, 0x6F, 0x00, 0x6B, 0xA7, 0x6B, 0x6B,
};

static bool Downloader_SaveRGBToPNG(const char *filename, const u8 *rgb_data, int width, int height)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) return false;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { fclose(fp); return false; }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_write_struct(&png_ptr, NULL); fclose(fp); return false; }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    for (int y = 0; y < height; y++) {
        png_bytep row = (png_bytep)(rgb_data + y * width * 3);
        png_write_row(png_ptr, row);
    }

    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

static bool Downloader_ExtractTitlepicToPng(const char *wad_path, const char *out_png_path)
{
    FILE *f = fopen(wad_path, "rb");
    if (!f) return false;

    char hdr[12];
    if (fread(hdr, 1, 12, f) != 12) { fclose(f); return false; }

    int32_t numlumps = *(int32_t*)(hdr + 4);
    int32_t infotableofs = *(int32_t*)(hdr + 8);

    if (numlumps <= 0 || numlumps > 65536 || infotableofs <= 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, infotableofs, SEEK_SET) != 0) { fclose(f); return false; }

    int32_t target_pos = 0;
    int32_t target_size = 0;

    for (int i = 0; i < numlumps; i++) {
        uint8_t entry[16];
        if (fread(entry, 1, 16, f) != 16) break;

        char name[9] = {0};
        memcpy(name, entry + 8, 8);
        for (int c = 0; c < 8; c++) name[c] = toupper((unsigned char)name[c]);

        if (!strncmp(name, "TITLEPIC", 8)) {
            target_pos = *(int32_t*)(entry);
            target_size = *(int32_t*)(entry + 4);
            break; // Found primary title graphic!
        } else if (!strncmp(name, "INTERPIC", 8) || !strncmp(name, "DMENUPIC", 8) || !strncmp(name, "BOSSBACK", 8)) {
            if (target_pos == 0) {
                target_pos = *(int32_t*)(entry);
                target_size = *(int32_t*)(entry + 4);
            }
        }
    }

    if (target_pos <= 0 || target_size <= 8 || target_size > 2000000) {
        fclose(f);
        return false;
    }

    if (fseek(f, target_pos, SEEK_SET) != 0) { fclose(f); return false; }

    uint8_t *data = (uint8_t*)malloc(target_size);
    if (!data) { fclose(f); return false; }

    if (fread(data, 1, target_size, f) != (size_t)target_size) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);

    // Decode Doom Picture format
    int16_t width = *(int16_t*)(data);
    int16_t height = *(int16_t*)(data + 2);

    bool ok = false;
    if (width > 0 && width <= 640 && height > 0 && height <= 480 && (8 + width * 4) <= target_size) {
        uint32_t *col_ptrs = (uint32_t*)(data + 8);
        u8 *rgb = (u8*)calloc(width * height * 3, 1);
        if (rgb) {
            for (int x = 0; x < width; x++) {
                uint32_t ofs = col_ptrs[x];
                while (ofs < (uint32_t)target_size) {
                    uint8_t topdelta = data[ofs];
                    if (topdelta == 0xFF) break;
                    uint8_t length = data[ofs + 1];
                    ofs += 3;
                    for (int r = 0; r < length; r++) {
                        int y = topdelta + r;
                        if (y >= 0 && y < height && x >= 0 && x < width && (ofs + r) < (uint32_t)target_size) {
                            uint8_t pal_idx = data[ofs + r];
                            int idx = (y * width + x) * 3;
                            rgb[idx + 0] = s_doom_palette[pal_idx * 3 + 0];
                            rgb[idx + 1] = s_doom_palette[pal_idx * 3 + 1];
                            rgb[idx + 2] = s_doom_palette[pal_idx * 3 + 2];
                        }
                    }
                    ofs += length + 1;
                }
            }
            ok = Downloader_SaveRGBToPNG(out_png_path, rgb, width, height);
            free(rgb);
        }
    } else if (target_size == 64000) {
        u8 *rgb = (u8*)malloc(320 * 200 * 3);
        if (rgb) {
            for (int i = 0; i < 64000; i++) {
                uint8_t pal_idx = data[i];
                rgb[i * 3 + 0] = s_doom_palette[pal_idx * 3 + 0];
                rgb[i * 3 + 1] = s_doom_palette[pal_idx * 3 + 1];
                rgb[i * 3 + 2] = s_doom_palette[pal_idx * 3 + 2];
            }
            ok = Downloader_SaveRGBToPNG(out_png_path, rgb, 320, 200);
            free(rgb);
        }
    }

    free(data);
    return ok;
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Trinity3DS/1.0.1 (Nintendo 3DS)");
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

    // Automatically extract TITLEPIC cover from WAD if no PNG exists yet!
    char base_name[64];
    strncpy(base_name, item->target_file, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char png_path[256];
    snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);

    if (access(png_path, F_OK) != 0) {
        char target_wad_path[256];
        snprintf(target_wad_path, sizeof(target_wad_path), "sdmc:/3ds/Trinity3DS/wads/%s", item->target_file);
        Downloader_ExtractTitlepicToPng(target_wad_path, png_path);
    }

    return true;
}

static int Downloader_WordWrap(const char *text, char lines[][64], int max_lines, int max_chars)
{
    if (!text || !text[0]) return 0;
    int line_count = 0;
    const char *p = text;

    while (*p && line_count < max_lines) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        int len = strlen(p);
        if (len <= max_chars) {
            strncpy(lines[line_count], p, sizeof(lines[line_count]) - 1);
            lines[line_count][sizeof(lines[line_count]) - 1] = '\0';
            line_count++;
            break;
        }

        int break_pos = max_chars;
        while (break_pos > 0 && p[break_pos] != ' ' && p[break_pos] != '\n') {
            break_pos--;
        }
        if (break_pos == 0) {
            break_pos = max_chars;
        }

        int copy_len = break_pos;
        if (copy_len >= 64) copy_len = 63;
        strncpy(lines[line_count], p, copy_len);
        lines[line_count][copy_len] = '\0';
        line_count++;

        p += break_pos;
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    }

    return line_count;
}

static void Downloader_ShowDescriptionModal(const repo_item_t *item)
{
    if (!item) return;

    char lines[64][64];
    int line_count = Downloader_WordWrap(item->desc, lines, 64, 56);
    if (line_count == 0) {
        strcpy(lines[0], "Keine Beschreibung verfuegbar.");
        line_count = 1;
    }

    int visible_lines = 11;
    int scroll_line = 0;
    int max_scroll = (line_count > visible_lines) ? (line_count - visible_lines) : 0;
    int debounce = 10;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();
        u32 kh = hidKeysHeld();
        touchPosition touch;
        hidTouchRead(&touch);

        if (debounce > 0) {
            debounce--;
        } else {
            if (kd & (KEY_B | KEY_X | KEY_A | KEY_START)) {
                break;
            }

            if ((kd & KEY_UP) || (kh & KEY_UP)) {
                if (scroll_line > 0) scroll_line--;
            }
            if ((kd & KEY_DOWN) || (kh & KEY_DOWN)) {
                if (scroll_line < max_scroll) scroll_line++;
            }

            if (kd & KEY_TOUCH) {
                // Button Up: py: 36..80
                if (touch.py >= 36 && touch.py <= 80 && touch.px >= 20 && touch.px <= 300) {
                    if (scroll_line > 0) scroll_line--;
                }
                // Button Down: py: 130..174
                else if (touch.py >= 130 && touch.py <= 174 && touch.px >= 20 && touch.px <= 300) {
                    if (scroll_line < max_scroll) scroll_line++;
                }
                // Button Back: py: 190..235
                else if (touch.py >= 190 && touch.py <= 235 && touch.px >= 20 && touch.px <= 300) {
                    break;
                }
            }
        }

        // Top Screen: Description Text
        u8 *top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        if (top_fb) {
            WP_FillRect(top_fb, 400, 0, 0, 400, 240, 18, 20, 24);

            // Crimson Header
            WP_FillRect(top_fb, 400, 0, 0, 400, 32, 130, 20, 20);
            WP_FillRect(top_fb, 400, 0, 30, 400, 2, 210, 40, 40);
            WP_DrawText(top_fb, 400, 16, 8, item->title, 255, 255, 255, 1);
            WP_DrawText(top_fb, 400, 280, 10, "[BESCHREIBUNG]", 255, 215, 0, 1);

            // Sub-header metadata
            char sub_str[96];
            snprintf(sub_str, sizeof(sub_str), "AUTOR: %s  |  BASIS: %s  |  GROESSE: %s",
                     item->author[0] ? item->author : "Unbekannt", item->base_iwad, item->size_str);
            WP_DrawText(top_fb, 400, 16, 36, sub_str, 180, 210, 240, 1);

            // Viewport Box (Y: 50 to 208, H: 158)
            WP_FillRect(top_fb, 400, 12, 50, 376, 158, 26, 28, 34);
            WP_DrawRect(top_fb, 400, 12, 50, 376, 158, 65, 115, 180);

            for (int i = 0; i < visible_lines; i++) {
                int line_idx = scroll_line + i;
                if (line_idx < line_count) {
                    int line_y = 56 + i * 13;
                    WP_DrawText(top_fb, 400, 20, line_y, lines[line_idx], 230, 235, 245, 1);
                }
            }

            // Scrollbar
            if (max_scroll > 0) {
                int bar_total_h = 154;
                int thumb_h = (visible_lines * bar_total_h) / line_count;
                if (thumb_h < 16) thumb_h = 16;
                int thumb_y = 52 + (scroll_line * (bar_total_h - thumb_h)) / max_scroll;

                WP_FillRect(top_fb, 400, 381, 52, 5, bar_total_h, 35, 38, 45);
                WP_FillRect(top_fb, 400, 381, thumb_y, 5, thumb_h, 100, 180, 255);
            }

            // Footer
            WP_FillRect(top_fb, 400, 12, 214, 376, 20, 22, 24, 28);
            WP_DrawRect(top_fb, 400, 12, 214, 376, 20, 50, 50, 60);
            WP_DrawText(top_fb, 400, 18, 220, "D-PAD: Scrollen   [B] / [X]: Schliessen", 255, 215, 0, 1);
            if (max_scroll > 0) {
                char page_buf[32];
                snprintf(page_buf, sizeof(page_buf), "%d / %d", scroll_line + 1, max_scroll + 1);
                WP_DrawText(top_fb, 400, 340, 220, page_buf, 180, 180, 200, 1);
            }
        }

        // Bottom Screen: Touch controls
        u8 *bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (bot_fb) {
            WP_FillRect(bot_fb, 320, 0, 0, 320, 240, 16, 16, 20);

            // Header bar
            WP_FillRect(bot_fb, 320, 0, 0, 320, 28, 30, 35, 45);
            WP_DrawCenteredText(bot_fb, 320, 0, 0, 320, 28, "BESCHREIBUNG & INFOS", 255, 215, 0, 1);

            bool can_up = (scroll_line > 0);
            WP_FillRect(bot_fb, 320, 20, 36, 280, 44, can_up ? 25 : 18, can_up ? 70 : 25, can_up ? 120 : 35);
            WP_DrawRect(bot_fb, 320, 20, 36, 280, 44, can_up ? 80 : 40, can_up ? 160 : 50, can_up ? 255 : 60);
            WP_DrawCenteredText(bot_fb, 320, 20, 36, 280, 44, "[ ▲ ]  NACH OBEN SCROLLEN", can_up ? 255 : 120, can_up ? 255 : 120, can_up ? 255 : 120, 1);

            char status_str[64];
            snprintf(status_str, sizeof(status_str), "Zeile %d bis %d von %d",
                     scroll_line + 1, (scroll_line + visible_lines > line_count) ? line_count : scroll_line + visible_lines, line_count);
            WP_FillRect(bot_fb, 320, 20, 86, 280, 38, 22, 24, 30);
            WP_DrawCenteredText(bot_fb, 320, 20, 86, 280, 38, status_str, 200, 220, 255, 1);

            bool can_down = (scroll_line < max_scroll);
            WP_FillRect(bot_fb, 320, 20, 130, 280, 44, can_down ? 25 : 18, can_down ? 70 : 25, can_down ? 120 : 35);
            WP_DrawRect(bot_fb, 320, 20, 130, 280, 44, can_down ? 80 : 40, can_down ? 160 : 50, can_down ? 255 : 60);
            WP_DrawCenteredText(bot_fb, 320, 20, 130, 280, 44, "[ ▼ ]  NACH UNTEN SCROLLEN", can_down ? 255 : 120, can_down ? 255 : 120, can_down ? 255 : 120, 1);

            WP_FillRect(bot_fb, 320, 20, 188, 280, 42, 120, 30, 30);
            WP_DrawRect(bot_fb, 320, 20, 188, 280, 42, 220, 60, 60);
            WP_DrawCenteredText(bot_fb, 320, 20, 188, 280, 42, "[ B / X ]  ZURUECK ZUR LISTE", 255, 255, 255, 1);
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

static void Downloader_RenderTopScreen(u8 *fb, const repo_item_t *cur, int sel_idx, int total_count, bool installed)
{
    // Background: Dark gunmetal gray
    WP_FillRect(fb, 400, 0, 0, 400, 240, 20, 20, 22);

    // Header banner: Deep Crimson
    WP_FillRect(fb, 400, 0, 0, 400, 32, 130, 20, 20);
    WP_FillRect(fb, 400, 0, 30, 400, 2, 210, 40, 40);
    WP_DrawText(fb, 400, 16, 8, "TRINITY3DS", 255, 255, 255, 2);
    WP_DrawText(fb, 400, 280, 12, "v1.0.1 WAD STORE", 255, 215, 0, 1);

    // WiFi signal
    int wifi = osGetWifiStrength();
    int wx = 190;
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

    // Card Box (Total width: 376, Left: X=12, Right: X=388, Top: Y=40, Bottom: Y=176)
    WP_FillRect(fb, 400, 12, 40, 376, 136, 30, 32, 38);
    WP_DrawRect(fb, 400, 12, 40, 376, 136, 70, 130, 200);

    // LEFT SIDE: Metadata Info (X: 20 to 266)
    // 1. Name
    if (strlen(cur->title) <= 17) {
        WP_DrawText(fb, 400, 20, 46, cur->title, 255, 255, 255, 2);
    } else {
        WP_DrawText(fb, 400, 20, 48, cur->title, 255, 255, 255, 1);
    }

    // 2. Autor
    char author_str[64];
    snprintf(author_str, sizeof(author_str), "AUTOR: %s", cur->author[0] ? cur->author : "Unbekannt");
    WP_DrawText(fb, 400, 20, 66, author_str, 255, 215, 0, 1);

    // 3. Kategorie
    if (cur->is_iwad) {
        WP_FillRect(fb, 400, 20, 80, 125, 15, 20, 120, 40);
        WP_DrawCenteredText(fb, 400, 20, 80, 125, 15, "[STANDALONE IWAD]", 255, 255, 255, 1);
    } else {
        WP_FillRect(fb, 400, 20, 80, 140, 15, 20, 70, 130);
        WP_DrawCenteredText(fb, 400, 20, 80, 140, 15, "[COMMUNITY MOD/WAD]", 180, 230, 255, 1);
    }

    // 4. Basis
    char req_str[64];
    snprintf(req_str, sizeof(req_str), "BASIS: %s", cur->base_iwad);
    WP_DrawText(fb, 400, 20, 98, req_str, 180, 215, 245, 1);

    // 5. Groesse
    char sz_str[64];
    snprintf(sz_str, sizeof(sz_str), "GROESSE: %s", cur->size_str);
    WP_DrawText(fb, 400, 20, 112, sz_str, 200, 200, 200, 1);

    // 6. Installationsstatus
    if (installed) {
        WP_FillRect(fb, 400, 20, 128, 145, 16, 20, 105, 30);
        WP_DrawCenteredText(fb, 400, 20, 128, 145, 16, "BEREITS INSTALLIERT", 100, 255, 120, 1);
    } else {
        WP_FillRect(fb, 400, 20, 128, 145, 16, 90, 70, 15);
        WP_DrawCenteredText(fb, 400, 20, 128, 145, 16, "NICHT INSTALLIERT", 255, 215, 0, 1);
    }

    // 7. [X] BESCHREIBUNG Button & Position
    WP_FillRect(fb, 400, 20, 150, 145, 18, 40, 60, 95);
    WP_DrawRect(fb, 400, 20, 150, 145, 18, 80, 140, 220);
    WP_DrawCenteredText(fb, 400, 20, 150, 145, 18, "[X] BESCHREIBUNG", 255, 255, 255, 1);

    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "#%d/%d", sel_idx + 1, total_count);
    WP_DrawText(fb, 400, 175, 154, pos_buf, 160, 160, 180, 1);

    // RIGHT SIDE: Cover Art Box (X: 274, Y: 46, W: 106, H: 124)
    int cover_x = 274;
    int cover_y = 46;
    int cover_w = 106;
    int cover_h = 124;

    WP_FillRect(fb, 400, cover_x, cover_y, cover_w, cover_h, 16, 18, 22);
    WP_DrawRect(fb, 400, cover_x, cover_y, cover_w, cover_h, 70, 95, 130);

    WP_UpdateCoverByFilename(cur->target_file);
    if (!WP_HasCover() && installed) {
        char base_name[64];
        strncpy(base_name, cur->target_file, sizeof(base_name) - 1);
        base_name[sizeof(base_name) - 1] = '\0';
        char *dot = strrchr(base_name, '.');
        if (dot) *dot = '\0';

        char png_path[256];
        snprintf(png_path, sizeof(png_path), "sdmc:/3ds/Trinity3DS/wads/%s.png", base_name);
        char wad_path[256];
        snprintf(wad_path, sizeof(wad_path), "sdmc:/3ds/Trinity3DS/wads/%s", cur->target_file);

        if (Downloader_ExtractTitlepicToPng(wad_path, png_path)) {
            WP_UpdateCoverByFilename(cur->target_file);
        }
    }
    if (WP_HasCover()) {
        WP_DrawCover(fb, 400, cover_x + 1, cover_y + 1, cover_w - 2, cover_h - 2);
    } else {
        WP_DrawCenteredText(fb, 400, cover_x, cover_y + 40, cover_w, 18, "KEIN", 110, 120, 135, 2);
        WP_DrawCenteredText(fb, 400, cover_x, cover_y + 62, cover_w, 18, "COVER", 110, 120, 135, 2);
    }

    // Controls Help Box (Bottom)
    WP_FillRect(fb, 400, 12, 182, 376, 50, 24, 24, 28);
    WP_DrawRect(fb, 400, 12, 182, 376, 50, 70, 70, 80);
    WP_DrawText(fb, 400, 20, 189, "STEUERUNG:", 255, 215, 0, 1);
    WP_DrawText(fb, 400, 100, 189, "D-PAD: Scrollen   L / R: A-Z Sprung", 200, 200, 200, 1);
    WP_DrawText(fb, 400, 20, 209, "A: Download   X / TOUCH: Beschreibung   B: Zurueck", 100, 255, 120, 1);
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
    // Left: [A] LADEN (X: 6, Y: 202, W: 125, H: 34)
    // Middle: [X] DETAILS (X: 136, Y: 202, W: 100, H: 34)
    // Right: [B] ZURUECK (X: 242, Y: 202, W: 72, H: 34)
    bool already_inst = (count > 0 && sel_idx < count && installed_flags) ? installed_flags[sel_idx] : false;

    if (already_inst) {
        WP_FillRect(fb, 320, 6, 202, 125, 34, 30, 70, 40);
        WP_DrawRect(fb, 320, 6, 202, 125, 34, 60, 150, 70);
        WP_DrawCenteredText(fb, 320, 6, 202, 125, 34, "NEU LADEN [A]", 200, 255, 200, 1);
    } else {
        WP_FillRect(fb, 320, 6, 202, 125, 34, 20, 120, 45);
        WP_DrawRect(fb, 320, 6, 202, 125, 34, 60, 230, 90);
        WP_DrawCenteredText(fb, 320, 6, 202, 125, 34, "[A] LADEN", 255, 255, 255, 1);
    }

    // Info / Description Button
    WP_FillRect(fb, 320, 136, 202, 100, 34, 35, 55, 85);
    WP_DrawRect(fb, 320, 136, 202, 100, 34, 70, 120, 210);
    WP_DrawCenteredText(fb, 320, 136, 202, 100, 34, "[X] DETAILS", 255, 255, 255, 1);

    // Back button
    WP_FillRect(fb, 320, 242, 202, 72, 34, 30, 30, 36);
    WP_DrawRect(fb, 320, 242, 202, 72, 34, 80, 80, 95);
    WP_DrawCenteredText(fb, 320, 242, 202, 72, 34, "[B] ZURUECK", 255, 255, 255, 1);
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

            // Touch on Bottom Action Button: [A] LADEN
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 6 && touch.px <= 131) {
                kDown |= KEY_A;
            }

            // Touch on Middle Button: [X] DETAILS
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 136 && touch.px <= 236) {
                kDown |= KEY_X;
            }

            // Touch on Right Button: [B] ZURUECK
            if (touch.py >= 202 && touch.py <= 236 && touch.px >= 242 && touch.px <= 314) {
                kDown |= KEY_B;
            }
        }

        // Show Description: [X] or [SELECT]
        if ((kDown & KEY_X) || (kDown & KEY_SELECT)) {
            if (count > 0 && selected_idx < count) {
                Downloader_ShowDescriptionModal(&s_catalog[selected_idx]);
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

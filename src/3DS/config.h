/* Trinity3DS - hand-written config.h for the Nintendo 3DS (devkitARM) build
 * of dsda-doom. Replaces the CMake-generated config.h. */
#ifndef TRINITY3DS_CONFIG_H
#define TRINITY3DS_CONFIG_H

#define PROJECT_NAME    "dsda-doom"
#define PROJECT_TARNAME "dsda-doom"
#define WAD_DATA        "dsda-doom.wad"
#define PROJECT_VERSION "0.30.1"
#define PROJECT_STRING  "dsda-doom 0.30.1 (Trinity3DS)"

#define DOOMWADDIR "sdmc:/3ds/Trinity3DS/wads"
#define DSDA_ABSOLUTE_PWAD_PATH "sdmc:/3ds/Trinity3DS/dsda-doom.wad"

/* Little endian ARM11 */
#undef WORDS_BIGENDIAN

#define HAVE_UNISTD_H
#define HAVE_DIRENT_H
#define HAVE_MKSTEMP

#include <time.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#define stricmp strcasecmp
#define strnicmp strncasecmp

/* Optional libraries available in the 3DS portlibs */
#define HAVE_LIBMAD
/* HAVE_LIBVORBISFILE: TODO - port vorbisplayer to Tremor (libvorbisidec) */

#endif

#ifndef __WAD_DOWNLOADER_H__
#define __WAD_DOWNLOADER_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runs the interactive WAD Downloader UI.
// Returns true if one or more new WADs were downloaded and installed
// (indicating the WAD picker should rescan / reload its list).
bool WAD_Downloader_Run(void);

#ifdef __cplusplus
}
#endif

#endif // __WAD_DOWNLOADER_H__

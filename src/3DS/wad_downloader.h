#ifndef __WAD_DOWNLOADER_H__
#define __WAD_DOWNLOADER_H__

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char title[64];
    char author[64];
    char category[32];   // "STANDALONE", "MEGAWAD", "EPISODE"
    char base_iwad[32];  // e.g. "DOOM2.WAD", "DOOM.WAD", "NONE"
    char desc[512];
    char url[256];
    char target_file[64];// Primary wad to check for installation
    char size_str[32];
    bool is_zip;
    bool is_iwad;
} repo_item_t;

// Runs the interactive WAD Downloader UI.
// Returns true if one or more new WADs were downloaded and installed
// (indicating the WAD picker should rescan / reload its list).
bool WAD_Downloader_Run(void);

// Catalog lookup for local WADs
const repo_item_t* Downloader_FindCatalogItem(const char *filename);

// Word wrap helper
int Downloader_WordWrap(const char *text, char lines[][64], int max_lines, int max_chars);

// Interactive full-screen description modal (shared between Downloader and Picker)
void Downloader_ShowDescriptionCustom(const char *title, const char *author, const char *base_iwad, const char *size_str, const char *desc);
void Downloader_ShowDescriptionModal(const repo_item_t *item);

// Cover & metadata download functions
bool Downloader_FetchOnlineCover(const char *target_filename, const char *out_png_path);
bool Downloader_FetchMetadataForWad(const char *filename, char *out_author, size_t author_len, char *out_base, size_t base_len, char *out_desc, size_t desc_len);

#ifdef __cplusplus
}
#endif

#endif // __WAD_DOWNLOADER_H__

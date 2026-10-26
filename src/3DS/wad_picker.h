#ifndef __WAD_PICKER_H__
#define __WAD_PICKER_H__

#include <stdbool.h>
#include <stddef.h>
#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Drawing primitives shared between WAD Browser and Downloader
void WP_DrawPixel(u8 *fb, int width, int x, int y, u8 r, u8 g, u8 b);
void WP_FillRect(u8 *fb, int width, int x, int y, int w, int h, u8 r, u8 g, u8 b);
void WP_DrawRect(u8 *fb, int width, int x, int y, int w, int h, u8 r, u8 g, u8 b);
void WP_DrawText(u8 *fb, int width, int x, int y, const char *text, u8 r, u8 g, u8 b, int scale);

// Rescans SD card for IWADs and PWADs
void WP_RescanWads(void);

// Scans SD card for IWADs and PWADs, and displays an interactive DualScreen
// WAD browser if multiple WADs exist.
// Returns true if a selection was made, false to fall back to standard PrBoom search.
bool WAD_Picker_Run(char *out_iwad, size_t iwad_len, char *out_pwad, size_t pwad_len);

#ifdef __cplusplus
}
#endif

#endif // __WAD_PICKER_H__

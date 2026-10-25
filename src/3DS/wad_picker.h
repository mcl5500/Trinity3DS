#ifndef __WAD_PICKER_H__
#define __WAD_PICKER_H__

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Scans SD card for IWADs and PWADs, and displays an interactive DualScreen
// WAD browser if multiple WADs exist.
// Returns true if a selection was made, false to fall back to standard PrBoom search.
bool WAD_Picker_Run(char *out_iwad, size_t iwad_len, char *out_pwad, size_t pwad_len);

#ifdef __cplusplus
}
#endif

#endif // __WAD_PICKER_H__

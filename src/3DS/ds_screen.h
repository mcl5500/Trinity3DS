#ifndef __DS_SCREEN_H__
#define __DS_SCREEN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "doomtype.h"

extern int ds_touch_weapon;

void DS_Init(void);
void DS_UpdateBottomScreen(void);
void DS_HandleTouch(int touch_x, int touch_y);
void DS_SelectWeaponSlot(int slot);
void DS_QuickSave(void);
void DS_QuickLoad(void);

#ifdef __cplusplus
}
#endif

#endif // __DS_SCREEN_H__

#ifndef __DS_SCREEN_H__
#define __DS_SCREEN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "doomtype.h"

void DS_Init(void);
void DS_UpdateBottomScreen(void);
void DS_HandleTouch(int touch_x, int touch_y);

#ifdef __cplusplus
}
#endif

#endif // __DS_SCREEN_H__

#ifndef __I_JOY_H__
#define __I_JOY_H__

#include <3ds.h>

extern int joyxmove;
extern int joyymove;
extern int joyrxmove;
extern int joyrymove;
extern u32 ctr_keys_held;
extern u32 ctr_keys_down;

void I_InitJoystick(void);
void I_ShutdownJoystick(void);
void I_PollJoystick(void);

#endif

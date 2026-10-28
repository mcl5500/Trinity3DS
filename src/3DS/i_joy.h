#ifndef __I_JOY_H__
#define __I_JOY_H__

extern int joybup;
extern int joybdown;
extern int joybleft;
extern int joybright;
extern int joybfire;
extern int joybstrafe;
extern int joybstrafeleft;
extern int joybstraferight;
extern int joybuse;
extern int joybnextweapon;
extern int joybspeed;

void I_InitJoystick(void);
void I_PollJoystick(void);

#endif

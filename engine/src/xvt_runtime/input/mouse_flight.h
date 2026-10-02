#ifndef XVT_MOUSE_FLIGHT_H
#define XVT_MOUSE_FLIGHT_H
#include "xvt_runtime/config/mouse_config.h"
/* The captured mouse as a virtual stick. Mouse travel moves a held deflection that stays until moved
 * back: full deflection after about 256 pixels at sensitivity 5, each notch doubling or halving the gain. The
 * left button fires; holding the right button past 250 ms makes horizontal travel roll, which recenters
 * on release; a shorter right tap targets the craft under the crosshair; the middle button targets the
 * nearest fighter; the first side button toggles the cockpit. */

/* Stores options and resets; NULL is ignored. */
void XvtMouseFlight_SetOptions(const XvtMouseOptions *options);
/* Recenters the stick and drops pending motion, buttons, queued keys and the roll lock. */
void XvtMouseFlight_Reset(void);
/* Drops pending motion, queued keys and a pending tap; the stick keeps its deflection. */
void XvtMouseFlight_DiscardPending(void);
/* Accumulate once per host frame; sample only when original local input is read. */
/* Once per input frame, while mouse flight is enabled, allowed and the pointer relative: adds the
 * frame's motion to the pending motion, samples the buttons, and queues the tap and button keys unless a
 * chat is open. Otherwise resets. */
void XvtMouseFlight_Pump(void);
/* Moves the stick by the pending motion (Y reversed unless invert is set) and returns 1; the first
 * sample, or one after more than 100 ms, drops the motion but keeps the stick. Returns 0, after a reset,
 * when mouse flight is not allowed, and 0 while inactive. */
int XvtMouseFlight_Sample(void);
/* The last Sample's axes, -127 to 127; any pointer may be NULL. */
void XvtMouseFlight_GetAxes(int *yaw, int *pitch, int *roll);
/* 1 while the left button (fire) is held, else 0. */
int XvtMouseFlight_ButtonsMask(void);
/* The next queued key, or 0. While mouse flight is not allowed or a chat is open, empties the queue and
 * returns 0. A full queue of 15 keys drops the key with a warning. */
uint16_t XvtMouseFlight_ReadKey(void);
/* While mouse flight is allowed and active, writes the stick's yaw and pitch and returns 1; else 0. */
int XvtMouseFlight_GetHudMarker(int *yaw, int *pitch);
#endif

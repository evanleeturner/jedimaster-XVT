#ifndef XVT_MOUSE_FLIGHT_H
#define XVT_MOUSE_FLIGHT_H
#include "xvt_runtime/config/mouse_config.h"
void XvtMouseFlight_SetOptions(const XvtMouseOptions* options);
void XvtMouseFlight_Reset(void);
void XvtMouseFlight_DiscardPending(void);
/* Accumulate once per host frame; sample only when original local input is read. */
void XvtMouseFlight_Pump(void);
int XvtMouseFlight_Sample(void);
void XvtMouseFlight_GetAxes(int* yaw, int* pitch, int* roll);
int XvtMouseFlight_ButtonsMask(void);
uint16_t XvtMouseFlight_ReadKey(void);
int XvtMouseFlight_GetHudMarker(int* yaw, int* pitch);
#endif

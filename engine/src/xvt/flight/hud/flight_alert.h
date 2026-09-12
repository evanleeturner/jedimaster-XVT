#ifndef XVT_FLIGHT_HUD_FLIGHT_ALERT_H
#define XVT_FLIGHT_HUD_FLIGHT_ALERT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flightAlertBoxVerticalOffset;
extern void* g_flightAlertBoxSavedPixels;
extern int g_flightAlertBoxSavedBytes;

void FlightAlert_SaveBoxBackground(void);
void FlightAlert_RestoreBoxBackground(void);
void FlightAlert_DrawBox(int verticalMode, char* line1, uint8_t bgColor);

#ifdef __cplusplus
}
#endif

#endif

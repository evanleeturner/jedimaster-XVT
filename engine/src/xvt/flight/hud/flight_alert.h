#ifndef XVT_FLIGHT_HUD_FLIGHT_ALERT_H
#define XVT_FLIGHT_HUD_FLIGHT_ALERT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flight_alert_box_vertical_offset;
extern void *g_flight_alert_box_saved_pixels;
extern int g_flight_alert_box_saved_bytes;

void flight_alert_save_box_background(void);
void flight_alert_restore_box_background(void);
void flight_alert_draw_box(int text_row, char *text, uint8_t bg_color);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_FLIGHT_FLIGHT_SURFACE_H
#define XVT_FLIGHT_FLIGHT_SURFACE_H

#include "aeron/compat/ddraw.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void *g_sw_framebuffer_base;
extern int g_flight_page_flip;
extern int g_flight_draw_to_hud_layer;
extern IDirectDrawSurface *g_flight_offscreen_surface;
extern uint8_t g_flight_display_surfaces_active;
extern int32_t g_flight_net_clock_lead_ticks;
extern uint8_t g_flight_surface_already_locked;
extern int g_surface_lock_count;

void flight_surface_clear_to_black(void);
int flight_surface_get_lock_count(void);
void flight_surface_lock(void);
void flight_surface_unlock(void);
int flight_surface_set_viewport480_byte_span(int byte_span);
void *flight_surface_set_software_framebuffer_base(void *framebuffer_base);
void *flight_surface_get_software_framebuffer_base(void);

#ifdef __cplusplus
}
#endif

#endif

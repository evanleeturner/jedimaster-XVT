#ifndef XVT_FLIGHT_FLIGHT_RENDER_H
#define XVT_FLIGHT_FLIGHT_RENDER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void flight_render_transition_hook_stub(void);
void flight_render_invoke_transition_hook(int transition_flags);
void flight_render_reset_palette(int transition_flags);
void flight_render_configure_callbacks_for_resolution(
	uint8_t initial_graphics_detail_preset);
void flight_render_install_callbacks(int pixel_mode);
void flight_render_set_pixel_mode_stub(int pixel_mode);

#ifdef __cplusplus
}
#endif

#endif

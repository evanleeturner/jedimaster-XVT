#ifndef XVT_FRONTEND_MISSION_BRIEFING_H
#define XVT_FRONTEND_MISSION_BRIEFING_H

#include <stdint.h>

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

int16_t mission_briefing_handle_map_mouse_input(
	const struct RECT *viewport_rect, const struct RECT *clip_rect,
	int16_t suppress_input, int left_down, int right_down, int16_t mouse_x,
	int16_t mouse_y);
int16_t mission_briefing_draw_map_viewport(const struct RECT *viewport_rect,
					   const struct RECT *clip_rect,
					   int16_t highlight_phase);

#ifdef __cplusplus
}
#endif

#endif

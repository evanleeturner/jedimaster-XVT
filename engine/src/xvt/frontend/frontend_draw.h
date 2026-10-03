#ifndef XVT_FRONTEND_FRONTEND_DRAW_H
#define XVT_FRONTEND_FRONTEND_DRAW_H

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Active frontend draw surface and clip bounds. */
extern uint8_t *g_draw_surface_ptr;

void frontend_draw_rect_assign(struct RECT *rect, int32_t left, int32_t top,
			       int32_t right, int32_t bottom);
void frontend_draw_rect_copy(struct RECT *dst, const struct RECT *src);
void frontend_draw_rect_offset_xy(struct RECT *rect, int dx, int dy);
void frontend_draw_rect_inset_xy(struct RECT *rect, int dx, int dy);
int frontend_draw_rect_clip_to_bounds(struct RECT *rect);
void frontend_draw_fill_rect_translucent(const struct RECT *src, int dx, int dy,
					 unsigned int color);
void frontend_draw_rect(struct RECT *rect, int dx, int dy, int color,
			int filled);
void frontend_draw_rect_outline(struct RECT *rect, int dx, int dy, int color);
int frontend_draw_point_in_rect(const struct RECT *rect, int x, int y);
void frontend_draw_line(int x0, int y0, int x1, int y1, int color);
void frontend_draw_horizontal_line_clipped(int x0, int x1, int y, int color);
void frontend_draw_vertical_line_clipped(int y0, int y1, int x, int color);

#ifdef __cplusplus
}
#endif

#endif

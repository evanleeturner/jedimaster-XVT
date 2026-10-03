#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_HUD_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_HUD_H
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Main-world markers use the capture scope to exclude map/CRT projections. */
/* Reset and BeginFrame both clear the target boxes and set the draw scope to cockpit. */
void xvt_render_hud_reset(void);
void xvt_render_hud_begin_frame(void);
/* Copies the target boxes gathered since the last reset into out. */
void xvt_render_hud_publish(struct xvt_render_snapshot *out);
/* Records a target box at the world position in g_world_loc_x/y/z. Does nothing without an open
 * tick or while the draw scope is map or CRT; counts a dropped record in the writer when
 * XVT_SNAP_TARGET_BOXES boxes are held. object is kept only when it is below the slot total,
 * else stored as none; the slot is not checked for an object. */
void xvt_render_hud_target_box(unsigned object, unsigned component, int extent,
			       int color);
/* Set and read the draw scope (XVT_SCOPE_*); it holds until the next set or reset. */
void xvt_render_draw_scope(unsigned scope);
unsigned xvt_render_draw_scope_current(void);
/* Returns palette entry index & 255 as opaque ARGB, from the raw 6-bit DAC palette widened to
 * 8 bits; classic brightness and pixel format do not apply. */
uint32_t xvt_render_draw_color(unsigned index);
#ifdef __cplusplus
}
#endif
#endif

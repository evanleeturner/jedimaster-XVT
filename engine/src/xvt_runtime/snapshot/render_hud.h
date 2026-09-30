#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_HUD_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_HUD_H
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Main-world markers use the capture scope to exclude map/CRT projections. */
/* Reset and BeginFrame both clear the target boxes and set the draw scope to cockpit. */
void XvtRenderHud_Reset(void);
void XvtRenderHud_BeginFrame(void);
/* Copies the target boxes gathered since the last reset into out. */
void XvtRenderHud_Publish(XvtRenderSnapshot* out);
/* Records a target box at the world position in worldlocx/y/z. Does nothing without an open
 * tick or while the draw scope is map or CRT; counts a dropped record in the writer when
 * XVT_SNAP_TARGET_BOXES boxes are held. object is kept only when it is below the slot total,
 * else stored as none; the slot is not checked for an object. */
void XvtRenderHud_TargetBox(unsigned object, unsigned component, int extent, int color);
/* Set and read the draw scope (XVT_SCOPE_*); it holds until the next set or reset. */
void XvtRenderDraw_Scope(unsigned scope);
unsigned XvtRenderDraw_ScopeCurrent(void);
/* Returns palette entry index & 255 as opaque ARGB, from the raw 6-bit DAC palette widened to
 * 8 bits; classic brightness and pixel format do not apply. */
uint32_t XvtRenderDraw_Color(unsigned index);
#ifdef __cplusplus
}
#endif
#endif

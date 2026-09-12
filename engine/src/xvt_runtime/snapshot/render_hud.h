#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_HUD_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_HUD_H
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Main-world markers use the capture scope to exclude map/CRT projections. */
void XvtRenderHud_Reset(void);
void XvtRenderHud_BeginFrame(void);
void XvtRenderHud_Publish(XvtRenderSnapshot* out);
void XvtRenderHud_TargetBox(unsigned object, unsigned component, int extent, int color);
void XvtRenderDraw_Scope(unsigned scope);
unsigned XvtRenderDraw_ScopeCurrent(void);
uint32_t XvtRenderDraw_Color(unsigned index);
#ifdef __cplusplus
}
#endif
#endif

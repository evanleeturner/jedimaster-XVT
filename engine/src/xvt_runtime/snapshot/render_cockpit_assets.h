#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_COCKPIT_ASSETS_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_COCKPIT_ASSETS_H
#include "xvt_runtime/snapshot/render_snapshot.h"

/* Internal asset-observer state; recovered callers use render_assets.h. */
void XvtRenderCockpit_Reset(void);
void XvtRenderCockpit_Forget(uint64_t id);
void XvtRenderCockpit_CaptureDefinition(XvtCockpitDefinition* definition);
uint64_t XvtRenderAssets_RegisterCockpit(const void* owner, uint16_t handle, const char* path,
										 const XvtSnapRect* viewport);
#endif

#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_MAP_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_MAP_H
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
void XvtRenderMap_Capture(XvtSnapMap* map, const XvtSnapObject* objects, unsigned count);
#ifdef __cplusplus
}
#endif
#endif

#ifndef XVT_REMASTER_COMPONENT_ANIMATION_H
#define XVT_REMASTER_COMPONENT_ANIMATION_H
#include "xvt_remaster/assets.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif

void XvtComponentAnimation_Reset(void);
void XvtComponentAnimation_Prepare(const XvtRenderSnapshot* snapshot);
int XvtComponentAnimation_Changed(void);
uint64_t XvtComponentAnimation_ObjectRevision(unsigned slot);
const float* XvtComponentAnimation_Angles(const XvtRenderSnapshot* snapshot, const XvtSnapObject* object,
										  const XvtMeshAsset* asset, float output[XVT_SNAP_COMPONENTS]);
#ifdef __cplusplus
}
#endif

#endif

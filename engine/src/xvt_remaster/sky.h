#ifndef XVT_REMASTER_SKY_H
#define XVT_REMASTER_SKY_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtSky_Prepare(AeronCommandBuffer* cmd, AeronScene3D* scene, const XvtRenderSnapshot* snapshot,
				   const XvtRenderView* view);
void XvtSky_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif

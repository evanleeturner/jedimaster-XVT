#ifndef XVT_REMASTER_LIGHTING_H
#define XVT_REMASTER_LIGHTING_H
#include "xvt_remaster/ship.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtLighting_Begin(AeronScene3D* scene, const XvtRenderSnapshot* snapshot);
void XvtLighting_AddPoint(AeronScene3D* scene, const float position[3], const float color[3], float intensity,
						  float minimum_range);
#ifdef __cplusplus
}
#endif
#endif

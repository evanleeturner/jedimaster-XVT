#ifndef XVT_REMASTER_ENGINE_GLOWS_H
#define XVT_REMASTER_ENGINE_GLOWS_H
#include "xvt_remaster/ship.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtEngineGlows_Submit(AeronCommandBuffer* cmd, AeronScene3D* scene, const XvtRenderSnapshot* snapshot,
						  const XvtSnapObject* object, const XvtMeshAsset* asset,
						  const AeronSceneMeshTable* table, const float transform[16], int draw);
void XvtEngineGlows_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif

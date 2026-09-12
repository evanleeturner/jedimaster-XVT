#ifndef XVT_REMASTER_SHIP_H
#define XVT_REMASTER_SHIP_H
#include "xvt_remaster/assets.h"
#include "xvt_remaster/render_math.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XvtShipSelection {
	uint64_t asset_id;
	uint16_t component;
} XvtShipSelection;

int XvtRemasterShip_Select(const XvtRenderSnapshot* snapshot, const XvtSnapObject* object,
						   XvtShipSelection* out);
void XvtRemasterShip_BuildMeshTable(const XvtMeshAsset* asset, const XvtSnapObject* object,
									uint16_t component, const float* visual_angles, AeronSceneMeshTable* out);
/* Conservative articulated sphere, in scene units, around instance translation. */
float XvtRemasterShip_Radius(const XvtMeshAsset* asset, const AeronSceneMeshTable* table,
							 const float transform[16]);
int XvtRemasterShip_Visible(const XvtRenderView* view, const float transform[16], float radius);
void XvtRemasterShip_SetEnvironment(AeronScene3D* scene, const XvtSnapLighting* lighting,
									const XvtSnapCamera* eye_camera, const float camera_position[3]);
#ifdef __cplusplus
}
#endif
#endif

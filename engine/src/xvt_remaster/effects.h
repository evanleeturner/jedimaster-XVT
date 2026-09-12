#ifndef XVT_REMASTER_EFFECTS_H
#define XVT_REMASTER_EFFECTS_H
#include "aeron/scene/billboard.h"
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XvtEffectFrame {
	AeronTexture* texture;
	float u0, v0, u1, v1;
	int width, height;
} XvtEffectFrame;

int XvtEffects_Frame(const XvtRenderSnapshot* snapshot, unsigned type, unsigned frame, XvtEffectFrame* out);
void XvtEffects_Quad(const XvtSnapCamera* camera, const float center[3], float half_w, float half_h,
					 float angle, float out[4][3]);
void XvtEffects_SetFrame(AeronSceneBillboardDesc* billboard, const XvtEffectFrame* frame, float intensity,
						 float alpha);
void XvtEffects_Submit(AeronScene3D* scene, const XvtRenderSnapshot* current,
					   const XvtRenderSnapshot* previous, const XvtSnapCamera* camera,
					   const XvtSnapCamera* previous_camera, int regenerate, const XvtSnapPreview* crt);
void XvtEffects_ProjectileMatrix(const XvtSnapObject* object, const int32_t camera[3],
								 const int32_t origin[3], float out[16]);
void XvtEffects_MapObject(AeronScene3D* scene, const XvtRenderSnapshot* s, const XvtSnapObject* object);
#ifdef __cplusplus
}
#endif
#endif

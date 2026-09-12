#ifndef XVT_REMASTER_RENDER_MATH_H
#define XVT_REMASTER_RENDER_MATH_H
#include "aeron/scene/scene3d.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XvtRenderView {
	AeronSceneCamera camera;
	int32_t origin_world[3];
	float view_proj[16], classic_pixel_scale;
} XvtRenderView;

typedef struct XvtLayoutTransform {
	float scale, source_width, source_height, target_width, target_height;
} XvtLayoutTransform;

int XvtRenderMath_BuildView(const XvtSnapCamera* camera, const int32_t origin[3], int width, int height,
							XvtRenderView* out);
int XvtRenderMath_BuildMainView(const XvtSnapCamera* camera, const int32_t origin[3], int width, int height,
								XvtRenderView* out);
int XvtRenderMath_ProjectWorld(const XvtRenderView* view, const int32_t world[3], float* x, float* y,
							   float* depth);
void XvtRenderMath_ObjectMatrix(const XvtSnapObject* object, const int32_t origin[3], float out[16]);
int XvtRenderMath_PoseChanged(const XvtRenderSnapshot* current, const XvtRenderSnapshot* previous);
int XvtRenderMath_Layout(float source_width, float source_height, float target_width, float target_height,
						 XvtLayoutTransform* out);
/* Anchors 0, 0.5 and 1 align an authored layout with a target edge or center. */
void XvtRenderMath_LayoutPoint(const XvtLayoutTransform* layout, float anchor_x, float anchor_y, float x,
							   float y, float* out_x, float* out_y);
void XvtRenderMath_LayoutInverse(const XvtLayoutTransform* layout, float anchor_x, float anchor_y, float x,
								 float y, float* out_x, float* out_y);
#ifdef __cplusplus
}
#endif
#endif

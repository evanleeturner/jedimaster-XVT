#ifndef XVT_ASSETS_MODEL_PREVIEW_H
#define XVT_ASSETS_MODEL_PREVIEW_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_nodeSwitchIndex;
extern int g_modelPreviewLightDirectionX;
extern int g_modelPreviewLightDirectionY;
extern int g_modelPreviewLightDirectionZ;
extern int g_modelPreviewStateGuard;
extern int g_modelPreviewRenderResourcesInitialized;
extern OptimizedPolyObject* g_modelPreviewModelData;
extern uint16_t g_modelPreviewAuxBufferHandle;
extern unsigned int g_modelPreviewAuxBufferCapacityBytes;

struct ModelPreviewCraftPosition {
	int x;
	int y;
	int z;
};

int ModelPreview_LoadModel(const char* modelFileName);
void ModelPreview_FreeResources(void);
int ModelPreview_RenderViewport(int x, int y, int width, int height, ...);
void ModelPreview_ScaleOptNodeTree(OptNode* node, OptimizedPolyObject* opt, double scale);
void ModelPreview_UnscaleOptNodeTree(OptNode* node, OptimizedPolyObject* opt, double scale);
void ModelPreview_ScaleOptRootNodes(OptimizedPolyObject* opt, double scale);
void ModelPreview_UnscaleOptRootNodes(OptimizedPolyObject* opt, double scale);
void ModelPreview_AccumulateOptNodeBounds(OptNode* node, OptimizedPolyObject* object);
double ModelPreview_ComputeOptBoundsExtent(OptimizedPolyObject* object, int axis);
int ModelPreview_ResetViewAndRenderState(void);
void ModelPreview_SetWhiteDirectionalLight(int x, int y, int z);
void ModelPreview_SetObjectEulerDegrees(float pitchDeg, float yawDeg, float rollDeg);
void ModelPreview_SetNodeSwitchIndex(int nodeSwitchIndex);
void ModelPreview_SetObjectWorldPosition(int x, int y, int z);
void ModelPreview_SaveState(void);
void ModelPreview_RestoreState(void);
void ModelPreview_SetObjectAngleDDegrees(float angleDeg);
int ModelPreview_GetDisplayedSizeMeters(void);

#ifdef __cplusplus
}
#endif

#endif

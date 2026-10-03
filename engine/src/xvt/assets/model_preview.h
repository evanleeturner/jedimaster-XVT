#ifndef XVT_ASSETS_MODEL_PREVIEW_H
#define XVT_ASSETS_MODEL_PREVIEW_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_nodeSwitchIndex;
extern int g_worldLightDirectionX;
extern int g_worldLightDirectionY;
extern int g_worldLightDirectionZ;
extern int g_modelPreviewSkipSceneReset;
extern int g_modelPreviewRenderResourcesInitialized;
extern struct OptimizedPolyObject *g_modelPreviewModelData;
extern uint16_t g_modelPreviewAuxBufferHandle;
extern unsigned int g_modelPreviewAuxBufferCapacityBytes;

struct ModelPreviewCraftPosition {
	/* Preview world x MissionSetup_DrawCraftLoadout gives a craft type,
	 * from its row of g_modelPreviewCraftPositions. */
	int x;
	int y; /* Preview world y for the craft type. */
	int z; /* Preview world z for the craft type. */
};

int ModelPreview_LoadModel(const char *modelFileName);
void ModelPreview_FreeResources(void);
int ModelPreview_RenderViewport(int x, int y, int width, int height, ...);
void ModelPreview_ScaleOptNodeTree(struct OptNode *node,
				   struct OptimizedPolyObject *opt,
				   double scale);
void ModelPreview_UnscaleOptNodeTree(struct OptNode *node,
				     struct OptimizedPolyObject *opt,
				     double scale);
void ModelPreview_ScaleOptRootNodes(struct OptimizedPolyObject *opt,
				    double scale);
void ModelPreview_UnscaleOptRootNodes(struct OptimizedPolyObject *opt,
				      double scale);
void ModelPreview_AccumulateOptNodeBounds(struct OptNode *node,
					  struct OptimizedPolyObject *object);
double ModelPreview_ComputeOptBoundsExtent(struct OptimizedPolyObject *object,
					   int axis);
int ModelPreview_ResetViewAndRenderState(void);
void ModelPreview_SetLightDirection(int x, int y, int z);
void ModelPreview_SetObjectEulerDegrees(float pitchDeg, float yawDeg,
					float rollDeg);
void ModelPreview_SetNodeSwitchIndex(int nodeSwitchIndex);
void ModelPreview_SetObjectWorldPosition(int x, int y, int z);
void ModelPreview_SaveState(void);
void ModelPreview_RestoreState(void);
void ModelPreview_SetObjectUpAxisAngleDegrees(float angleDeg);
int ModelPreview_GetDisplayedSizeMeters(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RENDER_SW3D_H
#define XVT_RENDER_SW3D_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_sw3dSkipOddScanlines;

extern ProjVertex* g_sw3dGeneratedClipVertex;
extern ProjVertex* g_sw3dClipTop;
extern ProjVertex* g_sw3dClipBottom;
extern int g_sw3dLightSampleBlockSize;
extern int g_sw3dLightSampleBlockMask;
extern float g_sw3dLightSampleInvBlockSize;
extern float g_sw3dLightSampleBlockSizeFloat;
extern int g_sw3dLightSampleBlockShift;
extern uint32_t g_sw3dFpuControlWordScratch;
extern uint32_t g_sw3dInitializeSceneSavedFpuControl;
extern int g_sw3dLightSampleCacheSceneStampBase;
extern SceneFace g_sw3dCockpitMaskSentinelFace;
extern SceneFace* g_sw3dCurrentFace;
extern int g_sw3dCurrentScanlineY;

struct FaceTextureGradients {
	OptVector gradient0;
	OptVector gradient1;
};

extern int g_sw3dSpanFramebufferRowOffset;
extern int g_sw3dSpanShadeDitherAccum;
extern int g_sw3dSpanUQ8;
extern int g_sw3dSpanVQ8;
extern int g_sw3dSpanShadeStepQ8;
extern int g_sw3dSpanLength;
extern int g_sw3dSpanStartX;
extern int g_sw3dSpanTextureWidthShift;
extern uint8_t* g_sw3dSpanShadeTable;
extern uint8_t* g_sw3dSpanTexels;
extern int g_sw3dSpanTexelMask;
extern int g_sw3dSpanShadeQ8;
extern int g_sw3dSpanStepUQ8;
extern int g_sw3dSpanStepVQ8;

void sw3d_ProjectMeshVertices(SceneMesh* mesh);
void sw3d_ProjectMeshVerticesDistant(SceneMesh* mesh);
void sw3d_RasterizeMeshFaces(SceneMesh* mesh);
void sw3d_ScanConvertFace(SceneFace* face);
int sw3d_SetupClippedEdge(SceneMesh* mesh, SceneEdge* edge, const ProjVertex* vTop, const ProjVertex* vBot);
int sw3d_SetupEdge(SceneEdge* edge, const ProjVertex* vTop, const ProjVertex* vBot);
void sw3d_DrawVisibleFacesToSurface(void);
void sw3d_DrawTexturedSpan(int startX, int endX, float depth);
void sw3d_InsertSpan(float xLeft, float xRight, int scanY, SceneFace* face);
int sw3d_DrawTexturedShadeSpanGeneric(void);
void sw3d_BlitOccludedSpan(const uint8_t* pSrcRaster, int startX, int endX, int scanY, float depth);
void sw3d_CopySpanToFramebuffer(const uint8_t* pSrcRasterBase, int startX, int pixelCount);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RENDER_RENDER_SCENE_H
#define XVT_RENDER_RENDER_SCENE_H

#include "aeron/compat/d3d.h"
#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SceneEdge {
	int yEnd;
	int yStart;
	float x;
	float lightIntensity;
	float dxdy;
	float dLightIntensityDy;
	ProjVertex* pClipVert;
};

extern IDirectDrawSurface* g_std3DZBufferSurface;
extern const float g_renderDistantDepth;
extern const float g_renderProjectionZeroFloat;
extern const float g_renderUnitFloat;
extern const float g_renderTextureUvHalfScale;
extern const float g_invDepthProjScale;
extern const float g_renderTriangleCornerCount;
extern const float g_renderQuadCornerCount;
extern const float g_renderLightDirectionUnitScale;
extern const float g_renderDirectionalLightIntensityScale;
extern const float g_renderZeroFloat;
extern const float g_renderAmbientLightIntensity;
extern const float g_renderRoughDistanceScale;
extern const float g_renderPointLightFacingThreshold;
extern const double g_renderHalfDouble;
extern const float g_renderHalfFloat;
extern const float g_renderSpecularApproxOtherComponentsScale;
extern const float g_renderSpecularApproxMaxComponentScale;
extern int g_capVertexAlpha;
extern int g_d3dVertexAlphaStateResetSlot;
extern int g_maxBatchTris;
extern D3DTLVERTEX* g_flightVertexBuffer;
extern float g_flightVpOriginY;
extern Std3DRenderTri* g_triBuffer;
extern int g_clipInputProjVertEndIndex;
extern int g_maxBatchVerts;
extern int g_d3dVertexCount;
extern int g_d3dIndexCount;
extern float g_flightVpOriginX;
extern uint16_t g_sceneSpanDataHandle;
extern uint16_t g_sceneSpanPtrListHandle;
extern uint16_t g_scenePhongDataHandle;
extern uint16_t g_visFaceListHandle;
extern uint16_t g_projVertListHandle;
extern ProjVertex* g_projVertList;
extern int g_projVertCount;
extern uint16_t g_sceneEdgeListHandle;
extern SceneEdge* g_sceneEdgeList;
extern int g_sceneEdgeCursor;
extern uint16_t g_vertexRemapHandle;
extern uint16_t g_sceneEdgeFlagsHandle;
extern int* g_sceneEdgeFlags;
extern int g_vertexRemapCapacity;
extern int g_sceneEdgeFlagsCapacity;
extern uint16_t g_sceneSclEdgeListHandle;
extern uint16_t g_scanlineSpanHeadsHandle;
extern SceneSpan** g_scanlineSpanHeads;
extern uint16_t g_meshQueueHandle;
extern int g_sceneSpanDataCapacity;
extern SceneSpan* g_pSceneSpanDataCur;
extern SceneSpan* g_pSceneSpanDataEnd;
extern SceneSpan** g_sceneSpanPtrList;
extern int g_sceneSpanPtrAvail;
extern SceneFace* g_visFaceList;
extern int g_renderSceneResetPending;
extern int g_visFaceCount;
extern int g_visFacePassStart;
extern int* g_vertexRemap;
extern OptVector g_meshEyePos;
extern uint8_t* g_activeRgb565ToPaletteIndexLut;
extern uint8_t g_defaultWhiteTextureRgb24[8 * 8 * 3];

static inline unsigned int RenderScene_GetMemoryHandle(const uint16_t* handle) { return *handle; }

struct ProjVertex {
	float sx;
	float sy;
	float w;
	float lightIntensity;
	float tu;
	float tv;
};

struct SceneMesh {
	ObjectRecord* pObject;
	float rotAngle;
	float viewPosX;
	float viewPosY;
	float viewPosZ;
	float viewOrient[9];
	float posX;
	float posY;
	float posZ;
	float orient[9];
	int nodeFlags[4]; ///< Elements 0-2 come from the OPT_TYPE_19 payload; element 3 receives g_curMeshFlags
					  ///< from OPT_TYPE_10 for selectors outside 5-8.
	int vertexCount;
	OptVector* pModelVerts;
	OptTexCoord* pUVs;
	OptVector* pVertNormals;
	int nodeType10Flags78; ///< Receives g_curMeshFlags when OPT_TYPE_10 param1 is 7 or 8; no downstream
						   ///< consumer is identified.
	int faceCount;
	int edgeCount;
	OptVector* pFaceNormals;
	FaceTextureGradients* pFaceTexturing;
	int nodeType10Flags56; ///< Receives g_curMeshFlags when OPT_TYPE_10 param1 is 5 or 6; no downstream
						   ///< consumer is identified.
	FaceRecord* pFaceGeom;
	char* pTextureName;
	void* pMaterial;
	void* pTexels;
	void* pPalette;
	uint16_t* pColorKeyPalette;
	int faceBaseIndex;
	int vertBaseIndex;
	int edgeBaseIndex;
	int visFaceCount;
	int projVertCursor;
	int clippedEdgeCount;
};

struct SceneFace {
	int faceIndex;
	SceneMesh* pMesh;
	int faceAndLayerId;
	int nearClipState;
	float gradients[9];
	float spanLightIntensityDx;
	SceneEdge* pScanEdge;
	void* pPhongData;
	int yTop;
	int yBot;
	float maxVertW;
	float minVertW;
	SceneEdge* edges[5];
	int edgeCount;
	SceneSpan** pSpans;
	int mipLevel;
};

struct SceneSpan {
	SceneSpan* next;
	int xStart;
	int xEnd;
	float lightIntensity;
	float dLightIntensityDx;
	SceneFace* face;
};

static __inline float RenderScene_AddTranslation(float position, float translation) {
	return position + translation;
}

void RenderScene_ProjectMeshVertices(SceneMesh* mesh);
void RenderScene_ProjectDistantMeshVertices(SceneMesh* mesh);
void RenderScene_DrawMeshFaces(const SceneMesh* mesh);
void RenderScene_DrawMesh(const SceneMesh* mesh);
void RenderScene_InitHardwareFrame(void);
extern int g_sceneFlushDrawTargetMarkers;
void RenderScene_FlushGeometry(void);
int RenderScene_EmitFlightVertex(int vertexIndex, const RenderClipVertex* vertices, const SceneFace* face);
void nullsub_2(void);
void std3D_FillZBufferFromViewportMask(void);
int RenderScene_ClearFrameBuffers(void);
void std3D_DetachAndReleaseZBufferSurface(void);
void RenderScene_ComputeVertexLighting(SceneMesh* mesh, ProjVertex* outVert, const OptVector* normal,
									   const OptVector* pos, const OptVector* eyePos);
void RenderScene_TransformFaceTextureGradients(SceneFace* face, const FaceTextureGradients* faceTexGradients,
											   const float* viewPosAndOrient);
void RenderScene_TransformProjectLegacyPoint(float outProjected[3], const float point[3],
											 const float viewPosAndOrient[12]);
void RenderScene_TransformProjectLegacyDistantPoint(float outProjected[3], const float point[3],
													const float viewPosAndOrient[12]);
void RenderScene_CullMeshFacesFromView(SceneMesh* mesh);
void RenderScene_DrawSceneMesh(SceneMesh* mesh);
void RenderScene_ApplyBwingBridgeRotation(OptimizedPolyObject* unusedModel, ObjectRecord* obj,
										  SceneMesh* mesh, int bridgeMeshIndex);
void RenderScene_DrawObjectModel(ObjectRecord* obj);
void RenderScene_DrawNoAssetSourceModel(ObjectRecord* obj, int nodeSwitchIndex);
void RenderScene_DrawModelNode(OptimizedPolyObject* object, OptNode* node, SceneMesh* mesh);
void RenderScene_ToggleVertexLightOcclusion(void);
int RenderScene_GetVertexLightOcclusionEnabled(void);
int RenderScene_IsSegmentOccludedByObjectModel(ObjectRecord* object, const OptVector* segmentStart,
											   const OptVector* segmentEnd);
int RenderScene_TestSegmentAgainstModelNode(OptimizedPolyObject* model, OptNode* node, SceneMesh* mesh,
											const OptVector* segmentStart, const OptVector* segmentEnd);
int RenderScene_TestSegmentAgainstMeshFaces(const SceneMesh* mesh, const OptVector* segmentStart,
											const OptVector* segmentEnd);
void RenderScene_AllocateBuffers(void);
void RenderScene_Initialize(int resetSceneState);
int RenderScene_UnlockBuffers(void);
void RenderScene_FreeBuffers(void);

#ifdef __cplusplus
}
#endif

#endif

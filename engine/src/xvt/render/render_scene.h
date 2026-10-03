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

/* One polygon edge as the software renderer scans it, set up by
 * sw3d_SetupClippedEdge; rows are viewport rows. */
struct SceneEdge {
	/* Row after the edge's last, capped at g_flightVpHeight. */
	int yEnd;
	/* First row the edge covers: its top y rounded up, 0 when above the
	 * viewport. */
	int yStart;
	float x;		 /* Edge's x on row yStart. */
	float lightIntensity;	 /* Light level on row yStart. */
	float dxdy;		 /* Change in x per row. */
	float dLightIntensityDy; /* Change in light level per row. */
	/* The vertex sw3d_SetupClippedEdge made where the edge crosses view
	 * depth 1, set by sw3d_RasterizeMeshFaces; NULL when it made none. */
	ProjVertex *pClipVert;
};

extern IDirectDrawSurface *g_std3DZBufferSurface;
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
extern D3DTLVERTEX *g_flightVertexBuffer;
extern float g_flightVpOriginY;
extern Std3DRenderTri *g_triBuffer;
extern int g_clipInputProjVertEndIndex;
extern int g_maxBatchVerts;
extern int g_d3dVertexCount;
extern int g_d3dTriangleCount;
extern float g_flightVpOriginX;
extern uint16_t g_sceneSpanDataHandle;
extern uint16_t g_sceneSpanPtrListHandle;
extern uint16_t g_sceneLightSampleDataHandle;
extern uint16_t g_visFaceListHandle;
extern uint16_t g_projVertListHandle;
extern ProjVertex *g_projVertList;
extern int g_projVertCount;
extern uint16_t g_sceneEdgeListHandle;
extern SceneEdge *g_sceneEdgeList;
extern int g_sceneEdgeCursor;
extern uint16_t g_vertexRemapHandle;
extern uint16_t g_sceneEdgeFlagsHandle;
extern int *g_sceneEdgeFlags;
extern int g_vertexRemapCapacity;
extern int g_sceneEdgeFlagsCapacity;
extern uint16_t g_sceneSclEdgeListHandle;
extern uint16_t g_scanlineSpanHeadsHandle;
extern SceneSpan **g_scanlineSpanHeads;
extern uint16_t g_meshQueueHandle;
extern int g_sceneSpanDataCapacity;
extern SceneSpan *g_pSceneSpanDataCur;
extern SceneSpan *g_pSceneSpanDataEnd;
extern SceneSpan **g_sceneSpanPtrList;
extern int g_sceneSpanPtrAvail;
extern SceneFace *g_visFaceList;
extern int g_renderSceneResetPending;
extern int g_visFaceCount;
extern int g_visFacePassStart;
extern int *g_vertexRemap;
extern OptVector g_meshEyePos;
extern uint8_t *g_activeRgb565ToPaletteIndexLut;
extern uint8_t g_defaultWhiteTextureRgb24[8 * 8 * 3];

static inline unsigned int RenderScene_GetMemoryHandle(const uint16_t *handle)
{
	return *handle;
}

/* One mesh vertex carried into the viewport, as the projection functions
 * write it to g_projVertList; RenderClipVertex has the same layout. */
struct ProjVertex {
	/* Viewport x in pixels; view-space x while scaledInverseDepth is
	 * negative. */
	float sx;
	/* Viewport y in pixels; view-space y while that is negative. */
	float sy;
	/* g_projScaleInt over the view depth; for a vertex closer than depth 1,
	 * the depth minus 1, which is negative. */
	float scaledInverseDepth;
	/* Light level, 0 to 1, from RenderScene_ComputeVertexLighting. */
	float lightIntensity;
	float tu; /* Horizontal texture coordinate. */
	float tv; /* Vertical texture coordinate. */
};

/* One mesh of an object's model on its way to the screen: the walk of the
 * model's nodes fills it, and the draw functions queue a copy. */
struct SceneMesh {
	ObjectRecord *pObject; /* Object the model belongs to. */
	/* Radians the next rotate-and-scale node turns this part by, from the
	 * craft's meshRotation byte; 0 for none. */
	float rotAngle;
	/* Position of the model's origin in view space; with viewOrient,
	 * Math3D_RotateVec3 then adding this carries a model point to view
	 * space. */
	float viewPosX;
	float viewPosY; /* y of that position. */
	float viewPosZ; /* z of that position: depth ahead of the eye. */
	/* Model-to-view rotation, scaled by scale nodes. */
	float viewOrient[9];
	/* Eye position in model space, used to cull faces turned away. */
	float eyeModelSpaceX;
	float eyeModelSpaceY; /* y of that position. */
	float eyeModelSpaceZ; /* z of that position. */
	/* View-to-model rotation, the inverse of viewOrient. */
	float viewToModelOrient[9];
	/* Nothing reads it. */
	int baseColorAndMaterials
		[4]; ///< Elements 0-2 come from the OPT_BASE_COLOR payload; element 3 receives
	///< g_curMeshMaterials from OPT_MATERIAL_BINDING for selectors outside 5-8.
	int vertexCount;	/* Vertices in pModelVerts. */
	OptVector *pModelVerts; /* Vertex positions in model space. */
	OptTexCoord *pUVs;	/* Texture coordinates, by face uv index. */
	/* Vertex normals: the model's own, else those that follow the face
	 * data. */
	OptVector *pVertNormals;
	/* Nothing reads it. */
	int perVertexMaterials; ///< Receives g_curMeshMaterials when OPT_MATERIAL_BINDING payloadCount is 7 or 8;
				///< no downstream consumer is identified.
	int faceCount;		/* Faces in pFaceGeom. */
	int edgeCount;		/* Edges the faces share. */
	OptVector *pFaceNormals;	      /* One normal per face. */
	FaceTextureGradients *pFaceTexturing; /* Texture axes per face. */
	/* Nothing reads it. */
	int perFaceMaterials; ///< Receives g_curMeshMaterials when OPT_MATERIAL_BINDING payloadCount is 5 or 6;
			      ///< no downstream consumer is identified.
	/* Corner, uv, normal and edge indices per face. */
	FaceRecord *pFaceGeom;
	/* Name of the texture node; RenderScene_DrawMeshFaces sets its first
	 * character to '_' when no color-key texture could be made for it. */
	char *pTextureName;
	void *pMaterial; /* The texture's OptTextureData header. */
	void *pTexels;	 /* Texels, just after that header. */
	void *pPalette;	 /* The texture's palette and shade tables. */
	/* pPalette plus 4096 bytes, read as 16-bit colors. */
	uint16_t *pColorKeyPalette;
	/* Index of the mesh's first face in g_visFaceList. */
	int faceBaseIndex;
	/* Index of its first vertex in g_projVertList. */
	int vertBaseIndex;
	/* g_sceneEdgeCursor when sw3d_RasterizeMeshFaces began; nothing reads
	 * it. */
	int edgeBaseIndex;
	int visFaceCount; /* Faces that passed the cull, from faceBaseIndex. */
	/* Vertices projected so far, from vertBaseIndex, near-clip vertices
	 * of the software renderer included. */
	int projVertCursor;
	/* Edges sw3d_RasterizeMeshFaces has written for the mesh. */
	int emittedEdgeCount;
};

/* One face that passed the cull, in g_visFaceList. */
struct SceneFace {
	int faceIndex;	  /* Index of the face in its mesh. */
	SceneMesh *pMesh; /* The queued mesh it belongs to. */
	/* faceIndex plus g_curLayerId << 16; nothing reads it. */
	int faceAndLayerId;
	/* -1 when a corner is closer than view depth 1 and the face needs the
	 * near clip; the draw then sets it to g_flightVpHeight. */
	int nearClipState;
	/* After projection, three planes in viewport x and y, each as the x
	 * factor, the y factor and the constant: u over view depth, v over
	 * view depth, and 1 over view depth. Before that, the face's u and v
	 * axes in view space and its texture origin. */
	float gradients[9];
	float spanLightIntensityDx; /* Change in light level per pixel. */
	/* The left edge sw3d_ScanConvertFace is filling spans from; NULL from
	 * the cull until then. */
	SceneEdge *pScanEdge;
	/* The face's row of 12-byte light samples in g_sceneLightSampleData. */
	void *pLightSamples;
	int yTop; /* First row of the face's spans. */
	int yBot; /* Row after its last. */
	/* Largest scaledInverseDepth of its corners; a corner closer than depth
	 * 1 counts as g_projScaleInt. */
	float maxScaledInverseDepth;
	float minScaledInverseDepth; /* Smallest, counted the same way. */
	/* Its edges as sw3d_RasterizeMeshFaces set them. */
	SceneEdge *edges[5];
	int edgeCount; /* Edges in edges. */
	/* One span pointer per row from yTop, taken from g_sceneSpanPtrList. */
	SceneSpan **pSpans;
	/* Estimated texels per viewport pixel, times 256, for choosing a mip
	 * level. */
	int texelsPerPixelQ8;
};

/* One run of pixels on a viewport row that a single face covers, in the
 * software renderer's per-row lists. */
struct SceneSpan {
	SceneSpan *next;	 /* Next span on the row; NULL at the end. */
	int xStart;		 /* First pixel. */
	int xEnd;		 /* Pixel after the last. */
	float lightIntensity;	 /* Light level at xStart. */
	float dLightIntensityDx; /* Change in light level per pixel. */
	/* The face drawn there; g_sw3dCockpitMaskSentinelFace for a run the
	 * cockpit covers. */
	SceneFace *face;
};

static __inline float RenderScene_AddTranslation(float position,
						 float translation)
{
	return position + translation;
}

void RenderScene_ProjectMeshVertices(SceneMesh *mesh);
void RenderScene_ProjectDistantMeshVertices(SceneMesh *mesh);
void RenderScene_DrawMeshFaces(const SceneMesh *mesh);
void RenderScene_DrawMeshHardware(const SceneMesh *mesh);
void RenderScene_InitHardwareFrame(void);
extern int g_sceneFlushDrawTargetMarkers;
void RenderScene_FlushGeometry(void);
int RenderScene_EmitFlightVertex(int vertexIndex,
				 const RenderClipVertex *vertices,
				 const SceneFace *face);
void nullsub_2(void);
void std3D_FillZBufferFromViewportMask(void);
int RenderScene_ClearFrameBuffers(void);
void std3D_DetachAndReleaseZBufferSurface(void);
void RenderScene_ComputeVertexLighting(SceneMesh *mesh, ProjVertex *outVert,
				       const OptVector *normal,
				       const OptVector *pos,
				       const OptVector *eyePos);
void RenderScene_TransformFaceTextureGradients(
	SceneFace *face, const FaceTextureGradients *faceTexGradients,
	const float *viewPosAndOrient);
void RenderScene_TransformProjectLegacyPoint(float outProjected[3],
					     const float point[3],
					     const float viewPosAndOrient[12]);
void RenderScene_TransformProjectLegacyDistantPoint(
	float outProjected[3], const float point[3],
	const float viewPosAndOrient[12]);
void RenderScene_CullMeshFacesFromView(SceneMesh *mesh);
void RenderScene_DrawSceneMesh(SceneMesh *mesh);
void RenderScene_ApplyBwingBridgeRotation(OptimizedPolyObject *unusedModel,
					  ObjectRecord *obj, SceneMesh *mesh,
					  int bridgeMeshIndex);
void RenderScene_DrawObjectModel(ObjectRecord *obj);
void RenderScene_DrawSelectedRootNode(ObjectRecord *obj, int rootNodeIndex);
void RenderScene_DrawModelNode(OptimizedPolyObject *model, OptNode *node,
			       SceneMesh *mesh);
void RenderScene_ToggleVertexLightOcclusion(void);
int RenderScene_GetVertexLightOcclusionEnabled(void);
int RenderScene_IsSegmentOccludedByObjectModel(ObjectRecord *object,
					       const OptVector *segmentStart,
					       const OptVector *segmentEnd);
int RenderScene_TestSegmentAgainstModelNode(OptimizedPolyObject *model,
					    OptNode *node, SceneMesh *mesh,
					    const OptVector *segmentStart,
					    const OptVector *segmentEnd);
int RenderScene_TestSegmentAgainstMeshFaces(const SceneMesh *mesh,
					    const OptVector *segmentStart,
					    const OptVector *segmentEnd);
void RenderScene_AllocateBuffers(void);
void RenderScene_Initialize(int resetSceneState);
int RenderScene_UnlockBuffers(void);
void RenderScene_FreeBuffers(void);

#ifdef __cplusplus
}
#endif

#endif

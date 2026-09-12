#include "xvt/render/sw3d.h"

#include "xvt/flight/flight_surface.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_clip.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"

#include <string.h>

typedef struct SoftwareLightSample {
	int stamp;
	float intensity;
	float rowDelta;
} SoftwareLightSample;

// GLOBAL: XVT 0x612284
int g_sw3dLightSampleBlockMask = 0;
// GLOBAL: XVT 0x612280
SceneFace* g_sw3dCurrentFace = NULL;
// GLOBAL: XVT 0x612288
int g_sw3dSpanShadeDitherAccum = 0;
// GLOBAL: XVT 0x61228C
int g_sw3dLightSampleBlockSize = 0;
// GLOBAL: XVT 0x612294
int g_sw3dSpanFramebufferRowOffset = 0;
// GLOBAL: XVT 0x61229C
float g_sw3dLightSampleSubrowLerpT = 0.0f;
// GLOBAL: XVT 0x612298
uint32_t g_sw3dFpuControlWordScratch = 0;
// GLOBAL: XVT 0x6122A0
uint32_t g_sw3dInitializeSceneSavedFpuControl = 0;
// GLOBAL: XVT 0x6122B0
float g_sw3dLightSampleRowsToNextBlockFloat = 0.0f;
// GLOBAL: XVT 0x6122B4
float g_sw3dLightSampleSubrowFloat = 0.0f;
// GLOBAL: XVT 0x6122A8
int g_sw3dSpanUQ8 = 0;
// GLOBAL: XVT 0x6122AC
int g_sw3dSpanVQ8 = 0;
// GLOBAL: XVT 0x6122B8
float g_sw3dLightSampleInvBlockSize = 0.0f;
// GLOBAL: XVT 0x6122C4
int g_sw3dSpanShadeStepQ8 = 0;
// GLOBAL: XVT 0x6122C8
int g_sw3dCurrentScanlineY = 0;
// GLOBAL: XVT 0x6122CC
int g_sw3dSpanLength = 0;
// GLOBAL: XVT 0x6122D0
int g_sw3dSpanStartX = 0;
// GLOBAL: XVT 0x6122D4
int g_sw3dCurrentLightSampleCacheStamp = 0;
// GLOBAL: XVT 0x612B58
SceneMesh* g_sw3dSpanSceneMesh = NULL;
// GLOBAL: XVT 0x612B5C
float g_sw3dSpanTextureWidthFloat = 0.0f;
// GLOBAL: XVT 0x612B60
float g_sw3dSpanTextureHeightFloat = 0.0f;
// GLOBAL: XVT 0x612B64
int g_sw3dSpanTextureWidthShift = 0;
// GLOBAL: XVT 0x612B68
int g_sw3dSpanTextureHeightShift = 0;
// GLOBAL: XVT 0x612B6C
uint8_t* g_sw3dSpanShadeTable = 0;
// GLOBAL: XVT 0x612B70
uint8_t* g_sw3dSpanTexels = 0;
// GLOBAL: XVT 0x612B74
int g_sw3dSpanTexelMask = 0;
// GLOBAL: XVT 0x612B80
int g_sw3dSpanShadeQ8 = 0;
// GLOBAL: XVT 0x612B84
int g_sw3dSpanStepUQ8 = 0;
// GLOBAL: XVT 0x612B88
int g_sw3dSpanStepVQ8 = 0;
// GLOBAL: XVT 0x612ADC
int g_sw3dLightSampleCacheSceneStampBase = 0;
// GLOBAL: XVT 0x612B50
float g_sw3dLightSampleBlockSizeFloat = 0.0f;
// GLOBAL: XVT 0x612B7C
int g_sw3dLightSampleBlockShift = 0;
// GLOBAL: XVT 0x612AE0
SceneFace g_sw3dCockpitMaskSentinelFace = { 0 };
// GLOBAL: XVT 0x523404
int g_sw3dSkipOddScanlines = 0;
// GLOBAL: XVT 0x527370
int g_sw3dShadeDitherInitialByScanlineParity[2] = { 0, 128 };

// GLOBAL: XVT 0x527378
const int g_sw3dTextureShiftBySizeDiv16[65] = {
	3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,  9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10,
};

// GLOBAL: XVT 0x527480
const float g_sw3dSpanOneFloat = 1.0f;
// GLOBAL: XVT 0x527484
const float g_sw3dLightIntensityToShadeScale = 15.0f;
// GLOBAL: XVT 0x527488
const float g_sw3dFloatToIntRoundBias = 12582912.0f;
// GLOBAL: XVT 0x5274A8
const float g_sw3dTexCoordBiasByShift[12] = {
	49152.0f, 24576.0f, 12288.0f, 6144.0f, 3072.0f, 1536.0f, 768.0f, 384.0f, 192.0f, 96.0f, 48.0f, 24.0f,
};

// GLOBAL: XVT 0x60F1C4
ProjVertex* g_sw3dGeneratedClipVertex = NULL;
// GLOBAL: XVT 0x60F1D0
ProjVertex* g_sw3dClipTop = NULL;
// GLOBAL: XVT 0x60F1E0
ProjVertex* g_sw3dClipBottom = NULL;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x470300
void sw3d_ProjectMeshVertices(SceneMesh* mesh) {
	SceneFace* face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertex* output;
	int vertexIndex;
	int faceIndex;

	mesh->vertBaseIndex = g_projVertCount;
	output = &g_projVertList[mesh->vertBaseIndex];
	mesh->projVertCursor = 0;
	for (vertexIndex = 0; vertexIndex < mesh->vertexCount; ++vertexIndex) {
		g_vertexRemap[vertexIndex] = -1;
	}
	for (faceIndex = 0; faceIndex < mesh->visFaceCount; ++faceIndex, ++face) {
		OptVector transformed;
		const FaceRecord* geometry;
		float totalW;
		float c00;
		float c01;
		float c02;
		float c10;
		float c11;
		float c12;
		float c20;
		float c21;
		float c22;
		float inverse;
		float scaled;
		float area;
		int cornerIndex;

		RenderScene_TransformFaceTextureGradients(face, &mesh->pFaceTexturing[face->faceIndex],
												  &mesh->viewPosX);
		geometry = &mesh->pFaceGeom[face->faceIndex];
		face->maxVertW = 0.0f;
		totalW = 0.0f;
		face->minVertW = (float)(unsigned int)g_projScaleInt;
		for (cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
			const int modelVertexIndex = geometry->vertexIdx[cornerIndex];
			const int normalIndex = geometry->normalIdx[cornerIndex];
			int remappedVertex;
			float vertexW;

			if (modelVertexIndex == -1) {
				break;
			}
			remappedVertex = g_vertexRemap[modelVertexIndex];
			if (remappedVertex == -1) {
				g_vertexRemap[modelVertexIndex] = mesh->projVertCursor;
				++mesh->projVertCursor;
				transformed.x = mesh->pModelVerts[modelVertexIndex].x;
				transformed.y = mesh->pModelVerts[modelVertexIndex].y;
				transformed.z = mesh->pModelVerts[modelVertexIndex].z;
				Math3D_RotateVec3(&transformed.x, mesh->viewOrient);
				transformed.x += mesh->viewPosX;
				transformed.y += mesh->viewPosY;
				transformed.z += mesh->viewPosZ;
				if (transformed.z < g_sw3dUnitFloat) {
					output->w = transformed.z - g_sw3dUnitFloat;
					output->sx = transformed.x;
					output->sy = transformed.y;
					face->nearClipState = -1;
					vertexW = (float)(unsigned int)g_projScaleInt;
				} else {
					output->w = (float)(unsigned int)g_projScaleInt / transformed.z;
					output->sx = output->w * transformed.x;
					output->sy = output->w * transformed.y;
					output->sx += (float)(g_flightVpWidth >> 1);
					output->sy += (float)(g_projOffsetY + (g_flightVpHeight >> 1));
					vertexW = output->w;
				}
				RenderScene_ComputeVertexLighting(mesh, output, &mesh->pVertNormals[normalIndex],
												  &mesh->pModelVerts[modelVertexIndex], &g_meshEyePos);
				++output;
			} else {
				const ProjVertex* projected = &g_projVertList[mesh->vertBaseIndex + remappedVertex];
				if (projected->w < 0.0f) {
					face->nearClipState = -1;
					vertexW = (float)(unsigned int)g_projScaleInt;
				} else {
					vertexW = projected->w;
				}
			}
			totalW += vertexW;
			if (face->maxVertW < vertexW)
				face->maxVertW = vertexW;
			if (face->minVertW > vertexW)
				face->minVertW = vertexW;
		}

		if (mesh->pUVs != NULL) {
			const int uvIndex = geometry->uvIdx[0];
			const int modelVertexIndex = geometry->vertexIdx[0];

			transformed.x = mesh->pModelVerts[modelVertexIndex].x;
			transformed.y = mesh->pModelVerts[modelVertexIndex].y;
			transformed.z = mesh->pModelVerts[modelVertexIndex].z;
			Math3D_RotateVec3(&transformed.x, mesh->viewOrient);
			transformed.x += mesh->viewPosX;
			transformed.y += mesh->viewPosY;
			transformed.z += mesh->viewPosZ;
			face->gradients[6] = transformed.x - mesh->pUVs[uvIndex].v * face->gradients[3] -
								 mesh->pUVs[uvIndex].u * face->gradients[0];
			face->gradients[7] = transformed.y - mesh->pUVs[uvIndex].v * face->gradients[4] -
								 mesh->pUVs[uvIndex].u * face->gradients[1];
			face->gradients[8] = transformed.z - mesh->pUVs[uvIndex].v * face->gradients[5] -
								 mesh->pUVs[uvIndex].u * face->gradients[2];
			c00 = face->gradients[8] * face->gradients[4] - face->gradients[5] * face->gradients[7];
			c01 = face->gradients[5] * face->gradients[6] - face->gradients[8] * face->gradients[3];
			c02 = face->gradients[7] * face->gradients[3] - face->gradients[4] * face->gradients[6];
			c10 = face->gradients[2] * face->gradients[7] - face->gradients[8] * face->gradients[1];
			c11 = face->gradients[8] * face->gradients[0] - face->gradients[2] * face->gradients[6];
			c12 = face->gradients[6] * face->gradients[1] - face->gradients[7] * face->gradients[0];
			c20 = face->gradients[5] * face->gradients[1] - face->gradients[2] * face->gradients[4];
			c21 = face->gradients[2] * face->gradients[3] - face->gradients[5] * face->gradients[0];
			c22 = face->gradients[4] * face->gradients[0] - face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f)
				c22 = 1.0f;
			inverse = g_sw3dUnitFloat /
					  (c21 * face->gradients[7] + (c20 * face->gradients[6] + c22 * face->gradients[8]));
			scaled = inverse * g_invProjScale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			face->gradients[2] -= (float)(g_flightVpWidth >> 1) * face->gradients[0];
			face->gradients[2] -= (float)(g_projOffsetY + (g_flightVpHeight >> 1)) * face->gradients[1];
			face->gradients[5] -= (float)(g_flightVpWidth >> 1) * face->gradients[3];
			face->gradients[5] -= (float)(g_projOffsetY + (g_flightVpHeight >> 1)) * face->gradients[4];
			face->gradients[8] -= (float)(g_flightVpWidth >> 1) * face->gradients[6];
			face->gradients[8] -= (float)(g_projOffsetY + (g_flightVpHeight >> 1)) * face->gradients[7];
			area = face->gradients[0] * face->gradients[4] - face->gradients[3] * face->gradients[1];
			if (area < g_sw3dZeroFloat)
				area = -area;
			{
				const OptTextureData* material = (const OptTextureData*)mesh->pMaterial;
				float lodScale;

				if (geometry->vertexIdx[3] == -1) {
					totalW = g_sw3dTriangleCornerCount / totalW;
				} else {
					totalW = g_sw3dQuadCornerCount / totalW;
				}
				lodScale = (float)(unsigned int)g_projScaleInt * totalW;
				face->mipLevel = (int)((float)((material->width * material->height) << 8) *
									   (area * (lodScale * lodScale)));
			}
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

// FUNCTION: XVT 0x4709C0
void sw3d_ProjectMeshVerticesDistant(SceneMesh* mesh) {
	const float projectionScale = (float)(unsigned int)g_projScaleInt / mesh->viewPosZ * g_sw3dDistantDepth;
	SceneFace* face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertex* output;
	int vertexBaseIndex;
	int vertexIndex;
	int faceIndex;

	vertexBaseIndex = g_projVertCount;
	mesh->vertBaseIndex = vertexBaseIndex;
	output = &g_projVertList[vertexBaseIndex];
	mesh->projVertCursor = 0;
	for (vertexIndex = 0; vertexIndex < mesh->vertexCount; ++vertexIndex) {
		g_vertexRemap[vertexIndex] = -1;
	}
	for (faceIndex = 0; faceIndex < mesh->visFaceCount; ++faceIndex, ++face) {
		const FaceRecord* geometry;
		int cornerIndex;

		RenderScene_TransformFaceTextureGradients(face, &mesh->pFaceTexturing[face->faceIndex],
												  &mesh->viewPosX);
		geometry = &mesh->pFaceGeom[face->faceIndex];
		face->maxVertW = 0.0f;
		face->minVertW = (float)(unsigned int)g_projScaleInt;
		for (cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
			OptVector transformed;
			const int modelVertexIndex = geometry->vertexIdx[cornerIndex];
			const int normalIndex = geometry->normalIdx[cornerIndex];
			int remappedVertex;
			float vertexW;

			if (modelVertexIndex == -1) {
				break;
			}
			remappedVertex = g_vertexRemap[modelVertexIndex];
			if (remappedVertex == -1) {
				g_vertexRemap[modelVertexIndex] = mesh->projVertCursor;
				++mesh->projVertCursor;
				transformed.x = mesh->pModelVerts[modelVertexIndex].x;
				transformed.y = mesh->pModelVerts[modelVertexIndex].y;
				transformed.z = mesh->pModelVerts[modelVertexIndex].z;
				Math3D_RotateVec3(&transformed.x, mesh->viewOrient);
				transformed.x += mesh->viewPosX;
				transformed.y += mesh->viewPosY;
				transformed.z += mesh->viewPosZ;
				transformed.z += g_sw3dDistantDepth;
				output->w = projectionScale / transformed.z;
				output->sx = output->w * transformed.x;
				output->sy = output->w * transformed.y;
				output->sx += (float)(g_flightVpWidth >> 1);
				output->sy += (float)(g_projOffsetY + (g_flightVpHeight >> 1));
				vertexW = output->w;
				RenderScene_ComputeVertexLighting(mesh, output, &mesh->pVertNormals[normalIndex],
												  &mesh->pModelVerts[modelVertexIndex], &g_meshEyePos);
				++output;
			} else {
				vertexW = g_projVertList[mesh->vertBaseIndex + remappedVertex].w;
			}
			if (face->maxVertW < vertexW) {
				face->maxVertW = vertexW;
			}
			if (face->minVertW > vertexW) {
				face->minVertW = vertexW;
			}
		}

		if (mesh->pUVs != NULL) {
			OptVector transformed;
			const int uvIndex = geometry->uvIdx[0];
			const OptVector* modelVertex = &mesh->pModelVerts[geometry->vertexIdx[0]];
			float c00;
			float c01;
			float c02;
			float c10;
			float c11;
			float c12;
			float c20;
			float c21;
			float c22;
			float inverse;
			float scaled;

			transformed.x = modelVertex->x;
			transformed.y = modelVertex->y;
			transformed.z = modelVertex->z;
			Math3D_RotateVec3(&transformed.x, mesh->viewOrient);
			transformed.x += mesh->viewPosX;
			transformed.y += mesh->viewPosY;
			transformed.z += mesh->viewPosZ;
			transformed.z += g_sw3dDistantDepth;
			face->gradients[6] = transformed.x - mesh->pUVs[uvIndex].v * face->gradients[3] -
								 mesh->pUVs[uvIndex].u * face->gradients[0];
			face->gradients[7] = transformed.y - mesh->pUVs[uvIndex].v * face->gradients[4] -
								 mesh->pUVs[uvIndex].u * face->gradients[1];
			face->gradients[8] = transformed.z - mesh->pUVs[uvIndex].v * face->gradients[5] -
								 mesh->pUVs[uvIndex].u * face->gradients[2];
			c00 = face->gradients[8] * face->gradients[4] - face->gradients[5] * face->gradients[7];
			c01 = face->gradients[5] * face->gradients[6] - face->gradients[8] * face->gradients[3];
			c02 = face->gradients[7] * face->gradients[3] - face->gradients[4] * face->gradients[6];
			c10 = face->gradients[2] * face->gradients[7] - face->gradients[8] * face->gradients[1];
			c11 = face->gradients[8] * face->gradients[0] - face->gradients[2] * face->gradients[6];
			c12 = face->gradients[1] * face->gradients[6] - face->gradients[7] * face->gradients[0];
			c20 = face->gradients[5] * face->gradients[1] - face->gradients[2] * face->gradients[4];
			c21 = face->gradients[2] * face->gradients[3] - face->gradients[5] * face->gradients[0];
			c22 = face->gradients[4] * face->gradients[0] - face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f) {
				c22 = 1.0f;
			}
			inverse = g_sw3dUnitFloat /
					  (c20 * face->gradients[6] + c21 * face->gradients[7] + c22 * face->gradients[8]);
			scaled = inverse / projectionScale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			face->gradients[2] -= (float)(g_flightVpWidth >> 1) * face->gradients[0];
			face->gradients[2] -= (float)(g_projOffsetY + (g_flightVpHeight >> 1)) * face->gradients[1];
			face->gradients[5] -= (float)(g_flightVpWidth >> 1) * face->gradients[3];
			face->gradients[5] -= (float)(g_projOffsetY + (g_flightVpHeight >> 1)) * face->gradients[4];
			face->gradients[8] -= (float)(g_flightVpWidth >> 1) * face->gradients[6];
			face->gradients[8] -= (float)(g_projOffsetY + (g_flightVpHeight >> 1)) * face->gradients[7];
			{
				const OptTextureData* material = (const OptTextureData*)mesh->pMaterial;
				float mipValue;
				mipValue = face->gradients[4] * face->gradients[0] * transformed.z * transformed.z;
				if (mipValue < g_sw3dZeroFloat)
					mipValue = -mipValue;
				face->mipLevel = (int)((float)((material->width * material->height) << 8) * mipValue);
				mipValue = face->gradients[1] * face->gradients[3] * transformed.z * transformed.z;
				if (mipValue < g_sw3dZeroFloat)
					mipValue = -mipValue;
				face->mipLevel += (int)((float)((material->width * material->height) << 8) * mipValue);
			}
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

// FUNCTION: XVT 0x471020
void sw3d_RasterizeMeshFaces(SceneMesh* mesh) {
	enum {
		SW3D_INVALID_EDGE = -1,
		SW3D_REJECTED_EDGE = -2,
	};

	ProjVertex* vertices = &g_projVertList[mesh->vertBaseIndex];
	SceneFace* faceCursor = &g_visFaceList[mesh->faceBaseIndex];
	int sceneEdgeCursor = g_sceneEdgeCursor;
	SceneFace* face;
	SceneEdge* outputEdge;
	SceneEdge* firstEdge;
	int edgeIndex;
	const int edgeCount = mesh->edgeCount;
	int faceIndex;
	int outputCount;

	mesh->edgeBaseIndex = sceneEdgeCursor;
	mesh->clippedEdgeCount = 0;
	firstEdge = outputEdge = &g_sceneEdgeList[sceneEdgeCursor];
	if (edgeCount > 0) {
		for (edgeIndex = 0; edgeIndex < mesh->edgeCount; ++edgeIndex) {
			g_sceneEdgeFlags[edgeIndex] = SW3D_INVALID_EDGE;
		}
	}

	for (faceIndex = 0; faceIndex < mesh->visFaceCount; ++faceIndex) {
		const FaceRecord* record;
		int cornerCount;
		int currentCorner;
		int previousCorner;

		outputCount = 0;
		record = &mesh->pFaceGeom[faceCursor->faceIndex];
		face = faceCursor;
		++faceCursor;

		if (face->nearClipState == SW3D_INVALID_EDGE) {
			face->nearClipState = g_flightVpHeight;
			g_sw3dClipTop = NULL;
			g_sw3dClipBottom = NULL;
			cornerCount = record->edgeIdx[(sizeof(record->edgeIdx) / sizeof(record->edgeIdx[0])) - 1] !=
								  SW3D_INVALID_EDGE
							  ? (int)(sizeof(record->edgeIdx) / sizeof(record->edgeIdx[0]))
							  : (int)(sizeof(record->edgeIdx) / sizeof(record->edgeIdx[0])) - 1;
			currentCorner = 0;
			for (previousCorner = cornerCount;;) {
				int sourceEdge;
				int existingEdge;

				--previousCorner;
				sourceEdge = record->edgeIdx[previousCorner];
				existingEdge = g_sceneEdgeFlags[sourceEdge];
				g_sw3dGeneratedClipVertex = NULL;
				if (existingEdge == SW3D_INVALID_EDGE) {
					if (sw3d_SetupClippedEdge(
							mesh, outputEdge, &vertices[g_vertexRemap[record->vertexIdx[previousCorner]]],
							&vertices[g_vertexRemap[record->vertexIdx[currentCorner]]]) >= 0) {
						face->edges[outputCount++] = outputEdge;
						outputEdge->pClipVert = g_sw3dGeneratedClipVertex;
						g_sceneEdgeFlags[sourceEdge] = mesh->clippedEdgeCount;
						++mesh->clippedEdgeCount;
						++outputEdge;
					} else if (g_sw3dGeneratedClipVertex == NULL) {
						g_sceneEdgeFlags[sourceEdge] = SW3D_REJECTED_EDGE;
					}
				} else if (existingEdge != SW3D_REJECTED_EDGE) {
					SceneEdge* edge;

					edge = &firstEdge[existingEdge];
					face->edges[outputCount++] = edge;
					if (edge->pClipVert != NULL) {
						g_sw3dClipBottom = g_sw3dClipTop;
						g_sw3dClipTop = edge->pClipVert;
					}
				}
				currentCorner = previousCorner;
				if (previousCorner <= 0)
					break;
			}

			if (g_sw3dClipBottom != NULL) {
				if (sw3d_SetupClippedEdge(mesh, outputEdge, g_sw3dClipTop, g_sw3dClipBottom) >= 0) {
					face->edges[outputCount++] = outputEdge;
					++mesh->clippedEdgeCount;
					++outputEdge;
				}
			}
		} else {
			face->nearClipState = g_flightVpHeight;
			cornerCount = record->edgeIdx[(sizeof(record->edgeIdx) / sizeof(record->edgeIdx[0])) - 1] !=
								  SW3D_INVALID_EDGE
							  ? (int)(sizeof(record->edgeIdx) / sizeof(record->edgeIdx[0]))
							  : (int)(sizeof(record->edgeIdx) / sizeof(record->edgeIdx[0])) - 1;
			currentCorner = 0;
			for (previousCorner = cornerCount;;) {
				int sourceEdge;
				int existingEdge;

				--previousCorner;
				sourceEdge = record->edgeIdx[previousCorner];
				existingEdge = g_sceneEdgeFlags[sourceEdge];
				if (existingEdge == SW3D_INVALID_EDGE) {
					if (sw3d_SetupEdge(outputEdge,
									   &vertices[g_vertexRemap[record->vertexIdx[previousCorner]]],
									   &vertices[g_vertexRemap[record->vertexIdx[currentCorner]]]) >= 0) {
						face->edges[outputCount++] = outputEdge;
						g_sceneEdgeFlags[sourceEdge] = mesh->clippedEdgeCount;
						++mesh->clippedEdgeCount;
						++outputEdge;
					} else {
						g_sceneEdgeFlags[sourceEdge] = SW3D_REJECTED_EDGE;
					}
				} else if (existingEdge != SW3D_REJECTED_EDGE) {
					face->edges[outputCount++] = &firstEdge[existingEdge];
				}
				currentCorner = previousCorner;
				if (previousCorner <= 0)
					break;
			}
		}

		face->edgeCount = outputCount;
		if (outputCount == 0) {
			face->yBot = 0;
			face->yTop = 0;
		} else {
			sw3d_ScanConvertFace(face);
		}
	}
	g_sceneEdgeCursor += mesh->clippedEdgeCount;
}

// FUNCTION: XVT 0x471410
void sw3d_ScanConvertFace(SceneFace* face) {
	SceneEdge* left;
	SceneEdge* right;
	SceneEdge* edge;
	SceneEdge* swapEdge;
	int edgeIndex;
	int edgeCount;
	int remainingEdges;
	int scanY;
	int runEnd;
	int runRows;
	int spanCount;
	float runRowsFloat;
	float leftStartX;
	float rightStartX;
	float leftStartLight;
	float rightStartLight;

	edgeCount = face->edgeCount;
	remainingEdges = edgeCount;
	left = face->edges[0];
	right = left;
	for (edgeIndex = 1; edgeIndex < edgeCount; ++edgeIndex) {
		edge = face->edges[edgeIndex];
		if (left->yStart > edge->yStart)
			left = edge;
		if (right->yEnd < edge->yEnd)
			right = edge;
	}

	face->yTop = left->yStart;
	face->yBot = right->yEnd;
	spanCount = face->yBot - face->yTop;
	if (spanCount < g_sceneSpanPtrAvail) {
		g_sceneSpanPtrAvail -= spanCount;
		face->pSpans = &g_sceneSpanPtrList[g_sceneSpanPtrAvail];

		for (edgeIndex = 0; edgeIndex < face->edgeCount; ++edgeIndex) {
			edge = face->edges[edgeIndex];
			if (left != edge && edge->yStart == left->yStart) {
				right = edge;
				break;
			}
		}
		if (edgeIndex == face->edgeCount) {
			face->yBot = face->yTop;
		} else {
			if (right->x < left->x || (right->x == left->x && right->dxdy < left->dxdy)) {
				swapEdge = left;
				left = right;
				right = swapEdge;
			}

			face->pScanEdge = left;
			scanY = left->yStart;
			runEnd = right->yEnd;
			if (right->yEnd > left->yEnd)
				runEnd = left->yEnd;
			runRows = runEnd - scanY;
			leftStartX = left->x;
			rightStartX = right->x;
			leftStartLight = left->lightIntensity;
			rightStartLight = right->lightIntensity;

			if (rightStartX - leftStartX > g_sw3dUnitFloat) {
				do {
					--runRows;
					face->spanLightIntensityDx =
						(right->lightIntensity - left->lightIntensity) / (right->x - left->x);
					sw3d_InsertSpan(left->x, right->x, scanY++, face);
					if (runRows <= 0)
						break;
					left->x += left->dxdy;
					left->lightIntensity += left->dLightIntensityDy;
					right->x += right->dxdy;
					right->lightIntensity += right->dLightIntensityDy;
				} while (1);
			} else {
				runRowsFloat = (float)runRows;
				face->spanLightIntensityDx =
					(runRowsFloat * right->dLightIntensityDy + right->lightIntensity -
					 (runRowsFloat * left->dLightIntensityDy + left->lightIntensity)) /
					(runRowsFloat * right->dxdy + right->x - (runRowsFloat * left->dxdy + left->x));
				do {
					--runRows;
					sw3d_InsertSpan(left->x, right->x, scanY++, face);
					if (runRows <= 0)
						break;
					left->x += left->dxdy;
					left->lightIntensity += left->dLightIntensityDy;
					right->x += right->dxdy;
					right->lightIntensity += right->dLightIntensityDy;
				} while (1);
			}

			while (remainingEdges > 0) {
				if (right->yEnd != left->yEnd) {
					--remainingEdges;
					if (scanY == left->yEnd) {
						left->x = leftStartX;
						left->lightIntensity = leftStartLight;
						for (edgeIndex = 0; edgeIndex < face->edgeCount; ++edgeIndex) {
							if (face->edges[edgeIndex]->yStart == scanY)
								break;
						}
						if (edgeIndex == face->edgeCount) {
							face->yBot = scanY;
							break;
						}
						left = face->edges[edgeIndex];
						face->pScanEdge = left;
						leftStartX = left->x;
						leftStartLight = left->lightIntensity;
						right->x += right->dxdy;
						right->lightIntensity += right->dLightIntensityDy;
					} else {
						right->x = rightStartX;
						right->lightIntensity = rightStartLight;
						for (edgeIndex = 0; edgeIndex < face->edgeCount; ++edgeIndex) {
							if (face->edges[edgeIndex]->yStart == scanY)
								break;
						}
						if (edgeIndex == face->edgeCount) {
							face->yBot = scanY;
							break;
						}
						right = face->edges[edgeIndex];
						rightStartX = right->x;
						rightStartLight = right->lightIntensity;
						left->x += left->dxdy;
						left->lightIntensity += left->dLightIntensityDy;
					}
				} else {
					remainingEdges -= 2;
					if (remainingEdges == 0)
						break;

					left->x = leftStartX;
					right->x = rightStartX;
					left->lightIntensity = leftStartLight;
					right->lightIntensity = rightStartLight;
					for (edgeIndex = 0; edgeIndex < face->edgeCount; ++edgeIndex) {
						if (face->edges[edgeIndex]->yStart == scanY)
							break;
					}
					if (edgeIndex == face->edgeCount) {
						face->yBot = scanY;
						break;
					}
					left = face->edges[edgeIndex];
					for (++edgeIndex; edgeIndex < face->edgeCount; ++edgeIndex) {
						if (face->edges[edgeIndex]->yStart == scanY)
							break;
					}
					if (edgeIndex == face->edgeCount) {
						face->yBot = scanY;
						break;
					}
					right = face->edges[edgeIndex];
					if (right->x < left->x || (right->x == left->x && right->dxdy < left->dxdy)) {
						swapEdge = left;
						left = right;
						right = swapEdge;
					}
					face->pScanEdge = left;
					leftStartX = left->x;
					rightStartX = right->x;
					leftStartLight = left->lightIntensity;
					rightStartLight = right->lightIntensity;
				}

				if (remainingEdges == 1)
					return;
				if (remainingEdges == 2) {
					if (right->yEnd != left->yEnd)
						return;
					runRows = left->yEnd - scanY;
					runRowsFloat = (float)runRows;
					if (right->dxdy * runRowsFloat + right->x - (left->dxdy * runRowsFloat + left->x) >
						g_sw3dUnitFloat) {
						do {
							--runRows;
							face->spanLightIntensityDx =
								(right->lightIntensity - left->lightIntensity) / (right->x - left->x);
							sw3d_InsertSpan(left->x, right->x, scanY++, face);
							if (runRows <= 0)
								break;
							left->x += left->dxdy;
							left->lightIntensity += left->dLightIntensityDy;
							right->x += right->dxdy;
							right->lightIntensity += right->dLightIntensityDy;
						} while (1);
					} else {
						face->spanLightIntensityDx =
							(right->lightIntensity - left->lightIntensity) / (right->x - left->x);
						do {
							--runRows;
							sw3d_InsertSpan(left->x, right->x, scanY++, face);
							if (runRows <= 0)
								break;
							left->x += left->dxdy;
							left->lightIntensity += left->dLightIntensityDy;
							right->x += right->dxdy;
							right->lightIntensity += right->dLightIntensityDy;
						} while (1);
					}
				} else {
					runEnd = left->yEnd < right->yEnd ? left->yEnd : right->yEnd;
					runRows = runEnd - scanY;
					do {
						--runRows;
						face->spanLightIntensityDx =
							(right->lightIntensity - left->lightIntensity) / (right->x - left->x);
						sw3d_InsertSpan(left->x, right->x, scanY++, face);
						if (runRows <= 0)
							break;
						left->x += left->dxdy;
						left->lightIntensity += left->dLightIntensityDy;
						right->x += right->dxdy;
						right->lightIntensity += right->dLightIntensityDy;
					} while (1);
				}
			}

			left->x = leftStartX;
			right->x = rightStartX;
			left->lightIntensity = leftStartLight;
			right->lightIntensity = rightStartLight;
		}
	} else {
		face->yBot = face->yTop;
	}
}

// FUNCTION: XVT 0x471A10
int sw3d_SetupClippedEdge(SceneMesh* mesh, SceneEdge* edge, const ProjVertex* vTop, const ProjVertex* vBot) {
	const ProjVertex* inside;
	const ProjVertex* outside;
#ifdef XVT_MODERN
	uint32_t coordinateBits;
#endif
	int secondY;
	int firstY;
	const ProjVertex* swapVertex;
	int swapY;
	float inverseHeight;
	float firstRowOffset;

	inside = vBot;
	outside = vTop;
#ifdef XVT_MODERN
	memcpy(&coordinateBits, &vBot->w, sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t*)&vBot->w > 0x80000000u) {
#endif
#ifdef XVT_MODERN
		memcpy(&coordinateBits, &vTop->w, sizeof(coordinateBits));
		if (coordinateBits > 0x80000000u)
#else
		if (*(const uint32_t*)&vTop->w > 0x80000000u)
#endif
			return -1;
		outside = vBot;
		inside = vTop;
	}

#ifdef XVT_MODERN
	memcpy(&coordinateBits, &outside->w, sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t*)&outside->w > 0x80000000u) {
#endif
		float insideInverseW;
		float insideX;
		float insideY;
		float clipFraction;
		float clippedY;

		g_sw3dClipBottom = g_sw3dClipTop;
		g_sw3dClipTop = &g_projVertList[mesh->projVertCursor + mesh->vertBaseIndex];
		g_sw3dGeneratedClipVertex = g_sw3dClipTop;
		++mesh->projVertCursor;
		++g_projVertCount;

		insideInverseW = g_sw3dUnitFloat / inside->w;
		insideX = (inside->sx - (float)(g_flightVpWidth >> 1)) * insideInverseW;
		insideY = (inside->sy - (float)(g_projOffsetY + (g_flightVpHeight >> 1))) * insideInverseW;
		clipFraction = outside->w /
					   (outside->w - insideInverseW * (float)(unsigned int)g_projScaleInt + g_sw3dUnitFloat);
		clippedY = (insideY - outside->sy) * clipFraction + outside->sy;
		g_sw3dClipTop->sx =
			((insideX - outside->sx) * clipFraction + outside->sx) * (float)(unsigned int)g_projScaleInt;
		g_sw3dClipTop->sy = clippedY * (float)(unsigned int)g_projScaleInt;
		g_sw3dClipTop->sx = (float)(g_flightVpWidth >> 1) + g_sw3dClipTop->sx;
		g_sw3dClipTop->sy = (float)(g_projOffsetY + (g_flightVpHeight >> 1)) + g_sw3dClipTop->sy;
		g_sw3dClipTop->w = (float)(unsigned int)g_projScaleInt;
		g_sw3dClipTop->lightIntensity =
			outside->lightIntensity + (inside->lightIntensity - outside->lightIntensity) * clipFraction;
		outside = g_sw3dClipTop;
	}

#ifdef XVT_MODERN
	memcpy(&coordinateBits, &outside->sy, sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t*)&outside->sy > 0x80000000u) {
#endif
		firstY = 0;
	} else {
		firstY = (int)outside->sy;
		if ((float)firstY != outside->sy)
			++firstY;
	}
#ifdef XVT_MODERN
	memcpy(&coordinateBits, &inside->sy, sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t*)&inside->sy > 0x80000000u) {
#endif
		secondY = 0;
	} else {
		secondY = (int)inside->sy;
		if ((float)secondY != inside->sy)
			++secondY;
	}
	if (firstY == secondY)
		return -1;
	if (firstY > secondY) {
		swapVertex = outside;
		outside = inside;
		inside = swapVertex;
		swapY = firstY;
		firstY = secondY;
		secondY = swapY;
	}
	if (secondY <= 0)
		return -1;
	if (g_flightVpHeight <= firstY)
		return -1;
	if (secondY > g_flightVpHeight)
		secondY = g_flightVpHeight;

	edge->yEnd = secondY;
	inverseHeight = g_sw3dUnitFloat / (inside->sy - outside->sy);
	edge->dxdy = (inside->sx - outside->sx) * inverseHeight;
	edge->dLightIntensityDy = (inside->lightIntensity - outside->lightIntensity) * inverseHeight;
	edge->pClipVert = NULL;
	firstRowOffset = (float)firstY - outside->sy;
	edge->x = outside->sx + firstRowOffset * edge->dxdy;
	edge->lightIntensity = outside->lightIntensity + firstRowOffset * edge->dLightIntensityDy;
	edge->yStart = firstY;
	return firstY;
}

// FUNCTION: XVT 0x471CE0
int sw3d_SetupEdge(SceneEdge* edge, const ProjVertex* first, const ProjVertex* second) {
	const ProjVertex* swapVertex;
	int firstY;
	int secondY;
	int swapY;
	float inverseHeight;
	float firstRowOffset;

	if (first->sy < 0.0f) {
		firstY = 0;
	} else {
		firstY = (int)first->sy;
		if ((float)firstY != first->sy)
			++firstY;
	}

	if (second->sy < 0.0f) {
		secondY = 0;
	} else {
		secondY = (int)second->sy;
		if ((float)secondY != second->sy)
			++secondY;
	}

	if (firstY == secondY)
		return -1;
	if (firstY > secondY) {
		swapVertex = first;
		first = second;
		second = swapVertex;
		swapY = firstY;
		firstY = secondY;
		secondY = swapY;
	}
	if (secondY <= 0)
		return -1;
	if (firstY >= g_flightVpHeight)
		return -1;
	if (secondY > g_flightVpHeight)
		secondY = g_flightVpHeight;

	edge->yEnd = secondY;
	inverseHeight = g_sw3dUnitFloat / (second->sy - first->sy);
	edge->dxdy = (second->sx - first->sx) * inverseHeight;
	edge->dLightIntensityDy = (second->lightIntensity - first->lightIntensity) * inverseHeight;
	edge->pClipVert = NULL;
	firstRowOffset = (float)firstY - first->sy;
	edge->x = first->sx + firstRowOffset * edge->dxdy;
	edge->lightIntensity = first->lightIntensity + firstRowOffset * edge->dLightIntensityDy;
	edge->yStart = firstY;
	return firstY;
}

// FUNCTION: XVT 0x4865E0
void sw3d_DrawVisibleFacesToSurface(void) {
	enum {
		SW3D_MIP_MIN_DIMENSION = 8,
		SW3D_MIP_THRESHOLD_Q8 = 256,
		SW3D_TEXTURE_SHIFT_MASK = 12,
		SW3D_TEXTURE_SHIFT_INDEX_SHIFT = 4,
	};

	SceneEdge spanStartEdge;
	int faceIndex;

	FlightLight_ResetSoftwareFaceSampleCache();
	if (g_useHardware3D != 0) {
		RenderScene_FlushGeometry();
		return;
	}

	if (g_flightSurfaceAlreadyLocked == 0) {
		FlightSurface_Lock();
	}
	g_sw3dSpanSceneMesh = NULL;
	faceIndex = g_visFacePassStart;
	while (faceIndex < g_visFaceCount) {
		const OptTextureData* material;
		SceneMesh** pMesh;
		float rowBaseDepth;
		int mipTexelOffset;
		int textureWidth;
		int textureHeight;
		int textureHeightShiftIndex;
		int spanIndex;

		g_sw3dCurrentFace = &g_visFaceList[faceIndex];
		g_sw3dCurrentScanlineY = g_sw3dCurrentFace->yTop;
		g_sw3dCurrentFace->pScanEdge = &spanStartEdge;
		mipTexelOffset = 0;
		pMesh = &g_sw3dCurrentFace->pMesh;
		material = (const OptTextureData*)(*pMesh)->pMaterial;
		textureWidth = material->width;
		textureHeight = material->height;
		if (g_mipmappingEnabled != mipTexelOffset && textureWidth * textureHeight == material->textureSize) {
			int mipMetric = (int)((float)g_sw3dCurrentFace->mipLevel * g_mipLodScale);

			while (mipMetric > SW3D_MIP_THRESHOLD_Q8 && textureWidth != SW3D_MIP_MIN_DIMENSION &&
				   textureHeight != SW3D_MIP_MIN_DIMENSION) {
				mipMetric >>= 2;
				mipTexelOffset += textureWidth * textureHeight;
				textureWidth >>= 1;
				textureHeight >>= 1;
			}
		}

		g_sw3dSpanTextureWidthFloat = (float)textureWidth;
		g_sw3dSpanTextureHeightFloat = (float)textureHeight;
		textureHeightShiftIndex = textureHeight;
		textureHeightShiftIndex &= ~SW3D_TEXTURE_SHIFT_MASK;
		g_sw3dSpanTextureWidthShift =
			g_sw3dTextureShiftBySizeDiv16[(textureWidth & ~SW3D_TEXTURE_SHIFT_MASK) >>
										  SW3D_TEXTURE_SHIFT_INDEX_SHIFT];
		g_sw3dSpanTexelMask = textureWidth * textureHeight - 1;
		g_sw3dSpanTextureHeightShift =
			g_sw3dTextureShiftBySizeDiv16[textureHeightShiftIndex >> SW3D_TEXTURE_SHIFT_INDEX_SHIFT];
		g_sw3dSpanShadeTable = (*pMesh)->pPalette;
		g_sw3dSpanTexels = (uint8_t*)(*pMesh)->pTexels + mipTexelOffset;
		g_sw3dSpanSceneMesh = *pMesh;
		rowBaseDepth = (float)(unsigned int)g_sw3dCurrentScanlineY * g_sw3dCurrentFace->gradients[7] +
					   g_sw3dCurrentFace->gradients[8];
		g_sw3dSpanFramebufferRowOffset = g_surfacePitch * (g_sw3dCurrentScanlineY + g_flightVpY) +
										 g_flight16bppBytesPerPixel * g_flightVpX;

		for (spanIndex = 0; (unsigned int)g_sw3dCurrentScanlineY < (unsigned int)g_sw3dCurrentFace->yBot;
			 ++spanIndex, ++g_sw3dCurrentScanlineY) {
			SceneFace* rowFace = g_sw3dCurrentFace;
			SceneSpan* span = rowFace->pSpans[spanIndex];

			if (span != NULL) {
				SceneSpan* occluder;
				int sampleSubrow;
				int startX;
				int endX;
				float startXFloat;
				float lightIntensity;

				sampleSubrow = g_sw3dCurrentScanlineY & g_sw3dLightSampleBlockMask;
				if (sampleSubrow != 0) {
					g_sw3dLightSampleSubrowFloat = (float)(unsigned int)sampleSubrow;
					g_sw3dLightSampleSubrowLerpT =
						g_sw3dLightSampleSubrowFloat * g_sw3dSpanLengthReciprocal[g_sw3dLightSampleBlockSize];
					g_sw3dLightSampleRowsToNextBlockFloat =
						(float)(unsigned int)(g_sw3dLightSampleBlockSize - sampleSubrow);
				} else {
					g_sw3dLightSampleSubrowLerpT = 0.0f;
					g_sw3dLightSampleSubrowFloat = 0.0f;
					g_sw3dLightSampleRowsToNextBlockFloat = (float)(unsigned int)g_sw3dLightSampleBlockSize;
				}
				g_sw3dCurrentLightSampleCacheStamp =
					g_sw3dLightSampleCacheSceneStampBase +
					((unsigned int)g_sw3dCurrentScanlineY >> g_sw3dLightSampleBlockShift);

				occluder = span->next;
				startX = span->xStart;
				endX = span->xEnd;
				startXFloat = (float)startX;
				lightIntensity = span->lightIntensity;
				spanStartEdge.lightIntensity = lightIntensity;
				spanStartEdge.x = startXFloat;
				g_sw3dCurrentFace->spanLightIntensityDx = span->dLightIntensityDx;
				if (occluder != NULL) {
					do {
						const int occluderStart = occluder->xStart;

						if (endX <= occluderStart) {
							break;
						}
						if (startX >= occluderStart) {
							const int occluderEnd = occluder->xEnd;

							if (startX < occluderEnd) {
								startX = occluder->xEnd;
								if (endX <= occluderEnd) {
									break;
								}
							}
						} else {
							float depth = (float)startX * g_sw3dCurrentFace->gradients[6] + rowBaseDepth;

							sw3d_DrawTexturedSpan(startX, occluderStart, depth);
							startX = occluder->xEnd;
							endX = span->xEnd;
							if (endX <= startX) {
								break;
							}
						}
						occluder = occluder->next;
					} while (occluder != NULL);
					if (endX > startX) {
						float depth = (float)startX * g_sw3dCurrentFace->gradients[6] + rowBaseDepth;

						sw3d_DrawTexturedSpan(startX, endX, depth);
					}
				} else {
					float depth = g_sw3dCurrentFace->gradients[6] * startXFloat + rowBaseDepth;

					sw3d_DrawTexturedSpan(startX, endX, depth);
				}
				rowFace = g_sw3dCurrentFace;
			}
			rowBaseDepth = rowFace->gradients[7] + rowBaseDepth;
			g_sw3dSpanFramebufferRowOffset += g_surfacePitch;
		}
		++faceIndex;
	}

	if (g_flightSurfaceAlreadyLocked == 0) {
		FlightSurface_Unlock();
	}
}

// FUNCTION: XVT 0x486980
void sw3d_InsertSpan(float xLeft, float xRight, int scanY, SceneFace* face) {
	SceneSpan* current;
	SceneSpan* next;
	SceneSpan* insertionNext;
	SceneSpan* previous;
	SceneSpan* span;
	float newDepth;
	float currentDepth;
	int startX;
	int endX;
	int overlapWidth;
	int crossingFromRight;
	int crossingFromLeft;
	int currentLeftWidth;
	int currentRightWidth;

	face->pSpans[scanY - face->yTop] = NULL;
	if (xLeft < 0.0f) {
		startX = 0;
	} else {
		startX = (int)xLeft;
		if ((float)startX != xLeft) {
			++startX;
		}
	}
	if (xRight < 0.0f) {
		endX = 0;
	} else {
		endX = (int)xRight;
		if ((float)endX != xRight) {
			++endX;
		}
	}
	if (endX >= g_flightVpWidth) {
		endX = g_flightVpWidth;
	}
	if (endX <= startX || startX >= g_flightVpWidth || (g_sw3dSkipOddScanlines && (scanY & 1))) {
		return;
	}

	previous = NULL;
	current = g_scanlineSpanHeads[scanY];
	while (current != NULL) {
		if (current->xEnd <= startX) {
			previous = current;
			current = current->next;
			continue;
		}
		if (current->xStart > startX) {
			break;
		}

		if (face->maxVertW <= current->face->minVertW) {
			startX = current->xEnd;
			if (startX >= endX) {
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}
		if (face->minVertW >= current->face->maxVertW) {
			if (current->xEnd > endX) {
				previous = current;
				current = current->next;
				continue;
			}
			current->xEnd = startX;
			if (current->xEnd == current->xStart) {
				current->face->pSpans[scanY - current->face->yTop] = NULL;
				if (previous == NULL) {
					g_scanlineSpanHeads[scanY] = current->next;
				} else {
					previous->next = current->next;
				}
				current = current->next;
			} else {
				previous = current;
				current = current->next;
			}
			continue;
		}

		newDepth =
			face->gradients[7] * (float)scanY + face->gradients[8] + face->gradients[6] * (float)startX;
		currentDepth = current->face->gradients[7] * (float)scanY + current->face->gradients[8] +
					   current->face->gradients[6] * (float)startX;
		if (newDepth <= currentDepth) {
			if (current->face->gradients[6] >= face->gradients[6]) {
				startX = current->xEnd;
				if (startX >= endX) {
					return;
				}
				previous = current;
				current = current->next;
				continue;
			}
			if (current->xEnd < endX) {
				overlapWidth = current->xEnd - startX;
				newDepth += (float)overlapWidth * face->gradients[6];
				currentDepth += (float)overlapWidth * current->face->gradients[6];
				if (newDepth <= currentDepth) {
					startX = current->xEnd;
					previous = current;
					current = current->next;
					continue;
				}
			} else {
				overlapWidth = endX - startX;
				newDepth += (float)overlapWidth * face->gradients[6];
				currentDepth += (float)overlapWidth * current->face->gradients[6];
				if (newDepth <= currentDepth) {
					return;
				}
			}
			currentLeftWidth =
				(int)((float)overlapWidth -
					  (newDepth - currentDepth) / (face->gradients[6] - current->face->gradients[6])) +
				1;
			if (currentLeftWidth < 0) {
				currentLeftWidth = 0;
			}
			startX += currentLeftWidth;
			if (startX >= endX) {
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}

		if (current->face->gradients[6] <= face->gradients[6]) {
			if (current->xEnd > endX) {
				previous = current;
				current = current->next;
				continue;
			}
			current->xEnd = startX;
			if (current->xEnd == current->xStart) {
				current->face->pSpans[scanY - current->face->yTop] = NULL;
				if (previous == NULL) {
					g_scanlineSpanHeads[scanY] = current->next;
				} else {
					previous->next = current->next;
				}
				current = current->next;
			} else {
				previous = current;
				current = current->next;
			}
			continue;
		}

		if (current->xEnd <= endX) {
			overlapWidth = current->xEnd - startX;
			newDepth += (float)overlapWidth * face->gradients[6];
			currentDepth += (float)overlapWidth * current->face->gradients[6];
			if (newDepth >= currentDepth) {
				current->xEnd = startX;
				if (current->xEnd == current->xStart) {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
					if (previous == NULL) {
						g_scanlineSpanHeads[scanY] = current->next;
					} else {
						previous->next = current->next;
					}
					current = current->next;
				} else {
					previous = current;
					current = current->next;
				}
				continue;
			}

			crossingFromRight =
				(int)((newDepth - currentDepth) / (face->gradients[6] - current->face->gradients[6]));
			if (crossingFromRight < 0) {
				crossingFromRight = 0;
			}
			if (crossingFromRight > overlapWidth) {
				crossingFromRight = overlapWidth;
			}
			crossingFromLeft = overlapWidth - crossingFromRight;
			currentLeftWidth = startX - current->xStart;
			currentRightWidth = endX - current->xEnd;

			if (currentLeftWidth <= crossingFromRight && currentLeftWidth <= crossingFromLeft &&
				currentRightWidth >= currentLeftWidth) {
				currentLeftWidth = current->xEnd - current->xStart - crossingFromRight;
				current->xStart += currentLeftWidth;
				current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
				next = current->next;
				if (next == NULL || next->xStart > current->xStart) {
					break;
				}
				if (previous == NULL) {
					g_scanlineSpanHeads[scanY] = next;
				} else {
					previous->next = next;
				}
				if (current->xStart < next->xEnd) {
					currentLeftWidth = next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL && insertionNext->xStart < current->xStart) {
					if (current->xStart < insertionNext->xEnd) {
						currentLeftWidth = insertionNext->xEnd - current->xStart;
						current->xStart += currentLeftWidth;
						current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
					}
					if (current->xStart >= current->xEnd) {
						break;
					}
					next = insertionNext;
					insertionNext = insertionNext->next;
				}
				if (current->xStart < current->xEnd) {
					next->next = current;
					current->next = insertionNext;
				} else {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
				}
				if (previous == NULL) {
					current = g_scanlineSpanHeads[scanY];
				} else {
					current = previous->next;
				}
				continue;
			} else if (currentLeftWidth >= crossingFromRight && crossingFromLeft >= crossingFromRight &&
					   currentRightWidth >= crossingFromRight) {
				current->xEnd = startX;
				if (current->xEnd == current->xStart) {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
					if (previous == NULL) {
						g_scanlineSpanHeads[scanY] = current->next;
					} else {
						previous->next = current->next;
					}
					current = current->next;
				} else {
					previous = current;
					current = current->next;
				}
				continue;
			}
			if (currentLeftWidth >= crossingFromLeft && crossingFromLeft <= crossingFromRight &&
				currentRightWidth >= crossingFromLeft) {
				startX = current->xEnd;
				if (startX >= endX) {
					return;
				}
				previous = current;
				current = current->next;
				continue;
			}
			endX = current->xEnd - crossingFromRight;
			if (startX >= endX) {
				return;
			}
			previous = current;
			current = current->next;
		} else {
			overlapWidth = endX - startX;
			newDepth += (float)overlapWidth * face->gradients[6];
			currentDepth += (float)overlapWidth * current->face->gradients[6];
			if (newDepth < currentDepth) {
				currentLeftWidth =
					(int)((float)overlapWidth -
						  (newDepth - currentDepth) / (face->gradients[6] - current->face->gradients[6])) +
					1;
				if (currentLeftWidth < 0) {
					currentLeftWidth = 0;
				}
				currentLeftWidth += startX;
				if (currentLeftWidth > endX) {
					currentLeftWidth = endX;
				}
				endX = currentLeftWidth;
				if (startX >= endX) {
					return;
				}
			}
			previous = current;
			current = current->next;
		}
	}

	span = g_pSceneSpanDataCur++;
	if (g_pSceneSpanDataCur == g_pSceneSpanDataEnd) {
		--g_pSceneSpanDataCur;
	}
	span->xStart = startX;
	span->xEnd = endX;
	span->face = face;
	if (face->pScanEdge == NULL) {
		span->lightIntensity = 0.0f;
	} else {
		span->lightIntensity = (float)startX;
		span->lightIntensity -= face->pScanEdge->x;
		span->lightIntensity *= face->spanLightIntensityDx;
		span->lightIntensity += face->pScanEdge->lightIntensity;
	}
	span->dLightIntensityDx = face->spanLightIntensityDx;
	face->pSpans[scanY - face->yTop] = span;
	if (previous == NULL) {
		g_scanlineSpanHeads[scanY] = span;
	} else {
		previous->next = span;
	}
	span->next = current;

	previous = span;
	while (current != NULL) {
		if (current->xStart >= span->xEnd) {
			return;
		}
		if (face->maxVertW <= current->face->minVertW) {
			if (current->xEnd >= span->xEnd) {
				span->xEnd = current->xStart;
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}
		if (face->minVertW >= current->face->maxVertW) {
			if (current->xEnd <= span->xEnd) {
				current->face->pSpans[scanY - current->face->yTop] = NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			currentLeftWidth = span->xEnd - current->xStart;
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth = next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL && insertionNext->xStart < current->xStart) {
					if (current->xStart < insertionNext->xEnd) {
						currentLeftWidth = insertionNext->xEnd - current->xStart;
						current->xStart += currentLeftWidth;
						current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
					}
					if (current->xStart >= current->xEnd) {
						break;
					}
					next = insertionNext;
					insertionNext = insertionNext->next;
				}
				if (current->xStart < current->xEnd) {
					next->next = current;
					current->next = insertionNext;
				} else {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
			continue;
		}

		newDepth = face->gradients[7] * (float)scanY + face->gradients[8] +
				   face->gradients[6] * (float)current->xStart;
		currentDepth = current->face->gradients[7] * (float)scanY + current->face->gradients[8] +
					   current->face->gradients[6] * (float)current->xStart;
		if (newDepth <= currentDepth) {
			if (current->face->gradients[6] >= face->gradients[6]) {
				if (current->xEnd >= span->xEnd) {
					span->xEnd = current->xStart;
					return;
				}
				previous = current;
				current = current->next;
				continue;
			}

			if (current->xEnd < span->xEnd) {
				overlapWidth = current->xEnd - current->xStart;
				newDepth += (float)overlapWidth * face->gradients[6];
				currentDepth += (float)overlapWidth * current->face->gradients[6];
				if (newDepth <= currentDepth) {
					previous = current;
					current = current->next;
					continue;
				}
				crossingFromRight =
					(int)((newDepth - currentDepth) / (face->gradients[6] - current->face->gradients[6]));
				if (crossingFromRight < 0) {
					crossingFromRight = 0;
				}
				current->xEnd -= crossingFromRight;
				if (current->xEnd <= current->xStart) {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
					previous->next = current->next;
					current = current->next;
				} else {
					previous = current;
					current = current->next;
				}
				continue;
			}

			overlapWidth = span->xEnd - current->xStart;
			newDepth += (float)overlapWidth * face->gradients[6];
			currentDepth += (float)overlapWidth * current->face->gradients[6];
			if (newDepth <= currentDepth) {
				span->xEnd = current->xStart;
				return;
			}
			crossingFromRight =
				(int)((newDepth - currentDepth) / (face->gradients[6] - current->face->gradients[6]));
			if (crossingFromRight < 0) {
				crossingFromRight = 0;
			}
			if (crossingFromRight > overlapWidth) {
				crossingFromRight = overlapWidth;
			}
			currentLeftWidth = overlapWidth - crossingFromRight;
			if (currentLeftWidth > crossingFromRight && current->xEnd - span->xEnd > crossingFromRight) {
				span->xEnd = current->xStart;
				return;
			}
			if (current->xEnd - span->xEnd > currentLeftWidth) {
				current->xStart += overlapWidth;
				current->lightIntensity += (float)overlapWidth * current->dLightIntensityDx;
				next = current->next;
				if (next != NULL && next->xStart < current->xStart) {
					previous->next = next;
					if (current->xStart < next->xEnd) {
						currentLeftWidth = next->xEnd - current->xStart;
						current->xStart += currentLeftWidth;
						current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
					}
					insertionNext = next->next;
					while (insertionNext != NULL && insertionNext->xStart < current->xStart) {
						if (current->xStart < insertionNext->xEnd) {
							currentLeftWidth = insertionNext->xEnd - current->xStart;
							current->xStart += currentLeftWidth;
							current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
						}
						if (current->xStart >= current->xEnd) {
							break;
						}
						next = insertionNext;
						insertionNext = insertionNext->next;
					}
					if (current->xStart < current->xEnd) {
						next->next = current;
						current->next = insertionNext;
					} else {
						current->face->pSpans[scanY - current->face->yTop] = NULL;
					}
					current = previous->next;
					continue;
				}
				previous = current;
				current = current->next;
			} else {
				current->xEnd = span->xEnd - crossingFromRight;
				previous = current;
				current = current->next;
			}
			continue;
		}

		if (current->face->gradients[6] <= face->gradients[6]) {
			if (current->xEnd <= span->xEnd) {
				current->face->pSpans[scanY - current->face->yTop] = NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			currentLeftWidth = span->xEnd - current->xStart;
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth = next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL && insertionNext->xStart < current->xStart) {
					if (current->xStart < insertionNext->xEnd) {
						currentLeftWidth = insertionNext->xEnd - current->xStart;
						current->xStart += currentLeftWidth;
						current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
					}
					if (current->xStart >= current->xEnd) {
						break;
					}
					next = insertionNext;
					insertionNext = insertionNext->next;
				}
				if (current->xStart < current->xEnd) {
					next->next = current;
					current->next = insertionNext;
				} else {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
			continue;
		}

		if (span->xEnd >= current->xEnd) {
			overlapWidth = current->xEnd - current->xStart;
			newDepth += (float)overlapWidth * face->gradients[6];
			currentDepth += (float)overlapWidth * current->face->gradients[6];
			if (newDepth >= currentDepth) {
				current->face->pSpans[scanY - current->face->yTop] = NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			crossingFromRight =
				(int)((newDepth - currentDepth) / (face->gradients[6] - current->face->gradients[6]));
			if (crossingFromRight < 0) {
				crossingFromRight = 0;
			}
			currentLeftWidth = overlapWidth - crossingFromRight;
			if (currentLeftWidth < 0) {
				currentLeftWidth = 0;
			}
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
			if (current->xEnd <= current->xStart) {
				current->face->pSpans[scanY - current->face->yTop] = NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth = next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL && insertionNext->xStart < current->xStart) {
					if (current->xStart < insertionNext->xEnd) {
						currentLeftWidth = insertionNext->xEnd - current->xStart;
						current->xStart += currentLeftWidth;
						current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
					}
					if (current->xStart >= current->xEnd) {
						break;
					}
					next = insertionNext;
					insertionNext = insertionNext->next;
				}
				if (current->xStart < current->xEnd) {
					next->next = current;
					current->next = insertionNext;
				} else {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
		} else {
			overlapWidth = span->xEnd - current->xStart;
			newDepth += (float)overlapWidth * face->gradients[6];
			currentDepth += (float)overlapWidth * current->face->gradients[6];
			if (newDepth >= currentDepth) {
				current->xStart += overlapWidth;
				current->lightIntensity += (float)overlapWidth * current->dLightIntensityDx;
				next = current->next;
				if (next != NULL && next->xStart < current->xStart) {
					previous->next = next;
					if (current->xStart < next->xEnd) {
						currentLeftWidth = next->xEnd - current->xStart;
						current->xStart += currentLeftWidth;
						current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
					}
					insertionNext = next->next;
					while (insertionNext != NULL && insertionNext->xStart < current->xStart) {
						if (current->xStart < insertionNext->xEnd) {
							currentLeftWidth = insertionNext->xEnd - current->xStart;
							current->xStart += currentLeftWidth;
							current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
						}
						if (current->xStart >= current->xEnd) {
							break;
						}
						next = insertionNext;
						insertionNext = insertionNext->next;
					}
					if (current->xStart < current->xEnd) {
						next->next = current;
						current->next = insertionNext;
					} else {
						current->face->pSpans[scanY - current->face->yTop] = NULL;
					}
					current = previous->next;
					continue;
				}
				previous = current;
				current = current->next;
				continue;
			}
			currentLeftWidth =
				(int)((float)overlapWidth -
					  (newDepth - currentDepth) / (face->gradients[6] - current->face->gradients[6]));
			if (currentLeftWidth < 0) {
				currentLeftWidth = 0;
			}
			if (currentLeftWidth > overlapWidth) {
				currentLeftWidth = overlapWidth;
			}
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth = next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL && insertionNext->xStart < current->xStart) {
					if (current->xStart < insertionNext->xEnd) {
						currentLeftWidth = insertionNext->xEnd - current->xStart;
						current->xStart += currentLeftWidth;
						current->lightIntensity += (float)currentLeftWidth * current->dLightIntensityDx;
					}
					if (current->xStart >= current->xEnd) {
						break;
					}
					next = insertionNext;
					insertionNext = insertionNext->next;
				}
				if (current->xStart < current->xEnd) {
					next->next = current;
					current->next = insertionNext;
				} else {
					current->face->pSpans[scanY - current->face->yTop] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
		}
	}
}

// FUNCTION: XVT 0x4879D0
void sw3d_DrawTexturedSpan(int startX, int endX, float depth) {
	enum {
		SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT = 3,
		SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT = 8,
		SW3D_FIXED_POINT_SHIFT = 8,
		SW3D_SHADE_LEVEL_MASK = 0xF,
		SW3D_SHADE_TABLE_LEVEL_STRIDE = 256,
		SW3D_TRUE_COLOR_SHADE_TABLE_OFFSET = 4096,
		SW3D_MAX_SHADE_Q8 = 0xEFF,
	};

	SceneFace* face;
	SoftwareLightSample* lightSamples;
	SoftwareLightSample* leftSample;
	SoftwareLightSample* rightSample;
	float* uGradient;
	float* vGradient;
	float uAtY;
	float vAtY;
	float viewDepthAtY;
	float uNumerator;
	float vNumerator;
	float inverseDepth;
	float u;
	float v;
	float rightU;
	float rightV;
	float leftLight;
	float rightLight;
	float lightIntensity;
	float lightIntensityAtEnd;
	float lightIntensityBlockStep;
	float uNumeratorBlockStep;
	float vNumeratorBlockStep;
	float depthBlockStep;
	float boundaryDepth;
	float sampleIntensity;
	float fixedPointValue;
	float fixedPointBias;
	int fixedPointBits;
	int fixedPointBiasBits;
	int nextUQ8;
	int nextVQ8;
	int endShadeQ8;
	int shadeDeltaQ8;
	int startBlock;
	int endBlock;
	int block;
	int boundaryX;
	int blockStartX;
	int blockStartY;
	int withinBlockX;
	int withinBlockY;
	int stampDelta;
	int pixelIndex;
	int useSpecializedTextureWrap;
	int textureWidthMask;
	int textureHeightMask;

	face = g_sw3dCurrentFace;
	lightSamples = (SoftwareLightSample*)face->pPhongData;
	viewDepthAtY = (float)(unsigned int)g_sw3dCurrentScanlineY * face->gradients[7] + face->gradients[8];
	uGradient = &face->gradients[0];
	vGradient = &face->gradients[3];
	uAtY = (float)g_sw3dCurrentScanlineY * uGradient[1];
	vAtY = (float)g_sw3dCurrentScanlineY * vGradient[1];
	uAtY += uGradient[2];
	vAtY += vGradient[2];
	uNumerator = (float)startX * uGradient[0] + uAtY;
	vNumerator = (float)startX * vGradient[0] + vAtY;
	inverseDepth = g_sw3dSpanOneFloat / depth;
	u = inverseDepth * uNumerator;
	v = inverseDepth * vNumerator;
	lightIntensity =
		((float)startX - face->pScanEdge->x) * face->spanLightIntensityDx + face->pScanEdge->lightIntensity;

	g_sw3dSpanShadeDitherAccum = g_sw3dShadeDitherInitialByScanlineParity[g_sw3dCurrentScanlineY & 1];
	startBlock = startX >> g_sw3dLightSampleBlockShift;
	endBlock = (endX - 1) >> g_sw3dLightSampleBlockShift;
	g_sw3dSpanStartX = startX;
	withinBlockX = startX & g_sw3dLightSampleBlockMask;
	withinBlockY = g_sw3dCurrentScanlineY & g_sw3dLightSampleBlockMask;
	blockStartX = startX - withinBlockX;
	blockStartY = g_sw3dCurrentScanlineY - withinBlockY;
	leftSample = &lightSamples[startBlock];
	stampDelta = g_sw3dCurrentLightSampleCacheStamp - leftSample->stamp;
	if (stampDelta != 0) {
		if (stampDelta != 1) {
			leftSample->stamp = g_sw3dCurrentLightSampleCacheStamp;
			leftSample->intensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
				face, blockStartX, blockStartY,
				depth - (float)withinBlockX * face->gradients[6] -
					g_sw3dLightSampleSubrowFloat * face->gradients[7]);
		} else {
			++leftSample->stamp;
			leftSample->intensity += leftSample->rowDelta;
		}
		leftSample->rowDelta = FlightLight_ComputeSoftwareFaceSampleIntensity(
								   face, blockStartX, blockStartY + g_sw3dLightSampleBlockSize,
								   depth - (float)withinBlockX * face->gradients[6] +
									   g_sw3dLightSampleRowsToNextBlockFloat * face->gradients[7]) -
							   leftSample->intensity;
	}
	leftLight = leftSample->intensity + g_sw3dLightSampleSubrowLerpT * leftSample->rowDelta;

	boundaryX = (startBlock + 1) << g_sw3dLightSampleBlockShift;
	g_sw3dSpanLength = boundaryX - g_sw3dSpanStartX;
	uNumerator = (float)boundaryX * uGradient[0] + uAtY;
	vNumerator = (float)boundaryX * vGradient[0] + vAtY;
	boundaryDepth = (float)boundaryX * face->gradients[6] + viewDepthAtY;
	block = startBlock + 1;
	inverseDepth = g_sw3dSpanOneFloat / boundaryDepth;
	rightSample = &lightSamples[block];
	stampDelta = g_sw3dCurrentLightSampleCacheStamp - rightSample->stamp;
	if (stampDelta != 0) {
		rightSample->stamp = g_sw3dCurrentLightSampleCacheStamp;
		if (stampDelta != 1) {
			rightSample->intensity =
				FlightLight_ComputeSoftwareFaceSampleIntensity(face, boundaryX, blockStartY, boundaryDepth);
			sampleIntensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
				face, boundaryX, blockStartY + g_sw3dLightSampleBlockSize, boundaryDepth);
		} else {
			sampleIntensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
				face, boundaryX, blockStartY + g_sw3dLightSampleBlockSize, boundaryDepth);
			rightSample->intensity += rightSample->rowDelta;
		}
		rightSample->rowDelta = sampleIntensity - rightSample->intensity;
	}
	rightLight = rightSample->intensity + g_sw3dLightSampleSubrowLerpT * rightSample->rowDelta;
	leftLight += (rightLight - leftLight) * ((float)withinBlockX * g_sw3dLightSampleInvBlockSize);
	rightU = inverseDepth * uNumerator;
	rightV = inverseDepth * vNumerator;

	fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureWidthShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
	fixedPointValue = (rightU - u) * g_sw3dSpanLengthReciprocal[g_sw3dSpanLength] + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanStepVQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureHeightShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
	fixedPointValue = (rightV - v) * g_sw3dSpanLengthReciprocal[g_sw3dSpanLength] + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanStepUQ8 = fixedPointBits - fixedPointBiasBits;

	if ((unsigned int)block > (unsigned int)endBlock) {
		g_sw3dSpanLength = endX - g_sw3dSpanStartX;
	} else {
		uNumeratorBlockStep = uGradient[0] * g_sw3dLightSampleBlockSizeFloat;
		vNumeratorBlockStep = vGradient[0] * g_sw3dLightSampleBlockSizeFloat;
		depthBlockStep = face->gradients[6] * g_sw3dLightSampleBlockSizeFloat;
	}

	lightIntensityAtEnd = (float)g_sw3dSpanLength * face->spanLightIntensityDx + lightIntensity;
	lightIntensityBlockStep = face->spanLightIntensityDx * g_sw3dLightSampleBlockSizeFloat;
	fixedPointBias = g_sw3dTexCoordBiasByShift[0];
	memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
	fixedPointValue = (leftLight + lightIntensity) * g_sw3dLightIntensityToShadeScale + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanShadeQ8 = fixedPointBits - fixedPointBiasBits;
	if (g_sw3dSpanShadeQ8 < 0) {
		g_sw3dSpanShadeQ8 = 0;
	}
	if (g_sw3dSpanShadeQ8 > SW3D_MAX_SHADE_Q8) {
		g_sw3dSpanShadeQ8 = SW3D_MAX_SHADE_Q8;
	}
	fixedPointValue = (rightLight + lightIntensityAtEnd) * g_sw3dLightIntensityToShadeScale + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	endShadeQ8 = fixedPointBits - fixedPointBiasBits;
	if (endShadeQ8 < 0) {
		endShadeQ8 = 0;
	}
	if (endShadeQ8 > SW3D_MAX_SHADE_Q8) {
		endShadeQ8 = SW3D_MAX_SHADE_Q8;
	}
	shadeDeltaQ8 = endShadeQ8 - g_sw3dSpanShadeQ8;
	if (shadeDeltaQ8 < 0) {
		shadeDeltaQ8 += g_sw3dLightSampleBlockSize;
	}
	fixedPointBias = g_sw3dFloatToIntRoundBias;
	memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
	fixedPointValue = (float)shadeDeltaQ8 * g_sw3dSpanLengthReciprocal[g_sw3dSpanLength] + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanShadeStepQ8 = fixedPointBits - fixedPointBiasBits;

	fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureWidthShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
	fixedPointValue = u + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanVQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointValue = rightU + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	nextUQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureHeightShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
	fixedPointValue = v + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanUQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointValue = rightV + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	nextVQ8 = fixedPointBits - fixedPointBiasBits;
	useSpecializedTextureWrap = g_sw3dSpanTextureWidthShift >= SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT &&
								g_sw3dSpanTextureWidthShift <= SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT &&
								g_sw3dSpanTextureHeightShift >= SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT &&
								g_sw3dSpanTextureHeightShift <= SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT;
	if (useSpecializedTextureWrap) {
		textureWidthMask = (1 << g_sw3dSpanTextureWidthShift) - 1;
		textureHeightMask = (1 << g_sw3dSpanTextureHeightShift) - 1;
	} else {
		textureWidthMask = 0;
		textureHeightMask = 0;
	}

	for (;;) {
		if (block <= endBlock) {
			boundaryDepth += depthBlockStep;
			inverseDepth = g_sw3dSpanOneFloat / boundaryDepth;
		}

		if (g_flight16bppBytesPerPixel == 2 && !useSpecializedTextureWrap) {
			sw3d_DrawTexturedShadeSpanGeneric();
		} else {
			for (pixelIndex = 0; pixelIndex < g_sw3dSpanLength; ++pixelIndex) {
				unsigned int shadeAccum;
				int texelIndex;
				int texel;

				if (useSpecializedTextureWrap) {
					texelIndex = (((g_sw3dSpanUQ8 >> SW3D_FIXED_POINT_SHIFT) & textureHeightMask)
								  << g_sw3dSpanTextureWidthShift) |
								 ((g_sw3dSpanVQ8 >> SW3D_FIXED_POINT_SHIFT) & textureWidthMask);
				} else {
					texelIndex = ((g_sw3dSpanUQ8 >> SW3D_FIXED_POINT_SHIFT) << g_sw3dSpanTextureWidthShift) +
								 (g_sw3dSpanVQ8 >> SW3D_FIXED_POINT_SHIFT);
					texelIndex &= g_sw3dSpanTexelMask;
				}
				texel = g_sw3dSpanTexels[texelIndex];
				shadeAccum = (unsigned int)(g_sw3dSpanShadeQ8 + g_sw3dSpanShadeDitherAccum);
				g_sw3dSpanShadeDitherAccum = (uint8_t)shadeAccum;
				if (g_flight16bppBytesPerPixel == 2) {
					uint16_t* surface16 =
						(uint16_t*)((uint8_t*)g_surfacePixels + g_sw3dSpanFramebufferRowOffset);
					uint16_t* shadeTable16 =
						(uint16_t*)(g_sw3dSpanShadeTable + SW3D_TRUE_COLOR_SHADE_TABLE_OFFSET);
					surface16[g_sw3dSpanStartX + pixelIndex] =
						shadeTable16[(((shadeAccum >> SW3D_FIXED_POINT_SHIFT) & SW3D_SHADE_LEVEL_MASK)
									  << SW3D_FIXED_POINT_SHIFT) +
									 texel];
				} else {
					uint8_t* surface8 =
						(uint8_t*)g_surfacePixels + g_sw3dSpanFramebufferRowOffset + g_sw3dSpanStartX;
					surface8[pixelIndex] = g_sw3dSpanShadeTable[(((shadeAccum >> SW3D_FIXED_POINT_SHIFT) &
																  SW3D_SHADE_LEVEL_MASK) *
																 SW3D_SHADE_TABLE_LEVEL_STRIDE) +
																texel];
				}
				g_sw3dSpanShadeQ8 += g_sw3dSpanShadeStepQ8;
				g_sw3dSpanUQ8 += g_sw3dSpanStepUQ8;
				g_sw3dSpanVQ8 += g_sw3dSpanStepVQ8;
			}
		}
		if (block > endBlock) {
			break;
		}

		uNumerator += uNumeratorBlockStep;
		vNumerator += vNumeratorBlockStep;
		g_sw3dSpanStartX += g_sw3dSpanLength;
		boundaryX += g_sw3dLightSampleBlockSize;
		if (block == endBlock) {
			g_sw3dSpanLength = endX - g_sw3dSpanStartX;
		} else {
			g_sw3dSpanLength = g_sw3dLightSampleBlockSize;
		}
		++block;
		rightSample = &lightSamples[block];
		stampDelta = g_sw3dCurrentLightSampleCacheStamp - rightSample->stamp;
		if (stampDelta != 0) {
			rightSample->stamp = g_sw3dCurrentLightSampleCacheStamp;
			if (stampDelta != 1) {
				rightSample->intensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundaryX, blockStartY, boundaryDepth);
				sampleIntensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundaryX, blockStartY + g_sw3dLightSampleBlockSize, boundaryDepth);
			} else {
				sampleIntensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundaryX, blockStartY + g_sw3dLightSampleBlockSize, boundaryDepth);
				rightSample->intensity += rightSample->rowDelta;
			}
			rightSample->rowDelta = sampleIntensity - rightSample->intensity;
		}
		rightU = inverseDepth * uNumerator;
		rightV = inverseDepth * vNumerator;
		rightLight = rightSample->intensity + g_sw3dLightSampleSubrowLerpT * rightSample->rowDelta;
		lightIntensityAtEnd += lightIntensityBlockStep;
		g_sw3dSpanShadeQ8 = endShadeQ8;
		fixedPointBias = g_sw3dTexCoordBiasByShift[0];
		memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
		fixedPointValue =
			(rightLight + lightIntensityAtEnd) * g_sw3dLightIntensityToShadeScale + fixedPointBias;
		memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
		endShadeQ8 = fixedPointBits - fixedPointBiasBits;
		if (endShadeQ8 < 0) {
			endShadeQ8 = 0;
		}
		if (endShadeQ8 > SW3D_MAX_SHADE_Q8) {
			endShadeQ8 = SW3D_MAX_SHADE_Q8;
		}
		shadeDeltaQ8 = endShadeQ8 - g_sw3dSpanShadeQ8;
		if (shadeDeltaQ8 < 0) {
			shadeDeltaQ8 += g_sw3dLightSampleBlockSize;
		}
		g_sw3dSpanShadeStepQ8 = shadeDeltaQ8 >> g_sw3dLightSampleBlockShift;
		g_sw3dSpanVQ8 = nextUQ8;
		g_sw3dSpanUQ8 = nextVQ8;
		fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureWidthShift];
		memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
		fixedPointValue = rightU + fixedPointBias;
		memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
		nextUQ8 = fixedPointBits - fixedPointBiasBits;
		fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureHeightShift];
		memcpy(&fixedPointBiasBits, &fixedPointBias, sizeof(fixedPointBiasBits));
		fixedPointValue = rightV + fixedPointBias;
		memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
		nextVQ8 = fixedPointBits - fixedPointBiasBits;
		g_sw3dSpanStepVQ8 = (nextUQ8 - g_sw3dSpanVQ8) >> g_sw3dLightSampleBlockShift;
		g_sw3dSpanStepUQ8 = (nextVQ8 - g_sw3dSpanUQ8) >> g_sw3dLightSampleBlockShift;
	}
}

// FUNCTION: XVT 0x497850
int sw3d_DrawTexturedShadeSpanGeneric(void) {
	uint8_t* pixel;
	uint8_t* pixelEnd;
	uint8_t* shadeTable;
	int result;

	pixel = (uint8_t*)g_surfacePixels;
	shadeTable = g_sw3dSpanShadeTable + 4096;
	pixel += g_sw3dSpanFramebufferRowOffset;
	pixelEnd = pixel;
	pixel += 2 * g_sw3dSpanStartX;
	result = g_sw3dSpanStartX + g_sw3dSpanLength;
	pixelEnd += 2 * result;
	while (pixel < pixelEnd) {
		int texelIndex;
		int texel;
		unsigned int shadeAccum;

		texelIndex = ((g_sw3dSpanUQ8 >> 8) << g_sw3dSpanTextureWidthShift) + (g_sw3dSpanVQ8 >> 8);
		texel = g_sw3dSpanTexels[texelIndex & g_sw3dSpanTexelMask];
		shadeAccum = (unsigned int)(g_sw3dSpanShadeQ8 + g_sw3dSpanShadeDitherAccum);
		g_sw3dSpanShadeDitherAccum = (uint8_t)shadeAccum;
		*(uint16_t*)pixel = ((uint16_t*)shadeTable)[(((shadeAccum >> 8) & 0xF) << 8) + texel];
		pixel += 2;
		g_sw3dSpanShadeQ8 += g_sw3dSpanShadeStepQ8;
		result = g_sw3dSpanUQ8 + g_sw3dSpanStepUQ8;
		g_sw3dSpanUQ8 = result;
		g_sw3dSpanVQ8 += g_sw3dSpanStepVQ8;
	}
	return result;
}

// FUNCTION: XVT 0x497940
void sw3d_BlitOccludedSpan(const uint8_t* pSrcRaster, int startX, int endX, int scanY, float depth) {
	SceneSpan* span;
	int drawX;

	drawX = startX;
	if (g_flight16bppBytesPerPixel == 2) {
		pSrcRaster -= 2 * startX;
	} else {
		pSrcRaster -= startX;
	}
	g_sw3dSpanFramebufferRowOffset =
		g_flight16bppBytesPerPixel * g_flightVpX + g_surfacePitch * (scanY + g_flightVpY);

	for (span = g_scanlineSpanHeads[scanY]; span != NULL; span = span->next) {
		SceneFace* face;
		int spanEnd;
		float spanDepth;
		float deltaX;
		float depthFalloff;

		spanEnd = span->xEnd;
		if (drawX >= spanEnd) {
			continue;
		}
		if (span->xStart > drawX) {
			break;
		}
		face = span->face;
		if (depth <= face->minVertW) {
			drawX = spanEnd;
			if (endX <= spanEnd) {
				return;
			}
		} else if (depth < face->maxVertW) {
			spanDepth = (float)scanY * face->gradients[7] + face->gradients[8];
			spanDepth = (float)drawX * face->gradients[6] + spanDepth;
			if (depth <= spanDepth) {
				if (face->gradients[6] >= 0.0f) {
					drawX = spanEnd;
					if (endX <= spanEnd) {
						return;
					}
				} else {
					if (endX > spanEnd) {
						deltaX = (float)(spanEnd - drawX);
						spanDepth = deltaX * face->gradients[6] + spanDepth;
						if (depth <= spanDepth) {
							drawX = spanEnd;
							continue;
						}
					} else {
						deltaX = (float)(endX - drawX);
						spanDepth = deltaX * face->gradients[6] + spanDepth;
						if (depth <= spanDepth) {
							return;
						}
					}
					depthFalloff = -face->gradients[6];
					drawX += (int)(deltaX - (depth - spanDepth) / depthFalloff);
					if (endX <= drawX) {
						return;
					}
				}
			} else if (face->gradients[6] > 0.0f) {
				if (endX >= spanEnd) {
					deltaX = (float)(spanEnd - drawX);
					spanDepth = deltaX * face->gradients[6] + spanDepth;
					if (depth >= spanDepth) {
						continue;
					}
				} else {
					deltaX = (float)(endX - drawX);
					spanDepth = deltaX * face->gradients[6] + spanDepth;
					if (depth >= spanDepth) {
						continue;
					}
				}
				depthFalloff = -face->gradients[6];
				endX = drawX + (int)(deltaX - (depth - spanDepth) / depthFalloff);
				if (endX <= drawX) {
					return;
				}
			}
		}
	}

	while (span != NULL) {
		if (endX <= span->xStart) {
			break;
		}
		if (depth <= span->face->minVertW) {
			sw3d_CopySpanToFramebuffer(pSrcRaster, drawX, span->xStart - drawX);
			drawX = span->xEnd;
			if (endX <= drawX) {
				return;
			}
		} else if (depth < span->face->maxVertW) {
			float spanDepth;

			spanDepth = (float)scanY * span->face->gradients[7] + span->face->gradients[8];
			spanDepth = (float)span->xStart * span->face->gradients[6] + spanDepth;
			if (depth <= spanDepth) {
				sw3d_CopySpanToFramebuffer(pSrcRaster, drawX, span->xStart - drawX);
				drawX = span->xEnd;
				if (span->face->gradients[6] >= 0.0f) {
					if (endX <= drawX) {
						return;
					}
				} else if (endX > drawX) {
					spanDepth = (float)(drawX - span->xStart) * span->face->gradients[6] + spanDepth;
					if (depth > spanDepth) {
						float depthFalloff;

						depthFalloff = -span->face->gradients[6];
						drawX -= (int)((depth - spanDepth) / depthFalloff);
					}
				} else {
					float depthFalloff;

					spanDepth = (float)(endX - span->xStart) * span->face->gradients[6] + spanDepth;
					if (depth <= spanDepth) {
						return;
					}
					depthFalloff = -span->face->gradients[6];
					drawX = endX - (int)((depth - spanDepth) / depthFalloff);
					if (endX <= drawX) {
						return;
					}
				}
			} else if (span->face->gradients[6] > 0.0f) {
				if (endX > span->xEnd) {
					spanDepth = (float)(span->xEnd - span->xStart) * span->face->gradients[6] + spanDepth;
					if (depth < spanDepth) {
						float depthFalloff;

						depthFalloff = -span->face->gradients[6];
						sw3d_CopySpanToFramebuffer(pSrcRaster, drawX,
												   span->xEnd - (int)((depth - spanDepth) / depthFalloff) -
													   drawX);
						drawX = span->xEnd;
					}
				} else {
					float deltaX;

					deltaX = (float)(endX - span->xStart);
					spanDepth = deltaX * span->face->gradients[6] + spanDepth;
					if (depth < spanDepth) {
						float depthFalloff;

						depthFalloff = -span->face->gradients[6];
						sw3d_CopySpanToFramebuffer(
							pSrcRaster, drawX,
							span->xStart + (int)(deltaX - (depth - spanDepth) / depthFalloff) - drawX);
						return;
					}
				}
			}
		}
		span = span->next;
	}

	sw3d_CopySpanToFramebuffer(pSrcRaster, drawX, endX - drawX);
}

// FUNCTION: XVT 0x497D80
void sw3d_CopySpanToFramebuffer(const uint8_t* pSrcRasterBase, int startX, int pixelCount) {
	if (pixelCount > 0) {
		if (g_flight16bppBytesPerPixel == 2) {
			uint8_t* dst = (uint8_t*)g_surfacePixels + g_sw3dSpanFramebufferRowOffset + startX + startX;

			pSrcRasterBase += 2 * startX;
			do {
				uint8_t lo = pSrcRasterBase[0];
				uint8_t hi = pSrcRasterBase[1];
				pSrcRasterBase += 2;
				dst[0] = lo;
				dst[1] = hi;
				dst += 2;
			} while (--pixelCount != 0);
		} else {
			uint8_t* dst = (uint8_t*)g_surfacePixels + startX + g_sw3dSpanFramebufferRowOffset;

			pSrcRasterBase += startX;
			do {
				*dst++ = *pSrcRasterBase++;
			} while (--pixelCount != 0);
		}
	}
}

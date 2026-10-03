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
	/* Lighting block row it was worked out for:
	 * g_sw3dCurrentLightSampleCacheStamp at the time. */
	int stamp;
	/* Light, 0 to 1, at its block corner on that block row's top row. */
	float intensity;
	/* Light at the corner one block lower, less intensity. */
	float rowDelta;
} SoftwareLightSample;

/* g_sw3dLightSampleBlockSize - 1, 15. RenderScene_AllocateBuffers sets the five
 * lighting block globals. */
// GLOBAL: XVT 0x612284
int g_sw3dLightSampleBlockMask = 0;
/* Face sw3d_DrawVisibleFacesToSurface is drawing; sw3d_DrawTexturedSpan draws
 * it. */
// GLOBAL: XVT 0x612280
SceneFace *g_sw3dCurrentFace = NULL;
/* Shade fraction carried from pixel to pixel, the low 8 bits of shade + carry,
 * which dithers between the 16 shade levels; each span starts it from
 * g_sw3dShadeDitherInitialByScanlineParity by its row's parity. */
// GLOBAL: XVT 0x612288
int g_sw3dSpanShadeDitherAccum = 0;
/* Side in pixels of a lighting block, 16 (set by RenderScene_AllocateBuffers):
 * the software renderer works out light at block corners and interpolates
 * between them. */
// GLOBAL: XVT 0x61228C
int g_sw3dLightSampleBlockSize = 0;
/* Byte offset in g_surfacePixels of the current row's first viewport pixel:
 * (row + g_flightVpY) * g_surfacePitch + g_flightVpX * g_flightBytesPerPixel.
 * Set by sw3d_DrawVisibleFacesToSurface for each row and by
 * sw3d_BlitOccludedSpan. */
// GLOBAL: XVT 0x612294
int g_sw3dSpanFramebufferRowOffset = 0;
/* The current row's place in its lighting block: row within the
 * block / g_sw3dLightSampleBlockSize, 0 at the block's top row.
 * sw3d_DrawVisibleFacesToSurface sets it, with the two other row
 * globals, for each row with a span. */
// GLOBAL: XVT 0x61229C
float g_sw3dLightSampleSubrowLerpT = 0.0f;
/* Only the modern build's RenderScene_Initialize writes it, clearing its 0x300
 * bits; nothing reads it. */
// GLOBAL: XVT 0x612298
uint32_t g_sw3dFpuControlWordScratch = 0;
/* FPU control word the original build's RenderScene_Initialize saves before it
 * sets single precision; nothing reads it. */
// GLOBAL: XVT 0x6122A0
uint32_t g_sw3dInitializeSceneSavedFpuControl = 0;
/* Rows from the current one to the next lighting block's top, as a float; set
 * with g_sw3dLightSampleSubrowLerpT. */
// GLOBAL: XVT 0x6122B0
float g_sw3dLightSampleRowsToNextBlockFloat = 0.0f;
/* The current row's offset within its lighting block, as a float; set with
 * g_sw3dLightSampleSubrowLerpT. */
// GLOBAL: XVT 0x6122B4
float g_sw3dLightSampleSubrowFloat = 0.0f;
/* Texture v of the next pixel, in texels with 8 fraction bits;
 * sw3d_DrawTexturedSpan sets it at each lighting block boundary and the pixel
 * loops step it. */
// GLOBAL: XVT 0x6122A8
int g_sw3dSpanVQ8 = 0;
/* Texture u of the next pixel, in texels with 8 fraction bits; set and stepped
 * like g_sw3dSpanVQ8. */
// GLOBAL: XVT 0x6122AC
int g_sw3dSpanUQ8 = 0;
/* 1 / g_sw3dLightSampleBlockSize, taken from g_sw3dSpanLengthReciprocal[16]. */
// GLOBAL: XVT 0x6122B8
float g_sw3dLightSampleInvBlockSize = 0.0f;
/* Change in g_sw3dSpanShadeQ8 per pixel. */
// GLOBAL: XVT 0x6122C4
int g_sw3dSpanShadeStepQ8 = 0;
/* Viewport row sw3d_DrawVisibleFacesToSurface is drawing. */
// GLOBAL: XVT 0x6122C8
int g_sw3dCurrentScanlineY = 0;
/* Pixels in the piece of the span being drawn: to the next lighting block
 * boundary, a whole block, or to the span's end. */
// GLOBAL: XVT 0x6122CC
int g_sw3dSpanLength = 0;
/* Viewport column of the first pixel of the piece being drawn. */
// GLOBAL: XVT 0x6122D0
int g_sw3dSpanStartX = 0;
/* Stamp of the current row's lighting block row,
 * g_sw3dLightSampleCacheSceneStampBase + (row >> g_sw3dLightSampleBlockShift):
 * a light sample with this stamp is current, and one stamped 1 less is from the
 * block row above. */
// GLOBAL: XVT 0x6122D4
int g_sw3dCurrentLightSampleCacheStamp = 0;
/* Mesh of the face being drawn; sw3d_DrawVisibleFacesToSurface writes it and
 * nothing reads it. */
// GLOBAL: XVT 0x612B58
SceneMesh *g_sw3dSpanSceneMesh = NULL;
/* Width of the texture level being drawn, as a float; nothing reads it. */
// GLOBAL: XVT 0x612B5C
float g_sw3dSpanTextureWidthFloat = 0.0f;
/* Height of the texture level being drawn, as a float; nothing reads it. */
// GLOBAL: XVT 0x612B60
float g_sw3dSpanTextureHeightFloat = 0.0f;
/* Width shift of the texture level being drawn, from
 * g_sw3dTextureShiftBySizeDiv16: log2 of the width for powers of two from 8 to
 * 1024. */
// GLOBAL: XVT 0x612B64
int g_sw3dSpanTextureWidthShift = 0;
/* Height shift of the texture level being drawn, found the same way. */
// GLOBAL: XVT 0x612B68
int g_sw3dSpanTextureHeightShift = 0;
/* The face's mesh palette used as shade tables: 16 levels of 256 8-bit pixels,
 * and from byte 4096 16 levels of 256 16-bit pixels. */
// GLOBAL: XVT 0x612B6C
uint8_t *g_sw3dSpanShadeTable = 0;
/* Texels of the texture level being drawn: the mesh's texels past the larger
 * levels skipped. */
// GLOBAL: XVT 0x612B70
uint8_t *g_sw3dSpanTexels = 0;
/* width * height - 1 of the texture level being drawn; the general path masks
 * texel indexes with it. */
// GLOBAL: XVT 0x612B74
int g_sw3dSpanTexelMask = 0;
/* Shade of the next pixel with 8 fraction bits, kept to 0 to 0xEFF at each
 * block boundary: the level drawn is ((shade + carry) >> 8) & 15. */
// GLOBAL: XVT 0x612B80
int g_sw3dSpanShadeQ8 = 0;
/* Change in g_sw3dSpanVQ8 per pixel. */
// GLOBAL: XVT 0x612B84
int g_sw3dSpanStepVQ8 = 0;
/* Change in g_sw3dSpanUQ8 per pixel. */
// GLOBAL: XVT 0x612B88
int g_sw3dSpanStepUQ8 = 0;
/* Base of the light sample stamps: RenderScene_Initialize adds g_flightVpHeight
 * to it each frame, so samples from earlier frames no longer match. */
// GLOBAL: XVT 0x612ADC
int g_sw3dLightSampleCacheSceneStampBase = 0;
/* 16.0, g_sw3dLightSampleBlockSize as a float. */
// GLOBAL: XVT 0x612B50
float g_sw3dLightSampleBlockSizeFloat = 0.0f;
/* 4, log2 of g_sw3dLightSampleBlockSize. */
// GLOBAL: XVT 0x612B7C
int g_sw3dLightSampleBlockShift = 0;
/* Stand-in face for the spans RenderScene_Initialize puts where the cockpit
 * covers the view: its depth range is 1.0e32 and its w row (0, 0, 1.0e32), so
 * no real face draws over them. */
// GLOBAL: XVT 0x612AE0
SceneFace g_sw3dCockpitMaskSentinelFace = {0};
/* 1 makes sw3d_InsertSpan leave out odd rows, so the software renderer draws
 * every other row. The Alt+I key flips it (Flight_UpdatePlayerStep in the
 * original build, XvtFlightSim_UpdatePlayerStep in the modern one); flight
 * start and end set 0. */
// GLOBAL: XVT 0x523404
int g_sw3dSkipOddScanlines = 0;
/* Dither carry each span starts with: 0 on even rows, 128 on odd rows. */
// GLOBAL: XVT 0x527370
int g_sw3dShadeDitherInitialByScanlineParity[2] = {0, 128};

/* Shift for a texture side, indexed by (side & ~12) >> 4: log2 of the side for
 * the powers of two from 8 to 1024. */
// GLOBAL: XVT 0x527378
const int g_sw3dTextureShiftBySizeDiv16[65] = {
	3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8,	8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,	9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10,
};

/* 1.0, the numerator of the 1 / w divisions. */
// GLOBAL: XVT 0x527480
const float g_sw3dSpanOneFloat = 1.0f;
/* 15.0: light times this gives the shade level. */
// GLOBAL: XVT 0x527484
const float g_sw3dLightIntensityToShadeScale = 15.0f;
/* 12582912.0, 1.5 * 2^23: added to a float, the sum's bits less these bits give
 * the float rounded to an integer. */
// GLOBAL: XVT 0x527488
const float g_sw3dFloatToIntRoundBias = 12582912.0f;
/* By shift 0 to 11, 1.5 * 2^(15 - shift): added to a value, the sum's bits less
 * these bits give the value times 2^(8 + shift), which turns a texture
 * coordinate of 0 to 1 into texels of a side of 2^shift with 8 fraction bits,
 * and with shift 0 a shade into 8 fraction bits. */
// GLOBAL: XVT 0x5274A8
const float g_sw3dTexCoordBiasByShift[12] = {
	49152.0f, 24576.0f, 12288.0f, 6144.0f, 3072.0f, 1536.0f,
	768.0f,	  384.0f,   192.0f,   96.0f,   48.0f,	24.0f,
};

/* Near-plane vertex sw3d_SetupClippedEdge made for the edge it set up last,
 * NULL when it made none; sw3d_RasterizeMeshFaces clears it before each edge
 * and stores it in the edge's pClipVert. */
// GLOBAL: XVT 0x60F1C4
ProjVertex *g_sw3dGeneratedClipVertex = NULL;
/* Newest near-plane vertex of the face being clipped, made by
 * sw3d_SetupClippedEdge or taken from a shared edge's pClipVert;
 * sw3d_RasterizeMeshFaces clears it for each clipped face. */
// GLOBAL: XVT 0x60F1D0
ProjVertex *g_sw3dLatestClipVertex = NULL;
/* The near-plane vertex before g_sw3dLatestClipVertex; when both are set,
 * sw3d_RasterizeMeshFaces closes the face with an edge between them. */
// GLOBAL: XVT 0x60F1E0
ProjVertex *g_sw3dPreviousClipVertex = NULL;

/* Projects a mesh's visible faces for the software renderer. For each face in
 * g_visFaceList from the mesh's faceBaseIndex it sets the face's texture rows
 * with RenderScene_TransformFaceTextureGradients, then projects each corner's
 * vertex the first time a face uses it, into g_projVertList from
 * g_projVertCount, its slot kept in g_vertexRemap, lit with
 * RenderScene_ComputeVertexLighting. A vertex is turned by viewOrient and
 * moved by viewPos; nearer than g_sw3dUnitFloat it keeps its view x and y with
 * scaledInverseDepth z - g_sw3dUnitFloat, a negative marker, and marks the
 * face's nearClipState -1; otherwise scaledInverseDepth is g_projScaleInt / z
 * and sx and sy that times x and y plus the viewport middle, plus
 * g_projOffsetY for y. Each face's minScaledInverseDepth and
 * maxScaledInverseDepth cover its corners, a near corner counting as
 * g_projScaleInt. For a textured mesh it turns the face's rows into the
 * screen-space rows of u, v and their divisor w (the inverse of the texture
 * frame, scaled by g_invProjScale and moved to the viewport middle) and sets
 * texelsPerPixelQ8 to t * a * s^2, with t = width * height << 8,
 * a = |gradients[0] * gradients[4] - gradients[3] * gradients[1]| and
 * s = g_projScaleInt * n / the sum of the n corners' scaledInverseDepth. Adds
 * the vertices made to g_projVertCount. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x470300
void sw3d_ProjectMeshVertices(SceneMesh *mesh)
{
	SceneFace *face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertex *output;
	int vertexIndex;
	int faceIndex;

	mesh->vertBaseIndex = g_projVertCount;
	output = &g_projVertList[mesh->vertBaseIndex];
	mesh->projVertCursor = 0;
	for (vertexIndex = 0; vertexIndex < mesh->vertexCount; ++vertexIndex) {
		g_vertexRemap[vertexIndex] = -1;
	}
	for (faceIndex = 0; faceIndex < mesh->visFaceCount;
	     ++faceIndex, ++face) {
		OptVector transformed;
		const FaceRecord *geometry;
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

		RenderScene_TransformFaceTextureGradients(
			face, &mesh->pFaceTexturing[face->faceIndex],
			&mesh->viewPosX);
		geometry = &mesh->pFaceGeom[face->faceIndex];
		face->maxScaledInverseDepth = 0.0f;
		totalW = 0.0f;
		face->minScaledInverseDepth =
			(float)(unsigned int)g_projScaleInt;
		for (cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
			const int modelVertexIndex =
				geometry->vertexIdx[cornerIndex];
			const int normalIndex =
				geometry->normalIdx[cornerIndex];
			int remappedVertex;
			float vertexW;

			if (modelVertexIndex == -1) {
				break;
			}
			remappedVertex = g_vertexRemap[modelVertexIndex];
			if (remappedVertex == -1) {
				g_vertexRemap[modelVertexIndex] =
					mesh->projVertCursor;
				++mesh->projVertCursor;
				transformed.x =
					mesh->pModelVerts[modelVertexIndex].x;
				transformed.y =
					mesh->pModelVerts[modelVertexIndex].y;
				transformed.z =
					mesh->pModelVerts[modelVertexIndex].z;
				Math3D_RotateVec3(&transformed.x,
						  mesh->viewOrient);
				transformed.x += mesh->viewPosX;
				transformed.y += mesh->viewPosY;
				transformed.z += mesh->viewPosZ;
				if (transformed.z < g_sw3dUnitFloat) {
					output->scaledInverseDepth =
						transformed.z - g_sw3dUnitFloat;
					output->sx = transformed.x;
					output->sy = transformed.y;
					face->nearClipState = -1;
					vertexW = (float)(unsigned int)
						g_projScaleInt;
				} else {
					output->scaledInverseDepth =
						(float)(unsigned int)
							g_projScaleInt /
						transformed.z;
					output->sx =
						output->scaledInverseDepth *
						transformed.x;
					output->sy =
						output->scaledInverseDepth *
						transformed.y;
					output->sx +=
						(float)(g_flightVpWidth >> 1);
					output->sy +=
						(float)(g_projOffsetY +
							(g_flightVpHeight >>
							 1));
					vertexW = output->scaledInverseDepth;
				}
				RenderScene_ComputeVertexLighting(
					mesh, output,
					&mesh->pVertNormals[normalIndex],
					&mesh->pModelVerts[modelVertexIndex],
					&g_meshEyePos);
				++output;
			} else {
				const ProjVertex *projected =
					&g_projVertList[mesh->vertBaseIndex +
							remappedVertex];
				if (projected->scaledInverseDepth < 0.0f) {
					face->nearClipState = -1;
					vertexW = (float)(unsigned int)
						g_projScaleInt;
				} else {
					vertexW = projected->scaledInverseDepth;
				}
			}
			totalW += vertexW;
			if (face->maxScaledInverseDepth < vertexW) {
				face->maxScaledInverseDepth = vertexW;
			}
			if (face->minScaledInverseDepth > vertexW) {
				face->minScaledInverseDepth = vertexW;
			}
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
			face->gradients[6] =
				transformed.x -
				mesh->pUVs[uvIndex].v * face->gradients[3] -
				mesh->pUVs[uvIndex].u * face->gradients[0];
			face->gradients[7] =
				transformed.y -
				mesh->pUVs[uvIndex].v * face->gradients[4] -
				mesh->pUVs[uvIndex].u * face->gradients[1];
			face->gradients[8] =
				transformed.z -
				mesh->pUVs[uvIndex].v * face->gradients[5] -
				mesh->pUVs[uvIndex].u * face->gradients[2];
			c00 = face->gradients[8] * face->gradients[4] -
			      face->gradients[5] * face->gradients[7];
			c01 = face->gradients[5] * face->gradients[6] -
			      face->gradients[8] * face->gradients[3];
			c02 = face->gradients[7] * face->gradients[3] -
			      face->gradients[4] * face->gradients[6];
			c10 = face->gradients[2] * face->gradients[7] -
			      face->gradients[8] * face->gradients[1];
			c11 = face->gradients[8] * face->gradients[0] -
			      face->gradients[2] * face->gradients[6];
			c12 = face->gradients[6] * face->gradients[1] -
			      face->gradients[7] * face->gradients[0];
			c20 = face->gradients[5] * face->gradients[1] -
			      face->gradients[2] * face->gradients[4];
			c21 = face->gradients[2] * face->gradients[3] -
			      face->gradients[5] * face->gradients[0];
			c22 = face->gradients[4] * face->gradients[0] -
			      face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f) {
				c22 = 1.0f;
			}
			inverse =
				g_sw3dUnitFloat / (c21 * face->gradients[7] +
						   (c20 * face->gradients[6] +
						    c22 * face->gradients[8]));
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
			face->gradients[2] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[0];
			face->gradients[2] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[1];
			face->gradients[5] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[3];
			face->gradients[5] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[4];
			face->gradients[8] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[6];
			face->gradients[8] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[7];
			area = face->gradients[0] * face->gradients[4] -
			       face->gradients[3] * face->gradients[1];
			if (area < g_sw3dZeroFloat) {
				area = -area;
			}
			{
				const OptTextureData *material =
					(const OptTextureData *)mesh->pMaterial;
				float lodScale;

				/* totalW, the sum of the corners' w values, becomes the corner count over that sum: the
				 * reciprocal of their mean. */
				if (geometry->vertexIdx[3] == -1) {
					totalW = g_sw3dTriangleCornerCount /
						 totalW;
				} else {
					totalW = g_sw3dQuadCornerCount / totalW;
				}
				lodScale = (float)(unsigned int)g_projScaleInt *
					   totalW;
				face->texelsPerPixelQ8 =
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      (area * (lodScale * lodScale)));
			}
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

/* Projects a distant mesh's visible faces the way
 * sw3d_ProjectMeshVertices does, but every vertex is pushed
 * g_sw3dDistantDepth further away and projected with
 * g_projScaleInt / viewPosZ * g_sw3dDistantDepth as its
 * scale, with no near test, and texelsPerPixelQ8 is
 * (width * height << 8) * |gradients[4] * gradients[0]| * z^2
 * plus the same with gradients[1] * gradients[3], each cut to
 * an integer, z being the first corner's pushed depth. */
// FUNCTION: XVT 0x4709C0
void sw3d_ProjectMeshVerticesDistant(SceneMesh *mesh)
{
	const float projectionScale = (float)(unsigned int)g_projScaleInt /
				      mesh->viewPosZ * g_sw3dDistantDepth;
	SceneFace *face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertex *output;
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
	for (faceIndex = 0; faceIndex < mesh->visFaceCount;
	     ++faceIndex, ++face) {
		const FaceRecord *geometry;
		int cornerIndex;

		RenderScene_TransformFaceTextureGradients(
			face, &mesh->pFaceTexturing[face->faceIndex],
			&mesh->viewPosX);
		geometry = &mesh->pFaceGeom[face->faceIndex];
		face->maxScaledInverseDepth = 0.0f;
		face->minScaledInverseDepth =
			(float)(unsigned int)g_projScaleInt;
		for (cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
			OptVector transformed;
			const int modelVertexIndex =
				geometry->vertexIdx[cornerIndex];
			const int normalIndex =
				geometry->normalIdx[cornerIndex];
			int remappedVertex;
			float vertexW;

			if (modelVertexIndex == -1) {
				break;
			}
			remappedVertex = g_vertexRemap[modelVertexIndex];
			if (remappedVertex == -1) {
				g_vertexRemap[modelVertexIndex] =
					mesh->projVertCursor;
				++mesh->projVertCursor;
				transformed.x =
					mesh->pModelVerts[modelVertexIndex].x;
				transformed.y =
					mesh->pModelVerts[modelVertexIndex].y;
				transformed.z =
					mesh->pModelVerts[modelVertexIndex].z;
				Math3D_RotateVec3(&transformed.x,
						  mesh->viewOrient);
				transformed.x += mesh->viewPosX;
				transformed.y += mesh->viewPosY;
				transformed.z += mesh->viewPosZ;
				transformed.z += g_sw3dDistantDepth;
				output->scaledInverseDepth =
					projectionScale / transformed.z;
				output->sx = output->scaledInverseDepth *
					     transformed.x;
				output->sy = output->scaledInverseDepth *
					     transformed.y;
				output->sx += (float)(g_flightVpWidth >> 1);
				output->sy += (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1));
				vertexW = output->scaledInverseDepth;
				RenderScene_ComputeVertexLighting(
					mesh, output,
					&mesh->pVertNormals[normalIndex],
					&mesh->pModelVerts[modelVertexIndex],
					&g_meshEyePos);
				++output;
			} else {
				vertexW = g_projVertList[mesh->vertBaseIndex +
							 remappedVertex]
						  .scaledInverseDepth;
			}
			if (face->maxScaledInverseDepth < vertexW) {
				face->maxScaledInverseDepth = vertexW;
			}
			if (face->minScaledInverseDepth > vertexW) {
				face->minScaledInverseDepth = vertexW;
			}
		}

		if (mesh->pUVs != NULL) {
			OptVector transformed;
			const int uvIndex = geometry->uvIdx[0];
			const OptVector *modelVertex =
				&mesh->pModelVerts[geometry->vertexIdx[0]];
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
			face->gradients[6] =
				transformed.x -
				mesh->pUVs[uvIndex].v * face->gradients[3] -
				mesh->pUVs[uvIndex].u * face->gradients[0];
			face->gradients[7] =
				transformed.y -
				mesh->pUVs[uvIndex].v * face->gradients[4] -
				mesh->pUVs[uvIndex].u * face->gradients[1];
			face->gradients[8] =
				transformed.z -
				mesh->pUVs[uvIndex].v * face->gradients[5] -
				mesh->pUVs[uvIndex].u * face->gradients[2];
			c00 = face->gradients[8] * face->gradients[4] -
			      face->gradients[5] * face->gradients[7];
			c01 = face->gradients[5] * face->gradients[6] -
			      face->gradients[8] * face->gradients[3];
			c02 = face->gradients[7] * face->gradients[3] -
			      face->gradients[4] * face->gradients[6];
			c10 = face->gradients[2] * face->gradients[7] -
			      face->gradients[8] * face->gradients[1];
			c11 = face->gradients[8] * face->gradients[0] -
			      face->gradients[2] * face->gradients[6];
			c12 = face->gradients[1] * face->gradients[6] -
			      face->gradients[7] * face->gradients[0];
			c20 = face->gradients[5] * face->gradients[1] -
			      face->gradients[2] * face->gradients[4];
			c21 = face->gradients[2] * face->gradients[3] -
			      face->gradients[5] * face->gradients[0];
			c22 = face->gradients[4] * face->gradients[0] -
			      face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f) {
				c22 = 1.0f;
			}
			inverse = g_sw3dUnitFloat / (c20 * face->gradients[6] +
						     c21 * face->gradients[7] +
						     c22 * face->gradients[8]);
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
			face->gradients[2] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[0];
			face->gradients[2] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[1];
			face->gradients[5] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[3];
			face->gradients[5] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[4];
			face->gradients[8] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[6];
			face->gradients[8] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[7];
			{
				const OptTextureData *material =
					(const OptTextureData *)mesh->pMaterial;
				float mipValue;
				mipValue = face->gradients[4] *
					   face->gradients[0] * transformed.z *
					   transformed.z;
				if (mipValue < g_sw3dZeroFloat) {
					mipValue = -mipValue;
				}
				face->texelsPerPixelQ8 =
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      mipValue);
				mipValue = face->gradients[1] *
					   face->gradients[3] * transformed.z *
					   transformed.z;
				if (mipValue < g_sw3dZeroFloat) {
					mipValue = -mipValue;
				}
				face->texelsPerPixelQ8 +=
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      mipValue);
			}
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

/* Builds the screen edges of a mesh's visible faces in g_sceneEdgeList from
 * g_sceneEdgeCursor and scan-converts each face. A model edge is set up once
 * and shared: g_sceneEdgeFlags maps it to the edge made for it, -1 before and
 * -2 when rejected. A face has 4 corners, or 3 when its last edge index is -1.
 * A face marked for near clipping (nearClipState -1) goes through
 * sw3d_SetupClippedEdge and gets a closing edge between its last two near-plane
 * vertices; any other face goes through sw3d_SetupEdge. Sets each face's
 * nearClipState to g_flightVpHeight, its edges and edgeCount, and calls
 * sw3d_ScanConvertFace, or sets yTop and yBot to 0 for a face left with no
 * edge. Records the mesh's edgeBaseIndex and emittedEdgeCount and advances
 * g_sceneEdgeCursor by the edges made. */
// FUNCTION: XVT 0x471020
void sw3d_RasterizeMeshFaces(SceneMesh *mesh)
{
	enum {
		SW3D_INVALID_EDGE = -1,
		SW3D_REJECTED_EDGE = -2,
		SW3D_FACE_NEEDS_NEAR_CLIP = -1,
	};

	ProjVertex *vertices = &g_projVertList[mesh->vertBaseIndex];
	SceneFace *faceCursor = &g_visFaceList[mesh->faceBaseIndex];
	int sceneEdgeCursor = g_sceneEdgeCursor;
	SceneFace *face;
	SceneEdge *outputEdge;
	SceneEdge *firstEdge;
	int edgeIndex;
	const int edgeCount = mesh->edgeCount;
	int faceIndex;
	int outputCount;

	mesh->edgeBaseIndex = sceneEdgeCursor;
	mesh->emittedEdgeCount = 0;
	firstEdge = outputEdge = &g_sceneEdgeList[sceneEdgeCursor];
	if (edgeCount > 0) {
		for (edgeIndex = 0; edgeIndex < mesh->edgeCount; ++edgeIndex) {
			g_sceneEdgeFlags[edgeIndex] = SW3D_INVALID_EDGE;
		}
	}

	for (faceIndex = 0; faceIndex < mesh->visFaceCount; ++faceIndex) {
		const FaceRecord *record;
		int cornerCount;
		int currentCorner;
		int previousCorner;

		outputCount = 0;
		record = &mesh->pFaceGeom[faceCursor->faceIndex];
		face = faceCursor;
		++faceCursor;

		if (face->nearClipState == SW3D_FACE_NEEDS_NEAR_CLIP) {
			face->nearClipState = g_flightVpHeight;
			g_sw3dLatestClipVertex = NULL;
			g_sw3dPreviousClipVertex = NULL;
			cornerCount =
				record->edgeIdx[(sizeof(record->edgeIdx) /
						 sizeof(record->edgeIdx[0])) -
						1] != SW3D_INVALID_EDGE
					? (int)(sizeof(record->edgeIdx) /
						sizeof(record->edgeIdx[0]))
					: (int)(sizeof(record->edgeIdx) /
						sizeof(record->edgeIdx[0])) -
						  1;
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
						    mesh, outputEdge,
						    &vertices
							    [g_vertexRemap
								     [record->vertexIdx
									      [previousCorner]]],
						    &vertices
							    [g_vertexRemap
								     [record->vertexIdx
									      [currentCorner]]]) >=
					    0) {
						face->edges[outputCount++] =
							outputEdge;
						outputEdge->pClipVert =
							g_sw3dGeneratedClipVertex;
						g_sceneEdgeFlags[sourceEdge] =
							mesh->emittedEdgeCount;
						++mesh->emittedEdgeCount;
						++outputEdge;
					} else if (g_sw3dGeneratedClipVertex ==
						   NULL) {
						g_sceneEdgeFlags[sourceEdge] =
							SW3D_REJECTED_EDGE;
					}
				} else if (existingEdge != SW3D_REJECTED_EDGE) {
					SceneEdge *edge;

					edge = &firstEdge[existingEdge];
					face->edges[outputCount++] = edge;
					if (edge->pClipVert != NULL) {
						g_sw3dPreviousClipVertex =
							g_sw3dLatestClipVertex;
						g_sw3dLatestClipVertex =
							edge->pClipVert;
					}
				}
				currentCorner = previousCorner;
				if (previousCorner <= 0) {
					break;
				}
			}

			if (g_sw3dPreviousClipVertex != NULL) {
				if (sw3d_SetupClippedEdge(
					    mesh, outputEdge,
					    g_sw3dLatestClipVertex,
					    g_sw3dPreviousClipVertex) >= 0) {
					face->edges[outputCount++] = outputEdge;
					++mesh->emittedEdgeCount;
					++outputEdge;
				}
			}
		} else {
			face->nearClipState = g_flightVpHeight;
			cornerCount =
				record->edgeIdx[(sizeof(record->edgeIdx) /
						 sizeof(record->edgeIdx[0])) -
						1] != SW3D_INVALID_EDGE
					? (int)(sizeof(record->edgeIdx) /
						sizeof(record->edgeIdx[0]))
					: (int)(sizeof(record->edgeIdx) /
						sizeof(record->edgeIdx[0])) -
						  1;
			currentCorner = 0;
			for (previousCorner = cornerCount;;) {
				int sourceEdge;
				int existingEdge;

				--previousCorner;
				sourceEdge = record->edgeIdx[previousCorner];
				existingEdge = g_sceneEdgeFlags[sourceEdge];
				if (existingEdge == SW3D_INVALID_EDGE) {
					if (sw3d_SetupEdge(
						    outputEdge,
						    &vertices
							    [g_vertexRemap
								     [record->vertexIdx
									      [previousCorner]]],
						    &vertices
							    [g_vertexRemap
								     [record->vertexIdx
									      [currentCorner]]]) >=
					    0) {
						face->edges[outputCount++] =
							outputEdge;
						g_sceneEdgeFlags[sourceEdge] =
							mesh->emittedEdgeCount;
						++mesh->emittedEdgeCount;
						++outputEdge;
					} else {
						g_sceneEdgeFlags[sourceEdge] =
							SW3D_REJECTED_EDGE;
					}
				} else if (existingEdge != SW3D_REJECTED_EDGE) {
					face->edges[outputCount++] =
						&firstEdge[existingEdge];
				}
				currentCorner = previousCorner;
				if (previousCorner <= 0) {
					break;
				}
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
	g_sceneEdgeCursor += mesh->emittedEdgeCount;
}

/* Turns a face's edges into spans, row by row, with sw3d_InsertSpan. Sets yTop
 * to the smallest yStart and yBot to the largest yEnd, and takes yBot - yTop
 * span pointers from the end of g_sceneSpanPtrList for pSpans; when that many
 * are not left it sets yBot to yTop and adds nothing. It walks a left and a
 * right edge down from the top, moving to the edge that starts where one ends,
 * and for each row inserts the span between them with spanLightIntensityDx, the
 * light change per pixel across it; a face with no second edge starting at its
 * top row, or with no edge starting where one ends, stops there with yBot at
 * that row. pScanEdge points at the left edge. The edges' x and light are put
 * back at the end. */
// FUNCTION: XVT 0x471410
void sw3d_ScanConvertFace(SceneFace *face)
{
	SceneEdge *left;
	SceneEdge *right;
	SceneEdge *edge;
	SceneEdge *swapEdge;
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
		if (left->yStart > edge->yStart) {
			left = edge;
		}
		if (right->yEnd < edge->yEnd) {
			right = edge;
		}
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
			if (right->x < left->x ||
			    (right->x == left->x && right->dxdy < left->dxdy)) {
				swapEdge = left;
				left = right;
				right = swapEdge;
			}

			face->pScanEdge = left;
			scanY = left->yStart;
			runEnd = right->yEnd;
			if (right->yEnd > left->yEnd) {
				runEnd = left->yEnd;
			}
			runRows = runEnd - scanY;
			leftStartX = left->x;
			rightStartX = right->x;
			leftStartLight = left->lightIntensity;
			rightStartLight = right->lightIntensity;

			if (rightStartX - leftStartX > g_sw3dUnitFloat) {
				do {
					--runRows;
					face->spanLightIntensityDx =
						(right->lightIntensity -
						 left->lightIntensity) /
						(right->x - left->x);
					sw3d_InsertSpan(left->x, right->x,
							scanY++, face);
					if (runRows <= 0) {
						break;
					}
					left->x += left->dxdy;
					left->lightIntensity +=
						left->dLightIntensityDy;
					right->x += right->dxdy;
					right->lightIntensity +=
						right->dLightIntensityDy;
				} while (1);
			} else {
				runRowsFloat = (float)runRows;
				face->spanLightIntensityDx =
					(runRowsFloat *
						 right->dLightIntensityDy +
					 right->lightIntensity -
					 (runRowsFloat *
						  left->dLightIntensityDy +
					  left->lightIntensity)) /
					(runRowsFloat * right->dxdy + right->x -
					 (runRowsFloat * left->dxdy + left->x));
				do {
					--runRows;
					sw3d_InsertSpan(left->x, right->x,
							scanY++, face);
					if (runRows <= 0) {
						break;
					}
					left->x += left->dxdy;
					left->lightIntensity +=
						left->dLightIntensityDy;
					right->x += right->dxdy;
					right->lightIntensity +=
						right->dLightIntensityDy;
				} while (1);
			}

			while (remainingEdges > 0) {
				if (right->yEnd != left->yEnd) {
					--remainingEdges;
					if (scanY == left->yEnd) {
						left->x = leftStartX;
						left->lightIntensity =
							leftStartLight;
						for (edgeIndex = 0;
						     edgeIndex <
						     face->edgeCount;
						     ++edgeIndex) {
							if (face->edges[edgeIndex]
								    ->yStart ==
							    scanY) {
								break;
							}
						}
						if (edgeIndex ==
						    face->edgeCount) {
							face->yBot = scanY;
							break;
						}
						left = face->edges[edgeIndex];
						face->pScanEdge = left;
						leftStartX = left->x;
						leftStartLight =
							left->lightIntensity;
						right->x += right->dxdy;
						right->lightIntensity +=
							right->dLightIntensityDy;
					} else {
						right->x = rightStartX;
						right->lightIntensity =
							rightStartLight;
						for (edgeIndex = 0;
						     edgeIndex <
						     face->edgeCount;
						     ++edgeIndex) {
							if (face->edges[edgeIndex]
								    ->yStart ==
							    scanY) {
								break;
							}
						}
						if (edgeIndex ==
						    face->edgeCount) {
							face->yBot = scanY;
							break;
						}
						right = face->edges[edgeIndex];
						rightStartX = right->x;
						rightStartLight =
							right->lightIntensity;
						left->x += left->dxdy;
						left->lightIntensity +=
							left->dLightIntensityDy;
					}
				} else {
					remainingEdges -= 2;
					if (remainingEdges == 0) {
						break;
					}

					left->x = leftStartX;
					right->x = rightStartX;
					left->lightIntensity = leftStartLight;
					right->lightIntensity = rightStartLight;
					for (edgeIndex = 0;
					     edgeIndex < face->edgeCount;
					     ++edgeIndex) {
						if (face->edges[edgeIndex]
							    ->yStart == scanY) {
							break;
						}
					}
					if (edgeIndex == face->edgeCount) {
						face->yBot = scanY;
						break;
					}
					left = face->edges[edgeIndex];
					for (++edgeIndex;
					     edgeIndex < face->edgeCount;
					     ++edgeIndex) {
						if (face->edges[edgeIndex]
							    ->yStart == scanY) {
							break;
						}
					}
					if (edgeIndex == face->edgeCount) {
						face->yBot = scanY;
						break;
					}
					right = face->edges[edgeIndex];
					if (right->x < left->x ||
					    (right->x == left->x &&
					     right->dxdy < left->dxdy)) {
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

				if (remainingEdges == 1) {
					return;
				}
				if (remainingEdges == 2) {
					if (right->yEnd != left->yEnd) {
						return;
					}
					runRows = left->yEnd - scanY;
					runRowsFloat = (float)runRows;
					if (right->dxdy * runRowsFloat +
						    right->x -
						    (left->dxdy * runRowsFloat +
						     left->x) >
					    g_sw3dUnitFloat) {
						do {
							--runRows;
							face->spanLightIntensityDx =
								(right->lightIntensity -
								 left->lightIntensity) /
								(right->x -
								 left->x);
							sw3d_InsertSpan(
								left->x,
								right->x,
								scanY++, face);
							if (runRows <= 0) {
								break;
							}
							left->x += left->dxdy;
							left->lightIntensity +=
								left->dLightIntensityDy;
							right->x += right->dxdy;
							right->lightIntensity +=
								right->dLightIntensityDy;
						} while (1);
					} else {
						face->spanLightIntensityDx =
							(right->lightIntensity -
							 left->lightIntensity) /
							(right->x - left->x);
						do {
							--runRows;
							sw3d_InsertSpan(
								left->x,
								right->x,
								scanY++, face);
							if (runRows <= 0) {
								break;
							}
							left->x += left->dxdy;
							left->lightIntensity +=
								left->dLightIntensityDy;
							right->x += right->dxdy;
							right->lightIntensity +=
								right->dLightIntensityDy;
						} while (1);
					}
				} else {
					runEnd = left->yEnd < right->yEnd
							 ? left->yEnd
							 : right->yEnd;
					runRows = runEnd - scanY;
					do {
						--runRows;
						face->spanLightIntensityDx =
							(right->lightIntensity -
							 left->lightIntensity) /
							(right->x - left->x);
						sw3d_InsertSpan(left->x,
								right->x,
								scanY++, face);
						if (runRows <= 0) {
							break;
						}
						left->x += left->dxdy;
						left->lightIntensity +=
							left->dLightIntensityDy;
						right->x += right->dxdy;
						right->lightIntensity +=
							right->dLightIntensityDy;
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

/* sw3d_SetupEdge for an edge whose ends may lie in front of the near plane,
 * marked by a negative scaledInverseDepth. Returns -1 when both ends do. When
 * one does, it makes a vertex where the edge crosses the near plane, taken from
 * the mesh's projected vertices (its projVertCursor and g_projVertCount grow by
 * 1), with scaledInverseDepth g_projScaleInt and its light interpolated, makes
 * it g_sw3dLatestClipVertex and g_sw3dGeneratedClipVertex, the old latest
 * becoming g_sw3dPreviousClipVertex, and sets the edge up from it to the other
 * end. The rest is as sw3d_SetupEdge, a negative sy counting as row 0. */
// FUNCTION: XVT 0x471A10
int sw3d_SetupClippedEdge(SceneMesh *mesh, SceneEdge *edge,
			  const ProjVertex *first, const ProjVertex *second)
{
	const ProjVertex *inside;
	const ProjVertex *outside;
#ifdef XVT_MODERN
	uint32_t coordinateBits;
#endif
	int secondY;
	int firstY;
	const ProjVertex *swapVertex;
	int swapY;
	float inverseHeight;
	float firstRowOffset;

	inside = second;
	outside = first;
#ifdef XVT_MODERN
	memcpy(&coordinateBits, &second->scaledInverseDepth,
	       sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t *)&second->scaledInverseDepth > 0x80000000u) {
#endif
#ifdef XVT_MODERN
		memcpy(&coordinateBits, &first->scaledInverseDepth,
		       sizeof(coordinateBits));
		if (coordinateBits > 0x80000000u)
#else
		if (*(const uint32_t *)&first->scaledInverseDepth > 0x80000000u)
#endif
			return -1;
		outside = second;
		inside = first;
	}

#ifdef XVT_MODERN
	memcpy(&coordinateBits, &outside->scaledInverseDepth,
	       sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t *)&outside->scaledInverseDepth > 0x80000000u) {
#endif
		float insideInverseW;
		float insideX;
		float insideY;
		float clipFraction;
		float clippedY;

		g_sw3dPreviousClipVertex = g_sw3dLatestClipVertex;
		g_sw3dLatestClipVertex = &g_projVertList[mesh->projVertCursor +
							 mesh->vertBaseIndex];
		g_sw3dGeneratedClipVertex = g_sw3dLatestClipVertex;
		++mesh->projVertCursor;
		++g_projVertCount;

		insideInverseW = g_sw3dUnitFloat / inside->scaledInverseDepth;
		insideX = (inside->sx - (float)(g_flightVpWidth >> 1)) *
			  insideInverseW;
		insideY = (inside->sy -
			   (float)(g_projOffsetY + (g_flightVpHeight >> 1))) *
			  insideInverseW;
		clipFraction =
			outside->scaledInverseDepth /
			(outside->scaledInverseDepth -
			 insideInverseW * (float)(unsigned int)g_projScaleInt +
			 g_sw3dUnitFloat);
		clippedY = (insideY - outside->sy) * clipFraction + outside->sy;
		g_sw3dLatestClipVertex->sx =
			((insideX - outside->sx) * clipFraction + outside->sx) *
			(float)(unsigned int)g_projScaleInt;
		g_sw3dLatestClipVertex->sy =
			clippedY * (float)(unsigned int)g_projScaleInt;
		g_sw3dLatestClipVertex->sx = (float)(g_flightVpWidth >> 1) +
					     g_sw3dLatestClipVertex->sx;
		g_sw3dLatestClipVertex->sy =
			(float)(g_projOffsetY + (g_flightVpHeight >> 1)) +
			g_sw3dLatestClipVertex->sy;
		g_sw3dLatestClipVertex->scaledInverseDepth =
			(float)(unsigned int)g_projScaleInt;
		g_sw3dLatestClipVertex->lightIntensity =
			outside->lightIntensity +
			(inside->lightIntensity - outside->lightIntensity) *
				clipFraction;
		outside = g_sw3dLatestClipVertex;
	}

#ifdef XVT_MODERN
	memcpy(&coordinateBits, &outside->sy, sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t *)&outside->sy > 0x80000000u) {
#endif
		firstY = 0;
	} else {
		firstY = (int)outside->sy;
		if ((float)firstY != outside->sy) {
			++firstY;
		}
	}
#ifdef XVT_MODERN
	memcpy(&coordinateBits, &inside->sy, sizeof(coordinateBits));
	if (coordinateBits > 0x80000000u) {
#else
	if (*(const uint32_t *)&inside->sy > 0x80000000u) {
#endif
		secondY = 0;
	} else {
		secondY = (int)inside->sy;
		if ((float)secondY != inside->sy) {
			++secondY;
		}
	}
	if (firstY == secondY) {
		return -1;
	}
	if (firstY > secondY) {
		swapVertex = outside;
		outside = inside;
		inside = swapVertex;
		swapY = firstY;
		firstY = secondY;
		secondY = swapY;
	}
	if (secondY <= 0) {
		return -1;
	}
	if (g_flightVpHeight <= firstY) {
		return -1;
	}
	if (secondY > g_flightVpHeight) {
		secondY = g_flightVpHeight;
	}

	edge->yEnd = secondY;
	inverseHeight = g_sw3dUnitFloat / (inside->sy - outside->sy);
	edge->dxdy = (inside->sx - outside->sx) * inverseHeight;
	edge->dLightIntensityDy =
		(inside->lightIntensity - outside->lightIntensity) *
		inverseHeight;
	edge->pClipVert = NULL;
	firstRowOffset = (float)firstY - outside->sy;
	edge->x = outside->sx + firstRowOffset * edge->dxdy;
	edge->lightIntensity = outside->lightIntensity +
			       firstRowOffset * edge->dLightIntensityDy;
	edge->yStart = firstY;
	return firstY;
}

/* Sets up a screen edge between two projected vertices for scan conversion. Its
 * rows run from the upper end's sy rounded up (0 when negative) to the lower
 * end's, cut at g_flightVpHeight; it stores yStart, yEnd, the x and light at
 * yStart, and their changes per row (dxdy, dLightIntensityDy), and clears
 * pClipVert. Returns yStart, or -1 when the edge covers no row, ends at row 0
 * or above, or starts at g_flightVpHeight or below. */
// FUNCTION: XVT 0x471CE0
int sw3d_SetupEdge(SceneEdge *edge, const ProjVertex *first,
		   const ProjVertex *second)
{
	const ProjVertex *swapVertex;
	int firstY;
	int secondY;
	int swapY;
	float inverseHeight;
	float firstRowOffset;

	if (first->sy < 0.0f) {
		firstY = 0;
	} else {
		firstY = (int)first->sy;
		if ((float)firstY != first->sy) {
			++firstY;
		}
	}

	if (second->sy < 0.0f) {
		secondY = 0;
	} else {
		secondY = (int)second->sy;
		if ((float)secondY != second->sy) {
			++secondY;
		}
	}

	if (firstY == secondY) {
		return -1;
	}
	if (firstY > secondY) {
		swapVertex = first;
		first = second;
		second = swapVertex;
		swapY = firstY;
		firstY = secondY;
		secondY = swapY;
	}
	if (secondY <= 0) {
		return -1;
	}
	if (firstY >= g_flightVpHeight) {
		return -1;
	}
	if (secondY > g_flightVpHeight) {
		secondY = g_flightVpHeight;
	}

	edge->yEnd = secondY;
	inverseHeight = g_sw3dUnitFloat / (second->sy - first->sy);
	edge->dxdy = (second->sx - first->sx) * inverseHeight;
	edge->dLightIntensityDy =
		(second->lightIntensity - first->lightIntensity) *
		inverseHeight;
	edge->pClipVert = NULL;
	firstRowOffset = (float)firstY - first->sy;
	edge->x = first->sx + firstRowOffset * edge->dxdy;
	edge->lightIntensity = first->lightIntensity +
			       firstRowOffset * edge->dLightIntensityDy;
	edge->yStart = firstY;
	return firstY;
}

/* Draws the frame's visible faces from the span buffer onto the flight surface,
 * from g_visFacePassStart to g_visFaceCount; with g_useHardware3D it calls
 * RenderScene_FlushGeometry instead. Clears the face lighting cache first, and
 * locks the surface around the drawing unless g_flightSurfaceAlreadyLocked is
 * set. For each face it picks the texture level: with g_mipmappingEnabled and a
 * texture whose textureSize is width * height, it halves both sides while
 * texelsPerPixelQ8 * g_mipLodScale, quartered at each level, is over 256 and
 * neither side is 8. It sets the span globals for that level and the mesh, then
 * for each of the face's rows with a span sets the lighting block row globals
 * and draws the span with sw3d_DrawTexturedSpan, leaving out the columns of
 * later spans in the row's list that overlap it. */
// FUNCTION: XVT 0x4865E0
void sw3d_DrawVisibleFacesToSurface(void)
{
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
		const OptTextureData *material;
		SceneMesh **pMesh;
		float rowBaseW;
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
		material = (const OptTextureData *)(*pMesh)->pMaterial;
		textureWidth = material->width;
		textureHeight = material->height;
		if (g_mipmappingEnabled != mipTexelOffset &&
		    textureWidth * textureHeight == material->textureSize) {
			int mipMetric = (int)((float)g_sw3dCurrentFace
						      ->texelsPerPixelQ8 *
					      g_mipLodScale);

			while (mipMetric > SW3D_MIP_THRESHOLD_Q8 &&
			       textureWidth != SW3D_MIP_MIN_DIMENSION &&
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
		g_sw3dSpanTextureWidthShift = g_sw3dTextureShiftBySizeDiv16
			[(textureWidth & ~SW3D_TEXTURE_SHIFT_MASK) >>
			 SW3D_TEXTURE_SHIFT_INDEX_SHIFT];
		g_sw3dSpanTexelMask = textureWidth * textureHeight - 1;
		g_sw3dSpanTextureHeightShift = g_sw3dTextureShiftBySizeDiv16
			[textureHeightShiftIndex >>
			 SW3D_TEXTURE_SHIFT_INDEX_SHIFT];
		g_sw3dSpanShadeTable = (*pMesh)->pPalette;
		g_sw3dSpanTexels =
			(uint8_t *)(*pMesh)->pTexels + mipTexelOffset;
		g_sw3dSpanSceneMesh = *pMesh;
		rowBaseW = (float)(unsigned int)g_sw3dCurrentScanlineY *
				   g_sw3dCurrentFace->gradients[7] +
			   g_sw3dCurrentFace->gradients[8];
		g_sw3dSpanFramebufferRowOffset =
			g_surfacePitch *
				(g_sw3dCurrentScanlineY + g_flightVpY) +
			g_flightBytesPerPixel * g_flightVpX;

		for (spanIndex = 0; (unsigned int)g_sw3dCurrentScanlineY <
				    (unsigned int)g_sw3dCurrentFace->yBot;
		     ++spanIndex, ++g_sw3dCurrentScanlineY) {
			SceneFace *rowFace = g_sw3dCurrentFace;
			SceneSpan *span = rowFace->pSpans[spanIndex];

			if (span != NULL) {
				SceneSpan *occluder;
				int sampleSubrow;
				int startX;
				int endX;
				float startXFloat;
				float lightIntensity;

				sampleSubrow = g_sw3dCurrentScanlineY &
					       g_sw3dLightSampleBlockMask;
				if (sampleSubrow != 0) {
					g_sw3dLightSampleSubrowFloat =
						(float)(unsigned int)
							sampleSubrow;
					g_sw3dLightSampleSubrowLerpT =
						g_sw3dLightSampleSubrowFloat *
						g_sw3dSpanLengthReciprocal
							[g_sw3dLightSampleBlockSize];
					g_sw3dLightSampleRowsToNextBlockFloat =
						(float)(unsigned int)(g_sw3dLightSampleBlockSize -
								      sampleSubrow);
				} else {
					g_sw3dLightSampleSubrowLerpT = 0.0f;
					g_sw3dLightSampleSubrowFloat = 0.0f;
					g_sw3dLightSampleRowsToNextBlockFloat =
						(float)(unsigned int)
							g_sw3dLightSampleBlockSize;
				}
				g_sw3dCurrentLightSampleCacheStamp =
					g_sw3dLightSampleCacheSceneStampBase +
					((unsigned int)g_sw3dCurrentScanlineY >>
					 g_sw3dLightSampleBlockShift);

				occluder = span->next;
				startX = span->xStart;
				endX = span->xEnd;
				startXFloat = (float)startX;
				lightIntensity = span->lightIntensity;
				spanStartEdge.lightIntensity = lightIntensity;
				spanStartEdge.x = startXFloat;
				g_sw3dCurrentFace->spanLightIntensityDx =
					span->dLightIntensityDx;
				if (occluder != NULL) {
					do {
						const int occluderStart =
							occluder->xStart;

						if (endX <= occluderStart) {
							break;
						}
						if (startX >= occluderStart) {
							const int occluderEnd =
								occluder->xEnd;

							if (startX <
							    occluderEnd) {
								startX =
									occluder->xEnd;
								if (endX <=
								    occluderEnd) {
									break;
								}
							}
						} else {
							float spanStartW =
								(float)startX *
									g_sw3dCurrentFace
										->gradients
											[6] +
								rowBaseW;

							sw3d_DrawTexturedSpan(
								startX,
								occluderStart,
								spanStartW);
							startX = occluder->xEnd;
							endX = span->xEnd;
							if (endX <= startX) {
								break;
							}
						}
						occluder = occluder->next;
					} while (occluder != NULL);
					if (endX > startX) {
						float spanStartW =
							(float)startX *
								g_sw3dCurrentFace
									->gradients
										[6] +
							rowBaseW;

						sw3d_DrawTexturedSpan(
							startX, endX,
							spanStartW);
					}
				} else {
					float spanStartW =
						g_sw3dCurrentFace
								->gradients[6] *
							startXFloat +
						rowBaseW;

					sw3d_DrawTexturedSpan(startX, endX,
							      spanStartW);
				}
				rowFace = g_sw3dCurrentFace;
			}
			rowBaseW = rowFace->gradients[7] + rowBaseW;
			g_sw3dSpanFramebufferRowOffset += g_surfacePitch;
		}
		++faceIndex;
	}

	if (g_flightSurfaceAlreadyLocked == 0) {
		FlightSurface_Unlock();
	}
}

/* Adds a face's span on row scanY to the row's span list,
 * g_scanlineSpanHeads[scanY], kept in column order, so that each pixel
 * ends with the nearest face. The span runs from xLeft to xRight, each
 * rounded up and negative ones taken as 0, its end cut at g_flightVpWidth;
 * nothing is added when it is empty, starts at g_flightVpWidth or past it,
 * or lies on an odd row while g_sw3dSkipOddScanlines is set. Against each
 * span it overlaps it settles depth by the two faces' depth ranges
 * (minScaledInverseDepth to maxScaledInverseDepth, larger nearer) when
 * those do not overlap, else by their w along the overlap,
 * gradients[6] * x + gradients[7] * y + gradients[8], splitting at the
 * column where those cross. It cuts the new span short, or trims, moves or
 * removes the spans it hides, moving a span's light with its start and
 * clearing the pSpans entry of a span removed. The new span is the next
 * entry at g_pSceneSpanDataCur, the pool's last entry again once it runs
 * out; its light at its start comes from the face's pScanEdge and
 * spanLightIntensityDx, 0 without a scan edge. It is stored in the face's
 * pSpans[scanY - yTop], which the call first clears. */
// FUNCTION: XVT 0x486980
void sw3d_InsertSpan(float xLeft, float xRight, int scanY, SceneFace *face)
{
	SceneSpan *current;
	SceneSpan *next;
	SceneSpan *insertionNext;
	SceneSpan *previous;
	SceneSpan *span;
	float newW;
	float currentW;
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
	if (endX <= startX || startX >= g_flightVpWidth ||
	    (g_sw3dSkipOddScanlines && (scanY & 1))) {
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

		if (face->maxScaledInverseDepth <=
		    current->face->minScaledInverseDepth) {
			startX = current->xEnd;
			if (startX >= endX) {
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}
		if (face->minScaledInverseDepth >=
		    current->face->maxScaledInverseDepth) {
			if (current->xEnd > endX) {
				previous = current;
				current = current->next;
				continue;
			}
			current->xEnd = startX;
			if (current->xEnd == current->xStart) {
				current->face
					->pSpans[scanY - current->face->yTop] =
					NULL;
				if (previous == NULL) {
					g_scanlineSpanHeads[scanY] =
						current->next;
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

		newW = face->gradients[7] * (float)scanY + face->gradients[8] +
		       face->gradients[6] * (float)startX;
		currentW = current->face->gradients[7] * (float)scanY +
			   current->face->gradients[8] +
			   current->face->gradients[6] * (float)startX;
		if (newW <= currentW) {
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
				newW += (float)overlapWidth *
					face->gradients[6];
				currentW += (float)overlapWidth *
					    current->face->gradients[6];
				if (newW <= currentW) {
					startX = current->xEnd;
					previous = current;
					current = current->next;
					continue;
				}
			} else {
				overlapWidth = endX - startX;
				newW += (float)overlapWidth *
					face->gradients[6];
				currentW += (float)overlapWidth *
					    current->face->gradients[6];
				if (newW <= currentW) {
					return;
				}
			}
			currentLeftWidth =
				(int)((float)overlapWidth -
				      (newW - currentW) /
					      (face->gradients[6] -
					       current->face->gradients[6])) +
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
				current->face
					->pSpans[scanY - current->face->yTop] =
					NULL;
				if (previous == NULL) {
					g_scanlineSpanHeads[scanY] =
						current->next;
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
			newW += (float)overlapWidth * face->gradients[6];
			currentW += (float)overlapWidth *
				    current->face->gradients[6];
			if (newW >= currentW) {
				current->xEnd = startX;
				if (current->xEnd == current->xStart) {
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
					if (previous == NULL) {
						g_scanlineSpanHeads[scanY] =
							current->next;
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
				(int)((newW - currentW) /
				      (face->gradients[6] -
				       current->face->gradients[6]));
			if (crossingFromRight < 0) {
				crossingFromRight = 0;
			}
			if (crossingFromRight > overlapWidth) {
				crossingFromRight = overlapWidth;
			}
			crossingFromLeft = overlapWidth - crossingFromRight;
			currentLeftWidth = startX - current->xStart;
			currentRightWidth = endX - current->xEnd;

			if (currentLeftWidth <= crossingFromRight &&
			    currentLeftWidth <= crossingFromLeft &&
			    currentRightWidth >= currentLeftWidth) {
				currentLeftWidth = current->xEnd -
						   current->xStart -
						   crossingFromRight;
				current->xStart += currentLeftWidth;
				current->lightIntensity +=
					(float)currentLeftWidth *
					current->dLightIntensityDx;
				next = current->next;
				if (next == NULL ||
				    next->xStart > current->xStart) {
					break;
				}
				if (previous == NULL) {
					g_scanlineSpanHeads[scanY] = next;
				} else {
					previous->next = next;
				}
				if (current->xStart < next->xEnd) {
					currentLeftWidth =
						next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity +=
						(float)currentLeftWidth *
						current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL &&
				       insertionNext->xStart <
					       current->xStart) {
					if (current->xStart <
					    insertionNext->xEnd) {
						currentLeftWidth =
							insertionNext->xEnd -
							current->xStart;
						current->xStart +=
							currentLeftWidth;
						current->lightIntensity +=
							(float)currentLeftWidth *
							current->dLightIntensityDx;
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
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
				}
				if (previous == NULL) {
					current = g_scanlineSpanHeads[scanY];
				} else {
					current = previous->next;
				}
				continue;
			} else if (currentLeftWidth >= crossingFromRight &&
				   crossingFromLeft >= crossingFromRight &&
				   currentRightWidth >= crossingFromRight) {
				current->xEnd = startX;
				if (current->xEnd == current->xStart) {
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
					if (previous == NULL) {
						g_scanlineSpanHeads[scanY] =
							current->next;
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
			if (currentLeftWidth >= crossingFromLeft &&
			    crossingFromLeft <= crossingFromRight &&
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
			newW += (float)overlapWidth * face->gradients[6];
			currentW += (float)overlapWidth *
				    current->face->gradients[6];
			if (newW < currentW) {
				currentLeftWidth =
					(int)((float)overlapWidth -
					      (newW - currentW) /
						      (face->gradients[6] -
						       current->face->gradients
							       [6])) +
					1;
				if (currentLeftWidth < 0) {
					currentLeftWidth = 0;
				}
				/* Here currentLeftWidth holds an x coordinate, the new span's new end, not a width. */
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
		if (face->maxScaledInverseDepth <=
		    current->face->minScaledInverseDepth) {
			if (current->xEnd >= span->xEnd) {
				span->xEnd = current->xStart;
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}
		if (face->minScaledInverseDepth >=
		    current->face->maxScaledInverseDepth) {
			if (current->xEnd <= span->xEnd) {
				current->face
					->pSpans[scanY - current->face->yTop] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			currentLeftWidth = span->xEnd - current->xStart;
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth *
						   current->dLightIntensityDx;
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth =
						next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity +=
						(float)currentLeftWidth *
						current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL &&
				       insertionNext->xStart <
					       current->xStart) {
					if (current->xStart <
					    insertionNext->xEnd) {
						currentLeftWidth =
							insertionNext->xEnd -
							current->xStart;
						current->xStart +=
							currentLeftWidth;
						current->lightIntensity +=
							(float)currentLeftWidth *
							current->dLightIntensityDx;
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
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
			continue;
		}

		newW = face->gradients[7] * (float)scanY + face->gradients[8] +
		       face->gradients[6] * (float)current->xStart;
		currentW = current->face->gradients[7] * (float)scanY +
			   current->face->gradients[8] +
			   current->face->gradients[6] * (float)current->xStart;
		if (newW <= currentW) {
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
				newW += (float)overlapWidth *
					face->gradients[6];
				currentW += (float)overlapWidth *
					    current->face->gradients[6];
				if (newW <= currentW) {
					previous = current;
					current = current->next;
					continue;
				}
				crossingFromRight =
					(int)((newW - currentW) /
					      (face->gradients[6] -
					       current->face->gradients[6]));
				if (crossingFromRight < 0) {
					crossingFromRight = 0;
				}
				current->xEnd -= crossingFromRight;
				if (current->xEnd <= current->xStart) {
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
					previous->next = current->next;
					current = current->next;
				} else {
					previous = current;
					current = current->next;
				}
				continue;
			}

			overlapWidth = span->xEnd - current->xStart;
			newW += (float)overlapWidth * face->gradients[6];
			currentW += (float)overlapWidth *
				    current->face->gradients[6];
			if (newW <= currentW) {
				span->xEnd = current->xStart;
				return;
			}
			crossingFromRight =
				(int)((newW - currentW) /
				      (face->gradients[6] -
				       current->face->gradients[6]));
			if (crossingFromRight < 0) {
				crossingFromRight = 0;
			}
			if (crossingFromRight > overlapWidth) {
				crossingFromRight = overlapWidth;
			}
			currentLeftWidth = overlapWidth - crossingFromRight;
			if (currentLeftWidth > crossingFromRight &&
			    current->xEnd - span->xEnd > crossingFromRight) {
				span->xEnd = current->xStart;
				return;
			}
			if (current->xEnd - span->xEnd > currentLeftWidth) {
				current->xStart += overlapWidth;
				current->lightIntensity +=
					(float)overlapWidth *
					current->dLightIntensityDx;
				next = current->next;
				if (next != NULL &&
				    next->xStart < current->xStart) {
					previous->next = next;
					if (current->xStart < next->xEnd) {
						currentLeftWidth =
							next->xEnd -
							current->xStart;
						current->xStart +=
							currentLeftWidth;
						current->lightIntensity +=
							(float)currentLeftWidth *
							current->dLightIntensityDx;
					}
					insertionNext = next->next;
					while (insertionNext != NULL &&
					       insertionNext->xStart <
						       current->xStart) {
						if (current->xStart <
						    insertionNext->xEnd) {
							currentLeftWidth =
								insertionNext
									->xEnd -
								current->xStart;
							current->xStart +=
								currentLeftWidth;
							current->lightIntensity +=
								(float)currentLeftWidth *
								current->dLightIntensityDx;
						}
						if (current->xStart >=
						    current->xEnd) {
							break;
						}
						next = insertionNext;
						insertionNext =
							insertionNext->next;
					}
					if (current->xStart < current->xEnd) {
						next->next = current;
						current->next = insertionNext;
					} else {
						current->face->pSpans
							[scanY -
							 current->face->yTop] =
							NULL;
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
				current->face
					->pSpans[scanY - current->face->yTop] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			currentLeftWidth = span->xEnd - current->xStart;
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth *
						   current->dLightIntensityDx;
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth =
						next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity +=
						(float)currentLeftWidth *
						current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL &&
				       insertionNext->xStart <
					       current->xStart) {
					if (current->xStart <
					    insertionNext->xEnd) {
						currentLeftWidth =
							insertionNext->xEnd -
							current->xStart;
						current->xStart +=
							currentLeftWidth;
						current->lightIntensity +=
							(float)currentLeftWidth *
							current->dLightIntensityDx;
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
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
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
			newW += (float)overlapWidth * face->gradients[6];
			currentW += (float)overlapWidth *
				    current->face->gradients[6];
			if (newW >= currentW) {
				current->face
					->pSpans[scanY - current->face->yTop] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			crossingFromRight =
				(int)((newW - currentW) /
				      (face->gradients[6] -
				       current->face->gradients[6]));
			if (crossingFromRight < 0) {
				crossingFromRight = 0;
			}
			currentLeftWidth = overlapWidth - crossingFromRight;
			if (currentLeftWidth < 0) {
				currentLeftWidth = 0;
			}
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth *
						   current->dLightIntensityDx;
			if (current->xEnd <= current->xStart) {
				current->face
					->pSpans[scanY - current->face->yTop] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth =
						next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity +=
						(float)currentLeftWidth *
						current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL &&
				       insertionNext->xStart <
					       current->xStart) {
					if (current->xStart <
					    insertionNext->xEnd) {
						currentLeftWidth =
							insertionNext->xEnd -
							current->xStart;
						current->xStart +=
							currentLeftWidth;
						current->lightIntensity +=
							(float)currentLeftWidth *
							current->dLightIntensityDx;
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
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
		} else {
			overlapWidth = span->xEnd - current->xStart;
			newW += (float)overlapWidth * face->gradients[6];
			currentW += (float)overlapWidth *
				    current->face->gradients[6];
			if (newW >= currentW) {
				current->xStart += overlapWidth;
				current->lightIntensity +=
					(float)overlapWidth *
					current->dLightIntensityDx;
				next = current->next;
				if (next != NULL &&
				    next->xStart < current->xStart) {
					previous->next = next;
					if (current->xStart < next->xEnd) {
						currentLeftWidth =
							next->xEnd -
							current->xStart;
						current->xStart +=
							currentLeftWidth;
						current->lightIntensity +=
							(float)currentLeftWidth *
							current->dLightIntensityDx;
					}
					insertionNext = next->next;
					while (insertionNext != NULL &&
					       insertionNext->xStart <
						       current->xStart) {
						if (current->xStart <
						    insertionNext->xEnd) {
							currentLeftWidth =
								insertionNext
									->xEnd -
								current->xStart;
							current->xStart +=
								currentLeftWidth;
							current->lightIntensity +=
								(float)currentLeftWidth *
								current->dLightIntensityDx;
						}
						if (current->xStart >=
						    current->xEnd) {
							break;
						}
						next = insertionNext;
						insertionNext =
							insertionNext->next;
					}
					if (current->xStart < current->xEnd) {
						next->next = current;
						current->next = insertionNext;
					} else {
						current->face->pSpans
							[scanY -
							 current->face->yTop] =
							NULL;
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
				      (newW - currentW) /
					      (face->gradients[6] -
					       current->face->gradients[6]));
			if (currentLeftWidth < 0) {
				currentLeftWidth = 0;
			}
			if (currentLeftWidth > overlapWidth) {
				currentLeftWidth = overlapWidth;
			}
			current->xStart += currentLeftWidth;
			current->lightIntensity += (float)currentLeftWidth *
						   current->dLightIntensityDx;
			next = current->next;
			if (next != NULL && next->xStart < current->xStart) {
				previous->next = next;
				if (current->xStart < next->xEnd) {
					currentLeftWidth =
						next->xEnd - current->xStart;
					current->xStart += currentLeftWidth;
					current->lightIntensity +=
						(float)currentLeftWidth *
						current->dLightIntensityDx;
				}
				insertionNext = next->next;
				while (insertionNext != NULL &&
				       insertionNext->xStart <
					       current->xStart) {
					if (current->xStart <
					    insertionNext->xEnd) {
						currentLeftWidth =
							insertionNext->xEnd -
							current->xStart;
						current->xStart +=
							currentLeftWidth;
						current->lightIntensity +=
							(float)currentLeftWidth *
							current->dLightIntensityDx;
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
					current->face
						->pSpans[scanY -
							 current->face->yTop] =
						NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
		}
	}
}

/* Draws pixels startX to endX - 1 of the current row of g_sw3dCurrentFace,
 * textured and shaded; spanStartW is the face's w at startX. Texture u and v
 * are worked out with perspective at each lighting block boundary (every
 * g_sw3dLightSampleBlockSize columns) and stepped evenly between, in texels
 * with 8 fraction bits. The light at each boundary comes from the face's light
 * samples, one per block column, kept by stamp: a sample stamped
 * g_sw3dCurrentLightSampleCacheStamp is used as it is, one stamped 1 less is
 * moved on by its rowDelta, any other is worked out with
 * FlightLight_ComputeSoftwareFaceSampleIntensity at the block's top and the
 * next block's top; the row takes it at g_sw3dLightSampleSubrowLerpT. That
 * light plus the span's interpolated vertex light, times 15, is the shade with
 * 8 fraction bits, kept to 0 to 0xEFF at each boundary and stepped across the
 * block; a negative change gets g_sw3dLightSampleBlockSize added. Each pixel is
 * the shade table entry for level ((shade + carry) >> 8) & 15 and its texel,
 * written at 8 or 16 bits. With both texture shifts 3 to 8 each coordinate
 * wraps on its own; otherwise the texel index is masked with
 * g_sw3dSpanTexelMask, through sw3d_DrawTexturedShadeSpanGeneric16bpp in 16-bit
 * color. */
// FUNCTION: XVT 0x4879D0
void sw3d_DrawTexturedSpan(int startX, int endX, float spanStartW)
{
	enum {
		SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT = 3,
		SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT = 8,
		SW3D_FIXED_POINT_SHIFT = 8,
		SW3D_SHADE_LEVEL_MASK = 0xF,
		SW3D_SHADE_TABLE_LEVEL_STRIDE = 256,
		SW3D_TRUE_COLOR_SHADE_TABLE_OFFSET = 4096,
		SW3D_MAX_SHADE_Q8 = 0xEFF,
	};

	SceneFace *face;
	SoftwareLightSample *lightSamples;
	SoftwareLightSample *leftSample;
	SoftwareLightSample *rightSample;
	float *uGradient;
	float *vGradient;
	float uAtY;
	float vAtY;
	float wAtY;
	float uNumerator;
	float vNumerator;
	float inverseW;
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
	float wBlockStep;
	float boundaryW;
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
	lightSamples = (SoftwareLightSample *)face->pLightSamples;
	wAtY = (float)(unsigned int)g_sw3dCurrentScanlineY *
		       face->gradients[7] +
	       face->gradients[8];
	uGradient = &face->gradients[0];
	vGradient = &face->gradients[3];
	uAtY = (float)g_sw3dCurrentScanlineY * uGradient[1];
	vAtY = (float)g_sw3dCurrentScanlineY * vGradient[1];
	uAtY += uGradient[2];
	vAtY += vGradient[2];
	uNumerator = (float)startX * uGradient[0] + uAtY;
	vNumerator = (float)startX * vGradient[0] + vAtY;
	inverseW = g_sw3dSpanOneFloat / spanStartW;
	u = inverseW * uNumerator;
	v = inverseW * vNumerator;
	lightIntensity = ((float)startX - face->pScanEdge->x) *
				 face->spanLightIntensityDx +
			 face->pScanEdge->lightIntensity;

	g_sw3dSpanShadeDitherAccum = g_sw3dShadeDitherInitialByScanlineParity
		[g_sw3dCurrentScanlineY & 1];
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
			leftSample->intensity =
				FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, blockStartX, blockStartY,
					spanStartW -
						(float)withinBlockX *
							face->gradients[6] -
						g_sw3dLightSampleSubrowFloat *
							face->gradients[7]);
		} else {
			++leftSample->stamp;
			leftSample->intensity += leftSample->rowDelta;
		}
		leftSample->rowDelta =
			FlightLight_ComputeSoftwareFaceSampleIntensity(
				face, blockStartX,
				blockStartY + g_sw3dLightSampleBlockSize,
				spanStartW -
					(float)withinBlockX *
						face->gradients[6] +
					g_sw3dLightSampleRowsToNextBlockFloat *
						face->gradients[7]) -
			leftSample->intensity;
	}
	leftLight = leftSample->intensity +
		    g_sw3dLightSampleSubrowLerpT * leftSample->rowDelta;

	boundaryX = (startBlock + 1) << g_sw3dLightSampleBlockShift;
	g_sw3dSpanLength = boundaryX - g_sw3dSpanStartX;
	uNumerator = (float)boundaryX * uGradient[0] + uAtY;
	vNumerator = (float)boundaryX * vGradient[0] + vAtY;
	boundaryW = (float)boundaryX * face->gradients[6] + wAtY;
	block = startBlock + 1;
	inverseW = g_sw3dSpanOneFloat / boundaryW;
	rightSample = &lightSamples[block];
	stampDelta = g_sw3dCurrentLightSampleCacheStamp - rightSample->stamp;
	if (stampDelta != 0) {
		rightSample->stamp = g_sw3dCurrentLightSampleCacheStamp;
		if (stampDelta != 1) {
			rightSample->intensity =
				FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundaryX, blockStartY,
					boundaryW);
			sampleIntensity =
				FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundaryX,
					blockStartY +
						g_sw3dLightSampleBlockSize,
					boundaryW);
		} else {
			sampleIntensity =
				FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundaryX,
					blockStartY +
						g_sw3dLightSampleBlockSize,
					boundaryW);
			rightSample->intensity += rightSample->rowDelta;
		}
		rightSample->rowDelta =
			sampleIntensity - rightSample->intensity;
	}
	rightLight = rightSample->intensity +
		     g_sw3dLightSampleSubrowLerpT * rightSample->rowDelta;
	leftLight += (rightLight - leftLight) *
		     ((float)withinBlockX * g_sw3dLightSampleInvBlockSize);
	rightU = inverseW * uNumerator;
	rightV = inverseW * vNumerator;

	fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureWidthShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias,
	       sizeof(fixedPointBiasBits));
	fixedPointValue =
		(rightU - u) * g_sw3dSpanLengthReciprocal[g_sw3dSpanLength] +
		fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanStepUQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointBias =
		g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureHeightShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias,
	       sizeof(fixedPointBiasBits));
	fixedPointValue =
		(rightV - v) * g_sw3dSpanLengthReciprocal[g_sw3dSpanLength] +
		fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanStepVQ8 = fixedPointBits - fixedPointBiasBits;

	if ((unsigned int)block > (unsigned int)endBlock) {
		g_sw3dSpanLength = endX - g_sw3dSpanStartX;
	} else {
		uNumeratorBlockStep =
			uGradient[0] * g_sw3dLightSampleBlockSizeFloat;
		vNumeratorBlockStep =
			vGradient[0] * g_sw3dLightSampleBlockSizeFloat;
		wBlockStep =
			face->gradients[6] * g_sw3dLightSampleBlockSizeFloat;
	}

	lightIntensityAtEnd =
		(float)g_sw3dSpanLength * face->spanLightIntensityDx +
		lightIntensity;
	lightIntensityBlockStep =
		face->spanLightIntensityDx * g_sw3dLightSampleBlockSizeFloat;
	fixedPointBias = g_sw3dTexCoordBiasByShift[0];
	memcpy(&fixedPointBiasBits, &fixedPointBias,
	       sizeof(fixedPointBiasBits));
	fixedPointValue = (leftLight + lightIntensity) *
				  g_sw3dLightIntensityToShadeScale +
			  fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanShadeQ8 = fixedPointBits - fixedPointBiasBits;
	if (g_sw3dSpanShadeQ8 < 0) {
		g_sw3dSpanShadeQ8 = 0;
	}
	if (g_sw3dSpanShadeQ8 > SW3D_MAX_SHADE_Q8) {
		g_sw3dSpanShadeQ8 = SW3D_MAX_SHADE_Q8;
	}
	fixedPointValue = (rightLight + lightIntensityAtEnd) *
				  g_sw3dLightIntensityToShadeScale +
			  fixedPointBias;
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
	memcpy(&fixedPointBiasBits, &fixedPointBias,
	       sizeof(fixedPointBiasBits));
	fixedPointValue = (float)shadeDeltaQ8 *
				  g_sw3dSpanLengthReciprocal[g_sw3dSpanLength] +
			  fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanShadeStepQ8 = fixedPointBits - fixedPointBiasBits;

	fixedPointBias = g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureWidthShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias,
	       sizeof(fixedPointBiasBits));
	fixedPointValue = u + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanUQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointValue = rightU + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	nextUQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointBias =
		g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureHeightShift];
	memcpy(&fixedPointBiasBits, &fixedPointBias,
	       sizeof(fixedPointBiasBits));
	fixedPointValue = v + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	g_sw3dSpanVQ8 = fixedPointBits - fixedPointBiasBits;
	fixedPointValue = rightV + fixedPointBias;
	memcpy(&fixedPointBits, &fixedPointValue, sizeof(fixedPointBits));
	nextVQ8 = fixedPointBits - fixedPointBiasBits;
	useSpecializedTextureWrap =
		g_sw3dSpanTextureWidthShift >=
			SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT &&
		g_sw3dSpanTextureWidthShift <=
			SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT &&
		g_sw3dSpanTextureHeightShift >=
			SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT &&
		g_sw3dSpanTextureHeightShift <=
			SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT;
	if (useSpecializedTextureWrap) {
		textureWidthMask = (1 << g_sw3dSpanTextureWidthShift) - 1;
		textureHeightMask = (1 << g_sw3dSpanTextureHeightShift) - 1;
	} else {
		textureWidthMask = 0;
		textureHeightMask = 0;
	}

	for (;;) {
		if (block <= endBlock) {
			boundaryW += wBlockStep;
			inverseW = g_sw3dSpanOneFloat / boundaryW;
		}

		if (g_flightBytesPerPixel == 2 && !useSpecializedTextureWrap) {
			sw3d_DrawTexturedShadeSpanGeneric16bpp();
		} else {
			for (pixelIndex = 0; pixelIndex < g_sw3dSpanLength;
			     ++pixelIndex) {
				unsigned int shadeAccum;
				int texelIndex;
				int texel;

				if (useSpecializedTextureWrap) {
					texelIndex =
						(((g_sw3dSpanVQ8 >>
						   SW3D_FIXED_POINT_SHIFT) &
						  textureHeightMask)
						 << g_sw3dSpanTextureWidthShift) |
						((g_sw3dSpanUQ8 >>
						  SW3D_FIXED_POINT_SHIFT) &
						 textureWidthMask);
				} else {
					texelIndex =
						((g_sw3dSpanVQ8 >>
						  SW3D_FIXED_POINT_SHIFT)
						 << g_sw3dSpanTextureWidthShift) +
						(g_sw3dSpanUQ8 >>
						 SW3D_FIXED_POINT_SHIFT);
					texelIndex &= g_sw3dSpanTexelMask;
				}
				texel = g_sw3dSpanTexels[texelIndex];
				shadeAccum =
					(unsigned int)(g_sw3dSpanShadeQ8 +
						       g_sw3dSpanShadeDitherAccum);
				g_sw3dSpanShadeDitherAccum =
					(uint8_t)shadeAccum;
				if (g_flightBytesPerPixel == 2) {
					uint16_t *surface16 =
						(uint16_t
							 *)((uint8_t *)
								    g_surfacePixels +
							    g_sw3dSpanFramebufferRowOffset);
					uint16_t *shadeTable16 =
						(uint16_t
							 *)(g_sw3dSpanShadeTable +
							    SW3D_TRUE_COLOR_SHADE_TABLE_OFFSET);
					surface16[g_sw3dSpanStartX +
						  pixelIndex] = shadeTable16
						[(((shadeAccum >>
						    SW3D_FIXED_POINT_SHIFT) &
						   SW3D_SHADE_LEVEL_MASK)
						  << SW3D_FIXED_POINT_SHIFT) +
						 texel];
				} else {
					uint8_t *surface8 =
						(uint8_t *)g_surfacePixels +
						g_sw3dSpanFramebufferRowOffset +
						g_sw3dSpanStartX;
					surface8[pixelIndex] = g_sw3dSpanShadeTable
						[(((shadeAccum >>
						    SW3D_FIXED_POINT_SHIFT) &
						   SW3D_SHADE_LEVEL_MASK) *
						  SW3D_SHADE_TABLE_LEVEL_STRIDE) +
						 texel];
				}
				g_sw3dSpanShadeQ8 += g_sw3dSpanShadeStepQ8;
				g_sw3dSpanVQ8 += g_sw3dSpanStepVQ8;
				g_sw3dSpanUQ8 += g_sw3dSpanStepUQ8;
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
		stampDelta =
			g_sw3dCurrentLightSampleCacheStamp - rightSample->stamp;
		if (stampDelta != 0) {
			rightSample->stamp = g_sw3dCurrentLightSampleCacheStamp;
			if (stampDelta != 1) {
				rightSample->intensity =
					FlightLight_ComputeSoftwareFaceSampleIntensity(
						face, boundaryX, blockStartY,
						boundaryW);
				sampleIntensity =
					FlightLight_ComputeSoftwareFaceSampleIntensity(
						face, boundaryX,
						blockStartY +
							g_sw3dLightSampleBlockSize,
						boundaryW);
			} else {
				sampleIntensity =
					FlightLight_ComputeSoftwareFaceSampleIntensity(
						face, boundaryX,
						blockStartY +
							g_sw3dLightSampleBlockSize,
						boundaryW);
				rightSample->intensity += rightSample->rowDelta;
			}
			rightSample->rowDelta =
				sampleIntensity - rightSample->intensity;
		}
		rightU = inverseW * uNumerator;
		rightV = inverseW * vNumerator;
		rightLight =
			rightSample->intensity +
			g_sw3dLightSampleSubrowLerpT * rightSample->rowDelta;
		lightIntensityAtEnd += lightIntensityBlockStep;
		g_sw3dSpanShadeQ8 = endShadeQ8;
		fixedPointBias = g_sw3dTexCoordBiasByShift[0];
		memcpy(&fixedPointBiasBits, &fixedPointBias,
		       sizeof(fixedPointBiasBits));
		fixedPointValue = (rightLight + lightIntensityAtEnd) *
					  g_sw3dLightIntensityToShadeScale +
				  fixedPointBias;
		memcpy(&fixedPointBits, &fixedPointValue,
		       sizeof(fixedPointBits));
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
		g_sw3dSpanShadeStepQ8 =
			shadeDeltaQ8 >> g_sw3dLightSampleBlockShift;
		g_sw3dSpanUQ8 = nextUQ8;
		g_sw3dSpanVQ8 = nextVQ8;
		fixedPointBias =
			g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureWidthShift];
		memcpy(&fixedPointBiasBits, &fixedPointBias,
		       sizeof(fixedPointBiasBits));
		fixedPointValue = rightU + fixedPointBias;
		memcpy(&fixedPointBits, &fixedPointValue,
		       sizeof(fixedPointBits));
		nextUQ8 = fixedPointBits - fixedPointBiasBits;
		fixedPointBias =
			g_sw3dTexCoordBiasByShift[g_sw3dSpanTextureHeightShift];
		memcpy(&fixedPointBiasBits, &fixedPointBias,
		       sizeof(fixedPointBiasBits));
		fixedPointValue = rightV + fixedPointBias;
		memcpy(&fixedPointBits, &fixedPointValue,
		       sizeof(fixedPointBits));
		nextVQ8 = fixedPointBits - fixedPointBiasBits;
		g_sw3dSpanStepUQ8 = (nextUQ8 - g_sw3dSpanUQ8) >>
				    g_sw3dLightSampleBlockShift;
		g_sw3dSpanStepVQ8 = (nextVQ8 - g_sw3dSpanVQ8) >>
				    g_sw3dLightSampleBlockShift;
	}
}

/* Draws the current piece of a span in 16-bit color for the texture sizes the
 * main loop leaves out: g_sw3dSpanLength pixels from g_sw3dSpanStartX, each the
 * 16-bit shade table entry for level ((shade + carry) >> 8) & 15 and the texel
 * at index i = ((v >> 8) << g_sw3dSpanTextureWidthShift) + (u >> 8), masked
 * with g_sw3dSpanTexelMask, stepping the shade, v and u. Returns the last v, or
 * g_sw3dSpanStartX + g_sw3dSpanLength when it draws nothing; its only caller,
 * sw3d_DrawTexturedSpan, ignores it. */
// FUNCTION: XVT 0x497850
int sw3d_DrawTexturedShadeSpanGeneric16bpp(void)
{
	uint8_t *pixel;
	uint8_t *pixelEnd;
	uint8_t *shadeTable;
	int result;

	pixel = (uint8_t *)g_surfacePixels;
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

		texelIndex =
			((g_sw3dSpanVQ8 >> 8) << g_sw3dSpanTextureWidthShift) +
			(g_sw3dSpanUQ8 >> 8);
		texel = g_sw3dSpanTexels[texelIndex & g_sw3dSpanTexelMask];
		shadeAccum = (unsigned int)(g_sw3dSpanShadeQ8 +
					    g_sw3dSpanShadeDitherAccum);
		g_sw3dSpanShadeDitherAccum = (uint8_t)shadeAccum;
		*(uint16_t *)pixel =
			((uint16_t *)
				 shadeTable)[(((shadeAccum >> 8) & 0xF) << 8) +
					     texel];
		pixel += 2;
		g_sw3dSpanShadeQ8 += g_sw3dSpanShadeStepQ8;
		result = g_sw3dSpanVQ8 + g_sw3dSpanStepVQ8;
		g_sw3dSpanVQ8 = result;
		g_sw3dSpanUQ8 += g_sw3dSpanStepUQ8;
	}
	return result;
}

/* Copies columns startX to endX - 1 of a row from pSrcRaster, which holds that
 * row from column startX at g_flightBytesPerPixel bytes a pixel, onto row scanY
 * of the flight surface, only where depth spriteW is nearer than the faces in
 * the row's span list (larger is nearer). A span whose face's
 * minScaledInverseDepth is at least spriteW hides its columns, a face whose
 * maxScaledInverseDepth is at most spriteW hides none, and otherwise the column
 * where the face's w crosses spriteW splits the span. Sets
 * g_sw3dSpanFramebufferRowOffset for the row. */
// FUNCTION: XVT 0x497940
void sw3d_BlitOccludedSpan(const uint8_t *pSrcRaster, int startX, int endX,
			   int scanY, float spriteW)
{
	SceneSpan *span;
	int drawX;

	drawX = startX;
	if (g_flightBytesPerPixel == 2) {
		pSrcRaster -= 2 * startX;
	} else {
		pSrcRaster -= startX;
	}
	g_sw3dSpanFramebufferRowOffset = g_flightBytesPerPixel * g_flightVpX +
					 g_surfacePitch * (scanY + g_flightVpY);

	for (span = g_scanlineSpanHeads[scanY]; span != NULL;
	     span = span->next) {
		SceneFace *face;
		int spanEnd;
		float spanW;
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
		if (spriteW <= face->minScaledInverseDepth) {
			drawX = spanEnd;
			if (endX <= spanEnd) {
				return;
			}
		} else if (spriteW < face->maxScaledInverseDepth) {
			spanW = (float)scanY * face->gradients[7] +
				face->gradients[8];
			spanW = (float)drawX * face->gradients[6] + spanW;
			if (spriteW <= spanW) {
				if (face->gradients[6] >= 0.0f) {
					drawX = spanEnd;
					if (endX <= spanEnd) {
						return;
					}
				} else {
					if (endX > spanEnd) {
						deltaX = (float)(spanEnd -
								 drawX);
						spanW = deltaX * face->gradients
									 [6] +
							spanW;
						if (spriteW <= spanW) {
							drawX = spanEnd;
							continue;
						}
					} else {
						deltaX = (float)(endX - drawX);
						spanW = deltaX * face->gradients
									 [6] +
							spanW;
						if (spriteW <= spanW) {
							return;
						}
					}
					depthFalloff = -face->gradients[6];
					drawX += (int)(deltaX -
						       (spriteW - spanW) /
							       depthFalloff);
					if (endX <= drawX) {
						return;
					}
				}
			} else if (face->gradients[6] > 0.0f) {
				if (endX >= spanEnd) {
					deltaX = (float)(spanEnd - drawX);
					spanW = deltaX * face->gradients[6] +
						spanW;
					if (spriteW >= spanW) {
						continue;
					}
				} else {
					deltaX = (float)(endX - drawX);
					spanW = deltaX * face->gradients[6] +
						spanW;
					if (spriteW >= spanW) {
						continue;
					}
				}
				depthFalloff = -face->gradients[6];
				endX = drawX +
				       (int)(deltaX -
					     (spriteW - spanW) / depthFalloff);
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
		if (spriteW <= span->face->minScaledInverseDepth) {
			sw3d_CopySpanToFramebuffer(pSrcRaster, drawX,
						   span->xStart - drawX);
			drawX = span->xEnd;
			if (endX <= drawX) {
				return;
			}
		} else if (spriteW < span->face->maxScaledInverseDepth) {
			float spanW;

			spanW = (float)scanY * span->face->gradients[7] +
				span->face->gradients[8];
			spanW = (float)span->xStart * span->face->gradients[6] +
				spanW;
			if (spriteW <= spanW) {
				sw3d_CopySpanToFramebuffer(pSrcRaster, drawX,
							   span->xStart -
								   drawX);
				drawX = span->xEnd;
				if (span->face->gradients[6] >= 0.0f) {
					if (endX <= drawX) {
						return;
					}
				} else if (endX > drawX) {
					spanW = (float)(drawX - span->xStart) *
							span->face
								->gradients[6] +
						spanW;
					if (spriteW > spanW) {
						float depthFalloff;

						depthFalloff =
							-span->face
								 ->gradients[6];
						drawX -= (int)((spriteW -
								spanW) /
							       depthFalloff);
					}
				} else {
					float depthFalloff;

					spanW = (float)(endX - span->xStart) *
							span->face
								->gradients[6] +
						spanW;
					if (spriteW <= spanW) {
						return;
					}
					depthFalloff =
						-span->face->gradients[6];
					drawX = endX - (int)((spriteW - spanW) /
							     depthFalloff);
					if (endX <= drawX) {
						return;
					}
				}
			} else if (span->face->gradients[6] > 0.0f) {
				if (endX > span->xEnd) {
					spanW = (float)(span->xEnd -
							span->xStart) *
							span->face
								->gradients[6] +
						spanW;
					if (spriteW < spanW) {
						float depthFalloff;

						depthFalloff =
							-span->face
								 ->gradients[6];
						sw3d_CopySpanToFramebuffer(
							pSrcRaster, drawX,
							span->xEnd -
								(int)((spriteW -
								       spanW) /
								      depthFalloff) -
								drawX);
						drawX = span->xEnd;
					}
				} else {
					float deltaX;

					deltaX = (float)(endX - span->xStart);
					spanW = deltaX *
							span->face
								->gradients[6] +
						spanW;
					if (spriteW < spanW) {
						float depthFalloff;

						depthFalloff =
							-span->face
								 ->gradients[6];
						sw3d_CopySpanToFramebuffer(
							pSrcRaster, drawX,
							span->xStart +
								(int)(deltaX -
								      (spriteW -
								       spanW) /
									      depthFalloff) -
								drawX);
						return;
					}
				}
			}
		}
		span = span->next;
	}

	sw3d_CopySpanToFramebuffer(pSrcRaster, drawX, endX - drawX);
}

/* Copies pixelCount pixels from pSrcRasterBase, from its pixel startX, to
 * column startX of the row at g_sw3dSpanFramebufferRowOffset in
 * g_surfacePixels, at g_flightBytesPerPixel bytes a pixel. Does nothing for a
 * count of 0 or less. Only sw3d_BlitOccludedSpan calls it. */
// FUNCTION: XVT 0x497D80
void sw3d_CopySpanToFramebuffer(const uint8_t *pSrcRasterBase, int startX,
				int pixelCount)
{
	if (pixelCount > 0) {
		if (g_flightBytesPerPixel == 2) {
			uint8_t *dst = (uint8_t *)g_surfacePixels +
				       g_sw3dSpanFramebufferRowOffset + startX +
				       startX;

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
			uint8_t *dst = (uint8_t *)g_surfacePixels + startX +
				       g_sw3dSpanFramebufferRowOffset;

			pSrcRasterBase += startX;
			do {
				*dst++ = *pSrcRasterBase++;
			} while (--pixelCount != 0);
		}
	}
}

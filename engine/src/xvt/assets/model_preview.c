#include "xvt/assets/model_preview.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif
#include "xvt/assets/file.h"

#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/memory.h"
#include <math.h>
#include <string.h>

// GLOBAL: XVT 0x518100
const float g_modelPreviewMatrixQ15ToFloatScale = 0.000030518509f;
// GLOBAL: XVT 0x518110
static const double g_modelPreviewInvLengthNumerator = 1.0;
// GLOBAL: XVT 0x5180F8
const double g_modelPreviewTargetBoundsExtent = 500.0;
// GLOBAL: XVT 0x518118
static const double g_modelPreviewLightDirectionQ15Scale = 32767.0;
// GLOBAL: XVT 0x518120
static const double g_degreesToQ16AngleScale = 182.04444444444445;
// GLOBAL: XVT 0x518128
static const double g_modelPreviewMetersScale = 1600.0;
// GLOBAL: XVT 0x518130
static const double g_modelPreviewQ16Scale = 0.0000152587890625;
// GLOBAL: XVT 0x520EC0
int16_t g_modelPreviewAngleD;
// GLOBAL: XVT 0x520EC4
OptimizedPolyObject* g_modelPreviewModelData = NULL;
// GLOBAL: XVT 0x520EC8
int g_modelPreviewStateGuard = 0;
// GLOBAL: XVT 0x520ECC
int g_modelPreviewRenderResourcesInitialized = 0;
// GLOBAL: XVT 0x520ED0
uint16_t g_modelPreviewAuxBufferHandle = 0;
// GLOBAL: XVT 0x520ED4
unsigned int g_modelPreviewAuxBufferCapacityBytes = 0;
// GLOBAL: XVT 0x520ED8
double g_modelPreviewBoundsExtent;
// GLOBAL: XVT 0x5233A0
int g_nodeSwitchIndex;
// GLOBAL: XVT 0x5561E8
ObjectRecord g_modelPreviewObject;
// GLOBAL: XVT 0x5561E0
double g_modelPreviewScale = 0.0;
// GLOBAL: XVT 0x556218
MobileObject g_modelPreviewMobileObject = { 0 };
// GLOBAL: XVT 0x555CC8
char g_modelPreviewOptFileName[128];
// GLOBAL: XVT 0x555CC0
int16_t g_savedModelPreviewPitch;
// GLOBAL: XVT 0x555CC4
int16_t g_savedModelPreviewYaw;
// GLOBAL: XVT 0x555D48
static float g_modelPreviewBoundsMaxZ = 0.0f;
// GLOBAL: XVT 0x555D4C
static float g_modelPreviewBoundsMinZ = 0.0f;
// GLOBAL: XVT 0x555D50
static float g_modelPreviewBoundsMaxX = 0.0f;
// GLOBAL: XVT 0x555D54
static float g_modelPreviewBoundsMinX = 0.0f;
// GLOBAL: XVT 0x555D58
int16_t g_savedModelPreviewLightDirectionZ;
// GLOBAL: XVT 0x555D5C
int g_savedModelPreviewWorldY;
// GLOBAL: XVT 0x555D60
int g_savedModelPreviewWorldZ;
// GLOBAL: XVT 0x555D64
int g_savedModelPreviewWorldX;
// GLOBAL: XVT 0x555D68
static float g_modelPreviewBoundsMaxY = 0.0f;
// GLOBAL: XVT 0x555D6C
static float g_modelPreviewBoundsMinY = 0.0f;
// GLOBAL: XVT 0x555D78
CraftData g_modelPreviewCraftScratch = { 0 };
// GLOBAL: XVT 0x555D70
int16_t g_savedModelPreviewLightDirectionY;
// GLOBAL: XVT 0x555D74
int16_t g_savedModelPreviewLightDirectionX;
// GLOBAL: XVT 0x5561DC
int g_savedModelPreviewNodeSwitchIndex;
// GLOBAL: XVT 0x55620C
int16_t g_savedModelPreviewAngleD;
// GLOBAL: XVT 0x556210
int16_t g_savedModelPreviewRoll;
// GLOBAL: XVT 0x5562D0
char g_savedModelPreviewModelFileName[128];
// GLOBAL: XVT 0x9D12EC
int g_modelPreviewLightDirectionX;
// GLOBAL: XVT 0x9D12F0
int g_modelPreviewLightDirectionY;
// GLOBAL: XVT 0x9D1304
int g_modelPreviewLightDirectionZ;
// GLOBAL: XVT 0xA60710
OptVector g_modelPreviewViewDelta = { 0.0f, 0.0f, 0.0f };
// GLOBAL: XVT 0xA6071C
float g_modelPreviewMatrix[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
// GLOBAL: XVT 0xA60740
OptVector g_modelPreviewNegViewDelta = { 0.0f, 0.0f, 0.0f };
// GLOBAL: XVT 0xA6074C
float g_modelPreviewObjectViewMatrix[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x429E70
int ModelPreview_LoadModel(const char* modelFileName) {
	enum {
		MODEL_PREVIEW_SLOT = 0,
		FILE_NAME_CAPACITY = 256,
#ifndef XVT_MODERN
		INVENTOR_SIGNATURE_LENGTH = sizeof("#inventor") - 1,
		INVENTOR_ASCII_LABEL_LENGTH = sizeof("ascii") - 1,
		INVENTOR_BINARY_LABEL_LENGTH = sizeof("binary") - 1,
#endif
	};
	char fileName[FILE_NAME_CAPACITY];
	char baseName[FILE_NAME_CAPACITY];
#ifdef XVT_MODERN
	char* extension;
#else
	int extensionIndex;
#endif
	XvtFile* stream;
#ifndef XVT_MODERN
	uint16_t importedHandle;
	uint16_t packedHandle;
#endif

	ModelPreview_FreeResources();
	ModelPreview_ResetViewAndRenderState();
	g_mipmappingEnabled = 1;
	if (g_modelPreviewModelData == NULL) {
		g_loadedModels[MODEL_PREVIEW_SLOT] = 0;
	}

#ifdef XVT_MODERN
	if (!modelFileName || strlen(modelFileName) >= sizeof(baseName))
		return 0;
	strcpy(baseName, modelFileName);
	extension = strrchr(baseName, '.');
	if (extension) {
		if (strcasecmp(extension, ".opt") != 0)
			return 0;
		*extension = '\0';
	}
	if (strlen(baseName) + sizeof(".opt") > sizeof(fileName))
		return 0;
#else
	strcpy(baseName, modelFileName);
	for (extensionIndex = 0; baseName[extensionIndex] != '.'; ++extensionIndex) {
	}
	baseName[extensionIndex] = '\0';
#endif

	strcpy(fileName, baseName);
	strcat(fileName, ".opt");
	File_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 0);
	stream = g_stream;
#ifdef XVT_MODERN
	if (!stream)
		return 0;
	File_Close(stream);
	g_stream = NULL;
	if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0)
		Memory_FreeHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
	g_loadedModels[MODEL_PREVIEW_SLOT] = OptModel_LoadFileToHandle(fileName);
#else
	if (stream == NULL) {
		strcpy(fileName, baseName);
		strcat(fileName, ".iv");
		File_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 0);
		stream = g_stream;
		if (stream == NULL) {
			return 0;
		}
		if (File_Scanf(stream, "%256s", fileName) != 1) {
			return 0;
		}
		if (_strnicmp(fileName, "#inventor", INVENTOR_SIGNATURE_LENGTH) != 0) {
			return 0;
		}
		if (File_Scanf(stream, " %256s", fileName) != 1) {
			return 0;
		}
		if (File_Scanf(stream, " %256s", fileName) != 1) {
			return 0;
		}
		if (_strnicmp(fileName, "ascii", INVENTOR_ASCII_LABEL_LENGTH) == 0) {
			if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0) {
				Memory_FreeHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
			}
			importedHandle = OptModel_LoadInventorAsciiToHandle(stream);
		} else {
			if (_strnicmp(fileName, "binary", INVENTOR_BINARY_LABEL_LENGTH) == 0) {
				if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0) {
					Memory_FreeHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
				}
				importedHandle = OptModel_LoadInventorBinaryToHandle(stream);
			} else {
				File_RawClose(stream);
				return 0;
			}
		}
		File_RawClose(stream);
		packedHandle = OptModel_ConvertImportedHandleToPacked(importedHandle);
		strcpy(fileName, baseName);
		strcat(fileName, ".opt");
		OptModel_SaveHandleToFile(fileName, packedHandle);
		g_loadedModels[MODEL_PREVIEW_SLOT] = packedHandle;
	} else {
		File_RawClose(stream);
		if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0) {
			Memory_FreeHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
		}
		g_loadedModels[MODEL_PREVIEW_SLOT] = OptModel_LoadFileToHandle(fileName);
	}
#endif

#ifdef XVT_MODERN
	if (!g_loadedModels[MODEL_PREVIEW_SLOT])
		return 0;
#endif
	strcpy(g_modelPreviewOptFileName, baseName);
	strcat(g_modelPreviewOptFileName, ".opt");
	if (g_mipmappingEnabled != 0) {
		g_flight16bppBytesPerPixel = 2;
		g_loadedModels[MODEL_PREVIEW_SLOT] = OptModel_CreateRuntimeHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
		g_flight16bppBytesPerPixel = 2;
	}
#ifdef XVT_MODERN
	if (!g_loadedModels[MODEL_PREVIEW_SLOT])
		return 0;
#endif
#ifdef XVT_MODERN
	XvtRenderAssets_RegisterOpt(g_loadedModels[MODEL_PREVIEW_SLOT], g_modelPreviewOptFileName);
	XvtRenderAssets_BindType(MODEL_PREVIEW_SLOT, g_loadedModels[MODEL_PREVIEW_SLOT]);
#endif
	g_modelPreviewModelData = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
	if (g_modelPreviewModelData->selfMarker != g_modelPreviewModelData) {
		OptModel_AdjustOptimizedPolyObjectPointers(g_modelPreviewModelData);
	}
	g_modelPreviewBoundsExtent =
		ModelPreview_ComputeOptBoundsExtent(g_modelPreviewModelData, MODEL_PREVIEW_SLOT);
	g_modelPreviewScale = g_modelPreviewTargetBoundsExtent / g_modelPreviewBoundsExtent;
	ModelPreview_ScaleOptRootNodes(g_modelPreviewModelData, g_modelPreviewScale);
	g_transformLightDirectionToObjectSpace = 1;

	if (g_modelPreviewStateGuard == 0) {
		memset(&g_modelPreviewMobileObject, 0, sizeof(g_modelPreviewMobileObject));
		memset(&g_modelPreviewCraftScratch, 0, sizeof(g_modelPreviewCraftScratch));
		g_modelPreviewObject.objectType = 0;
		g_modelPreviewObject.world_x = 0;
		g_modelPreviewObject.pitch = 0;
		g_modelPreviewObject.world_y = 0;
		g_modelPreviewObject.yaw = 0;
		g_modelPreviewObject.world_z = 0;
		g_modelPreviewObject.mobj = &g_modelPreviewMobileObject;
		g_modelPreviewMobileObject.pCraft = &g_modelPreviewCraftScratch;
		g_modelPreviewObject.roll = 0;
		g_modelPreviewAngleD = 0;
		ModelPreview_ResetViewAndRenderState();
		ModelPreview_SetWhiteDirectionalLight(1, 1, 1);
	}
	return 1;
}

// FUNCTION: XVT 0x42A350
void ModelPreview_FreeResources(void) {
	if (g_modelPreviewRenderResourcesInitialized != 0) {
		RenderScene_FreeBuffers();
		g_modelPreviewRenderResourcesInitialized = 0;
	}
	g_modelPreviewStateGuard = 0;
}

// FUNCTION: XVT 0x42A380
int ModelPreview_RenderViewport(int x, int y, int width, int height, ...) {
	enum {
		MODEL_PREVIEW_SLOT = 0,
		RENDER_SURFACE_MAX_WIDTH = 1024,
		RENDER_SURFACE_MAX_HEIGHT = 768,
		MODEL_PREVIEW_PROJECTION_SCALE = 512,
		MODEL_PREVIEW_PROJECTION_HALF_SCALE = MODEL_PREVIEW_PROJECTION_SCALE / 2,
		MODEL_PREVIEW_PERSPECTIVE_SHIFT = 9,
		SPAN_MASK_LONG_RUN_LENGTH = 255,
		SPAN_MASK_LONG_RUN_THRESHOLD = SPAN_MASK_LONG_RUN_LENGTH + 1,
	};

	float objectRow0X;
	float objectRow0Y;
	float objectRow0Z;
	float objectRow1X;
	float objectRow1Y;
	float objectRow1Z;
	float objectRow2X;
	float objectRow2Y;
	float objectRow2Z;
	uint8_t* auxBuffer;
	uint8_t* maskCursor;
	unsigned int row;
	unsigned int remainingWidth;
	int savedLocalLightsLevel;

	if (g_loadedModels[MODEL_PREVIEW_SLOT] == 0) {
		return 0;
	}

	if (x < 0) {
		width += x;
		x = 0;
	}
	if (y < 0) {
		height += y;
	}
	if (x >= RENDER_SURFACE_MAX_WIDTH) {
		return 0;
	}
	if (y >= RENDER_SURFACE_MAX_HEIGHT) {
		return 0;
	}
	if (y + height > RENDER_SURFACE_MAX_HEIGHT) {
		height = RENDER_SURFACE_MAX_HEIGHT - y;
	}
	if (x + width > RENDER_SURFACE_MAX_WIDTH) {
		width = RENDER_SURFACE_MAX_WIDTH - x;
	}

	g_flightVpWidth = (uint16_t)width;
	g_flightVpMaxX = (uint16_t)(width - 1);
	g_flightVpCenterX = (uint16_t)(width / 2);
	g_flightVpHeight = (uint16_t)height;
	g_flightVpMaxY = (uint16_t)(height - 1);
	g_flightVpCenterY = (uint16_t)(height / 2);
	g_flightVpY = y;
	g_flightVpX = x;
	g_flightVpBaseOffset = (unsigned int)(y * g_surfacePitch + x);
	g_projScaleInt = MODEL_PREVIEW_PROJECTION_SCALE;
	g_projScaleHalfInt = MODEL_PREVIEW_PROJECTION_HALF_SCALE;
	perspShift = MODEL_PREVIEW_PERSPECTIVE_SHIFT;
	g_projAspectY = 0;

	FVIEW_BuildCameraOrient(g_players[g_localPlayer].viewState.viewRoll,
							g_players[g_localPlayer].viewState.viewPitch,
							g_players[g_localPlayer].viewState.viewYaw, 0, 0, 0, NULL);
	FVIEW_SetObjectTransform(g_modelPreviewObject.roll, g_modelPreviewObject.pitch, g_modelPreviewObject.yaw,
							 g_modelPreviewAngleD, NULL);

	g_modelPreviewViewDelta.x =
		(float)(g_modelPreviewObject.world_x - g_players[g_localPlayer].viewState.savedTargetX);
	g_modelPreviewViewDelta.y =
		(float)(g_modelPreviewObject.world_y - g_players[g_localPlayer].viewState.savedTargetY);
	g_modelPreviewViewDelta.z =
		(float)(g_modelPreviewObject.world_z - g_players[g_localPlayer].viewState.savedTargetZ);
	g_modelPreviewMatrix[0] = (float)g_camMatR0_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[1] = (float)g_camMatR1_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[2] = (float)g_camMatR2_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[3] = (float)g_camMatR0_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[4] = (float)g_camMatR1_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[5] = (float)g_camMatR2_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[6] = (float)g_camMatR0_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[7] = (float)g_camMatR1_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[8] = (float)g_camMatR2_Z * g_modelPreviewMatrixQ15ToFloatScale;
	Math3D_RotateVec3(&g_modelPreviewViewDelta.x, g_modelPreviewMatrix);

	objectRow0X = (float)g_objViewMat_R0_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[0] = objectRow0X;
	objectRow0Y = (float)g_objViewMat_R0_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[1] = objectRow0Y;
	objectRow0Z = (float)g_objViewMat_R0_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[2] = objectRow0Z;
	objectRow1X = (float)g_objViewMat_R1_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[3] = objectRow1X;
	objectRow1Y = (float)g_objViewMat_R1_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[4] = objectRow1Y;
	objectRow1Z = (float)g_objViewMat_R1_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[5] = objectRow1Z;
	objectRow2X = (float)g_objViewMat_R2_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[6] = objectRow2X;
	objectRow2Y = (float)g_objViewMat_R2_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[7] = objectRow2Y;
	objectRow2Z = (float)g_objViewMat_R2_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[8] = objectRow2Z;

	g_modelPreviewNegViewDelta.x = -g_modelPreviewViewDelta.x;
	g_modelPreviewNegViewDelta.y = -g_modelPreviewViewDelta.y;
	g_modelPreviewNegViewDelta.z = -g_modelPreviewViewDelta.z;
	g_modelPreviewObjectViewMatrix[0] = objectRow0X;
	g_modelPreviewObjectViewMatrix[1] = objectRow1X;
	g_modelPreviewObjectViewMatrix[2] = objectRow2X;
	g_modelPreviewObjectViewMatrix[3] = objectRow0Y;
	g_modelPreviewObjectViewMatrix[4] = objectRow1Y;
	g_modelPreviewObjectViewMatrix[5] = objectRow2Y;
	g_modelPreviewObjectViewMatrix[6] = objectRow0Z;
	g_modelPreviewObjectViewMatrix[7] = objectRow1Z;
	g_modelPreviewObjectViewMatrix[8] = objectRow2Z;
	Math3D_RotateVec3(&g_modelPreviewNegViewDelta.x, g_modelPreviewObjectViewMatrix);

	if (g_modelPreviewRenderResourcesInitialized == 0) {
		RenderScene_AllocateBuffers();
		if (g_viewportSpanMaskOffset + g_flightVpHeight * ((g_flightVpWidth >> 7) + 2) >
			(int)g_modelPreviewAuxBufferCapacityBytes) {
			if (g_modelPreviewAuxBufferHandle != 0) {
				Memory_FreeHandle(g_modelPreviewAuxBufferHandle);
			}
			g_modelPreviewAuxBufferHandle = Memory_AllocHandle(
				g_viewportSpanMaskOffset + g_flightVpHeight * ((g_flightVpWidth >> 7) + 2), 0);
			g_modelPreviewAuxBufferCapacityBytes =
				g_viewportSpanMaskOffset + g_flightVpHeight * ((g_flightVpWidth >> 7) + 2);
		}
		auxBuffer = (uint8_t*)Memory_LockHandle(g_modelPreviewAuxBufferHandle);
		g_flightAuxBuffer = auxBuffer;
		maskCursor = &auxBuffer[g_viewportSpanMaskOffset];
		for (row = 0; row < g_flightVpHeight; ++row) {
			*maskCursor++ = 1;
			remainingWidth = g_flightVpWidth;
			if (remainingWidth >= SPAN_MASK_LONG_RUN_THRESHOLD) {
				*maskCursor++ = 0;
				remainingWidth -= SPAN_MASK_LONG_RUN_LENGTH;
				if (remainingWidth >= SPAN_MASK_LONG_RUN_THRESHOLD) {
					*maskCursor++ = 0;
					remainingWidth -= SPAN_MASK_LONG_RUN_THRESHOLD;
				}
			}
			*maskCursor++ = (uint8_t)remainingWidth;
		}
		g_modelPreviewRenderResourcesInitialized = 1;
	}

	depthZ = (int)g_modelPreviewViewDelta.z;
	g_modelPreviewObject.mobj->nodeSwitchIndex = (uint8_t)g_nodeSwitchIndex;
	savedLocalLightsLevel = g_localLightsLevel;
	g_localLightsLevel = 0;
	RenderScene_Initialize(1);
#ifdef XVT_MODERN
	XvtRenderCapture_FrontendPreview(g_loadedModels[0], &g_modelPreviewViewDelta.x, g_modelPreviewMatrix,
									 (float)g_modelPreviewScale, (uint16_t)g_nodeSwitchIndex, x, y, width,
									 height);
#endif
	RenderScene_DrawObjectModel(&g_modelPreviewObject);
	sw3d_DrawVisibleFacesToSurface();
	RenderScene_UnlockBuffers();
	g_localLightsLevel = savedLocalLightsLevel;
	return 1;
}

// FUNCTION: XVT 0x42A920
void ModelPreview_ScaleOptNodeTree(OptNode* node, OptimizedPolyObject* opt, double scale) {
	OptNode* resolvedNode;
	int childIndex;

	resolvedNode = node;
	if (resolvedNode == NULL) {
		return;
	}
	while (resolvedNode->nodeType == OPT_NODEREF) {
		resolvedNode = OptModel_ResolveNodeRef(opt, (const char*)resolvedNode->param2);
		if (resolvedNode == NULL) {
			return;
		}
	}

	switch (resolvedNode->nodeType) {
		case OPT_FACEDATA: {
			int count;
			OptPackedFaceData* faceData;
			OptVector* faceNormals;
			FaceTextureGradients* gradients;
			float* points;

			count = resolvedNode->param1;
			faceData = (OptPackedFaceData*)resolvedNode->param2;
			faceNormals = (OptVector*)&faceData->records[count];
			gradients = (FaceTextureGradients*)&faceNormals[count];
			points = (float*)gradients;
			if (count > 0) {
				do {
					points[0] = (float)(points[0] * scale);
					points[1] = (float)(points[1] * scale);
					points[2] = (float)(points[2] * scale);
					points += 3;
					points[0] = (float)(points[0] * scale);
					points[1] = (float)(points[1] * scale);
					points[2] = (float)(points[2] * scale);
					points += 3;
					--count;
				} while (count != 0);
			}
			break;
		}
		case OPT_MESHVERTS: {
			int count;
			float* vertices;

			count = resolvedNode->param1;
			vertices = (float*)resolvedNode->param2;
			if (count > 0) {
				do {
					vertices[0] = (float)(vertices[0] * scale);
					vertices[1] = (float)(vertices[1] * scale);
					vertices[2] = (float)(vertices[2] * scale);
					vertices += 3;
					--count;
				} while (count != 0);
			}
			break;
		}
		case OPT_FACEGROUP: {
			int count;
			float* childScales;

			count = resolvedNode->childCount;
			childScales = (float*)resolvedNode->param2;
			if (count > 0) {
				do {
					*childScales = (float)(*childScales / scale);
					++childScales;
					--count;
				} while (count != 0);
			}
			break;
		}
		default:
			break;
	}

	for (childIndex = 0; childIndex < resolvedNode->childCount; ++childIndex) {
		ModelPreview_ScaleOptNodeTree(resolvedNode->pChildren[childIndex], opt, scale);
	}
}

// FUNCTION: XVT 0x42AA60
void ModelPreview_UnscaleOptNodeTree(OptNode* node, OptimizedPolyObject* opt, double scale) {
	OptNode* resolvedNode;
	int childIndex;

	resolvedNode = node;
	if (resolvedNode == NULL) {
		return;
	}
	while (resolvedNode->nodeType == OPT_NODEREF) {
		resolvedNode = OptModel_ResolveNodeRef(opt, (const char*)resolvedNode->param2);
		if (resolvedNode == NULL) {
			return;
		}
	}

	switch (resolvedNode->nodeType) {
		case OPT_FACEDATA: {
			int count;
			OptPackedFaceData* faceData;
			OptVector* faceNormals;
			FaceTextureGradients* gradients;
			float* points;

			count = resolvedNode->param1;
			faceData = (OptPackedFaceData*)resolvedNode->param2;
			faceNormals = (OptVector*)&faceData->records[count];
			gradients = (FaceTextureGradients*)&faceNormals[count];
			points = (float*)gradients;
			if (count > 0) {
				do {
					points[0] = (float)(points[0] / scale);
					points[1] = (float)(points[1] / scale);
					points[2] = (float)(points[2] / scale);
					points += 3;
					points[0] = (float)(points[0] / scale);
					points[1] = (float)(points[1] / scale);
					points[2] = (float)(points[2] / scale);
					points += 3;
					--count;
				} while (count != 0);
			}
			break;
		}
		case OPT_MESHVERTS: {
			int count;
			float* vertices;

			count = resolvedNode->param1;
			vertices = (float*)resolvedNode->param2;
			if (count > 0) {
				do {
					vertices[0] = (float)(vertices[0] / scale);
					vertices[1] = (float)(vertices[1] / scale);
					vertices[2] = (float)(vertices[2] / scale);
					vertices += 3;
					--count;
				} while (count != 0);
			}
			break;
		}
		case OPT_FACEGROUP: {
			int count;
			float* childScales;

			count = resolvedNode->childCount;
			childScales = (float*)resolvedNode->param2;
			if (count > 0) {
				do {
					*childScales = (float)(*childScales * scale);
					++childScales;
					--count;
				} while (count != 0);
			}
			break;
		}
		default:
			break;
	}

	for (childIndex = 0; childIndex < resolvedNode->childCount; ++childIndex) {
		ModelPreview_UnscaleOptNodeTree(resolvedNode->pChildren[childIndex], opt, scale);
	}
}

// FUNCTION: XVT 0x42AC50
void ModelPreview_ScaleOptRootNodes(OptimizedPolyObject* opt, double scale) {
	int rootIndex;

	for (rootIndex = 0; rootIndex < opt->rootNodeCount; ++rootIndex) {
		ModelPreview_ScaleOptNodeTree(opt->rootNodes[rootIndex], opt, scale);
	}
}

// FUNCTION: XVT 0x42AC90
void ModelPreview_UnscaleOptRootNodes(OptimizedPolyObject* opt, double scale) {
	int rootIndex;

	for (rootIndex = 0; rootIndex < opt->rootNodeCount; ++rootIndex) {
		ModelPreview_UnscaleOptNodeTree(opt->rootNodes[rootIndex], opt, scale);
	}
}

// FUNCTION: XVT 0x42AD10
void ModelPreview_AccumulateOptNodeBounds(OptNode* node, OptimizedPolyObject* object) {
	OptNode* currentNode;
	int vertexCount;
	float* vertex;
	int childIndex;

	currentNode = node;
	if (currentNode != NULL) {
		while (currentNode->nodeType == OPT_NODEREF) {
			currentNode = OptModel_ResolveNodeRef(object, (const char*)currentNode->param2);
			if (currentNode == NULL)
				return;
		}

		if (currentNode->nodeType == OPT_MESHVERTS) {
			vertexCount = currentNode->param1;
			vertex = currentNode->param2;
			if (vertexCount > 0) {
				do {
					if (vertex[0] > g_modelPreviewBoundsMaxX)
						g_modelPreviewBoundsMaxX = vertex[0];
					if (vertex[0] < g_modelPreviewBoundsMinX)
						g_modelPreviewBoundsMinX = vertex[0];
					if (vertex[1] > g_modelPreviewBoundsMaxY)
						g_modelPreviewBoundsMaxY = vertex[1];
					if (vertex[1] < g_modelPreviewBoundsMinY)
						g_modelPreviewBoundsMinY = vertex[1];
					if (vertex[2] > g_modelPreviewBoundsMaxZ)
						g_modelPreviewBoundsMaxZ = vertex[2];
					if (vertex[2] < g_modelPreviewBoundsMinZ)
						g_modelPreviewBoundsMinZ = vertex[2];
					vertex += 3;
					--vertexCount;
				} while (vertexCount != 0);
			}
		}

		childIndex = 0;
		while (currentNode->childCount > childIndex) {
			ModelPreview_AccumulateOptNodeBounds(currentNode->pChildren[childIndex], object);
			++childIndex;
		}
	}
}

// FUNCTION: XVT 0x42AE30
double ModelPreview_ComputeOptBoundsExtent(OptimizedPolyObject* object, int axis) {
	int rootNodeIndex;
	double result;

	g_modelPreviewBoundsMaxX = 0.0f;
	g_modelPreviewBoundsMinX = 0.0f;
	g_modelPreviewBoundsMaxY = 0.0f;
	g_modelPreviewBoundsMinY = 0.0f;
	g_modelPreviewBoundsMaxZ = 0.0f;
	g_modelPreviewBoundsMinZ = 0.0f;
	for (rootNodeIndex = 0; rootNodeIndex < object->rootNodeCount; ++rootNodeIndex) {
		ModelPreview_AccumulateOptNodeBounds(object->rootNodes[rootNodeIndex], object);
	}

	g_modelPreviewBoundsMaxX -= g_modelPreviewBoundsMinX;
	g_modelPreviewBoundsMaxY -= g_modelPreviewBoundsMinY;
	g_modelPreviewBoundsMaxZ -= g_modelPreviewBoundsMinZ;
	if (axis == 0) {
		if (g_modelPreviewBoundsMaxY >= g_modelPreviewBoundsMaxX ||
			g_modelPreviewBoundsMaxZ >= g_modelPreviewBoundsMaxX) {
			if (g_modelPreviewBoundsMaxY <= g_modelPreviewBoundsMaxX ||
				g_modelPreviewBoundsMaxZ >= g_modelPreviewBoundsMaxY) {
				result = g_modelPreviewBoundsMaxZ;
			} else {
				result = g_modelPreviewBoundsMaxY;
			}
		} else {
			result = g_modelPreviewBoundsMaxX;
		}
	} else {
		if (axis == 1)
			result = g_modelPreviewBoundsMaxX;
		if (axis == 2)
			result = g_modelPreviewBoundsMaxY;
		if (axis == 3)
			result = g_modelPreviewBoundsMaxZ;
	}
	return result;
}

// FUNCTION: XVT 0x42AF90
int ModelPreview_ResetViewAndRenderState(void) {
	PlayerData* player = &g_players[g_localPlayer];

	g_projOffsetY = 0;
	player->viewState.savedTargetX = 0;
	player->viewState.savedTargetY = -1280;
	player->viewState.savedTargetZ = 0;
	player->viewState.viewRoll = 0;
	player->viewState.viewPitch = 0x4000;
	player->viewState.viewYaw = 0;
	g_lodDistanceScale = 1.0f;
	g_mipLodScale = 1.0f;
	g_localLightsLevel = 1;
	g_specularEnabled = 1;
	g_keepFullResTextures = 1;
	g_dirLightingEnabled = 1;
	g_ditheringEnabled = 1;
	return 1;
}

// FUNCTION: XVT 0x42B010
void ModelPreview_SetWhiteDirectionalLight(int x, int y, int z) {
	double lightX;
	double lightY;
	double lightZ;
	double invLength;

	y = -y;
	lightX = x;
	lightY = y;
	lightZ = z;
	invLength = g_modelPreviewInvLengthNumerator / sqrt(lightX * lightX + lightY * lightY + lightZ * lightZ);
	lightX *= invLength;
	lightY *= invLength;
	lightZ *= invLength;
	g_modelPreviewLightDirectionX = (int16_t)(int)(lightX * g_modelPreviewLightDirectionQ15Scale);
	g_modelPreviewLightDirectionY = (int16_t)(int)(lightY * g_modelPreviewLightDirectionQ15Scale);
	g_modelPreviewLightDirectionZ = (int16_t)(int)(lightZ * g_modelPreviewLightDirectionQ15Scale);
}

// FUNCTION: XVT 0x42B090
void ModelPreview_SetObjectEulerDegrees(float pitchDeg, float yawDeg, float rollDeg) {
	double angle;

	angle = pitchDeg;
	g_modelPreviewObject.pitch = (int16_t)(int)(angle * g_degreesToQ16AngleScale);
	angle = yawDeg;
	g_modelPreviewObject.yaw = (int16_t)(int)(angle * g_degreesToQ16AngleScale);
	angle = rollDeg;
	g_modelPreviewObject.roll = (int16_t)(int)(angle * g_degreesToQ16AngleScale);
}

// FUNCTION: XVT 0x42B0D0
void ModelPreview_SetNodeSwitchIndex(int nodeSwitchIndex) { g_nodeSwitchIndex = nodeSwitchIndex; }

// FUNCTION: XVT 0x42B0E0
void ModelPreview_SetObjectWorldPosition(int x, int y, int z) {
	g_modelPreviewObject.world_x = x;
	g_modelPreviewObject.world_y = y;
	g_modelPreviewObject.world_z = z;
}

// FUNCTION: XVT 0x42B100
void ModelPreview_SaveState(void) {
	strcpy(g_savedModelPreviewModelFileName, g_modelPreviewOptFileName);
	g_savedModelPreviewWorldX = g_modelPreviewObject.world_x;
	g_savedModelPreviewWorldY = g_modelPreviewObject.world_y;
	g_savedModelPreviewWorldZ = g_modelPreviewObject.world_z;
	g_savedModelPreviewNodeSwitchIndex = g_nodeSwitchIndex;
	g_savedModelPreviewPitch = g_modelPreviewObject.pitch;
	g_savedModelPreviewYaw = g_modelPreviewObject.yaw;
	g_savedModelPreviewRoll = g_modelPreviewObject.roll;
	g_savedModelPreviewLightDirectionX = (int16_t)g_modelPreviewLightDirectionX;
	g_savedModelPreviewLightDirectionY = (int16_t)g_modelPreviewLightDirectionY;
	g_savedModelPreviewLightDirectionZ = (int16_t)g_modelPreviewLightDirectionZ;
	g_savedModelPreviewAngleD = g_modelPreviewAngleD;
}

// FUNCTION: XVT 0x42B1B0
void ModelPreview_RestoreState(void) {
	ModelPreview_LoadModel(g_savedModelPreviewModelFileName);
	g_modelPreviewObject.world_x = g_savedModelPreviewWorldX;
	g_modelPreviewObject.world_y = g_savedModelPreviewWorldY;
	g_modelPreviewObject.world_z = g_savedModelPreviewWorldZ;
	g_modelPreviewObject.pitch = g_savedModelPreviewPitch;
	g_modelPreviewObject.yaw = g_savedModelPreviewYaw;
	g_modelPreviewObject.roll = g_savedModelPreviewRoll;
	g_nodeSwitchIndex = g_savedModelPreviewNodeSwitchIndex;
	g_modelPreviewLightDirectionX = g_savedModelPreviewLightDirectionX;
	g_modelPreviewLightDirectionY = g_savedModelPreviewLightDirectionY;
	g_modelPreviewLightDirectionZ = g_savedModelPreviewLightDirectionZ;
	g_modelPreviewAngleD = g_savedModelPreviewAngleD;
}

// FUNCTION: XVT 0x42B250
void ModelPreview_SetObjectAngleDDegrees(float angleDeg) {
	double angle = angleDeg;

	g_modelPreviewAngleD = (int16_t)(int)(angle * g_degreesToQ16AngleScale);
}

// FUNCTION: XVT 0x42B270
int ModelPreview_GetDisplayedSizeMeters(void) {
	double displayedSize = g_modelPreviewBoundsExtent;

	displayedSize *= g_modelPreviewMetersScale;
	displayedSize *= g_modelPreviewQ16Scale;
	return (int)displayedSize;
}

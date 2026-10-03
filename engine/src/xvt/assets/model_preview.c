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

/* 1 / 32767, which turns a 1.15 fixed point matrix entry into a float in
 * ModelPreview_RenderViewport. */
// GLOBAL: XVT 0x518100
const float g_modelPreviewMatrixQ15ToFloatScale = 0.000030518509f;
/* The 1.0 ModelPreview_SetLightDirection divides by the vector's length. */
// GLOBAL: XVT 0x518110
static const double g_modelPreviewInvLengthNumerator = 1.0;
/* Size, in model units, that ModelPreview_LoadModel scales a model's largest
 * extent to: 500. */
// GLOBAL: XVT 0x5180F8
const double g_modelPreviewTargetBoundsExtent = 500.0;
/* 32767, which turns a unit light direction into 1.15 fixed point in
 * ModelPreview_SetLightDirection. */
// GLOBAL: XVT 0x518118
static const double g_modelPreviewLightDirectionQ15Scale = 32767.0;
/* 65,536 / 360: degrees to angle units. */
// GLOBAL: XVT 0x518120
static const double g_degreesToQ16AngleScale = 182.04444444444445;
/* 1600, which ModelPreview_GetDisplayedSizeMeters multiplies the extent by. */
// GLOBAL: XVT 0x518128
static const double g_modelPreviewMetersScale = 1600.0;
/* 1 / 65,536, which ModelPreview_GetDisplayedSizeMeters multiplies the extent
 * by. */
// GLOBAL: XVT 0x518130
static const double g_modelPreviewQ16Scale = 0.0000152587890625;
/* Angle about the up axis, 65,536 a full circle, that
 * ModelPreview_RenderViewport passes to FVIEW_SetObjectTransform. Set by
 * ModelPreview_SetObjectUpAxisAngleDegrees and ModelPreview_RestoreState; 0
 * after each successful load. */
// GLOBAL: XVT 0x520EC0
int16_t g_modelPreviewUpAxisAngle;
/* The preview model's block, locked by ModelPreview_LoadModel; NULL until the
 * first load. The modern XvtFrontendTask_Shutdown sets it back to NULL. */
// GLOBAL: XVT 0x520EC4
OptimizedPolyObject *g_modelPreviewModelData = NULL;
/* Nothing sets this flag, and ModelPreview_LoadModel clears it through ModelPreview_FreeResources before
 * testing it, so every load resets the preview object, view and light. */
/* Only ModelPreview_FreeResources writes it, and it writes 0. */
// GLOBAL: XVT 0x520EC8
int g_modelPreviewSkipSceneReset = 0;
/* 1 once ModelPreview_RenderViewport has allocated the render buffers and the
 * span mask; ModelPreview_FreeResources frees the buffers and sets it to 0. */
// GLOBAL: XVT 0x520ECC
int g_modelPreviewRenderResourcesInitialized = 0;
/* Memory handle of the preview's span mask buffer: ModelPreview_RenderViewport
 * allocates it, regrows it when too small and points g_flightAuxBuffer at it.
 * The modern XvtFrontendTask_Shutdown sets it to 0. */
// GLOBAL: XVT 0x520ED0
uint16_t g_modelPreviewAuxBufferHandle = 0;
/* Bytes allocated for g_modelPreviewAuxBufferHandle; written by the same two
 * functions. */
// GLOBAL: XVT 0x520ED4
unsigned int g_modelPreviewAuxBufferCapacityBytes = 0;
/* The preview model's largest extent, in model units before scaling, from
 * ModelPreview_ComputeOptBoundsExtent; set by each load. */
// GLOBAL: XVT 0x520ED8
double g_modelPreviewBoundsExtent;
/* Which child of an OPT_NODESWITCH node is drawn: RenderScene_DrawModelNode
 * takes g_nodeSwitchIndex + 1, cut to the node's child count, as its selection.
 * RenderScene_DrawObjectModel and RenderScene_DrawSelectedRootNode set it from
 * the drawn object's mobj->nodeSwitchIndex (0 without a mobj);
 * ModelPreview_SetNodeSwitchIndex and ModelPreview_RestoreState set it for the
 * preview, which ModelPreview_RenderViewport copies into the preview object. */
// GLOBAL: XVT 0x5233A0
int g_nodeSwitchIndex;
/* The object the preview draws. Its objectType is 0, so it draws
 * g_loadedModels[0]; its mobj is g_modelPreviewMobileObject. Each successful
 * load resets its position and angles to 0. */
// GLOBAL: XVT 0x5561E8
ObjectRecord g_modelPreviewObject;
/* Factor ModelPreview_LoadModel scaled the preview model by:
 * g_modelPreviewTargetBoundsExtent over g_modelPreviewBoundsExtent. */
// GLOBAL: XVT 0x5561E0
double g_modelPreviewScale = 0.0;
/* The preview object's mobile part, cleared by each successful load, with
 * g_modelPreviewCraftScratch as its craft. */
// GLOBAL: XVT 0x556218
MobileObject g_modelPreviewMobileObject = {0};
/* Name of the preview's OPT file, set by ModelPreview_LoadModel once the file
 * is loaded; ModelPreview_SaveState copies it. */
// GLOBAL: XVT 0x555CC8
char g_modelPreviewOptFileName[128];
/* Preview pitch saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555CC0
int16_t g_savedModelPreviewPitch;
/* Preview yaw saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555CC4
int16_t g_savedModelPreviewYaw;
/* Largest z over the model's vertices, starting from 0, while
 * ModelPreview_ComputeOptBoundsExtent runs; afterwards the z extent. */
// GLOBAL: XVT 0x555D48
static float g_modelPreviewBoundsMaxZ = 0.0f;
/* Smallest z over the model's vertices, starting from 0, for
 * ModelPreview_ComputeOptBoundsExtent. */
// GLOBAL: XVT 0x555D4C
static float g_modelPreviewBoundsMinZ = 0.0f;
/* Largest x over the model's vertices, starting from 0, while
 * ModelPreview_ComputeOptBoundsExtent runs; afterwards the x extent. */
// GLOBAL: XVT 0x555D50
static float g_modelPreviewBoundsMaxX = 0.0f;
/* Smallest x over the model's vertices, starting from 0, for
 * ModelPreview_ComputeOptBoundsExtent. */
// GLOBAL: XVT 0x555D54
static float g_modelPreviewBoundsMinX = 0.0f;
/* Light direction z saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555D58
int16_t g_savedModelPreviewLightDirectionZ;
/* Preview world y saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555D5C
int g_savedModelPreviewWorldY;
/* Preview world z saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555D60
int g_savedModelPreviewWorldZ;
/* Preview world x saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555D64
int g_savedModelPreviewWorldX;
/* Largest y over the model's vertices, starting from 0, while
 * ModelPreview_ComputeOptBoundsExtent runs; afterwards the y extent. */
// GLOBAL: XVT 0x555D68
static float g_modelPreviewBoundsMaxY = 0.0f;
/* Smallest y over the model's vertices, starting from 0, for
 * ModelPreview_ComputeOptBoundsExtent. */
// GLOBAL: XVT 0x555D6C
static float g_modelPreviewBoundsMinY = 0.0f;
/* Craft record the preview's mobile object points at, cleared by each
 * successful load. */
// GLOBAL: XVT 0x555D78
CraftData g_modelPreviewCraftScratch = {0};
/* Light direction y saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555D70
int16_t g_savedModelPreviewLightDirectionY;
/* Light direction x saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x555D74
int16_t g_savedModelPreviewLightDirectionX;
/* g_nodeSwitchIndex saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x5561DC
int g_savedModelPreviewNodeSwitchIndex;
/* g_modelPreviewUpAxisAngle saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x55620C
int16_t g_savedModelPreviewUpAxisAngle;
/* Preview roll saved by ModelPreview_SaveState, put back by
 * ModelPreview_RestoreState. */
// GLOBAL: XVT 0x556210
int16_t g_savedModelPreviewRoll;
/* Preview file name saved by ModelPreview_SaveState; ModelPreview_RestoreState
 * loads it again. */
// GLOBAL: XVT 0x5562D0
char g_savedModelPreviewModelFileName[128];
/* X of the light direction in world axes, 1.15 fixed point. Flight start sets
 * all three to DEFAULT_MODEL_LIGHT_DIRECTION (Flight_MainLoop in the original
 * build, XvtFlightLoading_MissionSetup in the modern one);
 * ModelPreview_SetLightDirection and ModelPreview_RestoreState set them for the
 * preview. FVIEW_ComputeObjectViewMatrix turns them into the object's light
 * direction. */
// GLOBAL: XVT 0x9D12EC
int g_worldLightDirectionX;
/* Y of the light direction in world axes; written and read like
 * g_worldLightDirectionX. */
// GLOBAL: XVT 0x9D12F0
int g_worldLightDirectionY;
/* Z of the light direction in world axes; written and read like
 * g_worldLightDirectionX. */
// GLOBAL: XVT 0x9D1304
int g_worldLightDirectionZ;
/* The preview object's position less the camera's, turned into camera axes;
 * ModelPreview_RenderViewport sets it each draw and makes its z
 * g_viewSpaceDepth. The modern build passes it to
 * XvtRenderCapture_FrontendPreview. */
// GLOBAL: XVT 0xA60710
OptVector g_modelPreviewViewDelta = {0.0f, 0.0f, 0.0f};
/* ModelPreview_RenderViewport's float copy of a 1.15 matrix: first the camera
 * rotation, used to turn g_modelPreviewViewDelta, then the object-to-view
 * rotation, which the modern build passes to
 * XvtRenderCapture_FrontendPreview. */
// GLOBAL: XVT 0xA6071C
float g_modelPreviewMatrix[9] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
				 0.0f, 0.0f, 0.0f, 0.0f};
/* Minus g_modelPreviewViewDelta turned by g_modelPreviewObjectViewMatrix, set
 * each draw by ModelPreview_RenderViewport; nothing reads it. */
// GLOBAL: XVT 0xA60740
OptVector g_modelPreviewNegViewDelta = {0.0f, 0.0f, 0.0f};
/* The object-to-view rotation transposed, set each draw by
 * ModelPreview_RenderViewport only to turn g_modelPreviewNegViewDelta. */
// GLOBAL: XVT 0xA6074C
float g_modelPreviewObjectViewMatrix[9] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
					   0.0f, 0.0f, 0.0f, 0.0f};

/* Loads a model for the frontend preview into g_loadedModels[0] and returns 1,
 * or 0. It frees the preview's render buffers, resets the view and render
 * settings, turns g_mipmappingEnabled on, sets g_loadedModels[0] to 0 when
 * g_modelPreviewModelData is NULL, and opens the name with ".opt" in place of
 * its extension. The modern build returns 0 for a NULL name, one of 256 or more
 * characters, an extension other than ".opt", a file that does not open or a
 * load that fails. The original build cuts the name at its first '.' and, when
 * the OPT file does not open, imports the ".iv" file as OptModel_LoadHandle
 * does, saving it as the OPT file. An OPT file is loaded with
 * OptModel_LoadFileToHandle after the old slot's handle is freed, as an import
 * frees it too, and a runtime copy built with g_flightBytesPerPixel set to 2,
 * which it stays, takes the slot. It locks the copy into
 * g_modelPreviewModelData, scales it so its largest extent is
 * g_modelPreviewTargetBoundsExtent (g_modelPreviewBoundsExtent,
 * g_modelPreviewScale), sets g_transformLightDirectionToObjectSpace, and resets
 * the preview object and the view, and the light to (1, 1, 1). The modern build
 * also registers the copy for its renderer. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x429E70
int ModelPreview_LoadModel(const char *modelFileName)
{
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
	char *extension;
#else
	int extensionIndex;
#endif
	XvtFile *stream;
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
	if (!modelFileName || strlen(modelFileName) >= sizeof(baseName)) {
		return 0;
	}
	strcpy(baseName, modelFileName);
	extension = strrchr(baseName, '.');
	if (extension) {
		if (strcasecmp(extension, ".opt") != 0) {
			return 0;
		}
		*extension = '\0';
	}
	if (strlen(baseName) + sizeof(".opt") > sizeof(fileName)) {
		return 0;
	}
#else
	strcpy(baseName, modelFileName);
	for (extensionIndex = 0; baseName[extensionIndex] != '.';
	     ++extensionIndex) {
	}
	baseName[extensionIndex] = '\0';
#endif

	strcpy(fileName, baseName);
	strcat(fileName, ".opt");
	FeDiskIo_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 0);
	stream = g_stream;
#ifdef XVT_MODERN
	if (!stream) {
		return 0;
	}
	File_Close(stream);
	g_stream = NULL;
	if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0) {
		Memory_FreeHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
	}
	g_loadedModels[MODEL_PREVIEW_SLOT] =
		OptModel_LoadFileToHandle(fileName);
#else
	if (stream == NULL) {
		strcpy(fileName, baseName);
		strcat(fileName, ".iv");
		FeDiskIo_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 0);
		stream = g_stream;
		if (stream == NULL) {
			return 0;
		}
		if (File_Scanf(stream, "%256s", fileName) != 1) {
			return 0;
		}
		if (_strnicmp(fileName, "#inventor",
			      INVENTOR_SIGNATURE_LENGTH) != 0) {
			return 0;
		}
		if (File_Scanf(stream, " %256s", fileName) != 1) {
			return 0;
		}
		if (File_Scanf(stream, " %256s", fileName) != 1) {
			return 0;
		}
		if (_strnicmp(fileName, "ascii", INVENTOR_ASCII_LABEL_LENGTH) ==
		    0) {
			if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0) {
				Memory_FreeHandle(
					g_loadedModels[MODEL_PREVIEW_SLOT]);
			}
			importedHandle =
				OptModel_LoadInventorAsciiToHandle(stream);
		} else {
			if (_strnicmp(fileName, "binary",
				      INVENTOR_BINARY_LABEL_LENGTH) == 0) {
				if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0) {
					Memory_FreeHandle(
						g_loadedModels
							[MODEL_PREVIEW_SLOT]);
				}
				importedHandle =
					OptModel_LoadInventorBinaryToHandle(
						stream);
			} else {
				File_RawClose(stream);
				return 0;
			}
		}
		File_RawClose(stream);
		packedHandle =
			OptModel_ConvertImportedHandleToPacked(importedHandle);
		strcpy(fileName, baseName);
		strcat(fileName, ".opt");
		OptModel_SaveHandleToFile(fileName, packedHandle);
		g_loadedModels[MODEL_PREVIEW_SLOT] = packedHandle;
	} else {
		File_RawClose(stream);
		if (g_loadedModels[MODEL_PREVIEW_SLOT] != 0) {
			Memory_FreeHandle(g_loadedModels[MODEL_PREVIEW_SLOT]);
		}
		g_loadedModels[MODEL_PREVIEW_SLOT] =
			OptModel_LoadFileToHandle(fileName);
	}
#endif

#ifdef XVT_MODERN
	if (!g_loadedModels[MODEL_PREVIEW_SLOT]) {
		return 0;
	}
#endif
	strcpy(g_modelPreviewOptFileName, baseName);
	strcat(g_modelPreviewOptFileName, ".opt");
	if (g_mipmappingEnabled != 0) {
		g_flightBytesPerPixel = 2;
		g_loadedModels[MODEL_PREVIEW_SLOT] =
			OptModel_CreateRuntimeHandle(
				g_loadedModels[MODEL_PREVIEW_SLOT]);
		g_flightBytesPerPixel = 2;
	}
#ifdef XVT_MODERN
	if (!g_loadedModels[MODEL_PREVIEW_SLOT]) {
		return 0;
	}
#endif
#ifdef XVT_MODERN
	XvtRenderAssets_RegisterOpt(g_loadedModels[MODEL_PREVIEW_SLOT],
				    g_modelPreviewOptFileName);
	XvtRenderAssets_BindType(MODEL_PREVIEW_SLOT,
				 g_loadedModels[MODEL_PREVIEW_SLOT]);
#endif
	g_modelPreviewModelData = (OptimizedPolyObject *)Memory_LockHandle(
		g_loadedModels[MODEL_PREVIEW_SLOT]);
	if (g_modelPreviewModelData->selfMarker != g_modelPreviewModelData) {
		OptModel_AdjustOptimizedPolyObjectPointers(
			g_modelPreviewModelData);
	}
	/* The second argument is an axis, not a slot: MODEL_PREVIEW_SLOT passes 0, the largest extent. */
	g_modelPreviewBoundsExtent = ModelPreview_ComputeOptBoundsExtent(
		g_modelPreviewModelData, MODEL_PREVIEW_SLOT);
	g_modelPreviewScale =
		g_modelPreviewTargetBoundsExtent / g_modelPreviewBoundsExtent;
	ModelPreview_ScaleOptRootNodes(g_modelPreviewModelData,
				       g_modelPreviewScale);
	g_transformLightDirectionToObjectSpace = 1;

	if (g_modelPreviewSkipSceneReset == 0) {
		memset(&g_modelPreviewMobileObject, 0,
		       sizeof(g_modelPreviewMobileObject));
		memset(&g_modelPreviewCraftScratch, 0,
		       sizeof(g_modelPreviewCraftScratch));
		g_modelPreviewObject.objectType = 0;
		g_modelPreviewObject.world_x = 0;
		g_modelPreviewObject.pitch = 0;
		g_modelPreviewObject.world_y = 0;
		g_modelPreviewObject.yaw = 0;
		g_modelPreviewObject.world_z = 0;
		g_modelPreviewObject.mobj = &g_modelPreviewMobileObject;
		g_modelPreviewMobileObject.pCraft = &g_modelPreviewCraftScratch;
		g_modelPreviewObject.roll = 0;
		g_modelPreviewUpAxisAngle = 0;
		ModelPreview_ResetViewAndRenderState();
		ModelPreview_SetLightDirection(1, 1, 1);
	}
	return 1;
}

/* Frees the render buffers when g_modelPreviewRenderResourcesInitialized is set
 * and clears it; also clears g_modelPreviewSkipSceneReset. */
// FUNCTION: XVT 0x42A350
void ModelPreview_FreeResources(void)
{
	if (g_modelPreviewRenderResourcesInitialized != 0) {
		RenderScene_FreeBuffers();
		g_modelPreviewRenderResourcesInitialized = 0;
	}
	g_modelPreviewSkipSceneReset = 0;
}

/* Draws the preview model into the viewport at x, y, width by height and
 * returns 1; returns 0 when g_loadedModels[0] is 0, x is 1024 or more, or y is
 * 768 or more. A negative x moves the viewport's left edge to 0 and takes it
 * off the width; a negative y is taken off the height but kept. The right and
 * bottom edges are cut to 1024 by 768. It sets the flight viewport and
 * projection globals (scale 512, perspective shift 9), builds the camera from
 * the local player's view state and the object's transform with
 * g_modelPreviewUpAxisAngle, and sets g_modelPreviewViewDelta and the preview
 * matrices. The first draw after a load allocates the render buffers and the
 * span mask in g_modelPreviewAuxBufferHandle: per row the byte 1, then a 0
 * while the width left is 256 or more, taking 255 the first time and 256 the
 * second (twice at most), then the width left. It draws with local lights off,
 * the object's nodeSwitchIndex from g_nodeSwitchIndex, through
 * RenderScene_DrawObjectModel and sw3d_DrawVisibleFacesToSurface. The modern
 * build also records the draw for its renderer. The arguments after height are
 * ignored. */
// FUNCTION: XVT 0x42A380
int ModelPreview_RenderViewport(int x, int y, int width, int height, ...)
{
	enum {
		MODEL_PREVIEW_SLOT = 0,
		RENDER_SURFACE_MAX_WIDTH = 1024,
		RENDER_SURFACE_MAX_HEIGHT = 768,
		MODEL_PREVIEW_PROJECTION_SCALE = 512,
		MODEL_PREVIEW_PROJECTION_HALF_SCALE =
			MODEL_PREVIEW_PROJECTION_SCALE / 2,
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
	uint8_t *auxBuffer;
	uint8_t *maskCursor;
	unsigned int row;
	unsigned int remainingWidth;
	int savedLocalLightsEnabled;

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
	g_perspectiveShift = MODEL_PREVIEW_PERSPECTIVE_SHIFT;
	g_projAspectY = 0;

	FVIEW_BuildCameraOrient(g_players[g_localPlayer].viewState.viewRoll,
				g_players[g_localPlayer].viewState.viewPitch,
				g_players[g_localPlayer].viewState.viewYaw, 0,
				0, 0, NULL);
	FVIEW_SetObjectTransform(
		g_modelPreviewObject.roll, g_modelPreviewObject.pitch,
		g_modelPreviewObject.yaw, g_modelPreviewUpAxisAngle, NULL);

	g_modelPreviewViewDelta.x =
		(float)(g_modelPreviewObject.world_x -
			g_players[g_localPlayer].viewState.cameraWorldX);
	g_modelPreviewViewDelta.y =
		(float)(g_modelPreviewObject.world_y -
			g_players[g_localPlayer].viewState.cameraWorldY);
	g_modelPreviewViewDelta.z =
		(float)(g_modelPreviewObject.world_z -
			g_players[g_localPlayer].viewState.cameraWorldZ);
	g_modelPreviewMatrix[0] =
		(float)g_camMatR0_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[1] =
		(float)g_camMatR1_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[2] =
		(float)g_camMatR2_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[3] =
		(float)g_camMatR0_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[4] =
		(float)g_camMatR1_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[5] =
		(float)g_camMatR2_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[6] =
		(float)g_camMatR0_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[7] =
		(float)g_camMatR1_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[8] =
		(float)g_camMatR2_Z * g_modelPreviewMatrixQ15ToFloatScale;
	Math3D_RotateVec3(&g_modelPreviewViewDelta.x, g_modelPreviewMatrix);

	objectRow0X =
		(float)g_objViewMat_R0_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[0] = objectRow0X;
	objectRow0Y =
		(float)g_objViewMat_R0_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[1] = objectRow0Y;
	objectRow0Z =
		(float)g_objViewMat_R0_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[2] = objectRow0Z;
	objectRow1X =
		(float)g_objViewMat_R1_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[3] = objectRow1X;
	objectRow1Y =
		(float)g_objViewMat_R1_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[4] = objectRow1Y;
	objectRow1Z =
		(float)g_objViewMat_R1_Z * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[5] = objectRow1Z;
	objectRow2X =
		(float)g_objViewMat_R2_X * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[6] = objectRow2X;
	objectRow2Y =
		(float)g_objViewMat_R2_Y * g_modelPreviewMatrixQ15ToFloatScale;
	g_modelPreviewMatrix[7] = objectRow2Y;
	objectRow2Z =
		(float)g_objViewMat_R2_Z * g_modelPreviewMatrixQ15ToFloatScale;
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
	Math3D_RotateVec3(&g_modelPreviewNegViewDelta.x,
			  g_modelPreviewObjectViewMatrix);

	if (g_modelPreviewRenderResourcesInitialized == 0) {
		RenderScene_AllocateBuffers();
		if (g_viewportSpanMaskOffset +
			    g_flightVpHeight * ((g_flightVpWidth >> 7) + 2) >
		    (int)g_modelPreviewAuxBufferCapacityBytes) {
			if (g_modelPreviewAuxBufferHandle != 0) {
				Memory_FreeHandle(
					g_modelPreviewAuxBufferHandle);
			}
			g_modelPreviewAuxBufferHandle = Memory_AllocHandle(
				g_viewportSpanMaskOffset +
					g_flightVpHeight *
						((g_flightVpWidth >> 7) + 2),
				0);
			g_modelPreviewAuxBufferCapacityBytes =
				g_viewportSpanMaskOffset +
				g_flightVpHeight * ((g_flightVpWidth >> 7) + 2);
		}
		auxBuffer = (uint8_t *)Memory_LockHandle(
			g_modelPreviewAuxBufferHandle);
		g_flightAuxBuffer = auxBuffer;
		maskCursor = &auxBuffer[g_viewportSpanMaskOffset];
		for (row = 0; row < g_flightVpHeight; ++row) {
			*maskCursor++ = 1;
			remainingWidth = g_flightVpWidth;
			if (remainingWidth >= SPAN_MASK_LONG_RUN_THRESHOLD) {
				*maskCursor++ = 0;
				remainingWidth -= SPAN_MASK_LONG_RUN_LENGTH;
				if (remainingWidth >=
				    SPAN_MASK_LONG_RUN_THRESHOLD) {
					*maskCursor++ = 0;
					remainingWidth -=
						SPAN_MASK_LONG_RUN_THRESHOLD;
				}
			}
			*maskCursor++ = (uint8_t)remainingWidth;
		}
		g_modelPreviewRenderResourcesInitialized = 1;
	}

	g_viewSpaceDepth = (int)g_modelPreviewViewDelta.z;
	g_modelPreviewObject.mobj->nodeSwitchIndex = (uint8_t)g_nodeSwitchIndex;
	savedLocalLightsEnabled = g_localLightsEnabled;
	g_localLightsEnabled = 0;
	RenderScene_Initialize(1);
#ifdef XVT_MODERN
	XvtRenderCapture_FrontendPreview(
		g_loadedModels[0], &g_modelPreviewViewDelta.x,
		g_modelPreviewMatrix, (float)g_modelPreviewScale,
		(uint16_t)g_nodeSwitchIndex, x, y, width, height);
#endif
	RenderScene_DrawObjectModel(&g_modelPreviewObject);
	sw3d_DrawVisibleFacesToSurface();
	RenderScene_UnlockBuffers();
	g_localLightsEnabled = savedLocalLightsEnabled;
	return 1;
}

/* Multiplies by scale every vertex of each OPT_MESHVERTS node at or below node
 * and the two texture gradient vectors of each face of each OPT_FACEDATA node,
 * and divides by scale the first childCount floats of each OPT_FACEGROUP node.
 * Follows OPT_NODEREF links and stops at one that does not resolve; a node
 * reached through two links is scaled twice. Other face node types keep their
 * gradients. */
// FUNCTION: XVT 0x42A920
void ModelPreview_ScaleOptNodeTree(OptNode *node, OptimizedPolyObject *opt,
				   double scale)
{
	OptNode *resolvedNode;
	int childIndex;

	resolvedNode = node;
	if (resolvedNode == NULL) {
		return;
	}
	while (resolvedNode->nodeType == OPT_NODEREF) {
		resolvedNode = OptModel_ResolveNodeRef(
			opt, (const char *)resolvedNode->payload);
		if (resolvedNode == NULL) {
			return;
		}
	}

	switch (resolvedNode->nodeType) {
	case OPT_FACEDATA: {
		int count;
		OptPackedFaceData *faceData;
		OptVector *faceNormals;
		FaceTextureGradients *gradients;
		float *points;

		count = resolvedNode->payloadCount;
		faceData = (OptPackedFaceData *)resolvedNode->payload;
		faceNormals = (OptVector *)&faceData->records[count];
		gradients = (FaceTextureGradients *)&faceNormals[count];
		points = (float *)gradients;
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
		float *vertices;

		count = resolvedNode->payloadCount;
		vertices = (float *)resolvedNode->payload;
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
		float *lodThresholds;

		count = resolvedNode->childCount;
		lodThresholds = (float *)resolvedNode->payload;
		if (count > 0) {
			do {
				*lodThresholds =
					(float)(*lodThresholds / scale);
				++lodThresholds;
				--count;
			} while (count != 0);
		}
		break;
	}
	default:
		break;
	}

	for (childIndex = 0; childIndex < resolvedNode->childCount;
	     ++childIndex) {
		ModelPreview_ScaleOptNodeTree(
			resolvedNode->pChildren[childIndex], opt, scale);
	}
}

/* Undoes ModelPreview_ScaleOptNodeTree: divides where it multiplies and
 * multiplies where it divides. Only ModelPreview_UnscaleOptRootNodes calls
 * this, and nothing calls that. */
// FUNCTION: XVT 0x42AA60
void ModelPreview_UnscaleOptNodeTree(OptNode *node, OptimizedPolyObject *opt,
				     double scale)
{
	OptNode *resolvedNode;
	int childIndex;

	resolvedNode = node;
	if (resolvedNode == NULL) {
		return;
	}
	while (resolvedNode->nodeType == OPT_NODEREF) {
		resolvedNode = OptModel_ResolveNodeRef(
			opt, (const char *)resolvedNode->payload);
		if (resolvedNode == NULL) {
			return;
		}
	}

	switch (resolvedNode->nodeType) {
	case OPT_FACEDATA: {
		int count;
		OptPackedFaceData *faceData;
		OptVector *faceNormals;
		FaceTextureGradients *gradients;
		float *points;

		count = resolvedNode->payloadCount;
		faceData = (OptPackedFaceData *)resolvedNode->payload;
		faceNormals = (OptVector *)&faceData->records[count];
		gradients = (FaceTextureGradients *)&faceNormals[count];
		points = (float *)gradients;
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
		float *vertices;

		count = resolvedNode->payloadCount;
		vertices = (float *)resolvedNode->payload;
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
		float *lodThresholds;

		count = resolvedNode->childCount;
		lodThresholds = (float *)resolvedNode->payload;
		if (count > 0) {
			do {
				*lodThresholds =
					(float)(*lodThresholds * scale);
				++lodThresholds;
				--count;
			} while (count != 0);
		}
		break;
	}
	default:
		break;
	}

	for (childIndex = 0; childIndex < resolvedNode->childCount;
	     ++childIndex) {
		ModelPreview_UnscaleOptNodeTree(
			resolvedNode->pChildren[childIndex], opt, scale);
	}
}

/* Runs ModelPreview_ScaleOptNodeTree on each root of opt. */
// FUNCTION: XVT 0x42AC50
void ModelPreview_ScaleOptRootNodes(OptimizedPolyObject *opt, double scale)
{
	int rootIndex;

	for (rootIndex = 0; rootIndex < opt->rootNodeCount; ++rootIndex) {
		ModelPreview_ScaleOptNodeTree(opt->rootNodes[rootIndex], opt,
					      scale);
	}
}

/* Runs ModelPreview_UnscaleOptNodeTree on each root of opt. Nothing calls
 * this. */
// FUNCTION: XVT 0x42AC90
void ModelPreview_UnscaleOptRootNodes(OptimizedPolyObject *opt, double scale)
{
	int rootIndex;

	for (rootIndex = 0; rootIndex < opt->rootNodeCount; ++rootIndex) {
		ModelPreview_UnscaleOptNodeTree(opt->rootNodes[rootIndex], opt,
						scale);
	}
}

/* Widens the preview bounds globals (g_modelPreviewBoundsMinX to
 * g_modelPreviewBoundsMaxZ) to hold every vertex of each OPT_MESHVERTS node at
 * or below node. Follows OPT_NODEREF links and stops at one that does not
 * resolve. */
// FUNCTION: XVT 0x42AD10
void ModelPreview_AccumulateOptNodeBounds(OptNode *node,
					  OptimizedPolyObject *object)
{
	OptNode *currentNode;
	int vertexCount;
	float *vertex;
	int childIndex;

	currentNode = node;
	if (currentNode != NULL) {
		while (currentNode->nodeType == OPT_NODEREF) {
			currentNode = OptModel_ResolveNodeRef(
				object, (const char *)currentNode->payload);
			if (currentNode == NULL) {
				return;
			}
		}

		if (currentNode->nodeType == OPT_MESHVERTS) {
			vertexCount = currentNode->payloadCount;
			vertex = currentNode->payload;
			if (vertexCount > 0) {
				do {
					if (vertex[0] >
					    g_modelPreviewBoundsMaxX) {
						g_modelPreviewBoundsMaxX =
							vertex[0];
					}
					if (vertex[0] <
					    g_modelPreviewBoundsMinX) {
						g_modelPreviewBoundsMinX =
							vertex[0];
					}
					if (vertex[1] >
					    g_modelPreviewBoundsMaxY) {
						g_modelPreviewBoundsMaxY =
							vertex[1];
					}
					if (vertex[1] <
					    g_modelPreviewBoundsMinY) {
						g_modelPreviewBoundsMinY =
							vertex[1];
					}
					if (vertex[2] >
					    g_modelPreviewBoundsMaxZ) {
						g_modelPreviewBoundsMaxZ =
							vertex[2];
					}
					if (vertex[2] <
					    g_modelPreviewBoundsMinZ) {
						g_modelPreviewBoundsMinZ =
							vertex[2];
					}
					vertex += 3;
					--vertexCount;
				} while (vertexCount != 0);
			}
		}

		childIndex = 0;
		while (currentNode->childCount > childIndex) {
			ModelPreview_AccumulateOptNodeBounds(
				currentNode->pChildren[childIndex], object);
			++childIndex;
		}
	}
}

/* Returns the extent (max - min) of the object's vertices and the origin on one axis: axis 1 is X,
 * 2 is Y and 3 is Z, while axis 0 returns the largest of the three extents. */
/* Writes the six preview bounds globals, starting each at 0. For axis 0, when
 * the x and y extents are equal and both larger than the z extent, it returns
 * the z extent. Any axis other than 0 to 3 returns an uninitialized value. */
// FUNCTION: XVT 0x42AE30
double ModelPreview_ComputeOptBoundsExtent(OptimizedPolyObject *object,
					   int axis)
{
	int rootNodeIndex;
	double result;

	g_modelPreviewBoundsMaxX = 0.0f;
	g_modelPreviewBoundsMinX = 0.0f;
	g_modelPreviewBoundsMaxY = 0.0f;
	g_modelPreviewBoundsMinY = 0.0f;
	g_modelPreviewBoundsMaxZ = 0.0f;
	g_modelPreviewBoundsMinZ = 0.0f;
	for (rootNodeIndex = 0; rootNodeIndex < object->rootNodeCount;
	     ++rootNodeIndex) {
		ModelPreview_AccumulateOptNodeBounds(
			object->rootNodes[rootNodeIndex], object);
	}

	/* From here the Max globals hold the extents (max - min), not the maxima. */
	g_modelPreviewBoundsMaxX -= g_modelPreviewBoundsMinX;
	g_modelPreviewBoundsMaxY -= g_modelPreviewBoundsMinY;
	g_modelPreviewBoundsMaxZ -= g_modelPreviewBoundsMinZ;
	if (axis == 0) {
		if (g_modelPreviewBoundsMaxY >= g_modelPreviewBoundsMaxX ||
		    g_modelPreviewBoundsMaxZ >= g_modelPreviewBoundsMaxX) {
			if (g_modelPreviewBoundsMaxY <=
				    g_modelPreviewBoundsMaxX ||
			    g_modelPreviewBoundsMaxZ >=
				    g_modelPreviewBoundsMaxY) {
				result = g_modelPreviewBoundsMaxZ;
			} else {
				result = g_modelPreviewBoundsMaxY;
			}
		} else {
			result = g_modelPreviewBoundsMaxX;
		}
	} else {
		if (axis == 1) {
			result = g_modelPreviewBoundsMaxX;
		}
		if (axis == 2) {
			result = g_modelPreviewBoundsMaxY;
		}
		if (axis == 3) {
			result = g_modelPreviewBoundsMaxZ;
		}
	}
	return result;
}

/* Points the local player's camera at the preview: position (0, -1280, 0),
 * pitch 0x4000, roll and yaw 0, g_projOffsetY 0. Sets g_lodDistanceScale and
 * g_mipLodScale to 1.0, g_textureResolutionLevel to 1, and turns on local
 * lights, specular, directional lighting and dithering. Returns 1. */
// FUNCTION: XVT 0x42AF90
int ModelPreview_ResetViewAndRenderState(void)
{
	PlayerData *player = &g_players[g_localPlayer];

	g_projOffsetY = 0;
	player->viewState.cameraWorldX = 0;
	player->viewState.cameraWorldY = -1280;
	player->viewState.cameraWorldZ = 0;
	player->viewState.viewRoll = 0;
	player->viewState.viewPitch = 0x4000;
	player->viewState.viewYaw = 0;
	g_lodDistanceScale = 1.0f;
	g_mipLodScale = 1.0f;
	g_localLightsEnabled = 1;
	g_specularEnabled = 1;
	g_textureResolutionLevel = 1;
	g_dirLightingEnabled = 1;
	g_ditheringEnabled = 1;
	return 1;
}

/* Sets g_worldLightDirectionX, Y and Z to the direction (x, -y, z) scaled to
 * length 32767, each cut to an int16_t. Does not check for a zero vector. */
// FUNCTION: XVT 0x42B010
void ModelPreview_SetLightDirection(int x, int y, int z)
{
	double lightX;
	double lightY;
	double lightZ;
	double invLength;

	y = -y;
	lightX = x;
	lightY = y;
	lightZ = z;
	invLength = g_modelPreviewInvLengthNumerator /
		    sqrt(lightX * lightX + lightY * lightY + lightZ * lightZ);
	lightX *= invLength;
	lightY *= invLength;
	lightZ *= invLength;
	g_worldLightDirectionX =
		(int16_t)(int)(lightX * g_modelPreviewLightDirectionQ15Scale);
	g_worldLightDirectionY =
		(int16_t)(int)(lightY * g_modelPreviewLightDirectionQ15Scale);
	g_worldLightDirectionZ =
		(int16_t)(int)(lightZ * g_modelPreviewLightDirectionQ15Scale);
}

/* Sets the preview object's pitch, yaw and roll from degrees, times 65,536 /
 * 360, each cut to an int16_t. */
// FUNCTION: XVT 0x42B090
void ModelPreview_SetObjectEulerDegrees(float pitchDeg, float yawDeg,
					float rollDeg)
{
	double angle;

	angle = pitchDeg;
	g_modelPreviewObject.pitch =
		(int16_t)(int)(angle * g_degreesToQ16AngleScale);
	angle = yawDeg;
	g_modelPreviewObject.yaw =
		(int16_t)(int)(angle * g_degreesToQ16AngleScale);
	angle = rollDeg;
	g_modelPreviewObject.roll =
		(int16_t)(int)(angle * g_degreesToQ16AngleScale);
}

/* Sets g_nodeSwitchIndex. */
// FUNCTION: XVT 0x42B0D0
void ModelPreview_SetNodeSwitchIndex(int nodeSwitchIndex)
{
	g_nodeSwitchIndex = nodeSwitchIndex;
}

/* Sets the preview object's world position. */
// FUNCTION: XVT 0x42B0E0
void ModelPreview_SetObjectWorldPosition(int x, int y, int z)
{
	g_modelPreviewObject.world_x = x;
	g_modelPreviewObject.world_y = y;
	g_modelPreviewObject.world_z = z;
}

/* Saves the preview's file name, position, angles, light direction,
 * g_nodeSwitchIndex and g_modelPreviewUpAxisAngle in the g_savedModelPreview
 * globals. */
// FUNCTION: XVT 0x42B100
void ModelPreview_SaveState(void)
{
	strcpy(g_savedModelPreviewModelFileName, g_modelPreviewOptFileName);
	g_savedModelPreviewWorldX = g_modelPreviewObject.world_x;
	g_savedModelPreviewWorldY = g_modelPreviewObject.world_y;
	g_savedModelPreviewWorldZ = g_modelPreviewObject.world_z;
	g_savedModelPreviewNodeSwitchIndex = g_nodeSwitchIndex;
	g_savedModelPreviewPitch = g_modelPreviewObject.pitch;
	g_savedModelPreviewYaw = g_modelPreviewObject.yaw;
	g_savedModelPreviewRoll = g_modelPreviewObject.roll;
	g_savedModelPreviewLightDirectionX = (int16_t)g_worldLightDirectionX;
	g_savedModelPreviewLightDirectionY = (int16_t)g_worldLightDirectionY;
	g_savedModelPreviewLightDirectionZ = (int16_t)g_worldLightDirectionZ;
	g_savedModelPreviewUpAxisAngle = g_modelPreviewUpAxisAngle;
}

/* Loads the saved file name again with ModelPreview_LoadModel, ignoring a
 * failure, then puts back what ModelPreview_SaveState saved. */
// FUNCTION: XVT 0x42B1B0
void ModelPreview_RestoreState(void)
{
	ModelPreview_LoadModel(g_savedModelPreviewModelFileName);
	g_modelPreviewObject.world_x = g_savedModelPreviewWorldX;
	g_modelPreviewObject.world_y = g_savedModelPreviewWorldY;
	g_modelPreviewObject.world_z = g_savedModelPreviewWorldZ;
	g_modelPreviewObject.pitch = g_savedModelPreviewPitch;
	g_modelPreviewObject.yaw = g_savedModelPreviewYaw;
	g_modelPreviewObject.roll = g_savedModelPreviewRoll;
	g_nodeSwitchIndex = g_savedModelPreviewNodeSwitchIndex;
	g_worldLightDirectionX = g_savedModelPreviewLightDirectionX;
	g_worldLightDirectionY = g_savedModelPreviewLightDirectionY;
	g_worldLightDirectionZ = g_savedModelPreviewLightDirectionZ;
	g_modelPreviewUpAxisAngle = g_savedModelPreviewUpAxisAngle;
}

/* Sets g_modelPreviewUpAxisAngle from degrees, times 65,536 / 360, cut to an
 * int16_t. */
// FUNCTION: XVT 0x42B250
void ModelPreview_SetObjectUpAxisAngleDegrees(float angleDeg)
{
	double angle = angleDeg;

	g_modelPreviewUpAxisAngle =
		(int16_t)(int)(angle * g_degreesToQ16AngleScale);
}

/* Returns g_modelPreviewBoundsExtent times 1600 times 1 / 65,536, cut to an
 * int. */
// FUNCTION: XVT 0x42B270
int ModelPreview_GetDisplayedSizeMeters(void)
{
	double displayedSize = g_modelPreviewBoundsExtent;

	displayedSize *= g_modelPreviewMetersScale;
	displayedSize *= g_modelPreviewQ16Scale;
	return (int)displayedSize;
}

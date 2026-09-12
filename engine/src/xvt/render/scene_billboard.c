#include "xvt/render/scene_billboard.h"

#include "xvt/assets/object_type.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/render_quad.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"

enum {
	COMPONENT_OBJECT_TYPE = 89,
	BILLBOARD_MODEL_FRAME_LIMIT = 0x8000,
	BILLBOARD_INVALID_FRAME_START = 0xFF00,
	BILLBOARD_SCREEN_COORD_HIGH_MASK = -65536,
	BILLBOARD_DEFAULT_SCREEN_SIZE = 256,
	BILLBOARD_LIGHT_SIZE_SHIFT = 6,
	BILLBOARD_ALIGNMENT_QUARTER_TURN = 0x4000,
};

// GLOBAL: XVT 0x9A1FE6
uint16_t g_billboardModelNodeSwitchIndex = 0;
// GLOBAL: XVT 0x9A20A6
uint16_t g_billboardTargetSelectionState = 0;
// GLOBAL: XVT 0x9A8062
int16_t g_sceneBillboardQueueCount = 0;
// GLOBAL: XVT 0x9ED664
uint16_t g_billboardObjectOrTypeIndex = 0;

// GLOBAL: XVT 0x9ECA30
static SceneBillboardQueueEntry g_sceneBillboardQueue[32] = { { 0 } };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x401000
void SceneBillboard_QueueObjectTextured(int objectIndex) {
	ObjectRecord* object;
	uint16_t sourceObjectType;
	uint16_t frame;
	int absR0Z;
	int absR1Z;
	int axisX;
	int axisY;
	uint16_t rotationAngle;
	int projectedX;
	int projectedXHigh;
	int projectedY;
	int projectedYHigh;
	int screenY;
	uint16_t screenSize;

	object = &g_objectTable[objectIndex];
	sourceObjectType = object->objectType;
	g_billboardObjectOrTypeIndex = objectIndex;
	g_billboardTextureFrameSequence = g_modelTypeTable[sourceObjectType].textureFrameSequence;
	if (sourceObjectType == COMPONENT_OBJECT_TYPE) {
		frame = object->typeSpecificByte[0] >> 1;
	} else {
		if (g_billboardTextureFrameSequence == NULL) {
			return;
		}
		g_billboardTextureSequenceIndex = object->typeSpecificByte[0];
		frame = g_billboardTextureFrameSequence[g_billboardTextureSequenceIndex];
	}

	if (frame >= BILLBOARD_INVALID_FRAME_START) {
		return;
	}
	if (frame < BILLBOARD_MODEL_FRAME_LIMIT) {
		if (sourceObjectType == COMPONENT_OBJECT_TYPE) {
			sourceObjectType = object->mobj->sourceObjectType;
		}
		g_billboardModelNodeSwitchIndex = frame;
		RenderScene_DrawNoAssetSourceModel(object, frame);
		if (sourceObjectType == COMPONENT_OBJECT_TYPE) {
			g_billboardTextureSequenceIndex = g_objectTable[objectIndex].typeSpecificByte[1];
			frame = g_modelType132TextureFrameSequence[g_billboardTextureSequenceIndex];
		}
	}

	if (frame >= BILLBOARD_INVALID_FRAME_START || frame < BILLBOARD_MODEL_FRAME_LIMIT || depthZ < 0) {
		return;
	}
	absR0Z = g_objViewMat_R0_Z;
	absR1Z = g_objViewMat_R1_Z;
	if (absR0Z < 0) {
		absR0Z = -absR0Z;
	}
	if (absR1Z < 0) {
		absR1Z = -absR1Z;
	}
	if (absR1Z > absR0Z) {
		axisX = g_objViewMat_R0_X;
		axisY = g_objViewMat_R0_Y;
	} else {
		axisX = g_objViewMat_R1_X;
		axisY = g_objViewMat_R1_Y;
	}
	if (axisX < 0) {
		rotationAngle = (uint16_t)trig2_arctan(axisY, -axisX);
	} else {
		rotationAngle = (uint16_t)-trig2_arctan(axisY, axisX);
	}

	projectedX = TRANSFM2_ProjectScreenX(viewX, depthZ);
	projectedXHigh = projectedX & BILLBOARD_SCREEN_COORD_HIGH_MASK;
	if (projectedXHigh > 0 || projectedXHigh < BILLBOARD_SCREEN_COORD_HIGH_MASK) {
		return;
	}
	projectedY = TRANSFM2_ProjectScreenY(viewY, depthZ);
	projectedYHigh = projectedY & BILLBOARD_SCREEN_COORD_HIGH_MASK;
	if (projectedYHigh > 0 || projectedYHigh < BILLBOARD_SCREEN_COORD_HIGH_MASK) {
		return;
	}

	screenY = g_flightVpHeight - projectedY;
	screenSize = g_objectTable[objectIndex].mobj->lightIntensityScale;
	if (screenSize != 0) {
		screenSize = (uint16_t)(screenSize << BILLBOARD_LIGHT_SIZE_SHIFT);
		if (screenSize >= BILLBOARD_DEFAULT_SCREEN_SIZE) {
			screenSize = (uint16_t)(screenSize + BILLBOARD_DEFAULT_SCREEN_SIZE);
		}
	} else {
		screenSize = BILLBOARD_DEFAULT_SCREEN_SIZE;
	}
	SceneBillboard_QueueProjectedTextured(g_billboardObjectOrTypeIndex, frame, screenSize,
										  (int16_t)projectedX, (int16_t)screenY, depthZ, rotationAngle);
}

// FUNCTION: XVT 0x401250
void SceneBillboard_QueueProjectedTextured(int objectOrTypeIndex, int frame, int screenSize, int screenX,
										   int screenY, int depthZ, int rotationAngle) {
	int16_t count;

	count = g_sceneBillboardQueueCount;
	if (count < 32) {
		g_sceneBillboardQueue[count].objectOrTypeIndex = objectOrTypeIndex;
		g_sceneBillboardQueue[count].frame = frame;
		g_sceneBillboardQueue[count].screenSize = screenSize;
		g_sceneBillboardQueue[count].screenX = screenX;
		g_sceneBillboardQueue[count].screenY = screenY;
		g_sceneBillboardQueue[count].depthZ = depthZ;
		g_sceneBillboardQueue[count].rotationAngle = rotationAngle;
		g_sceneBillboardQueueCount = (int16_t)(count + 1);
	}
}

// FUNCTION: XVT 0x4012C0
void SceneBillboard_RenderQueuedTextured(int16_t drawTargetMarkers) {
	enum { TARGET_BOX_COLOR = 59 };

	int16_t queuedCount;
	int16_t swapped;
	uint16_t queueIndex;
	uint16_t currentTargetObjectIdx;

	queuedCount = g_sceneBillboardQueueCount;
	--g_sceneBillboardQueueCount;
	swapped = 1;
	if (queuedCount != 0) {
		do {
			if (swapped != 0) {
				int count;

				swapped = 0;
				queueIndex = 0;
				if (g_sceneBillboardQueueCount > 0) {
					count = g_sceneBillboardQueueCount;
					do {
						if (g_sceneBillboardQueue[queueIndex + 1].depthZ <
							g_sceneBillboardQueue[queueIndex].depthZ) {
							SceneBillboardQueueEntry temporary;

							temporary = g_sceneBillboardQueue[queueIndex];
							g_sceneBillboardQueue[queueIndex] = g_sceneBillboardQueue[queueIndex + 1];
							g_sceneBillboardQueue[queueIndex + 1] = temporary;
							swapped = 1;
						}
						++queueIndex;
					} while (queueIndex < count);
				}
			}

			RenderQuad_DrawModelTexture(&g_sceneBillboardQueue[g_sceneBillboardQueueCount]);
			queuedCount = g_sceneBillboardQueueCount;
			--g_sceneBillboardQueueCount;
		} while (queuedCount != 0);
	}

	if (drawTargetMarkers == 0) {
		return;
	}
	currentTargetObjectIdx = (uint16_t)g_players[g_localPlayer].currentTargetObjectIdx;
	if (currentTargetObjectIdx == UINT16_MAX) {
		return;
	}
	if (g_objectTable[currentTargetObjectIdx].genusId == CRAFT_GENUS_STARSHIP ||
		g_objectTable[currentTargetObjectIdx].genusId == CRAFT_GENUS_PLATFORM) {
		Targeting_DrawObjectBox(currentTargetObjectIdx,
								(uint16_t)g_players[g_localPlayer].selectedTargetComponent, TARGET_BOX_COLOR);
	} else {
		Targeting_DrawObjectBox(currentTargetObjectIdx, UINT16_MAX, TARGET_BOX_COLOR);
	}
}

// FUNCTION: XVT 0x41FF70
void RenderBillboard_DrawRollAlignedObjectModel(uint16_t objectIndex) {
	ObjectRecord* object;
	int deltaX;
	int deltaY;
	int deltaZ;
	int sideProjection;
	int upProjection;
	int16_t savedRoll;

	g_billboardObjectOrTypeIndex = objectIndex;
	object = &g_objectTable[objectIndex];
	deltaX = g_players[g_localPlayer].viewState.savedTargetX - object->world_x;
	deltaY = g_players[g_localPlayer].viewState.savedTargetY - object->world_y;
	deltaZ = g_players[g_localPlayer].viewState.savedTargetZ - object->world_z;
	sideProjection = Math_Dot3Q15(object->mobj->cachedSideX, object->mobj->cachedSideY,
								  object->mobj->cachedSideZ, deltaX, deltaY, deltaZ);
	upProjection = Math_Dot3Q15(object->mobj->cachedUpX, object->mobj->cachedUpY, object->mobj->cachedUpZ,
								deltaX, deltaY, deltaZ);
	savedRoll = object->roll;
	object->roll = (int16_t)(savedRoll + trig2_arctan(upProjection, sideProjection));
	object->roll -= BILLBOARD_ALIGNMENT_QUARTER_TURN;
	object->mobj->orientMatrixDirty = 1;
	FVIEW_SetObjectTransform(object->roll, object->pitch, object->yaw, 0, object);
	RenderScene_DrawObjectModel(object);
	object->roll = savedRoll;
	object->mobj->orientMatrixDirty = 1;
}

// FUNCTION: XVT 0x4243D0
int SceneBillboard_ComputeProjectedSize(int depthZ, uint16_t modelMaxExtent, uint16_t baseScreenSize) {
#ifdef XVT_MODERN
	if (depthZ < 0 && depthZ != INT32_MIN)
#else
	if (depthZ < 0)
#endif
		depthZ = -depthZ;
	depthZ >>= 8;
	if (depthZ != 0)
		depthZ = modelMaxExtent / depthZ;
#ifdef XVT_MODERN
	{
		uint32_t product;

		product = (uint32_t)baseScreenSize * (uint32_t)depthZ;
		depthZ = (int)(product >> 8);
		if ((product & 0x80000000u) != 0)
			depthZ -= 0x1000000;
	}
#else
	depthZ *= baseScreenSize;
	depthZ >>= 8;
#endif
	if (depthZ > 1024)
		depthZ = 1024;
	return depthZ;
}

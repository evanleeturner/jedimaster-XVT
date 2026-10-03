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
	BILLBOARD_EFFECT_SIZE_SHIFT = 6,
	BILLBOARD_ALIGNMENT_QUARTER_TURN = 0x4000,
};

/* Model node last chosen for a billboard object: the frame
 * SceneBillboard_DrawOrQueueObject draws as a node, or the mesh
 * Damage_QueueCraftBillboardsForObjectType or ProvingGrounds_DrawCourseObject
 * is on. Only ProvingGrounds_DrawCourseObject reads it. */
// GLOBAL: XVT 0x9A1FE6
uint16_t g_billboardModelNodeSwitchIndex = 0;
/* Selection marking for the meshes Damage_QueueCraftBillboardsForObjectType
 * walks: 1 for the whole object while it is the local beam target, 2 on the
 * mesh matching g_renderTargetComponentIdx, 0 on the others;
 * ProvingGrounds_DrawCourseObject sets 1. Only
 * Damage_QueueCraftBillboardsForObjectType reads it. */
// GLOBAL: XVT 0x9A20A6
uint16_t g_billboardTargetSelectionState = 0;
/* Billboards waiting in g_sceneBillboardQueue, 0 to 32.
 * SceneBillboard_QueueProjectedTextured adds one;
 * SceneBillboard_RenderQueuedTextured counts it down and leaves -1, and its
 * callers and the frame and map setup set it back to 0. */
// GLOBAL: XVT 0x9A8062
int16_t g_sceneBillboardQueueCount = 0;
/* Object index (or, in a few callers, a type or marker value) of the object
 * being drawn, which the billboard and model drawing code reads; many functions
 * write it, chiefly SceneBillboard_DrawOrQueueObject,
 * RenderQuad_DrawModelTexture, Damage_QueueCraftBillboardsForObjectType and
 * RenderNonCraftSceneObject. */
// GLOBAL: XVT 0x9ED664
uint16_t g_billboardObjectOrTypeIndex = 0;

/* Textured billboards waiting to be drawn, 32 entries, filled by
 * SceneBillboard_QueueProjectedTextured and drawn by
 * SceneBillboard_RenderQueuedTextured. */
// GLOBAL: XVT 0x9ECA30
static SceneBillboardQueueEntry g_sceneBillboardQueue[32] = {{0}};

/* Draws an object through its type's frame sequence: model frames at once,
 * texture frames as queued billboards. Uses the object-to-view matrix and the
 * g_viewSpace position the caller has set up. Sets g_billboardObjectOrTypeIndex
 * and g_billboardTextureFrameSequence; the frame is typeSpecificByte[0] >> 1
 * for object type 89 (COMPONENT_OBJECT_TYPE), else the sequence entry at
 * typeSpecificByte[0], stored in g_billboardTextureSequenceIndex, and it
 * returns when the type has no sequence. Frames from 0xFF00 up draw nothing. A
 * frame under 0x8000 is a model node: it sets g_billboardModelNodeSwitchIndex
 * and draws it with RenderScene_DrawSelectedRootNode, and stops there, except
 * for a type 89 object whose mobj's sourceObjectType is also 89, which then
 * takes a texture frame from g_objectType132TextureFrameSequence at
 * typeSpecificByte[1]. A texture frame (0x8000 to 0xFEFF) with view depth not
 * negative is queued at the projected point, returning when either coordinate
 * falls outside -65536 to 65535, with Y measured up from the viewport's bottom,
 * a size of effectSize << 6 (plus 256 when that is 256 or more, or 256 for no
 * effectSize), and the object's on-screen roll from row 0 or 1 of the
 * object-to-view matrix, whichever has the smaller Z term in size (row 1 on a
 * tie). Does not check that the object has a mobj. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x401000
void SceneBillboard_DrawOrQueueObject(int objectIndex)
{
	ObjectRecord *object;
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
	g_billboardTextureFrameSequence =
		g_objectTypeTable[sourceObjectType].textureFrameSequence;
	if (sourceObjectType == COMPONENT_OBJECT_TYPE) {
		frame = object->typeSpecificByte[0] >> 1;
	} else {
		if (g_billboardTextureFrameSequence == NULL) {
			return;
		}
		g_billboardTextureSequenceIndex = object->typeSpecificByte[0];
		frame = g_billboardTextureFrameSequence
			[g_billboardTextureSequenceIndex];
	}

	if (frame >= BILLBOARD_INVALID_FRAME_START) {
		return;
	}
	if (frame < BILLBOARD_MODEL_FRAME_LIMIT) {
		if (sourceObjectType == COMPONENT_OBJECT_TYPE) {
			/* From here sourceObjectType holds mobj->sourceObjectType, not the object's own type. */
			sourceObjectType = object->mobj->sourceObjectType;
		}
		g_billboardModelNodeSwitchIndex = frame;
		RenderScene_DrawSelectedRootNode(object, frame);
		if (sourceObjectType == COMPONENT_OBJECT_TYPE) {
			g_billboardTextureSequenceIndex =
				g_objectTable[objectIndex].typeSpecificByte[1];
			frame = g_objectType132TextureFrameSequence
				[g_billboardTextureSequenceIndex];
		}
	}

	if (frame >= BILLBOARD_INVALID_FRAME_START ||
	    frame < BILLBOARD_MODEL_FRAME_LIMIT || g_viewSpaceDepth < 0) {
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

	projectedX = TRANSFM2_ProjectScreenX(g_viewSpaceX, g_viewSpaceDepth);
	projectedXHigh = projectedX & BILLBOARD_SCREEN_COORD_HIGH_MASK;
	if (projectedXHigh > 0 ||
	    projectedXHigh < BILLBOARD_SCREEN_COORD_HIGH_MASK) {
		return;
	}
	projectedY = TRANSFM2_ProjectScreenY(g_viewSpaceY, g_viewSpaceDepth);
	projectedYHigh = projectedY & BILLBOARD_SCREEN_COORD_HIGH_MASK;
	if (projectedYHigh > 0 ||
	    projectedYHigh < BILLBOARD_SCREEN_COORD_HIGH_MASK) {
		return;
	}

	screenY = g_flightVpHeight - projectedY;
	screenSize = g_objectTable[objectIndex].mobj->effectSize;
	if (screenSize != 0) {
		screenSize =
			(uint16_t)(screenSize << BILLBOARD_EFFECT_SIZE_SHIFT);
		if (screenSize >= BILLBOARD_DEFAULT_SCREEN_SIZE) {
			screenSize = (uint16_t)(screenSize +
						BILLBOARD_DEFAULT_SCREEN_SIZE);
		}
	} else {
		screenSize = BILLBOARD_DEFAULT_SCREEN_SIZE;
	}
	SceneBillboard_QueueProjectedTextured(
		g_billboardObjectOrTypeIndex, frame, screenSize,
		(int16_t)projectedX, (int16_t)screenY, g_viewSpaceDepth,
		rotationAngle);
}

/* Adds a billboard to g_sceneBillboardQueue and raises
 * g_sceneBillboardQueueCount; does nothing when 32 wait. */
// FUNCTION: XVT 0x401250
void SceneBillboard_QueueProjectedTextured(int objectOrTypeIndex, int frame,
					   int screenSize, int screenX,
					   int screenY, int depthZ,
					   int rotationAngle)
{
	int16_t count;

	count = g_sceneBillboardQueueCount;
	if (count < 32) {
		g_sceneBillboardQueue[count].objectOrTypeIndex =
			objectOrTypeIndex;
		g_sceneBillboardQueue[count].frame = frame;
		g_sceneBillboardQueue[count].screenSize = screenSize;
		g_sceneBillboardQueue[count].screenX = screenX;
		g_sceneBillboardQueue[count].screenY = screenY;
		g_sceneBillboardQueue[count].depthZ = depthZ;
		g_sceneBillboardQueue[count].rotationAngle = rotationAngle;
		g_sceneBillboardQueueCount = (int16_t)(count + 1);
	}
}

/* Draws the queued billboards with RenderQuad_DrawModelTexture, farthest
 * (largest depthZ) first, sorting by bubble passes as it goes, and leaves
 * g_sceneBillboardQueueCount at -1. With drawTargetMarkers nonzero and a local
 * target, it then boxes the target with Targeting_DrawObjectBox in color 59,
 * around the selected component for a starship or platform. */
// FUNCTION: XVT 0x4012C0
void SceneBillboard_RenderQueuedTextured(int16_t drawTargetMarkers)
{
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
						if (g_sceneBillboardQueue
							    [queueIndex + 1]
								    .depthZ <
						    g_sceneBillboardQueue
							    [queueIndex]
								    .depthZ) {
							SceneBillboardQueueEntry
								temporary;

							temporary = g_sceneBillboardQueue
								[queueIndex];
							g_sceneBillboardQueue
								[queueIndex] = g_sceneBillboardQueue
									[queueIndex +
									 1];
							g_sceneBillboardQueue
								[queueIndex +
								 1] = temporary;
							swapped = 1;
						}
						++queueIndex;
					} while (queueIndex < count);
				}
			}

			RenderQuad_DrawModelTexture(
				&g_sceneBillboardQueue
					[g_sceneBillboardQueueCount]);
			queuedCount = g_sceneBillboardQueueCount;
			--g_sceneBillboardQueueCount;
		} while (queuedCount != 0);
	}

	if (drawTargetMarkers == 0) {
		return;
	}
	currentTargetObjectIdx =
		(uint16_t)g_players[g_localPlayer].currentTargetObjectIdx;
	if (currentTargetObjectIdx == UINT16_MAX) {
		return;
	}
	if (g_objectTable[currentTargetObjectIdx].genusId ==
		    CRAFT_GENUS_STARSHIP ||
	    g_objectTable[currentTargetObjectIdx].genusId ==
		    CRAFT_GENUS_PLATFORM) {
		Targeting_DrawObjectBox(currentTargetObjectIdx,
					(uint16_t)g_players[g_localPlayer]
						.selectedTargetComponent,
					TARGET_BOX_COLOR);
	} else {
		Targeting_DrawObjectBox(currentTargetObjectIdx, UINT16_MAX,
					TARGET_BOX_COLOR);
	}
}

/* Draws an object's model rolled to face the local player's camera: adds to its
 * roll trig2_arctan(up, side) of the camera's offset taken along the object's
 * cached up and side axes, less a quarter turn (0x4000), draws it with
 * FVIEW_SetObjectTransform and RenderScene_DrawObjectModel, and puts the roll
 * back, marking the orientation dirty both times. Sets
 * g_billboardObjectOrTypeIndex. */
// FUNCTION: XVT 0x41FF70
void SceneBillboard_DrawRollAlignedObjectModel(uint16_t objectIndex)
{
	ObjectRecord *object;
	int deltaX;
	int deltaY;
	int deltaZ;
	int sideProjection;
	int upProjection;
	int16_t savedRoll;

	g_billboardObjectOrTypeIndex = objectIndex;
	object = &g_objectTable[objectIndex];
	deltaX = g_players[g_localPlayer].viewState.cameraWorldX -
		 object->world_x;
	deltaY = g_players[g_localPlayer].viewState.cameraWorldY -
		 object->world_y;
	deltaZ = g_players[g_localPlayer].viewState.cameraWorldZ -
		 object->world_z;
	sideProjection = Math_Dot3Q15(
		object->mobj->cachedSideX, object->mobj->cachedSideY,
		object->mobj->cachedSideZ, deltaX, deltaY, deltaZ);
	upProjection =
		Math_Dot3Q15(object->mobj->cachedUpX, object->mobj->cachedUpY,
			     object->mobj->cachedUpZ, deltaX, deltaY, deltaZ);
	savedRoll = object->roll;
	object->roll = (int16_t)(savedRoll +
				 trig2_arctan(upProjection, sideProjection));
	object->roll -= BILLBOARD_ALIGNMENT_QUARTER_TURN;
	object->mobj->orientMatrixDirty = 1;
	FVIEW_SetObjectTransform(object->roll, object->pitch, object->yaw, 0,
				 object);
	RenderScene_DrawObjectModel(object);
	object->roll = savedRoll;
	object->mobj->orientMatrixDirty = 1;
}

/* Screen size of a billboard at a depth: s = modelMaxExtent / (size of
 * depthZ >> 8), or 0 when that is 0, then s * baseScreenSize >> 8, capped at
 * 1024. The modern build works the product without signed overflow and leaves
 * INT32_MIN as it is. Only RenderQuad_DrawModelTexture calls it. */
// FUNCTION: XVT 0x4243D0
int SceneBillboard_ComputeProjectedSize(int depthZ, uint16_t modelMaxExtent,
					uint16_t baseScreenSize)
{
#ifdef XVT_MODERN
	if (depthZ < 0 && depthZ != INT32_MIN)
#else
	if (depthZ < 0)
#endif
		depthZ = -depthZ;
	depthZ >>= 8;
	/* From here depthZ holds the model's extent over that depth, a scale, and then that scale times
	 * baseScreenSize over 256: the projected size returned. */
	if (depthZ != 0) {
		depthZ = modelMaxExtent / depthZ;
	}
#ifdef XVT_MODERN
	{
		uint32_t product;

		product = (uint32_t)baseScreenSize * (uint32_t)depthZ;
		depthZ = (int)(product >> 8);
		if ((product & 0x80000000u) != 0) {
			depthZ -= 0x1000000;
		}
	}
#else
	depthZ *= baseScreenSize;
	depthZ >>= 8;
#endif
	if (depthZ > 1024) {
		depthZ = 1024;
	}
	return depthZ;
}

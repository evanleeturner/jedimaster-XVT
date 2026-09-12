#include "xvt/flight/hud/flight_map.h"

#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_list.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/sw3d.h"
#include <string.h>

// GLOBAL: XVT 0x521240
const uint8_t g_flightIcons640FrameByObjectType[106] = {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x09, 0x0A, 0x0B, 0x0C,
	0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x00,
	0x1C, 0x1D, 0x1E, 0x1F, 0x41, 0x20, 0x21, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
	0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x40, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x35, 0x35,
	0x35, 0x35, 0x3F, 0x3D, 0x3E, 0x36, 0x37, 0x37, 0x37, 0x37, 0x37, 0x38, 0x39, 0x3A, 0x42, 0x3C,
	0x3B, 0x3B, 0x3B, 0x3C, 0x3C, 0x3A, 0x3D, 0x3D, 0x00, 0x00, 0x43, 0x44, 0x45, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D,
};
// GLOBAL: XVT 0x5212B0
const uint8_t g_flightIcons640WidthByFrame[72] = {
	8,  8, 7, 11, 8,  8,  11, 7, 10, 11, 7, 7, 8, 11, 11, 11, 7, 7, 6, 8,  8,  4, 5, 6,
	9,  4, 9, 6,  10, 9,  9,  9, 10, 9,  7, 7, 5, 9,  7,  6,  7, 7, 8, 9,  7,  7, 9, 11,
	10, 6, 9, 7,  6,  14, 17, 9, 9,  9,  6, 6, 7, 7,  7,  14, 9, 5, 4, 15, 14, 9, 0, 0,
};
// GLOBAL: XVT 0x5212F8
const uint8_t g_flightIcons640HeightByFrame[72] = {
	12, 13, 10, 10, 9,  9,  10, 11, 10, 10, 13, 11, 10, 9,  9,  11, 14, 13, 11, 13, 13, 6,  8,  11,
	9,  9,  9,  8,  15, 12, 13, 16, 11, 12, 15, 15, 17, 17, 18, 16, 17, 17, 17, 20, 17, 15, 17, 21,
	7,  7,  6,  9,  9,  14, 15, 6,  8,  8,  3,  6,  10, 7,  7,  17, 21, 14, 9,  14, 14, 15, 0,  0,
};
// GLOBAL: XVT 0x521340
const uint8_t g_flightMapIcons480x360FrameByObjectType[106] = {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x32, 0x33, 0x09, 0x34,
	0x0A, 0x0B, 0x0C, 0x0D, 0x35, 0x0E, 0x0F, 0x36, 0x10, 0x37, 0x11, 0x12, 0x13, 0x14, 0x15, 0x00,
	0x16, 0x17, 0x18, 0x19, 0x43, 0x38, 0x1A, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x39, 0x3A, 0x3B, 0x1F,
	0x3C, 0x20, 0x21, 0x22, 0x23, 0x24, 0x42, 0x25, 0x3D, 0x3E, 0x3F, 0x40, 0x26, 0x26, 0x26, 0x26,
	0x26, 0x26, 0x26, 0x26, 0x26, 0x41, 0x27, 0x28, 0x28, 0x28, 0x28, 0x29, 0x2A, 0x2B, 0x44, 0x2B,
	0x2C, 0x2D, 0x2E, 0x2E, 0x2E, 0x2F, 0x30, 0x31, 0x00, 0x00, 0x45, 0x46, 0x47, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
};
// GLOBAL: XVT 0x5213B0
const uint8_t g_flightMapIcons480x360WidthByFrame[72] = {
	5, 5, 5,  5, 5, 5, 7, 5, 5, 5, 9, 5, 5, 5, 3, 5, 5, 4, 6, 3, 5, 4, 4, 5,
	5, 5, 7,  3, 3, 3, 7, 5, 5, 5, 5, 5, 5, 6, 9, 5, 5, 7, 7, 4, 3, 4, 5, 3,
	4, 4, 12, 9, 8, 5, 8, 7, 8, 7, 3, 3, 5, 8, 8, 7, 7, 5, 5, 3, 2, 7, 7, 5,
};
// GLOBAL: XVT 0x5213F8
const uint8_t g_flightMapIcons480x360HeightByFrame[72] = {
	5, 6, 4, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5,  6, 6, 6, 5, 6, 4,  6, 5, 6, 7, 7,
	7, 6, 6, 6, 6, 7, 7, 7, 8, 9, 5, 7, 7,  4, 6, 5, 5, 7, 7,  8, 7, 6, 7, 4,
	4, 4, 4, 5, 8, 6, 4, 5, 6, 5, 8, 8, 10, 4, 4, 4, 6, 7, 10, 7, 4, 7, 7, 8,
};
// GLOBAL: XVT 0x521440
const uint8_t g_flightMapIcons320x240FrameByObjectType[106] = {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x32, 0x33, 0x09, 0x34,
	0x0A, 0x0B, 0x0C, 0x0D, 0x35, 0x0E, 0x0F, 0x36, 0x10, 0x37, 0x11, 0x12, 0x13, 0x14, 0x15, 0x00,
	0x16, 0x17, 0x18, 0x19, 0x43, 0x38, 0x1A, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x39, 0x3A, 0x3B, 0x1F,
	0x3C, 0x20, 0x21, 0x22, 0x23, 0x24, 0x42, 0x25, 0x3D, 0x3E, 0x3F, 0x40, 0x26, 0x26, 0x26, 0x26,
	0x26, 0x26, 0x26, 0x26, 0x26, 0x41, 0x27, 0x28, 0x28, 0x28, 0x28, 0x29, 0x2A, 0x2B, 0x44, 0x2B,
	0x2C, 0x2D, 0x2E, 0x2E, 0x2E, 0x2F, 0x30, 0x31, 0x00, 0x00, 0x45, 0x46, 0x47, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
};
// GLOBAL: XVT 0x5214B0
const uint8_t g_flightMapIcons320x240WidthByFrame[72] = {
	5, 5, 5,  5, 5, 5, 7, 5, 5, 5, 9, 5, 5, 5, 3, 5, 5, 4, 6, 3, 5, 4, 4, 5,
	5, 5, 7,  3, 3, 3, 7, 5, 5, 5, 5, 5, 5, 6, 9, 5, 5, 7, 7, 4, 3, 4, 5, 3,
	4, 4, 12, 9, 8, 5, 8, 7, 8, 7, 3, 3, 5, 8, 8, 7, 7, 5, 5, 3, 2, 7, 7, 5,
};
// GLOBAL: XVT 0x5214F8
const uint8_t g_flightMapIcons320x240HeightByFrame[72] = {
	5, 6, 4, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5,  6, 6, 6, 5, 6, 4,  6, 5, 6, 7, 7,
	7, 6, 6, 6, 6, 7, 7, 7, 8, 9, 5, 7, 7,  4, 6, 5, 5, 7, 7,  8, 7, 6, 7, 4,
	4, 4, 4, 5, 8, 6, 4, 5, 6, 5, 8, 8, 10, 4, 4, 4, 6, 7, 10, 7, 4, 7, 7, 8,
};
// GLOBAL: XVT 0x523588
const char g_flightMapIcons320x240ResourcePath[22] = "RESOURCE\\mapicons.ico";
// GLOBAL: XVT 0x5235A0
const char g_flightMapIcons480x360ResourcePath[22] = "RESOURCE\\mapicons.ico";
// GLOBAL: XVT 0x5235B8
const char g_flightIcons640x480ResourcePath[22] = "RESOURCE\\icons640.ico";
// GLOBAL: XVT 0x9A8E38
int g_flightIconFrameCount = 0;
// GLOBAL: XVT 0x9EC478
const char* g_flightIconResourcePath = NULL;
// GLOBAL: XVT 0x9ECC4C
uint8_t** g_flightIconFrames = NULL;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x435BC0
void FlightMap_RenderView(void) {
	enum { MAP_GRID_PLANE_Z = -65536 };

	FlightText_SetClipRect((int16_t)g_flightVpX, (int16_t)g_flightVpY,
						   (int16_t)(g_flightVpX + g_flightVpWidth),
						   (int16_t)(g_flightVpY + g_flightVpHeight));
	FlightMap_BuildRenderList();
	RenderList_SortDepthDescending();
	g_renderSceneResetPending = 1;
	if (g_players[g_localPlayer].viewState.savedTargetZ < MAP_GRID_PLANE_Z) {
		FlightMap_DrawObjectPass(1);
		FlightMap_DrawGrid();
		FlightMap_DrawObjectPass(0);
	} else {
		FlightMap_DrawObjectPass(0);
		FlightMap_DrawGrid();
		FlightMap_DrawObjectPass(1);
	}
	nullsub_14();
}

// FUNCTION: XVT 0x435C60
void FlightMap_UpdateCamera(int playerIdx) {
	if (g_players[playerIdx].mapCameraState > 1) {
		if (g_players[playerIdx].viewState.cameraFocusObjIdx != UINT16_MAX) {
			g_players[playerIdx].viewState.viewRoll =
				g_objectTable[g_players[playerIdx].viewState.cameraFocusObjIdx].roll;
			g_players[playerIdx].viewState.viewPitch =
				g_objectTable[g_players[playerIdx].viewState.cameraFocusObjIdx].pitch;
			g_players[playerIdx].viewState.viewYaw =
				g_objectTable[g_players[playerIdx].viewState.cameraFocusObjIdx].yaw;
			Mission_ResolveObjectOrMissionPointWorldLoc(g_players[playerIdx].viewState.cameraFocusObjIdx, 0);
			g_players[playerIdx].viewState.savedTargetX = worldlocx;
			g_players[playerIdx].viewState.savedTargetY = worldlocy;
			g_players[playerIdx].viewState.savedTargetZ = worldlocz;
			if (g_flightSimSideEffectsSuppressed == 0) {
				g_players[playerIdx].viewState.hudAimY +=
					FlightMap_ComputeAimStep(-g_players[playerIdx].viewState.hudAimY);
				if ((g_players[playerIdx].mapCameraState & 0x80) != 0) {
					g_players[playerIdx].viewState.viewRoll = 0;
					g_players[playerIdx].viewState.viewPitch = 0x4000;
					g_players[playerIdx].viewState.viewYaw = 0;
					g_players[playerIdx].viewState.hudAimX +=
						FlightMap_ComputeAimStep(-16384 - g_players[playerIdx].viewState.hudAimX);
				} else {
					g_players[playerIdx].viewState.hudAimX +=
						FlightMap_ComputeAimStep(-g_players[playerIdx].viewState.hudAimX);
				}
			}
		} else {
			g_players[playerIdx].viewState.viewRoll = 0;
			g_players[playerIdx].viewState.viewPitch = 0x4000;
			g_players[playerIdx].viewState.viewYaw = 0;
			g_players[playerIdx].viewState.hudAimY = 0;
			g_players[playerIdx].viewState.hudAimX =
				(int16_t)(((g_players[playerIdx].mapCameraState & 0x7F) << 14) / -127);
		}
		FVIEW_BuildCameraOrient(
			g_players[playerIdx].viewState.viewRoll, g_players[playerIdx].viewState.viewPitch,
			g_players[playerIdx].viewState.viewYaw, 0, g_players[playerIdx].viewState.hudAimX,
			g_players[playerIdx].viewState.hudAimY, NULL);
		if (g_players[playerIdx].viewState.cameraFocusObjIdx != UINT16_MAX) {
			g_players[playerIdx].viewState.savedTargetX -=
				Math_MulQ15(g_players[playerIdx].viewState.cameraDistance, g_camMatR2_X);
			g_players[playerIdx].viewState.savedTargetY -=
				Math_MulQ15(g_players[playerIdx].viewState.cameraDistance, g_camMatR2_Y);
			g_players[playerIdx].viewState.savedTargetZ -=
				Math_MulQ15(g_players[playerIdx].viewState.cameraDistance, g_camMatR2_Z);
		}
		return;
	}

	if (g_players[playerIdx].viewState.cameraFocusObjIdx != UINT16_MAX) {
		g_players[playerIdx].viewState.viewRoll =
			g_objectTable[g_players[playerIdx].viewState.cameraFocusObjIdx].roll;
		g_players[playerIdx].viewState.viewPitch =
			g_objectTable[g_players[playerIdx].viewState.cameraFocusObjIdx].pitch;
		g_players[playerIdx].viewState.viewYaw =
			g_objectTable[g_players[playerIdx].viewState.cameraFocusObjIdx].yaw;
		Mission_ResolveObjectOrMissionPointWorldLoc(g_players[playerIdx].viewState.cameraFocusObjIdx, 0);
		g_players[playerIdx].viewState.savedTargetX = worldlocx;
		g_players[playerIdx].viewState.savedTargetY = worldlocy;
		g_players[playerIdx].viewState.savedTargetZ = worldlocz;
		FVIEW_BuildCameraOrient(
			g_players[playerIdx].viewState.viewRoll, g_players[playerIdx].viewState.viewPitch,
			g_players[playerIdx].viewState.viewYaw, 0, g_players[playerIdx].viewState.hudAimX,
			g_players[playerIdx].viewState.hudAimY, NULL);
	} else {
		if (g_players[playerIdx].viewState.hudAimY != 0 || g_players[playerIdx].viewState.hudAimX != 0) {
			FVIEW_BuildCameraOrient(
				g_players[playerIdx].viewState.viewRoll, g_players[playerIdx].viewState.viewPitch,
				g_players[playerIdx].viewState.viewYaw, 0, g_players[playerIdx].viewState.hudAimX,
				g_players[playerIdx].viewState.hudAimY, NULL);
			trig2_ctop(g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z);
			g_players[playerIdx].viewState.hudAimY = 0;
			g_players[playerIdx].viewState.hudAimX = 0;
			g_players[playerIdx].viewState.viewPitch = pitchQ16;
			g_players[playerIdx].viewState.viewYaw = trig2_xyangle;
		}
		FVIEW_BuildCameraOrient(
			g_players[playerIdx].viewState.viewRoll, g_players[playerIdx].viewState.viewPitch,
			g_players[playerIdx].viewState.viewYaw, 0, g_players[playerIdx].viewState.hudAimX,
			g_players[playerIdx].viewState.hudAimY, NULL);
	}
	if (g_players[playerIdx].viewState.cameraFocusObjIdx != UINT16_MAX) {
		g_players[playerIdx].viewState.savedTargetX -=
			Math_MulQ15(g_players[playerIdx].viewState.cameraDistance, g_camMatR2_X);
		g_players[playerIdx].viewState.savedTargetY -=
			Math_MulQ15(g_players[playerIdx].viewState.cameraDistance, g_camMatR2_Y);
		g_players[playerIdx].viewState.savedTargetZ -=
			Math_MulQ15(g_players[playerIdx].viewState.cameraDistance, g_camMatR2_Z);
	}
	if (g_players[playerIdx].viewState.aimTargetIdx != UINT16_MAX) {
		trig2_ctop(g_objectTable[g_players[playerIdx].viewState.aimTargetIdx].world_x -
					   g_players[playerIdx].viewState.savedTargetX,
				   g_objectTable[g_players[playerIdx].viewState.aimTargetIdx].world_y -
					   g_players[playerIdx].viewState.savedTargetY,
				   g_objectTable[g_players[playerIdx].viewState.aimTargetIdx].world_z -
					   g_players[playerIdx].viewState.savedTargetZ);
		g_players[playerIdx].viewState.viewRoll = 0;
		g_players[playerIdx].viewState.viewPitch = pitchQ16;
		g_players[playerIdx].viewState.viewYaw = trig2_xyangle;
		FVIEW_BuildCameraOrient(g_players[playerIdx].viewState.viewRoll,
								g_players[playerIdx].viewState.viewPitch,
								g_players[playerIdx].viewState.viewYaw, 0, 0, 0, NULL);
	}
}

// FUNCTION: XVT 0x436320
void FlightMap_BuildRenderList(void) {
	unsigned int objectIdx;
	unsigned int objectType;
	unsigned int genusId;

	objectIdx = 0;
	RenderList_Reset();
	if (g_explosionObjectSlotEnd != 0) {
		do {
			objectType = g_objectTable[objectIdx].objectType;
			if (objectType != 0) {
				switch (g_objectTable[objectIdx].genusId) {
					case CRAFT_GENUS_STARFIGHTER:
					case CRAFT_GENUS_TRANSPORT:
					case CRAFT_GENUS_UTILITY_VEHICLE:
					case CRAFT_GENUS_FREIGHTER:
					case CRAFT_GENUS_STARSHIP:
					case CRAFT_GENUS_PLATFORM:
						if (RenderList_ProjectObjectBoundsForCulling(
								objectIdx, g_modelTypeTable[objectType].maxBoundsExtent, g_localPlayer)) {
							RenderList_QueueObject(objectIdx, depthZ);
						}
						break;
					case CRAFT_GENUS_PLAYER_PROJECTILE:
					case CRAFT_GENUS_OTHER_PROJECTILE:
					case CRAFT_GENUS_SMALL_DEBRIS:
					case CRAFT_GENUS_EXPLOSION:
						if (FlightView_IsObjectSphereVisible(objectIdx,
															 g_modelTypeTable[objectType].maxBoundsExtent)) {
							RenderList_QueueObject(objectIdx, depthZ);
						}
						break;
				}
			}
			++objectIdx;
		} while (objectIdx < g_explosionObjectSlotEnd);
	}

	objectIdx = g_regionMainObjectSlotEnd;
	if ((unsigned int)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount) > objectIdx) {
		do {
			objectType = g_objectTable[objectIdx].objectType;
			if (objectType != 0) {
				genusId = g_objectTable[objectIdx].genusId;
				if (genusId >= CRAFT_GENUS_MINE && genusId <= CRAFT_GENUS_SATELLITE &&
					RenderList_ProjectObjectBoundsForCulling(
						objectIdx, g_modelTypeTable[objectType].maxBoundsExtent, g_localPlayer)) {
					RenderList_QueueObject(objectIdx, depthZ);
				}
			}
			++objectIdx;
		} while (objectIdx < (unsigned int)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount));
	}
}

// FUNCTION: XVT 0x436780
void FlightMap_DrawObjectPass(int pass) {
	enum {
		MAP_GRID_PLANE_Z = -65536,
		MAP_MODEL_ICON_DISTANCE_SHIFT = 4,
		MAP_FOCUS_BOX_COLOR = 47,
		MAP_TARGET_BOX_COLOR = 59,
	};

	RenderObjectListEntry* savedRenderListHead;

	g_sceneBillboardQueueCount = 0;
	savedRenderListHead = g_renderListHead;
	while (g_renderListHead != NULL) {
		int objectIdx;
		int drawObject;

		objectIdx = g_renderListHead->objectIdx;
		drawObject = 0;
		if (pass != 0) {
			if (g_objectTable[objectIdx].world_z >= MAP_GRID_PLANE_Z) {
				drawObject = 1;
			}
		} else {
			if (g_objectTable[objectIdx].world_z < MAP_GRID_PLANE_Z) {
				drawObject = 1;
			}
		}

		if (drawObject != 0) {
			int genusId;
			int savedTargetX;
			int savedTargetY;

			RenderScene_Initialize(g_renderSceneResetPending);
			g_renderSceneResetPending = 0;
			savedTargetX = g_players[g_localPlayer].viewState.savedTargetX;
			savedTargetY = g_players[g_localPlayer].viewState.savedTargetY;
			g_camRelWorldX = g_objectTable[objectIdx].world_x - savedTargetX;
			g_camRelWorldY = g_objectTable[objectIdx].world_y - savedTargetY;
			g_camRelWorldZ =
				g_objectTable[objectIdx].world_z - g_players[g_localPlayer].viewState.savedTargetZ;
			depthZ = TRANSFM2_CamMatDotRow2(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
			viewX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
			viewY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
			genusId = g_objectTable[objectIdx].genusId;

			switch (genusId) {
				case CRAFT_GENUS_STARFIGHTER:
				case CRAFT_GENUS_TRANSPORT:
				case CRAFT_GENUS_UTILITY_VEHICLE:
				case CRAFT_GENUS_FREIGHTER:
				case CRAFT_GENUS_STARSHIP:
				case CRAFT_GENUS_PLATFORM:
				case CRAFT_GENUS_OBSTACLE:
					if (g_modelTypeTable[g_objectTable[objectIdx].objectType].maxBoundsExtent <
						g_renderListHead->sortDepth >> MAP_MODEL_ICON_DISTANCE_SHIFT) {
						FlightMap_DrawObjectIconAtViewPos(objectIdx, viewX, viewY, depthZ);
					} else {
						g_curCraft = g_objectTable[objectIdx].mobj->pCraft;
						if (genusId == CRAFT_GENUS_OBSTACLE) {
							g_transformLightDirectionToObjectSpace = 0;
						}
						FVIEW_SetObjectTransform(g_objectTable[objectIdx].roll,
												 g_objectTable[objectIdx].pitch, g_objectTable[objectIdx].yaw,
												 0, &g_objectTable[objectIdx]);
						if (genusId == CRAFT_GENUS_OBSTACLE) {
							ProvingGrounds_DrawCourseObject(objectIdx);
						} else {
							FlightLight_SetupObjectLighting(&g_objectTable[objectIdx]);
							Damage_QueueCraftBillboards(objectIdx);
							RenderScene_DrawObjectModel(&g_objectTable[objectIdx]);
							g_objectPointLightCount = 0;
						}
						g_transformLightDirectionToObjectSpace = 1;
						sw3d_DrawVisibleFacesToSurface();
					}
					if (g_players[g_localPlayer].viewState.cameraFocusObjIdx != objectIdx &&
						g_objectTable[objectIdx].playerOwnerIdx != -1) {
						FlightMap_DrawOtherPlayerObjectBox(objectIdx);
					}
					break;

				case CRAFT_GENUS_PLAYER_PROJECTILE:
				case CRAFT_GENUS_OTHER_PROJECTILE:
					FVIEW_SetObjectTransform(g_objectTable[objectIdx].roll, g_objectTable[objectIdx].pitch,
											 g_objectTable[objectIdx].yaw, 0, &g_objectTable[objectIdx]);
					RenderBillboard_DrawRollAlignedObjectModel(objectIdx);
					sw3d_DrawVisibleFacesToSurface();
					break;

				case CRAFT_GENUS_MINE:
				case CRAFT_GENUS_SATELLITE:
					if (g_modelTypeTable[g_objectTable[objectIdx].objectType].maxBoundsExtent <
						g_renderListHead->sortDepth >> MAP_MODEL_ICON_DISTANCE_SHIFT) {
						FlightMap_DrawObjectIconAtViewPos(objectIdx, viewX, viewY, depthZ);
						break;
					}
					g_sceneBillboardQueueCount = 0;
					if (g_regionMainObjectSlotEnd > objectIdx) {
						FVIEW_SetObjectTransform(g_objectTable[objectIdx].roll,
												 g_objectTable[objectIdx].pitch, g_objectTable[objectIdx].yaw,
												 0, &g_objectTable[objectIdx]);
						SceneBillboard_QueueObjectTextured(objectIdx);
					} else {
						FVIEW_SetObjectTransform(g_objectTable[objectIdx].roll,
												 g_objectTable[objectIdx].pitch, g_objectTable[objectIdx].yaw,
												 0, NULL);
						RenderNonCraftSceneObject(objectIdx);
					}
					sw3d_DrawVisibleFacesToSurface();
					SceneBillboard_RenderQueuedTextured(0);
					g_sceneBillboardQueueCount = 0;
					break;

				case CRAFT_GENUS_NORMAL_DEBRIS:
				case CRAFT_GENUS_SMALL_DEBRIS:
				case CRAFT_GENUS_EXPLOSION:
					g_sceneBillboardQueueCount = 0;
					if (g_regionMainObjectSlotEnd > objectIdx) {
						FVIEW_SetObjectTransform(g_objectTable[objectIdx].roll,
												 g_objectTable[objectIdx].pitch, g_objectTable[objectIdx].yaw,
												 0, &g_objectTable[objectIdx]);
						SceneBillboard_QueueObjectTextured(objectIdx);
					} else {
						FVIEW_SetObjectTransform(g_objectTable[objectIdx].roll,
												 g_objectTable[objectIdx].pitch, g_objectTable[objectIdx].yaw,
												 0, NULL);
						RenderNonCraftSceneObject(objectIdx);
					}
					sw3d_DrawVisibleFacesToSurface();
					SceneBillboard_RenderQueuedTextured(0);
					g_sceneBillboardQueueCount = 0;
					break;

				default:
					break;
			}

			if (g_players[g_localPlayer].viewState.cameraFocusObjIdx == objectIdx) {
				Targeting_DrawObjectBox(objectIdx, UINT16_MAX, MAP_FOCUS_BOX_COLOR);
			} else if ((uint16_t)g_players[g_localPlayer].currentTargetObjectIdx == objectIdx) {
				Targeting_DrawObjectBox(objectIdx, UINT16_MAX, MAP_TARGET_BOX_COLOR);
			}
			FlightSurface_Lock();
			FlightMap_DrawObjectOverlay(objectIdx);
			FlightSurface_Unlock();
			RenderScene_UnlockBuffers();
		}
		g_renderListHead = g_renderListHead->next;
	}
	g_renderListHead = savedRenderListHead;
}

// FUNCTION: XVT 0x436BC0
void FlightMap_DrawOtherPlayerObjectBox(int objectIdx) {
	enum {
		IFF_REBEL = 0,
		IFF_IMPERIAL = 1,
		IFF_BLUE = 2,
		IFF_IMPERIAL_2 = 4,
		COLOR_REBEL = 63,
		COLOR_IMPERIAL = 55,
		COLOR_BLUE = 51,
		COLOR_DEFAULT = 59,
	};

	MobileObject* mobileObject;
	CraftData* craft;
	uint8_t colorIndex;
	int playerIff;
	int team;
	int isHostile;

	mobileObject = g_objectTable[objectIdx].mobj;
	switch (mobileObject->iff) {
		case IFF_REBEL:
			colorIndex = COLOR_REBEL;
			break;
		case IFF_IMPERIAL:
		case IFF_IMPERIAL_2:
			colorIndex = COLOR_IMPERIAL;
			break;
		case IFF_BLUE:
			colorIndex = COLOR_BLUE;
			break;
		default:
			colorIndex = COLOR_DEFAULT;
			break;
	}

	craft = mobileObject->pCraft;
	if (g_flightMissionState.locatePlayersEnabled == 0) {
		playerIff = (uint16_t)g_players[g_localPlayer].playerIff;
		if (craft->iffVisibility[playerIff] == 0) {
			team = g_missionFlightGroups[g_objectTable[(uint16_t)objectIdx].flightGroupIdx].fg.team;
			if (team == playerIff) {
				isHostile = 0;
			} else {
				isHostile = g_missionTeams[playerIff].allies[team] == 0;
			}
			if (isHostile) {
				return;
			}
		}
	}

	if (Object_HasActiveDecoyBeam((uint16_t)objectIdx) != 0 ||
		(uint16_t)g_players[g_localPlayer].currentTargetObjectIdx == objectIdx) {
		return;
	}
	Targeting_DrawObjectBox(objectIdx, UINT16_MAX, colorIndex);
}

// FUNCTION: XVT 0x436D00
void FlightMap_DrawObjectOverlay(int objectIdx) {
	int savedViewPosition[3];
	int screenX;
	int screenY;
	unsigned int targetRef;
	int iff;
	int color;
	int drawOverlay;
	unsigned int boxExtent;
	int displayExtent;
	int textX;
	int textY;
	int velocityViewPosition[3];
	int lineScreenX;
	int lineScreenY;
	int distance;
	int genusId;
	uint16_t packedTargetRef;

	genusId = g_objectTable[objectIdx].genusId;
	if (genusId == 13 || genusId == 11) {
		return;
	}

	savedViewPosition[0] = viewX;
	savedViewPosition[1] = viewY;
	savedViewPosition[2] = depthZ;
	if (savedViewPosition[2] <= 0) {
		return;
	}
	screenX = TRANSFM2_ProjectScreenX(savedViewPosition[0], savedViewPosition[2]);
	screenY = TRANSFM2_ProjectScreenY(savedViewPosition[1], savedViewPosition[2]);
	screenX += g_flightClipLeft;
	screenY += g_flightClipTop;

	if ((uint16_t)g_players[g_localPlayer].currentTargetObjectIdx == objectIdx) {
		targetRef = UINT16_MAX;
		if (objectIdx < g_craftDataPoolCapacity) {
			targetRef = g_objectTable[objectIdx].mobj->pCraft->aiController.targetObjIdx;
		} else {
			MobileObject* mobileObject = g_objectTable[objectIdx].mobj;
			if (mobileObject != NULL) {
				CraftData* craft = mobileObject->pCraft;
				if (craft != NULL) {
					memcpy(&packedTargetRef, &craft->modelIndex, sizeof(packedTargetRef));
					targetRef = packedTargetRef;
				}
			}
		}
		if (targetRef != UINT16_MAX) {
			Mission_ResolveObjectOrMissionPointWorldLoc(targetRef, g_objectTable[objectIdx].flightGroupIdx);
			worldlocx -= g_players[g_localPlayer].viewState.savedTargetX;
			worldlocy -= g_players[g_localPlayer].viewState.savedTargetY;
			worldlocz -= g_players[g_localPlayer].viewState.savedTargetZ;
			viewX = TRANSFM2_CamMatDotRow0(worldlocx, worldlocy, worldlocz);
			viewY = TRANSFM2_CamMatDotRow1(worldlocx, worldlocy, worldlocz);
			depthZ = TRANSFM2_CamMatDotRow2(worldlocx, worldlocy, worldlocz);
			if (depthZ <= 0) {
				TRANSFM2_clipobjecteyez(savedViewPosition[0], savedViewPosition[1], savedViewPosition[2]);
			}
			g_flightDrawLineFn(g_flightClipLeft + TRANSFM2_ProjectScreenX(viewX, depthZ),
							   g_flightClipTop + TRANSFM2_ProjectScreenY(viewY, depthZ), screenX, screenY,
							   0x36);
		}
	}

	if (g_objectTable[objectIdx].mobj != NULL) {
		iff = (uint8_t)g_objectTable[objectIdx].mobj->iff;
	} else {
		iff = g_missionFlightGroups[g_objectTable[objectIdx].flightGroupIdx].fg.iff;
	}
	switch (iff) {
		case 0:
			color = 63;
			break;
		case 1:
		case 4:
			color = 55;
			break;
		case 2:
			color = 51;
			break;
		case 3:
			color = 59;
			break;
		case 5:
			color = 59;
			break;
		default:
			color = 59;
			break;
	}
	FlightText_SetBackgroundColor(color);
	FlightText_SetFontTier(0);

	drawOverlay = 0;
	if (objectIdx < g_craftDataPoolCapacity || g_objectTable[objectIdx].mobj == NULL ||
		g_objectTable[objectIdx].mobj->pCraft != NULL) {
		drawOverlay = 1;
	}
	if (drawOverlay == 0) {
		return;
	}

	viewX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY,
								   -65536 - g_players[g_localPlayer].viewState.savedTargetZ);
	viewY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY,
								   -65536 - g_players[g_localPlayer].viewState.savedTargetZ);
	depthZ = TRANSFM2_CamMatDotRow2(g_camRelWorldX, g_camRelWorldY,
									-65536 - g_players[g_localPlayer].viewState.savedTargetZ);
	if (depthZ <= 0) {
		TRANSFM2_clipobjecteyez(savedViewPosition[0], savedViewPosition[1], savedViewPosition[2]);
	}
	lineScreenX = TRANSFM2_ProjectScreenX(viewX, depthZ);
	lineScreenY = TRANSFM2_ProjectScreenY(viewY, depthZ);
	lineScreenX += g_flightClipLeft;
	lineScreenY += g_flightClipTop;
	g_flightDrawLineFn(lineScreenX, lineScreenY, screenX, screenY, g_flightTextBgColor);

	if (g_objectTable[objectIdx].mobj != NULL && g_objectTable[objectIdx].mobj->state == 0) {
		int moveY;

		if (g_objectTable[objectIdx].mobj->orientMatrixDirty != 0) {
			FVIEW_calcrotatemove(g_objectTable[objectIdx].pitch, g_objectTable[objectIdx].yaw,
								 &g_objectTable[objectIdx]);
			FVIEW_calcrotateorient(g_objectTable[objectIdx].roll, 0, &g_objectTable[objectIdx]);
		}
		g_camRelWorldX += Math_MulQ15(256, g_objectTable[objectIdx].mobj->moveX);
		g_camRelWorldY += Math_MulQ15(256, g_objectTable[objectIdx].mobj->moveY);
		if (g_objectTable[objectIdx].mobj->speed < 0x400) {
			g_camRelWorldX +=
				Math_MulQ15(32 * g_objectTable[objectIdx].mobj->speed, g_objectTable[objectIdx].mobj->moveX);
			moveY =
				Math_MulQ15(32 * g_objectTable[objectIdx].mobj->speed, g_objectTable[objectIdx].mobj->moveY);
		} else {
			g_camRelWorldX += g_objectTable[objectIdx].mobj->moveX;
			moveY = g_objectTable[objectIdx].mobj->moveY;
		}
		velocityViewPosition[2] = depthZ;
		g_camRelWorldY += moveY;
		velocityViewPosition[0] = viewX;
		velocityViewPosition[1] = viewY;
		viewX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY,
									   -65536 - g_players[g_localPlayer].viewState.savedTargetZ);
		viewY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY,
									   -65536 - g_players[g_localPlayer].viewState.savedTargetZ);
		depthZ = TRANSFM2_CamMatDotRow2(g_camRelWorldX, g_camRelWorldY,
										-65536 - g_players[g_localPlayer].viewState.savedTargetZ);
		if (depthZ <= 0) {
			TRANSFM2_clipobjecteyez(velocityViewPosition[0], velocityViewPosition[1],
									velocityViewPosition[2]);
		}
		g_flightDrawLineFn(g_flightClipLeft + TRANSFM2_ProjectScreenX(viewX, depthZ),
						   g_flightClipTop + TRANSFM2_ProjectScreenY(viewY, depthZ), lineScreenX, lineScreenY,
						   g_flightTextBgColor);
	}

	FlightText_SetBackgroundColor(0x40);
	textX = (int16_t)screenX;
	textY = (int16_t)screenY;
	boxExtent = Targeting_GetObjectBoxExtent(objectIdx);
	boxExtent *= g_projScaleInt;
	boxExtent /= savedViewPosition[2];
	if (boxExtent < g_screenWidth / 0x50u) {
		boxExtent = g_screenWidth / 0x50u;
	}
	if (boxExtent > (unsigned int)g_screenWidth >> 1) {
		boxExtent = (unsigned int)g_screenWidth >> 1;
	}
	displayExtent = boxExtent + 4;
	if (g_objectTable[objectIdx].mobj != NULL || g_objectTable[objectIdx].genusId != 8) {
		Hud_AppendObjectDisplayName(objectIdx, 2);
	} else {
		g_flightTextScratchBuffer[0] = 0;
	}
	if (g_flightTextScratchBuffer[0] != 0) {
		FlightText_SetCursor(textX - (FlightText_MeasureStringWidth(g_flightTextScratchBuffer) >> 1),
							 textY - g_flightFontLineHeight - displayExtent / 2 - 1);
		FlightText_DrawString(g_flightTextScratchBuffer);
	}
	if (g_objectTable[objectIdx].genusId != 6 && g_objectTable[objectIdx].genusId != 7) {
		textY += displayExtent >> 1;
		FlightText_SetBackgroundColor(0x40);
		if (g_players[g_localPlayer].viewState.cameraFocusObjIdx != UINT16_MAX) {
			pai_ObjectRefDirectionToObjectRef(g_players[g_localPlayer].viewState.cameraFocusObjIdx,
											  objectIdx);
			FlightText_SetCursor(textX - FlightText_MeasureStringWidth("00"), textY + 1);
			trig2_polardistance *= 161;
			distance = (trig2_polardistance >> 16) & 0xffff;
			if (distance >= 10000) {
				distance = 9999;
			}
			FlightText_DrawDecimalNumber((uint16_t)(distance / 100), 2, 1);
			g_flightDrawCharFn(0x2E);
			FlightText_DrawDecimalNumber((uint16_t)(distance - distance / 100 * 100), 2, 2);
		}
	}
}

// FUNCTION: XVT 0x437570
void FlightMap_DrawObjectIconAtViewPos(int objectIdx, int viewX, int viewY, int viewZ) {
	ObjectRecord* object;
	MobileObject* mobileObject;
	int objectType;
	int useMapIconBlitter;
	int iff;
	int colorGroup;
	int screenX;
	int screenY;
	int frameIdx;
	int iconWidth;

	object = &g_objectTable[objectIdx];
	objectType = object->objectType;
	mobileObject = object->mobj;
	if (mobileObject != NULL) {
		iff = (uint8_t)mobileObject->iff;
	} else {
		iff = g_missionFlightGroups[object->flightGroupIdx].fg.iff;
	}

	useMapIconBlitter = 0;
	switch (iff) {
		case 0:
			colorGroup = 0;
			break;
		case 1:
		case 4:
			colorGroup = 2;
			break;
		case 2:
			colorGroup = 3;
			break;
		case 3:
			useMapIconBlitter = 1;
			colorGroup = 2;
			break;
		case 5:
			colorGroup = 1;
			break;
		default:
			colorGroup = 1;
			break;
	}

	screenX = TRANSFM2_ProjectScreenX(viewX, viewZ);
	screenY = TRANSFM2_ProjectScreenY(viewY, viewZ);
	screenX += g_flightClipLeft;
	screenY += g_flightClipTop;
	if (g_flightIconResourcePath == g_flightIcons640x480ResourcePath) {
		frameIdx = 19;
		if (objectType <= 105) {
			frameIdx = g_flightIcons640FrameByObjectType[objectType];
		}
		iconWidth = g_flightIcons640WidthByFrame[frameIdx];
		objectType = g_flightIcons640HeightByFrame[frameIdx];
	} else if (g_flightIconResourcePath == g_flightMapIcons320x240ResourcePath) {
		frameIdx = 19;
		if (objectType <= 105) {
			frameIdx = g_flightMapIcons320x240FrameByObjectType[objectType];
		}
		iconWidth = g_flightMapIcons320x240WidthByFrame[frameIdx];
		objectType = g_flightMapIcons320x240HeightByFrame[frameIdx];
	} else if (g_flightIconResourcePath == g_flightMapIcons480x360ResourcePath) {
		frameIdx = 19;
		if (objectType <= 105) {
			frameIdx = g_flightMapIcons480x360FrameByObjectType[objectType];
		}
		iconWidth = g_flightMapIcons480x360WidthByFrame[frameIdx];
		objectType = g_flightMapIcons480x360HeightByFrame[frameIdx];
	} else {
		frameIdx = useMapIconBlitter;
		iconWidth = useMapIconBlitter;
	}

	if (colorGroup > 3) {
		colorGroup &= 3;
	}
	frameIdx += g_flightIconFrameCount * colorGroup / 4;
	screenX -= iconWidth / 2;
	screenY -= objectType / 2;
	if ((int16_t)screenX >= g_flightClipLeft && (int16_t)(screenX + iconWidth) < g_flightClipRight &&
		(int16_t)screenY >= g_flightClipTop && (int16_t)(screenY + objectType) < g_flightClipBottom) {
		FlightSurface_Lock();
		if (useMapIconBlitter != 0) {
			FlightSw_BlitMapIconRle(g_flightIconFrames[frameIdx], screenX, screenY, 0, 0);
		} else {
			g_flightBlitSpriteFn(g_flightIconFrames[frameIdx], screenX, screenY, 0, 0);
		}
		FlightSurface_Unlock();
	}
}

// FUNCTION: XVT 0x437860
void FlightMap_DrawObjectBoxCorners(int x, int y, int width, int height, unsigned int colorIndex) {
	int cornerWidth;
	int cornerHeight;
	int spanStart;
	int spanEnd;
	int screenY;
	int row;

	if (y + height > 0 && x + width > 0 && g_flightVpWidth > x && g_flightVpHeight > y && height > 0 &&
		width > 0) {
		cornerWidth = width >> 3;
		cornerHeight = height >> 3;
		if (cornerWidth < 3) {
			cornerWidth = 3;
		}
		if (cornerHeight < 3) {
			cornerHeight = 3;
		}
		if (cornerWidth > width) {
			cornerWidth = width;
		}
		if (cornerHeight > height) {
			cornerHeight = height;
		}

		FlightSurface_Lock();
		if (y >= 0) {
			spanStart = x;
			spanEnd = x + cornerWidth;
			if (spanEnd > 0 && x < g_flightVpWidth) {
				if (spanStart < 0) {
					spanStart = 0;
				}
				if (spanEnd > g_flightVpWidth) {
					spanEnd = g_flightVpWidth;
				}
				FlightSw_DrawHorizontalColorSpan(spanStart, spanEnd, y, colorIndex);
			}

			spanStart = x + width - cornerWidth;
			spanEnd = x + width;
			if (spanEnd > 0 && spanStart < g_flightVpWidth) {
				if (spanStart < 0) {
					spanStart = 0;
				}
				if (spanEnd > g_flightVpWidth) {
					spanEnd = g_flightVpWidth;
				}
				FlightSw_DrawHorizontalColorSpan(spanStart, spanEnd, y, colorIndex);
			}
		}

		if (g_flightVpHeight >= y + height) {
			spanStart = x;
			spanEnd = x + cornerWidth;
			if (spanEnd > 0 && x < g_flightVpWidth) {
				if (spanStart < 0) {
					spanStart = 0;
				}
				if (spanEnd > g_flightVpWidth) {
					spanEnd = g_flightVpWidth;
				}
				FlightSw_DrawHorizontalColorSpan(spanStart, spanEnd, y + height - 1, colorIndex);
			}

			spanStart = x + width - cornerWidth;
			spanEnd = x + width;
			if (spanEnd > 0 && spanStart < g_flightVpWidth) {
				if (spanStart < 0) {
					spanStart = 0;
				}
				if (spanEnd > g_flightVpWidth) {
					spanEnd = g_flightVpWidth;
				}
				FlightSw_DrawHorizontalColorSpan(spanStart, spanEnd, y + height - 1, colorIndex);
			}
		}

		for (row = 1; row < cornerHeight; ++row) {
			screenY = y + row;
			if (screenY >= 0 && screenY < g_flightVpHeight) {
				if (x >= 0) {
					FlightSw_DrawHorizontalColorSpan(x, x + 1, screenY, colorIndex);
				}
				if (g_flightVpWidth >= x + width) {
					FlightSw_DrawHorizontalColorSpan(x + width - 1, x + width, screenY, colorIndex);
				}
			}
		}

		for (row = height - cornerHeight; row < height - 1; ++row) {
			if (row >= cornerHeight) {
				screenY = y + row;
				if (screenY >= 0 && screenY < g_flightVpHeight) {
					if (x >= 0) {
						FlightSw_DrawHorizontalColorSpan(x, x + 1, screenY, colorIndex);
					}
					if (g_flightVpWidth >= x + width) {
						FlightSw_DrawHorizontalColorSpan(x + width - 1, x + width, screenY, colorIndex);
					}
				}
			}
		}
		FlightSurface_Unlock();
	}
}

// FUNCTION: XVT 0x437B20
void FlightMap_DrawGrid(void) {
	int lineCount;
	int gridX;
	int gridY;
	int otherViewX;
	int otherViewY;
	int otherDepthZ;
	int swapValue;
	int savedTargetX;
	int savedTargetY;
	int savedTargetZ;

	worldlocz = 0;
	worldlocy = 0;
	worldlocx = 0;
	gridY = -0x100000;
	lineCount = 33;
	do {
		savedTargetX = g_players[g_localPlayer].viewState.savedTargetX;
		savedTargetY = g_players[g_localPlayer].viewState.savedTargetY;
		savedTargetZ = g_players[g_localPlayer].viewState.savedTargetZ;
		g_camRelWorldX = -0x100000 - savedTargetX;
		g_camRelWorldY = gridY - savedTargetY;
		g_camRelWorldZ = -0x10000 - savedTargetZ;
		gridY += 0x10000;
		depthZ = TRANSFM2_CamMatDotRow2(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
		otherDepthZ = (int)((uint32_t)depthZ + ((uint32_t)g_camMatR2_X << 6));
		if (otherDepthZ > 0 || depthZ > 0) {
			viewX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
			viewY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
			otherViewX = (int)((uint32_t)viewX + ((uint32_t)g_camMatR0_X << 6));
			otherViewY = (int)((uint32_t)viewY + ((uint32_t)g_camMatR1_X << 6));
			if (otherDepthZ <= 0) {
				swapValue = otherDepthZ;
				otherDepthZ = depthZ;
				depthZ = swapValue;
				swapValue = otherViewY;
				otherViewY = viewY;
				viewY = swapValue;
				swapValue = otherViewX;
				otherViewX = viewX;
				viewX = swapValue;
			}
			if (depthZ <= 0) {
				TRANSFM2_clipobjecteyez(otherViewX, otherViewY, otherDepthZ);
			}
			FlightSurface_Lock();
			g_flightDrawLineFn(g_flightClipLeft + TRANSFM2_ProjectScreenX(viewX, depthZ),
							   g_flightClipTop + TRANSFM2_ProjectScreenY(viewY, depthZ),
							   g_flightClipLeft + TRANSFM2_ProjectScreenX(otherViewX, otherDepthZ),
							   g_flightClipTop + TRANSFM2_ProjectScreenY(otherViewY, otherDepthZ), 0x31);
			FlightSurface_Unlock();
		}
		--lineCount;
	} while (lineCount != 0);

	lineCount = 33;
	gridX = worldlocx - 0x100000;
	gridY = worldlocy - 0x100000;
	do {
		savedTargetX = g_players[g_localPlayer].viewState.savedTargetX;
		savedTargetY = g_players[g_localPlayer].viewState.savedTargetY;
		savedTargetZ = g_players[g_localPlayer].viewState.savedTargetZ;
		g_camRelWorldX = gridX - savedTargetX;
		g_camRelWorldY = gridY - savedTargetY;
		g_camRelWorldZ = -0x10000 - savedTargetZ;
		gridX += 0x10000;
		depthZ = TRANSFM2_CamMatDotRow2(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
		otherDepthZ = (int)((uint32_t)depthZ + ((uint32_t)g_camMatR2_Y << 6));
		if (otherDepthZ > 0 || depthZ > 0) {
			viewX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
			viewY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
			otherViewX = (int)((uint32_t)viewX + ((uint32_t)g_camMatR0_Y << 6));
			otherViewY = (int)((uint32_t)viewY + ((uint32_t)g_camMatR1_Y << 6));
			if (otherDepthZ <= 0) {
				swapValue = otherDepthZ;
				otherDepthZ = depthZ;
				depthZ = swapValue;
				swapValue = otherViewY;
				otherViewY = viewY;
				viewY = swapValue;
				swapValue = otherViewX;
				otherViewX = viewX;
				viewX = swapValue;
			}
			if (depthZ <= 0) {
				TRANSFM2_clipobjecteyez(otherViewX, otherViewY, otherDepthZ);
			}
			FlightSurface_Lock();
			g_flightDrawLineFn(g_flightClipLeft + TRANSFM2_ProjectScreenX(viewX, depthZ),
							   g_flightClipTop + TRANSFM2_ProjectScreenY(viewY, depthZ),
							   g_flightClipLeft + TRANSFM2_ProjectScreenX(otherViewX, otherDepthZ),
							   g_flightClipTop + TRANSFM2_ProjectScreenY(otherViewY, otherDepthZ), 0x31);
			FlightSurface_Unlock();
		}
		--lineCount;
	} while (lineCount != 0);
}

// FUNCTION: XVT 0x437ED0
void nullsub_14(void) {}

// FUNCTION: XVT 0x438010
int FlightMap_PickObjectNearestScreenCenter(int playerIdx) {
	int bestScore;
	int bestObject = UINT16_MAX;
	int objectIdx;
	int objectSlot;

	if (g_players[playerIdx].mapCameraState > 1) {
		int hudPitch = ((g_players[playerIdx].mapCameraState & 0x7F) << 14) / -127;
		FVIEW_BuildCameraOrient(0, 0x4000, 0, 0, (int16_t)hudPitch, 0, NULL);
	} else {
		FVIEW_BuildCameraOrient(
			g_players[playerIdx].viewState.viewRoll, g_players[playerIdx].viewState.viewPitch,
			g_players[playerIdx].viewState.viewYaw, 0, g_players[playerIdx].viewState.hudAimX,
			g_players[playerIdx].viewState.hudAimY, NULL);
		if (g_players[playerIdx].viewState.aimTargetIdx != UINT16_MAX) {
			trig2_ctop(g_objectTable[g_players[playerIdx].viewState.aimTargetIdx].world_x -
						   g_players[playerIdx].viewState.savedTargetX,
					   g_objectTable[g_players[playerIdx].viewState.aimTargetIdx].world_y -
						   g_players[playerIdx].viewState.savedTargetY,
					   g_objectTable[g_players[playerIdx].viewState.aimTargetIdx].world_z -
						   g_players[playerIdx].viewState.savedTargetZ);
			FVIEW_BuildCameraOrient(0, pitchQ16, trig2_xyangle, 0, 0, 0, NULL);
		}
	}

	bestScore = (g_screenHeight * g_screenHeight + g_screenWidth * g_screenWidth) >> 3;
	objectIdx = 0;
	for (objectSlot = 0; objectSlot < (int)g_explosionObjectSlotEnd; ++objectSlot) {
		if (g_objectTable[objectIdx].objectType != 0) {
			switch (g_objectTable[objectIdx].genusId) {
				case 0:
				case 1:
				case 2:
				case 3:
				case 4:
				case 5: {
					int projectedX;
					int projectedY;
					int score;

					if (!RenderList_ProjectObjectBoundsForCulling(
							objectSlot, g_modelTypeTable[g_objectTable[objectIdx].objectType].maxBoundsExtent,
							playerIdx))
						break;
					projectedX = (viewX << perspShift) / depthZ;
					projectedY = (viewY << perspShift) / depthZ;
					score = projectedX * projectedX + projectedY * projectedY;
					if (g_players[playerIdx].viewState.aimTargetIdx == objectSlot ||
						g_players[playerIdx].viewState.cameraFocusObjIdx == objectSlot)
						score += (g_screenWidth >> 4) * (g_screenWidth >> 4) +
								 (g_screenHeight >> 4) * (g_screenHeight >> 4);
					if (score < bestScore) {
						bestScore = score;
						bestObject = objectSlot;
					}
					break;
				}
				case 6:
				case 7:
				case 8:
				case 9:
				case 10:
				case 11:
				case 12:
				case 13:
				default:
					break;
			}
		}
		++objectIdx;
	}
	objectIdx = g_regionMainObjectSlotEnd;
	for (objectSlot = g_regionMainObjectSlotEnd;
		 objectSlot < g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount; ++objectSlot) {
		if (g_objectTable[objectIdx].objectType != 0 && g_objectTable[objectIdx].genusId >= 8 &&
			g_objectTable[objectIdx].genusId <= 9 &&
			RenderList_ProjectObjectBoundsForCulling(
				objectSlot, g_modelTypeTable[g_objectTable[objectIdx].objectType].maxBoundsExtent,
				playerIdx)) {
			int projectedX = (viewX << perspShift) / depthZ;
			int projectedY = (viewY << perspShift) / depthZ;
			int score = projectedX * projectedX + projectedY * projectedY;
			if (g_players[playerIdx].viewState.aimTargetIdx == objectSlot ||
				g_players[playerIdx].viewState.cameraFocusObjIdx == objectSlot)
				score += (g_screenWidth >> 4) * (g_screenWidth >> 4) +
						 (g_screenHeight >> 4) * (g_screenHeight >> 4);
			if (score < bestScore) {
				bestScore = score;
				bestObject = objectSlot;
			}
		}
		++objectIdx;
	}
	return bestObject;
}

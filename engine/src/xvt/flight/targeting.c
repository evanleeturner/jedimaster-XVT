#include "xvt/flight/targeting.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_hud.h"
#endif

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

/* How far off the aim line the object Targeting_TestAimCone last tested lies:
 * its up slope times 59578 / 65536 plus its side slope, each 256 times the
 * offset over the forward distance; 0xFFFF when the test returned early. Only
 * Targeting_TestAimCone writes it; Player_PickTargetInSight keeps the object
 * with the lowest. */
// GLOBAL: XVT 0x9A73A0
uint16_t g_targetAngleScore = -1;

/* Tells whether an object, or the point a reference names, lies in the aim cone
 * ahead of the player's craft: returns 1 when both its up and side slopes are
 * below a bound set by its size and distance, else 0. Returns 0 at once when
 * the player has no craft, the point is not ahead or is more than 0x20000 ahead
 * after scaling, or the side slope exceeds 160 or the up slope 100. Within a
 * rough distance of 655360 it measures at 1/16 scale and, for the player's
 * current target craft, at its selected component's center; beyond, at 1/256.
 * narrowCone keeps the bound at the object's size, halving it close in;
 * otherwise the bound is tripled, at least 9. Writes g_targetAngleScore (0xFFFF
 * first), g_lastRoughDistance and g_worldLocX, g_worldLocY and g_worldLocZ.
 * Reads the size of g_objectTable[objectIdx] without checking that objectIdx
 * names an object slot. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x482640
int16_t Targeting_TestAimCone(uint16_t objectIdx, int16_t narrowCone,
			      int playerIdx)
{
	int16_t scaleShift;
	int dx;
	int16_t dy, dz;
	ObjectRecord *target;
	int objectType;
	uint16_t meshIndex;
	int forward;
	int side;
	int up;
	int sideSlope;
	int upSlope;
	int targetExtent;
	int extentSlope;
	ModelIndex modelIndex;
	int upScore;
	int16_t boundSum;
	uint8_t boundShift;
	MobileObject **playerMobileObject;
	uint16_t objectIndex;

	g_targetAngleScore = -1;
	objectIndex = objectIdx;
	if (g_players[playerIdx].objectIndex == -1) {
		return 0;
	}
	pai_ObjectRefUpdateRoughDistance(objectIndex,
					 g_players[playerIdx].objectIndex);

	if (g_lastRoughDistance < 655360) {
		Mission_ResolveObjectOrMissionPointWorldLoc(objectIndex, 0);
		if (g_players[playerIdx].currentTargetObjectIdx ==
		    (int16_t)objectIndex) {
			target = &g_objectTable[objectIndex];
			objectType = target->objectType;
			if (objectType != 0 &&
			    g_activeRegionCraftObjectSlotEnd >
				    (int)objectIndex &&
			    target->mobj->pCraft != NULL) {
				meshIndex = (uint16_t)g_players[playerIdx]
						    .selectedTargetComponent;
				pai_RotateLocalVectorToWorldScratch(
					target,
					ModelMesh_GetCenterX(objectType,
							     meshIndex),
					ModelMesh_GetCenterZ(objectType,
							     meshIndex),
					-ModelMesh_GetCenterY(objectType,
							      meshIndex));
				g_worldLocX += g_rotatedX;
				g_worldLocY += g_rotatedY;
				g_worldLocZ += g_rotatedZ;
				g_lastRoughDistance = collide_roughdistance3d(
					g_worldLocX -
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.world_x,
					g_worldLocY -
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.world_y,
					g_worldLocZ -
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.world_z);
			}
		}
		dx = (g_worldLocX -
		      g_objectTable[g_players[playerIdx].objectIndex]
			      .world_x) >>
		     4;
		dy = (g_worldLocY -
		      g_objectTable[g_players[playerIdx].objectIndex]
			      .world_y) >>
		     4;
		dz = (g_worldLocZ -
		      g_objectTable[g_players[playerIdx].objectIndex]
			      .world_z) >>
		     4;
		scaleShift = 4;
	} else {
		Mission_ResolveObjectOrMissionPointWorldLoc(objectIndex, 0);
		dx = (g_worldLocX -
		      g_objectTable[g_players[playerIdx].objectIndex]
			      .world_x) >>
		     8;
		dy = (g_worldLocY -
		      g_objectTable[g_players[playerIdx].objectIndex]
			      .world_y) >>
		     8;
		dz = (g_worldLocZ -
		      g_objectTable[g_players[playerIdx].objectIndex]
			      .world_z) >>
		     8;
		scaleShift = 8;
	}

	target = &g_objectTable[g_players[playerIdx].objectIndex];
	if (target->mobj->orientMatrixDirty != 0) {
		FVIEW_calcrotatemove(target->pitch, target->yaw, target);
		FVIEW_calcrotateorient(
			g_objectTable[g_players[playerIdx].objectIndex].roll, 0,
			&g_objectTable[g_players[playerIdx].objectIndex]);
	}
	playerMobileObject =
		&g_objectTable[g_players[playerIdx].objectIndex].mobj;
	forward = Math_MulQ15((int16_t)dx, (*playerMobileObject)->cachedFwdX);
	forward += Math_MulQ15(dy, (*playerMobileObject)->cachedFwdY);
	forward += Math_MulQ15(dz, (*playerMobileObject)->cachedFwdZ);
	if (forward <= 0) {
		return 0;
	}
	if (forward > 0x20000) {
		return 0;
	}
	if (narrowCone != 0 && forward < 0x2000) {
		++scaleShift;
	}
	side = Math_MulQ15((int16_t)dx, (*playerMobileObject)->cachedSideX);
	side += Math_MulQ15(dy, (*playerMobileObject)->cachedSideY);
	side += Math_MulQ15(dz, (*playerMobileObject)->cachedSideZ);
	if (side < 0) {
		side = -side;
	}
	sideSlope = (int)(((uint64_t)(unsigned int)side << 8) + 128) /
		    (unsigned int)forward;
	if (sideSlope > 160) {
		return 0;
	}
	up = Math_MulQ15((int16_t)dx, (*playerMobileObject)->cachedUpX);
	up += Math_MulQ15(dy, (*playerMobileObject)->cachedUpY);
	up += Math_MulQ15(dz, (*playerMobileObject)->cachedUpZ);
	if (up < 0) {
		up = -up;
	}
	upSlope = (int)(((uint64_t)(unsigned int)up << 8) + 128) /
		  (unsigned int)forward;
	if (upSlope > 100) {
		return 0;
	}
	upScore = (59578 * upSlope) >> 16;
	target = &g_objectTable[objectIndex];
	if (target->mobj != NULL && target->mobj->pCraft != NULL) {
		modelIndex = target->mobj->pCraft->modelIndex;
		boundSum = g_modelDefs[modelIndex].boundSizeX;
		boundSum += g_modelDefs[modelIndex].boundSizeY;
		boundSum += g_modelDefs[modelIndex].boundSizeZ;
		boundShift = g_modelDefs[modelIndex].boundSizeShift;
		targetExtent = (boundSum / 3) << boundShift;
	} else {
		targetExtent =
			g_objectTypeTable[target->objectType].maxBoundsExtent;
	}
	extentSlope =
		(int)(((uint64_t)(unsigned int)(targetExtent >> scaleShift)
		       << 8) +
		      128) /
		(unsigned int)forward;
	if (extentSlope <= 0) {
		extentSlope = 1;
	}
	if (narrowCone == 0) {
		extentSlope *= 3;
		if (extentSlope < 10) {
			extentSlope = 9;
		}
	}
	g_targetAngleScore = (uint16_t)(upScore + sideSlope);
	return upScore < extentSlope && sideSlope < extentSlope;
}

/* Draws a target box around craft in the active region for the local player,
 * skipping empty slots, the player's own craft, its current target and craft
 * with an active decoy beam. Only some craft get a box, each in a color named
 * below: in a melee, the local player's team's starfighters
 * (COLOR_LOCAL_QUICK_START_CRAFT) and the craft of any team whose score is the
 * highest above 0 (COLOR_LEADING_TEAM); other players' craft, hostile or allied
 * in a melee, else by IFF. Unless locatePlayersEnabled is set, a hostile craft
 * the local player's team has not identified gets none. */
// FUNCTION: XVT 0x482BE0
void Targeting_DrawSceneObjectBoxes(void)
{
	enum {
		PLAYABLE_TEAM_COUNT = 8,
		NO_LEADING_TEAM = 10,
		COLOR_LOCAL_QUICK_START_CRAFT = 47,
		COLOR_HOSTILE_PLAYER = 51,
		COLOR_REBEL = 63,
		COLOR_IMPERIAL = 55,
		COLOR_BLUE = 51,
		COLOR_DEFAULT = 59,
		COLOR_LEADING_TEAM = 212,
		COLOR_ALLIED_PLAYER = 211,
	};

	int leadingTeam;
	int leadingScore;
	int teamIndex;
	int objectIdx;

	leadingTeam = NO_LEADING_TEAM;
	if (g_missionHeader.missionType == MISSION_TYPE_MELEE) {
		leadingScore = 0;
		for (teamIndex = 0; teamIndex < PLAYABLE_TEAM_COUNT;
		     ++teamIndex) {
			int teamScore;

			teamScore = g_flightMissionState.runtime
					    .teamScores[TEAM_SCORE_BONUS]
						       [teamIndex] +
				    g_flightMissionState.runtime
					    .teamScores[TEAM_SCORE_MISSION]
						       [teamIndex];
			if (teamScore > leadingScore) {
				leadingScore = teamScore;
				leadingTeam = teamIndex;
			}
		}
	}

	for (objectIdx = g_activeRegionObjectSlotStart;
	     objectIdx < (int)g_activeRegionCraftObjectSlotEnd; ++objectIdx) {
		ObjectRecord *object;
		uint8_t colorIndex;
		int team;
		int teamScore;

		object = &g_objectTable[objectIdx];
		if (object->objectType == 0 ||
		    g_players[g_localPlayer].objectIndex == objectIdx) {
			continue;
		}

		colorIndex = 0;
		team = object->mobj->team;
		teamScore = g_flightMissionState.runtime
				    .teamScores[TEAM_SCORE_MISSION][team];
		teamScore += g_flightMissionState.runtime
				     .teamScores[TEAM_SCORE_BONUS][team];
		if (g_missionHeader.missionType == MISSION_TYPE_MELEE &&
		    (uint16_t)g_players[g_localPlayer].team == team &&
		    object->genusId == CRAFT_GENUS_STARFIGHTER) {
			colorIndex = COLOR_LOCAL_QUICK_START_CRAFT;
		} else if (leadingTeam == NO_LEADING_TEAM ||
			   teamScore != leadingScore) {
			if (object->playerOwnerIdx != -1 &&
			    object->playerOwnerIdx != g_localPlayer) {
				if (g_missionHeader.missionType ==
				    MISSION_TYPE_MELEE) {
					int craftTeam;
					int playerTeam;
					int isHostile;

					craftTeam =
						g_missionFlightGroups
							[g_objectTable[(uint16_t)
									       objectIdx]
								 .flightGroupIdx]
								.fg.team;
					playerTeam =
						(uint16_t)
							g_players[g_localPlayer]
								.team;
					if (craftTeam == playerTeam) {
						isHostile = 0;
					} else {
						isHostile =
							g_missionTeams[playerTeam]
								.allies[craftTeam] ==
							0;
					}
					if (isHostile) {
						colorIndex =
							COLOR_HOSTILE_PLAYER;
					} else {
						colorIndex =
							COLOR_ALLIED_PLAYER;
					}
				} else {
					switch (object->mobj->iff) {
					case 0:
						colorIndex = COLOR_REBEL;
						break;
					case 1:
					case 4:
						colorIndex = COLOR_IMPERIAL;
						break;
					case 2:
						colorIndex = COLOR_BLUE;
						break;
					default:
						colorIndex = COLOR_DEFAULT;
						break;
					}
				}
			}
		} else {
			colorIndex = COLOR_LEADING_TEAM;
		}

		if (colorIndex != 0) {
			CraftData *craft;
			int playerTeam;
			int craftTeam;
			int isHostile;

			craft = object->mobj->pCraft;
			if (g_flightMissionState.locatePlayersEnabled == 0) {
				playerTeam =
					(uint16_t)g_players[g_localPlayer].team;
				if (craft->identifiedOrderByTeam[playerTeam] ==
				    0) {
					craftTeam =
						g_missionFlightGroups
							[g_objectTable[(uint16_t)
									       objectIdx]
								 .flightGroupIdx]
								.fg.team;
					if (craftTeam == playerTeam) {
						isHostile = 0;
					} else {
						isHostile =
							g_missionTeams[playerTeam]
								.allies[craftTeam] ==
							0;
					}
					if (isHostile) {
						continue;
					}
				}
			}

			if (Object_HasActiveDecoyBeam((uint16_t)objectIdx) ==
				    0 &&
			    (uint16_t)g_players[g_localPlayer]
					    .currentTargetObjectIdx !=
				    objectIdx) {
				Targeting_DrawObjectBox(objectIdx, UINT16_MAX,
							colorIndex);
			}
		}
	}
}

/* Draws the corner box around an object, or around its component componentIdx
 * unless that is UINT16_MAX, as the local player sees it. The size is the
 * component's max extent or Targeting_GetObjectBoxExtent, scaled by
 * g_projScaleInt over depth and clamped from 4 pixels (8 above 320x240) up to
 * three quarters of the screen width, plus 4. Draws map-view corners when the
 * local player's mapCameraState is set, else depth-tested HUD corners. Does
 * nothing for UINT16_MAX, in replay view, with the local player's target box
 * off, or behind the camera. The modern build also hands the box to
 * XvtRenderHud_TargetBox. */
// FUNCTION: XVT 0x482EB0
void Targeting_DrawObjectBox(uint16_t objectIdx, uint16_t componentIdx,
			     uint8_t colorIndex)
{
	enum {
		LOW_RESOLUTION_MIN_BOX_EXTENT = 4,
		DEFAULT_MIN_BOX_EXTENT = 8,
		BOX_SIZE_PADDING = 4,
	};

	unsigned int componentIndex;
	int screenX;
	int screenY;
	int depth;
	int objectExtent;
	int projectedExtent;
	int minimumExtent;
	int maximumExtent;
	int boxSize;

	if (objectIdx == UINT16_MAX || g_replayViewMode != 0 ||
	    g_players[g_localPlayer].targetBoxEnabled == 0) {
		return;
	}

	componentIndex = componentIdx;
	Targeting_ProjectObjectOrMissionPoint(objectIdx, componentIndex,
					      &screenX, &screenY, &depth);
	if (depth <= 0) {
		return;
	}

	if (componentIdx != UINT16_MAX) {
		objectExtent = ModelMesh_GetComponentMaxExtent(
			g_objectTable[objectIdx].objectType, componentIndex);
	} else {
		objectExtent = Targeting_GetObjectBoxExtent(objectIdx);
	}
#ifdef XVT_MODERN
	XvtRenderHud_TargetBox(objectIdx, componentIdx, objectExtent,
			       colorIndex);
#endif
	projectedExtent =
		(int)((unsigned int)(objectExtent * (int)g_projScaleInt) /
		      (unsigned int)depth);
	switch (g_flightResolutionMode) {
	case FLIGHT_RESOLUTION_320X240:
		minimumExtent = LOW_RESOLUTION_MIN_BOX_EXTENT;
		break;
	default:
		minimumExtent = DEFAULT_MIN_BOX_EXTENT;
		break;
	}
	maximumExtent = (int)((g_screenWidth >> 1) + (g_screenWidth >> 2));
	if (minimumExtent > projectedExtent) {
		projectedExtent = minimumExtent;
	}
	if (maximumExtent < projectedExtent) {
		projectedExtent = maximumExtent;
	}

	boxSize = projectedExtent + BOX_SIZE_PADDING;
	if (g_players[g_localPlayer].mapCameraState != 0) {
		int halfBoxSize;
		unsigned int drawColor;

		halfBoxSize = boxSize / 2;
		drawColor = colorIndex;
		FlightMap_DrawObjectBoxCorners(screenX - halfBoxSize,
					       screenY - halfBoxSize, boxSize,
					       boxSize, drawColor);
	} else {
		int halfBoxSize;
		unsigned int drawColor;

		halfBoxSize = boxSize / 2;
		drawColor = colorIndex;
		Hud_DrawDepthTestedBoxCorners(screenX - halfBoxSize,
					      screenY - halfBoxSize, boxSize,
					      boxSize, drawColor, depth);
	}
}

/* Returns an object's size for target boxes: for a craft, the mean of its
 * model's three bound sizes shifted left by boundSizeShift; else its type's
 * maxBoundsExtent. */
// FUNCTION: XVT 0x483030
int Targeting_GetObjectBoxExtent(unsigned int objectIdx)
{
	ObjectRecord *object;
	MobileObject *mobileObject;
	CraftData *craft;
	unsigned int modelIndex;
	int averageExtent;

	object = &g_objectTable[objectIdx];
	mobileObject = object->mobj;
	if (mobileObject != 0) {
		craft = mobileObject->pCraft;
		if (craft != 0) {
			modelIndex = craft->modelIndex;
			averageExtent = g_modelDefs[modelIndex].boundSizeX;
			averageExtent += g_modelDefs[modelIndex].boundSizeY;
			averageExtent += g_modelDefs[modelIndex].boundSizeZ;
			averageExtent /= 3;
			return (int)((unsigned int)averageExtent
				     << g_modelDefs[modelIndex].boundSizeShift);
		}

		return g_objectTypeTable[object->objectType].maxBoundsExtent;
	}

	return g_objectTypeTable[object->objectType].maxBoundsExtent;
}

/* Projects an object, a mission point reference (0x8000 and up, read from
 * flight group 0), or the center of the object's component componentIdx unless
 * that is UINT16_MAX, through the local player's camera. Always writes the view
 * depth to outViewZ; writes the screen position only when the depth is
 * positive. Writes g_worldLocX, g_worldLocY and g_worldLocZ. Only this file
 * calls it. */
// FUNCTION: XVT 0x4830D0
void Targeting_ProjectObjectOrMissionPoint(unsigned int objOrMissionPointRef,
					   uint16_t componentIdx,
					   int *outScreenX, int *outScreenY,
					   int *outViewZ)
{
	int deltaX;
	int deltaY;
	int deltaZ;
	int viewX;
	int viewY;
	int viewZ;
	ObjectRecord *object;
	int objectType;
	int localFwd;
	int localUp;
	int localSide;

	Mission_ResolveObjectOrMissionPointWorldLoc(objOrMissionPointRef, 0);
	if (componentIdx != UINT16_MAX) {
		object = &g_objectTable[objOrMissionPointRef];
		objectType = object->objectType;
		localFwd = ModelMesh_GetCenterY(objectType, componentIdx);
		localFwd = -localFwd;
		localUp = ModelMesh_GetCenterZ(objectType, componentIdx);
		localSide = ModelMesh_GetCenterX(objectType, componentIdx);

		pai_RotateLocalVectorToWorldScratch(object, localSide, localUp,
						    localFwd);
		g_worldLocX += g_rotatedX;
		g_worldLocY += g_rotatedY;
		g_worldLocZ += g_rotatedZ;
	}

	deltaX = g_worldLocX - g_players[g_localPlayer].viewState.cameraWorldX;
	deltaY = g_worldLocY - g_players[g_localPlayer].viewState.cameraWorldY;
	deltaZ = g_worldLocZ - g_players[g_localPlayer].viewState.cameraWorldZ;
	viewZ = TRANSFM2_CamMatDotRow2(deltaX, deltaY, deltaZ);
	*outViewZ = viewZ;
	if (viewZ > 0) {
		viewX = TRANSFM2_CamMatDotRow0(deltaX, deltaY, deltaZ);
		viewY = TRANSFM2_CamMatDotRow1(deltaX, deltaY, deltaZ);
		*outScreenX = TRANSFM2_ProjectScreenX(viewX, viewZ);
		*outScreenY = TRANSFM2_ProjectScreenY(viewY, viewZ);
	}
}

/* Nothing calls this. Writes an object's projected size in pixels, seen from
 * cameraX, cameraY and cameraZ through the current camera matrix, to both
 * outWidth and outHeight: the craft model's mean bound size, or the type's
 * maxBoundsExtent, times g_projScaleInt over view depth, measured at 1/16 scale
 * within 0x80000 units and 1/256 beyond. Writes 0 for UINT16_MAX or an object
 * behind the camera. Writes g_worldLocX, g_worldLocY, g_worldLocZ and trig2's
 * polar results. */
// FUNCTION: XVT 0x483220
void Targeting_ComputeProjectedObjectExtent(uint16_t objectIdx,
					    uint16_t *outWidth,
					    uint16_t *outHeight, int cameraX,
					    int cameraY, int cameraZ)
{
	int objectIndex;
	int deltaX;
	int deltaY;
	int deltaZ;
	uint16_t distanceShift;
	int viewDepth;
	MobileObject *mobileObject;
	CraftData *craft;
	int16_t averageExtent;
	int maxBoundsExtent;
	unsigned int projectedExtent;

	if (objectIdx == UINT16_MAX) {
		*outWidth = 0;
		*outHeight = 0;
		return;
	}

	objectIndex = objectIdx;
	g_worldLocX = g_objectTable[objectIndex].world_x;
	g_worldLocY = g_objectTable[objectIndex].world_y;
	g_worldLocZ = g_objectTable[objectIndex].world_z;
	trig2_ctop(cameraX - g_worldLocX, cameraY - g_worldLocY,
		   cameraZ - g_worldLocZ);
	if (trig2_polardistance < 0x80000) {
		Mission_ResolveObjectOrMissionPointWorldLoc(objectIdx, 0);
		deltaX = (g_worldLocX - cameraX) >> 4;
		deltaY = (g_worldLocY - cameraY) >> 4;
		deltaZ = (g_worldLocZ - cameraZ) >> 4;
		distanceShift = 4;
	} else {
		Mission_ResolveObjectOrMissionPointWorldLoc(objectIdx, 0);
		deltaX = (g_worldLocX - cameraX) >> 8;
		deltaY = (g_worldLocY - cameraY) >> 8;
		deltaZ = (g_worldLocZ - cameraZ) >> 8;
		distanceShift = 8;
	}

	viewDepth = TRANSFM2_CamMatDotRow2((int16_t)deltaX, (int16_t)deltaY,
					   (int16_t)deltaZ);
	if (viewDepth <= 0) {
		*outWidth = 0;
		*outHeight = 0;
		return;
	}

	mobileObject = g_objectTable[objectIndex].mobj;
	if (mobileObject != 0 && (craft = mobileObject->pCraft) != 0) {
		averageExtent = g_modelDefs[craft->modelIndex].boundSizeX;
		averageExtent += g_modelDefs[craft->modelIndex].boundSizeY;
		averageExtent += g_modelDefs[craft->modelIndex].boundSizeZ;
		maxBoundsExtent =
			(averageExtent / 3)
			<< g_modelDefs[craft->modelIndex].boundSizeShift;
	} else {
		maxBoundsExtent =
			g_objectTypeTable[g_objectTable[objectIndex].objectType]
				.maxBoundsExtent;
	}

	maxBoundsExtent >>= distanceShift;
	projectedExtent =
		g_projScaleInt * maxBoundsExtent / (unsigned int)viewDepth;
	*outWidth = projectedExtent;
	*outHeight = projectedExtent;
}

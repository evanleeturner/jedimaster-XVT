#include "xvt/flight/flight_object.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#endif

#include "xvt/assets/model_mesh.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

#include <string.h>

// GLOBAL: XVT 0x9A8C06
uint16_t g_localDebrisRecycleSlotCursor = 0;

// GLOBAL: XVT 0xA90A5E
uint16_t g_billboardTextureSequenceIndex = 0;
// GLOBAL: XVT 0xA90A60
int16_t* g_billboardTextureFrameSequence = NULL;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4015B0
void FlightObject_UpdateSpecialBehavior(void) {
	enum {
		SPECIAL_BEHAVIOR_UPDATE_TICKS = 29,
		FIRST_CREW_OBJECT_TYPE = 100,
		LAST_CREW_OBJECT_TYPE = 105,
		FIRST_DYNAMIC_MODEL_TYPE = 73,
		SPECIAL_FRAGMENT_OBJECT_TYPE = 89,
		TURRET_PROJECTILE_TYPE = 2,
		TURRET_IDLE_ROTATION_STEP = 4,
		TURRET_DIRECTION_TOGGLE_CHANCE = 0x600,
		SYSTEM_ROTATION_TOGGLE_CHANCE = 0x200,
		DAMAGE_FRAGMENT_CHANCE = 0x1800,
		SPECIAL_FRAGMENT_CHANCE = 0x800,
		S_FOIL_TRANSITION_ACTIVE = 1,
		S_FOIL_TRANSITION_CLOSING = 2,
		B_WING_OBJECT_TYPE = 4,
		X_WING_OBJECT_TYPE = 1,
		B_WING_MAX_ROTATION = 0x40,
		X_WING_UPPER_MAX_ROTATION = 12,
		X_WING_LOWER_MAX_ROTATION = 8,
	};

	uint16_t objectIndex;
#ifdef XVT_MODERN
	XvtFlightClock animationClock;
#endif

	if (g_flightMissionState.provingGroundsModeActive != 0)
		ProvingGrounds_UpdateCourse();
	if (g_flightGlobalCountdownTimers.crewMeshRotationUpdateTimer != 0
#ifdef XVT_MODERN
		|| !XvtFlightTiming_ReferenceDue()
#endif
	)
		return;

#ifdef XVT_MODERN
	XvtFlightTiming_AnimationEvent();
	animationClock = XvtFlightTiming_EnterReference();
#endif
	g_flightGlobalCountdownTimers.crewMeshRotationUpdateTimer = SPECIAL_BEHAVIOR_UPDATE_TICKS;
	for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		 objectIndex < g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount; ++objectIndex) {
		if (g_objectTable[objectIndex].objectType >= FIRST_CREW_OBJECT_TYPE &&
			g_objectTable[objectIndex].objectType <= LAST_CREW_OBJECT_TYPE) {
			g_objectTable[objectIndex].roll +=
				(int16_t)(SPECIAL_BEHAVIOR_UPDATE_TICKS *
						  ((objectIndex - g_regionStaticObjectSlotCount) >> 4) / 16);
			g_objectTable[objectIndex].pitch +=
				(int16_t)(SPECIAL_BEHAVIOR_UPDATE_TICKS *
						  ((objectIndex - g_regionStaticObjectSlotCount) >> 3) / 32);
			g_objectTable[objectIndex].yaw +=
				(int16_t)(SPECIAL_BEHAVIOR_UPDATE_TICKS *
						  (4 - ((objectIndex - g_regionStaticObjectSlotCount) >> 4)) / 16);
		}

		if (g_objectTable[objectIndex].mobj != NULL) {
			uint16_t objectType;
			CraftData* craft;

			objectType = g_objectTable[objectIndex].objectType;
			if (objectType == 0)
				continue;
			g_billboardTextureFrameSequence = g_modelTypeTable[objectType].textureFrameSequence;
			craft = g_objectTable[objectIndex].mobj->pCraft;
			switch (g_objectTable[objectIndex].genusId) {
				case CRAFT_GENUS_STARFIGHTER:
				case CRAFT_GENUS_TRANSPORT:
				case CRAFT_GENUS_UTILITY_VEHICLE:
				case CRAFT_GENUS_FREIGHTER:
				case CRAFT_GENUS_STARSHIP:
				case CRAFT_GENUS_PLATFORM: {
					uint16_t meshCount;
					int16_t sFoilMeshMoved;
					int allowSystemRotation;
					uint16_t meshIndex;

					if (objectType < FIRST_DYNAMIC_MODEL_TYPE)
						meshCount = (uint16_t)g_objectTypeMeshCache[objectType].meshCount;
					else
						meshCount = (uint16_t)ModelMesh_GetObjectTypeMeshCount(objectType);
					sFoilMeshMoved = 0;
					g_curCraft = g_objectTable[objectIndex].mobj->pCraft;
					if (g_curCraft->workingSubsystems != 0) {
						const char* planName;

						planName = g_planTable[g_curCraft->aiController.pendingPlanId].name;
						allowSystemRotation = strcmp(planName, "nullpln") != 0 &&
											  strcmp(planName, "stationaryldrpln") != 0 &&
											  strcmp(planName, "stationaryflwpln") != 0;
					} else {
						allowSystemRotation = 0;
					}

					if (allowSystemRotation != 0) {
						int weaponSlotIndex;

						for (weaponSlotIndex = 0; weaponSlotIndex < g_curCraft->laserSlotCount;
							 ++weaponSlotIndex) {
							unsigned int turretMeshIndex;
							MeshComponentType turretMeshType;
							TurretTargetState* turretTarget;

							if (g_curCraft->weaponSlots[weaponSlotIndex].projectileTypeId !=
								TURRET_PROJECTILE_TYPE)
								continue;
							turretMeshIndex =
								g_modelDefs[g_curCraft->modelIndex].weaponHardpoints[weaponSlotIndex].meshIdx;
							if (g_curCraft->componentHp[turretMeshIndex] == 0)
								continue;
							if (objectType < FIRST_DYNAMIC_MODEL_TYPE)
								turretMeshType =
									ModelMesh_GetCachedObjectTypeMeshType(objectType, turretMeshIndex);
							else
								turretMeshType = ModelMesh_GetObjectTypeMeshType(objectType, turretMeshIndex);
							if (turretMeshType != MESH_COMPONENT_21_LASR_TUR)
								continue;

							turretTarget = &g_curCraft->turretTargetStates[weaponSlotIndex];
							if (turretTarget->targetObjIdx != UINT16_MAX) {
								ObjectRecord* object;
								float* rotationScale;
								int turretSide;
								int turretForward;
								int turretUp;
								int angleY;
								int angleX;

								rotationScale = ModelMesh_GetRotScaleData(objectType, turretMeshIndex);
								object = &g_objectTable[objectIndex];
								Mission_ResolveObjectOrMissionPointWorldLoc(turretTarget->targetObjIdx, 0);
								worldlocx -= object->world_x;
								worldlocy -= object->world_y;
								worldlocz -= object->world_z;
								if (object->mobj->orientMatrixDirty != 0) {
									FVIEW_calcrotatemove(object->pitch, object->yaw, object);
									FVIEW_calcrotateorient(object->roll, 0, object);
								}
								turretSide =
									Math_Dot3Q15(worldlocx, worldlocy, worldlocz, object->mobj->cachedSideX,
												 object->mobj->cachedSideY, object->mobj->cachedSideZ);
								turretForward =
									-Math_Dot3Q15(worldlocx, worldlocy, worldlocz, object->mobj->cachedFwdX,
												  object->mobj->cachedFwdY, object->mobj->cachedFwdZ);
								turretUp =
									Math_Dot3Q15(worldlocx, worldlocy, worldlocz, object->mobj->cachedUpX,
												 object->mobj->cachedUpY, object->mobj->cachedUpZ);
								worldlocx = turretSide - (int)rotationScale[0];
								worldlocy = turretForward - (int)rotationScale[1];
								worldlocz = turretUp - (int)rotationScale[2];
								(void)Math_Dot3Q15(worldlocx, worldlocy, worldlocz, (int)rotationScale[3],
												   (int)rotationScale[4], (int)rotationScale[5]);
								angleX = Math_Dot3Q15(worldlocx, worldlocy, worldlocz, (int)rotationScale[6],
													  (int)rotationScale[7], (int)rotationScale[8]);
								angleY = Math_Dot3Q15(worldlocx, worldlocy, worldlocz, (int)rotationScale[9],
													  (int)rotationScale[10], (int)rotationScale[11]);
								g_curCraft->meshRotation[turretMeshIndex] =
									(uint8_t)((uint16_t)trig2_arctan(angleY, angleX) >> 8);
							} else {
								uint8_t rotation;

								rotation = g_curCraft->meshRotation[turretMeshIndex];
								if ((rotation & 1) != 0)
									rotation += TURRET_IDLE_ROTATION_STEP;
								else
									rotation -= TURRET_IDLE_ROTATION_STEP;
								g_curCraft->meshRotation[turretMeshIndex] = rotation;
								if ((uint16_t)GameRand() < TURRET_DIRECTION_TOGGLE_CHANCE)
									g_curCraft->meshRotation[turretMeshIndex] ^= 1;
							}
						}
					}

					for (meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
						MeshComponentType meshType;

						if (objectType < FIRST_DYNAMIC_MODEL_TYPE)
							meshType = ModelMesh_GetCachedObjectTypeMeshType(objectType, meshIndex);
						else
							meshType = ModelMesh_GetObjectTypeMeshType(objectType, meshIndex);
						if (meshType == MESH_COMPONENT_03_FUSELAGE) {
							uint8_t* animationState;

							animationState = &craft->componentState[meshCount];
							g_billboardTextureFrameSequence = g_fuselageDamageTextureFrameSequence;
							g_billboardTextureSequenceIndex = *animationState;
							FlightObject_AdvanceTextureFrameSequence(objectIndex);
							*animationState = (uint8_t)g_billboardTextureSequenceIndex;
						}
						if (g_curCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP) {
							if (ModelMesh_HasFuselage(objectType) == 0) {
								Craft_SpawnMainHullExplosionEffects(objectIndex, 0);
								meshIndex += 3;
							} else {
								Craft_DetachDamageableComponent(objectIndex, 0);
								if ((uint16_t)GameRand() < DAMAGE_FRAGMENT_CHANCE)
									Object_SpawnEffectFragment(objectIndex);
							}
						}
						if (allowSystemRotation != 0 && (meshType == MESH_COMPONENT_11_COMM_SYS ||
														 meshType == MESH_COMPONENT_23_COMM_SYS ||
														 meshType == MESH_COMPONENT_12_BEAM_SYS ||
														 meshType == MESH_COMPONENT_24_BEAM_SYS ||
														 meshType == MESH_COMPONENT_13_COMM_SYS ||
														 meshType == MESH_COMPONENT_25_COMM_SYS)) {
							uint8_t rotation;

							rotation = g_curCraft->meshRotation[meshIndex];
							if ((rotation & 1) != 0)
								rotation += TURRET_IDLE_ROTATION_STEP;
							else
								rotation -= TURRET_IDLE_ROTATION_STEP;
							g_curCraft->meshRotation[meshIndex] = rotation;
							if ((uint16_t)GameRand() < SYSTEM_ROTATION_TOGGLE_CHANCE)
								g_curCraft->meshRotation[meshIndex] ^= 1;
						}
						if (meshType == MESH_COMPONENT_07_BRIDGE && objectType == B_WING_OBJECT_TYPE &&
							(g_curCraft->sFoilState & S_FOIL_TRANSITION_ACTIVE) != 0) {
							if ((g_curCraft->sFoilState & S_FOIL_TRANSITION_CLOSING) != 0) {
								if (g_curCraft->meshRotation[meshIndex] < B_WING_MAX_ROTATION)
									g_curCraft->meshRotation[meshIndex] += 4;
							} else if (g_curCraft->meshRotation[meshIndex] != 0) {
								g_curCraft->meshRotation[meshIndex] -= 4;
								if (g_curCraft->meshRotation[meshIndex] > 0x80)
									g_curCraft->meshRotation[meshIndex] = 0;
							}
						}
						if (meshType == MESH_COMPONENT_20_WING &&
							(g_curCraft->sFoilState & S_FOIL_TRANSITION_ACTIVE) != 0) {
							if ((g_curCraft->sFoilState & S_FOIL_TRANSITION_CLOSING) != 0) {
								if (objectType == X_WING_OBJECT_TYPE) {
									int centerZ;
									uint16_t maxRotation;

									centerZ = ModelMesh_GetCenterZ(objectType, meshIndex);
									maxRotation = X_WING_LOWER_MAX_ROTATION;
									if (centerZ >= 0)
										maxRotation = X_WING_UPPER_MAX_ROTATION;
									if (g_curCraft->meshRotation[meshIndex] < maxRotation) {
										++g_curCraft->meshRotation[meshIndex];
										sFoilMeshMoved = 1;
									}
								} else if (objectType == B_WING_OBJECT_TYPE &&
										   g_curCraft->meshRotation[meshIndex] < B_WING_MAX_ROTATION) {
									g_curCraft->meshRotation[meshIndex] += 4;
									sFoilMeshMoved = 1;
								}
							} else if (objectType == X_WING_OBJECT_TYPE) {
								if (g_curCraft->meshRotation[meshIndex] != 0) {
									--g_curCraft->meshRotation[meshIndex];
									sFoilMeshMoved = 1;
								}
							} else if (objectType == B_WING_OBJECT_TYPE &&
									   g_curCraft->meshRotation[meshIndex] != 0) {
								g_curCraft->meshRotation[meshIndex] -= 4;
								sFoilMeshMoved = 1;
							}
						}
					}

					if ((g_curCraft->sFoilState & S_FOIL_TRANSITION_ACTIVE) != 0) {
						if ((g_curCraft->sFoilState & S_FOIL_TRANSITION_CLOSING) != 0) {
							if (sFoilMeshMoved == 0) {
								g_curCraft->sFoilState = S_FOIL_TRANSITION_CLOSING;
								msg_emitInFlightMessage(IFMSG_129_S_FOILS_HAVE_REACHED_CLOSED_POSITION,
														g_objectTable[objectIndex].playerOwnerIdx);
							}
						} else if (sFoilMeshMoved == 0) {
							g_curCraft->sFoilState = 0;
							msg_emitInFlightMessage(IFMSG_128_S_FOILS_HAVE_REACHED_OPEN_POSITION,
													g_objectTable[objectIndex].playerOwnerIdx);
						}
					}
					if (g_curCraft->objectKind == CRAFT_OBJECT_KIND_ACTIVE &&
						g_curCraft->cmTypeId == COUNTERMEASURE_TYPE_CHAFF &&
						g_curCraft->chaffActiveTimer != 0) {
						Object_SpawnLocalEffectFragment(objectIndex);
						Object_SpawnLocalEffectFragment(objectIndex);
						Object_SpawnLocalEffectFragment(objectIndex);
					}
					break;
				}
				case CRAFT_GENUS_SMALL_DEBRIS:
				case CRAFT_GENUS_EXPLOSION:
					if (g_objectTable[objectIndex].objectType == SPECIAL_FRAGMENT_OBJECT_TYPE) {
						g_objectTable[objectIndex].typeSpecificByte[1] = 0;
						if ((uint16_t)GameRand() < SPECIAL_FRAGMENT_CHANCE)
							Object_SpawnEffectFragment(objectIndex);
					} else {
						g_billboardTextureSequenceIndex = g_objectTable[objectIndex].typeSpecificByte[0];
						FlightObject_AdvanceTextureFrameSequence(objectIndex);
						g_objectTable[objectIndex].typeSpecificByte[0] =
							(uint8_t)g_billboardTextureSequenceIndex;
					}
					break;
				default:
					break;
			}
		} else {
			uint16_t objectType;

			objectType = g_objectTable[objectIndex].objectType;
			if (objectType != 0) {
				g_billboardTextureFrameSequence = g_modelTypeTable[objectType].textureFrameSequence;
				if (g_billboardTextureFrameSequence != NULL) {
					g_billboardTextureSequenceIndex = g_objectTable[objectIndex].typeSpecificByte[0];
					FlightObject_AdvanceTextureFrameSequence(objectIndex);
					g_objectTable[objectIndex].typeSpecificByte[0] = (uint8_t)g_billboardTextureSequenceIndex;
				}
			}
		}
	}
#ifdef XVT_MODERN
	XvtFlightTiming_RestoreClock(animationClock);
#endif
}

// FUNCTION: XVT 0x4021A0
void FlightObject_AdvanceTextureFrameSequence(unsigned int objectIdx) {
	int16_t sequenceValue;
	MobileObject* mobileObject;

	if (g_billboardTextureFrameSequence == NULL)
		return;

	++g_billboardTextureSequenceIndex;
	sequenceValue = g_billboardTextureFrameSequence[g_billboardTextureSequenceIndex];
	if (sequenceValue == -1) {
		g_objectTable[objectIdx].objectType = 0;
		if ((unsigned int)g_activeRegionCraftObjectSlotEnd > objectIdx) {
			mobileObject = g_objectTable[objectIdx].mobj;
			if (mobileObject->pCraft != NULL)
				Craft_ClearEffectiveAiObjectLink(mobileObject->pCraft);
		}
	} else if (sequenceValue == -3) {
		g_billboardTextureSequenceIndex = 0;
	} else if ((uint16_t)sequenceValue >= 0xFF00u && sequenceValue != -2) {
		g_billboardTextureSequenceIndex = (uint16_t)(sequenceValue + 0x100);
	}
}

// FUNCTION: XVT 0x402240
void FlightObject_UpdatePlayerHyperspaceTransition(int playerIdx) {
	enum {
		HYPERSPACE_PHASE_NONE = 0,
		HYPERSPACE_PHASE_ALIGN = 1,
		HYPERSPACE_PHASE_DEPART = 2,
		HYPERSPACE_PHASE_DURATION = 0x49C,
		HYPERSPACE_ACCELERATION_INTERVAL = 0xEC,
		HYPERSPACE_CLEARANCE_DISTANCE = 0x40000,
		HYPERSPACE_FORWARD_STEP = 224,
		HYPERSPACE_ABORT_SPEED = 10,
		HYPERSPACE_ALIGN_DECELERATION = 200,
		PLAYER_WAVE_MODE_PRESERVE = 1,
		UNLIMITED_WAVES = 99,
		MISSION_SCORE_POINT_SCALE = 40,
		ANGLE_HALF_TURN = 0x8000,
		ANGLE_FORWARD = 0x4000,
		ANGLE_REVERSE = 0xC000,
	};

	int objectIdx;
	ObjectRecord* playerObject;
	CraftData* craft;
	uint16_t tickDelta;
	unsigned int phaseElapsedTicks;
	int hyperspacePhase;

	objectIdx = g_players[playerIdx].objectIndex;
	playerObject = &g_objectTable[objectIdx];
	craft = playerObject->mobj->pCraft;
	if (craft->objectKind != CRAFT_OBJECT_KIND_ACTIVE)
		g_players[playerIdx].hyperspacePhase = HYPERSPACE_PHASE_NONE;
	tickDelta = g_elapsedTicks;
	phaseElapsedTicks = g_players[playerIdx].hyperspaceRuntime.phaseElapsedTicks + tickDelta;
	g_players[playerIdx].hyperspaceRuntime.phaseElapsedTicks = phaseElapsedTicks;
	hyperspacePhase = g_players[playerIdx].hyperspacePhase;

	switch (hyperspacePhase) {
		case HYPERSPACE_PHASE_ALIGN: {
			int16_t yaw;
			int16_t roll;
			int16_t pitch;

			roll = playerObject->roll;
			yaw = playerObject->yaw;
			pitch = playerObject->pitch;
			if (roll == 0 && pitch == ANGLE_FORWARD && yaw == 0) {
				if (phaseElapsedTicks >= HYPERSPACE_PHASE_DURATION) {
					g_players[playerIdx].hyperspacePhase = HYPERSPACE_PHASE_DEPART;
					msg_emitInFlightMessage(IFMSG_108_ENTERING_HYPERSPACE, playerIdx);
					g_players[playerIdx].hyperspaceRuntime.phaseElapsedTicks = 0;
				}
			} else {
				int16_t angleStep;

				angleStep = (int16_t)(16 * g_elapsedTicks);
				if ((uint16_t)roll < ANGLE_HALF_TURN) {
					roll = (int16_t)(roll - 2 * angleStep);
					if ((uint16_t)roll >= ANGLE_HALF_TURN)
						roll = 0;
				} else {
					roll = (int16_t)(roll + 2 * angleStep);
					if ((uint16_t)roll < ANGLE_HALF_TURN)
						roll = 0;
				}
				if ((uint16_t)yaw < ANGLE_HALF_TURN) {
					yaw = (int16_t)(yaw - angleStep);
					if ((uint16_t)yaw >= ANGLE_HALF_TURN)
						yaw = 0;
				} else {
					yaw = (int16_t)(yaw + angleStep);
					if ((uint16_t)yaw < ANGLE_HALF_TURN)
						yaw = 0;
				}
				if ((uint16_t)pitch >= ANGLE_REVERSE || (uint16_t)pitch <= ANGLE_FORWARD) {
					if ((uint16_t)pitch != ANGLE_FORWARD) {
						pitch = (int16_t)(pitch + angleStep);
						if ((uint16_t)pitch > ANGLE_FORWARD && (uint16_t)pitch < ANGLE_REVERSE)
							pitch = ANGLE_FORWARD;
					}
				} else {
					pitch = (int16_t)(pitch - angleStep);
					if ((uint16_t)pitch < ANGLE_FORWARD)
						pitch = ANGLE_FORWARD;
				}
			}
			g_objectTable[objectIdx].roll = roll;
			g_objectTable[objectIdx].yaw = yaw;
			g_objectTable[objectIdx].pitch = pitch;
			g_objectTable[objectIdx].mobj->orientMatrixDirty = 1;
			g_objectTable[objectIdx].mobj->moveVectorDirty = g_objectTable[objectIdx].mobj->orientMatrixDirty;
			g_objectTable[objectIdx].mobj->pCraft->pitch = (uint16_t)pitch;
			Flight_DecelerateHyperspaceSpeed(objectIdx, HYPERSPACE_ALIGN_DECELERATION);
			return;
		}
		case HYPERSPACE_PHASE_DEPART:
			break;
		default:
			return;
	}

	{
		if (phaseElapsedTicks < HYPERSPACE_PHASE_DURATION) {
			int16_t obstructionDetected;
			int candidateObjectType;

			obstructionDetected = 0;
			objectIdx = g_players[playerIdx].objectIndex;
			if (phaseElapsedTicks <= tickDelta) {
				int16_t candidateIdx;

				for (candidateIdx = (int16_t)g_activeRegionObjectSlotStart;
					 candidateIdx < g_activeRegionCraftObjectSlotEnd; ++candidateIdx) {
					ObjectRecord* candidate;
					ObjectRecord* playerCraftObject;
					int deltaX;
					int deltaY;
					int deltaZ;
					int candidateExtent;
					int playerExtent;

					candidate = &g_objectTable[candidateIdx];
					if (candidate->objectType != 0) {
						playerCraftObject = &g_objectTable[objectIdx];
						deltaX = candidate->world_x - playerCraftObject->world_x;
						deltaY = candidate->world_y - playerCraftObject->world_y;
						deltaZ = candidate->world_z - playerCraftObject->world_z;
						if (deltaX < 0)
							deltaX = -deltaX;
						if (deltaZ < 0)
							deltaZ = -deltaZ;
						candidateObjectType = candidate->objectType;
						candidateExtent = g_modelTypeTable[candidateObjectType].maxBoundsExtent;
						deltaX -= candidateExtent;
						deltaZ -= candidateExtent;
						playerExtent = g_modelTypeTable[playerCraftObject->objectType].maxBoundsExtent;
						if (playerExtent > deltaX && playerExtent > deltaZ && deltaY > 0 &&
							deltaY < HYPERSPACE_CLEARANCE_DISTANCE) {
							obstructionDetected = 1;
							break;
						}
					}
				}
				if (obstructionDetected == 0) {
					int staticObjectSlotEnd;

					candidateIdx = (int16_t)g_regionMainObjectSlotEnd;
					staticObjectSlotEnd = g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
					for (; candidateIdx < staticObjectSlotEnd; ++candidateIdx) {
						ObjectRecord* candidate;
						ObjectRecord* playerCraftObject;
						int deltaX;
						int deltaY;
						int deltaZ;
						int candidateExtent;
						int playerExtent;

						candidate = &g_objectTable[candidateIdx];
						if (candidate->objectType != 0) {
							playerCraftObject = &g_objectTable[objectIdx];
							deltaX = candidate->world_x - playerCraftObject->world_x;
							deltaY = candidate->world_y - playerCraftObject->world_y;
							deltaZ = candidate->world_z - playerCraftObject->world_z;
							if (deltaX < 0)
								deltaX = -deltaX;
							if (deltaZ < 0)
								deltaZ = -deltaZ;
							candidateObjectType = candidate->objectType;
							candidateExtent = g_modelTypeTable[candidateObjectType].maxBoundsExtent;
							deltaX -= candidateExtent;
							deltaZ -= candidateExtent;
							playerExtent = g_modelTypeTable[playerCraftObject->objectType].maxBoundsExtent;
							if (playerExtent > deltaX && playerExtent > deltaZ && deltaY > 0 &&
								deltaY < HYPERSPACE_CLEARANCE_DISTANCE) {
								obstructionDetected = 1;
								break;
							}
						}
					}
				}
			}

			if (obstructionDetected != 0) {
				int currentObjectIdx;
				uint8_t objectType;
				int useRebelCraftSound;

				msg_emitInFlightMessage(IFMSG_112_OBJECT_DETECTED_IN_JUMP_PATH_HYPERSPACE_JUMP_ABORTED,
										playerIdx);
				currentObjectIdx = g_players[playerIdx].objectIndex;
				useRebelCraftSound = 0;
				if (currentObjectIdx != -1) {
					objectType = g_objectTable[currentObjectIdx].objectType;
					if (objectType == CRAFT_SPECIES_X_WING || objectType == CRAFT_SPECIES_Y_WING ||
						objectType == CRAFT_SPECIES_A_WING || objectType == CRAFT_SPECIES_Z_95_HEADHUNTER ||
						objectType == CRAFT_SPECIES_B_WING) {
						useRebelCraftSound = 1;
					}
				}
				if (useRebelCraftSound != 0)
					fsfx_PlaySound(FLIGHT_SOUND_R2_WARNING, -1, playerIdx);
				else
					fsfx_PlaySound(FLIGHT_SOUND_GENERAL_WARNING, -1, playerIdx);
				g_players[playerIdx].hyperspacePhase = HYPERSPACE_PHASE_NONE;
				g_objectTable[objectIdx].mobj->speed = HYPERSPACE_ABORT_SPEED;
				return;
			}

			{
				uint16_t accelerationStage;

				accelerationStage = (uint16_t)(phaseElapsedTicks / HYPERSPACE_ACCELERATION_INTERVAL);
				if (accelerationStage == 0)
					Flight_AccelerateHyperspaceSpeed(objectIdx, 10);
				else if (accelerationStage == 1)
					Flight_AccelerateHyperspaceSpeed(objectIdx, 25);
				else if (accelerationStage == 2)
					Flight_AccelerateHyperspaceSpeed(objectIdx, 50);
				else if (accelerationStage == 3)
					Flight_AccelerateHyperspaceSpeed(objectIdx, 100);
				else
					Flight_AccelerateHyperspaceSpeed(objectIdx, 1000);
				g_objectTable[objectIdx].world_y +=
					HYPERSPACE_FORWARD_STEP * g_elapsedTicks * accelerationStage;
			}
			return;
		}

		{
			int flightGroupIdx;

			flightGroupIdx = playerObject->flightGroupIdx;
			Mission_RecordCraftOutcome((uint16_t)objectIdx, (uint16_t)flightGroupIdx,
									   FLIGHT_GROUP_OUTCOME_LEFT_REGION);
			if (g_missionHeader.missionType != MISSION_TYPE_QUICK_START &&
				g_flightMissionState.playerFlightGroupWaveMode == PLAYER_WAVE_MODE_PRESERVE &&
				g_missionFlightGroups[flightGroupIdx].fg.numberOfWaves != UNLIMITED_WAVES) {
				ModelIndex modelIndex;

				modelIndex = GetModelIndexFromType(g_objectTable[objectIdx].objectType);
				g_players[playerIdx].missionStats.missionScore +=
					MISSION_SCORE_POINT_SCALE * g_modelDefs[modelIndex].craftPointValue;
			}
			if (g_players[playerIdx].objectIndex != -1) {
				fsfx_UpdateBeamSystemLoop(0, playerIdx);
				fsfx_UpdateIncomingMissileWarning(0);
				fsfx_StopHyperZoomImp(playerIdx);
			}
			g_objectTable[objectIdx].objectType = 0;
			Player_SaveCraftSettings(playerIdx);
			Craft_ClearEffectiveAiObjectLink(craft);
			g_players[playerIdx].hyperspacePhase = HYPERSPACE_PHASE_NONE;
			Mission_ProcessFlightGroupWaveCompletion(flightGroupIdx);
			if (Player_BindToAvailableCraft(playerIdx, UINT32_MAX, 0, 0) != 0) {
				Player_EndFlightParticipation(playerIdx);
				Player_EmitRemotePlayerDepartedMessages(playerIdx);
			} else if (playerIdx == g_localPlayer) {
				msg_emitLocalPlayerCraftMessage(
					IFMSG_291_PREVIOUS_CRAFT_HYPERSPACED_NOW_PILOTING_ARG_ARG_ARG);
			}
		}
	}
}

// FUNCTION: XVT 0x459850
void FlightObject_UpdateDebrisAndTransientAnimations(void) {
	int objectIndex;
	uint16_t debrisIndex;
	int deltaX;
	int deltaY;
	int deltaZ;
	int16_t randomOffset;
	ObjectRecord* playerObject;
	MobileObject* mobileObject;
	int worldZ;
	int16_t offsetX;
	int16_t offsetY;
	int16_t offsetZ;

	objectIndex = g_players[g_localPlayer].objectIndex;
	if (objectIndex == -1)
		return;
	debrisIndex = g_localDebrisRecycleSlotCursor++;
	if (g_localDebrisRecycleSlotCursor == g_localDebrisSlotEnd)
		g_localDebrisRecycleSlotCursor = g_localTransientSlotStart;
	deltaX = g_objectTable[debrisIndex].world_x - g_objectTable[objectIndex].world_x;
	deltaY = g_objectTable[debrisIndex].world_y - g_objectTable[objectIndex].world_y;
	deltaZ = g_objectTable[debrisIndex].world_z - g_objectTable[objectIndex].world_z;
	if (deltaX < 0)
		deltaX = -deltaX;
	if (deltaY < 0)
		deltaY = -deltaY;
	if (deltaZ < 0)
		deltaZ = -deltaZ;
	if (collide_roughdistance3du((unsigned int)deltaX, (unsigned int)deltaY, (unsigned int)deltaZ) > 0x800) {
#ifdef XVT_MODERN
		XvtFlightIntegration_Reset(debrisIndex);
#endif
		g_objectTable[debrisIndex].objectType = (GameRand2() & 3) + 110;
		g_objectTable[debrisIndex].genusId = 11;
		g_objectTable[debrisIndex].mobj->state = 3;
		g_objectTable[debrisIndex].flightGroupIdx = -1;
		if (g_objectTable[objectIndex].mobj->orientMatrixDirty != 0) {
			FVIEW_calcrotatemove(g_objectTable[objectIndex].pitch, g_objectTable[objectIndex].yaw,
								 &g_objectTable[objectIndex]);
			FVIEW_calcrotateorient(g_objectTable[objectIndex].roll, 0, &g_objectTable[objectIndex]);
		}
		randomOffset = (int16_t)((GameRand2() & 0x3FF) - 512);
		offsetX = (int16_t)Math_MulQ15(randomOffset, g_objectTable[objectIndex].mobj->cachedSideX);
		offsetY = (int16_t)Math_MulQ15(randomOffset, g_objectTable[objectIndex].mobj->cachedSideY);
		offsetZ = (int16_t)Math_MulQ15(randomOffset, g_objectTable[objectIndex].mobj->cachedSideZ);
		randomOffset = (int16_t)((GameRand2() & 0x3FF) - 512);
		playerObject = &g_objectTable[objectIndex];
		offsetX += (int16_t)Math_MulQ15(randomOffset, playerObject->mobj->cachedUpX);
		offsetY += (int16_t)Math_MulQ15(randomOffset, playerObject->mobj->cachedUpY);
		offsetZ += (int16_t)Math_MulQ15(randomOffset, playerObject->mobj->cachedUpZ);
		mobileObject = playerObject->mobj;
		offsetX += mobileObject->cachedFwdX >> 4;
		offsetY += mobileObject->cachedFwdY >> 4;
		worldZ = playerObject->world_z;
		offsetZ += mobileObject->cachedFwdZ >> 4;
		g_objectTable[debrisIndex].world_x = playerObject->world_x + offsetX;
		g_objectTable[debrisIndex].world_y = playerObject->world_y + offsetY;
		g_objectTable[debrisIndex].world_z = worldZ + offsetZ;
		g_objectTable[debrisIndex].typeSpecificByte[0] = 2;
	}
}

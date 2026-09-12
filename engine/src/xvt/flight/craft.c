#include "xvt/flight/craft.h"

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

// GLOBAL: XVT 0x9ECA28
int g_craftDataPoolCapacity = 0;
// GLOBAL: XVT 0xA08104
CraftData* g_curCraft;
// GLOBAL: XVT 0xA07BD0
CraftData* g_craftDataPoolBase = 0;
// GLOBAL: XVT 0x5180E0
const double g_craftTechSpeedAccelerationRatingScale = 0.4444444444444444;
// GLOBAL: XVT 0x5180E8
const double g_craftTechRatingRoundingBias = 0.5;
// GLOBAL: XVT 0x5180F0
const double g_craftTechManeuverRatingScale = 0.005231575698284567;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x405790
void Craft_AdjustCurrentShieldEnergy(unsigned int unusedObjectIdx, uint16_t shieldIndex, int16_t delta) {
	int maxShieldEnergy;

	(void)unusedObjectIdx;
	maxShieldEnergy = 2 * g_modelDefs[g_curCraft->modelIndex].shieldStrength;
	g_curCraft->shieldEnergy[shieldIndex] = g_curCraft->shieldEnergy[shieldIndex] + delta;
	if (g_curCraft->shieldEnergy[shieldIndex] < 0)
		g_curCraft->shieldEnergy[shieldIndex] = 0;
	if (g_curCraft->shieldEnergy[shieldIndex] > maxShieldEnergy)
		g_curCraft->shieldEnergy[shieldIndex] = maxShieldEnergy;
}

// FUNCTION: XVT 0x405820
int Craft_GetObjectMaxShield(uint16_t objIdx) {
	return g_modelDefs[g_objectTable[objIdx].mobj->pCraft->modelIndex].shieldStrength * 2;
}

// FUNCTION: XVT 0x4269E0
ModelIndex GetModelIndexFromType(ObjectTypeId objectType) { return g_modelTypeTable[objectType].modelIndex; }

// FUNCTION: XVT 0x426A00
int BuildCraftTechStats(CraftTechStats* stats) {
	ModelIndex modelIndex;
	int* shieldRating;
	unsigned int hullRating;
	CraftGenus genusId;
	int scaledShieldRating;
	int scaledHullRating;
	int groupIndex;
	uint8_t weaponType;

	stats->genusId = g_modelTypeTable[stats->craftType].genusId;
	modelIndex = GetModelIndexFromType((unsigned int)stats->craftType);
	if (modelIndex == MODEL_INDEX_NONE) {
		return 0;
	}

	stats->speedRating =
		(int)((double)g_modelDefs[modelIndex].maxSpeed * g_craftTechSpeedAccelerationRatingScale +
			  g_craftTechRatingRoundingBias);
	stats->accelerationRating =
		(int)((double)g_modelDefs[modelIndex].accelRate * g_craftTechSpeedAccelerationRatingScale +
			  g_craftTechRatingRoundingBias);
	stats->maneuverRating = (int)((double)((uint16_t)g_modelDefs[modelIndex].pitchRate +
										   (uint16_t)g_modelDefs[modelIndex].rollRate) *
									  g_craftTechManeuverRatingScale +
								  g_craftTechRatingRoundingBias);

	shieldRating = &stats->shieldRating;
	if (g_modelDefs[modelIndex].hasShields != 0) {
		*shieldRating = g_modelDefs[modelIndex].shieldStrength / 50;
	} else {
		*shieldRating = 0;
	}
	hullRating = (unsigned int)g_modelDefs[modelIndex].hullStrength / 105;
	genusId = stats->genusId;
	stats->hullRating = (int)hullRating;
	if (genusId == CRAFT_GENUS_STARSHIP || genusId == CRAFT_GENUS_PLATFORM) {
		scaledShieldRating = 16 * *shieldRating;
		stats->hullRating = (int)(16 * hullRating);
		*shieldRating = scaledShieldRating;
	}
	if (genusId == CRAFT_GENUS_FREIGHTER) {
		scaledHullRating = 4 * stats->hullRating;
		*shieldRating = 4 * *shieldRating;
		stats->hullRating = scaledHullRating;
	}

	stats->laserCount = 0;
	stats->ionCount = 0;
	for (groupIndex = 0; groupIndex < 2; ++groupIndex) {
		weaponType = g_modelDefs[modelIndex].laserGroupWeaponType[groupIndex];
		if (weaponType == 0x89 || weaponType == 0x8B) {
			stats->laserCount += g_modelDefs[modelIndex].laserGroupSlotCount[groupIndex];
		}
		if (g_modelDefs[modelIndex].laserGroupWeaponType[groupIndex] == 0x8D) {
			stats->ionCount += g_modelDefs[modelIndex].laserGroupSlotCount[groupIndex];
		}
	}

	groupIndex = 0;
	stats->warheadRating = 0;
	do {
		if (g_modelDefs[modelIndex].warheadLauncherType[groupIndex] != 0) {
			stats->warheadRating += g_modelDefs[modelIndex].warheadLauncherValue[groupIndex] *
									g_modelDefs[modelIndex].warheadLauncherSlotCount[groupIndex];
		}
		++groupIndex;
	} while (groupIndex < 2);

	switch (stats->craftType) {
		case CRAFT_SPECIES_TIE_ADVANCED:
			stats->laserCount = 4;
			stats->ionCount = 0;
			stats->warheadRating = 8;
			break;
		case CRAFT_SPECIES_T_WING:
			stats->laserCount = 2;
			stats->ionCount = 0;
			stats->warheadRating = 8;
			break;
		case CRAFT_SPECIES_Z_95_HEADHUNTER:
			stats->laserCount = 2;
			stats->ionCount = 0;
			break;
		case CRAFT_SPECIES_R_41_STARCHASER:
			stats->laserCount = 2;
			stats->ionCount = 2;
			break;
		default:
			break;
	}
	return 1;
}

// FUNCTION: XVT 0x458750
void Craft_ClearTurretObjectLinks(CraftData* craft) {
	uint16_t turretIndex;

	for (turretIndex = 0; turretIndex < 16; ++turretIndex) {
		craft->turretObjectLinks[turretIndex] = NULL;
	}
}

// FUNCTION: XVT 0x458780
void Craft_ClearEffectiveAiObjectLink(CraftData* craft) {
	int remaining;
	ObjectRecord** objectLink;

	if (craft->effectiveAiObjectLink != NULL) {
		craft->effectiveAiObjectLink->objectType = 0;
		craft->effectiveAiObjectLink = NULL;
	}

	objectLink = craft->turretObjectLinks;
	remaining = 16;
	do {
		if (*objectLink != NULL) {
			(*objectLink)->objectType = 0;
			*objectLink = NULL;
		}
		++objectLink;
		--remaining;
	} while (remaining != 0);
}

// FUNCTION: XVT 0x458FA0
void Craft_DetachDamageableComponent(uint16_t objectIndex, int16_t detachAll) {
	int objectType;
	uint16_t meshCount;
	unsigned int meshIndex;
	CraftData* craft;
	uint16_t fragmentObjectIndex;
	int16_t rollImpulse;
	int16_t yawOffset;
	int16_t pitchOffset;
	int16_t pitch;

	objectType = g_objectTable[objectIndex].objectType;
	if (objectType < 73) {
		meshCount = (uint16_t)g_objectTypeMeshCache[objectType].meshCount;
	} else {
		meshCount = (uint16_t)ModelMesh_GetObjectTypeMeshCount(g_objectTable[objectIndex].objectType);
	}
	if (meshCount <= 1) {
		return;
	}

	meshIndex = 0;
	craft = g_objectTable[objectIndex].mobj->pCraft;
	if (meshCount == 0) {
		return;
	}

	do {
		if (craft->componentState[meshIndex] == 0 &&
			ModelMesh_IsObjectTypeMeshDamageable(g_objectTable[objectIndex].objectType, meshIndex) != 0) {
			fragmentObjectIndex = Object_SpawnDetachedComponent(objectIndex, (int16_t)meshIndex);
			if (fragmentObjectIndex != UINT16_MAX) {
				rollImpulse = (GameRand() & 0x3FFF) + 0x4000;
				yawOffset = (GameRand() & 0x7FF) + 0x400;
				pitchOffset = (GameRand() & 0xFFF) + 0x400;
				if ((GameRand() & 1) != 0) {
					rollImpulse = -rollImpulse;
					yawOffset = -yawOffset;
				}
				if ((GameRand() & 1) != 0) {
					pitchOffset = -pitchOffset;
				}

				g_objectTable[fragmentObjectIndex].mobj->rollImpulseRate = rollImpulse;
				g_objectTable[fragmentObjectIndex].yaw += yawOffset;
				g_objectTable[fragmentObjectIndex].pitch += pitchOffset;
				pitch = g_objectTable[fragmentObjectIndex].pitch;
				if ((uint16_t)pitch >= 0x8000) {
					g_objectTable[fragmentObjectIndex].pitch = -pitch;
					g_objectTable[fragmentObjectIndex].yaw += (uint16_t)0x8000;
				}
				g_objectTable[fragmentObjectIndex].mobj->orientMatrixDirty = 1;
				g_objectTable[fragmentObjectIndex].mobj->moveVectorDirty =
					g_objectTable[fragmentObjectIndex].mobj->orientMatrixDirty;
				g_objectTable[fragmentObjectIndex].mobj->lifetimeTimer =
					SIMULATION_TICKS_PER_SECOND * ((GameRand() & 1) + 1);
				g_objectTable[fragmentObjectIndex].typeSpecificByte[1] = 2;
				craft->componentState[meshIndex] = 4;
				craft->componentState[meshCount] = 2;
				if (detachAll == 0) {
					break;
				}
			}
		}
		++meshIndex;
	} while (meshCount > meshIndex);
}

// FUNCTION: XVT 0x484D80
WarheadKindIndex ObjectType_GetWarheadKindIndex(uint16_t objectType) {
	WarheadKindIndex result;

#ifdef XVT_MODERN
	result = -1;
#endif
	switch (objectType) {
		case WARHEAD_OBJECT_TYPE_PROTON_TORPEDO:
			result = WARHEAD_KIND_PROTON_TORPEDO;
			break;
		case WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE:
			result = WARHEAD_KIND_CONCUSSION_MISSILE;
			break;
		case WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO:
			result = WARHEAD_KIND_ADVANCED_PROTON_TORPEDO;
			break;
		case WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE:
			result = WARHEAD_KIND_ADVANCED_CONCUSSION_MISSILE;
			break;
		case WARHEAD_OBJECT_TYPE_SPACE_BOMB:
			result = WARHEAD_KIND_SPACE_BOMB;
			break;
		case WARHEAD_OBJECT_TYPE_HEAVY_ROCKET:
			result = WARHEAD_KIND_HEAVY_ROCKET;
			break;
		case WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE:
			result = WARHEAD_KIND_MAGNETIC_PULSE;
			break;
		default:
			break;
	}
	return result;
}

// FUNCTION: XVT 0x484E10
int Craft_IsSelectableDamageComponentMesh(int objectType, int meshIndex) {
	int adjustedMeshIndex;
	MeshComponentType meshType;
	int meshCount;
	int candidateMeshIndex;
	int objectTypeMeshCount;
	int adjustedCandidateIndex;
	MeshComponentType candidateMeshType;
	int candidateMeshCount;
	int targetId;

	adjustedMeshIndex = meshIndex;
	if (objectType < 73) {
		if (meshIndex < 0) {
			meshType = MESH_COMPONENT_00_HULL;
		} else {
			meshCount = g_objectTypeMeshCache[objectType].meshCount;
			if (meshCount <= meshIndex) {
				adjustedMeshIndex = meshCount - 1;
			}
			meshType = g_objectTypeMeshCache[objectType].meshTypes[adjustedMeshIndex];
		}
	} else {
		meshType = ModelMesh_GetObjectTypeMeshType(objectType, meshIndex);
	}
	if (meshType == MESH_COMPONENT_18_HULL || meshType == MESH_COMPONENT_19_ANTENNA) {
		return 0;
	}
	targetId = ModelMesh_GetTargetId(objectType, meshIndex);
	if (targetId == 0) {
		return 1;
	}
	if (targetId == 1 && meshType != MESH_COMPONENT_01_HULL && meshType != MESH_COMPONENT_03_FUSELAGE) {
		return 1;
	}
	if (objectType < 73) {
		objectTypeMeshCount = g_objectTypeMeshCache[objectType].meshCount;
	} else {
		objectTypeMeshCount = ModelMesh_GetObjectTypeMeshCount(objectType);
	}
	candidateMeshIndex = 0;
	while (objectTypeMeshCount > candidateMeshIndex) {
		if (ModelMesh_GetTargetId(objectType, candidateMeshIndex) == targetId) {
			adjustedCandidateIndex = candidateMeshIndex;
			if (objectType < 73) {
				if (candidateMeshIndex < 0) {
					candidateMeshType = MESH_COMPONENT_00_HULL;
				} else {
					candidateMeshCount = g_objectTypeMeshCache[objectType].meshCount;
					if (candidateMeshCount <= candidateMeshIndex) {
						adjustedCandidateIndex = candidateMeshCount - 1;
					}
					candidateMeshType = g_objectTypeMeshCache[objectType].meshTypes[adjustedCandidateIndex];
				}
			} else {
				candidateMeshType = ModelMesh_GetObjectTypeMeshType(objectType, candidateMeshIndex);
			}
			if (candidateMeshType == meshType) {
				return candidateMeshIndex == meshIndex;
			}
		}
		++candidateMeshIndex;
	}

	return 0;
}

// FUNCTION: XVT 0x4A6990
int Craft_DamageComponent(uint16_t victimObjIdx, int16_t hitMeshIndex, unsigned int damageAmount,
						  uint16_t sourceObjIdx) {
	enum { FIRST_OPT_OBJECT_TYPE = 73, SPECIAL_OBJECT_TYPE = 54, MAX_PLAYERS = 8 };

	unsigned int relatedObjIdx;
	int playerIndex;
	int meshCount;
	int meshType;
	unsigned int componentDamage;
	int meshIndex;
	--hitMeshIndex;
	meshIndex = (uint16_t)hitMeshIndex;

	if (g_curCraft->componentHp[meshIndex] == 0)
		return damageAmount;
	if (g_curCraft->componentHp[meshIndex] == UINT8_MAX) {
		int adjustedIndex;
		int objectType;
		if (g_objectTable[victimObjIdx].objectType != SPECIAL_OBJECT_TYPE)
			return damageAmount;
		adjustedIndex = meshIndex;
		objectType = g_objectTable[victimObjIdx].objectType;
		if (objectType < FIRST_OPT_OBJECT_TYPE) {
			if (adjustedIndex < 0)
				meshType = MESH_COMPONENT_00_HULL;
			else {
				meshCount = g_objectTypeMeshCache[objectType].meshCount;
				if (meshCount <= meshIndex)
					adjustedIndex = meshCount - 1;
				meshType = g_objectTypeMeshCache[objectType].meshTypes[adjustedIndex];
			}
		} else {
			meshType = ModelMesh_GetObjectTypeMeshType(objectType, meshIndex);
		}
		if (meshType != MESH_COMPONENT_07_BRIDGE)
			return damageAmount;
	}
	if (damageAmount == 0)
		damageAmount = 1;

	if (g_missionFileVersion == 14 && g_objectTable[victimObjIdx].objectType == SPECIAL_OBJECT_TYPE) {
		int adjustedIndex = meshIndex;
		int objectType = g_objectTable[victimObjIdx].objectType;
		if (objectType < FIRST_OPT_OBJECT_TYPE) {
			if (adjustedIndex < 0)
				meshType = MESH_COMPONENT_00_HULL;
			else {
				meshCount = g_objectTypeMeshCache[objectType].meshCount;
				if (meshCount <= meshIndex)
					adjustedIndex = meshCount - 1;
				meshType = g_objectTypeMeshCache[objectType].meshTypes[adjustedIndex];
			}
		} else {
			meshType = ModelMesh_GetObjectTypeMeshType(objectType, meshIndex);
		}
		if (meshType == MESH_COMPONENT_07_BRIDGE) {
			int shieldGeneratorCount;
			int i;
			objectType = g_objectTable[victimObjIdx].objectType;
			if (objectType < FIRST_OPT_OBJECT_TYPE)
				meshCount = g_objectTypeMeshCache[objectType].meshCount;
			else
				meshCount = ModelMesh_GetObjectTypeMeshCount(objectType);
			shieldGeneratorCount = 0;
			for (i = 0; meshCount > i; ++i) {
				int candidateIndex = i;
				objectType = g_objectTable[victimObjIdx].objectType;
				if (objectType < FIRST_OPT_OBJECT_TYPE) {
					if (candidateIndex < 0)
						meshType = MESH_COMPONENT_00_HULL;
					else {
						int candidateMeshCount = g_objectTypeMeshCache[objectType].meshCount;
						if (candidateMeshCount <= candidateIndex)
							candidateIndex = candidateMeshCount - 1;
						meshType = g_objectTypeMeshCache[objectType].meshTypes[candidateIndex];
					}
				} else {
					meshType = ModelMesh_GetObjectTypeMeshType(objectType, i);
				}
				if (meshType == MESH_COMPONENT_08_SHLD_GEN && g_curCraft->componentHp[i] != 0)
					++shieldGeneratorCount;
			}
			if (shieldGeneratorCount != 0 || g_curCraft->shieldEnergy[0] != 0 ||
				g_curCraft->shieldEnergy[1] != 0)
				return damageAmount;
			if (g_objectTable[sourceObjIdx].objectType == 40) {
				unsigned int hullMax = g_curCraft->hullMax;
				unsigned int residual =
					16 * g_curCraft->componentHp[meshIndex] - 5 * (hullMax / 100) - g_curCraft->hullDamage;
				damageAmount = hullMax + residual;
			} else {
				if (g_objectTable[sourceObjIdx].objectType != 3 ||
					g_objectTable[sourceObjIdx].mobj->pCraft->objectKind != CRAFT_OBJECT_KIND_BREAKING_UP)
					return damageAmount;
				damageAmount =
					g_curCraft->hullMax + 16 * g_curCraft->componentHp[meshIndex] - g_curCraft->hullDamage;
			}
		}
	}

	componentDamage = 16 * g_curCraft->componentHp[meshIndex];
	if (damageAmount >= componentDamage) {
		g_curCraft->componentHp[meshIndex] = 0;
		damageAmount -= componentDamage;

		if (g_missionFileVersion == 14 && g_flightMissionState.difficulty == 0) {
			int adjustedIndex = meshIndex;
			int objectType = g_objectTable[victimObjIdx].objectType;
			if (objectType < FIRST_OPT_OBJECT_TYPE) {
				if (adjustedIndex < 0)
					meshType = MESH_COMPONENT_00_HULL;
				else {
					if (g_objectTypeMeshCache[objectType].meshCount <= meshIndex)
						adjustedIndex = g_objectTypeMeshCache[objectType].meshCount - 1;
					meshType = g_objectTypeMeshCache[objectType].meshTypes[adjustedIndex];
				}
			} else {
				meshType = ModelMesh_GetObjectTypeMeshType(objectType, meshIndex);
			}
			if (meshType == MESH_COMPONENT_08_SHLD_GEN) {
				int activeGenerators = 0;
				int i;
				objectType = g_objectTable[victimObjIdx].objectType;
				if (objectType < FIRST_OPT_OBJECT_TYPE)
					meshCount = g_objectTypeMeshCache[objectType].meshCount;
				else
					meshCount = ModelMesh_GetObjectTypeMeshCount(objectType);
				for (i = 0; meshCount > i; ++i) {
					int candidateIndex;
					if (meshIndex == i)
						continue;
					candidateIndex = i;
					objectType = g_objectTable[victimObjIdx].objectType;
					if (objectType < FIRST_OPT_OBJECT_TYPE) {
						if (candidateIndex < 0)
							meshType = MESH_COMPONENT_00_HULL;
						else {
							int candidateMeshCount = g_objectTypeMeshCache[objectType].meshCount;
							if (candidateMeshCount <= candidateIndex)
								candidateIndex = candidateMeshCount - 1;
							meshType = g_objectTypeMeshCache[objectType].meshTypes[candidateIndex];
						}
					} else {
						meshType = ModelMesh_GetObjectTypeMeshType(objectType, i);
					}
					if (meshType == MESH_COMPONENT_08_SHLD_GEN && g_curCraft->componentHp[i] != 0)
						++activeGenerators;
				}
				if (activeGenerators == 0) {
					g_curCraft->shieldEnergy[1] = 0;
					g_curCraft->shieldEnergy[0] = g_curCraft->shieldEnergy[1];
				}
			}
		}

		relatedObjIdx = victimObjIdx;
		if (ModelMesh_IsObjectTypeMeshDamageable(g_objectTable[relatedObjIdx].objectType, meshIndex) != 0) {
			g_curCraft->componentState[meshIndex] = 2;
			for (playerIndex = 0; playerIndex < MAX_PLAYERS; ++playerIndex) {
				if (g_players[playerIndex].connectedFlag != 0 &&
					victimObjIdx == (uint16_t)g_players[playerIndex].currentTargetObjectIdx &&
					g_players[playerIndex].selectedTargetComponent == hitMeshIndex) {
					int objectType = g_objectTable[relatedObjIdx].objectType;
					int playerMeshCount;
					if (objectType < FIRST_OPT_OBJECT_TYPE)
						playerMeshCount = g_objectTypeMeshCache[objectType].meshCount;
					else
						playerMeshCount = ModelMesh_GetObjectTypeMeshCount(objectType);
					do {
						uint16_t nextComponent =
							(uint16_t)(g_players[playerIndex].selectedTargetComponent + 1);
						g_players[playerIndex].selectedTargetComponent = nextComponent;
						if (nextComponent >= playerMeshCount)
							g_players[playerIndex].selectedTargetComponent = 0;
						if (g_curCraft->componentState[(uint16_t)g_players[playerIndex]
														   .selectedTargetComponent] == 0 &&
							Craft_IsSelectableDamageComponentMesh(
								g_objectTable[relatedObjIdx].objectType,
								(uint16_t)g_players[playerIndex].selectedTargetComponent) != 0)
							break;
					} while (g_players[playerIndex].selectedTargetComponent != hitMeshIndex);
				}
			}

			if (g_flightMissionState.provingGroundsModeActive != 0) {
				unsigned int score;
				uint8_t seconds;
				++g_flightMissionState.provingGroundsTargetsDestroyed;
				score = g_flightMissionState.provingGroundsScore + 50;
				g_flightMissionState.provingGroundsScore = score;
				if (g_curCraft->meshRotation[meshIndex] != 0)
					g_flightMissionState.provingGroundsScore = score + 50;
				seconds = g_missionCountdownClock.seconds + 2;
				g_missionCountdownClock.seconds = seconds;
				if (seconds >= 60) {
					g_missionCountdownClock.seconds = seconds - 60;
					++g_missionCountdownClock.minutes;
				}
			}

			{
				uint16_t debrisIndex = Object_AllocSlotForGenus(CRAFT_GENUS_EXPLOSION);
				if (debrisIndex != UINT16_MAX) {
					int centerX;
					int centerY;
					int centerZ;
					int effectSize;
					g_objectTable[debrisIndex].world_x = g_objectTable[relatedObjIdx].world_x;
					g_objectTable[debrisIndex].world_y = g_objectTable[relatedObjIdx].world_y;
					g_objectTable[debrisIndex].world_z = g_objectTable[relatedObjIdx].world_z;
					centerX = ModelMesh_GetCenterX(g_objectTable[relatedObjIdx].objectType, meshIndex);
					centerY = ModelMesh_GetCenterY(g_objectTable[relatedObjIdx].objectType, meshIndex);
					centerZ = ModelMesh_GetCenterZ(g_objectTable[relatedObjIdx].objectType, meshIndex);
					if (g_flightMissionState.provingGroundsModeActive != 0) {
						int16_t meshRotation = g_curCraft->meshRotation[meshIndex];
						if (meshRotation != 0) {
							int16_t angle = -(int16_t)(meshRotation << 8);
							int16_t sine = trig2_getsignedsin(angle);
							int cosine = trig2_getsignedcos(angle);
							uint16_t rotatedX =
								(uint16_t)Math_Dot2Q15Wrapped(cosine, -sine, centerX, centerZ);
							centerZ = Math_Dot2Q15Wrapped(sine, cosine, centerX, centerZ);
							centerX = rotatedX;
						}
					}
					g_rotatedX = centerX;
					g_rotatedY = centerZ;
					g_rotatedZ = -centerY;
					pai_RotateLocalVectorToWorldScratch(&g_objectTable[relatedObjIdx], centerX, centerZ,
														-centerY);
					g_objectTable[debrisIndex].world_x += g_rotatedX;
					g_objectTable[debrisIndex].world_y += g_rotatedY;
					g_objectTable[debrisIndex].world_z += g_rotatedZ;
					g_objectTable[debrisIndex].objectType = -127;
					g_objectTable[debrisIndex].genusId = CRAFT_GENUS_EXPLOSION;
					g_objectTable[debrisIndex].mobj->state = 5;
					g_objectTable[debrisIndex].typeSpecificByte[0] = 2;
					g_objectTable[debrisIndex].mobj->framesAlive = 0;
					g_objectTable[debrisIndex].mobj->lifetimeTimer = 0;
					g_objectTable[debrisIndex].mobj->speed = g_objectTable[relatedObjIdx].mobj->speed;
					g_objectTable[debrisIndex].pitch = g_objectTable[relatedObjIdx].pitch;
					g_objectTable[debrisIndex].yaw = g_objectTable[relatedObjIdx].yaw;
					g_objectTable[debrisIndex].roll = 0;
					g_objectTable[debrisIndex].mobj->orientMatrixDirty = 1;
					g_objectTable[debrisIndex].mobj->moveVectorDirty =
						g_objectTable[debrisIndex].mobj->orientMatrixDirty;
					fsfx_PlaySound((GameRand() & 3) + FLIGHT_SOUND_SMALL_EXPLOSION_FIRST, debrisIndex,
								   g_localPlayer);
					effectSize =
						ModelMesh_GetComponentMaxExtent(g_objectTable[relatedObjIdx].objectType, meshIndex) >>
						9;
					if (effectSize > UINT8_MAX)
						effectSize = UINT8_MAX;
					g_objectTable[debrisIndex].mobj->lightIntensityScale = (uint8_t)effectSize;
				}
			}

			if (g_players[g_localPlayer].objectIndex == sourceObjIdx) {
				int adjustedIndex = meshIndex;
				int objectType = g_objectTable[relatedObjIdx].objectType;
				if (objectType < FIRST_OPT_OBJECT_TYPE) {
					if (adjustedIndex < 0)
						meshType = MESH_COMPONENT_00_HULL;
					else {
						meshCount = g_objectTypeMeshCache[objectType].meshCount;
						if (meshCount <= meshIndex)
							adjustedIndex = meshCount - 1;
						meshType = g_objectTypeMeshCache[objectType].meshTypes[adjustedIndex];
					}
				} else {
					meshType = ModelMesh_GetObjectTypeMeshType(objectType, meshIndex);
				}
				switch (meshType) {
					case MESH_COMPONENT_04_LASR_TUR:
					case MESH_COMPONENT_05_LASR_GUN:
					case MESH_COMPONENT_21_LASR_TUR:
						fsfx_speakorderack(g_localPlayer, -1, 23, 2, victimObjIdx, 0x4000);
						break;
					case MESH_COMPONENT_08_SHLD_GEN:
						fsfx_speakorderack(g_localPlayer, -1, 23, 3, victimObjIdx, UINT16_MAX);
						break;
					case MESH_COMPONENT_10_WHEAD_LN:
					case MESH_COMPONENT_22_WHEAD_LN:
						fsfx_speakorderack(g_localPlayer, -1, 23, 1, victimObjIdx, UINT16_MAX);
						break;
					case MESH_COMPONENT_11_COMM_SYS:
					case MESH_COMPONENT_12_BEAM_SYS:
					case MESH_COMPONENT_23_COMM_SYS:
					case MESH_COMPONENT_24_BEAM_SYS:
						fsfx_speakorderack(g_localPlayer, -1, 23, 4, victimObjIdx, UINT16_MAX);
						break;
					default:
						break;
				}
			}
		}
	} else {
		int newHp = (int)(16 * g_curCraft->componentHp[meshIndex] - damageAmount) >> 4;
		if (newHp == 0)
			newHp = 1;
		damageAmount = 0;
		g_curCraft->componentHp[meshIndex] = (uint8_t)newHp;
	}
	return damageAmount;
}

// FUNCTION: XVT 0x4A7480
void Craft_SpawnMainHullExplosionEffects(uint16_t objectIdx, int16_t forceMainExplosion) {
	/* Spawn main-hull explosion effects for eligible mesh components. */
	enum { FIRST_OPT_OBJECT_TYPE = 73, MAX_HULL_MESHES = 16, MAIN_EXPLOSION_SLOT_COUNT = 1 };

	uint8_t hullMeshes[MAX_HULL_MESHES];
	ObjectRecord* object;
	int meshCount;
	uint16_t objectType;
	uint16_t meshIndex;
	uint16_t hullCount;

	if ((uint16_t)GameRand() >= 0x1FFF && forceMainExplosion == 0)
		return;

	object = &g_objectTable[objectIdx];
	g_curCraft = object->mobj->pCraft;
	objectType = object->objectType;
	if (object->mobj->orientMatrixDirty != 0)
		FVIEW_SetObjectTransform(object->roll, object->pitch, object->yaw, 0, object);
	if (objectType < FIRST_OPT_OBJECT_TYPE)
		meshCount = g_objectTypeMeshCache[objectType].meshCount;
	else
		meshCount = ModelMesh_GetObjectTypeMeshCount(objectType);

	hullCount = 0;
	for (meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
		int lookupIndex = meshIndex;
		int meshType;
		if (objectType < FIRST_OPT_OBJECT_TYPE) {
			if (lookupIndex < 0)
				meshType = MESH_COMPONENT_00_HULL;
			else {
				int cachedMeshCount = g_objectTypeMeshCache[objectType].meshCount;
				if (lookupIndex >= cachedMeshCount)
					lookupIndex = cachedMeshCount - 1;
				meshType = g_objectTypeMeshCache[objectType].meshTypes[lookupIndex];
			}
		} else {
			meshType = ModelMesh_GetObjectTypeMeshType(objectType, lookupIndex);
		}
		if (meshType == MESH_COMPONENT_01_HULL)
			hullMeshes[hullCount++] = (uint8_t)meshIndex;
		if (hullCount == MAX_HULL_MESHES)
			break;
	}
	if (hullCount == 0) {
		hullMeshes[0] = 0;
		hullCount = 1;
	}

	if (forceMainExplosion != 0) {
		uint16_t explosionSlot;
		uint16_t clearedSlots;
		uint16_t mainMesh;
		int mainExtent;
		explosionSlot = g_objectSlotRangeByGenus[CRAFT_GENUS_EXPLOSION].start;
		for (clearedSlots = 0; clearedSlots < MAIN_EXPLOSION_SLOT_COUNT; ++clearedSlots)
			g_objectTable[explosionSlot++].objectType = 0;
		mainMesh = hullMeshes[0];
		mainExtent = g_modelTypeTable[objectType].maxBoundsExtent;
		Craft_SpawnExplosionObjectAtMesh(object, mainMesh, mainExtent, 0);
		fsfx_PlaySound(FLIGHT_SOUND_LARGE_EXPLOSION, objectIdx, g_localPlayer);
	} else {
		uint16_t randomValue = GameRand();
		uint8_t selectedMesh = hullMeshes[randomValue % hullCount];
		uint8_t componentHp = g_curCraft->componentHp[selectedMesh];
		if (componentHp != 0) {
			int explosionSize = g_modelTypeTable[objectType].maxBoundsExtent >> 4;
			uint16_t explosionIdx = Craft_SpawnExplosionObjectAtMesh(object, selectedMesh, explosionSize, 1);
			if (explosionIdx != UINT16_MAX)
				fsfx_PlaySound((GameRand2() & 3) + FLIGHT_SOUND_SMALL_EXPLOSION_FIRST, explosionIdx,
							   g_localPlayer);
		}
	}
}

// FUNCTION: XVT 0x4A76D0
int Craft_SpawnExplosionObjectAtMesh(ObjectRecord* objRecord, uint16_t meshIndex, int effectSize,
									 uint16_t useRandomVertex) {
	/* Place an explosion object at a mesh center or random vertex. */
	uint16_t objectType;
	int localX;
	int localY;
	int localZ;

	objectType = objRecord->objectType;

	if (useRandomVertex == 0) {
		localX = ModelMesh_GetCenterX(objectType, meshIndex);
		localY = ModelMesh_GetCenterY(objectType, meshIndex);
		localZ = ModelMesh_GetCenterZ(objectType, meshIndex);
	} else {
		useRandomVertex = (uint16_t)GameRand() % ModelMesh_GetVertexCount(objectType, meshIndex);
		localX = ModelMesh_GetVertexX(objectType, meshIndex, useRandomVertex);
		localY = ModelMesh_GetVertexY(objectType, meshIndex, useRandomVertex);
		localZ = ModelMesh_GetVertexZ(objectType, meshIndex, useRandomVertex);
	}

	pai_RotateLocalVectorToWorldScratch(objRecord, localX, localZ, -localY);
	{
		uint16_t objectIdx = Object_AllocSlotForGenus(13);
		if (objectIdx == UINT16_MAX)
			return objectIdx;
		g_objectTable[objectIdx].world_x = objRecord->world_x + g_rotatedX;
		g_objectTable[objectIdx].world_y = objRecord->world_y + g_rotatedY;
		g_objectTable[objectIdx].world_z = objRecord->world_z + g_rotatedZ;
		g_objectTable[objectIdx].objectType = useRandomVertex == 0 ? -127 : (uint8_t)((GameRand() & 1) + 127);
		g_objectTable[objectIdx].genusId = 13;
		g_objectTable[objectIdx].mobj->state = 5;
		g_objectTable[objectIdx].typeSpecificByte[0] = 2;
		g_objectTable[objectIdx].mobj->framesAlive = 0;
		g_objectTable[objectIdx].mobj->lifetimeTimer = 0;
		g_objectTable[objectIdx].mobj->lightIntensityScale = effectSize >> 6;
		g_objectTable[objectIdx].mobj->speed = 0;
		g_objectTable[objectIdx].pitch = 0;
		g_objectTable[objectIdx].yaw = 0;
		g_objectTable[objectIdx].roll = 0;
		g_objectTable[objectIdx].mobj->orientMatrixDirty = 1;
		g_objectTable[objectIdx].mobj->moveVectorDirty = g_objectTable[objectIdx].mobj->orientMatrixDirty;
		return objectIdx;
	}
}

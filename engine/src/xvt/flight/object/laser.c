#include "xvt/flight/object/laser.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/player_timing.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"
#endif

#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/model_mesh_internal.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/targeting.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/renderer.h"
#include "xvt/util/game_rand.h"
#include <limits.h>

// GLOBAL: XVT 0x51A3B8
const struct ProjectileTypeDataTables g_projectileDamageByObjectType = {
	{
		250,  500,   200,   400,  200,  400,  10000, 3000, 1000, 800, 800, 15000,
		6000, 65000, 35000, 3000, 6000, 9000, 500,   2000, 0,    0,   0,   0,
	},
	{
		2000, 2000, 1800, 1800, 1400, 1600, 250, 500, 1000, 900, 400, 300,
		600,  25,   175,  600,  350,  400,  500, 225, 0,    0,   0,   0,
	},
	{ 1, 1, 1, 1, 1, 2, 40, 25, 3, 3, 5, 45, 25, 120, 90, 25, 40, 35, 5, 10, 0, 0, 0, 0 },
	{ 0, 0x8000, 0, 0x8000, 0x8000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{
		921, 921, 921, 921, 921, 921, 512, 512, 921, 921, 921, 512,
		512, 48,  512, 512, 512, 512, 256, 256, 0,   0,   0,   0,
	},
	{ 0, 0, 0, 0, 0, 0, 2, 1, 0, 0, 0, 2, 1, 2, 2, 1, 1, 2, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 15, 10, 0, 0, 0, 20, 25, 40, 30, 10, 25, 25, 10, 10, 0, 0, 0, 0 },
};
// GLOBAL: XVT 0x5241F8
const uint8_t g_warheadTypeIds[11] = { 0x00, 0x96, 0x97, 0x90, 0x8F, 0x95, 0x94, 0x98, 0x99, 0x9A, 0x90 };
// GLOBAL: XVT 0x524208
const uint16_t g_warheadAmmoCounts[12] = {
	0x0000, 0x4000, 0x8000, 0xFFFF, 0xC000, 0xFFFF, 0xC000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
};
// GLOBAL: XVT 0x524220
const uint8_t g_meshTypeComponentMaxHp[32] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x04, 0xFF, 0xFF, 0x40, 0xFF, 0x20, 0x30, 0x30, 0x30, 0x70, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x20, 0x30, 0x30, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
// GLOBAL: XVT 0x524240
const uint8_t g_platformBeamDisabledComponentIds[60] = {
	0x16, 0x17, 0x15, 0x14, 0x13, 0x05, 0x0F, 0x10, 0x11, 0x12, 0x18, 0x06, 0x03, 0x05, 0x1B,
	0x09, 0x0A, 0xFF, 0x01, 0x04, 0x1A, 0x07, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x05, 0x0D, 0x0F, 0x13, 0x14, 0x18, 0x0B, 0x0E, 0x06,
	0x11, 0x12, 0x17, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0xFF, 0x15, 0x16, 0x17, 0x18, 0x1E, 0xFF,
};

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x404710
void laser_weaponsfire(void) {
	enum {
		BEAM_EFFECT_COUNT = 5,
		PLAYER_SHIELD_RECHARGE_RATE = 20,
		LARGE_CRAFT_SHIELD_RECHARGE_RATE = 5,
		MISSILE_LOCK_FIGHTER_RANGE = 101805,
		MISSILE_LOCK_LARGE_CRAFT_RANGE = 244332,
		MISSILE_BOAT_LOCK_TICKS = 354,
		DEFAULT_LOCK_TICKS = 708,
		BEAM_FIRE_COOLDOWN_TICKS = 59,
		BEAM_DRAIN_AMOUNT = 125,
		BEAM_TARGET_RANGE = 0x20000,
		BEAM_END_MESSAGE_BASE = 242,
		SYSTEM_MESSAGE_PANE_STATE = 256,
		AI_SHIELD_TRANSFER_LOW = 250,
		AI_SHIELD_TRANSFER_MEDIUM = 500,
		AI_SHIELD_TRANSFER_HIGH = 10000,
		AI_SHIELD_TRANSFER_PULSE = 100,
		MISSILE_BOAT_SHIELD_TRANSFER = 32,
		DEFAULT_SHIELD_TRANSFER = 4,
		LASER_CHARGE_LOW_THRESHOLD = 32,
		LASER_CHARGE_HIGH_THRESHOLD = 96,
		MAXIMUM_LASER_CHARGE = 127,
		BEAM_RECHARGE_STEP = 125,
		MAXIMUM_BEAM_CHARGE = 9999,
		ENGINE_OVERDRIVE_ACTIVE = 0,
		ENGINE_OVERDRIVE_DISENGAGED = -1,
		TURRET_PROJECTILE_TYPE = 2,
	};

	char doPeriodicPowerUpdate;
	uint16_t objectIdx;

	doPeriodicPowerUpdate = 0;
	if (g_flightGlobalCountdownTimers.weaponPowerUpdateTimer == 0
#ifdef XVT_MODERN
		&& XvtFlightTiming_ReferenceDue()
#endif
	) {
		doPeriodicPowerUpdate = 1;
		g_flightGlobalCountdownTimers.weaponPowerUpdateTimer = SIMULATION_TICKS_PER_SECOND;
	}

	{
		uint16_t clearObjIdx;

		for (clearObjIdx = g_activeRegionObjectSlotStart; clearObjIdx < g_activeRegionCraftObjectSlotEnd;
			 ++clearObjIdx) {
			CraftData* craft;
			uint16_t effectIndex;

			if (g_objectTable[clearObjIdx].objectType == CRAFT_SPECIES_UNKNOWN ||
				g_objectTable[clearObjIdx].mobj->state != 0) {
				continue;
			}
			craft = g_objectTable[clearObjIdx].mobj->pCraft;
			for (effectIndex = 0; effectIndex < BEAM_EFFECT_COUNT; ++effectIndex) {
				craft->beamEffectAccum[effectIndex] = 0;
			}
		}
	}

	for (objectIdx = g_activeRegionObjectSlotStart; objectIdx < g_activeRegionCraftObjectSlotEnd;
		 ++objectIdx) {
		int16_t shieldRechargeRate;

		if (g_objectTable[objectIdx].objectType == CRAFT_SPECIES_UNKNOWN ||
			g_objectTable[objectIdx].mobj->state != 0) {
			continue;
		}

		if (g_objectTable[objectIdx].playerOwnerIdx != -1) {
			int playerIdx;

			g_curCraft = g_objectTable[objectIdx].mobj->pCraft;
			playerIdx = g_objectTable[objectIdx].playerOwnerIdx;
			if (g_players[playerIdx].selectedWeaponMode != 0) {
				int firstWarheadSlot;
				uint16_t targetObjIdx;
				int16_t warheadCount;

				firstWarheadSlot = g_modelDefs[GetModelIndexFromType(g_objectTable[objectIdx].objectType)]
									   .warheadLauncherFirstSlot[g_players[playerIdx].selectedWarhead];
				targetObjIdx = (uint16_t)g_players[playerIdx].currentTargetObjectIdx;
				warheadCount = g_curCraft->weaponSlots[firstWarheadSlot].count +
							   g_curCraft->weaponSlots[firstWarheadSlot + 1].count;
				if (targetObjIdx == UINT16_MAX || warheadCount == 0) {
					g_players[playerIdx].missileLockState = 0;
					g_curCraft->warheadLockTicks = 0;
#ifdef XVT_MODERN
					XvtPlayerTiming_LockHalf(playerIdx, 0);
#endif
				} else {
					unsigned int lockRange;
					uint8_t targetGenus;

					targetGenus = g_objectTable[targetObjIdx].genusId;
					if (targetGenus == CRAFT_GENUS_STARSHIP || targetGenus == CRAFT_GENUS_PLATFORM) {
						Object_DirectionAndDistanceToMeshCenter(
							objectIdx, targetObjIdx, (uint16_t)g_players[playerIdx].selectedTargetComponent);
					} else {
						pai_ObjectRefDirectionToObjectRef(objectIdx, targetObjIdx);
					}
					lockRange = MISSILE_LOCK_FIGHTER_RANGE;
					if (targetObjIdx < g_activeRegionCraftObjectSlotEnd &&
						(targetGenus == CRAFT_GENUS_FREIGHTER || targetGenus == CRAFT_GENUS_STARSHIP ||
						 targetGenus == CRAFT_GENUS_PLATFORM)) {
						lockRange = MISSILE_LOCK_LARGE_CRAFT_RANGE;
					}
					if ((unsigned int)trig2_polardistance < lockRange &&
						Targeting_ScoreCandidate(targetObjIdx, 0, playerIdx) != 0) {
						ModelIndex missileBoatModelIndex;
						uint16_t lockThreshold;

						g_curCraft->warheadLockTicks += g_elapsedTicks;
						if (targetObjIdx < g_activeRegionCraftObjectSlotEnd) {
							CraftData* targetCraft;

							targetCraft = g_objectTable[targetObjIdx].mobj->pCraft;
							if (targetCraft->cmTypeId == COUNTERMEASURE_TYPE_CHAFF &&
								targetCraft->chaffActiveTimer != 0) {

#ifdef XVT_MODERN
								g_curCraft->warheadLockTicks -= XvtPlayerTiming_LockHalf(playerIdx, 1);
#else
								g_curCraft->warheadLockTicks -= g_elapsedTicks >> 1;
#endif
							}
						}
						missileBoatModelIndex = GetModelIndexFromType(CRAFT_SPECIES_MISSILE_BOAT);
						lockThreshold = GetModelIndexFromType(g_objectTable[objectIdx].objectType) ==
												missileBoatModelIndex
											? MISSILE_BOAT_LOCK_TICKS
											: DEFAULT_LOCK_TICKS;
						if (g_curCraft->warheadLockTicks >= (int16_t)lockThreshold) {
							g_players[playerIdx].missileLockState = 2;
						} else {
							g_players[playerIdx].missileLockState = 1;
						}
					} else {
						int16_t lockTicks;

						lockTicks = g_curCraft->warheadLockTicks;
						if (lockTicks > 0) {

#ifdef XVT_MODERN
							lockTicks -= XvtPlayerTiming_LockHalf(playerIdx, 2);
#else
							lockTicks -= g_elapsedTicks >> 1;
#endif

							lockTicks -= g_elapsedTicks;
							g_curCraft->warheadLockTicks = lockTicks;
							if (g_curCraft->warheadLockTicks < 0) {
								g_curCraft->warheadLockTicks = 0;
							}
						}
						g_players[playerIdx].missileLockState = 0;
					}
				}
			}

			{
				uint16_t beamTargetObjIdx;

				beamTargetObjIdx = UINT16_MAX;
				if ((g_curCraft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0 &&
					g_curCraft->beamActive != 0 && g_curCraft->beamTypeId != BEAM_TYPE_NONE &&
					g_players[playerIdx].regionSessionId == 0) {
					if (g_players[playerIdx].beamFireCooldownTimer == 0) {
						int16_t beamPresent;

						g_players[playerIdx].beamFireCooldownTimer = BEAM_FIRE_COOLDOWN_TICKS;
						beamPresent = (int16_t)(g_curCraft->beamPresent - BEAM_DRAIN_AMOUNT);
						if (beamPresent < 0) {
							beamPresent = 0;
						}
						g_curCraft->beamPresent = (uint16_t)beamPresent;
						if (beamPresent == 0 && g_curCraft->beamActive != 0) {
							g_curCraft->beamActive = 0;
							g_curCraft->beamTimer = 0;
							if (playerIdx == g_localPlayer) {
								msg_emitInFlightMessage((InFlightMessageId)((uint8_t)g_curCraft->beamTypeId +
																			BEAM_END_MESSAGE_BASE),
														g_localPlayer);
							}
						}
					}

					{
						uint16_t candidateObjIdx;

						candidateObjIdx = (uint16_t)g_players[playerIdx].currentTargetObjectIdx;
						if (candidateObjIdx != UINT16_MAX &&
							candidateObjIdx < g_activeRegionCraftObjectSlotEnd &&
							g_objectTable[candidateObjIdx].mobj->pCraft->objectKind ==
								CRAFT_OBJECT_KIND_ACTIVE &&
							Targeting_ScoreCandidate(candidateObjIdx, 0, playerIdx) != 0 &&
							(unsigned int)g_targetRangeScore < BEAM_TARGET_RANGE) {
							beamTargetObjIdx = candidateObjIdx;
						}
					}
					if (beamTargetObjIdx != UINT16_MAX) {
						CraftData* targetCraft;
						BeamType beamType;

						targetCraft = g_objectTable[beamTargetObjIdx].mobj->pCraft;
						beamType = g_curCraft->beamTypeId;
						if (beamType == BEAM_TYPE_TRACTOR || beamType == BEAM_TYPE_JAMMING) {
							if (targetCraft->cmTypeId == COUNTERMEASURE_TYPE_CHAFF &&
								targetCraft->chaffActiveTimer != 0) {
								if (playerIdx == g_localPlayer &&
									Hud_GetSystemMessagePaneState() != SYSTEM_MESSAGE_PANE_STATE) {
									msg_emitInFlightMessage(
										IFMSG_256_BEAM_DISRUPTED_BY_TARGET_S_COUNTERMEASURES, playerIdx);
								}
								beamTargetObjIdx = UINT16_MAX;
							} else {
								targetCraft->beamEffectAccum[(uint8_t)beamType] +=
									(uint16_t)g_curCraft->beamTimer;
							}
						}
					}
					if (playerIdx == g_localPlayer) {
						g_localBeamTargetObjIdx = beamTargetObjIdx;
						fsfx_UpdateBeamSystemLoop(1, playerIdx);
					}
				} else if (playerIdx == g_localPlayer) {
					g_localBeamTargetObjIdx = UINT16_MAX;
					if ((g_curCraft->systemFlags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
						fsfx_UpdateBeamSystemLoop(0, playerIdx);
					}
				}
			}

			shieldRechargeRate = PLAYER_SHIELD_RECHARGE_RATE;
			{
				uint16_t scanObjIdx;

				for (scanObjIdx = g_activeRegionObjectSlotStart;
					 scanObjIdx < g_activeRegionCraftObjectSlotEnd; ++scanObjIdx) {
					ObjectRecord* hostileObject;

					hostileObject = &g_objectTable[scanObjIdx];
					if (hostileObject->objectType != CRAFT_SPECIES_UNKNOWN &&
						(hostileObject->genusId == CRAFT_GENUS_STARSHIP ||
						 (hostileObject->genusId == CRAFT_GENUS_PLATFORM &&
						  (hostileObject->objectType == CRAFT_SPECIES_X7_FACTORY ||
						   hostileObject->objectType == CRAFT_SPECIES_REPAIR_YARD)))) {
						int hostileTeam;
						int playerTeam;

						hostileTeam = hostileObject->mobj->team;
						playerTeam = g_missionFlightGroups[g_objectTable[objectIdx].flightGroupIdx].fg.team;
						if (hostileTeam != playerTeam && g_missionTeams[hostileTeam].allies[playerTeam] < 1) {
							collide_ApplyHostileProximityWeaponDisruption(objectIdx, scanObjIdx);
						}
					}
				}
			}
		} else {
			if (doPeriodicPowerUpdate == 0) {
				continue;
			}
			{
				uint8_t genusId;

				genusId = g_objectTable[objectIdx].genusId;
				g_curCraft = g_objectTable[objectIdx].mobj->pCraft;
				if (genusId == CRAFT_GENUS_STARFIGHTER) {
					uint16_t groupAI;

					g_curCraft->shieldRedirect = POWER_RECHARGE_MAINTENANCE;
					g_curCraft->laserRedirect = POWER_RECHARGE_MAINTENANCE;
					if ((g_curCraft->systemFlags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
						int maxShield;

						groupAI = g_missionFlightGroups[g_objectTable[objectIdx].flightGroupIdx].fg.groupAI;
						if (g_curCraft->aiController.maneuverMode == AI_MANEUVER_MODE_AVOID_ATTACKER) {
							if (groupAI == 5) {
								g_curCraft->shieldRedirect = POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
							} else if (groupAI == 4 || groupAI == 3) {
								g_curCraft->shieldRedirect = POWER_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES;
							} else {
								g_curCraft->shieldRedirect = POWER_RECHARGE_MAINTENANCE;
							}
							g_curCraft->laserRedirect = POWER_RECHARGE_INCREASED;
						}
						maxShield = Craft_GetObjectMaxShield(objectIdx);
						if (g_curCraft->shieldEnergy[0] < maxShield) {
							uint16_t transferLimit;
							int16_t totalLaserCharge;
							int transferAmount;
							uint16_t slotIndex;
							uint16_t transferCount;

							if (g_curCraft->laserRedirect == POWER_RECHARGE_MAINTENANCE) {
								g_curCraft->laserRedirect = POWER_RECHARGE_MAXIMUM;
							}
							if (g_curCraft->shieldEnergy[0] > 0) {
								if (groupAI < 2) {
									transferLimit = 0;
								} else {
									int16_t updateMask;

									if (groupAI == 5) {
										updateMask = 1;
									} else if (groupAI == 4) {
										updateMask = 3;
									} else if (groupAI == 3) {
										updateMask = 7;
									} else {
										updateMask = 15;
									}
									transferLimit =
										((uint8_t)(updateMask & g_missionElapsedClock.seconds) == updateMask)
											? AI_SHIELD_TRANSFER_PULSE
											: 0;
								}
							} else {
								if (g_curCraft->shieldRedirect == POWER_RECHARGE_MAINTENANCE) {
									g_curCraft->shieldRedirect = POWER_RECHARGE_MAXIMUM;
								}
								if (groupAI < 2) {
									transferLimit = AI_SHIELD_TRANSFER_LOW;
								} else if (groupAI < 3) {
									transferLimit = AI_SHIELD_TRANSFER_MEDIUM;
								} else {
									transferLimit = AI_SHIELD_TRANSFER_HIGH;
								}
							}
							totalLaserCharge = 0;
							for (slotIndex = 0; slotIndex < g_curCraft->laserSlotCount; ++slotIndex) {
								int8_t charge;

								charge = g_curCraft->weaponSlots[slotIndex].laserCharge;
								if (charge > 0) {
									totalLaserCharge += charge;
								}
							}
							slotIndex = 0;
							transferCount = 0;
							transferAmount = g_objectTable[objectIdx].objectType == CRAFT_SPECIES_MISSILE_BOAT
												 ? MISSILE_BOAT_SHIELD_TRANSFER
												 : DEFAULT_SHIELD_TRANSFER;
							while (totalLaserCharge != 0 && transferCount < transferLimit) {
								if (g_curCraft->weaponSlots[slotIndex].laserCharge > 0) {
									--totalLaserCharge;
									--g_curCraft->weaponSlots[slotIndex].laserCharge;
									g_curCraft->shieldEnergy[0] += transferAmount;
									if (g_curCraft->shieldEnergy[0] >= maxShield) {
										totalLaserCharge = 0;
									}
								}
								++slotIndex;
								if (slotIndex >= g_curCraft->laserSlotCount) {
									slotIndex = 0;
								}
								++transferCount;
							}
						}
					}

					if (g_curCraft->laserRedirect == POWER_RECHARGE_MAINTENANCE) {
						int totalCharge;
						uint16_t chargedSlotCount;
						uint16_t slotIndex;

						totalCharge = 0;
						chargedSlotCount = 0;
						for (slotIndex = 0; slotIndex < g_curCraft->laserSlotCount; ++slotIndex) {
							if (g_curCraft->weaponSlots[slotIndex].projectileTypeId != 0) {
								totalCharge += g_curCraft->weaponSlots[slotIndex].laserCharge;
								++chargedSlotCount;
							}
						}
						if (chargedSlotCount != 0) {
							int16_t averageCharge;

							averageCharge = (int16_t)(totalCharge / (int)chargedSlotCount);
							if (averageCharge < LASER_CHARGE_LOW_THRESHOLD) {
								g_curCraft->laserRedirect = POWER_RECHARGE_MAXIMUM;
							} else {
								g_curCraft->laserRedirect = averageCharge < LASER_CHARGE_HIGH_THRESHOLD
																? POWER_RECHARGE_INCREASED
																: POWER_RECHARGE_MAINTENANCE;
							}
						}
					}
					shieldRechargeRate = PLAYER_SHIELD_RECHARGE_RATE;
				} else {
					int16_t liveShieldGenerators;

					g_curCraft->shieldRedirect = POWER_RECHARGE_MAINTENANCE;
					liveShieldGenerators = 0;
					g_curCraft->laserRedirect = POWER_RECHARGE_MAINTENANCE;
					if (g_flightMissionState.difficulty == 2 && genusId == CRAFT_GENUS_STARSHIP) {
						if (g_curCraft->hullDamage != 0) {
							if (g_objectTable[objectIdx].objectType == CRAFT_SPECIES_INTERDICTOR ||
								g_objectTable[objectIdx].objectType == CRAFT_SPECIES_VICTORY_STAR_DESTROYER ||
								g_objectTable[objectIdx].objectType ==
									CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER ||
								g_objectTable[objectIdx].objectType == CRAFT_SPECIES_SUPER_STAR_DESTROYER) {
								int meshCount;
								int meshIndex;

								meshCount =
									g_objectTable[objectIdx].objectType <
											(int)(sizeof(g_objectTypeMeshCache) /
												  sizeof(g_objectTypeMeshCache[0]))
										? g_objectTypeMeshCache[g_objectTable[objectIdx].objectType].meshCount
										: ModelMesh_GetObjectTypeMeshCount(
											  g_objectTable[objectIdx].objectType);
								for (meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
									MeshComponentType meshType;

									meshType = g_objectTable[objectIdx].objectType <
													   (int)(sizeof(g_objectTypeMeshCache) /
															 sizeof(g_objectTypeMeshCache[0]))
												   ? ModelMesh_GetCachedObjectTypeMeshType(
														 g_objectTable[objectIdx].objectType, meshIndex)
												   : ModelMesh_GetObjectTypeMeshType(
														 g_objectTable[objectIdx].objectType, meshIndex);
									if (meshType == MESH_COMPONENT_08_SHLD_GEN &&
										g_curCraft->componentHp[meshIndex] != 0) {
										++liveShieldGenerators;
									}
								}
							} else {
								liveShieldGenerators = 1;
							}
						}
						g_curCraft->shieldRedirect = POWER_RECHARGE_INCREASED;
					}
					shieldRechargeRate = LARGE_CRAFT_SHIELD_RECHARGE_RATE * liveShieldGenerators;
				}
			}
		}

		if (doPeriodicPowerUpdate != 0) {
			uint16_t slotIndex;

			g_curCraft = g_objectTable[objectIdx].mobj->pCraft;
			if (g_objectTable[objectIdx].objectType == CRAFT_SPECIES_Y_WING) {
				shieldRechargeRate *= 2;
			}
			if ((g_curCraft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0 &&
				shieldRechargeRate != 0) {
				int16_t shieldDelta;

				shieldDelta = (int16_t)(shieldRechargeRate *
										((uint8_t)g_curCraft->shieldRedirect - POWER_RECHARGE_MAINTENANCE));
				if (shieldDelta != 0) {
					if (g_curCraft->shieldDistribMode == SHIELD_DISTRIBUTION_FULLY_FORWARD) {
						Craft_AdjustCurrentShieldEnergy(objectIdx, 0, shieldDelta);
					} else if (g_curCraft->shieldDistribMode == SHIELD_DISTRIBUTION_FULLY_AFT) {
						Craft_AdjustCurrentShieldEnergy(objectIdx, 1, shieldDelta);
					} else {
						int16_t halfDelta;

						halfDelta = shieldDelta / 2;
						Craft_AdjustCurrentShieldEnergy(objectIdx, 0, halfDelta);
						Craft_AdjustCurrentShieldEnergy(objectIdx, 1, halfDelta);
					}
				}
			}

			if ((g_curCraft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				for (slotIndex = 0; slotIndex < g_curCraft->laserSlotCount; ++slotIndex) {
					uint8_t projectileType;
					int16_t chargeBasis;
					int16_t chargeDelta;

					projectileType = g_curCraft->weaponSlots[slotIndex].projectileTypeId;
					if (projectileType == 0 || projectileType == TURRET_PROJECTILE_TYPE) {
						continue;
					}
					chargeBasis = (int16_t)((uint8_t)g_curCraft->laserRedirect - POWER_RECHARGE_MAINTENANCE);
					if (g_curCraft->engineOutputScale == ENGINE_OVERDRIVE_ACTIVE) {
						chargeBasis = (int16_t)((uint8_t)g_curCraft->laserRedirect - 6);
					}
					if (g_objectTable[objectIdx].objectType == CRAFT_SPECIES_TIE_FIGHTER ||
						g_objectTable[objectIdx].objectType == CRAFT_SPECIES_TIE_BOMBER) {
						chargeDelta = (int16_t)(3 * chargeBasis);
					} else {
						chargeDelta = (int16_t)(2 * chargeBasis);
					}
					g_curCraft->weaponSlots[slotIndex].laserCharge += (int8_t)chargeDelta;
					if (chargeDelta < 0 && g_curCraft->weaponSlots[slotIndex].laserCharge < 0) {
						g_curCraft->weaponSlots[slotIndex].laserCharge = 0;
					}
					if (chargeDelta > 0 && g_curCraft->weaponSlots[slotIndex].laserCharge < 0) {
						g_curCraft->weaponSlots[slotIndex].laserCharge = MAXIMUM_LASER_CHARGE;
					}
				}
			}

			if (g_curCraft->engineOutputScale == ENGINE_OVERDRIVE_ACTIVE) {
				int anyLaserCharge;

				anyLaserCharge = 0;
				for (slotIndex = 0; slotIndex < g_curCraft->laserSlotCount; ++slotIndex) {
					if (g_curCraft->weaponSlots[slotIndex].laserCharge > 0) {
						anyLaserCharge = 1;
					}
				}
				if (anyLaserCharge == 0) {
					g_curCraft->engineOutputScale = ENGINE_OVERDRIVE_DISENGAGED;
					msg_emitInFlightMessage(IFMSG_285_ENGINE_OVERDRIVE_BOOSTERS_DISENGAGED, g_localPlayer);
					fsfx_PlaySound(FLIGHT_SOUND_POWER_DOWN, -1, g_localPlayer);
				}
			}

			if ((g_curCraft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
				int16_t beamPresent;

				beamPresent =
					(int16_t)(g_curCraft->beamPresent + BEAM_RECHARGE_STEP * ((uint8_t)g_curCraft->beamLevel -
																			  POWER_RECHARGE_MAINTENANCE));
				if (beamPresent < 0) {
					beamPresent = 0;
				}
				if (beamPresent > MAXIMUM_BEAM_CHARGE) {
					beamPresent = MAXIMUM_BEAM_CHARGE;
				}
				g_curCraft->beamPresent = (uint16_t)beamPresent;
				if (beamPresent == 0 && g_curCraft->beamActive != 0) {
					g_curCraft->beamActive = 0;
					g_curCraft->beamTimer = 0;
					if (g_objectTable[objectIdx].playerOwnerIdx == g_localPlayer) {
						msg_emitInFlightMessage(
							(InFlightMessageId)((uint8_t)g_curCraft->beamTypeId + BEAM_END_MESSAGE_BASE),
							g_localPlayer);
					}
				}
			}

			if (g_curCraft->chaffActiveTimer != 0) {
				--g_curCraft->chaffActiveTimer;
				if (g_curCraft->cmTypeId == COUNTERMEASURE_TYPE_CHAFF && g_curCraft->chaffActiveTimer == 0 &&
					g_objectTable[objectIdx].playerOwnerIdx != -1) {
					msg_emitInFlightMessage(IFMSG_368_CHAFF_BURST_EXPENDED,
											g_objectTable[objectIdx].playerOwnerIdx);
				}
			}
		}
	}

	for (objectIdx = g_activeRegionObjectSlotStart; objectIdx < g_activeRegionCraftObjectSlotEnd;
		 ++objectIdx) {
		uint16_t slotIndex;

		if (g_objectTable[objectIdx].objectType == CRAFT_SPECIES_UNKNOWN ||
			g_objectTable[objectIdx].mobj->state != 0) {
			continue;
		}
		g_curCraft = g_objectTable[objectIdx].mobj->pCraft;
		if (g_curCraft->weaponFireInhibitTimer != 0) {
			continue;
		}

		if (g_curCraft->beamEffectAccum[2] == 0
#ifdef XVT_MODERN
			&& (!XvtFlightTiming_IsUnlocked() || g_objectTable[objectIdx].playerOwnerIdx != -1 ||
				XvtFlightTiming_ReferenceDue())
#endif
		) {
#ifdef XVT_MODERN
			XvtFlightClock cannonClock = { g_elapsedTicks, g_simStepScale };
			if (g_objectTable[objectIdx].playerOwnerIdx == -1)
				cannonClock = XvtFlightTiming_EnterReference();
#endif

			for (slotIndex = 0; slotIndex < g_curCraft->cannonClassCount; ++slotIndex) {
				int16_t cooldown;

				cooldown = g_curCraft->laserState.fireCooldownTicks[slotIndex];
				if (cooldown != 0) {
					cooldown = (int16_t)(cooldown - g_elapsedTicks);
					if (cooldown < 0) {
						cooldown = 0;
					}
					g_curCraft->laserState.fireCooldownTicks[slotIndex] = cooldown;
				}
				if (g_objectTable[objectIdx].playerOwnerIdx == -1 && (int16_t)g_elapsedTicks > cooldown &&
					g_curCraft->laserState.linkMode[slotIndex] != 0) {
					if ((g_curCraft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0 &&
						g_curCraft->objectKind == CRAFT_OBJECT_KIND_ACTIVE) {
						laser_firelasersystem(objectIdx, slotIndex);
					}
					--g_curCraft->laserState.burstRemaining[slotIndex];
					g_curCraft->laserState.fireCooldownTicks[slotIndex] += 2 * g_elapsedTicks;
					g_curCraft->laserState.lastFireTimestamp[slotIndex] += 2 * g_elapsedTicks;
					if (g_curCraft->laserState.burstRemaining[slotIndex] == 0) {
						g_curCraft->laserState.linkMode[slotIndex] = 0;
					}
				}
			}

#ifdef XVT_MODERN
			XvtFlightTiming_RestoreClock(cannonClock);
#endif
		}

		for (slotIndex = 0; slotIndex < g_curCraft->laserSlotCount; ++slotIndex) {
			if (g_curCraft->weaponSlots[slotIndex].projectileTypeId == TURRET_PROJECTILE_TYPE) {
				uint16_t targetObjIdx;

				targetObjIdx = g_curCraft->turretTargetStates[slotIndex].targetObjIdx;
				if (targetObjIdx != UINT16_MAX) {

#ifdef XVT_MODERN
					if (XvtFlightTiming_ReferenceDue()) {
						XvtFlightClock weaponClock = XvtFlightTiming_EnterReference();
						laser_firewarheadlauncher(objectIdx, slotIndex, targetObjIdx);
						XvtFlightTiming_RestoreClock(weaponClock);
					}
#else
					laser_firewarheadlauncher(objectIdx, slotIndex, targetObjIdx);
#endif
				}
			}
		}

		for (slotIndex = 0; slotIndex < g_curCraft->warheadLauncherCount; ++slotIndex) {
			int16_t cooldown;

			cooldown = g_curCraft->warheadLauncherCooldownTicks[slotIndex];
			if (cooldown != 0) {
				cooldown = (int16_t)(cooldown - g_elapsedTicks);
				if (cooldown < 0) {
					cooldown = 0;
				}
				g_curCraft->warheadLauncherCooldownTicks[slotIndex] = cooldown;
			}
		}
	}

	for (objectIdx = g_regionMainObjectSlotEnd;
		 objectIdx < g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount; ++objectIdx) {
		if (g_objectTable[objectIdx].objectType != CRAFT_SPECIES_UNKNOWN &&
			g_objectTable[objectIdx].genusId == CRAFT_GENUS_MINE) {

#ifdef XVT_MODERN
			if (XvtFlightTiming_ReferenceDue()) {
				XvtFlightClock weaponClock = XvtFlightTiming_EnterReference();
				laser_UpdateMineWeaponFire(objectIdx);
				XvtFlightTiming_RestoreClock(weaponClock);
			}
#else
			laser_UpdateMineWeaponFire(objectIdx);
#endif
		}
	}
}

// FUNCTION: XVT 0x405860
uint16_t laser_GetProjectileLifetimeTicks(int projectileObjectType) {
	uint16_t wholeSecondsTicks;

	wholeSecondsTicks =
		(uint16_t)(236u * g_projectileDamageByObjectType
							  .lifetimeSeconds[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST]);
	wholeSecondsTicks =
		(uint16_t)(wholeSecondsTicks +
				   MATH2_fraction(g_projectileDamageByObjectType
									  .lifetimeFracQ16[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST],
								  236u));
	return wholeSecondsTicks;
}

// FUNCTION: XVT 0x405890
void laser_fireplayerweapon(int playerIdx) {
	enum {
		WEAPON_COOLDOWN_TICKS = 118,
		SYSTEM_NAME_MESSAGE_ARG = 87,
		LASER_SYSTEM_NAME_BASE = 92,
		WARHEAD_SYSTEM_NAME = 94
	};

	CraftData* craft;
	int objectIndex = g_players[playerIdx].objectIndex;

	if (objectIndex == -1)
		return;
	craft = g_objectTable[objectIndex].mobj->pCraft;
	if (craft->beamEffectAccum[2] != 0 &&
		(craft->cmTypeId != COUNTERMEASURE_TYPE_CHAFF || craft->chaffActiveTimer == 0)) {
		msg_emitInFlightMessage(IFMSG_361_WEAPON_FIRING_JAMMED_BY_BEAM_SYSTEM, playerIdx);
		return;
	}
	if (g_players[playerIdx].selectedWeaponMode == 0) {
		int selectedWeapon;
		int16_t cooldown;

		selectedWeapon = g_players[playerIdx].selectedWarhead;
		cooldown = craft->laserState.fireCooldownTicks[selectedWeapon];
		if (g_laserFireTimestampTrackingEnabled != 0) {
			int lockstepTimestamp;

			lockstepTimestamp = g_players[playerIdx].lockstepTimestamp;
			if (cooldown != 0) {
				int lastFireTimestamp;

				lastFireTimestamp = craft->laserState.lastFireTimestamp[selectedWeapon];
				if (lastFireTimestamp < lockstepTimestamp) {
					cooldown = 0;
					craft->laserState.lastFireTimestamp[selectedWeapon] = lockstepTimestamp;
				} else {
					cooldown = (int16_t)(2 * g_elapsedTicks);
				}
			} else {
				craft->laserState.lastFireTimestamp[selectedWeapon] = lockstepTimestamp;
			}
		}
		if ((int16_t)(g_elapsedTicks + (g_elapsedTicks >> 1)) > cooldown) {
			if ((craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				laser_firelasersystem(g_players[playerIdx].objectIndex, g_players[playerIdx].selectedWarhead);
			} else if (playerIdx == g_localPlayer) {
				int16_t selectedWarhead;

				selectedWarhead = g_players[playerIdx].selectedWarhead;
				g_msgArgTable[1] = SYSTEM_NAME_MESSAGE_ARG;
				g_msgArgTable[0] = (uint16_t)(selectedWarhead + LASER_SYSTEM_NAME_BASE);
				msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
			}
		}
		return;
	}
	if (craft->warheadLauncherCooldownTicks[g_players[playerIdx].selectedWarhead] <
		(int16_t)(g_elapsedTicks + (g_elapsedTicks >> 1))) {
		if ((craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) != 0) {
			laser_firerocketsystem(objectIndex, g_players[playerIdx].selectedWarhead);
			craft = g_objectTable[objectIndex].mobj->pCraft;
			{
				int firstSlot = g_modelDefs[craft->modelIndex]
									.warheadLauncherFirstSlot[g_players[playerIdx].selectedWarhead];
				if (craft->weaponSlots[firstSlot + 1].count + craft->weaponSlots[firstSlot].count == 0) {
					g_players[playerIdx].selectedWeaponMode = 0;
					g_players[playerIdx].selectedWarhead = 0;
					craft->laserState.fireCooldownTicks[0] = WEAPON_COOLDOWN_TICKS;
					craft->laserState.lastFireTimestamp[0] =
						g_players[playerIdx].lockstepTimestamp + WEAPON_COOLDOWN_TICKS;
				}
			}
		} else if (playerIdx == g_localPlayer) {
			g_msgArgTable[0] = WARHEAD_SYSTEM_NAME;
			g_msgArgTable[1] = SYSTEM_NAME_MESSAGE_ARG;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
		}
	}
}

// FUNCTION: XVT 0x405AC0
void laser_firelasersystem(int objectIndex, int laserSystemIndex) {
	int ownerPlayerIdx = g_objectTable[objectIndex].playerOwnerIdx;
	ModelIndex modelIndex;
	uint16_t firstSlot;
	uint16_t currentSlot;
	uint16_t lastSlot;
	uint16_t slotStep;
	uint16_t shotLimit;
	uint16_t shotsFired;
	AiController* aiController;

	g_curCraft = g_objectTable[objectIndex].mobj->pCraft;
	aiController = &g_curCraft->aiController;
	modelIndex = g_curCraft->modelIndex;
	if (g_curCraft->sFoilState != 0) {
		if (ownerPlayerIdx == g_localPlayer)
			msg_emitInFlightMessage(IFMSG_130_CANNONS_CANNOT_FIRE_WITH_S_FOILS_CLOSED, g_localPlayer);
		return;
	}
	shotsFired = 0;
	switch (g_curCraft->laserState.linkMode[laserSystemIndex]) {
		case 1:
			if (g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex] >
					g_curCraft->laserState.nextSlot[laserSystemIndex] ||
				g_modelDefs[modelIndex].laserGroupLastSlot[laserSystemIndex] <
					g_curCraft->laserState.nextSlot[laserSystemIndex])
				g_curCraft->laserState.nextSlot[laserSystemIndex] =
					g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex];
			firstSlot = g_curCraft->laserState.nextSlot[laserSystemIndex]++;
			lastSlot = firstSlot;
			if (g_modelDefs[modelIndex].laserGroupLastSlot[laserSystemIndex] <
				g_curCraft->laserState.nextSlot[laserSystemIndex])
				g_curCraft->laserState.nextSlot[laserSystemIndex] =
					(uint8_t)g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex];
			shotLimit = 1;
			slotStep = 1;
			break;
		case 2:
			if (g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex] >
					g_curCraft->laserState.nextSlot[laserSystemIndex] ||
				g_modelDefs[modelIndex].laserGroupLastSlot[laserSystemIndex] <
					g_curCraft->laserState.nextSlot[laserSystemIndex])
				g_curCraft->laserState.nextSlot[laserSystemIndex] =
					g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex];
			firstSlot = g_curCraft->laserState.nextSlot[laserSystemIndex];
			g_curCraft->laserState.nextSlot[laserSystemIndex] ^= 1;
			if (g_curCraft->laserState.nextSlot[laserSystemIndex] >
				g_modelDefs[modelIndex].laserGroupLastSlot[laserSystemIndex])
				g_curCraft->laserState.nextSlot[laserSystemIndex] =
					(uint8_t)g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex];
			lastSlot = g_modelDefs[modelIndex].laserGroupLastSlot[laserSystemIndex];
			shotLimit = (lastSlot - g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex] + 1) / 2;
			slotStep = 2;
			break;
		case 3:
			firstSlot = g_modelDefs[modelIndex].laserGroupFirstSlot[laserSystemIndex];
			lastSlot = g_modelDefs[modelIndex].laserGroupLastSlot[laserSystemIndex];
			slotStep = 1;
			shotLimit = lastSlot - firstSlot + 1;
			break;
		default:
#ifdef XVT_MODERN
			return;
#else
			break;
#endif
	}
	currentSlot = firstSlot;
	if (currentSlot <= lastSlot) {
		do {
			if (g_curCraft->weaponSlots[currentSlot].projectileTypeId != 0 &&
				g_curCraft->weaponSlots[currentSlot].laserCharge > 0) {
				firstSlot = g_modelDefs[modelIndex].laserGroupWeaponType[laserSystemIndex];
				if (g_curCraft->weaponSlots[currentSlot].laserCharge >= 64)
					++firstSlot;
				{
					unsigned int projectileIndex =
						laser_createprojectile(objectIndex, currentSlot, firstSlot);
					if (projectileIndex != UINT_MAX) {
						if (g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx].fg.status1 !=
								21 &&
							g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx].fg.status2 !=
								21) {
							if (ownerPlayerIdx != -1) {
								if (GetModelIndexFromType(5) == modelIndex ||
									GetModelIndexFromType(7) == modelIndex)
									g_curCraft->weaponSlots[currentSlot].laserCharge -= 3;
								else
									g_curCraft->weaponSlots[currentSlot].laserCharge -= 4;
							} else {
								g_curCraft->weaponSlots[currentSlot].laserCharge--;
							}
						}
						if (shotLimit >= 2) {
							if ((shotsFired & 1) == 0)
								fsfx_triggerweaponsfx(projectileIndex, ownerPlayerIdx);
						} else if (shotsFired < 2)
							fsfx_triggerweaponsfx(projectileIndex, ownerPlayerIdx);
						if (g_curCraft->weaponSlots[currentSlot].laserCharge < 0)
							g_curCraft->weaponSlots[currentSlot].laserCharge = 0;
						{
							projectileIndex -= g_projectileObjectSlotStart;
							if (ownerPlayerIdx != -1) {
								g_projectileGuidanceStates[projectileIndex].targetObjIdx =
									g_players[ownerPlayerIdx].currentTargetObjectIdx;
								if ((uint16_t)g_players[ownerPlayerIdx].currentTargetObjectIdx !=
									UINT16_MAX) {
									unsigned int targetObjectIndex =
										(uint16_t)g_players[ownerPlayerIdx].currentTargetObjectIdx;
									g_projectileGuidanceStates[projectileIndex].targetSignature =
										g_objectTable[targetObjectIndex].objectSignature;
								} else
									g_projectileGuidanceStates[projectileIndex].targetSignature = 0;
							} else {
								g_projectileGuidanceStates[projectileIndex].targetObjIdx =
									aiController->targetObjIdx;
								if (aiController->targetObjIdx != UINT16_MAX) {
									if (aiController->targetObjIdx < 0x8000)
										g_projectileGuidanceStates[projectileIndex].targetSignature =
											g_objectTable[aiController->targetObjIdx].objectSignature;
									else
										g_projectileGuidanceStates[projectileIndex].targetSignature = 0;
								} else
									g_projectileGuidanceStates[projectileIndex].targetSignature = 0;
							}
							++shotsFired;
							g_projectileGuidanceStates[projectileIndex].sourcePlayerIdx =
								(int8_t)ownerPlayerIdx;
						}
					}
				}
			}
			--shotLimit;
			if (shotLimit == 0)
				break;
			currentSlot += slotStep;
		} while (currentSlot <= lastSlot);
	}
	if (firstSlot == PROJECTILE_OBJECT_TYPE_ION_LASER ||
		firstSlot == PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
		g_curCraft->weaponStats.ionShotsFired += (uint16_t)shotsFired;
		if (ownerPlayerIdx != -1)
			g_players[ownerPlayerIdx].missionStats.ionShotsFired += (uint16_t)shotsFired;
	} else {
		g_curCraft->weaponStats.laserShotsFired += (uint16_t)shotsFired;
		if (ownerPlayerIdx != -1)
			g_players[ownerPlayerIdx].missionStats.laserShotsFired += (uint16_t)shotsFired;
	}
	g_curCraft->laserState.fireCooldownTicks[laserSystemIndex] += (int16_t)(47 * shotsFired + 2);
	g_curCraft->laserState.lastFireTimestamp[laserSystemIndex] += 47 * shotsFired + 2;
}

// FUNCTION: XVT 0x406030
void laser_firerocketsystem(int objectIndex, unsigned int launcherIndex) {
	int16_t shotsFired;
	int16_t incomplete;

	g_curCraft = g_objectTable[objectIndex].mobj->pCraft;
	{
		int weaponSlotIndex;
		uint16_t launcherSlot;
		unsigned int launcherFlags;

		launcherFlags = (uint8_t)g_curCraft->warheadLauncherFlags[launcherIndex];
		launcherSlot = g_modelDefs[g_curCraft->modelIndex].warheadLauncherFirstSlot[launcherIndex];
		shotsFired = 0;
		incomplete = 0;
		if (((uint16_t)launcherFlags & 0x7F) == 3) {
			weaponSlotIndex = launcherSlot;
			if (laser_firemissile(objectIndex, weaponSlotIndex, g_curCraft->warheadSlotTypeIds[launcherIndex],
								  launcherIndex) != -1) {
				shotsFired = 1;
			} else if (g_curCraft->weaponSlots[weaponSlotIndex].count != 0) {
				incomplete = 1;
			}
			++launcherSlot;
			if (laser_firemissile(objectIndex, launcherSlot, g_curCraft->warheadSlotTypeIds[launcherIndex],
								  launcherIndex) != -1) {
				++shotsFired;
			} else if (g_curCraft->weaponSlots[launcherSlot].count != 0) {
				incomplete = 1;
			}
		} else {
			if (((uint16_t)launcherFlags & 0x80) != 0) {
				++launcherSlot;
				if (laser_firemissile(objectIndex, launcherSlot,
									  g_curCraft->warheadSlotTypeIds[launcherIndex], launcherIndex) != -1) {
					shotsFired = 1;
				} else if (g_curCraft->weaponSlots[launcherSlot].count != 0) {
					incomplete = 1;
				}
			} else {
				if (laser_firemissile(objectIndex, launcherSlot,
									  g_curCraft->warheadSlotTypeIds[launcherIndex], launcherIndex) != -1) {
					shotsFired = 1;
				} else if (g_curCraft->weaponSlots[launcherSlot].count != 0) {
					incomplete = 1;
				}
			}
		}
	}
	g_curCraft->warheadLauncherCooldownTicks[launcherIndex] += 472;
	if (g_objectTable[objectIndex].playerOwnerIdx == g_localPlayer && incomplete == 0) {
		WarheadKindIndex warheadKind;
		PlayerData* player;

		player = &g_players[g_localPlayer];
		warheadKind = ObjectType_GetWarheadKindIndex(g_curCraft->warheadSlotTypeIds[player->selectedWarhead]);
		if (shotsFired == 0)
			msg_emitInFlightMessage((InFlightMessageId)((uint16_t)warheadKind + 38), g_localPlayer);
		else if (shotsFired == 1)
			msg_emitInFlightMessage((InFlightMessageId)((uint16_t)warheadKind + 48), g_localPlayer);
		else if (shotsFired == 2)
			msg_emitInFlightMessage((InFlightMessageId)((uint16_t)warheadKind + 58), g_localPlayer);
	}
}

// FUNCTION: XVT 0x406290
int laser_firemissile(int objectIndex, int weaponSlotIndex, int projectileTypeId,
					  unsigned int launcherIndex) {
	int ownerPlayerIdx = g_objectTable[objectIndex].playerOwnerIdx;
	AiController* controller = &g_curCraft->aiController;
	int projectileIndex = -1;

	if ((weaponSlotIndex + g_curCraft->weaponSlots)->projectileTypeId != 0 &&
		g_curCraft->weaponSlots[weaponSlotIndex].count != 0) {
		projectileIndex = laser_createprojectile(objectIndex, weaponSlotIndex, projectileTypeId);
		if (projectileIndex != -1) {
			++g_curCraft->weaponStats.warheadsFired;
			if (ownerPlayerIdx != -1) {
				++g_players[ownerPlayerIdx].warheadsFired;
				g_players[ownerPlayerIdx].missionStats.missionScore -=
					g_projectileDamageByObjectType
						.warheadPointValue[projectileTypeId - PROJECTILE_OBJECT_TYPE_FIRST];
			}
			g_flightMissionState.runtime
				.teamScores[TEAM_SCORE_MISSION]
						   [g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx].fg.team] -=
				g_projectileDamageByObjectType
					.warheadPointValue[projectileTypeId - PROJECTILE_OBJECT_TYPE_FIRST];
			fsfx_triggerweaponsfx((unsigned int)projectileIndex, ownerPlayerIdx);
			if (g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx].fg.status1 != 21 &&
				g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx].fg.status2 != 21)
				--g_curCraft->weaponSlots[weaponSlotIndex].count;

			projectileIndex -= g_projectileObjectSlotStart;
			if (launcherIndex < 2) {
				g_projectileGuidanceStates[projectileIndex].homingTier =
					(uint8_t)(g_curCraft->warheadLockTicks / SIMULATION_TICKS_PER_SECOND);
				if (g_projectileGuidanceStates[projectileIndex].homingTier > 6)
					g_projectileGuidanceStates[projectileIndex].homingTier = 6;
			}
			if (ownerPlayerIdx != -1) {
				g_projectileGuidanceStates[projectileIndex].targetObjIdx =
					(uint16_t)g_players[ownerPlayerIdx].currentTargetObjectIdx;
				g_projectileGuidanceStates[projectileIndex].targetComponentIdx =
					(uint16_t)g_players[ownerPlayerIdx].selectedTargetComponent;
				if ((uint16_t)g_players[ownerPlayerIdx].currentTargetObjectIdx != UINT16_MAX)
					g_projectileGuidanceStates[projectileIndex].targetSignature =
						g_objectTable[(uint16_t)g_players[ownerPlayerIdx].currentTargetObjectIdx]
							.objectSignature;
				else
					g_projectileGuidanceStates[projectileIndex].targetSignature = 0;
			} else {
				g_projectileGuidanceStates[projectileIndex].targetObjIdx = controller->targetObjIdx;
				g_projectileGuidanceStates[projectileIndex].targetComponentIdx = controller->targetComponent;
				if (controller->targetObjIdx != UINT16_MAX) {
					if (controller->targetObjIdx < 0x8000)
						g_projectileGuidanceStates[projectileIndex].targetSignature =
							g_objectTable[controller->targetObjIdx].objectSignature;
					else
						g_projectileGuidanceStates[projectileIndex].targetSignature = 0;
				} else {
					g_projectileGuidanceStates[projectileIndex].targetSignature = 0;
				}
			}
			g_projectileGuidanceStates[projectileIndex].sourcePlayerIdx = (int8_t)ownerPlayerIdx;
			if (g_projectileGuidanceStates[projectileIndex].targetObjIdx != UINT16_MAX &&
				g_objectTable[g_projectileGuidanceStates[projectileIndex].targetObjIdx].playerOwnerIdx != -1)
				laser_warnplayer((uint16_t)projectileIndex);
			if (launcherIndex < 2) {
				int launcherSlot =
					g_modelDefs[g_curCraft->modelIndex].warheadLauncherFirstSlot[launcherIndex];
				if (g_curCraft->weaponSlots[launcherSlot].count >=
					g_curCraft->weaponSlots[launcherSlot + 1].count)
					g_curCraft->warheadLauncherFlags[launcherIndex] &= (int8_t)~0x80;
				else
					g_curCraft->warheadLauncherFlags[launcherIndex] |= (int8_t)0x80;
			}
		}
	}
	return projectileIndex;
}

// FUNCTION: XVT 0x4065D0
int laser_createprojectile(int sourceObjectIndex, int weaponSlotIndex, int projectileObjectType) {
	ObjectRecord* source;
	uint16_t rangeEnd;
	uint16_t projectileIndex;
	uint16_t projectileGenus;
	ModelIndex modelIndex;
	int16_t hardpointZ;
	int16_t sourceType;
	int worldX;
	int worldY;
	int worldZ;
	uint16_t guidanceIndex;

	if (g_objectTable[sourceObjectIndex].playerOwnerIdx != -1) {
		projectileGenus = CRAFT_GENUS_PLAYER_PROJECTILE;
		projectileIndex = (uint16_t)(12 * g_objectTable[sourceObjectIndex].playerOwnerIdx +
									 g_objectSlotRangeByGenus[CRAFT_GENUS_PLAYER_PROJECTILE].start);
		rangeEnd = (uint16_t)(projectileIndex + 12);
		if (g_projectileDamageByObjectType
				.warheadClass[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST] != 0)
			projectileIndex = (uint16_t)(projectileIndex + 8);
		for (; projectileIndex < rangeEnd; ++projectileIndex) {
			if (g_objectTable[projectileIndex].objectType == 0) {
				g_objectTable[projectileIndex].mobj->sourceObjIdx = 0;
				g_objectTable[projectileIndex].mobj->lightIntensityScale = 0;
				break;
			}
		}
		if (projectileIndex < rangeEnd) {
			collide_ResetObjectProximityForSlot(projectileIndex);
		} else {
			projectileIndex = (uint16_t)(g_objectSlotRangeByGenus[CRAFT_GENUS_PLAYER_PROJECTILE].start + 96);
			rangeEnd = (uint16_t)(projectileIndex + 32);
			for (; projectileIndex < rangeEnd; ++projectileIndex) {
				if (g_objectTable[projectileIndex].objectType == 0) {
					g_objectTable[projectileIndex].mobj->sourceObjIdx = 0;
					g_objectTable[projectileIndex].mobj->lightIntensityScale = 0;
					break;
				}
			}
			if (projectileIndex < rangeEnd)
				collide_ResetObjectProximityForSlot(projectileIndex);
			else
				return -1;
		}
	} else {
		projectileGenus = CRAFT_GENUS_OTHER_PROJECTILE;
		projectileIndex = Object_AllocSlotForGenus(projectileGenus);
	}
	if (projectileIndex != UINT16_MAX) {
		source = &g_objectTable[sourceObjectIndex];
		sourceType = source->objectType;
		g_objectTable[projectileIndex].mobj->state = 1;
		g_objectTable[projectileIndex].genusId = (uint8_t)projectileGenus;
		g_objectTable[projectileIndex].objectType = (uint8_t)projectileObjectType;
		g_objectTable[projectileIndex].mobj->framesAlive = 1;
		g_objectTable[projectileIndex].mobj->sourceObjIdx = (uint16_t)sourceObjectIndex;
		g_objectTable[projectileIndex].mobj->sourceObjectType = (uint8_t)sourceType;
		modelIndex = GetModelIndexFromType(sourceType);
		g_objectTable[projectileIndex].mobj->iff = source->mobj->iff;
		g_objectTable[projectileIndex].pitch = source->pitch;
		g_objectTable[projectileIndex].roll = source->roll;
		g_objectTable[projectileIndex].yaw = source->yaw;
		g_objectTable[projectileIndex].mobj->speed =
			(uint16_t)(source->mobj->speed + g_projectileDamageByObjectType
												 .speed[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST]);
		g_projectileGuidanceStates[projectileIndex - g_projectileObjectSlotStart].minSpeed =
			g_objectTable[projectileIndex].mobj->speed;
		g_objectTable[projectileIndex].mobj->damageAmount =
			source->mobj->speed +
			g_projectileDamageByObjectType.damage[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST];
		if (g_objectTable[projectileIndex].mobj->damageAmount <
			g_projectileDamageByObjectType.damage[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST])
			g_objectTable[projectileIndex].mobj->damageAmount =
				g_projectileDamageByObjectType.damage[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST];
		g_objectTable[projectileIndex].mobj->lifetimeTimer =
			laser_GetProjectileLifetimeTicks(projectileObjectType);
		worldX = source->world_x;
		worldY = source->world_y;
		worldZ = source->world_z;
		hardpointZ = g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIndex].z;
		pai_calcrotatedpoint(source, g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIndex].x, hardpointZ,
							 g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIndex].y);
		if (sourceType == 53) {
			g_rotatedX *= 2;
			g_rotatedY *= 2;
			g_rotatedZ *= 2;
		}
		worldX += g_rotatedX;
		worldY += g_rotatedY;
		worldZ += g_rotatedZ;
		g_objectTable[projectileIndex].mobj->prevWorldX = worldX;
		g_objectTable[projectileIndex].mobj->prevWorldY = worldY;
		g_objectTable[projectileIndex].mobj->prevWorldZ = worldZ;
		if (source->playerOwnerIdx != -1)
			g_objectTable[projectileIndex].mobj->simStateTimestamp =
				g_players[source->playerOwnerIdx].lockstepTimestamp;
		if (g_projectileDamageByObjectType
					.warheadClass[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST] != 0 &&
			(source->genusId == CRAFT_GENUS_STARSHIP || source->genusId == CRAFT_GENUS_FREIGHTER ||
			 source->genusId == CRAFT_GENUS_PLATFORM)) {
			if (hardpointZ >= 0) {
				worldZ += g_projectileDamageByObjectType
							  .launchOffset[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST];
				g_objectTable[projectileIndex].pitch = 0;
			} else {
				worldZ -= g_projectileDamageByObjectType
							  .launchOffset[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST];
				g_objectTable[projectileIndex].pitch = INT16_MIN;
			}
			g_objectTable[projectileIndex].mobj->orientMatrixDirty = 1;
			g_objectTable[projectileIndex].mobj->moveVectorDirty =
				g_objectTable[projectileIndex].mobj->orientMatrixDirty;
			g_objectTable[projectileIndex].world_x = worldX;
			g_objectTable[projectileIndex].world_y = worldY;
			g_objectTable[projectileIndex].world_z = worldZ;
		} else {
			worldX += Math_MulQ15(g_projectileDamageByObjectType
									  .launchOffset[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST],
								  source->mobj->cachedFwdX);
			worldY += Math_MulQ15(g_projectileDamageByObjectType
									  .launchOffset[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST],
								  source->mobj->cachedFwdY);
			worldZ += Math_MulQ15(g_projectileDamageByObjectType
									  .launchOffset[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST],
								  source->mobj->cachedFwdZ);
			g_objectTable[projectileIndex].world_x = worldX;
			g_objectTable[projectileIndex].world_y = worldY;
			g_objectTable[projectileIndex].world_z = worldZ;
			g_objectTable[projectileIndex].mobj->moveX = source->mobj->moveX;
			g_objectTable[projectileIndex].mobj->moveY = source->mobj->moveY;
			g_objectTable[projectileIndex].mobj->moveZ = source->mobj->moveZ;
			g_objectTable[projectileIndex].mobj->cachedSideX = source->mobj->cachedSideX;
			g_objectTable[projectileIndex].mobj->cachedSideY = source->mobj->cachedSideY;
			g_objectTable[projectileIndex].mobj->cachedSideZ = source->mobj->cachedSideZ;
			g_objectTable[projectileIndex].mobj->cachedUpX = source->mobj->cachedUpX;
			g_objectTable[projectileIndex].mobj->cachedUpY = source->mobj->cachedUpY;
			g_objectTable[projectileIndex].mobj->cachedUpZ = source->mobj->cachedUpZ;
			g_objectTable[projectileIndex].mobj->cachedFwdX = source->mobj->cachedFwdX;
			g_objectTable[projectileIndex].mobj->cachedFwdY = source->mobj->cachedFwdY;
			g_objectTable[projectileIndex].mobj->cachedFwdZ = source->mobj->cachedFwdZ;
			g_objectTable[projectileIndex].mobj->orientMatrixDirty = 0;
			g_objectTable[projectileIndex].mobj->moveVectorDirty =
				g_objectTable[projectileIndex].mobj->orientMatrixDirty;
		}
		guidanceIndex = (uint16_t)(projectileIndex - g_projectileObjectSlotStart);
		g_projectileGuidanceStates[guidanceIndex].homingTier = 0;
		g_projectileGuidanceStates[guidanceIndex].targetObjIdx = UINT16_MAX;
		g_projectileGuidanceStates[guidanceIndex].targetSignature = 0;
		g_projectileGuidanceStates[guidanceIndex].targetComponentIdx = UINT16_MAX;
		g_projectileGuidanceStates[guidanceIndex].sourcePlayerIdx = -1;
		g_objectTable[projectileIndex].mobj->pWarheadGuidance = &g_projectileGuidanceStates[guidanceIndex];
		return projectileIndex;
	}
	return -1;
}

// FUNCTION: XVT 0x406D10
uint16_t laser_createprojectilefromstatic(uint16_t sourceObjIdx, uint16_t targetObjIdx) {
	int flightGroupIdx;
	uint16_t projectileType;
	uint16_t objectIndex;
	uint16_t guidanceIndex;

	flightGroupIdx = g_objectTable[sourceObjIdx].flightGroupIdx;
	projectileType = g_warheadTypeIds[g_missionFlightGroups[flightGroupIdx].fg.warhead];
	if (projectileType == 0) {
		return UINT16_MAX;
	}
	objectIndex = Object_AllocSlotForGenus(7);
	if (objectIndex == UINT16_MAX) {
		for (objectIndex = (uint16_t)(g_projectileObjectSlotStart + 128);
			 objectIndex < g_projectileObjectSlotEnd; objectIndex++) {
			if (g_projectileDamageByObjectType.warheadClass[g_objectTable[objectIndex].objectType -
															PROJECTILE_OBJECT_TYPE_FIRST] == 0 &&
				g_objectTable[objectIndex].mobj->team == g_missionFlightGroups[flightGroupIdx].fg.team) {
				break;
			}
		}
	}
	if (g_projectileObjectSlotEnd == objectIndex) {
		return UINT16_MAX;
	}

	g_objectTable[objectIndex].mobj->state = 1;
	g_objectTable[objectIndex].genusId = 7;
	g_objectTable[objectIndex].objectType = (uint8_t)projectileType;
	g_objectTable[objectIndex].mobj->framesAlive = 1;
	g_objectTable[objectIndex].mobj->sourceObjIdx = sourceObjIdx;
	g_objectTable[objectIndex].mobj->sourceObjectType = g_objectTable[sourceObjIdx].objectType;
	g_objectTable[objectIndex].mobj->iff = g_missionFlightGroups[flightGroupIdx].fg.iff;
	g_objectTable[objectIndex].pitch = 0;
	g_objectTable[objectIndex].roll = 0;
	g_objectTable[objectIndex].yaw = 0;
	g_objectTable[objectIndex].mobj->speed =
		g_projectileDamageByObjectType.speed[projectileType - PROJECTILE_OBJECT_TYPE_FIRST];
	g_projectileGuidanceStates[objectIndex - g_projectileObjectSlotStart].minSpeed =
		g_objectTable[objectIndex].mobj->speed;
	g_objectTable[objectIndex].mobj->damageAmount =
		g_projectileDamageByObjectType.damage[projectileType - PROJECTILE_OBJECT_TYPE_FIRST];
	g_objectTable[objectIndex].mobj->lifetimeTimer = laser_GetProjectileLifetimeTicks(projectileType);
	Mission_ResolveObjectOrMissionPointWorldLoc(sourceObjIdx, 0);
	g_objectTable[objectIndex].mobj->prevWorldX = worldlocx;
	g_objectTable[objectIndex].world_x = g_objectTable[objectIndex].mobj->prevWorldX;
	g_objectTable[objectIndex].mobj->prevWorldY = worldlocy;
	g_objectTable[objectIndex].world_y = g_objectTable[objectIndex].mobj->prevWorldY;
	g_objectTable[objectIndex].mobj->prevWorldZ = worldlocz + 384;
	g_objectTable[objectIndex].world_z = g_objectTable[objectIndex].mobj->prevWorldZ;
	guidanceIndex = (uint16_t)(objectIndex - g_projectileObjectSlotStart);
	g_projectileGuidanceStates[guidanceIndex].homingTier = (uint8_t)((GameRand() & 3) + 3);
	g_projectileGuidanceStates[guidanceIndex].targetObjIdx = targetObjIdx;
	if (targetObjIdx != UINT16_MAX && targetObjIdx < 0x8000) {
		g_projectileGuidanceStates[guidanceIndex].targetSignature =
			g_objectTable[targetObjIdx].objectSignature;
	} else {
		g_projectileGuidanceStates[guidanceIndex].targetSignature = 0;
	}
	g_projectileGuidanceStates[guidanceIndex].sourcePlayerIdx = -1;
	g_objectTable[objectIndex].mobj->pWarheadGuidance = &g_projectileGuidanceStates[guidanceIndex];
	if (g_objectTable[targetObjIdx].playerOwnerIdx != -1) {
		laser_warnplayer(guidanceIndex);
	}
	return objectIndex;
}

// FUNCTION: XVT 0x407090
int laser_createcountermeasureprojectile(unsigned int ownerObjIdx, int projectileObjectType) {
	uint16_t projectileIndex;
	ObjectRecord* owner;
	ObjectRecord* projectile;
	int ownerType;
	unsigned int rangeEnd;
	uint16_t guidanceIndex;
	CraftData* craft;
	uint16_t projectileGenus;

	if (g_objectTable[ownerObjIdx].playerOwnerIdx != -1) {
		int rangeStart;

		projectileGenus = 6;
		rangeStart = g_objectSlotRangeByGenus[6].start + 12 * g_objectTable[ownerObjIdx].playerOwnerIdx;
		projectileIndex = (uint16_t)rangeStart;
		rangeEnd = rangeStart + 12;
		if (g_projectileDamageByObjectType
				.warheadClass[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST] != 0)
			projectileIndex += 8;
		for (; projectileIndex < rangeEnd; ++projectileIndex) {
			if (g_objectTable[projectileIndex].objectType == 0) {
				g_objectTable[projectileIndex].mobj->sourceObjIdx = 0;
				g_objectTable[projectileIndex].mobj->lightIntensityScale = 0;
				break;
			}
		}
		if (projectileIndex >= rangeEnd) {
			int fallbackStart = (uint16_t)g_objectSlotRangeByGenus[6].start + 96;

			projectileIndex = (uint16_t)fallbackStart;
			rangeEnd = fallbackStart + 32;
			for (; projectileIndex < rangeEnd; ++projectileIndex) {
				if (g_objectTable[projectileIndex].objectType == 0) {
					g_objectTable[projectileIndex].mobj->sourceObjIdx = 0;
					g_objectTable[projectileIndex].mobj->lightIntensityScale = 0;
					break;
				}
			}
		}
		if (projectileIndex >= rangeEnd)
			return -1;
	} else {
		projectileGenus = 7;
		projectileIndex = Object_AllocSlotForGenus(7);
	}
	if (projectileIndex != UINT16_MAX) {

		owner = &g_objectTable[ownerObjIdx];
		ownerType = owner->objectType;
		g_objectTable[projectileIndex].mobj->state = 1;
		g_objectTable[projectileIndex].genusId = projectileGenus;
		g_objectTable[projectileIndex].objectType = (uint8_t)projectileObjectType;
		g_objectTable[projectileIndex].mobj->framesAlive = 1;
		g_objectTable[projectileIndex].mobj->sourceObjIdx = (uint16_t)ownerObjIdx;
		g_objectTable[projectileIndex].mobj->sourceObjectType = (uint8_t)ownerType;
		GetModelIndexFromType(ownerType);
		g_objectTable[projectileIndex].mobj->iff = owner->mobj->iff;
		g_objectTable[projectileIndex].mobj->team = owner->mobj->team;
		g_objectTable[projectileIndex].pitch = (int16_t)(INT16_MIN - owner->pitch);
		g_objectTable[projectileIndex].roll = owner->roll;
		g_objectTable[projectileIndex].yaw = (int16_t)(owner->yaw + 0x8000);
		g_objectTable[projectileIndex].mobj->speed =
			g_projectileDamageByObjectType.speed[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST] >> 1;
		g_projectileGuidanceStates[projectileIndex - g_projectileObjectSlotStart].minSpeed =
			g_projectileDamageByObjectType.speed[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST] +
			owner->mobj->speed;
		g_objectTable[projectileIndex].mobj->damageAmount =
			g_projectileDamageByObjectType.damage[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST] +
			owner->mobj->speed;
		if (g_objectTable[projectileIndex].mobj->damageAmount <
			g_projectileDamageByObjectType.damage[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST])
			g_objectTable[projectileIndex].mobj->damageAmount =
				g_projectileDamageByObjectType.damage[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST];
		g_objectTable[projectileIndex].mobj->lifetimeTimer =
			laser_GetProjectileLifetimeTicks(projectileObjectType);
		g_objectTable[projectileIndex].mobj->orientMatrixDirty = 1;
		g_objectTable[projectileIndex].mobj->moveVectorDirty =
			g_objectTable[projectileIndex].mobj->orientMatrixDirty;
		projectile = &g_objectTable[projectileIndex];
		projectile->world_x = owner->world_x;
		projectile->mobj->prevWorldX = projectile->world_x;
		projectile->world_y = owner->world_y;
		projectile->mobj->prevWorldY = projectile->world_y;
		projectile->world_z = owner->world_z;
		projectile->mobj->prevWorldZ = projectile->world_z;
		{
			int offset = ModelBounds_GetSizeY(projectile->objectType);
			ModelMeshScaleOperation moveOperation;

			offset = (uint16_t)(offset + ModelBounds_GetMaxY(ownerType));
			FVIEW_calcrotatemove(projectile->pitch, projectile->yaw, projectile);
			moveOperation.scale = offset;
			moveOperation.value = projectile->mobj->moveX;
			trig2_xmovedist = Math_MulQ15(moveOperation.value, moveOperation.scale);
			moveOperation.scale = offset;
			moveOperation.value = projectile->mobj->moveY;
			trig2_ymovedist = Math_MulQ15(moveOperation.value, moveOperation.scale);
			moveOperation.scale = offset;
			moveOperation.value = projectile->mobj->moveZ;
			trig2_zmovedist = Math_MulQ15(moveOperation.value, moveOperation.scale);
		}
		Object_AddTrigMoveDeltaAndClampWorldPosition((uint32_t*)projectile);
		guidanceIndex = projectileIndex - g_projectileObjectSlotStart;
		projectile->mobj->pWarheadGuidance = &g_projectileGuidanceStates[guidanceIndex];
		g_projectileGuidanceStates[guidanceIndex].homingTier = 0;
		g_projectileGuidanceStates[guidanceIndex].targetObjIdx = UINT16_MAX;
		g_projectileGuidanceStates[guidanceIndex].targetSignature = 0;
		g_projectileGuidanceStates[guidanceIndex].targetComponentIdx = UINT16_MAX;
		g_projectileGuidanceStates[guidanceIndex].sourcePlayerIdx = g_objectTable[ownerObjIdx].playerOwnerIdx;
		craft = g_objectTable[ownerObjIdx].mobj->pCraft;
		if (g_missionFlightGroups[g_objectTable[ownerObjIdx].flightGroupIdx].fg.status1 != 21 &&
			g_missionFlightGroups[g_objectTable[ownerObjIdx].flightGroupIdx].fg.status2 != 21)
			--craft->cmAmmoCount;
		craft->cmFireCooldownTimer = 472;

		if (projectileObjectType == COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) {
			uint16_t nearestTarget = UINT16_MAX;
			uint16_t nearestInterceptedTarget = UINT16_MAX;
			unsigned int nearestTargetDistance = UINT_MAX;
			unsigned int nearestInterceptedTargetDistance = UINT_MAX;
			unsigned int candidateIndex;

			candidateIndex = g_projectileObjectSlotStart;
			if ((unsigned int)g_projectileObjectSlotEnd > candidateIndex) {
				do {
					uint8_t candidateType = g_objectTable[candidateIndex].objectType;

					if (candidateType != 0 && g_objectTable[candidateIndex].mobj->state == 1 &&
						g_projectileDamageByObjectType
								.warheadClass[candidateType - PROJECTILE_OBJECT_TYPE_FIRST] != 0 &&
						g_projectileGuidanceStates[candidateIndex - g_projectileObjectSlotStart]
								.targetObjIdx == ownerObjIdx) {
						int interceptorCount = 0;
						unsigned int innerIndex;

						for (innerIndex = g_projectileObjectSlotStart;
							 innerIndex < (unsigned int)g_projectileObjectSlotEnd; ++innerIndex)
							if (candidateType == COUNTERMEASURE_PROJECTILE_OBJECT_TYPE &&
								g_projectileGuidanceStates[innerIndex - g_projectileObjectSlotStart]
										.targetObjIdx == candidateIndex)
								++interceptorCount;
						pai_ObjectRefDirectionToObjectRef(ownerObjIdx, candidateIndex);
						if (interceptorCount != 0) {
							if ((unsigned int)trig2_polardistance < nearestInterceptedTargetDistance) {
								nearestInterceptedTargetDistance = trig2_polardistance;
								nearestInterceptedTarget = (uint16_t)candidateIndex;
							}
						} else if ((unsigned int)trig2_polardistance < nearestTargetDistance) {
							nearestTargetDistance = trig2_polardistance;
							nearestTarget = (uint16_t)candidateIndex;
						}
					}
					++candidateIndex;
				} while ((unsigned int)g_projectileObjectSlotEnd > candidateIndex);
			}
			if (nearestTarget == UINT16_MAX) {
				uint16_t craftIndex;

				for (craftIndex = (uint16_t)g_activeRegionObjectSlotStart;
					 craftIndex < g_activeRegionCraftObjectSlotEnd; ++craftIndex) {
					ObjectRecord* candidate = &g_objectTable[craftIndex];

					if (candidate->objectType != 0) {
						CraftData* candidateCraft = candidate->mobj->pCraft;

						if (candidateCraft->objectKind == CRAFT_OBJECT_KIND_ACTIVE) {
							int isEnemy = 0;

							if (candidate->playerOwnerIdx == -1) {
								if (candidateCraft->aiController.targetObjIdx == ownerObjIdx)
									isEnemy = 1;
							} else {
								int hostilePlayer = 0;
								int ownerTeam =
									g_missionFlightGroups[g_objectTable[(uint16_t)ownerObjIdx].flightGroupIdx]
										.fg.team;
								uint16_t candidateIff =
									(uint16_t)g_players[candidate->playerOwnerIdx].playerIff;

								if (ownerTeam != candidateIff)
									hostilePlayer = g_missionTeams[candidateIff].allies[ownerTeam] < 1;
								if (hostilePlayer != 0)
									isEnemy = 1;
							}
							if (isEnemy != 0) {
								pai_ObjectRefDirectionToObjectRef(ownerObjIdx, craftIndex);
								if ((unsigned int)trig2_polardistance < nearestTargetDistance &&
									trig2_polardistance < 0x8000) {
									nearestTarget = craftIndex;
									nearestTargetDistance = trig2_polardistance;
								}
							}
						}
					}
				}
			}
			if (nearestTarget != UINT16_MAX) {
				g_projectileGuidanceStates[guidanceIndex].targetObjIdx = nearestTarget;
				g_projectileGuidanceStates[guidanceIndex].homingTier = 6;
				g_projectileGuidanceStates[guidanceIndex].targetSignature =
					g_objectTable[nearestTarget].objectSignature;
			} else if (nearestInterceptedTarget != UINT16_MAX) {
				g_projectileGuidanceStates[guidanceIndex].targetObjIdx = nearestInterceptedTarget;
				g_projectileGuidanceStates[guidanceIndex].homingTier = 6;
				g_projectileGuidanceStates[guidanceIndex].targetSignature =
					g_objectTable[nearestInterceptedTarget].objectSignature;
			}
		}
		if (g_projectileGuidanceStates[guidanceIndex].targetObjIdx >= g_activeRegionObjectSlotStart &&
			g_projectileGuidanceStates[guidanceIndex].targetObjIdx < g_activeRegionCraftObjectSlotEnd)
			g_objectTable[projectileIndex].mobj->lifetimeTimer >>= 1;
		if (g_projectileGuidanceStates[guidanceIndex].targetObjIdx != UINT16_MAX &&
			g_objectTable[g_projectileGuidanceStates[guidanceIndex].targetObjIdx].playerOwnerIdx ==
				g_localPlayer)
			fsfx_QueueVoiceSfx(37, 0, 0, 0, UINT16_MAX);
		if (g_objectTable[ownerObjIdx].playerOwnerIdx != -1)
			fsfx_triggerweaponsfx(projectileIndex, g_objectTable[ownerObjIdx].playerOwnerIdx);
		return projectileIndex;
	}
	return -1;
}

// FUNCTION: XVT 0x407910
void laser_warnplayer(uint16_t projectileGuidanceIdx) {
	int playerOwnerIdx;

	playerOwnerIdx =
		g_objectTable[g_projectileGuidanceStates[projectileGuidanceIdx].targetObjIdx].playerOwnerIdx;
	if (playerOwnerIdx == -1 || g_players[playerOwnerIdx].pendingActionId != 0) {
		return;
	}
	g_players[playerOwnerIdx].pendingActionId = 1;
	g_players[playerOwnerIdx].pendingActionIssuerPlayerIdx = UINT16_MAX;
	g_players[playerOwnerIdx].pendingActionParam = projectileGuidanceIdx + g_projectileObjectSlotStart;
	g_players[playerOwnerIdx].pendingActionTimer = 1416;
	if (playerOwnerIdx == g_localPlayer) {
		msg_emitInFlightMessage(IFMSG_116_MISSILE_WARNING_KEY_TO_TARGET, playerOwnerIdx);
		fsfx_speakorderack(g_localPlayer, -1, 12, -1, g_players[playerOwnerIdx].objectIndex, UINT16_MAX);
	}
}

// FUNCTION: XVT 0x446C60
void laser_UpdateMineWeaponFire(uint16_t mineObjIdx) {
	enum {
		MINE_COOLDOWN_RESET = -20,
		MINE_FIRE_RANGE = 0x10000,
		MINE_TYPE_B_LIVE_TARGET = CRAFT_SPECIES_MINE_TYPE_B,
		MINE_TYPE_C_FIRST_SIDE_ONLY = CRAFT_SPECIES_MINE_TYPE_C,
		SMALL_MINE_LAUNCH_OFFSET = 150,
		LARGE_MINE_LAUNCH_OFFSET = 170,
		TARGET_SPEED_ACCURACY_CUTOFF = 188,
		TARGET_SPEED_ACCURACY_BASE = 24063,
		MINE_PROJECTILE_SPEED_SHIFT = 1,
		MINE_PROJECTILE_LIFETIME_SCALE = 2,
		AIM_ERROR_BASE = 256,
		AIM_ERROR_MASK = 0x3FF,
		ANGLE_WRAPPED = 0x8000,
		ANGLE_QUARTER_EIGHTH = 0x2000,
		ANGLE_THREE_EIGHTHS = 0x6000,
		ANGLE_FIVE_EIGHTHS = 0xA000,
		ANGLE_SEVEN_EIGHTHS = 0xE000,
	};

	uint16_t flightGroupIdx;
	int16_t projectilePitch;
	uint16_t targetRef;
	int mineX;
	int mineY;
	int mineZ;
	int targetY;
	int targetX;
	int targetZ;
	int leadTargetX;
	int leadTargetY;
	int leadTargetZ;
	uint16_t projectileObjIdx;
	int16_t projectileYaw;

	if (g_objectTable[mineObjIdx].typeSpecificWord == 0)
		return;

	{
		uint8_t cooldown = g_objectTable[mineObjIdx].typeSpecificByte[1];
		uint16_t cooldownStep = g_elapsedTicks >> 1;

		if ((unsigned int)cooldown > cooldownStep) {
			g_objectTable[mineObjIdx].typeSpecificByte[1] = (uint8_t)(cooldown - cooldownStep);
			return;
		}
	}
	g_objectTable[mineObjIdx].typeSpecificByte[1] = (uint8_t)MINE_COOLDOWN_RESET;

	mineX = g_objectTable[mineObjIdx].world_x;
	mineY = g_objectTable[mineObjIdx].world_y;
	mineZ = g_objectTable[mineObjIdx].world_z;
	g_paifightSearchOriginX = mineX;
	g_paifightSearchOriginY = mineY;
	g_paifightSearchOriginZ = mineZ;
	flightGroupIdx = g_objectTable[mineObjIdx].flightGroupIdx;
	g_paiContext.requireLiveOrderTarget = 1;
	if (g_objectTable[mineObjIdx].objectType != MINE_TYPE_B_LIVE_TARGET)
		g_paiContext.requireLiveOrderTarget = 0;
	targetRef = paifight_FindNearestMatchingTargetFromOrigin(
		g_missionFlightGroups[flightGroupIdx].fg.orders[0].target1Type,
		g_missionFlightGroups[flightGroupIdx].fg.orders[0].target1,
		g_missionFlightGroups[flightGroupIdx].fg.orders[0].target1OrTarget2,
		g_missionFlightGroups[flightGroupIdx].fg.orders[0].target2Type,
		g_missionFlightGroups[flightGroupIdx].fg.orders[0].target2, 0);
	if (targetRef == UINT16_MAX) {
		targetRef = paifight_FindNearestMatchingTargetFromOrigin(
			g_missionFlightGroups[flightGroupIdx].fg.orders[0].secondaryTargetTypes[0],
			g_missionFlightGroups[flightGroupIdx].fg.orders[0].secondaryTargets[0],
			g_missionFlightGroups[flightGroupIdx].fg.orders[0].target3OrTarget4,
			g_missionFlightGroups[flightGroupIdx].fg.orders[0].secondaryTargetTypes[1],
			g_missionFlightGroups[flightGroupIdx].fg.orders[0].secondaryTargets[1], 0);
	}
	if (targetRef == UINT16_MAX)
		return;

	Mission_ResolveObjectOrMissionPointWorldLoc(targetRef, 0);
	targetX = worldlocx;
	targetY = worldlocy;
	targetZ = worldlocz;
	if ((unsigned int)collide_roughdistance3d(targetX - mineX, targetY - mineY, targetZ - mineZ) >=
		MINE_FIRE_RANGE)
		return;

	if (g_objectTable[targetRef].mobj != NULL) {
		uint16_t leadFrames;

		trig2_ctop(g_objectTable[targetRef].world_x - mineX, g_objectTable[targetRef].world_y - mineY,
				   g_objectTable[targetRef].world_z - mineZ);
		trig2_polardistance *= g_simStepScale;
		if (g_objectTable[mineObjIdx].objectType == MINE_TYPE_B_LIVE_TARGET)
			trig2_polardistance >>= 15;
		else
			trig2_polardistance >>= 14;
		leadFrames = (uint16_t)trig2_polardistance;
		leadFrames = (uint16_t)(leadFrames + (GameRand() & 3));
		leadFrames--;
		leadTargetX =
			g_objectTable[targetRef].world_x +
			leadFrames * (
#ifdef XVT_MODERN
							 (XvtFlightTiming_IsUnlocked() ? XvtReferenceMotion_Axis(targetRef, 0)
														   : (g_objectTable[targetRef].world_x -
															  g_objectTable[targetRef].mobj->prevWorldX))
#else
							 g_objectTable[targetRef].world_x - g_objectTable[targetRef].mobj->prevWorldX
#endif
						 );
		leadTargetY =
			g_objectTable[targetRef].world_y +
			leadFrames * (
#ifdef XVT_MODERN
							 (XvtFlightTiming_IsUnlocked() ? XvtReferenceMotion_Axis(targetRef, 1)
														   : (g_objectTable[targetRef].world_y -
															  g_objectTable[targetRef].mobj->prevWorldY))
#else
							 g_objectTable[targetRef].world_y - g_objectTable[targetRef].mobj->prevWorldY
#endif
						 );
		leadTargetZ =
			g_objectTable[targetRef].world_z +
			leadFrames * (
#ifdef XVT_MODERN
							 (XvtFlightTiming_IsUnlocked() ? XvtReferenceMotion_Axis(targetRef, 2)
														   : (g_objectTable[targetRef].world_z -
															  g_objectTable[targetRef].mobj->prevWorldZ))
#else
							 g_objectTable[targetRef].world_z - g_objectTable[targetRef].mobj->prevWorldZ
#endif
						 );
	} else {
		leadTargetX = targetX;
		leadTargetY = targetY;
		leadTargetZ = targetZ;
	}

	trig2_ctop(leadTargetX - mineX, leadTargetY - mineY, leadTargetZ - mineZ);
	projectileYaw = trig2_xyangle;
	projectilePitch = pitchQ16;
	{
		uint16_t launchOffset = g_objectTable[mineObjIdx].objectType > MINE_TYPE_B_LIVE_TARGET
									? LARGE_MINE_LAUNCH_OFFSET
									: SMALL_MINE_LAUNCH_OFFSET;

		if (pitchQ16 < ANGLE_QUARTER_EIGHTH) {
			mineZ += launchOffset;
		} else if (pitchQ16 > ANGLE_THREE_EIGHTHS) {
			if (g_objectTable[mineObjIdx].objectType >= MINE_TYPE_C_FIRST_SIDE_ONLY)
				return;
			mineZ -= launchOffset;
		} else if (trig2_xyangle < ANGLE_QUARTER_EIGHTH || trig2_xyangle > ANGLE_SEVEN_EIGHTHS) {
			mineY += launchOffset;
		} else if (trig2_xyangle < ANGLE_THREE_EIGHTHS) {
			mineX += launchOffset;
		} else if (trig2_xyangle < ANGLE_FIVE_EIGHTHS) {
			mineY -= launchOffset;
		} else {
			mineX -= launchOffset;
		}
	}

	{
		int16_t rangeScore = -1;
		uint16_t invertedRange;
		uint16_t targetSpeedAccuracy;
		uint16_t accuracyThreshold;

		if (trig2_polardistance < MINE_FIRE_RANGE)
			rangeScore = (int16_t)trig2_polardistance;
		invertedRange = (uint16_t)~rangeScore;
		if (g_objectTable[targetRef].mobj == NULL) {
			targetSpeedAccuracy = UINT16_MAX;
		} else {
			uint16_t targetSpeed = g_objectTable[targetRef].mobj->speed;

			targetSpeedAccuracy = UINT16_MAX;
			if (targetSpeed >= TARGET_SPEED_ACCURACY_CUTOFF) {
				targetSpeedAccuracy = (uint16_t)(TARGET_SPEED_ACCURACY_BASE - (targetSpeed << 7));
			}
		}
		accuracyThreshold = MATH2_fraction(invertedRange, targetSpeedAccuracy);
		if ((uint16_t)GameRand() > accuracyThreshold) {
			int16_t aimError = (int16_t)((GameRand() - AIM_ERROR_BASE) & AIM_ERROR_MASK);

			if ((uint16_t)GameRand() >= 0x8000u)
				aimError = (int16_t)-aimError;
			projectileYaw = (int16_t)(projectileYaw + aimError);
			aimError = (int16_t)((GameRand() - AIM_ERROR_BASE) & AIM_ERROR_MASK);
			if ((uint16_t)GameRand() >= 0x8000u) {
				projectilePitch = (int16_t)(projectilePitch - aimError);
				if ((projectilePitch & ANGLE_WRAPPED) != 0)
					projectilePitch = 0;
			} else {
				projectilePitch = (int16_t)(projectilePitch + aimError);
				if ((projectilePitch & ANGLE_WRAPPED) != 0)
					projectilePitch = INT16_MAX;
			}
		}
	}

	projectileObjIdx = Object_AllocSlotForGenus(CRAFT_GENUS_OTHER_PROJECTILE);
	if (projectileObjIdx != UINT16_MAX) {
		uint16_t projectileObjectType;
		int16_t launchOffset;
		int guidanceIndex;

		g_objectTable[projectileObjIdx].mobj->state = 1;
		g_objectTable[projectileObjIdx].genusId = CRAFT_GENUS_OTHER_PROJECTILE;
		if (g_objectTable[mineObjIdx].objectType == MINE_TYPE_B_LIVE_TARGET) {
			projectileObjectType = PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER;
		} else if (g_missionFlightGroups[flightGroupIdx].fg.iff == 1 ||
				   g_missionFlightGroups[flightGroupIdx].fg.iff == 4) {
			projectileObjectType = PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER;
		} else {
			projectileObjectType = PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER;
		}
		g_objectTable[projectileObjIdx].objectType = (uint8_t)projectileObjectType;
		g_objectTable[projectileObjIdx].mobj->framesAlive = 1;
		g_objectTable[projectileObjIdx].mobj->sourceObjIdx = mineObjIdx;
		g_objectTable[projectileObjIdx].mobj->sourceObjectType = 0;
		g_objectTable[projectileObjIdx].mobj->iff =
			g_missionFlightGroups[g_objectTable[mineObjIdx].flightGroupIdx].fg.iff;
		g_objectTable[projectileObjIdx].pitch = projectilePitch;
		g_objectTable[projectileObjIdx].roll = 0;
		g_objectTable[projectileObjIdx].yaw = projectileYaw;
		g_objectTable[projectileObjIdx].mobj->orientMatrixDirty = 1;
		g_objectTable[projectileObjIdx].mobj->moveVectorDirty =
			g_objectTable[projectileObjIdx].mobj->orientMatrixDirty;
		g_objectTable[projectileObjIdx].mobj->speed =
			g_projectileDamageByObjectType.speed[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST] >>
			MINE_PROJECTILE_SPEED_SHIFT;
		g_objectTable[projectileObjIdx].mobj->lifetimeTimer =
			(uint16_t)(MINE_PROJECTILE_LIFETIME_SCALE *
					   laser_GetProjectileLifetimeTicks(projectileObjectType));
		g_objectTable[projectileObjIdx].mobj->damageAmount =
			g_projectileDamageByObjectType.damage[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST];
		FVIEW_calcrotatemove(projectilePitch, projectileYaw, &g_objectTable[projectileObjIdx]);
		g_objectTable[projectileObjIdx].mobj->prevWorldX = mineX;
		g_objectTable[projectileObjIdx].mobj->prevWorldY = mineY;
		g_objectTable[projectileObjIdx].mobj->prevWorldZ = mineZ;
		launchOffset =
			g_projectileDamageByObjectType.launchOffset[projectileObjectType - PROJECTILE_OBJECT_TYPE_FIRST];
		mineX += Math_MulQ15(g_fviewMoveX_Q15, launchOffset);
		mineY += Math_MulQ15(g_fviewMoveY_Q15, launchOffset);
		mineZ += Math_MulQ15(g_fviewMoveZ_Q15, launchOffset);
		g_objectTable[projectileObjIdx].world_x = mineX;
		g_objectTable[projectileObjIdx].world_y = mineY;
		g_objectTable[projectileObjIdx].world_z = mineZ;
		fsfx_triggerweaponsfx(projectileObjIdx, g_localPlayer);
		guidanceIndex = (uint16_t)(projectileObjIdx - g_projectileObjectSlotStart);
		g_projectileGuidanceStates[guidanceIndex].homingTier = 0;
		g_projectileGuidanceStates[guidanceIndex].targetObjIdx = targetRef;
		if (targetRef == UINT16_MAX || targetRef >= 0x8000u)
			g_projectileGuidanceStates[guidanceIndex].targetSignature = 0;
		else
			g_projectileGuidanceStates[guidanceIndex].targetSignature =
				g_objectTable[targetRef].objectSignature;
		g_projectileGuidanceStates[guidanceIndex].sourcePlayerIdx = -1;
		g_objectTable[projectileObjIdx].mobj->pWarheadGuidance = &g_projectileGuidanceStates[guidanceIndex];
	}
}

// FUNCTION: XVT 0x4A7900
void laser_firewarheadlauncher(uint16_t sourceObjIdx, uint16_t weaponSlotIdx, uint16_t targetRef) {
	enum {
		LASER_CHARGE_VALUE_MASK = 0x7F,
		LASER_CHARGE_FLAG_MASK = 0x80,
		LASER_CHARGE_RESET_VALUE = 59,
		BEAM_FIRE_BLOCK_THRESHOLD = 0x28000,
		BEAM_SINGLE_DRAIN_THRESHOLD = 0x18000,
		BEAM_DOUBLE_DRAIN_THRESHOLD = 0x8000,
		BEAM_DRAIN_TICK_THRESHOLD = 118,
		SKILL_FAST_DRAIN_THRESHOLD = 0xAAAA,
		SKILL_MEDIUM_DRAIN_THRESHOLD = 0x5555,
		SKILL_SLOW_DRAIN_THRESHOLD = 0x4000,
		SKILL_SLOW_DRAIN_DIVISOR = 6,
		LARGE_LAUNCHER_OBJECT_TYPE = 54,
		HALF_SCALE_LAUNCHER_OBJECT_TYPE = 53,
		MODEL_TYPE_CACHE_CAPACITY = 73,
		WARHEAD_LAUNCH_RANGE = 0x14000,
		ANIMATED_MESH_ANGLE_SHIFT = 8,
		WEAPON_GROUP_COUNT = 2,
		PROJECTILE_LIFETIME_SCALE = 3,
		AIM_ERROR_BASE = 256,
		AIM_ERROR_MASK = 0x3FF,
		ANGLE_WRAPPED = 0x8000,
	};

	ObjectRecord* sourceObject;
	uint16_t effectiveSkill;
	uint16_t mainHullMeshIdx;
	uint16_t projectileObjIdx;
	uint16_t projectileType;
	uint16_t leadFrames;
	uint16_t leadScale;
	uint16_t projectileYaw;
	int16_t projectilePitch;
	uint8_t alternateHardpointIdx;
	uint8_t charge;
	uint8_t chargeValue;
	uint8_t drainAmount;
	uint8_t remainingCharge;
	uint8_t nearestRank;
	int16_t weaponCount;
	int16_t hardpointX;
	int16_t hardpointY;
	int16_t hardpointZ;
	int meshIdx;
	int modelIndex;
	uint8_t nearestVertexIdx;
	int localZ;
	int localX;
	int localY;
	int targetX;
	int targetY;
	int targetZ;
	int launchX;
	int launchY;
	int launchZ;
	int meshType;
	int collisionBlocked;
	int guidanceIndex;
	uint16_t weaponGroupIndex;
	int previousSlotIdx;
	int targetObjIdx;
	uint16_t ionWeapon;
	int launchOffset;

	if (g_curCraft->workingSubsystems == 0)
		return;

	modelIndex = g_curCraft->modelIndex;
	mainHullMeshIdx = g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIdx].meshIdx;
	alternateHardpointIdx = g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIdx].alternateMeshHardpointIdx;
	meshIdx = mainHullMeshIdx;
	if (g_curCraft->componentHp[meshIdx] == 0)
		return;

	sourceObject = &g_objectTable[sourceObjIdx];
	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	charge = (uint8_t)g_curCraft->weaponSlots[weaponSlotIdx].laserCharge;
	chargeValue = charge & LASER_CHARGE_VALUE_MASK;
	if (chargeValue != 0) {
		if (g_curCraft->beamEffectAccum[2] != 0) {
			if ((unsigned int)g_curCraft->beamEffectAccum[2] >= BEAM_FIRE_BLOCK_THRESHOLD)
				return;
			if ((unsigned int)g_curCraft->beamEffectAccum[2] >= BEAM_SINGLE_DRAIN_THRESHOLD) {
				if (g_missionElapsedClock.subsecondTicks < BEAM_DRAIN_TICK_THRESHOLD)
					return;
				drainAmount = 1;
			} else if ((unsigned int)g_curCraft->beamEffectAccum[2] >= BEAM_DOUBLE_DRAIN_THRESHOLD) {
				if (g_missionElapsedClock.subsecondTicks < BEAM_DRAIN_TICK_THRESHOLD)
					return;
				drainAmount = 2;
			} else {
				drainAmount = 1;
			}
		} else if (effectiveSkill >= SKILL_FAST_DRAIN_THRESHOLD) {
			drainAmount = (uint8_t)(g_elapsedTicks >> 1);
		} else if (effectiveSkill >= SKILL_MEDIUM_DRAIN_THRESHOLD) {
			drainAmount = (uint8_t)(g_elapsedTicks >> 2);
		} else if (effectiveSkill >= SKILL_SLOW_DRAIN_THRESHOLD) {
			drainAmount = (uint8_t)(g_elapsedTicks / SKILL_SLOW_DRAIN_DIVISOR);
		} else {
			drainAmount = (uint8_t)(g_elapsedTicks >> 3);
		}
		if (drainAmount == 0)
			drainAmount = 1;
		remainingCharge = chargeValue;
		chargeValue = (uint8_t)(chargeValue - drainAmount);
		if (chargeValue > remainingCharge)
			chargeValue = 0;
		g_curCraft->weaponSlots[weaponSlotIdx].laserCharge = charge & LASER_CHARGE_FLAG_MASK;
		g_curCraft->weaponSlots[weaponSlotIdx].laserCharge |= (int8_t)chargeValue;
		return;
	}

	g_curCraft->weaponSlots[weaponSlotIdx].laserCharge = charge & LASER_CHARGE_FLAG_MASK;
	g_curCraft->weaponSlots[weaponSlotIdx].laserCharge |= LASER_CHARGE_RESET_VALUE;
	launchX = sourceObject->world_x;
	launchY = sourceObject->world_y;
	launchZ = sourceObject->world_z;
	if (sourceObject->objectType == LARGE_LAUNCHER_OBJECT_TYPE) {
		Mission_ResolveObjectOrMissionPointWorldLoc(targetRef, 0);
		localX = worldlocx - launchX;
		localY = worldlocy - launchY;
		localZ = worldlocz - launchZ;
		if (sourceObject->mobj == NULL)
			return;
		if (sourceObject->mobj->orientMatrixDirty != 0) {
			FVIEW_calcrotatemove(sourceObject->pitch, sourceObject->yaw, sourceObject);
			FVIEW_calcrotateorient(sourceObject->roll, 0, sourceObject);
		}
		g_rotatedX = Math_Dot3Q15(sourceObject->mobj->cachedSideX, sourceObject->mobj->cachedSideY,
								  sourceObject->mobj->cachedSideZ, localX, localY, localZ);
		g_rotatedY = -Math_Dot3Q15(sourceObject->mobj->cachedFwdX, sourceObject->mobj->cachedFwdY,
								   sourceObject->mobj->cachedFwdZ, localX, localY, localZ);
		g_rotatedZ = Math_Dot3Q15(sourceObject->mobj->cachedUpX, sourceObject->mobj->cachedUpY,
								  sourceObject->mobj->cachedUpZ, localX, localY, localZ);
		mainHullMeshIdx = (uint16_t)ModelMesh_FindNearestLiveMainHullByBounds(
			sourceObject->objectType, g_rotatedX, g_rotatedY, g_rotatedZ);
		nearestRank = 0;
		for (previousSlotIdx = 0; previousSlotIdx < weaponSlotIdx; ++previousSlotIdx) {
			if (g_curCraft->turretTargetStates[previousSlotIdx].targetObjIdx == targetRef)
				++nearestRank;
		}
		nearestVertexIdx = ModelMesh_FindNearestVertexForPoint(
			sourceObject->objectType, g_rotatedX, g_rotatedY, g_rotatedZ, mainHullMeshIdx, nearestRank);
		localX = ModelMesh_GetVertexX(sourceObject->objectType, mainHullMeshIdx, nearestVertexIdx);
		localY = -ModelMesh_GetVertexY(sourceObject->objectType, mainHullMeshIdx, nearestVertexIdx);
		localZ = ModelMesh_GetVertexZ(sourceObject->objectType, mainHullMeshIdx, nearestVertexIdx);
		g_rotatedX = Math_Dot3Q15(sourceObject->mobj->cachedSideX, sourceObject->mobj->cachedUpX,
								  sourceObject->mobj->cachedFwdX, localX, localZ, localY);
		g_rotatedY = Math_Dot3Q15(sourceObject->mobj->cachedSideY, sourceObject->mobj->cachedUpY,
								  sourceObject->mobj->cachedFwdY, localX, localZ, localY);
		g_rotatedZ = Math_Dot3Q15(sourceObject->mobj->cachedSideZ, sourceObject->mobj->cachedUpZ,
								  sourceObject->mobj->cachedFwdZ, localX, localZ, localY);
	} else {
		if (alternateHardpointIdx == UINT8_MAX || (g_missionElapsedClock.subsecondTicks & 1) == 0) {
			hardpointX = g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIdx].x;
			hardpointY = g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIdx].y;
			hardpointZ = g_modelDefs[modelIndex].weaponHardpoints[weaponSlotIdx].z;
		} else if (sourceObject->objectType == HALF_SCALE_LAUNCHER_OBJECT_TYPE) {
			hardpointX =
				(int16_t)(ModelMesh_GetHardpointX(sourceObject->objectType, meshIdx, alternateHardpointIdx) >>
						  1);
			hardpointY =
				(int16_t)(ModelMesh_GetHardpointY(sourceObject->objectType, meshIdx, alternateHardpointIdx) >>
						  1);
			hardpointZ =
				(int16_t)(ModelMesh_GetHardpointZ(sourceObject->objectType, meshIdx, alternateHardpointIdx) >>
						  1);
		} else {
			hardpointX =
				(int16_t)ModelMesh_GetHardpointX(sourceObject->objectType, meshIdx, alternateHardpointIdx);
			hardpointY =
				(int16_t)ModelMesh_GetHardpointY(sourceObject->objectType, meshIdx, alternateHardpointIdx);
			hardpointZ =
				(int16_t)ModelMesh_GetHardpointZ(sourceObject->objectType, meshIdx, alternateHardpointIdx);
		}
		{
			int sourceObjectType = sourceObject->objectType;

			if (sourceObjectType < MODEL_TYPE_CACHE_CAPACITY)
				meshType = ModelMesh_GetCachedObjectTypeMeshType(sourceObjectType, meshIdx);
			else
				meshType = ModelMesh_GetObjectTypeMeshType(sourceObjectType, meshIdx);
		}
		if (meshType == MESH_COMPONENT_21_LASR_TUR) {
			g_rotatedX = hardpointX;
			g_rotatedY = hardpointY;
			g_rotatedZ = hardpointZ;
			if (sourceObject->objectType == HALF_SCALE_LAUNCHER_OBJECT_TYPE) {
				g_rotatedX *= 2;
				g_rotatedY *= 2;
				g_rotatedZ *= 2;
			}
			ModelMesh_ApplyAnimatedMeshRotationToPoint(
				(int16_t)(g_curCraft->meshRotation[meshIdx] << ANIMATED_MESH_ANGLE_SHIFT),
				sourceObject->objectType, meshIdx, g_rotatedX, g_rotatedY, g_rotatedZ);
			if (sourceObject->objectType == HALF_SCALE_LAUNCHER_OBJECT_TYPE) {
				g_rotatedX >>= 1;
				g_rotatedY >>= 1;
				g_rotatedZ >>= 1;
			}
			hardpointX = (int16_t)g_rotatedX;
			hardpointY = (int16_t)g_rotatedY;
			hardpointZ = (int16_t)g_rotatedZ;
		}
		pai_calcrotatedpoint(sourceObject, hardpointX, hardpointZ, hardpointY);
	}
	if (sourceObject->objectType == HALF_SCALE_LAUNCHER_OBJECT_TYPE) {
		g_rotatedX *= 2;
		g_rotatedY *= 2;
		g_rotatedZ *= 2;
	}
	launchX += g_rotatedX;
	launchY += g_rotatedY;
	launchZ += g_rotatedZ;

	targetObjIdx = targetRef;
	Mission_ResolveObjectOrMissionPointWorldLoc((uint16_t)targetObjIdx, 0);
	targetX = worldlocx;
	targetY = worldlocy;
	targetZ = worldlocz;
	{
		int targetDelta[3];

		targetDelta[0] = targetX - launchX;
		targetDelta[1] = targetY - launchY;
		targetDelta[2] = targetZ - launchZ;
		if ((unsigned int)collide_roughdistance3d(targetDelta[0], targetDelta[1], targetDelta[2]) >
			WARHEAD_LAUNCH_RANGE)
			return;
		g_collisionProbeWorldX = targetX;
		g_collisionSegmentStartWorldX = launchX;
		g_collisionProbeWorldY = targetY;
		g_collisionSegmentStartWorldY = launchY;
		g_collisionProbeWorldZ = targetZ;
		g_collisionSegmentStartWorldZ = launchZ;
		if (sourceObject->objectType == LARGE_LAUNCHER_OBJECT_TYPE) {
			g_warheadLaunchHullMeshOrdinal = mainHullMeshIdx;
			g_collideSweepRejectNearStartHits = 1;
			collisionBlocked = collide_CheckSweptModelCollision(sourceObjIdx, sourceObjIdx);
			g_collideSweepRejectNearStartHits = 0;
		} else {
			collisionBlocked = collide_CheckSweptModelCollision(sourceObjIdx, sourceObjIdx);
		}
		if (collisionBlocked != 0)
			return;

		weaponCount = g_curCraft->weaponSlots[weaponSlotIdx].count;
		if (g_objectTable[targetObjIdx].mobj != NULL) {
			trig2_ctop(targetDelta[0], targetDelta[1], targetDelta[2]);
			trig2_polardistance *= g_simStepScale;
			if (weaponCount != 0)
				trig2_polardistance >>= 15;
			else
				trig2_polardistance >>= 14;
			leadFrames = (uint16_t)trig2_polardistance;
			leadFrames = (uint16_t)(leadFrames + (GameRand() & 3));
			--leadFrames;
			if (g_objectTable[targetObjIdx].mobj->speed == 0)
				leadFrames = 0;
			leadScale = (uint16_t)MATH2_fraction(leadFrames, effectiveSkill);
			targetX += leadScale * (
#ifdef XVT_MODERN
									   (XvtFlightTiming_IsUnlocked()
											? XvtReferenceMotion_Axis(targetRef, 0) +
												  (targetX - g_objectTable[targetObjIdx].world_x)
											: (targetX - g_objectTable[targetObjIdx].mobj->prevWorldX))
#else
									   targetX - g_objectTable[targetObjIdx].mobj->prevWorldX
#endif
								   );
			targetY += leadScale * (
#ifdef XVT_MODERN
									   (XvtFlightTiming_IsUnlocked()
											? XvtReferenceMotion_Axis(targetRef, 1) +
												  (targetY - g_objectTable[targetObjIdx].world_y)
											: (targetY - g_objectTable[targetObjIdx].mobj->prevWorldY))
#else
									   targetY - g_objectTable[targetObjIdx].mobj->prevWorldY
#endif
								   );
			targetZ += leadScale * (
#ifdef XVT_MODERN
									   (XvtFlightTiming_IsUnlocked()
											? XvtReferenceMotion_Axis(targetRef, 2) +
												  (targetZ - g_objectTable[targetObjIdx].world_z)
											: (targetZ - g_objectTable[targetObjIdx].mobj->prevWorldZ))
#else
									   targetZ - g_objectTable[targetObjIdx].mobj->prevWorldZ
#endif
								   );
		}
	}
	trig2_ctop(targetX - launchX, targetY - launchY, targetZ - launchZ);
	projectileYaw = trig2_xyangle;
	projectilePitch = pitchQ16;
	{
		uint16_t accuracyThreshold = UINT16_MAX;

		if ((uint16_t)GameRand() > accuracyThreshold) {
			int16_t aimError = (int16_t)((GameRand() - AIM_ERROR_BASE) & AIM_ERROR_MASK);

			if ((uint16_t)GameRand() >= 0x8000u)
				aimError = (int16_t)-aimError;
			projectileYaw = (uint16_t)(projectileYaw + aimError);
			aimError = (int16_t)((GameRand() - AIM_ERROR_BASE) & AIM_ERROR_MASK);
			if ((uint16_t)GameRand() >= 0x8000u) {
				projectilePitch = (int16_t)(projectilePitch - aimError);
				if ((projectilePitch & ANGLE_WRAPPED) != 0)
					projectilePitch = 0;
			} else {
				projectilePitch = (int16_t)(projectilePitch + aimError);
				if ((projectilePitch & ANGLE_WRAPPED) != 0)
					projectilePitch = INT16_MAX;
			}
		}
	}
	projectileObjIdx = Object_AllocSlotForGenus(CRAFT_GENUS_OTHER_PROJECTILE);
	if (projectileObjIdx == UINT16_MAX)
		return;

	g_objectTable[projectileObjIdx].mobj->state = 1;
	g_objectTable[projectileObjIdx].genusId = CRAFT_GENUS_OTHER_PROJECTILE;
	g_objectTable[projectileObjIdx].mobj->iff = sourceObject->mobj->iff;
	ionWeapon = 0;
	for (weaponGroupIndex = 0; weaponGroupIndex < WEAPON_GROUP_COUNT; ++weaponGroupIndex) {
		if (g_modelDefs[modelIndex].laserGroupFirstSlot[weaponGroupIndex] <= weaponSlotIdx &&
			g_modelDefs[modelIndex].laserGroupLastSlot[weaponGroupIndex] >= weaponSlotIdx &&
			(g_modelDefs[modelIndex].laserGroupWeaponType[weaponGroupIndex] ==
				 PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER_2 ||
			 g_modelDefs[modelIndex].laserGroupWeaponType[weaponGroupIndex] ==
				 PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER_2)) {
			ionWeapon = 1;
		}
	}
	if (weaponCount != 0) {
		projectileType =
			ionWeapon != 0 ? PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER : PROJECTILE_OBJECT_TYPE_ION_LASER;
	} else if (sourceObject->mobj->iff == 0 || sourceObject->mobj->iff == 2) {
		projectileType = ionWeapon != 0 ? PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER_2
										: PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER;
	} else {
		projectileType = ionWeapon != 0 ? PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER_2
										: PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER;
	}
	g_objectTable[projectileObjIdx].objectType = (uint8_t)projectileType;
	g_objectTable[projectileObjIdx].mobj->framesAlive = 1;
	g_objectTable[projectileObjIdx].mobj->sourceObjIdx = sourceObjIdx;
	g_objectTable[projectileObjIdx].mobj->sourceObjectType = sourceObject->objectType;
	g_objectTable[projectileObjIdx].pitch = projectilePitch;
	g_objectTable[projectileObjIdx].roll = 0;
	g_objectTable[projectileObjIdx].yaw = (int16_t)projectileYaw;
	g_objectTable[projectileObjIdx].mobj->orientMatrixDirty = 1;
	g_objectTable[projectileObjIdx].mobj->moveVectorDirty =
		g_objectTable[projectileObjIdx].mobj->orientMatrixDirty;
	g_objectTable[projectileObjIdx].mobj->speed =
		g_projectileDamageByObjectType.speed[projectileType - PROJECTILE_OBJECT_TYPE_FIRST];
	g_objectTable[projectileObjIdx].mobj->damageAmount =
		g_projectileDamageByObjectType.damage[projectileType - PROJECTILE_OBJECT_TYPE_FIRST];
	g_objectTable[projectileObjIdx].mobj->lifetimeTimer =
		(uint16_t)(PROJECTILE_LIFETIME_SCALE * laser_GetProjectileLifetimeTicks(projectileType));
	FVIEW_calcrotatemove(projectilePitch, (int16_t)projectileYaw, &g_objectTable[projectileObjIdx]);
	g_objectTable[projectileObjIdx].mobj->prevWorldX = launchX;
	g_objectTable[projectileObjIdx].mobj->prevWorldY = launchY;
	g_objectTable[projectileObjIdx].mobj->prevWorldZ = launchZ;
	launchOffset =
		(int16_t)g_projectileDamageByObjectType.launchOffset[projectileType - PROJECTILE_OBJECT_TYPE_FIRST];
	launchX += Math_MulQ15(g_fviewMoveX_Q15, launchOffset);
	launchY += Math_MulQ15(g_fviewMoveY_Q15, launchOffset);
	launchZ += Math_MulQ15(g_fviewMoveZ_Q15, launchOffset);
	g_objectTable[projectileObjIdx].world_x = launchX;
	g_objectTable[projectileObjIdx].world_y = launchY;
	g_objectTable[projectileObjIdx].world_z = launchZ;

	guidanceIndex = (uint16_t)(projectileObjIdx - g_projectileObjectSlotStart);
	g_projectileGuidanceStates[guidanceIndex].homingTier = 0;
	g_projectileGuidanceStates[guidanceIndex].targetObjIdx = targetRef;
	if (targetRef == UINT16_MAX || targetRef >= 0x8000u)
		g_projectileGuidanceStates[guidanceIndex].targetSignature = 0;
	else
		g_projectileGuidanceStates[guidanceIndex].targetSignature = g_objectTable[targetRef].objectSignature;
	g_projectileGuidanceStates[guidanceIndex].minSpeed = g_objectTable[projectileObjIdx].mobj->speed;
	g_projectileGuidanceStates[guidanceIndex].sourcePlayerIdx = -1;
	g_objectTable[projectileObjIdx].mobj->pWarheadGuidance = &g_projectileGuidanceStates[guidanceIndex];
	fsfx_triggerweaponsfx(projectileObjIdx, g_localPlayer);
}

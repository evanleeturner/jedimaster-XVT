#ifndef XVT_RUNTIME_SNAPSHOT_RECORDS_H
#define XVT_RUNTIME_SNAPSHOT_RECORDS_H

#include "xvt/flight/flight.h"
#include "xvt/flight/player/player.h"

/* Original 32-bit world records; live game structs stay naturally aligned. */
#pragma pack(push, 1)

typedef struct XvtSnapshotMobileObjectProximityList {
	uint8_t count;
	int32_t score[16];
	uint16_t objIdx[16];
	int32_t overflowScore;
} XvtSnapshotMobileObjectProximityList;

typedef struct XvtSnapshotAiController {
	uint8_t currentOrderSlot;
	AiOrderScratch orderScratch;
	uint8_t orderStateFlag;
	uint8_t pendingPlanId;
	uint8_t currentPlanId;
	uint8_t waypointIndex;
	uint8_t savedPlanId;
	int32_t thinkInterval;
	int32_t thinkTimer;
	int16_t savedRandSeed;
	uint16_t targetObjIdx;
	uint16_t targetSignature;
	uint16_t targetComponent;
	uint8_t hasLiveTarget;
	int32_t aimPointX;
	int32_t aimPointY;
	int32_t aimPointZ;
	uint16_t candidateTargetIdx;
	uint8_t escortTargetFG;
	uint16_t targetZAngle;
	uint16_t targetRoll;
	uint16_t targetXYAngle;
	AiManeuverMode maneuverMode;
	uint8_t maneuverPhase;
	int32_t maneuverTimer;
	int16_t aiPlanState;
} XvtSnapshotAiController;

typedef struct XvtSnapshotAiFlightState {
	uint16_t threatObjIdx;
	uint16_t impactObjIdx;
	uint8_t goHomeFlag;
	uint8_t missionAbortedFlag;
	uint8_t departTimerFlag;
	uint8_t departClockHours;
	uint8_t departClockMinutes;
	uint8_t departClockSeconds;
	uint8_t maneuverCounter;
	uint8_t reactionTimer;
	uint8_t boardedAccountingDone;
	uint8_t orderActionCounter;
	uint8_t orderActionFlag;
	uint8_t objSignatureCount;
	uint16_t objSignatures[10];
	int16_t maxSpeedCache;
	int16_t motionScale;
	uint8_t climbState;
	uint8_t diveState;
	int16_t pitchRate;
	int16_t pitchAccel;
	uint8_t headingState;
	uint8_t headingForce;
	uint16_t headingStep;
	int16_t rollRate;
	int16_t rollAccel;
	uint8_t enterFlag;
	uint16_t rollStep;
	int16_t turnRate;
	int16_t turnAccel;
	uint8_t turnState;
	int16_t turnStep;
	uint8_t formationType;
	uint8_t separation;
} XvtSnapshotAiFlightState;

typedef struct XvtSnapshotCraftDamageStats {
	uint16_t lastSystemHitTime;
	int32_t damageReceivedTotal;
	int32_t damageReceivedByPlayerOwnedCraft;
	int32_t damageFromCollision;
	int32_t damageFromStarship;
	int32_t damageFromMine;
	int32_t damageFromFlightGroupAmount[48];
	int32_t damageFromPlayer[8];
	int32_t damageFromAiSkill[6];
	uint16_t installedHudFeatureMask;
	uint16_t activeHudFeatureMask;
} XvtSnapshotCraftDamageStats;

typedef struct XvtSnapshotPlayerViewState {
	int32_t savedTargetX;
	int32_t savedTargetY;
	int32_t savedTargetZ;
	uint16_t cameraFocusObjIdx;
	uint16_t aimTargetIdx;
	int16_t viewPitch;
	int16_t viewYaw;
	int16_t viewRoll;
	int16_t viewAngleD;
	int16_t hudAimX;
	int16_t hudAimY;
	uint8_t hudStateLive;
	uint8_t hudStateMirror;
	uint8_t hudAimXSnapState;
	uint8_t savedHudStateByte;
	uint8_t field_20;
	int16_t savedHudAimX;
	int16_t savedHudAimY;
	int16_t playerInputBlocked;
	int16_t cameraDistanceStep;
	uint16_t externalCameraActive;
	int32_t cameraDistance;
	int16_t transitionTimer;
	int16_t cameraRollHistory[60];
	int16_t cameraPitchHistory[60];
	int16_t cameraYawHistory[60];
	uint16_t field_199;
} XvtSnapshotPlayerViewState;

typedef struct XvtSnapshotPlayerNetworkRuntimeTail {
	uint16_t flightResolutionMode;
	int32_t directPlayId;
} XvtSnapshotPlayerNetworkRuntimeTail;

typedef struct XvtSnapshotObjectRecord {
	uint16_t objectSignature;
	uint8_t genusId;
	uint8_t objectType;
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
	int16_t yaw;
	int16_t pitch;
	int16_t roll;
	uint8_t flightGroupIdx;
	uint16_t typeSpecificWord;
	uint8_t typeSpecificByte[2];
	int32_t playerOwnerIdx;
	uint32_t mobj;
} XvtSnapshotObjectRecord;

typedef struct XvtSnapshotMobileObject {
	uint8_t state;
	uint8_t lightIntensityScale;
	int32_t simStateTimestamp;
	int32_t prevWorldX;
	int32_t prevWorldY;
	int32_t prevWorldZ;
	XvtSnapshotMobileObjectProximityList proximityList;
	int16_t rollImpulseRate;
	uint16_t speed;
	uint16_t speedRemainder;
	uint32_t damageAmount;
	uint16_t lifetimeTimer;
	uint16_t framesAlive;
	uint16_t sourceObjIdx;
	uint8_t sourceObjectType;
	uint8_t iff;
	uint8_t team;
	uint8_t nodeSwitchIndex;
	uint8_t moveVectorDirty;
	int16_t moveX;
	int16_t moveY;
	int16_t moveZ;
	uint8_t orientMatrixDirty;
	int16_t cachedFwdX;
	int16_t cachedFwdY;
	int16_t cachedFwdZ;
	int16_t cachedSideX;
	int16_t cachedSideY;
	int16_t cachedSideZ;
	int16_t cachedUpX;
	int16_t cachedUpY;
	int16_t cachedUpZ;
	uint32_t pWarheadGuidance;
	uint32_t pCraft;
	uint32_t pCharData;
} XvtSnapshotMobileObject;

typedef struct XvtSnapshotCraftData {
	int32_t craftIndexInGroup;
	uint8_t modelIndex;
	uint8_t leader_obj_idx;
	uint8_t field_006;
	CraftObjectKind objectKind;
	uint8_t missionAccountingDone;
	uint16_t aiSkill;
	uint8_t field_00B[2];
	uint16_t pitch;
	uint16_t yaw;
	int16_t breakupPitchRate;
	int16_t breakupYawRate;
	int32_t beamEffectAccum[5];
	uint8_t sFoilState;
	XvtSnapshotAiController aiController;
	uint16_t carriedObjectIndex;
	uint16_t carrierObjIdx;
	uint16_t lastAttackerObjIdx;
	uint16_t lastHitTimestamp;
	XvtSnapshotAiFlightState aiFlight;
	uint8_t waveNumber;
	int32_t pushAccumX;
	int32_t pushAccumY;
	int32_t pushAccumZ;
	uint16_t throttleSpeed;
	uint16_t engineOutputScale;
	int16_t commandedSpeed;
	uint32_t hullDamage;
	uint32_t systemDamageHullThreshold;
	uint32_t hullMax;
	int16_t subsystemDamage;
	XvtSnapshotCraftDamageStats damageStats;
	CraftSubsystemFlag systemFlags;
	CraftSubsystemFlag workingSubsystems;
	int16_t weaponFireInhibitTimer;
	uint8_t unusedMissionFlag;
	uint8_t notDisabledAccountingSuppress;
	uint8_t wasCaptured;
	int8_t attackedByTeam[10];
	uint8_t iffVisibility[10];
	uint8_t boardingState;
	char specialCargoName[16];
	int32_t shieldEnergy[2];
	PowerRechargeLevel shieldRedirect;
	ShieldDistributionMode shieldDistribMode;
	uint8_t cannonClassCount;
	PowerRechargeLevel laserRedirect;
	uint8_t laserSlotCount;
	CraftLaserState laserState;
	uint8_t warheadLauncherCount;
	uint8_t warheadSlotTypeIds[2];
	int8_t warheadLauncherFlags[2];
	int16_t warheadLauncherCooldownTicks[2];
	int16_t warheadLockTicks;
	BeamType beamTypeId;
	PowerRechargeLevel beamLevel;
	uint16_t beamPresent;
	uint8_t beamActive;
	int16_t beamTimer;
	int16_t beamTargetObjIdx;
	CountermeasureType cmTypeId;
	uint8_t cmAmmoCount;
	uint16_t chaffActiveTimer;
	uint16_t cmFireCooldownTimer;
	CraftWeaponStats weaponStats;
	uint8_t field_256[73];
	uint16_t field_29F;
	uint8_t systemDisplaySlotBySystem[DAMAGE_SYSTEM_ID_COUNT];
	uint16_t systemHealth[DAMAGE_SYSTEM_ID_COUNT];
	uint16_t systemTimer[DAMAGE_SYSTEM_ID_COUNT];
	uint8_t componentState[50];
	uint8_t meshRotation[50];
	uint8_t componentHp[50];
	uint16_t playerCommandAvoidTargetObjIdx;
	CraftWeaponSlot weaponSlots[16];
	uint16_t effectiveAiObjectSignature;
	TurretTargetState turretTargetStates[16];
	uint8_t field_3F2[44];
	uint32_t turretObjectLinks[16];
	uint32_t effectiveAiObjectLink;
} XvtSnapshotCraftData;

typedef struct XvtSnapshotMobileObjectCharData {
	uint16_t skillValue;
	uint8_t reserved02[2];
	XvtSnapshotAiController aiController;
	uint8_t reserved40[12];
} XvtSnapshotMobileObjectCharData;

typedef struct XvtSnapshotPlayerData {
	int32_t objectIndex;
	uint32_t boundObjectSignature;
	uint16_t pilotRating;
	int16_t iff;
	int16_t playerIff;
	uint16_t boundFlightGroupIdx;
	uint8_t connectedFlag;
	uint8_t regionSessionId;
	uint8_t boundCraftEngineGlowCount;
	uint8_t mapCameraState;
	uint8_t hyperspacePhase;
	PlayerHyperspaceRuntime hyperspaceRuntime;
	uint8_t targetBoxEnabled;
	int16_t currentTargetObjectIdx;
	int16_t targetCycleStart;
	int16_t targetPresetSlot[4];
	uint8_t missileLockState;
	uint8_t selectedWarhead;
	uint8_t selectedWeaponMode;
	int16_t selectedTargetComponent;
	int16_t targetingState;
	int16_t engineWashSourceObjIdx;
	uint16_t engineWashStrength;
	int16_t throttlePreset[2];
	PowerRechargeLevel laserPreset[2];
	PowerRechargeLevel shieldPreset[2];
	PowerRechargeLevel beamPreset[2];
	PlayerSavedCraftSettings savedCraftSettings;
	uint8_t savedHudViewState;
	uint8_t pendingActionId;
	int16_t pendingActionParam;
	uint16_t pendingActionIssuerPlayerIdx;
	int16_t yawRollSwap;
	int16_t smoothedInputYaw;
	int16_t smoothedInputPitch;
	uint16_t savedKeyMods;
	uint16_t keyModsHoldTimer;
	int32_t hardpointWorldX;
	int32_t hardpointWorldY;
	int32_t hardpointWorldZ;
	int32_t hardpointLocalX;
	int32_t hardpointLocalY;
	int32_t hardpointLocalZ;
	PlayerMissionRuntimeStats missionStats;
	uint16_t warheadsFired;
	PerMissionKills perMissionKills;
	char msgText[50];
	uint8_t msgLength;
	FlightChatRecipientMode msgTypeId;
	XvtSnapshotPlayerViewState viewState;
	XvtSnapshotPlayerNetworkRuntimeTail network;
	int32_t lockstepTimestamp;
	int32_t savedX;
	int32_t savedY;
	int32_t savedZ;
	int16_t savedRoll;
	int16_t savedPitch;
	int16_t savedYaw;
	uint16_t savedLifetimeTimer;
	int16_t savedSpeed;
	int16_t savedSpeedRemainder;
	int16_t savedRollImpulseRate;
	uint16_t savedFieldId;
	uint8_t savedRegion;
	int32_t pendingActionTimer;
	int32_t beamFireCooldownTimer;
	int32_t field_5B5;
	int32_t impactDamageCooldownTime;
} XvtSnapshotPlayerData;

typedef struct XvtSnapshotMissionFlightRuntimeState {
	int32_t teamScores[2][10];
	uint16_t teamKillStats[4][10];
	uint16_t teamFgCounters[2][10][48];
	uint8_t teamFgDesignationCode[10][48];
	uint8_t globalPrimaryGoalStatus;
	uint16_t globalGoalStatusUnused;
	uint8_t globalBonusGoalStatus;
	uint8_t teamGlobalGoalState[10][3];
	uint8_t teamGoalStatus[10][3];
	uint16_t globalGoalTriggerCounts[2][10][3][4];
	int32_t teamMissionCompletionTimeSeconds[10];
	uint8_t teamHasCountableCraft[10];
	uint8_t teamActiveGoalSequence[10];
} XvtSnapshotMissionFlightRuntimeState;

typedef struct XvtSnapshotFlightMissionState {
	uint8_t missionEndPending;
	uint8_t provingGroundsModeActive;
	uint8_t provingGroundsCraftType;
	uint8_t provingGroundsLevel;
	uint32_t provingGroundsScore;
	uint8_t reserved08[2];
	uint16_t provingGroundsCheckpointsPassed;
	uint8_t reserved0C[2];
	uint16_t provingGroundsCheckpointsRemaining;
	uint16_t provingGroundsTargetsDestroyed;
	uint16_t provingGroundsTimeBonus;
	uint8_t difficulty;
	uint8_t collisionsEnabled;
	uint8_t craftJumpingEnabled;
	uint8_t randomVariationEnabled;
	uint8_t reserved18;
	uint8_t locatePlayersEnabled;
	uint8_t aiOpponentsEnabled;
	uint8_t playerFlightGroupWaveMode;
	uint8_t missionTimeLimitMinutes;
	uint8_t teamVictoryTimeLimitMinutes;
	uint8_t teamVictoryTimeLimitStarted;
	uint8_t craftImpactBounceEnabled;
	int32_t connectedPlayerCount;
	int32_t maxConnectedPlayerCountThisMission;
	XvtSnapshotMissionFlightRuntimeState runtime;
	uint8_t messageTriggered[64];
	uint8_t messageDelayCountdown[64];
	int32_t globalUnitCraftCount[11];
} XvtSnapshotFlightMissionState;

#pragma pack(pop)
typedef char xvt_snapshot_size_FlightMissionState[(sizeof(XvtSnapshotFlightMissionState) == 3376) ? 1 : -1];
typedef char xvt_snapshot_size_ObjectRecord[(sizeof(XvtSnapshotObjectRecord) == 35) ? 1 : -1];
typedef char xvt_snapshot_size_MobileObject[(sizeof(XvtSnapshotMobileObject) == 177) ? 1 : -1];
typedef char xvt_snapshot_size_CraftData[(sizeof(XvtSnapshotCraftData) == 1122) ? 1 : -1];
typedef char xvt_snapshot_size_MobileObjectCharData[(sizeof(XvtSnapshotMobileObjectCharData) == 76) ? 1 : -1];
typedef char xvt_snapshot_size_PlayerData[(sizeof(XvtSnapshotPlayerData) == 1469) ? 1 : -1];

void XvtSnapshot_EncodeObjectRecord(XvtSnapshotObjectRecord* record, const ObjectRecord* live);
void XvtSnapshot_DecodeObjectRecord(ObjectRecord* live, const XvtSnapshotObjectRecord* record);
void XvtSnapshot_EncodeMobileObject(XvtSnapshotMobileObject* record, const MobileObject* live);
void XvtSnapshot_DecodeMobileObject(MobileObject* live, const XvtSnapshotMobileObject* record);
void XvtSnapshot_EncodeCraftData(XvtSnapshotCraftData* record, const CraftData* live);
void XvtSnapshot_DecodeCraftData(CraftData* live, const XvtSnapshotCraftData* record);
void XvtSnapshot_EncodeMobileObjectCharData(XvtSnapshotMobileObjectCharData* record,
											const MobileObjectCharData* live);
void XvtSnapshot_DecodeMobileObjectCharData(MobileObjectCharData* live,
											const XvtSnapshotMobileObjectCharData* record);
void XvtSnapshot_EncodePlayerData(XvtSnapshotPlayerData* record, const PlayerData* live);
void XvtSnapshot_DecodePlayerData(PlayerData* live, const XvtSnapshotPlayerData* record);

void XvtSnapshot_EncodeFlightMissionState(XvtSnapshotFlightMissionState* record,
										  const FlightMissionState* live);
void XvtSnapshot_DecodeFlightMissionState(FlightMissionState* live,
										  const XvtSnapshotFlightMissionState* record);

#endif

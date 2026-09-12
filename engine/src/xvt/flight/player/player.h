#ifndef XVT_FLIGHT_PLAYER_PLAYER_H
#define XVT_FLIGHT_PLAYER_PLAYER_H

#include "xvt/flight/craft.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlayerViewState {
	int savedTargetX;
	int savedTargetY;
	int savedTargetZ;
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
	int cameraDistance;
	int16_t transitionTimer;
	int16_t cameraRollHistory[60];
	int16_t cameraPitchHistory[60];
	int16_t cameraYawHistory[60];
	uint16_t field_199;
};

struct PlayerHyperspaceRuntime {
	unsigned int phaseElapsedTicks;
};

struct PlayerSavedCraftSettings {
	uint16_t throttleSpeed;
	PowerRechargeLevel laserRedirect;
	PowerRechargeLevel shieldRedirect;
	PowerRechargeLevel beamLevel;
	ShieldDistributionMode shieldDistribMode;
	uint8_t laserLinkMode[2];
	uint8_t warheadLauncherFlags[2];
};

struct PlayerMissionRuntimeStats {
	int missionScore;
	int ratingPromoPoints;
	int worseRatingPromoPoints;
	int field_0C;
	int field_10;
	int field_14;
	uint16_t laserShotsFired;
	uint16_t laserHitsScored;
	uint16_t ionShotsFired;
	uint16_t ionHitsScored;
};

#pragma pack(push, 1)

struct PerMissionKills {
	uint16_t warheadHits;
	uint16_t numCraftInspected;
	uint16_t numSpecialInspected;
	uint16_t killsFullOnFlightGroup[48];
	uint16_t killsSharedOnFlightGroup[48];
	uint16_t killsAssistOnFlightGroup[48];
	uint16_t killsFullOnPlayerRating[25];
	uint16_t killsSharedOnPlayerRating[25];
	uint16_t killsAssistOnPlayerRating[25];
	uint16_t killsFullOnAiRating[6];
	uint16_t killsSharedOnAiRating[6];
	uint16_t killsAssistOnAiRating[6];
	uint16_t killsFullOnPlayer[8];
	uint16_t killsSharedOnPlayer[8];
	uint16_t friendliesKilled;
	uint16_t totalCraftLosses;
	uint16_t lossesByCollisions;
	uint16_t lossesByStarships;
	uint16_t lossesByMines;
	uint16_t killsFullFromPlayer[8];
	uint16_t killsSharedFromPlayer[8];
	uint16_t killsFullFromFlightGroup[48];
	uint16_t killsSharedFromFlightGroup[48];
	uint16_t killedByPlayerRating[25];
	uint16_t killedByAiRating[6];
};

#pragma pack(pop)
typedef char xvt_size_PerMissionKills[(sizeof(PerMissionKills) == 808) ? 1 : -1];

/* Stored as int8_t in the binary (IDB enum FlightChatRecipientMode). */
typedef int8_t FlightChatRecipientMode;

enum {
	FLIGHT_CHAT_RECIPIENT_INACTIVE = 0x0,
	FLIGHT_CHAT_RECIPIENT_TEAM = 0x1,
	FLIGHT_CHAT_RECIPIENT_ENEMY = 0x2,
	FLIGHT_CHAT_RECIPIENT_ALL = 0x3,
};

struct PlayerNetworkRuntimeTail {
	uint16_t flightResolutionMode;
	int directPlayId;
};

struct PlayerData {
	int objectIndex;
	unsigned int boundObjectSignature;
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
	int hardpointWorldX;
	int hardpointWorldY;
	int hardpointWorldZ;
	int hardpointLocalX;
	int hardpointLocalY;
	int hardpointLocalZ;
	PlayerMissionRuntimeStats missionStats;
	uint16_t warheadsFired;
	PerMissionKills perMissionKills;
	char msgText[50];
	uint8_t msgLength;
	FlightChatRecipientMode msgTypeId;
	PlayerViewState viewState;
	PlayerNetworkRuntimeTail network;
	int lockstepTimestamp;
	int savedX;
	int savedY;
	int savedZ;
	int16_t savedRoll;
	int16_t savedPitch;
	int16_t savedYaw;
	uint16_t savedLifetimeTimer;
	int16_t savedSpeed;
	int16_t savedSpeedRemainder;
	int16_t savedRollImpulseRate;
	uint16_t savedFieldId;
	uint8_t savedRegion;
	int pendingActionTimer;
	int beamFireCooldownTimer;
	int field_5B5;
	int impactDamageCooldownTime;
};

struct PlayerFlightTransientTimers {
	uint16_t readyMessagePaneTimer;
	uint16_t shieldHitFlashTimer;
	uint16_t hullHitFlashTimer;
	uint16_t systemMessagePaneTimer;
	uint16_t flightGroupMessagePaneTimer;
	uint16_t targetDescriptionRefreshTimer;
	uint16_t mfdCraftListRefreshTimer;
	uint16_t missionGoalsRefreshTimer; ///< Two-second countdown; expiry forces a mission-goals MFD redraw.
};

extern PlayerFlightTransientTimers g_playerFlightTransientTimers[8];
extern int g_localPlayer;
extern PlayerData g_players[8];
extern char g_playerTauntText[8][4][70];

struct RemotePlayerRenderSample {
	int valid;
	uint16_t objectSignature;
	int worldX;
	int worldY;
	int worldZ;
	int rollDelta;
	int pitchDelta;
	int yawDelta;
	int16_t roll;
	int16_t pitch;
	int16_t yaw;
	int16_t moveX;
	int16_t moveY;
	int16_t moveZ;
	uint16_t speedMagnitude;
	int simStateTimestamp;
};

struct RemotePlayerSavedRenderPose {
	int valid;
	char gap4[2];
	int worldX;
	int worldY;
	int worldZ;
	char gap18[12];
	int16_t roll;
	int16_t pitch;
	int16_t yaw;
	char gap36[12];
};

int Player_BindToAvailableCraft(int playerIdx, uint32_t previousObjectIdx, int preferredObjectSignature,
								int resetTargetingState);
int Player_UnbindFromCurrentCraft(int playerIndex, int requireMultipleCraft, int assignAiPlan);
void Player_SaveCraftSettings(int playerIndex);
void Player_UpdateFlightControlsAndCamera(int playerIdx);
void FlightChat_HandleInput(int playerIdx);
int16_t Player_FindNearestObjective(int goalType, int playerIdx);
int Player_ScaleControlStepByElapsedTicks(int16_t step);
void Player_TransferShieldBankEnergy(uint16_t dstBank, uint16_t srcBank, int playerIdx);
void Player_UpdateHudViewForCameraFocus(int playerIdx);
uint16_t Player_PickTargetInSight(int playerIdx);
uint16_t Player_CycleTargetAnyIFF(uint16_t currentObjIdx, int16_t direction, int playerIdx);
uint16_t Player_CycleTarget(uint16_t currentObjIdx, int16_t direction, int playerIdx, int iffFilter,
							int targetFlags);
void Player_SetTarget(int newTargetObjIdx, int playerIdx);
uint16_t Player_SelectTargetComponentMesh(uint16_t targetObjIdx, unsigned int playerIdx);
int16_t USER_calcdeltapitch(int16_t angleQ16, int16_t yawAngleQ16, uint16_t objectIndex, CraftData* craft);
int16_t Player_CanRadioCommandCraft(int objectIndex);
void Player_IssueAiWingmanTargetOrder(uint16_t targetObjIdx, uint16_t commandId, uint16_t responseIndex,
									  int playerIdx);
int16_t Player_FindAttackerOfTarget(uint16_t targetObjIdx, int16_t excludedObjIdx);
void Player_StartPostDestructionState(int playerIdx, unsigned int sourceObjectIndex, int sourcePlayerIdx);
void Player_AppendKillMessageActorName(int slot, char* text, int objectIndex);
void Player_ComputePolarToObjectRef(int playerIdx, unsigned int objectRef);
void Player_EndFlightParticipation(int playerIdx);
void Player_EmitRemotePlayerDepartedMessages(int playerIdx);
void Player_ValidateCurrentTargets(int playerIdx);
void Player_ValidateAllCurrentTargets(void);
int Player_HasAvailableOwnedCraft(int playerIdx);
void Player_UpdateParticipationState(void);
int Player_FindNearestEnemyFighter(int playerIdx, int excludedObjectIdx);
void Player_HandleHyperspaceCommand(struct CraftData* craft, unsigned int playerIdx);

#ifdef __cplusplus
}
#endif

#endif

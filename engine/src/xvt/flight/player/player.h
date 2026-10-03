#ifndef XVT_FLIGHT_PLAYER_PLAYER_H
#define XVT_FLIGHT_PLAYER_PLAYER_H

#include "xvt/flight/craft.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlayerViewState {
	/* Camera world X for the player's view or map; many functions write it,
	 * chiefly the view, map and camera control code. */
	int cameraWorldX;
	int cameraWorldY; /* Camera world Y; kept like cameraWorldX. */
	/* Camera world Z, also the map camera's height; kept like
	 * cameraWorldX. */
	int cameraWorldZ;
	/* Object the camera follows: the player's craft in the cockpit, the
	 * target on the target camera; UINT16_MAX for a free camera. */
	uint16_t cameraFocusObjIdx;
	/* Object a map-camera key keeps the free camera looking at, UINT16_MAX
	 * for none; zooming moves toward it and keeps clear of it. */
	uint16_t aimTargetIdx;
	/* View pitch, 65,536 units a circle; the view and map code set the
	 * three view angles. */
	int16_t viewPitch;
	int16_t viewYaw;  /* View yaw; kept like viewPitch. */
	int16_t viewRoll; /* View roll; kept like viewPitch. */
	/* Fourth angle FlightView_UpdatePlayerCamera passes to
	 * FVIEW_BuildCameraOrient; no game code writes it. */
	int16_t viewAngleD;
	/* Look offset of the view from the craft's nose, passed to
	 * FVIEW_BuildCameraOrient; input, view keys and the map move it. */
	int16_t hudAimX;
	int16_t hudAimY; /* Second look offset; kept like hudAimX. */
	/* HUD view the player shows (HUD_VIEW_ value), set first by
	 * Hud_SetHudViewState; Hud_ForcePlayerViewState sets 0xFF to force a
	 * change. */
	uint8_t hudStateLive;
	/* Copy of hudStateLive made at the end of Hud_SetHudViewState, so it
	 * holds the previous view while the display is rebuilt. */
	uint8_t hudStateMirror;
	/* 0 or 8; the keypad 0 key flips it and sets hudAimX to it shifted left
	 * 10. */
	uint8_t hudAimXSnapState;
	/* hudStateLive kept when an outside camera takes over from the cockpit;
	 * Player_UpdateHudViewForCameraFocus restores it. */
	uint8_t savedHudStateByte;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t field_20;
	/* hudAimX kept with savedHudStateByte and restored with it. */
	int16_t savedHudAimX;
	/* hudAimY kept with savedHudStateByte and restored with it. */
	int16_t savedHudAimY;
	/* Nonzero while the stick drives the camera instead of the craft, as
	 * after the craft is lost or the player is out of the mission. */
	int16_t playerInputBlocked;
	/* Zoom step Player_UpdateFlightControlsAndCamera sets for the camera's
	 * mode, shrinking back to 32 without zoom keys. */
	int16_t cameraDistanceStep;
	/* 1 while the view is outside the cockpit. */
	uint16_t externalCameraActive;
	/* Distance of the outside camera from what it follows: 1024 on binding,
	 * changed by the zoom and camera keys. */
	int cameraDistance;
	/* Despite the name, a flag that never counts: 1 while the target camera
	 * is on, set by its key in Flight_ProcessPlayerActions, else 0. */
	int16_t transitionTimer;
	/* Player_UpdateHudViewForCameraFocus fills all 60 with viewRoll; no
	 * game code reads it. */
	int16_t cameraRollHistory[60];
	/* Filled like cameraRollHistory with viewPitch; no game code reads
	 * it. */
	int16_t cameraPitchHistory[60];
	/* Filled like cameraRollHistory with viewYaw; no game code reads it. */
	int16_t cameraYawHistory[60];
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint16_t field_199;
};

struct PlayerHyperspaceRuntime {
	/* Ticks since the current hyperspace jump stage began:
	 * FlightObject_UpdatePlayerHyperspaceTransition adds g_elapsedTicks and
	 * sets 0 between stages; Player_HandleHyperspaceCommand sets 0. */
	unsigned int phaseElapsedTicks;
};

struct PlayerSavedCraftSettings {
	/* The last craft's throttleSpeed, restored on binding when the
	 * signature matched or a previous craft was given. */
	uint16_t throttleSpeed;
	/* The last craft's laser recharge level. */
	PowerRechargeLevel laserRechargeLevel;
	/* The last craft's shield recharge level. */
	PowerRechargeLevel shieldRechargeLevel;
	PowerRechargeLevel beamLevel; /* The last craft's beamRechargeLevel. */
	/* The last craft's shield distribution. */
	ShieldDistributionMode shieldDistribMode;
	/* The last craft's laser link mode per group. */
	uint8_t laserLinkMode[2];
	/* The low two bits of the last craft's launcher flags. */
	uint8_t warheadLauncherFlags[2];
};

struct PlayerMissionRuntimeStats {
	/* Mission score: kills and goals add to it, friendly kills and
	 * penalties take from it; zeroed at spawn when a craft is bound to the
	 * player. */
	int missionScore;
	/* Promotion points this mission, the Better total of
	 * Mission_CreditDestructionDamageContributors, less 500 for each
	 * friendly destroyed. */
	int ratingPromoPoints;
	/* The Worse total of Mission_CreditDestructionDamageContributors plus 4
	 * a destroyed mine; the career takes them only while its own worse
	 * total is below half the promotion threshold. */
	int worseRatingPromoPoints;
	int field_0C; /* Set to 0 at spawn; nothing reads it. */
	int field_10; /* Set to 0 at spawn; nothing reads it. */
	/* Place among the teams in which the player's team met its primary
	 * goal, set by Mission_UpdateLogic; nothing reads it. */
	int primaryGoalFinishPlace;
	/* Laser shots this player fired (laser_firelasersystem); the career
	 * adds them. */
	uint16_t laserShotsFired;
	/* Laser hits this player scored (Mission_RecordProjectileHitStats); the
	 * career adds them. */
	uint16_t laserHitsScored;
	/* Ion shots this player fired; kept like laserShotsFired. */
	uint16_t ionShotsFired;
	/* Ion hits this player scored; kept like laserHitsScored. */
	uint16_t ionHitsScored;
};

#pragma pack(push, 1)

struct PerMissionKills {
	/* Warhead hits this player scored
	 * (Mission_RecordProjectileHitStats). */
	uint16_t warheadHits;
	/* Craft this player inspected (collide_collisions); nothing reads
	 * it. */
	uint16_t numCraftInspected;
	/* Special cargo craft this player inspected (collide_collisions). */
	uint16_t numSpecialInspected;
	/* Kills by victim flight group where this player did at least 0xAAAA /
	 * 65,536 of the damage (Mission_CreditPlayerKillContribution). */
	uint16_t killsFullOnFlightGroup[48];
	/* Kills by victim flight group where this player did at least 0x5999 /
	 * 65,536 of the damage. */
	uint16_t killsSharedOnFlightGroup[48];
	/* Kills by victim flight group where this player did at least 0x0CCC /
	 * 65,536 of the damage. */
	uint16_t killsAssistOnFlightGroup[48];
	/* Full kills of craft flown by players, by the victim's pilotRating. */
	uint16_t killsFullOnPlayerRating[25];
	/* Shared kills of craft flown by players, by the victim's
	 * pilotRating. */
	uint16_t killsSharedOnPlayerRating[25];
	/* Assists on craft flown by players, by the victim's pilotRating. */
	uint16_t killsAssistOnPlayerRating[25];
	/* Full kills of AI craft, by the victim flight group's AI level. */
	uint16_t killsFullOnAiRating[6];
	/* Shared kills of AI craft, by AI level. */
	uint16_t killsSharedOnAiRating[6];
	/* Assists on AI craft, by AI level. */
	uint16_t killsAssistOnAiRating[6];
	/* Full kills of each other player's craft. */
	uint16_t killsFullOnPlayer[8];
	/* Shared kills of each other player's craft. */
	uint16_t killsSharedOnPlayer[8];
	/* Craft of its own or an allied team this player destroyed with at
	 * least a shared part of the damage. */
	uint16_t friendliesKilled;
	/* Craft this player lost (Mission_RecordPlayerCraftLoss). */
	uint16_t totalCraftLosses;
	/* Losses where collisions did the most damage. */
	uint16_t lossesByCollisions;
	/* Losses where starships did the most damage. */
	uint16_t lossesByStarships;
	uint16_t lossesByMines; /* Losses where mines did the most damage. */
	/* Full kills each other player made on this player's craft. */
	uint16_t killsFullFromPlayer[8];
	/* Shared kills each other player made on this player's craft. */
	uint16_t killsSharedFromPlayer[8];
	/* Losses of this player's craft credited in full to a flight group
	 * (Mission_CreditDestructionDamageContributors). */
	uint16_t killsFullFromFlightGroup[48];
	/* Losses of this player's craft with shared credit to a flight
	 * group. */
	uint16_t killsSharedFromFlightGroup[48];
	/* This player's craft fully killed by players, by the killer's
	 * pilotRating. */
	uint16_t killedByPlayerRating[25];
	/* This player's craft fully killed by AI craft, by the killer's AI
	 * level. */
	uint16_t killedByAiRating[6];
};

#pragma pack(pop)
typedef char
	xvt_size_PerMissionKills[(sizeof(PerMissionKills) == 808) ? 1 : -1];

/* Stored as int8_t in the binary (IDB enum FlightChatRecipientMode). */
typedef int8_t FlightChatRecipientMode;

enum {
	FLIGHT_CHAT_RECIPIENT_INACTIVE = 0x0,
	FLIGHT_CHAT_RECIPIENT_TEAM = 0x1,
	FLIGHT_CHAT_RECIPIENT_ENEMY = 0x2,
	FLIGHT_CHAT_RECIPIENT_ALL = 0x3,
};

struct PlayerNetworkRuntimeTail {
	/* Flight resolution the player runs, exchanged at flight start; the HUD
	 * reads it. */
	uint16_t flightResolutionMode;
	/* The player's DirectPlay id, 0 for an empty slot; the career commit
	 * matches players by it. */
	int directPlayId;
};

struct PlayerData {
	/* Object slot of the craft the player flies, -1 for none. */
	int objectIndex;
	/* Signature of the bound craft; a different one in its slot means the
	 * craft is gone. */
	unsigned int boundObjectSignature;
	/* The player's rating, exchanged at flight start (g_pilotData.rating
	 * when alone); weighs kills and the kill tables. */
	uint16_t pilotRating;
	/* IFF of the flight group the player gets, set by Mission_Init. */
	int16_t iff;
	/* Team of the flight group the player gets, set by Mission_Init. */
	int16_t team;
	/* Flight group of the craft bound at spawn. */
	uint16_t boundFlightGroupIdx;
	/* 0 not in the flight, 1 flying, 2 out of the mission. */
	uint8_t participationState;
	/* 1 after the craft is destroyed, until the player is bound to another
	 * or leaves. */
	uint8_t awaitingNewCraft;
	/* The bound model's engineGlowCount, set at spawn; nothing reads it. */
	uint8_t boundCraftEngineGlowCount;
	/* Map camera, 0 off: bit 7 set while it opens or is open, the low 7
	 * bits a count up to 0x7F that rises by elapsed ticks with bit 7 set
	 * and falls toward 1 without it; 0xFF when watching after leaving the
	 * mission. */
	uint8_t mapCameraState;
	/* Hyperspace jump stage: 0 none, 1 lining up, 2 leaving. */
	uint8_t hyperspacePhase;
	/* Timing for the hyperspace jump. */
	PlayerHyperspaceRuntime hyperspaceRuntime;
	/* 1 to draw target boxes; only ever set to 1, at flight start and on a
	 * reset bind. */
	uint8_t targetBoxEnabled;
	/* Object the player has targeted, -1 for none; Player_SetTarget sets it
	 * and Player_ValidateCurrentTargets drops it. */
	int16_t currentTargetObjectIdx;
	/* Object the cycle keys start from without a target: the target last
	 * dropped, -1 after binding. */
	int16_t targetCycleStart;
	/* Stored targets, -1 empty: Shift+F5 to F7 store the current target in
	 * slots 0 to 2, F5 to F7 recall it; a slot is cleared when its object's
	 * outcome is recorded. Slot 3 is never stored. */
	int16_t targetPresetSlot[4];
	/* Warhead lock shown on the HUD: 0 none, 1 locking, 2 locked; cleared
	 * on a new target or craft. */
	uint8_t missileLockState;
	/* Cannon group or launcher selected, cycled by the W key. */
	uint8_t selectedWeaponBank;
	/* 0 with cannons selected, 1 with warheads. */
	uint8_t selectedWeaponMode;
	/* Mesh of the current target aimed at; Craft_DamageComponent moves it
	 * on when that one is destroyed. */
	int16_t selectedTargetComponent;
	/* -1 at flight start and on binding, 0 when a target is dropped;
	 * nothing reads it. */
	int16_t targetingState;
	/* Object whose engine wash strikes the player's craft, -1 for none:
	 * collide_collisions clears it at each check and
	 * collide_ApplyEngineWashDamage keeps the strongest; the sound code
	 * plays it. */
	int16_t engineWashSourceObjIdx;
	/* Strength of that wash; the sound uses a tenth of it. */
	uint16_t engineWashStrength;
	/* Throttle kept by Shift+9 or Shift+0 and restored by 9 or 0 with the
	 * three presets below; at spawn 21845 and full. */
	int16_t throttlePreset[2];
	/* Laser recharge level of each preset: maintenance and increased at
	 * spawn. */
	PowerRechargeLevel laserPreset[2];
	/* Shield recharge level of each preset: maintenance at spawn. */
	PowerRechargeLevel shieldPreset[2];
	/* Beam recharge level of each preset: maintenance at spawn. */
	PowerRechargeLevel beamPreset[2];
	/* Settings of the last craft flown, written by Player_SaveCraftSettings
	 * and restored on binding. */
	PlayerSavedCraftSettings savedCraftSettings;
	/* View the cockpit returns to, HUD only or forward; the view key sets
	 * it, forward at flight start. */
	uint8_t savedHudViewState;
	/* Request or order awaiting the player's answer, by id 1 to 9, 0 for
	 * none; Hud_UpdateFlightMessagePanes clears it when pendingActionTimer
	 * runs out. */
	uint8_t pendingActionId;
	int16_t pendingActionParam; /* Object the pending request names. */
	/* Player who made the pending request. */
	uint16_t pendingActionIssuerPlayerIdx;
	/* 1 while yaw input rolls the craft, as of the last controls update; a
	 * change clears the input smoothing. */
	int16_t yawRollSwap;
	/* Yaw input eased toward the stick by
	 * Player_UpdateFlightControlsAndCamera, before scaling by elapsed
	 * ticks. */
	int16_t smoothedInputYaw;
	/* Pitch input eased like smoothedInputYaw. */
	int16_t smoothedInputPitch;
	/* g_flightKeyMods at the player's last update, to spot the target key's
	 * press and release. */
	uint16_t savedKeyMods;
	/* Ticks the target key has been held; a tap shorter than
	 * TARGET_TAP_MAX_TICKS picks a target. */
	uint16_t keyModsHoldTimer;
	/* Offset, in world axes, from the craft to its primary hardpoint,
	 * recomputed as the craft moves; the view uses it. */
	int hardpointWorldX;
	/* Second hardpoint offset; kept like hardpointWorldX. */
	int hardpointWorldY;
	/* Third hardpoint offset; kept like hardpointWorldX. */
	int hardpointWorldZ;
	int prevHardpointWorldX; /* hardpointWorldX before its last update. */
	int prevHardpointWorldY; /* hardpointWorldY before its last update. */
	int prevHardpointWorldZ; /* hardpointWorldZ before its last update. */
	/* Score, promotion points and shots for this mission. */
	PlayerMissionRuntimeStats missionStats;
	/* Warheads this player fired this mission (laser_firemissile). */
	uint16_t warheadsFired;
	/* Kill and loss tallies for this mission. */
	PerMissionKills perMissionKills;
	char msgText[50];  /* Chat line being typed, followed by a cursor _. */
	uint8_t msgLength; /* Characters typed into msgText, at most 48. */
	/* Who a chat line goes to; inactive when not typing. */
	FlightChatRecipientMode chatRecipientMode;
	PlayerViewState viewState;	  /* Camera and HUD view state. */
	PlayerNetworkRuntimeTail network; /* Network identity and resolution. */
	/* Game time of the last input frame applied to the player's craft
	 * (Flight_AdvanceOneStep, XvtFlightSim_Advance in the modern build); 0
	 * at flight start. */
	int lockstepTimestamp;
	/* World X of the player's craft after that frame, kept to be put back
	 * before later frames are replayed. */
	int savedX;
	int savedY;		      /* World Y kept like savedX. */
	int savedZ;		      /* World Z kept like savedX. */
	int16_t savedRoll;	      /* Roll kept like savedX. */
	int16_t savedPitch;	      /* Pitch kept like savedX. */
	int16_t savedYaw;	      /* Yaw kept like savedX. */
	uint16_t savedLifetimeTimer;  /* lifetimeTimer kept like savedX. */
	int16_t savedSpeed;	      /* Speed kept like savedX. */
	int16_t savedSpeedRemainder;  /* speedRemainder kept like savedX. */
	int16_t savedRollImpulseRate; /* rollImpulseRate kept like savedX. */
	/* Signature of the craft the saved state belongs to; a mismatch skips
	 * restoring it. */
	uint16_t savedObjectSignature;
	/* awaitingNewCraft when the state was saved; a mismatch skips restoring
	 * it. */
	uint8_t savedAwaitingNewCraft;
	/* Ticks left to answer the pending request, 1416 when made;
	 * Flight_UpdateTimers counts it down. */
	int pendingActionTimer;
	/* Ticks before the beam drains charge again, BEAM_FIRE_COOLDOWN_TICKS
	 * after each drain; Flight_UpdateTimers counts it down. */
	int beamFireCooldownTimer;
	int field_5B5; /* Set to 0 at flight start; no game code reads it. */
	/* g_gameTime at which collide_collisions next checks engine wash; 0 at
	 * flight start. */
	int nextEngineWashCheckTime;
};

struct PlayerFlightTransientTimers {
	/* Ticks the ready message pane shows its message: 354, 1416 or 1652 by
	 * kind. */
	uint16_t readyMessagePaneTimer;
	/* Ticks the shield hit flash shows, SHIELD_HIT_FLASH_TICKS after a
	 * shield hit. */
	uint16_t shieldHitFlashTimer;
	/* Ticks the hull hit flash shows; each hull hit adds
	 * HULL_HIT_FLASH_TICKS. */
	uint16_t hullHitFlashTimer;
	/* Ticks the system message pane shows its message, 472 or 1888;
	 * Hud_ClearReadyMessageQueue sets 0. */
	uint16_t systemMessagePaneTimer;
	/* Ticks the flight group message pane shows its message, 1888 when
	 * set. */
	uint16_t flightGroupMessagePaneTimer;
	/* Ticks before the target description is rebuilt, 1180 when set. */
	uint16_t targetDescriptionRefreshTimer;
	/* Ticks before the craft list page redraws, REFRESH_TICKS when set. */
	uint16_t mfdCraftListRefreshTimer;
	uint16_t
		missionGoalsRefreshTimer; ///< Two-second countdown; expiry forces a mission-goals MFD redraw.
};

extern PlayerFlightTransientTimers g_playerFlightTransientTimers[8];
extern int g_localPlayer;
extern PlayerData g_players[8];
extern char g_playerTauntText[8][4][70];

struct RemotePlayerRenderSample {
	/* 1 while the sample holds the remote craft as last drawn;
	 * FlightSync_CaptureSamplesAndRestorePoses sets it. */
	int valid;
	/* Signature of the sampled craft; smoothing skips a craft whose
	 * signature differs. */
	uint16_t objectSignature;
	int worldX; /* World X the craft was drawn at. */
	int worldY; /* World Y the craft was drawn at. */
	int worldZ; /* World Z the craft was drawn at. */
	/* Change in roll since the previous sample, 0 without one. */
	int rollDelta;
	int pitchDelta; /* Change in pitch since the previous sample. */
	int yawDelta;	/* Change in yaw since the previous sample. */
	int16_t roll;	/* Roll the craft was drawn at. */
	int16_t pitch;	/* Pitch the craft was drawn at. */
	int16_t yaw;	/* Yaw the craft was drawn at. */
	int16_t moveX;	/* The craft's move vector X when sampled. */
	int16_t moveY;	/* Move vector Y when sampled. */
	int16_t moveZ;	/* Move vector Z when sampled. */
	uint16_t speedMagnitude; /* The craft's speed when sampled. */
	/* The craft's simulation time stamp when sampled. */
	int simStateTimestamp;
};

struct RemotePlayerSavedSimPose {
	/* 1 while a simulated pose is saved for
	 * FlightSync_CaptureSamplesAndRestorePoses to put back. */
	int valid;
	char gap4[2];	/* Nothing reads or writes it. */
	int worldX;	/* Simulated world X. */
	int worldY;	/* Simulated world Y. */
	int worldZ;	/* Simulated world Z. */
	char gap18[12]; /* Nothing reads or writes it. */
	int16_t roll;	/* Simulated roll. */
	int16_t pitch;	/* Simulated pitch. */
	int16_t yaw;	/* Simulated yaw. */
	char gap36[12]; /* Nothing reads or writes it. */
};

int Player_BindToAvailableCraft(int playerIdx, uint32_t previousObjectIdx,
				int preferredObjectSignature,
				int resetTargetingState);
int Player_UnbindFromCurrentCraft(int playerIndex, int requireMultipleCraft,
				  int assignAiPlan);
void Player_SaveCraftSettings(int playerIndex);
void Player_UpdateFlightControlsAndCamera(int playerIdx);
void FlightChat_HandleInput(int playerIdx);
int16_t Player_FindNearestObjective(int goalType, int playerIdx);
int Player_ScaleControlStepByElapsedTicks(int16_t step);
void Player_TransferShieldBankEnergy(uint16_t dstBank, uint16_t srcBank,
				     int playerIdx);
void Player_UpdateHudViewForCameraFocus(int playerIdx);
uint16_t Player_PickTargetInSight(int playerIdx);
uint16_t Player_CycleTargetAnyIFF(uint16_t currentObjIdx, int16_t direction,
				  int playerIdx);
uint16_t Player_CycleTarget(uint16_t currentObjIdx, int16_t direction,
			    int playerIdx, int iffFilter, int targetFlags);
void Player_SetTarget(int newTargetObjIdx, int playerIdx);
uint16_t Player_SelectTargetComponentMesh(uint16_t targetObjIdx,
					  unsigned int playerIdx);
int16_t USER_calcdeltapitch(int16_t pitchAngleQ16, int16_t yawAngleQ16,
			    uint16_t objectIndex, CraftData *craft);
int16_t Player_CanRadioCommandCraft(int playerIdx);
void Player_IssueAiWingmanTargetOrder(uint16_t targetObjIdx, uint16_t commandId,
				      uint16_t responseIndex, int playerIdx);
int16_t Player_FindAttackerOfTarget(uint16_t targetObjIdx,
				    int16_t excludedObjIdx);
void Player_StartPostDestructionState(int playerIdx,
				      unsigned int sourceObjectIndex,
				      int sourcePlayerIdx);
void Player_AppendKillMessageActorName(int slot, char *text, int objectIndex);
void Player_ComputePolarToObjectRef(int playerIdx, unsigned int objectRef);
void Player_EndFlightParticipation(int playerIdx);
void Player_EmitRemotePlayerDepartedMessages(int playerIdx);
void Player_ValidateCurrentTargets(int playerIdx);
void Player_ValidateAllCurrentTargets(void);
int Player_HasAvailableOwnedCraft(int playerIdx);
void Player_UpdateParticipationState(void);
int Player_FindNearestEnemyFighter(int playerIdx, int excludedObjectIdx);
void Player_HandleHyperspaceCommand(struct CraftData *craft,
				    unsigned int playerIdx);

#ifdef __cplusplus
}
#endif

#endif

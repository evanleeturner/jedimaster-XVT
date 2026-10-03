#ifndef XVT_FLIGHT_FLIGHT_H
#define XVT_FLIGHT_FLIGHT_H

#include "xvt/flight/mission/mission.h"
#include "xvt/xvt_typedefs.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum FlightSimulationTiming {
	SIMULATION_TICKS_PER_SECOND = 236,
};

extern int g_localTransientSlotStart;
extern int g_localDebrisSlotEnd;

extern uint16_t g_curCraftModelIndex;

extern uint16_t g_elapsedTicks;
extern uint16_t g_simStepsPerSecond;
extern int g_gameTime;
extern int g_singleObjectUpdateOverrideIdx;
extern void *g_flightMainWindowHandle;
extern uint32_t g_dynamicMusicLastUpdateMs;
extern int g_dynamicMusicTrackRemainingMs;
extern uint8_t g_dynamicMusicState;
extern uint8_t g_dynamicMusicOutcomeLatched;

/* The flight's shared countdown timers, in ticks: Flight_UpdateTimers counts
 * each down by the step's ticks to 0, and the code each one paces reloads it
 * when it runs. Saved and restored with the world state. */
struct FlightGlobalCountdownTimers {
	uint16_t unusedTimer00; /* Not used by name. */
	/* Until Mission_UpdateLogic next checks the goals; reloaded with
	 * 236. */
	uint16_t missionGoalEvaluationTimer;
	/* Until FlightObject_UpdateSpecialBehavior next runs its pass; it
	 * reloads it with 29 (SPECIAL_BEHAVIOR_UPDATE_TICKS). */
	uint16_t specialBehaviorUpdateTimer;
	/* Until Mission_UpdateFlightGroupArrivals next checks arrival triggers;
	 * it reloads it with 236, and collide_damagecraft sets 0 when it
	 * destroys a player's craft. */
	uint16_t missionArrivalTriggerScanTimer;
	/* Until Mission_UpdateFlightGroupArrivals next checks arrival delays;
	 * reloaded with 236. */
	uint16_t missionArrivalDelayScanTimer;
	/* Until Mission_UpdateLogic next checks the mission messages; it
	 * reloads it with 1,180 (MISSION_MESSAGE_REFRESH_TICKS). */
	uint16_t missionMessageScanTimer;
	uint16_t unusedTimer0C; /* Not used by name. */
	uint16_t unusedTimer0E; /* Not used by name. */
	uint16_t unusedTimer10; /* Not used by name. */
	uint16_t unusedTimer12; /* Not used by name. */
	/* Until laser_weaponsfire next updates weapon power; reloaded with
	 * 236. */
	uint16_t weaponPowerUpdateTimer;
};

extern FlightGlobalCountdownTimers g_flightGlobalCountdownTimers;

enum FlightLaunchArgument {
	FLIGHT_LAUNCH_ARG_MISSION_PATH,
	FLIGHT_LAUNCH_ARG_FORMAL_NAME,
	FLIGHT_LAUNCH_ARG_PILOT_NAME,
	FLIGHT_LAUNCH_ARG_IS_HOST,
	FLIGHT_LAUNCH_ARG_MP_GAME_NAME,
	FLIGHT_LAUNCH_ARG_UNUSED,
	FLIGHT_LAUNCH_ARG_NUM_PLAYERS,
	FLIGHT_LAUNCH_ARG_COUNT,
};

/* The launch command line split into arguments by Flight_Main
 * (XvtFlightEntry_Prepare in the modern build). */
struct FlightLaunchArgs {
	char *programName; /* Set to "xtie"; nothing reads it. */
	char *sentinel;	   /* Set to "/trebla"; nothing reads it. */
	/* Pointers into the command line, one per FLIGHT_LAUNCH_ARG_ index: the
	 * mission file, the formal name and the player's name, whether this
	 * player hosts, the game name, one nothing reads, and the player
	 * count. */
	char *arguments[FLIGHT_LAUNCH_ARG_COUNT];
};

extern const uint16_t g_graphicsDetailDistanceThresholdByPreset[4];
extern const uint16_t g_starGridDivisorByGraphicsDetailPreset[4];
extern const uint16_t g_backdropsEnabledByGraphicsDetailPreset[4];
extern const uint16_t g_debrisEnabledByGraphicsDetailPreset[4];
extern uint16_t g_graphicsDetailDistanceThreshold;
extern int g_generateMissionPalette;
extern uint8_t g_transformLightDirectionToObjectSpace;
extern uint16_t g_starGridDivisor;
extern uint8_t g_backdropsEnabled;
extern uint8_t g_debrisEnabled;
extern int g_flightSimSideEffectsSuppressed;
extern int g_flightSfxSideEffectGate;
extern uint8_t g_dormantFlightRegionSessionEarlyReturnFlag;
extern uint8_t g_flightAltMToggle;
extern uint8_t g_flightConfSfxEnabled;
extern uint8_t g_flightConfVoiceEnabled;
extern uint16_t g_localBeamTargetObjIdx;
extern const uint16_t g_subsystemIdToFlag[12];
extern const uint8_t g_subsystemMessageArgById[10];
extern const uint16_t g_subsystemRepairDuration[12];
extern const uint16_t g_subsystemFailureHudMaskByRandomSlot[16];

/* The flight's mission state: options taken from the game configuration at
 * flight start, the proving grounds counters, the time limits and the mission's
 * runtime goal state. Saved and restored with the world state and folded into
 * the checksums; the modern build's snapshot code lists every field. */
struct FlightMissionState {
	/* 1 once the mission is to end: by the time limit
	 * (Flight_UpdateTimers), when no connected player is left or the local
	 * player has left (Flight_RecountPlayersAndCheckMissionEnd), on
	 * quitting, and from the network code. The simulation stops stepping at
	 * it. */
	uint8_t missionEndPending;
	/* Nonzero while the mission is the proving grounds course; only
	 * Mission_Init writes it. */
	uint8_t provingGroundsModeActive;
	/* Craft type flown on the proving grounds course: flight start sets 2
	 * for the traincourse launch option, else 0, before the mission
	 * loads. */
	uint8_t provingGroundsCraftType;
	/* Proving grounds level: flight start sets 4 for traincourse, else
	 * 0. */
	uint8_t provingGroundsLevel;
	/* Proving grounds score in points; flight start credits 10,000 for each
	 * level below the starting one. */
	uint32_t provingGroundsScore;
	/* Zeroed by Mission_Init and copied by the modern build's snapshot;
	 * nothing reads it. */
	uint8_t unused08[2];
	/* Course checkpoints passed this level. */
	uint16_t provingGroundsCheckpointsPassed;
	/* Zeroed by Mission_Init and copied by the modern build's snapshot;
	 * nothing reads it. */
	uint8_t unused0C[2];
	/* Course checkpoints left this level. */
	uint16_t provingGroundsCheckpointsRemaining;
	/* Course targets hit, counted by Craft_DamageComponent. */
	uint16_t provingGroundsTargetsDestroyed;
	/* Bonus points for time left when a level is finished, counted up by
	 * ProvingGrounds_UpdateCourse. */
	uint16_t provingGroundsTimeBonus;
	/* Game difficulty for the flight: g_gameConfig.difficulty (easy when
	 * past hard), or medium for a multiplayer combat engagement. */
	uint8_t difficulty;
	/* Collisions option (g_gameConfig.collisions); read by
	 * collide_collisions. */
	uint8_t collisionsEnabled;
	/* Craft jumping option, for the J key (g_gameConfig.craftJumping). */
	uint8_t craftJumpingEnabled;
	/* Random setup option (g_gameConfig.randomSetup), forced to 0 in a
	 * combat engagement sequence; read by the mission setup and
	 * arrivals. */
	uint8_t randomVariationEnabled;
	/* Battle length option (g_gameConfig.battleLengthIndex); set at flight
	 * start, and no flight code reads it here. */
	uint8_t battleLengthIndex;
	/* Locate players option (g_gameConfig.locatePlayers); read by the
	 * targeting, HUD and map code. */
	uint8_t locatePlayersEnabled;
	/* AI opponents option: g_gameConfig.aiOpponents in multiplayer, 1 solo;
	 * Mission_Init also writes it. */
	uint8_t aiOpponentsEnabled;
	/* The g_gameConfig.craftWaves option; read by the flight group arrival
	 * and replacement code. */
	uint8_t playerFlightGroupWaveMode;
	/* Mission time limit in minutes: the multiplayer option, or 255 solo,
	 * which Mission_Init replaces with the mission file's own limit; 0 for
	 * none. Starting the team victory limit replaces it
	 * (Flight_UpdateTimers). */
	uint8_t missionTimeLimitMinutes;
	/* Minutes the mission goes on once one team is left or has met its
	 * goals: g_gameConfig.lastTeamTimeLimitMinutes in multiplayer, 0
	 * solo. */
	uint8_t teamVictoryTimeLimitMinutes;
	/* 1 once Flight_UpdateTimers has started the team victory limit. */
	uint8_t teamVictoryTimeLimitStarted;
	/* Set to 1 at flight start; collide_laserhitcraft reads it. */
	uint8_t craftImpactBounceEnabled;
	/* Players connected: the session's count at flight start, then kept by
	 * Mission_UpdateLogic. */
	int32_t connectedPlayerCount;
	/* Most players connected at once this mission; above 1 it lets the team
	 * victory limit start. */
	int32_t maxConnectedPlayerCountThisMission;
	/* The mission's runtime goal and score state, set up by
	 * Mission_InitFlightRuntimeState. */
	MissionFlightRuntimeState runtime;
	/* Per mission message, 1 once its trigger has fired
	 * (Mission_UpdateLogic). */
	uint8_t messageTriggered[64];
	/* Per mission message, the delay left before it shows, counted down
	 * once per message check (1,180 ticks); set from the message's delay5s
	 * when it triggers. */
	uint8_t messageDelayCountdown[64];
	/* Per global unit, the craft placed so far;
	 * Mission_InitFlightGroupObjectSlot counts up and numbers each craft by
	 * it. */
	int32_t globalUnitCraftCount[11];
};

extern uint8_t *g_worldStateDupBuffer;
extern unsigned int g_worldChecksumRegionLengths[16];
extern uint8_t *g_worldStateBuffer;
extern int g_worldStateDupSize;
extern uint16_t g_worldStateDupHandle;
extern unsigned int g_worldChecksum[16];
extern unsigned int g_worldStateSize;
extern uint16_t g_worldStateHandle;
extern int g_activeFlightPlayerCount;
extern int g_flightPlayerCount;
extern int g_lastLocalReplayInputTimestamp;
extern unsigned int g_flightUpdateDurationHistogram[20];
extern int g_flightPacketDropScore;
extern int g_predictedFrameDelta;
extern int g_flightLastStepTargetTimestamp;
extern int g_flightPrevHostPacketDropCount;
extern int g_flightConfNoPilot;
extern FlightMissionState g_flightMissionState;
extern int g_flightNetBufferWorldMessagesUntilChecksum;
extern unsigned int g_flightNetWorldChecksumEpoch;
extern int g_internetPlayEnabled;
extern uint8_t g_worldStateReservedByte;
extern int g_worldStateDebrisSlotCount;
extern int g_unusedWorldStateSerializedDword;
extern int g_flightConfNewNet;
extern int g_preFlightResolutionMode;
extern const float g_lodConfigMaxValue;
extern const float g_lodConfigScaleFactor;
extern const float g_lodConfigCurveDouble;
extern const float g_lodConfigCurveThreshold;
extern const float g_mipmapConfigScaleFactor;
extern uint8_t g_dynamicMusicInitialStartMinuteChoices[4];
extern uint8_t g_dynamicMusicInitialStartSecondChoices[4];
extern const char g_paiPlanResourceBaseName[8];
extern int g_flightConfTrainCourse;
extern int g_unusedFlightCmdLinePlusSwitchFlag;
extern int g_flightConfNoLauncher;
extern uint8_t g_flightConfMusicEnabled;
extern uint8_t g_unusedFlightRuntimeBlock[768];
extern uint8_t g_flightNoiseTable[512];
extern int g_unusedFlightStartupObjectPassState;
extern uint8_t g_unusedFlightMessageRuntimeState;
extern int g_unusedFlightSessionResetState;
extern XvtFile *g_unusedFlightDebugLogFile;
extern int g_laserFireTimestampTrackingEnabled;
extern int g_unusedFlightTransientResetState;
extern uint8_t g_unusedFlightNetworkBlock[48];
extern PlayerData g_localPlayerSnapshotOnOptionsSyncFailure;
extern int g_flightInProgressLaunch;
extern FlightLaunchArgs g_flightLaunchArgs;
extern int g_flightStartedWithDashArg;
extern uint32_t g_flightSoundInitStartTimeMs;

void Flight_ResetUnusedResumeSlots(void);
void Flight_UpdateTimers(void);
void Flight_UpdateDynamicMusicState(void);
uint8_t *Flight_GetDuplicateWorldStateBuffer(void);
int Flight_GetDuplicateWorldStateSize(void);
void Flight_AllocWorldStateBuffers(void);
void Flight_FreeWorldStateBuffers(void);
void Flight_SaveWorldState(void);
void Flight_RestoreWorldState(void);
size_t Flight_CalculateWorldStateBufferSize(void);
void Flight_ChecksumWorldState(int unusedArg0, int unusedArg1);
int Flight_ComputeWorldStateResyncSegmentSize(int worldStateSize);
int Flight_BuildWorldStateResyncSegmentChecksums(int *outChecksums,
						 uint8_t *worldState,
						 int worldStateSize);
int Flight_BuildWorldStateObjectPresenceMap(uint8_t *outMap,
					    uint8_t *worldState);
void Flight_ApplyWorldStateObjectPresenceMap(const uint8_t *presenceMap);
void Flight_StepSimToTime(int targetGameTime);
void Flight_AdvanceOneStep(int targetGameTime);
void Flight_MainLoop(int unused);
void Flight_RunMissionLoop(void);
int Flight_UpdateActivePlayerCount(void);
int Flight_RecountPlayersAndCheckMissionEnd(void);
int Flight_ComputeLiveWorldStateChecksum(void);
unsigned int Flight_ChecksumBufferRotateXor(const void *data,
					    unsigned int size);

static __inline uint32_t Flight_RotateChecksumLeft(uint32_t checksum)
{
#ifdef XVT_MODERN
	return (checksum << 1) | (checksum >> 31);
#else
	return _rotl(checksum, 1);
#endif
}

void Flight_UpdatePlayerStep(int playerIdx);
void Flight_ProcessPlayerActions(int playerIdx);
char Flight_ApplyGraphicsDetailPreset(uint16_t preset);
#ifndef XVT_MODERN
int WinMain(void *hInstance, void *hPrevInstance, char *lpCmdLine,
	    int nShowCmd);
#endif
int Flight_Main(char *missionCmdLine);
int Flight_UpdateAndFocusMainWindow(void);
int32_t Flight_PumpWindowMessages(void);
int32_t Flight_WndProc(void *hWnd, unsigned int Msg, uint32_t wParam,
		       int32_t lParam);
void Flight_UpdateCraftSteeringAndSpeed(void);
void Flight_SlewObjectSpeedTowardTarget(unsigned int objectIdx, int targetSpeed,
					int allowDecel, int throttleFraction);
void Flight_AccelerateObjectSpeed(int objectIdx, int accelerationPerSecond);
void Flight_DecelerateObjectSpeed(int objectIdx, int decelerationPerSecond);
void Flight_UpdateDivePulloutPitchTarget(int objectIdx);

#ifdef __cplusplus
}
#endif

#endif

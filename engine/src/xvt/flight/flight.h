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
extern uint16_t g_simStepScale;
extern int g_gameTime;
extern int g_singleObjectUpdateOverrideIdx;
extern void* g_flightMainWindowHandle;
extern uint32_t g_dynamicMusicLastUpdateTick;
extern int g_dynamicMusicTrackRemainingMs;
extern uint8_t g_dynamicMusicState;
extern uint8_t g_dynamicMusicOutcomeLatched;

struct FlightGlobalCountdownTimers {
	uint16_t unusedTimer00;
	uint16_t missionGoalEvaluationTimer;
	uint16_t crewMeshRotationUpdateTimer;
	uint16_t missionArrivalTriggerScanTimer;
	uint16_t missionArrivalDelayScanTimer;
	uint16_t missionMessageScanTimer;
	uint16_t unusedTimer0C;
	uint16_t unusedTimer0E;
	uint16_t unusedTimer10;
	uint16_t unusedTimer12;
	uint16_t weaponPowerUpdateTimer;
};

extern FlightGlobalCountdownTimers g_flightGlobalCountdownTimers;

enum FlightLaunchArgument {
	FLIGHT_LAUNCH_ARG_MISSION_PATH,
	FLIGHT_LAUNCH_ARG_SESSION_NAME,
	FLIGHT_LAUNCH_ARG_PILOT_NAME,
	FLIGHT_LAUNCH_ARG_LOCAL_ID,
	FLIGHT_LAUNCH_ARG_MP_GAME_NAME,
	FLIGHT_LAUNCH_ARG_UNUSED,
	FLIGHT_LAUNCH_ARG_NUM_PLAYERS,
	FLIGHT_LAUNCH_ARG_COUNT,
};

struct FlightLaunchArgs {
	char* programName;
	char* sentinel;
	char* arguments[FLIGHT_LAUNCH_ARG_COUNT];
};

extern const uint16_t g_graphicsDetailDistanceThresholdByPreset[4];
extern const uint16_t g_starDensityByGraphicsDetailPreset[4];
extern const uint16_t g_backdropsEnabledByGraphicsDetailPreset[4];
extern const uint16_t g_debrisEnabledByGraphicsDetailPreset[4];
extern uint16_t g_graphicsDetailDistanceThreshold;
extern int g_generateMissionPalette;
extern uint8_t g_transformLightDirectionToObjectSpace;
extern uint16_t g_starDensity;
extern uint8_t g_backdropsEnabled;
extern uint8_t g_debrisEnabled;
extern int g_flightSimSideEffectsSuppressed;
extern int g_flightSfxSideEffectGate;
extern uint8_t g_dormantFlightRegionSessionEarlyReturnFlag;
extern uint8_t g_flightAltLToggle;
extern uint8_t g_flightConfSfxEnabled;
extern uint8_t g_flightConfVoiceEnabled;
extern uint16_t g_localBeamTargetObjIdx;
extern const uint16_t g_subsystemIdToFlag[12];
extern const uint8_t g_subsystemMessageArgById[10];
extern const uint16_t g_subsystemRepairDuration[12];
extern const uint16_t g_subsystemFailureHudMaskByRandomSlot[16];

struct FlightMissionState {
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
	MissionFlightRuntimeState runtime;
	uint8_t messageTriggered[64];
	uint8_t messageDelayCountdown[64];
	int32_t globalUnitCraftCount[11];
};

extern uint8_t* g_worldStateDupBuffer;
extern unsigned int g_peerChecksumRegionLengths[16];
extern uint8_t* g_worldStateBuffer;
extern int worldStateSize;
extern uint16_t g_worldStateDupHandle;
extern unsigned int g_worldChecksum[16];
extern unsigned int g_worldStateSize;
extern uint16_t g_worldStateHandle;
extern int g_activeFlightPlayerCount;
extern int g_flightPlayerCount;
extern int g_lastLocalReplayInputTimestamp;
extern unsigned int g_flightUpdateDurationHistogram[20];
extern int g_flightPingDropScore;
extern int g_predictedFrameDelta;
extern int g_flightLastStepTargetTimestamp;
extern int g_flightPingPrevHostDropCount;
extern int g_flightConfNoPilot;
extern FlightMissionState g_flightMissionState;
extern int g_flightNetBufferWorldMessagesUntilChecksum;
extern unsigned int g_flightNetWorldChecksumEpoch;
extern int g_asyncFlag;
extern uint8_t g_worldStateReservedByte;
extern int g_worldStateReservedDword;
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
extern uint8_t g_flightRuntimeScratch[768];
extern uint8_t g_flightNoiseTable[512];
extern int g_flightStartupObjectPassState;
extern uint8_t g_flightMessageRuntimeState;
extern int g_flightSessionResetState;
extern XvtFile* g_unusedFlightDebugLogFile;
extern int g_laserFireTimestampTrackingEnabled;
extern int g_flightTransientResetState;
extern uint8_t g_flightNetworkRuntimeScratch[48];
extern PlayerData g_localPlayerSnapshotOnFlightExit;
extern int g_flightInProgressLaunch;
extern FlightLaunchArgs g_flightLaunchArgs;
extern int g_flightStartedWithDashArg;
extern uint32_t g_flightSoundInitStartTimeMs;

void Flight_ResetUnusedResumeSlots(void);
void Flight_UpdateTimers(void);
void Flight_UpdateDynamicMusicState(void);
uint8_t* Flight_GetDuplicateWorldStateBuffer(void);
int Flight_GetSerializedWorldStateSize(void);
void Flight_AllocWorldStateBuffers(void);
void Flight_FreeWorldStateBuffers(void);
void Flight_SaveWorldState(void);
void Flight_RestoreWorldState(void);
size_t Flight_CalculateWorldStateBufferSize(void);
void Flight_ChecksumWorldState(int unusedArg0, int unusedArg1);
int Flight_ComputeWorldStateResyncSegmentSize(int worldStateSize);
int Flight_BuildWorldStateResyncSegmentChecksums(int* outChecksums, uint8_t* worldState, int worldStateSize);
int Flight_BuildWorldStateObjectPresenceMap(uint8_t* outMap, uint8_t* worldState);
void Flight_ApplyWorldStateObjectPresenceMap(const uint8_t* presenceMap);
void Flight_StepSimToTime(int targetGameTime);
void Flight_AdvanceOneStep(int targetGameTime);
void Flight_MainLoop(int unused);
void Flight_RunMissionLoop(void);
int Flight_UpdateActivePlayerCount(void);
int Flight_RecountPlayersAndCheckMissionEnd(void);
int Flight_ComputeLiveWorldStateChecksum(void);
unsigned int Flight_ChecksumBufferRotateXor(const void* data, unsigned int size);

static __inline uint32_t Flight_RotateChecksumLeft(uint32_t checksum) {
#ifdef XVT_MODERN
	return (checksum << 1) | (checksum >> 31);
#else
	return _rotl(checksum, 1);
#endif
}

void Flight_UpdateEntity(int playerIdx);
void Flight_ProcessPlayerActions(int playerIdx);
char Flight_ApplyGraphicsDetailPreset(uint16_t preset);
#ifndef XVT_MODERN
int WinMain(void* hInstance, void* hPrevInstance, char* lpCmdLine, int nShowCmd);
#endif
int Flight_Main(char* missionCmdLine);
int Flight_UpdateAndFocusMainWindow(void);
int32_t Flight_PumpWindowMessages(void);
int32_t StubWndProc(void* hWnd, unsigned int Msg, uint32_t wParam, int32_t lParam);
void Flight_UpdateCraftSteeringAndSpeed(void);
void Flight_SlewObjectSpeedTowardTarget(unsigned int objectIdx, int targetSpeed, int allowDecel, int fracQ16);
void Flight_AccelerateHyperspaceSpeed(int objectIdx, int accelerationPerTick);
void Flight_DecelerateHyperspaceSpeed(int objectIdx, int deceleration);
void Flight_UpdateDivePulloutPitchTarget(int objectIdx);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_FRONTEND_FRONTEND_MISSION_H
#define XVT_FRONTEND_FRONTEND_MISSION_H

#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct FrontendMissionHeader {
	uint8_t timeLimitMin;
	uint8_t timeLimitSec;
	uint8_t winType;
	uint8_t backdrop;
	uint8_t rescue;
	uint8_t allWaypointsShown;
	uint8_t variables[8];
	char iffNames[4][20];
	MissionType
		missionType; ///< One-byte mission mode; XVT uses the shared legacy values through SKIRMISH (0..4).
	uint8_t goalsUnimportant; ///< Nonzero suppresses normal mission-goal importance/failure handling.
	uint8_t timeLimitMinutes; ///< Mission countdown duration in whole minutes; zero disables the
							  ///< header-supplied limit.
	uint8_t reserved[61];
};

#pragma pack(pop)
typedef char xvt_size_FrontendMissionHeader[(sizeof(FrontendMissionHeader) == 158) ? 1 : -1];

#pragma pack(push, 1)

struct FrontendMission {
	XvtFlightGroup flightGroups[48];
	MissionMessage messages[64];
	GlobalGoal globalGoals[10][7];
	FrontendMissionHeader header;
	Team teams[10];
	uint16_t flightGroupCount;
	uint16_t messageCount;
	uint16_t field_13DF0;
	uint16_t formatVersion;
};

#pragma pack(pop)
typedef char xvt_size_FrontendMission[(sizeof(FrontendMission) == 81396) ? 1 : -1];

typedef enum FrontendMissionSessionMode {
	FRONTEND_MISSION_SESSION_NONE = 0x0,
	FRONTEND_MISSION_SESSION_SINGLEPLAYER = 0x2,
	FRONTEND_MISSION_SESSION_NET_CLIENT = 0x3,
	FRONTEND_MISSION_SESSION_NET_HOST = 0x4,
} FrontendMissionSessionMode;

extern FrontendMission g_frontendMission;
extern FrontendMissionSessionMode g_frontendMissionSessionMode;

int FrontendMission_LoadForBriefing(void);
void FrontendMission_Reset(void);
void FrontendMission_LoadCurrentMissionData(void);
void FrontendMission_LoadFile(const char* fileName, FrontendMission* outMission);
void FrontendMission_LoadCurrent(void);
void FrontendMission_InitPlayerState(void);

#ifdef __cplusplus
}
#endif

#endif

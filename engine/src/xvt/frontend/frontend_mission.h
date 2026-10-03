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

/* A mission file's header as the frontend loads it: the flight engine's
 * MissionHeader without its two counts. */
struct FrontendMissionHeader {
	uint8_t timeLimitMin; /* Loaded; nothing in the frontend reads it. */
	uint8_t timeLimitSec; /* Loaded; nothing in the frontend reads it. */
	/* Loaded; FrontendMission_InitForBriefing sets 1. Nothing reads it. */
	uint8_t winType;
	uint8_t backdrop; /* Loaded; nothing in the frontend reads it. */
	uint8_t rescue;	  /* Loaded; nothing in the frontend reads it. */
	/* Loaded; FrontendMission_InitForBriefing sets 0. Nothing reads it. */
	uint8_t allWaypointsShown;
	uint8_t variables[8]; /* Loaded; nothing in the frontend reads it. */
	char iffNames[4][20]; /* Loaded; nothing in the frontend reads it. */
	/* Loaded; nothing in the frontend reads it. */
	MissionType
		missionType; ///< One-byte mission mode; XVT uses the shared legacy values through SKIRMISH (0..4).
	/* The debriefing's player statistics add mission result rows only
	 * while this is 0. */
	uint8_t goalsUnimportant; ///< Nonzero suppresses normal mission-goal importance/failure handling.
	/* Loaded; nothing in the frontend reads it. */
	uint8_t timeLimitMinutes; ///< Mission countdown duration in whole minutes; zero disables the
	///< header-supplied limit.
	uint8_t reserved[61]; /* Loaded; nothing reads it. */
};

#pragma pack(pop)
typedef char xvt_size_FrontendMissionHeader
	[(sizeof(FrontendMissionHeader) == 158) ? 1 : -1];

#pragma pack(push, 1)

/* A mission file as the frontend loads it, without its briefings. */
struct FrontendMission {
	/* The mission's flight groups; the first flightGroupCount are
	 * loaded. */
	XvtFlightGroup flightGroups[48];
	/* In-flight messages, each in the slot the file gives before it; only
	 * the loaders touch them. */
	MissionMessage messages[64];
	/* Each team's global goals, as many as the file gives for it; only the
	 * loaders touch them. */
	GlobalGoal globalGoals[10][7];
	FrontendMissionHeader header; /* The file's header. */
	/* Team records; those the file flags absent stay zero. The screens
	 * read their names. */
	Team teams[10];
	/* Flight groups loaded; most readers take it as a signed 16-bit
	 * count. */
	uint16_t flightGroupCount;
	uint16_t messageCount; /* Messages loaded; only the loaders read it. */
	/* Never read or written by name; the loaders' memset leaves it 0. */
	uint16_t unused13DF0;
	/* The file's first word; the loaders accept 12, 13 and 14 and stop
	 * after it otherwise. Only they read it. */
	uint16_t formatVersion;
};

#pragma pack(pop)
typedef char
	xvt_size_FrontendMission[(sizeof(FrontendMission) == 81396) ? 1 : -1];

typedef enum FrontendMissionSessionMode {
	FRONTEND_MISSION_SESSION_NONE = 0x0,
	FRONTEND_MISSION_SESSION_SINGLEPLAYER = 0x2,
	FRONTEND_MISSION_SESSION_NET_CLIENT = 0x3,
	FRONTEND_MISSION_SESSION_NET_HOST = 0x4,
} FrontendMissionSessionMode;

extern FrontendMission g_frontendMission;
extern FrontendMissionSessionMode g_frontendMissionSessionMode;

int FrontendMission_LoadForBriefing(void);
void FrontendMission_InitForBriefing(void);
void FrontendMission_LoadCurrentWithBriefing(void);
void FrontendMission_LoadFile(const char *fileName,
			      FrontendMission *outMission);
void FrontendMission_LoadCurrent(void);
void FrontendMission_InitPlayerState(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_FRONTEND_PILOT_H
#define XVT_FRONTEND_PILOT_H

#include "xvt/assets/file.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as int32_t in the binary (IDB enum PilotRating). */
typedef int32_t PilotRating;

enum {
	PILOT_RATING_TARGET_DRONE = 0x0,
	PILOT_RATING_GROUND_CREW = 0x1,
	PILOT_RATING_TRAINEE = 0x2,
	PILOT_RATING_FLIGHT_CADET = 0x3,
	PILOT_RATING_OFFICER_4TH_CLASS = 0x4,
	PILOT_RATING_OFFICER_3RD_CLASS = 0x5,
	PILOT_RATING_OFFICER_2ND_CLASS = 0x6,
	PILOT_RATING_OFFICER_1ST_CLASS = 0x7,
	PILOT_RATING_VETERAN_4TH_GRADE = 0x8,
	PILOT_RATING_VETERAN_3RD_GRADE = 0x9,
	PILOT_RATING_VETERAN_2ND_GRADE = 0xA,
	PILOT_RATING_VETERAN_1ST_GRADE = 0xB,
	PILOT_RATING_ACE_4TH_LEVEL = 0xC,
	PILOT_RATING_ACE_3RD_LEVEL = 0xD,
	PILOT_RATING_ACE_2ND_LEVEL = 0xE,
	PILOT_RATING_ACE_1ST_LEVEL = 0xF,
	PILOT_RATING_TOP_ACE_4TH_ORDER = 0x10,
	PILOT_RATING_TOP_ACE_3RD_ORDER = 0x11,
	PILOT_RATING_TOP_ACE_2ND_ORDER = 0x12,
	PILOT_RATING_TOP_ACE_1ST_ORDER = 0x13,
	PILOT_RATING_JEDI_4TH_DEGREE = 0x14,
	PILOT_RATING_JEDI_3RD_DEGREE = 0x15,
	PILOT_RATING_JEDI_2ND_DEGREE = 0x16,
	PILOT_RATING_JEDI_1ST_DEGREE = 0x17,
	PILOT_RATING_JEDI_MASTER = 0x18,
	PILOT_RATING_RESERVED_25 = 0x19,
	PILOT_RATING_RESERVED_26 = 0x1A,
	PILOT_RATING_RESERVED_27 = 0x1B,
	PILOT_RATING_RESERVED_28 = 0x1C,
	PILOT_RATING_RESERVED_29 = 0x1D,
	PILOT_RATING_RESERVED_30 = 0x1E,
	PILOT_RATING_RESERVED_31 = 0x1F,
};

struct PilotDataSelection {
	char name[14];
	int totalScore;
	int localPlayerId;
	int launchSessionMarker; ///< Persisted marker set to 1 when launch/debrief session state is captured; no
							 ///< XVT reader is identified.
	int isHost;
	unsigned int numHumanPlayersLastMission;
	int gameMode;
	uint8_t xvtRecordPayload[672]; ///< Opaque 672-byte payload from the XvT-compatible pilot-record prefix.
	int team;
	MissionDirectoryId missionDirectoryId;
	int missionDescriptionIds[5];
};

/* Stored as int32_t in the binary (IDB enum PilotPromotionDelta). */
typedef int32_t PilotPromotionDelta;

enum {
	PILOT_PROMOTION_DEMOTION = -1,
	PILOT_PROMOTION_NONE = 0x0,
	PILOT_PROMOTION_PROMOTION = 0x1,
};

int Pilot_DeleteCurrent(void);
int Pilot_CreateNew(const char* pilotName);
int Pilot_Save(int useTemporaryFile);
int Pilot_FindAndLoadByName(const char* pilotName);
int Pilot_ParseCommandLine(const char* cmdLine);
int Pilot_LoadXvtRecord(XvtFile* stream);
int Pilot_LoadFromPath(const char* basePilotPath);
int Pilot_WriteXvtRecord(const char* fileName, XvtFile* stream);

#ifdef __cplusplus
}
#endif

#endif

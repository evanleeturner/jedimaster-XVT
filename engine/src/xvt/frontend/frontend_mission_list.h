#ifndef XVT_FRONTEND_FRONTEND_MISSION_LIST_H
#define XVT_FRONTEND_FRONTEND_MISSION_LIST_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as int32_t in the binary (IDB enum MissionDirectoryId). */
typedef int32_t MissionDirectoryId;

enum {
	MISSION_DIRECTORY_TRAINING_EXERCISES = 0x0,
	MISSION_DIRECTORY_MELEES = 0x1,
	MISSION_DIRECTORY_TOURNAMENTS = 0x2,
	MISSION_DIRECTORY_COMBAT_ENGAGEMENTS = 0x3,
	MISSION_DIRECTORY_BATTLES = 0x4,
	MISSION_DIRECTORY_CAMPAIGNS = 0x5,
};

struct MissionListEntry {
	char fileName[64];
	char description[128];
	char sectionName[128];
	int missionIdx;
	int isUnavailable;
};

int FrontendMissionList_FreeScreenResources(int frameCounter);
int FrontendMissionList_FreeScreenResourcesAndClearInputGate(void);

#ifdef __cplusplus
}
#endif

#endif

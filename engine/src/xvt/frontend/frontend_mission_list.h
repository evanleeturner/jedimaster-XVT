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

/* One mission of a mission list file, as MissionSetup_LoadMissionList reads
 * it: an id line, a file name line and a description line, under the last
 * "[section]" line. */
struct MissionListEntry {
	/* Mission file name, lowercased, without a leading '*' or '&' entry
	 * marker. Some screens read its first character as a player count. */
	char fileName[64];
	char description[128]; /* The mission's title line. */
	/* Name of the section the entry sits in, without the brackets; empty
	 * before the first section. */
	char sectionName[128];
	int missionIdx; /* Mission id: the number on the entry's first line. */
	/* 1 when the entry is marked '&', or marked '*' and its campaign
	 * mission has not been flown; such entries are left out of lists. */
	int isUnavailable;
};

int FrontendMissionList_FreeScreenResources(int frameCounter);
int FrontendMissionList_FreeScreenResourcesAndClearInputGate(void);

#ifdef __cplusplus
}
#endif

#endif

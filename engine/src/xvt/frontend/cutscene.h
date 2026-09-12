#ifndef XVT_FRONTEND_CUTSCENE_H
#define XVT_FRONTEND_CUTSCENE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CutsceneEntry {
	char movieName[128];
	char thumbnailSprite[32];
	char description[128];
	int missionIdx;
	int playAfterDebriefing;
	int missionDescriptionId;
};

extern int g_cutsceneCount;
extern CutsceneEntry* g_cutsceneTable;

int Cutscene_LoadTable(char* fileName);
int Cutscene_PlayForCurrentMissionPhase(int phase);

#ifdef __cplusplus
}
#endif

#endif

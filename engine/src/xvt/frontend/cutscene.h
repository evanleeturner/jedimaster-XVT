#ifndef XVT_FRONTEND_CUTSCENE_H
#define XVT_FRONTEND_CUTSCENE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One cutscene of movies\cutscene.lst. */
struct CutsceneEntry {
	char movieName[128]; /* Movie to play, without folder or extension. */
	/* Image shown for it in the pilot record's cutscene viewer. */
	char thumbnailSprite[32];
	/* Text shown under the thumbnail in the cutscene viewer. */
	char description[128];
	int campaignId; /* Campaign it belongs to: the campaigns mission id. */
	/* 0 to play before the mission, 1 after its debriefing. */
	int playAfterDebriefing;
	/* Mission it goes with: the training-exercises mission id. */
	int campaignMissionId;
};

extern int g_cutsceneCount;
extern struct CutsceneEntry *g_cutsceneTable;

int Cutscene_LoadTable(char *fileName);
int Cutscene_PlayForCurrentMissionPhase(int phase);

#ifdef __cplusplus
}
#endif

#endif

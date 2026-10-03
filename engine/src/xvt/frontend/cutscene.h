#ifndef XVT_FRONTEND_CUTSCENE_H
#define XVT_FRONTEND_CUTSCENE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One cutscene of movies\cutscene.lst. */
struct cutscene_entry {
	char movie_name[128]; /* Movie to play, without folder or extension. */
	/* Image shown for it in the pilot record's cutscene viewer. */
	char thumbnail_sprite[32];
	/* Text shown under the thumbnail in the cutscene viewer. */
	char description[128];
	int campaign_id; /* Campaign it belongs to: the campaigns mission id. */
	/* 0 to play before the mission, 1 after its debriefing. */
	int play_after_debriefing;
	/* Mission it goes with: the training-exercises mission id. */
	int campaign_mission_id;
};

extern int g_cutscene_count;
extern struct cutscene_entry *g_cutscene_table;

int cutscene_load_table(char *file_name);
int cutscene_play_for_current_mission_phase(int phase);

#ifdef __cplusplus
}
#endif

#endif

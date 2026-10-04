#include "xvt_runtime/runtime/cutscene_task.h"

#include <string.h>

#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/movie_task.h"

static struct {
	unsigned int entry_index;
	int phase;
	int active;
	int waiting;
} g_cutscene;

static void xvt_cutscene_task_restore(void)
{
	frontend_display_clear_back_buffer();
	frontend_display_present_frame();
	frontend_display_clear_back_buffer();
	frontend_display_clear_offscreen_surface();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	frontend_display_enable_offscreen_restore();
	cd_audio_request_resume_playback();
}

int xvt_cutscene_task_play(int phase)
{
	if (!g_cutscene.active) {
		if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_TRAINING_EXERCISES ||
		    g_pilot_data.mission_sequence_active != 1 ||
		    !g_cutscene_table) {
			return 0;
		}
		g_cutscene.phase = phase;
		g_cutscene.entry_index = 0;
		g_cutscene.active = 1;
	}
	int result;
	if (g_cutscene.waiting) {
		if (!xvt_movie_task_take_result(&result)) {
			return XVT_MOVIE_PENDING;
		}
		g_cutscene.waiting = 0;
		xvt_cutscene_task_restore();
		if (result != 0) {
			xvt_cutscene_task_reset();
			return 0;
		}
		++g_cutscene.entry_index;
	}
	for (; g_cutscene.entry_index < (unsigned int)g_cutscene_count;
	     ++g_cutscene.entry_index) {
		const struct cutscene_entry *entry =
			&g_cutscene_table[g_cutscene.entry_index];
		if (entry->campaign_id !=
			    g_pilot_data.mission_description_ids[5] ||
		    entry->play_after_debriefing != g_cutscene.phase ||
		    entry->campaign_mission_id !=
			    g_pilot_data.mission_description_ids[0]) {
			continue;
		}
		cd_audio_suspend_playback();
		frontend_display_disable_offscreen_restore();
		frontend_display_unlock_back_buffer();
		frontend_display_clear_back_buffer();
		frontend_display_present_frame();
		frontend_display_clear_back_buffer();
		result = xvt_movie_task_begin(entry->movie_name, 1);
		if (result == XVT_MOVIE_PENDING) {
			g_cutscene.waiting = 1;
			return XVT_MOVIE_PENDING;
		}
		xvt_cutscene_task_restore();
		if (result != 0) {
			xvt_cutscene_task_reset();
			return 0;
		}
	}
	xvt_cutscene_task_reset();
	return 1;
}

void xvt_cutscene_task_reset(void)
{
	memset(&g_cutscene, 0, sizeof(g_cutscene));
}

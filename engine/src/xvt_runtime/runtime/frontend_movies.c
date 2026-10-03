#include "xvt_runtime/runtime/frontend_movies.h"

#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/movie_task.h"

static int g_viewer_pending;

int xvt_frontend_movies_play_viewer(const char *name)
{
	int result = movie_play(name, 0);
	g_viewer_pending = result == XVT_MOVIE_PENDING;
	return g_viewer_pending;
}

int xvt_frontend_movies_resume_viewer(void)
{
	int result;
	if (!g_viewer_pending) {
		return 0;
	}
	if (!xvt_movie_task_take_result(&result)) {
		return 1;
	}
	g_viewer_pending = 0;
	frontend_display_clear_back_buffer();
	frontend_display_present_frame();
	frontend_display_clear_back_buffer();
	frontend_display_enable_offscreen_restore();
	pilot_record_redraw_background();
	cd_audio_request_resume_playback();
	return 0;
}

void xvt_frontend_movies_reset(void) { g_viewer_pending = 0; }

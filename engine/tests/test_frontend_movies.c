#define _POSIX_C_SOURCE 200809L
/* Checks the pilot record's movie viewer (xvt_runtime/runtime/frontend_movies.h) against the promises in
 * its header: a movie that cannot start leaves nothing pending, a started one keeps ResumeViewer at 1
 * until its result arrives, the arriving result is discarded with CD audio asked to resume, an untaken
 * result from the movie task is handed back instead of starting the movie, and Reset forgets the viewer's
 * movie without touching the movie task. The movies are empty files in a temporary asset folder, which
 * Aeron's decoder reports unreadable from its own thread; the test ticks the movie task until then.
 * Every case starts with no viewer movie, no movie task state, a cleared frontend and CD audio suspended.
 *
 * Not checked here: the display clearing and the pilot record redraw, which need a window and the game's
 * images. */
#include <string.h>
#include <time.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/frontend_movies.h"
#include "xvt_runtime/runtime/movie_task.h"

static struct xvt_test_assets g_assets;

static void fresh(void)
{
	xvt_frontend_movies_reset();
	xvt_movie_task_shutdown();
	xvt_test_close_assets(&g_assets);
	memset(&g_front_state, 0, sizeof g_front_state);
	g_front_state.cd_audio_suspend_state = CD_AUDIO_SUSPENDED;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	xvt_test_open_assets(&g_assets);
	xvt_test_add_asset(&g_assets, "movies/intro.smk");
}

/* Ticks the movie task until the movie completes, then reaps it as the port does on the next frame. */
static void finish_movie(void)
{
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && xvt_movie_task_is_active(); ++i) {
		xvt_movie_task_update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	xvt_movie_task_reap_finished();
}

static void check_nothing_pending(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 0);
	/* A movie that cannot be found is not pending. */
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_play_viewer("absent"), 0);
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_SUSPENDED);
}

static void check_viewer_waits_for_result(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_play_viewer("intro"), 1);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 1);

	/* Completed but not reaped: the result has not arrived. */
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && xvt_movie_task_is_active(); ++i) {
		xvt_movie_task_update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 1);

	/* Once it arrives: discarded, CD audio asked to resume, and nothing pending any more. */
	xvt_movie_task_reap_finished();
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 0);
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_RESUME_PENDING);
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 0);
}

static void check_untaken_result_handed_back(void)
{
	fresh();
	/* A movie started elsewhere completes and is reaped, its result untaken. */
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), XVT_MOVIE_PENDING);
	finish_movie();

	/* movie_play hands that result back: the viewer's movie is not started and nothing is pending. */
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_play_viewer("intro"), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 0);
}

static void check_reset(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_play_viewer("intro"), 1);
	xvt_frontend_movies_reset();
	XVT_ASSERT_INT_EQ(xvt_frontend_movies_resume_viewer(), 0);
	/* The movie was not stopped, and its result is still there to take. */
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
	finish_movie();
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 1);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_SUSPENDED);
}

int main(void)
{
	check_nothing_pending();
	check_viewer_waits_for_result();
	check_untaken_result_handed_back();
	check_reset();
	xvt_frontend_movies_reset();
	xvt_movie_task_shutdown();
	xvt_test_close_assets(&g_assets);
	return 0;
}

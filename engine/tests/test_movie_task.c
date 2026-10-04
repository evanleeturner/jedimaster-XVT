#define _POSIX_C_SOURCE 200809L
/* Checks the movie task (xvt_runtime/runtime/movie_task.h) against the promises in its header, as far as
 * they hold without a playable movie: Begin's refusals, one movie at a time, a movie that fails during
 * playback completing with result 2, the result held until the player is reaped and then taken once, the
 * frontend state Begin saves and the reap restores, Stop, the network session's fallback to Flyby1a, and
 * Shutdown. The movies are empty files in a temporary asset folder; Aeron's decoder accepts the file and
 * then reports it unreadable from its worker thread, which the test waits for. Every case starts from
 * Shutdown, a cleared frontend and a single-player session.
 *
 * Not checked here: decoding, drawing, subtitles, skipping with a key, the multiplayer wait and the frame
 * delay of a real movie; they need a Smacker file from the game and a window. */
#include <string.h>
#include <time.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/movie_task.h"

static struct xvt_test_assets g_assets;

static void fresh(void)
{
	xvt_movie_task_shutdown();
	memset(&g_front_state, 0, sizeof g_front_state);
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
}

/* Binds storage to a fresh asset folder holding an empty movies/<name>.smk. */
static void movies(const char *name)
{
	xvt_test_open_assets(&g_assets);
	char path[XVT_TEST_PATH_CAPACITY];
	snprintf(path, sizeof path, "movies/%s.smk", name);
	xvt_test_add_asset(&g_assets, path);
}

static void end_movies(void)
{
	xvt_movie_task_shutdown();
	xvt_test_close_assets(&g_assets);
}

/* Ticks the movie until it completes; the decoder reports an empty file from its own thread. */
static void tick_until_complete(void)
{
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && xvt_movie_task_is_active(); ++i) {
		xvt_movie_task_update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
}

static void check_idle(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_continues_without_focus(), 0);
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
	XVT_ASSERT_INT_EQ(result, 77);
	XVT_ASSERT_INT_EQ(xvt_movie_task_next_wake_delay_us(), 10000);
	/* With no movie, Update, PausedFrame, Stop and ReapFinished change nothing. */
	xvt_movie_task_update();
	xvt_movie_task_paused_frame();
	xvt_movie_task_stop();
	xvt_movie_task_reap_finished();
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
}

static void check_begin_refusals(void)
{
	char long_name[300];
	memset(long_name, 'm', sizeof long_name - 1);
	long_name[sizeof long_name - 1] = 0;
	fresh();
	/* No storage bound: no movie can be found. */
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), 2);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);

	movies("intro");
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin(NULL, 0), 2);
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("", 0), 2);
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin(long_name, 0), 2);
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("absent", 0), 2);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
	end_movies();
}

static void check_lifecycle(void)
{
	fresh();
	movies("intro");
	g_front_state.frontend_display_wnd_proc_mode = 3;
	g_front_state.offscreen_restore_enabled = 1;
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_movie_task_continues_without_focus(), 0);

	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), 2);
	/* One movie at a time; no result while it plays. */
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);

	/* The frontend state Begin saved is changed while the movie plays. */
	g_front_state.frontend_display_wnd_proc_mode = 1;
	g_front_state.offscreen_restore_enabled = 0;

	/* The empty file fails: the movie completes with result 2. */
	tick_until_complete();
	/* Completed but not reaped: no result yet, and no new movie. */
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), 2);

	xvt_movie_task_reap_finished();
	XVT_ASSERT_INT_EQ(g_front_state.frontend_display_wnd_proc_mode, 3);
	XVT_ASSERT_INT_EQ(g_front_state.offscreen_restore_enabled, 1);
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 1);
	XVT_ASSERT_INT_EQ(result, 2);
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);

	/* With the result taken, the next movie can begin. */
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), XVT_MOVIE_PENDING);
	end_movies();
}

static void check_stop(void)
{
	fresh();
	movies("intro");
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), XVT_MOVIE_PENDING);
	xvt_movie_task_stop();
	/* A stopped movie counts as finished and completes on its next tick. Its result is 0, unless the
	 * decoder has already reported the empty file, which makes it 2. */
	xvt_movie_task_update();
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	xvt_movie_task_reap_finished();
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 1);
	XVT_ASSERT_TRUE(result == 0 || result == 2);
	end_movies();
}

static void check_network_fallback(void)
{
	/* In a network session, a synchronized movie that is missing falls back to Flyby1a. */
	fresh();
	movies("Flyby1a");
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_CLIENT;
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("absent", 1), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_movie_task_continues_without_focus(), 1);
	xvt_movie_task_shutdown();

	/* Not without synchronize... */
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("absent", 0), 2);
	/* ...and synchronize does nothing outside a network session. */
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("absent", 1), 2);
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("Flyby1a", 1),
			  XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(xvt_movie_task_continues_without_focus(), 0);
	end_movies();
}

static void check_shutdown_forgets(void)
{
	fresh();
	movies("intro");
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), XVT_MOVIE_PENDING);
	tick_until_complete();
	xvt_movie_task_reap_finished();
	/* An untaken result is forgotten. */
	xvt_movie_task_shutdown();
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);

	/* So is an active movie. */
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), XVT_MOVIE_PENDING);
	xvt_movie_task_shutdown();
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_begin("intro", 0), XVT_MOVIE_PENDING);
	end_movies();
}

int main(void)
{
	check_idle();
	check_begin_refusals();
	check_lifecycle();
	check_stop();
	check_network_fallback();
	check_shutdown_forgets();
	return 0;
}

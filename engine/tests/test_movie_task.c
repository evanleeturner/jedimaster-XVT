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
#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/movie_task.h"

#include <string.h>
#include <time.h>

static XvtTestAssets g_assets;

static void Fresh(void)
{
	XvtMovieTask_Shutdown();
	memset(&g_frontState, 0, sizeof g_frontState);
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
}

/* Binds storage to a fresh asset folder holding an empty movies/<name>.smk. */
static void Movies(const char *name)
{
	char path[XVT_TEST_PATH_CAPACITY];
	XvtTest_OpenAssets(&g_assets);
	snprintf(path, sizeof path, "movies/%s.smk", name);
	XvtTest_AddAsset(&g_assets, path);
}

static void EndMovies(void)
{
	XvtMovieTask_Shutdown();
	XvtTest_CloseAssets(&g_assets);
}

/* Ticks the movie until it completes; the decoder reports an empty file from its own thread. */
static void TickUntilComplete(void)
{
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && XvtMovieTask_IsActive(); ++i) {
		XvtMovieTask_Update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
}

static void CheckIdle(void)
{
	Fresh();
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_ContinuesWithoutFocus(), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
	XVT_ASSERT_INT_EQ(result, 77);
	XVT_ASSERT_INT_EQ(XvtMovieTask_NextWakeDelayUs(), 10000);
	/* With no movie, Update, PausedFrame, Stop and ReapFinished change nothing. */
	XvtMovieTask_Update();
	XvtMovieTask_PausedFrame();
	XvtMovieTask_Stop();
	XvtMovieTask_ReapFinished();
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
}

static void CheckBeginRefusals(void)
{
	char long_name[300];
	memset(long_name, 'm', sizeof long_name - 1);
	long_name[sizeof long_name - 1] = 0;
	Fresh();
	/* No storage bound: no movie can be found. */
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);

	Movies("intro");
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin(NULL, 0), 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("", 0), 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin(long_name, 0), 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("absent", 0), 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
	EndMovies();
}

static void CheckLifecycle(void)
{
	Fresh();
	Movies("intro");
	g_frontState.frontendDisplayWndProcMode = 3;
	g_frontState.offscreenRestoreEnabled = 1;
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtMovieTask_ContinuesWithoutFocus(), 0);

	/* One movie at a time; no result while it plays. */
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);

	/* The frontend state Begin saved is changed while the movie plays. */
	g_frontState.frontendDisplayWndProcMode = 1;
	g_frontState.offscreenRestoreEnabled = 0;

	/* The empty file fails: the movie completes with result 2. */
	TickUntilComplete();
	/* Completed but not reaped: no result yet, and no new movie. */
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), 2);

	XvtMovieTask_ReapFinished();
	XVT_ASSERT_INT_EQ(g_frontState.frontendDisplayWndProcMode, 3);
	XVT_ASSERT_INT_EQ(g_frontState.offscreenRestoreEnabled, 1);
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 1);
	XVT_ASSERT_INT_EQ(result, 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);

	/* With the result taken, the next movie can begin. */
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), XVT_MOVIE_PENDING);
	EndMovies();
}

static void CheckStop(void)
{
	Fresh();
	Movies("intro");
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), XVT_MOVIE_PENDING);
	XvtMovieTask_Stop();
	/* A stopped movie counts as finished and completes on its next tick. Its result is 0, unless the
	 * decoder has already reported the empty file, which makes it 2. */
	XvtMovieTask_Update();
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XvtMovieTask_ReapFinished();
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 1);
	XVT_ASSERT_TRUE(result == 0 || result == 2);
	EndMovies();
}

static void CheckNetworkFallback(void)
{
	/* In a network session, a synchronized movie that is missing falls back to Flyby1a. */
	Fresh();
	Movies("Flyby1a");
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("absent", 1), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtMovieTask_ContinuesWithoutFocus(), 1);
	XvtMovieTask_Shutdown();

	/* Not without synchronize... */
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("absent", 0), 2);
	/* ...and synchronize does nothing outside a network session. */
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("absent", 1), 2);
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("Flyby1a", 1), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(XvtMovieTask_ContinuesWithoutFocus(), 0);
	EndMovies();
}

static void CheckShutdownForgets(void)
{
	Fresh();
	Movies("intro");
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), XVT_MOVIE_PENDING);
	TickUntilComplete();
	XvtMovieTask_ReapFinished();
	/* An untaken result is forgotten. */
	XvtMovieTask_Shutdown();
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);

	/* So is an active movie. */
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), XVT_MOVIE_PENDING);
	XvtMovieTask_Shutdown();
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), XVT_MOVIE_PENDING);
	EndMovies();
}

int main(void)
{
	CheckIdle();
	CheckBeginRefusals();
	CheckLifecycle();
	CheckStop();
	CheckNetworkFallback();
	CheckShutdownForgets();
	return 0;
}

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
#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/frontend_movies.h"
#include "xvt_runtime/runtime/movie_task.h"

#include <string.h>
#include <time.h>

static struct XvtTestAssets g_assets;

static void Fresh(void)
{
	XvtFrontendMovies_Reset();
	XvtMovieTask_Shutdown();
	XvtTest_CloseAssets(&g_assets);
	memset(&g_frontState, 0, sizeof g_frontState);
	g_frontState.cdAudioSuspendState = CDAudio_Suspended;
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	XvtTest_OpenAssets(&g_assets);
	XvtTest_AddAsset(&g_assets, "movies/intro.smk");
}

/* Ticks the movie task until the movie completes, then reaps it as the port does on the next frame. */
static void FinishMovie(void)
{
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && XvtMovieTask_IsActive(); ++i) {
		XvtMovieTask_Update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XvtMovieTask_ReapFinished();
}

static void CheckNothingPending(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 0);
	/* A movie that cannot be found is not pending. */
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_PlayViewer("absent"), 0);
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 0);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_Suspended);
}

static void CheckViewerWaitsForResult(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_PlayViewer("intro"), 1);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 1);

	/* Completed but not reaped: the result has not arrived. */
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && XvtMovieTask_IsActive(); ++i) {
		XvtMovieTask_Update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 1);

	/* Once it arrives: discarded, CD audio asked to resume, and nothing pending any more. */
	XvtMovieTask_ReapFinished();
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 0);
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState,
			  CDAudio_ResumePending);
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 0);
}

static void CheckUntakenResultHandedBack(void)
{
	Fresh();
	/* A movie started elsewhere completes and is reaped, its result untaken. */
	XVT_ASSERT_INT_EQ(XvtMovieTask_Begin("intro", 0), XVT_MOVIE_PENDING);
	FinishMovie();

	/* Movie_Play hands that result back: the viewer's movie is not started and nothing is pending. */
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_PlayViewer("intro"), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 0);
}

static void CheckReset(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_PlayViewer("intro"), 1);
	XvtFrontendMovies_Reset();
	XVT_ASSERT_INT_EQ(XvtFrontendMovies_ResumeViewer(), 0);
	/* The movie was not stopped, and its result is still there to take. */
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
	FinishMovie();
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 1);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_Suspended);
}

int main(void)
{
	CheckNothingPending();
	CheckViewerWaitsForResult();
	CheckUntakenResultHandedBack();
	CheckReset();
	XvtFrontendMovies_Reset();
	XvtMovieTask_Shutdown();
	XvtTest_CloseAssets(&g_assets);
	return 0;
}

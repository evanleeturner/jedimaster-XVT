#include "xvt_runtime/runtime/frontend_movies.h"

#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/movie_task.h"

static int g_viewerPending;

int XvtFrontendMovies_PlayViewer(const char* name) {
	int result = Movie_Play(name, 0);
	g_viewerPending = result == XVT_MOVIE_PENDING;
	return g_viewerPending;
}

int XvtFrontendMovies_ResumeViewer(void) {
	int result;
	if (!g_viewerPending)
		return 0;
	if (!XvtMovieTask_TakeResult(&result))
		return 1;
	g_viewerPending = 0;
	FrontendDisplay_ClearBackBuffer();
	FrontendDisplay_PresentFrame();
	FrontendDisplay_ClearBackBuffer();
	FrontendDisplay_EnableOffscreenRestore();
	PilotRecord_RedrawBackground();
	CDAudio_RequestResumePlayback();
	return 0;
}

void XvtFrontendMovies_Reset(void) { g_viewerPending = 0; }

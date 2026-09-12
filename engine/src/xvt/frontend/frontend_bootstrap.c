#include "xvt/frontend/frontend_bootstrap.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/movie_task.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/credits.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/movie.h"

#include <stdio.h>

// FUNCTION: XVT 0x4F0860
int FrontendBootstrap_ExitIntroAndLoadCredits(int frameCounter) {
	(void)frameCounter;
	Credits_LoadScreenResources();
	return 0;
}

// FUNCTION: XVT 0x4F0870
int FrontendBootstrap_PlayOpeningAndEnterCredits(int frameCounter) {
	(void)frameCounter;
	FrontendDisplay_UnlockBackBuffer();
#ifdef XVT_MODERN
	if (Movie_Play("Opening", 0) == XVT_MOVIE_PENDING)
		return 0;
#else
	Movie_Play("Opening", 0);
#endif
	FrontendDisplay_ClearBackBuffer();
	FrontendScreen_SetCallbacks(Config_CreditsScreen, (FrontendScreenExitFn)FrontendBootstrap_LoadResources);
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	return 0;
}

// FUNCTION: XVT 0x4F08B0
int FrontendBootstrap_InitMode(void) {
	FrontendDisplay_DisableEscapeClose();
	FrontendDisplay_SetSurfaceClearColor(0);
	FrontendCursor_Hide();
	FrontendDisplay_ClearPresentFrameReady();
	FrontendDisplay_DisableOffscreenRestore();
	FrontendText_LoadFont(12);
	if (g_movieSubtitleFile != NULL) {
		File_RawClose(g_movieSubtitleFile);
		g_movieSubtitleFile = NULL;
	}
	return 0;
}

// FUNCTION: XVT 0x4FB5E0
int FrontendBootstrap_LoadResources(int frameCounter) {
	(void)frameCounter;

	if (g_frontendCreditsFile != NULL) {
		File_Close(g_frontendCreditsFile);
		g_frontendCreditsFile = NULL;
	}
	FrontImage_FreeResourceByName("background");
	FrontImage_FreeResourceByName("leclogo");
	FrontImage_FreeResourceByName("totallylogo");
	FrontImage_FreeResourceByName("comp01");
	FrontImage_FreeResourceByName("testers");
	FrontImage_FreeResourceByName("lakota");
	FrontImage_FreeResourceByName("artists");
	FrontendText_ResetGlyphScratch();
	Frontend_LoadResources();
	CDAudio_EnableLoopCurrentTrack();
	return 0;
}

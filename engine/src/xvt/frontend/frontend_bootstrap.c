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

/* Exit function of the first screen when the intro plays: runs once
 * FrontendBootstrap_PlayOpeningAndEnterCredits has switched to the credits,
 * and loads the credits screen through Credits_LoadScreenResources. Returns
 * 0. */
// FUNCTION: XVT 0x4F0860
int FrontendBootstrap_ExitIntroAndLoadCredits(int frameCounter)
{
	(void)frameCounter;
	Credits_LoadScreenResources();
	return 0;
}

/* Update function of the first screen when the intro plays. Unlocks the back
 * buffer, plays the "Opening" movie, clears the back buffer, makes
 * Credits_UpdateScreen and FrontendBootstrap_ExitCreditsAndLoadFrontend the
 * screen's callbacks and locks the back buffer again into g_drawSurfacePtr.
 * The original build plays the whole movie inside Movie_Play. In the modern
 * build Movie_Play starts it and answers XVT_MOVIE_PENDING, and this returns
 * at once with the back buffer unlocked; no frontend frame runs while the
 * movie plays, and the next call takes its result and goes on. Returns 0 on
 * every path and ignores the movie's result. */
// FUNCTION: XVT 0x4F0870
int FrontendBootstrap_PlayOpeningAndEnterCredits(int frameCounter)
{
	(void)frameCounter;
	FrontendDisplay_UnlockBackBuffer();
#ifdef XVT_MODERN
	if (Movie_Play("Opening", 0) == XVT_MOVIE_PENDING) {
		return 0;
	}
#else
	Movie_Play("Opening", 0);
#endif
	FrontendDisplay_ClearBackBuffer();
	FrontendScreen_SetCallbacks(
		Credits_UpdateScreen,
		(FrontendScreenExitFn)
			FrontendBootstrap_ExitCreditsAndLoadFrontend);
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	return 0;
}

/* Start function of the frontend when the intro plays, run once before the
 * first frame: by the main loop through modeInitFn in the original build, by
 * XvtFrontendTask_Update in the modern one. Turns off quitting on Esc, sets
 * the surface clear color to 0, hides the cursor, turns off clearing the back
 * buffer after each present and refilling it from the offscreen surface, and
 * loads font size 12. Closes g_movieSubtitleFile and sets it NULL when it is
 * open. Returns 0, which in the original build lets the main loop start. */
// FUNCTION: XVT 0x4F08B0
int FrontendBootstrap_InitMode(void)
{
	FrontendDisplay_DisableEscapeClose();
	FrontendDisplay_SetSurfaceClearColor(0);
	FrontendCursor_Hide();
	FrontendDisplay_DisableClearAfterPresent();
	FrontendDisplay_DisableOffscreenRestore();
	FrontendText_LoadFont(12);
	if (g_movieSubtitleFile != NULL) {
		File_RawClose(g_movieSubtitleFile);
		g_movieSubtitleFile = NULL;
	}
	return 0;
}

/* Exit function of the credits screen, run once the credits have switched to
 * the concourse. Closes g_frontendCreditsFile and sets it NULL when it is
 * open, frees the seven credits images, stops the text fade, loads the
 * frontend through Frontend_LoadResources and turns on looping of the
 * current CD track. Returns 0 and ignores what Frontend_LoadResources
 * returns. */
// FUNCTION: XVT 0x4FB5E0
int FrontendBootstrap_ExitCreditsAndLoadFrontend(int frameCounter)
{
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
	FrontendText_StopTextFade();
	Frontend_LoadResources();
	CDAudio_EnableLoopCurrentTrack();
	return 0;
}

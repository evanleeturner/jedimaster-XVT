#ifndef XVT_FRONTEND_MOVIE_H
#define XVT_FRONTEND_MOVIE_H

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*MovieInputCallback)(void* hWnd, unsigned int message, void* wParam, void* lParam, void* context,
								  int* handledResult);

typedef int (*MovieProgressCallback)(unsigned int currentFrame, unsigned int finalFrame, void* context);

struct MoviePlaybackParams {
	const char* movieName;
	int displayWidth;
	int displayHeight;
	IDirectDrawSurface* primarySurface;
	IDirectDrawSurface* decodeSurface;
	IDirectDrawPalette* palette;
	IDirectSound* directSound;
	void* unused1C; ///< Unused playback-parameter slot; the sole caller leaves it uninitialized and all movie
					///< consumers skip offset 0x1C.
	void* window;
	MovieInputCallback inputCallback;
	void* inputCallbackContext;
	MovieProgressCallback progressCallback;
	void* progressCallbackContext;
};

struct MovieDirtyRect {
	int x;
	int y;
	int width;
	int height;
};

struct MovieMultiplayerSyncPlayer {
	int playerId;
	int isWaiting;
};

extern MovieDirtyRect g_movieMergedDirtyRects[256];
extern unsigned int g_movieBottomMargin;
extern int g_movieY;
extern int g_movieRightMargin;
extern MovieMultiplayerSyncPlayer g_movieMultiplayerSyncPlayers[8];
extern XvtFile* g_movieSubtitleFile;

HRESULT Movie_BlitRectToDisplay(int x, int y, int width, int height);
HRESULT Movie_UpdateDirectDrawPalette(void);
int32_t AERON_DXAPI Movie_WindowProc(void* hWnd, unsigned int message, void* wParam, void* lParam);
int Movie_HandlePaint(void* hWnd);
int Movie_RunSmackerPlayback(const MoviePlaybackParams* params);
int Movie_GetSmackBufferFormat(void);
int Movie_InitializeSystemPalette(void* hWnd);
void Movie_DecodeAndPresentFrame(void);
void Movie_MergeDirtyRectLists(MovieDirtyRect* currentRects, unsigned int currentCount,
							   MovieDirtyRect* previousRects, unsigned int previousCount,
							   MovieDirtyRect** mergedRects, unsigned int* mergedCount);
int Movie_ComputeRectUnionAndIntersection(const MovieDirtyRect* a, const MovieDirtyRect* b,
										  MovieDirtyRect* unionRect, MovieDirtyRect* intersectionRect);
int Movie_Play(const char* name, int synchronizeMultiplayer);
extern int g_movieSkipRequested;
extern int g_moviePlaybackCompletionState;
extern unsigned int g_movieMultiplayerSyncDeadlineTick;
extern int g_moviePreviousWndProcMode;
int Movie_SingleplayerFrameCallback(int context, unsigned int eventCode, int keyCode, int eventArg3,
									int eventArg4, uint32_t* playbackFlag);
int Movie_MultiplayerFrameCallback(int context, unsigned int eventCode, int keyCode, int eventArg3,
								   int eventArg4, uint32_t* playbackFlag);
void Movie_DrawMultiplayerSyncStatus(void);
void Movie_UpdateMultiplayerSyncTimeout(void);
int Movie_MultiplayerSyncCallback(int initialize);
unsigned int Movie_ReadSubtitleCue(char* line1, char* line2, char* line3);
void Movie_DrawSubtitles(unsigned int frameNumber);

#ifdef __cplusplus
}
#endif

#endif

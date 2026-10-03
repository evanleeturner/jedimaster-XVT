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

typedef int (*MovieInputCallback)(void *hWnd, unsigned int message,
				  void *wParam, void *lParam, void *context,
				  int *handledResult);

typedef int (*MovieProgressCallback)(unsigned int currentFrame,
				     unsigned int finalFrame, void *context);

/* What the original build's Movie_Play passes to Movie_RunSmackerPlayback. */
struct MoviePlaybackParams {
	/* Movie name, without folder or extension. */
	const char *movieName;
	/* Display width the movie is centered in; Movie_Play passes 640. */
	int displayWidth;
	/* Display height the movie is centered in; Movie_Play passes 480. */
	int displayHeight;
	/* The display's primary surface, flipped in page-flip full screen. */
	IDirectDrawSurface *primarySurface;
	/* Surface frames are decoded into and copied from. */
	IDirectDrawSurface *decodeSurface;
	/* Display palette the movie's colors are written to. */
	IDirectDrawPalette *palette;
	/* DirectSound object handed to Smacker for the sound. */
	IDirectSound *directSound;
	/* Never read or written by name. */
	void *unused1C; ///< Unused playback-parameter slot; the sole caller leaves it uninitialized and all movie
			///< consumers skip offset 0x1C.
	/* The game window: blits land in its client area, and decoding waits
	 * while another window has focus. */
	void *window;
	/* Gets every window message first; a 0 from it skips the default
	 * handling. */
	MovieInputCallback inputCallback;
	/* Passed to inputCallback; Movie_Play leaves it unset and both
	 * callbacks ignore it. */
	void *inputCallbackContext;
	/* Called with the current and last frame numbers; 1 ends playback.
	 * NULL ends playback at the last frame. */
	MovieProgressCallback progressCallback;
	/* Passed to progressCallback; Movie_Play leaves it unset and the
	 * callback ignores it. */
	void *progressCallbackContext;
};

/* A changed area of a movie frame, in decode surface pixels. */
struct MovieDirtyRect {
	int x; /* Left edge. */
	int y; /* Top edge. */
	/* Width; Movie_MergeDirtyRectLists negates it to mark a rectangle
	 * used. */
	int width;
	int height; /* Height. */
};

/* One player of a network game's movie. */
struct MovieMultiplayerSyncPlayer {
	int playerId; /* DirectPlay id; 0 for an empty entry. */
	/* 1 once the player has finished or stopped the movie and waits for
	 * the others, 0 while watching. */
	int isWaiting;
};

extern MovieDirtyRect g_movieMergedDirtyRects[256];
extern unsigned int g_movieBottomMargin;
extern int g_movieY;
extern int g_movieRightMargin;
extern MovieMultiplayerSyncPlayer g_movieMultiplayerSyncPlayers[8];
extern XvtFile *g_movieSubtitleFile;

HRESULT Movie_BlitRectToDisplay(int x, int y, int width, int height);
HRESULT Movie_UpdateDirectDrawPalette(void);
int32_t AERON_DXAPI Movie_WindowProc(void *hWnd, unsigned int message,
				     void *wParam, void *lParam);
int Movie_HandlePaint(void *hWnd);
int Movie_RunSmackerPlayback(const MoviePlaybackParams *params);
int Movie_GetSmackBufferFormat(void);
int Movie_InitializeSystemPalette(void *hWnd);
void Movie_DecodeAndPresentFrame(void);
void Movie_MergeDirtyRectLists(MovieDirtyRect *currentRects,
			       unsigned int currentCount,
			       MovieDirtyRect *previousRects,
			       unsigned int previousCount,
			       MovieDirtyRect **mergedRects,
			       unsigned int *mergedCount);
int Movie_ComputeRectUnionAndIntersection(const MovieDirtyRect *a,
					  const MovieDirtyRect *b,
					  MovieDirtyRect *unionRect,
					  MovieDirtyRect *intersectionRect);
int Movie_Play(const char *name, int synchronizeMultiplayer);
extern int g_movieSkipRequested;
extern int g_moviePlaybackCompletionState;
extern unsigned int g_movieMultiplayerSyncDeadlineMs;
extern int g_moviePreviousWndProcMode;
int Movie_SingleplayerInputCallback(int window, unsigned int eventCode,
				    int keyCode, int lParam,
				    int callbackContext,
				    uint32_t *playbackFlag);
int Movie_MultiplayerInputCallback(int window, unsigned int eventCode,
				   int keyCode, int lParam, int callbackContext,
				   uint32_t *playbackFlag);
void Movie_DrawMultiplayerSyncStatus(void);
void Movie_UpdateMultiplayerSyncTimeout(void);
int Movie_MultiplayerSyncCallback(int currentFrame);
unsigned int Movie_ReadSubtitleCue(char *line1, char *line2, char *line3);
void Movie_DrawSubtitles(unsigned int frameNumber);

#ifdef __cplusplus
}
#endif

#endif

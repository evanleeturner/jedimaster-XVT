#include "xvt/frontend/movie.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/movie_task.h"
#endif

#include "xvt/assets/file.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"
#include "xvt/util/win32.h"

#ifdef XVT_MODERN
#include "aeron/aeron.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MoviePaletteEntry {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t flags;
};

struct MoviePixelFormat {
	uint32_t size;
	uint32_t flags;
	uint32_t fourCC;
	uint32_t rgbBitCount;
	uint32_t redMask;
	uint32_t greenMask;
	uint32_t blueMask;
	uint32_t alphaMask;
};

struct MovieWindowPos {
	void* window;
	void* insertAfter;
	int x;
	int y;
	int width;
	int height;
	unsigned int flags;
};

struct MovieWin32Message {
	void* window;
	uint32_t message;
	uint32_t wParam;
	int32_t lParam;
	uint32_t time;
	int32_t pointX;
	int32_t pointY;
};

struct MovieSmackHandle {
	uint32_t field00;
	uint32_t width;
	uint32_t height;
	uint32_t frameCount;
	uint8_t gap10[0x58];
	uint32_t paletteChanged;
	uint8_t gap6C[0x308];
	uint32_t currentFrame;
	uint8_t gap378[8];
	uint32_t dirtyX;
	uint32_t dirtyY;
	uint32_t dirtyWidth;
	uint32_t dirtyHeight;
};

typedef char xvt_size_MovieSmackHandle[(sizeof(struct MovieSmackHandle) == 0x390) ? 1 : -1];

// GLOBAL: XVT 0x52C80C
int g_movieY = 0;
// GLOBAL: XVT 0x52C810
int g_movieRightMargin = 0;

typedef HRESULT(AERON_DXAPI* MovieGetPixelFormatFn)(IDirectDrawSurface* surface,
													struct MoviePixelFormat* pixelFormat);

#ifndef XVT_MODERN
__declspec(dllimport) void* __stdcall GetFocus(void);
__declspec(dllimport) int __stdcall SmackToBuffer(struct MovieSmackHandle* handle, int x, int y, int pitch,
												  int height, void* pixels, int format);
__declspec(dllimport) int __stdcall SmackDoFrame(struct MovieSmackHandle* handle);
__declspec(dllimport) int __stdcall SmackToBufferRect(struct MovieSmackHandle* handle, int rectIndex);
__declspec(dllimport) int __stdcall SmackNextFrame(struct MovieSmackHandle* handle);
__declspec(dllimport) void* __stdcall GetDC(void* hWnd);
__declspec(dllimport) unsigned int __stdcall GetSystemPaletteEntries(void* hdc, unsigned int startIndex,
																	 unsigned int entryCount,
																	 struct MoviePaletteEntry* entries);
__declspec(dllimport) int __stdcall ReleaseDC(void* hWnd, void* hdc);
__declspec(dllimport) void* __stdcall BeginPaint(void* hWnd, void* paint);
__declspec(dllimport) int __stdcall EndPaint(void* hWnd, const void* paint);
__declspec(dllimport) int __stdcall GetUpdateRect(void* hWnd, RECT* rect, int erase);
__declspec(dllimport) void __stdcall PostQuitMessage(int exitCode);
__declspec(dllimport) int32_t __stdcall DefWindowProcA(void* hWnd, unsigned int message, void* wParam,
													   void* lParam);
__declspec(dllimport) int __stdcall ClientToScreen(void* hWnd, POINT* point);
__declspec(dllimport) int __stdcall PeekMessageA(struct MovieWin32Message* message, void* hWnd,
												 unsigned int filterMin, unsigned int filterMax,
												 unsigned int removeMessage);
__declspec(dllimport) int __stdcall TranslateMessage(const struct MovieWin32Message* message);
__declspec(dllimport) int32_t __stdcall DispatchMessageA(const struct MovieWin32Message* message);
__declspec(dllimport) void __stdcall SmackSoundUseDirectSound(IDirectSound* directSound);
__declspec(dllimport) struct MovieSmackHandle* __stdcall SmackOpen(const char* fileName, unsigned int flags,
																   int extraBuffer);
__declspec(dllimport) int __stdcall SmackWait(struct MovieSmackHandle* handle);
__declspec(dllimport) void __stdcall SmackClose(struct MovieSmackHandle* handle);
#else
int SmackToBuffer(struct MovieSmackHandle* handle, int x, int y, int pitch, int height, void* pixels,
				  int format);
int SmackDoFrame(struct MovieSmackHandle* handle);
int SmackToBufferRect(struct MovieSmackHandle* handle, int rectIndex);
int SmackNextFrame(struct MovieSmackHandle* handle);
void SmackSoundUseDirectSound(IDirectSound* directSound);
struct MovieSmackHandle* SmackOpen(const char* fileName, unsigned int flags, int extraBuffer);
int SmackWait(struct MovieSmackHandle* handle);
void SmackClose(struct MovieSmackHandle* handle);
#endif

// GLOBAL: XVT 0x52C808
int g_movieX = 0;
// GLOBAL: XVT 0x52C814
unsigned int g_movieBottomMargin = 0;
// GLOBAL: XVT 0x52C818
const struct MoviePlaybackParams* g_moviePlaybackParams = 0;
// GLOBAL: XVT 0x52C81C
struct MovieSmackHandle* g_movieSmackHandle = 0;
// GLOBAL: XVT 0x52C820
POINT g_movieClientScreenOrigin = { 0, 0 };
// GLOBAL: XVT 0x6661A0
int g_movieClientOffsetX = 0;
// GLOBAL: XVT 0x6661A4
int g_movieClientOffsetY = 0;
// GLOBAL: XVT 0x52C828
int g_movieFrameAvailable = 0;
// GLOBAL: XVT 0x52C82C
int g_movieSmackBufferFormat = 0;
// GLOBAL: XVT 0x52C830
unsigned int g_moviePreviousDirtyRectCount = 0;
// GLOBAL: XVT 0x6661A8
static MovieDirtyRect g_movieDirtyRectsA[256] = { { 0 } };
// GLOBAL: XVT 0x6671A8
static MovieDirtyRect g_movieDirtyRectsB[256] = { { 0 } };
// GLOBAL: XVT 0x52C834
MovieDirtyRect* g_moviePreviousDirtyRects = g_movieDirtyRectsA;
// GLOBAL: XVT 0x52C838
MovieDirtyRect* g_movieCurrentDirtyRects = g_movieDirtyRectsB;
// GLOBAL: XVT 0x665DA0
struct MoviePaletteEntry g_moviePaletteEntries[256] = { { 0 } };
// GLOBAL: XVT 0xAA6078
XvtFile* g_movieSubtitleFile = NULL;
// GLOBAL: XVT 0xAA5E70
static char g_movieSubtitleLine2[256] = { 0 };
// GLOBAL: XVT 0xAA5F70
static char g_movieSubtitleLine3[256] = { 0 };
// GLOBAL: XVT 0xAA6070
static unsigned int g_movieActiveSubtitleFrame = 0;
// GLOBAL: XVT 0xAA6074
static unsigned int g_movieNextSubtitleFrame = 0;
// GLOBAL: XVT 0xAA6080
int g_moviePreviousWndProcMode = 0;
// GLOBAL: XVT 0xAA6084
int g_moviePlaybackCompletionState = 0;
// GLOBAL: XVT 0xAA607C
unsigned int g_movieMultiplayerSyncDeadlineTick = 0;
// GLOBAL: XVT 0x665D98
int g_movieSkipRequested = 0;
// GLOBAL: XVT 0x6681A8
MovieDirtyRect g_movieMergedDirtyRects[256] = { { 0 } };
// GLOBAL: XVT 0xAA6090
MovieMultiplayerSyncPlayer g_movieMultiplayerSyncPlayers[8] = { { 0 } };

// FUNCTION: XVT 0x4EECB0
HRESULT Movie_BlitRectToDisplay(int x, int y, int width, int height) {
	IDirectDrawSurface* backBuffer;
	DDSCAPS caps;
	RECT sourceRect;
	uint32_t destinationX;
	uint32_t destinationY;
	HRESULT result;

	sourceRect.left = x;
	destinationX = g_movieClientOffsetX + g_movieClientScreenOrigin.x + x;
	sourceRect.top = y;
	destinationY = g_movieClientScreenOrigin.y + g_movieClientOffsetY + y;
	sourceRect.right = x + width;
	sourceRect.bottom = y + height;
	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		caps.dwCaps = DDSCAPS_BACKBUFFER;
		result = g_moviePlaybackParams->primarySurface->lpVtbl->GetAttachedSurface(
			g_moviePlaybackParams->primarySurface, &caps, &backBuffer);
		while (result == DX_DDERR_SURFACELOST) {
			result =
				g_moviePlaybackParams->primarySurface->lpVtbl->Restore(g_moviePlaybackParams->primarySurface);
			if (result != 0) {
				return result;
			}
			g_moviePlaybackParams->primarySurface->lpVtbl->SetPalette(g_moviePlaybackParams->primarySurface,
																	  g_moviePlaybackParams->palette);
			Movie_UpdateDirectDrawPalette();
			result = g_moviePlaybackParams->primarySurface->lpVtbl->GetAttachedSurface(
				g_moviePlaybackParams->primarySurface, &caps, &backBuffer);
		}
		do {
			result = backBuffer->lpVtbl->BltFast(backBuffer, destinationX, destinationY,
												 g_moviePlaybackParams->decodeSurface, &sourceRect,
												 DDBLTFAST_WAIT);
			if (result != DX_DDERR_SURFACELOST) {
				break;
			}
			if (g_moviePlaybackParams->decodeSurface->lpVtbl->IsLost(g_moviePlaybackParams->decodeSurface) ==
				DX_DDERR_SURFACELOST) {
				result = g_moviePlaybackParams->decodeSurface->lpVtbl->Restore(
					g_moviePlaybackParams->decodeSurface);
				if (result != 0) {
					break;
				}
			}
		} while (1);
	} else {
		do {
			result = g_moviePlaybackParams->primarySurface->lpVtbl->BltFast(
				g_moviePlaybackParams->primarySurface, destinationX, destinationY,
				g_moviePlaybackParams->decodeSurface, &sourceRect, DDBLTFAST_WAIT);
			if (result != DX_DDERR_SURFACELOST) {
				break;
			}
			if (g_moviePlaybackParams->primarySurface->lpVtbl->IsLost(
					g_moviePlaybackParams->primarySurface) == DX_DDERR_SURFACELOST) {
				result = g_moviePlaybackParams->primarySurface->lpVtbl->Restore(
					g_moviePlaybackParams->primarySurface);
				if (result != 0) {
					break;
				}
				g_moviePlaybackParams->primarySurface->lpVtbl->SetPalette(
					g_moviePlaybackParams->primarySurface, g_moviePlaybackParams->palette);
				Movie_UpdateDirectDrawPalette();
			}
			if (g_moviePlaybackParams->decodeSurface->lpVtbl->IsLost(g_moviePlaybackParams->decodeSurface) ==
				DX_DDERR_SURFACELOST) {
				result = g_moviePlaybackParams->decodeSurface->lpVtbl->Restore(
					g_moviePlaybackParams->decodeSurface);
				if (result != 0) {
					break;
				}
			}
		} while (1);
	}
	return result;
}

// FUNCTION: XVT 0x4EEE70
HRESULT Movie_UpdateDirectDrawPalette(void) {
	struct MoviePaletteEntry* entry;
	uint8_t* smackColor;
	unsigned int index;

	entry = &g_moviePaletteEntries[10];
	smackColor = (uint8_t*)g_movieSmackHandle + 0x8A;
	for (index = 10; index < 246; ++index) {
		entry->red = *smackColor++;
		entry->green = *smackColor++;
		entry->blue = *smackColor++;
		++entry;
	}
	return g_moviePlaybackParams->palette->lpVtbl->SetEntries(g_moviePlaybackParams->palette, 0, 0, 256,
															  g_moviePaletteEntries);
}

// FUNCTION: XVT 0x4EEEC0
int32_t AERON_DXAPI Movie_WindowProc(void* hWnd, unsigned int message, void* wParam, void* lParam) {
	int useDefault;
	int handledResult;
	struct MovieWindowPos* windowPos;
	uint16_t originX;
	uint16_t alignedX;

	useDefault = 1;
	handledResult = 0;
	if (g_moviePlaybackParams != NULL && g_moviePlaybackParams->inputCallback != NULL) {
		useDefault = g_moviePlaybackParams->inputCallback(
			hWnd, message, wParam, lParam, g_moviePlaybackParams->inputCallbackContext, &handledResult);
	}

	switch (message) {
		case 0x02:
			FrontendDisplay_SetWndProcMode(g_moviePreviousWndProcMode);
			FrontendDisplay_Shutdown(1);
#ifndef XVT_MODERN
			PostQuitMessage(0);
#else
			Aeron_RequestQuit();
#endif
			return 0;
		case 0x46:
			windowPos = lParam;
			if ((windowPos->flags & 2) == 0) {
				originX = (uint16_t)g_movieClientScreenOrigin.x;
				alignedX = (uint16_t)((originX + windowPos->x + 1) & 0xFFFC);
				alignedX = (uint16_t)(alignedX - originX);
				windowPos->x = alignedX;
				g_movieClientOffsetX = alignedX;
				g_movieClientOffsetY = windowPos->y;
			}
			break;
		case 0x30F:
			if (g_moviePlaybackParams != NULL && g_moviePlaybackParams->primarySurface != NULL &&
				g_moviePlaybackParams->palette != NULL) {
				g_moviePlaybackParams->primarySurface->lpVtbl->SetPalette(
					g_moviePlaybackParams->primarySurface, g_moviePlaybackParams->palette);
			}
			return 0;
		default:
			break;
	}

	if (useDefault == 0) {
		return handledResult;
	}
	switch (message) {
		case 0x0F:
			Movie_HandlePaint(hWnd);
			return 0;
		case 0x14:
			return 1;
		case 0x311:
			if (wParam == hWnd) {
				return 0;
			}
			break;
		default:
			break;
	}
#ifndef XVT_MODERN
	return DefWindowProcA(hWnd, (uint16_t)message, wParam, lParam);
#else
	return 0;
#endif
}

// FUNCTION: XVT 0x4EF040
int Movie_HandlePaint(void* hWnd) {
	RECT updateRect;
#ifndef XVT_MODERN
	uint8_t paint[64];
#endif
	int result;

#ifndef XVT_MODERN
	BeginPaint(hWnd, paint);
	result = EndPaint(hWnd, paint);
#else
	(void)hWnd;
	result = 0;
#endif
	if (g_movieFrameAvailable != 0 && g_movieSmackHandle != NULL && g_moviePlaybackParams != NULL) {
		if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
			Movie_BlitRectToDisplay(0, 0, ((int*)g_movieSmackHandle)[1], ((int*)g_movieSmackHandle)[2]);
			return g_moviePlaybackParams->primarySurface->lpVtbl->Flip(g_moviePlaybackParams->primarySurface,
																	   NULL, 1);
		}
#ifndef XVT_MODERN
		GetUpdateRect(hWnd, &updateRect, 0);
#else
		updateRect.left = 0;
		updateRect.top = 0;
		updateRect.right = ((int*)g_movieSmackHandle)[1];
		updateRect.bottom = ((int*)g_movieSmackHandle)[2];
#endif
		return Movie_BlitRectToDisplay(updateRect.left, updateRect.top, updateRect.right - updateRect.left,
									   updateRect.bottom - updateRect.top);
	}
	return result;
}

// FUNCTION: XVT 0x4EF100
int Movie_RunSmackerPlayback(const MoviePlaybackParams* params) {
#ifdef XVT_MODERN
	return XvtMovieTask_Begin(params->movieName, params->progressCallback != NULL);
#else
	enum {
		MOVIE_STATUS_OK = 0,
		MOVIE_STATUS_NOT_FOUND = 2,
		MOVIE_STATUS_TOO_LARGE = 3,
		MOVIE_STATUS_SKIPPED = 5,
		MOVIE_WINDOW_MODE = 2,
		SMACK_OPEN_FLAGS = 0xFE000,
		SMACK_DEFAULT_EXTRA_BUFFER = -1,
		REMOVE_MESSAGE = 1,
		FLIP_WAIT = 1,
		PROGRESS_FINISHED = 1,
		MOVIE_PATH_CAPACITY = 256,
		MOVIE_EXTENSION_LENGTH = 3,
		MOVIE_EXTENSION_LAST_INDEX = 2,
	};
	struct MovieWin32Message message;
	char fileName[MOVIE_PATH_CAPACITY];
	char* subtitleExtension;
	MovieProgressCallback progressCallback;

	g_moviePlaybackParams = params;
	Movie_InitializeSystemPalette(params->window);
	g_movieSmackBufferFormat = Movie_GetSmackBufferFormat();
	ClientToScreen(g_moviePlaybackParams->window, &g_movieClientScreenOrigin);
	FrontendDisplay_ClearBackBuffer();
	FrontendDisplay_ClearOffscreenSurface();
	FrontendDisplay_PresentFrame();
	FrontendDisplay_ClearBackBuffer();
	SmackSoundUseDirectSound(g_moviePlaybackParams->directSound);

	strcpy(fileName, "movies\\");
	strcat(fileName, g_moviePlaybackParams->movieName);
	strcat(fileName, ".smk");
	g_movieSmackHandle = SmackOpen(fileName, SMACK_OPEN_FLAGS, SMACK_DEFAULT_EXTRA_BUFFER);
	if (g_movieSmackHandle == NULL) {
		strcpy(fileName, "d:\\movies\\");
		fileName[0] = File_GetCdDriveLetter();
		strcat(fileName, g_moviePlaybackParams->movieName);
		strcat(fileName, ".smk");
		g_movieSmackHandle = SmackOpen(fileName, SMACK_OPEN_FLAGS, SMACK_DEFAULT_EXTRA_BUFFER);
		if (g_movieSmackHandle == NULL) {
			g_moviePlaybackParams = NULL;
			return MOVIE_STATUS_NOT_FOUND;
		}
	}

	subtitleExtension = fileName + strlen(fileName) - MOVIE_EXTENSION_LENGTH;
	subtitleExtension[MOVIE_EXTENSION_LAST_INDEX] = 't';
	subtitleExtension[0] = 't';
	subtitleExtension[1] = 'x';
	g_movieSubtitleFile = File_RawOpen(fileName, "r");
	if (g_movieSmackHandle->width > (unsigned int)g_moviePlaybackParams->displayWidth &&
		g_movieSmackHandle->height > (unsigned int)g_moviePlaybackParams->displayHeight) {
		g_moviePlaybackParams = NULL;
		SmackClose(g_movieSmackHandle);
		return MOVIE_STATUS_TOO_LARGE;
	}

	g_movieX = ((unsigned int)g_moviePlaybackParams->displayWidth - g_movieSmackHandle->width) / 2;
	g_movieY = ((unsigned int)g_moviePlaybackParams->displayHeight - g_movieSmackHandle->height) / 2;
	g_movieRightMargin = g_moviePlaybackParams->displayWidth - g_movieSmackHandle->width - g_movieX;
	g_movieBottomMargin = g_moviePlaybackParams->displayHeight - g_movieSmackHandle->height - g_movieY;
	g_moviePreviousWndProcMode = FrontendDisplay_GetWndProcMode();
	FrontendDisplay_SetWndProcMode(MOVIE_WINDOW_MODE);
	g_moviePlaybackCompletionState = 0;
	g_movieSkipRequested = 0;
	while (FrontendDisplay_GetWndProcMode() == MOVIE_WINDOW_MODE) {
		Net_PumpIncomingPackets();
		if (PeekMessageA(&message, NULL, 0, 0, REMOVE_MESSAGE) != 0) {
			TranslateMessage(&message);
			DispatchMessageA(&message);
		} else if (g_moviePlaybackCompletionState == 0) {
			if (SmackWait(g_movieSmackHandle) == 0)
				Movie_DecodeAndPresentFrame();
		} else {
			progressCallback = g_moviePlaybackParams->progressCallback;
			if (progressCallback != NULL) {
				if (progressCallback(g_movieSmackHandle->currentFrame, g_movieSmackHandle->frameCount - 1,
									 g_moviePlaybackParams->progressCallbackContext) == PROGRESS_FINISHED) {
					FrontendDisplay_SetWndProcMode(g_moviePreviousWndProcMode);
				}
				if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
					g_moviePlaybackParams->primarySurface->lpVtbl->Flip(g_moviePlaybackParams->primarySurface,
																		NULL, FLIP_WAIT);
				}
			} else {
				FrontendDisplay_SetWndProcMode(g_moviePreviousWndProcMode);
			}
		}
	}

	if (g_movieSubtitleFile != NULL) {
		File_RawClose(g_movieSubtitleFile);
		g_movieSubtitleFile = NULL;
	}
	SmackClose(g_movieSmackHandle);
	g_moviePlaybackParams = NULL;
	return g_movieSkipRequested == 0 ? MOVIE_STATUS_OK : MOVIE_STATUS_SKIPPED;
#endif
}

// FUNCTION: XVT 0x4EF500
int Movie_GetSmackBufferFormat(void) {
	struct MoviePixelFormat pixelFormat;
	IDirectDrawSurface* surface;

	pixelFormat.size = sizeof(pixelFormat);
	pixelFormat.flags = 0x40;
	surface = g_moviePlaybackParams->primarySurface;
	((MovieGetPixelFormatFn)surface->lpVtbl->GetPixelFormat)(surface, &pixelFormat);

	if (pixelFormat.rgbBitCount == 8)
		return 0;
	if (pixelFormat.redMask == 0xF800 && pixelFormat.greenMask == 0x07E0 && pixelFormat.blueMask == 0x001F)
		return (int)0xC0000000u;
	if (pixelFormat.redMask == 0x7C00 && pixelFormat.greenMask == 0x03E0 && pixelFormat.blueMask == 0x001F)
		return (int)0x80000000u;
	return 0;
}

// FUNCTION: XVT 0x4EF590
int Movie_InitializeSystemPalette(void* hWnd) {
	int index;
#ifndef XVT_MODERN
	void* dc;

	dc = GetDC(hWnd);
	GetSystemPaletteEntries(dc, 0, 256, g_moviePaletteEntries);
#else
	(void)hWnd;
#endif

	for (index = 0; index < 10; ++index) {
		g_moviePaletteEntries[index].flags = 0;
	}
	for (index = 10; index < 246; ++index) {
		g_moviePaletteEntries[index].flags = 4;
	}
	for (index = 246; index < 256; ++index) {
		g_moviePaletteEntries[index].flags = 0;
	}

#ifdef XVT_MODERN
	return 1;
#else
	return ReleaseDC(hWnd, dc);
#endif
}

// FUNCTION: XVT 0x4EF600
void Movie_DecodeAndPresentFrame(void) {
#ifndef XVT_MODERN
	DDSURFACEDESC surfaceDesc;
	MovieDirtyRect* mergedRects;
	unsigned int mergedCount;
	unsigned int currentCount;
	unsigned int dirtyIndex;
	void* focusWindow;
	int result;

	focusWindow = GetFocus();
	if (g_moviePlaybackParams->window != focusWindow)
		return;
	if (g_movieSmackHandle->paletteChanged != 0)
		Movie_UpdateDirectDrawPalette();
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	while (g_moviePlaybackParams->decodeSurface->lpVtbl->Lock(
			   g_moviePlaybackParams->decodeSurface, NULL, &surfaceDesc, 1, NULL) == DX_DDERR_SURFACELOST) {
		result = g_moviePlaybackParams->decodeSurface->lpVtbl->Restore(g_moviePlaybackParams->decodeSurface);
		if (result != 0)
			return;
	}
	SmackToBuffer(g_movieSmackHandle, g_movieX, g_movieY, surfaceDesc.lPitch, g_movieSmackHandle->height,
				  surfaceDesc.lpSurface, g_movieSmackBufferFormat);
	SmackDoFrame(g_movieSmackHandle);
	g_movieFrameAvailable = 1;
	g_moviePlaybackParams->decodeSurface->lpVtbl->Unlock(g_moviePlaybackParams->decodeSurface,
														 surfaceDesc.lpSurface);
	Movie_DrawSubtitles(g_movieSmackHandle->currentFrame);
	if (g_moviePlaybackParams->progressCallback != NULL &&
		g_moviePlaybackParams->progressCallback(g_movieSmackHandle->currentFrame,
												g_movieSmackHandle->frameCount - 1,
												g_moviePlaybackParams->progressCallbackContext) == 1) {
		FrontendDisplay_SetWndProcMode(g_moviePreviousWndProcMode);
	}
	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		currentCount = 0;
		if (SmackToBufferRect(g_movieSmackHandle, 0) != 0) {
			dirtyIndex = 0;
			do {
				if (g_movieSmackHandle->dirtyWidth != 0) {
					g_movieCurrentDirtyRects[dirtyIndex].x = g_movieSmackHandle->dirtyX;
					++dirtyIndex;
					++currentCount;
					g_movieCurrentDirtyRects[dirtyIndex - 1].y = g_movieSmackHandle->dirtyY;
					g_movieCurrentDirtyRects[dirtyIndex - 1].width = g_movieSmackHandle->dirtyWidth;
					g_movieCurrentDirtyRects[dirtyIndex - 1].height = g_movieSmackHandle->dirtyHeight;
				}
			} while (SmackToBufferRect(g_movieSmackHandle, 0) != 0);
		}
		Movie_MergeDirtyRectLists(g_movieCurrentDirtyRects, currentCount, g_moviePreviousDirtyRects,
								  g_moviePreviousDirtyRectCount, &mergedRects, &mergedCount);
		if (mergedCount-- != 0) {
			do {
				Movie_BlitRectToDisplay(mergedRects[mergedCount].x, mergedRects[mergedCount].y,
										mergedRects[mergedCount].width, mergedRects[mergedCount].height);
			} while (mergedCount-- != 0);
		}
		g_moviePlaybackParams->primarySurface->lpVtbl->Flip(g_moviePlaybackParams->primarySurface, NULL, 1);
		mergedRects = g_moviePreviousDirtyRects;
		g_moviePreviousDirtyRects = g_movieCurrentDirtyRects;
		g_movieCurrentDirtyRects = mergedRects;
		g_moviePreviousDirtyRectCount = currentCount;
	} else {
		while (SmackToBufferRect(g_movieSmackHandle, 0) != 0) {
			Movie_BlitRectToDisplay(g_movieSmackHandle->dirtyX, g_movieSmackHandle->dirtyY,
									g_movieSmackHandle->dirtyWidth, g_movieSmackHandle->dirtyHeight);
		}
	}
	if (g_movieSmackHandle->frameCount - g_movieSmackHandle->currentFrame == 1) {
		g_moviePlaybackCompletionState = 1;
		return;
	}
	SmackNextFrame(g_movieSmackHandle);
#endif
}

// FUNCTION: XVT 0x4EF8E0
void Movie_MergeDirtyRectLists(MovieDirtyRect* currentRects, unsigned int currentCount,
							   MovieDirtyRect* previousRects, unsigned int previousCount,
							   MovieDirtyRect** mergedRects, unsigned int* mergedCount) {
	MovieDirtyRect candidate;
	MovieDirtyRect bestUnion;
	MovieDirtyRect rectUnion;
	MovieDirtyRect intersection;
	MovieDirtyRect* bestRect;
	MovieDirtyRect* output;
	unsigned int remaining;
	unsigned int outputCount;
	unsigned int index;
	unsigned int restoreIndex;

	if (currentCount == 0) {
		*mergedRects = previousRects;
		*mergedCount = previousCount;
		return;
	}
	if (previousCount == 0) {
		*mergedRects = currentRects;
		*mergedCount = currentCount;
		return;
	}

	*mergedRects = g_movieMergedDirtyRects;
	outputCount = 0;
	candidate = currentRects[0];
	remaining = currentCount + previousCount - 1;
	currentRects[0].width = -currentRects[0].width;
	if (remaining != 0) {
		output = g_movieMergedDirtyRects;
		do {
			unsigned int bestCost;
			unsigned int bestUnionArea;
			unsigned int index;
			unsigned int previousIndex;
			int candidateArea;

			bestCost = 0x1000000;
			candidateArea = candidate.width * candidate.height;
			for (index = 0; index < currentCount; ++index) {
				unsigned int cost;
				int unionArea;

				if (currentRects[index].width <= 0)
					continue;
				Movie_ComputeRectUnionAndIntersection(&currentRects[index], &candidate, &rectUnion,
													  &intersection);
				unionArea = rectUnion.width * rectUnion.height;
				cost = intersection.width * intersection.height -
					   currentRects[index].height * currentRects[index].width - candidateArea + unionArea;
				if (cost < bestCost) {
					bestCost = cost;
					bestUnionArea = unionArea;
					bestUnion = rectUnion;
					bestRect = &currentRects[index];
					if (cost == 0)
						break;
				}
			}

			for (previousIndex = 0; previousIndex < previousCount; ++previousIndex) {
				unsigned int cost;
				int unionArea;

				if (previousRects[previousIndex].width <= 0)
					continue;
				Movie_ComputeRectUnionAndIntersection(&previousRects[previousIndex], &candidate, &rectUnion,
													  &intersection);
				unionArea = rectUnion.width * rectUnion.height;
				cost = intersection.width * intersection.height -
					   previousRects[previousIndex].height * previousRects[previousIndex].width -
					   candidateArea + unionArea;
				if (cost < bestCost) {
					bestCost = cost;
					bestUnionArea = unionArea;
					bestUnion = rectUnion;
					bestRect = &previousRects[previousIndex];
					if (cost == 0)
						break;
				}
			}

			if (bestCost != 0 && bestUnionArea / bestCost < 20) {
				*output++ = candidate;
				++outputCount;
				if (bestCost != 0x1000000) {
					candidate = *bestRect;
					bestRect->width = -bestRect->width;
				}
			} else {
				candidate = bestUnion;
				bestRect->width = -bestRect->width;
			}
			--remaining;
		} while (remaining != 0);
	}

	g_movieMergedDirtyRects[outputCount] = candidate;
	*mergedCount = outputCount + 1;
	for (index = 0; index < currentCount; ++index)
		currentRects[index].width = abs(currentRects[index].width);
	for (restoreIndex = 0; restoreIndex < previousCount; ++restoreIndex)
		previousRects[restoreIndex].width = abs(previousRects[restoreIndex].width);
}

/* Computes the bounding union and overlap intersection of two movie
 * rectangles; clears the intersection when they do not overlap. The return
 * value is incidental (last computed bottom edge). */
// FUNCTION: XVT 0x4EFBD0
int Movie_ComputeRectUnionAndIntersection(const MovieDirtyRect* a, const MovieDirtyRect* b,
										  MovieDirtyRect* unionRect, MovieDirtyRect* intersectionRect) {
	int x;
	int width;
	int y;
	int height;

	if (b->x < a->x) {
		unionRect->width = a->width - b->x + a->x;
		unionRect->x = b->x;
		intersectionRect->x = a->x;
		intersectionRect->width = a->width;
	} else {
		intersectionRect->width = a->width - b->x + a->x;
		intersectionRect->x = b->x;
		unionRect->x = a->x;
		unionRect->width = a->width;
	}
	if (b->y < a->y) {
		unionRect->height = a->height - b->y + a->y;
		unionRect->y = b->y;
		intersectionRect->y = a->y;
		intersectionRect->height = a->height;
	} else {
		intersectionRect->height = a->height - b->y + a->y;
		intersectionRect->y = b->y;
		unionRect->y = a->y;
		unionRect->height = a->height;
	}
	width = b->width;
	x = b->x;
	if (a->x + a->width < x + width)
		unionRect->width = width - unionRect->x + x;
	else
		intersectionRect->width = width - intersectionRect->x + x;
	y = b->y;
	height = b->height;
	if (a->y + a->height < y + height)
		unionRect->height = y - unionRect->y + height;
	else
		intersectionRect->height = y - intersectionRect->y + height;
	if (intersectionRect->width <= 0 || intersectionRect->height <= 0) {
		intersectionRect->x = 0;
		intersectionRect->y = 0;
		intersectionRect->width = 0;
		intersectionRect->height = 0;
		return 0;
	}
	return y + height;
}

// FUNCTION: XVT 0x4EFCE0
int Movie_Play(const char* name, int synchronizeMultiplayer) {
#ifdef XVT_MODERN
	int result;
	if (XvtMovieTask_TakeResult(&result))
		return result;
	return XvtMovieTask_Begin(name, synchronizeMultiplayer);
#else
	enum {
		MOVIE_PATH_CAPACITY = 128,
		MOVIE_NAME_CAPACITY = 256,
		MOVIE_DISPLAY_WIDTH = 640,
		MOVIE_DISPLAY_HEIGHT = 480,
		MOVIE_STATUS_NOT_FOUND = 2,
	};

	MoviePlaybackParams playbackParams;
	char moviePath[MOVIE_PATH_CAPACITY];
	char movieName[MOVIE_NAME_CAPACITY];
	XvtFile* probeStream;

	strcpy(movieName, name);
	strcpy(moviePath, "movies\\");
	strcat(moviePath, movieName);
	strcat(moviePath, ".smk");
	probeStream = File_RawOpen(moviePath, g_fileModeReadBinary);
	if (synchronizeMultiplayer != 0 &&
		g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (probeStream == NULL) {
			strcpy(moviePath, "d:\\movies\\");
			moviePath[0] = File_GetCdDriveLetter();
			strcat(moviePath, movieName);
			strcat(moviePath, ".smk");
			probeStream = File_RawOpen(moviePath, g_fileModeReadBinary);
			if (probeStream == NULL) {
				strcpy(movieName, "Flyby1a");
			} else {
				File_RawClose(probeStream);
			}
		} else {
			File_RawClose(probeStream);
		}
	} else {
		if (probeStream == NULL) {
			strcpy(moviePath, "d:\\movies\\");
			moviePath[0] = File_GetCdDriveLetter();
			strcat(moviePath, movieName);
			strcat(moviePath, ".smk");
			for (;;) {
				probeStream = File_RawOpen(moviePath, g_fileModeReadBinary);
				if (probeStream != NULL)
					break;
				FrontendDisplay_EnableOffscreenRestore();
				if (FrontendDialog_ShowConfirmDialog(
						FrontendString_Get(FRONTSTR_827_PLEASE_INSERT_BALANCE_OF_POWER_CD),
						FrontendString_Get(FRONTSTR_828_INTO_YOUR_CD_ROM_DRIVE),
						FrontendString_Get(FRONTSTR_829_EMPTY_TRANSLATION_PLACEHOLDER),
						FrontendString_Get(FRONTSTR_523_OKAY),
						FrontendString_Get(FRONTSTR_019_CANCEL)) == 0) {
					FrontendDisplay_DisableOffscreenRestore();
					FrontendDisplay_ClearBackBuffer();
					return MOVIE_STATUS_NOT_FOUND;
				}
				FrontendDisplay_DisableOffscreenRestore();
				FrontendDisplay_ClearBackBuffer();
			}
		}
		File_RawClose(probeStream);
	}

	playbackParams.movieName = movieName;
	playbackParams.primarySurface = g_frontState.primarySurface;
	playbackParams.displayWidth = MOVIE_DISPLAY_WIDTH;
	playbackParams.displayHeight = MOVIE_DISPLAY_HEIGHT;
	if (g_optNoFullscreen == 0 && g_noPageFlip == 0)
		playbackParams.decodeSurface = g_frontState.offscreenSurface;
	else
		playbackParams.decodeSurface = g_frontState.backBufferSurface;
	playbackParams.palette = g_frontState.ddPalette;
	playbackParams.directSound = g_frontState.frontendDirectSound;
	playbackParams.window = g_frontState.hWnd;
	if (synchronizeMultiplayer != 0 &&
		g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		playbackParams.inputCallback = (MovieInputCallback)Movie_MultiplayerFrameCallback;
		playbackParams.progressCallback = (MovieProgressCallback)Movie_MultiplayerSyncCallback;
	} else {
		playbackParams.inputCallback = (MovieInputCallback)Movie_SingleplayerFrameCallback;
		playbackParams.progressCallback = NULL;
	}
	return Movie_RunSmackerPlayback(&playbackParams);
#endif
}

// FUNCTION: XVT 0x4F0070
int Movie_SingleplayerFrameCallback(int context, unsigned int eventCode, int keyCode, int eventArg3,
									int eventArg4, uint32_t* playbackFlag) {
	(void)context;
	(void)eventArg3;
	(void)eventArg4;

	switch (eventCode) {
		case 0x0F:
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_ClearOffscreenSurface();
			FrontendDisplay_PresentFrame();
			FrontendDisplay_ClearBackBuffer();
			return 1;
		case 0x102:
			switch (keyCode) {
				case 8:
				case 13:
				case 27:
				case 32:
					FrontendDisplay_SetWndProcMode(g_moviePreviousWndProcMode);
					break;
				default:
					break;
			}
			*playbackFlag = 0;
			return 0;
		case 0x202:
		case 0x205:
		case 0x208:
			FrontendDisplay_SetWndProcMode(g_moviePreviousWndProcMode);
			*playbackFlag = 0;
			return 0;
		default:
			return 1;
	}
}

// FUNCTION: XVT 0x4F0140
int Movie_MultiplayerFrameCallback(int context, unsigned int eventCode, int keyCode, int eventArg3,
								   int eventArg4, uint32_t* playbackFlag) {
	enum {
		moviePaintEvent = 15,
		movieKeyDownEvent = 0x102,
		movieMouseAbortEvent = 0x202,
		movieMouseContinueEvent = 0x205,
		movieMouseCloseEvent = 0x208,
	};

	int stopPlayback = 0;
	int packet[2];

	(void)context;
	(void)eventArg3;
	(void)eventArg4;
	switch (eventCode) {
		case moviePaintEvent:
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_ClearOffscreenSurface();
			FrontendDisplay_PresentFrame();
			FrontendDisplay_ClearBackBuffer();
			break;
		case movieKeyDownEvent:
			switch (keyCode) {
				case 8:
				case 13:
				case 27:
				case 32:
					stopPlayback = 1;
					break;
				case 'C':
				case 'c':
					if (g_moviePlaybackCompletionState == 2 && Net_IsHost() != 0) {
						packet[0] = NET_PACKET_MOVIE_SYNC;
						packet[1] = 1;
						Net_SendPacketAndFlush(0, packet, sizeof(packet));
					}
					break;
				case 'E':
				case 'e':
					if (g_moviePlaybackCompletionState == 2 && Net_IsHost() == 0) {
						g_movieSkipRequested = -1;
						FrontendDisplay_SetWndProcMode(g_moviePreviousWndProcMode);
					}
					break;
				default:
					break;
			}
			break;
		case movieMouseAbortEvent:
		case movieMouseContinueEvent:
		case movieMouseCloseEvent:
			stopPlayback = 1;
			break;
		default:
			break;
	}
	if (stopPlayback == 1) {
		packet[0] = NET_PACKET_MOVIE_SYNC;
		packet[1] = 0;
		Net_SendPacketAndFlush(0, packet, sizeof(packet));
		*playbackFlag = 0;
		return 0;
	}
	return 1;
}

// FUNCTION: XVT 0x4F02F0
void Movie_DrawMultiplayerSyncStatus(void) {
	RECT rect;
	const char* statusStrings[2];
	char text[100];
	unsigned int playerIndex;
	unsigned int displayWidth;
	unsigned int horizontalMargin;
	unsigned int cellWidth;
	unsigned int readyPlayerCount;
	unsigned int rosterIndex;
	int color;
	int localPlayerWaiting;

	playerIndex = 0;
	while (playerIndex < 8) {
		if (Net_GetLocalPlayerId() == g_movieMultiplayerSyncPlayers[playerIndex].playerId)
			break;
		++playerIndex;
	}
	if (playerIndex < 8) {
		localPlayerWaiting = g_movieMultiplayerSyncPlayers[playerIndex].isWaiting;
	} else {
		localPlayerWaiting = g_missionBriefingActive;
	}
	if (localPlayerWaiting != 1)
		return;

	rect.left = 0;
	rect.top = 0;
	rect.right = g_moviePlaybackParams->displayWidth - 1;
	rect.bottom = g_movieY - 1;
	statusStrings[0] = FrontendString_Get(FRONTSTR_804_WATCHING);
	statusStrings[1] = FrontendString_Get(FRONTSTR_805_WAITING);
	color = FrontendDisplay_PackRGB(0xFF, 0xFF, 0xFF);
	displayWidth = g_moviePlaybackParams->displayWidth;
	horizontalMargin = displayWidth / 20;
	cellWidth = (displayWidth - 2 * horizontalMargin) >> 2;
	readyPlayerCount = Net_CountReadyPlayers();
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 0), -1);
	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		if (g_movieMultiplayerSyncPlayers[playerIndex].playerId != 0) {
			rect.left = horizontalMargin + cellWidth * (playerIndex & 3);
			rect.top = ((unsigned int)g_movieY >> 1) * (playerIndex >> 2);
			rect.right = rect.left;
			rect.right += cellWidth;
			rect.bottom = rect.top + ((unsigned int)g_movieY >> 1);
			rosterIndex = 0;
			if (readyPlayerCount != 0) {
				for (;;) {
					if (g_mpRoster[rosterIndex].playerId ==
						g_movieMultiplayerSyncPlayers[playerIndex].playerId) {
						strcpy(text, g_mpRoster[rosterIndex].name);
						break;
					}
					++rosterIndex;
					if (readyPlayerCount > rosterIndex)
						continue;
					break;
				}
			}
			strcat(text, statusStrings[g_movieMultiplayerSyncPlayers[playerIndex].isWaiting]);
			FrontendText_DrawCentered(12, text, &rect, color);
		}
	}
	FrontendDisplay_UnlockBackBuffer();
	if (g_optNoFullscreen != 0 || g_noPageFlip != 0)
		Movie_BlitRectToDisplay(0, 0, g_moviePlaybackParams->displayWidth, g_movieY);
}

// FUNCTION: XVT 0x4F0510
void Movie_UpdateMultiplayerSyncTimeout(void) {
	enum {
		HOST_TIMEOUT_MS = 5000,
		CLIENT_TIMEOUT_MS = 20000,
		TIMEOUT_COMPLETION_STATE = 2,
		PROMPT_FONT_SIZE = 12,
		RGB_CHANNEL_MAX = 0xFF
	};

	unsigned int timeoutMs;
	RECT rect;
	int packet[2];

	timeoutMs = Net_IsHost() != 0 ? HOST_TIMEOUT_MS : CLIENT_TIMEOUT_MS;
	if (g_movieMultiplayerSyncDeadlineTick == 0) {
		packet[0] = NET_PACKET_MOVIE_SYNC;
		packet[1] = 0;
		Net_SendPacketAndFlush(0, packet, sizeof(packet));
		g_movieMultiplayerSyncDeadlineTick = timeoutMs + GetTickCount();
		if (g_movieMultiplayerSyncDeadlineTick == 0)
			++g_movieMultiplayerSyncDeadlineTick;
		rect.left = g_movieX;
		rect.right = g_moviePlaybackParams->displayWidth - (int)g_movieRightMargin - 1;
		rect.top = g_movieY;
		rect.bottom = g_moviePlaybackParams->displayHeight - (int)g_movieBottomMargin - 1;
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 0), -1);
		FrontendDisplay_UnlockBackBuffer();
		if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
			FrontendDisplay_PresentFrame();
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 0), -1);
			FrontendDisplay_UnlockBackBuffer();
			return;
		}
		Movie_BlitRectToDisplay(g_movieX, g_movieY,
								g_moviePlaybackParams->displayWidth - (int)g_movieRightMargin,
								g_moviePlaybackParams->displayHeight - (int)g_movieBottomMargin);
		return;
	}

	{
		unsigned int deadlineTick;
		const char* message;

		deadlineTick = g_movieMultiplayerSyncDeadlineTick;
		if (deadlineTick - GetTickCount() <= timeoutMs)
			return;
		g_moviePlaybackCompletionState = TIMEOUT_COMPLETION_STATE;
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		rect.left = 0;
		rect.top = g_moviePlaybackParams->displayHeight - (int)g_movieBottomMargin;
		rect.right = g_moviePlaybackParams->displayWidth - 1;
		rect.bottom = g_moviePlaybackParams->displayHeight - 1;
		FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 0), -1);
		if (Net_IsHost() != 0)
			message = FrontendString_Get(FRONTSTR_807_STILL_WAITING_FOR_OTHERS_HIT_C_TO_CONTINUE_THE_GAME);
		else
			message = FrontendString_Get(FRONTSTR_806_STILL_WAITING_FOR_OTHERS_HIT_E_TO_EXIT_THE_GAME);
		FrontendText_DrawCentered(PROMPT_FONT_SIZE, message, &rect,
								  FrontendDisplay_PackRGB(RGB_CHANNEL_MAX, RGB_CHANNEL_MAX, RGB_CHANNEL_MAX));
		FrontendDisplay_UnlockBackBuffer();
		if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
			FrontendDisplay_PresentFrame();
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 0), -1);
			FrontendText_DrawCentered(
				PROMPT_FONT_SIZE, message, &rect,
				FrontendDisplay_PackRGB(RGB_CHANNEL_MAX, RGB_CHANNEL_MAX, RGB_CHANNEL_MAX));
			FrontendDisplay_UnlockBackBuffer();
			FrontendDisplay_PresentFrame();
			return;
		}
		Movie_BlitRectToDisplay(0, g_moviePlaybackParams->displayHeight - (int)g_movieBottomMargin,
								g_moviePlaybackParams->displayWidth, (int)g_movieBottomMargin);
	}
}

// FUNCTION: XVT 0x4F07D0
int Movie_MultiplayerSyncCallback(int initialize) {
	enum { MAX_MULTIPLAYER_PLAYERS = 8 };

	unsigned int playerIndex;
	unsigned int readyPlayerCount;
	int waitingPlayerCount;

	if (initialize == 0) {
		readyPlayerCount = Net_CountReadyPlayers();
		for (playerIndex = 0; playerIndex < MAX_MULTIPLAYER_PLAYERS; ++playerIndex) {
			if (playerIndex < readyPlayerCount) {
				g_movieMultiplayerSyncPlayers[playerIndex].playerId = g_mpRoster[playerIndex].playerId;
				g_movieMultiplayerSyncPlayers[playerIndex].isWaiting = 0;
			} else {
				g_movieMultiplayerSyncPlayers[playerIndex].playerId = 0;
			}
		}
		g_movieMultiplayerSyncDeadlineTick = 0;
	}

	FrontendNet_ProcessNetworkPackets();
	waitingPlayerCount = 0;
	for (playerIndex = 0; playerIndex < MAX_MULTIPLAYER_PLAYERS; ++playerIndex) {
		if (g_movieMultiplayerSyncPlayers[playerIndex].playerId != 0 &&
			g_movieMultiplayerSyncPlayers[playerIndex].isWaiting == 0)
			++waitingPlayerCount;
	}
	if (waitingPlayerCount == 0)
		return 1;

	Movie_DrawMultiplayerSyncStatus();
	if (g_moviePlaybackCompletionState == 1)
		Movie_UpdateMultiplayerSyncTimeout();
	return 0;
}

// FUNCTION: XVT 0x4F0900
unsigned int Movie_ReadSubtitleCue(char* line1, char* line2, char* line3) {
	unsigned int frameNumber;
	int scanResult;

	if (g_movieSubtitleFile == NULL) {
		return 0;
	}
	scanResult = File_Scanf(g_movieSubtitleFile, "%u\n", &frameNumber);
	if (scanResult == 0 || scanResult == EOF) {
		line1[0] = '\0';
		return UINT16_MAX;
	}

	if (File_Gets(line1, 256, g_movieSubtitleFile) == NULL) {
		line1[0] = '\0';
	} else {
		if (line1[strlen(line1) - 1] == '\n') {
			line1[strlen(line1) - 1] = '\0';
		}
		if (line1[0] == '.') {
			line1[0] = '\0';
		}
	}

	if (File_Gets(line2, 256, g_movieSubtitleFile) == NULL) {
		line2[0] = '\0';
	} else {
		if (line2[strlen(line2) - 1] == '\n') {
			line2[strlen(line2) - 1] = '\0';
		}
		if (line2[0] == '.') {
			line2[0] = '\0';
		}
	}

	if (File_Gets(line3, 256, g_movieSubtitleFile) == NULL) {
		line3[0] = '\0';
	} else {
		if (line3[strlen(line3) - 1] == '\n') {
			line3[strlen(line3) - 1] = '\0';
		}
		if (line3[0] == '.') {
			line3[0] = '\0';
		}
	}

	return frameNumber;
}

// FUNCTION: XVT 0x4F0A50
void Movie_DrawSubtitles(unsigned int frameNumber) {
	RECT rect;
	unsigned int lineHeight;
	int textColor;

	if (g_movieSubtitleFile != NULL) {
		if (frameNumber == 0) {
			g_movieNextSubtitleFrame = 0;
			g_movieActiveSubtitleFrame = 0;
		}
		if (g_movieNextSubtitleFrame == frameNumber) {
			g_movieActiveSubtitleFrame = g_movieNextSubtitleFrame;
			g_movieNextSubtitleFrame =
				Movie_ReadSubtitleCue(g_frontendScratchBuffer, g_movieSubtitleLine2, g_movieSubtitleLine3);
		}
		if (g_movieActiveSubtitleFrame == frameNumber ||
			g_movieActiveSubtitleFrame - frameNumber == (unsigned int)-1) {
			textColor = FrontendDisplay_PackRGB(0xFF, 0xFF, 0xFF);
			lineHeight = g_movieBottomMargin / 3;
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			rect.left = 0;
			rect.top = g_moviePlaybackParams->displayHeight - g_movieBottomMargin;
			rect.right = g_moviePlaybackParams->displayWidth - 1;
			rect.bottom = g_moviePlaybackParams->displayHeight - 1;
			FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 0), -1);
			rect.bottom = rect.top + lineHeight;
			FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, textColor);
			rect.top = rect.bottom;
			rect.bottom += lineHeight;
			FrontendText_DrawCentered(12, g_movieSubtitleLine2, &rect, textColor);
			rect.top = rect.bottom;
			rect.bottom += lineHeight;
			FrontendText_DrawCentered(12, g_movieSubtitleLine3, &rect, textColor);
			FrontendDisplay_UnlockBackBuffer();
			if (g_optNoFullscreen != 0) {
				Movie_BlitRectToDisplay(0, g_moviePlaybackParams->displayHeight - g_movieBottomMargin,
										g_moviePlaybackParams->displayWidth, g_movieBottomMargin);
			}
		}
	}
}

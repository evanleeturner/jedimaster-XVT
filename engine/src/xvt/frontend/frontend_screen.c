#include "xvt/frontend/frontend_screen.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_frontend.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#endif
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"

#include <stdlib.h>
#include <string.h>

// FUNCTION: XVT 0x4DC330
void FrontendScreen_SetCallbacks(FrontendScreenUpdateFn updateFn, FrontendScreenExitFn exitFn) {
	struct FrontendScreenState* state;

	state = &g_frontState.screenStates[g_frontState.screenStackTop];
	state->updateFn = updateFn;
	state = &g_frontState.screenStates[g_frontState.screenStackTop];
	state->exitFn = exitFn;
	g_frontState.frameCounter = -1;
	g_frontState.screenCallbacksDirty = 1;
}

// FUNCTION: XVT 0x4DC380
int FrontendScreen_QueuePush(int (*updateFn)(int), const RECT* screenRect) {
	g_frontState.pendingScreenUpdateFn = updateFn;
	FrontendDraw_RectCopy(&g_frontState.pendingScreenRect, screenRect);
	return 1;
}

// FUNCTION: XVT 0x4DC3B0
int FrontendScreen_RunModal(FrontendScreenUpdateFn updateFn, RECT* screenRect) {
#ifdef XVT_MODERN
	return XvtDialog_Begin(updateFn, screenRect);
#else
	enum { FRAME_FINISHED = 1, FRAME_QUIT = 2 };

	int frameResult;

	FrontendDisplay_UnlockBackBuffer();
	FrontendScreen_PushState(updateFn, screenRect);
	g_frontState.frameCounter = 0;
	memset(g_frontState.joystickButtonReleased[0], 0, sizeof(g_frontState.joystickButtonReleased[0]));
	memset(g_frontState.joystickButtonReleased[1], 0, sizeof(g_frontState.joystickButtonReleased[1]));
	if (g_frontState.glyphScratchTtl != 0)
		--g_frontState.glyphScratchTtl;
	g_frontState.mouseClickLatch = 0;
	g_frontState.mouseRightClickLatch = 0;
	for (;;) {
		frameResult = FrontendDisplay_RunFrame();
		if (frameResult == FRAME_FINISHED)
			break;
		if (frameResult == FRAME_QUIT)
			exit(0);
	}
	Keyboard_FlushCharBuffer();
	FrontendScreen_PopState();
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	g_frontState.mouseClickLatch = 0;
	g_frontState.mouseRightClickLatch = 0;
	return 1;
#endif
}

// FUNCTION: XVT 0x4DC450
int FrontendScreen_PushState(FrontendScreenUpdateFn updateFn, RECT* screenRect) {
	RECT rect;
	int slot;
	int width;
	int height;
	int wasBackBufferLocked;
	uint8_t* pixels;
	int x;
	int y;
	int displayBpp;

	if (g_frontState.screenStackTop >= FRONTEND_SCREEN_MAX_STACK - 1)
		return 0;

	slot = g_frontState.screenStackTop;
	g_frontState.screenStates[slot].savedFrameCounter = g_frontState.frameCounter;
	g_frontState.screenStates[slot].savedClipMinX = g_frontState.clipMinX;
	g_frontState.screenStates[slot].savedClipMaxX = g_frontState.clipMaxX;
	g_frontState.screenStates[slot].savedClipMinY = g_frontState.clipMinY;
	g_frontState.screenStates[slot].savedClipMaxY = g_frontState.clipMaxY;

	if (screenRect != NULL) {
		if (screenRect->left < 0)
			screenRect->left = 0;
		if (screenRect->top < 0)
			screenRect->top = 0;
		if (screenRect->right >= 640)
			screenRect->right = 639;
		if (screenRect->bottom >= 480)
			screenRect->bottom = 479;
		if (screenRect->left > screenRect->right || screenRect->bottom < screenRect->top)
			return 0;

		FrontendDraw_RectCopy(&g_frontState.screenStates[slot].savedRect, screenRect);
		width = screenRect->right - screenRect->left + 1;
		height = screenRect->bottom - screenRect->top + 1;
		displayBpp = g_frontState.displayBpp;
		switch (displayBpp) {
			case 8: {
				uint8_t* destination;
				uint8_t* source;

				pixels = (uint8_t*)malloc(width * height);
				if (pixels == NULL)
					return 0;

				wasBackBufferLocked = g_frontState.backBufferLocked;
				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_LockOffscreenSurface();
				else
					g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();

				destination = pixels;
				source =
					g_drawSurfacePtr + screenRect->left + screenRect->top * g_frontState.drawSurfacePitch;
				for (y = 0; y < height; ++y) {
					memcpy(destination, source, width);
					source += g_frontState.drawSurfacePitch;
					destination += width;
				}

				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_UnlockOffscreenSurface(1);
				if (wasBackBufferLocked == 0)
					FrontendDisplay_UnlockBackBuffer();
				break;
			}

			case 16: {
				uint16_t* destination;
				uint16_t* source;

				pixels = (uint8_t*)malloc(2 * width * height);
				if (pixels == NULL)
					return 0;

				wasBackBufferLocked = g_frontState.backBufferLocked;
				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_LockOffscreenSurface();
				else
					g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();

				destination = (uint16_t*)pixels;
				source = (uint16_t*)(g_drawSurfacePtr + 2 * screenRect->left +
									 screenRect->top * g_frontState.drawSurfacePitch);
				for (y = 0; y < height; ++y) {
					for (x = 0; x < width; ++x)
						destination[x] = source[x];
					source += g_frontState.drawSurfacePitch >> 1;
					destination += width;
				}

				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_UnlockOffscreenSurface(1);
				if (wasBackBufferLocked == 0)
					FrontendDisplay_UnlockBackBuffer();
				break;
			}
		}

#ifdef XVT_MODERN
		XvtRenderFrontend_Screen(slot, 0);
#endif
		g_frontState.screenStates[slot].savedImage.width = width;
		g_frontState.screenStates[slot].savedImage.height = height;
		g_frontState.screenStates[slot].savedImage.pixels = pixels;
		displayBpp = g_frontState.displayBpp;
		switch (displayBpp) {
			case 8:
				g_frontState.screenStates[slot].savedImage.pixelCount = width * height;
				g_frontState.screenStates[slot].savedImage.isCompressed = 0;
				FrontImage_CompressRLE(&g_frontState.screenStates[slot].savedImage);
				break;

			case 16:
				g_frontState.screenStates[slot].savedImage.pixelCount = 2 * width * height;
				g_frontState.screenStates[slot].savedImage.isCompressed = 0;
				break;
		}

		if (g_frontState.offscreenRestoreEnabled != 0)
			FrontendDisplay_SaveBackBuffer();
		FrontendDisplay_SetScreenClipRect640x480(screenRect);
	} else {
		g_frontState.screenStates[slot].savedImage.width = 0;
		g_frontState.screenStates[slot].savedImage.height = 0;
		g_frontState.screenStates[slot].savedImage.pixels = NULL;
		g_frontState.screenStates[slot].savedImage.pixelCount = 0;
		FrontendDraw_RectAssign(&rect, 0, 0, 639, 479);
		FrontendDisplay_SetScreenClipRect640x480(&rect);
	}

	++g_frontState.screenStackTop;
	g_frontState.screenStates[g_frontState.screenStackTop].updateFn = updateFn;
	g_frontState.frameCounter = -1;
	return 1;
}

// FUNCTION: XVT 0x4DC7E0
void FrontendScreen_PopState(void) {
	int width;
	int height;
	int slot;
	uint8_t* pixels;
	int rowBytes;
	int wasBackBufferLocked;
	RECT rect;
	int column;
	int displayBpp;
	uint8_t* rowDestination;
	uint8_t* destination;
	uint8_t* source;

	if (g_frontState.screenStackTop == 0)
		return;

	slot = g_frontState.screenStackTop - 1;
	FrontendDraw_RectAssign(&rect, 0, 0, 640, 480);
	FrontendDisplay_SetScreenClipRect640x480(&rect);
	if (g_frontState.screenStates[slot].savedImage.pixelCount > 0) {

#ifdef XVT_MODERN
		XvtRenderFrontend_Screen(slot, 1);
		XvtRenderFrontend_Suppress(1);
#endif
		displayBpp = g_frontState.displayBpp;
		switch (displayBpp) {
			case 8: {
				wasBackBufferLocked = g_frontState.backBufferLocked;
				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_LockOffscreenSurface();
				else
					g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();

				FrontImage_BlitOpaque(&g_frontState.screenStates[slot].savedImage,
									  g_frontState.screenStates[slot].savedRect.left,
									  g_frontState.screenStates[slot].savedRect.top);
				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_UnlockOffscreenSurface(1);
				if (wasBackBufferLocked == 0)
					FrontendDisplay_UnlockBackBuffer();
				break;
			}
			case 16: {
				wasBackBufferLocked = g_frontState.backBufferLocked;
				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_LockOffscreenSurface();
				else
					g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();

				width = g_frontState.screenStates[slot].savedImage.width;
				height = g_frontState.screenStates[slot].savedImage.height;
				pixels = g_frontState.screenStates[slot].savedImage.pixels;
				rowDestination = &g_drawSurfacePtr[2 * g_frontState.screenStates[slot].savedRect.left +
												   g_frontState.drawSurfacePitch *
													   g_frontState.screenStates[slot].savedRect.top];
				if (height > 0) {
					rowBytes = width + width;
					do {
						if (width > 0) {
							source = pixels;
							destination = rowDestination;
							for (column = width; column != 0; --column) {
								*(uint16_t*)destination = *(uint16_t*)source;
								source += 2;
								destination += 2;
							}
						}
						pixels += rowBytes;
						rowDestination += g_frontState.drawSurfacePitch & ~1;
						--height;
					} while (height != 0);
				}
				if (g_frontState.offscreenRestoreEnabled != 0)
					FrontendDisplay_UnlockOffscreenSurface(1);
				if (wasBackBufferLocked == 0)
					FrontendDisplay_UnlockBackBuffer();
				break;
			}
		}

#ifdef XVT_MODERN
		XvtRenderFrontend_Suppress(0);
		if (g_frontState.offscreenRestoreEnabled)
			XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN, XVT_TARGET_FRONT_BACKUP);
		XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACK);
#endif
		free(g_frontState.screenStates[slot].savedImage.pixels);
	}

	FrontendDisplay_SetScreenClipRect640x480((const RECT*)&g_frontState.screenStates[slot].savedClipMinX);
	g_frontState.screenStackTop = slot;
	g_frontState.frameCounter = g_frontState.screenStates[slot].savedFrameCounter;
}

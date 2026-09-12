#include "xvt/frontend/frontend_cursor.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_frontend.h"
#endif
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"

#ifdef XVT_MODERN
#include "aeron/aeron.h"
#include "xvt_runtime/runtime/presentation.h"
#else
__declspec(dllimport) int __stdcall ShowCursor(int show);
__declspec(dllimport) int __stdcall SetCursorPos(int x, int y);
#endif

#include <string.h>

// GLOBAL: XVT 0x52C100
const uint8_t g_defaultCursorBitmap[100] = {
	1,    1,    1,    1,    1,    1,    1,    1,    1,    0,    1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	1,    0,    0,    1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1,    0,    0,    0,    1,    0xFF, 0xFF, 0xFF,
	0xFF, 1,    0,    0,    0,    0,    1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1,    0,    0,    0,    1,
	0xFF, 0xFF, 1,    0xFF, 0xFF, 0xFF, 1,    0,    0,    1,    0xFF, 1,    0,    1,    0xFF, 0xFF, 0xFF,
	1,    0,    1,    1,    0,    0,    0,    1,    0xFF, 0xFF, 0xFF, 1,    1,    0,    0,    0,    0,
	0,    1,    0xFF, 0xFF, 1,    0,    0,    0,    0,    0,    0,    0,    1,    1,    0,
};

// FUNCTION: XVT 0x4B49A0
int FrontendCursor_SetImageFromResourceName(const char* resourceName, void* saveBuf) {
	RECT resourceRect;
	int resourceIndex;

	resourceIndex = FrontImage_FindResourceByName(resourceName);
	if (resourceIndex == -1)
		return 0;
	if (g_frontState.resourceTable[resourceIndex].image->isCompressed != 0)
		return 0;
	FrontImage_GetResourceRect(resourceName, &resourceRect);
	g_frontState.cursorMaskPixels = g_frontState.resourceTable[resourceIndex].image->pixels;
	g_frontState.cursorSaveBuf = (uint8_t*)saveBuf;
	g_frontState.cursorWidth = resourceRect.right - resourceRect.left + 1;
	g_frontState.cursorHeight = resourceRect.bottom - resourceRect.top + 1;
	strcpy(g_frontState.cursorSpriteName, resourceName);
#ifdef XVT_MODERN
	return 0;
#endif
}

// FUNCTION: XVT 0x4DDC40
void FrontendCursor_Init(void) {
	g_frontState.cursorWidth = 10;
	g_frontState.cursorHeight = 10;
	g_frontState.cursorMaskPixels = g_frontState.cursorDefaultMask;
	g_frontState.cursorSaveBuf = g_frontState.cursorDefaultSaveBuf;
	memcpy(g_frontState.cursorDefaultMask, g_defaultCursorBitmap, sizeof(g_frontState.cursorDefaultMask));
	memset(g_frontState.cursorSpriteName, 0, sizeof(g_frontState.cursorSpriteName));
}

// FUNCTION: XVT 0x4DDC90
void FrontendCursor_Draw(void) {
	RECT clippedRect;
	RECT originalRect;
	int cursorWidth;
	int cursorHeight;
	int visibleWidth;
	int visibleHeight;
	int displayBpp;
	uint8_t* backBuffer;
	uint8_t* cursorPixels;
	uint8_t* saveBuffer;
	uint8_t* destination;
	int rowsRemaining;
	int column;
	int maskValue;

	if (g_frontState.mouseX < 0 || g_frontState.mouseX >= 640 || g_frontState.mouseY < 0 ||
		g_frontState.mouseY >= 480)
		return;

	cursorHeight = g_frontState.cursorHeight;
	clippedRect.left = 0;
	cursorWidth = g_frontState.cursorWidth;
	clippedRect.top = 0;
	clippedRect.right = g_frontState.cursorWidth - 1;
	clippedRect.bottom = g_frontState.cursorHeight - 1;
	FrontendDraw_RectOffsetXY(&clippedRect, g_frontState.mouseX, g_frontState.mouseY);
	FrontendDraw_RectCopy(&originalRect, &clippedRect);
	FrontendDraw_RectClipToBounds(&clippedRect);
	visibleWidth = clippedRect.right - originalRect.right + cursorWidth;
	visibleHeight = cursorHeight + clippedRect.bottom - originalRect.bottom;
	backBuffer = FrontendDisplay_LockBackBuffer();
	cursorPixels = g_frontState.cursorMaskPixels;
	saveBuffer = g_frontState.cursorSaveBuf;
	displayBpp = g_frontState.displayBpp;
	g_drawSurfacePtr = backBuffer;

#ifdef XVT_MODERN
	XvtRenderFrontend_Cursor(0);
#endif
	if (g_frontState.cursorSpriteName[0] != '\0') {
		switch (displayBpp) {
			case 8: {
				destination =
					&backBuffer[g_frontState.mouseX + g_frontState.mouseY * g_frontState.drawSurfacePitch];
				if (visibleHeight > 0) {
					rowsRemaining = visibleHeight;
					do {
						memcpy(saveBuffer, destination, visibleWidth);
						saveBuffer += cursorWidth;
						destination += g_frontState.drawSurfacePitch;
						--rowsRemaining;
					} while (rowsRemaining != 0);
				}
				break;
			}
			case 16: {
				destination = &backBuffer[2 * g_frontState.mouseX +
										  g_frontState.mouseY * g_frontState.drawSurfacePitch];
				if (visibleHeight > 0) {
					rowsRemaining = visibleHeight;
					do {
						if (visibleWidth > 0) {
							uint16_t* sourcePixel;
							uint16_t* savedPixel;
							int pixelsRemaining;

							sourcePixel = (uint16_t*)destination;
							savedPixel = (uint16_t*)saveBuffer;
							pixelsRemaining = visibleWidth;
							do {
								*savedPixel++ = *sourcePixel++;
								--pixelsRemaining;
							} while (pixelsRemaining != 0);
						}
						saveBuffer += 2 * cursorWidth;
						destination += g_frontState.drawSurfacePitch & ~1;
						--rowsRemaining;
					} while (rowsRemaining != 0);
				}
				break;
			}
			default:
				break;
		}
		FrontImage_DrawSprite(g_frontState.cursorSpriteName, g_frontState.mouseX, g_frontState.mouseY);
	} else {
		switch (displayBpp) {
			case 8: {
				destination =
					&backBuffer[g_frontState.mouseX + g_frontState.mouseY * g_frontState.drawSurfacePitch];
				if (visibleHeight > 0) {
					rowsRemaining = visibleHeight;
					do {
						memcpy(saveBuffer, destination, visibleWidth);
						for (column = 0; column < visibleWidth; ++column) {
							if (cursorPixels[column] != 0)
								destination[column] = cursorPixels[column];
						}
						saveBuffer += cursorWidth;
						cursorPixels += cursorWidth;
						destination += g_frontState.drawSurfacePitch;
						--rowsRemaining;
					} while (rowsRemaining != 0);
				}
				break;
			}
			case 16: {
				destination = &backBuffer[2 * g_frontState.mouseX +
										  g_frontState.mouseY * g_frontState.drawSurfacePitch];
				if (visibleHeight > 0) {
					rowsRemaining = visibleHeight;
					do {
						column = 0;
						if (visibleWidth > 0) {
							uint16_t* destinationPixel;
							uint16_t* savedPixel;

							destinationPixel = (uint16_t*)destination;
							savedPixel = (uint16_t*)saveBuffer;
							do {
								*savedPixel = *destinationPixel;
								maskValue = cursorPixels[column];
								switch (maskValue) {
									case 1:
										*destinationPixel = 31;
										break;
									case 0xFF:
										*destinationPixel = 0xFFFF;
										break;
									default:
										break;
								}
								++destinationPixel;
								++savedPixel;
								++column;
							} while (column < visibleWidth);
						}
						cursorPixels += cursorWidth;
						saveBuffer += 2 * cursorWidth;
						destination += g_frontState.drawSurfacePitch & ~1;
						--rowsRemaining;
					} while (rowsRemaining != 0);
				}
				break;
			}
			default:
				break;
		}
	}

#ifdef XVT_MODERN
	XvtRenderFrontend_EndCursor();
#endif
	FrontendDisplay_UnlockBackBuffer();
	g_frontState.cursorPrevDrawX = g_frontState.mouseX;
	g_frontState.cursorPrevDrawY = g_frontState.mouseY;
	g_frontState.cursorPrevDrawWidth = visibleWidth;
	g_frontState.cursorPrevDrawHeight = visibleHeight;
}

// FUNCTION: XVT 0x4DDF90
void FrontendCursor_Restore(void) {
	uint8_t* destination;
	uint8_t* source;
	int displayBpp;

	destination = FrontendDisplay_LockBackBuffer();
	source = g_frontState.cursorSaveBuf;
	displayBpp = g_frontState.displayBpp;
	g_drawSurfacePtr = destination;
#ifdef XVT_MODERN
	XvtRenderFrontend_Cursor(1);
#endif

	switch (displayBpp) {
		case 8: {
			int sourcePitch;
			int copyWidth;
			int remainingRows;
			int rowOffset;

			rowOffset = g_frontState.drawSurfacePitch;
			rowOffset *= g_frontState.cursorPrevDrawY;
			rowOffset += g_frontState.cursorPrevDrawX;
			destination += rowOffset;
			sourcePitch = g_frontState.cursorWidth;
			copyWidth = g_frontState.cursorPrevDrawWidth;
			if (g_frontState.cursorPrevDrawHeight > 0) {
				remainingRows = g_frontState.cursorPrevDrawHeight;
				do {
					memcpy(destination, source, copyWidth);
					source += sourcePitch;
					destination += g_frontState.drawSurfacePitch;
					--remainingRows;
				} while (remainingRows != 0);
			}
			break;
		}
		case 16: {
			int copyWidth;
			int sourcePitch;
			int remainingRows;
			int rowOffset;
			uint8_t* rowDestination;

			rowOffset = g_frontState.drawSurfacePitch;
			rowOffset *= g_frontState.cursorPrevDrawY;
			rowOffset += 2 * g_frontState.cursorPrevDrawX;
			rowDestination = destination + rowOffset;
			copyWidth = g_frontState.cursorPrevDrawWidth;
			sourcePitch = g_frontState.cursorWidth;
			if (g_frontState.cursorPrevDrawHeight > 0) {
				remainingRows = g_frontState.cursorPrevDrawHeight;
				do {
					if (copyWidth > 0) {
						uint16_t* sourcePixel;
						uint16_t* destinationPixel;
						int remainingPixels;

						sourcePixel = (uint16_t*)source;
						destinationPixel = (uint16_t*)rowDestination;
						remainingPixels = copyWidth;
						do {
							*destinationPixel++ = *sourcePixel++;
							--remainingPixels;
						} while (remainingPixels != 0);
					}
					source += 2 * sourcePitch;
					rowDestination += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
					--remainingRows;
				} while (remainingRows != 0);
			}
			break;
		}
		default:
			break;
	}
	FrontendDisplay_UnlockBackBuffer();
}

// FUNCTION: XVT 0x4DE090
int* FrontendCursor_GetPos(int* outX, int* outY) {
	*outX = g_frontState.mouseX;
	*outY = g_frontState.mouseY;
	return outX;
}

// FUNCTION: XVT 0x4DE0B0
int FrontendCursor_SetPos(int x, int y) {
	if (x > 640) {
		x = 640;
	} else if (x < 0) {
		x = 0;
	}

	if (y > 480) {
		y = 480;
	} else if (y < 0) {
		y = 0;
	}

	g_frontState.mouseX = x;
	g_frontState.mouseY = y;
#ifdef XVT_MODERN
	return XvtPresentation_WarpClassic(x, y);
#else
	return SetCursorPos(x, y);
#endif
}

// FUNCTION: XVT 0x4DE100
void FrontendCursor_Show(void) { g_frontState.cursorVisible = 1; }

// FUNCTION: XVT 0x4DE110
void FrontendCursor_Hide(void) { g_frontState.cursorVisible = 0; }

// FUNCTION: XVT 0x4DE120
int FrontendCursor_IsVisible(void) { return g_frontState.cursorVisible; }

// FUNCTION: XVT 0x4DE130
int FrontendCursor_GetDimensions(int* outWidth, int* outHeight) {
	*outWidth = g_frontState.cursorWidth;
	*outHeight = g_frontState.cursorHeight;
	return 1;
}

// FUNCTION: XVT 0x4DE150
int FrontendCursor_HideOsCursor(void) {
#ifdef XVT_MODERN
	return Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(0) >= 0) {
	}
	return 1;
#endif
}

// FUNCTION: XVT 0x4DE170
int FrontendCursor_ShowOsCursor(void) {
#ifdef XVT_MODERN
	/* The port renders its own cursor, so legacy show requests keep the host cursor hidden. */
	return Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(1) < 0) {
	}
	return 1;
#endif
}

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

/* The built-in cursor, a 10 by 10 arrow pointing up and left, one byte per
 * pixel, row by row: 0 is transparent, 1 the outline and 0xFF the fill.
 * FrontendCursor_Draw draws 1 as 31 (blue) and 0xFF as 0xFFFF (white) at 16
 * bits per pixel, and the byte as the palette index at 8. FrontendCursor_Init
 * copies it into g_frontState.cursorDefaultMask; the modern build's renderer
 * also reads it. */
// GLOBAL: XVT 0x52C100
const uint8_t g_defaultCursorBitmap[100] = {
	1,    1,    1,	  1,	1,    1,    1,	  1,	1,    0,    1,	  0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1,    0,	  0,	1,    0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 1,	  0,	0,    0,    1,	  0xFF, 0xFF, 0xFF, 0xFF, 1,
	0,    0,    0,	  0,	1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1,	  0,
	0,    0,    1,	  0xFF, 0xFF, 1,    0xFF, 0xFF, 0xFF, 1,    0,	  0,
	1,    0xFF, 1,	  0,	1,    0xFF, 0xFF, 0xFF, 1,    0,    1,	  1,
	0,    0,    0,	  1,	0xFF, 0xFF, 0xFF, 1,	1,    0,    0,	  0,
	0,    0,    1,	  0xFF, 0xFF, 1,    0,	  0,	0,    0,    0,	  0,
	0,    1,    1,	  0,
};

/* Makes the registered image resourceName the frontend cursor, with saveBuf as
 * the buffer for the pixels under it: points g_frontState.cursorMaskPixels at
 * the image's pixels, sets g_frontState.cursorSaveBuf, sets cursorWidth and
 * cursorHeight to the image's width and height plus 1 (the right and bottom of
 * FrontImage_GetResourceRect's rect, plus 1) and copies the name into
 * cursorSpriteName, after which FrontendCursor_Draw draws it with
 * FrontImage_DrawSprite. Returns 0, changing nothing, when no resource has the
 * name or its image is RLE-compressed. On success the modern build returns 0;
 * the original build's function ends without a return statement there. Does not
 * check saveBuf's size or the name's length (63 characters fit). */
// FUNCTION: XVT 0x4B49A0
int FrontendCursor_SetImageFromResourceName(const char *resourceName,
					    void *saveBuf)
{
	struct RECT resourceRect;
	int resourceIndex;

	resourceIndex = FrontImage_FindResourceByName(resourceName);
	if (resourceIndex == -1) {
		return 0;
	}
	if (g_frontState.resourceTable[resourceIndex].image->isCompressed !=
	    0) {
		return 0;
	}
	FrontImage_GetResourceRect(resourceName, &resourceRect);
	g_frontState.cursorMaskPixels =
		g_frontState.resourceTable[resourceIndex].image->pixels;
	g_frontState.cursorSaveBuf = (uint8_t *)saveBuf;
	g_frontState.cursorWidth = resourceRect.right - resourceRect.left + 1;
	g_frontState.cursorHeight = resourceRect.bottom - resourceRect.top + 1;
	strcpy(g_frontState.cursorSpriteName, resourceName);
#ifdef XVT_MODERN
	return 0;
#endif
}

/* Sets the frontend cursor to the built-in 10 by 10 arrow: copies
 * g_defaultCursorBitmap into g_frontState.cursorDefaultMask, points
 * cursorMaskPixels and cursorSaveBuf at the state's default mask and save
 * buffer, sets cursorWidth and cursorHeight to 10 and clears
 * cursorSpriteName. */
// FUNCTION: XVT 0x4DDC40
void FrontendCursor_Init(void)
{
	g_frontState.cursorWidth = 10;
	g_frontState.cursorHeight = 10;
	g_frontState.cursorMaskPixels = g_frontState.cursorDefaultMask;
	g_frontState.cursorSaveBuf = g_frontState.cursorDefaultSaveBuf;
	memcpy(g_frontState.cursorDefaultMask, g_defaultCursorBitmap,
	       sizeof(g_frontState.cursorDefaultMask));
	memset(g_frontState.cursorSpriteName, 0,
	       sizeof(g_frontState.cursorSpriteName));
}

/* Draws the frontend cursor on the back buffer at g_frontState.mouseX and
 * mouseY, first copying the pixels it covers into g_frontState.cursorSaveBuf.
 * With a cursorSpriteName set it draws that image with FrontImage_DrawSprite.
 * Else it draws the mask at cursorMaskPixels, one byte per pixel, where 0 is
 * transparent: at 16 bits per pixel 1 is drawn as 31 and 0xFF as 0xFFFF and
 * other values not at all, at 8 bits each nonzero byte is drawn as the palette
 * index. The save and the mask clip only their right and bottom edges, to the
 * clip bounds. Draws and saves nothing when the cursor position is outside 0 to
 * 639 by 0 to 479. Locks the back buffer into g_drawSurfacePtr and unlocks it
 * at the end, leaving the pointer set. Records the position and the visible
 * size in g_frontState.cursorPrevDrawX, cursorPrevDrawY, cursorPrevDrawWidth
 * and cursorPrevDrawHeight. The modern build also marks the cursor for its
 * renderer. */
// FUNCTION: XVT 0x4DDC90
void FrontendCursor_Draw(void)
{
	struct RECT clippedRect;
	struct RECT originalRect;
	int cursorWidth;
	int cursorHeight;
	int visibleWidth;
	int visibleHeight;
	int displayBpp;
	uint8_t *backBuffer;
	uint8_t *cursorPixels;
	uint8_t *saveBuffer;
	uint8_t *backBufferRow;
	int rowsRemaining;
	int column;
	int maskValue;

	if (g_frontState.mouseX < 0 || g_frontState.mouseX >= 640 ||
	    g_frontState.mouseY < 0 || g_frontState.mouseY >= 480) {
		return;
	}

	cursorHeight = g_frontState.cursorHeight;
	clippedRect.left = 0;
	cursorWidth = g_frontState.cursorWidth;
	clippedRect.top = 0;
	clippedRect.right = g_frontState.cursorWidth - 1;
	clippedRect.bottom = g_frontState.cursorHeight - 1;
	FrontendDraw_RectOffsetXY(&clippedRect, g_frontState.mouseX,
				  g_frontState.mouseY);
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
			backBufferRow =
				&backBuffer[g_frontState.mouseX +
					    g_frontState.mouseY *
						    g_frontState
							    .drawSurfacePitch];
			if (visibleHeight > 0) {
				rowsRemaining = visibleHeight;
				do {
					memcpy(saveBuffer, backBufferRow,
					       visibleWidth);
					saveBuffer += cursorWidth;
					backBufferRow +=
						g_frontState.drawSurfacePitch;
					--rowsRemaining;
				} while (rowsRemaining != 0);
			}
			break;
		}
		case 16: {
			backBufferRow =
				&backBuffer[2 * g_frontState.mouseX +
					    g_frontState.mouseY *
						    g_frontState
							    .drawSurfacePitch];
			if (visibleHeight > 0) {
				rowsRemaining = visibleHeight;
				do {
					if (visibleWidth > 0) {
						uint16_t *sourcePixel;
						uint16_t *savedPixel;
						int pixelsRemaining;

						sourcePixel = (uint16_t *)
							backBufferRow;
						savedPixel =
							(uint16_t *)saveBuffer;
						pixelsRemaining = visibleWidth;
						do {
							*savedPixel++ =
								*sourcePixel++;
							--pixelsRemaining;
						} while (pixelsRemaining != 0);
					}
					saveBuffer += 2 * cursorWidth;
					backBufferRow +=
						g_frontState.drawSurfacePitch &
						~1;
					--rowsRemaining;
				} while (rowsRemaining != 0);
			}
			break;
		}
		default:
			break;
		}
		FrontImage_DrawSprite(g_frontState.cursorSpriteName,
				      g_frontState.mouseX, g_frontState.mouseY);
	} else {
		switch (displayBpp) {
		case 8: {
			backBufferRow =
				&backBuffer[g_frontState.mouseX +
					    g_frontState.mouseY *
						    g_frontState
							    .drawSurfacePitch];
			if (visibleHeight > 0) {
				rowsRemaining = visibleHeight;
				do {
					memcpy(saveBuffer, backBufferRow,
					       visibleWidth);
					for (column = 0; column < visibleWidth;
					     ++column) {
						if (cursorPixels[column] != 0) {
							backBufferRow[column] =
								cursorPixels
									[column];
						}
					}
					saveBuffer += cursorWidth;
					cursorPixels += cursorWidth;
					backBufferRow +=
						g_frontState.drawSurfacePitch;
					--rowsRemaining;
				} while (rowsRemaining != 0);
			}
			break;
		}
		case 16: {
			backBufferRow =
				&backBuffer[2 * g_frontState.mouseX +
					    g_frontState.mouseY *
						    g_frontState
							    .drawSurfacePitch];
			if (visibleHeight > 0) {
				rowsRemaining = visibleHeight;
				do {
					column = 0;
					if (visibleWidth > 0) {
						uint16_t *destinationPixel;
						uint16_t *savedPixel;

						destinationPixel = (uint16_t *)
							backBufferRow;
						savedPixel =
							(uint16_t *)saveBuffer;
						do {
							*savedPixel =
								*destinationPixel;
							maskValue = cursorPixels
								[column];
							switch (maskValue) {
							case 1:
								*destinationPixel =
									31;
								break;
							case 0xFF:
								*destinationPixel =
									0xFFFF;
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
					backBufferRow +=
						g_frontState.drawSurfacePitch &
						~1;
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

/* Nothing calls this. Copies the pixels FrontendCursor_Draw saved back to the
 * back buffer at g_frontState.cursorPrevDrawX and cursorPrevDrawY, over the
 * visible size it recorded, which erases the cursor. Locks the back buffer into
 * g_drawSurfacePtr and unlocks it at the end. The modern build also marks the
 * restore for its renderer. */
// FUNCTION: XVT 0x4DDF90
void FrontendCursor_Restore(void)
{
	uint8_t *destination;
	uint8_t *source;
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
		uint8_t *rowDestination;

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
					uint16_t *sourcePixel;
					uint16_t *destinationPixel;
					int remainingPixels;

					sourcePixel = (uint16_t *)source;
					destinationPixel =
						(uint16_t *)rowDestination;
					remainingPixels = copyWidth;
					do {
						*destinationPixel++ =
							*sourcePixel++;
						--remainingPixels;
					} while (remainingPixels != 0);
				}
				source += 2 * sourcePitch;
				rowDestination +=
					g_frontState.drawSurfacePitch &
					0xFFFFFFFE;
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

/* Copies g_frontState.mouseX and mouseY to *outX and *outY. Returns outX. */
// FUNCTION: XVT 0x4DE090
int *FrontendCursor_GetPos(int *outX, int *outY)
{
	*outX = g_frontState.mouseX;
	*outY = g_frontState.mouseY;
	return outX;
}

/* Moves the cursor: clamps x to 0 to 640 and y to 0 to 480, stores them in
 * g_frontState.mouseX and mouseY and moves the system cursor there. Returns
 * SetCursorPos's result in the original build and XvtPresentation_WarpClassic's
 * in the modern build. */
// FUNCTION: XVT 0x4DE0B0
int FrontendCursor_SetPos(int x, int y)
{
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

/* Sets g_frontState.cursorVisible to 1: the frame loop draws the cursor after
 * each update. */
// FUNCTION: XVT 0x4DE100
void FrontendCursor_Show(void) { g_frontState.cursorVisible = 1; }

/* Sets g_frontState.cursorVisible to 0: the frame loop stops drawing the
 * cursor. */
// FUNCTION: XVT 0x4DE110
void FrontendCursor_Hide(void) { g_frontState.cursorVisible = 0; }

/* Nothing calls this. Returns g_frontState.cursorVisible. */
// FUNCTION: XVT 0x4DE120
int FrontendCursor_IsVisible(void) { return g_frontState.cursorVisible; }

/* Copies g_frontState.cursorWidth and cursorHeight to *outWidth and *outHeight.
 * Returns 1. */
// FUNCTION: XVT 0x4DE130
int FrontendCursor_GetDimensions(int *outWidth, int *outHeight)
{
	*outWidth = g_frontState.cursorWidth;
	*outHeight = g_frontState.cursorHeight;
	return 1;
}

/* Hides the system's own mouse cursor. The original build calls ShowCursor(0)
 * until the display count is under 0 and returns 1; the modern build hides the
 * host cursor and returns Aeron_SetHostCursorVisible's result, 1 on success. */
// FUNCTION: XVT 0x4DE150
int FrontendCursor_HideOsCursor(void)
{
#ifdef XVT_MODERN
	return Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(0) >= 0) {
	}
	return 1;
#endif
}

/* Only the original build calls this. It calls ShowCursor(1) until the display
 * count is 0 or more and returns 1. The modern build's body keeps the host
 * cursor hidden and returns Aeron_SetHostCursorVisible's result. */
// FUNCTION: XVT 0x4DE170
int FrontendCursor_ShowOsCursor(void)
{
#ifdef XVT_MODERN
	/* The port renders its own cursor, so legacy show requests keep the host cursor hidden. */
	return Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(1) < 0) {
	}
	return 1;
#endif
}

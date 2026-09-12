#include "xvt/frontend/frontend_draw.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_frontend.h"
#endif
#include "xvt/frontend/frontend_state.h"

#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0xAA6CFC
uint8_t* g_drawSurfacePtr;

// FUNCTION: XVT 0x4D6460
void FrontendDraw_RectAssign(RECT* rect, int32_t left, int32_t top, int32_t right, int32_t bottom) {
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
}

// FUNCTION: XVT 0x4D6480
void FrontendDraw_RectCopy(RECT* dst, const RECT* src) { *dst = *src; }

// FUNCTION: XVT 0x4D64A0
void FrontendDraw_RectOffsetXY(RECT* rect, int dx, int dy) {
	rect->left += dx;
	rect->right += dx;
	rect->top += dy;
	rect->bottom += dy;
}

// FUNCTION: XVT 0x4D64C0
void FrontendDraw_RectInsetXY(RECT* rect, int dx, int dy) {
	rect->left += dx;
	rect->right -= dx;
	rect->top += dy;
	rect->bottom -= dy;
}

// FUNCTION: XVT 0x4D64E0
int FrontendDraw_RectClipToBounds(RECT* rect) {
	int result;
	int bound;

	result = 0;
	bound = g_frontState.clipMinX;
	if (rect->left < bound) {
		rect->left = bound;
		if (rect->right < bound) {
			rect->right = bound - 1;
		}
		result = 1;
	}

	bound = g_frontState.clipMaxX;
	if (rect->right > bound) {
		rect->right = bound;
		if (rect->left > bound) {
			rect->left = bound + 1;
		}
		result |= 4;
	}

	bound = g_frontState.clipMinY;
	if (rect->top < bound) {
		rect->top = bound;
		if (rect->bottom < bound) {
			rect->bottom = bound - 1;
		}
		result |= 2;
	}

	bound = g_frontState.clipMaxY;
	if (rect->bottom > bound) {
		rect->bottom = bound;
		if (rect->top > bound) {
			rect->top = bound + 1;
		}
		result |= 8;
	}

	return result;
}

// FUNCTION: XVT 0x4D6550
void FrontendDraw_FillRectTranslucent(const RECT* src, int dx, int dy, unsigned int color) {
	int drawSurfacePitch;
	int width;
	uint8_t* destination;
	RECT clippedRect;
	int bottomEnd;
	int remainingRows;

	if (src->right <= src->left || src->top >= src->bottom)
		return;

	FrontendDraw_RectCopy(&clippedRect, src);
	FrontendDraw_RectOffsetXY(&clippedRect, dx, dy);
	if (clippedRect.left > 640 || clippedRect.right < 0 || clippedRect.top > 480 || clippedRect.bottom < 0)
		return;

	FrontendDraw_RectClipToBounds(&clippedRect);

#ifdef XVT_MODERN
	XvtRenderFrontend_Paint(XVT_PAINT_TRANSLUCENT, clippedRect.left, clippedRect.top, clippedRect.right + 1,
							clippedRect.bottom + 1, color);
#endif
	bottomEnd = clippedRect.bottom + 1;
	width = clippedRect.right - clippedRect.left + 1;
	drawSurfacePitch = g_frontState.drawSurfacePitch;
	if (g_frontState.displayBpp == 8) {
		destination = &g_drawSurfacePtr[clippedRect.top * g_frontState.drawSurfacePitch + clippedRect.left];
		if (bottomEnd <= clippedRect.top)
			return;

		remainingRows = bottomEnd - clippedRect.top;
		do {
			memset(destination, color, (size_t)width);
			destination += drawSurfacePitch;
			--remainingRows;
		} while (remainingRows != 0);
		return;
	}

	if (g_frontState.displayBpp == 16) {
		unsigned int colorHalf;

		destination = &g_drawSurfacePtr[2 * clippedRect.left + clippedRect.top * drawSurfacePitch];
		if (g_frontState.pixelFormat555 != 0) {
			colorHalf = ((color & 0x1f) + ((color & 0x7c00) << 6) + 8 * (color & 0x3e0)) >> 1;
			if (bottomEnd <= clippedRect.top)
				return;

			remainingRows = bottomEnd - clippedRect.top;
			drawSurfacePitch &= 0xfffffffe;
			do {
				if (width > 0) {
					uint16_t* pixel;
					int remainingWidth;

					pixel = (uint16_t*)destination;
					remainingWidth = width;
					do {
						unsigned int value;
						unsigned int blended;

						value = *pixel;
						blended = ((value & 0x1f) + ((value & 0x7c00) << 6) + 8 * (value & 0x3e0)) >> 1;
						blended += colorHalf;
						*pixel = (uint16_t)((blended & 0x1f) + ((blended >> 6) & 0x7c00) +
											((blended >> 3) & 0x3e0));
						++pixel;
						--remainingWidth;
					} while (remainingWidth != 0);
				}
				destination += drawSurfacePitch;
				--remainingRows;
			} while (remainingRows != 0);
			return;
		}

		colorHalf = ((color & 0x1f) + 32 * (color & 0xf800) + 8 * (color & 0x7e0)) >> 1;
		if (bottomEnd <= clippedRect.top)
			return;

		remainingRows = bottomEnd - clippedRect.top;
		drawSurfacePitch &= 0xfffffffe;
		do {
			if (width > 0) {
				uint16_t* pixel;
				int remainingWidth;

				pixel = (uint16_t*)destination;
				remainingWidth = width;
				do {
					unsigned int value;
					unsigned int blended;

					value = *pixel;
					blended = ((value & 0x1f) + 32 * (value & 0xf800) + 8 * (value & 0x7e0)) >> 1;
					blended += colorHalf;
					*pixel =
						(uint16_t)((blended & 0x1f) + ((blended >> 5) & 0xf800) + ((blended >> 3) & 0x7e0));
					++pixel;
					--remainingWidth;
				} while (remainingWidth != 0);
			}
			destination += drawSurfacePitch;
			--remainingRows;
		} while (remainingRows != 0);
	}
}

// FUNCTION: XVT 0x4D67E0
void FrontendDraw_Rect(RECT* rect, int dx, int dy, int color, int filled) {
	RECT clippedRect;
	int bottomEnd;
	int width;
	int drawSurfacePitch;
	int displayBpp;
	uint8_t* destination;
	int remainingRows;
	uint8_t* wordDestination;
	int remainingWidth;

	if (filled == 0) {
		FrontendDraw_RectOutline(rect, dx, dy, color);
		return;
	}
	if (rect->right <= rect->left || rect->top >= rect->bottom) {
		return;
	}

	FrontendDraw_RectCopy(&clippedRect, rect);
	FrontendDraw_RectOffsetXY(&clippedRect, dx, dy);
	if (clippedRect.left > 640 || clippedRect.right < 0 || clippedRect.top > 480 || clippedRect.bottom < 0) {
		return;
	}

	FrontendDraw_RectClipToBounds(&clippedRect);

#ifdef XVT_MODERN
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, clippedRect.left, clippedRect.top, clippedRect.right + 1,
							clippedRect.bottom + 1, color);
#endif
	bottomEnd = clippedRect.bottom + 1;
	width = clippedRect.right - clippedRect.left + 1;
	drawSurfacePitch = g_frontState.drawSurfacePitch;
	displayBpp = g_frontState.displayBpp;
	switch (displayBpp) {
		case 8:
			destination =
				&g_drawSurfacePtr[clippedRect.left + clippedRect.top * g_frontState.drawSurfacePitch];
			if (clippedRect.top >= bottomEnd) {
				break;
			}
			remainingRows = bottomEnd - clippedRect.top;
			do {
				memset(destination, color, (size_t)width);
				destination += drawSurfacePitch;
				--remainingRows;
			} while (remainingRows != 0);
			break;

		case 16:
			destination = &g_drawSurfacePtr[2 * clippedRect.left + clippedRect.top * drawSurfacePitch];
			if (clippedRect.top >= bottomEnd) {
				break;
			}
			remainingRows = bottomEnd - clippedRect.top;
			do {
				wordDestination = destination;
				remainingWidth = width;
				if (remainingWidth > 0) {
					while (remainingWidth > 0) {
						*(uint16_t*)wordDestination = (uint16_t)color;
						wordDestination += 2;
						--remainingWidth;
					}
				}
				destination += drawSurfacePitch;
				--remainingRows;
			} while (remainingRows != 0);
			break;
	}
}

// FUNCTION: XVT 0x4D6950
void FrontendDraw_RectOutline(RECT* rect, int dx, int dy, int color) {
	int width;
	int drawSurfacePitch;
	int drawBottom;
	int drawRight;
	int drawLeft;
	int drawTop;
	int bottom;
	RECT clippedRect;
	RECT unclippedRect;
	int displayBpp;
	int interiorTop;

	drawTop = 1;
	drawLeft = 1;
	drawRight = 1;
	drawBottom = 1;
	if (rect->right <= rect->left || rect->top >= rect->bottom) {
		return;
	}

	FrontendDraw_RectCopy(&clippedRect, rect);
	FrontendDraw_RectOffsetXY(&clippedRect, dx, dy);
	if (clippedRect.left > 640 || clippedRect.right < 0 || clippedRect.top > 480 || clippedRect.bottom < 0) {
		return;
	}

#ifdef XVT_MODERN
	XvtRenderFrontend_Paint(XVT_PAINT_FRAME, clippedRect.left, clippedRect.top, clippedRect.right + 1,
							clippedRect.bottom + 1, color);
#endif
	FrontendDraw_RectCopy(&unclippedRect, &clippedRect);
	FrontendDraw_RectClipToBounds(&clippedRect);
	if (unclippedRect.left != clippedRect.left) {
		drawLeft = 0;
	}
	if (unclippedRect.right != clippedRect.right) {
		drawRight = 0;
	}
	if (unclippedRect.top != clippedRect.top) {
		drawTop = 0;
	}
	if (unclippedRect.bottom != clippedRect.bottom) {
		drawBottom = 0;
	}

	bottom = clippedRect.bottom;
	width = clippedRect.right - clippedRect.left + 1;
	drawSurfacePitch = g_frontState.drawSurfacePitch;
	displayBpp = g_frontState.displayBpp;
	switch (displayBpp) {
		case 8: {
			uint8_t* destination;
			destination =
				&g_drawSurfacePtr[clippedRect.left + clippedRect.top * g_frontState.drawSurfacePitch];
			if (drawTop != 0) {
				memset(destination, color, (size_t)width);
				destination += drawSurfacePitch;
			}
			interiorTop = clippedRect.top + 1;
			if (bottom > interiorTop) {
				bottom -= interiorTop;
				do {
					if (drawLeft != 0) {
						destination[0] = (uint8_t)color;
					}
					if (drawRight != 0) {
						destination[width - 1] = (uint8_t)color;
					}
					destination += drawSurfacePitch;
					--bottom;
				} while (bottom != 0);
			}
			if (drawBottom != 0) {
				memset(destination, color, (size_t)width);
			}
			break;
		}

		case 16: {
			uint8_t* destination;
			destination = &g_drawSurfacePtr[clippedRect.left * 2 + clippedRect.top * drawSurfacePitch];
			if (drawTop != 0) {
				int x;
				for (x = 0; x < width; ++x) {
					((uint16_t*)destination)[x] = (uint16_t)color;
				}
				destination += drawSurfacePitch;
			}
			interiorTop = clippedRect.top + 1;
			if (bottom > interiorTop) {
				bottom -= interiorTop;
				do {
					if (drawLeft != 0) {
						*(uint16_t*)destination = (uint16_t)color;
					}
					if (drawRight != 0) {
						((uint16_t*)destination)[width - 1] = (uint16_t)color;
					}
					destination += drawSurfacePitch;
					--bottom;
				} while (bottom != 0);
			}
			if (drawBottom != 0) {
				int x;
				for (x = 0; x < width; ++x) {
					((uint16_t*)destination)[x] = (uint16_t)color;
				}
			}
			break;
		}
	}
}

// FUNCTION: XVT 0x4D6B90
int FrontendDraw_PointInRect(const RECT* rect, int x, int y) {
	if (rect->left > x || rect->right < x) {
		return 0;
	}
	if (rect->top > y || rect->bottom < y) {
		return 0;
	}
	return 1;
}

/* Draws a clipped line by stepping one scanline at a time and filling the
 * horizontal run covered by that scanline. The run length is tracked in 12-bit
 * fixed point, and each of the four slope quadrants gets its own walk so the
 * fill always advances to the right. Only 8bpp surfaces are supported. */
// FUNCTION: XVT 0x505CD0
void FrontendDraw_Line(int x0, int y0, int x1, int y1, int color) {
	int startY;
	int endY;
	int startX;
	int endX;
	int xStep;
	int xFraction;
	int accumulator;
	int rows;
	int run;
	uint8_t* destination;
	char clipped;

#ifdef XVT_MODERN
	if (x0 != x1 && y0 != y1)
		XvtRenderFrontend_Paint(XVT_PAINT_LINE, x0, y0, x1, y1, color);
#endif
	clipped = 0;
	endY = y1;
	startY = y0;
	accumulator = 2048;
	if (endY == startY) {
		FrontendDraw_HorizontalLineClipped(x0, x1, startY, color);
		return;
	}

	endX = x1;
	startX = x0;
	if (endX == startX) {
		FrontendDraw_VerticalLineClipped(startY, endY, startX, color);
		return;
	}

	if (endY > startY && endX > startX) {
		xStep = ((endX - startX + 1) << 12) / (endY - startY + 1);
		if (g_frontState.clipMinX > startX) {
			startY += ((g_frontState.clipMinX - startX) << 12) / xStep;
			startX = g_frontState.clipMinX;
			clipped = 1;
		}
		if (startY < g_frontState.clipMinY) {
			clipped = 1;
			startX += ((g_frontState.clipMinY - startY) * xStep) >> 12;
			startY = g_frontState.clipMinY;
		}
		if (g_frontState.clipMaxX < endX) {
			endY += ((g_frontState.clipMaxX - endX) << 12) / xStep;
			endX = g_frontState.clipMaxX;
			clipped = 1;
		}
		if (g_frontState.clipMaxY < endY) {
			clipped = 1;
			endX -= ((endY - g_frontState.clipMaxY) * xStep) >> 12;
			endY = g_frontState.clipMaxY;
		}
		if (clipped != 0) {
			if (endX < startX || startY > endY) {
				return;
			}
			xStep = ((endX - startX + 1) << 12) / (endY - startY + 1);
		}

		xFraction = xStep & 0xfff;
		xStep >>= 12;
		destination = g_drawSurfacePtr + g_frontState.drawSurfacePitch * startY + startX;
		if (xStep != 0) {
			rows = endY - startY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					run = xStep;
					if (accumulator >= 4096) {
						++run;
						accumulator -= 4096;
					}
					memset(destination, color, (size_t)run);
					destination += g_frontState.drawSurfacePitch + run;
					--rows;
				} while (rows != 0);
			}
		} else {
			rows = endY - startY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					*destination = (uint8_t)color;
					if (accumulator >= 4096) {
						accumulator -= 4096;
						++destination;
					}
					destination += g_frontState.drawSurfacePitch;
					--rows;
				} while (rows != 0);
			}
		}
	} else if (endY > startY && endX < startX) {
		xStep = ((startX - endX + 1) << 12) / (endY - startY + 1);
		if (g_frontState.clipMaxX < startX) {
			startY += ((startX - g_frontState.clipMaxX) << 12) / xStep;
			startX = g_frontState.clipMaxX;
			clipped = 1;
		}
		if (startY < g_frontState.clipMinY) {
			clipped = 1;
			startX -= ((g_frontState.clipMinY - startY) * xStep) >> 12;
			startY = g_frontState.clipMinY;
		}
		if (g_frontState.clipMinX > endX) {
			endY += ((endX - g_frontState.clipMinX) << 12) / xStep;
			endX = g_frontState.clipMinX;
			clipped = 1;
		}
		if (g_frontState.clipMaxY < endY) {
			clipped = 1;
			endX += ((endY - g_frontState.clipMaxY) * xStep) >> 12;
			endY = g_frontState.clipMaxY;
		}
		if (clipped != 0) {
			if (endX > startX || startY > endY) {
				return;
			}
			xStep = ((startX - endX + 1) << 12) / (endY - startY + 1);
		}

		xFraction = xStep & 0xfff;
		xStep >>= 12;
		destination = &g_drawSurfacePtr[endX + endY * g_frontState.drawSurfacePitch];
		if (xStep != 0) {
			rows = endY - startY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					run = xStep;
					if (accumulator >= 4096) {
						++run;
						accumulator -= 4096;
					}
					memset(destination, color, (size_t)run);
					destination += run - g_frontState.drawSurfacePitch;
					--rows;
				} while (rows != 0);
			}
		} else {
			rows = endY - startY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					*destination = (uint8_t)color;
					if (accumulator >= 4096) {
						accumulator -= 4096;
						++destination;
					}
					destination -= g_frontState.drawSurfacePitch;
					--rows;
				} while (rows != 0);
			}
		}
	} else if (endY < startY && endX > startX) {
		xStep = ((endX - startX + 1) << 12) / (startY - endY + 1);
		if (g_frontState.clipMinX > startX) {
			startY += ((startX - g_frontState.clipMinX) << 12) / xStep;
			startX = g_frontState.clipMinX;
			clipped = 1;
		}
		if (startY > g_frontState.clipMaxY) {
			clipped = 1;
			startX += ((startY - g_frontState.clipMaxY) * xStep) >> 12;
			startY = g_frontState.clipMaxY;
		}
		if (g_frontState.clipMaxX < endX) {
			endY += ((endX - g_frontState.clipMaxX) << 12) / xStep;
			endX = g_frontState.clipMaxX;
			clipped = 1;
		}
		if (g_frontState.clipMinY > endY) {
			clipped = 1;
			endX -= ((g_frontState.clipMinY - endY) * xStep) >> 12;
			endY = g_frontState.clipMinY;
		}
		if (clipped != 0) {
			if (endX < startX || startY < endY) {
				return;
			}
			xStep = ((endX - startX + 1) << 12) / (startY - endY + 1);
		}

		xFraction = xStep & 0xfff;
		xStep >>= 12;
		destination = g_drawSurfacePtr + g_frontState.drawSurfacePitch * startY + startX;
		if (xStep != 0) {
			rows = startY - endY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					run = xStep;
					if (accumulator >= 4096) {
						++run;
						accumulator -= 4096;
					}
					memset(destination, color, (size_t)run);
					destination += run - g_frontState.drawSurfacePitch;
					--rows;
				} while (rows != 0);
			}
		} else {
			rows = startY - endY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					*destination = (uint8_t)color;
					if (accumulator >= 4096) {
						accumulator -= 4096;
						++destination;
					}
					destination -= g_frontState.drawSurfacePitch;
					--rows;
				} while (rows != 0);
			}
		}
	} else {
		xStep = ((startX - endX + 1) << 12) / (startY - endY + 1);
		if (g_frontState.clipMaxX < startX) {
			startY += ((g_frontState.clipMaxX - startX) << 12) / xStep;
			startX = g_frontState.clipMaxX;
			clipped = 1;
		}
		if (startY > g_frontState.clipMaxY) {
			clipped = 1;
			startX -= ((startY - g_frontState.clipMaxY) * xStep) >> 12;
			startY = g_frontState.clipMaxY;
		}
		if (g_frontState.clipMinX > endX) {
			endY += ((g_frontState.clipMinX - endX) << 12) / xStep;
			endX = g_frontState.clipMinX;
			clipped = 1;
		}
		if (g_frontState.clipMinY > endY) {
			clipped = 1;
			endX += ((g_frontState.clipMinY - endY) * xStep) >> 12;
			endY = g_frontState.clipMinY;
		}
		if (clipped != 0) {
			if (endX > startX || startY < endY) {
				return;
			}
			xStep = ((startX - endX + 1) << 12) / (startY - endY + 1);
		}

		xFraction = xStep & 0xfff;
		xStep >>= 12;
		destination = &g_drawSurfacePtr[endX + endY * g_frontState.drawSurfacePitch];
		if (xStep != 0) {
			rows = startY - endY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					run = xStep;
					if (accumulator >= 4096) {
						++run;
						accumulator -= 4096;
					}
					memset(destination, color, (size_t)run);
					destination += g_frontState.drawSurfacePitch + run;
					--rows;
				} while (rows != 0);
			}
		} else {
			rows = startY - endY + 1;
			if (rows > 0) {
				do {
					accumulator += xFraction;
					*destination = (uint8_t)color;
					if (accumulator >= 4096) {
						accumulator -= 4096;
						++destination;
					}
					destination += g_frontState.drawSurfacePitch;
					--rows;
				} while (rows != 0);
			}
		}
	}
}

/* Draws an inclusive horizontal span clipped to the active frontend surface
 * bounds. The forward and reverse argument paths retain the original clipping
 * edge behavior. */
// FUNCTION: XVT 0x5063B0
void FrontendDraw_HorizontalLineClipped(int x0, int x1, int y, int color) {
	int pixelShift;
	uint8_t* destination;
	uint16_t* wordDestination;
	int count;

#ifdef XVT_MODERN
	XvtRenderFrontend_Paint(XVT_PAINT_LINE, x0, y, x1, y, color);
#endif

	pixelShift = g_frontState.displayBpp >> 4;
	if (y > g_frontState.clipMaxY || y < g_frontState.clipMinY)
		return;

	if (x0 <= x1) {
		if (g_frontState.clipMinX > x0) {
			x0 = g_frontState.clipMinX;
			if (x1 < x0)
				return;
		}
		if (g_frontState.clipMaxX < x1) {
			x1 = g_frontState.clipMaxX;
			if (x0 > x1)
				return;
		}

		destination = &g_drawSurfacePtr[(x0 << pixelShift) + g_frontState.drawSurfacePitch * y];
		switch (pixelShift) {
			case 0:
				count = x1 - x0 + 1;
				memset(destination, (uint16_t)color, (size_t)count);
				return;

			case 1:
				count = x1 - x0 + 1;
				color = (uint16_t)color;
				if (count <= 0)
					return;
				wordDestination = (uint16_t*)destination;
				do {
					*wordDestination = (uint16_t)color;
					++wordDestination;
					--count;
				} while (count != 0);
				return;
		}
		return;
	}

	if (g_frontState.clipMaxX < x0) {
		x0 = g_frontState.clipMaxX;
		if (x1 >= x0)
			return;
	}
	if (g_frontState.clipMinX > x1)
		x1 = g_frontState.clipMinX;
	if (x1 >= x0)
		return;

	destination = &g_drawSurfacePtr[(x1 << pixelShift) + g_frontState.drawSurfacePitch * y];
	switch (pixelShift) {
		case 0:
			count = x0 - x1 + 1;
			memset(destination, (uint16_t)color, (size_t)count);
			return;

		case 1:
			count = x0 - x1 + 1;
			color = (uint16_t)color;
			if (count <= 0)
				return;
			wordDestination = (uint16_t*)destination;
			do {
				*wordDestination = (uint16_t)color;
				++wordDestination;
				--count;
			} while (count != 0);
	}
}

/* Draws an inclusive vertical span clipped to the active frontend surface
 * bounds. Handles both argument orders with the draw block duplicated per
 * direction, as in the original. */
// FUNCTION: XVT 0x506550
void FrontendDraw_VerticalLineClipped(int y0, int y1, int x, int color) {
	int shift;
	int start;
	int end;
	int y;
	uint8_t* p;

#ifdef XVT_MODERN
	XvtRenderFrontend_Paint(XVT_PAINT_LINE, x, y0, x, y1, color);
#endif

	if (g_frontState.clipMinX > x || g_frontState.clipMaxX < x)
		return;
	shift = g_frontState.displayBpp >> 4;
	if (y1 >= y0) {
		end = y1;
		start = y0;
		if (g_frontState.clipMaxY < end) {
			end = g_frontState.clipMaxY;
			if (end < y0)
				return;
		}
		if (g_frontState.clipMinY > start) {
			start = g_frontState.clipMinY;
			if (start > end)
				return;
		}
		p = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * start + (x << shift)];
		if (shift != 0) {
			if (shift != 1)
				return;
			for (y = start; y <= end; ++y) {
				*(uint16_t*)p = (uint16_t)color;
				p += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
			}
		} else {
			for (y = start; y <= end; ++y) {
				*p = (uint8_t)color;
				p += g_frontState.drawSurfacePitch;
			}
		}
	} else {
		start = y1;
		end = y0;
		if (g_frontState.clipMinY > start) {
			start = g_frontState.clipMinY;
			if (start >= y0)
				return;
		}
		if (g_frontState.clipMaxY < end) {
			end = g_frontState.clipMaxY;
			if (end <= start)
				return;
		}
		p = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * start + (x << shift)];
		if (shift != 0) {
			if (shift != 1)
				return;
			for (y = start; y <= end; ++y) {
				*(uint16_t*)p = (uint16_t)color;
				p += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
			}
		} else {
			for (y = start; y <= end; ++y) {
				*p = (uint8_t)color;
				p += g_frontState.drawSurfacePitch;
			}
		}
	}
}

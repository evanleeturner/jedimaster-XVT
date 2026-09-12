#include "xvt/frontend/front_image.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_frontend.h"
#endif

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#endif
#include "xvt/frontend/frontend_state.h"

#include "xvt/assets/file.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_text.h"

#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)

typedef struct FrontImageBmpFileHeader {
	uint16_t signature;
	uint32_t fileSize;
	uint16_t reserved0;
	uint16_t reserved1;
	uint32_t pixelOffset;
} FrontImageBmpFileHeader;

typedef struct FrontImageBmpInfoHeader {
	uint32_t headerSize;
	int32_t width;
	int32_t height;
	uint16_t planes;
	uint16_t bitsPerPixel;
	uint32_t compression;
	uint32_t imageSize;
	int32_t pixelsPerMeterX;
	int32_t pixelsPerMeterY;
	uint32_t colorsUsed;
	uint32_t colorsImportant;
} FrontImageBmpInfoHeader;

#pragma pack(pop)
typedef char xvt_size_FrontImageBmpFileHeader[(sizeof(FrontImageBmpFileHeader) == 14) ? 1 : -1];
typedef char xvt_size_FrontImageBmpInfoHeader[(sizeof(FrontImageBmpInfoHeader) == 40) ? 1 : -1];

// GLOBAL: XVT 0x664C88
static int16_t g_paletteRemapCache[256] = { 0 };

// FUNCTION: XVT 0x4B4A60
int FrontImage_RegisterResourceDefault(const char* fileName, const char* name) {
	ImageResource* image;
	FrontImageResourceRecord entry;

	if (*fileName == '\0')
		return 0;
	if (*name == '\0')
		return 0;
	if (FrontImage_FindResourceByName(name) != -1)
		return 0;

	image = malloc(sizeof(*image));
	if (image == NULL)
		return 0;
	memset(image, 0, sizeof(*image));
	if (FrontImage_LoadBmpFile(fileName, image, 1, 1) == 0) {
		free(image);
		return 0;
	}

	entry.image = image;
	strncpy(entry.name, name, sizeof(entry.name));
	FrontImage_InsertResourceSorted(&entry);
	return 1;
}

// FUNCTION: XVT 0x4B4B20
int FrontImage_RegisterResource(const char* fileName, const char* name, int makePalette, int compressRLE) {
	ImageResource* image;
	FrontImageResourceRecord entry;

	if (*fileName == '\0')
		return 0;
	if (*name == '\0')
		return 0;
	if (FrontImage_FindResourceByName(name) != -1)
		return 0;

	image = malloc(sizeof(*image));
	if (image == NULL)
		return 0;
	memset(image, 0, sizeof(*image));
	if (FrontImage_LoadBmpFile(fileName, image, makePalette, compressRLE) == 0) {
		free(image);
		return 0;
	}

	entry.image = image;
	strncpy(entry.name, name, sizeof(entry.name));
	FrontImage_InsertResourceSorted(&entry);
	return 1;
}

// FUNCTION: XVT 0x4B4BF0
void FrontImage_FreeResourceByName(const char* name) {
	int resourceIndex;

	resourceIndex = FrontImage_FindResourceByName(name);
	if (resourceIndex == -1)
		return;

	if (g_frontState.resourceTable[resourceIndex].image != NULL) {
#ifdef XVT_MODERN
		XvtRenderAssets_FreeImage(g_frontState.resourceTable[resourceIndex].image);
#endif
		if (g_frontState.resourceTable[resourceIndex].image->pixels != NULL) {
			free(g_frontState.resourceTable[resourceIndex].image->pixels);
			g_frontState.resourceTable[resourceIndex].image->pixels = NULL;
		}
		free(g_frontState.resourceTable[resourceIndex].image);
		g_frontState.resourceTable[resourceIndex].image = NULL;
	}
	FrontImage_RemoveResourceAt(resourceIndex);
}

// FUNCTION: XVT 0x4B4C70
void FrontImage_FreeAllResources(void) {
	int resourceIndex;

	if (g_frontState.resourceTable == NULL)
		return;

	for (resourceIndex = 511; resourceIndex >= 0; --resourceIndex) {
		FrontImage_FreeResourceByName(g_frontState.resourceTable[resourceIndex].name);
	}
}

// FUNCTION: XVT 0x4B4CA0
int FrontImage_ResourceExists(const char* name) {
	if (*name == '\0') {
		return 0;
	}

	return FrontImage_FindResourceByName(name) != -1;
}

// FUNCTION: XVT 0x4B4CC0
int FrontImage_GetResourceRect(const char* name, RECT* outRect) {
	int resourceIndex;

	if (*name == '\0') {
		FrontendDraw_RectAssign(outRect, 0, 0, 0, 0);
		return 0;
	}

	resourceIndex = FrontImage_FindResourceByName(name);
	if (resourceIndex == -1) {
		FrontendDraw_RectAssign(outRect, 0, 0, 0, 0);
		return 0;
	}

	FrontendDraw_RectAssign(outRect, 0, 0, g_frontState.resourceTable[resourceIndex].image->width,
							g_frontState.resourceTable[resourceIndex].image->height);
	return 1;
}

// FUNCTION: XVT 0x4B4D40
int FrontImage_DrawSpriteTranslucent(const char* name, int x, int y) {
	int resourceIndex;
	ImageResource* image;

	resourceIndex = FrontImage_FindResourceByName(name);
	if (resourceIndex == -1)
		return 0;

	image = g_frontState.resourceTable[resourceIndex].image;
	if (image->isCompressed != 0)
		return 0;

	return FrontImage_BlitTranslucent(image, x, y);
}

// FUNCTION: XVT 0x4B4D90
int FrontImage_BlitTranslucent(ImageResource* image, int x, int y) {
	int clipOffsetX;
	int clipOffsetY;
	int imageWidth;
	int visibleWidth;
	int visibleHeight;
	int clipResult;
	int displayBpp;
	uint8_t* source;
	uint8_t* destination;
	RECT clippedRect;
	RECT unclippedRect;

	if (image == NULL)
		return 0;

	{
		clippedRect.left = x;
		imageWidth = image->width;
		clippedRect.top = y;
		clippedRect.right = x + imageWidth - 1;
		clippedRect.bottom = y + image->height - 1;
		FrontendDraw_RectCopy(&unclippedRect, &clippedRect);
		clipResult = FrontendDraw_RectClipToBounds(&clippedRect);
		if (clippedRect.right >= clippedRect.left && clippedRect.bottom >= clippedRect.top) {
			clipOffsetX = clippedRect.left - unclippedRect.left;
			clipOffsetY = clippedRect.top - unclippedRect.top;
			imageWidth = image->width;
			visibleWidth = image->width + clippedRect.right - clipOffsetX - unclippedRect.right;
			visibleHeight = clippedRect.bottom + image->height - unclippedRect.bottom - clipOffsetY;

#ifdef XVT_MODERN
			XvtRenderFrontend_Image(image, clipOffsetX, clipOffsetY, x + clipOffsetX, y + clipOffsetY,
									visibleWidth, visibleHeight, XVT_SPRITE_FRONT_TRANSLUCENT, 0);
#endif
			displayBpp = g_frontState.displayBpp;
			if (displayBpp == 8) {
				int rowsRemaining;

				source = &image->pixels[imageWidth * clipOffsetY + clipOffsetX];
				destination =
					&g_drawSurfacePtr[x + clipOffsetX + g_frontState.drawSurfacePitch * (y + clipOffsetY)];
				if (visibleHeight > 0) {
					rowsRemaining = visibleHeight;
					do {
						int column;

						for (column = 0; visibleWidth > column; ++column) {
							uint8_t sourcePixel;

							sourcePixel = source[column];
							if (sourcePixel != 0)
								destination[column] = sourcePixel;
						}
						destination += g_frontState.drawSurfacePitch;
						source += image->width;
						--rowsRemaining;
					} while (rowsRemaining != 0);
				}
			} else if (displayBpp == 16) {
				int rowsRemaining;

				source = &image->pixels[imageWidth * clipOffsetY + clipOffsetX];
				destination = &g_drawSurfacePtr[2 * x + 2 * clipOffsetX +
												g_frontState.drawSurfacePitch * (y + clipOffsetY)];
				if (g_frontState.pixelFormat555 != 0) {
					if (visibleHeight > 0) {
						rowsRemaining =
							clippedRect.bottom + image->height - unclippedRect.bottom - clipOffsetY;
						do {
							int column;
							uint8_t* destinationPixel;

							column = 0;
							if (visibleWidth > 0) {
								destinationPixel = destination;
								do {
									uint8_t sourcePixel;

									sourcePixel = source[column];
									if (sourcePixel != 0) {
										int sourceColor;
										unsigned int blended;

										sourceColor = image->colorLUT[sourcePixel];
										blended = (((*(uint16_t*)destinationPixel & 0x1F) +
													8 * ((*(uint16_t*)destinationPixel & 0x3E0) +
														 8 * (*(uint16_t*)destinationPixel & 0x7C00u))) >>
												   1) +
												  (((sourceColor & 0x1F) + ((sourceColor & 0x7C00) << 6) +
													8 * (sourceColor & 0x3E0u)) >>
												   1);
										*(uint16_t*)destinationPixel =
											(uint16_t)((blended & 0x1F) + ((blended >> 6) & 0x7C00) +
													   ((blended >> 3) & 0x3E0));
									}
									destinationPixel += 2;
									++column;
								} while (column < visibleWidth);
							}
							source += image->width;
							destination += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
							--rowsRemaining;
						} while (rowsRemaining != 0);
					}
				} else if (visibleHeight > 0) {
					rowsRemaining = clippedRect.bottom + image->height - unclippedRect.bottom - clipOffsetY;
					do {
						int column;
						uint8_t* destinationPixel;

						column = 0;
						if (visibleWidth > 0) {
							destinationPixel = destination;
							do {
								uint8_t sourcePixel;

								sourcePixel = source[column];
								if (sourcePixel != 0) {
									unsigned int blended;

									blended = (((*(uint16_t*)destinationPixel & 0x1F) +
												8 * ((*(uint16_t*)destinationPixel & 0x7E0) +
													 4 * (*(uint16_t*)destinationPixel & 0xF800u))) >>
											   1) +
											  (((image->colorLUT[sourcePixel] & 0x1F) +
												32 * (image->colorLUT[sourcePixel] & 0xF800) +
												8 * (image->colorLUT[sourcePixel] & 0x7E0u)) >>
											   1);
									*(uint16_t*)destinationPixel =
										(uint16_t)((blended & 0x1F) + ((blended >> 3) & 0x7E0) +
												   ((blended >> 5) & 0xF800));
								}
								destinationPixel += 2;
								++column;
							} while (column != visibleWidth);
						}
						source += image->width;
						destination += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
						--rowsRemaining;
					} while (rowsRemaining != 0);
				}
			}
		}
		return clipResult;
	}
}

// FUNCTION: XVT 0x4B50B0
int FrontImage_DrawSpriteRectTransparent(const char* name, RECT* srcRect, int dstX, int dstY) {
	int resourceIndex;
	ImageResource* image;

	resourceIndex = FrontImage_FindResourceByName(name);
	if (resourceIndex == -1) {
		return 0;
	}
	image = g_frontState.resourceTable[resourceIndex].image;
	if (image->isCompressed != 0) {
		return 0;
	}
	return FrontImage_BlitRectTransparent(image, srcRect, dstX, dstY);
}

// FUNCTION: XVT 0x4B5100
int FrontImage_BlitRectTransparent(ImageResource* image, RECT* srcRect, int dstX, int dstY) {
	int rowsRemaining;
	int displayBpp;
	int visibleHeight;
	int clipOffsetX;
	int clipOffsetY;
	int visibleWidth;
	int clipResult;
	RECT destinationRect;
	RECT unclippedRect;

	if (image == NULL) {
		return 0;
	}
	FrontendDraw_RectCopy(&destinationRect, srcRect);
	FrontendDraw_RectOffsetXY(&destinationRect, dstX - srcRect->left, dstY - srcRect->top);
	FrontendDraw_RectCopy(&unclippedRect, &destinationRect);
	clipResult = FrontendDraw_RectClipToBounds(&destinationRect);
	if (destinationRect.right >= destinationRect.left) {
		if (destinationRect.top <= destinationRect.bottom) {
			int sourceTop;
			int sourceLeft;

			clipOffsetX = destinationRect.left - unclippedRect.left;
			clipOffsetY = destinationRect.top - unclippedRect.top;
			sourceLeft = srcRect->left;
			visibleWidth = srcRect->right - sourceLeft;
			visibleWidth -= clipOffsetX;
			visibleWidth += destinationRect.right + 1;
			visibleWidth -= unclippedRect.right;
			sourceTop = srcRect->top;
			visibleHeight = srcRect->bottom - sourceTop;
			visibleHeight += destinationRect.bottom + 1;
			visibleHeight -= clipOffsetY;
			visibleHeight -= unclippedRect.bottom;

#ifdef XVT_MODERN
			XvtRenderFrontend_Image(image, sourceLeft + clipOffsetX, sourceTop + clipOffsetY,
									dstX + clipOffsetX, dstY + clipOffsetY, visibleWidth, visibleHeight,
									XVT_SPRITE_FRONT_KEYED, 0);
#endif
			displayBpp = g_frontState.displayBpp;
			switch (displayBpp) {
				case 8: {
					uint8_t* source;
					uint8_t* destination;
					int column;

					source =
						&image->pixels[image->width * (sourceTop + clipOffsetY) + sourceLeft + clipOffsetX];
					destination = &g_drawSurfacePtr[dstX + clipOffsetX +
													g_frontState.drawSurfacePitch * (dstY + clipOffsetY)];
					if (visibleHeight > 0) {
						rowsRemaining = visibleHeight;
						do {
							column = 0;
							if (visibleWidth > 0) {
								do {
									if (source[column] != 0)
										destination[column] = source[column];
									++column;
								} while (column - visibleWidth < 0);
							}
							source += image->width;
							destination += g_frontState.drawSurfacePitch;
							--rowsRemaining;
						} while (rowsRemaining != 0);
					}
					break;
				}
				case 16: {
					uint8_t* source;
					uint8_t* destination;
					int column;

					source =
						&image->pixels[image->width * (sourceTop + clipOffsetY) + sourceLeft + clipOffsetX];
					destination = &g_drawSurfacePtr[2 * (dstX + clipOffsetX) +
													g_frontState.drawSurfacePitch * (dstY + clipOffsetY)];
					if (visibleHeight > 0) {
						rowsRemaining = visibleHeight;
						do {
							column = 0;
							if (visibleWidth > 0) {
								do {
									if (source[column] != 0) {
										*(uint16_t*)&destination[2 * column] =
											(uint16_t)image->colorLUT[source[column]];
									}
									++column;
								} while (column < visibleWidth);
							}
							source += image->width;
							destination += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
							--rowsRemaining;
						} while (rowsRemaining != 0);
					}
					break;
				}
			}
		}
	}
	return clipResult;
}

// FUNCTION: XVT 0x4B52C0
int FrontImage_DrawSpriteRectTinted(const char* name, RECT* srcRect, int dstX, int dstY,
									unsigned int tintColor) {
	int resourceIndex;
	ImageResource* image;

	resourceIndex = FrontImage_FindResourceByName(name);
	if (resourceIndex == -1)
		return 0;
	image = g_frontState.resourceTable[resourceIndex].image;
	if (image->isCompressed != 0)
		return 0;
	return FrontImage_BlitRectTinted(image, srcRect, dstX, dstY, tintColor);
}

// FUNCTION: XVT 0x4B5310
int FrontImage_BlitRectTinted(ImageResource* image, const RECT* srcRect, int dstX, int dstY,
							  unsigned int tintColor) {
	int visibleHeight;
	int displayBpp;
	int sourceTop;
	int sourceLeft;
	int clipOffsetX;
	int clipOffsetY;
	int visibleWidth;
	int clipResult;
	RECT destinationRect;
	uint8_t* source;
	uint8_t* destination;
	int rowsRemaining;
	RECT unclippedRect;
	int column;
	const RECT* sourceRect;
	int destinationY;
	uint8_t sourcePixel;
	int intensity;
	unsigned int tintRed;
	unsigned int tintGreen;
	int red;
	int green;
	int blue;

	if (image == NULL)
		return 0;

	sourceRect = srcRect;
	destinationY = dstY;
	FrontendDraw_RectCopy(&destinationRect, sourceRect);
	FrontendDraw_RectOffsetXY(&destinationRect, dstX - sourceRect->left, destinationY - sourceRect->top);
	FrontendDraw_RectCopy(&unclippedRect, &destinationRect);
	clipResult = FrontendDraw_RectClipToBounds(&destinationRect);
	if (destinationRect.right >= destinationRect.left) {
		if (destinationRect.top > destinationRect.bottom)
			return clipResult;

		clipOffsetX = destinationRect.left - unclippedRect.left;
		clipOffsetY = destinationRect.top - unclippedRect.top;
		sourceLeft = sourceRect->left;
		visibleWidth =
			sourceRect->right - sourceLeft - clipOffsetX - unclippedRect.right + destinationRect.right + 1;
		sourceTop = sourceRect->top;
		visibleHeight =
			sourceRect->bottom - sourceTop - clipOffsetY - unclippedRect.bottom + destinationRect.bottom + 1;

#ifdef XVT_MODERN
		XvtRenderFrontend_Image(image, sourceLeft + clipOffsetX, sourceTop + clipOffsetY, dstX + clipOffsetX,
								dstY + clipOffsetY, visibleWidth, visibleHeight, XVT_SPRITE_FRONT_TINTED,
								tintColor);
#endif
		displayBpp = g_frontState.displayBpp;
		if (displayBpp == 8) {
			destination = &g_drawSurfacePtr[dstX + clipOffsetX +
											g_frontState.drawSurfacePitch * (destinationY + clipOffsetY)];
			source = &image->pixels[image->width * (sourceTop + clipOffsetY) + sourceLeft + clipOffsetX];
			if (visibleHeight > 0) {
				do {
					for (column = 0; column < visibleWidth; ++column) {
						if (source[column] != 0)
							destination[column] = (uint8_t)tintColor;
					}
					destination += g_frontState.drawSurfacePitch;
					source += image->width;
					--visibleHeight;
				} while (visibleHeight != 0);
			}
		} else if (displayBpp == 16) {
			source = &image->pixels[image->width * (sourceTop + clipOffsetY) + sourceLeft + clipOffsetX];
			destination = &g_drawSurfacePtr[2 * (dstX + clipOffsetX) +
											g_frontState.drawSurfacePitch * (destinationY + clipOffsetY)];
			if (visibleHeight > 0) {
				for (rowsRemaining = visibleHeight; rowsRemaining != 0; --rowsRemaining) {
					for (column = 0; column < visibleWidth; ++column) {
						sourcePixel = source[column];
						if (sourcePixel == 0)
							continue;
						intensity = image->colorLUT[sourcePixel] & 0x1F;
						if (g_frontState.pixelFormat555 != 0) {
							tintRed = tintColor >> 10;
							tintGreen = tintColor & 0x3E0;
						} else {
							tintRed = tintColor >> 11;
							tintGreen = tintColor & 0x7E0;
						}
						red = intensity * (int)tintRed / 31;
						green = intensity * (int)(tintGreen >> 5) / 31;
						blue = intensity * (int)(tintColor & 0x1F) / 31;
						if (g_frontState.pixelFormat555 != 0)
							red <<= 5;
						else
							red <<= 6;
						*(uint16_t*)&destination[2 * column] = ((red + green) << 5) + blue;
					}
					source += image->width;
					destination += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
				}
			}
		}
	}

	return clipResult;
}

// FUNCTION: XVT 0x4B5570
int FrontImage_DrawSprite(const char* name, int x, int y) {
	int resourceIndex;

	resourceIndex = FrontImage_FindResourceByName(name);
	if (resourceIndex == -1)
		return 0;

	return FrontImage_BlitClipped(g_frontState.resourceTable[resourceIndex].image, x, y);
}

// FUNCTION: XVT 0x4B55B0
int FrontImage_BlitClipped(ImageResource* image, int x, int y) {
	RECT clippedRect;
	RECT originalRect;
	int clipResult;
	int sourceX;
	int sourceY;
	int width;
	int visibleWidth;
	int visibleHeight;
	int displayBpp;
	const uint8_t* source;
	uint8_t* destination;
	int row;
	int column;
	uint8_t pixel;

	if (image == NULL)
		return 0;

	clippedRect.left = x;
	clippedRect.top = y;
	clippedRect.right = x + image->width - 1;
	clippedRect.bottom = y + image->height - 1;
	FrontendDraw_RectCopy(&originalRect, &clippedRect);
	clipResult = FrontendDraw_RectClipToBounds(&clippedRect);
	if (clippedRect.right >= clippedRect.left && clippedRect.top <= clippedRect.bottom) {
		sourceX = clippedRect.left - originalRect.left;
		sourceY = clippedRect.top - originalRect.top;
		width = image->width;
		visibleWidth = clippedRect.right - sourceX - originalRect.right + width;
		visibleHeight = clippedRect.bottom + image->height - sourceY - originalRect.bottom;

#ifdef XVT_MODERN
		XvtRenderFrontend_Image(image, sourceX, sourceY, x + sourceX, y + sourceY, visibleWidth,
								visibleHeight, XVT_SPRITE_FRONT_KEYED, 0);
#endif
		displayBpp = g_frontState.displayBpp;
		if (image->isCompressed == 0) {
			if (displayBpp == 8) {
				source = &image->pixels[sourceY * width + sourceX];
				destination = &g_drawSurfacePtr[x + sourceX + g_frontState.drawSurfacePitch * (y + sourceY)];
				for (row = visibleHeight; row > 0; --row) {
					for (column = 0; column < visibleWidth; ++column) {
						pixel = source[column];
						if (pixel != 0)
							destination[column] = pixel;
					}
					source += image->width;
					destination += g_frontState.drawSurfacePitch;
				}
			} else if (displayBpp == 16) {
				source = &image->pixels[sourceY * width + sourceX];
				destination =
					&g_drawSurfacePtr[2 * (x + sourceX) + g_frontState.drawSurfacePitch * (y + sourceY)];
				for (row = visibleHeight; row > 0; --row) {
					for (column = 0; column < visibleWidth; ++column) {
						if (source[column] != 0) {
							*(uint16_t*)&destination[2 * column] = image->colorLUT[source[column]];
						}
					}
					source += image->width;
					destination += g_frontState.drawSurfacePitch & ~1;
				}
			}
		} else {
			if (displayBpp == 8) {
				FrontImage_BlitRLE8(image, x + sourceX, y + sourceY, sourceX, sourceY, visibleWidth,
									visibleHeight);
			} else if (displayBpp == 16) {
				FrontImage_BlitRLE16(image, x + sourceX, y + sourceY, sourceX, sourceY, visibleWidth,
									 visibleHeight);
			}
		}
	}

	return clipResult;
}

// FUNCTION: XVT 0x4B5770
void FrontImage_BlitRLE8(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
						 int visibleWidth, int visibleHeight) {
	const uint8_t* row;
	const uint8_t* rowStart;
	uint8_t* destination;
	int rowLength;
	int tokenOffset;
	int destinationOffset;
	uint8_t started;
	uint8_t token;
	int count;
	int endOffset;
	int rowsRemaining;
	uint8_t value;

	if (visibleWidth == image->width) {
		int fastRowsRemaining;

		row = image->pixels;
		destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + destX];
		while (srcTop > 0) {
#ifdef XVT_MODERN
			memcpy(&rowLength, row, sizeof(rowLength));
#else
			rowLength = *(const int*)row;
#endif
			row += rowLength;
			--srcTop;
		}

		if (visibleHeight <= 0)
			return;

		fastRowsRemaining = visibleHeight;
		do {
			int fastDestinationOffset;

			fastDestinationOffset = 0;
			row += sizeof(rowLength);
			for (;;) {
				token = *row++;
				if (token == 0x80)
					break;

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					memcpy(destination + fastDestinationOffset, row, token);
					fastDestinationOffset += token;
					row += token;
					continue;
				}

				if ((token & 0x40u) != 0) {
					fastDestinationOffset += token & 0x3F;
					continue;
				}

				value = *row++;
				memset(destination + fastDestinationOffset, value, token);
				fastDestinationOffset += token;
			}

			destination += g_frontState.drawSurfacePitch;
			--fastRowsRemaining;
		} while (fastRowsRemaining != 0);
		return;
	}

	row = image->pixels;
	destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + destX];
	while (srcTop > 0) {
#ifdef XVT_MODERN
		memcpy(&rowLength, row, sizeof(rowLength));
#else
		rowLength = *(const int*)row;
#endif
		row += rowLength;
		--srcTop;
	}

	if (visibleHeight <= 0)
		return;

	rowsRemaining = visibleHeight;
	do {
		rowStart = row;
		destinationOffset = 0;
		tokenOffset = sizeof(rowLength);
#ifdef XVT_MODERN
		memcpy(&rowLength, rowStart, sizeof(rowLength));
#else
		rowLength = *(const int*)rowStart;
#endif
		started = 0;

		for (;;) {
			token = rowStart[tokenOffset++];
			if (!started) {
				if (token == 0x80)
					break;

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					endOffset = destinationOffset + count;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						tokenOffset += count;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x80;
					} else {
						destinationOffset = endOffset;
						tokenOffset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x40;
					} else {
						destinationOffset += count;
					}
				} else {
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token == 0)
							++tokenOffset;
					} else {
						++tokenOffset;
						destinationOffset += count;
					}
				}
			}

			if (started != 1 || token == 0)
				continue;
			if (token == 0x80)
				break;

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				endOffset = destinationOffset + token;
				if (endOffset >= visibleWidth) {
					token = (uint8_t)(visibleWidth - destinationOffset);
					memcpy(destination + destinationOffset, rowStart + tokenOffset, token);
					break;
				}

				memcpy(destination + destinationOffset, rowStart + tokenOffset, token);
				destinationOffset = endOffset;
				tokenOffset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				destinationOffset += token;
				if (destinationOffset >= visibleWidth)
					break;
			} else {
				value = rowStart[tokenOffset++];
				count = token;
				endOffset = destinationOffset + count;
				if (endOffset >= visibleWidth) {
					token = (uint8_t)(visibleWidth - destinationOffset);
					memset(destination + destinationOffset, value, token);
					break;
				}

				memset(destination + destinationOffset, value, count);
				destinationOffset = endOffset;
			}
		}

		row += rowLength;
		destination += g_frontState.drawSurfacePitch;
		--rowsRemaining;
	} while (rowsRemaining != 0);
}

// FUNCTION: XVT 0x4B5A80
void FrontImage_BlitRLE16(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
						  int visibleWidth, int visibleHeight) {
	const uint8_t* row;
	const uint8_t* rowStart;
	uint8_t* destination;
	int rowLength;
	int tokenOffset;
	int destinationOffset;
	uint8_t started;
	uint8_t token;
	int count;
	int endOffset;
	int rowsRemaining;
	int color;
	int pixelIndex;
	uint16_t* fillDestination;
	int fillCount;

	if (visibleWidth == image->width) {
		int fastRowsRemaining;

		row = image->pixels;
		destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + 2 * destX];
		while (srcTop > 0) {
#ifdef XVT_MODERN
			memcpy(&rowLength, row, sizeof(rowLength));
#else
			rowLength = *(const int*)row;
#endif
			row += rowLength;
			--srcTop;
		}

		if (visibleHeight <= 0)
			return;

		fastRowsRemaining = visibleHeight;
		do {
			int fastDestinationOffset;

			fastDestinationOffset = 0;
			row += sizeof(rowLength);
			for (;;) {
				token = *row++;
				if (token == 0x80)
					break;

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					for (pixelIndex = 0; pixelIndex < token; ++pixelIndex) {
						*(uint16_t*)&destination[2 * (fastDestinationOffset + pixelIndex)] =
							image->colorLUT[row[pixelIndex]];
					}
					fastDestinationOffset += token;
					row += token;
				} else if ((token & 0x40u) != 0) {
					fastDestinationOffset += token & 0x3F;
				} else {
					color = image->colorLUT[*row++];
					if (token != 0) {
						fillDestination = (uint16_t*)&destination[2 * fastDestinationOffset];
						for (fillCount = token; fillCount != 0; --fillCount) {
							*fillDestination++ = (uint16_t)color;
						}
					}
					fastDestinationOffset += token;
				}
			}

			destination += g_frontState.drawSurfacePitch & ~1;
			--fastRowsRemaining;
		} while (fastRowsRemaining != 0);
		return;
	}

	row = image->pixels;
	destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + 2 * destX];
	while (srcTop > 0) {
#ifdef XVT_MODERN
		memcpy(&rowLength, row, sizeof(rowLength));
#else
		rowLength = *(const int*)row;
#endif
		row += rowLength;
		--srcTop;
	}

	if (visibleHeight <= 0)
		return;

	rowsRemaining = visibleHeight;
	do {
		rowStart = row;
		destinationOffset = 0;
		tokenOffset = sizeof(rowLength);
#ifdef XVT_MODERN
		memcpy(&rowLength, rowStart, sizeof(rowLength));
#else
		rowLength = *(const int*)rowStart;
#endif
		started = 0;

		for (;;) {
			token = rowStart[tokenOffset++];
			if (!started) {
				if (token == 0x80)
					break;
				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					endOffset = destinationOffset + count;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						tokenOffset += count;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x80;
					} else {
						destinationOffset = endOffset;
						tokenOffset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x40;
					} else {
						destinationOffset += count;
					}
				} else {
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token == 0)
							++tokenOffset;
					} else {
						++tokenOffset;
						destinationOffset += count;
					}
				}
			}

			if (started != 1 || token == 0)
				continue;
			if (token == 0x80)
				break;

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				endOffset = destinationOffset + token;
				if (endOffset >= visibleWidth) {
					count = (uint8_t)(visibleWidth - destinationOffset);
					if (count != 0) {
						pixelIndex = 0;
						do {
							*(uint16_t*)&destination[2 * (destinationOffset + pixelIndex)] =
								image->colorLUT[rowStart[tokenOffset + pixelIndex]];
							++pixelIndex;
						} while (pixelIndex < count);
					}
					break;
				}

				pixelIndex = 0;
				if (token != 0) {
					do {
						*(uint16_t*)&destination[2 * (destinationOffset + pixelIndex)] =
							image->colorLUT[rowStart[tokenOffset + pixelIndex]];
						++pixelIndex;
					} while (pixelIndex < token);
				}
				destinationOffset = endOffset;
				tokenOffset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				destinationOffset += token;
				if (destinationOffset >= visibleWidth)
					break;
			} else {
				color = image->colorLUT[rowStart[tokenOffset++]];
				endOffset = destinationOffset + token;
				if (endOffset >= visibleWidth) {
					count = (uint8_t)(visibleWidth - destinationOffset);
					if (count != 0) {
						fillDestination = (uint16_t*)&destination[2 * destinationOffset];
						for (fillCount = count; fillCount != 0; --fillCount) {
							*fillDestination++ = (uint16_t)color;
						}
					}
					break;
				}

				if (token != 0) {
					fillDestination = (uint16_t*)&destination[2 * destinationOffset];
					for (fillCount = token; fillCount != 0; --fillCount) {
						*fillDestination++ = (uint16_t)color;
					}
				}
				destinationOffset = endOffset;
			}
		}

		row += rowLength;
		destination += g_frontState.drawSurfacePitch & ~1;
		--rowsRemaining;
	} while (rowsRemaining != 0);
}

// FUNCTION: XVT 0x4B5DE0
int FrontImage_DrawSpriteOpaque(const char* name, int x, int y) {
	int resourceIndex;

	resourceIndex = FrontImage_FindResourceByName(name);
	if (resourceIndex == -1)
		return 0;

	return FrontImage_BlitOpaque(g_frontState.resourceTable[resourceIndex].image, x, y);
}

// FUNCTION: XVT 0x4B5E20
int FrontImage_BlitOpaque(ImageResource* image, int x, int y) {
	int clipResult;
	RECT clippedRect;
	RECT originalRect;
	int sourceX;
	int sourceY;
	int width;
	int visibleWidth;
	int visibleHeight;
	int displayBpp;
	const uint8_t* source;
	uint8_t* destination;
	int column;

	if (image == NULL)
		return 0;

	clippedRect.left = x;
	clippedRect.top = y;
	clippedRect.right = x + image->width - 1;
	clippedRect.bottom = y + image->height - 1;
	FrontendDraw_RectCopy(&originalRect, &clippedRect);
	clipResult = FrontendDraw_RectClipToBounds(&clippedRect);
	if (clippedRect.right >= clippedRect.left && clippedRect.top <= clippedRect.bottom) {
		sourceX = clippedRect.left - originalRect.left;
		sourceY = clippedRect.top - originalRect.top;
		width = image->width;
		visibleWidth = clippedRect.right - sourceX - originalRect.right + width;
		visibleHeight = clippedRect.bottom + image->height - sourceY - originalRect.bottom;

#ifdef XVT_MODERN
		XvtRenderFrontend_Image(image, sourceX, sourceY, x + sourceX, y + sourceY, visibleWidth,
								visibleHeight, XVT_SPRITE_FRONT_OPAQUE, 0);
#endif
		displayBpp = g_frontState.displayBpp;
		if (image->isCompressed == 0) {
			if (displayBpp == 8) {
				source = &image->pixels[sourceY * width + sourceX];
				destination = &g_drawSurfacePtr[x + sourceX + g_frontState.drawSurfacePitch * (y + sourceY)];
				if (visibleHeight > 0) {
					do {
						for (column = 0; column < visibleWidth; ++column)
							destination[column] = source[column];
						source += image->width;
						destination += g_frontState.drawSurfacePitch;
						--visibleHeight;
					} while (visibleHeight != 0);
				}
			} else if (displayBpp == 16) {
				source = &image->pixels[sourceY * width + sourceX];
				destination =
					&g_drawSurfacePtr[2 * (x + sourceX) + g_frontState.drawSurfacePitch * (y + sourceY)];
				if (visibleHeight > 0) {
					do {
						for (column = 0; column < visibleWidth; ++column)
							*(uint16_t*)&destination[2 * column] = image->colorLUT[source[column]];
						source += image->width;
						destination += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
						--visibleHeight;
					} while (visibleHeight != 0);
				}
			}
		} else if (displayBpp == 8) {
			FrontImage_BlitRLE8Opaque(image, x + sourceX, y + sourceY, sourceX, sourceY, visibleWidth,
									  visibleHeight);
		} else if (displayBpp == 16) {
			FrontImage_BlitRLE16Opaque(image, x + sourceX, y + sourceY, sourceX, sourceY, visibleWidth,
									   visibleHeight);
		}
	}

	return clipResult;
}

// FUNCTION: XVT 0x4B5FE0
void FrontImage_BlitRLE8Opaque(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
							   int visibleWidth, int visibleHeight) {
	const uint8_t* row;
	const uint8_t* rowStart;
	uint8_t* destination;
	int rowLength;
	int tokenOffset;
	int destinationOffset;
	uint8_t started;
	uint8_t token;
	int count;
	int endOffset;
	int rowsRemaining;
	uint8_t value;

	row = image->pixels;
	if (visibleWidth == image->width) {
		int fastRowsRemaining;

		destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + destX];
		while (srcTop > 0) {
#ifdef XVT_MODERN
			memcpy(&rowLength, row, sizeof(rowLength));
#else
			rowLength = *(const int*)row;
#endif
			row += rowLength;
			--srcTop;
		}

		if (visibleHeight <= 0)
			return;

		fastRowsRemaining = visibleHeight;
		do {
			int fastDestinationOffset;

			fastDestinationOffset = 0;
			row += sizeof(rowLength);
			for (;;) {
				token = *row++;
				if (token == 0x80)
					break;

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					memcpy(destination + fastDestinationOffset, row, token);
					fastDestinationOffset += token;
					row += token;
					continue;
				}

				if ((token & 0x40u) != 0) {
					token &= 0x3F;
					memset(destination + fastDestinationOffset, 0, token);
					fastDestinationOffset += token;
					continue;
				}

				value = *row++;
				memset(destination + fastDestinationOffset, value, token);
				fastDestinationOffset += token;
			}

			destination += g_frontState.drawSurfacePitch;
			--fastRowsRemaining;
		} while (fastRowsRemaining != 0);
		return;
	}

	destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + destX];
	while (srcTop > 0) {
#ifdef XVT_MODERN
		memcpy(&rowLength, row, sizeof(rowLength));
#else
		rowLength = *(const int*)row;
#endif
		row += rowLength;
		--srcTop;
	}

	if (visibleHeight <= 0)
		return;

	rowsRemaining = visibleHeight;
	do {
		rowStart = row;
		destinationOffset = 0;
		tokenOffset = sizeof(rowLength);
#ifdef XVT_MODERN
		memcpy(&rowLength, rowStart, sizeof(rowLength));
#else
		rowLength = *(const int*)rowStart;
#endif
		started = 0;

		for (;;) {
			token = rowStart[tokenOffset++];
			if (!started) {
				if (token == 0x80)
					break;

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					endOffset = destinationOffset + count;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						tokenOffset += count;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x80;
					} else {
						destinationOffset = endOffset;
						tokenOffset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x40;
					} else {
						destinationOffset += count;
					}
				} else {
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token == 0)
							++tokenOffset;
					} else {
						++tokenOffset;
						destinationOffset += count;
					}
				}
			}

			if (started != 1 || token == 0)
				continue;
			if (token == 0x80)
				break;

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				endOffset = destinationOffset + token;
				if (endOffset >= visibleWidth) {
					token = (uint8_t)(visibleWidth - destinationOffset);
					memcpy(destination + destinationOffset, rowStart + tokenOffset, token);
					break;
				}

				memcpy(destination + destinationOffset, rowStart + tokenOffset, token);
				destinationOffset = endOffset;
				tokenOffset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				endOffset = destinationOffset + token;
				if (endOffset >= visibleWidth) {
					token = (uint8_t)(visibleWidth - destinationOffset);
					memset(destination + destinationOffset, 0, token);
					break;
				}

				memset(destination + destinationOffset, 0, token);
				destinationOffset = endOffset;
			} else {
				value = rowStart[tokenOffset++];
				count = token;
				endOffset = destinationOffset + count;
				if (endOffset >= visibleWidth) {
					token = (uint8_t)(visibleWidth - destinationOffset);
					memset(destination + destinationOffset, value, token);
					break;
				}

				memset(destination + destinationOffset, value, count);
				destinationOffset = endOffset;
			}
		}

		row += rowLength;
		destination += g_frontState.drawSurfacePitch;
		--rowsRemaining;
	} while (rowsRemaining != 0);
}

#ifndef XVT_MODERN
#pragma optimize("y", off)
#endif
// FUNCTION: XVT 0x4B6340
void FrontImage_BlitRLE16Opaque(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
								int visibleWidth, int visibleHeight) {
	const uint8_t* row;
	const uint8_t* rowStart;
	uint8_t* destination;
	int rowLength;
	int tokenOffset;
	int destinationOffset;
	uint8_t started;
	uint8_t token;
	int count;
	int endOffset;
	int rowsRemaining;
	int color;
	int pixelIndex;
	uint16_t* fillDestination;
	int fillCount;
	int sourceRowsToSkip;

	if (visibleWidth == image->width) {
		int fastRowsRemaining;

		row = image->pixels;
		destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + 2 * destX];
		sourceRowsToSkip = srcTop;
		if (sourceRowsToSkip > 0) {
			do {
#ifdef XVT_MODERN
				memcpy(&rowLength, row, sizeof(rowLength));
#else
				rowLength = *(const int*)row;
#endif
				row += rowLength;
				--sourceRowsToSkip;
			} while (sourceRowsToSkip != 0);
		}

		if (visibleHeight <= 0)
			return;

		fastRowsRemaining = visibleHeight;
		do {
			int fastDestinationOffset;

			fastDestinationOffset = 0;
			row += sizeof(rowLength);
			for (;;) {
				token = *row++;
				if (token == 0x80)
					break;

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					for (pixelIndex = 0; pixelIndex < token; ++pixelIndex) {
						*(uint16_t*)&destination[2 * (fastDestinationOffset + pixelIndex)] =
							image->colorLUT[row[pixelIndex]];
					}
					fastDestinationOffset += token;
					row += token;
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					color = image->colorLUT[0];
					if (token != 0) {
						fillDestination = (uint16_t*)&destination[2 * fastDestinationOffset];
						for (fillCount = token; fillCount != 0; --fillCount) {
							*fillDestination++ = (uint16_t)color;
						}
					}
					fastDestinationOffset += token;
				} else {
					color = image->colorLUT[*row++];
					if (token != 0) {
						fillDestination = (uint16_t*)&destination[2 * fastDestinationOffset];
						for (fillCount = token; fillCount != 0; --fillCount) {
							*fillDestination++ = (uint16_t)color;
						}
					}
					fastDestinationOffset += token;
				}
			}

			destination += g_frontState.drawSurfacePitch & ~1;
			--fastRowsRemaining;
		} while (fastRowsRemaining != 0);
		return;
	}

	row = image->pixels;
	destination = &g_drawSurfacePtr[g_frontState.drawSurfacePitch * destY + 2 * destX];
	sourceRowsToSkip = srcTop;
	if (sourceRowsToSkip > 0) {
		do {
#ifdef XVT_MODERN
			memcpy(&rowLength, row, sizeof(rowLength));
#else
			rowLength = *(const int*)row;
#endif
			--sourceRowsToSkip;
			row += rowLength;
		} while (sourceRowsToSkip != 0);
	}

	if (visibleHeight <= 0)
		return;

	rowsRemaining = visibleHeight;
	do {
		rowStart = row;
		destinationOffset = 0;
		tokenOffset = sizeof(rowLength);
#ifdef XVT_MODERN
		memcpy(&rowLength, rowStart, sizeof(rowLength));
#else
		rowLength = *(const int*)rowStart;
#endif
		started = 0;

		for (;;) {
			token = rowStart[tokenOffset++];
			if (started == 0) {
				if (token == 0x80)
					break;
				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					endOffset = destinationOffset + count;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						tokenOffset += count;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x80;
					} else {
						destinationOffset = endOffset;
						tokenOffset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token != 0)
							token |= 0x40;
					} else {
						destinationOffset += count;
					}
				} else {
					count = token;
					endOffset = destinationOffset + token;
					if (endOffset >= srcLeft) {
						count = srcLeft - destinationOffset;
						started = 1;
						token -= count;
						destinationOffset = 0;
						if (token == 0)
							++tokenOffset;
					} else {
						++tokenOffset;
						destinationOffset += count;
					}
				}
			}

			if (started != 1 || token == 0)
				continue;
			if (token == 0x80)
				break;

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				endOffset = destinationOffset + token;
				if (endOffset >= visibleWidth) {
					count = (uint8_t)(visibleWidth - destinationOffset);
					for (pixelIndex = 0; pixelIndex < count; ++pixelIndex) {
						*(uint16_t*)&destination[2 * (destinationOffset + pixelIndex)] =
							image->colorLUT[rowStart[tokenOffset + pixelIndex]];
					}
					break;
				}

				for (pixelIndex = 0; pixelIndex < token; ++pixelIndex) {
					*(uint16_t*)&destination[2 * (destinationOffset + pixelIndex)] =
						image->colorLUT[rowStart[tokenOffset + pixelIndex]];
				}
				destinationOffset = endOffset;
				tokenOffset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				endOffset = destinationOffset + token;
				color = image->colorLUT[0];
				if (endOffset >= visibleWidth) {
					count = (uint8_t)(visibleWidth - destinationOffset);
					fillDestination = (uint16_t*)&destination[2 * destinationOffset];
					for (fillCount = count; fillCount != 0; --fillCount) {
						*fillDestination++ = (uint16_t)color;
					}
					break;
				}

				fillDestination = (uint16_t*)&destination[2 * destinationOffset];
				for (fillCount = token; fillCount != 0; --fillCount) {
					*fillDestination++ = (uint16_t)color;
				}
				destinationOffset = endOffset;
			} else {
				color = image->colorLUT[rowStart[tokenOffset++]];
				endOffset = destinationOffset + token;
				if (endOffset >= visibleWidth) {
					count = (uint8_t)(visibleWidth - destinationOffset);
					fillDestination = (uint16_t*)&destination[2 * destinationOffset];
					for (fillCount = count; fillCount != 0; --fillCount) {
						*fillDestination++ = (uint16_t)color;
					}
					break;
				}

				fillDestination = (uint16_t*)&destination[2 * destinationOffset];
				for (fillCount = token; fillCount != 0; --fillCount) {
					*fillDestination++ = (uint16_t)color;
				}
				destinationOffset = endOffset;
			}
		}

		row += rowLength;
		destination += g_frontState.drawSurfacePitch & ~1;
		--rowsRemaining;
	} while (rowsRemaining != 0);
}
#ifndef XVT_MODERN
#pragma optimize("y", on)
#endif

// FUNCTION: XVT 0x4B6780
int FrontImage_DrawGlyph(ImageResource* glyph, int x, int y, unsigned int color, int allowColorRemap) {
	int clipResult;
	RECT sourceRect;
	RECT originalRect;

	if (glyph == NULL)
		return 0;

	sourceRect.left = x;
	sourceRect.top = y;
	sourceRect.right = x + glyph->width - 1;
	sourceRect.bottom = y + glyph->height - 1;
	FrontendDraw_RectCopy(&originalRect, &sourceRect);
	clipResult = FrontendDraw_RectClipToBounds(&sourceRect);
	if (sourceRect.right < sourceRect.left)
		return clipResult;
	if (sourceRect.bottom < sourceRect.top)
		return clipResult;

#ifdef XVT_MODERN
	XvtRenderFrontend_Glyph(glyph, x, y, color, allowColorRemap);
#endif
	{
		int clipLeftSkip;
		int clipTopSkip;
		int visibleWidth;
		int visibleRows;
		int displayBpp;

		clipLeftSkip = sourceRect.left - originalRect.left;
		clipTopSkip = sourceRect.top - originalRect.top;
		visibleWidth = sourceRect.right - clipLeftSkip - originalRect.right + glyph->width;
		visibleRows = sourceRect.bottom + glyph->height - originalRect.bottom - clipTopSkip;
		displayBpp = g_frontState.displayBpp;
		if (glyph->isCompressed == 0) {
			switch (displayBpp) {
				case 8: {
					uint8_t* source;
					uint8_t* destination;

					source = &glyph->pixels[glyph->width * clipTopSkip + clipLeftSkip];
					destination = &g_drawSurfacePtr[x + clipLeftSkip +
													g_frontState.drawSurfacePitch * (y + clipTopSkip)];
					if (visibleRows > 0) {
						do {
							int column;

							for (column = 0; column < visibleWidth; ++column) {
								if (source[column] != 0)
									destination[column] = (uint8_t)color;
							}
							source += glyph->width;
							destination += g_frontState.drawSurfacePitch;
							--visibleRows;
						} while (visibleRows != 0);
					}
					break;
				}
				case 16: {
					uint8_t* source;
					uint8_t* destination;
					uint16_t drawColor;

					drawColor = (uint16_t)color;
					if (allowColorRemap != 0 && g_frontState.glyphScratchTtl != 0)
						drawColor = (uint16_t)FrontImage_GetFadedGlyphColor16(color);
					source = &glyph->pixels[glyph->width * clipTopSkip + clipLeftSkip];
					destination = &g_drawSurfacePtr[2 * (x + clipLeftSkip) +
													g_frontState.drawSurfacePitch * (y + clipTopSkip)];
					if (visibleRows > 0) {
						do {
							int column;

							for (column = 0; column < visibleWidth; ++column) {
								if (source[column] != 0)
									*(uint16_t*)&destination[2 * column] = drawColor;
							}
							source += glyph->width;
							destination += g_frontState.drawSurfacePitch & 0xFFFFFFFE;
							--visibleRows;
						} while (visibleRows != 0);
					}
					break;
				}
			}
		} else {
			switch (displayBpp) {
				case 8:
					FrontImage_BlitGlyphRLE_8bpp(glyph, x + clipLeftSkip, y + clipTopSkip, clipLeftSkip,
												 clipTopSkip, visibleWidth, visibleRows, (uint8_t)color);
					break;
				case 16: {
					unsigned int drawColor;

					drawColor = color;
					if (allowColorRemap != 0 && g_frontState.glyphScratchTtl != 0)
						drawColor = FrontImage_GetFadedGlyphColor16(color);
					FrontImage_BlitGlyphRLE_16bpp(glyph, x + clipLeftSkip, y + clipTopSkip, clipLeftSkip,
												  clipTopSkip, visibleWidth, visibleRows, drawColor);
					break;
				}
			}
		}
	}
	return clipResult;
}

// FUNCTION: XVT 0x4B69B0
void FrontImage_BlitGlyphRLE_8bpp(ImageResource* glyph, int destX, int destY, int clipLeftSkip,
								  int clipTopSkip, int visibleWidth, int visibleRows, uint8_t color) {
	const uint8_t* row;
	uint8_t* destination;
	uint8_t token;
	int destinationOffset;
	int y;

	if (visibleWidth == glyph->width) {
		row = glyph->pixels;
		destination = &g_drawSurfacePtr[destX + g_frontState.drawSurfacePitch * destY];
		for (y = 0; y < clipTopSkip; ++y) {
			int rowLength;

#ifdef XVT_MODERN
			memcpy(&rowLength, row, sizeof(rowLength));
#else
			rowLength = *(const int*)row;
#endif
			row += rowLength;
		}

		if (visibleRows <= 0)
			return;

		y = visibleRows;
		destinationOffset = 0;
		row += 4;
		for (;;) {
			token = *row++;
			if (token != 0x80) {
				int count;

				if ((token & 0x80) != 0) {
					token &= 0x7F;
					count = token;
					memset(destination + destinationOffset, color, count);
					destinationOffset += count;
					row += count;
					continue;
				}

				if ((token & 0x40) != 0) {
					destinationOffset += token & 0x3F;
					continue;
				}

				count = token;
				++row;
				memset(destination + destinationOffset, color, count);
				destinationOffset += count;
				continue;
			}

			destination += g_frontState.drawSurfacePitch;
			if (--y == 0)
				return;
			destinationOffset = 0;
			row += 4;
		}
	}

	row = glyph->pixels;
	destination = &g_drawSurfacePtr[destX + g_frontState.drawSurfacePitch * destY];
	for (y = 0; y < clipTopSkip; ++y) {
		int rowLength;

#ifdef XVT_MODERN
		memcpy(&rowLength, row, sizeof(rowLength));
#else
		rowLength = *(const int*)row;
#endif
		row += rowLength;
	}

	for (y = 0; y < visibleRows; ++y) {
		int rowLength;
		int tokenOffset;
		char started;

		destinationOffset = 0;
		tokenOffset = 4;
		started = 0;
#ifdef XVT_MODERN
		memcpy(&rowLength, row, sizeof(rowLength));
#else
		rowLength = *(const int*)row;
#endif
		for (;;) {
			token = row[tokenOffset++];
			if (started == 0) {
				if (token == 0x80)
					break;

				if ((token & 0x80) != 0) {
					token &= 0x7F;
					if (clipLeftSkip <= destinationOffset + token) {
						int skip;

						skip = clipLeftSkip - destinationOffset;
						started = 1;
						destinationOffset = 0;
						tokenOffset += skip;
						token -= (uint8_t)skip;
						if (token != 0)
							token |= 0x80;
					} else {
						destinationOffset += token;
						tokenOffset += token;
					}
				} else if ((token & 0x40) != 0) {
					token &= 0x3F;
					if (clipLeftSkip <= destinationOffset + token) {
						int skip;

						skip = clipLeftSkip - destinationOffset;
						started = 1;
						destinationOffset = 0;
						token -= (uint8_t)skip;
						if (token != 0)
							token |= 0x40;
					} else {
						destinationOffset += token;
					}
				} else {
					if (clipLeftSkip <= destinationOffset + token) {
						int skip;

						skip = clipLeftSkip - destinationOffset;
						started = 1;
						destinationOffset = 0;
						token -= (uint8_t)skip;
						if (token == 0)
							++tokenOffset;
					} else {
						++tokenOffset;
						destinationOffset += token;
					}
				}
			}

			if (started != 1 || token == 0)
				continue;
			if (token == 0x80)
				break;

			if ((token & 0x80) != 0) {
				token &= 0x7F;
				if (destinationOffset + token >= visibleWidth) {
					token = (uint8_t)(visibleWidth - destinationOffset);
					memset(destination + destinationOffset, color, token);
					break;
				}

				memset(destination + destinationOffset, color, token);
				destinationOffset += token;
				tokenOffset += token;
			} else if ((token & 0x40) != 0) {
				destinationOffset += token & 0x3F;
				if (destinationOffset >= visibleWidth)
					break;
			} else {
				++tokenOffset;
				if (destinationOffset + token >= visibleWidth) {
					token = (uint8_t)(visibleWidth - destinationOffset);
					memset(destination + destinationOffset, color, token);
					break;
				}

				memset(destination + destinationOffset, color, token);
				destinationOffset += token;
			}
		}

		row += rowLength;
		destination += g_frontState.drawSurfacePitch;
	}
}

// FUNCTION: XVT 0x4B6CA0
void FrontImage_BlitGlyphRLE_16bpp(ImageResource* glyph, int destX, int destY, int clipLeftSkip,
								   int clipTopSkip, int visibleWidth, int visibleRows, unsigned int color) {
	const uint8_t* row;
	uint8_t* destination;
	uint8_t token;
	int destinationOffset;
	int y;

	if (visibleWidth == glyph->width) {
		unsigned int destinationPitch;

		row = glyph->pixels;
		destination = &g_drawSurfacePtr[2 * destX + g_frontState.drawSurfacePitch * destY];
		for (y = clipTopSkip; y > 0; --y) {
			int rowLength;

#ifdef XVT_MODERN
			memcpy(&rowLength, row, sizeof(rowLength));
#else
			rowLength = *(const int*)row;
#endif
			row += rowLength;
		}

		if (visibleRows > 0) {
			y = visibleRows;
			destinationPitch = g_frontState.drawSurfacePitch & 0xFFFFFFFE;
			do {
				destinationOffset = 0;
				row += 4;
				for (;;) {
					int count;

					token = *row++;
					if (token == 0x80)
						break;

					if ((token & 0x80) != 0) {
						uint16_t* fillDestination;
						int fillCount;

						count = token & 0x7F;
						fillDestination = (uint16_t*)&destination[2 * destinationOffset];
						fillCount = count;
						while (fillCount > 0) {
							*fillDestination = (uint16_t)color;
							++fillDestination;
							--fillCount;
						}
						destinationOffset += count;
						row += count;
					} else if ((token & 0x40) != 0) {
						destinationOffset += token & 0x3F;
					} else {
						uint16_t* fillDestination;
						int fillCount;

						count = token;
						++row;
						fillDestination = (uint16_t*)&destination[2 * destinationOffset];
						fillCount = count;
						while (fillCount > 0) {
							*fillDestination = (uint16_t)color;
							++fillDestination;
							--fillCount;
						}
						destinationOffset += count;
					}
				}
				destination += destinationPitch;
				--y;
			} while (y != 0);
		}
		return;
	}

	row = glyph->pixels;
	destination = &g_drawSurfacePtr[2 * destX + g_frontState.drawSurfacePitch * destY];
	for (y = clipTopSkip; y > 0; --y) {
		int rowLength;

#ifdef XVT_MODERN
		memcpy(&rowLength, row, sizeof(rowLength));
#else
		rowLength = *(const int*)row;
#endif
		row += rowLength;
	}

	if (visibleRows > 0) {
		unsigned int destinationPitch;

		y = visibleRows;
		destinationPitch = g_frontState.drawSurfacePitch & 0xFFFFFFFE;
		do {
			int rowLength;
			int tokenOffset;
			char started;

			destinationOffset = 0;
			tokenOffset = 4;
			started = 0;
#ifdef XVT_MODERN
			memcpy(&rowLength, row, sizeof(rowLength));
#else
			rowLength = *(const int*)row;
#endif
			for (;;) {
				token = row[tokenOffset++];
				if (started == 0) {
					if (token == 0x80)
						break;

					if ((token & 0x80) != 0) {
						token &= 0x7F;
						if (clipLeftSkip <= destinationOffset + token) {
							int skip;

							skip = clipLeftSkip - destinationOffset;
							started = 1;
							destinationOffset = 0;
							tokenOffset += skip;
							token -= (uint8_t)skip;
							if (token != 0)
								token |= 0x80;
						} else {
							destinationOffset += token;
							tokenOffset += token;
						}
					} else if ((token & 0x40) != 0) {
						token &= 0x3F;
						if (clipLeftSkip <= destinationOffset + token) {
							int skip;

							skip = clipLeftSkip - destinationOffset;
							started = 1;
							destinationOffset = 0;
							token -= (uint8_t)skip;
							if (token != 0)
								token |= 0x40;
						} else {
							destinationOffset += token;
						}
					} else {
						if (clipLeftSkip <= destinationOffset + token) {
							int skip;

							skip = clipLeftSkip - destinationOffset;
							started = 1;
							destinationOffset = 0;
							token -= (uint8_t)skip;
							if (token == 0)
								++tokenOffset;
						} else {
							++tokenOffset;
							destinationOffset += token;
						}
					}
				}

				if (started != 1 || token == 0)
					continue;
				if (token == 0x80)
					break;

				if ((token & 0x80) != 0) {
					int count;
					uint16_t* fillDestination;
					int fillCount;

					count = token & 0x7F;
					if (destinationOffset + count >= visibleWidth) {
						count = (uint8_t)(visibleWidth - destinationOffset);
						fillDestination = (uint16_t*)&destination[2 * destinationOffset];
						fillCount = count;
						while (fillCount > 0) {
							*fillDestination = (uint16_t)color;
							++fillDestination;
							--fillCount;
						}
						break;
					}
					fillDestination = (uint16_t*)&destination[2 * destinationOffset];
					fillCount = count;
					while (fillCount > 0) {
						*fillDestination = (uint16_t)color;
						++fillDestination;
						--fillCount;
					}
					destinationOffset += count;
					tokenOffset += count;
				} else if ((token & 0x40) != 0) {
					destinationOffset += token & 0x3F;
					if (destinationOffset >= visibleWidth)
						break;
				} else {
					int count;
					uint16_t* fillDestination;
					int fillCount;

					++tokenOffset;
					count = token;
					if (destinationOffset + count >= visibleWidth) {
						count = (uint8_t)(visibleWidth - destinationOffset);
						fillDestination = (uint16_t*)&destination[2 * destinationOffset];
						fillCount = count;
						while (fillCount > 0) {
							*fillDestination = (uint16_t)color;
							++fillDestination;
							--fillCount;
						}
						break;
					}
					fillDestination = (uint16_t*)&destination[2 * destinationOffset];
					fillCount = count;
					while (fillCount > 0) {
						*fillDestination = (uint16_t)color;
						++fillDestination;
						--fillCount;
					}
					destinationOffset += count;
				}
			}

			row += rowLength;
			destination += destinationPitch;
			--y;
		} while (y != 0);
	}
}

// FUNCTION: XVT 0x4B6FB0
int FrontImage_LoadBmpFile(const char* fileName, ImageResource* image, int makePalette, int compressRLE) {
	uint8_t* pixels;
	XvtFile* stream;
	int result;
	BITMAPINFOHEADER infoHeader;
	BITMAPFILEHEADER fileHeader;
	uint8_t palette[256 * 4];

	pixels = NULL;
	stream = File_Open(fileName, "rb");
	result = 0;
	memset(palette, 0, sizeof(palette));
	if (stream != NULL) {
		File_ReadCount(stream, &fileHeader, sizeof(fileHeader));
		if (fileHeader.bfType == 0x4D42) {
			int rowPadding;

			File_ReadCount(stream, &infoHeader, sizeof(infoHeader));
			rowPadding = infoHeader.biWidth % 4;
			if (rowPadding != 0)
				rowPadding = 4 - rowPadding;
			pixels = malloc(rowPadding + infoHeader.biHeight * infoHeader.biWidth);
			if (pixels != NULL) {
				memset(pixels, 0, rowPadding + infoHeader.biHeight * infoHeader.biWidth);
				if (infoHeader.biPlanes == 1) {
					unsigned int bitsPerPixel;
					int displayBpp;

					bitsPerPixel = infoHeader.biBitCount;
					switch (bitsPerPixel) {
						case 4:
							FrontImage_ReadBmpPalette(stream, palette, 16);
							result = FrontImage_DecodeBmp4bpp(stream, pixels, &fileHeader, &infoHeader);
							break;
						case 8:
							FrontImage_ReadBmpPalette(stream, palette, 256);
							result = FrontImage_DecodeBmp8bpp(stream, pixels, &fileHeader, &infoHeader);
							break;
					}

					displayBpp = g_frontState.displayBpp;
					switch (displayBpp) {
						case 8:
							if (result == 1 && makePalette == 1)
								FrontImage_RemapPalette(pixels, palette, &infoHeader);
							break;

						case 16: {
							int* colorEntry;
							uint8_t* paletteEntry;

							paletteEntry = palette;
							colorEntry = image->colorLUT;
							do {
								if (g_frontState.pixelFormat555 != 0) {
									int green;
									int red;
									int blue;
									int color;

									green = paletteEntry[1] >> 3;
									color = green;
									red = paletteEntry[0] >> 3;
									red <<= 5;
									color += red;
									blue = paletteEntry[2] >> 3;
									color <<= 5;
									*colorEntry = color + blue;
								} else {
									int red;
									int green;
									int blue;
									int color;

									red = paletteEntry[0] >> 3;
									color = red << 6;
									green = paletteEntry[1] >> 2;
									color += green;
									blue = paletteEntry[2] >> 3;
									color <<= 5;
									*colorEntry = color + blue;
								}
								paletteEntry += 4;
								++colorEntry;
							} while (paletteEntry < palette + sizeof(palette));
							break;
						}
					}
				}
			}
		}
		File_Close(stream);
	}

	if (result != 1) {
		if (pixels != NULL)
			free(pixels);
		return 0;
	}

	{
		int imageHeight;

		imageHeight = infoHeader.biHeight;
		image->width = infoHeader.biWidth;
		image->height = imageHeight;
		image->pixels = pixels;
		image->isCompressed = 0;
		image->pixelCount = image->height * infoHeader.biWidth;
		if (compressRLE == 1)
			FrontImage_CompressRLE(image);
	}
#ifdef XVT_MODERN
	XvtRenderAssets_RegisterFrontendImage(image, fileName, makePalette, g_frontState.pixelFormat555);
#endif
	return 1;
}

// FUNCTION: XVT 0x4B7240
int FrontImage_DecodeBmp4bpp(XvtFile* stream, void* dstPixels, const BITMAPFILEHEADER* fileHeader,
							 const BITMAPINFOHEADER* infoHeader) {
	size_t dataSize;
	uint8_t* data;

	dataSize = fileHeader->bfSize - fileHeader->bfOffBits;
	data = malloc(dataSize);
	if (data == NULL)
		return 0;

	if (infoHeader->biCompression == 0) {
		int srcOffset;
		int16_t row;

		srcOffset = 0;
		File_ReadCount(stream, data, dataSize);
		for (row = 0; row < infoHeader->biHeight; ++row) {
			int16_t column;
			int dstRowOffset;

			dstRowOffset = infoHeader->biWidth * (infoHeader->biHeight - row - 1);
			for (column = 0; column < infoHeader->biWidth; ++column) {
				if ((column & 1) != 0) {
					((uint8_t*)dstPixels)[dstRowOffset + column] = data[srcOffset] & 0x0F;
					++srcOffset;
				} else {
					((uint8_t*)dstPixels)[dstRowOffset + column] = data[srcOffset] >> 4;
				}
			}

			if ((infoHeader->biWidth & 1) != 0)
				++srcOffset;
			if ((infoHeader->biWidth & 6) != 0)
				srcOffset += (int16_t)(4 - ((infoHeader->biWidth >> 1) & 3));
		}
	}

	free(data);
	return 1;
}

// FUNCTION: XVT 0x4B7320
int FrontImage_DecodeBmp8bpp(XvtFile* stream, void* dstPixels, const BITMAPFILEHEADER* fileHeader,
							 const BITMAPINFOHEADER* infoHeader) {
	int rowPadding;
	uint8_t* data;
	int sourceOffset;
	int destinationOffset;
	uint8_t paddingBuffer[4];

	(void)fileHeader;

	rowPadding = infoHeader->biWidth % 4;
	if (rowPadding != 0)
		rowPadding = 4 - rowPadding;

	data = NULL;
	if (infoHeader->biCompression != 0) {
		data = malloc(infoHeader->biSizeImage);
		if (data == NULL)
			return 0;
	}

	switch (infoHeader->biCompression) {
		case 0: {
			int16_t row;

			for (row = 0; row < infoHeader->biHeight; ++row) {
				File_ReadCount(stream,
							   (uint8_t*)dstPixels + infoHeader->biWidth * (infoHeader->biHeight - row - 1),
							   infoHeader->biWidth);
				File_ReadCount(stream, paddingBuffer, rowPadding);
			}
			break;
		}
		case 1: {
			int rowStartOffset;
			int16_t decodeComplete;

			File_ReadCount(stream, data, infoHeader->biSizeImage);
			sourceOffset = 0;
			decodeComplete = 0;
			destinationOffset = infoHeader->biWidth * (infoHeader->biHeight - 1);
			rowStartOffset = destinationOffset;
			do {
				uint8_t runLength;

				runLength = data[sourceOffset];
				++sourceOffset;
				if (runLength == 0) {
					uint8_t escapeCode;

					escapeCode = data[sourceOffset];
					++sourceOffset;
					switch (escapeCode) {
						case 0:
							rowStartOffset -= infoHeader->biWidth;
							destinationOffset = rowStartOffset;
							break;
						case 1:
							decodeComplete = 1;
							break;
						case 2: {
							uint8_t deltaX;
							uint8_t deltaY;
							int columnOffset;

							deltaX = data[sourceOffset];
							++sourceOffset;
							deltaY = data[sourceOffset];
							++sourceOffset;
							columnOffset = destinationOffset - rowStartOffset;
							rowStartOffset -= deltaY * infoHeader->biWidth;
							destinationOffset = deltaX + rowStartOffset + columnOffset;
							break;
						}
						default: {
							int16_t runIndex;

							for (runIndex = 0; runIndex < escapeCode; ++runIndex) {
								((uint8_t*)dstPixels)[destinationOffset] = data[sourceOffset];
								++sourceOffset;
								++destinationOffset;
							}
							if ((escapeCode & 1) != 0)
								++sourceOffset;
							break;
						}
					}
				} else {
					uint8_t runValue;
					int16_t runIndex;

					runValue = data[sourceOffset];
					++sourceOffset;
					for (runIndex = 0; runIndex < runLength; ++runIndex) {
						((uint8_t*)dstPixels)[destinationOffset] = runValue;
						++destinationOffset;
					}
				}
			} while (decodeComplete == 0);
			break;
		}
		default:
			break;
	}

	if (data != NULL)
		free(data);
	return 1;
}

// FUNCTION: XVT 0x4B7510
void FrontImage_RemapPalette(uint8_t* pixels, const uint8_t* srcPalette, const BITMAPINFOHEADER* infoHeader) {
	int width;
	int height;
	int i;

	width = infoHeader->biWidth;
	height = infoHeader->biHeight;
	for (i = 0; i < 256; ++i) {
		g_paletteRemapCache[i] = 0x100;
	}

	if (height > 0) {
		int rows;

		rows = height;
		do {
			if (width > 0) {
				int x;

				x = width;
				do {
					uint8_t srcIndex;

					srcIndex = *pixels;
					*pixels = FrontImage_RemapPaletteIndex(&srcPalette[4 * srcIndex], srcIndex);
					++pixels;
					--x;
				} while (x != 0);
			}

			--rows;
		} while (rows != 0);
	}
}

// FUNCTION: XVT 0x4B7570
char FrontImage_RemapPaletteIndex(const uint8_t* srcRgb, int srcIndex) {
	int16_t* cachedIndex;
	int value;

	cachedIndex = &g_paletteRemapCache[srcIndex];
	if (*cachedIndex < 256) {
		return (char)*cachedIndex;
	}
	if (srcIndex == 0) {
		return 0;
	}

	value = FrontendDisplay_PackRGB(srcRgb[0], srcRgb[1], srcRgb[2]);
	*cachedIndex = (int16_t)value;
	return (char)value;
}

// FUNCTION: XVT 0x4B75B0
int FrontImage_CompressRLE(ImageResource* image) {
	uint8_t* lastToken;
	uint8_t* compressedPixels;
	int compressedSize;
	uint8_t* sourceRow;
	int encodedWidth;
	uint8_t runLengthByte;
	int sourceSize;
	uint8_t* compressedWrite;
	int row;

	sourceSize = image->width * image->height;
	compressedPixels = malloc(sourceSize);
	if (compressedPixels == NULL) {
		image->isCompressed = 0;
		return 0;
	}

	compressedSize = 0;
	compressedWrite = compressedPixels;
	sourceRow = image->pixels;
	encodedWidth = 640;
	if (image->width <= encodedWidth)
		encodedWidth = image->width;

	row = 0;
	if (image->height > 0) {
		do {
			uint8_t* source;
			uint8_t* tokenWrite;
			uint8_t value;
			int encodedBytes;
			int consumed;
			int copyIndex;
			int rowSize;

			source = sourceRow;
			encodedBytes = 0;
			consumed = 0;
			tokenWrite = g_frontState.rleRowBuffer.data;
			value = *sourceRow;
			lastToken = g_frontState.rleRowBuffer.data;
			g_frontState.rleRowBuffer.data[0] = 0;

			if (encodedWidth > 0) {
				for (;;) {
					int runLength;

					for (runLength = 0; runLength < 63; ++runLength) {
						if (consumed >= encodedWidth)
							break;
						if (source[runLength] != value)
							break;
						++consumed;
					}

					if (value != 0) {
						if (runLength > 2) {
							*tokenWrite = (uint8_t)runLength;
							encodedBytes += 2;
							lastToken = tokenWrite;
							tokenWrite[1] = value;
							tokenWrite += 2;
						} else {
							if ((*lastToken & 0x80u) == 0) {
								++encodedBytes;
								*tokenWrite = (uint8_t)(runLength | 0x80);
								lastToken = tokenWrite++;
								for (copyIndex = 0; copyIndex < runLength; ++copyIndex)
									tokenWrite[copyIndex] = source[copyIndex];
							} else {
								runLengthByte = (uint8_t)(runLength + (*lastToken & 0x7f));
								if (runLengthByte < 0x80) {
									*lastToken = (uint8_t)(runLengthByte | 0x80);
									for (copyIndex = 0; copyIndex < runLength; ++copyIndex)
										tokenWrite[copyIndex] = source[copyIndex];
								} else {
									++encodedBytes;
									*tokenWrite = (uint8_t)(runLength | 0x80);
									lastToken = tokenWrite++;
									for (copyIndex = 0; copyIndex < runLength; ++copyIndex)
										tokenWrite[copyIndex] = source[copyIndex];
								}
							}
							tokenWrite += runLength;
							encodedBytes += runLength;
						}
					} else {
						++encodedBytes;
						lastToken = tokenWrite;
						*tokenWrite++ = (uint8_t)(runLength | 0x40);
					}

					if (consumed >= encodedWidth)
						break;
					source += runLength;
					value = *source;
				}

				*tokenWrite = 0x80;
				rowSize = encodedBytes + 5;
				g_frontState.rleRowBuffer.encodedSize = rowSize;
			} else {
				g_frontState.rleRowBuffer.data[0] = 0x80;
				g_frontState.rleRowBuffer.encodedSize = 5;
				rowSize = 5;
			}

			if (sourceSize < compressedSize + rowSize) {
				free(compressedPixels);
				compressedSize = image->width * image->height + 1;
				break;
			}
			memcpy(compressedWrite, &g_frontState.rleRowBuffer, rowSize);
			compressedWrite += rowSize;
			compressedSize += rowSize;
			++row;
			sourceRow += image->width;
		} while (row < image->height);
	}

	if (image->width * image->height < compressedSize)
		return 0;

	{
		uint8_t* resizedPixels;

		resizedPixels = realloc(compressedPixels, compressedSize);
		if (compressedPixels == NULL) {
			free(compressedPixels);
			image->isCompressed = 0;
			return 0;
		}
		free(image->pixels);
		image->pixels = resizedPixels;
		image->isCompressed = 1;
		image->pixelCount = compressedSize;
	}
	return 1;
}

// FUNCTION: XVT 0x4B7840
int FrontImage_EncodeGlyphRow(FrontImageRleRowBuffer* rowBuffer, const uint8_t* srcPixels, int width) {
	uint8_t* tokenWrite;
	uint8_t* lastToken;
	const uint8_t* source;
	uint8_t value;
	uint8_t runLengthByte;
	int encodedBytes;
	int consumed;
	int copyIndex;
	int rowSize;

	encodedBytes = 0;
	source = srcPixels;
	consumed = 0;
	value = *source;
	rowBuffer->data[0] = 0;
	tokenWrite = rowBuffer->data;
	lastToken = tokenWrite;

	if (width <= 0) {
		*tokenWrite = 0x80;
		rowBuffer->encodedSize = 5;
		return 5;
	}

	for (;;) {
		int runLength;

		for (runLength = 0; runLength < 63; ++runLength) {
			if (consumed >= width)
				break;
			if (source[runLength] != value)
				break;
			++consumed;
		}

		if (value == 0) {
			lastToken = tokenWrite;
			*tokenWrite++ = (uint8_t)(runLength | 0x40);
			++encodedBytes;
		} else if (runLength > 2) {
			++encodedBytes;
			*tokenWrite = (uint8_t)runLength;
			++encodedBytes;
			lastToken = tokenWrite;
			tokenWrite[1] = value;
			tokenWrite += 2;
		} else {
			if ((*lastToken & 0x80u) == 0) {
				*tokenWrite = (uint8_t)(runLength | 0x80);
				++encodedBytes;
				lastToken = tokenWrite++;
				for (copyIndex = 0; copyIndex < runLength; ++copyIndex)
					tokenWrite[copyIndex] = source[copyIndex];
			} else {
				runLengthByte = (uint8_t)(runLength + (*lastToken & 0x7f));
				if (runLengthByte < 0x80) {
					*lastToken = (uint8_t)(runLengthByte | 0x80);
					for (copyIndex = 0; copyIndex < runLength; ++copyIndex)
						tokenWrite[copyIndex] = source[copyIndex];
				} else {
					*tokenWrite = (uint8_t)(runLength | 0x80);
					++encodedBytes;
					lastToken = tokenWrite++;
					for (copyIndex = 0; copyIndex < runLength; ++copyIndex)
						tokenWrite[copyIndex] = source[copyIndex];
				}
			}
			tokenWrite += runLength;
			encodedBytes += runLength;
		}

		if (consumed >= width)
			break;
		source += runLength;
		value = *source;
	}

	*tokenWrite = 0x80;
	rowSize = encodedBytes + 5;
	rowBuffer->encodedSize = rowSize;
	return rowSize;
}

// FUNCTION: XVT 0x4B7970
void FrontImage_InsertResourceSorted(const FrontImageResourceRecord* entry) {
	int insertIndex;
	int resourceIndex;
	int destinationIndex;

	insertIndex = 0;
	while (g_frontState.resourceCount > insertIndex) {
		if (strncmp(entry->name, g_frontState.resourceTable[insertIndex].name, sizeof(entry->name)) < 0) {
			break;
		}
		++insertIndex;
	}

	if (g_frontState.resourceCount > insertIndex) {
		resourceIndex = g_frontState.resourceCount - insertIndex;
		destinationIndex = g_frontState.resourceCount;
		do {
			g_frontState.resourceTable[destinationIndex] = g_frontState.resourceTable[destinationIndex - 1];
			--destinationIndex;
			--resourceIndex;
		} while (resourceIndex != 0);
	}
	g_frontState.resourceTable[insertIndex] = *entry;
	++g_frontState.resourceCount;
}

// FUNCTION: XVT 0x4B7A10
void FrontImage_RemoveResourceAt(int index) {
	int sourceIndex;
	int currentIndex;
	FrontImageResourceRecord* resource;

	currentIndex = index;
	if (index < 0) {
		return;
	}
	if (g_frontState.resourceCount <= index) {
		return;
	}
	if (g_frontState.resourceCount - 1 > index) {
		sourceIndex = index;
		do {
			resource = &g_frontState.resourceTable[sourceIndex];
			*resource = resource[1];
			++currentIndex;
			++sourceIndex;
		} while (g_frontState.resourceCount - 1 > currentIndex);
	}
	--g_frontState.resourceCount;
}

// FUNCTION: XVT 0x4B7A70
int FrontImage_FindResourceByName(const char* name) {
	if (name == NULL) {
		return -1;
	}

	return FrontImage_BSearchResource(g_frontState.resourceTable, g_frontState.resourceCount - 1, name);
}

// FUNCTION: XVT 0x4B7AA0
int FrontImage_BSearchResource(const FrontImageResourceRecord* table, int hi, const char* key) {
	int baseIndex;
	int searchHi;
	int middle;
	int comparison;
	const FrontImageResourceRecord* middleEntry;

	baseIndex = 0;
	searchHi = hi;
	while (1) {
		if (searchHi < 0) {
			return -1;
		}
		middle = searchHi >> 1;
		middleEntry = &table[middle];
		comparison = strncmp(middleEntry->name, key, sizeof(middleEntry->name));
		if (comparison == 0) {
			return middle + baseIndex;
		}
		if (searchHi <= 0) {
			return -1;
		}
		if (comparison < 0) {
			baseIndex += middle + 1;
			searchHi -= middle + 1;
			table = middleEntry + 1;
		} else {
			searchHi = middle - 1;
		}
	}
}

// FUNCTION: XVT 0x4B7B10
int FrontImage_SaveBmpFile(char* fileName, const void* pixels, int width, int height, int pitch, int bpp,
						   int is555, const void* palette) {
	int y;
	int x;
	FrontImageBmpInfoHeader infoHeader;
	FrontImageBmpFileHeader fileHeader;
	const uint8_t* colorTable;
	XvtFile* stream;
	int fileSize;
	int ok;

	colorTable = (const uint8_t*)palette;
	if (bpp == 8 && colorTable == NULL) {
		return 0;
	}

	stream = File_Open(fileName, "wb");
	if (stream == NULL) {
		return 0;
	}

	File_Seek(stream, 54, SEEK_SET);
	fileSize = 54;

	switch (bpp) {
		case 8:
			for (y = height - 1; y >= 0; y--) {
				const uint8_t* row = (const uint8_t*)pixels + y * pitch;

				for (x = 0; x < width; x++) {
					ok = File_WriteByte(stream, colorTable[4 * *row]);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, colorTable[4 * *row + 1]);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, colorTable[4 * *row + 2]);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					fileSize++;
					fileSize++;
					fileSize++;
					row++;
				}

				if ((x & 1) != 0) {
					ok = File_WriteByte(stream, 0);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, 0);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, 0);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					fileSize++;
					fileSize++;
					fileSize++;
				}
			}
			break;

		case 16:
			for (y = height - 1; y >= 0; y--) {
				const uint16_t* row = (const uint16_t*)((const uint8_t*)pixels + y * pitch);

				for (x = 0; x < width; x++) {
					uint16_t value;
					uint16_t greenRed;
					uint16_t green;
					uint16_t red;

					value = *row;
					greenRed = value >> 5;
					if (is555) {
						green = 8 * greenRed;
						red = greenRed >> 5;
					} else {
						green = 4 * greenRed;
						red = greenRed >> 6;
					}

					ok = File_WriteByte(stream, 8 * value);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, green);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, 8 * red);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					fileSize++;
					fileSize++;
					fileSize++;
					row++;
				}

				if ((x & 1) != 0) {
					ok = File_WriteByte(stream, 0);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, 0);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					ok = File_WriteByte(stream, 0);
					if (ok == 0) {
						File_Close(stream);
						return 0;
					}
					fileSize++;
					fileSize++;
					fileSize++;
				}
			}
			break;
	}

	File_Seek(stream, 0, SEEK_SET);
	fileHeader.signature = 0x4d42;
	fileHeader.fileSize = (uint32_t)fileSize;
	fileHeader.pixelOffset = 54;
	infoHeader.headerSize = 40;

	if ((width & 1) != 0) {
		infoHeader.width = width + 1;
	} else {
		infoHeader.width = width;
	}
	infoHeader.height = height;
	infoHeader.planes = 1;
	infoHeader.bitsPerPixel = 24;
	infoHeader.compression = 0;
	infoHeader.imageSize = (uint32_t)(fileSize - 54);
	infoHeader.pixelsPerMeterX = 0;
	infoHeader.pixelsPerMeterY = 0;
	infoHeader.colorsUsed = 0;
	infoHeader.colorsImportant = 0;

	ok = File_WriteCount(stream, &fileHeader, sizeof(fileHeader));
	if (ok == 0) {
		File_Close(stream);
		return 0;
	}
	ok = File_WriteCount(stream, &infoHeader, sizeof(infoHeader));
	if (ok == 0) {
		File_Close(stream);
		return 0;
	}

	File_Close(stream);
	return 1;
}

// FUNCTION: XVT 0x4D6D30
int FrontImage_LoadBmpPaletteFile(const char* fileName, uint8_t* destRgba) {
	XvtFile* stream;
	int result;
	unsigned int bitsPerPixel;
	FrontImageBmpFileHeader fileHeader;
	FrontImageBmpInfoHeader infoHeader;

	if (destRgba == NULL) {
		return 0;
	}

	stream = File_Open(fileName, "rb");
	result = 0;
	if (stream != NULL) {
		File_ReadCount(stream, &fileHeader, sizeof(fileHeader));
		if (fileHeader.signature == 0x4D42) {
			File_ReadCount(stream, &infoHeader, sizeof(infoHeader));
			if (infoHeader.planes == 1) {
				bitsPerPixel = infoHeader.bitsPerPixel;
				switch (bitsPerPixel) {
					case 4:
						FrontImage_ReadBmpPalette(stream, destRgba, 16);
						result = 1;
						break;
					case 8:
						FrontImage_ReadBmpPalette(stream, destRgba, 256);
						result = 1;
						break;
				}
			}
		}
		File_Close(stream);
	}
	return result;
}

// FUNCTION: XVT 0x4D6DD0
void FrontImage_ReadBmpPalette(XvtFile* stream, uint8_t* dest, int count) {
	uint8_t entry[4];
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	int i;

	memset(dest, 0, 256 * 4);
	for (i = 0; i < count; i++) {
		File_ReadCount(stream, entry, sizeof(entry));
		blue = entry[0];
		green = entry[1];
		red = entry[2];
		dest[i * 4] = red;
		dest[i * 4 + 1] = green;
		dest[i * 4 + 2] = blue;
		dest[i * 4 + 3] = 0;
	}
}

// FUNCTION: XVT 0x4D6F50
unsigned int FrontImage_GetFadedGlyphColor16(unsigned int color16) {
	unsigned int green;
	unsigned int blue;
	unsigned int color;
	uint16_t* cacheEntry;
	uint16_t cachedColor;
	unsigned int result;
	unsigned int fadeMultiplier;

	color = color16;
	cacheEntry = &g_frontState.glyphScratchBuffer[color];
	cachedColor = *cacheEntry;
	if (cachedColor != 0) {
		return cachedColor;
	}

	blue = color;
	if (g_frontState.pixelFormat555 != 0) {
		blue &= 0x1F;
		green = color;
		green &= 0x3E0;
		color &= 0x7C00;
		green >>= 5;
		color >>= 10;
	} else {
		blue &= 0x1F;
		green = color;
		green &= 0x7E0;
		color &= 0xF800;
		green >>= 5;
		color >>= 11;
	}

	fadeMultiplier = g_frontState.glyphScratchReload - g_frontState.glyphScratchTtl;
	blue = fadeMultiplier * blue / (unsigned int)g_frontState.glyphScratchReload;
	green = fadeMultiplier * green / (unsigned int)g_frontState.glyphScratchReload;
	color = fadeMultiplier * color / (unsigned int)g_frontState.glyphScratchReload;
	if (g_frontState.pixelFormat555 != 0) {
		color = (color & 0x1F) << 5;
		green &= 0x1F;
		blue &= 0x1F;
	} else {
		color = (color & 0x1F) << 6;
		green &= 0x3F;
		blue &= 0x1F;
	}

	result = ((color + green) << 5) + blue;
	if (result == 0) {
		result = 1;
	}
	*cacheEntry = (uint16_t)result;
	return result;
}

// FUNCTION: XVT 0x4DF880
int FrontImage_LoadResourceList(char* fileName) {
	int compressRLE;
	char resourceFileName[256];
	char resourceName[256];
	XvtFile* stream;
	int fieldCount;

	stream = File_Open(fileName, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(resourceFileName, 255, stream) == NULL) {
		File_Close(stream);
		return 1;
	}

	for (;;) {
#ifdef XVT_MODERN
		fieldCount = File_Scanf(stream, "%255s %255s %d\n", resourceFileName, resourceName, &compressRLE);
#else
		fieldCount = File_Scanf(stream, "%s %s %d\n", resourceFileName, resourceName, &compressRLE);
#endif
		if (fieldCount == EOF) {
			File_Close(stream);
			return 1;
		}
		if (fieldCount != 3) {
			File_Close(stream);
			return 0;
		}
		FrontImage_RegisterResource(resourceFileName, resourceName, 0, compressRLE);
	}
}

// FUNCTION: XVT 0x4DF950
int FrontImage_UnloadResourceList(char* fileName) {
	int ignoredFlags;
	char resourceFileName[256];
	char resourceName[256];
	XvtFile* stream;
	int fieldCount;

	stream = File_Open(fileName, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(resourceFileName, 255, stream) == NULL) {
		File_Close(stream);
		return 1;
	}

	for (;;) {
#ifdef XVT_MODERN
		fieldCount = File_Scanf(stream, "%255s %255s %d\n", resourceFileName, resourceName, &ignoredFlags);
#else
		fieldCount = File_Scanf(stream, "%s %s %d\n", resourceFileName, resourceName, &ignoredFlags);
#endif
		if (fieldCount == EOF) {
			File_Close(stream);
			return 1;
		}
		if (fieldCount != 3) {
			File_Close(stream);
			return 0;
		}
		FrontImage_FreeResourceByName(resourceName);
	}
}

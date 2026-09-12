#ifndef XVT_FRONTEND_FRONT_IMAGE_H
#define XVT_FRONTEND_FRONT_IMAGE_H

#include "xvt/assets/file.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ImageResource {
	int width;
	int height;
	int isCompressed;
	int pixelCount;
	uint8_t* pixels;
	int colorLUT[256];
};

struct FrontImageResourceRecord {
	char name[64];
	ImageResource* image;
};

struct FrontImageRleRowBuffer {
	int encodedSize;
	uint8_t data[1277];
};

int FrontImage_RegisterResourceDefault(const char* fileName, const char* name);
int FrontImage_RegisterResource(const char* fileName, const char* name, int makePalette, int compressRLE);
void FrontImage_FreeResourceByName(const char* name);
void FrontImage_FreeAllResources(void);
int FrontImage_ResourceExists(const char* name);
int FrontImage_GetResourceRect(const char* name, RECT* outRect);
int FrontImage_DrawSpriteTranslucent(const char* name, int x, int y);
int FrontImage_BlitTranslucent(ImageResource* image, int x, int y);
int FrontImage_DrawSpriteRectTransparent(const char* name, RECT* srcRect, int dstX, int dstY);
int FrontImage_BlitRectTransparent(ImageResource* image, RECT* srcRect, int dstX, int dstY);
int FrontImage_DrawSpriteRectTinted(const char* name, RECT* srcRect, int dstX, int dstY,
									unsigned int tintColor);
int FrontImage_BlitRectTinted(ImageResource* image, const RECT* srcRect, int dstX, int dstY,
							  unsigned int tintColor);
int FrontImage_DrawSprite(const char* name, int x, int y);
int FrontImage_BlitClipped(ImageResource* image, int x, int y);
void FrontImage_BlitRLE8(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
						 int visibleWidth, int visibleHeight);
void FrontImage_BlitRLE16(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
						  int visibleWidth, int visibleHeight);
int FrontImage_DrawSpriteOpaque(const char* name, int x, int y);
int FrontImage_BlitOpaque(ImageResource* image, int x, int y);
void FrontImage_BlitRLE8Opaque(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
							   int visibleWidth, int visibleHeight);
void FrontImage_BlitRLE16Opaque(ImageResource* image, int destX, int destY, int srcLeft, int srcTop,
								int visibleWidth, int visibleHeight);
int FrontImage_DrawGlyph(ImageResource* glyph, int x, int y, unsigned int color, int allowColorRemap);
void FrontImage_BlitGlyphRLE_8bpp(ImageResource* glyph, int destX, int destY, int clipLeftSkip,
								  int clipTopSkip, int visibleWidth, int visibleRows, uint8_t color);
void FrontImage_BlitGlyphRLE_16bpp(ImageResource* glyph, int destX, int destY, int clipLeftSkip,
								   int clipTopSkip, int visibleWidth, int visibleRows, unsigned int color);
int FrontImage_LoadBmpFile(const char* fileName, ImageResource* image, int makePalette, int compressRLE);
int FrontImage_DecodeBmp4bpp(XvtFile* stream, void* dstPixels, const BITMAPFILEHEADER* fileHeader,
							 const BITMAPINFOHEADER* infoHeader);
int FrontImage_DecodeBmp8bpp(XvtFile* stream, void* dstPixels, const BITMAPFILEHEADER* fileHeader,
							 const BITMAPINFOHEADER* infoHeader);
void FrontImage_RemapPalette(uint8_t* pixels, const uint8_t* srcPalette, const BITMAPINFOHEADER* infoHeader);
char FrontImage_RemapPaletteIndex(const uint8_t* srcRgb, int srcIndex);
int FrontImage_CompressRLE(ImageResource* image);
int FrontImage_EncodeGlyphRow(FrontImageRleRowBuffer* rowBuffer, const uint8_t* srcPixels, int width);
void FrontImage_InsertResourceSorted(const FrontImageResourceRecord* entry);
void FrontImage_RemoveResourceAt(int index);
int FrontImage_FindResourceByName(const char* name);
int FrontImage_BSearchResource(const FrontImageResourceRecord* table, int hi, const char* key);
int FrontImage_SaveBmpFile(char* fileName, const void* pixels, int width, int height, int pitch, int bpp,
						   int is555, const void* palette);
int FrontImage_LoadBmpPaletteFile(const char* fileName, uint8_t* destRgba);
void FrontImage_ReadBmpPalette(XvtFile* stream, uint8_t* dest, int count);
unsigned int FrontImage_GetFadedGlyphColor16(unsigned int color16);
int FrontImage_LoadResourceList(char* fileName);
int FrontImage_UnloadResourceList(char* fileName);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RENDER_IMAGE_QUANTIZER_H
#define XVT_RENDER_IMAGE_QUANTIZER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct ImageQuantizerPaletteEntry {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t reserved[6];
};

struct ImageQuantizerNode {
	uint8_t childIndex;
	uint8_t level;
	uint8_t childrenMask;
	uint8_t midpointRed;
	uint8_t midpointGreen;
	uint8_t midpointBlue;
	unsigned int paletteIndex;
	unsigned int pixelCount;
	double quantizationError;
	double redSum;
	double greenSum;
	double blueSum;
	struct ImageQuantizerNode* parent;
	struct ImageQuantizerNode* children[8];
};

#pragma pack(pop)
typedef char xvt_size_ImageQuantizerPaletteEntry[(sizeof(ImageQuantizerPaletteEntry) == 9) ? 1 : -1];
#if defined(_MSC_VER) && !defined(XVT_MODERN)
typedef char xvt_size_ImageQuantizerNode[(sizeof(ImageQuantizerNode) == 82) ? 1 : -1];
#else
typedef char xvt_size_ImageQuantizerNode[(sizeof(ImageQuantizerNode) == 118) ? 1 : -1];
#endif

extern unsigned int g_imageQuantizerNodeCount;
extern const double g_imageQuantizerMaxSquaredRgbErrorPerPixel;

void ImageQuantizer_ReportProgress(const char* stage, unsigned int completed, unsigned int total);
void ImageQuantizer_FatalAllocationError(const char* context, const char* message);
void* ImageQuantizer_AllocateImage(void);
void ImageQuantizer_CompressPixelRuns(unsigned int* image);
void ImageQuantizer_DestroyImage(void* image);
int ImageQuantizer_ExpandPixelRuns(uint32_t* image);
void ImageQuantizer_QuantizeImage(unsigned int* image, unsigned int paletteSize, int treeDepth, int dither,
								  int outputMode);
unsigned int ImageQuantizer_AssignPaletteColors(uint32_t* image, unsigned int paletteSize, int dither,
												int outputMode);
void ImageQuantizer_ClassifyImageColors(unsigned int* image);
void ImageQuantizer_FindNearestPaletteEntryRecursive(ImageQuantizerNode* node);
void ImageQuantizer_BuildPaletteEntriesRecursive(ImageQuantizerNode* node);
int ImageQuantizer_DitherImageToPalette(uint32_t* image);
int ImageQuantizer_InitializeColorTree(int treeDepth);
ImageQuantizerNode* ImageQuantizer_AllocateNode(int childIndex, int level, ImageQuantizerNode* parent,
												int midpointRed, int midpointGreen, int midpointBlue);
void ImageQuantizer_CollapseDeepestLevelRecursive(ImageQuantizerNode* node);
unsigned int ImageQuantizer_MergeNodeIntoParent(ImageQuantizerNode* node);
void ImageQuantizer_ReduceColorTree(unsigned int targetColorCount);
void ImageQuantizer_ReduceColorTreePassRecursive(ImageQuantizerNode* node);
void ImageQuantizer_QuantizeImageLists(unsigned int** imageListHeads, unsigned int listCount,
									   unsigned int paletteSize, int treeDepth, int dither, int outputMode);
int ImageQuantizer_BeginPaletteCollection(int targetColorCount, int treeDepth);
void ImageQuantizer_ExportPalette6BitAndDestroy(int colorCount, int treeDepth, uint8_t* paletteRgb);
void ImageQuantizer_ClassifyIndexed16BppImage(const uint8_t* indexedPixels, const uint16_t* palette16,
											  unsigned int width, unsigned int height);
void ImageQuantizer_ClassifyEncodedTexLevelImage(const uint8_t* encodedImage, const uint8_t* paletteRgba,
												 unsigned int width, unsigned int height, int packingMode);

#ifdef __cplusplus
}
#endif

#endif

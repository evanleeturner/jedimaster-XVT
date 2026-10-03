#ifndef XVT_RENDER_IMAGE_QUANTIZER_H
#define XVT_RENDER_IMAGE_QUANTIZER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct ImageQuantizerPaletteEntry {
	uint8_t red;	     /* Red, the rounded mean of a node's pixels. */
	uint8_t green;	     /* Green, the same way. */
	uint8_t blue;	     /* Blue, the same way. */
	uint8_t reserved[6]; /* Nothing reads or writes it by name. */
};

struct ImageQuantizerNode {
	/* Which child of its parent it is: the color's red, green and blue bits
	 * at its level as 4, 2 and 1. */
	uint8_t childIndex;
	uint8_t level; /* Depth in the tree, 0 for the root. */
	/* The (1 << i) bit is set while child i is in the tree. */
	uint8_t childrenMask;
	/* Middle of the node's red range; 0x80 at the root. */
	uint8_t midpointRed;
	uint8_t midpointGreen;	   /* Middle of the node's green range. */
	uint8_t midpointBlue;	   /* Middle of the node's blue range. */
	unsigned int paletteIndex; /* Palette entry built from this node. */
	/* Pixels classified to this node, merged children's included. */
	unsigned int pixelCount;
	/* Sum, over the pixels passing through the node, of their squared
	 * distance to its midpoint; the root gets 196608 per pixel instead. */
	double quantizationError;
	double redSum;	 /* Sum of the red of the node's pixels. */
	double greenSum; /* Sum of the green of the node's pixels. */
	double blueSum;	 /* Sum of the blue of the node's pixels. */
	/* Parent node; the root's parent is itself. */
	struct ImageQuantizerNode *parent;
	/* Children by childIndex, NULL where none was made. */
	struct ImageQuantizerNode *children[8];
};

#pragma pack(pop)
typedef char xvt_size_ImageQuantizerPaletteEntry
	[(sizeof(struct ImageQuantizerPaletteEntry) == 9) ? 1 : -1];
#if defined(_MSC_VER) && !defined(XVT_MODERN)
typedef char xvt_size_ImageQuantizerNode
	[(sizeof(struct ImageQuantizerNode) == 82) ? 1 : -1];
#else
typedef char xvt_size_ImageQuantizerNode
	[(sizeof(struct ImageQuantizerNode) == 118) ? 1 : -1];
#endif

extern unsigned int g_imageQuantizerNodeCount;
extern const double g_imageQuantizerMaxSquaredRgbErrorPerPixel;

void ImageQuantizer_ReportProgress(const char *stage, unsigned int completed,
				   unsigned int total);
void ImageQuantizer_FatalAllocationError(const char *context,
					 const char *message);
void *ImageQuantizer_AllocateImage(void);
void ImageQuantizer_CompressPixelRuns(unsigned int *image);
void ImageQuantizer_DestroyImage(void *image);
int ImageQuantizer_ExpandPixelRuns(uint32_t *image);
void ImageQuantizer_QuantizeImage(unsigned int *image, unsigned int paletteSize,
				  int treeDepth, int dither, int colorspace);
unsigned int ImageQuantizer_AssignPaletteColors(uint32_t *image,
						unsigned int paletteSize,
						int dither, int colorspace);
void ImageQuantizer_ClassifyImageColors(unsigned int *image);
void ImageQuantizer_FindNearestPaletteEntryRecursive(
	struct ImageQuantizerNode *node);
void ImageQuantizer_BuildPaletteEntriesRecursive(
	struct ImageQuantizerNode *node);
int ImageQuantizer_DitherImageToPalette(uint32_t *image);
int ImageQuantizer_InitializeColorTree(int treeDepth);
struct ImageQuantizerNode *
ImageQuantizer_AllocateNode(int childIndex, int level,
			    struct ImageQuantizerNode *parent, int midpointRed,
			    int midpointGreen, int midpointBlue);
void ImageQuantizer_CollapseDeepestLevelRecursive(
	struct ImageQuantizerNode *node);
unsigned int
ImageQuantizer_MergeNodeIntoParent(struct ImageQuantizerNode *node);
void ImageQuantizer_ReduceColorTree(unsigned int targetColorCount);
void ImageQuantizer_ReduceColorTreePassRecursive(
	struct ImageQuantizerNode *node);
void ImageQuantizer_QuantizeImageLists(unsigned int **imageListHeads,
				       unsigned int listCount,
				       unsigned int paletteSize, int treeDepth,
				       int dither, int colorspace);
int ImageQuantizer_BeginPaletteCollection(int targetColorCount, int treeDepth);
void ImageQuantizer_ExportPalette6BitAndDestroy(int colorCount, int treeDepth,
						uint8_t *paletteRgb);
void ImageQuantizer_ClassifyIndexedRgb565Image(const uint8_t *indexedPixels,
					       const uint16_t *palette16,
					       unsigned int width,
					       unsigned int height);
void ImageQuantizer_ClassifyEncodedTexLevelImage(const uint8_t *encodedImage,
						 const uint8_t *paletteRgba,
						 unsigned int width,
						 unsigned int height,
						 int packingMode);

#ifdef __cplusplus
}
#endif

#endif

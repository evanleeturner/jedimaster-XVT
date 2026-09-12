#include "xvt/render/image_quantizer.h"

#include "xvt/flight/fediskio.h"
#include "xvt/render/flight_sw.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma pack(push, 1)

typedef struct ImageQuantizerPixelRun {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t length;
	uint16_t paletteIndex;
} ImageQuantizerPixelRun;

typedef struct ImageQuantizerImageLayout {
	uint8_t reserved0000[0x1024];
	uint32_t compressionMode;
	uint32_t comparePaletteIndex;
	uint32_t compressionIneffective;
	uint32_t width;
	uint32_t height;
	uint8_t reserved1038[0x4E];
	ImageQuantizerPixelRun* pixels;
	uint8_t reservedAfterPixels[4];
	uint32_t runCount;
	uint32_t sourceRunPixelsRemaining;
} ImageQuantizerImageLayout;

typedef struct ImageQuantizerOwnedBuffers {
	uint8_t reserved0000[0x1014];
	void* buffer1014;
	void* buffer1018;
	uint8_t reserved101C[0x28];
	void* buffer1044;
	void* buffer1048;
	void* buffer104C;
	uint8_t reserved1050[0x32];
	void* buffer1082;
	void* buffer1086;
	uint8_t reserved108A[0x10];
	void* buffer109A;
	uint8_t reserved109E[0x810];
	void* buffer18AE;
	void* buffer18B2;
} ImageQuantizerOwnedBuffers;

typedef struct ImageQuantizerLegacyImageRecord {
	uint32_t field0000;
	uint32_t field0004;
	uint32_t field0008;
	uint8_t field000C;
	uint8_t reserved000D[0x7FF];
	uint32_t field080C;
	uint32_t field0810;
	char formatName[0x800];
	uint32_t field1014;
	uint32_t field1018;
	uint32_t field101C;
	uint32_t field1020;
	uint32_t field1024;
	uint32_t field1028;
	uint32_t field102C;
	uint32_t field1030;
	uint32_t field1034;
	uint32_t field1038;
	uint32_t field103C;
	uint32_t field1040;
	uint32_t field1044;
	uint32_t field1048;
	uint32_t field104C;
	uint32_t field1050;
	uint32_t field1054;
	uint32_t field1058;
	uint32_t field105C;
	uint16_t field1060;
	float field1062;
	float field1066;
	uint32_t field106A;
	uint32_t field106E;
	uint32_t field1072;
	uint32_t field1076;
	uint32_t field107A;
	uint32_t field107E;
	uint32_t field1082;
	uint32_t field1086;
	uint32_t field108A;
	uint32_t field108E;
	uint8_t reserved1092[4];
	uint32_t field1096;
	uint32_t field109A;
	uint32_t timestamp109E;
	uint8_t field10A2;
	uint8_t reserved10A3[0x7FF];
	uint32_t field18A2;
	uint32_t field18A6;
	uint32_t field18AA;
	uint32_t field18AE;
	uint32_t field18B2;
	uint32_t field18B6;
	uint32_t field18BA;
	uint32_t field18BE;
	uint32_t field18C2;
} ImageQuantizerLegacyImageRecord;

#pragma pack(pop)
typedef char xvt_size_ImageQuantizerPixelRun[(sizeof(ImageQuantizerPixelRun) == 6) ? 1 : -1];
typedef char
	xvt_size_ImageQuantizerLegacyImageRecord[(sizeof(ImageQuantizerLegacyImageRecord) == 0x18C6) ? 1 : -1];
#if defined(_MSC_VER) && !defined(XVT_MODERN)
typedef char xvt_size_ImageQuantizerImageLayout[(sizeof(ImageQuantizerImageLayout) == 0x1096) ? 1 : -1];
typedef char xvt_size_ImageQuantizerOwnedBuffers[(sizeof(ImageQuantizerOwnedBuffers) == 0x18B6) ? 1 : -1];
#else
typedef char xvt_size_ImageQuantizerImageLayout[(sizeof(ImageQuantizerImageLayout) == 0x109A) ? 1 : -1];
typedef char xvt_size_ImageQuantizerOwnedBuffers[(sizeof(ImageQuantizerOwnedBuffers) == 0x18DE) ? 1 : -1];
#endif

typedef struct ImageQuantizerNodePoolBlock {
	ImageQuantizerNode nodes[2048];
	struct ImageQuantizerNodePoolBlock* previous;
} ImageQuantizerNodePoolBlock;

// GLOBAL: XVT 0x518148
const double g_imageQuantizerMaxSquaredRgbErrorPerPixel = 196608.0;

// GLOBAL: XVT 0x521C20
static const char g_imageQuantizerUnableToQuantizeMessage[28] = "Unable to quantize image";

// GLOBAL: XVT 0x556928
static ImageQuantizerNode* g_imageQuantizerRoot = 0;
// GLOBAL: XVT 0x55692C
static int g_imageQuantizerMaxTreeDepth = 0;
// GLOBAL: XVT 0x556930
static unsigned int g_imageQuantizerColorCount = 0;
// GLOBAL: XVT 0x556934
static uint8_t g_imageQuantizerSearchRed = 0;
// GLOBAL: XVT 0x556935
static uint8_t g_imageQuantizerSearchGreen = 0;
// GLOBAL: XVT 0x556936
static uint8_t g_imageQuantizerSearchBlue = 0;
// GLOBAL: XVT 0x55693D
static ImageQuantizerPaletteEntry* g_imageQuantizerPaletteEntries = 0;
// GLOBAL: XVT 0x556941
static double g_imageQuantizerNearestDistanceSq = 0.0;
// GLOBAL: XVT 0x556949
static double g_imageQuantizerPruneThreshold = 0.0;
// GLOBAL: XVT 0x556951
static double g_imageQuantizerNextPruneThreshold = 0.0;
// GLOBAL: XVT 0x556959
static uint32_t* g_imageQuantizerSquaredDiffTable = 0;
// GLOBAL: XVT 0x55695D
unsigned int g_imageQuantizerNodeCount = 0;
// GLOBAL: XVT 0x556961
unsigned int g_imageQuantizerPoolNodesRemaining = 0;
// GLOBAL: XVT 0x556965
static unsigned int g_imageQuantizerNearestPaletteIndex = 0;
// GLOBAL: XVT 0x556969
ImageQuantizerNode* g_imageQuantizerNextNode = 0;
// GLOBAL: XVT 0x55696D
ImageQuantizerNodePoolBlock* g_imageQuantizerNodePoolHead = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x443890
void ImageQuantizer_ReportProgress(const char* stage, unsigned int completed, unsigned int total) {
	(void)stage;
	(void)completed;
	(void)total;
}

// FUNCTION: XVT 0x4438B0
void ImageQuantizer_FatalAllocationError(const char* context, const char* message) {
	(void)context;
	(void)message;
	FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
}

// FUNCTION: XVT 0x4438C0
void* ImageQuantizer_AllocateImage(void) {
	ImageQuantizerLegacyImageRecord* image;

	image = malloc(sizeof(ImageQuantizerLegacyImageRecord));
	if (image == NULL) {
		ImageQuantizer_FatalAllocationError("Unable to allocate image", "Memory allocation failed");
		return NULL;
	}

	image->field0000 = 0;
	image->field0004 = 0;
	image->field0008 = 0;
	image->field000C = 0;
	image->field080C = 0;
	image->field0810 = 0;
	strcpy(image->formatName, "MIFF");
	image->field1014 = 0;
	image->field1018 = 0;
	image->field101C = 0;
	image->field1020 = 0;
	image->field1024 = 1;
	image->field1028 = 0;
	image->field102C = 2;
	image->field1030 = 0;
	image->field1034 = 0;
	image->field1038 = 8;
	image->field103C = 0;
	image->field1040 = 0;
	image->field1060 = 2;
	image->field1062 = 72.0f;
	image->field1066 = 72.0f;
	image->field1044 = 0;
	image->field1048 = 0;
	image->field104C = 0;
	image->field1050 = 0;
	image->field1054 = 0;
	image->field1058 = 0;
	image->field105C = 0;
	image->field1076 = 0;
	image->field107A = 0;
	image->field106E = 0;
	image->field1072 = 0;
	image->field106A = 0;
	image->field107E = 0;
	image->field1082 = 0;
	image->field1086 = 0;
	image->field108A = 0;
	image->field108E = 0;
	image->field1096 = 0;
	image->field109A = 0;
	image->field10A2 = 0;
	image->field18A2 = 0;
	image->field18A6 = 0;
	image->timestamp109E = (uint32_t)time(NULL);
	image->field18AA = 0;
	image->field18AE = 0;
	image->field18B2 = 0;
	image->field18B6 = 0;
	image->field18BA = 0;
	image->field18BE = 0;
	image->field18C2 = 0;
	return image;
}

// FUNCTION: XVT 0x443A50
void ImageQuantizer_CompressPixelRuns(unsigned int* image) {
	ImageQuantizerImageLayout* imageLayout;
	ImageQuantizerPixelRun* sourceRun;
	ImageQuantizerPixelRun* destinationRun;
	ImageQuantizerPixelRun* resizedRuns;
	unsigned int pixelIndex;
	unsigned int remaining;
	unsigned int compressionMode;
	unsigned int height;

	pixelIndex = 0;
	if (image == NULL)
		return;

	imageLayout = (ImageQuantizerImageLayout*)image;
	sourceRun = imageLayout->pixels;
	remaining = sourceRun->length + 1;
	imageLayout->runCount = 0;
	imageLayout->sourceRunPixelsRemaining = remaining;
	destinationRun = sourceRun;
	destinationRun->length = 0xFF;
	if (imageLayout->comparePaletteIndex != 0) {
		if (imageLayout->width * imageLayout->height != 0) {
			do {
				remaining = imageLayout->sourceRunPixelsRemaining;
				if (remaining != 0) {
					--remaining;
				} else {
					++sourceRun;
					remaining = sourceRun->length;
				}
				imageLayout->sourceRunPixelsRemaining = remaining;
				if (destinationRun->red == sourceRun->red && destinationRun->green == sourceRun->green &&
					sourceRun->blue == destinationRun->blue &&
					sourceRun->paletteIndex == destinationRun->paletteIndex &&
					destinationRun->length < 0xFF) {
					++destinationRun->length;
				} else {
					if (imageLayout->runCount != 0)
						++destinationRun;
					++imageLayout->runCount;
					*destinationRun = *sourceRun;
					destinationRun->length = 0;
				}
				++pixelIndex;
			} while (imageLayout->width * imageLayout->height > pixelIndex);
		}
	} else {
		for (pixelIndex = 0; imageLayout->width * imageLayout->height > pixelIndex; ++pixelIndex) {
			remaining = imageLayout->sourceRunPixelsRemaining;
			if (remaining != 0) {
				--remaining;
			} else {
				++sourceRun;
				remaining = sourceRun->length;
			}
			imageLayout->sourceRunPixelsRemaining = remaining;
			if (destinationRun->red == sourceRun->red && destinationRun->green == sourceRun->green &&
				sourceRun->blue == destinationRun->blue && destinationRun->length < 0xFF) {
				++destinationRun->length;
			} else {
				if (imageLayout->runCount != 0)
					++destinationRun;
				++imageLayout->runCount;
				*destinationRun = *sourceRun;
				destinationRun->length = 0;
			}
		}
	}

	resizedRuns = realloc(imageLayout->pixels, imageLayout->runCount * sizeof(ImageQuantizerPixelRun));
	compressionMode = imageLayout->compressionMode;
	imageLayout->pixels = resizedRuns;
	height = imageLayout->height;
	if (compressionMode == 1) {
		if ((height * imageLayout->width * 3) / 4 <= imageLayout->runCount)
			imageLayout->compressionIneffective = 1;
	} else if ((height * imageLayout->width) / 2 <= imageLayout->runCount) {
		imageLayout->compressionIneffective = 1;
	}
}

// FUNCTION: XVT 0x443C30
void ImageQuantizer_DestroyImage(void* image) {
	ImageQuantizerOwnedBuffers* ownedBuffers;

	if (image == NULL)
		return;

	ownedBuffers = (ImageQuantizerOwnedBuffers*)image;
	if (ownedBuffers->buffer1014 != NULL)
		free(ownedBuffers->buffer1014);
	if (ownedBuffers->buffer1018 != NULL)
		free(ownedBuffers->buffer1018);
	if (ownedBuffers->buffer1044 != NULL)
		free(ownedBuffers->buffer1044);
	if (ownedBuffers->buffer1048 != NULL)
		free(ownedBuffers->buffer1048);
	if (ownedBuffers->buffer104C != NULL)
		free(ownedBuffers->buffer104C);
	if (ownedBuffers->buffer1082 != NULL)
		free(ownedBuffers->buffer1082);
	if (ownedBuffers->buffer1086 != NULL)
		free(ownedBuffers->buffer1086);
	if (ownedBuffers->buffer109A != NULL)
		free(ownedBuffers->buffer109A);
	if (ownedBuffers->buffer18AE != NULL)
		free(ownedBuffers->buffer18AE);
	if (ownedBuffers->buffer18B2 != NULL)
		free(ownedBuffers->buffer18B2);
	free(ownedBuffers);
}

// FUNCTION: XVT 0x443D10
int ImageQuantizer_ExpandPixelRuns(uint32_t* image) {
	ImageQuantizerImageLayout* imageLayout;
	ImageQuantizerPixelRun* resizedPixels;
	ImageQuantizerPixelRun* destination;
	ImageQuantizerPixelRun* source;
	unsigned int sourceIndex;
	unsigned int pixelCount;
	unsigned int expandedPixelCount;
	unsigned int runCount;
	int copies;

	imageLayout = (ImageQuantizerImageLayout*)image;
	pixelCount = imageLayout->width * imageLayout->height;
	if (imageLayout->runCount == pixelCount)
		return 1;
	resizedPixels =
		(ImageQuantizerPixelRun*)realloc(imageLayout->pixels, pixelCount * sizeof(ImageQuantizerPixelRun));
	if (resizedPixels == NULL)
		return 0;
	runCount = imageLayout->runCount;
	imageLayout->pixels = resizedPixels;
	source = resizedPixels + runCount - 1;
	expandedPixelCount = imageLayout->width * imageLayout->height;
	destination = resizedPixels + expandedPixelCount - 1;
	sourceIndex = 0;
	if (runCount != 0) {
		do {
			copies = source->length;
			if (copies >= 0) {
				++copies;
				do {
					*destination = *source;
					destination->length = 0;
					--destination;
					--copies;
				} while (copies != 0);
			}
			--source;
			++sourceIndex;
		} while (imageLayout->runCount > sourceIndex);
	}
	imageLayout->runCount = imageLayout->width * imageLayout->height;
	return 1;
}

// FUNCTION: XVT 0x443DE0
void ImageQuantizer_QuantizeImage(unsigned int* image, unsigned int paletteSize, int treeDepth, int dither,
								  int outputMode) {
	unsigned int targetColorCount = paletteSize;
	int effectiveDepth = treeDepth;
	int effectiveDither = dither;
	uint32_t* imageWords = image;
	unsigned int value;
	unsigned int storedRunCount;
	ImageQuantizerNodePoolBlock* previousBlock;

	if (paletteSize == 2 && outputMode == 2 && dither != 0) {
		return;
	}
	if (targetColorCount == 0) {
		targetColorCount = 1;
	}
	if (targetColorCount > 0xFFFF) {
		targetColorCount = 0xFFFF;
	}
	memcpy(&storedRunCount, (uint8_t*)image + 4238, sizeof(storedRunCount));
	if (imageWords[1036] * imageWords[1037] == storedRunCount) {
		ImageQuantizer_CompressPixelRuns(image);
	}
	if (effectiveDepth == 0) {
		effectiveDepth = 1;
		value = targetColorCount;
		while (value != 0) {
			value >>= 2;
			++effectiveDepth;
		}
		if (effectiveDither != 0) {
			--effectiveDepth;
		}
		if (imageWords[1033] == 2) {
			effectiveDepth += 2;
		}
	}
	ImageQuantizer_InitializeColorTree(effectiveDepth);
	ImageQuantizer_ClassifyImageColors(image);
	if ((g_imageQuantizerColorCount >> 1) < targetColorCount) {
		effectiveDither = 0;
	}
	if (targetColorCount < g_imageQuantizerColorCount) {
		ImageQuantizer_ReduceColorTree(targetColorCount);
	}
	ImageQuantizer_AssignPaletteColors(image, targetColorCount, effectiveDither, outputMode);
	while (g_imageQuantizerNodePoolHead != NULL) {
		previousBlock = g_imageQuantizerNodePoolHead->previous;
		free(g_imageQuantizerNodePoolHead);
		g_imageQuantizerNodePoolHead = previousBlock;
	}
	g_imageQuantizerSquaredDiffTable -= 255;
	free(g_imageQuantizerSquaredDiffTable);
}

// FUNCTION: XVT 0x443EF0
unsigned int ImageQuantizer_AssignPaletteColors(uint32_t* image, unsigned int paletteSize, int dither,
												int outputMode) {
	ImageQuantizerImageLayout* layout;
	ImageQuantizerPixelRun* pixel;
	ImageQuantizerPaletteEntry* palette;
	uint8_t* paletteBytesPtr;
	unsigned int completed;
	unsigned int paletteBytes;
	unsigned int colorCount;
	unsigned int pixelCount;
	unsigned int level;
	unsigned int childIndex;
	int firstLuma;
	int secondLuma;
	unsigned int offset;
	int bitPosition;
	int result;
	ImageQuantizerNode* node;
	uint8_t* imageBytes;

	layout = (ImageQuantizerImageLayout*)image;
	imageBytes = (uint8_t*)image;
	memcpy(&palette, imageBytes + 0x104C, sizeof(palette));
	free(palette);
	colorCount = g_imageQuantizerColorCount;
	paletteBytes = colorCount * sizeof(ImageQuantizerPaletteEntry);
	palette = malloc(paletteBytes);
	if (palette == NULL) {
		ImageQuantizer_FatalAllocationError("Unable to assign palette", "Memory allocation failed");
		return 1;
	}
	memcpy(imageBytes + 0x104C, &palette, sizeof(palette));
	paletteBytesPtr = (uint8_t*)palette;
	g_imageQuantizerPaletteEntries = palette;
	g_imageQuantizerColorCount = 0;
	ImageQuantizer_BuildPaletteEntriesRecursive(g_imageQuantizerRoot);
	if (paletteSize == 2 && outputMode == 2 && colorCount >= 2) {
		firstLuma = 77 * palette[0].red + 150 * palette[0].green + 29 * palette[0].blue;
		secondLuma = 77 * palette[1].red + 150 * palette[1].green + 29 * palette[1].blue;
		if (firstLuma < secondLuma) {
			palette[0].red = 0;
			palette[0].green = 0;
			palette[0].blue = 0;
			palette[1].red = 255;
			palette[1].green = 255;
			palette[1].blue = 255;
		} else {
			palette[1].red = 0;
			palette[1].green = 0;
			palette[1].blue = 0;
			palette[0].red = 255;
			palette[0].green = 255;
			palette[0].blue = 255;
		}
	}
	if (outputMode != 3) {
		layout->comparePaletteIndex = 0;
		layout->compressionMode = 2;
	}
	memcpy(imageBytes + 0x1054, &g_imageQuantizerColorCount, sizeof(g_imageQuantizerColorCount));
	result = dither ? ImageQuantizer_DitherImageToPalette(image) == 0 : 0;
	if (result != 0) {
		return (unsigned int)result;
	}
	pixel = layout->pixels;
	pixelCount = layout->runCount;
	for (completed = 0; completed < pixelCount; ++completed, ++pixel) {
		node = g_imageQuantizerRoot;
		level = 1;
		bitPosition = 7;
		while (level <= (unsigned int)g_imageQuantizerMaxTreeDepth) {
			childIndex = ((pixel->red >> bitPosition) & 1u) << 2;
			childIndex |= ((pixel->green >> bitPosition) & 1u) << 1;
			childIndex |= (pixel->blue >> bitPosition) & 1u;
			if ((node->childrenMask & (1u << childIndex)) == 0) {
				break;
			}
			node = node->children[childIndex];
			--bitPosition;
			++level;
		}
		g_imageQuantizerSearchRed = pixel->red;
		g_imageQuantizerSearchGreen = pixel->green;
		g_imageQuantizerSearchBlue = pixel->blue;
		g_imageQuantizerNearestDistanceSq = g_imageQuantizerMaxSquaredRgbErrorPerPixel;
		ImageQuantizer_FindNearestPaletteEntryRecursive(node->parent);
		if (layout->compressionMode == 2) {
			pixel->paletteIndex = (uint16_t)g_imageQuantizerNearestPaletteIndex;
		} else {
			offset = 9 * g_imageQuantizerNearestPaletteIndex;
			pixel->red = paletteBytesPtr[offset];
			pixel->green = paletteBytesPtr[offset + 1];
			pixel->blue = paletteBytesPtr[offset + 2];
		}
		if (pixelCount - completed == 1 || completed % layout->height == 0) {
			ImageQuantizer_ReportProgress("  Assigning image colors...  ", completed, pixelCount);
		}
	}
	return 0;
}

// FUNCTION: XVT 0x4441F0
void ImageQuantizer_ClassifyImageColors(unsigned int* image) {
	ImageQuantizerPixelRun* pixel;
	ImageQuantizerNode* node;
	unsigned int completed;
	unsigned int level;
	unsigned int childIndex;
	unsigned int runLength;
	unsigned int midpointOffset;
	int bitPosition;
	int redOffset;
	int greenOffset;
	int blueOffset;
	double pixelWeight;
	double pixelError;

	g_imageQuantizerRoot->quantizationError += (double)((ImageQuantizerImageLayout*)image)->width *
											   (double)((ImageQuantizerImageLayout*)image)->height *
											   g_imageQuantizerMaxSquaredRgbErrorPerPixel;
	pixel = ((ImageQuantizerImageLayout*)image)->pixels;
	for (completed = 0; completed < ((ImageQuantizerImageLayout*)image)->runCount; ++completed) {
		if (g_imageQuantizerNodeCount > 0x41241) {
			ImageQuantizer_CollapseDeepestLevelRecursive(g_imageQuantizerRoot);
			--g_imageQuantizerMaxTreeDepth;
		}

		node = g_imageQuantizerRoot;
		bitPosition = 7;
		level = 1;
		runLength = (unsigned int)pixel->length + 1;
		if ((unsigned int)g_imageQuantizerMaxTreeDepth >= 1) {
			pixelWeight = (double)runLength;
			do {
				childIndex = ((pixel->red >> bitPosition) & 1u) << 2;
				childIndex |= ((pixel->green >> bitPosition) & 1u) << 1;
				childIndex |= (pixel->blue >> bitPosition) & 1u;
				if (node->children[childIndex] == NULL) {
					node->childrenMask |= (uint8_t)(1u << childIndex);
					midpointOffset = (1u << (8 - level)) >> 1;
					blueOffset = midpointOffset;
					if ((childIndex & 1) == 0) {
						blueOffset = -midpointOffset;
					}
					greenOffset = midpointOffset;
					if ((childIndex & 2) == 0) {
						greenOffset = -midpointOffset;
					}
					redOffset = midpointOffset;
					if ((childIndex & 4) == 0) {
						redOffset = -midpointOffset;
					}
					node->children[childIndex] = ImageQuantizer_AllocateNode(
						childIndex, level, node, node->midpointRed + redOffset,
						node->midpointGreen + greenOffset, node->midpointBlue + blueOffset);
					if (node->children[childIndex] == NULL) {
						ImageQuantizer_FatalAllocationError(g_imageQuantizerUnableToQuantizeMessage,
															"Memory allocation failed");
						exit(1);
					}
					if (level == (unsigned int)g_imageQuantizerMaxTreeDepth) {
						++g_imageQuantizerColorCount;
					}
				}
				node = node->children[childIndex];
				pixelError = (double)g_imageQuantizerSquaredDiffTable[(int)pixel->red - node->midpointRed];
				pixelError +=
					(double)g_imageQuantizerSquaredDiffTable[(int)pixel->green - node->midpointGreen];
				pixelError += (double)g_imageQuantizerSquaredDiffTable[(int)pixel->blue - node->midpointBlue];
				node->quantizationError += pixelError * pixelWeight;
				--bitPosition;
				++level;
			} while (level <= (unsigned int)g_imageQuantizerMaxTreeDepth);
		}

		node->pixelCount += runLength;
		node->redSum += (double)(runLength * pixel->red);
		node->greenSum += (double)(runLength * pixel->green);
		node->blueSum += (double)(runLength * pixel->blue);
		++pixel;
		if (((ImageQuantizerImageLayout*)image)->runCount - completed == 1 ||
			completed % ((ImageQuantizerImageLayout*)image)->height == 0) {
			ImageQuantizer_ReportProgress("  Classifying image colors...  ", completed,
										  ((ImageQuantizerImageLayout*)image)->runCount);
		}
	}
}

// FUNCTION: XVT 0x4444F0
void ImageQuantizer_FindNearestPaletteEntryRecursive(ImageQuantizerNode* node) {
	unsigned int childIndex;

	if (node->childrenMask != 0) {
		childIndex = 0;
		do {
			if ((node->childrenMask & (1u << childIndex)) != 0) {
				ImageQuantizer_FindNearestPaletteEntryRecursive(node->children[childIndex]);
			}
			++childIndex;
		} while (childIndex < 8);
	}

	if (node->pixelCount != 0) {
		ImageQuantizerPaletteEntry* entry;
		double distanceSq;

		entry = &g_imageQuantizerPaletteEntries[node->paletteIndex];
		distanceSq =
			(double)g_imageQuantizerSquaredDiffTable[(int)entry->green - g_imageQuantizerSearchGreen];
		distanceSq += (double)g_imageQuantizerSquaredDiffTable[(int)entry->red - g_imageQuantizerSearchRed];
		distanceSq += (double)g_imageQuantizerSquaredDiffTable[(int)entry->blue - g_imageQuantizerSearchBlue];
		if (distanceSq < g_imageQuantizerNearestDistanceSq) {
			g_imageQuantizerNearestDistanceSq = distanceSq;
			g_imageQuantizerNearestPaletteIndex = (uint16_t)node->paletteIndex;
		}
	}
}

// FUNCTION: XVT 0x4445E0
void ImageQuantizer_BuildPaletteEntriesRecursive(ImageQuantizerNode* node) {
	unsigned int childIndex;
	unsigned int pixelCount;
	double halfPixelCount;
	double pixelCountDouble;

	if (node->childrenMask != 0) {
		childIndex = 0;
		do {
			if ((node->childrenMask & (1u << childIndex)) != 0)
				ImageQuantizer_BuildPaletteEntriesRecursive(node->children[childIndex]);
			++childIndex;
		} while (childIndex < 8);
	}

	pixelCount = node->pixelCount;
	if (pixelCount != 0) {
		halfPixelCount = (double)(pixelCount >> 1);
		pixelCountDouble = (double)pixelCount;
		g_imageQuantizerPaletteEntries[g_imageQuantizerColorCount].red =
			(uint8_t)((node->redSum + halfPixelCount) / pixelCountDouble);
		g_imageQuantizerPaletteEntries[g_imageQuantizerColorCount].green =
			(uint8_t)((node->greenSum + halfPixelCount) / pixelCountDouble);
		g_imageQuantizerPaletteEntries[g_imageQuantizerColorCount].blue =
			(uint8_t)((node->blueSum + halfPixelCount) / pixelCountDouble);
		node->paletteIndex = g_imageQuantizerColorCount;
		++g_imageQuantizerColorCount;
	}
}

// FUNCTION: XVT 0x4446C0
int ImageQuantizer_DitherImageToPalette(uint32_t* image) {
	ImageQuantizerImageLayout* layout;
	ImageQuantizerPixelRun* pixels;
	uint8_t* palette;
	int* errors;
	unsigned int paletteCount;
	unsigned int row;
	unsigned int column;
	unsigned int pixelIndex;
	unsigned int rowWidth;
	int direction;
	int errorRed;
	int errorGreen;
	int errorBlue;
	int nearestIndex;
	int nearestDistance;

	if (ImageQuantizer_ExpandPixelRuns(image) == 0) {
		return 1;
	}
	layout = (ImageQuantizerImageLayout*)image;
	pixels = layout->pixels;
	memcpy(&palette, (uint8_t*)image + 0x104C, sizeof(palette));
	paletteCount = *(uint32_t*)((uint8_t*)image + 0x1054) >> 4;
	if (paletteCount == 0) {
		paletteCount = 1;
	}
	rowWidth = layout->width;
	errors = calloc((rowWidth + 2) * 6, sizeof(*errors));
	if (errors == NULL || palette == NULL) {
		free(errors);
		ImageQuantizer_FatalAllocationError("Unable to dither image", "Memory allocation failed");
		return 1;
	}
	pixelIndex = 0;
	for (row = 0; row < layout->height; ++row) {
		direction = (row & 1) == 0 ? 1 : -1;
		for (column = 0; column < rowWidth; ++column) {
			unsigned int sourceColumn = direction > 0 ? column : rowWidth - column - 1;
			ImageQuantizerPixelRun* pixel = &pixels[pixelIndex + sourceColumn];
			int red = pixel->red + errors[(column + 1) * 3 + 0] / 16;
			int green = pixel->green + errors[(column + 1) * 3 + 1] / 16;
			int blue = pixel->blue + errors[(column + 1) * 3 + 2] / 16;
			unsigned int paletteIndex;
			if (red < 0)
				red = 0;
			if (green < 0)
				green = 0;
			if (blue < 0)
				blue = 0;
			if (red > 255)
				red = 255;
			if (green > 255)
				green = 255;
			if (blue > 255)
				blue = 255;
			nearestIndex = 0;
			nearestDistance = INT_MAX;
			for (paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
				int dr = red - palette[paletteIndex * 9 + 0];
				int dg = green - palette[paletteIndex * 9 + 1];
				int db = blue - palette[paletteIndex * 9 + 2];
				int distance = dr * dr + dg * dg + db * db;
				if (distance < nearestDistance) {
					nearestDistance = distance;
					nearestIndex = (int)paletteIndex;
				}
			}
			pixel->paletteIndex = (uint16_t)nearestIndex;
			if (layout->compressionMode != 2) {
				pixel->red = palette[nearestIndex * 9 + 0];
				pixel->green = palette[nearestIndex * 9 + 1];
				pixel->blue = palette[nearestIndex * 9 + 2];
			}
			errorRed = red - palette[nearestIndex * 9 + 0];
			errorGreen = green - palette[nearestIndex * 9 + 1];
			errorBlue = blue - palette[nearestIndex * 9 + 2];
			errors[(column + 2) * 3 + 0] += 7 * errorRed;
			errors[(column + 2) * 3 + 1] += 7 * errorGreen;
			errors[(column + 2) * 3 + 2] += 7 * errorBlue;
			errors[column * 3 + 0] += 3 * errorRed;
			errors[column * 3 + 1] += 3 * errorGreen;
			errors[column * 3 + 2] += 3 * errorBlue;
			errors[(column + 1) * 3 + 0] = 5 * errorRed;
			errors[(column + 1) * 3 + 1] = 5 * errorGreen;
			errors[(column + 1) * 3 + 2] = 5 * errorBlue;
		}
		memset(errors, 0, (rowWidth + 2) * 3 * sizeof(*errors));
		pixelIndex += rowWidth;
		ImageQuantizer_ReportProgress("  Dithering image...  ", row, layout->height);
	}
	free(errors);
	return 0;
}

// FUNCTION: XVT 0x444CB0
int ImageQuantizer_InitializeColorTree(int treeDepth) {
	int difference;

	g_imageQuantizerNodePoolHead = NULL;
	g_imageQuantizerNodeCount = 0;
	g_imageQuantizerPoolNodesRemaining = 0;
	if (treeDepth > 8) {
		treeDepth = 8;
	}
	if (treeDepth < 2) {
		treeDepth = 2;
	}
	g_imageQuantizerMaxTreeDepth = treeDepth;
	g_imageQuantizerRoot = ImageQuantizer_AllocateNode(0, 0, NULL, 0x80, 0x80, 0x80);
	g_imageQuantizerSquaredDiffTable = malloc(0x7FC);
	if (g_imageQuantizerRoot == NULL || g_imageQuantizerSquaredDiffTable == NULL) {
		ImageQuantizer_FatalAllocationError(g_imageQuantizerUnableToQuantizeMessage,
											"Memory allocation failed");
		exit(1);
	}
	g_imageQuantizerRoot->parent = g_imageQuantizerRoot;
	g_imageQuantizerRoot->quantizationError = 0.0;
	g_imageQuantizerColorCount = 0;
	g_imageQuantizerSquaredDiffTable += 255;
	difference = -255;
	do {
		g_imageQuantizerSquaredDiffTable[difference] = difference * difference;
		++difference;
	} while (difference <= 255);

	return difference;
}

// FUNCTION: XVT 0x444D90
ImageQuantizerNode* ImageQuantizer_AllocateNode(int childIndex, int level, ImageQuantizerNode* parent,
												int midpointRed, int midpointGreen, int midpointBlue) {
	ImageQuantizerNodePoolBlock* poolBlock;
	ImageQuantizerNode* node;

	if (g_imageQuantizerPoolNodesRemaining == 0) {
		poolBlock = (ImageQuantizerNodePoolBlock*)malloc(sizeof(ImageQuantizerNodePoolBlock));
		if (poolBlock == NULL)
			return NULL;
		poolBlock->previous = g_imageQuantizerNodePoolHead;
		g_imageQuantizerNodePoolHead = poolBlock;
		g_imageQuantizerNextNode = poolBlock->nodes;
		g_imageQuantizerPoolNodesRemaining = 2048;
	}
	++g_imageQuantizerNodeCount;
	--g_imageQuantizerPoolNodesRemaining;
	node = g_imageQuantizerNextNode;
	++g_imageQuantizerNextNode;
	node->parent = parent;
	memset(node->children, 0, sizeof(node->children));
	node->childIndex = childIndex;
	node->level = level;
	node->childrenMask = 0;
	node->midpointRed = midpointRed;
	node->midpointGreen = midpointGreen;
	node->midpointBlue = midpointBlue;
	node->quantizationError = 0.0;
	node->pixelCount = 0;
	node->redSum = 0.0;
	node->greenSum = 0.0;
	node->blueSum = 0.0;
	return node;
}

// FUNCTION: XVT 0x444E60
void ImageQuantizer_CollapseDeepestLevelRecursive(ImageQuantizerNode* node) {
	int childIndex;

	if (node->childrenMask != 0) {
		for (childIndex = 0; childIndex < 8; childIndex++) {
			if ((node->childrenMask & (1 << childIndex)) != 0) {
				ImageQuantizer_CollapseDeepestLevelRecursive(node->children[childIndex]);
			}
		}
	}

	if (node->level == g_imageQuantizerMaxTreeDepth) {
		ImageQuantizer_MergeNodeIntoParent(node);
	}
}

// FUNCTION: XVT 0x444EB0
unsigned int ImageQuantizer_MergeNodeIntoParent(ImageQuantizerNode* node) {
	ImageQuantizerNode* parent;
	unsigned int pixelCount;

	parent = node->parent;
	parent->childrenMask &= ~(1 << node->childIndex);
	pixelCount = node->pixelCount + parent->pixelCount;
	parent->pixelCount = pixelCount;
	parent->redSum += node->redSum;
	parent->greenSum += node->greenSum;
	parent->blueSum += node->blueSum;
	--g_imageQuantizerNodeCount;
	return pixelCount;
}

// FUNCTION: XVT 0x444F00
void ImageQuantizer_ReduceColorTree(unsigned int targetColorCount) {
	unsigned int initialColorCount;

	initialColorCount = g_imageQuantizerColorCount;
	g_imageQuantizerNextPruneThreshold = 1.0;
	while (targetColorCount < g_imageQuantizerColorCount) {
		g_imageQuantizerPruneThreshold = g_imageQuantizerNextPruneThreshold;
		g_imageQuantizerNextPruneThreshold = g_imageQuantizerRoot->quantizationError - 1.0;
		g_imageQuantizerColorCount = 0;
		ImageQuantizer_ReduceColorTreePassRecursive(g_imageQuantizerRoot);
		ImageQuantizer_ReportProgress("  Reducing image colors...  ",
									  initialColorCount - g_imageQuantizerColorCount,
									  initialColorCount - targetColorCount + 1);
	}
}

// FUNCTION: XVT 0x444F90
void ImageQuantizer_ReduceColorTreePassRecursive(ImageQuantizerNode* node) {
	unsigned int childIndex;

	if (node->childrenMask != 0) {
		for (childIndex = 0; childIndex < 8; ++childIndex) {
			if ((node->childrenMask & (1u << childIndex)) != 0)
				ImageQuantizer_ReduceColorTreePassRecursive(node->children[childIndex]);
		}
	}

	if (node->quantizationError <= g_imageQuantizerPruneThreshold) {
		ImageQuantizer_MergeNodeIntoParent(node);
		return;
	}

	if (node->pixelCount != 0)
		++g_imageQuantizerColorCount;
	if (node->quantizationError < g_imageQuantizerNextPruneThreshold)
		g_imageQuantizerNextPruneThreshold = node->quantizationError;
}

// FUNCTION: XVT 0x445020
void ImageQuantizer_QuantizeImageLists(unsigned int** imageListHeads, unsigned int listCount,
									   unsigned int paletteSize, int treeDepth, int dither, int outputMode) {
	unsigned int targetColorCount = paletteSize == 0 ? 1 : paletteSize;
	int effectiveDepth = treeDepth;
	unsigned int value;
	unsigned int listIndex;
	unsigned int storedRunCount;
	int hasIndexedImage = 0;
	ImageQuantizerImageLayout* image;
	ImageQuantizerImageLayout* nextImage;
	ImageQuantizerNodePoolBlock* previousBlock;

	if (targetColorCount > 0xFFFF) {
		targetColorCount = 0xFFFF;
	}
	if (effectiveDepth == 0) {
		effectiveDepth = 1;
		value = targetColorCount;
		while (value != 0) {
			value >>= 2;
			++effectiveDepth;
		}
		if (dither != 0) {
			--effectiveDepth;
		}
		for (listIndex = 0; listIndex < listCount; ++listIndex) {
			if (imageListHeads[listIndex] != NULL && imageListHeads[listIndex][1033] == 2) {
				hasIndexedImage = 1;
			}
		}
		if (hasIndexedImage != 0) {
			effectiveDepth += 2;
		}
	}
	ImageQuantizer_InitializeColorTree(effectiveDepth);
	for (listIndex = 0; listIndex < listCount; ++listIndex) {
		image = (ImageQuantizerImageLayout*)imageListHeads[listIndex];
		while (image != NULL) {
			memcpy(&storedRunCount, (uint8_t*)image + 4238, sizeof(storedRunCount));
			if (image->width * image->height == storedRunCount) {
				ImageQuantizer_CompressPixelRuns((unsigned int*)image);
			}
			memcpy(&nextImage, (uint8_t*)image + 6338, sizeof(nextImage));
			image = nextImage;
		}
	}
	for (listIndex = 0; listIndex < listCount; ++listIndex) {
		image = (ImageQuantizerImageLayout*)imageListHeads[listIndex];
		while (image != NULL) {
			ImageQuantizer_ClassifyImageColors((unsigned int*)image);
			memcpy(&nextImage, (uint8_t*)image + 6338, sizeof(nextImage));
			image = nextImage;
		}
	}
	if ((g_imageQuantizerColorCount >> 1) < targetColorCount) {
		dither = 0;
	}
	if (targetColorCount < g_imageQuantizerColorCount) {
		ImageQuantizer_ReduceColorTree(targetColorCount);
	}
	for (listIndex = 0; listIndex < listCount; ++listIndex) {
		image = (ImageQuantizerImageLayout*)imageListHeads[listIndex];
		while (image != NULL) {
			ImageQuantizer_AssignPaletteColors((unsigned int*)image, targetColorCount, dither, outputMode);
			memcpy(&nextImage, (uint8_t*)image + 6338, sizeof(nextImage));
			image = nextImage;
		}
	}
	while (g_imageQuantizerNodePoolHead != NULL) {
		previousBlock = g_imageQuantizerNodePoolHead->previous;
		free(g_imageQuantizerNodePoolHead);
		g_imageQuantizerNodePoolHead = previousBlock;
	}
	g_imageQuantizerSquaredDiffTable -= 255;
	free(g_imageQuantizerSquaredDiffTable);
}

// FUNCTION: XVT 0x4451E0
int ImageQuantizer_BeginPaletteCollection(int targetColorCount, int treeDepth) {
	(void)targetColorCount;
	return ImageQuantizer_InitializeColorTree(treeDepth);
}

// FUNCTION: XVT 0x4451F0
void ImageQuantizer_ExportPalette6BitAndDestroy(int colorCount, int treeDepth, uint8_t* paletteRgb) {
	int remainingColors;
	unsigned int entryOffset;
	int blue;
	int green;
	ImageQuantizerPaletteEntry* entry;
	ImageQuantizerNodePoolBlock* previousBlock;
	uint8_t* output;

	(void)treeDepth;
	remainingColors = colorCount;
	ImageQuantizer_ReduceColorTree(colorCount);
	g_imageQuantizerPaletteEntries = malloc(sizeof(ImageQuantizerPaletteEntry) * g_imageQuantizerColorCount);
	if (g_imageQuantizerPaletteEntries == NULL) {
		ImageQuantizer_FatalAllocationError(g_imageQuantizerUnableToQuantizeMessage,
											"Memory allocation failed");
		exit(1);
	}
	entryOffset = 0;
	g_imageQuantizerColorCount = 0;
	ImageQuantizer_BuildPaletteEntriesRecursive(g_imageQuantizerRoot);
	if (colorCount > 0) {
		output = paletteRgb;
		do {
			entry = (ImageQuantizerPaletteEntry*)((uint8_t*)g_imageQuantizerPaletteEntries + entryOffset);
			green = entry->green;
			blue = entry->blue;
			entryOffset += sizeof(ImageQuantizerPaletteEntry);
			output[0] = entry->red >> 2;
			--remainingColors;
			output[1] = green >> 2;
			output[2] = blue >> 2;
			output += 3;
		} while (remainingColors != 0);
	}
	free(g_imageQuantizerPaletteEntries);
	do {
		previousBlock = g_imageQuantizerNodePoolHead->previous;
		free(g_imageQuantizerNodePoolHead);
		g_imageQuantizerNodePoolHead = previousBlock;
	} while (previousBlock != NULL);
	g_imageQuantizerSquaredDiffTable -= 255;
	free(g_imageQuantizerSquaredDiffTable);
}

// FUNCTION: XVT 0x4452D0
void ImageQuantizer_ClassifyIndexed16BppImage(const uint8_t* indexedPixels, const uint16_t* palette16,
											  unsigned int width, unsigned int height) {
	ImageQuantizerImageLayout* image;
	ImageQuantizerPixelRun* sample;
	unsigned int row;
	unsigned int column;
	uint16_t channel;

	image = ImageQuantizer_AllocateImage();
	if (image == NULL) {
		return;
	}
	image->comparePaletteIndex = 0;
	image->width = width;
	image->height = height;
	image->runCount = height * image->width;
	image->pixels = malloc(image->runCount * sizeof(ImageQuantizerPixelRun));
	if (image->pixels == NULL) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	sample = image->pixels;
	row = 0;
	while (row < image->height) {
		column = 0;
		while (column < image->width) {
			channel = palette16[*indexedPixels];
			channel >>= 11;
			channel <<= 3;
			sample->red = (uint8_t)channel;
			channel = palette16[*indexedPixels];
			channel >>= 5;
			channel <<= 2;
			sample->green = (uint8_t)channel;
			channel = palette16[*indexedPixels];
			channel <<= 3;
			sample->blue = (uint8_t)channel;
			++indexedPixels;
			sample->paletteIndex = 0;
			sample->length = 0;
			++sample;
			++column;
		}
		++row;
	}
	ImageQuantizer_CompressPixelRuns((unsigned int*)image);
	ImageQuantizer_ClassifyImageColors((unsigned int*)image);
	ImageQuantizer_DestroyImage(image);
}

// FUNCTION: XVT 0x4453D0
void ImageQuantizer_ClassifyEncodedTexLevelImage(const uint8_t* encodedImage, const uint8_t* paletteRgba,
												 unsigned int width, unsigned int height, int packingMode) {
	uint8_t command;
	ImageQuantizerImageLayout* image;
	uint8_t runLength;
	const uint8_t* commandPtr;
	ImageQuantizerPixelRun* sample;
	int paletteBase;
	int paletteIndex;

	image = ImageQuantizer_AllocateImage();
	if (image == NULL) {
		return;
	}
	image->comparePaletteIndex = 0;
	image->width = width;
	image->height = height;
	image->runCount = height * image->width;
	image->pixels = malloc(image->runCount * sizeof(ImageQuantizerPixelRun));
	commandPtr = encodedImage + 16;
	paletteBase = 0;
	sample = image->pixels;
	while (*commandPtr != 0xff) {
		while (*commandPtr != 0xfe) {
			command = *commandPtr;
			if (command == 0xfb) {
				paletteBase = commandPtr[1] + (commandPtr[2] << 8);
				commandPtr += 3;
			} else if (command == 0xfc) {
				runLength = commandPtr[1];
				commandPtr += 2;
				do {
					sample->red = 0x80;
					sample->green = 0x80;
					sample->blue = 0x80;
					sample->paletteIndex = 0;
					sample->length = 0;
					++sample;
				} while (runLength-- != 0);
			} else {
				if (command == 0xfd) {
					runLength = commandPtr[1];
					paletteIndex = commandPtr[2];
					commandPtr += 3;
				} else {
					runLength = command;
					paletteIndex =
						(uint8_t)(runLength >> g_flightSwRlePaletteShiftByPackingMode[packingMode]) +
						paletteBase;
					++commandPtr;
					runLength = g_flightSwRleRunLengthMaskByPackingMode[packingMode];
					runLength &= command;
				}
				paletteIndex *= 4;
				do {
					sample->red = paletteRgba[paletteIndex];
					sample->green = paletteRgba[paletteIndex + 1];
					sample->blue = paletteRgba[paletteIndex + 2];
					sample->paletteIndex = 0;
					sample->length = 0;
					++sample;
				} while (runLength-- != 0);
			}
		}
		++commandPtr;
	}
	ImageQuantizer_CompressPixelRuns((unsigned int*)image);
	ImageQuantizer_ClassifyImageColors((unsigned int*)image);
	ImageQuantizer_DestroyImage(image);
}

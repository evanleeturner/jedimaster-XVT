/* The color quantizer: it sorts an image's colors into a color tree, reduces the tree to a target
 * count, and maps each pixel to its nearest palette entry. Its data follows early ImageMagick's
 * image (magick/image.h): ImageQuantizerPixelRun is ImageMagick's RunlengthPacket (red, green,
 * blue, length, index); ImageQuantizerLegacyImageRecord follows its Image struct (two 2,048-byte
 * name buffers, then class, matte, compression, columns, rows); and colorClass holds its ClassType,
 * 1 for direct color and 2 for a palette image. Names in this file say what the game does with a
 * value, so some differ from ImageMagick's. */

#include "xvt/render/image_quantizer.h"

#include "xvt/flight/fediskio.h"
#include "xvt/render/flight_sw.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma pack(push, 1)

typedef struct ImageQuantizerPixelRun {
	uint8_t red;		/* Red, 0 to 255. */
	uint8_t green;		/* Green, 0 to 255. */
	uint8_t blue;		/* Blue, 0 to 255. */
	uint8_t lengthMinusOne; /* Pixels in the run less 1. */
	/* Palette entry the run maps to once colors are assigned. */
	uint16_t paletteIndex;
} ImageQuantizerPixelRun;

typedef struct ImageQuantizerImageLayout {
	/* The record's bytes before colorClass, not named in this view. */
	uint8_t reserved0000[0x1024];
	/* 1 for direct color, 2 for a palette image;
	 * ImageQuantizer_AssignPaletteColors sets 2 unless colorspace is 3. */
	uint32_t colorClass;
	/* Nonzero makes ImageQuantizer_CompressPixelRuns also need equal
	 * palette indexes to join pixels; every writer sets it to 0. */
	uint32_t comparePaletteIndex;
	/* 2 from ImageQuantizer_AllocateImage; ImageQuantizer_CompressPixelRuns
	 * sets 1 when the runs save too little. */
	uint32_t compressionType;
	uint32_t width;	 /* Width in pixels. */
	uint32_t height; /* Height in pixels. */
	/* Not all reserved: the palette pointer at offset 0x104C and the palette color count at 0x1054 sit in
	 * these bytes, and this file reads and writes them by raw offset. */
	uint8_t reserved1038[0x4E];	/* Record bytes 0x1038 to 0x1085. */
	ImageQuantizerPixelRun *pixels; /* The pixel runs, from malloc. */
	/* Nothing reads or writes it by name. */
	uint8_t reservedAfterPixels[4];
	/* Runs at pixels: width * height until ImageQuantizer_CompressPixelRuns
	 * joins them. */
	uint32_t runCount;
	/* Pixels left in the source run while ImageQuantizer_CompressPixelRuns
	 * walks the runs. */
	uint32_t sourceRunPixelsRemaining;
} ImageQuantizerImageLayout;

typedef struct ImageQuantizerOwnedBuffers {
	uint8_t reserved0000[0x1014]; /* Bytes this view does not name. */
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1014;
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1018;
	uint8_t reserved101C[0x28]; /* Bytes this view does not name. */
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1044;
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1048;
	/* The palette, freed by ImageQuantizer_DestroyImage when not NULL. */
	void *palette;
	uint8_t reserved1050[0x32]; /* Bytes this view does not name. */
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1082;
	/* The pixel runs, freed by ImageQuantizer_DestroyImage when not
	 * NULL. */
	void *pixels;
	uint8_t reserved108A[0x10]; /* Bytes this view does not name. */
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer109A;
	uint8_t reserved109E[0x810]; /* Bytes this view does not name. */
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer18AE;
	/* ImageQuantizer_DestroyImage frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer18B2;
} ImageQuantizerOwnedBuffers;

/* The image record ImageQuantizer_AllocateImage makes, at its 32-bit layout:
 * palette and pixels are 4-byte slots. In the 64-bit build the pointers kept
 * in the record take 8 bytes: the palette pointer copied to 0x104C also fills
 * field1050, the pixel run pointer also fills field108A, and each field of
 * ImageQuantizerImageLayout and ImageQuantizerOwnedBuffers after a pointer
 * sits 4 bytes later per pointer before it than in the 32-bit layout, so the
 * layout's runCount falls on reserved1092. The notes below on what
 * ImageQuantizer_DestroyImage frees hold for the 32-bit build. */
typedef struct ImageQuantizerLegacyImageRecord {
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field0000;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field0004;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field0008;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint8_t field000C;
	/* Left as malloc leaves it; nothing reads or writes it by name. */
	uint8_t reserved000D[0x7FF];
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field080C;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field0810;
	/* "MIFF" from ImageQuantizer_AllocateImage; nothing reads it. */
	char formatName[0x800];
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1014;
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1018;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field101C;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1020;
	/* 1, direct color, from ImageQuantizer_AllocateImage; see
	 * ImageQuantizerImageLayout. */
	uint32_t colorClass;
	/* 0 from ImageQuantizer_AllocateImage; see
	 * ImageQuantizerImageLayout. */
	uint32_t comparePaletteIndex;
	/* 2 from ImageQuantizer_AllocateImage; see
	 * ImageQuantizerImageLayout. */
	uint32_t compressionType;
	/* 0 from ImageQuantizer_AllocateImage; the caller sets the width. */
	uint32_t width;
	/* 0 from ImageQuantizer_AllocateImage; the caller sets the height. */
	uint32_t height;
	/* 8 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1038;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field103C;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1040;
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1044;
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1048;
	/* 0 from ImageQuantizer_AllocateImage;
	 * ImageQuantizer_AssignPaletteColors keeps the palette pointer at this
	 * offset, 0x104C. */
	uint32_t palette;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1050;
	/* 0 from ImageQuantizer_AllocateImage;
	 * ImageQuantizer_AssignPaletteColors stores the color count at this
	 * offset, 0x1054. */
	uint32_t paletteColorCount;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1058;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field105C;
	/* 2 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint16_t field1060;
	/* 72.0 from ImageQuantizer_AllocateImage; nothing else reads or writes
	 * it by name. */
	float field1062;
	/* 72.0 from ImageQuantizer_AllocateImage; nothing else reads or writes
	 * it by name. */
	float field1066;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field106A;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field106E;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1072;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1076;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field107A;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field107E;
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1082;
	/* 0 from ImageQuantizer_AllocateImage: the 32-bit slot of the pixel run
	 * pointer, which the other code reaches through
	 * ImageQuantizerImageLayout. */
	uint32_t pixels;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field108A;
	/* 0 from ImageQuantizer_AllocateImage; see
	 * ImageQuantizerImageLayout. */
	uint32_t runCount;
	/* Left as malloc leaves it and not named in this view; these bytes
	 * are ImageQuantizerImageLayout's sourceRunPixelsRemaining in the
	 * 32-bit build and its runCount in the 64-bit one. */
	uint8_t reserved1092[4];
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field1096;
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field109A;
	/* time(NULL) when ImageQuantizer_AllocateImage made the record; nothing
	 * reads it. */
	uint32_t timestamp109E;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint8_t field10A2;
	/* Left as malloc leaves it; nothing reads or writes it by name. */
	uint8_t reserved10A3[0x7FF];
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field18A2;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field18A6;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field18AA;
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field18AE;
	/* 0 from ImageQuantizer_AllocateImage; ImageQuantizer_DestroyImage
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field18B2;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field18B6;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field18BA;
	/* 0 from ImageQuantizer_AllocateImage; nothing else reads or writes it
	 * by name. */
	uint32_t field18BE;
	/* 0 from ImageQuantizer_AllocateImage;
	 * ImageQuantizer_QuantizeImageLists reads the next image of a list at
	 * this offset, 6338. */
	uint32_t nextImage;
} ImageQuantizerLegacyImageRecord;

#pragma pack(pop)
typedef char xvt_size_ImageQuantizerPixelRun
	[(sizeof(ImageQuantizerPixelRun) == 6) ? 1 : -1];
typedef char xvt_size_ImageQuantizerLegacyImageRecord
	[(sizeof(ImageQuantizerLegacyImageRecord) == 0x18C6) ? 1 : -1];
#if defined(_MSC_VER) && !defined(XVT_MODERN)
typedef char xvt_size_ImageQuantizerImageLayout
	[(sizeof(ImageQuantizerImageLayout) == 0x1096) ? 1 : -1];
typedef char xvt_size_ImageQuantizerOwnedBuffers
	[(sizeof(ImageQuantizerOwnedBuffers) == 0x18B6) ? 1 : -1];
#else
typedef char xvt_size_ImageQuantizerImageLayout
	[(sizeof(ImageQuantizerImageLayout) == 0x109A) ? 1 : -1];
typedef char xvt_size_ImageQuantizerOwnedBuffers
	[(sizeof(ImageQuantizerOwnedBuffers) == 0x18DE) ? 1 : -1];
#endif

typedef struct ImageQuantizerNodePoolBlock {
	/* Nodes ImageQuantizer_AllocateNode hands out in order. */
	ImageQuantizerNode nodes[2048];
	/* The block made before this one, NULL for the first; the pools are
	 * freed along these links. */
	struct ImageQuantizerNodePoolBlock *previous;
} ImageQuantizerNodePoolBlock;

/* 196608.0, 3 * 256 * 256: the start value of a nearest-color search and the
 * error each pixel adds to the tree's root. */
// GLOBAL: XVT 0x518148
const double g_imageQuantizerMaxSquaredRgbErrorPerPixel = 196608.0;

/* Context text passed to ImageQuantizer_FatalAllocationError, which ignores
 * it. */
// GLOBAL: XVT 0x521C20
static const char g_imageQuantizerUnableToQuantizeMessage[28] =
	"Unable to quantize image";

/* Root of the color tree, made by ImageQuantizer_InitializeColorTree; its
 * parent is itself. */
// GLOBAL: XVT 0x556928
static ImageQuantizerNode *g_imageQuantizerRoot = 0;
/* Level of the color tree's leaves: ImageQuantizer_InitializeColorTree sets 2
 * to 8, and ImageQuantizer_ClassifyImageColors lowers it by 1 each time it
 * collapses the deepest level. */
// GLOBAL: XVT 0x55692C
static int g_imageQuantizerMaxTreeDepth = 0;
/* Colors in the color tree: leaves made while classifying, nodes holding pixels
 * after a reduction pass, then palette entries as they are built. */
// GLOBAL: XVT 0x556930
static unsigned int g_imageQuantizerColorCount = 0;
/* Red of the color ImageQuantizer_FindNearestPaletteEntryRecursive looks for;
 * ImageQuantizer_AssignPaletteColors sets the three search channels. */
// GLOBAL: XVT 0x556934
static uint8_t g_imageQuantizerSearchRed = 0;
/* Green of the searched color; see g_imageQuantizerSearchRed. */
// GLOBAL: XVT 0x556935
static uint8_t g_imageQuantizerSearchGreen = 0;
/* Blue of the searched color; see g_imageQuantizerSearchRed. */
// GLOBAL: XVT 0x556936
static uint8_t g_imageQuantizerSearchBlue = 0;
/* Palette being built or searched, 9-byte entries; set by
 * ImageQuantizer_AssignPaletteColors and
 * ImageQuantizer_ExportPalette6BitAndDestroy. */
// GLOBAL: XVT 0x55693D
static ImageQuantizerPaletteEntry *g_imageQuantizerPaletteEntries = 0;
/* Smallest squared distance found so far by
 * ImageQuantizer_FindNearestPaletteEntryRecursive. */
// GLOBAL: XVT 0x556941
static double g_imageQuantizerNearestDistanceSq = 0.0;
/* Error at or under which a reduction pass merges a node into its parent. */
// GLOBAL: XVT 0x556949
static double g_imageQuantizerPruneThreshold = 0.0;
/* Smallest error above the threshold met in the last reduction pass, the next
 * pass's threshold; ImageQuantizer_ReduceColorTree starts it at 1.0 and each
 * pass at the root's error less 1.0. */
// GLOBAL: XVT 0x556951
static double g_imageQuantizerNextPruneThreshold = 0.0;
/* Squares of the differences -255 to 255, pointing at the square of 0 so a
 * signed difference indexes it; made by ImageQuantizer_InitializeColorTree and
 * freed by the functions that end a quantization. */
// GLOBAL: XVT 0x556959
static uint32_t *g_imageQuantizerSquaredDiffTable = 0;
/* Color tree nodes made and not merged away; ImageQuantizer_ClassifyImageColors
 * collapses the deepest level while it is over 0x41241. */
// GLOBAL: XVT 0x55695D
unsigned int g_imageQuantizerNodeCount = 0;
/* Unused nodes left in the newest pool block; 2048 when a block is made. */
// GLOBAL: XVT 0x556961
unsigned int g_imageQuantizerPoolNodesRemaining = 0;
/* Palette entry ImageQuantizer_FindNearestPaletteEntryRecursive found nearest
 * the searched color. */
// GLOBAL: XVT 0x556965
static unsigned int g_imageQuantizerNearestPaletteIndex = 0;
/* Next unused node of the newest pool block. */
// GLOBAL: XVT 0x556969
ImageQuantizerNode *g_imageQuantizerNextNode = 0;
/* Newest block of the node pool, linked to the older ones through previous;
 * NULL when there is none. */
// GLOBAL: XVT 0x55696D
ImageQuantizerNodePoolBlock *g_imageQuantizerNodePoolHead = 0;

/* Does nothing; its arguments are ignored. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x443890
void ImageQuantizer_ReportProgress(const char *stage, unsigned int completed,
				   unsigned int total)
{
	(void)stage;
	(void)completed;
	(void)total;
}

/* Ignores its arguments and calls FeDiskIo_FatalError with the out-of-memory
 * message, which ends the program. */
// FUNCTION: XVT 0x4438B0
void ImageQuantizer_FatalAllocationError(const char *context,
					 const char *message)
{
	(void)context;
	(void)message;
	FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
}

/* Allocates an image record (0x18C6 bytes) and gives it default values: format
 * name "MIFF", colorClass 1, compressionType 2, field1038 8, field1060 2, both
 * floats 72.0, timestamp109E the current time, and 0 elsewhere; the three
 * reserved arrays are left as malloc leaves them. Returns it; when malloc fails
 * it calls ImageQuantizer_FatalAllocationError, which ends the program. */
// FUNCTION: XVT 0x4438C0
void *ImageQuantizer_AllocateImage(void)
{
	ImageQuantizerLegacyImageRecord *image;

	image = malloc(sizeof(ImageQuantizerLegacyImageRecord));
	if (image == NULL) {
		ImageQuantizer_FatalAllocationError("Unable to allocate image",
						    "Memory allocation failed");
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
	image->colorClass = 1;
	image->comparePaletteIndex = 0;
	image->compressionType = 2;
	image->width = 0;
	image->height = 0;
	image->field1038 = 8;
	image->field103C = 0;
	image->field1040 = 0;
	image->field1060 = 2;
	image->field1062 = 72.0f;
	image->field1066 = 72.0f;
	image->field1044 = 0;
	image->field1048 = 0;
	image->palette = 0;
	image->field1050 = 0;
	image->paletteColorCount = 0;
	image->field1058 = 0;
	image->field105C = 0;
	image->field1076 = 0;
	image->field107A = 0;
	image->field106E = 0;
	image->field1072 = 0;
	image->field106A = 0;
	image->field107E = 0;
	image->field1082 = 0;
	image->pixels = 0;
	image->field108A = 0;
	image->runCount = 0;
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
	image->nextImage = 0;
	return image;
}

/* Joins neighboring pixels of the same color, and with comparePaletteIndex set
 * the same palette index, into runs of up to 256, in place, then shrinks the
 * pixel buffer to runCount runs with realloc, without checking its result. Sets
 * compressionType to 1 when runCount is at least width * height * 3 / 4 for
 * direct color, or width * height / 2 for a palette image. Does nothing for
 * NULL. */
// FUNCTION: XVT 0x443A50
void ImageQuantizer_CompressPixelRuns(unsigned int *image)
{
	ImageQuantizerImageLayout *imageLayout;
	ImageQuantizerPixelRun *sourceRun;
	ImageQuantizerPixelRun *destinationRun;
	ImageQuantizerPixelRun *resizedRuns;
	unsigned int pixelIndex;
	unsigned int remaining;
	unsigned int colorClass;
	unsigned int height;

	pixelIndex = 0;
	if (image == NULL) {
		return;
	}

	imageLayout = (ImageQuantizerImageLayout *)image;
	sourceRun = imageLayout->pixels;
	remaining = sourceRun->lengthMinusOne + 1;
	imageLayout->runCount = 0;
	imageLayout->sourceRunPixelsRemaining = remaining;
	destinationRun = sourceRun;
	destinationRun->lengthMinusOne = 0xFF;
	if (imageLayout->comparePaletteIndex != 0) {
		if (imageLayout->width * imageLayout->height != 0) {
			do {
				remaining =
					imageLayout->sourceRunPixelsRemaining;
				if (remaining != 0) {
					--remaining;
				} else {
					++sourceRun;
					remaining = sourceRun->lengthMinusOne;
				}
				imageLayout->sourceRunPixelsRemaining =
					remaining;
				if (destinationRun->red == sourceRun->red &&
				    destinationRun->green == sourceRun->green &&
				    sourceRun->blue == destinationRun->blue &&
				    sourceRun->paletteIndex ==
					    destinationRun->paletteIndex &&
				    destinationRun->lengthMinusOne < 0xFF) {
					++destinationRun->lengthMinusOne;
				} else {
					if (imageLayout->runCount != 0) {
						++destinationRun;
					}
					++imageLayout->runCount;
					*destinationRun = *sourceRun;
					destinationRun->lengthMinusOne = 0;
				}
				++pixelIndex;
			} while (imageLayout->width * imageLayout->height >
				 pixelIndex);
		}
	} else {
		for (pixelIndex = 0;
		     imageLayout->width * imageLayout->height > pixelIndex;
		     ++pixelIndex) {
			remaining = imageLayout->sourceRunPixelsRemaining;
			if (remaining != 0) {
				--remaining;
			} else {
				++sourceRun;
				remaining = sourceRun->lengthMinusOne;
			}
			imageLayout->sourceRunPixelsRemaining = remaining;
			if (destinationRun->red == sourceRun->red &&
			    destinationRun->green == sourceRun->green &&
			    sourceRun->blue == destinationRun->blue &&
			    destinationRun->lengthMinusOne < 0xFF) {
				++destinationRun->lengthMinusOne;
			} else {
				if (imageLayout->runCount != 0) {
					++destinationRun;
				}
				++imageLayout->runCount;
				*destinationRun = *sourceRun;
				destinationRun->lengthMinusOne = 0;
			}
		}
	}

	resizedRuns =
		realloc(imageLayout->pixels,
			imageLayout->runCount * sizeof(ImageQuantizerPixelRun));
	colorClass = imageLayout->colorClass;
	imageLayout->pixels = resizedRuns;
	height = imageLayout->height;
	if (colorClass == 1) {
		if ((height * imageLayout->width * 3) / 4 <=
		    imageLayout->runCount) {
			imageLayout->compressionType = 1;
		}
	} else if ((height * imageLayout->width) / 2 <= imageLayout->runCount) {
		imageLayout->compressionType = 1;
	}
}

/* Frees each pointer the ImageQuantizerOwnedBuffers view names that is not
 * NULL, palette and pixels included, then the record. Does nothing for NULL. */
// FUNCTION: XVT 0x443C30
void ImageQuantizer_DestroyImage(void *image)
{
	ImageQuantizerOwnedBuffers *ownedBuffers;

	if (image == NULL) {
		return;
	}

	ownedBuffers = (ImageQuantizerOwnedBuffers *)image;
	if (ownedBuffers->buffer1014 != NULL) {
		free(ownedBuffers->buffer1014);
	}
	if (ownedBuffers->buffer1018 != NULL) {
		free(ownedBuffers->buffer1018);
	}
	if (ownedBuffers->buffer1044 != NULL) {
		free(ownedBuffers->buffer1044);
	}
	if (ownedBuffers->buffer1048 != NULL) {
		free(ownedBuffers->buffer1048);
	}
	if (ownedBuffers->palette != NULL) {
		free(ownedBuffers->palette);
	}
	if (ownedBuffers->buffer1082 != NULL) {
		free(ownedBuffers->buffer1082);
	}
	if (ownedBuffers->pixels != NULL) {
		free(ownedBuffers->pixels);
	}
	if (ownedBuffers->buffer109A != NULL) {
		free(ownedBuffers->buffer109A);
	}
	if (ownedBuffers->buffer18AE != NULL) {
		free(ownedBuffers->buffer18AE);
	}
	if (ownedBuffers->buffer18B2 != NULL) {
		free(ownedBuffers->buffer18B2);
	}
	free(ownedBuffers);
}

/* Expands the runs to one per pixel in place, working back from the end, after
 * growing the buffer to width * height runs with realloc; sets runCount to
 * width * height. Returns 1, also when runCount already is width * height; 0
 * when realloc fails. */
// FUNCTION: XVT 0x443D10
int ImageQuantizer_ExpandPixelRuns(uint32_t *image)
{
	ImageQuantizerImageLayout *imageLayout;
	ImageQuantizerPixelRun *resizedPixels;
	ImageQuantizerPixelRun *destination;
	ImageQuantizerPixelRun *source;
	unsigned int sourceIndex;
	unsigned int pixelCount;
	unsigned int expandedPixelCount;
	unsigned int runCount;
	int copies;

	imageLayout = (ImageQuantizerImageLayout *)image;
	pixelCount = imageLayout->width * imageLayout->height;
	if (imageLayout->runCount == pixelCount) {
		return 1;
	}
	resizedPixels = (ImageQuantizerPixelRun *)realloc(
		imageLayout->pixels,
		pixelCount * sizeof(ImageQuantizerPixelRun));
	if (resizedPixels == NULL) {
		return 0;
	}
	runCount = imageLayout->runCount;
	imageLayout->pixels = resizedPixels;
	source = resizedPixels + runCount - 1;
	expandedPixelCount = imageLayout->width * imageLayout->height;
	destination = resizedPixels + expandedPixelCount - 1;
	sourceIndex = 0;
	if (runCount != 0) {
		do {
			copies = source->lengthMinusOne;
			if (copies >= 0) {
				++copies;
				do {
					*destination = *source;
					destination->lengthMinusOne = 0;
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

/* Reduces one image to at most paletteSize colors (paletteSize kept to 1 to
 * 0xFFFF): joins its runs when the run count at byte 4238 is width * height,
 * builds a color tree of treeDepth levels (for 0: 1 plus the right shifts by 2
 * that bring paletteSize to 0, 1 less with dither, 2 more for a palette image),
 * classifies the colors, reduces the tree when it holds more than paletteSize,
 * and assigns the palette, without dither when half the tree's color count is
 * under paletteSize. Then frees the node pool and the squared-difference table.
 * Returns at once for paletteSize 2 with colorspace 2 and dither. Nothing calls
 * this. */
// FUNCTION: XVT 0x443DE0
void ImageQuantizer_QuantizeImage(unsigned int *image, unsigned int paletteSize,
				  int treeDepth, int dither, int colorspace)
{
	unsigned int targetColorCount = paletteSize;
	int effectiveDepth = treeDepth;
	int effectiveDither = dither;
	uint32_t *imageWords = image;
	unsigned int value;
	unsigned int storedRunCount;
	ImageQuantizerNodePoolBlock *previousBlock;

	if (paletteSize == 2 && colorspace == 2 && dither != 0) {
		return;
	}
	if (targetColorCount == 0) {
		targetColorCount = 1;
	}
	if (targetColorCount > 0xFFFF) {
		targetColorCount = 0xFFFF;
	}
	memcpy(&storedRunCount, (uint8_t *)image + 4238,
	       sizeof(storedRunCount));
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
	ImageQuantizer_AssignPaletteColors(image, targetColorCount,
					   effectiveDither, colorspace);
	while (g_imageQuantizerNodePoolHead != NULL) {
		previousBlock = g_imageQuantizerNodePoolHead->previous;
		free(g_imageQuantizerNodePoolHead);
		g_imageQuantizerNodePoolHead = previousBlock;
	}
	g_imageQuantizerSquaredDiffTable -= 255;
	free(g_imageQuantizerSquaredDiffTable);
}

/* Builds the image's palette from the color tree and maps its runs to it. Frees
 * the old palette (the pointer at byte 0x104C), allocates
 * g_imageQuantizerColorCount 9-byte entries there and fills them with
 * ImageQuantizer_BuildPaletteEntriesRecursive, which counts
 * g_imageQuantizerColorCount again. For paletteSize 2 with colorspace 2 the two
 * entries become white and black, white for the one with the larger 77 R + 150
 * G + 29 B. Unless colorspace is 3 it sets comparePaletteIndex 0 and colorClass
 * 2. Stores the color count at byte 0x1054. With dither it returns 1 when
 * ImageQuantizer_DitherImageToPalette succeeds (returns 0); when that fails, or
 * without dither, each run goes down the tree by its color bits as far as
 * children exist, and the entry nearest its color under that node's parent
 * (ImageQuantizer_FindNearestPaletteEntryRecursive) becomes its paletteIndex,
 * or for direct color its color; it returns 0. When the palette cannot be
 * allocated it calls ImageQuantizer_FatalAllocationError, which ends the
 * program. */
// FUNCTION: XVT 0x443EF0
unsigned int ImageQuantizer_AssignPaletteColors(uint32_t *image,
						unsigned int paletteSize,
						int dither, int colorspace)
{
	ImageQuantizerImageLayout *layout;
	ImageQuantizerPixelRun *pixel;
	ImageQuantizerPaletteEntry *palette;
	uint8_t *paletteBytesPtr;
	unsigned int completed;
	unsigned int paletteBytes;
	unsigned int colorCount;
	unsigned int runCount;
	unsigned int level;
	unsigned int childIndex;
	int firstLuma;
	int secondLuma;
	unsigned int offset;
	int bitPosition;
	int imageDithered;
	ImageQuantizerNode *node;
	uint8_t *imageBytes;

	layout = (ImageQuantizerImageLayout *)image;
	imageBytes = (uint8_t *)image;
	memcpy(&palette, imageBytes + 0x104C, sizeof(palette));
	free(palette);
	colorCount = g_imageQuantizerColorCount;
	paletteBytes = colorCount * sizeof(ImageQuantizerPaletteEntry);
	palette = malloc(paletteBytes);
	if (palette == NULL) {
		ImageQuantizer_FatalAllocationError("Unable to assign palette",
						    "Memory allocation failed");
		return 1;
	}
	memcpy(imageBytes + 0x104C, &palette, sizeof(palette));
	paletteBytesPtr = (uint8_t *)palette;
	g_imageQuantizerPaletteEntries = palette;
	g_imageQuantizerColorCount = 0;
	ImageQuantizer_BuildPaletteEntriesRecursive(g_imageQuantizerRoot);
	if (paletteSize == 2 && colorspace == 2 && colorCount >= 2) {
		firstLuma = 77 * palette[0].red + 150 * palette[0].green +
			    29 * palette[0].blue;
		secondLuma = 77 * palette[1].red + 150 * palette[1].green +
			     29 * palette[1].blue;
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
	if (colorspace != 3) {
		layout->comparePaletteIndex = 0;
		layout->colorClass = 2;
	}
	memcpy(imageBytes + 0x1054, &g_imageQuantizerColorCount,
	       sizeof(g_imageQuantizerColorCount));
	imageDithered =
		dither ? ImageQuantizer_DitherImageToPalette(image) == 0 : 0;
	if (imageDithered != 0) {
		return (unsigned int)imageDithered;
	}
	pixel = layout->pixels;
	runCount = layout->runCount;
	for (completed = 0; completed < runCount; ++completed, ++pixel) {
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
		g_imageQuantizerNearestDistanceSq =
			g_imageQuantizerMaxSquaredRgbErrorPerPixel;
		ImageQuantizer_FindNearestPaletteEntryRecursive(node->parent);
		if (layout->colorClass == 2) {
			pixel->paletteIndex =
				(uint16_t)g_imageQuantizerNearestPaletteIndex;
		} else {
			offset = 9 * g_imageQuantizerNearestPaletteIndex;
			pixel->red = paletteBytesPtr[offset];
			pixel->green = paletteBytesPtr[offset + 1];
			pixel->blue = paletteBytesPtr[offset + 2];
		}
		if (runCount - completed == 1 ||
		    completed % layout->height == 0) {
			ImageQuantizer_ReportProgress(
				"  Assigning image colors...  ", completed,
				runCount);
		}
	}
	return 0;
}

/* Adds an image's runs to the color tree. Adds width * height * 196608 to the
 * root's error. For each run it first, when more than 0x41241 nodes exist,
 * collapses the deepest level with ImageQuantizer_CollapseDeepestLevelRecursive
 * and lowers g_imageQuantizerMaxTreeDepth. It walks down
 * g_imageQuantizerMaxTreeDepth levels by the color's bits from the top (red 4,
 * green 2, blue 1), making missing children with the parent's midpoints moved
 * up or down, by the child's bits, by (1 << (8 - level)) >> 1, and counting
 * each new leaf in g_imageQuantizerColorCount, and adds to each node it passes
 * the run's squared distance to its midpoint times the run length. The last
 * node gets the run's pixel count and channel sums. Exits the program when a
 * node cannot be made. */
// FUNCTION: XVT 0x4441F0
void ImageQuantizer_ClassifyImageColors(unsigned int *image)
{
	ImageQuantizerPixelRun *pixel;
	ImageQuantizerNode *node;
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

	g_imageQuantizerRoot->quantizationError +=
		(double)((ImageQuantizerImageLayout *)image)->width *
		(double)((ImageQuantizerImageLayout *)image)->height *
		g_imageQuantizerMaxSquaredRgbErrorPerPixel;
	pixel = ((ImageQuantizerImageLayout *)image)->pixels;
	for (completed = 0;
	     completed < ((ImageQuantizerImageLayout *)image)->runCount;
	     ++completed) {
		if (g_imageQuantizerNodeCount > 0x41241) {
			ImageQuantizer_CollapseDeepestLevelRecursive(
				g_imageQuantizerRoot);
			--g_imageQuantizerMaxTreeDepth;
		}

		node = g_imageQuantizerRoot;
		bitPosition = 7;
		level = 1;
		runLength = (unsigned int)pixel->lengthMinusOne + 1;
		if ((unsigned int)g_imageQuantizerMaxTreeDepth >= 1) {
			pixelWeight = (double)runLength;
			do {
				childIndex = ((pixel->red >> bitPosition) & 1u)
					     << 2;
				childIndex |=
					((pixel->green >> bitPosition) & 1u)
					<< 1;
				childIndex |= (pixel->blue >> bitPosition) & 1u;
				if (node->children[childIndex] == NULL) {
					node->childrenMask |=
						(uint8_t)(1u << childIndex);
					midpointOffset =
						(1u << (8 - level)) >> 1;
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
					node->children[childIndex] =
						ImageQuantizer_AllocateNode(
							childIndex, level, node,
							node->midpointRed +
								redOffset,
							node->midpointGreen +
								greenOffset,
							node->midpointBlue +
								blueOffset);
					if (node->children[childIndex] ==
					    NULL) {
						ImageQuantizer_FatalAllocationError(
							g_imageQuantizerUnableToQuantizeMessage,
							"Memory allocation failed");
						exit(1);
					}
					if (level ==
					    (unsigned int)
						    g_imageQuantizerMaxTreeDepth) {
						++g_imageQuantizerColorCount;
					}
				}
				node = node->children[childIndex];
				pixelError =
					(double)g_imageQuantizerSquaredDiffTable
						[(int)pixel->red -
						 node->midpointRed];
				pixelError +=
					(double)g_imageQuantizerSquaredDiffTable
						[(int)pixel->green -
						 node->midpointGreen];
				pixelError +=
					(double)g_imageQuantizerSquaredDiffTable
						[(int)pixel->blue -
						 node->midpointBlue];
				node->quantizationError +=
					pixelError * pixelWeight;
				--bitPosition;
				++level;
			} while (level <=
				 (unsigned int)g_imageQuantizerMaxTreeDepth);
		}

		node->pixelCount += runLength;
		node->redSum += (double)(runLength * pixel->red);
		node->greenSum += (double)(runLength * pixel->green);
		node->blueSum += (double)(runLength * pixel->blue);
		++pixel;
		if (((ImageQuantizerImageLayout *)image)->runCount -
				    completed ==
			    1 ||
		    completed % ((ImageQuantizerImageLayout *)image)->height ==
			    0) {
			ImageQuantizer_ReportProgress(
				"  Classifying image colors...  ", completed,
				((ImageQuantizerImageLayout *)image)->runCount);
		}
	}
}

/* Searches node's subtree, children first, for the palette entry nearest the
 * searched color by squared distance, looking at the entry of each node that
 * holds pixels; updates g_imageQuantizerNearestDistanceSq and
 * g_imageQuantizerNearestPaletteIndex on a closer one. Only
 * ImageQuantizer_AssignPaletteColors calls it. */
// FUNCTION: XVT 0x4444F0
void ImageQuantizer_FindNearestPaletteEntryRecursive(ImageQuantizerNode *node)
{
	unsigned int childIndex;

	if (node->childrenMask != 0) {
		childIndex = 0;
		do {
			if ((node->childrenMask & (1u << childIndex)) != 0) {
				ImageQuantizer_FindNearestPaletteEntryRecursive(
					node->children[childIndex]);
			}
			++childIndex;
		} while (childIndex < 8);
	}

	if (node->pixelCount != 0) {
		ImageQuantizerPaletteEntry *entry;
		double distanceSq;

		entry = &g_imageQuantizerPaletteEntries[node->paletteIndex];
		distanceSq = (double)g_imageQuantizerSquaredDiffTable
			[(int)entry->green - g_imageQuantizerSearchGreen];
		distanceSq += (double)g_imageQuantizerSquaredDiffTable
			[(int)entry->red - g_imageQuantizerSearchRed];
		distanceSq += (double)g_imageQuantizerSquaredDiffTable
			[(int)entry->blue - g_imageQuantizerSearchBlue];
		if (distanceSq < g_imageQuantizerNearestDistanceSq) {
			g_imageQuantizerNearestDistanceSq = distanceSq;
			g_imageQuantizerNearestPaletteIndex =
				(uint16_t)node->paletteIndex;
		}
	}
}

/* Walks node's subtree, children first, and for each node holding pixels
 * writes palette entry g_imageQuantizerColorCount, each channel
 * (sum + (pixelCount >> 1)) / pixelCount, stores that index in the node's
 * paletteIndex and adds 1 to g_imageQuantizerColorCount. */
// FUNCTION: XVT 0x4445E0
void ImageQuantizer_BuildPaletteEntriesRecursive(ImageQuantizerNode *node)
{
	unsigned int childIndex;
	unsigned int pixelCount;
	double halfPixelCount;
	double pixelCountDouble;

	if (node->childrenMask != 0) {
		childIndex = 0;
		do {
			if ((node->childrenMask & (1u << childIndex)) != 0) {
				ImageQuantizer_BuildPaletteEntriesRecursive(
					node->children[childIndex]);
			}
			++childIndex;
		} while (childIndex < 8);
	}

	pixelCount = node->pixelCount;
	if (pixelCount != 0) {
		halfPixelCount = (double)(pixelCount >> 1);
		pixelCountDouble = (double)pixelCount;
		g_imageQuantizerPaletteEntries[g_imageQuantizerColorCount].red =
			(uint8_t)((node->redSum + halfPixelCount) /
				  pixelCountDouble);
		g_imageQuantizerPaletteEntries[g_imageQuantizerColorCount]
			.green = (uint8_t)((node->greenSum + halfPixelCount) /
					   pixelCountDouble);
		g_imageQuantizerPaletteEntries[g_imageQuantizerColorCount]
			.blue = (uint8_t)((node->blueSum + halfPixelCount) /
					  pixelCountDouble);
		node->paletteIndex = g_imageQuantizerColorCount;
		++g_imageQuantizerColorCount;
	}
}

/* Dithers the image to its palette. Expands the runs to one per pixel and walks
 * the rows, odd rows right to left, adding to each pixel the error carried to
 * it divided by 16, clamping to 0 to 255, and taking the nearest by squared
 * distance of the first (color count at byte 0x1054) >> 4 entries, at least 1;
 * it stores the index in paletteIndex and, for direct color, the entry's color.
 * Of each pixel's error it adds 7 times to the next pixel's carry; the 3 and 5
 * times shares land on entries already passed, and the carry is cleared after
 * each row. Returns 0, or 1 when the expansion fails; a failed allocation calls
 * ImageQuantizer_FatalAllocationError, which ends the program. */
// FUNCTION: XVT 0x4446C0
int ImageQuantizer_DitherImageToPalette(uint32_t *image)
{
	ImageQuantizerImageLayout *layout;
	ImageQuantizerPixelRun *pixels;
	uint8_t *palette;
	int *errors;
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
	layout = (ImageQuantizerImageLayout *)image;
	pixels = layout->pixels;
	memcpy(&palette, (uint8_t *)image + 0x104C, sizeof(palette));
	paletteCount = *(uint32_t *)((uint8_t *)image + 0x1054) >> 4;
	if (paletteCount == 0) {
		paletteCount = 1;
	}
	rowWidth = layout->width;
	errors = calloc((rowWidth + 2) * 6, sizeof(*errors));
	if (errors == NULL || palette == NULL) {
		free(errors);
		ImageQuantizer_FatalAllocationError("Unable to dither image",
						    "Memory allocation failed");
		return 1;
	}
	pixelIndex = 0;
	for (row = 0; row < layout->height; ++row) {
		direction = (row & 1) == 0 ? 1 : -1;
		for (column = 0; column < rowWidth; ++column) {
			unsigned int sourceColumn =
				direction > 0 ? column : rowWidth - column - 1;
			ImageQuantizerPixelRun *pixel =
				&pixels[pixelIndex + sourceColumn];
			int red =
				pixel->red + errors[(column + 1) * 3 + 0] / 16;
			int green = pixel->green +
				    errors[(column + 1) * 3 + 1] / 16;
			int blue =
				pixel->blue + errors[(column + 1) * 3 + 2] / 16;
			unsigned int paletteIndex;
			if (red < 0) {
				red = 0;
			}
			if (green < 0) {
				green = 0;
			}
			if (blue < 0) {
				blue = 0;
			}
			if (red > 255) {
				red = 255;
			}
			if (green > 255) {
				green = 255;
			}
			if (blue > 255) {
				blue = 255;
			}
			nearestIndex = 0;
			nearestDistance = INT_MAX;
			for (paletteIndex = 0; paletteIndex < paletteCount;
			     ++paletteIndex) {
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
			if (layout->colorClass != 2) {
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
		ImageQuantizer_ReportProgress("  Dithering image...  ", row,
					      layout->height);
	}
	free(errors);
	return 0;
}

/* Starts a new color tree: clears the node pool bookkeeping without freeing old
 * blocks, sets g_imageQuantizerMaxTreeDepth to treeDepth kept to 2 to 8, makes
 * the root (midpoints 0x80, its own parent, error 0), sets
 * g_imageQuantizerColorCount to 0 and builds g_imageQuantizerSquaredDiffTable.
 * Returns 256, where its loop stops. Exits the program when an allocation
 * fails. */
// FUNCTION: XVT 0x444CB0
int ImageQuantizer_InitializeColorTree(int treeDepth)
{
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
	g_imageQuantizerRoot =
		ImageQuantizer_AllocateNode(0, 0, NULL, 0x80, 0x80, 0x80);
	g_imageQuantizerSquaredDiffTable = malloc(0x7FC);
	if (g_imageQuantizerRoot == NULL ||
	    g_imageQuantizerSquaredDiffTable == NULL) {
		ImageQuantizer_FatalAllocationError(
			g_imageQuantizerUnableToQuantizeMessage,
			"Memory allocation failed");
		exit(1);
	}
	g_imageQuantizerRoot->parent = g_imageQuantizerRoot;
	g_imageQuantizerRoot->quantizationError = 0.0;
	g_imageQuantizerColorCount = 0;
	g_imageQuantizerSquaredDiffTable += 255;
	difference = -255;
	do {
		g_imageQuantizerSquaredDiffTable[difference] =
			difference * difference;
		++difference;
	} while (difference <= 255);

	return difference;
}

/* Takes the next node from the pool, making a new block of 2048 with malloc
 * when the newest is used up, and fills it: the given parent, childIndex, level
 * and midpoints, no children, and 0 error, count and sums. Adds 1 to
 * g_imageQuantizerNodeCount. Returns it, or NULL when a block cannot be
 * allocated. */
// FUNCTION: XVT 0x444D90
ImageQuantizerNode *ImageQuantizer_AllocateNode(int childIndex, int level,
						ImageQuantizerNode *parent,
						int midpointRed,
						int midpointGreen,
						int midpointBlue)
{
	ImageQuantizerNodePoolBlock *poolBlock;
	ImageQuantizerNode *node;

	if (g_imageQuantizerPoolNodesRemaining == 0) {
		poolBlock = (ImageQuantizerNodePoolBlock *)malloc(
			sizeof(ImageQuantizerNodePoolBlock));
		if (poolBlock == NULL) {
			return NULL;
		}
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

/* Merges every node at level g_imageQuantizerMaxTreeDepth in node's subtree
 * into its parent with ImageQuantizer_MergeNodeIntoParent, children first. */
// FUNCTION: XVT 0x444E60
void ImageQuantizer_CollapseDeepestLevelRecursive(ImageQuantizerNode *node)
{
	int childIndex;

	if (node->childrenMask != 0) {
		for (childIndex = 0; childIndex < 8; childIndex++) {
			if ((node->childrenMask & (1 << childIndex)) != 0) {
				ImageQuantizer_CollapseDeepestLevelRecursive(
					node->children[childIndex]);
			}
		}
	}

	if (node->level == g_imageQuantizerMaxTreeDepth) {
		ImageQuantizer_MergeNodeIntoParent(node);
	}
}

/* Takes node out of its parent's childrenMask and adds its pixel count and
 * channel sums to the parent's; lowers g_imageQuantizerNodeCount. Returns the
 * parent's new pixel count. The node's error is not passed on, and its memory
 * stays in the pool. */
// FUNCTION: XVT 0x444EB0
unsigned int ImageQuantizer_MergeNodeIntoParent(ImageQuantizerNode *node)
{
	ImageQuantizerNode *parent;
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

/* Merges color tree nodes until at most targetColorCount hold pixels: each pass
 * merges every node whose error is at most the threshold, the smallest error
 * left by the last pass (1.0 at first), and counts g_imageQuantizerColorCount
 * again. */
// FUNCTION: XVT 0x444F00
void ImageQuantizer_ReduceColorTree(unsigned int targetColorCount)
{
	unsigned int initialColorCount;

	initialColorCount = g_imageQuantizerColorCount;
	g_imageQuantizerNextPruneThreshold = 1.0;
	while (targetColorCount < g_imageQuantizerColorCount) {
		g_imageQuantizerPruneThreshold =
			g_imageQuantizerNextPruneThreshold;
		g_imageQuantizerNextPruneThreshold =
			g_imageQuantizerRoot->quantizationError - 1.0;
		g_imageQuantizerColorCount = 0;
		ImageQuantizer_ReduceColorTreePassRecursive(
			g_imageQuantizerRoot);
		ImageQuantizer_ReportProgress(
			"  Reducing image colors...  ",
			initialColorCount - g_imageQuantizerColorCount,
			initialColorCount - targetColorCount + 1);
	}
}

/* One reduction pass over node's subtree, children first: merges a node whose
 * error is at most g_imageQuantizerPruneThreshold into its parent; any other
 * node is counted in g_imageQuantizerColorCount when it holds pixels and lowers
 * g_imageQuantizerNextPruneThreshold to its error when that is smaller. */
// FUNCTION: XVT 0x444F90
void ImageQuantizer_ReduceColorTreePassRecursive(ImageQuantizerNode *node)
{
	unsigned int childIndex;

	if (node->childrenMask != 0) {
		for (childIndex = 0; childIndex < 8; ++childIndex) {
			if ((node->childrenMask & (1u << childIndex)) != 0) {
				ImageQuantizer_ReduceColorTreePassRecursive(
					node->children[childIndex]);
			}
		}
	}

	if (node->quantizationError <= g_imageQuantizerPruneThreshold) {
		ImageQuantizer_MergeNodeIntoParent(node);
		return;
	}

	if (node->pixelCount != 0) {
		++g_imageQuantizerColorCount;
	}
	if (node->quantizationError < g_imageQuantizerNextPruneThreshold) {
		g_imageQuantizerNextPruneThreshold = node->quantizationError;
	}
}

/* Quantizes every image of listCount linked lists (each image's next at byte
 * 6338) with one color tree shared by all, the steps and defaults as in
 * ImageQuantizer_QuantizeImage, 2 more tree levels when any list's first image
 * is a palette image. Nothing calls this. */
// FUNCTION: XVT 0x445020
void ImageQuantizer_QuantizeImageLists(unsigned int **imageListHeads,
				       unsigned int listCount,
				       unsigned int paletteSize, int treeDepth,
				       int dither, int colorspace)
{
	unsigned int targetColorCount = paletteSize == 0 ? 1 : paletteSize;
	int effectiveDepth = treeDepth;
	unsigned int value;
	unsigned int listIndex;
	unsigned int storedRunCount;
	int hasIndexedImage = 0;
	ImageQuantizerImageLayout *image;
	ImageQuantizerImageLayout *nextImage;
	ImageQuantizerNodePoolBlock *previousBlock;

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
			if (imageListHeads[listIndex] != NULL &&
			    imageListHeads[listIndex][1033] == 2) {
				hasIndexedImage = 1;
			}
		}
		if (hasIndexedImage != 0) {
			effectiveDepth += 2;
		}
	}
	ImageQuantizer_InitializeColorTree(effectiveDepth);
	for (listIndex = 0; listIndex < listCount; ++listIndex) {
		image = (ImageQuantizerImageLayout *)imageListHeads[listIndex];
		while (image != NULL) {
			memcpy(&storedRunCount, (uint8_t *)image + 4238,
			       sizeof(storedRunCount));
			if (image->width * image->height == storedRunCount) {
				ImageQuantizer_CompressPixelRuns(
					(unsigned int *)image);
			}
			memcpy(&nextImage, (uint8_t *)image + 6338,
			       sizeof(nextImage));
			image = nextImage;
		}
	}
	for (listIndex = 0; listIndex < listCount; ++listIndex) {
		image = (ImageQuantizerImageLayout *)imageListHeads[listIndex];
		while (image != NULL) {
			ImageQuantizer_ClassifyImageColors(
				(unsigned int *)image);
			memcpy(&nextImage, (uint8_t *)image + 6338,
			       sizeof(nextImage));
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
		image = (ImageQuantizerImageLayout *)imageListHeads[listIndex];
		while (image != NULL) {
			ImageQuantizer_AssignPaletteColors(
				(unsigned int *)image, targetColorCount, dither,
				colorspace);
			memcpy(&nextImage, (uint8_t *)image + 6338,
			       sizeof(nextImage));
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

/* Starts the color tree for a mission palette with
 * ImageQuantizer_InitializeColorTree(treeDepth) and returns its result;
 * targetColorCount is ignored. FeDiskIo_InitResources calls it only while
 * g_generateMissionPalette is set, which FeDiskIo_InitResources clears before
 * loading because g_paletteGenerationEnabled is never set, so it never runs. */
// FUNCTION: XVT 0x4451E0
int ImageQuantizer_BeginPaletteCollection(int targetColorCount, int treeDepth)
{
	(void)targetColorCount;
	return ImageQuantizer_InitializeColorTree(treeDepth);
}

/* Ends a mission palette: reduces the tree to colorCount colors, builds the
 * palette entries and writes colorCount of them to paletteRgb as 6-bit triplets
 * (each channel >> 2), even when fewer were built, then frees the entries, the
 * node pool and the squared-difference table. treeDepth is ignored. Does not
 * check that a pool block exists; exits the program when the entries cannot be
 * allocated. FeDiskIo_InitResources calls it only while
 * g_generateMissionPalette is set, which FeDiskIo_InitResources clears before
 * loading because g_paletteGenerationEnabled is never set, so it never runs. */
// FUNCTION: XVT 0x4451F0
void ImageQuantizer_ExportPalette6BitAndDestroy(int colorCount, int treeDepth,
						uint8_t *paletteRgb)
{
	int remainingColors;
	unsigned int entryOffset;
	int blue;
	int green;
	ImageQuantizerPaletteEntry *entry;
	ImageQuantizerNodePoolBlock *previousBlock;
	uint8_t *output;

	(void)treeDepth;
	remainingColors = colorCount;
	ImageQuantizer_ReduceColorTree(colorCount);
	g_imageQuantizerPaletteEntries =
		malloc(sizeof(ImageQuantizerPaletteEntry) *
		       g_imageQuantizerColorCount);
	if (g_imageQuantizerPaletteEntries == NULL) {
		ImageQuantizer_FatalAllocationError(
			g_imageQuantizerUnableToQuantizeMessage,
			"Memory allocation failed");
		exit(1);
	}
	entryOffset = 0;
	g_imageQuantizerColorCount = 0;
	ImageQuantizer_BuildPaletteEntriesRecursive(g_imageQuantizerRoot);
	if (colorCount > 0) {
		output = paletteRgb;
		do {
			entry = (ImageQuantizerPaletteEntry
					 *)((uint8_t *)
						    g_imageQuantizerPaletteEntries +
					    entryOffset);
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

/* Adds an 8-bit image with a 5-6-5 palette to the color tree: makes a temporary
 * record of width * height one-pixel runs, each color widened to 8 bits a
 * channel by shifting (red and blue << 3, green << 2), then joins, classifies
 * and destroys it. Calls FeDiskIo_FatalError when the pixels cannot be
 * allocated. OptModel_BuildRuntimeNode calls it only while
 * g_generateMissionPalette is set, which FeDiskIo_InitResources clears before
 * loading because g_paletteGenerationEnabled is never set, so it never runs. */
// FUNCTION: XVT 0x4452D0
void ImageQuantizer_ClassifyIndexedRgb565Image(const uint8_t *indexedPixels,
					       const uint16_t *palette16,
					       unsigned int width,
					       unsigned int height)
{
	ImageQuantizerImageLayout *image;
	ImageQuantizerPixelRun *sample;
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
	image->pixels =
		malloc(image->runCount * sizeof(ImageQuantizerPixelRun));
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
			sample->lengthMinusOne = 0;
			++sample;
			++column;
		}
		++row;
	}
	ImageQuantizer_CompressPixelRuns((unsigned int *)image);
	ImageQuantizer_ClassifyImageColors((unsigned int *)image);
	ImageQuantizer_DestroyImage(image);
}

/* Adds a run-length texture image to the color tree: decodes it, from 16 bytes
 * past encodedImage, into a temporary record of one-pixel runs until a 0xFF at
 * a row's start. 0xFB sets the palette base from the next two bytes, low byte
 * first; 0xFC gives input[1] + 1 gray (0x80) pixels; 0xFD gives input[1] + 1
 * pixels of entry input[2]; any other byte gives (byte & mask) + 1 pixels of
 * entry base + (byte >> shift), mask and shift from
 * g_flightSwRleRunLengthMaskByPackingMode and
 * g_flightSwRlePaletteShiftByPackingMode; colors come from paletteRgba, 4 bytes
 * per entry. Then joins, classifies and destroys the record. Does not check the
 * pixel allocation or that the decoded pixels fit width * height.
 * TexLevel_Convert24BppPalettesTo8Bpp calls it only while
 * g_generateMissionPalette is set, which FeDiskIo_InitResources clears before
 * loading because g_paletteGenerationEnabled is never set, so it never runs. */
// FUNCTION: XVT 0x4453D0
void ImageQuantizer_ClassifyEncodedTexLevelImage(const uint8_t *encodedImage,
						 const uint8_t *paletteRgba,
						 unsigned int width,
						 unsigned int height,
						 int packingMode)
{
	uint8_t command;
	ImageQuantizerImageLayout *image;
	uint8_t runLengthMinusOne;
	const uint8_t *commandPtr;
	ImageQuantizerPixelRun *sample;
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
	image->pixels =
		malloc(image->runCount * sizeof(ImageQuantizerPixelRun));
	commandPtr = encodedImage + 16;
	paletteBase = 0;
	sample = image->pixels;
	while (*commandPtr != 0xff) {
		while (*commandPtr != 0xfe) {
			command = *commandPtr;
			if (command == 0xfb) {
				paletteBase =
					commandPtr[1] + (commandPtr[2] << 8);
				commandPtr += 3;
			} else if (command == 0xfc) {
				runLengthMinusOne = commandPtr[1];
				commandPtr += 2;
				do {
					sample->red = 0x80;
					sample->green = 0x80;
					sample->blue = 0x80;
					sample->paletteIndex = 0;
					sample->lengthMinusOne = 0;
					++sample;
				} while (runLengthMinusOne-- != 0);
			} else {
				if (command == 0xfd) {
					runLengthMinusOne = commandPtr[1];
					paletteIndex = commandPtr[2];
					commandPtr += 3;
				} else {
					runLengthMinusOne = command;
					paletteIndex =
						(uint8_t)(runLengthMinusOne >>
							  g_flightSwRlePaletteShiftByPackingMode
								  [packingMode]) +
						paletteBase;
					++commandPtr;
					runLengthMinusOne =
						g_flightSwRleRunLengthMaskByPackingMode
							[packingMode];
					runLengthMinusOne &= command;
				}
				/* From here paletteIndex is a byte offset into paletteRgba, four bytes per entry. */
				paletteIndex *= 4;
				do {
					sample->red = paletteRgba[paletteIndex];
					sample->green =
						paletteRgba[paletteIndex + 1];
					sample->blue =
						paletteRgba[paletteIndex + 2];
					sample->paletteIndex = 0;
					sample->lengthMinusOne = 0;
					++sample;
				} while (runLengthMinusOne-- != 0);
			}
		}
		++commandPtr;
	}
	ImageQuantizer_CompressPixelRuns((unsigned int *)image);
	ImageQuantizer_ClassifyImageColors((unsigned int *)image);
	ImageQuantizer_DestroyImage(image);
}

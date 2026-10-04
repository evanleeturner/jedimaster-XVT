/* The color quantizer: it sorts an image's colors into a color tree, reduces the tree to a target
 * count, and maps each pixel to its nearest palette entry. Its data follows early ImageMagick's
 * image (magick/image.h): image_quantizer_pixel_run is ImageMagick's RunlengthPacket (red, green,
 * blue, length, index); image_quantizer_legacy_image_record follows its Image struct (two 2,048-byte
 * name buffers, then class, matte, compression, columns, rows); and color_class holds its ClassType,
 * 1 for direct color and 2 for a palette image. Names in this file say what the game does with a
 * value, so some differ from ImageMagick's. */

#include "xvt/render/image_quantizer.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xvt/flight/fediskio.h"
#include "xvt/render/flight_sw.h"

#pragma pack(push, 1)

struct image_quantizer_pixel_run {
	uint8_t red;		  /* Red, 0 to 255. */
	uint8_t green;		  /* Green, 0 to 255. */
	uint8_t blue;		  /* Blue, 0 to 255. */
	uint8_t length_minus_one; /* Pixels in the run less 1. */
	/* Palette entry the run maps to once colors are assigned. */
	uint16_t palette_index;
};

struct image_quantizer_image_layout {
	/* The record's bytes before color_class, not named in this view. */
	uint8_t reserved0000[0x1024];
	/* 1 for direct color, 2 for a palette image;
	 * image_quantizer_assign_palette_colors sets 2 unless colorspace is 3. */
	uint32_t color_class;
	/* Nonzero makes image_quantizer_compress_pixel_runs also need equal
	 * palette indexes to join pixels; every writer sets it to 0. */
	uint32_t compare_palette_index;
	/* 2 from image_quantizer_allocate_image; image_quantizer_compress_pixel_runs
	 * sets 1 when the runs save too little. */
	uint32_t compression_type;
	uint32_t width;	 /* Width in pixels. */
	uint32_t height; /* Height in pixels. */
	/* Not all reserved: the palette pointer at offset 0x104C and the palette color count at 0x1054 sit in
	 * these bytes, and this file reads and writes them by raw offset. */
	uint8_t reserved1038[0x4E]; /* Record bytes 0x1038 to 0x1085. */
	struct image_quantizer_pixel_run
		*pixels; /* The pixel runs, from malloc. */
	/* Nothing reads or writes it by name. */
	uint8_t reserved_after_pixels[4];
	/* Runs at pixels: width * height until image_quantizer_compress_pixel_runs
	 * joins them. */
	uint32_t run_count;
	/* Pixels left in the source run while image_quantizer_compress_pixel_runs
	 * walks the runs. */
	uint32_t source_run_pixels_remaining;
};

struct image_quantizer_owned_buffers {
	uint8_t reserved0000[0x1014]; /* Bytes this view does not name. */
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1014;
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1018;
	uint8_t reserved101c[0x28]; /* Bytes this view does not name. */
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1044;
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1048;
	/* The palette, freed by image_quantizer_destroy_image when not NULL. */
	void *palette;
	uint8_t reserved1050[0x32]; /* Bytes this view does not name. */
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer1082;
	/* The pixel runs, freed by image_quantizer_destroy_image when not
	 * NULL. */
	void *pixels;
	uint8_t reserved108a[0x10]; /* Bytes this view does not name. */
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer109a;
	uint8_t reserved109e[0x810]; /* Bytes this view does not name. */
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer18ae;
	/* image_quantizer_destroy_image frees it when not NULL; nothing else
	 * reads or writes it by name. */
	void *buffer18b2;
};

/* The image record image_quantizer_allocate_image makes, at its 32-bit layout:
 * palette and pixels are 4-byte slots. In the 64-bit build the pointers kept
 * in the record take 8 bytes: the palette pointer copied to 0x104C also fills
 * field1050, the pixel run pointer also fills field108a, and each field of
 * image_quantizer_image_layout and image_quantizer_owned_buffers after a pointer
 * sits 4 bytes later per pointer before it than in the 32-bit layout, so the
 * layout's run_count falls on reserved1092. The notes below on what
 * image_quantizer_destroy_image frees hold for the 32-bit build. */
struct image_quantizer_legacy_image_record {
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field0000;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field0004;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field0008;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint8_t field000c;
	/* Left as malloc leaves it; nothing reads or writes it by name. */
	uint8_t reserved000d[0x7FF];
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field080c;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field0810;
	/* "MIFF" from image_quantizer_allocate_image; nothing reads it. */
	char format_name[0x800];
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1014;
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1018;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field101c;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1020;
	/* 1, direct color, from image_quantizer_allocate_image; see
	 * image_quantizer_image_layout. */
	uint32_t color_class;
	/* 0 from image_quantizer_allocate_image; see
	 * image_quantizer_image_layout. */
	uint32_t compare_palette_index;
	/* 2 from image_quantizer_allocate_image; see
	 * image_quantizer_image_layout. */
	uint32_t compression_type;
	/* 0 from image_quantizer_allocate_image; the caller sets the width. */
	uint32_t width;
	/* 0 from image_quantizer_allocate_image; the caller sets the height. */
	uint32_t height;
	/* 8 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1038;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field103c;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1040;
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1044;
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1048;
	/* 0 from image_quantizer_allocate_image;
	 * image_quantizer_assign_palette_colors keeps the palette pointer at this
	 * offset, 0x104C. */
	uint32_t palette;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1050;
	/* 0 from image_quantizer_allocate_image;
	 * image_quantizer_assign_palette_colors stores the color count at this
	 * offset, 0x1054. */
	uint32_t palette_color_count;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1058;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field105c;
	/* 2 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint16_t field1060;
	/* 72.0 from image_quantizer_allocate_image; nothing else reads or writes
	 * it by name. */
	float field1062;
	/* 72.0 from image_quantizer_allocate_image; nothing else reads or writes
	 * it by name. */
	float field1066;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field106a;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field106e;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1072;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1076;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field107a;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field107e;
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field1082;
	/* 0 from image_quantizer_allocate_image: the 32-bit slot of the pixel run
	 * pointer, which the other code reaches through
	 * image_quantizer_image_layout. */
	uint32_t pixels;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field108a;
	/* 0 from image_quantizer_allocate_image; see
	 * image_quantizer_image_layout. */
	uint32_t run_count;
	/* Left as malloc leaves it and not named in this view; these bytes
	 * are image_quantizer_image_layout's source_run_pixels_remaining in the
	 * 32-bit build and its run_count in the 64-bit one. */
	uint8_t reserved1092[4];
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field1096;
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field109a;
	/* time(NULL) when image_quantizer_allocate_image made the record; nothing
	 * reads it. */
	uint32_t timestamp109e;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint8_t field10a2;
	/* Left as malloc leaves it; nothing reads or writes it by name. */
	uint8_t reserved10a3[0x7FF];
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field18a2;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field18a6;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field18aa;
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field18ae;
	/* 0 from image_quantizer_allocate_image; image_quantizer_destroy_image
	 * frees the pointer at this offset when it is not NULL. */
	uint32_t field18b2;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field18b6;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field18ba;
	/* 0 from image_quantizer_allocate_image; nothing else reads or writes it
	 * by name. */
	uint32_t field18be;
	/* 0 from image_quantizer_allocate_image;
	 * image_quantizer_quantize_image_lists reads the next image of a list at
	 * this offset, 6338. */
	uint32_t next_image;
};

#pragma pack(pop)
typedef char xvt_size_image_quantizer_pixel_run
	[(sizeof(struct image_quantizer_pixel_run) == 6) ? 1 : -1];
typedef char xvt_size_image_quantizer_legacy_image_record
	[(sizeof(struct image_quantizer_legacy_image_record) == 0x18C6) ? 1
									: -1];
#if defined(_MSC_VER) && !defined(XVT_MODERN)
typedef char xvt_size_image_quantizer_image_layout
	[(sizeof(struct image_quantizer_image_layout) == 0x1096) ? 1 : -1];
typedef char xvt_size_image_quantizer_owned_buffers
	[(sizeof(struct image_quantizer_owned_buffers) == 0x18B6) ? 1 : -1];
#else
typedef char xvt_size_image_quantizer_image_layout
	[(sizeof(struct image_quantizer_image_layout) == 0x109A) ? 1 : -1];
typedef char xvt_size_image_quantizer_owned_buffers
	[(sizeof(struct image_quantizer_owned_buffers) == 0x18DE) ? 1 : -1];
#endif

struct image_quantizer_node_pool_block {
	/* Nodes image_quantizer_allocate_node hands out in order. */
	struct image_quantizer_node nodes[2048];
	/* The block made before this one, NULL for the first; the pools are
	 * freed along these links. */
	struct image_quantizer_node_pool_block *previous;
};

/* 196608.0, 3 * 256 * 256: the start value of a nearest-color search and the
 * error each pixel adds to the tree's root. */
// GLOBAL: XVT 0x518148
const double g_image_quantizer_max_squared_rgb_error_per_pixel = 196608.0;

/* Context text passed to image_quantizer_fatal_allocation_error, which ignores
 * it. */
// GLOBAL: XVT 0x521C20
static const char g_image_quantizer_unable_to_quantize_message[28] =
	"Unable to quantize image";

/* Root of the color tree, made by image_quantizer_initialize_color_tree; its
 * parent is itself. */
// GLOBAL: XVT 0x556928
static struct image_quantizer_node *g_image_quantizer_root = 0;
/* Level of the color tree's leaves: image_quantizer_initialize_color_tree sets 2
 * to 8, and image_quantizer_classify_image_colors lowers it by 1 each time it
 * collapses the deepest level. */
// GLOBAL: XVT 0x55692C
static int g_image_quantizer_max_tree_depth = 0;
/* Colors in the color tree: leaves made while classifying, nodes holding pixels
 * after a reduction pass, then palette entries as they are built. */
// GLOBAL: XVT 0x556930
static unsigned int g_image_quantizer_color_count = 0;
/* Red of the color image_quantizer_find_nearest_palette_entry_recursive looks for;
 * image_quantizer_assign_palette_colors sets the three search channels. */
// GLOBAL: XVT 0x556934
static uint8_t g_image_quantizer_search_red = 0;
/* Green of the searched color; see g_image_quantizer_search_red. */
// GLOBAL: XVT 0x556935
static uint8_t g_image_quantizer_search_green = 0;
/* Blue of the searched color; see g_image_quantizer_search_red. */
// GLOBAL: XVT 0x556936
static uint8_t g_image_quantizer_search_blue = 0;
/* Palette being built or searched, 9-byte entries; set by
 * image_quantizer_assign_palette_colors and
 * image_quantizer_export_palette6_bit_and_destroy. */
// GLOBAL: XVT 0x55693D
static struct image_quantizer_palette_entry *g_image_quantizer_palette_entries =
	0;
/* Smallest squared distance found so far by
 * image_quantizer_find_nearest_palette_entry_recursive. */
// GLOBAL: XVT 0x556941
static double g_image_quantizer_nearest_distance_sq = 0.0;
/* Error at or under which a reduction pass merges a node into its parent. */
// GLOBAL: XVT 0x556949
static double g_image_quantizer_prune_threshold = 0.0;
/* Smallest error above the threshold met in the last reduction pass, the next
 * pass's threshold; image_quantizer_reduce_color_tree starts it at 1.0 and each
 * pass at the root's error less 1.0. */
// GLOBAL: XVT 0x556951
static double g_image_quantizer_next_prune_threshold = 0.0;
/* Squares of the differences -255 to 255, pointing at the square of 0 so a
 * signed difference indexes it; made by image_quantizer_initialize_color_tree and
 * freed by the functions that end a quantization. */
// GLOBAL: XVT 0x556959
static uint32_t *g_image_quantizer_squared_diff_table = 0;
/* Color tree nodes made and not merged away; image_quantizer_classify_image_colors
 * collapses the deepest level while it is over 0x41241. */
// GLOBAL: XVT 0x55695D
unsigned int g_image_quantizer_node_count = 0;
/* Unused nodes left in the newest pool block; 2048 when a block is made. */
// GLOBAL: XVT 0x556961
static unsigned int g_image_quantizer_pool_nodes_remaining = 0;
/* Palette entry image_quantizer_find_nearest_palette_entry_recursive found nearest
 * the searched color. */
// GLOBAL: XVT 0x556965
static unsigned int g_image_quantizer_nearest_palette_index = 0;
/* Next unused node of the newest pool block. */
// GLOBAL: XVT 0x556969
static struct image_quantizer_node *g_image_quantizer_next_node = 0;
/* Newest block of the node pool, linked to the older ones through previous;
 * NULL when there is none. */
// GLOBAL: XVT 0x55696D
static struct image_quantizer_node_pool_block
	*g_image_quantizer_node_pool_head = 0;

/* Does nothing; its arguments are ignored. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x443890
void image_quantizer_report_progress(const char *stage, unsigned int completed,
				     unsigned int total)
{
	(void)stage;
	(void)completed;
	(void)total;
}

/* Ignores its arguments and calls fe_disk_io_fatal_error with the out-of-memory
 * message, which ends the program. */
// FUNCTION: XVT 0x4438B0
void image_quantizer_fatal_allocation_error(const char *context,
					    const char *message)
{
	(void)context;
	(void)message;
	fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
}

/* Allocates an image record (0x18C6 bytes) and gives it default values: format
 * name "MIFF", color_class 1, compression_type 2, field1038 8, field1060 2, both
 * floats 72.0, timestamp109e the current time, and 0 elsewhere; the three
 * reserved arrays are left as malloc leaves them. Returns it; when malloc fails
 * it calls image_quantizer_fatal_allocation_error, which ends the program. */
// FUNCTION: XVT 0x4438C0
void *image_quantizer_allocate_image(void)
{
	struct image_quantizer_legacy_image_record *image;

	image = malloc(sizeof(*image));
	if (image == NULL) {
		image_quantizer_fatal_allocation_error(
			"Unable to allocate image", "Memory allocation failed");
		return NULL;
	}

	image->field0000 = 0;
	image->field0004 = 0;
	image->field0008 = 0;
	image->field000c = 0;
	image->field080c = 0;
	image->field0810 = 0;
	strcpy(image->format_name, "MIFF");
	image->field1014 = 0;
	image->field1018 = 0;
	image->field101c = 0;
	image->field1020 = 0;
	image->color_class = 1;
	image->compare_palette_index = 0;
	image->compression_type = 2;
	image->width = 0;
	image->height = 0;
	image->field1038 = 8;
	image->field103c = 0;
	image->field1040 = 0;
	image->field1060 = 2;
	image->field1062 = 72.0f;
	image->field1066 = 72.0f;
	image->field1044 = 0;
	image->field1048 = 0;
	image->palette = 0;
	image->field1050 = 0;
	image->palette_color_count = 0;
	image->field1058 = 0;
	image->field105c = 0;
	image->field1076 = 0;
	image->field107a = 0;
	image->field106e = 0;
	image->field1072 = 0;
	image->field106a = 0;
	image->field107e = 0;
	image->field1082 = 0;
	image->pixels = 0;
	image->field108a = 0;
	image->run_count = 0;
	image->field1096 = 0;
	image->field109a = 0;
	image->field10a2 = 0;
	image->field18a2 = 0;
	image->field18a6 = 0;
	image->timestamp109e = (uint32_t)time(NULL);
	image->field18aa = 0;
	image->field18ae = 0;
	image->field18b2 = 0;
	image->field18b6 = 0;
	image->field18ba = 0;
	image->field18be = 0;
	image->next_image = 0;
	return image;
}

/* Joins neighboring pixels of the same color, and with compare_palette_index set
 * the same palette index, into runs of up to 256, in place, then shrinks the
 * pixel buffer to run_count runs with realloc, without checking its result. Sets
 * compression_type to 1 when run_count is at least width * height * 3 / 4 for
 * direct color, or width * height / 2 for a palette image. Does nothing for
 * NULL. */
// FUNCTION: XVT 0x443A50
void image_quantizer_compress_pixel_runs(unsigned int *image)
{
	unsigned int pixel_index = 0;
	if (image == NULL) {
		return;
	}

	struct image_quantizer_image_layout *image_layout =
		(struct image_quantizer_image_layout *)image;
	struct image_quantizer_pixel_run *source_run = image_layout->pixels;
	unsigned int remaining = source_run->length_minus_one + 1;
	image_layout->run_count = 0;
	image_layout->source_run_pixels_remaining = remaining;
	struct image_quantizer_pixel_run *destination_run = source_run;
	destination_run->length_minus_one = 0xFF;
	if (image_layout->compare_palette_index != 0) {
		if (image_layout->width * image_layout->height != 0) {
			do {
				remaining =
					image_layout
						->source_run_pixels_remaining;
				if (remaining != 0) {
					--remaining;
				} else {
					++source_run;
					remaining =
						source_run->length_minus_one;
				}
				image_layout->source_run_pixels_remaining =
					remaining;
				if (destination_run->red == source_run->red &&
				    destination_run->green ==
					    source_run->green &&
				    source_run->blue == destination_run->blue &&
				    source_run->palette_index ==
					    destination_run->palette_index &&
				    destination_run->length_minus_one < 0xFF) {
					++destination_run->length_minus_one;
				} else {
					if (image_layout->run_count != 0) {
						++destination_run;
					}
					++image_layout->run_count;
					*destination_run = *source_run;
					destination_run->length_minus_one = 0;
				}
				++pixel_index;
			} while (image_layout->width * image_layout->height >
				 pixel_index);
		}
	} else {
		for (pixel_index = 0;
		     image_layout->width * image_layout->height > pixel_index;
		     ++pixel_index) {
			remaining = image_layout->source_run_pixels_remaining;
			if (remaining != 0) {
				--remaining;
			} else {
				++source_run;
				remaining = source_run->length_minus_one;
			}
			image_layout->source_run_pixels_remaining = remaining;
			if (destination_run->red == source_run->red &&
			    destination_run->green == source_run->green &&
			    source_run->blue == destination_run->blue &&
			    destination_run->length_minus_one < 0xFF) {
				++destination_run->length_minus_one;
			} else {
				if (image_layout->run_count != 0) {
					++destination_run;
				}
				++image_layout->run_count;
				*destination_run = *source_run;
				destination_run->length_minus_one = 0;
			}
		}
	}

	struct image_quantizer_pixel_run *resized_runs;
	resized_runs = realloc(image_layout->pixels,
			       image_layout->run_count * sizeof(*resized_runs));
	unsigned int color_class = image_layout->color_class;
	image_layout->pixels = resized_runs;
	unsigned int height = image_layout->height;
	if (color_class == 1) {
		if ((height * image_layout->width * 3) / 4 <=
		    image_layout->run_count) {
			image_layout->compression_type = 1;
		}
	} else if ((height * image_layout->width) / 2 <=
		   image_layout->run_count) {
		image_layout->compression_type = 1;
	}
}

/* Frees each pointer the image_quantizer_owned_buffers view names that is not
 * NULL, palette and pixels included, then the record. Does nothing for NULL. */
// FUNCTION: XVT 0x443C30
void image_quantizer_destroy_image(void *image)
{
	if (image == NULL) {
		return;
	}

	struct image_quantizer_owned_buffers *owned_buffers =
		(struct image_quantizer_owned_buffers *)image;
	if (owned_buffers->buffer1014 != NULL) {
		free(owned_buffers->buffer1014);
	}
	if (owned_buffers->buffer1018 != NULL) {
		free(owned_buffers->buffer1018);
	}
	if (owned_buffers->buffer1044 != NULL) {
		free(owned_buffers->buffer1044);
	}
	if (owned_buffers->buffer1048 != NULL) {
		free(owned_buffers->buffer1048);
	}
	if (owned_buffers->palette != NULL) {
		free(owned_buffers->palette);
	}
	if (owned_buffers->buffer1082 != NULL) {
		free(owned_buffers->buffer1082);
	}
	if (owned_buffers->pixels != NULL) {
		free(owned_buffers->pixels);
	}
	if (owned_buffers->buffer109a != NULL) {
		free(owned_buffers->buffer109a);
	}
	if (owned_buffers->buffer18ae != NULL) {
		free(owned_buffers->buffer18ae);
	}
	if (owned_buffers->buffer18b2 != NULL) {
		free(owned_buffers->buffer18b2);
	}
	free(owned_buffers);
}

/* Expands the runs to one per pixel in place, working back from the end, after
 * growing the buffer to width * height runs with realloc; sets run_count to
 * width * height. Returns 1, also when run_count already is width * height; 0
 * when realloc fails. */
// FUNCTION: XVT 0x443D10
int image_quantizer_expand_pixel_runs(uint32_t *image)
{
	struct image_quantizer_image_layout *image_layout =
		(struct image_quantizer_image_layout *)image;
	unsigned int pixel_count = image_layout->width * image_layout->height;
	if (image_layout->run_count == pixel_count) {
		return 1;
	}
	struct image_quantizer_pixel_run *resized_pixels;
	resized_pixels = (struct image_quantizer_pixel_run *)realloc(
		image_layout->pixels, pixel_count * sizeof(*resized_pixels));
	if (resized_pixels == NULL) {
		return 0;
	}
	unsigned int run_count = image_layout->run_count;
	image_layout->pixels = resized_pixels;
	struct image_quantizer_pixel_run *source =
		resized_pixels + run_count - 1;
	unsigned int expanded_pixel_count =
		image_layout->width * image_layout->height;
	struct image_quantizer_pixel_run *destination =
		resized_pixels + expanded_pixel_count - 1;
	unsigned int source_index = 0;
	if (run_count != 0) {
		do {
			int copies = source->length_minus_one;
			if (copies >= 0) {
				++copies;
				do {
					*destination = *source;
					destination->length_minus_one = 0;
					--destination;
					--copies;
				} while (copies != 0);
			}
			--source;
			++source_index;
		} while (image_layout->run_count > source_index);
	}
	image_layout->run_count = image_layout->width * image_layout->height;
	return 1;
}

/* Reduces one image to at most palette_size colors (palette_size kept to 1 to
 * 0xFFFF): joins its runs when the run count at byte 4238 is width * height,
 * builds a color tree of tree_depth levels (for 0: 1 plus the right shifts by 2
 * that bring palette_size to 0, 1 less with dither, 2 more for a palette image),
 * classifies the colors, reduces the tree when it holds more than palette_size,
 * and assigns the palette, without dither when half the tree's color count is
 * under palette_size. Then frees the node pool and the squared-difference table.
 * Returns at once for palette_size 2 with colorspace 2 and dither. Nothing calls
 * this. */
// FUNCTION: XVT 0x443DE0
void image_quantizer_quantize_image(unsigned int *image,
				    unsigned int palette_size, int tree_depth,
				    int dither, int colorspace)
{
	unsigned int target_color_count = palette_size;
	int effective_depth = tree_depth;
	int effective_dither = dither;
	uint32_t *image_words = image;

	if (palette_size == 2 && colorspace == 2 && dither != 0) {
		return;
	}
	if (target_color_count == 0) {
		target_color_count = 1;
	}
	if (target_color_count > 0xFFFF) {
		target_color_count = 0xFFFF;
	}
	unsigned int stored_run_count;
	memcpy(&stored_run_count, (uint8_t *)image + 4238,
	       sizeof(stored_run_count));
	if (image_words[1036] * image_words[1037] == stored_run_count) {
		image_quantizer_compress_pixel_runs(image);
	}
	if (effective_depth == 0) {
		effective_depth = 1;
		unsigned int value = target_color_count;
		while (value != 0) {
			value >>= 2;
			++effective_depth;
		}
		if (effective_dither != 0) {
			--effective_depth;
		}
		if (image_words[1033] == 2) {
			effective_depth += 2;
		}
	}
	image_quantizer_initialize_color_tree(effective_depth);
	image_quantizer_classify_image_colors(image);
	if ((g_image_quantizer_color_count >> 1) < target_color_count) {
		effective_dither = 0;
	}
	if (target_color_count < g_image_quantizer_color_count) {
		image_quantizer_reduce_color_tree(target_color_count);
	}
	image_quantizer_assign_palette_colors(image, target_color_count,
					      effective_dither, colorspace);
	while (g_image_quantizer_node_pool_head != NULL) {
		struct image_quantizer_node_pool_block *previous_block =
			g_image_quantizer_node_pool_head->previous;
		free(g_image_quantizer_node_pool_head);
		g_image_quantizer_node_pool_head = previous_block;
	}
	g_image_quantizer_squared_diff_table -= 255;
	free(g_image_quantizer_squared_diff_table);
}

/* Builds the image's palette from the color tree and maps its runs to it. Frees
 * the old palette (the pointer at byte 0x104C), allocates
 * g_image_quantizer_color_count 9-byte entries there and fills them with
 * image_quantizer_build_palette_entries_recursive, which counts
 * g_image_quantizer_color_count again. For palette_size 2 with colorspace 2 the two
 * entries become white and black, white for the one with the larger 77 R + 150
 * G + 29 B. Unless colorspace is 3 it sets compare_palette_index 0 and color_class
 * 2. Stores the color count at byte 0x1054. With dither it returns 1 when
 * image_quantizer_dither_image_to_palette succeeds (returns 0); when that fails, or
 * without dither, each run goes down the tree by its color bits as far as
 * children exist, and the entry nearest its color under that node's parent
 * (image_quantizer_find_nearest_palette_entry_recursive) becomes its paletteIndex,
 * or for direct color its color; it returns 0. When the palette cannot be
 * allocated it calls image_quantizer_fatal_allocation_error, which ends the
 * program. */
// FUNCTION: XVT 0x443EF0
unsigned int image_quantizer_assign_palette_colors(uint32_t *image,
						   unsigned int palette_size,
						   int dither, int colorspace)
{
	struct image_quantizer_image_layout *layout =
		(struct image_quantizer_image_layout *)image;
	uint8_t *image_bytes = (uint8_t *)image;
	struct image_quantizer_palette_entry *palette;
	memcpy(&palette, image_bytes + 0x104C, sizeof(palette));
	free(palette);
	unsigned int color_count = g_image_quantizer_color_count;
	unsigned int palette_bytes =
		color_count * sizeof(struct image_quantizer_palette_entry);
	palette = malloc(palette_bytes);
	if (palette == NULL) {
		image_quantizer_fatal_allocation_error(
			"Unable to assign palette", "Memory allocation failed");
		return 1;
	}
	memcpy(image_bytes + 0x104C, &palette, sizeof(palette));
	uint8_t *palette_bytes_ptr = (uint8_t *)palette;
	g_image_quantizer_palette_entries = palette;
	g_image_quantizer_color_count = 0;
	image_quantizer_build_palette_entries_recursive(g_image_quantizer_root);
	if (palette_size == 2 && colorspace == 2 && color_count >= 2) {
		int first_luma = 77 * palette[0].red + 150 * palette[0].green +
				 29 * palette[0].blue;
		int second_luma = 77 * palette[1].red + 150 * palette[1].green +
				  29 * palette[1].blue;
		if (first_luma < second_luma) {
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
		layout->compare_palette_index = 0;
		layout->color_class = 2;
	}
	memcpy(image_bytes + 0x1054, &g_image_quantizer_color_count,
	       sizeof(g_image_quantizer_color_count));
	int image_dithered =
		dither ? image_quantizer_dither_image_to_palette(image) == 0
		       : 0;
	if (image_dithered != 0) {
		return (unsigned int)image_dithered;
	}
	struct image_quantizer_pixel_run *pixel = layout->pixels;
	unsigned int run_count = layout->run_count;
	for (unsigned int completed = 0; completed < run_count;
	     ++completed, ++pixel) {
		struct image_quantizer_node *node = g_image_quantizer_root;
		unsigned int level = 1;
		int bit_position = 7;
		while (level <=
		       (unsigned int)g_image_quantizer_max_tree_depth) {
			unsigned int child_index =
				((pixel->red >> bit_position) & 1u) << 2;
			child_index |= ((pixel->green >> bit_position) & 1u)
				       << 1;
			child_index |= (pixel->blue >> bit_position) & 1u;
			if ((node->children_mask & (1u << child_index)) == 0) {
				break;
			}
			node = node->children[child_index];
			--bit_position;
			++level;
		}
		g_image_quantizer_search_red = pixel->red;
		g_image_quantizer_search_green = pixel->green;
		g_image_quantizer_search_blue = pixel->blue;
		g_image_quantizer_nearest_distance_sq =
			g_image_quantizer_max_squared_rgb_error_per_pixel;
		image_quantizer_find_nearest_palette_entry_recursive(
			node->parent);
		if (layout->color_class == 2) {
			pixel->palette_index = (uint16_t)
				g_image_quantizer_nearest_palette_index;
		} else {
			unsigned int offset =
				9 * g_image_quantizer_nearest_palette_index;
			pixel->red = palette_bytes_ptr[offset];
			pixel->green = palette_bytes_ptr[offset + 1];
			pixel->blue = palette_bytes_ptr[offset + 2];
		}
		if (run_count - completed == 1 ||
		    completed % layout->height == 0) {
			image_quantizer_report_progress(
				"  Assigning image colors...  ", completed,
				run_count);
		}
	}
	return 0;
}

/* Adds an image's runs to the color tree. Adds width * height * 196608 to the
 * root's error. For each run it first, when more than 0x41241 nodes exist,
 * collapses the deepest level with image_quantizer_collapse_deepest_level_recursive
 * and lowers g_image_quantizer_max_tree_depth. It walks down
 * g_image_quantizer_max_tree_depth levels by the color's bits from the top (red 4,
 * green 2, blue 1), making missing children with the parent's midpoints moved
 * up or down, by the child's bits, by (1 << (8 - level)) >> 1, and counting
 * each new leaf in g_image_quantizer_color_count, and adds to each node it passes
 * the run's squared distance to its midpoint times the run length. The last
 * node gets the run's pixel count and channel sums. Exits the program when a
 * node cannot be made. */
// FUNCTION: XVT 0x4441F0
void image_quantizer_classify_image_colors(unsigned int *image)
{
	g_image_quantizer_root->quantization_error +=
		(double)((struct image_quantizer_image_layout *)image)->width *
		(double)((struct image_quantizer_image_layout *)image)->height *
		g_image_quantizer_max_squared_rgb_error_per_pixel;
	struct image_quantizer_pixel_run *pixel =
		((struct image_quantizer_image_layout *)image)->pixels;
	for (unsigned int completed = 0;
	     completed <
	     ((struct image_quantizer_image_layout *)image)->run_count;
	     ++completed) {
		if (g_image_quantizer_node_count > 0x41241) {
			image_quantizer_collapse_deepest_level_recursive(
				g_image_quantizer_root);
			--g_image_quantizer_max_tree_depth;
		}

		struct image_quantizer_node *node = g_image_quantizer_root;
		int bit_position = 7;
		unsigned int level = 1;
		unsigned int run_length =
			(unsigned int)pixel->length_minus_one + 1;
		if ((unsigned int)g_image_quantizer_max_tree_depth >= 1) {
			double pixel_weight = (double)run_length;
			do {
				unsigned int child_index =
					((pixel->red >> bit_position) & 1u)
					<< 2;
				child_index |=
					((pixel->green >> bit_position) & 1u)
					<< 1;
				child_index |=
					(pixel->blue >> bit_position) & 1u;
				if (node->children[child_index] == NULL) {
					node->children_mask |=
						(uint8_t)(1u << child_index);
					unsigned int midpoint_offset =
						(1u << (8 - level)) >> 1;
					int blue_offset = midpoint_offset;
					if ((child_index & 1) == 0) {
						blue_offset = -midpoint_offset;
					}
					int green_offset = midpoint_offset;
					if ((child_index & 2) == 0) {
						green_offset = -midpoint_offset;
					}
					int red_offset = midpoint_offset;
					if ((child_index & 4) == 0) {
						red_offset = -midpoint_offset;
					}
					node->children[child_index] =
						image_quantizer_allocate_node(
							child_index, level,
							node,
							node->midpoint_red +
								red_offset,
							node->midpoint_green +
								green_offset,
							node->midpoint_blue +
								blue_offset);
					if (node->children[child_index] ==
					    NULL) {
						image_quantizer_fatal_allocation_error(
							g_image_quantizer_unable_to_quantize_message,
							"Memory allocation failed");
						exit(1);
					}
					if (level ==
					    (unsigned int)
						    g_image_quantizer_max_tree_depth) {
						++g_image_quantizer_color_count;
					}
				}
				node = node->children[child_index];
				double pixel_error = (double)
					g_image_quantizer_squared_diff_table
						[(int)pixel->red -
						 node->midpoint_red];
				pixel_error += (double)
					g_image_quantizer_squared_diff_table
						[(int)pixel->green -
						 node->midpoint_green];
				pixel_error += (double)
					g_image_quantizer_squared_diff_table
						[(int)pixel->blue -
						 node->midpoint_blue];
				node->quantization_error +=
					pixel_error * pixel_weight;
				--bit_position;
				++level;
			} while (
				level <=
				(unsigned int)g_image_quantizer_max_tree_depth);
		}

		node->pixel_count += run_length;
		node->red_sum += (double)(run_length * pixel->red);
		node->green_sum += (double)(run_length * pixel->green);
		node->blue_sum += (double)(run_length * pixel->blue);
		++pixel;
		if (((struct image_quantizer_image_layout *)image)->run_count -
				    completed ==
			    1 ||
		    completed % ((struct image_quantizer_image_layout *)image)
					    ->height ==
			    0) {
			image_quantizer_report_progress(
				"  Classifying image colors...  ", completed,
				((struct image_quantizer_image_layout *)image)
					->run_count);
		}
	}
}

/* Searches node's subtree, children first, for the palette entry nearest the
 * searched color by squared distance, looking at the entry of each node that
 * holds pixels; updates g_image_quantizer_nearest_distance_sq and
 * g_image_quantizer_nearest_palette_index on a closer one. Only
 * image_quantizer_assign_palette_colors calls it. */
// FUNCTION: XVT 0x4444F0
void image_quantizer_find_nearest_palette_entry_recursive(
	const struct image_quantizer_node *node)
{
	if (node->children_mask != 0) {
		unsigned int child_index = 0;
		do {
			if ((node->children_mask & (1u << child_index)) != 0) {
				image_quantizer_find_nearest_palette_entry_recursive(
					node->children[child_index]);
			}
			++child_index;
		} while (child_index < 8);
	}

	if (node->pixel_count != 0) {
		struct image_quantizer_palette_entry *entry =
			&g_image_quantizer_palette_entries[node->palette_index];
		double distance_sq =
			(double)g_image_quantizer_squared_diff_table
				[(int)entry->green -
				 g_image_quantizer_search_green];
		distance_sq += (double)g_image_quantizer_squared_diff_table
			[(int)entry->red - g_image_quantizer_search_red];
		distance_sq += (double)g_image_quantizer_squared_diff_table
			[(int)entry->blue - g_image_quantizer_search_blue];
		if (distance_sq < g_image_quantizer_nearest_distance_sq) {
			g_image_quantizer_nearest_distance_sq = distance_sq;
			g_image_quantizer_nearest_palette_index =
				(uint16_t)node->palette_index;
		}
	}
}

/* Walks node's subtree, children first, and for each node holding pixels
 * writes palette entry g_image_quantizer_color_count, each channel
 * (sum + (pixel_count >> 1)) / pixel_count, stores that index in the node's
 * paletteIndex and adds 1 to g_image_quantizer_color_count. */
// FUNCTION: XVT 0x4445E0
void image_quantizer_build_palette_entries_recursive(
	struct image_quantizer_node *node)
{
	if (node->children_mask != 0) {
		unsigned int child_index = 0;
		do {
			if ((node->children_mask & (1u << child_index)) != 0) {
				image_quantizer_build_palette_entries_recursive(
					node->children[child_index]);
			}
			++child_index;
		} while (child_index < 8);
	}

	unsigned int pixel_count = node->pixel_count;
	if (pixel_count != 0) {
		double half_pixel_count = (double)(pixel_count >> 1);
		double pixel_count_double = (double)pixel_count;
		g_image_quantizer_palette_entries[g_image_quantizer_color_count]
			.red = (uint8_t)((node->red_sum + half_pixel_count) /
					 pixel_count_double);
		g_image_quantizer_palette_entries[g_image_quantizer_color_count]
			.green =
			(uint8_t)((node->green_sum + half_pixel_count) /
				  pixel_count_double);
		g_image_quantizer_palette_entries[g_image_quantizer_color_count]
			.blue = (uint8_t)((node->blue_sum + half_pixel_count) /
					  pixel_count_double);
		node->palette_index = g_image_quantizer_color_count;
		++g_image_quantizer_color_count;
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
 * image_quantizer_fatal_allocation_error, which ends the program. */
// FUNCTION: XVT 0x4446C0
int image_quantizer_dither_image_to_palette(uint32_t *image)
{
	if (image_quantizer_expand_pixel_runs(image) == 0) {
		return 1;
	}
	struct image_quantizer_image_layout *layout =
		(struct image_quantizer_image_layout *)image;
	struct image_quantizer_pixel_run *pixels = layout->pixels;
	uint8_t *palette;
	memcpy(&palette, (uint8_t *)image + 0x104C, sizeof(palette));
	unsigned int palette_count =
		*(uint32_t *)((uint8_t *)image + 0x1054) >> 4;
	if (palette_count == 0) {
		palette_count = 1;
	}
	unsigned int row_width = layout->width;
	int *errors;
	errors = calloc((row_width + 2) * 6, sizeof(*errors));
	if (errors == NULL || palette == NULL) {
		free(errors);
		image_quantizer_fatal_allocation_error(
			"Unable to dither image", "Memory allocation failed");
		return 1;
	}
	unsigned int pixel_index = 0;
	for (unsigned int row = 0; row < layout->height; ++row) {
		int direction = (row & 1) == 0 ? 1 : -1;
		for (unsigned int column = 0; column < row_width; ++column) {
			unsigned int source_column =
				direction > 0 ? column : row_width - column - 1;
			struct image_quantizer_pixel_run *pixel =
				&pixels[pixel_index + source_column];
			int red =
				pixel->red + errors[(column + 1) * 3 + 0] / 16;
			int green = pixel->green +
				    errors[(column + 1) * 3 + 1] / 16;
			int blue =
				pixel->blue + errors[(column + 1) * 3 + 2] / 16;
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
			int nearest_index = 0;
			int nearest_distance = INT_MAX;
			for (unsigned int palette_index = 0;
			     palette_index < palette_count; ++palette_index) {
				int dr = red - palette[palette_index * 9 + 0];
				int dg = green - palette[palette_index * 9 + 1];
				int db = blue - palette[palette_index * 9 + 2];
				int distance = dr * dr + dg * dg + db * db;
				if (distance < nearest_distance) {
					nearest_distance = distance;
					nearest_index = (int)palette_index;
				}
			}
			pixel->palette_index = (uint16_t)nearest_index;
			if (layout->color_class != 2) {
				pixel->red = palette[nearest_index * 9 + 0];
				pixel->green = palette[nearest_index * 9 + 1];
				pixel->blue = palette[nearest_index * 9 + 2];
			}
			int error_red = red - palette[nearest_index * 9 + 0];
			int error_green =
				green - palette[nearest_index * 9 + 1];
			int error_blue = blue - palette[nearest_index * 9 + 2];
			errors[(column + 2) * 3 + 0] += 7 * error_red;
			errors[(column + 2) * 3 + 1] += 7 * error_green;
			errors[(column + 2) * 3 + 2] += 7 * error_blue;
			errors[column * 3 + 0] += 3 * error_red;
			errors[column * 3 + 1] += 3 * error_green;
			errors[column * 3 + 2] += 3 * error_blue;
			errors[(column + 1) * 3 + 0] = 5 * error_red;
			errors[(column + 1) * 3 + 1] = 5 * error_green;
			errors[(column + 1) * 3 + 2] = 5 * error_blue;
		}
		memset(errors, 0, (row_width + 2) * 3 * sizeof(*errors));
		pixel_index += row_width;
		image_quantizer_report_progress("  Dithering image...  ", row,
						layout->height);
	}
	free(errors);
	return 0;
}

/* Starts a new color tree: clears the node pool bookkeeping without freeing old
 * blocks, sets g_image_quantizer_max_tree_depth to tree_depth kept to 2 to 8, makes
 * the root (midpoints 0x80, its own parent, error 0), sets
 * g_image_quantizer_color_count to 0 and builds g_image_quantizer_squared_diff_table.
 * Returns 256, where its loop stops. Exits the program when an allocation
 * fails. */
// FUNCTION: XVT 0x444CB0
int image_quantizer_initialize_color_tree(int tree_depth)
{
	g_image_quantizer_node_pool_head = NULL;
	g_image_quantizer_node_count = 0;
	g_image_quantizer_pool_nodes_remaining = 0;
	if (tree_depth > 8) {
		tree_depth = 8;
	}
	if (tree_depth < 2) {
		tree_depth = 2;
	}
	g_image_quantizer_max_tree_depth = tree_depth;
	g_image_quantizer_root =
		image_quantizer_allocate_node(0, 0, NULL, 0x80, 0x80, 0x80);
	g_image_quantizer_squared_diff_table = malloc(0x7FC);
	if (g_image_quantizer_root == NULL ||
	    g_image_quantizer_squared_diff_table == NULL) {
		image_quantizer_fatal_allocation_error(
			g_image_quantizer_unable_to_quantize_message,
			"Memory allocation failed");
		exit(1);
	}
	g_image_quantizer_root->parent = g_image_quantizer_root;
	g_image_quantizer_root->quantization_error = 0.0;
	g_image_quantizer_color_count = 0;
	g_image_quantizer_squared_diff_table += 255;
	int difference = -255;
	do {
		g_image_quantizer_squared_diff_table[difference] =
			difference * difference;
		++difference;
	} while (difference <= 255);

	return difference;
}

/* Takes the next node from the pool, making a new block of 2048 with malloc
 * when the newest is used up, and fills it: the given parent, child_index, level
 * and midpoints, no children, and 0 error, count and sums. Adds 1 to
 * g_image_quantizer_node_count. Returns it, or NULL when a block cannot be
 * allocated. */
// FUNCTION: XVT 0x444D90
struct image_quantizer_node *image_quantizer_allocate_node(
	int child_index, int level, struct image_quantizer_node *parent,
	int midpoint_red, int midpoint_green, int midpoint_blue)
{
	if (g_image_quantizer_pool_nodes_remaining == 0) {
		struct image_quantizer_node_pool_block *pool_block;
		pool_block = (struct image_quantizer_node_pool_block *)malloc(
			sizeof(*pool_block));
		if (pool_block == NULL) {
			return NULL;
		}
		pool_block->previous = g_image_quantizer_node_pool_head;
		g_image_quantizer_node_pool_head = pool_block;
		g_image_quantizer_next_node = pool_block->nodes;
		g_image_quantizer_pool_nodes_remaining = 2048;
	}
	++g_image_quantizer_node_count;
	--g_image_quantizer_pool_nodes_remaining;
	struct image_quantizer_node *node = g_image_quantizer_next_node;
	++g_image_quantizer_next_node;
	node->parent = parent;
	memset(node->children, 0, sizeof(node->children));
	node->child_index = child_index;
	node->level = level;
	node->children_mask = 0;
	node->midpoint_red = midpoint_red;
	node->midpoint_green = midpoint_green;
	node->midpoint_blue = midpoint_blue;
	node->quantization_error = 0.0;
	node->pixel_count = 0;
	node->red_sum = 0.0;
	node->green_sum = 0.0;
	node->blue_sum = 0.0;
	return node;
}

/* Merges every node at level g_image_quantizer_max_tree_depth in node's subtree
 * into its parent with image_quantizer_merge_node_into_parent, children first. */
// FUNCTION: XVT 0x444E60
void image_quantizer_collapse_deepest_level_recursive(
	const struct image_quantizer_node *node)
{
	if (node->children_mask != 0) {
		for (int child_index = 0; child_index < 8; child_index++) {
			if ((node->children_mask & (1 << child_index)) != 0) {
				image_quantizer_collapse_deepest_level_recursive(
					node->children[child_index]);
			}
		}
	}

	if (node->level == g_image_quantizer_max_tree_depth) {
		image_quantizer_merge_node_into_parent(node);
	}
}

/* Takes node out of its parent's children_mask and adds its pixel count and
 * channel sums to the parent's; lowers g_image_quantizer_node_count. Returns the
 * parent's new pixel count. The node's error is not passed on, and its memory
 * stays in the pool. */
// FUNCTION: XVT 0x444EB0
unsigned int
image_quantizer_merge_node_into_parent(const struct image_quantizer_node *node)
{
	struct image_quantizer_node *parent = node->parent;
	parent->children_mask &= ~(1 << node->child_index);
	unsigned int pixel_count = node->pixel_count + parent->pixel_count;
	parent->pixel_count = pixel_count;
	parent->red_sum += node->red_sum;
	parent->green_sum += node->green_sum;
	parent->blue_sum += node->blue_sum;
	--g_image_quantizer_node_count;
	return pixel_count;
}

/* Merges color tree nodes until at most target_color_count hold pixels: each pass
 * merges every node whose error is at most the threshold, the smallest error
 * left by the last pass (1.0 at first), and counts g_image_quantizer_color_count
 * again. */
// FUNCTION: XVT 0x444F00
void image_quantizer_reduce_color_tree(unsigned int target_color_count)
{
	unsigned int initial_color_count = g_image_quantizer_color_count;
	g_image_quantizer_next_prune_threshold = 1.0;
	while (target_color_count < g_image_quantizer_color_count) {
		g_image_quantizer_prune_threshold =
			g_image_quantizer_next_prune_threshold;
		g_image_quantizer_next_prune_threshold =
			g_image_quantizer_root->quantization_error - 1.0;
		g_image_quantizer_color_count = 0;
		image_quantizer_reduce_color_tree_pass_recursive(
			g_image_quantizer_root);
		image_quantizer_report_progress(
			"  Reducing image colors...  ",
			initial_color_count - g_image_quantizer_color_count,
			initial_color_count - target_color_count + 1);
	}
}

/* One reduction pass over node's subtree, children first: merges a node whose
 * error is at most g_image_quantizer_prune_threshold into its parent; any other
 * node is counted in g_image_quantizer_color_count when it holds pixels and lowers
 * g_image_quantizer_next_prune_threshold to its error when that is smaller. */
// FUNCTION: XVT 0x444F90
void image_quantizer_reduce_color_tree_pass_recursive(
	const struct image_quantizer_node *node)
{
	if (node->children_mask != 0) {
		for (unsigned int child_index = 0; child_index < 8;
		     ++child_index) {
			if ((node->children_mask & (1u << child_index)) != 0) {
				image_quantizer_reduce_color_tree_pass_recursive(
					node->children[child_index]);
			}
		}
	}

	if (node->quantization_error <= g_image_quantizer_prune_threshold) {
		image_quantizer_merge_node_into_parent(node);
		return;
	}

	if (node->pixel_count != 0) {
		++g_image_quantizer_color_count;
	}
	if (node->quantization_error < g_image_quantizer_next_prune_threshold) {
		g_image_quantizer_next_prune_threshold =
			node->quantization_error;
	}
}

/* Quantizes every image of listCount linked lists (each image's next at byte
 * 6338) with one color tree shared by all, the steps and defaults as in
 * image_quantizer_quantize_image, 2 more tree levels when any list's first image
 * is a palette image. Nothing calls this. */
// FUNCTION: XVT 0x445020
void image_quantizer_quantize_image_lists(unsigned int **image_list_heads,
					  unsigned int list_count,
					  unsigned int palette_size,
					  int tree_depth, int dither,
					  int colorspace)
{
	unsigned int target_color_count = palette_size == 0 ? 1 : palette_size;
	int effective_depth = tree_depth;

	if (target_color_count > 0xFFFF) {
		target_color_count = 0xFFFF;
	}
	unsigned int list_index;
	if (effective_depth == 0) {
		effective_depth = 1;
		unsigned int value = target_color_count;
		while (value != 0) {
			value >>= 2;
			++effective_depth;
		}
		if (dither != 0) {
			--effective_depth;
		}
		int has_indexed_image = 0;
		for (list_index = 0; list_index < list_count; ++list_index) {
			if (image_list_heads[list_index] != NULL &&
			    image_list_heads[list_index][1033] == 2) {
				has_indexed_image = 1;
			}
		}
		if (has_indexed_image != 0) {
			effective_depth += 2;
		}
	}
	image_quantizer_initialize_color_tree(effective_depth);
	unsigned int stored_run_count;
	struct image_quantizer_image_layout *image;
	struct image_quantizer_image_layout *next_image;
	for (list_index = 0; list_index < list_count; ++list_index) {
		image = (struct image_quantizer_image_layout *)
			image_list_heads[list_index];
		while (image != NULL) {
			memcpy(&stored_run_count, (uint8_t *)image + 4238,
			       sizeof(stored_run_count));
			if (image->width * image->height == stored_run_count) {
				image_quantizer_compress_pixel_runs(
					(unsigned int *)image);
			}
			memcpy(&next_image, (uint8_t *)image + 6338,
			       sizeof(next_image));
			image = next_image;
		}
	}
	for (list_index = 0; list_index < list_count; ++list_index) {
		image = (struct image_quantizer_image_layout *)
			image_list_heads[list_index];
		while (image != NULL) {
			image_quantizer_classify_image_colors(
				(unsigned int *)image);
			memcpy(&next_image, (uint8_t *)image + 6338,
			       sizeof(next_image));
			image = next_image;
		}
	}
	if ((g_image_quantizer_color_count >> 1) < target_color_count) {
		dither = 0;
	}
	if (target_color_count < g_image_quantizer_color_count) {
		image_quantizer_reduce_color_tree(target_color_count);
	}
	for (list_index = 0; list_index < list_count; ++list_index) {
		image = (struct image_quantizer_image_layout *)
			image_list_heads[list_index];
		while (image != NULL) {
			image_quantizer_assign_palette_colors(
				(unsigned int *)image, target_color_count,
				dither, colorspace);
			memcpy(&next_image, (uint8_t *)image + 6338,
			       sizeof(next_image));
			image = next_image;
		}
	}
	while (g_image_quantizer_node_pool_head != NULL) {
		struct image_quantizer_node_pool_block *previous_block =
			g_image_quantizer_node_pool_head->previous;
		free(g_image_quantizer_node_pool_head);
		g_image_quantizer_node_pool_head = previous_block;
	}
	g_image_quantizer_squared_diff_table -= 255;
	free(g_image_quantizer_squared_diff_table);
}

/* Starts the color tree for a mission palette with
 * image_quantizer_initialize_color_tree(tree_depth) and returns its result;
 * target_color_count is ignored. fe_disk_io_init_resources calls it only while
 * g_generate_mission_palette is set, which fe_disk_io_init_resources clears before
 * loading because g_palette_generation_enabled is never set, so it never runs. */
// FUNCTION: XVT 0x4451E0
int image_quantizer_begin_palette_collection(int target_color_count,
					     int tree_depth)
{
	(void)target_color_count;
	return image_quantizer_initialize_color_tree(tree_depth);
}

/* Ends a mission palette: reduces the tree to colorCount colors, builds the
 * palette entries and writes colorCount of them to palette_rgb as 6-bit triplets
 * (each channel >> 2), even when fewer were built, then frees the entries, the
 * node pool and the squared-difference table. tree_depth is ignored. Does not
 * check that a pool block exists; exits the program when the entries cannot be
 * allocated. fe_disk_io_init_resources calls it only while
 * g_generate_mission_palette is set, which fe_disk_io_init_resources clears before
 * loading because g_palette_generation_enabled is never set, so it never runs. */
// FUNCTION: XVT 0x4451F0
void image_quantizer_export_palette6_bit_and_destroy(int color_count,
						     int tree_depth,
						     uint8_t *palette_rgb)
{
	(void)tree_depth;
	int remaining_colors = color_count;
	image_quantizer_reduce_color_tree(color_count);
	g_image_quantizer_palette_entries =
		malloc(sizeof(*g_image_quantizer_palette_entries) *
		       g_image_quantizer_color_count);
	if (g_image_quantizer_palette_entries == NULL) {
		image_quantizer_fatal_allocation_error(
			g_image_quantizer_unable_to_quantize_message,
			"Memory allocation failed");
		exit(1);
	}
	unsigned int entry_offset = 0;
	g_image_quantizer_color_count = 0;
	image_quantizer_build_palette_entries_recursive(g_image_quantizer_root);
	if (color_count > 0) {
		uint8_t *output = palette_rgb;
		do {
			struct image_quantizer_palette_entry *entry =
				(struct image_quantizer_palette_entry
					 *)((uint8_t *)
						    g_image_quantizer_palette_entries +
					    entry_offset);
			int green = entry->green;
			int blue = entry->blue;
			entry_offset +=
				sizeof(struct image_quantizer_palette_entry);
			output[0] = entry->red >> 2;
			--remaining_colors;
			output[1] = green >> 2;
			output[2] = blue >> 2;
			output += 3;
		} while (remaining_colors != 0);
	}
	free(g_image_quantizer_palette_entries);
	struct image_quantizer_node_pool_block *previous_block;
	do {
		previous_block = g_image_quantizer_node_pool_head->previous;
		free(g_image_quantizer_node_pool_head);
		g_image_quantizer_node_pool_head = previous_block;
	} while (previous_block != NULL);
	g_image_quantizer_squared_diff_table -= 255;
	free(g_image_quantizer_squared_diff_table);
}

/* Adds an 8-bit image with a 5-6-5 palette to the color tree: makes a temporary
 * record of width * height one-pixel runs, each color widened to 8 bits a
 * channel by shifting (red and blue << 3, green << 2), then joins, classifies
 * and destroys it. Calls fe_disk_io_fatal_error when the pixels cannot be
 * allocated. opt_model_build_runtime_node calls it only while
 * g_generate_mission_palette is set, which fe_disk_io_init_resources clears before
 * loading because g_palette_generation_enabled is never set, so it never runs. */
// FUNCTION: XVT 0x4452D0
void image_quantizer_classify_indexed_rgb565_image(
	const uint8_t *indexed_pixels, const uint16_t *palette16,
	unsigned int width, unsigned int height)
{
	struct image_quantizer_image_layout *image =
		image_quantizer_allocate_image();
	if (image == NULL) {
		return;
	}
	image->compare_palette_index = 0;
	image->width = width;
	image->height = height;
	image->run_count = height * image->width;
	image->pixels = malloc(image->run_count * sizeof(*image->pixels));
	if (image->pixels == NULL) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	struct image_quantizer_pixel_run *sample = image->pixels;
	unsigned int row = 0;
	while (row < image->height) {
		unsigned int column = 0;
		while (column < image->width) {
			uint16_t channel = palette16[*indexed_pixels];
			channel >>= 11;
			channel <<= 3;
			sample->red = (uint8_t)channel;
			channel = palette16[*indexed_pixels];
			channel >>= 5;
			channel <<= 2;
			sample->green = (uint8_t)channel;
			channel = palette16[*indexed_pixels];
			channel <<= 3;
			sample->blue = (uint8_t)channel;
			++indexed_pixels;
			sample->palette_index = 0;
			sample->length_minus_one = 0;
			++sample;
			++column;
		}
		++row;
	}
	image_quantizer_compress_pixel_runs((unsigned int *)image);
	image_quantizer_classify_image_colors((unsigned int *)image);
	image_quantizer_destroy_image(image);
}

/* Adds a run-length texture image to the color tree: decodes it, from 16 bytes
 * past encoded_image, into a temporary record of one-pixel runs until a 0xFF at
 * a row's start. 0xFB sets the palette base from the next two bytes, low byte
 * first; 0xFC gives input[1] + 1 gray (0x80) pixels; 0xFD gives input[1] + 1
 * pixels of entry input[2]; any other byte gives (byte & mask) + 1 pixels of
 * entry base + (byte >> shift), mask and shift from
 * g_flight_sw_rle_run_length_mask_by_packing_mode and
 * g_flight_sw_rle_palette_shift_by_packing_mode; colors come from palette_rgba, 4 bytes
 * per entry. Then joins, classifies and destroys the record. Does not check the
 * pixel allocation or that the decoded pixels fit width * height.
 * tex_level_convert24_bpp_palettes_to8_bpp calls it only while
 * g_generate_mission_palette is set, which fe_disk_io_init_resources clears before
 * loading because g_palette_generation_enabled is never set, so it never runs. */
// FUNCTION: XVT 0x4453D0
void image_quantizer_classify_encoded_tex_level_image(
	const uint8_t *encoded_image, const uint8_t *palette_rgba,
	unsigned int width, unsigned int height, int packing_mode)
{
	struct image_quantizer_image_layout *image =
		image_quantizer_allocate_image();
	if (image == NULL) {
		return;
	}
	image->compare_palette_index = 0;
	image->width = width;
	image->height = height;
	image->run_count = height * image->width;
	image->pixels = malloc(image->run_count * sizeof(*image->pixels));
	const uint8_t *command_ptr = encoded_image + 16;
	int palette_base = 0;
	struct image_quantizer_pixel_run *sample = image->pixels;
	uint8_t run_length_minus_one;
	int palette_index;
	while (*command_ptr != 0xff) {
		while (*command_ptr != 0xfe) {
			uint8_t command = *command_ptr;
			if (command == 0xfb) {
				palette_base =
					command_ptr[1] + (command_ptr[2] << 8);
				command_ptr += 3;
			} else if (command == 0xfc) {
				run_length_minus_one = command_ptr[1];
				command_ptr += 2;
				do {
					sample->red = 0x80;
					sample->green = 0x80;
					sample->blue = 0x80;
					sample->palette_index = 0;
					sample->length_minus_one = 0;
					++sample;
				} while (run_length_minus_one-- != 0);
			} else {
				if (command == 0xfd) {
					run_length_minus_one = command_ptr[1];
					palette_index = command_ptr[2];
					command_ptr += 3;
				} else {
					run_length_minus_one = command;
					palette_index =
						(uint8_t)(run_length_minus_one >>
							  g_flight_sw_rle_palette_shift_by_packing_mode
								  [packing_mode]) +
						palette_base;
					++command_ptr;
					run_length_minus_one =
						g_flight_sw_rle_run_length_mask_by_packing_mode
							[packing_mode];
					run_length_minus_one &= command;
				}
				/* From here paletteIndex is a byte offset into palette_rgba, four bytes per entry. */
				palette_index *= 4;
				do {
					sample->red =
						palette_rgba[palette_index];
					sample->green =
						palette_rgba[palette_index + 1];
					sample->blue =
						palette_rgba[palette_index + 2];
					sample->palette_index = 0;
					sample->length_minus_one = 0;
					++sample;
				} while (run_length_minus_one-- != 0);
			}
		}
		++command_ptr;
	}
	image_quantizer_compress_pixel_runs((unsigned int *)image);
	image_quantizer_classify_image_colors((unsigned int *)image);
	image_quantizer_destroy_image(image);
}

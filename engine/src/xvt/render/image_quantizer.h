#ifndef XVT_RENDER_IMAGE_QUANTIZER_H
#define XVT_RENDER_IMAGE_QUANTIZER_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct image_quantizer_palette_entry {
	uint8_t red;	     /* Red, the rounded mean of a node's pixels. */
	uint8_t green;	     /* Green, the same way. */
	uint8_t blue;	     /* Blue, the same way. */
	uint8_t reserved[6]; /* Nothing reads or writes it by name. */
};

struct image_quantizer_node {
	/* Which child of its parent it is: the color's red, green and blue bits
	 * at its level as 4, 2 and 1. */
	uint8_t child_index;
	uint8_t level; /* Depth in the tree, 0 for the root. */
	/* The (1 << i) bit is set while child i is in the tree. */
	uint8_t children_mask;
	/* Middle of the node's red range; 0x80 at the root. */
	uint8_t midpoint_red;
	uint8_t midpoint_green;	    /* Middle of the node's green range. */
	uint8_t midpoint_blue;	    /* Middle of the node's blue range. */
	unsigned int palette_index; /* Palette entry built from this node. */
	/* Pixels classified to this node, merged children's included. */
	unsigned int pixel_count;
	/* Sum, over the pixels passing through the node, of their squared
	 * distance to its midpoint; the root gets 196608 per pixel instead. */
	double quantization_error;
	double red_sum;	  /* Sum of the red of the node's pixels. */
	double green_sum; /* Sum of the green of the node's pixels. */
	double blue_sum;  /* Sum of the blue of the node's pixels. */
	/* Parent node; the root's parent is itself. */
	struct image_quantizer_node *parent;
	/* Children by child_index, NULL where none was made. */
	struct image_quantizer_node *children[8];
};

#pragma pack(pop)
typedef char xvt_size_image_quantizer_palette_entry
	[(sizeof(struct image_quantizer_palette_entry) == 9) ? 1 : -1];
#if defined(_MSC_VER) && !defined(XVT_MODERN)
typedef char xvt_size_image_quantizer_node
	[(sizeof(struct image_quantizer_node) == 82) ? 1 : -1];
#else
typedef char xvt_size_image_quantizer_node
	[(sizeof(struct image_quantizer_node) == 118) ? 1 : -1];
#endif

extern unsigned int g_image_quantizer_node_count;
extern const double g_image_quantizer_max_squared_rgb_error_per_pixel;

void image_quantizer_report_progress(const char *stage, unsigned int completed,
				     unsigned int total);
void image_quantizer_fatal_allocation_error(const char *context,
					    const char *message);
void *image_quantizer_allocate_image(void);
void image_quantizer_compress_pixel_runs(unsigned int *image);
void image_quantizer_destroy_image(void *image);
int image_quantizer_expand_pixel_runs(uint32_t *image);
void image_quantizer_quantize_image(unsigned int *image,
				    unsigned int palette_size, int tree_depth,
				    int dither, int colorspace);
unsigned int image_quantizer_assign_palette_colors(uint32_t *image,
						   unsigned int palette_size,
						   int dither, int colorspace);
void image_quantizer_classify_image_colors(unsigned int *image);
void image_quantizer_find_nearest_palette_entry_recursive(
	struct image_quantizer_node *node);
void image_quantizer_build_palette_entries_recursive(
	struct image_quantizer_node *node);
int image_quantizer_dither_image_to_palette(uint32_t *image);
int image_quantizer_initialize_color_tree(int tree_depth);
struct image_quantizer_node *image_quantizer_allocate_node(
	int child_index, int level, struct image_quantizer_node *parent,
	int midpoint_red, int midpoint_green, int midpoint_blue);
void image_quantizer_collapse_deepest_level_recursive(
	struct image_quantizer_node *node);
unsigned int
image_quantizer_merge_node_into_parent(struct image_quantizer_node *node);
void image_quantizer_reduce_color_tree(unsigned int target_color_count);
void image_quantizer_reduce_color_tree_pass_recursive(
	struct image_quantizer_node *node);
void image_quantizer_quantize_image_lists(unsigned int **image_list_heads,
					  unsigned int list_count,
					  unsigned int palette_size,
					  int tree_depth, int dither,
					  int colorspace);
int image_quantizer_begin_palette_collection(int target_color_count,
					     int tree_depth);
void image_quantizer_export_palette6_bit_and_destroy(int color_count,
						     int tree_depth,
						     uint8_t *palette_rgb);
void image_quantizer_classify_indexed_rgb565_image(
	const uint8_t *indexed_pixels, const uint16_t *palette16,
	unsigned int width, unsigned int height);
void image_quantizer_classify_encoded_tex_level_image(
	const uint8_t *encoded_image, const uint8_t *palette_rgba,
	unsigned int width, unsigned int height, int packing_mode);

#ifdef __cplusplus
}
#endif

#endif

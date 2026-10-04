#ifndef XVT_RENDER_COLOR_H
#define XVT_RENDER_COLOR_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct rgb_triplet {
	uint8_t r; /* Red; 0 to 63 in the flight palette g_sw_palette. */
	uint8_t g; /* Green, on the same scale. */
	uint8_t b; /* Blue, on the same scale. */
};

typedef enum std_color_mode {
	STDCOLOR_PAL = 0x0,
	STDCOLOR_RGB = 0x1,
	STDCOLOR_RGBA = 0x2,
} std_color_mode;

struct color_info {
	std_color_mode color_mode; /* Paletted, RGB, or RGB with alpha. */
	int bpp;		   /* Bits per pixel. */
	int red_bpp;		   /* Bits of red. */
	int green_bpp;		   /* Bits of green. */
	int blue_bpp;		   /* Bits of blue. */
	int red_pos_shift;	   /* Bit position of red's lowest bit. */
	int green_pos_shift;	   /* Bit position of green's lowest bit. */
	int blue_pos_shift;	   /* Bit position of blue's lowest bit. */
	/* Right shift taking an 8-bit red down to red_bpp bits. */
	int red_pos_shift_right;
	/* Right shift taking an 8-bit green down to green_bpp bits. */
	int green_pos_shift_right;
	/* Right shift taking an 8-bit blue down to blue_bpp bits. */
	int blue_pos_shift_right;
	int alpha_bpp;	     /* Bits of alpha, 0 for none. */
	int alpha_pos_shift; /* Bit position of alpha's lowest bit. */
	/* Right shift taking an 8-bit alpha down to alpha_bpp bits. */
	int alpha_pos_shift_right;
};

unsigned int color_find_nearest_rgb_triplet_index(const uint8_t *target_rgb,
						  const uint8_t *palette,
						  unsigned int start_index,
						  unsigned int end_index);
void color_build_rgb565_to_palette_index_lut(uint8_t *out_table,
					     unsigned int start_index,
					     unsigned int end_index);
uint8_t color_find_nearest_rgb565_index(const uint16_t *palette, int target_red,
					int target_green, int target_blue,
					int start_index, int end_index);

#ifdef __cplusplus
}
#endif

#endif

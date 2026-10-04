#ifndef XVT_RENDER_FLIGHT_PALETTE_H
#define XVT_RENDER_FLIGHT_PALETTE_H

#include <stdint.h>

#include "xvt/render/color.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flight_bytes_per_pixel;
extern int g_flight_brightness_scale_q8;
extern uint16_t g_flight_palette16_bpp[256];
extern struct rgb_triplet g_sw_palette[256];
extern uint8_t g_palette_dirty_flags;

void flight_palette_build_rgb_range(const struct rgb_triplet *src_rgb,
				    struct rgb_triplet *dst_rgb,
				    int start_index, int count);
void flight_palette_apply_to_display(void);
void flight_palette_set_range(struct rgb_triplet *rgb_triples,
			      int16_t start_idx, uint16_t count);
void flight_palette_get_full(struct rgb_triplet *dst_palette);
void flight_palette_set_full(struct rgb_triplet *rgb_triples);
void flight_palette_reset_if8_bit(void);
int16_t flight_palette_build16_bpp_range(struct rgb_triplet *src_rgb,
					 uint16_t *dst16, int start_index,
					 int count);

#ifdef __cplusplus
}
#endif

#endif

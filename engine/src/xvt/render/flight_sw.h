#ifndef XVT_RENDER_FLIGHT_SW_H
#define XVT_RENDER_FLIGHT_SW_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { FLIGHT_SW_16BPP_BYTES_PER_PIXEL = 2 };

typedef enum flight_resolution_mode {
	FLIGHT_RESOLUTION_320X240 = 0x13,
	FLIGHT_RESOLUTION_640X480 = 0x101,
	FLIGHT_RESOLUTION_640X480_16BPP = 0x111,
	FLIGHT_RESOLUTION_480X360 = 0x112,
} flight_resolution_mode;

extern int16_t g_flight_sw_rot_sprite_output_offset_y;
extern int16_t g_flight_sw_rot_sprite_output_offset_x;
extern int16_t g_flight_sw_rot_sprite_input_corner_x;
extern int16_t g_flight_sw_rot_sprite_input_corner_y;
extern int16_t g_flight_sw_rot_sprite_edge_cursor_x;
extern int16_t g_flight_sw_rot_sprite_edge_cursor_y;
extern int g_flight_resolution_mode;
extern unsigned int g_vesa_page_size_bytes;
extern unsigned int g_vesa_grains_per_page;
extern uint8_t *g_flight_sw_framebuffer_base;
extern int g_flight_viewport_inset_x;
extern int g_starfield_grid_dimension;
extern int g_starfield_colors8_initialized;
extern int g_starfield_colors16_initialized;
extern int g_starfield_random_vector_indices_initialized;
extern uint16_t g_starfield_colors8_handle;
extern uint16_t g_starfield_colors16_handle;
extern uint16_t g_starfield_random_vector_indices_handle;
extern int32_t g_starfield_jitter_x[125];
extern int32_t g_starfield_jitter_y[125];
extern int32_t g_starfield_jitter_z[125];
extern uint8_t g_flight_background_color_index;
#ifndef XVT_MODERN
extern unsigned int g_vesa_window;
#endif
extern uint8_t g_flight_sw_rle_run_length_mask_by_packing_mode[9];
extern uint8_t g_flight_sw_rle_palette_shift_by_packing_mode[9];
extern int g_flight_sw_rot_sprite_span_runs_enabled;
extern uint16_t g_flight_sw_rot_sprite_saved_clip_min_x;
extern uint16_t g_flight_sw_rot_sprite_saved_clip_max_x;
extern int16_t g_flight_sw_rot_sprite_dest_y_mode;
extern int g_flight_sw_rot_sprite_dest_pitch_bytes;
extern uint16_t g_flight_sw_rot_sprite_saved_primary_edge_y;
extern uint16_t g_flight_sw_rot_sprite_saved_primary_edge_x;
extern uint16_t g_flight_fill_rect_bottom16bpp;
extern uint16_t g_flight_fill_rect_right16bpp;
extern uint16_t g_flight_fill_rect_left16bpp;
extern uint16_t g_flight_fill_rect_top16bpp;
extern int g_flight_fill_rect_current_y16bpp;
extern int g_flight_fill_rect_remaining_rows16bpp;
extern int32_t g_flight_fill_rect_current_y8bpp;
extern uint16_t g_flight_fill_rect_top8bpp;
extern uint16_t g_flight_fill_rect_bottom8bpp;
extern uint16_t g_flight_fill_rect_right8bpp;
extern uint16_t g_flight_fill_rect_left8bpp;
extern unsigned int g_flight_fill_rect_remaining_rows8bpp;

/* One point of a rotated sprite's edge, in pixels from the edge's start. */
struct flight_sw_rot_sprite_edge_point {
	int16_t x; /* Steps along x. */
	int16_t y; /* Steps along y. */
};

/* The tables flight_sw_build_sprite_rotation_coeffs makes for one rotation angle,
 * with which the software renderer draws rotated sprites. The first five
 * fields are also read as an array of five 16-bit values by
 * flight_sw_rotate_sprite_point. */
struct flight_sw_rot_sprite_coeff_state {
	/* The angle, 65,536 units to the circle. */
	uint16_t rotation_angle;
	/* The sine's magnitude for the angle folded into a quarter turn, as
	 * flight_sw_lookup_sprite_sine_magnitude_q16 returns it (65536 for 1). */
	int16_t sin_magnitude_q16;
	/* The 0x8000 bit of the angle: set when the sine is negative. */
	uint16_t sin_sign_mask;
	/* The cosine's magnitude, in the same form as sin_magnitude_q16. */
	int16_t cos_magnitude_q16;
	/* The 0x8000 bit of the angle plus 0x4000: set when the cosine is
	 * negative. */
	uint16_t cos_sign_mask;
	/* The cosine's magnitude for the edge's angle (the folded angle, or a
	 * quarter turn less it with primary_axis_swap), in the same form. */
	uint16_t primary_cos_magnitude_q16;
	/* 0x80000000 / primary_cos_magnitude_q16, rescaled by g_proj_aspect_y /
	 * 65536 with primary_axis_swap when pixels are not square; nothing reads
	 * it. */
	uint16_t primary_step_reciprocal;
	/* Edge points: g_flight_sw_rot_sprite_viewport_width without
	 * primary_axis_swap, g_flight_sw_rot_sprite_viewport_height with it. */
	uint16_t scan_count;
	uint16_t flip_y; /* 1 when the angle is 0x8000 or more, else 0. */
	/* 2 when the angle is from 0x4000 up to 0xC000, else 0. */
	uint16_t flip_x;
	uint16_t flip_count; /* (flipX >> 1) + flipY: 0, 1 or 2. */
	/* primary_axis_swap | flipY | flipX, 0 to 7: picks the octant functions
	 * that walk the sprite. */
	uint16_t octant;
	/* 4 when the folded angle is g_flight_sw_rot_sprite_axis_swap_threshold_angle
	 * or more, so the edge steps along y; else 0. */
	uint16_t primary_axis_swap;
	/* The same test for the angle a quarter turn on. */
	uint16_t secondary_axis_swap;
	/* What flight_sw_advance_rot_sprite_secondary_scale adds to
	 * g_flight_sw_rot_sprite_secondary_scale_accum each step. */
	uint16_t secondary_scale_low;
	/* Nonzero: each step moves the span base by 1, or 2 on a carry from
	 * that sum. 0: only a carry moves it, by 1. */
	uint16_t secondary_scale_high;
	int16_t first_edge_x; /* x of the edge's first point. */
	int16_t last_edge_x;  /* x of the edge's last point. */
	/* g_flight_sw_rot_sprite_viewport_max_y less the first point's y. */
	int16_t first_edge_screen_y;
	/* g_flight_sw_rot_sprite_viewport_max_y less the last point's y. */
	int16_t last_edge_screen_y;
	int16_t first_edge_y; /* y of the edge's first point. */
	int16_t last_edge_y;  /* y of the edge's last point. */
	/* Entry 0: how far the last point lies from the first on x and on y,
	 * as magnitudes. Entries 1 to scan_count: the edge's points from (0,
	 * 0), one step along the main axis each and the cross axis following
	 * the edge's slope. */
	struct flight_sw_rot_sprite_edge_point
		edge_points_with_predecessor[1601];
	/* A step factor in 256ths that flight_sw_prepare_rotated_sprite_scale_state
	 * folds into the vertical step. */
	uint16_t secondary_step_byte;
	uint16_t run_length_count; /* Entries in run_lengths. */
	/* Lengths of the runs of edge points that share a cross-axis
	 * coordinate. */
	uint16_t run_lengths[1600];
	/* Where the walk's first line starts in the destination buffer. */
	void *dest_line_ptr;
	/* Byte offset of each edge point from a line's start, flips applied:
	 * x times bytes per pixel, plus y times
	 * g_flight_sw_rot_sprite_dest_pitch_bytes when g_flight_sw_rot_sprite_dest_y_mode
	 * is positive, else minus it. */
	int span_offsets[1600];
	/* g_flight_sw_rot_sprite_dest_pitch_bytes, negated when
	 * g_flight_sw_rot_sprite_dest_y_mode is positive. */
	int dest_pitch_delta;
};

/* One pixel of the radar target marker, relative to the marker's point. */
struct flight_radar_marker_offset {
	int8_t x; /* Columns right. */
	int8_t y; /* Rows down. */
};

/* One pixel of the cross marker, relative to its center. */
struct flight_sw_marker_offset {
	int8_t dx; /* Columns right. */
	int8_t dy; /* Rows down. */
};

/* The scale of a rotated sprite on screen, from
 * flight_sw_prepare_rotated_sprite_scale_state. Steps are in 256ths of a pixel
 * per texel, split into bytes. */
struct flight_sw_rot_sprite_scale_state {
	/* The sprite's screen size, 256 for one pixel per texel. */
	uint16_t screen_scale;
	/* Horizontal step the step tables were last built for, low byte. */
	uint8_t cached_step_low_byte;
	uint8_t cached_step_high_byte; /* Its high byte. */
	/* (screen_scale * primary_cos_magnitude_q16) >> 16, times aspect_scale_y >> 8
	 * with primary_axis_swap: low byte. */
	uint8_t horizontal_step_low_byte;
	uint8_t horizontal_step_high_byte; /* Its high byte. */
	/* Destination lines per sprite row: the horizontal base step plus its
	 * secondary_step_byte 256ths, times inverse_aspect_scale_y >> 8 without
	 * secondary_axis_swap; low byte. */
	uint8_t vertical_step_low_byte;
	uint8_t vertical_step_high_byte; /* Its high byte. */
	/* Entry n: n + 1 horizontal steps, fraction part, in 65536ths of a
	 * pixel. */
	uint16_t low_word_step_table[256];
	/* Entry n: n + 1 horizontal steps, whole pixels. */
	uint16_t high_word_step_table[256];
	/* 256 with square pixels, else 233: y scale in 256ths. */
	uint16_t aspect_scale_y;
	/* 256 with square pixels, else 282. */
	uint16_t inverse_aspect_scale_y;
};

/* One run of a rotated sprite's row, scaled, ready to draw along the edge. */
struct flight_sw_rot_sprite_span_run {
	/* Edge position of its first pixel, before g_flight_sw_rot_sprite_span_base_x
	 * is added. */
	int start_x;
	/* Sprite color index, drawn through the sprite palette. */
	int color_index;
	int length; /* Pixels. */
};

extern struct flight_sw_rot_sprite_span_run
	g_flight_sw_rot_sprite_span_runs[512];
extern struct flight_sw_rot_sprite_scale_state
	g_flight_sw_rot_sprite_scale_state;
extern uint8_t *g_flight_sw_rot_sprite_dest_line_ptr;
extern int16_t g_flight_sw_rot_sprite_skip_secondary_scale_step;
extern uint16_t g_flight_sw_rot_sprite_secondary_scale_accum;
extern int16_t g_flight_sw_rot_sprite_clip_min_x;
extern int16_t g_flight_sw_rot_sprite_span_base_x;
extern int16_t g_flight_sw_rot_sprite_primary_edge_x;
extern int16_t g_flight_sw_rot_sprite_primary_edge_y;
extern int16_t g_flight_sw_rot_sprite_viewport_max_y;
extern int16_t g_flight_sw_rot_sprite_clip_max_x;
extern struct flight_sw_rot_sprite_coeff_state *g_flight_sw_rot_sprite_coeffs;
extern int g_flight_sw_rot_sprite_span_run_countdown;

/* sprite_payload describes the same 44-byte image header as tex_level_image_header, under different field
 * names; some code reads one image through both. */
struct sprite_payload {
	uint32_t payload_size; /* Never read or written through this name. */
	/* Never read or written through this name; tex_level_image_header calls
	 * it palette_offset. */
	uint32_t color_table24_offset;
	/* Bytes from the header to the encoded image: a
	 * flight_sw_rot_sprite_data_header, then the rows. */
	uint32_t row_data_offset;
	/* Bytes from the header to the drawing palette: one byte per color for
	 * 8-bit drawing, a low and a high byte for 16-bit. */
	uint32_t display_palette_offset;
	uint32_t width;	  /* Texels per row. */
	uint32_t height;  /* Rows. */
	int32_t anchor_x; /* Never read or written through this name. */
	int32_t anchor_y; /* Never read or written through this name. */
	/* Bits of run length in a run byte; the bits above them hold the color
	 * index. */
	int32_t packing_mode;
	int32_t bits_per_pixel; /* Never read or written through this name. */
	int32_t color_count;	/* Colors in the drawing palette. */
};

void flight_sw_init_framebuffer(void);
void flight_sw_set_render_target(void *surface, int width, unsigned int height,
				 int pitch_bytes);
int flight_sw_get_line_offset(int line);
int flight_sw_get_line_pitch(void);
int flight_sw_compute_pixel_offset8bpp(int x, int y);
void flight_sw_blit_sprite_rle8bpp(uint8_t *rle_data, int x, int y,
				   int transparent_color_index, int mirror);
void flight_sw_blit_sprite_rle_faded8bpp(uint8_t *rle_data, int x, int y,
					 int transparent_color_index,
					 int8_t palette_shift,
					 int16_t fade_amount);
void flight_sw_blit_sprite_rle_impl8bpp(uint8_t *rle_data, int x, int y,
					int transparent_color_index, int mirror,
					char is_faded, int16_t fade_amount);
void flight_sw_blit_map_icon_rle(uint8_t *rle_data, int x, int y,
				 int transparent_index, int mirror);
void flight_sw_draw_pixel8bpp(uint16_t x, uint16_t y, int8_t color_index);
void flight_sw_fill_clip_rect8bpp(void);
void flight_sw_fill_rect_or_border8bpp(uint16_t border_thickness);
void flight_sw_fill_rect_clipped8bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				     uint16_t y2, uint16_t border_thickness);
void flight_sw_save_screen_rect8bpp(uint8_t *buffer, int x, int y,
				    int16_t width, int height);
void flight_sw_restore_screen_rect8bpp(uint8_t *buffer, int x, int y,
				       int16_t width, int height);
void flight_sw_draw_point_array8bpp(uint16_t *points, int16_t count);
void flight_sw_erase_point_array8bpp(uint16_t *points, int16_t count);
void flight_sw_draw_radar_target_marker8bpp(void);
void flight_sw_restore_radar_target_marker8bpp(void);
uint8_t flight_sw_draw_cross_marker8bpp(uint16_t x, uint16_t y, uint8_t color);
uint8_t flight_sw_restore_cross_marker8bpp(uint16_t x, uint16_t y);
void flight_starfield_render(void);
void rts_vga2_set_current_page(uint8_t window, uint16_t page);
void flight_screenshot_capture(void);
void flight_sw_draw_line8bpp(int x1, int y1, int x2, int y2, uint8_t color_idx);
void flight_sw_draw_rot_sprite_span_runs8(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets);
void flight_sw_draw_clipped_rot_sprite_span_runs8(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets);
void flight_sw_draw_rot_sprite_span_runs16(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets);
void flight_sw_draw_clipped_rot_sprite_span_runs16(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets);
void flight_sw_draw_rotated_sprite_quad(int16_t screen_x, int16_t screen_y,
					uint16_t screen_size,
					struct sprite_payload *sprite);
void flight_sw_clip_and_blit_prepared_rotated_sprite(int *corner_coords);
void flight_sw_prepare_sprite_rotation_tables(int16_t rotation_angle,
					      int bytes_per_pixel);
int flight_sw_load_sprite_palette_tables(struct sprite_payload *sprite);
uint16_t flight_sw_lookup_scaled_tangent(uint16_t angle, int16_t scale_percent);
void flight_sw_prepare_rotated_sprite_scale_state(
	uint16_t screen_size,
	const struct flight_sw_rot_sprite_coeff_state *rotation_coeffs,
	struct flight_sw_rot_sprite_scale_state *scale_state);
void flight_sw_rotate_sprite_point(
	const uint16_t *rotation_coeffs,
	const struct flight_sw_rot_sprite_scale_state *scale_state);
void flight_sw_build_sprite_rotation_coeffs(uint16_t rotation_angle,
					    uint16_t *out_coeffs);
void flight_sw_rasterize_prepared_rotated_sprite(uint8_t *sprite_data,
						 int packing_mode);
void flight_sw_advance_rot_sprite_secondary_scale(void);
int flight_sw_init_rot_sprite_for_current_octant(void);
int flight_sw_step_rot_sprite_for_current_octant(void);
int flight_sw_init_rot_sprite_octant0(void);
int flight_sw_step_rot_sprite_octant0(void);
int flight_sw_init_rot_sprite_octant1(void);
int flight_sw_step_rot_sprite_octant1(void);
int flight_sw_init_rot_sprite_octant2(void);
int flight_sw_step_rot_sprite_octant2(void);
int flight_sw_init_rot_sprite_octant3(void);
int flight_sw_step_rot_sprite_octant3(void);
int flight_sw_init_rot_sprite_octant4(void);
int flight_sw_step_rot_sprite_octant4(void);
int flight_sw_init_rot_sprite_octant5(void);
int flight_sw_step_rot_sprite_octant5(void);
int flight_sw_init_rot_sprite_octant6(void);
int flight_sw_step_rot_sprite_octant6(void);
int flight_sw_init_rot_sprite_octant7(void);
int flight_sw_step_rot_sprite_octant7(void);
uint8_t *flight_sw_set_rotated_sprite_dest_buffer(uint8_t *buffer_address);
unsigned int set_flight_viewport(unsigned int requested_width,
				 unsigned int requested_height,
				 int viewport_mode,
				 unsigned int requested_base_offset);
void flight_sw_copy_legacy8_bit_viewport_to_framebuffer(
	const uint8_t *src_pixels);
unsigned int push_flight_viewport(uint16_t width, uint16_t height,
				  int16_t refresh_span_mask,
				  unsigned int base_offset);
int pop_flight_viewport(void);
void flight_sw_blit_rect_to_flight_surface(
	uint8_t *source_base, uint16_t transparent_color_index,
	uint16_t source_x, uint16_t source_y, uint16_t destination_x,
	uint16_t destination_y, uint16_t width_pixels, uint16_t height_pixels,
	uint16_t source_pitch);
void flight_sw_copy_framebuffer_rect_to_buffer(uint8_t *dst_pixels,
					       uint16_t src_x, uint16_t src_y,
					       uint16_t dst_x, uint16_t dst_y,
					       uint16_t width_pixels,
					       uint16_t height_pixels,
					       uint16_t dst_pitch_bytes);
void flight_sw_draw_horizontal_color_span(int x_start, int x_end, int y,
					  uint8_t color_index);
void flight_sw_copy_viewport_span_mask_rle(const uint8_t *encoded_mask,
					   uint16_t width, uint16_t height,
					   int16_t mirror_horizontal);
extern uint8_t *g_flight_aux_buffer;
extern uint16_t g_viewport_span_mask_offset;
extern int g_flight_sw_rot_sprite_coeff_cache_valid;

void flight_sw_build_full_viewport_span_mask_rle(uint16_t width,
						 unsigned int height);
int32_t flight_sw_compute_pixel_offset(int x, int y);
void flight_sw_blit_sprite_rle16bpp(uint8_t *rle_data, int x, int y,
				    int transparent_color_index, int mirror);
void flight_sw_blit_sprite_rle_faded16bpp(uint8_t *rle_data, int x, int y,
					  int transparent_color_index,
					  int8_t palette_shift,
					  int16_t fade_amount);
void flight_sw_blit_sprite_rle_impl16bpp(uint8_t *rle_data, int16_t x,
					 int16_t y, int transparent_color_index,
					 int mirror, char is_faded,
					 int16_t fade_amount);
void flight_sw_blit_map_icon_rle16bpp(uint8_t *rle_data, int x, int y,
				      int transparent_index, int mirror);
void flight_sw_draw_pixel16bpp(uint16_t x, uint16_t y, int8_t color_index);
void flight_sw_fill_clip_rect16bpp(void);
void flight_sw_fill_rect_or_border16bpp(uint16_t border_thickness);
void flight_sw_fill_rect_clipped16bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				      uint16_t y2, uint16_t border_thickness);
void flight_sw_save_screen_rect16bpp(uint16_t *buffer, int x, int y,
				     int16_t width, int height);
void flight_sw_restore_screen_rect16bpp(uint16_t *buffer, int x, int y,
					int16_t width, int height);
void flight_sw_draw_point_array16bpp(uint16_t *points, int16_t count);
void flight_sw_erase_point_array16bpp(uint16_t *points, int16_t count);
void flight_sw_draw_radar_target_marker16bpp(void);
void flight_sw_restore_radar_target_marker16bpp(void);
uint16_t flight_sw_draw_cross_marker16bpp(uint16_t x, uint16_t y,
					  uint8_t color_index);
uint16_t flight_sw_restore_cross_marker16bpp(uint16_t x, uint16_t y);
void flight_sw_draw_line16bpp(int x1, int y1, int x2, int y2,
			      uint8_t color_idx);
int16_t flight_sw_lookup_sprite_sine_magnitude_q16(int16_t angle);
void flight_sw_blit_prepared_rotated_sprite_spans(uint8_t *p_dst,
						  int row_skip_bytes,
						  int start_x, int start_y,
						  int end_x, int end_y);

#ifdef __cplusplus
}
#endif

#endif

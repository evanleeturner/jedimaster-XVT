#include "xvt/render/flight_sw.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_camera.h"
#endif

#include "xvt/assets/file.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/front_image.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/compat/framebuffer_address.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Stars on each side of each of the starfield's three grids: 32 /
 * g_star_grid_divisor, set by flight_starfield_render on each call. */
// GLOBAL: XVT 0x51A830
int g_starfield_grid_dimension = 0;
/* 1 once flight_starfield_render has made the 8-bit star colors; nothing sets it
 * back to 0. */
// GLOBAL: XVT 0x51A834
int g_starfield_colors8_initialized = 0;
/* 1 once flight_starfield_render has made the 16-bit star colors; nothing sets
 * it back to 0. */
// GLOBAL: XVT 0x51A838
int g_starfield_colors16_initialized = 0;
/* 1 once flight_starfield_render has picked each star's jitter vector; nothing
 * sets it back to 0. */
// GLOBAL: XVT 0x51A83C
int g_starfield_random_vector_indices_initialized = 0;
/* Memory handle of the 3072 star colors as palette indices, made by
 * flight_starfield_render. */
// GLOBAL: XVT 0x51A840
uint16_t g_starfield_colors8_handle = 0;
/* Memory handle of the 3072 star colors as 16-bit colors, made by
 * flight_starfield_render. */
// GLOBAL: XVT 0x51A844
uint16_t g_starfield_colors16_handle = 0;
/* Memory handle of 3 * 3072 bytes, of which flight_starfield_render fills the
 * first 3072 with each star's jitter vector, 0 to 124. */
// GLOBAL: XVT 0x51A848
uint16_t g_starfield_random_vector_indices_handle = 0;
/* x of the 125 star jitter vectors: backdrop_build_star_offsets_and_render fills
 * it, and flight_starfield_render adds one vector to each star. */
// GLOBAL: XVT 0x9A7810
int32_t g_starfield_jitter_x[125] = {0};
/* y of the 125 star jitter vectors, filled and read as g_starfield_jitter_x
 * is. */
// GLOBAL: XVT 0x9A7400
int32_t g_starfield_jitter_y[125] = {0};
/* z of the 125 star jitter vectors, filled and read as g_starfield_jitter_x
 * is. */
// GLOBAL: XVT 0x9A7600
int32_t g_starfield_jitter_z[125] = {0};
/* Palette index of empty sky: flight_starfield_render draws a star only over a
 * pixel of this color (its 16-bit color at 16 bits). Starts at 0xFB; set by
 * fe_disk_io_init_resources, flight_view_render and hud_update3d_crt. */
// GLOBAL: XVT 0x523408
uint8_t g_flight_background_color_index = 0xFB;

/* The viewport and camera matrix push_flight_viewport saves, which
 * pop_flight_viewport puts back. */
struct flight_viewport_save_state {
	uint16_t viewport_x;  /* g_flight_vp_x. */
	uint16_t pad02;	      /* Never read or written. */
	uint16_t viewport_y;  /* g_flight_vp_y. */
	uint16_t pad06;	      /* Never read or written. */
	int cam_mat_r0_x;     /* g_cam_mat_r0_x. */
	int cam_mat_r1_x;     /* g_cam_mat_r1_x. */
	int cam_mat_r0_y;     /* g_cam_mat_r0_y. */
	int cam_mat_r1_y;     /* g_cam_mat_r1_y. */
	int cam_mat_r2_x;     /* g_cam_mat_r2_x. */
	int cam_mat_r0_z;     /* g_cam_mat_r0_z. */
	int cam_mat_r1_z;     /* g_cam_mat_r1_z. */
	int cam_mat_r2_y;     /* g_cam_mat_r2_y. */
	int cam_mat_r2_z;     /* g_cam_mat_r2_z. */
	uint16_t base_offset; /* g_flight_vp_base_offset, cut to 16 bits. */
	uint16_t pad2e;	      /* Never read or written. */
	uint16_t height;      /* g_flight_vp_height. */
	uint16_t pad32;	      /* Never read or written. */
	uint16_t width;	      /* g_flight_vp_width. */
	uint16_t pad36;	      /* Never read or written. */
};

typedef char xvt_size_flight_viewport_save_state
	[(sizeof(struct flight_viewport_save_state) == 56) ? 1 : -1];

/* The start of a rotated sprite's encoded image: where its corner lies
 * relative to its screen point, in texels. */
struct flight_sw_rot_sprite_data_header {
	/* x of the first corner, used while g_flight_sw_rot_sprite_span_runs_enabled
	 * is 1, which it always is. */
	int32_t corner_x;
	int32_t corner_y; /* y of the first corner, used negated. */
	/* x used, negated, when that flag is not 1, which never happens. */
	int32_t alternate_corner_x;
	int32_t unused0c; /* Never read or written. */
};

/* The viewport and camera matrix push_flight_viewport saves and pop_flight_viewport
 * puts back. */
// GLOBAL: XVT 0x555C88
static struct flight_viewport_save_state g_saved_flight_viewport = {0};

/* Entry i is tan(i * pi / 512) * 65536 / 1.1, rounded, up to 65535 at entry
 * 136; the last three are 0. Read by flight_sw_lookup_scaled_tangent for 91
 * percent. */
// GLOBAL: XVT 0x51C018
static uint16_t g_flight_sw_tangent91_pct[140] = {
	0,     366,   731,   1097,  1463,  1828,  2194,	 2561,	2927,  3293,
	3660,  4027,  4395,  4762,  5131,  5499,  5868,	 6237,	6607,  6977,
	7348,  7720,  8092,  8464,  8838,  9212,  9586,	 9962,	10338, 10715,
	11093, 11471, 11851, 12231, 12613, 12995, 13379, 13763, 14149, 14536,
	14924, 15313, 15703, 16095, 16488, 16882, 17277, 17674, 18073, 18473,
	18874, 19277, 19682, 20088, 20496, 20906, 21317, 21731, 22146, 22563,
	22982, 23403, 23826, 24251, 24678, 25107, 25539, 25973, 26409, 26848,
	27289, 27732, 28178, 28627, 29078, 29532, 29989, 30449, 30911, 31377,
	31845, 32317, 32791, 33269, 33751, 34235, 34723, 35215, 35710, 36209,
	36711, 37217, 37727, 38242, 38760, 39282, 39809, 40340, 40875, 41415,
	41960, 42509, 43063, 43622, 44186, 44755, 45330, 45910, 46495, 47086,
	47683, 48286, 48895, 49509, 50131, 50758, 51392, 52033, 52681, 53336,
	53999, 54668, 55345, 56030, 56723, 57424, 58134, 58852, 59578, 60314,
	61059, 61813, 62577, 63351, 64135, 64929, 65535, 0,	0,     0,
};
/* Only the first 8 entries of g_flight_sw_tangent100_pct are 100% values; from entry 8 on it repeats
 * g_flight_sw_tangent91_pct, as the original data does. */
/* Entries 0 to 7 are tan(i * pi / 512) * 65536, rounded. Read by
 * flight_sw_lookup_scaled_tangent for any percentage but 91 and 110. */
// GLOBAL: XVT 0x51C130
static uint16_t g_flight_sw_tangent100_pct[140] = {
	0,     402,   804,   1206,  1608,  2011,  2414,	 2817,	2927,  3293,
	3660,  4027,  4395,  4762,  5131,  5499,  5868,	 6237,	6607,  6977,
	7348,  7720,  8092,  8464,  8838,  9212,  9586,	 9962,	10338, 10715,
	11093, 11471, 11851, 12231, 12613, 12995, 13379, 13763, 14149, 14536,
	14924, 15313, 15703, 16095, 16488, 16882, 17277, 17674, 18073, 18473,
	18874, 19277, 19682, 20088, 20496, 20906, 21317, 21731, 22146, 22563,
	22982, 23403, 23826, 24251, 24678, 25107, 25539, 25973, 26409, 26848,
	27289, 27732, 28178, 28627, 29078, 29532, 29989, 30449, 30911, 31377,
	31845, 32317, 32791, 33269, 33751, 34235, 34723, 35215, 35710, 36209,
	36711, 37217, 37727, 38242, 38760, 39282, 39809, 40340, 40875, 41415,
	41960, 42509, 43063, 43622, 44186, 44755, 45330, 45910, 46495, 47086,
	47683, 48286, 48895, 49509, 50131, 50758, 51392, 52033, 52681, 53336,
	53999, 54668, 55345, 56030, 56723, 57424, 58134, 58852, 59578, 60314,
	61059, 61813, 62577, 63351, 64135, 64929, 65535, 0,	0,     0,
};
/* Entry i is tan(i * pi / 512) * 65536 * 1.1, rounded, up to 65535 at entry
 * 121; the last two are 0. Read by flight_sw_lookup_scaled_tangent for 110
 * percent. */
// GLOBAL: XVT 0x51C248
static uint16_t g_flight_sw_tangent110_pct[124] = {
	0,     442,   885,   1327,  1770,  2212,  2655,	 3098,	3542,  3985,
	4429,  4873,  5318,  5763,  6208,  6654,  7100,	 7547,	7995,  8443,
	8891,  9341,  9791,  10242, 10693, 11146, 11599, 12054, 12509, 12965,
	13422, 13880, 14340, 14800, 15261, 15724, 16188, 16654, 17120, 17588,
	18058, 18528, 19001, 19474, 19950, 20427, 20906, 21386, 21868, 22352,
	22838, 23326, 23815, 24307, 24800, 25296, 25794, 26294, 26796, 27301,
	27808, 28317, 28829, 29344, 29860, 30380, 30902, 31427, 31955, 32486,
	33019, 33556, 34096, 34639, 35185, 35734, 36287, 36843, 37403, 37966,
	38533, 39103, 39678, 40256, 40838, 41425, 42015, 42610, 43209, 43812,
	44420, 45033, 45650, 46272, 46899, 47532, 48169, 48811, 49459, 50112,
	50771, 51436, 52106, 52783, 53465, 54154, 54849, 55551, 56259, 56974,
	57697, 58426, 59162, 59906, 60658, 61417, 62185, 62961, 63744, 64537,
	65338, 65535, 0,     0,
};

/* x and y of the 10 pixels of the radar target marker at 16 bits, the shape of
 * g_radar_target_marker_shape10. */
// GLOBAL: XVT 0x523910
static int8_t g_radar_target_marker_shape16bpp[20] = {
	-1, 1, -2, 1, -2, 0, -2, -1, -1, -1, 1, -1, 2, -1, 2, 0, 2, 1, 1, 1,
};
/* The 10-pixel radar target marker, then two unused 0, 0 entries. */
// GLOBAL: XVT 0x51A7E8
static struct flight_radar_marker_offset g_radar_target_marker_shape10[12] = {
	{-1, 1}, {-2, 1}, {-2, 0}, {-2, -1}, {-1, -1}, {1, -1},
	{2, -1}, {2, 0},  {2, 1},  {1, 1},   {0, 0},   {0, 0},
};
/* The 12-pixel radar target marker. */
// GLOBAL: XVT 0x51A800
static struct flight_radar_marker_offset g_radar_target_marker_shape12[12] = {
	{-1, 2}, {-2, 2}, {-2, 1}, {-2, 0}, {-2, -1}, {-1, -1},
	{1, -1}, {2, -1}, {2, 0},  {2, 1},  {2, 2},   {1, 2},
};
/* The radar target marker shape at 8 bits, set by flight_sw_init_framebuffer:
 * g_radar_target_marker_shape10 at 320x240 or in an unknown mode,
 * g_radar_target_marker_shape12 at 640x480 and 480x360. */
// GLOBAL: XVT 0x51A818
static int8_t *g_radar_target_marker_shape =
	(int8_t *)g_radar_target_marker_shape10;
/* Pixels in g_radar_target_marker_shape, 10 or 12, set with it. */
// GLOBAL: XVT 0x51A81C
static int g_radar_target_marker_point_count = 10;
/* The 7 pixels of the cross marker: 5 across and 3 down through its center. */
// GLOBAL: XVT 0x51A820
static struct flight_sw_marker_offset g_flight_sw_cross_marker_offsets[7] = {
	{-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, -1},
};
/* The same 7 pixels for 16-bit drawing. */
// GLOBAL: XVT 0x523928
static struct flight_sw_marker_offset g_flight_sw_cross_marker_offsets16bpp[7] =
	{
		{-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, -1},
};
/* (1 << mode) - 1 for packing modes 0 to 8: the run-length bits of a sprite run
 * byte. */
// GLOBAL: XVT 0x523600
uint8_t g_flight_sw_rle_run_length_mask_by_packing_mode[9] = {
	0, 1, 3, 7, 15, 31, 63, 127, 255};
/* The shift for packing modes 0 to 8, equal to the mode, that brings a run
 * byte's color index down. */
// GLOBAL: XVT 0x523610
uint8_t g_flight_sw_rle_palette_shift_by_packing_mode[9] = {0, 1, 2, 3, 4,
							    5, 6, 7, 8};
/* Starts at 1, and every writer sets 1:
 * flight_sw_rasterize_prepared_rotated_sprite, render_quad_draw_model_texture,
 * backdrop_draw_model_tex_quad_at_screen, and at flight start flight_main_loop in the
 * original build and xvt_flight_loading_globals in the modern one. So the arms of
 * flight_sw_draw_rotated_sprite_quad and flight_sw_rasterize_prepared_rotated_sprite
 * for other values never run. */
// GLOBAL: XVT 0x52361C
int g_flight_sw_rot_sprite_span_runs_enabled = 1;
/* The pixels under the radar target marker at 8 bits, saved by
 * flight_sw_draw_radar_target_marker8bpp and put back by
 * flight_sw_restore_radar_target_marker8bpp; up to 12 used. */
// GLOBAL: XVT 0x54F9C0
static uint8_t g_radar_target_marker_saved_pixels[16] = {0};
/* The pixels under the cross marker at 8 bits; the two functions that use it
 * have no caller. */
// GLOBAL: XVT 0x54F9D8
static uint8_t g_flight_sw_cross_marker_saved_pixels[7] = {0};
/* Row pitch in bytes of the target flight_sw_set_render_target was last given with
 * a pitch; g_flight_line_pitch_ptr points here while that target is in use. */
// GLOBAL: XVT 0x54F9B0
static int g_flight_alt_line_pitch = 0;
/* Byte offset of each row of that target: row times width times
 * g_flight_bytes_per_pixel, not times its pitch. */
// GLOBAL: XVT 0x54F9E0
static int g_flight_alt_line_offset_table[768] = {0};
/* The pixels under the cross marker at 16 bits; the two functions that use it
 * have no caller. */
// GLOBAL: XVT 0x5569C0
static uint16_t g_flight_sw_cross_marker_saved_pixels16bpp[7] = {0};
/* The 10 pixels under the radar target marker at 16 bits, saved by
 * flight_sw_draw_radar_target_marker16bpp and put back by
 * flight_sw_restore_radar_target_marker16bpp. */
// GLOBAL: XVT 0x556998
uint16_t g_radar_target_marker_saved_pixels16bpp[10];
/* Row after the last of the rectangle flight_sw_fill_rect_or_border16bpp fills. Set
 * by flight_sw_fill_clip_rect16bpp, flight_sw_fill_rect_clipped16bpp and
 * flight_text_clear_remaining_line_background. */
// GLOBAL: XVT 0x556988
uint16_t g_flight_fill_rect_bottom16bpp = 0;
/* Column after the last of that rectangle, set as g_flight_fill_rect_bottom16bpp
 * is. */
// GLOBAL: XVT 0x55698C
uint16_t g_flight_fill_rect_right16bpp = 0;
/* First column of that rectangle, set as g_flight_fill_rect_bottom16bpp is. */
// GLOBAL: XVT 0x556990
uint16_t g_flight_fill_rect_left16bpp = 0;
/* First row of that rectangle, set as g_flight_fill_rect_bottom16bpp is. */
// GLOBAL: XVT 0x556994
uint16_t g_flight_fill_rect_top16bpp = 0;
/* Row flight_sw_fill_rect_or_border16bpp is filling; only it writes it. */
// GLOBAL: XVT 0x5569B8
int g_flight_fill_rect_current_y16bpp = 0;
/* Rows flight_sw_fill_rect_or_border16bpp has left to fill; only it writes it. */
// GLOBAL: XVT 0x5569D0
int g_flight_fill_rect_remaining_rows16bpp = 0;
/* Row flight_sw_fill_rect_or_border8bpp is filling; only it writes it. */
// GLOBAL: XVT 0x54F9D0
int32_t g_flight_fill_rect_current_y8bpp = 0;
/* First row of the rectangle flight_sw_fill_rect_or_border8bpp fills. Set by
 * flight_sw_fill_clip_rect8bpp, flight_sw_fill_rect_clipped8bpp and
 * flight_text_clear_remaining_line_background8bpp. */
// GLOBAL: XVT 0x54F9B8
uint16_t g_flight_fill_rect_top8bpp = 0;
/* Row after the last of that rectangle, set as g_flight_fill_rect_top8bpp is. */
// GLOBAL: XVT 0x54F9A8
uint16_t g_flight_fill_rect_bottom8bpp = 0;
/* Column after the last of that rectangle, set as g_flight_fill_rect_top8bpp
 * is. */
// GLOBAL: XVT 0x54F9AC
uint16_t g_flight_fill_rect_right8bpp = 0;
/* First column of that rectangle, set as g_flight_fill_rect_top8bpp is. */
// GLOBAL: XVT 0x54F9B4
uint16_t g_flight_fill_rect_left8bpp = 0;
/* Rows flight_sw_fill_rect_or_border8bpp has left to fill; only it writes it. */
// GLOBAL: XVT 0x5505E8
unsigned int g_flight_fill_rect_remaining_rows8bpp = 0;
/* y of the point flight_sw_rotate_sprite_point last turned and scaled, in pixels
 * from the sprite's screen point; only it writes it. */
// GLOBAL: XVT 0x9A7B40
int16_t g_flight_sw_rot_sprite_output_offset_y = 0;
/* x of that point; only flight_sw_rotate_sprite_point writes it. */
// GLOBAL: XVT 0x9A7B48
int16_t g_flight_sw_rot_sprite_output_offset_x = 0;
/* x of the sprite point flight_sw_rotate_sprite_point turns, in texels;
 * flight_sw_draw_rotated_sprite_quad sets it for each corner, and
 * flight_sw_rotate_sprite_point leaves its magnitude there. */
// GLOBAL: XVT 0x9A8D4A
int16_t g_flight_sw_rot_sprite_input_corner_x = 0;
/* y of that point, set and left as g_flight_sw_rot_sprite_input_corner_x is. */
// GLOBAL: XVT 0x9A8D4E
int16_t g_flight_sw_rot_sprite_input_corner_y = 0;
/* Screen x of the sprite's first corner, set by flight_sw_draw_rotated_sprite_quad;
 * the octant set-up functions then move it along the edge. */
// GLOBAL: XVT 0x9A8D42
int16_t g_flight_sw_rot_sprite_edge_cursor_x = 0;
/* Screen y of the sprite's first corner, set and moved as
 * g_flight_sw_rot_sprite_edge_cursor_x is. */
// GLOBAL: XVT 0x9A8D48
int16_t g_flight_sw_rot_sprite_edge_cursor_y = 0;
/* Row of the radar target marker to put back: hud_draw_radar_blips copies
 * g_radar_target_marker_draw_y into it. */
// GLOBAL: XVT 0xA08C86
uint16_t g_radar_target_marker_restore_y;
/* Column of the radar target marker to put back: hud_draw_radar_blips copies
 * g_radar_target_marker_draw_x into it. */
// GLOBAL: XVT 0xA08C88
uint16_t g_radar_target_marker_restore_x;
/* Column where the radar target marker is drawn, set by hud_add_blip_to_radar for
 * the target's blip. */
// GLOBAL: XVT 0xA0A1D2
uint16_t g_radar_target_marker_draw_x = 0;
/* Row where the radar target marker is drawn, set by hud_add_blip_to_radar for the
 * target's blip. */
// GLOBAL: XVT 0xA0A1D4
uint16_t g_radar_target_marker_draw_y = 0;
/* Column of the RLE sprite being drawn, set by the four RLE blit functions. */
// GLOBAL: XVT 0x9CC452
static int16_t g_flight_sw_rle_sprite_x = 0;
/* Row of the RLE sprite being drawn: the four RLE blit functions set it and
 * raise it by 1 per row. */
// GLOBAL: XVT 0x9CC458
static int16_t g_flight_sw_rle_sprite_y = 0;
/* Added to each run color of an RLE sprite: flight_sw_blit_sprite_rle8bpp and
 * flight_sw_blit_sprite_rle16bpp set it to 0, the faded draws to their palette
 * shift, and a 0xFB code in the data sets it from the next byte, except in a
 * faded draw. */
// GLOBAL: XVT 0x9ED21D
static int8_t g_flight_sw_rle_palette_shift = 0;
/* The transparent index the RLE blit functions were last given; nothing reads
 * it. */
// GLOBAL: XVT 0x9ED23A
static uint8_t g_flight_sw_rle_transparent_color = 0;

#ifndef XVT_MODERN
/* Window number passed to rts_vga2_set_current_page, which ignores it; nothing
 * writes it, so it stays 0. */
// GLOBAL: XVT 0x5233D4
unsigned int g_vesa_window = 0;
#endif
/* The flight display mode, a flight_resolution_mode. flight_main_loop in the
 * original build and xvt_flight_loading_globals in the modern one set it from
 * g_surface_width: 320x240 for 320, 480x360 for 480, else 640x480. */
// GLOBAL: XVT 0x5233EC
int g_flight_resolution_mode = FLIGHT_RESOLUTION_640X480;
/* 1 when pixels are square (g_proj_aspect_y of 0), set by
 * flight_sw_prepare_sprite_rotation_tables: picks the 100 percent tangent table and
 * the 256 scales. */
// GLOBAL: XVT 0x5235F8
static int g_flight_sw_rot_sprite_square_pixel_mode = 0;
/* 1 once g_flight_sw_rot_sprite_coeff_cache holds tables, set by
 * flight_sw_prepare_sprite_rotation_tables. Set to 0, to have them built again, by
 * hud_update3d_crt and at flight start: by flight_main_loop in the original
 * build, xvt_flight_loading_globals in the modern one. */
// GLOBAL: XVT 0x5235F4
int g_flight_sw_rot_sprite_coeff_cache_valid = 0;
/* Nothing writes it, so it stays 0 and set_flight_viewport's inset of 160 never
 * applies. */
// GLOBAL: XVT 0x5233DC
int g_flight_viewport_inset_x = 0;
/* Where the software drawing functions write: the surface
 * flight_surface_get_software_framebuffer_base returns, or a target given to
 * flight_sw_set_render_target. Starts at 0xA0000, the VGA window. Many functions
 * write it, chiefly flight_surface_lock and flight_sw_set_render_target. */
// GLOBAL: XVT 0x5233F4
uint8_t *g_flight_sw_framebuffer_base = (uint8_t *)(uintptr_t)0xA0000;
/* Offset in g_flight_aux_buffer of the viewport span mask in use: 0xC000, or
 * 0xE000 from push_flight_viewport until pop_flight_viewport, which alone write
 * it. */
// GLOBAL: XVT 0x52747C
uint16_t g_viewport_span_mask_offset = 0xC000;
/* The row offset table flight_sw_get_line_offset reads: g_flight_line_offset_table,
 * or g_flight_alt_line_offset_table while flight_sw_set_render_target has a target
 * with a pitch. */
// GLOBAL: XVT 0x5505E0
int *g_flight_active_line_offset_table;
/* Points at the row pitch flight_sw_get_line_pitch returns: g_surface_pitch, or
 * g_flight_alt_line_pitch while flight_sw_set_render_target has a target with a
 * pitch. */
// GLOBAL: XVT 0x5505E4
int *g_flight_line_pitch_ptr;
/* A shared work buffer that holds, among other things, the viewport span masks
 * at g_viewport_span_mask_offset. Many functions write it, chiefly
 * fe_disk_io_init_global_buffers and fe_disk_io_lock_global_buffers. */
// GLOBAL: XVT 0x9A8074
uint8_t *g_flight_aux_buffer = 0;
/* Destination line the rotated-sprite walk is drawing:
 * flight_sw_prepare_sprite_rotation_tables sets it to the buffer's start,
 * flight_sw_rasterize_prepared_rotated_sprite to the first line, and each octant
 * step moves it. */
// GLOBAL: XVT 0x9A8C18
uint8_t *g_flight_sw_rot_sprite_dest_line_ptr = 0;
/* The sprite's 8-bit drawing palette from flight_sw_load_sprite_palette_tables;
 * flight_sw_blit_prepared_rotated_sprite_spans maps sprite colors through it. */
// GLOBAL: XVT 0x9A8C40
static uint8_t g_flight_sw_rot_sprite_palette8[256] = {0};
/* The runs of the sprite row being drawn, scaled, built by
 * flight_sw_rasterize_prepared_rotated_sprite. Holds 512; nothing checks the count
 * against that. */
// GLOBAL: XVT 0x9A57E0
struct flight_sw_rot_sprite_span_run g_flight_sw_rot_sprite_span_runs[512] = {
	{0}};
/* Index in run_lengths by which the octant 0 to 3 functions step
 * g_flight_sw_rot_sprite_clip_min_x. */
// GLOBAL: XVT 0x9A73F6
static int16_t g_flight_sw_rot_sprite_clip_min_run_idx03 = 0;
/* Row of the edge's far end, which the octant functions set and step. */
// GLOBAL: XVT 0x9A7BB0
static int16_t g_flight_sw_rot_sprite_secondary_edge_y = 0;
/* Column of the edge's far end, which the octant functions set and step. */
// GLOBAL: XVT 0x9A7BB2
static int16_t g_flight_sw_rot_sprite_secondary_edge_x = 0;
/* 1 makes the next flight_sw_advance_rot_sprite_secondary_scale only clear it: set
 * at a sprite's start, and by that function when the span base reaches a step
 * in the edge. */
// GLOBAL: XVT 0x9CD266
int16_t g_flight_sw_rot_sprite_skip_secondary_scale_step = 0;
/* Byte offset of each row of the software surface, row times g_surface_pitch:
 * flight_sw_init_framebuffer fills g_screen_height rows, and
 * flight_sw_set_render_target given a pitch of -1 fills the target's rows. */
// GLOBAL: XVT 0x9CC460
static int g_flight_line_offset_table[768] = {0};
/* High bytes of the sprite's 16-bit drawing colors, from
 * flight_sw_load_sprite_palette_tables. */
// GLOBAL: XVT 0x9D1160
static uint8_t g_flight_sw_rot_sprite_palette16_high[256] = {0};
/* Sum flight_sw_advance_rot_sprite_secondary_scale adds secondary_scale_low to, whose
 * carries move the span base; set to 0 at a sprite's start. */
// GLOBAL: XVT 0x9D12E0
uint16_t g_flight_sw_rot_sprite_secondary_scale_accum = 0;
/* Index in run_lengths by which the octant 0 to 3 functions step
 * g_flight_sw_rot_sprite_clip_max_x. */
// GLOBAL: XVT 0x9D1314
static int16_t g_flight_sw_rot_sprite_clip_max_run_idx03 = 0;
/* Index in run_lengths by which the octant 4 to 7 functions step
 * g_flight_sw_rot_sprite_clip_max_x. */
// GLOBAL: XVT 0x9D8C14
static int16_t g_flight_sw_rot_sprite_clip_max_run_idx47 = 0;
/* Index in run_lengths by which the octant 4 to 7 functions step
 * g_flight_sw_rot_sprite_clip_min_x. */
// GLOBAL: XVT 0x9D8C2A
static int16_t g_flight_sw_rot_sprite_clip_min_run_idx47 = 0;
/* g_flight_vp_width, copied by flight_sw_prepare_sprite_rotation_tables. */
// GLOBAL: XVT 0x9D8C2C
static int16_t g_flight_sw_rot_sprite_viewport_width = 0;
/* Low bytes of the sprite's 16-bit drawing colors, from
 * flight_sw_load_sprite_palette_tables. */
// GLOBAL: XVT 0x9D6830
static uint8_t g_flight_sw_rot_sprite_palette16_low[256] = {0};
/* The rotation tables for the last angle, built by
 * flight_sw_build_sprite_rotation_coeffs when the angle changes or
 * g_flight_sw_rot_sprite_coeff_cache_valid is 0. */
// GLOBAL: XVT 0x9CD280
static struct flight_sw_rot_sprite_coeff_state
	g_flight_sw_rot_sprite_coeff_cache = {0};
/* The buffer rotated sprites are drawn into before
 * flight_sw_blit_prepared_rotated_sprite_spans copies them out; set through
 * flight_sw_set_rotated_sprite_dest_buffer by fe_disk_io_init_global_buffers and
 * fe_disk_io_lock_global_buffers. */
// GLOBAL: XVT 0x9D77C4
static uint8_t *g_flight_sw_rot_sprite_dest_buffer = NULL;
/* g_flight_vp_max_x, copied by flight_sw_prepare_sprite_rotation_tables. */
// GLOBAL: XVT 0x9D77F0
static int16_t g_flight_sw_rot_sprite_viewport_max_x = 0;
/* Edge position from which the clipped span functions draw on the current line;
 * the octant functions set and step it. */
// GLOBAL: XVT 0x9EC458
int16_t g_flight_sw_rot_sprite_clip_min_x = 0;
/* Copy of g_flight_sw_rot_sprite_clip_min_x taken by
 * flight_sw_rasterize_prepared_rotated_sprite; nothing reads it. */
// GLOBAL: XVT 0x9E9646
uint16_t g_flight_sw_rot_sprite_saved_clip_min_x = 0;
/* Copy of g_flight_sw_rot_sprite_clip_max_x taken by
 * flight_sw_rasterize_prepared_rotated_sprite; nothing reads it. */
// GLOBAL: XVT 0x9E9650
uint16_t g_flight_sw_rot_sprite_saved_clip_max_x = 0;
/* Edge position of the current line's start: run positions are added to it to
 * index span_offsets. Set by the octant set-up functions and moved by
 * flight_sw_advance_rot_sprite_secondary_scale. */
// GLOBAL: XVT 0x9EC462
int16_t g_flight_sw_rot_sprite_span_base_x = 0;
/* Column where the current line starts: fixed by the set-up for octants 0 to 3,
 * stepped by the octant 4 to 7 functions. */
// GLOBAL: XVT 0x9E9654
int16_t g_flight_sw_rot_sprite_primary_edge_x = 0;
/* Row where the current line starts: stepped by the octant 0 to 3 functions,
 * fixed by the set-up for octants 4 to 7. */
// GLOBAL: XVT 0x9E9652
int16_t g_flight_sw_rot_sprite_primary_edge_y = 0;
/* g_flight_vp_max_y, copied by flight_sw_prepare_sprite_rotation_tables. */
// GLOBAL: XVT 0x9ED230
int16_t g_flight_sw_rot_sprite_viewport_max_y = 0;
/* Set to -1 by flight_sw_prepare_sprite_rotation_tables, its only writer, so the
 * arms that test it for a positive value never run. */
// GLOBAL: XVT 0x9E95F2
int16_t g_flight_sw_rot_sprite_dest_y_mode = 0;
/* Row pitch of g_flight_sw_rot_sprite_dest_buffer: g_flight_bytes_per_pixel *
 * g_flight_vp_width, set by flight_sw_prepare_sprite_rotation_tables. */
// GLOBAL: XVT 0x9A8D50
int g_flight_sw_rot_sprite_dest_pitch_bytes = 0;
/* The scale of the sprite being drawn, from
 * flight_sw_prepare_rotated_sprite_scale_state. */
// GLOBAL: XVT 0x9EC610
struct flight_sw_rot_sprite_scale_state g_flight_sw_rot_sprite_scale_state = {
	0};
/* Copy of g_flight_sw_rot_sprite_primary_edge_y taken by
 * flight_sw_rasterize_prepared_rotated_sprite; nothing reads it. */
// GLOBAL: XVT 0xA00850
uint16_t g_flight_sw_rot_sprite_saved_primary_edge_y = 0;
/* Copy of g_flight_sw_rot_sprite_primary_edge_x taken by
 * flight_sw_rasterize_prepared_rotated_sprite; nothing reads it. */
// GLOBAL: XVT 0xA00852
uint16_t g_flight_sw_rot_sprite_saved_primary_edge_x = 0;
/* Pixels left in the row the screen-rectangle save and restore functions are
 * copying. */
// GLOBAL: XVT 0x9ED238
static uint16_t g_saved_row_pixels_remaining = 0;
/* Edge position at which the clipped span functions stop on the current line;
 * under 0, flight_sw_rasterize_prepared_rotated_sprite draws nothing on it. The
 * octant functions set and step it. */
// GLOBAL: XVT 0x9D114C
int16_t g_flight_sw_rot_sprite_clip_max_x = 0;
/* Points at g_flight_sw_rot_sprite_coeff_cache once
 * flight_sw_prepare_sprite_rotation_tables runs. */
// GLOBAL: XVT 0x9FE7D4
struct flight_sw_rot_sprite_coeff_state *g_flight_sw_rot_sprite_coeffs = 0;
/* Runs left for a span draw function: flight_sw_rasterize_prepared_rotated_sprite
 * sets it to the row's run count, and the function counts it down. */
// GLOBAL: XVT 0xA60A50
int g_flight_sw_rot_sprite_span_run_countdown = 0;
/* g_flight_vp_height, copied by flight_sw_prepare_sprite_rotation_tables. */
// GLOBAL: XVT 0xA07CD0
static int16_t g_flight_sw_rot_sprite_viewport_height = 0;
/* Folded angle from which a rotated sprite's edge steps along y: 0x2000 with
 * square pixels, else 0x2200. Set by flight_sw_prepare_sprite_rotation_tables and
 * flight_sw_prepare_rotated_sprite_scale_state. */
// GLOBAL: XVT 0xA080F2
static uint16_t g_flight_sw_rot_sprite_axis_swap_threshold_angle = 0;

/* Fills g_flight_line_offset_table for g_screen_height rows of g_surface_pitch, sets
 * g_flight_sw_framebuffer_base from flight_surface_get_software_framebuffer_base and
 * clears g_surface_pitch * g_screen_height bytes there (the original build at
 * 640x480 a VESA page at a time), picks the radar target marker for
 * g_flight_resolution_mode (12 pixels at 640x480 and 480x360, else 10), and
 * points g_flight_line_pitch_ptr and g_flight_active_line_offset_table at
 * g_surface_pitch and that table. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40DF00
void flight_sw_init_framebuffer(void)
{
	unsigned int line;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	for (line = 0; line < (unsigned int)g_screen_height; ++line) {
		g_flight_line_offset_table[line] = line * g_surface_pitch;
	}

	g_flight_sw_framebuffer_base =
		flight_surface_get_software_framebuffer_base();
	switch (g_flight_resolution_mode) {
	case FLIGHT_RESOLUTION_320X240:
		memset(g_flight_sw_framebuffer_base, 0,
		       g_surface_pitch * g_screen_height);
		g_radar_target_marker_shape =
			(int8_t *)g_radar_target_marker_shape10;
		g_radar_target_marker_point_count = 10;
		break;

#ifndef XVT_MODERN
	case FLIGHT_RESOLUTION_640X480:
		for (page = 0;
		     page < (unsigned int)(g_surface_pitch * g_screen_height) /
				    g_vesa_page_size_bytes;
		     ++page) {
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			memset(g_flight_sw_framebuffer_base, 0,
			       g_vesa_page_size_bytes);
		}
		if ((unsigned int)(g_surface_pitch * g_screen_height) %
			    g_vesa_page_size_bytes !=
		    0) {
			rts_vga2_set_current_page(
				(uint8_t)g_vesa_window,
				(uint16_t)((unsigned int)(g_surface_pitch *
							  g_screen_height) /
					   g_vesa_page_size_bytes));
			memset(g_flight_sw_framebuffer_base, 0,
			       (unsigned int)(g_surface_pitch *
					      g_screen_height) %
				       g_vesa_page_size_bytes);
		}
		g_radar_target_marker_shape =
			(int8_t *)g_radar_target_marker_shape12;
		g_radar_target_marker_point_count = 12;
		break;
#else
	case FLIGHT_RESOLUTION_640X480:
		memset(g_flight_sw_framebuffer_base, 0,
		       g_surface_pitch * g_screen_height);
		g_radar_target_marker_shape =
			(int8_t *)g_radar_target_marker_shape12;
		g_radar_target_marker_point_count = 12;
		break;
#endif

	case FLIGHT_RESOLUTION_480X360:
		memset(g_flight_sw_framebuffer_base, 0,
		       g_surface_pitch * g_screen_height);
		g_radar_target_marker_shape =
			(int8_t *)g_radar_target_marker_shape12;
		g_radar_target_marker_point_count = 12;
		break;

	default:
		memset(g_flight_sw_framebuffer_base, 0,
		       g_surface_pitch * g_screen_height);
		g_radar_target_marker_shape =
			(int8_t *)g_radar_target_marker_shape10;
		g_radar_target_marker_point_count = 10;
		break;
	}

	g_flight_line_pitch_ptr = &g_surface_pitch;
	g_flight_active_line_offset_table = g_flight_line_offset_table;
}

/* Points the software drawing functions at a target. With surface NULL: back at
 * the surface from flight_surface_get_software_framebuffer_base, with
 * g_surface_pitch and g_flight_line_offset_table. With pitch_bytes -1: at surface
 * with g_surface_pitch, refilling g_flight_line_offset_table for height rows.
 * Otherwise at surface with pitch_bytes in g_flight_alt_line_pitch, and
 * g_flight_alt_line_offset_table filled for height rows of width *
 * g_flight_bytes_per_pixel bytes, so row offsets follow that product, not
 * pitch_bytes. Does not check height against the tables' 768 rows. */
// FUNCTION: XVT 0x40E0C0
void flight_sw_set_render_target(void *surface, int width, unsigned int height,
				 int pitch_bytes)
{
	int16_t line;
	int line_index;
	int bytes_per_pixel;
	int line_offset;

	if (surface == NULL) {
		g_flight_sw_framebuffer_base =
			flight_surface_get_software_framebuffer_base();
		g_flight_line_pitch_ptr = &g_surface_pitch;
		g_flight_active_line_offset_table = g_flight_line_offset_table;
		return;
	}

	g_flight_sw_framebuffer_base = (uint8_t *)surface;
	if (pitch_bytes == -1) {
		line = 0;
		if (height != 0) {
			do {
				line_index = line++;
				g_flight_line_offset_table[line_index] =
					line_index * g_surface_pitch;
			} while ((unsigned int)(int)line < height);
		}
		g_flight_line_pitch_ptr = &g_surface_pitch;
		g_flight_active_line_offset_table = g_flight_line_offset_table;
		return;
	}

	line = 0;
	if (height != 0) {
		bytes_per_pixel = g_flight_bytes_per_pixel;
		do {
			line_index = line++;
			line_offset = width;
			line_offset *= bytes_per_pixel;
			line_offset *= line_index;
			g_flight_alt_line_offset_table[line_index] =
				line_offset;
		} while ((unsigned int)(int)line < height);
	}
	g_flight_alt_line_pitch = pitch_bytes;
	g_flight_line_pitch_ptr = &g_flight_alt_line_pitch;
	g_flight_active_line_offset_table = g_flight_alt_line_offset_table;
}

/* Returns the byte offset of row line from the active row table. Does not check
 * line. */
// FUNCTION: XVT 0x40E190
int flight_sw_get_line_offset(int line)
{
	return g_flight_active_line_offset_table[line];
}

/* Returns the active row pitch in bytes, *g_flight_line_pitch_ptr. */
// FUNCTION: XVT 0x40E1A0
int flight_sw_get_line_pitch(void) { return *g_flight_line_pitch_ptr; }

/* Returns x plus y times the active row pitch. */
// FUNCTION: XVT 0x40EA50
int flight_sw_compute_pixel_offset8bpp(int x, int y)
{
	return x + y * flight_sw_get_line_pitch();
}

/* Draws an RLE sprite at 8 bits without fading: sets g_flight_sw_rle_palette_shift
 * to 0 and calls flight_sw_blit_sprite_rle_impl8bpp. */
// FUNCTION: XVT 0x40EA60
void flight_sw_blit_sprite_rle8bpp(uint8_t *rle_data, int x, int y,
				   int transparent_color_index, int mirror)
{
	g_flight_sw_rle_palette_shift = 0;
	flight_sw_blit_sprite_rle_impl8bpp(
		rle_data, x, y, transparent_color_index, mirror, 0, 0);
}

/* Draws an RLE sprite at 8 bits, faded: sets g_flight_sw_rle_palette_shift to
 * palette_shift and calls flight_sw_blit_sprite_rle_impl8bpp without mirroring. */
// FUNCTION: XVT 0x40EA90
void flight_sw_blit_sprite_rle_faded8bpp(uint8_t *rle_data, int x, int y,
					 int transparent_color_index,
					 int8_t palette_shift,
					 int16_t fade_amount)
{
	g_flight_sw_rle_palette_shift = palette_shift;
	flight_sw_blit_sprite_rle_impl8bpp(
		rle_data, x, y, transparent_color_index, 0, 1, fade_amount);
}

/* Draws an RLE sprite on the 8-bit frame buffer from x, y, a row at a time,
 * rightward or, with mirror, leftward. A byte under 0xFB is a run of (byte & 3)
 * + 1 pixels of color byte >> 2; 0xFD is a run of the next byte + 1 pixels of
 * the color after it; 0xFC draws the byte after next + 1 pixels alternating
 * between the next byte's color and the color 1 above it; 0xFB sets
 * g_flight_sw_rle_palette_shift from the next byte, which a faded draw skips; 0xFE
 * ends a row and 0xFF the sprite. When unfaded, runs under 0xFB add
 * g_flight_sw_rle_palette_shift to their color. Any run but 0xFC whose color then
 * equals transparent_color_index is skipped. Faded, each drawn color becomes
 * color - fade_amount + g_flight_sw_rle_palette_shift, or g_flight_sw_rle_palette_shift
 * alone when fade_amount is 0 or less. Does not clip. */
// FUNCTION: XVT 0x40EAC0
void flight_sw_blit_sprite_rle_impl8bpp(uint8_t *rle_data, int x, int y,
					int transparent_color_index, int mirror,
					char is_faded, int16_t fade_amount)
{
	unsigned int pixel_offset;
	uint8_t *destination;
	uint8_t token;
	uint8_t color;
	int16_t alternating_pixels_remaining;
	uint16_t run_length;
	uint8_t *source;
	int mirror_flag;

	source = rle_data;
	mirror_flag = mirror;
	g_flight_sw_rle_sprite_x = x;
	g_flight_sw_rle_sprite_y = y;
	g_flight_sw_rle_transparent_color = (uint8_t)transparent_color_index;

	for (;;) {
		pixel_offset = (uint16_t)g_flight_sw_rle_sprite_x +
			       flight_sw_get_line_offset(
				       (uint16_t)g_flight_sw_rle_sprite_y);
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			unsigned int page;

			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		destination = g_flight_sw_framebuffer_base + pixel_offset;

		for (;;) {
			token = *source++;
			if (token < 0xFB) {
				run_length = token & 3;
				color = token >> 2;
				if (is_faded == 0) {
					color += g_flight_sw_rle_palette_shift;
				}
			} else {
				if (token > 0xFB) {
					if (token == 0xFC) {
						/* In this two-color dither run, run_length holds the second color, written to every
						 * other pixel. */
						color = source[0];
						if (is_faded != 0) {
							if (fade_amount > 0) {
								color -= (uint8_t)
									fade_amount;
								color += (uint8_t)
									g_flight_sw_rle_palette_shift;
								run_length =
									color;
								++run_length;
							} else {
								color = (uint8_t)
									g_flight_sw_rle_palette_shift;
								run_length =
									color;
							}
						} else {
							run_length = color;
							++run_length;
						}
						alternating_pixels_remaining =
							(int16_t)source[1] + 1;
						source += 2;
						while (alternating_pixels_remaining >
						       0) {
							*destination = color;
							if (mirror_flag == 0) {
								++destination;
							} else {
								--destination;
							}
							--alternating_pixels_remaining;
							if (alternating_pixels_remaining >
							    0) {
								*destination = (uint8_t)
									run_length;
								if (mirror_flag ==
								    0) {
									++destination;
								} else {
									--destination;
								}
								--alternating_pixels_remaining;
							}
						}
						continue;
					}
					if (token == 0xFD) {
						run_length = source[0];
						color = source[1];
						source += 2;
					} else {
						break;
					}
				} else {
					if (is_faded == 0) {
						g_flight_sw_rle_palette_shift =
							(int8_t)*source;
					}
					++source;
					continue;
				}
			}

			++run_length;
			if (color == transparent_color_index) {
				if (mirror_flag == 0) {
					destination += run_length;
				} else {
					destination -= run_length;
				}
				continue;
			}
			if (is_faded != 0) {
				if (fade_amount > 0) {
					color -= (uint8_t)fade_amount;
					color += (uint8_t)
						g_flight_sw_rle_palette_shift;
				} else {
					color = (uint8_t)
						g_flight_sw_rle_palette_shift;
				}
			}
			if (mirror_flag == 0) {
				memset(destination, color, run_length);
				destination += run_length;
			} else {
				memset(destination - (run_length - 1), color,
				       run_length);
				destination -= run_length;
			}
		}

		if (token == 0xFF) {
			return;
		}
		++g_flight_sw_rle_sprite_y;
	}
}

/* Draws a map icon in the RLE form flight_sw_blit_sprite_rle_impl8bpp reads, at 8
 * bits, or through flight_sw_blit_map_icon_rle16bpp at 16 bits, without fading.
 * Each drawn color is 4 above the run's color: runs under 0xFB add
 * g_flight_sw_rle_palette_shift first, 0xFD and 0xFC runs do not, 0xFB sets it, and
 * transparentIndex is tested before the 4 is added (not for 0xFC). */
// FUNCTION: XVT 0x40ECE0
void flight_sw_blit_map_icon_rle(uint8_t *rle_data, int x, int y,
				 int transparent_index, int mirror)
{
	struct {
		uint8_t value;
		uint8_t padding[3];
	} color;

	uint8_t *destination;
	unsigned int pixel_offset;
	uint16_t run_length;
	uint16_t next_color;
	uint16_t *next;
	uint16_t *count;
#ifndef XVT_MODERN
	uint8_t **source;
	void *color_ref;
	uint8_t **cursor;
	int *mirror_ref;
	int *transparent_ref;
#endif

	if (g_flight_bytes_per_pixel == 2) {
		flight_sw_blit_map_icon_rle16bpp(rle_data, x, y,
						 transparent_index, mirror);
		return;
	}

	next = &next_color;
	count = &run_length;
#ifndef XVT_MODERN
	source = &rle_data;
	color_ref = &color;
	cursor = &destination;
	mirror_ref = &mirror;
	transparent_ref = &transparent_index;
#endif

	g_flight_sw_rle_sprite_x = (int16_t)x;
	g_flight_sw_rle_sprite_y = (int16_t)y;
	g_flight_sw_rle_transparent_color = (uint8_t)transparent_index;

	for (;;) {
		pixel_offset = (uint16_t)g_flight_sw_rle_sprite_x +
			       flight_sw_get_line_offset(
				       (uint16_t)g_flight_sw_rle_sprite_y);
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    xvt_framebuffer_address_is_legacy_base(
			    g_flight_sw_framebuffer_base)) {
			unsigned int page;

			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		destination = g_flight_sw_framebuffer_base + pixel_offset;

		for (;;) {
			color.value = *rle_data++;
			if (color.value < 0xFB) {
				*count = color.value;
				color.value >>= 2;
				*count &= 3;
				color.value +=
					(uint8_t)g_flight_sw_rle_palette_shift;
			} else {
				if (color.value > 0xFB) {
					if (color.value == 0xFC) {
						color.value = *rle_data;
						*next = (uint8_t)color.value +
							1;
						++rle_data;
						*count = *rle_data + 1;
						++rle_data;
						if ((int16_t)*count > 0) {
							uint8_t *pixel;

							color.value += 4;
							do {
								pixel = destination;
								*pixel =
									color.value;
								if (mirror ==
								    0) {
									destination =
										pixel +
										1;
								} else {
									destination =
										pixel -
										1;
								}
								--*count;
								if ((int16_t)*count >
								    0) {
									pixel = destination;
									*pixel =
										(uint8_t)(*next +
											  4);
									if (mirror ==
									    0) {
										destination =
											pixel +
											1;
									} else {
										destination =
											pixel -
											1;
									}
								}
								--*count;
							} while (
								(int16_t)*count >
								0);
						}
						continue;
					}
					if (color.value == 0xFD) {
						*count = *rle_data++;
						color.value = *rle_data++;
					} else {
						break;
					}
				} else {
					g_flight_sw_rle_palette_shift =
						(int8_t)*rle_data++;
					continue;
				}
			}

			++*count;
			if (color.value == transparent_index) {
				if (mirror == 0) {
					destination += *count;
				} else {
					destination -= *count;
				}
				continue;
			}
			if (mirror == 0) {
				if (*count > 0) {
					memset(destination,
					       (int8_t)(color.value + 4),
					       *count);
					destination += *count;
				}
			} else {
				if (*count > 0) {
					uint8_t *pixel;

					while (*count > 0) {
						pixel = destination;
						*pixel = (int8_t)(color.value +
								  4);
						destination = pixel - 1;
						--*count;
					}
				}
			}
		}

		if (color.value == 0xFF) {
			return;
		}
		++g_flight_sw_rle_sprite_y;
	}
}

/* Writes color_index at x, y on the 8-bit frame buffer through the active row
 * table. Does not clip. flight_render_install_callbacks stores it in
 * g_flight_draw_pixel_fn, which nothing calls. */
// FUNCTION: XVT 0x40EFE0
void flight_sw_draw_pixel8bpp(uint16_t x, uint16_t y, int8_t color_index)
{
	unsigned int pixel_offset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	pixel_offset = x + flight_sw_get_line_offset(y);
#ifndef XVT_MODERN
	if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
	    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
		page = pixel_offset / g_vesa_page_size_bytes;
		pixel_offset %= g_vesa_page_size_bytes;
		rts_vga2_set_current_page((uint8_t)g_vesa_window,
					  (uint16_t)page);
	}
#endif
	g_flight_sw_framebuffer_base[pixel_offset] = color_index;
}

/* Fills the g_flightClip rectangle with g_flight_text_bg_color at 8 bits, through
 * flight_sw_fill_rect_or_border8bpp. */
// FUNCTION: XVT 0x40F9F0
void flight_sw_fill_clip_rect8bpp(void)
{
	g_flight_fill_rect_bottom8bpp = (uint16_t)g_flight_clip_bottom;
	g_flight_fill_rect_top8bpp = (uint16_t)g_flight_clip_top;
	g_flight_fill_rect_left8bpp = (uint16_t)g_flight_clip_left;
	g_flight_fill_rect_right8bpp = (uint16_t)g_flight_clip_right;
	flight_sw_fill_rect_or_border8bpp(0);
}

/* Fills the 8-bit g_flightFillRect rectangle with g_flight_text_bg_color or, with
 * border_thickness nonzero, only a frame that many pixels thick; does nothing
 * when it has no width. Steps g_flight_fill_rect_current_y8bpp and
 * g_flight_fill_rect_remaining_rows8bpp down the rows. Drawing to the legacy
 * 0xA0000 base outside 320x240, it works out a VESA page per row for
 * rts_vga2_set_current_page, which does nothing. Does not clip. */
// FUNCTION: XVT 0x40FA30
void flight_sw_fill_rect_or_border8bpp(uint16_t border_thickness)
{
	int direct_framebuffer;
	unsigned int pixel_offset;
	unsigned int border_row;
	unsigned int bottom_row;

	g_flight_fill_rect_current_y8bpp = g_flight_fill_rect_top8bpp;
	g_flight_fill_rect_remaining_rows8bpp =
		g_flight_fill_rect_bottom8bpp - g_flight_fill_rect_top8bpp;
	direct_framebuffer = 0;
	if ((int16_t)(g_flight_fill_rect_right8bpp -
		      g_flight_fill_rect_left8bpp) <= 0) {
		return;
	}

	pixel_offset =
		flight_sw_get_line_offset(g_flight_fill_rect_current_y8bpp);
	if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
	    xvt_framebuffer_address_is_legacy_base(
		    g_flight_sw_framebuffer_base)) {
		unsigned int page;

		page = pixel_offset / g_vesa_page_size_bytes;
		pixel_offset %= g_vesa_page_size_bytes;
#ifdef XVT_MODERN
		rts_vga2_set_current_page((uint8_t)g_flight_resolution_mode,
					  page);
#else
		rts_vga2_set_current_page((uint8_t)g_vesa_window, page);
#endif
		if (pixel_offset + g_flight_fill_rect_remaining_rows8bpp *
					   g_surface_pitch >
		    0xFFFF) {
			direct_framebuffer = 1;
		}
	}
	if (!xvt_framebuffer_address_is_legacy_base(
		    g_flight_sw_framebuffer_base)) {
		direct_framebuffer = 1;
	}

	if (!direct_framebuffer) {
		uint8_t *destination;

		if (border_thickness != 0) {
			destination =
				&g_flight_sw_framebuffer_base[pixel_offset];
			border_row = 0;
			while (border_row < border_thickness) {
				uint8_t *row_start;
				int16_t width;

				row_start =
					&destination
						[g_flight_fill_rect_left8bpp];
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*row_start++ = g_flight_text_bg_color;
				}
				++border_row;
				destination += flight_sw_get_line_pitch();
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
			while (g_flight_fill_rect_remaining_rows8bpp >
			       border_thickness) {
				uint8_t *row_start;
				unsigned int count;
				int16_t width;

				row_start =
					&destination
						[g_flight_fill_rect_left8bpp];
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				count = border_thickness;
				while (count-- != 0) {
					*row_start++ = g_flight_text_bg_color;
				}
				row_start +=
					(int16_t)(width - 2 * border_thickness);
				count = border_thickness;
				while (count-- != 0) {
					*row_start++ = g_flight_text_bg_color;
				}
				destination += flight_sw_get_line_pitch();
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
			bottom_row = 0;
			while (bottom_row < border_thickness) {
				uint8_t *row_start;
				int16_t width;

				row_start =
					&destination
						[g_flight_fill_rect_left8bpp];
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*row_start++ = g_flight_text_bg_color;
				}
				++bottom_row;
				destination += flight_sw_get_line_pitch();
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
		} else {
			destination =
				&g_flight_sw_framebuffer_base[pixel_offset];
			while (g_flight_fill_rect_remaining_rows8bpp != 0) {
				uint8_t *row_start;
				int16_t width;

				row_start =
					&destination
						[g_flight_fill_rect_left8bpp];
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*row_start++ = g_flight_text_bg_color;
				}
				destination += flight_sw_get_line_pitch();
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
		}
	} else {
		if (border_thickness != 0) {
			border_row = 0;
			while (border_row < border_thickness) {
				uint8_t *destination;
				int16_t width;

				pixel_offset =
					g_flight_fill_rect_left8bpp +
					flight_sw_get_line_offset(
						g_flight_fill_rect_current_y8bpp);
				if (g_flight_resolution_mode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    xvt_framebuffer_address_is_legacy_base(
					    g_flight_sw_framebuffer_base)) {
					unsigned int page;

					page = pixel_offset /
					       g_vesa_page_size_bytes;
					pixel_offset %= g_vesa_page_size_bytes;
#ifdef XVT_MODERN
					rts_vga2_set_current_page(
						(uint8_t)
							g_flight_resolution_mode,
						page);
#else
					rts_vga2_set_current_page(
						(uint8_t)g_vesa_window, page);
#endif
				}
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				destination = &g_flight_sw_framebuffer_base
						      [pixel_offset];
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*destination++ = g_flight_text_bg_color;
				}
				++border_row;
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
			while (g_flight_fill_rect_remaining_rows8bpp >
			       border_thickness) {
				uint8_t *destination;
				unsigned int count;
				int16_t width;

				pixel_offset =
					g_flight_fill_rect_left8bpp +
					flight_sw_get_line_offset(
						g_flight_fill_rect_current_y8bpp);
				if (g_flight_resolution_mode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    xvt_framebuffer_address_is_legacy_base(
					    g_flight_sw_framebuffer_base)) {
					unsigned int page;

					page = pixel_offset /
					       g_vesa_page_size_bytes;
					pixel_offset %= g_vesa_page_size_bytes;
#ifdef XVT_MODERN
					rts_vga2_set_current_page(
						(uint8_t)
							g_flight_resolution_mode,
						page);
#else
					rts_vga2_set_current_page(
						(uint8_t)g_vesa_window, page);
#endif
				}
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				destination = &g_flight_sw_framebuffer_base
						      [pixel_offset];
				count = border_thickness;
				while (count-- != 0) {
					*destination++ = g_flight_text_bg_color;
				}
				destination +=
					(int16_t)(width - 2 * border_thickness);
				count = border_thickness;
				while (count-- != 0) {
					*destination++ = g_flight_text_bg_color;
				}
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
			bottom_row = 0;
			while (bottom_row < border_thickness) {
				uint8_t *destination;
				int16_t width;

				pixel_offset =
					g_flight_fill_rect_left8bpp +
					flight_sw_get_line_offset(
						g_flight_fill_rect_current_y8bpp);
				if (g_flight_resolution_mode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    xvt_framebuffer_address_is_legacy_base(
					    g_flight_sw_framebuffer_base)) {
					unsigned int page;

					page = pixel_offset /
					       g_vesa_page_size_bytes;
					pixel_offset %= g_vesa_page_size_bytes;
#ifdef XVT_MODERN
					rts_vga2_set_current_page(
						(uint8_t)
							g_flight_resolution_mode,
						page);
#else
					rts_vga2_set_current_page(
						(uint8_t)g_vesa_window, page);
#endif
				}
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				destination = &g_flight_sw_framebuffer_base
						      [pixel_offset];
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*destination++ = g_flight_text_bg_color;
				}
				++bottom_row;
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
		} else {
			while (g_flight_fill_rect_remaining_rows8bpp != 0) {
				uint8_t *destination;
				int16_t width;

				pixel_offset =
					g_flight_fill_rect_left8bpp +
					flight_sw_get_line_offset(
						g_flight_fill_rect_current_y8bpp);
				if (g_flight_resolution_mode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    xvt_framebuffer_address_is_legacy_base(
					    g_flight_sw_framebuffer_base)) {
					unsigned int page;

					page = pixel_offset /
					       g_vesa_page_size_bytes;
					pixel_offset %= g_vesa_page_size_bytes;
#ifdef XVT_MODERN
					rts_vga2_set_current_page(
						(uint8_t)
							g_flight_resolution_mode,
						page);
#else
					rts_vga2_set_current_page(
						(uint8_t)g_vesa_window, page);
#endif
				}
				width = (int16_t)(g_flight_fill_rect_right8bpp -
						  g_flight_fill_rect_left8bpp);
				destination = &g_flight_sw_framebuffer_base
						      [pixel_offset];
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*destination++ = g_flight_text_bg_color;
				}
				--g_flight_fill_rect_remaining_rows8bpp;
				++g_flight_fill_rect_current_y8bpp;
			}
		}
	}
}

/* Clips the rectangle from x1, y1 to x2, y2 (ends not included) to the
 * g_flightClip rectangle, stores it in the 8-bit g_flightFillRect globals, and
 * fills it or its frame with flight_sw_fill_rect_or_border8bpp when anything is
 * left. */
// FUNCTION: XVT 0x410040
void flight_sw_fill_rect_clipped8bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				     uint16_t y2, uint16_t border_thickness)
{
	g_flight_fill_rect_left8bpp = x1;
	g_flight_fill_rect_right8bpp = x2;
	g_flight_fill_rect_top8bpp = y1;
	g_flight_fill_rect_bottom8bpp = y2;
	if (g_flight_clip_left > (int)x1) {
		g_flight_fill_rect_left8bpp = (uint16_t)g_flight_clip_left;
	}
	if (g_flight_clip_right < (int)x2) {
		g_flight_fill_rect_right8bpp = (uint16_t)g_flight_clip_right;
	}
	if (g_flight_clip_top > (int)y1) {
		g_flight_fill_rect_top8bpp = (uint16_t)g_flight_clip_top;
	}
	if (g_flight_clip_bottom < (int)y2) {
		g_flight_fill_rect_bottom8bpp = (uint16_t)g_flight_clip_bottom;
	}
	if (g_flight_fill_rect_top8bpp < g_flight_fill_rect_bottom8bpp &&
	    g_flight_fill_rect_right8bpp > g_flight_fill_rect_left8bpp) {
		flight_sw_fill_rect_or_border8bpp(border_thickness);
	}
}

/* Copies height rows of width pixels from x, y on the 8-bit frame buffer into
 * buffer, row after row. Does not clip. */
// FUNCTION: XVT 0x410230
void flight_sw_save_screen_rect8bpp(uint8_t *buffer, int x, int y,
				    int16_t width, int height)
{
	unsigned int pixel_offset;
	uint8_t *source;
	int rows_remaining;
	uint8_t pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	rows_remaining = height;
	if (rows_remaining == 0) {
		return;
	}
	do {
		pixel_offset = flight_sw_get_line_offset(y) + x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_sw_framebuffer_base == g_flight_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			rts_vga2_set_current_page(1, (uint16_t)page);
		}
#endif
		g_saved_row_pixels_remaining = width;
		source = g_flight_sw_framebuffer_base + pixel_offset;
		while (g_saved_row_pixels_remaining > 0) {
			pixel = *source++;
			*buffer++ = pixel;
			--g_saved_row_pixels_remaining;
		}
		--rows_remaining;
		++y;
	} while (rows_remaining != 0);
}

/* Writes height rows of width pixels from buffer back at x, y on the 8-bit
 * frame buffer. Does not clip. */
// FUNCTION: XVT 0x4102F0
void flight_sw_restore_screen_rect8bpp(uint8_t *buffer, int x, int y,
				       int16_t width, int height)
{
	int rows_remaining;
	uint8_t *destination;
	unsigned int pixel_offset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	rows_remaining = height;
	for (; rows_remaining != 0; ++y) {
		pixel_offset = flight_sw_get_line_offset(y) + x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_sw_framebuffer_base == g_flight_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		g_saved_row_pixels_remaining = width;
		destination = g_flight_sw_framebuffer_base;
		destination += pixel_offset;
		while (g_saved_row_pixels_remaining > 0) {
			*destination++ = *buffer++;
			--g_saved_row_pixels_remaining;
		}
		--rows_remaining;
	}
}

/* Draws count points, each three 16-bit words: x, y, and a color in the low
 * byte of the third. A pixel is drawn only where the frame buffer holds palette
 * index 44. The third word then becomes a mask: 1 when the pixel was drawn,
 * else 0, and at 640x480 the pixel below gets the same test and adds the 0x2
 * bit. Does not clip. */
// FUNCTION: XVT 0x4103A0
void flight_sw_draw_point_array8bpp(uint16_t *points, int16_t count)
{
	struct flight_sw_point_record {
		uint16_t x;
		uint16_t y;
		uint8_t drawn_mask;
		uint8_t payload_high;
	};

	unsigned int pixel_offset;
	uint8_t *destination;
	uint8_t color;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (count == 0) {
		return;
	}
	do {
		pixel_offset = points[0];
		color = (uint8_t)points[2];
		pixel_offset += flight_sw_get_line_offset(points[1]);
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			rts_vga2_set_current_page(1, (uint16_t)page);
		}
#endif
		destination = g_flight_sw_framebuffer_base + pixel_offset;
		if (*destination != 44) {
			points[2] = 0;
		} else {
			*destination = color;
			points[2] = 1;
		}

		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
			pixel_offset = points[0];
			pixel_offset += flight_sw_get_line_offset(
				(uint16_t)(points[1] + 1));
#ifndef XVT_MODERN
			if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flight_sw_framebuffer_base ==
				    g_sw_framebuffer_base) {
				page = pixel_offset / g_vesa_page_size_bytes;
				pixel_offset %= g_vesa_page_size_bytes;
				rts_vga2_set_current_page(
					(uint8_t)g_vesa_window, (uint16_t)page);
				rts_vga2_set_current_page(1, (uint16_t)page);
			}
#endif
			destination = g_flight_sw_framebuffer_base;
			destination += pixel_offset;
			if (*destination != 44) {
				points[2] &= 1;
			} else {
				*destination = color;
				((struct flight_sw_point_record *)points)
					->drawn_mask |= 2;
			}
		}
		--count;
		points += 3;
	} while (count != 0);
}

/* Undoes flight_sw_draw_point_array8bpp: writes palette index 44 back at each
 * point whose mask has the 0x1 bit and, at 640x480, below it where the mask has
 * the 0x2 bit. */
// FUNCTION: XVT 0x4104D0
void flight_sw_erase_point_array8bpp(uint16_t *points, int16_t count)
{
	uint16_t *current;
	int16_t remaining;
	unsigned int pixel_offset;
	uint8_t *destination;
	unsigned int x;
	unsigned int y;
	uint8_t mask;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = count;
	if (remaining == 0) {
		return;
	}
	current = points;
	do {
		x = current[0];
		y = current[1];
		pixel_offset = flight_sw_get_line_offset(y) + x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		destination = g_flight_sw_framebuffer_base + pixel_offset;
		mask = (uint8_t)current[2];
		if ((mask & 1) != 0) {
			*destination = 44;
		}
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
			x = current[0];
			y = (uint16_t)(current[1] + 1);
			pixel_offset = flight_sw_get_line_offset(y) + x;
#ifndef XVT_MODERN
			if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flight_sw_framebuffer_base ==
				    g_sw_framebuffer_base) {
				page = pixel_offset / g_vesa_page_size_bytes;
				pixel_offset %= g_vesa_page_size_bytes;
				rts_vga2_set_current_page(
					(uint8_t)g_vesa_window, (uint16_t)page);
			}
#endif
			destination =
				g_flight_sw_framebuffer_base + pixel_offset;
			mask = (uint8_t)current[2];
			if ((mask & 2) != 0) {
				*destination = 44;
			}
		}
		--remaining;
		current += 3;
	} while (remaining != 0);
}

/* Draws the radar target marker at g_radar_target_marker_draw_x and
 * g_radar_target_marker_draw_y in palette index 206: g_radar_target_marker_point_count
 * pixels of g_radar_target_marker_shape, saving the pixels under them in
 * g_radar_target_marker_saved_pixels. Does not clip. */
// FUNCTION: XVT 0x4105E0
void flight_sw_draw_radar_target_marker8bpp(void)
{
	unsigned int pixel_offset;
	uint16_t offset_index;
	uint16_t saved_pixel_index;
	int16_t remaining;
	int8_t *offset;
	uint8_t *pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	saved_pixel_index = 0;
	offset_index = 0;
	remaining = (int16_t)g_radar_target_marker_point_count;
	if (remaining == (int16_t)saved_pixel_index) {
		return;
	}
	do {
		offset = &g_radar_target_marker_shape[offset_index];
		pixel_offset =
			flight_sw_get_line_offset(g_radar_target_marker_draw_y +
						  offset[1]) +
			offset[0] + g_radar_target_marker_draw_x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			rts_vga2_set_current_page(1, (uint16_t)page);
		}
#endif
		offset_index += 2;
		pixel = g_flight_sw_framebuffer_base + pixel_offset;
		g_radar_target_marker_saved_pixels[saved_pixel_index++] =
			*pixel;
		*pixel = 206;
		--remaining;
	} while (remaining != 0);
}

/* Puts back the pixels flight_sw_draw_radar_target_marker8bpp saved, at
 * g_radar_target_marker_restore_x and g_radar_target_marker_restore_y. */
// FUNCTION: XVT 0x4106C0
void flight_sw_restore_radar_target_marker8bpp(void)
{
	unsigned int pixel_offset;
	uint16_t offset_index;
	uint16_t saved_pixel_index;
	int16_t remaining;
	int8_t *offset;
	uint8_t pixel;
	uint8_t *framebuffer_base;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	offset_index = 0;
	saved_pixel_index = 0;
	remaining = (int16_t)g_radar_target_marker_point_count;
	if (remaining == (int16_t)saved_pixel_index) {
		return;
	}
	do {
		offset = g_radar_target_marker_shape + offset_index;
		pixel_offset =
			offset[0] +
			flight_sw_get_line_offset(
				g_radar_target_marker_restore_y + offset[1]) +
			g_radar_target_marker_restore_x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_sw_framebuffer_base == g_flight_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		offset_index += 2;
		framebuffer_base = g_flight_sw_framebuffer_base;
		pixel = g_radar_target_marker_saved_pixels[saved_pixel_index++];
		framebuffer_base[pixel_offset] = pixel;
		--remaining;
	} while (remaining != 0);
}

/* Draws the 7-pixel cross marker centered on x, y in color, saving the pixels
 * under it; returns color. Nothing calls this. */
// FUNCTION: XVT 0x410780
uint8_t flight_sw_draw_cross_marker8bpp(uint16_t x, uint16_t y, uint8_t color)
{
	unsigned int pixel_offset;
	uint16_t offset_index;
	uint16_t saved_pixel_index;
	int16_t remaining;
	unsigned int coordinates[2];
	uint8_t *pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = 7;
	coordinates[0] = y;
	offset_index = 0;
	coordinates[1] = x;
	saved_pixel_index = 0;
	do {
		pixel_offset =
			flight_sw_get_line_offset(
				coordinates[0] +
				((int8_t *)g_flight_sw_cross_marker_offsets)
					[offset_index + 1]) +
			((int8_t *)g_flight_sw_cross_marker_offsets)
				[offset_index] +
			coordinates[1];
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			rts_vga2_set_current_page(1, (uint16_t)page);
		}
#endif
		offset_index += 2;
		pixel = g_flight_sw_framebuffer_base + pixel_offset;
		g_flight_sw_cross_marker_saved_pixels[saved_pixel_index++] =
			*pixel;
		*pixel = color;
		--remaining;
	} while (remaining != 0);

	return color;
}

/* Puts back the 7 pixels saved around x, y and returns the last of them.
 * Nothing calls this. */
// FUNCTION: XVT 0x410860
uint8_t flight_sw_restore_cross_marker8bpp(uint16_t x, uint16_t y)
{
	int16_t remaining = 7;
	uint16_t saved_pixel_index = 0;
	uint16_t offset_index = 0;
	unsigned int pixel_offset;
	uint8_t pixel;
	uint8_t *framebuffer_base;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	do {
		pixel_offset =
			((int8_t *)g_flight_sw_cross_marker_offsets)
				[offset_index] +
			flight_sw_get_line_offset(
				y + ((int8_t *)g_flight_sw_cross_marker_offsets)
					    [offset_index + 1]) +
			x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		offset_index += 2;
		framebuffer_base = g_flight_sw_framebuffer_base;
		pixel = g_flight_sw_cross_marker_saved_pixels
			[saved_pixel_index++];
		framebuffer_base[pixel_offset] = pixel;
		--remaining;
	} while (remaining != 0);
	return pixel;
}

/* Draws the background stars. Three grids of g_starfield_grid_dimension by
 * g_starfield_grid_dimension points lie on three faces of a cube around the eye,
 * turned by the camera matrix, each point moved by its star's jitter vector; a
 * point behind the eye is mirrored through it, so the opposite faces show too.
 * A point where |x| and |y| are under z that projects inside the viewport is
 * drawn in its star's color, but only over a pixel of the sky color
 * (g_flight_background_color_index, or its 16-bit color). On first use it makes
 * the star colors (grays: the palette entry from 0x40 on nearest gray 8 to 23,
 * or that level in each channel of a 16-bit color) and each star's random
 * jitter vector, 0 to 124. Calls fe_disk_io_fatal_error(0) when an allocation
 * fails. */
// FUNCTION: XVT 0x410930
void flight_starfield_render(void)
{
	enum {
		STAR_COUNT = 3072,
		STAR_GRID_SPAN = 32,
		STAR_JITTER_COUNT = 125,
		STAR_PLANE_COUNT = 3,
	};

	int row_index;
	int plane_index;
	uint8_t *colors8;
	uint16_t *colors16;
	uint8_t *random_vector_indices;
	float step_view_z[3];
	float step_view_y[3];
	float initial_view[3];
	float step_view_x[3];
	int column_index;
	float base_view[3];
	int screen_x;
	int screen_y;
	int row_axis;
	int column_axis;
	int star_index;
	uint8_t target_rgb[3];
	uint16_t background_color16;

	g_starfield_grid_dimension = STAR_GRID_SPAN / g_star_grid_divisor;
	if (g_flight_bytes_per_pixel == 2) {
		background_color16 =
			g_flight_palette16_bpp[g_flight_background_color_index];
		if (!g_starfield_colors16_initialized) {
			int color_index;
			g_starfield_colors16_handle = memory_alloc_handle(
				STAR_COUNT * sizeof(uint16_t), 0);
			if (g_starfield_colors16_handle == 0) {
				fe_disk_io_fatal_error(0);
			}
			colors16 = (uint16_t *)memory_get_handle_block(
				g_starfield_colors16_handle);
			if (display_is_pixel_format555()) {
				for (color_index = 0; color_index < STAR_COUNT;
				     ++color_index) {
					int shade = (game_rand() & 0xF) + 8;
					colors16[color_index] =
						(uint16_t)(0x421 * shade);
				}
			} else {
				for (color_index = 0; color_index < STAR_COUNT;
				     ++color_index) {
					int shade = (game_rand() & 0xF) + 8;
					colors16[color_index] =
						(uint16_t)(0x841 * shade);
				}
			}
			memory_handle_block_done_stub(
				g_starfield_colors16_handle);
			g_starfield_colors16_initialized = 1;
		}
		colors16 = (uint16_t *)memory_get_handle_block(
			g_starfield_colors16_handle);
	} else {
		if (!g_starfield_colors8_initialized) {
			int color_index;
			g_starfield_colors8_handle =
				memory_alloc_handle(STAR_COUNT, 0);
			if (g_starfield_colors8_handle == 0) {
				fe_disk_io_fatal_error(0);
			}
			colors8 = (uint8_t *)memory_get_handle_block(
				g_starfield_colors8_handle);
			for (color_index = 0; color_index < STAR_COUNT;
			     ++color_index) {
				int shade = (game_rand() & 0xF) + 8;
				target_rgb[0] = (uint8_t)shade;
				target_rgb[1] = (uint8_t)shade;
				target_rgb[2] = (uint8_t)shade;
				colors8[color_index] = (uint8_t)
					color_find_nearest_rgb_triplet_index(
						target_rgb,
						(const uint8_t *)g_sw_palette,
						0x40, 0x100);
			}
			memory_handle_block_done_stub(
				g_starfield_colors8_handle);
			g_starfield_colors8_initialized = 1;
		}
		colors8 = (uint8_t *)memory_get_handle_block(
			g_starfield_colors8_handle);
	}
	if (!g_starfield_random_vector_indices_initialized) {
		int vector_index;
		g_starfield_random_vector_indices_handle =
			memory_alloc_handle(STAR_COUNT * STAR_PLANE_COUNT, 0);
		if (g_starfield_random_vector_indices_handle == 0) {
			fe_disk_io_fatal_error(0);
		}
		random_vector_indices = (uint8_t *)memory_get_handle_block(
			g_starfield_random_vector_indices_handle);
		for (vector_index = 0; vector_index < STAR_COUNT;
		     ++vector_index) {
			int random_index;
			do {
				random_index = game_rand() & 0x7F;
			} while (random_index > STAR_JITTER_COUNT - 1);
			random_vector_indices[vector_index] =
				(uint8_t)random_index;
		}
		memory_handle_block_done_stub(
			g_starfield_random_vector_indices_handle);
		g_starfield_random_vector_indices_initialized = 1;
	}
	random_vector_indices = (uint8_t *)memory_get_handle_block(
		g_starfield_random_vector_indices_handle);

	initial_view[0] =
		(float)(-(g_cam_mat_r0_x + g_cam_mat_r0_y + g_cam_mat_r0_z) >>
			2);
	initial_view[1] =
		(float)(-(g_cam_mat_r1_x + g_cam_mat_r1_y + g_cam_mat_r1_z) >>
			2);
	initial_view[2] =
		(float)(-(g_cam_mat_r2_x + g_cam_mat_r2_y + g_cam_mat_r2_z) >>
			2);
	base_view[0] = initial_view[0];
	base_view[1] = initial_view[1];
	base_view[2] = initial_view[2];
	step_view_x[0] =
		(float)(g_cam_mat_r0_x >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_y[0] =
		(float)(g_cam_mat_r1_x >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_z[0] =
		(float)(g_cam_mat_r2_x >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_x[1] =
		(float)(g_cam_mat_r0_y >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_y[1] =
		(float)(g_cam_mat_r1_y >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_z[1] =
		(float)(g_cam_mat_r2_y >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_x[2] =
		(float)(g_cam_mat_r0_z >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_y[2] =
		(float)(g_cam_mat_r1_z >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	step_view_z[2] =
		(float)(g_cam_mat_r2_z >> 1) *
		g_sw3d_span_length_reciprocal[g_starfield_grid_dimension];
	star_index = 0;
	column_axis = 0;
	row_axis = 1;
	for (plane_index = 0; plane_index < STAR_PLANE_COUNT; ++plane_index) {
		for (row_index = 0; row_index < g_starfield_grid_dimension;
		     ++row_index) {
			float current_view_z = base_view[2];
			float current_view_y = base_view[1];
			float current_view_x = base_view[0];
			for (column_index = 0;
			     column_index < g_starfield_grid_dimension;
			     ++column_index) {
				uint8_t jitter_index =
					random_vector_indices[star_index];
				float view_x =
					current_view_x +
					g_starfield_jitter_x[jitter_index];
				float view_y =
					current_view_y +
					g_starfield_jitter_y[jitter_index];
				float view_z =
					current_view_z +
					g_starfield_jitter_z[jitter_index];
				float abs_view;
				if (view_z < 0.0f) {
					view_x = -view_x;
					view_y = -view_y;
					view_z = -view_z;
				}
				if (view_x < 0.0f) {
					abs_view = -view_x;
				} else {
					abs_view = view_x;
				}
				if (abs_view < view_z) {
					if (view_y < 0.0f) {
						abs_view = -view_y;
					} else {
						abs_view = view_y;
					}
					if (abs_view < view_z) {
						float projection_scale =
							g_proj_scale_int /
							view_z;
						screen_x =
							g_flight_vp_center_x +
							(int)(view_x *
							      projection_scale);
						screen_y =
							g_flight_vp_center_y +
							(int)(view_y *
							      projection_scale) +
							g_proj_offset_y;
						if (screen_x >= 0 &&
						    screen_x <
							    g_flight_vp_width &&
						    screen_y >= 0 &&
						    screen_y <
							    g_flight_vp_height) {
							if (g_flight_bytes_per_pixel ==
							    2) {
								uint16_t *pixel =
									(uint16_t
										 *)(g_flight_sw_framebuffer_base +
										    g_surface_pitch *
											    (g_flight_vp_y +
											     screen_y)) +
									(g_flight_vp_x +
									 screen_x);
								if (*pixel ==
								    background_color16) {
									*pixel = colors16
										[star_index];
								}
							} else {
								uint8_t *pixel =
									g_flight_sw_framebuffer_base +
									g_surface_pitch *
										(g_flight_vp_y +
										 screen_y) +
									g_flight_vp_x +
									screen_x;
								if (*pixel ==
								    g_flight_background_color_index) {
									*pixel = colors8
										[star_index];
								}
							}
						}
					}
				}
				current_view_z += step_view_z[column_axis];
				current_view_y += step_view_y[column_axis];
				current_view_x += step_view_x[column_axis];
				++star_index;
			}
			base_view[0] += step_view_x[row_axis];
			base_view[1] += step_view_y[row_axis];
			base_view[2] += step_view_z[row_axis];
		}
		base_view[0] = initial_view[0];
		base_view[1] = initial_view[1];
		base_view[2] = initial_view[2];
		if (plane_index == 0) {
			++row_axis;
		}
		if (plane_index == 1) {
			++column_axis;
		}
	}
	memory_handle_block_done_stub(g_starfield_random_vector_indices_handle);
	if (g_flight_bytes_per_pixel == 2) {
		memory_handle_block_done_stub(g_starfield_colors16_handle);
	} else {
		memory_handle_block_done_stub(g_starfield_colors8_handle);
	}
}

/* Does nothing in either build. */
// FUNCTION: XVT 0x410FF0
void rts_vga2_set_current_page(uint8_t window, uint16_t page)
{
	(void)window;
	(void)page;
}

/* Saves the screen as flightscreenN.bmp, N the first number with no such file
 * (in the modern build, in the user folder). Builds a BMP palette from
 * g_sw_palette with each channel shifted up 2 bits, releases every lock on the
 * flight surface, flips, locks with g_flight_draw_to_hud_layer at 0, writes the
 * surface with front_image_save_bmp_file at 8 or 16 bits, unlocks, puts
 * g_flight_draw_to_hud_layer back and takes the locks again. */
// FUNCTION: XVT 0x411010
void flight_screenshot_capture(void)
{
	char file_name[64];
	uint8_t palette[1024];
	int file_index;
	int lock_count;
	int remaining_locks;
	int saved_lock_back_buffer_for_hud_draw;
	int i;

	file_index = 0;
	for (;;) {
		sprintf(file_name, "flightscreen%d.bmp", file_index);
#ifdef XVT_MODERN
		g_stream = xvt_storage_open_root(AERON_VFS_ROOT_USER, file_name,
						 g_file_mode_read_binary);
#else
		fe_disk_io_open_global_stream(file_name,
					      g_file_mode_read_binary, 0, 1);
#endif
		if (g_stream == NULL) {
			break;
		}
#ifdef XVT_MODERN
		file_close(g_stream);
#else
		FILE_RAW_CLOSE((xvt_file *)g_stream);
#endif
		++file_index;
	}

	for (i = 0; i < 256; ++i) {
		palette[4 * i + 0] = (uint8_t)(g_sw_palette[i].b << 2);
		palette[4 * i + 1] = (uint8_t)(g_sw_palette[i].g << 2);
		palette[4 * i + 2] = (uint8_t)(g_sw_palette[i].r << 2);
		palette[4 * i + 3] = 0;
	}

	lock_count = flight_surface_get_lock_count();
	if (lock_count > 0) {
		remaining_locks = lock_count;
		do {
			flight_surface_unlock();
			--remaining_locks;
		} while (remaining_locks != 0);
	}

	flight_display_flip();
	saved_lock_back_buffer_for_hud_draw = g_flight_draw_to_hud_layer;
	g_flight_draw_to_hud_layer = 0;
	flight_surface_lock();
	front_image_save_bmp_file(file_name, g_surface_pixels, g_surface_width,
				  g_surface_height, g_surface_pitch,
				  8 * g_flight_bytes_per_pixel,
				  display_is_pixel_format555(), palette);
	flight_surface_unlock();
	g_flight_draw_to_hud_layer = saved_lock_back_buffer_for_hud_draw;

	if (lock_count > 0) {
		do {
			flight_surface_lock();
			--lock_count;
		} while (lock_count != 0);
	}
}

/* Draws a line from x1, y1 to x2, y2 in color_idx on the 8-bit frame buffer,
 * clipped to the g_flightClip rectangle (right and bottom edges excluded),
 * stepping along the longer axis with a running error. Every line stops one
 * pixel short of its far end on the axis it steps along. Rows are
 * g_surface_pitch apart from g_flight_sw_framebuffer_base, not taken from the row
 * table. */
// FUNCTION: XVT 0x411120
void flight_sw_draw_line8bpp(int x1, int y1, int x2, int y2, uint8_t color_idx)
{
	int delta_x;
	int delta_y;
	int start_y;
	int end_x;
	int end_y;
	uint8_t *pixel;

	end_x = x2;
	delta_x = end_x - x1;
	if (delta_x < 0) {
		int swap_x;

		swap_x = x1;
		start_y = y2;
		end_y = y1;
		delta_x = -delta_x;
		x1 = end_x;
		end_x = swap_x;
	} else {
		if (delta_x == 0) {
			int count;

			if (x1 < g_flight_clip_left) {
				return;
			}
			if (x1 >= g_flight_clip_right) {
				return;
			}

			if (y2 < y1) {
				int swap_y;

				swap_y = y1;
				y1 = y2;
				y2 = swap_y;
			}
			if (y1 < g_flight_clip_top) {
				y1 = g_flight_clip_top;
			}
			if (y2 >= g_flight_clip_bottom) {
				y2 = g_flight_clip_bottom - 1;
			}

			count = y2 - y1;
			if (count > 0) {
				pixel = &g_flight_sw_framebuffer_base
						[y1 * g_surface_pitch + x1];
				while (count-- != 0) {
					*pixel = color_idx;
					pixel += g_surface_pitch;
				}
			}
			return;
		}
		start_y = y1;
		end_y = y2;
	}

	if (x1 < g_flight_clip_right) {
		if (end_x >= g_flight_clip_left) {
			delta_y = end_y - start_y;
			if (delta_y < 0) {
				delta_y = -delta_y;
				if (start_y < g_flight_clip_top) {
					return;
				}
				if (end_y >= g_flight_clip_bottom) {
					return;
				}

				if (start_y >= g_flight_clip_bottom) {
					int advance;

					advance = math2_ab_over_c32(
						start_y - g_flight_clip_bottom +
							1,
						delta_x, delta_y);
					x1 += advance;
					if (x1 >= g_flight_clip_right) {
						return;
					}
					start_y = g_flight_clip_bottom - 1;
				}
				if (x1 < g_flight_clip_left) {
					int advance;
					int clipped_x_distance;

					clipped_x_distance =
						g_flight_clip_left - x1;
					advance = math2_ab_over_c32(
						clipped_x_distance, delta_y,
						delta_x);
					start_y -= advance;
					if (start_y < g_flight_clip_top) {
						return;
					}
					x1 = g_flight_clip_left;
				}
				if (end_x >= g_flight_clip_right) {
					end_x = g_flight_clip_right - 1;
				}
				if (end_y < g_flight_clip_top) {
					end_y = g_flight_clip_top;
				}

				pixel = &g_flight_sw_framebuffer_base
						[start_y * g_surface_pitch +
						 x1];
				if (delta_x >= delta_y) {
					int error;
					int y_steps;
					int x_count;

					error = delta_x >> 1;
					x_count = end_x - x1;
					y_steps = start_y - end_y + 1;
					while (x_count-- != 0) {
						*pixel++ = color_idx;
						error -= delta_y;
						if (error < 0) {
							error += delta_x;
							--y_steps;
							if (y_steps == 0) {
								return;
							}
							pixel -=
								g_surface_pitch;
						}
					}
				} else {
					int error;
					int x_steps;
					int y_count;

					error = delta_y >> 1;
					x_steps = end_x - x1 + 1;
					y_count = start_y - end_y;
					while (y_count-- != 0) {
						*pixel = color_idx;
						pixel -= g_surface_pitch;
						error -= delta_x;
						if (error < 0) {
							error += delta_y;
							--x_steps;
							if (x_steps == 0) {
								return;
							}
							++pixel;
						}
					}
				}
			} else if (delta_y > 0) {
				if (start_y >= g_flight_clip_bottom) {
					return;
				}
				if (end_y < g_flight_clip_top) {
					return;
				}

				if (start_y < g_flight_clip_top) {
					int advance;
					int clipped_y_distance;

					clipped_y_distance =
						g_flight_clip_top - start_y;
					advance = math2_ab_over_c32(
						clipped_y_distance, delta_x,
						delta_y);
					x1 += advance;
					if (x1 >= g_flight_clip_right) {
						return;
					}
					start_y = g_flight_clip_top;
				}
				if (x1 < g_flight_clip_left) {
					int advance;
					int clipped_x_distance;

					clipped_x_distance =
						g_flight_clip_left - x1;
					advance = math2_ab_over_c32(
						clipped_x_distance, delta_y,
						delta_x);
					start_y += advance;
					if (start_y >= g_flight_clip_bottom) {
						return;
					}
					x1 = g_flight_clip_left;
				}
				if (end_x >= g_flight_clip_right) {
					end_x = g_flight_clip_right - 1;
				}
				if (end_y >= g_flight_clip_bottom) {
					end_y = g_flight_clip_bottom - 1;
				}

				pixel = &g_flight_sw_framebuffer_base
						[start_y * g_surface_pitch +
						 x1];
				if (delta_x >= delta_y) {
					int error;
					int y_steps;
					int x_count;

					error = delta_x >> 1;
					x_count = end_x - x1;
					y_steps = end_y - start_y + 1;
					while (x_count-- != 0) {
						*pixel++ = color_idx;
						error -= delta_y;
						if (error < 0) {
							error += delta_x;
							--y_steps;
							if (y_steps == 0) {
								return;
							}
							pixel +=
								g_surface_pitch;
						}
					}
				} else {
					int error;
					int x_steps;
					int y_count;

					error = delta_y >> 1;
					x_steps = end_x - x1 + 1;
					y_count = end_y - start_y;
					while (y_count-- != 0) {
						*pixel = color_idx;
						pixel += g_surface_pitch;
						error -= delta_x;
						if (error < 0) {
							error += delta_y;
							--x_steps;
							if (x_steps == 0) {
								return;
							}
							++pixel;
						}
					}
				}
			} else if (start_y >= g_flight_clip_top &&
				   start_y < g_flight_clip_bottom) {
				int count;

				if (g_flight_clip_left > x1) {
					x1 = g_flight_clip_left;
				}
				if (end_x >= g_flight_clip_right) {
					end_x = g_flight_clip_right - 1;
				}

				count = end_x - x1;
				if (count > 0) {
					pixel = g_flight_sw_framebuffer_base;
					pixel += start_y * g_surface_pitch;
					pixel += x1;
					while (count-- != 0) {
						*pixel++ = color_idx;
					}
				}
			}
		}
	}
}

/* Draws g_flight_sw_rot_sprite_span_run_countdown runs of a rotated sprite's line at
 * 8 bits, counting it down to 0: pixel i of a run goes at dest_base +
 * span_offsets[g_flight_sw_rot_sprite_span_base_x + start_x + i] in the run's color
 * index. Does not clip; the countdown and each length must be at least 1. */
// FUNCTION: XVT 0x4213E0
void flight_sw_draw_rot_sprite_span_runs8(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets)
{
	int color_index;
	int dest_offset;
	int span_x;
	int length;
	const int *run_offsets;

	for (;;) {
		color_index = runs->color_index;
		span_x = g_flight_sw_rot_sprite_span_base_x;
		span_x += runs->start_x;
		length = runs->length;
		runs++;
		run_offsets = &span_offsets[span_x];
		do {
			dest_offset = *run_offsets++;
			dest_base += dest_offset;
			*dest_base = (uint8_t)color_index;
			dest_base -= dest_offset;
			length--;
		} while (length != 0);
		g_flight_sw_rot_sprite_span_run_countdown--;
		if (g_flight_sw_rot_sprite_span_run_countdown == 0) {
			break;
		}
	}
}

/* Draws the runs as flight_sw_draw_rot_sprite_span_runs8 does, but only the part of
 * each from g_flight_sw_rot_sprite_clip_min_x up to, not including,
 * g_flight_sw_rot_sprite_clip_max_x. */
// FUNCTION: XVT 0x421430
void flight_sw_draw_clipped_rot_sprite_span_runs8(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets)
{
	int length;
	int span_start;
	int span_end;
	int color_index;
	const struct flight_sw_rot_sprite_span_run *draw_run;
	const int *run_offsets;
	int dest_offset;

	do {
		draw_run = runs++;
		length = draw_run->length;
		span_start =
			g_flight_sw_rot_sprite_span_base_x + draw_run->start_x;
		span_end = span_start + length;
		if (span_start < g_flight_sw_rot_sprite_clip_min_x) {
			span_start = g_flight_sw_rot_sprite_clip_min_x;
		}
		if (span_start <= g_flight_sw_rot_sprite_clip_max_x &&
		    span_end >= g_flight_sw_rot_sprite_clip_min_x) {
			if (span_end > g_flight_sw_rot_sprite_clip_max_x) {
				span_end = g_flight_sw_rot_sprite_clip_max_x;
			}
			length = span_end - span_start;
			if (length != 0) {
				color_index = draw_run->color_index;
				run_offsets = &span_offsets[span_start];
				do {
					dest_offset = *run_offsets++;
					dest_base[dest_offset] =
						(uint8_t)color_index;
					length--;
				} while (length != 0);
			}
		}
		g_flight_sw_rot_sprite_span_run_countdown--;
	} while (g_flight_sw_rot_sprite_span_run_countdown != 0);
}

/* The 16-bit form of flight_sw_draw_rot_sprite_span_runs8: writes the color index in
 * a pixel's low byte and 0x80 in its high byte, marking it for
 * flight_sw_blit_prepared_rotated_sprite_spans. */
// FUNCTION: XVT 0x4214A0
void flight_sw_draw_rot_sprite_span_runs16(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets)
{
	int color_index;
	int length;
	int span_x;
	const int *run_offsets;
	int dest_offset;

	do {
		span_x = g_flight_sw_rot_sprite_span_base_x;
		color_index = runs->color_index;
		span_x += runs->start_x;
		length = runs->length;
		runs++;
		run_offsets = &span_offsets[span_x];
		do {
			dest_offset = *run_offsets++;
			dest_base[dest_offset] = (uint8_t)color_index;
			dest_base[dest_offset + 1] = 0x80;
			length--;
		} while (length != 0);
		g_flight_sw_rot_sprite_span_run_countdown--;
	} while (g_flight_sw_rot_sprite_span_run_countdown != 0);
}

/* The 16-bit form of flight_sw_draw_clipped_rot_sprite_span_runs8, marking pixels as
 * flight_sw_draw_rot_sprite_span_runs16 does. */
// FUNCTION: XVT 0x4214F0
void flight_sw_draw_clipped_rot_sprite_span_runs16(
	const struct flight_sw_rot_sprite_span_run *runs, uint8_t *dest_base,
	const int *span_offsets)
{
	const struct flight_sw_rot_sprite_span_run *draw_run;
	int clip_min_x;
	int length;
	int color_index;
	int dest_offset;
	int clip_max_x;
	int span_start;
	int span_end;

	do {
		draw_run = runs++;
		length = draw_run->length;
		span_start =
			g_flight_sw_rot_sprite_span_base_x + draw_run->start_x;
		span_end = span_start + length;
		clip_min_x = g_flight_sw_rot_sprite_clip_min_x;
		clip_max_x = g_flight_sw_rot_sprite_clip_max_x;
		if (span_start < clip_min_x) {
			span_start = clip_min_x;
		}
		if (span_start <= clip_max_x && span_end >= clip_min_x) {
			if (span_end > clip_max_x) {
				span_end = clip_max_x;
			}
			length = span_end - span_start;
			if (length != 0) {
				color_index = draw_run->color_index;
				do {
					dest_offset = span_offsets[span_start];
					dest_base[dest_offset] =
						(uint8_t)color_index;
					dest_base[dest_offset + 1] = 0x80;
					span_start++;
					length--;
				} while (length != 0);
			}
		}
		g_flight_sw_rot_sprite_span_run_countdown--;
	} while (g_flight_sw_rot_sprite_span_run_countdown != 0);
}

/* Draws a rotated, scaled sprite on the software surface. Takes its first
 * corner from the data header, sets the scale for screen_size with
 * flight_sw_prepare_rotated_sprite_scale_state, and turns that corner to find the
 * screen point, from screen_x and screen_y, where the walk starts;
 * flight_sw_rasterize_prepared_rotated_sprite then draws the image into
 * g_flight_sw_rot_sprite_dest_buffer. Then turns the other three corners (the first
 * plus the width, plus the width less the height, and less the height) and
 * passes the four to flight_sw_clip_and_blit_prepared_rotated_sprite, which copies
 * what was drawn to the screen. flight_sw_prepare_sprite_rotation_tables must have
 * run for the angle. */
// FUNCTION: XVT 0x421560
void flight_sw_draw_rotated_sprite_quad(int16_t screen_x, int16_t screen_y,
					uint16_t screen_size,
					struct sprite_payload *sprite)
{
	struct flight_sw_rot_sprite_data_header *sprite_data;
	int16_t corner_x;
	int16_t corner_y;
	int corner_coords[8];

	sprite_data = (struct flight_sw_rot_sprite_data_header
			       *)((uint8_t *)sprite + sprite->row_data_offset);
	if (g_flight_sw_rot_sprite_span_runs_enabled == 1) {
		g_flight_sw_rot_sprite_input_corner_x =
			(int16_t)sprite_data->corner_x;
	} else {
		g_flight_sw_rot_sprite_input_corner_x =
			-(int16_t)sprite_data->alternate_corner_x;
	}
	corner_x = g_flight_sw_rot_sprite_input_corner_x;
	g_flight_sw_rot_sprite_input_corner_y = -(int16_t)sprite_data->corner_y;
	corner_y = g_flight_sw_rot_sprite_input_corner_y;
	flight_sw_prepare_rotated_sprite_scale_state(
		screen_size, g_flight_sw_rot_sprite_coeffs,
		&g_flight_sw_rot_sprite_scale_state);
	flight_sw_rotate_sprite_point(
		&g_flight_sw_rot_sprite_coeffs->rotation_angle,
		&g_flight_sw_rot_sprite_scale_state);
	g_flight_sw_rot_sprite_edge_cursor_x =
		screen_x + g_flight_sw_rot_sprite_output_offset_x;
	corner_coords[0] =
		(int16_t)(screen_x + g_flight_sw_rot_sprite_output_offset_x);
	g_flight_sw_rot_sprite_edge_cursor_y =
		screen_y + g_flight_sw_rot_sprite_output_offset_y;
	corner_coords[1] =
		(int16_t)(screen_y + g_flight_sw_rot_sprite_output_offset_y);
	flight_sw_rasterize_prepared_rotated_sprite(
		(uint8_t *)(sprite_data + 1), sprite->packing_mode);

	g_flight_sw_rot_sprite_input_corner_x =
		corner_x + (int16_t)sprite->width;
	g_flight_sw_rot_sprite_input_corner_y = corner_y;
	flight_sw_rotate_sprite_point(
		&g_flight_sw_rot_sprite_coeffs->rotation_angle,
		&g_flight_sw_rot_sprite_scale_state);
	corner_coords[2] = screen_x + g_flight_sw_rot_sprite_output_offset_x;
	corner_coords[3] = screen_y + g_flight_sw_rot_sprite_output_offset_y;

	g_flight_sw_rot_sprite_input_corner_x =
		corner_x + (int16_t)sprite->width;
	g_flight_sw_rot_sprite_input_corner_y =
		corner_y - (int16_t)sprite->height;
	flight_sw_rotate_sprite_point(
		&g_flight_sw_rot_sprite_coeffs->rotation_angle,
		&g_flight_sw_rot_sprite_scale_state);
	corner_coords[4] = screen_x + g_flight_sw_rot_sprite_output_offset_x;
	corner_coords[5] = screen_y + g_flight_sw_rot_sprite_output_offset_y;

	g_flight_sw_rot_sprite_input_corner_x = corner_x;
	g_flight_sw_rot_sprite_input_corner_y =
		corner_y - (int16_t)sprite->height;
	flight_sw_rotate_sprite_point(
		&g_flight_sw_rot_sprite_coeffs->rotation_angle,
		&g_flight_sw_rot_sprite_scale_state);
	corner_coords[6] = screen_x + g_flight_sw_rot_sprite_output_offset_x;
	corner_coords[7] = screen_y + g_flight_sw_rot_sprite_output_offset_y;
	flight_sw_clip_and_blit_prepared_rotated_sprite(corner_coords);
}

/* Turns the four corners' y into rows from the bottom (g_flight_vp_max_y minus y,
 * written back into corner_coords), takes their bounding box widened by 2 on
 * each side, clips it to the viewport, returning when it lies wholly outside,
 * and calls flight_sw_blit_prepared_rotated_sprite_spans on that box of
 * g_flight_sw_rot_sprite_dest_buffer. */
// FUNCTION: XVT 0x421700
void flight_sw_clip_and_blit_prepared_rotated_sprite(int *corner_coords)
{
	int min_x;
	int max_x;
	int min_y;
	int max_y;
	int raw_y2;
	int raw_y3;
	int raw_y4;
	int flipped_y2;
	int flipped_y3;
	int flipped_y4;
	int start_x;
	int start_y;
	int end_x;
	int end_y;

	max_y = g_flight_vp_max_y - corner_coords[1];
	raw_y2 = corner_coords[3];
	raw_y3 = corner_coords[5];
	corner_coords[1] = max_y;
	flipped_y2 = g_flight_vp_max_y - raw_y2;
	raw_y4 = corner_coords[7];
	corner_coords[3] = flipped_y2;
	flipped_y3 = g_flight_vp_max_y - raw_y3;
	corner_coords[5] = flipped_y3;
	min_y = max_y;
	flipped_y4 = g_flight_vp_max_y - raw_y4;
	corner_coords[7] = flipped_y4;

	min_x = corner_coords[0];
	max_x = corner_coords[0];
	if (min_x > corner_coords[2]) {
		min_x = corner_coords[2];
	}
	if (min_x > corner_coords[4]) {
		min_x = corner_coords[4];
	}
	if (min_x > corner_coords[6]) {
		min_x = corner_coords[6];
	}
	if (max_x < corner_coords[2]) {
		max_x = corner_coords[2];
	}
	if (max_x < corner_coords[4]) {
		max_x = corner_coords[4];
	}
	if (max_x < corner_coords[6]) {
		max_x = corner_coords[6];
	}

	if (min_y > flipped_y2) {
		min_y = flipped_y2;
	}
	if (min_y > flipped_y3) {
		min_y = flipped_y3;
	}
	if (min_y > flipped_y4) {
		min_y = flipped_y4;
	}
	if (max_y < flipped_y2) {
		max_y = flipped_y2;
	}
	if (max_y < flipped_y3) {
		max_y = flipped_y3;
	}
	if (max_y < flipped_y4) {
		max_y = flipped_y4;
	}

	start_x = min_x - 2;
	start_y = min_y - 2;
	end_x = max_x + 2;
	end_y = max_y + 2;
	if (end_y < 0 || start_y >= g_flight_sw_rot_sprite_viewport_height) {
		return;
	}
	if (end_y >= g_flight_sw_rot_sprite_viewport_height) {
		end_y = g_flight_sw_rot_sprite_viewport_max_y;
	}
	if (start_y < 0) {
		start_y = 0;
	}
	if (end_x < 0 || start_x >= g_flight_sw_rot_sprite_viewport_width) {
		return;
	}
	if (end_x >= g_flight_sw_rot_sprite_viewport_width) {
		end_x = g_flight_sw_rot_sprite_viewport_max_x;
	}
	if (start_x < 0) {
		start_x = 0;
	}

	flight_sw_blit_prepared_rotated_sprite_spans(
		g_flight_sw_rot_sprite_dest_buffer +
			g_flight_bytes_per_pixel * start_x +
			g_flight_sw_rot_sprite_dest_pitch_bytes * start_y,
		g_flight_sw_rot_sprite_dest_pitch_bytes +
			g_flight_bytes_per_pixel * (start_x - end_x),
		start_x, start_y, end_x, end_y);
}

/* Readies the rotated-sprite state for the current viewport and an angle:
 * copies the viewport's size and limits, sets the destination pitch and line,
 * g_flight_sw_rot_sprite_dest_y_mode to -1, square-pixel mode when g_proj_aspect_y is 0
 * and the axis-swap angle (0x2000, else 0x2200), and points
 * g_flight_sw_rot_sprite_coeffs at the cache. Rebuilds the cache with
 * flight_sw_build_sprite_rotation_coeffs when rotationAngle differs from the cached
 * angle, compared as signed 16-bit values, or
 * g_flight_sw_rot_sprite_coeff_cache_valid is 0. Ignores bytes_per_pixel. */
// FUNCTION: XVT 0x421850
void flight_sw_prepare_sprite_rotation_tables(int16_t rotation_angle,
					      int bytes_per_pixel)
{
	(void)bytes_per_pixel;
	g_flight_sw_rot_sprite_viewport_width = (int16_t)g_flight_vp_width;
	g_flight_sw_rot_sprite_dest_pitch_bytes =
		g_flight_bytes_per_pixel * g_flight_vp_width;
	g_flight_sw_rot_sprite_dest_line_ptr =
		g_flight_sw_rot_sprite_dest_buffer;
	g_flight_sw_rot_sprite_viewport_max_y = (int16_t)g_flight_vp_max_y;
	g_flight_sw_rot_sprite_viewport_max_x = (int16_t)g_flight_vp_max_x;
	g_flight_sw_rot_sprite_viewport_height = (int16_t)g_flight_vp_height;
	g_flight_sw_rot_sprite_square_pixel_mode = 0;
	g_flight_sw_rot_sprite_dest_y_mode = -1;
	if (g_proj_aspect_y == 0) {
		g_flight_sw_rot_sprite_square_pixel_mode = 1;
	}
	g_flight_sw_rot_sprite_coeffs = &g_flight_sw_rot_sprite_coeff_cache;
	g_flight_sw_rot_sprite_axis_swap_threshold_angle = 0x2000;
	if (g_flight_sw_rot_sprite_square_pixel_mode != 1) {
		g_flight_sw_rot_sprite_axis_swap_threshold_angle = 0x2200;
	}

	if ((int16_t)g_flight_sw_rot_sprite_coeff_cache.rotation_angle !=
		    rotation_angle ||
	    g_flight_sw_rot_sprite_coeff_cache_valid == 0) {
		flight_sw_build_sprite_rotation_coeffs(
			rotation_angle,
			&g_flight_sw_rot_sprite_coeff_cache.rotation_angle);
		g_flight_sw_rot_sprite_coeff_cache_valid = 1;
	}
}

/* Copies the sprite's colorCount drawing colors: at 16 bits each color's low
 * and high byte into g_flight_sw_rot_sprite_palette16_low and
 * g_flight_sw_rot_sprite_palette16_high, else one byte each into
 * g_flight_sw_rot_sprite_palette8. Returns colorCount, or 0 when it is negative.
 * Does not check colorCount against 256. */
// FUNCTION: XVT 0x421930
int flight_sw_load_sprite_palette_tables(struct sprite_payload *sprite)
{
	uint8_t *palette;
	int color_count;
	int color_index;

	color_count = sprite->color_count;
	palette = (uint8_t *)sprite + sprite->display_palette_offset;
	if (g_flight_bytes_per_pixel == 2) {
		for (color_index = 0; color_index < color_count;
		     color_index++) {
			g_flight_sw_rot_sprite_palette16_low[color_index] =
				*palette++;
			g_flight_sw_rot_sprite_palette16_high[color_index] =
				*palette++;
		}
	} else {
		for (color_index = 0; color_index < color_count;
		     color_index++) {
			g_flight_sw_rot_sprite_palette8[color_index] =
				*palette++;
		}
	}

	return color_index;
}

/* Returns entry angle >> 6 of g_flight_sw_tangent91_pct when scale_percent is 91,
 * of g_flight_sw_tangent110_pct when it is 110, else of g_flight_sw_tangent100_pct.
 * Does not check the entry against the table's size. */
// FUNCTION: XVT 0x421980
uint16_t flight_sw_lookup_scaled_tangent(uint16_t angle, int16_t scale_percent)
{
	angle >>= 6;
	if (scale_percent == 91) {
		return g_flight_sw_tangent91_pct[angle];
	}
	if (scale_percent == 110) {
		return g_flight_sw_tangent110_pct[angle];
	}
	return g_flight_sw_tangent100_pct[angle];
}

/* Sets scale_state for a sprite of screen_size: the screen scale; the aspect
 * scales, 256 and 256 with square pixels, else 233 and 282, setting
 * g_flight_sw_rot_sprite_axis_swap_threshold_angle to 0x2000 or 0x2200 as well; the
 * horizontal step, (screen_size * primary_cos_magnitude_q16) >> 16, times
 * aspect_scale_y >> 8 with primary_axis_swap; and the vertical step, the base step
 * plus (base * secondary_step_byte) >> 8, times inverse_aspect_scale_y >> 8 without
 * secondary_axis_swap. Rebuilds the run-width tables when the horizontal step
 * differs from the one they were built for. */
// FUNCTION: XVT 0x4219D0
void flight_sw_prepare_rotated_sprite_scale_state(
	uint16_t screen_size,
	struct flight_sw_rot_sprite_coeff_state *rotation_coeffs,
	struct flight_sw_rot_sprite_scale_state *scale_state)
{
	uint16_t *aspect_scale_y;
	uint16_t *inverse_aspect_scale_y;
	unsigned int base_horizontal_step;
	unsigned int vertical_step;
	int square_pixel_mode;
	uint8_t horizontal_step_low;
	uint8_t cached_step_low;

	scale_state->screen_scale = screen_size;
	aspect_scale_y = &scale_state->aspect_scale_y;
	square_pixel_mode = g_flight_sw_rot_sprite_square_pixel_mode;
	if (square_pixel_mode == 1) {
		*aspect_scale_y = 256;
		inverse_aspect_scale_y = &scale_state->inverse_aspect_scale_y;
		*inverse_aspect_scale_y = 256;
		g_flight_sw_rot_sprite_axis_swap_threshold_angle = 0x2000;
	} else {
		*aspect_scale_y = 233;
		inverse_aspect_scale_y = &scale_state->inverse_aspect_scale_y;
		*inverse_aspect_scale_y = 282;
		g_flight_sw_rot_sprite_axis_swap_threshold_angle = 0x2200;
	}

	base_horizontal_step = ((unsigned int)screen_size *
				rotation_coeffs->primary_cos_magnitude_q16) >>
			       16;
	scale_state->horizontal_step_low_byte = (uint8_t)base_horizontal_step;
	scale_state->horizontal_step_high_byte =
		(uint8_t)(base_horizontal_step >> 8);
	if (rotation_coeffs->primary_axis_swap != 0) {
		unsigned int scaled_horizontal_step;

		scaled_horizontal_step =
			(base_horizontal_step * *aspect_scale_y) >> 8;
		scale_state->horizontal_step_low_byte =
			(uint8_t)scaled_horizontal_step;
		scale_state->horizontal_step_high_byte =
			(uint8_t)(scaled_horizontal_step >> 8);
	}
	vertical_step = ((base_horizontal_step *
			  rotation_coeffs->secondary_step_byte) >>
			 8) +
			base_horizontal_step;
	if (rotation_coeffs->secondary_axis_swap == 0) {
		vertical_step = (vertical_step * *inverse_aspect_scale_y) >> 8;
	}
	horizontal_step_low = scale_state->horizontal_step_low_byte;
	cached_step_low = scale_state->cached_step_low_byte;
	scale_state->vertical_step_low_byte = (uint8_t)vertical_step;
	scale_state->vertical_step_high_byte = (uint8_t)(vertical_step >> 8);
	if (cached_step_low != horizontal_step_low ||
	    scale_state->cached_step_high_byte !=
		    scale_state->horizontal_step_high_byte) {
		uint16_t table_index;
		unsigned int packed_accumulator;
		unsigned int packed_step;
		uint8_t horizontal_step_high;

		horizontal_step_high = scale_state->horizontal_step_high_byte;
		scale_state->cached_step_low_byte = horizontal_step_low;
		scale_state->cached_step_high_byte = horizontal_step_high;
		packed_accumulator =
			((unsigned int)horizontal_step_high << 16) |
			((unsigned int)horizontal_step_low << 8);
		packed_step = packed_accumulator;
		table_index = 0;
		do {
			scale_state->low_word_step_table[table_index] =
				(uint16_t)packed_accumulator;
			scale_state->high_word_step_table[table_index] =
				(uint16_t)(packed_accumulator >> 16);
			packed_accumulator += packed_step;
			++table_index;
		} while (table_index < 256);
	}
}

/* Turns and scales the sprite point in g_flight_sw_rot_sprite_input_corner_x and Y
 * (texels) into g_flight_sw_rot_sprite_output_offset_x and Y (pixels from the
 * sprite's screen point). Scales the magnitudes by screen_scale over 256 with
 * rounding, y first by inverse_aspect_scale_y over 256; then x' = (x * cos + y *
 * sin + 0x8000) >> 16 and y' = (x * sin - y * cos + 0x8000) >> 16, with signed
 * sine and cosine magnitudes from rotation_coeffs (65536 standing for 1); then
 * scales y' by aspect_scale_y over 256 with rounding. Leaves the input's
 * magnitudes in the input globals. */
// FUNCTION: XVT 0x421AE0
void flight_sw_rotate_sprite_point(
	uint16_t *rotation_coeffs,
	struct flight_sw_rot_sprite_scale_state *scale_state)
{
	int16_t original_corner_x;
	int16_t original_corner_y;
	int scaled_x;
	int rotated_x_from_x;
	int scaled_y;
	int rotated_x_from_y;
	int rotated_y_from_x;
	int rotated_y_from_y;
	int rotated_y;
	int final_y;

	original_corner_x = g_flight_sw_rot_sprite_input_corner_x;
	if (g_flight_sw_rot_sprite_input_corner_x < 0) {
		g_flight_sw_rot_sprite_input_corner_x =
			-g_flight_sw_rot_sprite_input_corner_x;
	}
	scaled_x = (g_flight_sw_rot_sprite_input_corner_x *
			    scale_state->screen_scale +
		    128) >>
		   8;
	original_corner_y = g_flight_sw_rot_sprite_input_corner_y;
	if (g_flight_sw_rot_sprite_input_corner_y < 0) {
		g_flight_sw_rot_sprite_input_corner_y =
			-g_flight_sw_rot_sprite_input_corner_y;
	}
	scaled_y = (g_flight_sw_rot_sprite_input_corner_y *
			    scale_state->inverse_aspect_scale_y +
		    128) >>
		   8;
	scaled_y = (scaled_y * scale_state->screen_scale + 128) >> 8;

	rotated_x_from_x = (uint16_t)scaled_x * rotation_coeffs[3];
	if (((original_corner_x ^ rotation_coeffs[4]) & 0x8000) != 0) {
		rotated_x_from_x = -rotated_x_from_x;
	}
	rotated_x_from_y = (uint16_t)scaled_y * rotation_coeffs[1];
	if (((original_corner_y ^ rotation_coeffs[2]) & 0x8000) != 0) {
		rotated_x_from_y = -rotated_x_from_y;
	}
	rotated_x_from_x += rotated_x_from_y + 0x8000;
	g_flight_sw_rot_sprite_output_offset_x = rotated_x_from_x >> 16;

	rotated_y_from_x = (uint16_t)scaled_x * rotation_coeffs[1];
	if (((original_corner_x ^ rotation_coeffs[2]) & 0x8000) != 0) {
		rotated_y_from_x = -rotated_y_from_x;
	}
	rotated_y_from_y = (uint16_t)scaled_y * rotation_coeffs[3];
	if (((original_corner_y ^ rotation_coeffs[4]) & 0x8000) == 0) {
		rotated_y_from_y = -rotated_y_from_y;
	}
	rotated_y_from_x += rotated_y_from_y + 0x8000;
	rotated_y = rotated_y_from_x >> 16;
	g_flight_sw_rot_sprite_output_offset_y = rotated_y;
	if ((int16_t)rotated_y < 0) {
		g_flight_sw_rot_sprite_output_offset_y = -(int16_t)rotated_y;
	}
	final_y = (g_flight_sw_rot_sprite_output_offset_y *
			   scale_state->aspect_scale_y +
		   128) >>
		  8;
	if ((int16_t)rotated_y < 0) {
		final_y = -final_y;
	}
	g_flight_sw_rot_sprite_output_offset_y = final_y;
}

/* Fills the flight_sw_rot_sprite_coeff_state at out_coeffs for rotationAngle: the
 * sign bits and flips; the angle folded into a quarter turn and its sine and
 * cosine magnitudes; whether the edge steps along y (folded angle at or over
 * g_flight_sw_rot_sprite_axis_swap_threshold_angle, folding it once more); the edge's
 * scan_count points from (0, 0), one step along the main axis each, the cross
 * axis following a 16-bit fraction that starts at one half and grows by the
 * tangent (91, 100 or 110 percent by pixel shape); the runs of points that
 * share a cross coordinate; the secondary scale for the angle a quarter turn
 * on; the octant; the first and last points and the extents in entry 0; and
 * each point's destination byte offset with the flips applied. */
// FUNCTION: XVT 0x421C50
void flight_sw_build_sprite_rotation_coeffs(uint16_t rotation_angle,
					    uint16_t *out_coeffs)
{
	struct flight_sw_rot_sprite_coeff_state *coeffs;
	uint16_t primary_angle;
	uint16_t primary_step;
	uint16_t secondary_angle;
	uint16_t primary_step_reciprocal;
	uint16_t flip_y;
	uint16_t flip_x;
	int16_t primary_axis_swap;
	int16_t secondary_axis_swap;
	uint16_t point_index;
	uint16_t run_index;
	uint16_t remaining;
	uint16_t span_index;

	coeffs = (struct flight_sw_rot_sprite_coeff_state *)out_coeffs;
	coeffs->rotation_angle = rotation_angle;
	coeffs->sin_sign_mask = rotation_angle & 0x8000;
	flip_y = 0;
	coeffs->cos_sign_mask = (rotation_angle + 0x4000) & 0x8000;
	flip_x = 0;
	if (rotation_angle >= 0x8000) {
		rotation_angle &= 0x7FFF;
		flip_y = 1;
		if (rotation_angle < 0x4000) {
			flip_x = 2;
		}
	} else if (rotation_angle >= 0x4000) {
		flip_x = 2;
	}
	coeffs->flip_y = flip_y;
	coeffs->flip_x = flip_x;

	if (rotation_angle >= 0x4000) {
		primary_angle = 0x8000 - rotation_angle;
	} else {
		primary_angle = rotation_angle;
	}
	coeffs->sin_magnitude_q16 =
		flight_sw_lookup_sprite_sine_magnitude_q16(primary_angle);
	coeffs->cos_magnitude_q16 = flight_sw_lookup_sprite_sine_magnitude_q16(
		primary_angle + 0x4000);

	if (primary_angle < g_flight_sw_rot_sprite_axis_swap_threshold_angle) {
		primary_axis_swap = 0;
		coeffs->primary_axis_swap = primary_axis_swap;
		if (g_flight_sw_rot_sprite_square_pixel_mode == 1) {
			primary_step = flight_sw_lookup_scaled_tangent(
				primary_angle, 100);
		} else {
			primary_step = flight_sw_lookup_scaled_tangent(
				primary_angle, 91);
		}
	} else {
		primary_axis_swap = 4;
		coeffs->primary_axis_swap = primary_axis_swap;
		primary_angle = 0x4000 - primary_angle;
		if (g_flight_sw_rot_sprite_square_pixel_mode == 1) {
			primary_step = flight_sw_lookup_scaled_tangent(
				primary_angle, 100);
		} else {
			primary_step = flight_sw_lookup_scaled_tangent(
				primary_angle, 110);
		}
	}
	coeffs->primary_cos_magnitude_q16 =
		flight_sw_lookup_sprite_sine_magnitude_q16(primary_angle +
							   0x4000);
	primary_step_reciprocal =
		(uint16_t)(0x80000000u / coeffs->primary_cos_magnitude_q16);
	if (primary_axis_swap != 0 &&
	    g_flight_sw_rot_sprite_square_pixel_mode == 0) {
		primary_step_reciprocal =
			(uint16_t)(((unsigned int)primary_step_reciprocal *
					    g_proj_aspect_y +
				    0x8000) >>
				   16);
	}
	coeffs->primary_step_reciprocal = primary_step_reciprocal;

	coeffs->edge_points_with_predecessor[1].x = 0;
	coeffs->edge_points_with_predecessor[1].y = 0;
	if (coeffs->primary_axis_swap == 0) {
		uint16_t accumulator;
		uint16_t remaining_columns;
		int16_t edge_x;
		int16_t edge_y;

		edge_x = 0;
		edge_y = 0;
		accumulator = 0x8000;
		point_index = 2;
		coeffs->scan_count = g_flight_sw_rot_sprite_viewport_width;
		remaining_columns = g_flight_sw_rot_sprite_viewport_width - 1;
		while (remaining_columns-- != 0) {
			uint16_t previous_accumulator;

			++edge_x;
			previous_accumulator = accumulator;
			accumulator += primary_step;
			if (previous_accumulator > accumulator) {
				++edge_y;
			}
			coeffs->edge_points_with_predecessor[point_index].x =
				edge_x;
			coeffs->edge_points_with_predecessor[point_index].y =
				edge_y;
			++point_index;
		}
	} else {
		uint16_t accumulator;
		uint16_t remaining_rows;
		int16_t edge_x;
		int16_t edge_y;

		accumulator = 0x8000;
		edge_x = 0;
		edge_y = 0;
		coeffs->scan_count = g_flight_sw_rot_sprite_viewport_height;
		point_index = 2;
		remaining_rows = g_flight_sw_rot_sprite_viewport_height - 1;
		while (remaining_rows-- != 0) {
			uint16_t previous_accumulator;

			++edge_y;
			previous_accumulator = accumulator;
			accumulator += primary_step;
			if (previous_accumulator > accumulator) {
				++edge_x;
			}
			coeffs->edge_points_with_predecessor[point_index].x =
				edge_x;
			coeffs->edge_points_with_predecessor[point_index].y =
				edge_y;
			++point_index;
		}
	}

	point_index = 1;
	run_index = 0;
	remaining = coeffs->scan_count;
	while (remaining != 0) {
		uint16_t run_length;
		int16_t coordinate;

		run_length = 1;
		if (coeffs->primary_axis_swap != 0) {
			coordinate = coeffs->edge_points_with_predecessor
					     [point_index]
						     .x;
		} else {
			coordinate = coeffs->edge_points_with_predecessor
					     [point_index]
						     .y;
		}
		--remaining;
		while (remaining != 0) {
			int16_t next_coordinate;

			if (coeffs->primary_axis_swap != 0) {
				next_coordinate =
					coeffs->edge_points_with_predecessor
						[point_index + 1]
							.x;
			} else {
				next_coordinate =
					coeffs->edge_points_with_predecessor
						[point_index + 1]
							.y;
			}
			if (next_coordinate != coordinate) {
				break;
			}
			++run_length;
			++point_index;
			--remaining;
		}
		++point_index;
		coeffs->run_lengths[run_index] = run_length;
		++run_index;
	}
	coeffs->run_length_count = run_index;

	secondary_angle = (rotation_angle + 0x4000) & 0x7FFF;
	if (secondary_angle >= 0x4000) {
		secondary_angle = 0x8000 - secondary_angle;
	}
	if (secondary_angle <
	    g_flight_sw_rot_sprite_axis_swap_threshold_angle) {
		secondary_axis_swap = 0;
		coeffs->secondary_axis_swap = secondary_axis_swap;
		if (g_flight_sw_rot_sprite_square_pixel_mode == 1) {
			coeffs->secondary_scale_low =
				flight_sw_lookup_scaled_tangent(secondary_angle,
								100);
		} else {
			coeffs->secondary_scale_low =
				flight_sw_lookup_scaled_tangent(secondary_angle,
								91);
		}
	} else {
		secondary_axis_swap = 4;
		coeffs->secondary_axis_swap = secondary_axis_swap;
		secondary_angle = 0x4000 - secondary_angle;
		if (g_flight_sw_rot_sprite_square_pixel_mode == 1) {
			coeffs->secondary_scale_low =
				flight_sw_lookup_scaled_tangent(secondary_angle,
								100);
		} else {
			coeffs->secondary_scale_low =
				flight_sw_lookup_scaled_tangent(secondary_angle,
								110);
		}
	}
	coeffs->secondary_scale_high = 0;
	if (secondary_axis_swap != primary_axis_swap ||
	    g_flight_sw_rot_sprite_square_pixel_mode != 0) {
		coeffs->secondary_step_byte =
			(uint16_t)((0x800000u +
				    (unsigned int)primary_step *
					    coeffs->secondary_scale_low) >>
				   24);
	} else if ((coeffs->rotation_angle >= 0xE000 ||
		    coeffs->rotation_angle < 0xA000) &&
		   (coeffs->rotation_angle >= 0x6000 ||
		    coeffs->rotation_angle < 0x2000)) {
		coeffs->secondary_scale_low = 256;
		coeffs->secondary_scale_high = 256;
		coeffs->secondary_step_byte =
			(uint16_t)(((0xD800u * primary_step) >> 16) >> 8);
	} else {
		unsigned int secondary_scale;

		secondary_scale =
			(unsigned int)(uint16_t)(0x1000000 /
						 coeffs->secondary_scale_low)
			<< 8;
		coeffs->secondary_scale_low = (uint16_t)secondary_scale;
		coeffs->secondary_scale_high =
			(uint16_t)(secondary_scale >> 16);
		coeffs->secondary_step_byte = primary_step >> 8;
	}

	coeffs->octant =
		coeffs->primary_axis_swap | coeffs->flip_y | coeffs->flip_x;
	coeffs->flip_count = (coeffs->flip_x >> 1) + coeffs->flip_y;
	coeffs->first_edge_x = coeffs->edge_points_with_predecessor[1].x;
	coeffs->first_edge_y = coeffs->edge_points_with_predecessor[1].y;
	coeffs->first_edge_screen_y =
		g_flight_sw_rot_sprite_viewport_max_y - coeffs->first_edge_y;
	coeffs->last_edge_x =
		coeffs->edge_points_with_predecessor[coeffs->scan_count].x;
	coeffs->last_edge_y =
		coeffs->edge_points_with_predecessor[coeffs->scan_count].y;
	coeffs->last_edge_screen_y =
		g_flight_sw_rot_sprite_viewport_max_y - coeffs->last_edge_y;
	coeffs->edge_points_with_predecessor[0].x =
		coeffs->last_edge_x - coeffs->first_edge_x;
	if (coeffs->edge_points_with_predecessor[0].x < 0) {
		coeffs->edge_points_with_predecessor[0].x =
			-coeffs->edge_points_with_predecessor[0].x;
	}
	coeffs->edge_points_with_predecessor[0].y =
		coeffs->last_edge_screen_y - coeffs->first_edge_screen_y;
	if (coeffs->edge_points_with_predecessor[0].y < 0) {
		coeffs->edge_points_with_predecessor[0].y =
			-coeffs->edge_points_with_predecessor[0].y;
	}

	if (g_flight_bytes_per_pixel == 2) {
		for (span_index = 0; span_index < coeffs->scan_count;
		     ++span_index) {
			int16_t x;
			int16_t y;

			x = coeffs->edge_points_with_predecessor[span_index + 1]
				    .x;
			if (coeffs->flip_x != 0) {
				x = -x;
			}
			y = coeffs->edge_points_with_predecessor[span_index + 1]
				    .y;
			if (coeffs->flip_y != 0) {
				y = -y;
			}
			if (g_flight_sw_rot_sprite_dest_y_mode > 0) {
				coeffs->span_offsets[span_index] =
					g_flight_sw_rot_sprite_dest_pitch_bytes *
						y +
					2 * x;
			} else {
				coeffs->span_offsets[span_index] =
					2 * x -
					g_flight_sw_rot_sprite_dest_pitch_bytes *
						y;
			}
		}
	} else {
		for (span_index = 0; span_index < coeffs->scan_count;
		     ++span_index) {
			int16_t x;
			int16_t y;

			x = coeffs->edge_points_with_predecessor[span_index + 1]
				    .x;
			if (coeffs->flip_x != 0) {
				x = -x;
			}
			y = coeffs->edge_points_with_predecessor[span_index + 1]
				    .y;
			if (coeffs->flip_y != 0) {
				y = -y;
			}
			if (g_flight_sw_rot_sprite_dest_y_mode > 0) {
				coeffs->span_offsets[span_index] =
					g_flight_sw_rot_sprite_dest_pitch_bytes *
						y +
					x;
			} else {
				coeffs->span_offsets[span_index] =
					x -
					g_flight_sw_rot_sprite_dest_pitch_bytes *
						y;
			}
		}
	}
}

/* Draws the encoded sprite rows into g_flight_sw_rot_sprite_dest_buffer along the
 * rotated edge. Sets g_flight_sw_rot_sprite_span_runs_enabled to 1, then returns at
 * once when flight_sw_init_rot_sprite_for_current_octant finds nothing to draw; else
 * saves the start state in the four g_flightSwRotSpriteSaved globals and points
 * the walk at its first line. Each row, until a 0xFF, is decoded up to its
 * 0xFE: 0xFB sets a palette base from the next two bytes, low first; 0xFC skips
 * the next byte + 1 texels; 0xFD is a run of the next byte + 1 texels of the
 * color after it; any other byte is a run of (byte & mask) + 1 texels of color
 * palette base + (byte >> shift) for packing_mode. Each run's width comes from
 * the step tables. The row is then drawn on each of the n lines the vertical
 * step gives it, stepping a line after each with the octant step function
 * (returning when that returns 0) and flight_sw_advance_rot_sprite_secondary_scale;
 * with n of 0 it is drawn once without stepping. A row with runs is drawn only
 * while g_flight_sw_rot_sprite_clip_max_x is 0 or more: unclipped when its positions
 * run from at least 0 and g_flight_sw_rot_sprite_clip_min_x to under
 * g_flight_sw_rot_sprite_clip_max_x, else clipped. */
// FUNCTION: XVT 0x422170
void flight_sw_rasterize_prepared_rotated_sprite(uint8_t *sprite_data,
						 int packing_mode)
{
	uint8_t color_index;
	uint8_t scale_overflow;
	uint8_t scale_low;
	uint8_t scale_high;
	int16_t rows_remaining;
	int16_t rows_to_draw;
	uint16_t previous_fraction;
	int palette_base;
	int previous_scaled_x;
	uint8_t next_scale_overflow;
	uint8_t next_scale_high;
	uint8_t previous_scale_high;
	unsigned int previous_scale_position;
	uint8_t token;
	uint8_t run_length;
	unsigned int run_index;
	int span_run_count;
	int scaled_x;
	uint16_t scaled_fraction;
	uint8_t *cursor;

	g_flight_sw_rot_sprite_span_runs_enabled = 1;
	if (flight_sw_init_rot_sprite_for_current_octant() == 0) {
		return;
	}

	g_flight_sw_rot_sprite_saved_primary_edge_x =
		g_flight_sw_rot_sprite_primary_edge_x;
	g_flight_sw_rot_sprite_saved_primary_edge_y =
		g_flight_sw_rot_sprite_primary_edge_y;
	g_flight_sw_rot_sprite_saved_clip_min_x =
		g_flight_sw_rot_sprite_clip_min_x;
	g_flight_sw_rot_sprite_saved_clip_max_x =
		g_flight_sw_rot_sprite_clip_max_x;
	if (g_flight_sw_rot_sprite_dest_y_mode > 0) {
		g_flight_sw_rot_sprite_coeffs->dest_line_ptr =
			&g_flight_sw_rot_sprite_dest_line_ptr
				[g_flight_bytes_per_pixel *
					 g_flight_sw_rot_sprite_primary_edge_x +
				 g_flight_sw_rot_sprite_dest_pitch_bytes *
					 g_flight_sw_rot_sprite_primary_edge_y];
		g_flight_sw_rot_sprite_coeffs->dest_pitch_delta =
			-g_flight_sw_rot_sprite_dest_pitch_bytes;
	} else {
		g_flight_sw_rot_sprite_coeffs->dest_line_ptr =
			&g_flight_sw_rot_sprite_dest_line_ptr
				[g_flight_sw_rot_sprite_dest_pitch_bytes *
					 (g_flight_sw_rot_sprite_viewport_max_y -
					  g_flight_sw_rot_sprite_primary_edge_y) +
				 g_flight_bytes_per_pixel *
					 g_flight_sw_rot_sprite_primary_edge_x];
		g_flight_sw_rot_sprite_coeffs->dest_pitch_delta =
			g_flight_sw_rot_sprite_dest_pitch_bytes;
	}

	scale_low = 0;
	scale_high = 0;
	scale_overflow = 0;
	cursor = sprite_data;
	palette_base = 0;
	g_flight_sw_rot_sprite_skip_secondary_scale_step = 1;
	g_flight_sw_rot_sprite_secondary_scale_accum = 0;
	g_flight_sw_rot_sprite_dest_line_ptr =
		g_flight_sw_rot_sprite_coeffs->dest_line_ptr;
	while (*cursor != 0xFF) {
		previous_scale_position =
			((unsigned int)scale_overflow << 8) + scale_high;
		next_scale_overflow = scale_overflow;
		next_scale_high = scale_high;
		if ((uint8_t)(scale_low + g_flight_sw_rot_sprite_scale_state
						  .vertical_step_low_byte) <
		    scale_low) {
			++next_scale_high;
			if (scale_high == 0xFF) {
				++next_scale_overflow;
			}
		}
		previous_scale_high = next_scale_high;
		next_scale_high += g_flight_sw_rot_sprite_scale_state
					   .vertical_step_high_byte;
		if (next_scale_high < previous_scale_high) {
			++next_scale_overflow;
		}
		rows_remaining =
			(int16_t)(((unsigned int)next_scale_overflow << 8) +
				  next_scale_high - previous_scale_position);
		rows_to_draw = rows_remaining;

		span_run_count = 0;
		scaled_x = 0;
		scaled_fraction = 0;
		if (g_flight_sw_rot_sprite_span_runs_enabled == 1) {
			while (*cursor != 0xFE) {
				token = *cursor;
				if (token == 0xFB) {
					palette_base =
						cursor[1] + (cursor[2] << 8);
					cursor += 3;
				} else if (token == 0xFC) {
					previous_fraction = scaled_fraction;
					run_index = cursor[1];
					cursor += 2;
					scaled_x +=
						g_flight_sw_rot_sprite_scale_state
							.high_word_step_table
								[run_index];
					scaled_fraction +=
						g_flight_sw_rot_sprite_scale_state
							.low_word_step_table
								[run_index];
					if (scaled_fraction <
					    previous_fraction) {
						++scaled_x;
					}
				} else {
					if (token == 0xFD) {
						run_length = cursor[1];
						color_index = cursor[2];
						cursor += 3;
					} else {
						++cursor;
						color_index =
							(uint8_t)(palette_base +
								  (token >>
								   g_flight_sw_rle_palette_shift_by_packing_mode
									   [packing_mode]));
						run_length =
							token &
							g_flight_sw_rle_run_length_mask_by_packing_mode
								[packing_mode];
					}
					previous_fraction = scaled_fraction;
					previous_scaled_x = scaled_x;
					run_index = run_length;
					scaled_x +=
						g_flight_sw_rot_sprite_scale_state
							.high_word_step_table
								[run_index];
					scaled_fraction +=
						g_flight_sw_rot_sprite_scale_state
							.low_word_step_table
								[run_index];
					if (scaled_fraction <
					    previous_fraction) {
						++scaled_x;
					}
					g_flight_sw_rot_sprite_span_runs
						[span_run_count]
							.start_x =
						previous_scaled_x;
					g_flight_sw_rot_sprite_span_runs
						[span_run_count]
							.color_index =
						color_index;
					g_flight_sw_rot_sprite_span_runs
						[span_run_count]
							.length =
						scaled_x - previous_scaled_x +
						1;
					++span_run_count;
				}
			}
			++cursor;
		}

		do {
			if (span_run_count != 0 &&
			    g_flight_sw_rot_sprite_clip_max_x >= 0) {
				if ((int16_t)(scaled_x +
					      g_flight_sw_rot_sprite_span_base_x) >=
					    0 &&
				    g_flight_sw_rot_sprite_clip_max_x >
					    (int16_t)(scaled_x +
						      g_flight_sw_rot_sprite_span_base_x) &&
				    g_flight_sw_rot_sprite_span_base_x >= 0 &&
				    g_flight_sw_rot_sprite_span_base_x >=
					    g_flight_sw_rot_sprite_clip_min_x) {
					g_flight_sw_rot_sprite_span_run_countdown =
						span_run_count;
					if (g_flight_bytes_per_pixel == 2) {
						flight_sw_draw_rot_sprite_span_runs16(
							g_flight_sw_rot_sprite_span_runs,
							g_flight_sw_rot_sprite_dest_line_ptr,
							g_flight_sw_rot_sprite_coeffs
								->span_offsets);
					} else {
						flight_sw_draw_rot_sprite_span_runs8(
							g_flight_sw_rot_sprite_span_runs,
							g_flight_sw_rot_sprite_dest_line_ptr,
							g_flight_sw_rot_sprite_coeffs
								->span_offsets);
					}
				} else {
					g_flight_sw_rot_sprite_span_run_countdown =
						span_run_count;
					if (g_flight_bytes_per_pixel == 2) {
						flight_sw_draw_clipped_rot_sprite_span_runs16(
							g_flight_sw_rot_sprite_span_runs,
							g_flight_sw_rot_sprite_dest_line_ptr,
							g_flight_sw_rot_sprite_coeffs
								->span_offsets);
					} else {
						flight_sw_draw_clipped_rot_sprite_span_runs8(
							g_flight_sw_rot_sprite_span_runs,
							g_flight_sw_rot_sprite_dest_line_ptr,
							g_flight_sw_rot_sprite_coeffs
								->span_offsets);
					}
				}
			}
			if (rows_to_draw != 0) {
				if (flight_sw_step_rot_sprite_for_current_octant() ==
				    0) {
					return;
				}
				flight_sw_advance_rot_sprite_secondary_scale();
			}
			--rows_remaining;
		} while (rows_remaining != 0 && rows_to_draw != 0);

		{
			uint8_t previous_scale_low;

			previous_scale_low = scale_low;
			scale_low += g_flight_sw_rot_sprite_scale_state
					     .vertical_step_low_byte;
			if (scale_low < previous_scale_low) {
				++scale_high;
			}
		}
		previous_scale_high = scale_high;
		scale_high += g_flight_sw_rot_sprite_scale_state
				      .vertical_step_high_byte;
		if (scale_high < previous_scale_high) {
			++scale_overflow;
		}
	}
}

/* Moves the span base along the edge as lines advance. When
 * g_flight_sw_rot_sprite_skip_secondary_scale_step is set it clears it and returns.
 * Else adds secondary_scale_low to g_flight_sw_rot_sprite_secondary_scale_accum; with
 * secondary_scale_high 0 it goes on only on a carry, moving 1, otherwise it moves
 * 1, or 2 on a carry. It moves g_flight_sw_rot_sprite_span_base_x up when flip_count
 * is 1 without primary_axis_swap, or is not 1 with it, else down, and sets the
 * skip flag when the edge point beside the base's (the next when moving up, the
 * one before when moving down) has another cross coordinate. For that lookup
 * the base is reduced into 0 to scan_count - 1 as a 16-bit unsigned value, so a
 * negative base counts from 65536. */
// FUNCTION: XVT 0x422560
void flight_sw_advance_rot_sprite_secondary_scale(void)
{
	int16_t span_step;
	uint16_t previous_accum;
	uint16_t point_index;
	unsigned int scan_count;
	struct flight_sw_rot_sprite_edge_point *current_point;
	int16_t current_coordinate;

	span_step = 1;
	if (g_flight_sw_rot_sprite_skip_secondary_scale_step != 0) {
		g_flight_sw_rot_sprite_skip_secondary_scale_step = 0;
		return;
	}

	previous_accum = g_flight_sw_rot_sprite_secondary_scale_accum;
	g_flight_sw_rot_sprite_secondary_scale_accum +=
		g_flight_sw_rot_sprite_coeffs->secondary_scale_low;
	if (g_flight_sw_rot_sprite_coeffs->secondary_scale_high != 0) {
		if (previous_accum >
		    g_flight_sw_rot_sprite_secondary_scale_accum) {
			span_step = 2;
		}
	} else if (previous_accum <=
		   g_flight_sw_rot_sprite_secondary_scale_accum) {
		return;
	}

	point_index = g_flight_sw_rot_sprite_span_base_x;
	while (point_index < 0) {
		point_index += g_flight_sw_rot_sprite_coeffs->scan_count;
	}
	scan_count = g_flight_sw_rot_sprite_coeffs->scan_count;
	while ((int)point_index >= (int)scan_count) {
		point_index -=
			(int16_t)g_flight_sw_rot_sprite_coeffs->scan_count;
	}

	if (g_flight_sw_rot_sprite_coeffs->primary_axis_swap == 0) {
		current_point =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[point_index +
								1];
		current_coordinate = current_point->y;
		if (g_flight_sw_rot_sprite_coeffs->flip_count == 1) {
			g_flight_sw_rot_sprite_span_base_x += span_step;
			if (current_point[1].y != current_coordinate) {
				g_flight_sw_rot_sprite_skip_secondary_scale_step =
					1;
			}
		} else {
			g_flight_sw_rot_sprite_span_base_x -= span_step;
			if (g_flight_sw_rot_sprite_coeffs
				    ->edge_points_with_predecessor[point_index]
				    .y != current_coordinate) {
				g_flight_sw_rot_sprite_skip_secondary_scale_step =
					1;
			}
		}
	} else {
		current_point =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[point_index +
								1];
		current_coordinate = current_point->x;
		if (g_flight_sw_rot_sprite_coeffs->flip_count != 1) {
			g_flight_sw_rot_sprite_span_base_x += span_step;
			if (current_point[1].x != current_coordinate) {
				g_flight_sw_rot_sprite_skip_secondary_scale_step =
					1;
			}
		} else {
			g_flight_sw_rot_sprite_span_base_x -= span_step;
			if (g_flight_sw_rot_sprite_coeffs
				    ->edge_points_with_predecessor[point_index]
				    .x != current_coordinate) {
				g_flight_sw_rot_sprite_skip_secondary_scale_step =
					1;
			}
		}
	}
}

/* Calls the set-up function for g_flight_sw_rot_sprite_coeffs->octant and returns
 * what it returns; for an octant over 7, returns the octant. */
// FUNCTION: XVT 0x4226A0
int flight_sw_init_rot_sprite_for_current_octant(void)
{
	uint16_t octant;

	octant = g_flight_sw_rot_sprite_coeffs->octant;
	switch (octant) {
	case 0:
		return flight_sw_init_rot_sprite_octant0();
	case 1:
		return flight_sw_init_rot_sprite_octant1();
	case 2:
		return flight_sw_init_rot_sprite_octant2();
	case 3:
		return flight_sw_init_rot_sprite_octant3();
	case 4:
		return flight_sw_init_rot_sprite_octant4();
	case 5:
		return flight_sw_init_rot_sprite_octant5();
	case 6:
		return flight_sw_init_rot_sprite_octant6();
	case 7:
		return flight_sw_init_rot_sprite_octant7();
	default:
		return octant;
	}
}

/* Calls the line-step function for g_flight_sw_rot_sprite_coeffs->octant and
 * returns what it returns; for an octant over 7, returns the octant. */
// FUNCTION: XVT 0x422710
int flight_sw_step_rot_sprite_for_current_octant(void)
{
	uint16_t octant;

	octant = g_flight_sw_rot_sprite_coeffs->octant;
	switch (octant) {
	case 0:
		return flight_sw_step_rot_sprite_octant0();
	case 1:
		return flight_sw_step_rot_sprite_octant1();
	case 2:
		return flight_sw_step_rot_sprite_octant2();
	case 3:
		return flight_sw_step_rot_sprite_octant3();
	case 4:
		return flight_sw_step_rot_sprite_octant4();
	case 5:
		return flight_sw_step_rot_sprite_octant5();
	case 6:
		return flight_sw_step_rot_sprite_octant6();
	case 7:
		return flight_sw_step_rot_sprite_octant7();
	default:
		return octant;
	}
}

/* Sets up the walk for octant 0: the edge steps along x, no flips. Moves the
 * edge cursor a whole edge (the extents in entry 0, plus 1) at a time until its
 * x is inside the viewport, moving the span base a viewport width each time;
 * puts the line start at the viewport's left side
 * (g_flight_sw_rot_sprite_primary_edge_x 0) on the edge's row there and the far end's
 * row in g_flight_sw_rot_sprite_secondary_edge_y; then sets the clip bounds from the
 * edge points where the edge crosses the viewport's rows, and
 * g_flight_sw_rot_sprite_span_base_x. Returns 0 when the line start's row is at or
 * past g_flight_sw_rot_sprite_viewport_height, else 1. */
// FUNCTION: XVT 0x422780
int flight_sw_init_rot_sprite_octant0(void)
{
	int16_t span_base_offset;
	int16_t viewport_width;
	int16_t viewport_height;
	int16_t *negative_edge_delta_x_ptr;
	int16_t *negative_edge_delta_y_ptr;
	int16_t *positive_edge_delta_x_ptr;
	int16_t *positive_edge_delta_y_ptr;
	int16_t edge_cursor_minimum;
	uint16_t point_index;
	int16_t clip_min_x;
	uint16_t max_point_index;

	span_base_offset = 0;
	if (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		viewport_width = g_flight_sw_rot_sprite_viewport_width;
		negative_edge_delta_x_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .x;
		negative_edge_delta_y_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .y;
		edge_cursor_minimum = 0;
		do {
			span_base_offset -= viewport_width;
			g_flight_sw_rot_sprite_edge_cursor_x +=
				*negative_edge_delta_x_ptr + 1;
			g_flight_sw_rot_sprite_edge_cursor_y +=
				*negative_edge_delta_y_ptr + 1;
		} while (g_flight_sw_rot_sprite_edge_cursor_x <
			 edge_cursor_minimum);
	}
	if (g_flight_sw_rot_sprite_edge_cursor_x >=
	    g_flight_sw_rot_sprite_viewport_width) {
		positive_edge_delta_x_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .x;
		positive_edge_delta_y_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .y;
		do {
			g_flight_sw_rot_sprite_edge_cursor_x -=
				*positive_edge_delta_x_ptr + 1;
			span_base_offset +=
				g_flight_sw_rot_sprite_viewport_width;
			g_flight_sw_rot_sprite_edge_cursor_y -=
				*positive_edge_delta_y_ptr + 1;
		} while (g_flight_sw_rot_sprite_edge_cursor_x >=
			 g_flight_sw_rot_sprite_viewport_width);
	}

	viewport_height = g_flight_sw_rot_sprite_viewport_height;
	g_flight_sw_rot_sprite_edge_cursor_y -=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor
				[g_flight_sw_rot_sprite_edge_cursor_x + 1]
			.y;
	g_flight_sw_rot_sprite_primary_edge_y =
		g_flight_sw_rot_sprite_edge_cursor_y;
	if (g_flight_sw_rot_sprite_edge_cursor_y >= viewport_height) {
		g_flight_sw_rot_sprite_secondary_edge_y =
			g_flight_sw_rot_sprite_edge_cursor_y +
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y;
		return 0;
	}

	clip_min_x = 0;
	g_flight_sw_rot_sprite_primary_edge_x = 0;
	if (g_flight_sw_rot_sprite_edge_cursor_y < 0) {
		clip_min_x = -g_flight_sw_rot_sprite_edge_cursor_y;
		g_flight_sw_rot_sprite_edge_cursor_y = clip_min_x;
		g_flight_sw_rot_sprite_clip_min_run_idx03 = clip_min_x;
		if (g_flight_sw_rot_sprite_coeffs
			    ->edge_points_with_predecessor[0]
			    .y < clip_min_x) {
			clip_min_x = -1;
		} else {
			point_index = 0;
			while (point_index <=
			       g_flight_sw_rot_sprite_coeffs
				       ->edge_points_with_predecessor[0]
				       .x) {
				if ((uint16_t)g_flight_sw_rot_sprite_coeffs
					    ->edge_points_with_predecessor
						    [point_index + 1]
					    .y ==
				    g_flight_sw_rot_sprite_edge_cursor_y) {
					clip_min_x =
						g_flight_sw_rot_sprite_coeffs
							->edge_points_with_predecessor
								[point_index +
								 1]
							.x;
					break;
				}
				++point_index;
			}
		}
	}
	g_flight_sw_rot_sprite_clip_min_x = clip_min_x;
	g_flight_sw_rot_sprite_secondary_edge_x =
		g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			.x;
	g_flight_sw_rot_sprite_clip_max_run_idx03 = viewport_height;
	g_flight_sw_rot_sprite_secondary_edge_y =
		g_flight_sw_rot_sprite_primary_edge_y +
		g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			.y;

	if (g_flight_sw_rot_sprite_secondary_edge_y < 0) {
		g_flight_sw_rot_sprite_clip_max_x = -1;
	} else if (g_flight_sw_rot_sprite_secondary_edge_y >= 0 &&
		   g_flight_sw_rot_sprite_secondary_edge_y < viewport_height) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	} else {
		g_flight_sw_rot_sprite_clip_max_run_idx03 =
			viewport_height - g_flight_sw_rot_sprite_primary_edge_y;
		max_point_index = 0;
		g_flight_sw_rot_sprite_clip_max_x = -1;
		while (max_point_index <=
		       g_flight_sw_rot_sprite_coeffs
			       ->edge_points_with_predecessor[0]
			       .x) {
			if ((uint16_t)g_flight_sw_rot_sprite_coeffs
				    ->edge_points_with_predecessor
					    [max_point_index + 1]
				    .y ==
			    g_flight_sw_rot_sprite_clip_max_run_idx03) {
				g_flight_sw_rot_sprite_clip_max_x =
					g_flight_sw_rot_sprite_coeffs
						->edge_points_with_predecessor
							[max_point_index + 1]
						.x -
					1;
				break;
			}
			++max_point_index;
		}
	}
	g_flight_sw_rot_sprite_span_base_x =
		span_base_offset + g_flight_sw_rot_sprite_edge_cursor_x;
	return 1;
}

/* Steps the octant 0 walk a line: moves g_flight_sw_rot_sprite_dest_line_ptr back by
 * dest_pitch_delta, raises g_flight_sw_rot_sprite_primary_edge_y and
 * g_flight_sw_rot_sprite_secondary_edge_y by 1, and moves the clip bounds by the run
 * lengths as either end enters or leaves the viewport's rows. Returns 0 once
 * the line start's row reaches the viewport height, else 1. */
// FUNCTION: XVT 0x4229F0
int flight_sw_step_rot_sprite_octant0(void)
{
	uint8_t *dest_line_ptr;
	int16_t viewport_height;
	int16_t min_run_index;

	dest_line_ptr = g_flight_sw_rot_sprite_dest_line_ptr;
	dest_line_ptr -= g_flight_sw_rot_sprite_coeffs->dest_pitch_delta;
	++g_flight_sw_rot_sprite_primary_edge_y;
	g_flight_sw_rot_sprite_dest_line_ptr = dest_line_ptr;
	if (g_flight_sw_rot_sprite_primary_edge_y == 0) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
	} else {
		viewport_height = g_flight_sw_rot_sprite_viewport_height;
		if (g_flight_sw_rot_sprite_primary_edge_y >= viewport_height) {
			return 0;
		}
		if (g_flight_sw_rot_sprite_primary_edge_y < 0) {
			--g_flight_sw_rot_sprite_clip_min_run_idx03;
			g_flight_sw_rot_sprite_clip_min_x -=
				g_flight_sw_rot_sprite_coeffs->run_lengths
					[g_flight_sw_rot_sprite_clip_min_run_idx03];
		}
	}

	++g_flight_sw_rot_sprite_secondary_edge_y;
	if (g_flight_sw_rot_sprite_secondary_edge_y == 0) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		min_run_index =
			g_flight_sw_rot_sprite_coeffs->run_length_count - 1;
		g_flight_sw_rot_sprite_clip_min_run_idx03 = min_run_index;
		g_flight_sw_rot_sprite_clip_min_x =
			g_flight_sw_rot_sprite_clip_max_x -
			g_flight_sw_rot_sprite_coeffs
				->run_lengths[min_run_index] +
			1;
		if (g_flight_sw_rot_sprite_clip_min_x < 0) {
			g_flight_sw_rot_sprite_clip_min_x = 0;
			return 1;
		}
	} else if (g_flight_sw_rot_sprite_secondary_edge_y >=
		   g_flight_sw_rot_sprite_viewport_height) {
		if (g_flight_sw_rot_sprite_secondary_edge_y ==
		    g_flight_sw_rot_sprite_viewport_height) {
			g_flight_sw_rot_sprite_clip_max_run_idx03 =
				g_flight_sw_rot_sprite_coeffs->run_length_count;
		}
		--g_flight_sw_rot_sprite_clip_max_run_idx03;
		g_flight_sw_rot_sprite_clip_max_x -=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx03];
	}
	return 1;
}

/* Sets up the walk for octant 1 (flipY) the way flight_sw_init_rot_sprite_octant0
 * does for octant 0, the cursor's row moving the other way. Returns 0 when the
 * far end's row is at or past the viewport height, else 1. */
// FUNCTION: XVT 0x422B10
int flight_sw_init_rot_sprite_octant1(void)
{
	int16_t span_base_offset;
	int16_t clip_min_value;
	int16_t clip_max_value;
	int16_t point_index;
	int16_t target_y;
	int16_t edge_delta_x;
	int16_t viewport_width;
	int16_t edge_cursor_minimum;
	int16_t *negative_edge_delta_x_ptr;
	int16_t *negative_edge_delta_y_ptr;
	int16_t *positive_edge_delta_x_ptr;
	int16_t *positive_edge_delta_y_ptr;
	int16_t *edge_delta_x_ptr;

	span_base_offset = 0;
	if (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		viewport_width = g_flight_sw_rot_sprite_viewport_width;
		negative_edge_delta_x_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .x;
		negative_edge_delta_y_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .y;
		edge_cursor_minimum = 0;
		do {
			span_base_offset -= viewport_width;
			g_flight_sw_rot_sprite_edge_cursor_x +=
				*negative_edge_delta_x_ptr + 1;
			g_flight_sw_rot_sprite_edge_cursor_y -=
				*negative_edge_delta_y_ptr + 1;
		} while (g_flight_sw_rot_sprite_edge_cursor_x <
			 edge_cursor_minimum);
	}
	if (g_flight_sw_rot_sprite_edge_cursor_x >=
	    g_flight_sw_rot_sprite_viewport_width) {
		viewport_width = g_flight_sw_rot_sprite_viewport_width;
		positive_edge_delta_x_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .x;
		positive_edge_delta_y_ptr =
			&g_flight_sw_rot_sprite_coeffs
				 ->edge_points_with_predecessor[0]
				 .y;
		do {
			span_base_offset += viewport_width;
			g_flight_sw_rot_sprite_edge_cursor_x -=
				*positive_edge_delta_x_ptr + 1;
			g_flight_sw_rot_sprite_edge_cursor_y +=
				*positive_edge_delta_y_ptr + 1;
		} while (g_flight_sw_rot_sprite_edge_cursor_x >=
			 viewport_width);
	}

	g_flight_sw_rot_sprite_primary_edge_x = 0;
	g_flight_sw_rot_sprite_edge_cursor_y +=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor
				[g_flight_sw_rot_sprite_edge_cursor_x + 1]
			.y;
	g_flight_sw_rot_sprite_primary_edge_y =
		g_flight_sw_rot_sprite_edge_cursor_y;
	if (g_flight_sw_rot_sprite_edge_cursor_y < 0) {
		clip_min_value = -1;
	} else if (g_flight_sw_rot_sprite_edge_cursor_y <
		   g_flight_sw_rot_sprite_viewport_height) {
		clip_min_value = 0;
	} else {
		target_y = g_flight_sw_rot_sprite_edge_cursor_y -
			   g_flight_sw_rot_sprite_viewport_height;
		g_flight_sw_rot_sprite_clip_min_run_idx03 = target_y;
		clip_min_value = -1;
		if (g_flight_sw_rot_sprite_coeffs
			    ->edge_points_with_predecessor[0]
			    .y >= target_y + 1) {
			point_index = 0;
			edge_delta_x = g_flight_sw_rot_sprite_coeffs
					       ->edge_points_with_predecessor[0]
					       .x;
			while (point_index <= edge_delta_x &&
			       (uint16_t)g_flight_sw_rot_sprite_coeffs
						       ->edge_points_with_predecessor
							       [point_index + 1]
						       .y -
					       target_y !=
				       1) {
				++point_index;
			}
			if (point_index <= edge_delta_x) {
				clip_min_value =
					g_flight_sw_rot_sprite_coeffs
						->edge_points_with_predecessor
							[point_index + 1]
						.x;
			}
		}
	}
	g_flight_sw_rot_sprite_clip_min_x = clip_min_value;

	edge_delta_x_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .x;
	g_flight_sw_rot_sprite_secondary_edge_x = *edge_delta_x_ptr;
	g_flight_sw_rot_sprite_clip_max_run_idx03 =
		g_flight_sw_rot_sprite_viewport_height;
	g_flight_sw_rot_sprite_secondary_edge_y =
		g_flight_sw_rot_sprite_edge_cursor_y -
		g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			.y;
	if (g_flight_sw_rot_sprite_secondary_edge_y >=
	    g_flight_sw_rot_sprite_viewport_height) {
		return 0;
	}

	if (g_flight_sw_rot_sprite_secondary_edge_y >= 0) {
		clip_max_value = g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	} else if (g_flight_sw_rot_sprite_edge_cursor_y >= 0) {
		g_flight_sw_rot_sprite_clip_max_run_idx03 =
			g_flight_sw_rot_sprite_edge_cursor_y;
		point_index = 0;
		edge_delta_x = g_flight_sw_rot_sprite_coeffs
				       ->edge_points_with_predecessor[0]
				       .x;
		target_y = g_flight_sw_rot_sprite_coeffs->first_edge_y +
			   g_flight_sw_rot_sprite_edge_cursor_y + 1;
		while (point_index <= edge_delta_x &&
		       g_flight_sw_rot_sprite_coeffs
				       ->edge_points_with_predecessor
					       [point_index + 1]
				       .y != target_y) {
			++point_index;
		}
		clip_max_value = -1;
		if (point_index <= edge_delta_x) {
			clip_max_value = g_flight_sw_rot_sprite_coeffs
						 ->edge_points_with_predecessor
							 [point_index + 1]
						 .x -
					 1;
		}
	} else {
		clip_max_value = -1;
	}
	g_flight_sw_rot_sprite_clip_max_x = clip_max_value;
	g_flight_sw_rot_sprite_span_base_x =
		span_base_offset + g_flight_sw_rot_sprite_edge_cursor_x;
	return 1;
}

/* Steps the octant 1 walk a line as flight_sw_step_rot_sprite_octant0 does. Returns
 * 0 once the far end's row reaches the viewport height, else 1. */
// FUNCTION: XVT 0x422D90
int flight_sw_step_rot_sprite_octant1(void)
{
	uint16_t viewport_height;

	g_flight_sw_rot_sprite_dest_line_ptr -=
		g_flight_sw_rot_sprite_coeffs->dest_pitch_delta;
	++g_flight_sw_rot_sprite_primary_edge_y;
	if (g_flight_sw_rot_sprite_primary_edge_y == 0) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->run_lengths[0];
		--g_flight_sw_rot_sprite_clip_max_x;
		g_flight_sw_rot_sprite_clip_max_run_idx03 = 0;
	} else {
		viewport_height = g_flight_sw_rot_sprite_viewport_height;
		if (g_flight_sw_rot_sprite_primary_edge_y >=
		    (int16_t)viewport_height) {
			if (g_flight_sw_rot_sprite_primary_edge_y ==
			    (int16_t)viewport_height) {
				g_flight_sw_rot_sprite_clip_min_run_idx03 = -1;
			}
			++g_flight_sw_rot_sprite_clip_min_run_idx03;
			g_flight_sw_rot_sprite_clip_min_x +=
				g_flight_sw_rot_sprite_coeffs->run_lengths
					[g_flight_sw_rot_sprite_clip_min_run_idx03];
		}
	}

	++g_flight_sw_rot_sprite_secondary_edge_y;
	if (g_flight_sw_rot_sprite_secondary_edge_y == 0) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		return 1;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_y >=
	    g_flight_sw_rot_sprite_viewport_height) {
		return 0;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_y < 0 &&
	    g_flight_sw_rot_sprite_primary_edge_y > 0) {
		++g_flight_sw_rot_sprite_clip_max_run_idx03;
		g_flight_sw_rot_sprite_clip_max_x +=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx03];
	}
	return 1;
}

/* Sets up the walk for octant 2 (flipX) the way flight_sw_init_rot_sprite_octant0
 * does for octant 0, with the line start at the viewport's right side
 * (g_flight_sw_rot_sprite_primary_edge_x of g_flight_sw_rot_sprite_viewport_max_x). Returns
 * 0 when the far end's row is under 0, else 1. */
// FUNCTION: XVT 0x422E90
int flight_sw_init_rot_sprite_octant2(void)
{
	int16_t span_base_offset;
	int16_t viewport_right_delta;
	int16_t min_point_index;
	int16_t max_point_index;

	span_base_offset = 0;
	while (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		g_flight_sw_rot_sprite_edge_cursor_x +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		span_base_offset += g_flight_sw_rot_sprite_viewport_width;
		g_flight_sw_rot_sprite_edge_cursor_y -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}
	while (g_flight_sw_rot_sprite_edge_cursor_x >=
	       g_flight_sw_rot_sprite_viewport_width) {
		g_flight_sw_rot_sprite_edge_cursor_x -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		span_base_offset -= g_flight_sw_rot_sprite_viewport_width;
		g_flight_sw_rot_sprite_edge_cursor_y +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}

	viewport_right_delta = g_flight_sw_rot_sprite_viewport_max_x -
			       g_flight_sw_rot_sprite_edge_cursor_x;
	g_flight_sw_rot_sprite_edge_cursor_y -=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor[viewport_right_delta + 1]
			.y;
	g_flight_sw_rot_sprite_primary_edge_y =
		g_flight_sw_rot_sprite_edge_cursor_y;
	g_flight_sw_rot_sprite_primary_edge_x =
		g_flight_sw_rot_sprite_viewport_max_x;
	g_flight_sw_rot_sprite_secondary_edge_x =
		g_flight_sw_rot_sprite_viewport_max_x -
		g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			.x;
	g_flight_sw_rot_sprite_secondary_edge_y =
		g_flight_sw_rot_sprite_edge_cursor_y +
		g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			.y;
	g_flight_sw_rot_sprite_clip_min_x = -1;
	g_flight_sw_rot_sprite_clip_max_x = -1;
	g_flight_sw_rot_sprite_clip_min_run_idx03 = -1;
	g_flight_sw_rot_sprite_clip_max_run_idx03 =
		g_flight_sw_rot_sprite_viewport_height;

	if (g_flight_sw_rot_sprite_edge_cursor_y >= 0) {
		if (g_flight_sw_rot_sprite_edge_cursor_y >=
		    g_flight_sw_rot_sprite_viewport_height) {
			g_flight_sw_rot_sprite_span_base_x =
				viewport_right_delta + span_base_offset;
			return 1;
		}
		min_point_index = 0;
	} else {
		g_flight_sw_rot_sprite_clip_min_run_idx03 =
			-g_flight_sw_rot_sprite_edge_cursor_y - 1;
		min_point_index = -1;
		if (g_flight_sw_rot_sprite_coeffs
			    ->edge_points_with_predecessor[0]
			    .y >= -g_flight_sw_rot_sprite_edge_cursor_y) {
			min_point_index = 0;
			if (g_flight_sw_rot_sprite_coeffs
				    ->edge_points_with_predecessor[0]
				    .x >= 0) {
				while ((uint16_t)g_flight_sw_rot_sprite_coeffs
					       ->edge_points_with_predecessor
						       [min_point_index + 1]
					       .y !=
				       -g_flight_sw_rot_sprite_edge_cursor_y) {
					++min_point_index;
					if (min_point_index >
					    g_flight_sw_rot_sprite_coeffs
						    ->edge_points_with_predecessor
							    [0]
						    .x) {
						break;
					}
				}
			}
			if (min_point_index <=
			    g_flight_sw_rot_sprite_coeffs
				    ->edge_points_with_predecessor[0]
				    .x) {
				min_point_index =
					g_flight_sw_rot_sprite_coeffs
						->edge_points_with_predecessor
							[min_point_index + 1]
						.x;
			} else {
				min_point_index = -1;
			}
		}
	}
	/* min_point_index is an edge point index only during the search above; here it holds the clip minimum
	 * x: 0, -1, or the found point's x. */
	g_flight_sw_rot_sprite_clip_min_x = min_point_index;

	if (g_flight_sw_rot_sprite_secondary_edge_y < 0) {
		return 0;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_y <
	    g_flight_sw_rot_sprite_viewport_height) {
		max_point_index = g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	} else {
		g_flight_sw_rot_sprite_clip_max_run_idx03 =
			g_flight_sw_rot_sprite_viewport_height -
			g_flight_sw_rot_sprite_edge_cursor_y - 1;
		max_point_index = g_flight_sw_rot_sprite_coeffs
					  ->edge_points_with_predecessor[0]
					  .x;
		if (max_point_index >= 0) {
			while ((uint16_t)g_flight_sw_rot_sprite_coeffs
				       ->edge_points_with_predecessor
					       [max_point_index + 1]
				       .y !=
			       g_flight_sw_rot_sprite_clip_max_run_idx03) {
				--max_point_index;
				if (max_point_index < 0) {
					break;
				}
			}
		}
		if (max_point_index >= 0) {
			max_point_index = g_flight_sw_rot_sprite_coeffs
						  ->edge_points_with_predecessor
							  [max_point_index + 1]
						  .x;
		} else {
			max_point_index = -1;
		}
	}
	/* max_point_index is an edge point index only during the search above; here it holds the clip maximum
	 * x: scan_count - 1, -1, or the found point's x. */
	g_flight_sw_rot_sprite_clip_max_x = max_point_index;
	g_flight_sw_rot_sprite_span_base_x =
		viewport_right_delta + span_base_offset;
	return 1;
}

/* Steps the octant 2 walk a line: moves g_flight_sw_rot_sprite_dest_line_ptr on by
 * dest_pitch_delta, lowers both edge rows by 1 and moves the clip bounds. Returns
 * 0 once the far end's row is under 0, else 1. */
// FUNCTION: XVT 0x4230F0
int flight_sw_step_rot_sprite_octant2(void)
{
	g_flight_sw_rot_sprite_dest_line_ptr +=
		g_flight_sw_rot_sprite_coeffs->dest_pitch_delta;
	--g_flight_sw_rot_sprite_primary_edge_y;
	if (g_flight_sw_rot_sprite_primary_edge_y ==
	    g_flight_sw_rot_sprite_viewport_max_y) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
		g_flight_sw_rot_sprite_clip_max_x = -1;
		g_flight_sw_rot_sprite_clip_max_run_idx03 = -1;
	} else if (g_flight_sw_rot_sprite_primary_edge_y < 0) {
		if (g_flight_sw_rot_sprite_primary_edge_y == -1) {
			g_flight_sw_rot_sprite_clip_min_run_idx03 = -1;
		}
		++g_flight_sw_rot_sprite_clip_min_run_idx03;
		g_flight_sw_rot_sprite_clip_min_x +=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx03];
	}

	--g_flight_sw_rot_sprite_secondary_edge_y;
	if (g_flight_sw_rot_sprite_secondary_edge_y ==
	    g_flight_sw_rot_sprite_viewport_max_y) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		return 1;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_y < 0) {
		return 0;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_y >=
		    g_flight_sw_rot_sprite_viewport_height &&
	    g_flight_sw_rot_sprite_primary_edge_y <
		    g_flight_sw_rot_sprite_viewport_height) {
		++g_flight_sw_rot_sprite_clip_max_run_idx03;
		g_flight_sw_rot_sprite_clip_max_x +=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx03];
	}
	return 1;
}

/* Sets up the walk for octant 3 (flipX and flipY), with the line start at the
 * viewport's right side. Returns 0 when the line start's row is under 0, else
 * 1. */
// FUNCTION: XVT 0x423200
int flight_sw_init_rot_sprite_octant3(void)
{
	int16_t span_base_offset;
	int16_t *edge_delta_x_ptr;
	int16_t *edge_delta_y_ptr;
	int16_t clip_value;
	int16_t point_index;
	int16_t edge_point_index;
	int16_t target_y;

	span_base_offset = 0;
	while (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		g_flight_sw_rot_sprite_edge_cursor_x +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		span_base_offset += g_flight_sw_rot_sprite_viewport_width;
		g_flight_sw_rot_sprite_edge_cursor_y +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}
	while (g_flight_sw_rot_sprite_edge_cursor_x >=
	       g_flight_sw_rot_sprite_viewport_width) {
		span_base_offset -= g_flight_sw_rot_sprite_viewport_width;
		g_flight_sw_rot_sprite_edge_cursor_x -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		g_flight_sw_rot_sprite_edge_cursor_y -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}

	edge_point_index = g_flight_sw_rot_sprite_viewport_max_x -
			   g_flight_sw_rot_sprite_edge_cursor_x;
	g_flight_sw_rot_sprite_edge_cursor_y +=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor[edge_point_index + 1]
			.y;
	g_flight_sw_rot_sprite_primary_edge_y =
		g_flight_sw_rot_sprite_edge_cursor_y;
	edge_delta_y_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .y;
	g_flight_sw_rot_sprite_primary_edge_x =
		g_flight_sw_rot_sprite_viewport_max_x;
	edge_delta_x_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .x;
	g_flight_sw_rot_sprite_secondary_edge_x =
		g_flight_sw_rot_sprite_viewport_max_x - *edge_delta_x_ptr;
	g_flight_sw_rot_sprite_secondary_edge_y =
		g_flight_sw_rot_sprite_edge_cursor_y - *edge_delta_y_ptr;
	g_flight_sw_rot_sprite_clip_min_x = -1;
	g_flight_sw_rot_sprite_clip_max_x = -1;
	g_flight_sw_rot_sprite_clip_min_run_idx03 = -1;
	g_flight_sw_rot_sprite_clip_max_run_idx03 =
		g_flight_sw_rot_sprite_coeffs->run_length_count;

	if (g_flight_sw_rot_sprite_edge_cursor_y < 0) {
		return 0;
	}
	clip_value = 0;
	if (g_flight_sw_rot_sprite_edge_cursor_y >=
	    g_flight_sw_rot_sprite_viewport_height) {
		target_y = g_flight_sw_rot_sprite_edge_cursor_y -
			   g_flight_sw_rot_sprite_viewport_max_y;
		g_flight_sw_rot_sprite_clip_min_run_idx03 = target_y;
		clip_value = -1;
		if (*edge_delta_y_ptr >= target_y) {
			point_index = 0;
			while (point_index <= *edge_delta_x_ptr &&
			       (uint16_t)g_flight_sw_rot_sprite_coeffs
					       ->edge_points_with_predecessor
						       [point_index + 1]
					       .y != target_y) {
				++point_index;
			}
			if (point_index <= *edge_delta_x_ptr) {
				clip_value =
					g_flight_sw_rot_sprite_coeffs
						->edge_points_with_predecessor
							[point_index + 1]
						.x;
			}
		}
	}
	g_flight_sw_rot_sprite_clip_min_x = clip_value;

	if (g_flight_sw_rot_sprite_secondary_edge_y >=
	    g_flight_sw_rot_sprite_viewport_height) {
		clip_value = -1;
	} else if (g_flight_sw_rot_sprite_secondary_edge_y >= 0) {
		clip_value = g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	} else {
		target_y = g_flight_sw_rot_sprite_secondary_edge_y +
			   *edge_delta_y_ptr;
		g_flight_sw_rot_sprite_clip_max_run_idx03 = target_y + 1;
		point_index = *edge_delta_x_ptr;
		while (point_index >= 0 &&
		       (uint16_t)g_flight_sw_rot_sprite_coeffs
				       ->edge_points_with_predecessor
					       [point_index + 1]
				       .y != target_y) {
			--point_index;
		}
		clip_value = -1;
		if (point_index >= 0) {
			clip_value = g_flight_sw_rot_sprite_coeffs
					     ->edge_points_with_predecessor
						     [point_index + 1]
					     .x;
		}
	}
	g_flight_sw_rot_sprite_clip_max_x = clip_value;
	g_flight_sw_rot_sprite_span_base_x =
		g_flight_sw_rot_sprite_viewport_max_x -
		g_flight_sw_rot_sprite_edge_cursor_x + span_base_offset;
	return 1;
}

/* Steps the octant 3 walk a line as flight_sw_step_rot_sprite_octant2 does. Returns
 * 0 once the line start's row is under 0, else 1. */
// FUNCTION: XVT 0x423480
int flight_sw_step_rot_sprite_octant3(void)
{
	uint8_t *dest_line_ptr;
	int16_t viewport_max_y;
	uint16_t run_length_count;
	int16_t min_run_index;

	dest_line_ptr =
		&g_flight_sw_rot_sprite_dest_line_ptr
			[g_flight_sw_rot_sprite_coeffs->dest_pitch_delta];
	viewport_max_y = g_flight_sw_rot_sprite_viewport_max_y;
	--g_flight_sw_rot_sprite_primary_edge_y;
	g_flight_sw_rot_sprite_dest_line_ptr = dest_line_ptr;
	--g_flight_sw_rot_sprite_secondary_edge_y;
	if (viewport_max_y == g_flight_sw_rot_sprite_primary_edge_y) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
		g_flight_sw_rot_sprite_clip_min_run_idx03 = -1;
	} else {
		if (g_flight_sw_rot_sprite_primary_edge_y < 0) {
			return 0;
		}
		if (g_flight_sw_rot_sprite_primary_edge_y >=
			    g_flight_sw_rot_sprite_viewport_height &&
		    g_flight_sw_rot_sprite_secondary_edge_y <
			    g_flight_sw_rot_sprite_viewport_height) {
			--g_flight_sw_rot_sprite_clip_min_run_idx03;
			g_flight_sw_rot_sprite_clip_min_x -=
				g_flight_sw_rot_sprite_coeffs->run_lengths
					[g_flight_sw_rot_sprite_clip_min_run_idx03];
		}
	}

	if (g_flight_sw_rot_sprite_viewport_max_y ==
	    g_flight_sw_rot_sprite_secondary_edge_y) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		run_length_count =
			g_flight_sw_rot_sprite_coeffs->run_length_count;
		min_run_index = run_length_count;
		--min_run_index;
		g_flight_sw_rot_sprite_clip_max_run_idx03 = run_length_count;
		g_flight_sw_rot_sprite_clip_min_run_idx03 = min_run_index;
		g_flight_sw_rot_sprite_clip_min_x =
			g_flight_sw_rot_sprite_clip_max_x -
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx03];
		++g_flight_sw_rot_sprite_clip_min_x;
		if (g_flight_sw_rot_sprite_clip_min_x < 0) {
			g_flight_sw_rot_sprite_clip_min_x = 0;
			return 1;
		}
	} else if (g_flight_sw_rot_sprite_secondary_edge_y < 0) {
		--g_flight_sw_rot_sprite_clip_max_run_idx03;
		g_flight_sw_rot_sprite_clip_max_x -=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx03];
	}
	return 1;
}

/* Sets up the walk for octant 4: the edge steps along y, no flips. Moves the
 * edge cursor a whole edge at a time until its y is inside the viewport, moving
 * the span base a viewport height each time; puts the line start at the
 * viewport's top (g_flight_sw_rot_sprite_primary_edge_y 0) in the edge's column there
 * and the far end's column in g_flight_sw_rot_sprite_secondary_edge_x; then sets the
 * clip bounds and g_flight_sw_rot_sprite_span_base_x. Returns 0 when the far end's
 * column is under 0, and in the modern build when the clip search finds no edge
 * point; else 1. */
// FUNCTION: XVT 0x4235D0
int flight_sw_init_rot_sprite_octant4(void)
{
	int16_t span_base_offset;
	int16_t clip_min_value;
	int16_t clip_max_value;
	int16_t point_index;
	int16_t target_x;
	int16_t max_point_index;
	int16_t edge_cursor_minimum;
	int16_t *edge_delta_y_ptr;
	int16_t *edge_delta_x_ptr;

	span_base_offset = 0;
	if (g_flight_sw_rot_sprite_edge_cursor_y < 0) {
		edge_delta_x_ptr = &g_flight_sw_rot_sprite_coeffs
					    ->edge_points_with_predecessor[0]
					    .x;
		edge_delta_y_ptr = &g_flight_sw_rot_sprite_coeffs
					    ->edge_points_with_predecessor[0]
					    .y;
		edge_cursor_minimum = 0;
		do {
			g_flight_sw_rot_sprite_edge_cursor_x +=
				*edge_delta_x_ptr + 1;
			g_flight_sw_rot_sprite_edge_cursor_y +=
				*edge_delta_y_ptr + 1;
			span_base_offset -=
				g_flight_sw_rot_sprite_viewport_height;
		} while (g_flight_sw_rot_sprite_edge_cursor_y <
			 edge_cursor_minimum);
	}
	if (g_flight_sw_rot_sprite_edge_cursor_y >=
	    g_flight_sw_rot_sprite_viewport_height) {
		edge_delta_x_ptr = &g_flight_sw_rot_sprite_coeffs
					    ->edge_points_with_predecessor[0]
					    .x;
		edge_delta_y_ptr = &g_flight_sw_rot_sprite_coeffs
					    ->edge_points_with_predecessor[0]
					    .y;
		do {
			g_flight_sw_rot_sprite_edge_cursor_x -=
				*edge_delta_x_ptr + 1;
			g_flight_sw_rot_sprite_edge_cursor_y -=
				*edge_delta_y_ptr + 1;
			span_base_offset +=
				g_flight_sw_rot_sprite_viewport_height;
		} while (g_flight_sw_rot_sprite_edge_cursor_y >=
			 g_flight_sw_rot_sprite_viewport_height);
	}

	g_flight_sw_rot_sprite_edge_cursor_x -=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor
				[g_flight_sw_rot_sprite_edge_cursor_y + 1]
			.x;
	g_flight_sw_rot_sprite_primary_edge_y = 0;
	g_flight_sw_rot_sprite_primary_edge_x =
		g_flight_sw_rot_sprite_edge_cursor_x;
	g_flight_sw_rot_sprite_clip_min_x = 0;
	g_flight_sw_rot_sprite_clip_min_run_idx47 = -1;
	g_flight_sw_rot_sprite_clip_max_run_idx47 =
		g_flight_sw_rot_sprite_viewport_width;
	if (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		target_x = -g_flight_sw_rot_sprite_edge_cursor_x;
		g_flight_sw_rot_sprite_clip_min_run_idx47 = target_x - 1;
		if (g_flight_sw_rot_sprite_coeffs
			    ->edge_points_with_predecessor[0]
			    .x < target_x) {
			clip_min_value = -1;
		} else {
			point_index = 0;
			max_point_index =
				g_flight_sw_rot_sprite_coeffs
					->edge_points_with_predecessor[0]
					.y;
			while (point_index <= max_point_index) {
				if ((uint16_t)g_flight_sw_rot_sprite_coeffs
					    ->edge_points_with_predecessor
						    [point_index + 1]
					    .x == target_x) {
					clip_min_value =
						g_flight_sw_rot_sprite_coeffs
							->edge_points_with_predecessor
								[point_index +
								 1]
							.y;
					break;
				}
				++point_index;
			}
#ifdef XVT_MODERN
			if (point_index > max_point_index) {
				return 0;
			}
#endif
		}
		g_flight_sw_rot_sprite_clip_min_x = clip_min_value;
	}

	g_flight_sw_rot_sprite_secondary_edge_x =
		g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			.x +
		g_flight_sw_rot_sprite_edge_cursor_x;
	g_flight_sw_rot_sprite_secondary_edge_y =
		g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			.y;
	if (g_flight_sw_rot_sprite_secondary_edge_x < 0) {
		return 0;
	}

	if (g_flight_sw_rot_sprite_secondary_edge_x <
	    g_flight_sw_rot_sprite_viewport_width) {
		clip_max_value = g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	} else if (g_flight_sw_rot_sprite_edge_cursor_x <
		   g_flight_sw_rot_sprite_viewport_width) {
		target_x = g_flight_sw_rot_sprite_viewport_width -
			   g_flight_sw_rot_sprite_edge_cursor_x;
		g_flight_sw_rot_sprite_clip_max_run_idx47 = target_x - 1;
		point_index = 0;
		max_point_index = g_flight_sw_rot_sprite_coeffs
					  ->edge_points_with_predecessor[0]
					  .y;
		while (point_index <= max_point_index) {
			if ((uint16_t)g_flight_sw_rot_sprite_coeffs
				    ->edge_points_with_predecessor[point_index +
								   1]
				    .x == target_x) {
				clip_max_value =
					g_flight_sw_rot_sprite_coeffs
						->edge_points_with_predecessor
							[point_index + 1]
						.y -
					1;
				break;
			}
			++point_index;
		}
#ifdef XVT_MODERN
		if (point_index > max_point_index) {
			return 0;
		}
#endif
	} else {
		clip_max_value = -1;
	}
	g_flight_sw_rot_sprite_clip_max_x = clip_max_value;
	g_flight_sw_rot_sprite_span_base_x =
		g_flight_sw_rot_sprite_edge_cursor_y + span_base_offset;
	return 1;
}

/* Steps the octant 4 walk a line: moves g_flight_sw_rot_sprite_dest_line_ptr back one
 * pixel, lowers g_flight_sw_rot_sprite_primary_edge_x and
 * g_flight_sw_rot_sprite_secondary_edge_x by 1 and moves the clip bounds. Returns 0
 * once the far end's column is under 0, else 1. */
// FUNCTION: XVT 0x423810
int flight_sw_step_rot_sprite_octant4(void)
{
	--g_flight_sw_rot_sprite_primary_edge_x;
	g_flight_sw_rot_sprite_dest_line_ptr -= g_flight_bytes_per_pixel;
	if (g_flight_sw_rot_sprite_primary_edge_x ==
	    g_flight_sw_rot_sprite_viewport_max_x) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
		g_flight_sw_rot_sprite_clip_max_x = -1;
		g_flight_sw_rot_sprite_clip_max_run_idx47 = -1;
	} else if (g_flight_sw_rot_sprite_primary_edge_x < 0) {
		++g_flight_sw_rot_sprite_clip_min_run_idx47;
		g_flight_sw_rot_sprite_clip_min_x +=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx47];
	}

	--g_flight_sw_rot_sprite_secondary_edge_x;
	if (g_flight_sw_rot_sprite_secondary_edge_x < 0) {
		return 0;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_x ==
	    g_flight_sw_rot_sprite_viewport_max_x) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		return 1;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_x >=
		    g_flight_sw_rot_sprite_viewport_width &&
	    g_flight_sw_rot_sprite_primary_edge_x <
		    g_flight_sw_rot_sprite_viewport_width) {
		++g_flight_sw_rot_sprite_clip_max_run_idx47;
		g_flight_sw_rot_sprite_clip_max_x +=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx47];
	}
	return 1;
}

/* Sets up the walk for octant 5 (edge along y, flipY) the way
 * flight_sw_init_rot_sprite_octant4 does for octant 4, with the line start at the
 * viewport's bottom (g_flight_sw_rot_sprite_primary_edge_y of
 * g_flight_sw_rot_sprite_viewport_max_y). Returns 0 when the line start's column is
 * at or past the viewport width, else 1. */
// FUNCTION: XVT 0x423900
int flight_sw_init_rot_sprite_octant5(void)
{
	int16_t span_base_offset;
	int16_t *edge_delta_y_ptr;
	int16_t *edge_delta_x_ptr;
	int16_t clip_value;
	int16_t target_x;
	int16_t point_index;

	span_base_offset = 0;
	while (g_flight_sw_rot_sprite_edge_cursor_y < 0) {
		g_flight_sw_rot_sprite_edge_cursor_x -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		span_base_offset += g_flight_sw_rot_sprite_viewport_height;
		g_flight_sw_rot_sprite_edge_cursor_y +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}
	while (g_flight_sw_rot_sprite_edge_cursor_y >=
	       g_flight_sw_rot_sprite_viewport_height) {
		span_base_offset -= g_flight_sw_rot_sprite_viewport_height;
		g_flight_sw_rot_sprite_edge_cursor_x +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		g_flight_sw_rot_sprite_edge_cursor_y -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}

	edge_delta_y_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .y;
	g_flight_sw_rot_sprite_edge_cursor_x -=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor
				[(int16_t)(g_flight_sw_rot_sprite_viewport_max_y -
					   g_flight_sw_rot_sprite_edge_cursor_y) +
				 1]
			.x;
	g_flight_sw_rot_sprite_primary_edge_y =
		g_flight_sw_rot_sprite_viewport_max_y;
	g_flight_sw_rot_sprite_primary_edge_x =
		g_flight_sw_rot_sprite_edge_cursor_x;
	g_flight_sw_rot_sprite_secondary_edge_y =
		g_flight_sw_rot_sprite_viewport_max_y - *edge_delta_y_ptr;
	edge_delta_x_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .x;
	g_flight_sw_rot_sprite_clip_min_x = 0;
	g_flight_sw_rot_sprite_clip_min_run_idx47 = -1;
	g_flight_sw_rot_sprite_secondary_edge_x =
		g_flight_sw_rot_sprite_edge_cursor_x + *edge_delta_x_ptr;
	g_flight_sw_rot_sprite_clip_max_run_idx47 =
		g_flight_sw_rot_sprite_viewport_width;
	if (g_flight_sw_rot_sprite_edge_cursor_x >=
	    g_flight_sw_rot_sprite_viewport_width) {
		return 0;
	}

	if (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		if (g_flight_sw_rot_sprite_secondary_edge_x < 0) {
			g_flight_sw_rot_sprite_span_base_x =
				g_flight_sw_rot_sprite_viewport_max_y -
				g_flight_sw_rot_sprite_edge_cursor_y +
				span_base_offset;
			g_flight_sw_rot_sprite_clip_max_x = -1;
			return 1;
		}
		target_x = -g_flight_sw_rot_sprite_edge_cursor_x;
		g_flight_sw_rot_sprite_clip_min_run_idx47 = target_x;
		if (*edge_delta_x_ptr < target_x) {
			clip_value = -1;
		} else {
			point_index = 0;
			clip_value = target_x;
			if (*edge_delta_y_ptr >= 0) {
				do {
					if ((uint16_t)g_flight_sw_rot_sprite_coeffs
						    ->edge_points_with_predecessor
							    [point_index + 1]
						    .x == target_x) {
						clip_value =
							g_flight_sw_rot_sprite_coeffs
								->edge_points_with_predecessor
									[point_index +
									 1]
								.y;
						break;
					}
					++point_index;
				} while (point_index <= *edge_delta_y_ptr);
			}
		}
		g_flight_sw_rot_sprite_clip_min_x = clip_value;
	}

	if (g_flight_sw_rot_sprite_secondary_edge_x >=
	    g_flight_sw_rot_sprite_viewport_width) {
		if (g_flight_sw_rot_sprite_edge_cursor_x <
		    g_flight_sw_rot_sprite_viewport_width) {
			target_x = g_flight_sw_rot_sprite_viewport_width -
				   g_flight_sw_rot_sprite_edge_cursor_x;
			g_flight_sw_rot_sprite_clip_max_run_idx47 = target_x;
			point_index = 0;
			clip_value = target_x;
			if (*edge_delta_y_ptr >= 0) {
				do {
					if ((uint16_t)g_flight_sw_rot_sprite_coeffs
						    ->edge_points_with_predecessor
							    [point_index + 1]
						    .x == target_x) {
						clip_value =
							g_flight_sw_rot_sprite_coeffs
								->edge_points_with_predecessor
									[point_index +
									 1]
								.y -
							1;
						break;
					}
					++point_index;
				} while (point_index <= *edge_delta_y_ptr);
			}
		} else {
			clip_value = -1;
		}
	} else if (g_flight_sw_rot_sprite_secondary_edge_x < 0) {
		clip_value = -1;
	} else {
		clip_value = g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	}
	g_flight_sw_rot_sprite_clip_max_x = clip_value;
	g_flight_sw_rot_sprite_span_base_x =
		g_flight_sw_rot_sprite_viewport_max_y -
		g_flight_sw_rot_sprite_edge_cursor_y + span_base_offset;
	return 1;
}

/* Steps the octant 5 walk a line: moves g_flight_sw_rot_sprite_dest_line_ptr on one
 * pixel, raises both edge columns by 1 and moves the clip bounds. Returns 0
 * once the line start's column reaches the viewport width, else 1. */
// FUNCTION: XVT 0x423B90
int flight_sw_step_rot_sprite_octant5(void)
{
	g_flight_sw_rot_sprite_dest_line_ptr += g_flight_bytes_per_pixel;
	++g_flight_sw_rot_sprite_primary_edge_x;
	if (g_flight_sw_rot_sprite_primary_edge_x == 0) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
	} else if (g_flight_sw_rot_sprite_primary_edge_x < 0) {
		--g_flight_sw_rot_sprite_clip_min_run_idx47;
		g_flight_sw_rot_sprite_clip_min_x -=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx47];
		if (g_flight_sw_rot_sprite_clip_min_x < 0) {
			g_flight_sw_rot_sprite_clip_min_x = 0;
		}
	} else if (g_flight_sw_rot_sprite_primary_edge_x >=
		   g_flight_sw_rot_sprite_viewport_width) {
		return 0;
	}

	++g_flight_sw_rot_sprite_secondary_edge_x;
	if (g_flight_sw_rot_sprite_secondary_edge_x == 0) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		g_flight_sw_rot_sprite_clip_min_run_idx47 =
			g_flight_sw_rot_sprite_coeffs->run_length_count;
		--g_flight_sw_rot_sprite_clip_min_run_idx47;
		g_flight_sw_rot_sprite_clip_min_x =
			g_flight_sw_rot_sprite_viewport_height -
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx47];
		if (g_flight_sw_rot_sprite_clip_min_x < 0) {
			g_flight_sw_rot_sprite_clip_min_x = 0;
			return 1;
		}
	} else if (g_flight_sw_rot_sprite_secondary_edge_x >=
		   g_flight_sw_rot_sprite_viewport_width) {
		if (g_flight_sw_rot_sprite_secondary_edge_x ==
		    g_flight_sw_rot_sprite_viewport_width) {
			g_flight_sw_rot_sprite_clip_max_x =
				g_flight_sw_rot_sprite_viewport_max_y;
			g_flight_sw_rot_sprite_clip_max_run_idx47 =
				g_flight_sw_rot_sprite_coeffs->run_length_count;
		}
		--g_flight_sw_rot_sprite_clip_max_run_idx47;
		g_flight_sw_rot_sprite_clip_max_x -=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx47];
	}
	return 1;
}

/* Sets up the walk for octant 6 (edge along y, flipX), with the line start at
 * the viewport's top. Returns 0 when the line start's column is under 0, else
 * 1. */
// FUNCTION: XVT 0x423CD0
int flight_sw_init_rot_sprite_octant6(void)
{
	int16_t span_base_offset;
	int16_t clip_value;
	int16_t target_x;
	int16_t point_index;
	int16_t edge_delta_y;
	int16_t *edge_delta_x_ptr;
	int16_t *edge_delta_y_ptr;

	span_base_offset = 0;
	while (g_flight_sw_rot_sprite_edge_cursor_y < 0) {
		g_flight_sw_rot_sprite_edge_cursor_x -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		span_base_offset -= g_flight_sw_rot_sprite_viewport_height;
		g_flight_sw_rot_sprite_edge_cursor_y +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}
	while (g_flight_sw_rot_sprite_edge_cursor_y >=
	       g_flight_sw_rot_sprite_viewport_height) {
		span_base_offset += g_flight_sw_rot_sprite_viewport_height;
		g_flight_sw_rot_sprite_edge_cursor_x +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		g_flight_sw_rot_sprite_edge_cursor_y -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}

	g_flight_sw_rot_sprite_edge_cursor_x +=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor
				[g_flight_sw_rot_sprite_edge_cursor_y + 1]
			.x;
	edge_delta_x_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .x;
	edge_delta_y_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .y;
	g_flight_sw_rot_sprite_primary_edge_y = 0;
	g_flight_sw_rot_sprite_primary_edge_x =
		g_flight_sw_rot_sprite_edge_cursor_x;
	g_flight_sw_rot_sprite_secondary_edge_x =
		g_flight_sw_rot_sprite_edge_cursor_x - *edge_delta_x_ptr;
	g_flight_sw_rot_sprite_secondary_edge_y = *edge_delta_y_ptr;
	g_flight_sw_rot_sprite_clip_min_x = -1;
	g_flight_sw_rot_sprite_clip_min_run_idx47 = -1;
	g_flight_sw_rot_sprite_clip_max_x = -1;
	g_flight_sw_rot_sprite_clip_max_run_idx47 =
		g_flight_sw_rot_sprite_coeffs->run_length_count;
	if (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		return 0;
	}

	clip_value = -1;
	if (g_flight_sw_rot_sprite_edge_cursor_x <
	    g_flight_sw_rot_sprite_viewport_width) {
		clip_value = 0;
	} else {
		target_x = g_flight_sw_rot_sprite_edge_cursor_x -
			   g_flight_sw_rot_sprite_viewport_max_x;
		g_flight_sw_rot_sprite_clip_min_run_idx47 = target_x;
		if (*edge_delta_x_ptr >= target_x) {
			point_index = 0;
			edge_delta_y = *edge_delta_y_ptr;
			while (point_index <= edge_delta_y &&
			       (uint16_t)g_flight_sw_rot_sprite_coeffs
					       ->edge_points_with_predecessor
						       [point_index + 1]
					       .x != target_x) {
				++point_index;
			}
			if (point_index <= edge_delta_y) {
				clip_value =
					g_flight_sw_rot_sprite_coeffs
						->edge_points_with_predecessor
							[point_index + 1]
						.y;
			}
		}
	}
	g_flight_sw_rot_sprite_clip_min_x = clip_value;

	if (g_flight_sw_rot_sprite_viewport_width <=
	    g_flight_sw_rot_sprite_secondary_edge_x) {
		clip_value = -1;
	} else if (g_flight_sw_rot_sprite_secondary_edge_x >= 0 &&
		   g_flight_sw_rot_sprite_viewport_width >
			   g_flight_sw_rot_sprite_secondary_edge_x) {
		clip_value = g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	} else {
		target_x = g_flight_sw_rot_sprite_secondary_edge_x +
			   *edge_delta_x_ptr;
		g_flight_sw_rot_sprite_clip_max_run_idx47 = target_x + 1;
		point_index = *edge_delta_y_ptr;
		while (point_index >= 0 &&
		       (uint16_t)g_flight_sw_rot_sprite_coeffs
				       ->edge_points_with_predecessor
					       [point_index + 1]
				       .x != target_x) {
			--point_index;
		}
		clip_value = -1;
		if (point_index >= 0) {
			clip_value = g_flight_sw_rot_sprite_coeffs
					     ->edge_points_with_predecessor
						     [point_index + 1]
					     .y;
		}
	}

	g_flight_sw_rot_sprite_clip_max_x = clip_value;
	g_flight_sw_rot_sprite_span_base_x =
		span_base_offset + g_flight_sw_rot_sprite_edge_cursor_y;
	return 1;
}

/* Steps the octant 6 walk a line as flight_sw_step_rot_sprite_octant4 does. Returns
 * 0 once the line start's column is under 0, else 1. */
// FUNCTION: XVT 0x423F30
int flight_sw_step_rot_sprite_octant6(void)
{
	--g_flight_sw_rot_sprite_secondary_edge_x;
	g_flight_sw_rot_sprite_dest_line_ptr -= g_flight_bytes_per_pixel;
	--g_flight_sw_rot_sprite_primary_edge_x;
	if (g_flight_sw_rot_sprite_primary_edge_x < 0) {
		return 0;
	}
	if (g_flight_sw_rot_sprite_primary_edge_x ==
	    g_flight_sw_rot_sprite_viewport_max_x) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
	} else if (g_flight_sw_rot_sprite_primary_edge_x >=
			   g_flight_sw_rot_sprite_viewport_width &&
		   g_flight_sw_rot_sprite_secondary_edge_x <
			   g_flight_sw_rot_sprite_viewport_width) {
		--g_flight_sw_rot_sprite_clip_min_run_idx47;
		g_flight_sw_rot_sprite_clip_min_x -=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx47];
		if (g_flight_sw_rot_sprite_clip_min_x < 0) {
			g_flight_sw_rot_sprite_clip_min_x = 0;
		}
	}

	if (g_flight_sw_rot_sprite_viewport_max_x ==
	    g_flight_sw_rot_sprite_secondary_edge_x) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		g_flight_sw_rot_sprite_clip_min_run_idx47 =
			g_flight_sw_rot_sprite_coeffs->run_length_count - 1;
		g_flight_sw_rot_sprite_clip_min_x =
			g_flight_sw_rot_sprite_clip_max_x -
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx47] +
			1;
		if (g_flight_sw_rot_sprite_clip_min_x < 0) {
			g_flight_sw_rot_sprite_clip_min_x = 0;
			return 1;
		}
	} else if (g_flight_sw_rot_sprite_secondary_edge_x < 0) {
		--g_flight_sw_rot_sprite_clip_max_run_idx47;
		g_flight_sw_rot_sprite_clip_max_x -=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx47];
		if (g_flight_sw_rot_sprite_clip_max_x < 0) {
			g_flight_sw_rot_sprite_clip_max_x = 0;
		}
	}
	return 1;
}

/* Sets up the walk for octant 7 (edge along y, flipX and flipY), with the line
 * start at the viewport's bottom. Returns 0 when the far end's column is at or
 * past the viewport width, else 1. */
// FUNCTION: XVT 0x424060
int flight_sw_init_rot_sprite_octant7(void)
{
	int16_t span_base_offset;
	int16_t *edge_delta_x_ptr;
	int16_t *edge_delta_y_ptr;
	int16_t clip_value;
	int16_t point_index;
	int16_t target_x;

	span_base_offset = 0;
	while (g_flight_sw_rot_sprite_edge_cursor_y < 0) {
		g_flight_sw_rot_sprite_edge_cursor_x +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		span_base_offset += g_flight_sw_rot_sprite_viewport_height;
		g_flight_sw_rot_sprite_edge_cursor_y +=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}
	while (g_flight_sw_rot_sprite_edge_cursor_y >=
	       g_flight_sw_rot_sprite_viewport_height) {
		span_base_offset -= g_flight_sw_rot_sprite_viewport_height;
		g_flight_sw_rot_sprite_edge_cursor_x -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.x +
			1;
		g_flight_sw_rot_sprite_edge_cursor_y -=
			g_flight_sw_rot_sprite_coeffs
				->edge_points_with_predecessor[0]
				.y +
			1;
	}

	edge_delta_x_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .x;
	g_flight_sw_rot_sprite_edge_cursor_x +=
		g_flight_sw_rot_sprite_coeffs
			->edge_points_with_predecessor
				[g_flight_sw_rot_sprite_viewport_max_y -
				 g_flight_sw_rot_sprite_edge_cursor_y + 1]
			.x;
	g_flight_sw_rot_sprite_primary_edge_y =
		g_flight_sw_rot_sprite_viewport_max_y;
	g_flight_sw_rot_sprite_primary_edge_x =
		g_flight_sw_rot_sprite_edge_cursor_x;
	edge_delta_y_ptr =
		&g_flight_sw_rot_sprite_coeffs->edge_points_with_predecessor[0]
			 .y;
	g_flight_sw_rot_sprite_secondary_edge_x =
		g_flight_sw_rot_sprite_edge_cursor_x - *edge_delta_x_ptr;
	g_flight_sw_rot_sprite_secondary_edge_y =
		g_flight_sw_rot_sprite_viewport_max_y - *edge_delta_y_ptr;
	g_flight_sw_rot_sprite_clip_min_run_idx47 = -1;
	g_flight_sw_rot_sprite_clip_max_run_idx47 =
		g_flight_sw_rot_sprite_viewport_width;

	clip_value = -1;
	if (g_flight_sw_rot_sprite_edge_cursor_x >= 0) {
		if (g_flight_sw_rot_sprite_edge_cursor_x <
		    g_flight_sw_rot_sprite_viewport_width) {
			clip_value = 0;
		} else {
			target_x = g_flight_sw_rot_sprite_edge_cursor_x -
				   g_flight_sw_rot_sprite_viewport_max_x;
			g_flight_sw_rot_sprite_clip_min_run_idx47 =
				target_x - 1;
			if (*edge_delta_x_ptr >= target_x) {
				point_index = 0;
				if (*edge_delta_y_ptr >= 0) {
					while ((uint16_t)g_flight_sw_rot_sprite_coeffs
						       ->edge_points_with_predecessor
							       [point_index + 1]
						       .x != target_x) {
						++point_index;
						if (point_index >
						    *edge_delta_y_ptr) {
							break;
						}
					}
				}
				if (point_index <= *edge_delta_y_ptr) {
					clip_value =
						g_flight_sw_rot_sprite_coeffs
							->edge_points_with_predecessor
								[point_index +
								 1]
							.y;
				}
			}
		}
	}
	g_flight_sw_rot_sprite_clip_min_x = clip_value;

	if (g_flight_sw_rot_sprite_secondary_edge_x >=
	    g_flight_sw_rot_sprite_viewport_width) {
		return 0;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_x >= 0) {
		clip_value = g_flight_sw_rot_sprite_coeffs->scan_count - 1;
	} else if (g_flight_sw_rot_sprite_edge_cursor_x < 0) {
		clip_value = -1;
	} else {
		target_x = g_flight_sw_rot_sprite_secondary_edge_x +
			   *edge_delta_x_ptr;
		g_flight_sw_rot_sprite_clip_max_run_idx47 = target_x;
		point_index = *edge_delta_y_ptr;
		if (point_index >= 0) {
			while ((uint16_t)g_flight_sw_rot_sprite_coeffs
				       ->edge_points_with_predecessor
					       [point_index + 1]
				       .x != target_x) {
				--point_index;
				if (point_index < 0) {
					break;
				}
			}
		}
		clip_value = -1;
		if (point_index >= 0) {
			clip_value = g_flight_sw_rot_sprite_coeffs
					     ->edge_points_with_predecessor
						     [point_index + 1]
					     .y;
		}
	}

	g_flight_sw_rot_sprite_clip_max_x = clip_value;
	g_flight_sw_rot_sprite_span_base_x =
		g_flight_sw_rot_sprite_viewport_max_y -
		g_flight_sw_rot_sprite_edge_cursor_y + span_base_offset;
	return 1;
}

/* Steps the octant 7 walk a line as flight_sw_step_rot_sprite_octant5 does. Returns
 * 0 once the far end's column reaches the viewport width, else 1. */
// FUNCTION: XVT 0x4242E0
int flight_sw_step_rot_sprite_octant7(void)
{
	g_flight_sw_rot_sprite_dest_line_ptr += g_flight_bytes_per_pixel;
	++g_flight_sw_rot_sprite_primary_edge_x;
	if (g_flight_sw_rot_sprite_primary_edge_x == 0) {
		g_flight_sw_rot_sprite_clip_min_x = 0;
		g_flight_sw_rot_sprite_clip_max_x = -1;
		g_flight_sw_rot_sprite_clip_max_run_idx47 = -1;
	} else if (g_flight_sw_rot_sprite_primary_edge_x >=
		   g_flight_sw_rot_sprite_viewport_width) {
		if (g_flight_sw_rot_sprite_primary_edge_x ==
		    g_flight_sw_rot_sprite_viewport_width) {
			g_flight_sw_rot_sprite_clip_min_run_idx47 = -1;
		}
		++g_flight_sw_rot_sprite_clip_min_run_idx47;
		g_flight_sw_rot_sprite_clip_min_x +=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_min_run_idx47];
	}

	++g_flight_sw_rot_sprite_secondary_edge_x;
	if (g_flight_sw_rot_sprite_secondary_edge_x == 0) {
		g_flight_sw_rot_sprite_clip_max_x =
			g_flight_sw_rot_sprite_coeffs->scan_count - 1;
		return 1;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_x >=
	    g_flight_sw_rot_sprite_viewport_width) {
		return 0;
	}
	if (g_flight_sw_rot_sprite_secondary_edge_x < 0 &&
	    g_flight_sw_rot_sprite_primary_edge_x >= 0) {
		++g_flight_sw_rot_sprite_clip_max_run_idx47;
		g_flight_sw_rot_sprite_clip_max_x +=
			g_flight_sw_rot_sprite_coeffs->run_lengths
				[g_flight_sw_rot_sprite_clip_max_run_idx47];
	}
	return 1;
}

/* Sets g_flight_sw_rot_sprite_dest_buffer to bufferAddress and returns it. */
// FUNCTION: XVT 0x426C50
uint8_t *flight_sw_set_rotated_sprite_dest_buffer(uint8_t *buffer_address)
{
	return g_flight_sw_rot_sprite_dest_buffer = buffer_address;
}

/* Sets the flight viewport to requested_width by requested_height at byte offset
 * requested_base_offset in the surface: the width, height, last column and row
 * and centers (halves, rounded down), g_flight_vp_base_offset, and g_flight_vp_y and
 * g_flight_vp_x from the offset and g_surface_pitch, x in pixels. Returns
 * g_flight_vp_x. Would halve the size and add 120 rows and 160 bytes to the
 * offset when g_flight_viewport_inset_x is 160, which it never is. Ignores
 * viewport_mode. */
// FUNCTION: XVT 0x426C60
unsigned int set_flight_viewport(unsigned int requested_width,
				 unsigned int requested_height,
				 int viewport_mode,
				 unsigned int requested_base_offset)
{
	unsigned int width;
	unsigned int height;
	unsigned int base_offset;
	int pitch;

	(void)viewport_mode;

	if (g_flight_viewport_inset_x == 160) {
		width = requested_width >> 1;
		height = requested_height >> 1;
		pitch = g_surface_pitch;
		base_offset = requested_base_offset + 120 * pitch + 160;
	} else {
		width = requested_width;
		height = requested_height;
		base_offset = requested_base_offset;
		pitch = g_surface_pitch;
	}

	g_flight_vp_width = width;
	g_flight_vp_max_x = width - 1;
	g_flight_vp_center_x = width >> 1;
	g_flight_vp_height = height;
	g_flight_vp_max_y = height - 1;
	g_flight_vp_center_y = height >> 1;
	g_flight_vp_base_offset = base_offset;
	g_flight_vp_y = base_offset / pitch;
	return g_flight_vp_x = base_offset % pitch /
			       (unsigned int)g_flight_bytes_per_pixel;
}

/* Copies g_flight_vp_height rows of g_flight_vp_width bytes, packed in src_pixels,
 * into the viewport on the frame buffer, g_surface_pitch apart. Nothing calls
 * this. */
// FUNCTION: XVT 0x426D50
void flight_sw_copy_legacy8_bit_viewport_to_framebuffer(
	const uint8_t *src_pixels)
{
	int row;
	uint8_t *dst_pixels;
	uint16_t row_width;
	const uint8_t **src_cursor;
	unsigned int copy_width;
	unsigned int advance_width;
	int viewport_x;
	int viewport_y;

	row = 0;
	src_cursor = &src_pixels;
	viewport_x = g_flight_vp_x;
	viewport_y = g_flight_vp_y;
	dst_pixels = g_flight_sw_framebuffer_base;
	dst_pixels += g_flight_bytes_per_pixel * viewport_x;
	dst_pixels += g_surface_pitch * viewport_y;
	if (g_flight_vp_height != 0) {
		row_width = g_flight_vp_width;
		do {
			++row;
			copy_width = row_width;
			memcpy(dst_pixels, *src_cursor, copy_width);
			dst_pixels += g_surface_pitch;
			advance_width = row_width;
			*src_cursor += advance_width;
		} while (row < g_flight_vp_height);
	}
}

/* Saves the viewport and camera matrix in g_saved_flight_viewport (the modern
 * build also saves its own camera state), sets the viewport to width by height
 * at base_offset as set_flight_viewport does, without the inset, and sets
 * g_viewport_span_mask_offset to the second mask, 0xE000. Returns g_flight_vp_x.
 * Ignores refresh_span_mask. */
// FUNCTION: XVT 0x426F40
unsigned int push_flight_viewport(uint16_t width, uint16_t height,
				  int16_t refresh_span_mask,
				  unsigned int base_offset)
{
	(void)refresh_span_mask;
#ifdef XVT_MODERN
	xvt_render_camera_save_viewport();
#endif

	g_saved_flight_viewport.width = g_flight_vp_width;
	g_saved_flight_viewport.height = g_flight_vp_height;
	g_saved_flight_viewport.base_offset = (uint16_t)g_flight_vp_base_offset;
	g_saved_flight_viewport.viewport_y = (uint16_t)g_flight_vp_y;
	g_saved_flight_viewport.viewport_x = (uint16_t)g_flight_vp_x;
	g_saved_flight_viewport.cam_mat_r0_x = g_cam_mat_r0_x;
	g_saved_flight_viewport.cam_mat_r1_x = g_cam_mat_r1_x;
	g_saved_flight_viewport.cam_mat_r2_x = g_cam_mat_r2_x;
	g_saved_flight_viewport.cam_mat_r0_y = g_cam_mat_r0_y;
	g_saved_flight_viewport.cam_mat_r1_y = g_cam_mat_r1_y;
	g_saved_flight_viewport.cam_mat_r2_y = g_cam_mat_r2_y;
	g_saved_flight_viewport.cam_mat_r0_z = g_cam_mat_r0_z;
	g_saved_flight_viewport.cam_mat_r1_z = g_cam_mat_r1_z;
	g_saved_flight_viewport.cam_mat_r2_z = g_cam_mat_r2_z;

	g_flight_vp_width = width;
	g_flight_vp_max_x = width - 1;
	g_flight_vp_center_x = width >> 1;
	g_flight_vp_height = height;
	g_flight_vp_max_y = height - 1;
	{
		unsigned int remainder;
		int pitch;

		pitch = g_surface_pitch;
		g_flight_vp_center_y = height >> 1;
		g_flight_vp_base_offset = base_offset;
		g_flight_vp_y = base_offset / (unsigned int)pitch;
		remainder = base_offset % (unsigned int)pitch;
		g_flight_vp_x =
			remainder / (unsigned int)g_flight_bytes_per_pixel;
		g_viewport_span_mask_offset = 0xE000;
	}
	return (unsigned int)g_flight_vp_x;
}

/* Puts back the camera matrix and viewport push_flight_viewport saved (the modern
 * build also restores its own camera state), with g_flight_vp_base_offset cut to
 * 16 bits, and sets g_viewport_span_mask_offset back to 0xC000. Returns
 * g_flight_vp_x. */
// FUNCTION: XVT 0x427070
int pop_flight_viewport(void)
{
	g_cam_mat_r0_x = g_saved_flight_viewport.cam_mat_r0_x;
	g_cam_mat_r1_x = g_saved_flight_viewport.cam_mat_r1_x;
	g_cam_mat_r2_x = g_saved_flight_viewport.cam_mat_r2_x;
	g_cam_mat_r0_y = g_saved_flight_viewport.cam_mat_r0_y;
	g_cam_mat_r1_y = g_saved_flight_viewport.cam_mat_r1_y;
	g_cam_mat_r2_y = g_saved_flight_viewport.cam_mat_r2_y;
	g_cam_mat_r0_z = g_saved_flight_viewport.cam_mat_r0_z;
	g_cam_mat_r1_z = g_saved_flight_viewport.cam_mat_r1_z;
	g_cam_mat_r2_z = g_saved_flight_viewport.cam_mat_r2_z;
#ifdef XVT_MODERN
	xvt_render_camera_restore_viewport();
#endif
	g_flight_vp_width = g_saved_flight_viewport.width;
	g_flight_vp_max_x = g_saved_flight_viewport.width - 1;
	g_flight_vp_center_x = g_saved_flight_viewport.width >> 1;
	g_flight_vp_height = g_saved_flight_viewport.height;
	g_flight_vp_max_y = g_saved_flight_viewport.height - 1;
	g_flight_vp_center_y = g_saved_flight_viewport.height >> 1;
	g_flight_vp_base_offset = g_saved_flight_viewport.base_offset;
	g_viewport_span_mask_offset = 0xC000;
	g_flight_vp_y = g_saved_flight_viewport.viewport_y;
	return g_flight_vp_x = g_saved_flight_viewport.viewport_x;
}

/* Copies a width_pixels by height_pixels block from source_base (source_pitch bytes
 * per row, from source_x, source_y) to destination_x, destination_y on the frame
 * buffer, g_flight_bytes_per_pixel bytes per pixel. With transparent_color_index
 * 0xFFFF every pixel is copied; otherwise pixels equal to that index, or at 16
 * bits to its palette color, are skipped. Does not clip. */
// FUNCTION: XVT 0x427150
void flight_sw_blit_rect_to_flight_surface(
	uint8_t *source_base, uint16_t transparent_color_index,
	uint16_t source_x, uint16_t source_y, uint16_t destination_x,
	uint16_t destination_y, uint16_t width_pixels, uint16_t height_pixels,
	uint16_t source_pitch)
{
	unsigned int transparent_color;
	int destination_offset;
	uint8_t *source;
	uint8_t *destination;
	int rows_remaining;
	int columns_remaining;

	destination_offset = g_surface_pitch * destination_y +
			     g_flight_bytes_per_pixel * destination_x;
	if (g_flight_bytes_per_pixel == 1) {
		transparent_color = (transparent_color_index << 24) |
				    (transparent_color_index << 16) |
				    (transparent_color_index << 8) |
				    transparent_color_index;
	} else {
		transparent_color =
			g_flight_palette16_bpp[transparent_color_index];
	}
	destination = g_flight_sw_framebuffer_base + destination_offset;
	source = source_base + source_pitch * source_y +
		 g_flight_bytes_per_pixel * source_x;
	if (transparent_color_index == 0xFFFF) {
		if (height_pixels != 0) {
			rows_remaining = height_pixels;
			do {
				memcpy(destination, source,
				       width_pixels * g_flight_bytes_per_pixel);
				destination += g_surface_pitch;
				source += source_pitch;
				--rows_remaining;
			} while (rows_remaining != 0);
		}
	} else if (height_pixels != 0) {
		rows_remaining = height_pixels;
		do {
			if (width_pixels != 0) {
				columns_remaining = width_pixels;
				do {
					if (g_flight_bytes_per_pixel == 1) {
						if ((uint8_t)
							    transparent_color !=
						    *source) {
							*destination = *source;
						}
					} else if (
						g_flight_bytes_per_pixel == 2 &&
						(uint16_t)transparent_color !=
							*(uint16_t *)source) {
						*(uint16_t *)destination =
							*(uint16_t *)source;
					}
					source += g_flight_bytes_per_pixel;
					destination += g_flight_bytes_per_pixel;
					--columns_remaining;
				} while (columns_remaining != 0);
			}
			destination += g_surface_pitch -
				       width_pixels * g_flight_bytes_per_pixel;
			source += source_pitch -
				  width_pixels * g_flight_bytes_per_pixel;
			--rows_remaining;
		} while (rows_remaining != 0);
	}
}

/* Copies a width_pixels by height_pixels block from srcX, srcY on the frame
 * buffer to dstX, dstY in dst_pixels, dst_pitch_bytes per row. Nothing calls
 * this. */
// FUNCTION: XVT 0x4272D0
void flight_sw_copy_framebuffer_rect_to_buffer(uint8_t *dst_pixels,
					       uint16_t src_x, uint16_t src_y,
					       uint16_t dst_x, uint16_t dst_y,
					       uint16_t width_pixels,
					       uint16_t height_pixels,
					       uint16_t dst_pitch_bytes)
{
	uint8_t *source;
	uint8_t *destination;
	int rows_remaining;

	source = g_flight_sw_framebuffer_base + g_surface_pitch * src_y +
		 g_flight_bytes_per_pixel * src_x;
	destination = dst_pixels + dst_pitch_bytes * dst_y +
		      g_flight_bytes_per_pixel * dst_x;
	if (height_pixels != 0) {
		rows_remaining = height_pixels;
		do {
			memcpy(destination, source,
			       width_pixels * g_flight_bytes_per_pixel);
			source += g_surface_pitch;
			destination += dst_pitch_bytes;
			--rows_remaining;
		} while (rows_remaining != 0);
	}
}

/* Fills row y from xStart up to, not including, xEnd with color_index (its
 * palette color at 16 bits), all measured from the g_flightClip rectangle's top
 * left; draws nothing when xEnd is not past xStart. Does not clip. */
// FUNCTION: XVT 0x4377B0
void flight_sw_draw_horizontal_color_span(int x_start, int x_end, int y,
					  uint8_t color_index)
{
	int framebuffer_x_end;
	int framebuffer_x_start;
	int framebuffer_y;
	uint8_t *row_base;

	framebuffer_x_start = g_flight_clip_left + x_start;
	framebuffer_x_end = g_flight_clip_left + x_end;
	framebuffer_y = g_flight_clip_top + y;

	if (g_flight_bytes_per_pixel == 2) {
		uint16_t color;
		uint16_t *destination;
		uint8_t *framebuffer_base;

		framebuffer_base = g_flight_sw_framebuffer_base;
		row_base = framebuffer_base + g_surface_pitch * framebuffer_y;
		color = g_flight_palette16_bpp[color_index];
		if (framebuffer_x_end <= framebuffer_x_start) {
			return;
		}
		destination = (uint16_t *)row_base + framebuffer_x_start;
		while (framebuffer_x_start < framebuffer_x_end) {
			*destination++ = color;
			++framebuffer_x_start;
		}
	} else {
		row_base = g_flight_sw_framebuffer_base +
			   g_surface_pitch * framebuffer_y;
		if (framebuffer_x_end <= framebuffer_x_start) {
			return;
		}
		memset(row_base + framebuffer_x_start, color_index,
		       (size_t)(framebuffer_x_end - framebuffer_x_start));
	}
}

/* Copies a span mask of height rows, width pixels each, from encoded_mask into
 * g_flight_aux_buffer at g_viewport_span_mask_offset, in the form
 * render_scene_initialize reads: per row a signed first byte, then run lengths,
 * where a 0 adds 255 to the next byte and 0, 0 adds 511. The source writes a
 * long run as 0 and a byte b, meaning 256 + b; when g_screen_width is not 320, b
 * of 0 is followed by a byte c, meaning 512 + c, and b of 0xFF means 511. With
 * mirror_horizontal each row's runs are reversed, and its first byte negated
 * when it has an even number of runs. Calls nullsub_2 in the hardware path. */
// FUNCTION: XVT 0x442090
void flight_sw_copy_viewport_span_mask_rle(const uint8_t *encoded_mask,
					   uint16_t width, uint16_t height,
					   int16_t mirror_horizontal)
{
	uint8_t *destination;
	uint8_t row_start_parity;
	uint16_t decoded_width;
	uint8_t encoded_run;
	uint8_t extended_run;
	int run_length;
	uint16_t mirror_decoded_width;
	uint8_t *temp_write;
	uint16_t reversed_decoded_width;
	uint8_t *temp_read;
	int16_t run_count;
	unsigned int reverse_index;
	uint8_t reverse_run;
	uint16_t rows_remaining;

	struct {
		uint8_t saved_row_start_parity;
		uint8_t row_runs[99];
	} mirror_row;

	destination = g_flight_aux_buffer + g_viewport_span_mask_offset;
	if (height != 0) {
		rows_remaining = height;
		do {
			row_start_parity = *encoded_mask;
			if (mirror_horizontal == 0) {
				decoded_width = 0;
				*destination++ = row_start_parity;
				++encoded_mask;
				while (width > decoded_width) {
					encoded_run = *encoded_mask++;
					if (encoded_run == 0) {
						decoded_width += 0xFFu;
						*destination++ = 0;
						extended_run = *encoded_mask++;
						run_length =
							(int)g_screen_width;
						if (run_length == 320) {
							encoded_run =
								(uint8_t)(extended_run +
									  1);
						} else if (extended_run == 0) {
							decoded_width += 0x100u;
							*destination++ = 0;
							extended_run =
								*encoded_mask++;
							encoded_run =
								(uint8_t)(extended_run +
									  1);
						} else if (extended_run ==
							   0xFFu) {
							decoded_width += 0x100u;
							*destination++ = 0;
							encoded_run = 0;
						} else {
							encoded_run =
								(uint8_t)(extended_run +
									  1);
						}
					}
					*destination++ = encoded_run;
					run_length = encoded_run;
					decoded_width =
						(uint16_t)(decoded_width +
							   run_length);
				}
			} else {
				mirror_decoded_width = 0;
				++encoded_mask;
				temp_write = mirror_row.row_runs;
				mirror_row.saved_row_start_parity =
					row_start_parity;
				while (width > mirror_decoded_width) {
					encoded_run = *encoded_mask++;
					if (encoded_run == 0) {
						mirror_decoded_width += 0xFFu;
						*temp_write++ = 0;
						extended_run = *encoded_mask++;
						run_length =
							(int)g_screen_width;
						if (run_length == 320) {
							encoded_run =
								(uint8_t)(extended_run +
									  1);
						} else if (extended_run == 0) {
							mirror_decoded_width +=
								0x100u;
							*temp_write++ = 0;
							extended_run =
								*encoded_mask++;
							encoded_run =
								(uint8_t)(extended_run +
									  1);
						} else if (extended_run ==
							   0xFFu) {
							mirror_decoded_width +=
								0x100u;
							*temp_write++ = 0;
							encoded_run = 0;
						} else {
							encoded_run =
								(uint8_t)(extended_run +
									  1);
						}
					}
					*temp_write++ = encoded_run;
					run_length = encoded_run;
					mirror_decoded_width =
						(uint16_t)(mirror_decoded_width +
							   run_length);
				}

				reversed_decoded_width = 0;
				temp_read = mirror_row.row_runs;
				*destination =
					mirror_row.saved_row_start_parity;
				run_count = 0;
				while (width > reversed_decoded_width) {
					uint8_t *extension_prefix;

					extension_prefix = temp_read;
					reverse_run = *temp_read++;
					if (reverse_run == 0) {
						reversed_decoded_width += 0xFFu;
						reverse_run = *temp_read;
						*extension_prefix = reverse_run;
						*temp_read++ = 0;
						if (reverse_run == 0) {
							reversed_decoded_width +=
								0x100u;
							reverse_run =
								*temp_read;
							*extension_prefix =
								reverse_run;
							*temp_read++ = 0;
						}
					}
					++run_count;
					run_length = reverse_run;
					reversed_decoded_width =
						(uint16_t)(reversed_decoded_width +
							   run_length);
				}

				reverse_index =
					(unsigned int)(temp_read -
						       mirror_row.row_runs);
				if ((run_count & 1) == 0) {
					*destination = (uint8_t)-*destination;
				}
				++destination;
				while (run_count != 0) {
					--reverse_index;
					reverse_run = mirror_row.row_runs
							      [reverse_index];
					*destination++ = reverse_run;
					if (reverse_run == 0) {
						--reverse_index;
						reverse_run =
							mirror_row.row_runs
								[reverse_index];
						*destination++ = reverse_run;
						if (reverse_run == 0) {
							--reverse_index;
							reverse_run =
								mirror_row.row_runs
									[reverse_index];
							*destination++ =
								reverse_run;
						}
					}
					--run_count;
				}
			}
			--rows_remaining;
		} while (rows_remaining != 0);
	}

	if (g_use_hardware3d != 0) {
		nullsub_2();
	}
}

/* Writes into g_flight_aux_buffer at g_viewport_span_mask_offset a span mask of
 * height rows, each one unmasked run of width pixels (first byte 1), in the run
 * form flight_sw_copy_viewport_span_mask_rle writes. Calls nullsub_2 in the hardware
 * path. */
// FUNCTION: XVT 0x442250
void flight_sw_build_full_viewport_span_mask_rle(uint16_t width,
						 unsigned int height)
{
	uint16_t row_index;
	uint16_t width_code;
	uint8_t *cursor;

	row_index = 0;
	cursor = g_flight_aux_buffer + g_viewport_span_mask_offset;
	while (row_index < height) {
		width_code = width;
		*cursor++ = 1;
		if (width >= 0x100u) {
			*cursor++ = 0;
			width_code = (uint16_t)(width - 0xFFu);
			if (width_code >= 0x100u) {
				width_code = (uint16_t)(width_code - 0x100u);
				*cursor++ = 0;
			}
		}
		++row_index;
		*cursor++ = (uint8_t)width_code;
	}
	if (g_use_hardware3d != 0) {
		nullsub_2();
	}
}

/* Returns y times the active row pitch plus x times g_flight_bytes_per_pixel. */
// FUNCTION: XVT 0x4498E0
int32_t flight_sw_compute_pixel_offset(int x, int y)
{
	return y * flight_sw_get_line_pitch() + x * g_flight_bytes_per_pixel;
}

/* Draws an RLE sprite at 16 bits without fading: sets g_flight_sw_rle_palette_shift
 * to 0 and calls flight_sw_blit_sprite_rle_impl16bpp. */
// FUNCTION: XVT 0x449900
void flight_sw_blit_sprite_rle16bpp(uint8_t *rle_data, int x, int y,
				    int transparent_color_index, int mirror)
{
	g_flight_sw_rle_palette_shift = 0;
	flight_sw_blit_sprite_rle_impl16bpp(
		rle_data, x, y, transparent_color_index, mirror, 0, 0);
}

/* Draws an RLE sprite at 16 bits, faded: sets g_flight_sw_rle_palette_shift to
 * palette_shift and calls flight_sw_blit_sprite_rle_impl16bpp without mirroring. */
// FUNCTION: XVT 0x449930
void flight_sw_blit_sprite_rle_faded16bpp(uint8_t *rle_data, int x, int y,
					  int transparent_color_index,
					  int8_t palette_shift,
					  int16_t fade_amount)
{
	unsigned int zero_extended_fade_amount;

	g_flight_sw_rle_palette_shift = palette_shift;
	zero_extended_fade_amount = (uint16_t)fade_amount;
	flight_sw_blit_sprite_rle_impl16bpp(rle_data, x, y,
					    transparent_color_index, 0, 1,
					    (int16_t)zero_extended_fade_amount);
}

/* The 16-bit form of flight_sw_blit_sprite_rle_impl8bpp: the same codes,
 * transparency and fading, each color drawn as its g_flight_palette16_bpp
 * entry. */
// FUNCTION: XVT 0x449970
void flight_sw_blit_sprite_rle_impl16bpp(uint8_t *rle_data, int16_t x,
					 int16_t y, int transparent_color_index,
					 int mirror, char is_faded,
					 int16_t fade_amount)
{
	unsigned int pixel_offset;
	uint8_t *source;
	uint8_t token;
	uint8_t color;
	int16_t alternating_pixels_remaining;
	uint16_t run_length;
	int mirror_flag;
	uint16_t *destination;

	g_flight_sw_rle_transparent_color = (uint8_t)transparent_color_index;
	g_flight_sw_rle_sprite_x = x;
	g_flight_sw_rle_sprite_y = y;
	mirror_flag = mirror;
	source = rle_data;

	for (;;) {
		pixel_offset = flight_sw_get_line_offset(
				       (uint16_t)g_flight_sw_rle_sprite_y) +
			       2 * (uint16_t)g_flight_sw_rle_sprite_x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			unsigned int page;

			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		destination = (uint16_t *)(g_flight_sw_framebuffer_base +
					   pixel_offset);

		for (;;) {
			token = *source++;
			if (token < 0xFB) {
				run_length = token & 3;
				color = token >> 2;
				if (is_faded == 0) {
					color += g_flight_sw_rle_palette_shift;
				}
			} else {
				if (token > 0xFB) {
					if (token == 0xFC) {
						/* In this two-color dither run, run_length holds the second color's palette index,
						 * drawn on every other pixel. */
						color = source[0];
						if (is_faded != 0) {
							if (fade_amount > 0) {
								color -= (uint8_t)
									fade_amount;
								color += (uint8_t)
									g_flight_sw_rle_palette_shift;
								run_length =
									color;
								++run_length;
							} else {
								color = (uint8_t)
									g_flight_sw_rle_palette_shift;
								run_length =
									color;
							}
						} else {
							run_length = color;
							++run_length;
						}
						alternating_pixels_remaining =
							(int16_t)source[1] + 1;
						source += 2;
						while (alternating_pixels_remaining >
						       0) {
							*destination =
								g_flight_palette16_bpp
									[color];
							if (mirror_flag == 0) {
								++destination;
							} else {
								--destination;
							}
							--alternating_pixels_remaining;
							if (alternating_pixels_remaining >
							    0) {
								*destination = g_flight_palette16_bpp
									[(uint8_t)
										 run_length];
								if (mirror_flag ==
								    0) {
									++destination;
								} else {
									--destination;
								}
								--alternating_pixels_remaining;
							}
						}
						continue;
					}
					if (token == 0xFD) {
						run_length = source[0];
						color = source[1];
						source += 2;
					} else {
						break;
					}
				} else {
					if (is_faded == 0) {
						g_flight_sw_rle_palette_shift =
							(int8_t)*source;
					}
					++source;
					continue;
				}
			}

			++run_length;
			if (color == transparent_color_index) {
				if (mirror_flag == 0) {
					destination += run_length;
				} else {
					destination -= run_length;
				}
				continue;
			}
			if (is_faded != 0) {
				if (fade_amount > 0) {
					color -= (uint8_t)fade_amount;
					color += (uint8_t)
						g_flight_sw_rle_palette_shift;
				} else {
					color = (uint8_t)
						g_flight_sw_rle_palette_shift;
				}
			}
			if (mirror_flag == 0) {
				while (run_length > 0) {
					*destination++ =
						g_flight_palette16_bpp[color];
					--run_length;
				}
			} else {
				while (run_length > 0) {
					*destination-- =
						g_flight_palette16_bpp[color];
					--run_length;
				}
			}
		}

		if (token == 0xFF) {
			return;
		}
		++g_flight_sw_rle_sprite_y;
	}
}

/* The 16-bit form of flight_sw_blit_map_icon_rle: each color drawn as the
 * g_flight_palette16_bpp entry 4 above it. */
// FUNCTION: XVT 0x449BD0
void flight_sw_blit_map_icon_rle16bpp(uint8_t *rle_data, int x, int y,
				      int transparent_index, int mirror)
{
	unsigned int pixel_offset;
	uint8_t token;
	uint8_t **source;
	xvt_framebuffer_address destination;
	uint16_t *palette_color;
	uint16_t **palette;

	g_flight_sw_rle_transparent_color = (uint8_t)transparent_index;
	g_flight_sw_rle_sprite_x = (int16_t)x;
	g_flight_sw_rle_sprite_y = (int16_t)y;
	source = &rle_data;
	palette = &palette_color;

	for (;;) {
		pixel_offset = flight_sw_get_line_offset(
				       (uint16_t)g_flight_sw_rle_sprite_y) +
			       2 * (uint16_t)g_flight_sw_rle_sprite_x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    xvt_framebuffer_address_is_legacy_base(
			    g_flight_sw_framebuffer_base)) {
			unsigned int page;

			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		memcpy(&destination, &g_flight_sw_framebuffer_base,
		       sizeof(destination));
		destination = xvt_framebuffer_address_from_base(destination,
								pixel_offset);

		for (;;) {
			uint16_t run_length;
			uint16_t pixels_remaining;

			token = *(*source)++;
			if (token < 0xFB) {
				run_length = token;
				run_length &= 3;
				token >>= 2;
				token += (uint8_t)g_flight_sw_rle_palette_shift;
			} else {
				if (token > 0xFB) {
					if (token == 0xFC) {
						uint16_t palette_index;
						uint16_t next_palette_index;

						palette_index = 0;
						memcpy(&palette_index,
						       (*source)++,
						       sizeof(**source));
						next_palette_index =
							palette_index;
						++next_palette_index;
						run_length = 0;
						memcpy(&run_length, (*source)++,
						       sizeof(**source));
						++run_length;
						if ((int16_t)run_length > 0) {
							*palette =
								&g_flight_palette16_bpp
									[palette_index +
									 4];
							do {
								xvt_framebuffer_address_store16(
									&destination,
									**palette);
								if (mirror ==
								    0) {
									destination +=
										2;
								} else {
									destination -=
										2;
								}
								--run_length;
								if ((int16_t)
									    run_length >
								    0) {
									xvt_framebuffer_address_store16(
										&destination,
										g_flight_palette16_bpp
											[(uint8_t)
												 next_palette_index +
											 4]);
									if (mirror ==
									    0) {
										destination +=
											2;
									} else {
										destination -=
											2;
									}
								}
								--run_length;
							} while (
								(int16_t)
									run_length >
								0);
						}
						continue;
					}
					if (token == 0xFD) {
						run_length = 0;
						memcpy(&run_length, (*source)++,
						       sizeof(**source));
						memcpy(&token, (*source)++,
						       sizeof(token));
					} else {
						break;
					}
				} else {
					g_flight_sw_rle_palette_shift =
						(int8_t)*(*source)++;
					continue;
				}
			}

			++run_length;
			if (transparent_index == token) {
				if (mirror == 0) {
					destination += 2 * run_length;
				} else {
					destination -= 2 * run_length;
				}
			} else if (mirror == 0) {
				if (run_length > 0) {
					memcpy(&pixels_remaining, &run_length,
					       sizeof(pixels_remaining));
					*palette =
						&g_flight_palette16_bpp[token +
									4];
					do {
						xvt_framebuffer_address_store16(
							&destination,
							**palette);
						destination += 2;
						--pixels_remaining;
					} while (pixels_remaining != 0);
				}
			} else {
				if (run_length > 0) {
					memcpy(&pixels_remaining, &run_length,
					       sizeof(pixels_remaining));
					*palette =
						&g_flight_palette16_bpp[token +
									4];
					do {
						xvt_framebuffer_address_store16(
							&destination,
							**palette);
						destination -= 2;
						--pixels_remaining;
					} while (pixels_remaining != 0);
				}
			}
		}

		if (token == 0xFF) {
			return;
		}
		++g_flight_sw_rle_sprite_y;
	}
}

/* Writes the palette color of color_index at x, y on the 16-bit frame buffer
 * through the active row table. color_index is signed, so an index from 0x80 up
 * reads before g_flight_palette16_bpp. Does not clip.
 * flight_render_install_callbacks stores it in g_flight_draw_pixel_fn, which nothing
 * calls. */
// FUNCTION: XVT 0x449EF0
void flight_sw_draw_pixel16bpp(uint16_t x, uint16_t y, int8_t color_index)
{
	unsigned int pixel_offset;
	uint16_t color;
	uint8_t *framebuffer_base;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	pixel_offset = flight_sw_get_line_offset(y) + 2 * x;
#ifndef XVT_MODERN
	if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
	    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
		page = pixel_offset / g_vesa_page_size_bytes;
		pixel_offset %= g_vesa_page_size_bytes;
		rts_vga2_set_current_page((uint8_t)g_vesa_window,
					  (uint16_t)page);
	}
#endif
	color = g_flight_palette16_bpp[(int)color_index];
	framebuffer_base = g_flight_sw_framebuffer_base;
	*(uint16_t *)(framebuffer_base + pixel_offset) = color;
}

/* Fills the g_flightClip rectangle with g_flight_text_bg_color at 16 bits, through
 * flight_sw_fill_rect_or_border16bpp. */
// FUNCTION: XVT 0x44A570
void flight_sw_fill_clip_rect16bpp(void)
{
	g_flight_fill_rect_bottom16bpp = g_flight_clip_bottom;
	g_flight_fill_rect_top16bpp = g_flight_clip_top;
	g_flight_fill_rect_left16bpp = g_flight_clip_left;
	g_flight_fill_rect_right16bpp = g_flight_clip_right;
	flight_sw_fill_rect_or_border16bpp(0);
}

/* The 16-bit form of flight_sw_fill_rect_or_border8bpp, filling with
 * g_flight_palette16_bpp[g_flight_text_bg_color] and stepping the 16-bit
 * g_flightFillRect globals. */
// FUNCTION: XVT 0x44A5B0
void flight_sw_fill_rect_or_border16bpp(uint16_t border_thickness)
{
	int16_t row;
	unsigned int pixel_offset;
	uint16_t *destination;
	int16_t pixels_remaining;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	g_flight_fill_rect_current_y16bpp = g_flight_fill_rect_top16bpp;
	g_flight_fill_rect_remaining_rows16bpp =
		g_flight_fill_rect_bottom16bpp - g_flight_fill_rect_top16bpp;
	if ((int16_t)(g_flight_fill_rect_right16bpp -
		      g_flight_fill_rect_left16bpp) <= 0) {
		return;
	}

	if (border_thickness != 0) {
		for (row = 0; row < border_thickness; ++row) {
			pixel_offset =
				flight_sw_get_line_offset(
					g_flight_fill_rect_current_y16bpp) +
				2 * g_flight_fill_rect_left16bpp;
#ifndef XVT_MODERN
			if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flight_sw_framebuffer_base ==
				    g_sw_framebuffer_base) {
				page = pixel_offset / g_vesa_page_size_bytes;
				pixel_offset %= g_vesa_page_size_bytes;
				rts_vga2_set_current_page(
					(uint8_t)g_vesa_window, (uint16_t)page);
			}
#endif
			destination =
				&((uint16_t *)g_flight_sw_framebuffer_base)
					[pixel_offset / 2];
			pixels_remaining = g_flight_fill_rect_right16bpp -
					   g_flight_fill_rect_left16bpp;
			if (pixels_remaining-- != 0) {
				do {
					*destination++ = g_flight_palette16_bpp
						[g_flight_text_bg_color];
				} while (pixels_remaining-- != 0);
			}
			--g_flight_fill_rect_remaining_rows16bpp;
			++g_flight_fill_rect_current_y16bpp;
		}

		while ((unsigned int)g_flight_fill_rect_remaining_rows16bpp >
		       border_thickness) {
			int16_t center_width;
			int16_t left_pixels_remaining;
			int16_t right_pixels_remaining;

			pixel_offset =
				flight_sw_get_line_offset(
					g_flight_fill_rect_current_y16bpp) +
				2 * g_flight_fill_rect_left16bpp;
#ifndef XVT_MODERN
			if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flight_sw_framebuffer_base ==
				    g_sw_framebuffer_base) {
				page = pixel_offset / g_vesa_page_size_bytes;
				pixel_offset %= g_vesa_page_size_bytes;
				rts_vga2_set_current_page(
					(uint8_t)g_vesa_window, (uint16_t)page);
			}
#endif
			destination =
				&((uint16_t *)g_flight_sw_framebuffer_base)
					[pixel_offset / 2];
			left_pixels_remaining = border_thickness;
			right_pixels_remaining = border_thickness;
			center_width = g_flight_fill_rect_right16bpp -
				       2 * border_thickness -
				       g_flight_fill_rect_left16bpp;
			while (left_pixels_remaining-- != 0) {
				*destination++ = g_flight_palette16_bpp
					[g_flight_text_bg_color];
			}
			destination += center_width;
			while (right_pixels_remaining-- != 0) {
				*destination++ = g_flight_palette16_bpp
					[g_flight_text_bg_color];
			}
			--g_flight_fill_rect_remaining_rows16bpp;
			++g_flight_fill_rect_current_y16bpp;
		}

		for (row = 0; row < border_thickness; ++row) {
			pixel_offset =
				flight_sw_get_line_offset(
					g_flight_fill_rect_current_y16bpp) +
				2 * g_flight_fill_rect_left16bpp;
#ifndef XVT_MODERN
			if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flight_sw_framebuffer_base ==
				    g_sw_framebuffer_base) {
				page = pixel_offset / g_vesa_page_size_bytes;
				pixel_offset %= g_vesa_page_size_bytes;
				rts_vga2_set_current_page(
					(uint8_t)g_vesa_window, (uint16_t)page);
			}
#endif
			destination =
				&((uint16_t *)g_flight_sw_framebuffer_base)
					[pixel_offset / 2];
			pixels_remaining = g_flight_fill_rect_right16bpp -
					   g_flight_fill_rect_left16bpp;
			if (pixels_remaining-- != 0) {
				do {
					*destination++ = g_flight_palette16_bpp
						[g_flight_text_bg_color];
				} while (pixels_remaining-- != 0);
			}
			--g_flight_fill_rect_remaining_rows16bpp;
			++g_flight_fill_rect_current_y16bpp;
		}
		return;
	}

	while (g_flight_fill_rect_remaining_rows16bpp != 0) {
		pixel_offset = flight_sw_get_line_offset(
				       g_flight_fill_rect_current_y16bpp) +
			       2 * g_flight_fill_rect_left16bpp;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		destination = &(
			(uint16_t *)
				g_flight_sw_framebuffer_base)[pixel_offset / 2];
		pixels_remaining = g_flight_fill_rect_right16bpp -
				   g_flight_fill_rect_left16bpp;
		if (pixels_remaining <= 0) {
			return;
		}
		if (pixels_remaining-- != 0) {
			do {
				*destination++ = g_flight_palette16_bpp
					[g_flight_text_bg_color];
			} while (pixels_remaining-- != 0);
		}
		--g_flight_fill_rect_remaining_rows16bpp;
		++g_flight_fill_rect_current_y16bpp;
	}
}

/* Clips the rectangle from x1, y1 to x2, y2 (ends not included) to the
 * g_flightClip rectangle, stores it in the 16-bit g_flightFillRect globals, and
 * fills it or its frame with flight_sw_fill_rect_or_border16bpp when anything is
 * left. */
// FUNCTION: XVT 0x44A980
void flight_sw_fill_rect_clipped16bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				      uint16_t y2, uint16_t border_thickness)
{
	uint16_t clipped_top;
	uint16_t clipped_bottom;

	g_flight_fill_rect_left16bpp = x1;
	g_flight_fill_rect_right16bpp = x2;
	clipped_top = y1;
	clipped_bottom = y2;
	if (x1 < g_flight_clip_left) {
		g_flight_fill_rect_left16bpp = g_flight_clip_left;
	}
	if (x2 > g_flight_clip_right) {
		g_flight_fill_rect_right16bpp = g_flight_clip_right;
	}

	g_flight_fill_rect_top16bpp = y1;
	if (g_flight_fill_rect_top16bpp < g_flight_clip_top) {
		clipped_top = g_flight_clip_top;
	}
	g_flight_fill_rect_bottom16bpp = y2;
	if (g_flight_fill_rect_bottom16bpp > g_flight_clip_bottom) {
		clipped_bottom = g_flight_clip_bottom;
	}
	g_flight_fill_rect_bottom16bpp = clipped_bottom;
	g_flight_fill_rect_top16bpp = clipped_top;
	if (clipped_bottom > clipped_top &&
	    g_flight_fill_rect_left16bpp < g_flight_fill_rect_right16bpp) {
		flight_sw_fill_rect_or_border16bpp(border_thickness);
	}
}

/* Copies height rows of width 16-bit pixels from x, y on the frame buffer into
 * buffer, row after row. Does not clip. */
// FUNCTION: XVT 0x44AB10
void flight_sw_save_screen_rect16bpp(uint16_t *buffer, int x, int y,
				     int16_t width, int height)
{
	uint16_t *output;
	uint16_t *source;
	uint16_t pixel;
	unsigned int pixel_offset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	output = buffer;
	if (height == 0) {
		return;
	}
	do {
		pixel_offset = flight_sw_get_line_offset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		g_saved_row_pixels_remaining = width;
		source = (uint16_t *)(g_flight_sw_framebuffer_base +
				      pixel_offset);
		while (g_saved_row_pixels_remaining > 0) {
			pixel = *source++;
			*output++ = pixel;
			--g_saved_row_pixels_remaining;
		}
		--height;
		++y;
	} while (height != 0);
}

/* Writes height rows of width 16-bit pixels from buffer back at x, y. Does not
 * clip. */
// FUNCTION: XVT 0x44ABC0
void flight_sw_restore_screen_rect16bpp(uint16_t *buffer, int x, int y,
					int16_t width, int height)
{
	uint16_t *input;
	uint16_t *destination;
	uint16_t pixel;
	unsigned int pixel_offset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	input = buffer;
	if (height == 0) {
		return;
	}
	do {
		pixel_offset = flight_sw_get_line_offset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		g_saved_row_pixels_remaining = width;
		destination = (uint16_t *)(g_flight_sw_framebuffer_base +
					   pixel_offset);
		while (g_saved_row_pixels_remaining > 0) {
			pixel = *input;
			*destination = pixel;
			++destination;
			++input;
			--g_saved_row_pixels_remaining;
		}
		--height;
		++y;
	} while (height != 0);
}

/* The 16-bit form of flight_sw_draw_point_array8bpp, testing for the palette color
 * of index 44 and with no second row at 640x480: a point not drawn has its
 * third word set to 0, and a drawn one keeps its color there. */
// FUNCTION: XVT 0x44AC70
void flight_sw_draw_point_array16bpp(uint16_t *points, int16_t count)
{
	uint16_t x;
	uint16_t y;
	uint16_t *current;
	int16_t remaining;
	unsigned int pixel_offset;
	int16_t *destination;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	current = points;
	remaining = count;
	if (remaining == 0) {
		return;
	}
	do {
		x = current[0];
		y = current[1];
		pixel_offset = flight_sw_get_line_offset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_sw_framebuffer_base == g_flight_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			rts_vga2_set_current_page(1, (uint16_t)page);
		}
#endif
		destination = (int16_t *)(g_flight_sw_framebuffer_base +
					  pixel_offset);
		if (*destination != g_flight_palette16_bpp[44]) {
			current[2] = 0;
		} else {
			*destination = (int16_t)
				g_flight_palette16_bpp[(uint8_t)current[2]];
		}
		--remaining;
		current += 3;
	} while (remaining != 0);
}

/* Writes the palette color of index 44 back at each point whose third word has
 * a nonzero low byte. */
// FUNCTION: XVT 0x44AD30
void flight_sw_erase_point_array16bpp(uint16_t *points, int16_t count)
{
	unsigned int pixel_offset;
#ifndef XVT_MODERN
	unsigned int legacy_resolution_mode;
#endif
	int16_t remaining;
	uint16_t *current;

#ifndef XVT_MODERN
	legacy_resolution_mode = FLIGHT_RESOLUTION_320X240;
#endif
	remaining = count;
	if (remaining == 0) {
		return;
	}
	current = points;
	do {
		unsigned int x;
		unsigned int y;
		uint16_t *destination;
#ifndef XVT_MODERN
		unsigned int page;
#endif

		x = current[0];
		y = current[1];
		pixel_offset = flight_sw_get_line_offset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != legacy_resolution_mode &&
		    g_sw_framebuffer_base == g_flight_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		destination = (uint16_t *)(g_flight_sw_framebuffer_base +
					   pixel_offset);
		if ((uint8_t)current[2] != 0) {
			*destination = g_flight_palette16_bpp[44];
		}

		--remaining;
		current += 3;
	} while (remaining != 0);
}

/* Draws the 10-pixel radar target marker of g_radar_target_marker_shape16bpp at
 * g_radar_target_marker_draw_x and g_radar_target_marker_draw_y in the palette color of
 * index 206, saving the pixels under it. Does not clip. */
// FUNCTION: XVT 0x44ADD0
void flight_sw_draw_radar_target_marker16bpp(void)
{
	unsigned int pixel_offset;
	int16_t remaining;
	uint16_t offset_index;
	uint16_t saved_pixel_index;
	uint16_t *pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = 10;
	offset_index = 0;
	saved_pixel_index = 0;
	do {
		pixel_offset =
			flight_sw_get_line_offset(
				g_radar_target_marker_draw_y +
				g_radar_target_marker_shape16bpp[offset_index +
								 1]) +
			2 * (g_radar_target_marker_draw_x +
			     g_radar_target_marker_shape16bpp[offset_index]);
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			rts_vga2_set_current_page(1, (uint16_t)page);
		}
#endif
		offset_index += 2;
		pixel = (uint16_t *)(g_flight_sw_framebuffer_base +
				     pixel_offset);
		g_radar_target_marker_saved_pixels16bpp[saved_pixel_index++] =
			*pixel;
		*pixel = g_flight_palette16_bpp[206];
		--remaining;
	} while (remaining != 0);
}

/* Puts back the 10 pixels flight_sw_draw_radar_target_marker16bpp saved, at
 * g_radar_target_marker_restore_x and g_radar_target_marker_restore_y. */
// FUNCTION: XVT 0x44AEB0
void flight_sw_restore_radar_target_marker16bpp(void)
{
	unsigned int pixel_offset;
	int16_t remaining;
	uint16_t offset_index;
	uint16_t saved_pixel_index;
	uint16_t pixel;
	uint8_t *framebuffer_base;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	offset_index = 0;
	remaining = 10;
	saved_pixel_index = 0;
	do {
		pixel_offset =
			flight_sw_get_line_offset(
				g_radar_target_marker_restore_y +
				g_radar_target_marker_shape16bpp[offset_index +
								 1]) +
			2 * (g_radar_target_marker_restore_x +
			     g_radar_target_marker_shape16bpp[offset_index]);
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_sw_framebuffer_base == g_flight_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		framebuffer_base = g_flight_sw_framebuffer_base + pixel_offset;
		offset_index += 2;
		pixel = g_radar_target_marker_saved_pixels16bpp
			[saved_pixel_index++];
		*(uint16_t *)framebuffer_base = pixel;
		--remaining;
	} while (remaining != 0);
}

/* Draws the 7-pixel cross marker centered on x, y in the palette color of
 * color_index, saving the pixels under it; returns that color. Nothing calls
 * this. */
// FUNCTION: XVT 0x44AF70
uint16_t flight_sw_draw_cross_marker16bpp(uint16_t x, uint16_t y,
					  uint8_t color_index)
{
	int16_t remaining;
	unsigned int coordinates[2];
	uint16_t offset_index;
	uint16_t saved_pixel_index;
	uint16_t *color;
	uint16_t result;

	remaining = 7;
	coordinates[0] = y;
	offset_index = 0;
	saved_pixel_index = 0;
	coordinates[1] = x;
	color = &g_flight_palette16_bpp[color_index];
	do {
		unsigned int pixel_offset;
		uint16_t *pixel;

		pixel_offset =
			flight_sw_get_line_offset(
				coordinates[0] +
				((int8_t *)
					 g_flight_sw_cross_marker_offsets16bpp)
					[offset_index + 1]) +
			2 * (coordinates[1] +
			     ((int8_t *)g_flight_sw_cross_marker_offsets16bpp)
				     [offset_index]);
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			unsigned int page;

			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
			rts_vga2_set_current_page(1, (uint16_t)page);
		}
#endif
		offset_index += 2;
		pixel = (uint16_t *)(g_flight_sw_framebuffer_base +
				     pixel_offset);
		g_flight_sw_cross_marker_saved_pixels16bpp
			[saved_pixel_index++] = *pixel;
		result = *color;
		*pixel = *color;
		--remaining;
	} while (remaining != 0);
	return result;
}

/* Puts back the 7 pixels saved around x, y and returns the last of them.
 * Nothing calls this. */
// FUNCTION: XVT 0x44B070
uint16_t flight_sw_restore_cross_marker16bpp(uint16_t x, uint16_t y)
{
	int16_t remaining;
	uint16_t offset_index;
	uint16_t saved_pixel_index;
	unsigned int pixel_offset;
	uint8_t *framebuffer_base;
	uint16_t pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = 7;
	offset_index = 0;
	saved_pixel_index = 0;
	do {
		pixel_offset =
			flight_sw_get_line_offset(
				y +
				((int8_t *)
					 g_flight_sw_cross_marker_offsets16bpp)
					[offset_index + 1]) +
			2 * (x +
			     ((int8_t *)g_flight_sw_cross_marker_offsets16bpp)
				     [offset_index]);
#ifndef XVT_MODERN
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
		    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
			page = pixel_offset / g_vesa_page_size_bytes;
			pixel_offset %= g_vesa_page_size_bytes;
			rts_vga2_set_current_page((uint8_t)g_vesa_window,
						  (uint16_t)page);
		}
#endif
		offset_index += 2;
		framebuffer_base = g_flight_sw_framebuffer_base;
		pixel = g_flight_sw_cross_marker_saved_pixels16bpp
			[saved_pixel_index++];
		--remaining;
		*(uint16_t *)(framebuffer_base + pixel_offset) = pixel;
	} while (remaining != 0);
	return pixel;
}

/* The 16-bit form of flight_sw_draw_line8bpp, drawing in the palette color of
 * color_idx with rows the active row pitch apart. The modern build also returns
 * from an upward line whose clipped end lies below its clipped start. */
// FUNCTION: XVT 0x44B140
void flight_sw_draw_line16bpp(int x1, int y1, int x2, int y2, uint8_t color_idx)
{
	uint16_t color;
	int delta_x;
	int delta_y;
	int start_y;
	int end_x;
	uint8_t *pixel;

	color = g_flight_palette16_bpp[color_idx];
	end_x = x2;
	delta_x = end_x - x1;
	if (delta_x < 0) {
		int swap_x;

		swap_x = x1;
		start_y = y2;
		y2 = y1;
		delta_x = -delta_x;
		x1 = end_x;
		end_x = swap_x;
	} else {
		if (delta_x == 0) {
			int count;

			if (x1 < g_flight_clip_left) {
				return;
			}
			if (x1 >= g_flight_clip_right) {
				return;
			}

			if (y2 < y1) {
				int swap_y;

				swap_y = y1;
				y1 = y2;
				y2 = swap_y;
			}
			if (y1 < g_flight_clip_top) {
				y1 = g_flight_clip_top;
			}
			if (y2 >= g_flight_clip_bottom) {
				y2 = g_flight_clip_bottom - 1;
			}

			count = y2 - y1;
			if (count > 0) {
				pixel = g_flight_sw_framebuffer_base +
					y1 * flight_sw_get_line_pitch() +
					x1 * g_flight_bytes_per_pixel;
				while (count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += flight_sw_get_line_pitch();
				}
			}
			return;
		}
		start_y = y1;
	}

	if (x1 < g_flight_clip_right && end_x >= g_flight_clip_left) {
		delta_y = y2 - start_y;
		if (delta_y < 0) {
			delta_y = -delta_y;
			if (start_y < g_flight_clip_top) {
				return;
			}
			if (y2 >= g_flight_clip_bottom) {
				return;
			}

			if (start_y >= g_flight_clip_bottom) {
				int advance;

				advance = math2_ab_over_c32(
					start_y - g_flight_clip_bottom + 1,
					delta_x, delta_y);
				x1 += advance;
				if (x1 >= g_flight_clip_right) {
					return;
				}
				start_y = g_flight_clip_bottom - 1;
			}
			if (x1 < g_flight_clip_left) {
				int advance;

				advance = math2_ab_over_c32(g_flight_clip_left -
								    x1,
							    delta_y, delta_x);
				start_y -= advance;
				if (start_y < g_flight_clip_top) {
					return;
				}
				x1 = g_flight_clip_left;
			}
			if (end_x >= g_flight_clip_right) {
				end_x = g_flight_clip_right - 1;
			}
			if (y2 < g_flight_clip_top) {
				y2 = g_flight_clip_top;
			}

#ifdef XVT_MODERN
			if (y2 > start_y) {
				return;
			}
#endif

			pixel = g_flight_sw_framebuffer_base +
				start_y * flight_sw_get_line_pitch() +
				x1 * g_flight_bytes_per_pixel;
			if (delta_x >= delta_y) {
				int error;
				int y_steps;
				int x_count;

				error = delta_x >> 1;
				x_count = end_x - x1;
				y_steps = start_y - y2 + 1;
				while (x_count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += g_flight_bytes_per_pixel;
					error -= delta_y;
					if (error < 0) {
						error += delta_x;
						--y_steps;
						if (y_steps == 0) {
							return;
						}
						pixel -=
							flight_sw_get_line_pitch();
					}
				}
			} else {
				int error;
				int x_steps;
				int y_count;

				error = delta_y >> 1;
				x_steps = end_x - x1 + 1;
				y_count = start_y - y2;
				while (y_count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel -= flight_sw_get_line_pitch();
					error -= delta_x;
					if (error < 0) {
						error += delta_y;
						--x_steps;
						if (x_steps == 0) {
							return;
						}
						pixel +=
							g_flight_bytes_per_pixel;
					}
				}
			}
		} else if (delta_y > 0) {
			if (start_y >= g_flight_clip_bottom) {
				return;
			}
			if (y2 < g_flight_clip_top) {
				return;
			}

			if (start_y < g_flight_clip_top) {
				int advance;

				advance = math2_ab_over_c32(g_flight_clip_top -
								    start_y,
							    delta_x, delta_y);
				x1 += advance;
				if (x1 >= g_flight_clip_right) {
					return;
				}
				start_y = g_flight_clip_top;
			}
			if (x1 < g_flight_clip_left) {
				int advance;

				advance = math2_ab_over_c32(g_flight_clip_left -
								    x1,
							    delta_y, delta_x);
				start_y += advance;
				if (start_y >= g_flight_clip_bottom) {
					return;
				}
				x1 = g_flight_clip_left;
			}
			if (end_x >= g_flight_clip_right) {
				end_x = g_flight_clip_right - 1;
			}
			if (y2 >= g_flight_clip_bottom) {
				y2 = g_flight_clip_bottom - 1;
			}

			pixel = g_flight_sw_framebuffer_base +
				start_y * flight_sw_get_line_pitch() +
				x1 * g_flight_bytes_per_pixel;
			if (delta_x >= delta_y) {
				int error;
				int y_steps;
				int x_count;

				error = delta_x >> 1;
				x_count = end_x - x1;
				y_steps = y2 - start_y + 1;
				while (x_count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += g_flight_bytes_per_pixel;
					error -= delta_y;
					if (error < 0) {
						error += delta_x;
						--y_steps;
						if (y_steps == 0) {
							return;
						}
						pixel +=
							flight_sw_get_line_pitch();
					}
				}
			} else {
				int error;
				int x_steps;
				int y_count;

				error = delta_y >> 1;
				x_steps = end_x - x1 + 1;
				y_count = y2 - start_y;
				while (y_count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += flight_sw_get_line_pitch();
					error -= delta_x;
					if (error < 0) {
						error += delta_y;
						--x_steps;
						if (x_steps == 0) {
							return;
						}
						pixel +=
							g_flight_bytes_per_pixel;
					}
				}
			}
		} else if (start_y >= g_flight_clip_top &&
			   start_y < g_flight_clip_bottom) {
			int count;

			if (x1 < g_flight_clip_left) {
				x1 = g_flight_clip_left;
			}
			if (end_x >= g_flight_clip_right) {
				end_x = g_flight_clip_right - 1;
			}

			count = end_x - x1;
			if (count > 0) {
				pixel = g_flight_sw_framebuffer_base +
					start_y * flight_sw_get_line_pitch() +
					x1 * g_flight_bytes_per_pixel;
				while (count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += g_flight_bytes_per_pixel;
				}
			}
		}
	}
}

/* Returns trig2_calcsinemagnitude(angle): the sine's magnitude with 65536
 * standing for 1, as a 16-bit pattern. */
// FUNCTION: XVT 0x46A3B0
int16_t flight_sw_lookup_sprite_sine_magnitude_q16(int16_t angle)
{
	return trig2_calcsinemagnitude(angle);
}

/* Copies what was drawn in the box from start_x, start_y up to, not including,
 * end_x, end_y from the rotated-sprite buffer at pDst to the screen, locking the
 * flight surface unless g_flight_surface_already_locked is set. A drawn pixel has
 * 0x80 in its high byte at 16 bits, which is replaced by the sprite's palette
 * color, or is under 0x40 at 8 bits, which is mapped through
 * g_flight_sw_rot_sprite_palette8. Each run of drawn pixels goes out through
 * sw3d_blit_occluded_span with g_proj_scale_int / g_view_space_depth as its inverse
 * depth. row_skip_bytes takes the pointer from a row's end to the next row's
 * start. */
// FUNCTION: XVT 0x486470
void flight_sw_blit_prepared_rotated_sprite_spans(uint8_t *p_dst,
						  int row_skip_bytes,
						  int start_x, int start_y,
						  int end_x, int end_y)
{
	int scan_y;
	uint8_t *pixel;
	int scan_x;
	float sprite_w;

	if (g_flight_surface_already_locked == 0) {
		flight_surface_lock();
	}
	scan_y = start_y;
	sprite_w = (float)(unsigned int)g_proj_scale_int /
		   (float)g_view_space_depth;
	if (g_flight_bytes_per_pixel == 2) {
		if (end_y > scan_y) {
			pixel = p_dst;
			do {
				scan_x = start_x;
				while (scan_x < end_x) {
					uint8_t *run_start;
					int run_start_x;

					while (scan_x < end_x &&
					       pixel[1] != 0x80) {
						pixel += 2;
						++scan_x;
					}
					if (scan_x == end_x) {
						break;
					}

					run_start_x = scan_x;
					run_start = pixel;
					while (scan_x < end_x &&
					       pixel[1] == 0x80) {
						pixel[1] =
							g_flight_sw_rot_sprite_palette16_high
								[pixel[0]];
						pixel[0] =
							g_flight_sw_rot_sprite_palette16_low
								[pixel[0]];
						pixel += 2;
						++scan_x;
					}
					sw3d_blit_occluded_span(
						run_start, run_start_x, scan_x,
						scan_y, sprite_w);
				}
				++scan_y;
				pixel += row_skip_bytes;
			} while (scan_y < end_y);
		}
	} else if (end_y > scan_y) {
		pixel = p_dst;
		do {
			scan_x = start_x;
			while (scan_x < end_x) {
				uint8_t *run_start;
				int run_start_x;

				while (scan_x < end_x && *pixel >= 0x40) {
					++pixel;
					++scan_x;
				}
				if (scan_x == end_x) {
					break;
				}

				run_start_x = scan_x;
				run_start = pixel;
				while (scan_x < end_x && *pixel < 0x40) {
					uint8_t color;

					color = *pixel;
					*pixel = g_flight_sw_rot_sprite_palette8
						[color];
					++pixel;
					++scan_x;
				}
				sw3d_blit_occluded_span(run_start, run_start_x,
							scan_x, scan_y,
							sprite_w);
			}
			++scan_y;
			pixel += row_skip_bytes;
		} while (scan_y < end_y);
	}
	if (g_flight_surface_already_locked == 0) {
		flight_surface_unlock();
	}
}

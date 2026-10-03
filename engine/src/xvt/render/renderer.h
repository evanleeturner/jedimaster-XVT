#ifndef XVT_RENDER_RENDERER_H
#define XVT_RENDER_RENDERER_H

#include "aeron/compat/ddraw.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flight_blit_sprite_fn)(uint8_t *rle_data, int x, int y,
				      int end_marker, int mirror);
typedef void (*flight_blit_sprite_faded_fn)(uint8_t *rle_data, int x, int y,
					    int end_marker,
					    int8_t palette_shift, int16_t fade);
typedef void (*flight_draw_char_fn)(uint8_t ch);
typedef void (*flight_set_palette_range_fn)(struct rgb_triplet *palette,
					    int16_t start_idx, uint16_t count);
typedef void (*flight_palette_fn)(struct rgb_triplet *palette);
typedef int (*flight_compute_pixel_offset_fn)(int x, int y);
typedef void (*flight_fill_rect_clipped_fn)(uint16_t x1, uint16_t y1,
					    uint16_t x2, uint16_t y2,
					    uint16_t border_thickness);
typedef void (*flight_screen_rect_fn)(void *buffer, int x, int y, int16_t width,
				      int height);
typedef void (*flight_draw_point_array_fn)(uint16_t *points, int16_t count);
typedef void (*flight_draw_pixel_fn)(uint16_t x, uint16_t y, int8_t color);
typedef void (*flight_draw_line_fn)(int x1, int y1, int x2, int y2,
				    uint8_t color);

extern IDirectDraw *g_flight_direct_draw;
extern int g_forced_lod_level;
extern float g_lod_distance_scale;
extern int g_mipmapping_enabled;
extern float g_mip_lod_scale;
extern int g_dithering_enabled;
extern int g_local_lights_enabled;
extern int g_specular_enabled;
extern int g_dir_lighting_enabled;
extern int g_texture_resolution_level;
extern uint8_t g_flight_transparent_color_index;
extern uint8_t g_flight_pixel_mode;
extern uint8_t g_flight_graphics_detail_preset;
extern uint16_t g_flight_viewport_mode;
extern int g_use_hardware3d;
extern int g_bilinear_enabled;
extern int g_loading_model;
extern int g_surface_width;
extern int g_surface_height;
extern void *g_surface_pixels;
extern int g_display_mode_width;
extern int g_display_mode_height;
extern unsigned int g_screen_width;
extern unsigned int g_screen_height;
extern void *g_flight_offscreen_buffer;
extern void (*g_flight_fill_clip_rect_fn)(void);
extern int g_flight_vp_x;
extern int g_flight_vp_y;
extern uint16_t g_flight_vp_width;
extern uint16_t g_flight_vp_height;
extern uint16_t g_render_target_component_idx;
extern uint16_t g_render_object_ref;
extern uint16_t g_render_object_ref_flags;
extern uint16_t g_flight_vp_center_x;
extern uint16_t g_flight_vp_max_x;
extern uint16_t g_flight_vp_max_y;
extern uint16_t g_flight_vp_center_y;
extern unsigned int g_flight_vp_base_offset;
extern int g_surface_pitch;
extern int g_proj_offset_y;
extern uint32_t g_proj_scale_int;
extern flight_blit_sprite_fn g_flight_blit_sprite_fn;
extern flight_blit_sprite_faded_fn g_flight_blit_sprite_faded_fn;
extern flight_draw_char_fn g_flight_draw_char_fn;
extern void (*g_flight_init_line_buffer_fn)(void);
extern void (*g_flight_render_transition_hook)(void);
extern void (*g_flight_reset_palette_fn)(void);
extern flight_set_palette_range_fn g_flight_set_palette_range_fn;
extern flight_palette_fn g_flight_get_palette_fn;
extern flight_palette_fn g_flight_set_palette_fn;
extern flight_compute_pixel_offset_fn g_flight_compute_pixel_offset_fn;
extern flight_fill_rect_clipped_fn g_flight_fill_rect_clipped_fn;
extern flight_screen_rect_fn g_flight_save_screen_rect_fn;
extern flight_screen_rect_fn g_flight_restore_screen_rect_fn;
extern flight_draw_point_array_fn g_flight_draw_point_array_fn;
extern flight_draw_point_array_fn g_flight_draw_point_array_masked_fn;
extern flight_draw_pixel_fn g_flight_draw_pixel_fn;
extern void (*g_flight_draw_radar_target_marker_fn)(void);
extern void (*g_flight_restore_radar_target_marker_fn)(void);
extern flight_draw_line_fn g_flight_draw_line_fn;
extern int g_cur_mat_r0_x;
extern int g_cur_mat_r0_y;
extern int g_cur_mat_r0_z;
extern int g_cur_mat_r1_x;
extern int g_cur_mat_r1_y;
extern int g_cur_mat_r1_z;
extern int g_cur_mat_r2_x;
extern int g_cur_mat_r2_y;
extern int g_cur_mat_r2_z;
extern int g_obj_view_mat_r0_x;
extern int g_obj_view_mat_r0_y;
extern int g_obj_view_mat_r0_z;
extern int g_obj_view_mat_r1_x;
extern int g_obj_view_mat_r1_y;
extern int g_obj_view_mat_r1_z;
extern int g_obj_view_mat_r2_x;
extern int g_obj_view_mat_r2_y;
extern int g_obj_view_mat_r2_z;
extern int g_object_light_direction_x;
extern int g_object_light_direction_y;
extern int g_object_light_direction_z;
extern int g_fview_move_x_q15;
extern int g_fview_move_y_q15;
extern int g_fview_move_z_q15;

IDirectDraw *renderer_get_direct_draw(void);
void renderer_init_d3d_device(void);

#ifdef __cplusplus
}
#endif

#endif

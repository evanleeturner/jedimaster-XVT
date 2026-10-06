#ifndef XVT_FLIGHT_FLIGHT_VIEW_H
#define XVT_FLIGHT_FLIGHT_VIEW_H

#include <stdint.h>

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The viewport and camera matrix push_flight_viewport saves in
 * g_saved_flight_viewport before it sets a new viewport, and pop_flight_viewport
 * puts back. Those functions use the copy of this struct that flight_sw.c
 * defines for itself, with the same fields; no file that includes this header
 * uses this one. */
struct flight_viewport_save_state {
	uint16_t viewport_x;  /* g_flight_vp_x: the viewport's left column. */
	uint16_t pad02;	      /* Never read or written. */
	uint16_t viewport_y;  /* g_flight_vp_y: the viewport's top row. */
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

extern int g_cam_rel_world_x;
extern int g_cam_rel_world_y;
extern int g_cam_rel_world_z;
extern uint16_t g_flight_initial_texture_cache_flush_pending;

/* Argument of flight_view_scale_q15, which nothing calls. */
struct flight_view_scale {
	int value; /* Number to scale. */
	int scale; /* Q15 factor (32,768 is 1.0). */
};

HRESULT flight_view_composite_masked_software_surface(void);
extern int g_current_object_bounds_extent;
int16_t flight_view_rotate_view_by_input(int pitch_step, int yaw_or_roll_step,
					 int player_idx);
void flight_view_update_player_camera(int player_idx);
void flight_view_render(void);
int flight_view_compute_object_view_position(uint16_t object_idx);
int flight_view_project_and_test_sphere_visible(int object_idx,
						unsigned int sphere_radius);
int flight_view_cull_world_sphere_to_viewport(int world_x, int world_y,
					      int world_z, int sphere_radius);
void flight_view_render_startup_frame(void);
void flight_view_render_frame(void);

#ifdef __cplusplus
}
#endif

#endif

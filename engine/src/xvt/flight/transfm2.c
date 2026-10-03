#include "xvt/flight/transfm2.h"

#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/render/renderer.h"

/* Camera matrix, row 2, Y term. The nine g_camMatR globals rotate a world
 * offset from the camera into view space, in Q15 fixed point (32,768 is
 * 1.0): row 0 gives view X (rightward on screen), row 1 view Y (downward on
 * screen), row 2 depth. Two functions write them, all nine together:
 * fview_build_camera_orient, and pop_flight_viewport, which puts back the copy
 * push_flight_viewport saved. */
// GLOBAL: XVT 0x9FE7F4
int g_cam_mat_r2_y = 0;
/* Camera matrix, row 1, Z term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA00480
int g_cam_mat_r1_z = 0;
/* Camera matrix, row 0, Z term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA00488
int g_cam_mat_r0_z = 0;
/* Camera matrix, row 2, Z term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA00490
int g_cam_mat_r2_z = 0;
/* Camera matrix, row 1, X term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA004A4
int g_cam_mat_r1_x = 0;
/* Camera matrix, row 0, X term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA004A8
int g_cam_mat_r0_x = 0;
/* Camera matrix, row 2, X term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA004B0
int g_cam_mat_r2_x = 0;
/* Camera matrix, row 1, Y term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA004B8
int g_cam_mat_r1_y = 0;
/* Camera matrix, row 0, Y term; see g_cam_mat_r2_y. */
// GLOBAL: XVT 0xA004CC
int g_cam_mat_r0_y = 0;
/* View-space X of the point being drawn or tested: the camera-relative
 * world offset dotted with camera row 0. Many functions write it, chiefly
 * flight_view_compute_object_view_position, the flight map and HUD drawing
 * code, and transfm2_clipobjecteyez. */
// GLOBAL: XVT 0x9A8E2C
int g_view_space_x = 0;
/* View-space Y of the point being drawn or tested, written beside
 * g_view_space_x by the same functions. */
// GLOBAL: XVT 0x9A8E30
int g_view_space_y = 0;
/* Depth of the point being drawn or tested, along camera row 2; 0 or less
 * means at or behind the eye. Many functions write it, chiefly the ones that
 * write g_view_space_x; transfm2_clipobjecteyez sets it to 1 and
 * backdrop_draw_model_tex_quad_at_screen to 0x7FFFFFFF. */
// GLOBAL: XVT 0x9A8E34
int g_view_space_depth = 0;
/* Half of g_proj_scale_int, added to the shifted numerator before the divide
 * in the screen projections. flight_display_configure_resolution_state sets it
 * to 128 at 320x240 and in an unknown mode, 256 at 640x480 and 480x360;
 * model_preview_render_viewport sets MODEL_PREVIEW_PROJECTION_HALF_SCALE. */
// GLOBAL: XVT 0x9A1FF0
int32_t g_proj_scale_half_int = 0;
/* Left shift applied to a view-space coordinate before it is divided by
 * depth, so the projection scale is 2 to this power. The same two
 * functions that write g_proj_scale_half_int write it: 8 at 320x240, 9 at
 * 640x480 and 480x360, MODEL_PREVIEW_PERSPECTIVE_SHIFT in the model
 * preview. */
// GLOBAL: XVT 0x9D8C04
uint8_t g_perspective_shift = 0;
/* Column of the viewport's center, counted from the viewport's left edge:
 * half the viewport width. Four functions write it: set_flight_viewport,
 * push_flight_viewport, pop_flight_viewport and model_preview_render_viewport. */
// GLOBAL: XVT 0xA080F4
uint16_t g_flight_vp_center_x = 0;
/* Vertical scale for transfm2_project_screen_y_fixed_point, as a fraction of
 * 65,536; 0 means none. Both writers, flight_display_configure_resolution_state
 * and model_preview_render_viewport, set it to 0, so the scaling never runs. */
// GLOBAL: XVT 0x9ECC44
uint16_t g_proj_aspect_y = 0;

/* Moves the point in g_view_space_x and g_view_space_y, which lies at or behind
 * the eye, along the segment toward the view-space point (x, y, z) to where
 * that segment crosses depth 0, then sets g_view_space_depth to 1 so the point
 * can be projected. Returns the size of the move made in Y; the callers
 * ignore it. Does not check that g_view_space_depth is 0 or less; the flight
 * map code calls it only then. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x427390
int transfm2_clipobjecteyez(int x, int y, int z)
{
	int negative_depth;
	int interpolation_delta;
	int current_x;
	int current_y;

	negative_depth = (int)(0u - (uint32_t)g_view_space_depth);
	current_x = g_view_space_x;
	if (current_x < x) {
		interpolation_delta = math2_ab_over_c32(
			negative_depth,
			(int)((uint32_t)x - (uint32_t)current_x),
			(int)((uint32_t)z + (uint32_t)negative_depth));
		g_view_space_x = (int)((uint32_t)g_view_space_x +
				       (uint32_t)interpolation_delta);
	} else {
		interpolation_delta = math2_ab_over_c32(
			negative_depth,
			(int)((uint32_t)current_x - (uint32_t)x),
			(int)((uint32_t)z + (uint32_t)negative_depth));
		g_view_space_x = (int)((uint32_t)g_view_space_x -
				       (uint32_t)interpolation_delta);
	}

	current_y = g_view_space_y;
	if (current_y < y) {
		interpolation_delta = math2_ab_over_c32(
			negative_depth,
			(int)((uint32_t)y - (uint32_t)current_y),
			(int)((uint32_t)z + (uint32_t)negative_depth));
		g_view_space_y = (int)((uint32_t)g_view_space_y +
				       (uint32_t)interpolation_delta);
	} else {
		interpolation_delta = math2_ab_over_c32(
			negative_depth,
			(int)((uint32_t)current_y - (uint32_t)y),
			(int)((uint32_t)z + (uint32_t)negative_depth));
		g_view_space_y = (int)((uint32_t)g_view_space_y -
				       (uint32_t)interpolation_delta);
	}

	g_view_space_depth = 1;
	return interpolation_delta;
}

/* Returns (x, y, z) dotted with camera row 0, Q15: view-space X of a
 * camera-relative offset. */
// FUNCTION: XVT 0x427420
int transfm2_cam_mat_dot_row0(int x, int y, int z)
{
	return math_dot3q15(g_cam_mat_r0_x, g_cam_mat_r0_y, g_cam_mat_r0_z, x,
			    y, z);
}

/* Returns (x, y, z) dotted with camera row 1, Q15: view-space Y. */
// FUNCTION: XVT 0x427490
int transfm2_cam_mat_dot_row1(int x, int y, int z)
{
	return math_dot3q15(g_cam_mat_r1_x, g_cam_mat_r1_y, g_cam_mat_r1_z, x,
			    y, z);
}

/* Returns (x, y, z) dotted with camera row 2, Q15: depth. */
// FUNCTION: XVT 0x427500
int transfm2_cam_mat_dot_row2(int x, int y, int z)
{
	return math_dot3q15(g_cam_mat_r2_x, g_cam_mat_r2_y, g_cam_mat_r2_z, x,
			    y, z);
}

/* Returns transfm2_project_screen_x_fixed_point for view_z read as unsigned. */
// FUNCTION: XVT 0x427570
int transfm2_project_screen_x(int view_x, int view_z)
{
	return transfm2_project_screen_x_fixed_point(view_x,
						     (unsigned int)view_z);
}

/* Returns transfm2_project_screen_y_fixed_point for view_z read as unsigned. */
// FUNCTION: XVT 0x427590
int transfm2_project_screen_y(int view_y, int view_z)
{
	return transfm2_project_screen_y_fixed_point(view_y,
						     (unsigned int)view_z);
}

/* Returns the viewport column of a view-space X at a depth:
 * g_flight_vp_center_x plus the size of view_x shifted left by
 * g_perspective_shift, plus g_proj_scale_half_int, divided by depth, with
 * view_x's sign. When the quotient would not fit in 32 bits, a depth of 0
 * included, the offset is 0x7FFFFF00 with that sign. A negative depth is read
 * as a large unsigned one; callers test depth first. */
// FUNCTION: XVT 0x4275B0
int transfm2_project_screen_x_fixed_point(int view_x, unsigned int depth)
{
	if (view_x < 0) {
		uint64_t numerator;

		numerator = (uint64_t)(0u - (uint32_t)view_x) *
				    (1u << (g_perspective_shift & 31)) +
			    (uint32_t)g_proj_scale_half_int;
		if ((numerator & ~(uint64_t)UINT32_MAX) >=
		    ((uint64_t)depth << 32)) {
			view_x = 0x7FFFFF00;
		} else {
			view_x = (int)(uint32_t)(numerator / depth);
		}
		view_x = (int)(0u - (uint32_t)view_x);
	} else {
		uint64_t numerator;

		numerator = (uint64_t)(uint32_t)view_x *
				    (1u << (g_perspective_shift & 31)) +
			    (uint32_t)g_proj_scale_half_int;
		if ((numerator & ~(uint64_t)UINT32_MAX) >=
		    ((uint64_t)depth << 32)) {
			view_x = 0x7FFFFF00;
		} else {
			view_x = (int)(uint32_t)(numerator / depth);
		}
	}

	return (int)((uint32_t)g_flight_vp_center_x + (uint32_t)view_x);
}

/* Returns the viewport row of a view-space Y at a depth, worked as in
 * transfm2_project_screen_x_fixed_point, the offset scaled by g_proj_aspect_y when
 * that is nonzero, plus g_flight_vp_center_y and g_proj_offset_y. */
// FUNCTION: XVT 0x427650
int transfm2_project_screen_y_fixed_point(int view_y, unsigned int depth)
{
	uint64_t numerator;
	uint32_t quotient;
	int projected_offset;

	if (view_y < 0) {
		numerator = (uint64_t)(0u - (uint32_t)view_y)
			    << g_perspective_shift;
		numerator += (uint32_t)g_proj_scale_half_int;
		if (((uint32_t *)&numerator)[1] < depth) {
			quotient = (uint32_t)(numerator / depth);
		} else {
			quotient = 0x7FFFFF00u;
		}
		projected_offset = (int)(0u - quotient);
	} else {
		numerator = (uint64_t)(uint32_t)view_y << g_perspective_shift;
		numerator += (uint32_t)g_proj_scale_half_int;
		if (((uint32_t *)&numerator)[1] < depth) {
			projected_offset = (int)(uint32_t)(numerator / depth);
		} else {
			projected_offset = 0x7FFFFF00;
		}
	}

	if (g_proj_aspect_y != 0) {
		if (projected_offset < 0) {
			projected_offset = -(int)math2_longfraction(
				0u - (uint32_t)projected_offset,
				g_proj_aspect_y);
		} else {
			projected_offset = (int)math2_longfraction(
				(unsigned int)projected_offset,
				g_proj_aspect_y);
		}
	}

	projected_offset =
		(int)((uint32_t)projected_offset + g_flight_vp_center_y);
	projected_offset =
		(int)((uint32_t)projected_offset + (uint32_t)g_proj_offset_y);
	return projected_offset;
}

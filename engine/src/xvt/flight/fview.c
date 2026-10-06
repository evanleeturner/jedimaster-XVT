#include "xvt/flight/fview.h"

#include <stdint.h>

#include "xvt/assets/model_preview.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/render_camera.h"

/* Forward axis of the object last oriented, X term, Q15 (32,768 is 1.0);
 * the negated row 2 of the current object matrix (g_cur_mat_r2_x). The nine
 * g_fview axis globals have two writers, each setting all nine:
 * fview_calcrotateorient, from the matrix it has just turned, and
 * fview_set_object_transform, from an object's cached axes. */
// GLOBAL: XVT 0x9D12E8
int g_fview_forward_x_q15 = 0;
/* Forward axis, Y term; see g_fview_forward_x_q15. */
// GLOBAL: XVT 0x9D1264
int g_fview_forward_y_q15 = 0;
/* Forward axis, Z term; see g_fview_forward_x_q15. */
// GLOBAL: XVT 0x9D1154
int g_fview_forward_z_q15 = 0;
/* Side axis of the object last oriented, X term: row 0 of the current
 * object matrix; see g_fview_forward_x_q15. */
// GLOBAL: XVT 0x9A8D80
int g_fview_side_x_q15 = 0;
/* Side axis, Y term; see g_fview_side_x_q15. */
// GLOBAL: XVT 0x9A8D60
int g_fview_side_y_q15 = 0;
/* Side axis, Z term; see g_fview_side_x_q15. */
// GLOBAL: XVT 0x9A8D8C
int g_fview_side_z_q15 = 0;
/* Up axis of the object last oriented, X term: row 1 of the current object
 * matrix; see g_fview_forward_x_q15. */
// GLOBAL: XVT 0x9A8E20
int g_fview_up_x_q15 = 0;
/* Up axis, Y term; see g_fview_up_x_q15. */
// GLOBAL: XVT 0x9A8E1C
int g_fview_up_y_q15 = 0;
/* Up axis, Z term; see g_fview_up_x_q15. */
// GLOBAL: XVT 0x9A8E28
int g_fview_up_z_q15 = 0;

/* Builds the camera matrix g_cam_mat_r0_x to g_cam_mat_r2_z from view angles
 * (a full circle is 65,536): fview_calcrotatemove for view_pitch and view_yaw,
 * fview_calcrotateorient for view_up_axis_angle and view_roll, rows 1 and 2
 * negated, then a turn by hud_aim_x about the side axis and by hud_aim_y about
 * row 1 as it stood before that turn. Those calls also write g_cur_mat_r0_x to
 * g_cur_mat_r2_z, the g_fviewMove globals and the g_fview axis globals, and,
 * when obj_record is not NULL, store that move vector and those axes, as they
 * were before the negation, in obj_record's mobj. The modern build also calls
 * xvt_render_camera_build with the same angles. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x427940
void fview_build_camera_orient(int16_t view_roll, int16_t view_pitch,
			       int16_t view_yaw, int16_t view_up_axis_angle,
			       int16_t hud_aim_x, int16_t hud_aim_y,
			       const struct object_record *obj_record)
{
	fview_calcrotatemove(view_pitch, view_yaw, obj_record);
	fview_calcrotateorient(view_roll, view_up_axis_angle, obj_record);

	g_cur_mat_r2_x = -g_cur_mat_r2_x;
	g_cur_mat_r2_y = -g_cur_mat_r2_y;
	g_cur_mat_r2_z = -g_cur_mat_r2_z;
	g_cur_mat_r1_x = -g_cur_mat_r1_x;
	/* Build the camera basis from the current orientation and HUD aim offsets. */
	int axis_x = g_cur_mat_r1_x;
	g_cur_mat_r1_y = -g_cur_mat_r1_y;
	int axis_y = g_cur_mat_r1_y;
	g_cur_mat_r1_z = -g_cur_mat_r1_z;
	int axis_z = g_cur_mat_r1_z;

	fview_transformaxes(g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z,
			    hud_aim_x);
	fview_transformaxes(axis_x, axis_y, axis_z, hud_aim_y);

	g_cam_mat_r0_x = g_cur_mat_r0_x;
	g_cam_mat_r0_y = g_cur_mat_r0_y;
	g_cam_mat_r0_z = g_cur_mat_r0_z;
	g_cam_mat_r1_x = g_cur_mat_r1_x;
	g_cam_mat_r1_y = g_cur_mat_r1_y;
	g_cam_mat_r1_z = g_cur_mat_r1_z;
	g_cam_mat_r2_x = g_cur_mat_r2_x;
	g_cam_mat_r2_y = g_cur_mat_r2_y;
	g_cam_mat_r2_z = g_cur_mat_r2_z;
	xvt_render_camera_build(view_roll, view_pitch, view_yaw,
				view_up_axis_angle, hud_aim_x, hud_aim_y);
}

/* Sets the current object matrix (g_cur_mat_r0_x to g_cur_mat_r2_z) and the
 * g_fview axis globals for an object, then builds its object-to-view matrix
 * with fview_compute_object_view_matrix and returns what that returns. With
 * obj_record NULL, or its mobj's orient_matrix_dirty set, it works from the
 * angles through fview_calcrotatemove and fview_calcrotateorient, which
 * refresh a record's cached axes; otherwise it takes the cached axes from
 * the record's mobj. Does not check that mobj is set. */
// FUNCTION: XVT 0x427A60
int fview_set_object_transform(int16_t roll, int16_t pitch, int16_t yaw,
			       int16_t up_axis_angle,
			       const struct object_record *obj_record)
{
	if (obj_record == NULL) {
		fview_calcrotatemove(pitch, yaw, obj_record);
		fview_calcrotateorient(roll, up_axis_angle, obj_record);
		return fview_compute_object_view_matrix();
	}

	if (obj_record->mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(pitch, yaw, obj_record);
		fview_calcrotateorient(roll, up_axis_angle, obj_record);
		return fview_compute_object_view_matrix();
	}

	g_fview_forward_x_q15 = obj_record->mobj->cached_fwd_x;
	g_fview_forward_y_q15 = obj_record->mobj->cached_fwd_y;
	g_fview_forward_z_q15 = obj_record->mobj->cached_fwd_z;
	g_fview_side_x_q15 = obj_record->mobj->cached_side_x;
	g_fview_side_y_q15 = obj_record->mobj->cached_side_y;
	g_fview_side_z_q15 = obj_record->mobj->cached_side_z;
	g_fview_up_x_q15 = obj_record->mobj->cached_up_x;
	g_fview_up_y_q15 = obj_record->mobj->cached_up_y;
	g_fview_up_z_q15 = obj_record->mobj->cached_up_z;

	g_cur_mat_r2_x = -g_fview_forward_x_q15;
	g_cur_mat_r2_y = -g_fview_forward_y_q15;
	g_cur_mat_r2_z = -g_fview_forward_z_q15;
	g_cur_mat_r0_x = g_fview_side_x_q15;
	g_cur_mat_r0_y = g_fview_side_y_q15;
	g_cur_mat_r0_z = g_fview_side_z_q15;
	g_cur_mat_r1_x = g_fview_up_x_q15;
	g_cur_mat_r1_y = g_fview_up_y_q15;
	g_cur_mat_r1_z = g_fview_up_z_q15;

	return fview_compute_object_view_matrix();
}

/* Starts the current object matrix from a pitch and a yaw (a full circle is
 * 65,536), with no roll: writes all nine g_cur_mat_r0_x to g_cur_mat_r2_z, and
 * g_fview_move_x_q15, g_fview_move_y_q15 and g_fview_move_z_q15, the forward
 * direction (negated row 2), Q15. When obj_record is not NULL it also stores
 * that direction as its mobj's move_x, move_y and move_z and clears
 * move_vector_dirty. */
// FUNCTION: XVT 0x427BD0
void fview_calcrotatemove(int16_t pitch, int16_t yaw,
			  const struct object_record *obj_record)
{
	int16_t cos_neg_b = trig2_getsignedcos(-yaw);
	int16_t cos_c000_minus_a =
		trig2_getsignedcos((int16_t)(0xc000 - pitch));
	int16_t sin_neg_b = trig2_getsignedsin(-yaw);
	int16_t sin_c000_minus_a =
		trig2_getsignedsin((int16_t)(0xc000 - pitch));

	g_cur_mat_r0_x = cos_neg_b;
	g_cur_mat_r0_y = sin_neg_b;
	g_cur_mat_r0_z = 0;
	g_cur_mat_r2_x = math_mul_q15(-sin_neg_b, cos_c000_minus_a);
	g_cur_mat_r2_y = math_mul_q15(cos_neg_b, cos_c000_minus_a);
	g_cur_mat_r2_z = sin_c000_minus_a;
	g_cur_mat_r1_x = -math_mul_q15(sin_neg_b, sin_c000_minus_a);
	g_cur_mat_r1_z = -cos_c000_minus_a;
	g_cur_mat_r1_y = -math_mul_q15(-cos_neg_b, sin_c000_minus_a);

	g_fview_move_x_q15 = -g_cur_mat_r2_x;
	g_fview_move_z_q15 = -g_cur_mat_r2_z;
	g_fview_move_y_q15 = -g_cur_mat_r2_y;
	if (obj_record != NULL) {
		obj_record->mobj->move_x = (int16_t)g_fview_move_x_q15;
		obj_record->mobj->move_y = (int16_t)g_fview_move_y_q15;
		obj_record->mobj->move_z = (int16_t)g_fview_move_z_q15;
		obj_record->mobj->move_vector_dirty = 0;
	}
}

/* Finishes the current object matrix begun by fview_calcrotatemove: turns it
 * by up_axis_angle about its row 1, then by roll about its row 2
 * (fview_transformaxes), and copies the axes into the g_fview axis globals.
 * When obj_record is not NULL it also caches them in its mobj (cached_fwd_x to
 * cached_up_z) and clears orient_matrix_dirty. */
// FUNCTION: XVT 0x427D30
void fview_calcrotateorient(int16_t roll, int16_t up_axis_angle,
			    const struct object_record *obj_record)
{
	fview_transformaxes(g_cur_mat_r1_x, g_cur_mat_r1_y, g_cur_mat_r1_z,
			    up_axis_angle);
	fview_transformaxes(g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z,
			    roll);

	g_fview_forward_x_q15 = -g_cur_mat_r2_x;
	g_fview_forward_y_q15 = -g_cur_mat_r2_y;
	g_fview_side_x_q15 = g_cur_mat_r0_x;
	g_fview_side_y_q15 = g_cur_mat_r0_y;
	g_fview_up_x_q15 = g_cur_mat_r1_x;
	g_fview_forward_z_q15 = -g_cur_mat_r2_z;
	g_fview_side_z_q15 = g_cur_mat_r0_z;
	g_fview_up_y_q15 = g_cur_mat_r1_y;
	g_fview_up_z_q15 = g_cur_mat_r1_z;

	if (obj_record != NULL) {
		obj_record->mobj->cached_fwd_x = (int16_t)g_fview_forward_x_q15;
		obj_record->mobj->cached_fwd_y = (int16_t)g_fview_forward_y_q15;
		obj_record->mobj->cached_fwd_z = (int16_t)g_fview_forward_z_q15;
		obj_record->mobj->cached_side_x = (int16_t)g_fview_side_x_q15;
		obj_record->mobj->cached_side_y = (int16_t)g_fview_side_y_q15;
		obj_record->mobj->cached_side_z = (int16_t)g_fview_side_z_q15;
		obj_record->mobj->cached_up_x = (int16_t)g_fview_up_x_q15;
		obj_record->mobj->cached_up_y = (int16_t)g_fview_up_y_q15;
		obj_record->mobj->cached_up_z = (int16_t)g_fview_up_z_q15;
		obj_record->mobj->orient_matrix_dirty = 0;
	}
}

/* Builds g_obj_view_mat_r0_x to g_obj_view_mat_r2_z, the rotation from the
 * current object matrix into view space: each entry is one row of the
 * object matrix (rows 0, 2 and 1, in that order) dotted with one row of the
 * camera matrix, by math_dot3q15_wrapped. With
 * g_transform_light_direction_to_object_space set it also turns the world light
 * direction into object space the same way, into g_object_light_direction_x to
 * Z, and returns the Z term; otherwise it copies the world direction
 * unchanged and returns g_obj_view_mat_r2_z. */
// FUNCTION: XVT 0x427E90
int fview_compute_object_view_matrix(void)
{
	g_obj_view_mat_r0_x = math_dot3q15_wrapped(
		g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z, g_cam_mat_r0_x,
		g_cam_mat_r0_y, g_cam_mat_r0_z);
	g_obj_view_mat_r0_y = math_dot3q15_wrapped(
		g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z, g_cam_mat_r1_x,
		g_cam_mat_r1_y, g_cam_mat_r1_z);
	g_obj_view_mat_r0_z = math_dot3q15_wrapped(
		g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z, g_cam_mat_r2_x,
		g_cam_mat_r2_y, g_cam_mat_r2_z);
	g_obj_view_mat_r1_x = math_dot3q15_wrapped(
		g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z, g_cam_mat_r0_x,
		g_cam_mat_r0_y, g_cam_mat_r0_z);
	g_obj_view_mat_r1_y = math_dot3q15_wrapped(
		g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z, g_cam_mat_r1_x,
		g_cam_mat_r1_y, g_cam_mat_r1_z);
	g_obj_view_mat_r1_z = math_dot3q15_wrapped(
		g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z, g_cam_mat_r2_x,
		g_cam_mat_r2_y, g_cam_mat_r2_z);
	g_obj_view_mat_r2_x = math_dot3q15_wrapped(
		g_cur_mat_r1_x, g_cur_mat_r1_y, g_cur_mat_r1_z, g_cam_mat_r0_x,
		g_cam_mat_r0_y, g_cam_mat_r0_z);
	g_obj_view_mat_r2_y = math_dot3q15_wrapped(
		g_cur_mat_r1_x, g_cur_mat_r1_y, g_cur_mat_r1_z, g_cam_mat_r1_x,
		g_cam_mat_r1_y, g_cam_mat_r1_z);
	int result = math_dot3q15_wrapped(g_cur_mat_r1_x, g_cur_mat_r1_y,
					  g_cur_mat_r1_z, g_cam_mat_r2_x,
					  g_cam_mat_r2_y, g_cam_mat_r2_z);
	g_obj_view_mat_r2_z = result;
	if (g_transform_light_direction_to_object_space != 0) {
		g_object_light_direction_x = math_dot3q15_wrapped(
			g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z,
			g_world_light_direction_x, g_world_light_direction_y,
			g_world_light_direction_z);
		g_object_light_direction_y = math_dot3q15_wrapped(
			g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z,
			g_world_light_direction_x, g_world_light_direction_y,
			g_world_light_direction_z);
		result = math_dot3q15_wrapped(
			g_cur_mat_r1_x, g_cur_mat_r1_y, g_cur_mat_r1_z,
			g_world_light_direction_x, g_world_light_direction_y,
			g_world_light_direction_z);
		g_object_light_direction_z = result;
	} else {
		g_object_light_direction_x = g_world_light_direction_x;
		g_object_light_direction_y = g_world_light_direction_y;
		g_object_light_direction_z = g_world_light_direction_z;
	}
	return result;
}

/* Turns all three rows of the current object matrix (g_cur_mat_r0_x to
 * g_cur_mat_r2_z) by angle_q16 (a full circle is 65,536) about the axis
 * (axis_x_q15, axis_y_q15, axis_z_q15), building the rotation from the axis
 * and the angle's cosine and sine. Does nothing when the angle is 0. Does
 * not check that the axis has length 1. */
// FUNCTION: XVT 0x4290B0
void fview_transformaxes(int axis_x_q15, int axis_y_q15, int axis_z_q15,
			 int16_t angle_q16)
{
	enum { Q15_ONE = 0x7FFF };

	if (angle_q16 == 0) {
		return;
	}

	int16_t cosine = trig2_getsignedcos(angle_q16);
	int16_t sine = trig2_getsignedsin(angle_q16);
	int coefficient00;
	int coefficient01;
	int coefficient02;
	int coefficient10;
	int coefficient11;
	int coefficient12;
	int coefficient20;
	int coefficient21;
	int coefficient22;
	if (cosine >= 0) {
		const int cosine_complement = Q15_ONE - cosine;

		coefficient00 = math_rodrigues_term_nonnegative_cos(
			axis_x_q15, axis_x_q15, cosine_complement, cosine);
		coefficient01 = math_rodrigues_term_nonnegative_cos(
			axis_x_q15, axis_y_q15, cosine_complement,
			math_mul_q15(sine, axis_z_q15));
		coefficient02 = math_rodrigues_term_nonnegative_cos(
			axis_x_q15, axis_z_q15, cosine_complement,
			-math_mul_q15(sine, axis_y_q15));
		coefficient10 = math_rodrigues_term_nonnegative_cos(
			axis_x_q15, axis_y_q15, cosine_complement,
			-math_mul_q15(sine, axis_z_q15));
		coefficient11 = math_rodrigues_term_nonnegative_cos(
			axis_y_q15, axis_y_q15, cosine_complement, cosine);
		coefficient12 = math_rodrigues_term_nonnegative_cos(
			axis_y_q15, axis_z_q15, cosine_complement,
			math_mul_q15(sine, axis_x_q15));
		coefficient20 = math_rodrigues_term_nonnegative_cos(
			axis_x_q15, axis_z_q15, cosine_complement,
			math_mul_q15(sine, axis_y_q15));
		coefficient21 = math_rodrigues_term_nonnegative_cos(
			axis_y_q15, axis_z_q15, cosine_complement,
			-math_mul_q15(sine, axis_x_q15));
		coefficient22 = math_rodrigues_term_nonnegative_cos(
			axis_z_q15, axis_z_q15, cosine_complement, cosine);
	} else {
		const int cosine_magnitude = -cosine;

		coefficient00 = math_rodrigues_term_negative_cos(
			axis_x_q15, axis_x_q15, cosine_magnitude, cosine);
		coefficient01 = math_rodrigues_term_negative_cos(
			axis_x_q15, axis_y_q15, cosine_magnitude,
			math_mul_q15(sine, axis_z_q15));
		coefficient02 = math_rodrigues_term_negative_cos(
			axis_x_q15, axis_z_q15, cosine_magnitude,
			-math_mul_q15(sine, axis_y_q15));
		coefficient10 = math_rodrigues_term_negative_cos(
			axis_x_q15, axis_y_q15, cosine_magnitude,
			-math_mul_q15(sine, axis_z_q15));
		coefficient11 = math_rodrigues_term_negative_cos(
			axis_y_q15, axis_y_q15, cosine_magnitude, cosine);
		coefficient12 = math_rodrigues_term_negative_cos(
			axis_y_q15, axis_z_q15, cosine_magnitude,
			math_mul_q15(sine, axis_x_q15));
		coefficient20 = math_rodrigues_term_negative_cos(
			axis_x_q15, axis_z_q15, cosine_magnitude,
			math_mul_q15(sine, axis_y_q15));
		coefficient21 = math_rodrigues_term_negative_cos(
			axis_y_q15, axis_z_q15, cosine_magnitude,
			-math_mul_q15(sine, axis_x_q15));
		coefficient22 = math_rodrigues_term_negative_cos(
			axis_z_q15, axis_z_q15, cosine_magnitude, cosine);
	}

	int new_x = math_dot3q15_wrapped(g_cur_mat_r0_x, g_cur_mat_r0_y,
					 g_cur_mat_r0_z, coefficient00,
					 coefficient10, coefficient20);
	int new_y = math_dot3q15_wrapped(g_cur_mat_r0_x, g_cur_mat_r0_y,
					 g_cur_mat_r0_z, coefficient01,
					 coefficient11, coefficient21);
	int new_z = math_dot3q15_wrapped(g_cur_mat_r0_x, g_cur_mat_r0_y,
					 g_cur_mat_r0_z, coefficient02,
					 coefficient12, coefficient22);
	g_cur_mat_r0_x = new_x;
	g_cur_mat_r0_y = new_y;
	g_cur_mat_r0_z = new_z;

	new_x = math_dot3q15_wrapped(g_cur_mat_r1_x, g_cur_mat_r1_y,
				     g_cur_mat_r1_z, coefficient00,
				     coefficient10, coefficient20);
	new_y = math_dot3q15_wrapped(g_cur_mat_r1_x, g_cur_mat_r1_y,
				     g_cur_mat_r1_z, coefficient01,
				     coefficient11, coefficient21);
	new_z = math_dot3q15_wrapped(g_cur_mat_r1_x, g_cur_mat_r1_y,
				     g_cur_mat_r1_z, coefficient02,
				     coefficient12, coefficient22);
	g_cur_mat_r1_x = new_x;
	g_cur_mat_r1_y = new_y;
	g_cur_mat_r1_z = new_z;

	new_x = math_dot3q15_wrapped(g_cur_mat_r2_x, g_cur_mat_r2_y,
				     g_cur_mat_r2_z, coefficient00,
				     coefficient10, coefficient20);
	new_y = math_dot3q15_wrapped(g_cur_mat_r2_x, g_cur_mat_r2_y,
				     g_cur_mat_r2_z, coefficient01,
				     coefficient11, coefficient21);
	new_z = math_dot3q15_wrapped(g_cur_mat_r2_x, g_cur_mat_r2_y,
				     g_cur_mat_r2_z, coefficient02,
				     coefficient12, coefficient22);
	g_cur_mat_r2_x = new_x;
	g_cur_mat_r2_y = new_y;
	g_cur_mat_r2_z = new_z;
}

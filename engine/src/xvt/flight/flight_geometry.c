/* Shared flight geometry: the direction and the rough distance between two
 * objects or mission points, and an object's local axes turned into world
 * axes. Flight, the HUD, targeting, lasers, collisions, missions and the
 * computer pilots all call these, so they live apart from the pilots' code. */
#include "xvt/flight/flight_geometry.h"

#include "xvt/flight/fview.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"

/* The last rough distance a range test worked out, in world units; callers read
 * it right after the call that sets it. pai_object_ref_update_rough_distance and
 * pai_is_object_within_range_of_craft take the larger of the X and Y offsets plus
 * half the smaller, then add half the Z offset when that sum is the larger,
 * else halve the sum and add the Z offset. Many functions write it, chiefly
 * those two. */
// GLOBAL: XVT 0x9A1FF8
int g_last_rough_distance = 0;

/* Works out the direction and distance from one object or mission point to
 * another with trig2_ctop, which sets trig2_xyangle, trig2_pitch and
 * trig2_polardistance. A mission point reference is read from flight group 0's
 * points. Leaves g_world_loc_x, g_world_loc_y and g_world_loc_z at from_ref. */
// FUNCTION: XVT 0x4034E0
void pai_object_ref_direction_to_object_ref(unsigned int from_ref,
					    unsigned int to_ref)
{
	mission_resolve_object_or_mission_point_world_loc(to_ref, 0);
	int target_x = g_world_loc_x;
	int target_y = g_world_loc_y;
	int target_z = g_world_loc_z;

	mission_resolve_object_or_mission_point_world_loc(from_ref, 0);
	target_x = target_x - g_world_loc_x;
	target_y = target_y - g_world_loc_y;
	target_z = target_z - g_world_loc_z;
	trig2_ctop(target_x, target_y, target_z);
}

/* Sets g_last_rough_distance to the rough distance, in world units, between two
 * objects or mission points. A mission point reference is read from flight
 * group 0's points. Leaves g_world_loc_x, g_world_loc_y and g_world_loc_z at
 * to_ref. */
// FUNCTION: XVT 0x403540
void pai_object_ref_update_rough_distance(unsigned int from_ref,
					  unsigned int to_ref)
{
	mission_resolve_object_or_mission_point_world_loc(from_ref, 0);
	int delta_x = g_world_loc_x;
	int delta_y = g_world_loc_y;
	int delta_z = g_world_loc_z;

	mission_resolve_object_or_mission_point_world_loc(to_ref, 0);
	delta_x -= g_world_loc_x;
	delta_y -= g_world_loc_y;
	delta_z -= g_world_loc_z;

	if (delta_x < 0) {
		delta_x = -delta_x;
	}
	if (delta_y < 0) {
		delta_y = -delta_y;
	}
	if (delta_z < 0) {
		delta_z = -delta_z;
	}

	int xy_score;
	if (delta_x > delta_y) {
		xy_score = delta_x + (delta_y >> 1);
	} else {
		xy_score = delta_y + (delta_x >> 1);
	}

	if (xy_score > delta_z) {
		g_last_rough_distance = xy_score + (delta_z >> 1);
	} else {
		g_last_rough_distance = xy_score;
		g_last_rough_distance >>= 1;
		g_last_rough_distance += delta_z;
	}
}

/* Turns a vector given along an object's side, up and forward axes into world
 * axes, in g_rotated_x, g_rotated_y and g_rotated_z. The axes are 1.15 fixed
 * point, so the result keeps the input's unit. When the object's
 * orient_matrix_dirty is set it first rebuilds the object's cached move vector
 * and axes with fview_calcrotatemove and fview_calcrotateorient, which also set
 * the shared matrix globals those two write. */
// FUNCTION: XVT 0x4035D0
void pai_calcrotatedpoint(const struct object_record *obj, int16_t side_arg,
			  int16_t up_arg, int16_t fwd_arg)
{
	/* Rotate local coordinates through the cached Q15 orientation basis. */
	if (obj->mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(obj->pitch, obj->yaw, obj);
		fview_calcrotateorient(obj->roll, 0, obj);
	}

	g_rotated_x = math_mul_q15(side_arg, obj->mobj->cached_side_x);
	int result = math_mul_q15(up_arg, obj->mobj->cached_up_x);
	g_rotated_x += result;
	g_rotated_x += math_mul_q15(fwd_arg, obj->mobj->cached_fwd_x);
	g_rotated_y = math_mul_q15(side_arg, obj->mobj->cached_side_y);
	g_rotated_y += math_mul_q15(up_arg, obj->mobj->cached_up_y);
	g_rotated_y += math_mul_q15(fwd_arg, obj->mobj->cached_fwd_y);
	g_rotated_z = math_mul_q15(side_arg, obj->mobj->cached_side_z);
	g_rotated_z += math_mul_q15(up_arg, obj->mobj->cached_up_z);
	g_rotated_z += math_mul_q15(fwd_arg, obj->mobj->cached_fwd_z);
}

/* Does the same as pai_calcrotatedpoint, with the vector passed as int. */
// FUNCTION: XVT 0x4037B0
void pai_rotate_local_vector_to_world_scratch(
	const struct object_record *obj_record, int local_side, int local_up,
	int local_fwd)
{
	if (obj_record->mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(obj_record->pitch, obj_record->yaw,
				     obj_record);
		fview_calcrotateorient(obj_record->roll, 0, obj_record);
	}

	g_rotated_x = math_mul_q15(local_side, obj_record->mobj->cached_side_x);
	int result = math_mul_q15(local_up, obj_record->mobj->cached_up_x);
	g_rotated_x += result;
	g_rotated_x += math_mul_q15(local_fwd, obj_record->mobj->cached_fwd_x);
	g_rotated_y = math_mul_q15(local_side, obj_record->mobj->cached_side_y);
	g_rotated_y += math_mul_q15(local_up, obj_record->mobj->cached_up_y);
	g_rotated_y += math_mul_q15(local_fwd, obj_record->mobj->cached_fwd_y);
	g_rotated_z = math_mul_q15(local_side, obj_record->mobj->cached_side_z);
	g_rotated_z += math_mul_q15(local_up, obj_record->mobj->cached_up_z);
	g_rotated_z += math_mul_q15(local_fwd, obj_record->mobj->cached_fwd_z);
}

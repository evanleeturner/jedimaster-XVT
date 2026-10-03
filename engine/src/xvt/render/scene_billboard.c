#include "xvt/render/scene_billboard.h"

#include "xvt/assets/object_type.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/render_quad.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"

enum {
	COMPONENT_OBJECT_TYPE = 89,
	BILLBOARD_MODEL_FRAME_LIMIT = 0x8000,
	BILLBOARD_INVALID_FRAME_START = 0xFF00,
	BILLBOARD_SCREEN_COORD_HIGH_MASK = -65536,
	BILLBOARD_DEFAULT_SCREEN_SIZE = 256,
	BILLBOARD_EFFECT_SIZE_SHIFT = 6,
	BILLBOARD_ALIGNMENT_QUARTER_TURN = 0x4000,
};

/* Model node last chosen for a billboard object: the frame
 * scene_billboard_draw_or_queue_object draws as a node, or the mesh
 * damage_queue_craft_billboards_for_object_type or proving_grounds_draw_course_object
 * is on. Only proving_grounds_draw_course_object reads it. */
// GLOBAL: XVT 0x9A1FE6
uint16_t g_billboard_model_node_switch_index = 0;
/* Selection marking for the meshes damage_queue_craft_billboards_for_object_type
 * walks: 1 for the whole object while it is the local beam target, 2 on the
 * mesh matching g_render_target_component_idx, 0 on the others;
 * proving_grounds_draw_course_object sets 1. Only
 * damage_queue_craft_billboards_for_object_type reads it. */
// GLOBAL: XVT 0x9A20A6
uint16_t g_billboard_target_selection_state = 0;
/* Billboards waiting in g_scene_billboard_queue, 0 to 32.
 * scene_billboard_queue_projected_textured adds one;
 * scene_billboard_render_queued_textured counts it down and leaves -1, and its
 * callers and the frame and map setup set it back to 0. */
// GLOBAL: XVT 0x9A8062
int16_t g_scene_billboard_queue_count = 0;
/* Object index (or, in a few callers, a type or marker value) of the object
 * being drawn, which the billboard and model drawing code reads; many functions
 * write it, chiefly scene_billboard_draw_or_queue_object,
 * render_quad_draw_model_texture, damage_queue_craft_billboards_for_object_type and
 * render_non_craft_scene_object. */
// GLOBAL: XVT 0x9ED664
uint16_t g_billboard_object_or_type_index = 0;

/* Textured billboards waiting to be drawn, 32 entries, filled by
 * scene_billboard_queue_projected_textured and drawn by
 * scene_billboard_render_queued_textured. */
// GLOBAL: XVT 0x9ECA30
static struct scene_billboard_queue_entry g_scene_billboard_queue[32] = {{0}};

/* Draws an object through its type's frame sequence: model frames at once,
 * texture frames as queued billboards. Uses the object-to-view matrix and the
 * g_viewSpace position the caller has set up. Sets g_billboard_object_or_type_index
 * and g_billboard_texture_frame_sequence; the frame is type_specific_byte[0] >> 1
 * for object type 89 (COMPONENT_OBJECT_TYPE), else the sequence entry at
 * type_specific_byte[0], stored in g_billboard_texture_sequence_index, and it
 * returns when the type has no sequence. Frames from 0xFF00 up draw nothing. A
 * frame under 0x8000 is a model node: it sets g_billboard_model_node_switch_index
 * and draws it with render_scene_draw_selected_root_node, and stops there, except
 * for a type 89 object whose mobj's source_object_type is also 89, which then
 * takes a texture frame from g_object_type132_texture_frame_sequence at
 * type_specific_byte[1]. A texture frame (0x8000 to 0xFEFF) with view depth not
 * negative is queued at the projected point, returning when either coordinate
 * falls outside -65536 to 65535, with Y measured up from the viewport's bottom,
 * a size of effect_size << 6 (plus 256 when that is 256 or more, or 256 for no
 * effect_size), and the object's on-screen roll from row 0 or 1 of the
 * object-to-view matrix, whichever has the smaller Z term in size (row 1 on a
 * tie). Does not check that the object has a mobj. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x401000
void scene_billboard_draw_or_queue_object(int object_index)
{
	struct object_record *object;
	uint16_t source_object_type;
	uint16_t frame;
	int abs_r0z;
	int abs_r1z;
	int axis_x;
	int axis_y;
	uint16_t rotation_angle;
	int projected_x;
	int projected_x_high;
	int projected_y;
	int projected_y_high;
	int screen_y;
	uint16_t screen_size;

	object = &g_object_table[object_index];
	source_object_type = object->object_type;
	g_billboard_object_or_type_index = object_index;
	g_billboard_texture_frame_sequence =
		g_object_type_table[source_object_type].texture_frame_sequence;
	if (source_object_type == COMPONENT_OBJECT_TYPE) {
		frame = object->type_specific_byte[0] >> 1;
	} else {
		if (g_billboard_texture_frame_sequence == NULL) {
			return;
		}
		g_billboard_texture_sequence_index =
			object->type_specific_byte[0];
		frame = g_billboard_texture_frame_sequence
			[g_billboard_texture_sequence_index];
	}

	if (frame >= BILLBOARD_INVALID_FRAME_START) {
		return;
	}
	if (frame < BILLBOARD_MODEL_FRAME_LIMIT) {
		if (source_object_type == COMPONENT_OBJECT_TYPE) {
			/* From here source_object_type holds mobj->source_object_type, not the object's own type. */
			source_object_type = object->mobj->source_object_type;
		}
		g_billboard_model_node_switch_index = frame;
		render_scene_draw_selected_root_node(object, frame);
		if (source_object_type == COMPONENT_OBJECT_TYPE) {
			g_billboard_texture_sequence_index =
				g_object_table[object_index]
					.type_specific_byte[1];
			frame = g_object_type132_texture_frame_sequence
				[g_billboard_texture_sequence_index];
		}
	}

	if (frame >= BILLBOARD_INVALID_FRAME_START ||
	    frame < BILLBOARD_MODEL_FRAME_LIMIT || g_view_space_depth < 0) {
		return;
	}
	abs_r0z = g_obj_view_mat_r0_z;
	abs_r1z = g_obj_view_mat_r1_z;
	if (abs_r0z < 0) {
		abs_r0z = -abs_r0z;
	}
	if (abs_r1z < 0) {
		abs_r1z = -abs_r1z;
	}
	if (abs_r1z > abs_r0z) {
		axis_x = g_obj_view_mat_r0_x;
		axis_y = g_obj_view_mat_r0_y;
	} else {
		axis_x = g_obj_view_mat_r1_x;
		axis_y = g_obj_view_mat_r1_y;
	}
	if (axis_x < 0) {
		rotation_angle = (uint16_t)trig2_arctan(axis_y, -axis_x);
	} else {
		rotation_angle = (uint16_t)-trig2_arctan(axis_y, axis_x);
	}

	projected_x =
		transfm2_project_screen_x(g_view_space_x, g_view_space_depth);
	projected_x_high = projected_x & BILLBOARD_SCREEN_COORD_HIGH_MASK;
	if (projected_x_high > 0 ||
	    projected_x_high < BILLBOARD_SCREEN_COORD_HIGH_MASK) {
		return;
	}
	projected_y =
		transfm2_project_screen_y(g_view_space_y, g_view_space_depth);
	projected_y_high = projected_y & BILLBOARD_SCREEN_COORD_HIGH_MASK;
	if (projected_y_high > 0 ||
	    projected_y_high < BILLBOARD_SCREEN_COORD_HIGH_MASK) {
		return;
	}

	screen_y = g_flight_vp_height - projected_y;
	screen_size = g_object_table[object_index].mobj->effect_size;
	if (screen_size != 0) {
		screen_size =
			(uint16_t)(screen_size << BILLBOARD_EFFECT_SIZE_SHIFT);
		if (screen_size >= BILLBOARD_DEFAULT_SCREEN_SIZE) {
			screen_size = (uint16_t)(screen_size +
						 BILLBOARD_DEFAULT_SCREEN_SIZE);
		}
	} else {
		screen_size = BILLBOARD_DEFAULT_SCREEN_SIZE;
	}
	scene_billboard_queue_projected_textured(
		g_billboard_object_or_type_index, frame, screen_size,
		(int16_t)projected_x, (int16_t)screen_y, g_view_space_depth,
		rotation_angle);
}

/* Adds a billboard to g_scene_billboard_queue and raises
 * g_scene_billboard_queue_count; does nothing when 32 wait. */
// FUNCTION: XVT 0x401250
void scene_billboard_queue_projected_textured(int object_or_type_index,
					      int frame, int screen_size,
					      int screen_x, int screen_y,
					      int depth_z, int rotation_angle)
{
	int16_t count;

	count = g_scene_billboard_queue_count;
	if (count < 32) {
		g_scene_billboard_queue[count].object_or_type_index =
			object_or_type_index;
		g_scene_billboard_queue[count].frame = frame;
		g_scene_billboard_queue[count].screen_size = screen_size;
		g_scene_billboard_queue[count].screen_x = screen_x;
		g_scene_billboard_queue[count].screen_y = screen_y;
		g_scene_billboard_queue[count].depth_z = depth_z;
		g_scene_billboard_queue[count].rotation_angle = rotation_angle;
		g_scene_billboard_queue_count = (int16_t)(count + 1);
	}
}

/* Draws the queued billboards with render_quad_draw_model_texture, farthest
 * (largest depth_z) first, sorting by bubble passes as it goes, and leaves
 * g_scene_billboard_queue_count at -1. With draw_target_markers nonzero and a local
 * target, it then boxes the target with targeting_draw_object_box in color 59,
 * around the selected component for a starship or platform. */
// FUNCTION: XVT 0x4012C0
void scene_billboard_render_queued_textured(int16_t draw_target_markers)
{
	enum { TARGET_BOX_COLOR = 59 };

	int16_t queued_count;
	int16_t swapped;
	uint16_t queue_index;
	uint16_t current_target_object_idx;

	queued_count = g_scene_billboard_queue_count;
	--g_scene_billboard_queue_count;
	swapped = 1;
	if (queued_count != 0) {
		do {
			if (swapped != 0) {
				int count;

				swapped = 0;
				queue_index = 0;
				if (g_scene_billboard_queue_count > 0) {
					count = g_scene_billboard_queue_count;
					do {
						if (g_scene_billboard_queue
							    [queue_index + 1]
								    .depth_z <
						    g_scene_billboard_queue
							    [queue_index]
								    .depth_z) {
							struct scene_billboard_queue_entry
								temporary;

							temporary = g_scene_billboard_queue
								[queue_index];
							g_scene_billboard_queue
								[queue_index] = g_scene_billboard_queue
									[queue_index +
									 1];
							g_scene_billboard_queue
								[queue_index +
								 1] = temporary;
							swapped = 1;
						}
						++queue_index;
					} while (queue_index < count);
				}
			}

			render_quad_draw_model_texture(
				&g_scene_billboard_queue
					[g_scene_billboard_queue_count]);
			queued_count = g_scene_billboard_queue_count;
			--g_scene_billboard_queue_count;
		} while (queued_count != 0);
	}

	if (draw_target_markers == 0) {
		return;
	}
	current_target_object_idx =
		(uint16_t)g_players[g_local_player].current_target_object_idx;
	if (current_target_object_idx == UINT16_MAX) {
		return;
	}
	if (g_object_table[current_target_object_idx].genus_id ==
		    CRAFT_GENUS_STARSHIP ||
	    g_object_table[current_target_object_idx].genus_id ==
		    CRAFT_GENUS_PLATFORM) {
		targeting_draw_object_box(current_target_object_idx,
					  (uint16_t)g_players[g_local_player]
						  .selected_target_component,
					  TARGET_BOX_COLOR);
	} else {
		targeting_draw_object_box(current_target_object_idx, UINT16_MAX,
					  TARGET_BOX_COLOR);
	}
}

/* Draws an object's model rolled to face the local player's camera: adds to its
 * roll trig2_arctan(up, side) of the camera's offset taken along the object's
 * cached up and side axes, less a quarter turn (0x4000), draws it with
 * fview_set_object_transform and render_scene_draw_object_model, and puts the roll
 * back, marking the orientation dirty both times. Sets
 * g_billboard_object_or_type_index. */
// FUNCTION: XVT 0x41FF70
void scene_billboard_draw_roll_aligned_object_model(uint16_t object_index)
{
	struct object_record *object;
	int delta_x;
	int delta_y;
	int delta_z;
	int side_projection;
	int up_projection;
	int16_t saved_roll;

	g_billboard_object_or_type_index = object_index;
	object = &g_object_table[object_index];
	delta_x = g_players[g_local_player].view_state.camera_world_x -
		  object->world_x;
	delta_y = g_players[g_local_player].view_state.camera_world_y -
		  object->world_y;
	delta_z = g_players[g_local_player].view_state.camera_world_z -
		  object->world_z;
	side_projection = math_dot3q15(
		object->mobj->cached_side_x, object->mobj->cached_side_y,
		object->mobj->cached_side_z, delta_x, delta_y, delta_z);
	up_projection = math_dot3q15(
		object->mobj->cached_up_x, object->mobj->cached_up_y,
		object->mobj->cached_up_z, delta_x, delta_y, delta_z);
	saved_roll = object->roll;
	object->roll = (int16_t)(saved_roll +
				 trig2_arctan(up_projection, side_projection));
	object->roll -= BILLBOARD_ALIGNMENT_QUARTER_TURN;
	object->mobj->orient_matrix_dirty = 1;
	fview_set_object_transform(object->roll, object->pitch, object->yaw, 0,
				   object);
	render_scene_draw_object_model(object);
	object->roll = saved_roll;
	object->mobj->orient_matrix_dirty = 1;
}

/* Screen size of a billboard at a depth: s = model_max_extent / (size of
 * depth_z >> 8), or 0 when that is 0, then s * base_screen_size >> 8, capped at
 * 1024. The modern build works the product without signed overflow and leaves
 * INT32_MIN as it is. Only render_quad_draw_model_texture calls it. */
// FUNCTION: XVT 0x4243D0
int scene_billboard_compute_projected_size(int depth_z,
					   uint16_t model_max_extent,
					   uint16_t base_screen_size)
{
#ifdef XVT_MODERN
	if (depth_z < 0 && depth_z != INT32_MIN)
#else
	if (depth_z < 0)
#endif
		depth_z = -depth_z;
	depth_z >>= 8;
	/* From here depth_z holds the model's extent over that depth, a scale, and then that scale times
	 * base_screen_size over 256: the projected size returned. */
	if (depth_z != 0) {
		depth_z = model_max_extent / depth_z;
	}
#ifdef XVT_MODERN
	{
		uint32_t product;

		product = (uint32_t)base_screen_size * (uint32_t)depth_z;
		depth_z = (int)(product >> 8);
		if ((product & 0x80000000u) != 0) {
			depth_z -= 0x1000000;
		}
	}
#else
	depth_z *= base_screen_size;
	depth_z >>= 8;
#endif
	if (depth_z > 1024) {
		depth_z = 1024;
	}
	return depth_z;
}

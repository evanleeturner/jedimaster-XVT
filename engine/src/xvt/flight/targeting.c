#include "xvt/flight/targeting.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_hud.h"
#endif

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

/* How far off the aim line the object targeting_test_aim_cone last tested lies:
 * its up slope times 59578 / 65536 plus its side slope, each 256 times the
 * offset over the forward distance; 0xFFFF when the test returned early. Only
 * targeting_test_aim_cone writes it; player_pick_target_in_sight keeps the object
 * with the lowest. */
// GLOBAL: XVT 0x9A73A0
uint16_t g_target_angle_score = -1;

/* Tells whether an object, or the point a reference names, lies in the aim cone
 * ahead of the player's craft: returns 1 when both its up and side slopes are
 * below a bound set by its size and distance, else 0. Returns 0 at once when
 * the player has no craft, the point is not ahead or is more than 0x20000 ahead
 * after scaling, or the side slope exceeds 160 or the up slope 100. Within a
 * rough distance of 655360 it measures at 1/16 scale and, for the player's
 * current target craft, at its selected component's center; beyond, at 1/256.
 * narrow_cone keeps the bound at the object's size, halving it close in;
 * otherwise the bound is tripled, at least 9. Writes g_target_angle_score (0xFFFF
 * first), g_last_rough_distance and g_world_loc_x, g_world_loc_y and g_world_loc_z.
 * Reads the size of g_object_table[object_idx] without checking that object_idx
 * names an object slot. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x482640
int16_t targeting_test_aim_cone(uint16_t object_idx, int16_t narrow_cone,
				int player_idx)
{
	g_target_angle_score = -1;
	uint16_t object_index = object_idx;
	if (g_players[player_idx].object_index == -1) {
		return 0;
	}
	pai_object_ref_update_rough_distance(
		object_index, g_players[player_idx].object_index);

	int16_t scale_shift;
	int dx;
	int16_t dy;
	int16_t dz;
	struct object_record *target;
	if (g_last_rough_distance < 655360) {
		mission_resolve_object_or_mission_point_world_loc(object_index,
								  0);
		if (g_players[player_idx].current_target_object_idx ==
		    (int16_t)object_index) {
			target = &g_object_table[object_index];
			int object_type = target->object_type;
			if (object_type != 0 &&
			    g_active_region_craft_object_slot_end >
				    (int)object_index &&
			    target->mobj->p_craft != NULL) {
				uint16_t mesh_index =
					(uint16_t)g_players[player_idx]
						.selected_target_component;
				pai_rotate_local_vector_to_world_scratch(
					target,
					model_mesh_get_center_x(object_type,
								mesh_index),
					model_mesh_get_center_z(object_type,
								mesh_index),
					-model_mesh_get_center_y(object_type,
								 mesh_index));
				g_world_loc_x += g_rotated_x;
				g_world_loc_y += g_rotated_y;
				g_world_loc_z += g_rotated_z;
				g_last_rough_distance = collide_roughdistance3d(
					g_world_loc_x -
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.world_x,
					g_world_loc_y -
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.world_y,
					g_world_loc_z -
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.world_z);
			}
		}
		dx = (g_world_loc_x -
		      g_object_table[g_players[player_idx].object_index]
			      .world_x) >>
		     4;
		dy = (g_world_loc_y -
		      g_object_table[g_players[player_idx].object_index]
			      .world_y) >>
		     4;
		dz = (g_world_loc_z -
		      g_object_table[g_players[player_idx].object_index]
			      .world_z) >>
		     4;
		scale_shift = 4;
	} else {
		mission_resolve_object_or_mission_point_world_loc(object_index,
								  0);
		dx = (g_world_loc_x -
		      g_object_table[g_players[player_idx].object_index]
			      .world_x) >>
		     8;
		dy = (g_world_loc_y -
		      g_object_table[g_players[player_idx].object_index]
			      .world_y) >>
		     8;
		dz = (g_world_loc_z -
		      g_object_table[g_players[player_idx].object_index]
			      .world_z) >>
		     8;
		scale_shift = 8;
	}

	target = &g_object_table[g_players[player_idx].object_index];
	if (target->mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(target->pitch, target->yaw, target);
		fview_calcrotateorient(
			g_object_table[g_players[player_idx].object_index].roll,
			0, &g_object_table[g_players[player_idx].object_index]);
	}
	struct mobile_object **player_mobile_object =
		&g_object_table[g_players[player_idx].object_index].mobj;
	int forward = math_mul_q15((int16_t)dx,
				   (*player_mobile_object)->cached_fwd_x);
	forward += math_mul_q15(dy, (*player_mobile_object)->cached_fwd_y);
	forward += math_mul_q15(dz, (*player_mobile_object)->cached_fwd_z);
	if (forward <= 0) {
		return 0;
	}
	if (forward > 0x20000) {
		return 0;
	}
	if (narrow_cone != 0 && forward < 0x2000) {
		++scale_shift;
	}
	int side = math_mul_q15((int16_t)dx,
				(*player_mobile_object)->cached_side_x);
	side += math_mul_q15(dy, (*player_mobile_object)->cached_side_y);
	side += math_mul_q15(dz, (*player_mobile_object)->cached_side_z);
	if (side < 0) {
		side = -side;
	}
	int side_slope = (int)(((uint64_t)(unsigned int)side << 8) + 128) /
			 (unsigned int)forward;
	if (side_slope > 160) {
		return 0;
	}
	int up =
		math_mul_q15((int16_t)dx, (*player_mobile_object)->cached_up_x);
	up += math_mul_q15(dy, (*player_mobile_object)->cached_up_y);
	up += math_mul_q15(dz, (*player_mobile_object)->cached_up_z);
	if (up < 0) {
		up = -up;
	}
	int up_slope = (int)(((uint64_t)(unsigned int)up << 8) + 128) /
		       (unsigned int)forward;
	if (up_slope > 100) {
		return 0;
	}
	int up_score = (59578 * up_slope) >> 16;
	target = &g_object_table[object_index];
	int target_extent;
	if (target->mobj != NULL && target->mobj->p_craft != NULL) {
		model_index model_index = target->mobj->p_craft->model_index;
		int16_t bound_sum = g_model_defs[model_index].bound_size_x;
		bound_sum += g_model_defs[model_index].bound_size_y;
		bound_sum += g_model_defs[model_index].bound_size_z;
		uint8_t bound_shift =
			g_model_defs[model_index].bound_size_shift;
		target_extent = (bound_sum / 3) << bound_shift;
	} else {
		target_extent = g_object_type_table[target->object_type]
					.max_bounds_extent;
	}
	int extent_slope =
		(int)(((uint64_t)(unsigned int)(target_extent >> scale_shift)
		       << 8) +
		      128) /
		(unsigned int)forward;
	if (extent_slope <= 0) {
		extent_slope = 1;
	}
	if (narrow_cone == 0) {
		extent_slope *= 3;
		if (extent_slope < 10) {
			extent_slope = 9;
		}
	}
	g_target_angle_score = (uint16_t)(up_score + side_slope);
	return up_score < extent_slope && side_slope < extent_slope;
}

/* Draws a target box around craft in the active region for the local player,
 * skipping empty slots, the player's own craft, its current target and craft
 * with an active decoy beam. Only some craft get a box, each in a color named
 * below: in a melee, the local player's team's starfighters
 * (COLOR_LOCAL_QUICK_START_CRAFT) and the craft of any team whose score is the
 * highest above 0 (COLOR_LEADING_TEAM); other players' craft, hostile or allied
 * in a melee, else by IFF. Unless locate_players_enabled is set, a hostile craft
 * the local player's team has not identified gets none. */
// FUNCTION: XVT 0x482BE0
void targeting_draw_scene_object_boxes(void)
{
	enum {
		PLAYABLE_TEAM_COUNT = 8,
		NO_LEADING_TEAM = 10,
		COLOR_LOCAL_QUICK_START_CRAFT = 47,
		COLOR_HOSTILE_PLAYER = 51,
		COLOR_REBEL = 63,
		COLOR_IMPERIAL = 55,
		COLOR_BLUE = 51,
		COLOR_DEFAULT = 59,
		COLOR_LEADING_TEAM = 212,
		COLOR_ALLIED_PLAYER = 211,
	};

	int leading_team = NO_LEADING_TEAM;
	int leading_score;
	if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
		leading_score = 0;
		for (int team_index = 0; team_index < PLAYABLE_TEAM_COUNT;
		     ++team_index) {
			int team_score =
				g_flight_mission_state.runtime
					.team_scores[TEAM_SCORE_BONUS]
						    [team_index] +
				g_flight_mission_state.runtime
					.team_scores[TEAM_SCORE_MISSION]
						    [team_index];
			if (team_score > leading_score) {
				leading_score = team_score;
				leading_team = team_index;
			}
		}
	}

	for (int object_idx = g_active_region_object_slot_start;
	     object_idx < (int)g_active_region_craft_object_slot_end;
	     ++object_idx) {
		struct object_record *object = &g_object_table[object_idx];
		if (object->object_type == 0 ||
		    g_players[g_local_player].object_index == object_idx) {
			continue;
		}

		uint8_t color_index = 0;
		int team = object->mobj->team;
		int team_score = g_flight_mission_state.runtime
					 .team_scores[TEAM_SCORE_MISSION][team];
		team_score += g_flight_mission_state.runtime
				      .team_scores[TEAM_SCORE_BONUS][team];
		if (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
		    (uint16_t)g_players[g_local_player].team == team &&
		    object->genus_id == CRAFT_GENUS_STARFIGHTER) {
			color_index = COLOR_LOCAL_QUICK_START_CRAFT;
		} else if (leading_team == NO_LEADING_TEAM ||
			   team_score != leading_score) {
			if (object->player_owner_idx != -1 &&
			    object->player_owner_idx != g_local_player) {
				if (g_mission_header.mission_type ==
				    MISSION_TYPE_MELEE) {
					int craft_team =
						g_mission_flight_groups
							[g_object_table[(uint16_t)
										object_idx]
								 .flight_group_idx]
								.fg.team;
					int player_team =
						(uint16_t)g_players
							[g_local_player]
								.team;
					int is_hostile;
					if (craft_team == player_team) {
						is_hostile = 0;
					} else {
						is_hostile =
							g_mission_teams[player_team]
								.allies[craft_team] ==
							0;
					}
					if (is_hostile) {
						color_index =
							COLOR_HOSTILE_PLAYER;
					} else {
						color_index =
							COLOR_ALLIED_PLAYER;
					}
				} else {
					switch (object->mobj->iff) {
					case 0:
						color_index = COLOR_REBEL;
						break;
					case 1:
					case 4:
						color_index = COLOR_IMPERIAL;
						break;
					case 2:
						color_index = COLOR_BLUE;
						break;
					default:
						color_index = COLOR_DEFAULT;
						break;
					}
				}
			}
		} else {
			color_index = COLOR_LEADING_TEAM;
		}

		if (color_index != 0) {
			struct craft_data *craft = object->mobj->p_craft;
			if (g_flight_mission_state.locate_players_enabled ==
			    0) {
				int player_team =
					(uint16_t)g_players[g_local_player]
						.team;
				if (craft->identified_order_by_team
					    [player_team] == 0) {
					int craft_team =
						g_mission_flight_groups
							[g_object_table[(uint16_t)
										object_idx]
								 .flight_group_idx]
								.fg.team;
					int is_hostile;
					if (craft_team == player_team) {
						is_hostile = 0;
					} else {
						is_hostile =
							g_mission_teams[player_team]
								.allies[craft_team] ==
							0;
					}
					if (is_hostile) {
						continue;
					}
				}
			}

			if (object_has_active_decoy_beam(
				    (uint16_t)object_idx) == 0 &&
			    (uint16_t)g_players[g_local_player]
					    .current_target_object_idx !=
				    object_idx) {
				targeting_draw_object_box(
					object_idx, UINT16_MAX, color_index);
			}
		}
	}
}

/* Draws the corner box around an object, or around its component component_idx
 * unless that is UINT16_MAX, as the local player sees it. The size is the
 * component's max extent or targeting_get_object_box_extent, scaled by
 * g_proj_scale_int over depth and clamped from 4 pixels (8 above 320x240) up to
 * three quarters of the screen width, plus 4. Draws map-view corners when the
 * local player's map_camera_state is set, else depth-tested HUD corners. Does
 * nothing for UINT16_MAX, in replay view, with the local player's target box
 * off, or behind the camera. The modern build also hands the box to
 * xvt_render_hud_target_box. */
// FUNCTION: XVT 0x482EB0
void targeting_draw_object_box(uint16_t object_idx, uint16_t component_idx,
			       uint8_t color_index)
{
	enum {
		LOW_RESOLUTION_MIN_BOX_EXTENT = 4,
		DEFAULT_MIN_BOX_EXTENT = 8,
		BOX_SIZE_PADDING = 4,
	};

	if (object_idx == UINT16_MAX || g_replay_view_mode != 0 ||
	    g_players[g_local_player].target_box_enabled == 0) {
		return;
	}

	unsigned int component_index = component_idx;
	int screen_x;
	int screen_y;
	int depth;
	targeting_project_object_or_mission_point(object_idx, component_index,
						  &screen_x, &screen_y, &depth);
	if (depth <= 0) {
		return;
	}

	int object_extent;
	if (component_idx != UINT16_MAX) {
		object_extent = model_mesh_get_component_max_extent(
			g_object_table[object_idx].object_type,
			component_index);
	} else {
		object_extent = targeting_get_object_box_extent(object_idx);
	}
#ifdef XVT_MODERN
	xvt_render_hud_target_box(object_idx, component_idx, object_extent,
				  color_index);
#endif
	int projected_extent =
		(int)((unsigned int)(object_extent * (int)g_proj_scale_int) /
		      (unsigned int)depth);
	int minimum_extent;
	switch (g_flight_resolution_mode) {
	case FLIGHT_RESOLUTION_320X240:
		minimum_extent = LOW_RESOLUTION_MIN_BOX_EXTENT;
		break;
	default:
		minimum_extent = DEFAULT_MIN_BOX_EXTENT;
		break;
	}
	int maximum_extent =
		(int)((g_screen_width >> 1) + (g_screen_width >> 2));
	if (minimum_extent > projected_extent) {
		projected_extent = minimum_extent;
	}
	if (maximum_extent < projected_extent) {
		projected_extent = maximum_extent;
	}

	int box_size = projected_extent + BOX_SIZE_PADDING;
	if (g_players[g_local_player].map_camera_state != 0) {
		int half_box_size = box_size / 2;
		unsigned int draw_color = color_index;
		flight_map_draw_object_box_corners(
			screen_x - half_box_size, screen_y - half_box_size,
			box_size, box_size, draw_color);
	} else {
		int half_box_size = box_size / 2;
		unsigned int draw_color = color_index;
		hud_draw_depth_tested_box_corners(
			screen_x - half_box_size, screen_y - half_box_size,
			box_size, box_size, draw_color, depth);
	}
}

/* Returns an object's size for target boxes: for a craft, the mean of its
 * model's three bound sizes shifted left by bound_size_shift; else its type's
 * max_bounds_extent. */
// FUNCTION: XVT 0x483030
int targeting_get_object_box_extent(unsigned int object_idx)
{
	struct object_record *object = &g_object_table[object_idx];
	struct mobile_object *mobile_object = object->mobj;
	if (mobile_object != 0) {
		struct craft_data *craft = mobile_object->p_craft;
		if (craft != 0) {
			unsigned int model_index = craft->model_index;
			int average_extent =
				g_model_defs[model_index].bound_size_x;
			average_extent +=
				g_model_defs[model_index].bound_size_y;
			average_extent +=
				g_model_defs[model_index].bound_size_z;
			average_extent /= 3;
			return (int)((unsigned int)average_extent
				     << g_model_defs[model_index]
						.bound_size_shift);
		}

		return g_object_type_table[object->object_type]
			.max_bounds_extent;
	}

	return g_object_type_table[object->object_type].max_bounds_extent;
}

/* Projects an object, a mission point reference (0x8000 and up, read from
 * flight group 0), or the center of the object's component component_idx unless
 * that is UINT16_MAX, through the local player's camera. Always writes the view
 * depth to out_view_z; writes the screen position only when the depth is
 * positive. Writes g_world_loc_x, g_world_loc_y and g_world_loc_z. Only this file
 * calls it. */
// FUNCTION: XVT 0x4830D0
void targeting_project_object_or_mission_point(
	unsigned int obj_or_mission_point_ref, uint16_t component_idx,
	int *out_screen_x, int *out_screen_y, int *out_view_z)
{
	mission_resolve_object_or_mission_point_world_loc(
		obj_or_mission_point_ref, 0);
	if (component_idx != UINT16_MAX) {
		struct object_record *object =
			&g_object_table[obj_or_mission_point_ref];
		int object_type = object->object_type;
		int local_fwd =
			model_mesh_get_center_y(object_type, component_idx);
		local_fwd = -local_fwd;
		int local_up =
			model_mesh_get_center_z(object_type, component_idx);
		int local_side =
			model_mesh_get_center_x(object_type, component_idx);

		pai_rotate_local_vector_to_world_scratch(object, local_side,
							 local_up, local_fwd);
		g_world_loc_x += g_rotated_x;
		g_world_loc_y += g_rotated_y;
		g_world_loc_z += g_rotated_z;
	}

	int delta_x = g_world_loc_x -
		      g_players[g_local_player].view_state.camera_world_x;
	int delta_y = g_world_loc_y -
		      g_players[g_local_player].view_state.camera_world_y;
	int delta_z = g_world_loc_z -
		      g_players[g_local_player].view_state.camera_world_z;
	int view_z = transfm2_cam_mat_dot_row2(delta_x, delta_y, delta_z);
	*out_view_z = view_z;
	if (view_z > 0) {
		int view_x =
			transfm2_cam_mat_dot_row0(delta_x, delta_y, delta_z);
		int view_y =
			transfm2_cam_mat_dot_row1(delta_x, delta_y, delta_z);
		*out_screen_x = transfm2_project_screen_x(view_x, view_z);
		*out_screen_y = transfm2_project_screen_y(view_y, view_z);
	}
}

/* Nothing calls this. Writes an object's projected size in pixels, seen from
 * camera_x, camera_y and camera_z through the current camera matrix, to both
 * out_width and out_height: the craft model's mean bound size, or the type's
 * max_bounds_extent, times g_proj_scale_int over view depth, measured at 1/16 scale
 * within 0x80000 units and 1/256 beyond. Writes 0 for UINT16_MAX or an object
 * behind the camera. Writes g_world_loc_x, g_world_loc_y, g_world_loc_z and trig2's
 * polar results. */
// FUNCTION: XVT 0x483220
void targeting_compute_projected_object_extent(uint16_t object_idx,
					       uint16_t *out_width,
					       uint16_t *out_height,
					       int camera_x, int camera_y,
					       int camera_z)
{
	if (object_idx == UINT16_MAX) {
		*out_width = 0;
		*out_height = 0;
		return;
	}

	int object_index = object_idx;
	g_world_loc_x = g_object_table[object_index].world_x;
	g_world_loc_y = g_object_table[object_index].world_y;
	g_world_loc_z = g_object_table[object_index].world_z;
	trig2_ctop(camera_x - g_world_loc_x, camera_y - g_world_loc_y,
		   camera_z - g_world_loc_z);
	int delta_x;
	int delta_y;
	int delta_z;
	uint16_t distance_shift;
	if (trig2_polardistance < 0x80000) {
		mission_resolve_object_or_mission_point_world_loc(object_idx,
								  0);
		delta_x = (g_world_loc_x - camera_x) >> 4;
		delta_y = (g_world_loc_y - camera_y) >> 4;
		delta_z = (g_world_loc_z - camera_z) >> 4;
		distance_shift = 4;
	} else {
		mission_resolve_object_or_mission_point_world_loc(object_idx,
								  0);
		delta_x = (g_world_loc_x - camera_x) >> 8;
		delta_y = (g_world_loc_y - camera_y) >> 8;
		delta_z = (g_world_loc_z - camera_z) >> 8;
		distance_shift = 8;
	}

	int view_depth = transfm2_cam_mat_dot_row2(
		(int16_t)delta_x, (int16_t)delta_y, (int16_t)delta_z);
	if (view_depth <= 0) {
		*out_width = 0;
		*out_height = 0;
		return;
	}

	struct mobile_object *mobile_object = g_object_table[object_index].mobj;
	struct craft_data *craft;
	int max_bounds_extent;
	if (mobile_object != 0 && (craft = mobile_object->p_craft) != 0) {
		int16_t average_extent =
			g_model_defs[craft->model_index].bound_size_x;
		average_extent += g_model_defs[craft->model_index].bound_size_y;
		average_extent += g_model_defs[craft->model_index].bound_size_z;
		max_bounds_extent =
			(average_extent / 3)
			<< g_model_defs[craft->model_index].bound_size_shift;
	} else {
		max_bounds_extent =
			g_object_type_table[g_object_table[object_index]
						    .object_type]
				.max_bounds_extent;
	}

	max_bounds_extent >>= distance_shift;
	unsigned int projected_extent =
		g_proj_scale_int * max_bounds_extent / (unsigned int)view_depth;
	*out_width = projected_extent;
	*out_height = projected_extent;
}

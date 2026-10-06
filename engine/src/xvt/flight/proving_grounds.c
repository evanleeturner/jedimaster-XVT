#include "xvt/flight/proving_grounds.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math.h"
#include "xvt/net/flight_net.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Place values for proving_grounds_draw_score_decimal, indexed by the digit places
 * left to draw: entry n is 10 to the power n - 1; entry 0 is never read. Only
 * that function reads it. */
// GLOBAL: XVT 0x520EF0
int g_proving_grounds_score_decimal_divisors[9] = {
	1, 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000};
/* Ticks left before the course animation frame steps. Only
 * proving_grounds_update_course writes it: it counts down by g_elapsed_ticks and,
 * below 0, adds 29. */
// GLOBAL: XVT 0x520EE8
static int g_proving_grounds_course_anim_timer = 0;
/* Course animation frame, 0 to 3, stepped every 29 ticks and shown as each
 * course object's node_switch_index. Only proving_grounds_update_course writes
 * it. */
// GLOBAL: XVT 0x520EEC
static int g_proving_grounds_course_anim_frame = 0;
/* Ticks per obstacle animation step, by proving grounds level 0 to 19, shorter
 * at higher levels. */
// GLOBAL: XVT 0x5234C8
static const uint16_t
	g_proving_grounds_obstacle_anim_period_ticks_by_level[20] = {
		24, 24, 24, 24, 20, 16, 14, 14, 14, 12,
		12, 12, 10, 8,	6,  6,	6,  6,	6,  6,
};
/* The status panel's five labels, in proving_grounds_status_label_id order;
 * string_table_load_game_strings fills them. */
// GLOBAL: XVT 0xA606F0
const char *g_proving_grounds_status_labels[5] = {0};
/* The local player's roll over the last four pose records in the proving
 * grounds, newest first. proving_grounds_record_local_player_pose_history writes it;
 * collide_collisions puts a colliding craft back to entry 3. */
// GLOBAL: XVT 0x9A8D58
int16_t g_proving_grounds_local_player_roll_history[4] = {0};
/* The local player's pitch over the last four pose records, newest first; kept
 * like g_proving_grounds_local_player_roll_history. */
// GLOBAL: XVT 0x9ED220
int16_t g_proving_grounds_local_player_pitch_history[4] = {0};
/* The local player's yaw over the last four pose records, newest first; kept
 * like g_proving_grounds_local_player_roll_history. */
// GLOBAL: XVT 0xA004C0
int16_t g_proving_grounds_local_player_yaw_history[4] = {0};
/* The local player's world Z over the last four pose records, newest first,
 * taken from prev_world_z; kept like g_proving_grounds_local_player_roll_history. */
// GLOBAL: XVT 0xA08200
int g_proving_grounds_local_player_world_z_history[4] = {0};
/* The local player's world X over the last four pose records, newest first,
 * taken from prev_world_x; kept like g_proving_grounds_local_player_roll_history. */
// GLOBAL: XVT 0xA08220
int g_proving_grounds_local_player_world_x_history[4] = {0};
/* The local player's world Y over the last four pose records, newest first,
 * taken from prev_world_y; kept like g_proving_grounds_local_player_roll_history. */
// GLOBAL: XVT 0xA08230
int g_proving_grounds_local_player_world_y_history[4] = {0};
/* Ticks left before each obstacle animation steps: 0 turns cargo meshes, 1
 * antennas, 2 misc hull meshes. Only proving_grounds_update_course writes it. */
// GLOBAL: XVT 0xA00738
static int16_t g_proving_grounds_obstacle_anim_timers[3] = {0};
/* Object slot, 1 to 12, of the course gate the local player passed last; gate 1
 * is the start and finish. Two functions write it: proving_grounds_start_level
 * sets 1 and proving_grounds_update_course moves it on as gates are crossed. */
// GLOBAL: XVT 0x9A8078
uint16_t g_proving_grounds_current_checkpoint_obj_idx = 0;

/* Shifts the local player's pose history one place older, dropping entry 3, and
 * records the craft's prev_world_x, prev_world_y and prev_world_z and current roll,
 * pitch and yaw as entry 0. In the modern build with unlocked timing it records
 * only when xvt_player_timing_record_recovery gives a reference position, which it
 * records in place of the previous position. Does not check that the local
 * player has a craft. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x42B290
void proving_grounds_record_local_player_pose_history(void)
{
	int32_t reference_position[3];
	if (xvt_flight_timing_is_unlocked() &&
	    !xvt_player_timing_record_recovery(g_local_player,
					       reference_position)) {
		return;
	}

	uint16_t history_index = 2;
	do {
		g_proving_grounds_local_player_world_x_history[history_index +
							       1] =
			g_proving_grounds_local_player_world_x_history
				[history_index];
		g_proving_grounds_local_player_world_y_history[history_index +
							       1] =
			g_proving_grounds_local_player_world_y_history
				[history_index];
		g_proving_grounds_local_player_world_z_history[history_index +
							       1] =
			g_proving_grounds_local_player_world_z_history
				[history_index];
		g_proving_grounds_local_player_roll_history[history_index + 1] =
			g_proving_grounds_local_player_roll_history
				[history_index];
		g_proving_grounds_local_player_pitch_history[history_index +
							     1] =
			g_proving_grounds_local_player_pitch_history
				[history_index];
		g_proving_grounds_local_player_yaw_history[history_index + 1] =
			g_proving_grounds_local_player_yaw_history
				[history_index];
	} while (history_index-- != 0);

	struct object_record *object =
		&g_object_table[g_players[g_local_player].object_index];

	g_proving_grounds_local_player_world_x_history[0] =
		xvt_flight_timing_is_unlocked() ? reference_position[0]
						: object->mobj->prev_world_x;

	g_proving_grounds_local_player_world_y_history[0] =
		xvt_flight_timing_is_unlocked() ? reference_position[1]
						: object->mobj->prev_world_y;

	g_proving_grounds_local_player_world_z_history[0] =
		xvt_flight_timing_is_unlocked() ? reference_position[2]
						: object->mobj->prev_world_z;

	g_proving_grounds_local_player_roll_history[0] = object->roll;
	g_proving_grounds_local_player_pitch_history[0] = object->pitch;
	g_proving_grounds_local_player_yaw_history[0] = object->yaw;
}

/* Draws one course object. The gate at g_proving_grounds_current_checkpoint_obj_idx
 * and the one after it are drawn whole, with their billboards queued; every
 * other one gets g_billboard_object_or_type_index 0x7000 plus its slot. Then each
 * draws its first main hull node (its last node when it has none) with
 * node_switch_index 1; a gate before the current one does so with
 * g_billboard_target_selection_state 1 and g_render_object_ref set to
 * g_billboard_object_or_type_index. Restores node_switch_index and g_render_object_ref;
 * writes g_billboard_model_node_switch_index. The branch that sets
 * g_local_beam_target_obj_idx never runs: it wants a slot below the current gate
 * where only the current gate and the next arrive. */
// FUNCTION: XVT 0x42B380
void proving_grounds_draw_course_object(uint16_t object_index)
{
	enum {
		PROVING_GROUNDS_BILLBOARD_REF_BASE = 0x7000,
		PROVING_GROUNDS_SELECTED_NODE_STATE = 1,
	};

	if (object_index == g_proving_grounds_current_checkpoint_obj_idx ||
	    (int)g_proving_grounds_current_checkpoint_obj_idx -
			    (int)object_index ==
		    -1) {
		if (object_index <
		    g_proving_grounds_current_checkpoint_obj_idx) {
			g_local_beam_target_obj_idx = object_index;
		}
		damage_queue_craft_billboards(object_index);
		render_scene_draw_object_model(&g_object_table[object_index]);
	} else {
		g_billboard_object_or_type_index =
			object_index + PROVING_GROUNDS_BILLBOARD_REF_BASE;
	}

	g_billboard_model_node_switch_index = 0;
	int object_type = g_object_table[object_index].object_type;
	int mesh_count;
	if (object_type < (int)(sizeof(g_object_type_mesh_cache) /
				sizeof(g_object_type_mesh_cache[0]))) {
		mesh_count = g_object_type_mesh_cache[object_type].mesh_count;
	} else {
		mesh_count = model_mesh_get_object_type_mesh_count(object_type);
	}
	mesh_component_type mesh_type;
	for (uint16_t mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
		++g_billboard_model_node_switch_index;
		int mesh_type_index = mesh_index;
		object_type = g_object_table[object_index].object_type;
		if (object_type < (int)(sizeof(g_object_type_mesh_cache) /
					sizeof(g_object_type_mesh_cache[0]))) {
			mesh_type = model_mesh_get_cached_object_type_mesh_type(
				object_type, mesh_type_index);
		} else {
			mesh_type = model_mesh_get_object_type_mesh_type(
				object_type, mesh_type_index);
		}
		if (mesh_type == MESH_COMPONENT_01_MAIN_HULL) {
			break;
		}
	}

	uint16_t selected_node_index = g_billboard_model_node_switch_index - 1;
	uint16_t saved_render_object_ref = g_render_object_ref;
	g_billboard_model_node_switch_index = selected_node_index;
	if (object_index < g_proving_grounds_current_checkpoint_obj_idx) {
		g_billboard_target_selection_state = 1;
		g_render_object_ref = g_billboard_object_or_type_index;
	}
	uint8_t saved_node_switch_index =
		g_object_table[object_index].mobj->node_switch_index;
	g_object_table[object_index].mobj->node_switch_index =
		PROVING_GROUNDS_SELECTED_NODE_STATE;
	render_scene_draw_selected_root_node(&g_object_table[object_index],
					     selected_node_index);
	g_object_table[object_index].mobj->node_switch_index =
		saved_node_switch_index;
	g_render_object_ref = saved_render_object_ref;
}

/* Builds the course in object slots 1 to 12: gates of object types 98 and 99 in
 * turn, each turned by a fixed table of angles and chained to the one before
 * through its model bounds. Each gets genus 14, family 6, flight group 1,
 * signature 1, no motion, damage_amount 0x7FFF, the craft record of the same
 * pool index with every component intact (component_hp 255), hull_max and
 * system_damage_hull_threshold 0x7FFF, only shields working, 0x7FFF forward
 * shield. Sets flight group 1's status1 to 5 and zeroes the proving grounds
 * targets destroyed and score. Writes g_cur_craft. */
// FUNCTION: XVT 0x42B550
void proving_grounds_init_course_objects(void)
{
	g_flight_mission_state.proving_grounds_targets_destroyed = 0;
	g_flight_mission_state.proving_grounds_score = 0;
	int offset_x = 0;
	int offset_y = 0;
	int offset_z = 0;

	uint16_t object_types[13];
	object_types[1] = 98;
	uint16_t pitches[13];
	pitches[1] = 0x4000;
	uint16_t yaws[13];
	yaws[1] = 0;
	uint16_t rolls[13];
	rolls[1] = 0;
	object_types[2] = 99;
	pitches[2] = 0x4000;
	yaws[2] = 0;
	rolls[2] = 0;
	object_types[3] = 98;
	pitches[3] = 0;
	yaws[3] = 0;
	rolls[3] = 0;
	object_types[4] = 99;
	pitches[4] = 0;
	yaws[4] = 0x4000;
	rolls[4] = 0;
	object_types[5] = 98;
	pitches[5] = 0x4000;
	yaws[5] = 0xC000;
	rolls[5] = 0;
	object_types[6] = 99;
	pitches[6] = 0x4000;
	yaws[6] = 0xC000;
	rolls[6] = 0x8000;
	object_types[7] = 98;
	pitches[7] = 0x8000;
	yaws[7] = 0;
	rolls[7] = 0;
	object_types[8] = 99;
	pitches[8] = 0x8000;
	yaws[8] = 0x8000;
	rolls[8] = 0;
	object_types[9] = 98;
	pitches[9] = 0x4000;
	yaws[9] = 0x8000;
	rolls[9] = 0;
	object_types[10] = 99;
	pitches[10] = 0x4000;
	yaws[10] = 0x8000;
	rolls[10] = 0x4000;
	object_types[11] = 98;
	pitches[11] = 0x4000;
	yaws[11] = 0x4000;
	rolls[11] = 0;
	uint16_t object_index = 1;
	object_types[12] = 99;
	pitches[12] = 0x4000;
	yaws[12] = 0x4000;
	rolls[12] = 0x4000;

	uint16_t component_index;
	int16_t local_y;
	int16_t size_y;
	int16_t local_z;
	int delta_x;
	do {
		uint16_t object_type = object_types[object_index];
		g_object_table[object_index].object_type = (uint8_t)object_type;
		g_object_table[object_index].object_signature = 1;
		g_object_table[object_index].mobj->family = 6;
		g_object_table[object_index].genus_id = 14;
		g_object_table[object_index].mobj->roll_impulse_rate = 0;
		g_object_table[object_index].mobj->speed = 0;
		g_object_table[object_index].mobj->speed_remainder = 0;
		g_object_table[object_index].mobj->damage_amount = 0x7FFF;
		g_object_table[object_index].mobj->lifetime_timer = 0;
		g_object_table[object_index].mobj->seconds_alive = 0;
		g_object_table[object_index].mobj->source_obj_idx = 0;
		g_object_table[object_index].mobj->source_object_type = 0;
		g_object_table[object_index].mobj->iff = 0;
		g_object_table[object_index].mobj->orient_matrix_dirty = 1;
		g_object_table[object_index].mobj->move_vector_dirty =
			g_object_table[object_index].mobj->orient_matrix_dirty;
		g_object_table[object_index].flight_group_idx = 1;
		g_mission_flight_groups[1].fg.status1 = 5;
		g_cur_craft = &g_craft_data_pool_base[object_index];
		g_object_table[object_index].mobj->p_craft = g_cur_craft;

		for (component_index = 0; component_index < 2;
		     ++component_index) {
			g_object_table[object_index]
				.type_specific_byte[component_index] = 0;
		}
		component_index = 0;
		do {
			g_cur_craft->component_state[component_index] = 0;
			g_cur_craft->mesh_rotation[component_index] = 0;
			g_cur_craft->component_hp[component_index] =
				(uint8_t)-1;
		} while (++component_index < 50);
		g_cur_craft->hull_max = 0x7FFF;
		g_cur_craft->system_damage_hull_threshold = 0x7FFF;
		g_cur_craft->hull_damage = 0;
		g_cur_craft->damage_stats.last_system_hit_time = 0;
		g_cur_craft->unused_mission_flag = 0;
		g_cur_craft->attacked_by_team[0] = 0;
		g_cur_craft->not_disabled_accounting_suppress = 0;
		g_cur_craft->captured_by_flight_group = 0;
		g_cur_craft->s_foil_state = 0;
		for (uint16_t beam_index = 0; beam_index < 5; ++beam_index) {
			g_cur_craft->beam_effect_accum[beam_index] = 0;
		}
		g_cur_craft->system_flags = CRAFT_SUBSYSTEM_FLAG_SHIELDS;
		g_cur_craft->working_subsystems = g_cur_craft->system_flags;
		g_cur_craft->shield_distrib_mode =
			SHIELD_DISTRIBUTION_FULLY_FORWARD;
		g_cur_craft->shield_energy[0] = 0x7FFF;
		g_cur_craft->shield_energy[1] = 0;
		g_object_table[object_index].roll = rolls[object_index];
		g_object_table[object_index].yaw = yaws[object_index];
		g_object_table[object_index].pitch = pitches[object_index];
		fview_calcrotatemove(g_object_table[object_index].pitch,
				     g_object_table[object_index].yaw,
				     &g_object_table[object_index]);
		fview_calcrotateorient(g_object_table[object_index].roll, 0,
				       &g_object_table[object_index]);

		int16_t local_x = (int16_t)-model_bounds_get_max_y(object_type);
		if (object_type == 98) {
			local_y = 0;
			size_y = (int16_t)model_bounds_get_size_y(object_type);
			local_z = 0;
		} else if (object_type == 99) {
			local_y = 0;
			delta_x = model_bounds_get_min_y(object_type);
			size_y = (int16_t)(delta_x + model_bounds_get_size_y(
							     object_type));
			delta_x = model_bounds_get_min_z(object_type);
			local_z = (int16_t)(delta_x + model_bounds_get_size_z(
							      object_type));
		}

		delta_x = math_mul_q15(0, g_fview_side_x_q15);
		int delta_y = math_mul_q15(0, g_fview_side_y_q15);
		int delta_z = math_mul_q15(0, g_fview_side_z_q15);
		delta_x += math_mul_q15(local_x, g_fview_forward_x_q15);
		delta_y += math_mul_q15(local_x, g_fview_forward_y_q15);
		delta_z += math_mul_q15(local_x, g_fview_forward_z_q15);
		delta_x += math_mul_q15(0, g_fview_up_x_q15);
		delta_y += math_mul_q15(0, g_fview_up_y_q15);
		delta_z += math_mul_q15(0, g_fview_up_z_q15);
		g_object_table[object_index].world_x = offset_x - 2 * delta_x;
		g_object_table[object_index].world_y = offset_y - 2 * delta_y;
		g_object_table[object_index].world_z = offset_z - 2 * delta_z;

		delta_x = math_mul_q15(local_y, g_fview_side_x_q15);
		delta_y = math_mul_q15(local_y, g_fview_side_y_q15);
		delta_z = math_mul_q15(local_y, g_fview_side_z_q15);
		delta_x += math_mul_q15(size_y, g_fview_forward_x_q15);
		delta_y += math_mul_q15(size_y, g_fview_forward_y_q15);
		delta_z += math_mul_q15(size_y, g_fview_forward_z_q15);
		delta_x += math_mul_q15(local_z, g_fview_up_x_q15);
		delta_y += math_mul_q15(local_z, g_fview_up_y_q15);
		delta_z += math_mul_q15(local_z, g_fview_up_z_q15);
		offset_x += 2 * delta_x;
		offset_y += 2 * delta_y;
		offset_z += 2 * delta_z;
		g_object_table[object_index].mobj->prev_world_x =
			g_object_table[object_index].world_x;
		g_object_table[object_index].mobj->prev_world_y =
			g_object_table[object_index].world_y;
		g_object_table[object_index].mobj->prev_world_z =
			g_object_table[object_index].world_z;
		XVT_LOG_DEBUG(
			"proving.checkpoint_placed checkpoint=%u type=%u x=%d y=%d z=%d roll=%u pitch=%u yaw=%u",
			(unsigned)object_index, (unsigned)object_type,
			g_object_table[object_index].world_x,
			g_object_table[object_index].world_y,
			g_object_table[object_index].world_z,
			(unsigned)g_object_table[object_index].roll,
			(unsigned)g_object_table[object_index].pitch,
			(unsigned)g_object_table[object_index].yaw);
	} while (++object_index < 13);
}

/* Starts the given proving grounds level: gate 1 current, 0 checkpoints passed,
 * 12 remaining; the countdown clock gets (10 - level) / 2 minutes plus 30
 * seconds for an odd level up to 8, or 5 * (20 - level) seconds above 8. Then
 * sets up every obstacle craft's components: gate 1 hides all of them
 * (component_state 2), with component_hp 255 on main hulls and 0 elsewhere; other
 * gates hide the main hull, give laser guns 2 * level, show cargo from level 2
 * with 3 * level, misc hull from level 5 with 255 and antennas from level 3
 * with 24 * level, else hide them with 0. Rebuilds the HUD unless in replay
 * view. Writes g_cur_craft. Does not check the level. */
// FUNCTION: XVT 0x42BD80
void proving_grounds_start_level(uint16_t level)
{
	enum {
		FIRST_CHECKPOINT_OBJECT = 1,
		CHECKPOINT_COUNT = 12,
		LONG_TIMER_LAST_LEVEL = 8,
		LONG_TIMER_BASE_MINUTES = 10,
		SHORT_TIMER_BASE_LEVEL = 20,
		SECONDS_PER_HALF_MINUTE = 30,
		SECONDS_PER_SHORT_LEVEL = 5,
		COMPONENT_DISABLED = 2,
		LASER_HP_PER_LEVEL = 2,
		CARGO_FIRST_ACTIVE_LEVEL = 2,
		CARGO_HP_PER_LEVEL = 3,
		HULL_FIRST_ACTIVE_LEVEL = 5,
		ANTENNA_FIRST_ACTIVE_LEVEL = 3,
		ANTENNA_HP_PER_LEVEL = 24,
	};

	g_proving_grounds_current_checkpoint_obj_idx = FIRST_CHECKPOINT_OBJECT;
	g_flight_mission_state.proving_grounds_checkpoints_passed = 0;
	g_flight_mission_state.proving_grounds_checkpoints_remaining =
		CHECKPOINT_COUNT;
	uint8_t countdown_seconds;
	if (level > LONG_TIMER_LAST_LEVEL) {
		g_mission_countdown_clock.minutes = 0;
		countdown_seconds = SECONDS_PER_SHORT_LEVEL *
				    (SHORT_TIMER_BASE_LEVEL - level);
	} else {
		g_mission_countdown_clock.minutes =
			(LONG_TIMER_BASE_MINUTES - level) / 2;
		countdown_seconds = SECONDS_PER_HALF_MINUTE * (level & 1);
	}
	g_mission_countdown_clock.seconds = countdown_seconds;

	int model_node_index;
	int object_type;
	int mesh_type_index;
	int mesh_count;
	mesh_component_type mesh_type;
	for (uint16_t object_index = 0;
	     object_index < (int)g_active_region_craft_object_slot_end;
	     ++object_index) {
		if (g_object_table[object_index].object_type == 0 ||
		    g_object_table[object_index].genus_id !=
			    CRAFT_GENUS_OBSTACLE) {
			continue;
		}

		g_cur_craft = g_object_table[object_index].mobj->p_craft;
		object_type = g_object_table[object_index].object_type;
		if (object_type < (int)(sizeof(g_object_type_mesh_cache) /
					sizeof(g_object_type_mesh_cache[0]))) {
			mesh_count = g_object_type_mesh_cache[object_type]
					     .mesh_count;
		} else {
			mesh_count = model_mesh_get_object_type_mesh_count(
				object_type);
		}

		model_node_index = 0;
		for (uint16_t mesh_index = 0; mesh_index < mesh_count;
		     ++mesh_index) {
			++model_node_index;
			mesh_type_index = mesh_index;
			object_type = g_object_table[object_index].object_type;
			if (object_type <
			    (int)(sizeof(g_object_type_mesh_cache) /
				  sizeof(g_object_type_mesh_cache[0]))) {
				mesh_type =
					model_mesh_get_cached_object_type_mesh_type(
						object_type, mesh_type_index);
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type, mesh_type_index);
			}

			if (object_index == FIRST_CHECKPOINT_OBJECT) {
				if (mesh_type == MESH_COMPONENT_01_MAIN_HULL) {
					g_cur_craft->component_hp
						[model_node_index - 1] =
						UINT8_MAX;
				} else {
					g_cur_craft->component_hp
						[model_node_index - 1] = 0;
				}
				g_cur_craft->component_state[model_node_index -
							     1] =
					COMPONENT_DISABLED;
				continue;
			}

			switch (mesh_type) {
			case MESH_COMPONENT_01_MAIN_HULL:
				g_cur_craft->component_state[model_node_index -
							     1] =
					COMPONENT_DISABLED;
				break;
			case MESH_COMPONENT_05_LASR_GUN:
				g_cur_craft
					->component_hp[model_node_index - 1] =
					LASER_HP_PER_LEVEL * level;
				g_cur_craft->component_state[model_node_index -
							     1] = 0;
				break;
			case MESH_COMPONENT_17_CARGO:
				if (level < CARGO_FIRST_ACTIVE_LEVEL) {
					g_cur_craft->component_state
						[model_node_index - 1] =
						COMPONENT_DISABLED;
					g_cur_craft->component_hp
						[model_node_index - 1] = 0;
				} else {
					g_cur_craft->component_hp
						[model_node_index - 1] =
						CARGO_HP_PER_LEVEL * level;
					g_cur_craft->component_state
						[model_node_index - 1] = 0;
					g_cur_craft->mesh_rotation
						[model_node_index - 1] = 0;
				}
				break;
			case MESH_COMPONENT_18_MISC_HULL:
				if (level < HULL_FIRST_ACTIVE_LEVEL) {
					g_cur_craft->component_state
						[model_node_index - 1] =
						COMPONENT_DISABLED;
					g_cur_craft->component_hp
						[model_node_index - 1] = 0;
				} else {
					g_cur_craft->component_state
						[model_node_index - 1] = 0;
					g_cur_craft->mesh_rotation
						[model_node_index - 1] = 0;
					g_cur_craft->component_hp
						[model_node_index - 1] =
						UINT8_MAX;
				}
				break;
			case MESH_COMPONENT_19_ANTENNA:
				if (level < ANTENNA_FIRST_ACTIVE_LEVEL) {
					g_cur_craft->component_state
						[model_node_index - 1] =
						COMPONENT_DISABLED;
					g_cur_craft->component_hp
						[model_node_index - 1] = 0;
				} else {
					g_cur_craft->component_hp
						[model_node_index - 1] =
						ANTENNA_HP_PER_LEVEL * level;
					g_cur_craft->component_state
						[model_node_index - 1] = 0;
					g_cur_craft->mesh_rotation
						[model_node_index - 1] = 0;
				}
				break;
			default:
				break;
			}
		}
	}

	if (g_replay_view_mode == 0) {
		hud_init_hud(g_local_player);
	}
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"proving.level_started level=%d minutes=%d seconds=%d score=%u tick=%d",
			(int)level, (int)g_mission_countdown_clock.minutes,
			(int)g_mission_countdown_clock.seconds,
			(unsigned)g_flight_mission_state.proving_grounds_score,
			g_game_time);
	}
}

/* Runs the course for one simulation step. Counts down the obstacle animation
 * timers by g_elapsed_ticks, turning cargo meshes from level 3, antennas from
 * level 4 and misc hull meshes from level 6 one step per period (the misc hull
 * uses the previous level's period); steps the course animation frame every 29
 * ticks into each object's node_switch_index (frame 3 shows as 1). When the local
 * player crosses the next gate (gate 1 after 12) it becomes current and is
 * counted; crossing gate 1 ends the level: IFMSG_198, then the clock runs down
 * to 0 one second at a time, each adding 10 to proving_grounds_time_bonus and the
 * score, beeping every 100 points, drawn with a wait of 4 ticks that holds the
 * game until it ends; then IFMSG_199 and the next level starts. Writes
 * g_cur_craft and g_input_timestamp. Does not check the level against the
 * 20-entry period table. */
// FUNCTION: XVT 0x42C0A0
void proving_grounds_update_course(void)
{
	enum {
		OBSTACLE_ANIM_COUNT = 3,
		SPECIAL_ANIM_INDEX = 2,
		COURSE_ANIM_PERIOD_TICKS = 29,
		COURSE_ANIM_FRAME_COUNT = 4,
		COURSE_OBJECT_FIRST = 1,
		COURSE_OBJECT_END = 13,
		COURSE_ANIM_PING_PONG_FRAME = 3,
		COURSE_ANIM_PING_PONG_DISPLAY_FRAME = 1,
		CARGO_FIRST_ANIM_LEVEL = 3,
		HULL_FIRST_ANIM_LEVEL = 6,
		ANTENNA_FIRST_ANIM_LEVEL = 4,
		LAST_CHECKPOINT_OBJECT = 12,
		SCORE_INCREMENT = 10,
		SCORE_SOUND_INTERVAL = 100,
		FRAME_DELAY_TICKS = 4,
	};

	int16_t obstacle_anim_steps[OBSTACLE_ANIM_COUNT];

	for (uint16_t anim_index = 0; anim_index < OBSTACLE_ANIM_COUNT;
	     ++anim_index) {
		int16_t anim_timer =
			g_proving_grounds_obstacle_anim_timers[anim_index] -
			g_elapsed_ticks;
		g_proving_grounds_obstacle_anim_timers[anim_index] = anim_timer;
		if (anim_timer < 0) {
			uint16_t anim_period =
				g_proving_grounds_obstacle_anim_period_ticks_by_level
					[g_flight_mission_state
						 .proving_grounds_level];
			if (anim_index == SPECIAL_ANIM_INDEX) {
				anim_period =
					g_proving_grounds_obstacle_anim_period_ticks_by_level
						[g_flight_mission_state
							 .proving_grounds_level -
						 1];
			}
			int16_t anim_step_count =
				1 - anim_timer / (int)anim_period;
			obstacle_anim_steps[anim_index] = anim_step_count;
			g_proving_grounds_obstacle_anim_timers[anim_index] =
				anim_timer + anim_step_count * anim_period;
		} else {
			obstacle_anim_steps[anim_index] = 0;
		}
	}

	g_proving_grounds_course_anim_timer -= g_elapsed_ticks;
	if (g_proving_grounds_course_anim_timer < 0) {
		g_proving_grounds_course_anim_timer += COURSE_ANIM_PERIOD_TICKS;
		++g_proving_grounds_course_anim_frame;
	}

	int object_type;
	int mesh_count;
	int model_node_index;
	int mesh_type_index;
	mesh_component_type mesh_type;
	for (uint16_t object_index = COURSE_OBJECT_FIRST;
	     object_index < COURSE_OBJECT_END; ++object_index) {
		g_cur_craft = g_object_table[object_index].mobj->p_craft;
		if (g_proving_grounds_course_anim_frame ==
		    COURSE_ANIM_FRAME_COUNT) {
			g_proving_grounds_course_anim_frame = 0;
		}
		g_object_table[object_index].mobj->node_switch_index =
			(uint8_t)g_proving_grounds_course_anim_frame;
		if (g_proving_grounds_course_anim_frame ==
		    COURSE_ANIM_PING_PONG_FRAME) {
			g_object_table[object_index].mobj->node_switch_index =
				COURSE_ANIM_PING_PONG_DISPLAY_FRAME;
		}

		object_type = g_object_table[object_index].object_type;
		if (object_type < (int)(sizeof(g_object_type_mesh_cache) /
					sizeof(g_object_type_mesh_cache[0]))) {
			mesh_count = g_object_type_mesh_cache[object_type]
					     .mesh_count;
		} else {
			mesh_count = model_mesh_get_object_type_mesh_count(
				object_type);
		}

		model_node_index = 0;
		for (int mesh_index = 0; mesh_index < mesh_count;
		     ++mesh_index) {
			++model_node_index;
			mesh_type_index = mesh_index;
			object_type = g_object_table[object_index].object_type;
			if (object_type <
			    (int)(sizeof(g_object_type_mesh_cache) /
				  sizeof(g_object_type_mesh_cache[0]))) {
				mesh_type =
					model_mesh_get_cached_object_type_mesh_type(
						object_type, mesh_type_index);
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type, mesh_type_index);
			}

			switch (mesh_type) {
			case MESH_COMPONENT_17_CARGO:
				if (g_flight_mission_state
					    .proving_grounds_level >=
				    CARGO_FIRST_ANIM_LEVEL) {
					g_cur_craft->mesh_rotation
						[model_node_index - 1] +=
						obstacle_anim_steps[0];
				}
				break;
			case MESH_COMPONENT_18_MISC_HULL:
				if (g_flight_mission_state
					    .proving_grounds_level >=
				    HULL_FIRST_ANIM_LEVEL) {
					g_cur_craft->mesh_rotation
						[model_node_index - 1] +=
						obstacle_anim_steps[2];
				}
				break;
			case MESH_COMPONENT_19_ANTENNA:
				if (g_flight_mission_state
					    .proving_grounds_level >=
				    ANTENNA_FIRST_ANIM_LEVEL) {
					g_cur_craft->mesh_rotation
						[model_node_index - 1] +=
						obstacle_anim_steps[1];
				}
				break;
			default:
				break;
			}
		}
	}

	uint16_t next_checkpoint_object = COURSE_OBJECT_FIRST;
	if (g_proving_grounds_current_checkpoint_obj_idx !=
	    LAST_CHECKPOINT_OBJECT) {
		next_checkpoint_object =
			g_proving_grounds_current_checkpoint_obj_idx + 1;
	}
	if (proving_grounds_has_player_crossed_checkpoint(
		    next_checkpoint_object)) {
		g_proving_grounds_current_checkpoint_obj_idx =
			next_checkpoint_object;
		++g_flight_mission_state.proving_grounds_checkpoints_passed;
		--g_flight_mission_state.proving_grounds_checkpoints_remaining;
		XVT_LOG_DEBUG(
			"proving.checkpoint_crossed checkpoint=%u passed=%u remaining=%u level=%d minutes=%d seconds=%d predicted=%d",
			(unsigned)next_checkpoint_object,
			(unsigned)g_flight_mission_state
				.proving_grounds_checkpoints_passed,
			(unsigned)g_flight_mission_state
				.proving_grounds_checkpoints_remaining,
			(int)g_flight_mission_state.proving_grounds_level,
			(int)g_mission_countdown_clock.minutes,
			(int)g_mission_countdown_clock.seconds,
			g_flight_sim_side_effects_suppressed);
		if (next_checkpoint_object == COURSE_OBJECT_FIRST) {
			msg_emit_in_flight_message(
				IFMSG_198_CONGRATULATIONS_LEVEL_COMPLETED_TIME_LEFT_BONUS,
				g_local_player);
			g_flight_mission_state.proving_grounds_time_bonus = 0;
			proving_grounds_render_time_bonus_frame();
			while (g_mission_countdown_clock.minutes != 0 ||
			       g_mission_countdown_clock.seconds != 0) {
				if (g_mission_countdown_clock.seconds != 0) {
					--g_mission_countdown_clock.seconds;
				} else {
					g_mission_countdown_clock.seconds = 59;
					--g_mission_countdown_clock.minutes;
				}
				g_flight_mission_state
					.proving_grounds_time_bonus +=
					SCORE_INCREMENT;
				g_flight_mission_state.proving_grounds_score +=
					SCORE_INCREMENT;
				if ((int32_t)g_flight_mission_state
						    .proving_grounds_score %
					    SCORE_SOUND_INTERVAL ==
				    0) {
					fsfx_play_sound(
						FLIGHT_SOUND_CONFIRM_BEEP, -1,
						g_local_player);
				}
				proving_grounds_render_time_bonus_frame();
				do {
					g_input_timestamp +=
						time_consume_elapsed_ticks();
				} while ((unsigned int)g_input_timestamp <
					 FRAME_DELAY_TICKS);
				g_input_timestamp = 0;
			}
			g_msg_arg_table[0] =
				g_flight_mission_state
					.proving_grounds_time_bonus;
			msg_emit_in_flight_message(
				IFMSG_199_BONUS_POINTS_AWARDED_ARG,
				g_local_player);
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"proving.level_completed level=%d left=%u bonus=%u score=%u targets=%u tick=%d",
					(int)g_flight_mission_state
						.proving_grounds_level,
					(unsigned)(g_flight_mission_state
							   .proving_grounds_time_bonus /
						   10),
					(unsigned)g_flight_mission_state
						.proving_grounds_time_bonus,
					(unsigned)g_flight_mission_state
						.proving_grounds_score,
					(unsigned)g_flight_mission_state
						.proving_grounds_targets_destroyed,
					g_game_time);
			}
			++g_flight_mission_state.proving_grounds_level;
			proving_grounds_start_level(
				g_flight_mission_state.proving_grounds_level);
		}
	}
}

/* Tells whether the local player's craft crossed a gate's plane: returns 1 when
 * its current and previous positions, both within 0x4000 units of the plane's
 * point on every axis, lie on opposite sides of the plane or on it; else 0. The
 * plane faces along the gate's forward axis through a point on it: -maxY of the
 * model bounds for type 98, else 0, moved 1024 back for the current gate and 32
 * forward for any other. Only this file calls it. */
// FUNCTION: XVT 0x42C410
int proving_grounds_has_player_crossed_checkpoint(uint16_t checkpoint_obj_idx)
{
	uint16_t checkpoint_index = checkpoint_obj_idx;
	uint16_t model_type = g_object_table[checkpoint_index].object_type;
	int16_t checkpoint_offset;
	if (model_type == 98) {
		checkpoint_offset =
			(int16_t)-model_bounds_get_max_y(model_type);
	} else {
		checkpoint_offset = 0;
	}
	if (checkpoint_obj_idx ==
	    g_proving_grounds_current_checkpoint_obj_idx) {
		checkpoint_offset = (int16_t)(checkpoint_offset - 1024);
	} else {
		checkpoint_offset = (int16_t)(checkpoint_offset + 32);
	}

	struct object_record *checkpoint = &g_object_table[checkpoint_index];
	int checkpoint_x =
		math_mul_q15(checkpoint_offset, checkpoint->mobj->cached_fwd_x);
	int checkpoint_y =
		math_mul_q15(checkpoint_offset, checkpoint->mobj->cached_fwd_y);
	int checkpoint_z =
		math_mul_q15(checkpoint_offset, checkpoint->mobj->cached_fwd_z);

	checkpoint_x = checkpoint->world_x + 2 * checkpoint_x;
	checkpoint_y = checkpoint->world_y + 2 * checkpoint_y;
	checkpoint_z = checkpoint->world_z + 2 * checkpoint_z;

	struct object_record *player_object =
		&g_object_table[g_players[g_local_player].object_index];
	int current_delta_x = player_object->world_x - checkpoint_x;
	int current_delta_y = player_object->world_y - checkpoint_y;
	int current_delta_z = player_object->world_z - checkpoint_z;
	if (current_delta_x > 0x4000) {
		return 0;
	}
	if (current_delta_x < -0x4000) {
		return 0;
	}
	if (current_delta_y > 0x4000) {
		return 0;
	}
	if (current_delta_y < -0x4000) {
		return 0;
	}
	if (current_delta_z > 0x4000) {
		return 0;
	}
	if (current_delta_z < -0x4000) {
		return 0;
	}

	int previous_delta_x = player_object->mobj->prev_world_x - checkpoint_x;
	int previous_delta_y = player_object->mobj->prev_world_y - checkpoint_y;
	int previous_delta_z = player_object->mobj->prev_world_z - checkpoint_z;
	if (previous_delta_x > 0x4000) {
		return 0;
	}
	if (previous_delta_x < -0x4000) {
		return 0;
	}
	if (previous_delta_y > 0x4000) {
		return 0;
	}
	if (previous_delta_y < -0x4000) {
		return 0;
	}
	if (previous_delta_z > 0x4000) {
		return 0;
	}
	if (previous_delta_z < -0x4000) {
		return 0;
	}

	int16_t current_side = (int16_t)math_dot3q15_wrapped(
		(int16_t)current_delta_x, (int16_t)current_delta_y,
		(int16_t)current_delta_z, checkpoint->mobj->cached_fwd_x,
		checkpoint->mobj->cached_fwd_y, checkpoint->mobj->cached_fwd_z);
	int16_t previous_side = (int16_t)math_dot3q15_wrapped(
		(int16_t)previous_delta_x, (int16_t)previous_delta_y,
		(int16_t)previous_delta_z, checkpoint->mobj->cached_fwd_x,
		checkpoint->mobj->cached_fwd_y, checkpoint->mobj->cached_fwd_z);

	return (current_side >= 0 && previous_side <= 0) ||
	       (current_side <= 0 && previous_side >= 0);
}

/* Draws the proving grounds panel of the HUD near x, y: on a full HUD redraw
 * the labels and level, every call the gates remaining and passed, targets
 * destroyed and score. 640x480 doubles the column width and offsets; the panel
 * sits one column right for craft models 7, 8, 11 and 15, else one left. The
 * modern build also records each field for its cockpit readouts. */
// FUNCTION: XVT 0x42C750
void proving_grounds_draw_status_panel(int16_t x, int16_t y)
{
	int16_t column_width;
	int16_t panel_y;
	int16_t panel_anchor_x;
	int panel_width;
	int level_label_x_offset;
	int level_value_x_offset;
	int score_label_x_offset;
	int value_x_offset;
	int score_value_x_offset;

	switch (g_flight_resolution_mode) {
	case FLIGHT_RESOLUTION_320X240:
		panel_y = y;
		column_width = 8;
		level_label_x_offset = 26;
		level_value_x_offset = 53;
		panel_width = 90;
		score_label_x_offset = 20;
		value_x_offset = 75;
		score_value_x_offset = 45;
		panel_y -= 6;
		panel_anchor_x = x;
		break;
	case FLIGHT_RESOLUTION_640X480:
		panel_anchor_x = x;
		column_width = 16;
		level_label_x_offset = 52;
		level_value_x_offset = 106;
		panel_width = 180;
		score_label_x_offset = 40;
		value_x_offset = 150;
		score_value_x_offset = 90;
		panel_anchor_x += 10;
		panel_y = y;
		panel_y -= 10;
		break;
	default:
		panel_y = y;
		column_width = 8;
		level_label_x_offset = 26;
		level_value_x_offset = 53;
		panel_width = 90;
		score_label_x_offset = 20;
		value_x_offset = 75;
		score_value_x_offset = 45;
		panel_y -= 6;
		panel_anchor_x = x;
		break;
	}

	int16_t panel_x;
	switch (g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft->model_index) {
	case 7:
	case 8:
	case 11:
	case 15:
		panel_x = panel_anchor_x + column_width;
		break;
	default:
		panel_x = panel_anchor_x - column_width;
		break;
	}

	if (g_hud_full_redraw_in_progress != 0) {
		flight_text_set_font_tier(1);
		flight_text_set_clip_rect(panel_x + column_width, panel_y,
					  panel_x + 10 * column_width,
					  panel_y + g_flight_font_line_height);
		flight_text_set_clear_line_background(1);
		flight_text_set_background_color(0x30);
		g_flight_fill_clip_rect_fn();
		flight_text_set_color(0x49);
		flight_text_set_cursor(panel_x + level_label_x_offset, panel_y);
		xvt_cockpit_text_record_field(
			(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						    0),
			g_proving_grounds_status_labels[PROVING_STATUS_LEVEL],
			XVT_COCKPIT_ALIGN_LEFT);
		flight_text_draw_string(
			g_proving_grounds_status_labels[PROVING_STATUS_LEVEL]);
		flight_text_set_color(0x4A);
		flight_text_set_cursor(panel_x + level_value_x_offset, panel_y);
		xvt_cockpit_readouts_record_number(
			XVT_COCKPIT_NUMBER_COURSE_LEVEL,
			g_flight_mission_state.proving_grounds_level, 2, 2);
		flight_text_draw_decimal_number(
			g_flight_mission_state.proving_grounds_level, 2, 2);
		flight_text_set_clip_rect(
			panel_x, panel_y + g_flight_font_line_height + 1,
			panel_x + panel_width,
			panel_y + 5 * g_flight_font_line_height + 1);
		flight_text_set_color(0x45);
		flight_text_set_cursor(panel_x,
				       panel_y + g_flight_font_line_height + 1);
		xvt_cockpit_text_record_field(
			(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						    1),
			g_proving_grounds_status_labels
				[PROVING_STATUS_SEGMENTS_LEFT],
			XVT_COCKPIT_ALIGN_LEFT);
		flight_text_draw_string(g_proving_grounds_status_labels
						[PROVING_STATUS_SEGMENTS_LEFT]);
		flight_text_set_cursor(
			panel_x, panel_y + 2 * g_flight_font_line_height + 1);
		xvt_cockpit_text_record_field(
			(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						    2),
			g_proving_grounds_status_labels
				[PROVING_STATUS_SEGMENTS_DONE],
			XVT_COCKPIT_ALIGN_LEFT);
		flight_text_draw_string(g_proving_grounds_status_labels
						[PROVING_STATUS_SEGMENTS_DONE]);
		flight_text_set_color(0x4D);
		flight_text_set_cursor(
			panel_x, panel_y + 3 * g_flight_font_line_height + 1);
		xvt_cockpit_text_record_field(
			(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						    3),
			g_proving_grounds_status_labels
				[PROVING_STATUS_TARGETS_HIT],
			XVT_COCKPIT_ALIGN_LEFT);
		flight_text_draw_string(g_proving_grounds_status_labels
						[PROVING_STATUS_TARGETS_HIT]);
		flight_text_set_color(0x51);
		flight_text_set_cursor(panel_x + score_label_x_offset,
				       panel_y + 4 * g_flight_font_line_height +
					       1);
		xvt_cockpit_text_record_field(
			(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						    4),
			g_proving_grounds_status_labels[PROVING_STATUS_SCORE],
			XVT_COCKPIT_ALIGN_LEFT);
		flight_text_draw_string(
			g_proving_grounds_status_labels[PROVING_STATUS_SCORE]);
	}

	flight_text_set_font_tier(1);
	xvt_cockpit_readouts_record_course(panel_x, panel_y, panel_width,
					   5 * g_flight_font_line_height + 1);
	flight_text_set_clip_rect(panel_x,
				  panel_y + g_flight_font_line_height + 1,
				  panel_x + panel_width,
				  panel_y + 5 * g_flight_font_line_height + 1);
	flight_text_set_clear_line_background(1);
	flight_text_set_background_color(0x30);
	flight_text_set_color(0x46);
	flight_text_set_cursor(panel_x + value_x_offset,
			       panel_y + g_flight_font_line_height + 1);
	xvt_cockpit_readouts_record_number(
		XVT_COCKPIT_NUMBER_COURSE_REMAINING,
		g_flight_mission_state.proving_grounds_checkpoints_remaining, 3,
		1);
	flight_text_draw_decimal_number(
		g_flight_mission_state.proving_grounds_checkpoints_remaining, 3,
		1);
	g_flight_draw_char_fn(' ');
	flight_text_set_cursor(panel_x + value_x_offset,
			       panel_y + 2 * g_flight_font_line_height + 1);
	xvt_cockpit_readouts_record_number(
		XVT_COCKPIT_NUMBER_COURSE_PASSED,
		g_flight_mission_state.proving_grounds_checkpoints_passed, 3,
		1);
	flight_text_draw_decimal_number(
		g_flight_mission_state.proving_grounds_checkpoints_passed, 3,
		1);
	g_flight_draw_char_fn(' ');
	flight_text_set_color(0x4E);
	flight_text_set_cursor(panel_x + value_x_offset,
			       panel_y + 3 * g_flight_font_line_height + 1);
	xvt_cockpit_readouts_record_number(
		XVT_COCKPIT_NUMBER_COURSE_TARGETS,
		g_flight_mission_state.proving_grounds_targets_destroyed, 3, 1);
	flight_text_draw_decimal_number(
		g_flight_mission_state.proving_grounds_targets_destroyed, 3, 1);
	g_flight_draw_char_fn(' ');
	flight_text_set_color(0x52);
	flight_text_set_cursor(panel_x + score_value_x_offset,
			       panel_y + 4 * g_flight_font_line_height + 1);
	xvt_cockpit_readouts_record_number(
		XVT_COCKPIT_NUMBER_COURSE_SCORE,
		g_flight_mission_state.proving_grounds_score, 6, 1);
	proving_grounds_draw_score_decimal(
		g_flight_mission_state.proving_grounds_score, 6, 1);
	g_flight_draw_char_fn(' ');
	flight_text_set_font_tier(2);
}

/* Draws score in width right-aligned decimal places with g_flight_draw_char_fn:
 * leading zeros show as spaces until minDigits places remain, and a place whose
 * digit comes out above 9 shows 9. Draws nothing for width 0. Does not check
 * that width is at most 8. */
// FUNCTION: XVT 0x42CBD0
void proving_grounds_draw_score_decimal(int score, unsigned int width,
					unsigned int min_digits)
{
	int16_t saw_digit = 0;
	unsigned int remaining_width = width;
	if (remaining_width == 0) {
		return;
	}

	uint16_t draw_char;
	do {
		int divisor = g_proving_grounds_score_decimal_divisors
			[remaining_width];
		int digit = score / divisor;
		score -= divisor * (uint16_t)digit;
		if (saw_digit != 0 || min_digits >= remaining_width ||
		    (uint16_t)digit != 0) {
			saw_digit = 1;
			draw_char = (uint16_t)digit;
			if (draw_char > 9) {
				draw_char = 9;
			}
			draw_char += '0';
		} else {
			draw_char = ' ';
		}
		g_flight_draw_char_fn((uint8_t)draw_char);
		--remaining_width;
	} while (remaining_width != 0);
}

/* Draws the countdown clock as minutes:seconds and the time bonus near the
 * bottom of the screen in shadowed text, at y 456 in 640x480 and y 190
 * otherwise, then redraws the HUD with the surface locked. Leaves
 * g_flight_text_shadow_enabled at 1. */
// FUNCTION: XVT 0x42CC40
void proving_grounds_render_time_bonus_frame(void)
{
	enum {
		LOW_TEXT_Y = 190,
		LOW_CLOCK_X = 200,
		LOW_BONUS_X = 255,
		HIGH_TEXT_Y = 456,
		HIGH_CLOCK_X = 360,
		HIGH_BONUS_X = 465,
		CLOCK_DIGITS = 2,
		BONUS_DIGITS = 5,
		COLOR_BACKGROUND = 0x2C,
		COLOR_TEXT = 0x43,
	};

	int text_y;
	int clock_x;
	int bonus_x;

	switch (g_flight_resolution_mode) {
	case FLIGHT_RESOLUTION_320X240:
		text_y = LOW_TEXT_Y;
		clock_x = LOW_CLOCK_X;
		bonus_x = LOW_BONUS_X;
		break;
	case FLIGHT_RESOLUTION_640X480:
		text_y = HIGH_TEXT_Y;
		clock_x = HIGH_CLOCK_X;
		bonus_x = HIGH_BONUS_X;
		break;
	default:
		text_y = LOW_TEXT_Y;
		clock_x = LOW_CLOCK_X;
		bonus_x = LOW_BONUS_X;
		break;
	}

	g_flight_text_shadow_enabled = 1;
	flight_text_set_background_color(COLOR_BACKGROUND);
	flight_text_set_color(COLOR_TEXT);
	flight_text_set_clear_line_background(0);
	flight_text_set_font_tier(1);
	flight_text_set_clip_rect(0, text_y, g_screen_width, g_screen_height);
	flight_text_set_cursor(clock_x, text_y);
	flight_text_draw_decimal_number(g_mission_countdown_clock.minutes,
					CLOCK_DIGITS, CLOCK_DIGITS);
	g_flight_draw_char_fn(':');
	flight_text_draw_decimal_number(g_mission_countdown_clock.seconds,
					CLOCK_DIGITS, CLOCK_DIGITS);
	flight_text_set_cursor(bonus_x, text_y);
	flight_text_draw_decimal_number(
		g_flight_mission_state.proving_grounds_time_bonus, BONUS_DIGITS,
		BONUS_DIGITS);
	flight_surface_lock();
	hud_render_hud(g_local_player);
	flight_surface_unlock();
}

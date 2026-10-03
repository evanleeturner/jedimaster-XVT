#include "xvt/flight/object/object.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"
#endif

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/util/game_rand.h"
#include <string.h>

/* Per object type counted from WARHEAD_OBJECT_TYPE_PROTON_TORPEDO (143),
 * the first entry of the shot's row of 7 in the two homing tables below;
 * object_update_lifetime_and_movement adds the shot's homing_tier, 0 to 6. */
// GLOBAL: XVT 0x521CB6
const uint8_t g_projectile_homing_profile_base_by_object_type[18] = {
	7, 0, 0, 0, 0, 0, 14, 21, 28, 14, 14, 14, 35, 0, 0, 0, 0, 0};
/* Homing turn rate in angle units per simulated second, by row plus homing
 * tier; each row of 7 starts with 0, so tier 0 never turns. */
// GLOBAL: XVT 0x521CC8
const uint16_t g_projectile_homing_turn_rate_by_profile[44] = {
	0,    1024, 2048, 3072, 5120, 7168,  9216,  0,	   512,	  1024,	 2048,
	3072, 4608, 6144, 0,	2048, 4096,  5120,  10240, 14336, 18432, 0,
	32,   64,   80,	  96,	112,  128,   0,	    512,   1024,  1280,	 1536,
	1792, 2048, 0,	  4096, 8192, 12288, 16384, 20480, 24576, 0,	 0,
};
/* Speed change per simulated second while homing, by the same rows: added
 * when the shot's yaw has reached the target's bearing and it is below its
 * cruise_speed, taken away (to no less than HOMING_MIN_TURN_SPEED, 200) while
 * its yaw still turns. */
// GLOBAL: XVT 0x521D20
const uint16_t g_projectile_homing_speed_adjust_rate_by_profile[44] = {
	0,   50,  100, 200, 300, 400, 500, 0,	25,   50,   100,
	150, 200, 250, 0,   100, 200, 400, 600, 800,  1000, 0,
	0,   0,	  0,   0,   0,	 0,   0,   10,	20,   30,   40,
	50,  60,  0,   100, 200, 400, 600, 800, 1000, 0,    0,
};

/* The slot numbers below describe the layout of g_object_table that
 * mission_init sets when a mission loads: craft 0 to 31, shots 32 to 191,
 * debris 192 to 207, explosions 208 to 223, character slots 224 to 479,
 * local slots 480 to 487, then 64 static slots, 488 to 551. Where a comment
 * says "restored", a world-state load copies the value back in:
 * flight_restore_world_state in the original build, xvt_snapshot_decode_prefix
 * in the modern one. */

/* One past the last craft slot, 32 (CRAFT_SLOT_COUNT); set by mission_init
 * and restored. Craft loops across the game run from
 * g_active_region_object_slot_start up to here. */
// GLOBAL: XVT 0x9A1FE0
int g_active_region_craft_object_slot_end = 0;
/* The object table: g_region_main_object_slot_end slots with a mobile_object,
 * then g_region_static_object_slot_count static slots. mission_init allocates
 * g_object_table_handle; fe_disk_io_lock_global_buffers points this at its locked
 * memory. In the modern build xvt_flight_loading_reset sets NULL. */
// GLOBAL: XVT 0x9A1FE8
struct object_record *g_object_table = 0;
/* Per mobile slot, the pool entries object_relink_mobile_object_pointers
 * would link. mission_init sets every index to -1 and nothing else writes
 * them, so the relink links none. */
// GLOBAL: XVT 0x99F9A0
struct mobile_object_link_indices g_mobile_object_link_indices[488] = {{0}};
/* Object type each slot was last spawned with, written by
 * mission_init_flight_group_object_slot; mission_init fills it with -1. Read
 * only by object_relink_mobile_object_pointers, in a branch that never runs. */
// GLOBAL: XVT 0x9A1090
int g_spawn_object_type_by_object_slot[488] = {0};
/* Entries in g_mobile_object_char_data_pool, 256 (CHAR_DATA_SLOT_COUNT); set by
 * mission_init and restored. */
// GLOBAL: XVT 0x9A7BA4
int g_mobile_object_char_data_count = 0;
/* First debris slot, 192 (g_projectile_object_slot_end); set by mission_init
 * and restored. */
// GLOBAL: XVT 0x9A7B58
int g_debris_object_slot_start = 0;
/* The character records, g_mobile_object_char_data_count of them, in the
 * locked memory of g_mobile_object_char_data_handle; fe_disk_io_lock_global_buffers
 * sets it. In the modern build xvt_flight_loading_reset sets NULL. */
// GLOBAL: XVT 0x9A8DA0
struct mobile_object_char_data *g_mobile_object_char_data_pool = 0;
/* One guidance record per shot slot, indexed by slot minus
 * g_projectile_object_slot_start (g_projectile_object_slots_total + 1 entries),
 * in the locked memory of g_warhead_guidance_pool_handle;
 * fe_disk_io_lock_global_buffers sets it. In the modern build
 * xvt_flight_loading_reset sets NULL. */
// GLOBAL: XVT 0x9A8E18
struct warhead_guidance_state *g_projectile_guidance_states = 0;
/* Static slots after g_region_main_object_slot_end, 64
 * (STATIC_OBJECT_SLOT_COUNT), set by mission_init and restored; -1 before
 * the first mission. */
// GLOBAL: XVT 0x9D1308
int g_region_static_object_slot_count = -1;
/* One past the last debris slot, 208; set by mission_init and restored. */
// GLOBAL: XVT 0x9D1140
int g_debris_object_slot_end = 0;
/* First of the 256 character slots, 224 (g_explosion_object_slot_end); set by
 * mission_init and restored. They are genus 16's range in
 * g_object_slot_range_by_genus, and flight_update_timers counts down the AI
 * timers of any object in them. */
// GLOBAL: XVT 0x9D1144
int g_mobile_object_char_data_slot_start = 0;
/* First explosion slot, 208 (g_debris_object_slot_end); set by mission_init
 * and restored. */
// GLOBAL: XVT 0x9D1310
int g_explosion_object_slot_start = 0;
/* One mobile_object per slot below g_region_main_object_slot_end, in the locked
 * memory of g_mobile_object_pool_handle; fe_disk_io_lock_global_buffers sets it.
 * In the modern build xvt_flight_loading_reset sets NULL. */
// GLOBAL: XVT 0x9D6820
struct mobile_object *g_mobile_object_pool_base = 0;
/* First shot slot, 32; set by mission_init and restored. Slots 32 to 159
 * hold player shots (12 per player, then 32 shared from slot 128), 160 to
 * 191 everyone else's. */
// GLOBAL: XVT 0x9E9640
int g_projectile_object_slot_start = 0;
/* Number of local slots at the end of the main region: mission_init sets 8
 * (LOCAL_DEBRIS_SLOT_COUNT); it is restored. Only the world-state checksums
 * read it, and the modern build's check that a loaded world matches the live
 * slot ranges: the checksums cover slots 0 to g_region_main_object_slot_end minus
 * this, which leaves out the 8 local slots, and mix in the value. */
// GLOBAL: XVT 0x9D767C
int g_local_debris_slot_count = 0;
/* Shot slots in all, 160: 128 for players and 32 for the rest; set by
 * mission_init and restored. */
// GLOBAL: XVT 0x9FD430
unsigned int g_projectile_object_slots_total = 0;
/* One past the last shot slot, 192; set by mission_init and restored. */
// GLOBAL: XVT 0xA00854
int g_projectile_object_slot_end = 0;
/* One past the last explosion slot, 224; set by mission_init and
 * restored. The map view walks slots 0 up to here. */
// GLOBAL: XVT 0x9EC47C
unsigned int g_explosion_object_slot_end = 0;
/* One past the last slot with a mobile_object, 488, set by mission_init and
 * restored; -1 before the first mission. The static slots follow it. */
// GLOBAL: XVT 0xA07CE8
int g_region_main_object_slot_end = -1;
/* First craft slot, 0; set by mission_init and restored. */
// GLOBAL: XVT 0xA07CEC
int g_active_region_object_slot_start = 0;
/* Debris slots in all, 16; set by mission_init and restored. Only the
 * world-state checksums and the modern build's range check read it. */
// GLOBAL: XVT 0xA0814C
unsigned int g_debris_object_slots_total = 0;
/* One past the last character slot, 480, where the local slots begin; set
 * by mission_init and restored. */
// GLOBAL: XVT 0x9FE7DC
int g_mobile_object_char_data_slot_end = 0;
/* The slot range object_alloc_slot_for_genus searches, per genus; only
 * mission_init writes it. It sets genera 0 to 5 to the craft slots, 6 to
 * the player shot slots (32 to 159), 7 to the other shot slots (160 to
 * 191), 11 to debris, 13 to explosions, 16 to the character slots, and 8
 * to 10, 12, 14 and 15 to empty; 17 to 19 stay 0. */
// GLOBAL: XVT 0xA082C0
struct object_slot_range g_object_slot_range_by_genus[20] = {{0}};

/* Advances every object in the mobile slots by one simulation step. First,
 * for each player flying a starfighter (remote players, then the local
 * one), moves hardpointWorld* to prevHardpointWorld* in g_players and stores
 * the craft's primary hardpoint world position in hardpointWorld*. Then,
 * for each slot below g_region_main_object_slot_end, or only
 * g_single_object_update_override_idx when that is not -1: an object with its
 * own sim_state_timestamp is stepped by its own elapsed ticks, which changes
 * g_elapsed_ticks and g_sim_steps_per_second for that object (both are put back
 * on return), and is skipped when that comes to 0 (a player's craft still
 * gets prevWorld* updated); lifetime_timer counts down, and at 0 the object
 * explodes or is removed by genus (a destroyed craft is recorded through
 * mission_record_craft_outcome); prevWorld* takes the current position,
 * except while a single object is overridden; a nonzero roll_impulse_rate
 * turns roll (and on a craft after an impact decays); then craft (genus 0
 * to 4), shots, small debris and explosions move along their move vector.
 * A craft with working systems adds its push accumulators, at most
 * max_push_rate per simulated second (250 while boarding, 750 while dropping
 * off, else the model's), and drags a carried object to its docking point.
 * A homing shot explodes when its target, in a mobile slot, is gone or its
 * slot reused; holds course while the target craft runs a decoy beam; and
 * otherwise turns toward the target (a mesh center on a craft) and changes
 * speed by the homing tables. In the modern build, timing-unlocked play
 * hands the integration to XvtFlightIntegration_*, and each object stepped
 * is reported to xvt_reference_motion_committed. Also writes
 * trig2_*movedist, g_rotated* and g_worldLoc*. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x445570
void object_update_lifetime_and_movement(void)
{
	enum {
		PLAYER_COUNT = 8,
		SPEED_TO_WORLD_SCALE = 4660,
		MOVE_VECTOR_SHIFT = 15,
		ROLL_IMPULSE_DECAY_SCALE = 1 << 12,
		ROLL_IMPULSE_ANGLE_SCALE = 4,
		BOARDING_PUSH_RATE = 250,
		DROPOFF_PUSH_RATE = 750,
		EXPLOSION_OBJECT_TYPE_FIRST = 127,
		EXPLOSION_OBJECT_TYPE_PROJECTILE = 129,
		EXPLOSION_OBJECT_TYPE_LARGE = 130,
		MODEL_LIGHT_SCALE_SHIFT = 9,
		HOMING_OBJECT_TYPE_FIRST = WARHEAD_OBJECT_TYPE_PROTON_TORPEDO,
		HOMING_MIN_TURN_SPEED = 200,
	};

	uint16_t player_index;
	uint16_t object_index;
	int saved_sim_steps_per_second;
	int saved_elapsed_ticks;
	int override_processed;
	int object_offset_index;

	for (player_index = 0; player_index < PLAYER_COUNT; ++player_index) {
		int player_object_index;
		struct object_record *player_object;
		model_index player_model_index;

		if (g_players[player_index].participation_state == 0 ||
		    player_index == g_local_player) {
			continue;
		}
		player_object_index = g_players[player_index].object_index;
		if (player_object_index == -1) {
			continue;
		}
		player_object = &g_object_table[player_object_index];
		if (player_object->genus_id == CRAFT_GENUS_STARFIGHTER &&
		    (g_single_object_update_override_idx == -1 ||
		     g_single_object_update_override_idx ==
			     player_object_index)) {
			player_model_index = get_model_index_from_type(
				player_object->object_type);
			pai_calcrotatedpoint(
				&g_object_table[g_players[player_index]
							.object_index],
				0,
				g_model_defs[player_model_index]
					.primary_hardpoint_z,
				g_model_defs[player_model_index]
					.primary_hardpoint_y);
			g_players[player_index].prev_hardpoint_world_x =
				g_players[player_index].hardpoint_world_x;
			g_players[player_index].prev_hardpoint_world_y =
				g_players[player_index].hardpoint_world_y;
			g_players[player_index].prev_hardpoint_world_z =
				g_players[player_index].hardpoint_world_z;
			g_players[player_index].hardpoint_world_x = g_rotated_x;
			g_players[player_index].hardpoint_world_y = g_rotated_y;
			g_players[player_index].hardpoint_world_z = g_rotated_z;
		}
	}

	player_index = (uint16_t)g_local_player;
	if (g_players[player_index].object_index != -1) {
		int player_object_index = g_players[player_index].object_index;
		struct object_record *player_object =
			&g_object_table[player_object_index];

		if (player_object->genus_id == CRAFT_GENUS_STARFIGHTER &&
		    (g_single_object_update_override_idx == -1 ||
		     g_single_object_update_override_idx ==
			     player_object_index)) {
			model_index player_model_index =
				get_model_index_from_type(
					player_object->object_type);

			pai_calcrotatedpoint(
				&g_object_table[g_players[player_index]
							.object_index],
				0,
				g_model_defs[player_model_index]
					.primary_hardpoint_z,
				g_model_defs[player_model_index]
					.primary_hardpoint_y);
			g_players[player_index].prev_hardpoint_world_x =
				g_players[player_index].hardpoint_world_x;
			g_players[player_index].prev_hardpoint_world_y =
				g_players[player_index].hardpoint_world_y;
			g_players[player_index].prev_hardpoint_world_z =
				g_players[player_index].hardpoint_world_z;
			g_players[player_index].hardpoint_world_x = g_rotated_x;
			g_players[player_index].hardpoint_world_y = g_rotated_y;
			g_players[player_index].hardpoint_world_z = g_rotated_z;
		}
	}

	saved_sim_steps_per_second = g_sim_steps_per_second;
	saved_elapsed_ticks = g_elapsed_ticks;
	object_index = 0;
	override_processed = 0;
	for (; object_index < g_region_main_object_slot_end; ++object_index) {
		struct object_record *object;
		struct mobile_object *mobile_object;
		uint16_t genus_id;
		uint16_t movement_distance;

		if (g_single_object_update_override_idx != -1) {
			if (override_processed != 0) {
				break;
			}
			override_processed = 1;
			object_index =
				(uint16_t)g_single_object_update_override_idx;
		}

		object_offset_index = object_index;
		g_sim_steps_per_second = (uint16_t)saved_sim_steps_per_second;
		g_elapsed_ticks = (uint16_t)saved_elapsed_ticks;
		object = &g_object_table[object_offset_index];
		mobile_object = object->mobj;
		if (mobile_object != NULL &&
		    mobile_object->sim_state_timestamp != 0) {
			g_elapsed_ticks =
				(uint16_t)(g_elapsed_ticks + g_game_time -
					   mobile_object->sim_state_timestamp);
			if (g_elapsed_ticks == 0) {
				if (g_single_object_update_override_idx == -1 &&
				    object->player_owner_idx != -1) {
					if (g_flight_mission_state
							    .proving_grounds_mode_active !=
						    0 &&
					    object->player_owner_idx ==
						    g_local_player) {
						proving_grounds_record_local_player_pose_history();
					}
					object->mobj->prev_world_x =
						object->world_x;
					object->mobj->prev_world_y =
						object->world_y;
					object->mobj->prev_world_z =
						object->world_z;
				}
				continue;
			} else {
				g_sim_steps_per_second =
					(uint16_t)(SIMULATION_TICKS_PER_SECOND /
						   g_elapsed_ticks);
				if (g_sim_steps_per_second == 0) {
					g_sim_steps_per_second = 1;
				}
				mobile_object->sim_state_timestamp +=
					g_elapsed_ticks;
			}
		}

		if (object->object_type == 0) {
			continue;
		}

		genus_id = object->genus_id;
		mobile_object = object->mobj;
		if (mobile_object->lifetime_timer != 0) {
			uint16_t old_lifetime = mobile_object->lifetime_timer;
			uint16_t new_lifetime =
				(uint16_t)(old_lifetime - g_elapsed_ticks);

			if (new_lifetime > old_lifetime) {
				new_lifetime = 0;
			}
			mobile_object->lifetime_timer = new_lifetime;
			if (new_lifetime == 0) {
				switch (genus_id) {
				case CRAFT_GENUS_STARFIGHTER:
					craft_detach_damageable_component(
						object_index, 1);
					collide_convert_object_to_explosion(
						object_index,
						(game_rand() &
						 1) + EXPLOSION_OBJECT_TYPE_FIRST);
					mission_record_craft_outcome(
						object_index,
						g_object_table
							[object_offset_index]
								.flight_group_idx,
						FLIGHT_GROUP_OUTCOME_DESTROYED);
					break;
				case CRAFT_GENUS_TRANSPORT:
				case CRAFT_GENUS_UTILITY_VEHICLE:
				case CRAFT_GENUS_FREIGHTER:
				case CRAFT_GENUS_STARSHIP:
				case CRAFT_GENUS_PLATFORM:
					if (model_mesh_has_fuselage(
						    object->object_type) == 0) {
						craft_spawn_main_hull_explosion_effects(
							object_index, 1);
						collide_convert_object_to_explosion(
							object_index,
							EXPLOSION_OBJECT_TYPE_LARGE);
						g_object_table
							[object_offset_index]
								.mobj
								->effect_size =
							(uint8_t)(g_object_type_table
									  [object->object_type]
										  .max_bounds_extent >>
								  MODEL_LIGHT_SCALE_SHIFT);
					} else {
						collide_convert_object_to_explosion(
							object_index,
							EXPLOSION_OBJECT_TYPE_LARGE);
					}
					mission_record_craft_outcome(
						object_index,
						g_object_table
							[object_offset_index]
								.flight_group_idx,
						FLIGHT_GROUP_OUTCOME_DESTROYED);
					break;
				case CRAFT_GENUS_PLAYER_PROJECTILE:
				case CRAFT_GENUS_OTHER_PROJECTILE:
					if (g_projectile_type_data.warhead_class
						    [object->object_type -
						     PROJECTILE_OBJECT_TYPE_FIRST] !=
					    0) {
						collide_convert_object_to_explosion(
							object_index,
							EXPLOSION_OBJECT_TYPE_PROJECTILE);
					} else {
						object->object_type = 0;
						continue;
					}
					break;
				case CRAFT_GENUS_SMALL_DEBRIS:
					collide_convert_object_to_explosion(
						object_index,
						(game_rand() &
						 1) + EXPLOSION_OBJECT_TYPE_FIRST);
					break;
				default:
					object->object_type = 0;
					continue;
				}
			}
		}

		mobile_object = object->mobj;
		if (g_single_object_update_override_idx == -1) {
			if (g_flight_mission_state
					    .proving_grounds_mode_active != 0 &&
			    g_object_table[object_offset_index]
					    .player_owner_idx ==
				    g_local_player) {
				proving_grounds_record_local_player_pose_history();
			}
			object->mobj->prev_world_x = object->world_x;
			object->mobj->prev_world_y = object->world_y;
			object->mobj->prev_world_z = object->world_z;
		}

		mobile_object = object->mobj;
		{
			int16_t *roll_impulse_rate_field =
				&mobile_object->roll_impulse_rate;
			int16_t roll_impulse_rate = *roll_impulse_rate_field;

			if (roll_impulse_rate != 0) {
				if (object_index <
				    g_active_region_craft_object_slot_end) {
					struct craft_data *craft =
						mobile_object->p_craft;

					if (craft->ai_flight.impact_obj_idx !=
					    UINT16_MAX) {
						int16_t *
							updated_roll_impulse_rate_field;

						if (roll_impulse_rate < 0) {
							*roll_impulse_rate_field =
								(int16_t)(roll_impulse_rate +
#ifdef XVT_MODERN
									  (xvt_flight_timing_is_unlocked()
										   ? xvt_flight_integration_rate(
											     object_index,
											     XVT_INTEGRATE_SPIN_DECAY,
											     ROLL_IMPULSE_DECAY_SCALE,
											     g_elapsed_ticks,
											     236)
										   : ((g_elapsed_ticks *
										       ROLL_IMPULSE_DECAY_SCALE) /
										      SIMULATION_TICKS_PER_SECOND))
#else
									  (g_elapsed_ticks *
									   ROLL_IMPULSE_DECAY_SCALE) /
										  SIMULATION_TICKS_PER_SECOND
#endif
								);
							updated_roll_impulse_rate_field =
								&object->mobj
									 ->roll_impulse_rate;
							if (*updated_roll_impulse_rate_field >=
							    0) {
								*updated_roll_impulse_rate_field =
									0;
								roll_impulse_rate =
									object->mobj
										->roll_impulse_rate;
								craft->ai_flight
									.impact_obj_idx =
									UINT16_MAX;
							}
						} else {
							*roll_impulse_rate_field =
								(int16_t)(roll_impulse_rate +
#ifdef XVT_MODERN
									  (xvt_flight_timing_is_unlocked()
										   ? xvt_flight_integration_rate(
											     object_index,
											     XVT_INTEGRATE_SPIN_DECAY,
											     -ROLL_IMPULSE_DECAY_SCALE,
											     g_elapsed_ticks,
											     236)
										   : ((g_elapsed_ticks *
										       ROLL_IMPULSE_DECAY_SCALE) /
										      -SIMULATION_TICKS_PER_SECOND))
#else
									  (g_elapsed_ticks *
									   ROLL_IMPULSE_DECAY_SCALE) /
										  -SIMULATION_TICKS_PER_SECOND
#endif
								);
							updated_roll_impulse_rate_field =
								&object->mobj
									 ->roll_impulse_rate;
							if (*updated_roll_impulse_rate_field <=
							    0) {
								*updated_roll_impulse_rate_field =
									0;
								roll_impulse_rate =
									object->mobj
										->roll_impulse_rate;
								craft->ai_flight
									.impact_obj_idx =
									UINT16_MAX;
							}
						}
					}
				}
				object->roll +=
					(int16_t)(ROLL_IMPULSE_ANGLE_SCALE *
						  (
#ifdef XVT_MODERN
							  (xvt_flight_timing_is_unlocked()
								   ? xvt_flight_integration_rate(
									     object_index,
									     XVT_INTEGRATE_SPIN_ANGLE,
									     roll_impulse_rate,
									     g_elapsed_ticks,
									     236)
								   : g_elapsed_ticks *
									     roll_impulse_rate /
									     SIMULATION_TICKS_PER_SECOND)
#else
							  g_elapsed_ticks *
							  roll_impulse_rate /
							  SIMULATION_TICKS_PER_SECOND
#endif
								  ));
				mobile_object->orient_matrix_dirty = 1;
			}
		}

		movement_distance = 0;
		mobile_object = object->mobj;
		if (mobile_object->speed != 0) {
			movement_distance =
				(uint16_t)(g_elapsed_ticks *
					   ((SPEED_TO_WORLD_SCALE *
						     mobile_object->speed +
					     128) >>
					    8) /
					   SIMULATION_TICKS_PER_SECOND);
		}

		switch (genus_id) {
		case CRAFT_GENUS_STARFIGHTER:
		case CRAFT_GENUS_TRANSPORT:
		case CRAFT_GENUS_UTILITY_VEHICLE:
		case CRAFT_GENUS_FREIGHTER:
		case CRAFT_GENUS_STARSHIP: {
			struct craft_data *craft = mobile_object->p_craft;

			if (mobile_object->move_vector_dirty != 0) {
				fview_calcrotatemove(object->pitch, object->yaw,
						     object);
				mobile_object = object->mobj;
			}
#ifdef XVT_MODERN
			if (xvt_flight_timing_is_unlocked()) {
				xvt_flight_integration_move(object_index);
			} else {
#endif

				trig2_xmovedist =
					math_mul_q15(mobile_object->move_x,
						     movement_distance);
				trig2_ymovedist =
					math_mul_q15(mobile_object->move_y,
						     movement_distance);
				trig2_zmovedist =
					math_mul_q15(mobile_object->move_z,
						     movement_distance);

#ifdef XVT_MODERN
			}
#endif

			if (craft->working_subsystems != 0) {
				int max_push_rate;
				int push_accumulator;
				int16_t clamped_push_rate;
				int push_step;

				if (craft->ai_controller.maneuver_mode ==
				    AI_MANEUVER_MODE_BOARD) {
					max_push_rate = BOARDING_PUSH_RATE;
				} else if (craft->ai_controller.maneuver_mode ==
					   AI_MANEUVER_MODE_DROPOFF) {
					max_push_rate = DROPOFF_PUSH_RATE;
				} else {
					max_push_rate =
						g_model_defs[craft->model_index]
							.max_push_rate;
				}

				push_accumulator = craft->push_accum_x;
				if (push_accumulator != 0) {
					clamped_push_rate =
						(int16_t)max_push_rate;
					if (push_accumulator < -max_push_rate) {
						clamped_push_rate =
							(int16_t)-max_push_rate;
					} else if (push_accumulator <=
						   max_push_rate) {
						clamped_push_rate = (int16_t)
							push_accumulator;
					}
#ifdef XVT_MODERN
					if (xvt_flight_timing_is_unlocked()) {
						xvt_flight_integration_push(
							object_index, 0,
							&craft->push_accum_x,
							max_push_rate,
							&trig2_xmovedist);
					} else {
#endif

						push_step =
							g_elapsed_ticks *
							clamped_push_rate /
							SIMULATION_TICKS_PER_SECOND;
						if (push_step == 0) {
							push_step =
								craft->push_accum_x;
						}
						craft->push_accum_x =
							push_accumulator -
							push_step;
						trig2_xmovedist += push_step;

#ifdef XVT_MODERN
					}
#endif
				}

				push_accumulator = craft->push_accum_y;
				if (push_accumulator != 0) {
					clamped_push_rate =
						(int16_t)max_push_rate;
					if (push_accumulator < -max_push_rate) {
						clamped_push_rate =
							(int16_t)-max_push_rate;
					} else if (push_accumulator <=
						   max_push_rate) {
						clamped_push_rate = (int16_t)
							push_accumulator;
					}
#ifdef XVT_MODERN
					if (xvt_flight_timing_is_unlocked()) {
						xvt_flight_integration_push(
							object_index, 1,
							&craft->push_accum_y,
							max_push_rate,
							&trig2_ymovedist);
					} else {
#endif

						push_step =
							g_elapsed_ticks *
							clamped_push_rate /
							SIMULATION_TICKS_PER_SECOND;
						if (push_step == 0) {
							push_step =
								craft->push_accum_y;
						}
						craft->push_accum_y =
							push_accumulator -
							push_step;
						trig2_ymovedist += push_step;

#ifdef XVT_MODERN
					}
#endif
				}

				push_accumulator = craft->push_accum_z;
				if (push_accumulator != 0) {
					clamped_push_rate =
						(int16_t)max_push_rate;
					if (push_accumulator < -max_push_rate) {
						clamped_push_rate =
							(int16_t)-max_push_rate;
					} else if (push_accumulator <=
						   max_push_rate) {
						clamped_push_rate = (int16_t)
							push_accumulator;
					}
#ifdef XVT_MODERN
					if (xvt_flight_timing_is_unlocked()) {
						xvt_flight_integration_push(
							object_index, 2,
							&craft->push_accum_z,
							max_push_rate,
							&trig2_zmovedist);
					} else {
#endif

						push_step =
							g_elapsed_ticks *
							clamped_push_rate /
							SIMULATION_TICKS_PER_SECOND;
						if (push_step == 0) {
							push_step =
								craft->push_accum_z;
						}
						craft->push_accum_z =
							push_accumulator -
							push_step;
						trig2_zmovedist += push_step;

#ifdef XVT_MODERN
					}
#endif
				}
			}

			object_add_trig_move_delta_and_clamp_world_position(
				(uint32_t *)object);
			if (craft->carried_object_index != UINT16_MAX) {
				uint16_t carried_object_index =
					craft->carried_object_index;
				struct object_record *carried_object =
					&g_object_table[carried_object_index];

				if (carried_object->mobj != NULL) {
					model_index carried_model_index =
						carried_object->mobj->p_craft
							->model_index;
					int16_t docking_forward =
						g_model_defs
							[carried_model_index]
								.dock_forward;
					int16_t docking_up_offset;

					if (carried_object->genus_id <=
					    CRAFT_GENUS_UTILITY_VEHICLE) {
						docking_up_offset =
							(int16_t)(g_model_defs[craft->model_index]
									  .dock_to_up
										  [0] -
								  g_model_defs[carried_model_index]
									  .dock_from_up
										  [0]);
					} else if (
						object->genus_id <=
						CRAFT_GENUS_UTILITY_VEHICLE) {
						docking_up_offset =
							(int16_t)(g_model_defs[craft->model_index]
									  .dock_to_up
										  [1] -
								  g_model_defs[carried_model_index]
									  .dock_from_up
										  [0]);
					} else {
						docking_up_offset =
							(int16_t)(g_model_defs[craft->model_index]
									  .dock_to_up
										  [1] -
								  g_model_defs[carried_model_index]
									  .dock_from_up
										  [1]);
					}
					pai_calcrotatedpoint(object, 0,
							     docking_up_offset,
							     docking_forward);
					carried_object =
						&g_object_table
							[carried_object_index];
					carried_object->mobj->prev_world_x =
						carried_object->world_x;
					carried_object->mobj->prev_world_y =
						carried_object->world_y;
					carried_object->mobj->prev_world_z =
						carried_object->world_z;
					carried_object->world_x =
						g_rotated_x + object->world_x;
					carried_object->world_y =
						g_rotated_y + object->world_y;
					carried_object->world_z =
						g_rotated_z + object->world_z;
				}
			}
			break;
		}
		case CRAFT_GENUS_PLAYER_PROJECTILE:
		case CRAFT_GENUS_OTHER_PROJECTILE: {
			struct warhead_guidance_state *guidance =
				mobile_object->p_warhead_guidance;

			if (
#ifdef XVT_MODERN
				/* Expiry can convert the object after genus_id was cached for movement. */
				(object->genus_id ==
					 CRAFT_GENUS_PLAYER_PROJECTILE ||
				 object->genus_id ==
					 CRAFT_GENUS_OTHER_PROJECTILE) &&
#endif
				guidance->homing_tier != 0 &&
				guidance->target_obj_idx != UINT16_MAX) {
				int target_object_index =
					guidance->target_obj_idx;

				if (target_object_index <
				    g_region_main_object_slot_end) {
					struct object_record *target_object =
						&g_object_table
							[target_object_index];

					if (target_object->object_type == 0 ||
					    guidance->target_signature !=
						    target_object
							    ->object_signature) {
						collide_convert_object_to_explosion(
							object_index,
							EXPLOSION_OBJECT_TYPE_PROJECTILE);
						break;
					}
				}
				{
					uint16_t target_component_index =
						guidance->target_component_idx;
					uint16_t profile_index =
						(uint16_t)(g_projectile_homing_profile_base_by_object_type
								   [object->object_type -
								    HOMING_OBJECT_TYPE_FIRST] +
							   guidance->homing_tier);
					int decoy_active = 0;

					if (target_object_index <
					    g_active_region_craft_object_slot_end) {
						struct craft_data *target_craft =
							g_object_table
								[target_object_index]
									.mobj
									->p_craft;

						if (target_craft->beam_type_id ==
							    BEAM_TYPE_DECOY &&
						    target_craft->beam_active !=
							    0) {
							decoy_active = 1;
						}
					}
					if (decoy_active == 0) {
						struct object_record
							*target_object;
						int target_object_type;
						int center_x;
						int center_y;
						int center_z;
						uint16_t old_yaw;
						uint16_t old_pitch;
						int16_t yaw_delta;
						int16_t pitch_delta;
						int16_t absolute_delta;
						int turn_step;

						mission_resolve_object_or_mission_point_world_loc(
							target_object_index, 0);
						if (target_object_index >=
							    g_active_region_craft_object_slot_end ||
						    g_object_table[target_object_index]
								    .mobj
								    ->family !=
							    0) {
							g_rotated_x =
								g_world_loc_x;
							g_rotated_y =
								g_world_loc_y;
							g_rotated_z =
								g_world_loc_z;
						} else {
							target_object =
								&g_object_table
									[target_object_index];
							if (target_component_index ==
							    UINT16_MAX) {
								target_object_type =
									target_object
										->object_type;
								center_y = -model_mesh_get_center_y(
									target_object_type,
									0);
								center_z = model_mesh_get_center_z(
									target_object_type,
									0);
								center_x = model_mesh_get_center_x(
									target_object_type,
									0);
							} else {
								target_object_type =
									target_object
										->object_type;
								center_y = -model_mesh_get_center_y(
									target_object_type,
									target_component_index);
								center_z = model_mesh_get_center_z(
									target_object_type,
									target_component_index);
								center_x = model_mesh_get_center_x(
									target_object_type,
									target_component_index);
							}
							pai_rotate_local_vector_to_world_scratch(
								target_object,
								center_x,
								center_z,
								center_y);
							g_rotated_x +=
								g_world_loc_x;
							g_rotated_y +=
								g_world_loc_y;
							g_rotated_z +=
								g_world_loc_z;
						}
						mission_resolve_object_or_mission_point_world_loc(
							object_index, 0);
						g_rotated_x -= g_world_loc_x;
						g_rotated_y -= g_world_loc_y;
						g_rotated_z -= g_world_loc_z;
						trig2_ctop(g_rotated_x,
							   g_rotated_y,
							   g_rotated_z);
						old_yaw = object->yaw;

						yaw_delta =
							(int16_t)(trig2_xyangle -
								  old_yaw);
						absolute_delta = yaw_delta;
#ifdef XVT_MODERN
						if (xvt_flight_timing_is_unlocked()) {
							turn_step = xvt_flight_integration_rate(
								object_index,
								XVT_INTEGRATE_HOME_YAW,
								g_projectile_homing_turn_rate_by_profile
									[profile_index],
								g_elapsed_ticks,
								236);
						} else
#endif
							turn_step =
								g_elapsed_ticks *
								g_projectile_homing_turn_rate_by_profile
									[profile_index] /
								SIMULATION_TICKS_PER_SECOND;
						if (yaw_delta < 0) {
							absolute_delta =
								(int16_t)(old_yaw -
									  trig2_xyangle);
						}
						if ((uint16_t)turn_step >=
						    absolute_delta) {
							struct mobile_object *
								homing_mobile_object;
							uint16_t speed;

							object->yaw =
								trig2_xyangle;
#ifdef XVT_MODERN
							if (xvt_flight_timing_is_unlocked()) {
								xvt_flight_integration_clear(
									object_index,
									XVT_INTEGRATE_HOME_YAW);
							}
#endif
							homing_mobile_object =
								object->mobj;
							speed = homing_mobile_object
									->speed;
							if (guidance->cruise_speed >
							    speed) {
								homing_mobile_object
									->speed =
									(uint16_t)(speed +
#ifdef XVT_MODERN
										   (xvt_flight_timing_is_unlocked()
											    ? xvt_flight_integration_rate(
												      object_index,
												      XVT_INTEGRATE_HOME_SPEED,
												      g_projectile_homing_speed_adjust_rate_by_profile
													      [profile_index],
												      g_elapsed_ticks,
												      236)
											    : (g_elapsed_ticks *
											       g_projectile_homing_speed_adjust_rate_by_profile
												       [profile_index] /
											       SIMULATION_TICKS_PER_SECOND))
#else
										   g_elapsed_ticks *
											   g_projectile_homing_speed_adjust_rate_by_profile
												   [profile_index] /
											   SIMULATION_TICKS_PER_SECOND
#endif
									);
							}
						} else {
							if (yaw_delta < 0) {
								turn_step =
									-turn_step;
							}
							object->yaw =
								(int16_t)(old_yaw +
									  turn_step);
							{
								uint16_t *speed =
									&object->mobj
										 ->speed;

								if (*speed >
								    HOMING_MIN_TURN_SPEED) {
									*speed =
										(uint16_t)(*speed +
#ifdef XVT_MODERN
											   (xvt_flight_timing_is_unlocked()
												    ? xvt_flight_integration_rate(
													      object_index,
													      XVT_INTEGRATE_HOME_SPEED,
													      -g_projectile_homing_speed_adjust_rate_by_profile
														      [profile_index],
													      g_elapsed_ticks,
													      236)
												    : (g_elapsed_ticks *
												       g_projectile_homing_speed_adjust_rate_by_profile
													       [profile_index] /
												       -SIMULATION_TICKS_PER_SECOND))
#else
											   g_elapsed_ticks *
												   g_projectile_homing_speed_adjust_rate_by_profile
													   [profile_index] /
												   -SIMULATION_TICKS_PER_SECOND
#endif
										);
									speed = &object->mobj
											 ->speed;
									if (*speed <
									    HOMING_MIN_TURN_SPEED) {
										*speed =
											HOMING_MIN_TURN_SPEED;
									}
								}
							}
						}

						old_pitch = object->pitch;
						pitch_delta =
							(int16_t)(trig2_pitch -
								  old_pitch);
						absolute_delta = pitch_delta;
#ifdef XVT_MODERN
						if (xvt_flight_timing_is_unlocked()) {
							turn_step = xvt_flight_integration_rate(
								object_index,
								XVT_INTEGRATE_HOME_PITCH,
								g_projectile_homing_turn_rate_by_profile
									[profile_index],
								g_elapsed_ticks,
								236);
						} else
#endif
							turn_step =
								g_elapsed_ticks *
								g_projectile_homing_turn_rate_by_profile
									[profile_index] /
								SIMULATION_TICKS_PER_SECOND;
						if (pitch_delta < 0) {
							absolute_delta =
								(int16_t)(old_pitch -
									  trig2_pitch);
						}
						if ((uint16_t)turn_step >=
						    absolute_delta) {
							object->pitch =
								trig2_pitch;
#ifdef XVT_MODERN
							if (xvt_flight_timing_is_unlocked()) {
								xvt_flight_integration_clear(
									object_index,
									XVT_INTEGRATE_HOME_PITCH);
							}
#endif
						} else {
							if (pitch_delta < 0) {
								turn_step =
									-turn_step;
							}
							object->pitch =
								(int16_t)(old_pitch +
									  turn_step);
						}

						mobile_object
							->orient_matrix_dirty =
							1;
						mobile_object
							->move_vector_dirty =
							mobile_object
								->orient_matrix_dirty;
						fview_calcrotatemove(
							object->pitch,
							object->yaw, object);
						object->mobj->move_x = (int16_t)
							g_fview_move_x_q15;
						object->mobj->move_y = (int16_t)
							g_fview_move_y_q15;
						object->mobj->move_z = (int16_t)
							g_fview_move_z_q15;
					}
				}
			}

			mobile_object = object->mobj;
			if (mobile_object->move_vector_dirty != 0) {
				fview_calcrotatemove(object->pitch, object->yaw,
						     object);
				mobile_object = object->mobj;
			}
#ifdef XVT_MODERN
			if (xvt_flight_timing_is_unlocked()) {
				xvt_flight_integration_move(object_index);
			} else {
#endif

				trig2_xmovedist =
					math_mul_q15(mobile_object->move_x,
						     movement_distance);
				trig2_ymovedist =
					math_mul_q15(mobile_object->move_y,
						     movement_distance);
				trig2_zmovedist =
					math_mul_q15(mobile_object->move_z,
						     movement_distance);

#ifdef XVT_MODERN
			}
#endif

			object_add_trig_move_delta_and_clamp_world_position(
				(uint32_t *)object);
			break;
		}
		case CRAFT_GENUS_SMALL_DEBRIS:
		case CRAFT_GENUS_EXPLOSION: {
			if (mobile_object->move_vector_dirty != 0) {
				fview_calcrotatemove(object->pitch, object->yaw,
						     object);
				mobile_object = object->mobj;
			}
#ifdef XVT_MODERN
			if (xvt_flight_timing_is_unlocked()) {
				xvt_flight_integration_move(object_index);
			} else {
#endif

				trig2_xmovedist =
					math_mul_q15(mobile_object->move_x,
						     movement_distance);
				trig2_ymovedist =
					math_mul_q15(mobile_object->move_y,
						     movement_distance);
				trig2_zmovedist =
					math_mul_q15(mobile_object->move_z,
						     movement_distance);

#ifdef XVT_MODERN
			}
#endif

			object_add_trig_move_delta_and_clamp_world_position(
				(uint32_t *)object);
			break;
		}
		default:
			break;
		}
#ifdef XVT_MODERN
		xvt_reference_motion_committed(object_index,
					       g_game_time + g_elapsed_ticks);
#endif
	}

	g_sim_steps_per_second = (uint16_t)saved_sim_steps_per_second;
	g_elapsed_ticks = (uint16_t)saved_elapsed_ticks;
}

/* Adds trig2_xmovedist, trig2_ymovedist and trig2_zmovedist to an object's
 * world_x, world_y and world_z (object_words points at its object_record, read
 * as 32-bit words 1 to 3) and clamps each to 0x1000000 either way of 0.
 * Returns world_z after the lower clamp only, so a value above 0x1000000
 * comes back unclamped although the stored one is clamped; no caller uses
 * the result. */
// FUNCTION: XVT 0x4464C0
int object_add_trig_move_delta_and_clamp_world_position(uint32_t *object_words)
{
	int32_t result;

	object_words[1] += (uint32_t)trig2_xmovedist;
	if ((int32_t)object_words[1] < -0x01000000) {
		object_words[1] = (uint32_t)-0x01000000;
	}
	if ((int32_t)object_words[1] > 0x01000000) {
		object_words[1] = 0x01000000;
	}

	object_words[2] += (uint32_t)trig2_ymovedist;
	if ((int32_t)object_words[2] < -0x01000000) {
		object_words[2] = (uint32_t)-0x01000000;
	}
	if ((int32_t)object_words[2] > 0x01000000) {
		object_words[2] = 0x01000000;
	}

	object_words[3] += (uint32_t)trig2_zmovedist;
	if ((int32_t)object_words[3] < -0x01000000) {
		object_words[3] = (uint32_t)-0x01000000;
	}
	result = (int32_t)object_words[3];
	if (result > 0x01000000) {
		object_words[3] = 0x01000000;
	}
	return result;
}

/* Draws one object that is not a craft (debris, a mine, an effect, a
 * static object) for the current view, using the g_viewSpace* position and
 * object view matrix the caller set. With no texture frame sequence for its
 * type, it queues fuselage billboards (damage_queue_craft_billboards) and
 * draws the model, but only while type_specific_byte[0] is 0. Otherwise the
 * frame at type_specific_byte[0] in the sequence picks nothing (0xFF00 and
 * up), the model (below 0x8000), or a textured billboard of size 256 queued
 * at the projected point and turned to the object's roll on screen; the
 * billboard is skipped when the view depth is negative or the point falls
 * outside -65,536 to 65,535 on either axis. Writes
 * g_billboard_object_or_type_index. */
// FUNCTION: XVT 0x446550
void render_non_craft_scene_object(uint16_t object_index)
{
	enum {
		NONCRAFT_MODEL_FRAME_LIMIT = 0x8000,
		NONCRAFT_INVALID_FRAME_START = 0xFF00,
		NONCRAFT_SCREEN_COORD_HIGH_MASK = -65536,
		NONCRAFT_DEFAULT_SCREEN_SIZE = 256,
	};

	struct object_record *object;
	struct object_type_info *model_type;
	uint16_t rotation_angle;
	int16_t *texture_frame_sequence;
	uint16_t frame;
	int abs_r0z;
	int abs_r1z;
	int axis_x;
	int axis_y;
	int projected_x;
	int projected_x_high;
	int projected_y;
	int projected_y_high;
	int viewport_center;
	int screen_y;

	object = &g_object_table[object_index];
	model_type = &g_object_type_table[object->object_type];
	g_billboard_object_or_type_index = object_index;
	texture_frame_sequence = model_type->texture_frame_sequence;
	if (texture_frame_sequence == NULL) {
		if (object->type_specific_byte[0] != 0) {
			return;
		}
		damage_queue_craft_billboards(object_index);
		render_scene_draw_object_model(
			&g_object_table[g_billboard_object_or_type_index]);
		return;
	}

	frame = texture_frame_sequence[object->type_specific_byte[0]];
	if (frame >= NONCRAFT_INVALID_FRAME_START) {
		return;
	}
	if (frame < NONCRAFT_MODEL_FRAME_LIMIT) {
		render_scene_draw_object_model(object);
		return;
	}
	if (g_view_space_depth < 0) {
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
	projected_x_high = projected_x & NONCRAFT_SCREEN_COORD_HIGH_MASK;
	if (projected_x_high > 0 ||
	    projected_x_high < NONCRAFT_SCREEN_COORD_HIGH_MASK) {
		return;
	}
	projected_y =
		transfm2_project_screen_y(g_view_space_y, g_view_space_depth);
	projected_y_high = projected_y & NONCRAFT_SCREEN_COORD_HIGH_MASK;
	if (projected_y_high > 0 ||
	    projected_y_high < NONCRAFT_SCREEN_COORD_HIGH_MASK) {
		return;
	}
	viewport_center = g_flight_vp_height >> 1;
	projected_y -= viewport_center;
	screen_y = viewport_center - projected_y;
	scene_billboard_queue_projected_textured(
		g_billboard_object_or_type_index, frame,
		NONCRAFT_DEFAULT_SCREEN_SIZE, (int16_t)projected_x,
		(int16_t)screen_y, g_view_space_depth, rotation_angle);
}

/* Breaks a piece off the craft in source_object_index: a small-debris slot
 * takes the craft's position, angles and motion
 * (object_copy_state_preserving_storage) and becomes object type
 * CRAFT_SPECIES_COMPONENT, family 3, with no player owner, the craft's type
 * as source_object_type, twice mesh_index in type_specific_byte[0] (the mesh to
 * draw) and a life of 4 to 11 simulated seconds. Returns the new slot, or
 * UINT16_MAX when no debris slot is free. Does not check mesh_index. */
// FUNCTION: XVT 0x4591C0
uint16_t object_spawn_detached_component(uint16_t source_object_index,
					 int16_t mesh_index)
{
	uint16_t object_index;
	int object_offset_index;

	object_index = object_alloc_slot_for_genus(CRAFT_GENUS_SMALL_DEBRIS);
	if (object_index == UINT16_MAX) {
		return UINT16_MAX;
	}

	object_copy_state_preserving_storage(object_index, source_object_index);
	object_offset_index = object_index;
	g_object_table[object_offset_index].mobj->family = 3;
	g_object_table[object_offset_index].genus_id = CRAFT_GENUS_SMALL_DEBRIS;
	g_object_table[object_offset_index].mobj->effect_size = 0;
	g_object_table[object_offset_index].object_type =
		CRAFT_SPECIES_COMPONENT;
	g_object_table[object_offset_index].mobj->source_object_type =
		g_object_table[source_object_index].object_type;
	g_object_table[object_offset_index].player_owner_idx = -1;
	g_object_table[object_offset_index].mobj->seconds_alive = 0;
	g_object_table[object_offset_index].mobj->lifetime_timer =
		SIMULATION_TICKS_PER_SECOND * ((game_rand() & 7) + 4);
	g_object_table[object_offset_index].type_specific_byte[0] =
		(uint8_t)(mesh_index * 2);
	g_object_table[object_offset_index].type_specific_byte[1] = 0;

	return object_index;
}

/* Throws a fragment effect off source_obj_idx: an explosion slot takes the
 * source's state and becomes an explosion (family 5) of object type 133 or
 * 134 at random, with no player owner, its yaw and pitch each turned by a
 * random 0x100 to 0x8FF either way, its speed raised by 50 to 305, and a
 * life of 1 to 4 simulated seconds. Returns the new slot, or UINT16_MAX
 * when no explosion slot is free. */
// FUNCTION: XVT 0x4592D0
uint16_t object_spawn_effect_fragment(uint16_t source_obj_idx)
{
	uint16_t object_index;
	int object_offset_index;
	int16_t yaw_offset;
	int16_t pitch_offset;
	uint16_t *pitch;
	struct mobile_object *mobile_object;

	object_index = object_alloc_slot_for_genus(CRAFT_GENUS_EXPLOSION);
	if (object_index == UINT16_MAX) {
		return UINT16_MAX;
	}

	object_copy_state_preserving_storage(object_index, source_obj_idx);
	object_offset_index = object_index;
	g_object_table[object_offset_index].mobj->family = 5;
	g_object_table[object_offset_index].genus_id = CRAFT_GENUS_EXPLOSION;
	g_object_table[object_offset_index].mobj->effect_size = 0;
	g_object_table[object_offset_index].object_type =
		(uint8_t)((game_rand() & 1) - 123);
	g_object_table[object_offset_index].mobj->source_object_type =
		g_object_table[source_obj_idx].object_type;
	g_object_table[object_offset_index].player_owner_idx = -1;

	yaw_offset = (game_rand() & 0x7FF) + 0x100;
	pitch_offset = (game_rand() & 0x7FF) + 0x100;
	if ((game_rand() & 1) != 0) {
		yaw_offset = -yaw_offset;
	}
	if ((game_rand() & 1) != 0) {
		pitch_offset = -pitch_offset;
	}
	g_object_table[object_offset_index].yaw += yaw_offset;
	g_object_table[object_offset_index].pitch += pitch_offset;
	pitch = &g_object_table[object_offset_index].pitch;
	if (*pitch >= 0x8000) {
		*pitch = -*pitch;
		g_object_table[object_offset_index].yaw += 0x8000;
	}

	g_object_table[object_offset_index].mobj->orient_matrix_dirty = 1;
	g_object_table[object_offset_index].mobj->move_vector_dirty =
		g_object_table[object_offset_index].mobj->orient_matrix_dirty;
	mobile_object = g_object_table[object_offset_index].mobj;
	mobile_object->speed += (game_rand() & 0xFF) + 50;
	g_object_table[object_offset_index].mobj->seconds_alive = 0;
	g_object_table[object_offset_index].mobj->lifetime_timer =
		SIMULATION_TICKS_PER_SECOND * ((game_rand() & 3) + 1);
	g_object_table[object_offset_index].type_specific_byte[0] = 0;

	return object_index;
}

/* Like object_spawn_effect_fragment, but the fragment is object type 157
 * ((uint8_t)-99) with effect_size 2 and type_specific_byte[0] 2; it faces back
 * along the source (a half turn of yaw, pitch mirrored), turned by a random
 * 0x100 to 0x20FF either way on each angle, flies at speed 35 to 50 for 39 to
 * 42 ticks, and is moved at once by four times the distance one step at
 * g_sim_steps_per_second covers. Returns the new slot, or UINT16_MAX when no
 * explosion slot is free. Its only caller, flight_object_update_special_behavior,
 * makes three at a time for an active craft whose chaff is active. */
// FUNCTION: XVT 0x459480
uint16_t object_spawn_local_effect_fragment(uint16_t source_obj_idx)
{
	uint16_t object_index;
	int object_offset_index;
	int16_t yaw_offset;
	int16_t pitch_offset;
	struct object_record *object;
	uint16_t speed_per_frame;

	object_index = object_alloc_slot_for_genus(CRAFT_GENUS_EXPLOSION);
	if (object_index == UINT16_MAX) {
		return UINT16_MAX;
	}

	object_copy_state_preserving_storage(object_index, source_obj_idx);
	object_offset_index = object_index;
	g_object_table[object_offset_index].mobj->family = 5;
	g_object_table[object_offset_index].genus_id = CRAFT_GENUS_EXPLOSION;
	g_object_table[object_offset_index].mobj->effect_size = 2;
	g_object_table[object_offset_index].object_type = (uint8_t)-99;
	g_object_table[object_offset_index].mobj->source_object_type =
		g_object_table[source_obj_idx].object_type;
	g_object_table[object_offset_index].player_owner_idx = -1;

	yaw_offset = (game_rand() & 0x1FFF) + 0x100;
	pitch_offset = (game_rand() & 0x1FFF) + 0x100;
	if ((game_rand() & 1) != 0) {
		yaw_offset = -yaw_offset;
	}
	if ((game_rand() & 1) != 0) {
		pitch_offset = -pitch_offset;
	}
	g_object_table[object_offset_index].yaw += 0x8000;
	g_object_table[object_offset_index].yaw += yaw_offset;
	g_object_table[object_offset_index].pitch =
		(int16_t)((uint16_t)0x8000 -
			  g_object_table[object_offset_index].pitch);
	g_object_table[object_offset_index].pitch += pitch_offset;
	if (g_object_table[object_offset_index].pitch >= 0x8000) {
		g_object_table[object_offset_index].pitch =
			-g_object_table[object_offset_index].pitch;
		g_object_table[object_offset_index].yaw += 0x8000;
	}

	g_object_table[object_offset_index].mobj->orient_matrix_dirty = 1;
	g_object_table[object_offset_index].mobj->move_vector_dirty =
		g_object_table[object_offset_index].mobj->orient_matrix_dirty;
	g_object_table[object_offset_index].mobj->speed =
		(game_rand() & 0xF) + 35;
	g_object_table[object_offset_index].mobj->seconds_alive = 0;
	g_object_table[object_offset_index].mobj->lifetime_timer =
		(game_rand() & 3) + 39;
	g_object_table[object_offset_index].type_specific_byte[0] = 2;

	object = &g_object_table[object_offset_index];
	object->mobj->prev_world_x = object->world_x;
	object->mobj->prev_world_y = object->world_y;
	object->mobj->prev_world_z = object->world_z;
	speed_per_frame =
		math2_mphconvert(object->mobj->speed, g_sim_steps_per_second);
	if (speed_per_frame != 0) {
		int movement_speed;
		int z_move;

		if (object->mobj->move_vector_dirty != 0) {
			fview_calcrotatemove(object->pitch, object->yaw,
					     object);
		}
		movement_speed = speed_per_frame;
		trig2_xmovedist =
			math_mul_q15(object->mobj->move_x, movement_speed);
		trig2_ymovedist =
			math_mul_q15(object->mobj->move_y, movement_speed);
		z_move = math_mul_q15(object->mobj->move_z, movement_speed);
		trig2_xmovedist *= 4;
		trig2_ymovedist *= 4;
		trig2_zmovedist = 4 * z_move;
		object_add_trig_move_delta_and_clamp_world_position(
			(uint32_t *)object);
	}

	return object_index;
}

/* Finds the first free slot (objectType 0) in genus_id's range in
 * g_object_slot_range_by_genus, sets its mobj->source_obj_idx and effect_size to 0,
 * and resets its proximity lists (collide_reset_object_proximity_for_slot; the
 * modern build also calls xvt_flight_integration_reset_slot_and_motion). Returns
 * the slot, or UINT16_MAX when the range is full or empty. Does not check
 * genus_id. */
// FUNCTION: XVT 0x459750
uint16_t object_alloc_slot_for_genus(uint16_t genus_id)
{
	uint16_t end;
	uint16_t object_index;

	end = g_object_slot_range_by_genus[genus_id].end;
	object_index = g_object_slot_range_by_genus[genus_id].start;
	if (end > object_index) {
		for (;;) {
			if (g_object_table[object_index].object_type == 0) {
				g_object_table[object_index]
					.mobj->source_obj_idx = 0;
				g_object_table[object_index].mobj->effect_size =
					0;
				break;
			}
			++object_index;
			if (end <= object_index) {
				break;
			}
		}
	}

	if (end > object_index) {
		collide_reset_object_proximity_for_slot(object_index);
#ifdef XVT_MODERN
		xvt_flight_integration_reset_slot_and_motion(object_index);
#endif
		return object_index;
	}

	return UINT16_MAX;
}

/* Returns the first free static slot (objectType 0) of the
 * g_region_static_object_slot_count after g_region_main_object_slot_end, or
 * UINT16_MAX when all are used. */
// FUNCTION: XVT 0x4597F0
uint16_t object_find_free_mission_slot(void)
{
	uint16_t object_index;
	int main_object_slot_end;
	int mission_slot_end;

	object_index = (uint16_t)g_region_main_object_slot_end;
	main_object_slot_end = g_region_main_object_slot_end;
	mission_slot_end = g_region_static_object_slot_count;
	mission_slot_end += main_object_slot_end;
	while (object_index < mission_slot_end) {
		if (g_object_table[object_index].object_type == 0) {
			return object_index;
		}
		++object_index;
	}
	return UINT16_MAX;
}

/* Copies the object in src_obj_idx onto dst_obj_idx: the contents of its craft,
 * guidance and character records where both slots have one, its
 * mobile_object where both have one, and its object_record; the destination
 * keeps its own mobj, p_craft, p_warhead_guidance and p_char_data pointers. Then
 * resets the destination's proximity lists; the modern build first calls
 * xvt_flight_integration_reset_slot_and_motion. Does not check that the
 * destination has a mobile_object. */
// FUNCTION: XVT 0x459F30
void object_copy_state_preserving_storage(unsigned int dst_obj_idx,
					  unsigned int src_obj_idx)
{
	struct craft_data *destination_craft;
	struct craft_data *source_craft;
	struct warhead_guidance_state *destination_guidance;
	struct warhead_guidance_state *source_guidance;
	struct mobile_object_char_data *destination_char_data;
	struct mobile_object_char_data *source_char_data;
	struct mobile_object *destination_mobile_object;
	struct mobile_object *source_mobile_object;
	struct craft_data *preserved_craft;
	struct warhead_guidance_state *preserved_guidance;
	struct mobile_object_char_data *preserved_char_data;
	struct object_record *destination_object;
	struct object_record *source_object;

#ifdef XVT_MODERN
	xvt_flight_integration_reset_slot_and_motion(dst_obj_idx);
#endif

	destination_craft = g_object_table[dst_obj_idx].mobj->p_craft;
	if (destination_craft != NULL) {
		source_craft = g_object_table[src_obj_idx].mobj->p_craft;
		if (source_craft != NULL) {
			memcpy(destination_craft, source_craft,
			       sizeof(*destination_craft));
		}
	}

	destination_guidance =
		g_object_table[dst_obj_idx].mobj->p_warhead_guidance;
	if (destination_guidance != NULL) {
		source_guidance =
			g_object_table[src_obj_idx].mobj->p_warhead_guidance;
		if (source_guidance != NULL) {
			*destination_guidance = *source_guidance;
		}
	}

	destination_char_data = g_object_table[dst_obj_idx].mobj->p_char_data;
	if (destination_char_data != NULL) {
		source_char_data =
			g_object_table[src_obj_idx].mobj->p_char_data;
		if (source_char_data != NULL) {
			memcpy(destination_char_data, source_char_data,
			       sizeof(*destination_char_data));
		}
	}

	destination_mobile_object = g_object_table[dst_obj_idx].mobj;
	if (destination_mobile_object != NULL) {
		source_mobile_object = g_object_table[src_obj_idx].mobj;
		if (source_mobile_object != NULL) {
			preserved_craft = destination_mobile_object->p_craft;
			preserved_guidance =
				destination_mobile_object->p_warhead_guidance;
			preserved_char_data =
				destination_mobile_object->p_char_data;
			memcpy(destination_mobile_object, source_mobile_object,
			       sizeof(*destination_mobile_object));
			g_object_table[dst_obj_idx].mobj->p_craft =
				preserved_craft;
			g_object_table[dst_obj_idx].mobj->p_warhead_guidance =
				preserved_guidance;
			g_object_table[dst_obj_idx].mobj->p_char_data =
				preserved_char_data;
		}
	}

	destination_object = &g_object_table[dst_obj_idx];
	destination_mobile_object = destination_object->mobj;
	source_object = &g_object_table[src_obj_idx];
	memcpy(destination_object, source_object, sizeof(*destination_object));
	g_object_table[dst_obj_idx].mobj = destination_mobile_object;
	collide_reset_object_proximity_for_slot((uint16_t)dst_obj_idx);
}

/* Points mobj of each slot below g_region_main_object_slot_end at its entry in
 * g_mobile_object_pool_base. A second pass would point p_warhead_guidance,
 * p_craft or p_char_data at the pool entries named in
 * g_mobile_object_link_indices, but every index there is -1, so it links
 * nothing. */
// FUNCTION: XVT 0x45A0C0
void object_relink_mobile_object_pointers(void)
{
	int object_index;
	struct mobile_object_link_indices *link_indices;
	int linked_object_index;

	object_index = 0;
	if (g_region_main_object_slot_end > 0) {
		do {
			g_object_table[object_index].mobj =
				&g_mobile_object_pool_base[object_index];
			++object_index;
		} while (g_region_main_object_slot_end > object_index);
	}

	object_index = 0;
	if (g_region_main_object_slot_end > 0) {
		link_indices = g_mobile_object_link_indices;
		do {
			if (link_indices->warhead_guidance_idx != -1) {
				g_mobile_object_pool_base[object_index]
					.p_warhead_guidance =
					&g_projectile_guidance_states
						[link_indices
							 ->warhead_guidance_idx];
			} else if (link_indices->craft_data_idx != -1) {
				g_mobile_object_pool_base[object_index]
					.p_craft =
					&g_craft_data_pool_base
						[link_indices->craft_data_idx];
				/* Nothing in this build sets craft_data_idx, so this branch never runs. The table it reads
				 * holds the object type each slot was spawned with, not an object index. */
				linked_object_index =
					g_spawn_object_type_by_object_slot
						[link_indices->craft_data_idx];
				if (linked_object_index != -1) {
					g_mobile_object_pool_base[object_index]
						.p_craft
						->effective_ai_object_link =
						&g_object_table
							[linked_object_index];
				}
			} else if (link_indices->char_data_idx != -1) {
				g_mobile_object_pool_base[object_index]
					.p_char_data =
					&g_mobile_object_char_data_pool
						[link_indices->char_data_idx];
			}
			++link_indices;
			++object_index;
		} while (g_region_main_object_slot_end > object_index);
	}
}

/* Measures from the object in from_obj_idx to the center of mesh mesh_idx of
 * target_obj_idx, turned by the target's orientation: leaves the direction
 * and distance in trig2_ctop's outputs (trig2_xyangle, trig2_pitch,
 * trig2_polardistance) and returns collide_roughdistance3d of the offset.
 * Writes g_worldLoc* and g_rotated*. Does not check mesh_idx. */
// FUNCTION: XVT 0x4836E0
unsigned int object_direction_and_distance_to_mesh_center(
	uint16_t from_obj_idx, uint16_t target_obj_idx, unsigned int mesh_idx)
{
	/* Resolve, rotate, and measure the target mesh center. */
	struct object_record *from_object = &g_object_table[from_obj_idx];
	int origin_x = from_object->world_x;
	int origin_y = from_object->world_y;
	int origin_z = from_object->world_z;
	int object_type;
	int delta_x;
	int delta_y;
	int delta_z;
	int target_world_x;
	int target_world_y;
	int target_world_z;

	mission_resolve_object_or_mission_point_world_loc(target_obj_idx, 0);
	target_world_x = g_world_loc_x;
	target_world_y = g_world_loc_y;
	target_world_z = g_world_loc_z;
	object_type = g_object_table[target_obj_idx].object_type;
	g_rotated_x = model_mesh_get_center_x(object_type, mesh_idx);
	g_rotated_y = model_mesh_get_center_z(object_type, mesh_idx);
	g_rotated_z = -model_mesh_get_center_y(object_type, mesh_idx);
	pai_rotate_local_vector_to_world_scratch(
		&g_object_table[target_obj_idx], g_rotated_x, g_rotated_y,
		g_rotated_z);

	delta_x = g_rotated_x + target_world_x - origin_x;
	delta_y = g_rotated_y + target_world_y - origin_y;
	delta_z = g_rotated_z + target_world_z - origin_z;
	trig2_ctop(delta_x, delta_y, delta_z);
	return (unsigned int)collide_roughdistance3d(delta_x, delta_y, delta_z);
}

/* Returns 1 when obj_idx is a player's craft whose beam system works and is
 * on, is the decoy beam, and has output; else 0, including for UINT16_MAX,
 * slots past the craft slots, AI craft and objects with no craft record. */
// FUNCTION: XVT 0x484F80
uint8_t object_has_active_decoy_beam(uint16_t obj_idx)
{
	struct craft_data *craft;

	if (obj_idx == UINT16_MAX) {
		return 0;
	}
	if (obj_idx >= g_active_region_craft_object_slot_end) {
		return 0;
	}

	if (g_object_table[obj_idx].player_owner_idx == -1) {
		return 0;
	}

	craft = g_object_table[obj_idx].mobj->p_craft;
	if (craft == NULL) {
		return 0;
	}

	if ((craft->working_subsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) ==
		    0 ||
	    craft->beam_active == 0 || craft->beam_type_id != BEAM_TYPE_DECOY ||
	    craft->beam_output == 0) {
		return 0;
	}

	return 1;
}

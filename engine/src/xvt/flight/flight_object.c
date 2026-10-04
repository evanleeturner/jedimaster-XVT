#include "xvt/flight/flight_object.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#endif

#include <string.h>

#include "xvt/assets/model_mesh.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

/* Slot index flight_object_recycle_local_debris_near_player looks at next, from
 * g_local_transient_slot_start up to g_local_debris_slot_end, then back. Two
 * functions write it: that one, one slot per call, and
 * mission_init_flight_runtime_state, which sets it to the start. */
// GLOBAL: XVT 0x9A8C06
uint16_t g_local_debris_recycle_slot_cursor = 0;

/* Position in g_billboard_texture_frame_sequence: loaded from an object's
 * type_specific_byte[0], or for a craft's fuselage from the component_state entry
 * after its last mesh, stepped, then stored back. Three functions write it:
 * flight_object_update_special_behavior, flight_object_advance_texture_frame_sequence
 * and scene_billboard_draw_or_queue_object. */
// GLOBAL: XVT 0xA90A5E
uint16_t g_billboard_texture_sequence_index = 0;
/* Frame sequence of the object being animated or drawn: its type's
 * texture_frame_sequence, or g_fuselage_damage_texture_frame_sequence for a fuselage.
 * NULL when the type has none. Written by flight_object_update_special_behavior
 * and scene_billboard_draw_or_queue_object. */
// GLOBAL: XVT 0xA90A60
int16_t *g_billboard_texture_frame_sequence = NULL;

/* Animates objects. Every call, runs proving_grounds_update_course in the proving
 * grounds; the rest runs only when the global special_behavior_update_timer has
 * reached 0 (in the modern build also only on a reference step), and rearms it
 * to 29 ticks. It then walks the slots from g_active_region_object_slot_start
 * through the static slots: crew object types 100 to 105 tumble at rates set by
 * their slot index. A craft, starfighter through platform, steps its fuselage
 * damage animation once per fuselage mesh; while breaking up it spawns hull
 * explosions or knocks off components with fragments; with a working subsystem
 * and a pending plan other than nullpln, stationaryldrpln and stationaryflwpln
 * it aims each live rotating laser turret at its turret target or swings it
 * idly, and swings its communications and beam meshes; it moves X-wing and
 * B-wing S-foil meshes and, when none moved, ends the S-foil move with
 * IFMSG_128 or IFMSG_129; with chaff active it spawns three local fragments.
 * Small debris and explosions step their texture animation, except type 89,
 * which may spawn an effect fragment; objects without mobile data step theirs
 * too. Writes g_cur_craft, g_billboard_texture_frame_sequence and
 * g_billboard_texture_sequence_index. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4015B0
void flight_object_update_special_behavior(void)
{
	enum {
		SPECIAL_BEHAVIOR_UPDATE_TICKS = 29,
		FIRST_CREW_OBJECT_TYPE = 100,
		LAST_CREW_OBJECT_TYPE = 105,
		FIRST_DYNAMIC_MODEL_TYPE = 73,
		SPECIAL_FRAGMENT_OBJECT_TYPE = 89,
		TURRET_PROJECTILE_TYPE = 2,
		TURRET_IDLE_ROTATION_STEP = 4,
		TURRET_DIRECTION_TOGGLE_CHANCE = 0x600,
		SYSTEM_ROTATION_TOGGLE_CHANCE = 0x200,
		DAMAGE_FRAGMENT_CHANCE = 0x1800,
		SPECIAL_FRAGMENT_CHANCE = 0x800,
		S_FOIL_TRANSITION_ACTIVE = 1,
		S_FOIL_TRANSITION_CLOSING = 2,
		B_WING_OBJECT_TYPE = 4,
		X_WING_OBJECT_TYPE = 1,
		B_WING_MAX_ROTATION = 0x40,
		X_WING_UPPER_MAX_ROTATION = 12,
		X_WING_LOWER_MAX_ROTATION = 8,
	};

	uint16_t object_index;
#ifdef XVT_MODERN
	struct xvt_flight_clock animation_clock;
#endif

	if (g_flight_mission_state.proving_grounds_mode_active != 0) {
		proving_grounds_update_course();
	}
	if (g_flight_global_countdown_timers.special_behavior_update_timer != 0
#ifdef XVT_MODERN
	    || !xvt_flight_timing_reference_due()
#endif
	)
		return;

#ifdef XVT_MODERN
	xvt_flight_timing_animation_event();
	animation_clock = xvt_flight_timing_enter_reference();
#endif
	g_flight_global_countdown_timers.special_behavior_update_timer =
		SPECIAL_BEHAVIOR_UPDATE_TICKS;
	for (object_index = (uint16_t)g_active_region_object_slot_start;
	     object_index <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     ++object_index) {
		if (g_object_table[object_index].object_type >=
			    FIRST_CREW_OBJECT_TYPE &&
		    g_object_table[object_index].object_type <=
			    LAST_CREW_OBJECT_TYPE) {
			g_object_table[object_index].roll +=
				(int16_t)(SPECIAL_BEHAVIOR_UPDATE_TICKS *
					  ((object_index -
					    g_region_static_object_slot_count) >>
					   4) /
					  16);
			g_object_table[object_index].pitch +=
				(int16_t)(SPECIAL_BEHAVIOR_UPDATE_TICKS *
					  ((object_index -
					    g_region_static_object_slot_count) >>
					   3) /
					  32);
			g_object_table[object_index].yaw +=
				(int16_t)(SPECIAL_BEHAVIOR_UPDATE_TICKS *
					  (4 -
					   ((object_index -
					     g_region_static_object_slot_count) >>
					    4)) /
					  16);
		}

		if (g_object_table[object_index].mobj != NULL) {
			uint16_t object_type;
			struct craft_data *craft;

			object_type = g_object_table[object_index].object_type;
			if (object_type == 0) {
				continue;
			}
			g_billboard_texture_frame_sequence =
				g_object_type_table[object_type]
					.texture_frame_sequence;
			craft = g_object_table[object_index].mobj->p_craft;
			switch (g_object_table[object_index].genus_id) {
			case CRAFT_GENUS_STARFIGHTER:
			case CRAFT_GENUS_TRANSPORT:
			case CRAFT_GENUS_UTILITY_VEHICLE:
			case CRAFT_GENUS_FREIGHTER:
			case CRAFT_GENUS_STARSHIP:
			case CRAFT_GENUS_PLATFORM: {
				uint16_t mesh_count;
				int16_t s_foil_mesh_moved;
				int allow_system_rotation;
				uint16_t mesh_index;

				if (object_type < FIRST_DYNAMIC_MODEL_TYPE) {
					mesh_count =
						(uint16_t)g_object_type_mesh_cache
							[object_type]
								.mesh_count;
				} else {
					mesh_count = (uint16_t)
						model_mesh_get_object_type_mesh_count(
							object_type);
				}
				s_foil_mesh_moved = 0;
				g_cur_craft = g_object_table[object_index]
						      .mobj->p_craft;
				if (g_cur_craft->working_subsystems != 0) {
					const char *plan_name;

					plan_name =
						g_plan_table
							[g_cur_craft
								 ->ai_controller
								 .running_plan_id]
								.name;
					allow_system_rotation =
						strcmp(plan_name, "nullpln") !=
							0 &&
						strcmp(plan_name,
						       "stationaryldrpln") !=
							0 &&
						strcmp(plan_name,
						       "stationaryflwpln") != 0;
				} else {
					allow_system_rotation = 0;
				}

				if (allow_system_rotation != 0) {
					int weapon_slot_index;

					for (weapon_slot_index = 0;
					     weapon_slot_index <
					     g_cur_craft->laser_slot_count;
					     ++weapon_slot_index) {
						unsigned int turret_mesh_index;
						mesh_component_type
							turret_mesh_type;
						struct turret_target_state
							*turret_target;

						if (g_cur_craft
							    ->weapon_slots
								    [weapon_slot_index]
							    .projectile_type_id !=
						    TURRET_PROJECTILE_TYPE) {
							continue;
						}
						turret_mesh_index =
							g_model_defs[g_cur_craft
									     ->model_index]
								.weapon_hardpoints
									[weapon_slot_index]
								.mesh_idx;
						if (g_cur_craft->component_hp
							    [turret_mesh_index] ==
						    0) {
							continue;
						}
						if (object_type <
						    FIRST_DYNAMIC_MODEL_TYPE) {
							turret_mesh_type = model_mesh_get_cached_object_type_mesh_type(
								object_type,
								turret_mesh_index);
						} else {
							turret_mesh_type = model_mesh_get_object_type_mesh_type(
								object_type,
								turret_mesh_index);
						}
						if (turret_mesh_type !=
						    MESH_COMPONENT_21_ROTATING_LASR_TUR) {
							continue;
						}

						turret_target =
							&g_cur_craft->turret_target_states
								 [weapon_slot_index];
						if (turret_target
							    ->target_obj_idx !=
						    UINT16_MAX) {
							struct object_record
								*object;
							float *rotation_scale;
							int turret_side;
							int turret_forward;
							int turret_up;
							int target_along_axis_y;
							int target_along_axis_x;

							rotation_scale = model_mesh_get_rot_scale_data(
								object_type,
								turret_mesh_index);
							object =
								&g_object_table
									[object_index];
							mission_resolve_object_or_mission_point_world_loc(
								turret_target
									->target_obj_idx,
								0);
							g_world_loc_x -=
								object->world_x;
							g_world_loc_y -=
								object->world_y;
							g_world_loc_z -=
								object->world_z;
							if (object->mobj
								    ->orient_matrix_dirty !=
							    0) {
								fview_calcrotatemove(
									object->pitch,
									object->yaw,
									object);
								fview_calcrotateorient(
									object->roll,
									0,
									object);
							}
							turret_side = math_dot3q15(
								g_world_loc_x,
								g_world_loc_y,
								g_world_loc_z,
								object->mobj
									->cached_side_x,
								object->mobj
									->cached_side_y,
								object->mobj
									->cached_side_z);
							turret_forward = -math_dot3q15(
								g_world_loc_x,
								g_world_loc_y,
								g_world_loc_z,
								object->mobj
									->cached_fwd_x,
								object->mobj
									->cached_fwd_y,
								object->mobj
									->cached_fwd_z);
							turret_up = math_dot3q15(
								g_world_loc_x,
								g_world_loc_y,
								g_world_loc_z,
								object->mobj
									->cached_up_x,
								object->mobj
									->cached_up_y,
								object->mobj
									->cached_up_z);
							g_world_loc_x =
								turret_side -
								(int)rotation_scale
									[0];
							g_world_loc_y =
								turret_forward -
								(int)rotation_scale
									[1];
							g_world_loc_z =
								turret_up -
								(int)rotation_scale
									[2];
							(void)math_dot3q15(
								g_world_loc_x,
								g_world_loc_y,
								g_world_loc_z,
								(int)rotation_scale
									[3],
								(int)rotation_scale
									[4],
								(int)rotation_scale
									[5]);
							target_along_axis_x = math_dot3q15(
								g_world_loc_x,
								g_world_loc_y,
								g_world_loc_z,
								(int)rotation_scale
									[6],
								(int)rotation_scale
									[7],
								(int)rotation_scale
									[8]);
							target_along_axis_y = math_dot3q15(
								g_world_loc_x,
								g_world_loc_y,
								g_world_loc_z,
								(int)rotation_scale
									[9],
								(int)rotation_scale
									[10],
								(int)rotation_scale
									[11]);
							g_cur_craft->mesh_rotation
								[turret_mesh_index] =
								(uint8_t)((uint16_t)trig2_arctan(
										  target_along_axis_y,
										  target_along_axis_x) >>
									  8);
						} else {
							uint8_t rotation;

							rotation =
								g_cur_craft->mesh_rotation
									[turret_mesh_index];
							if ((rotation & 1) !=
							    0) {
								rotation +=
									TURRET_IDLE_ROTATION_STEP;
							} else {
								rotation -=
									TURRET_IDLE_ROTATION_STEP;
							}
							g_cur_craft->mesh_rotation
								[turret_mesh_index] =
								rotation;
							if ((uint16_t)
								    game_rand() <
							    TURRET_DIRECTION_TOGGLE_CHANCE) {
								g_cur_craft->mesh_rotation
									[turret_mesh_index] ^=
									1;
							}
						}
					}
				}

				for (mesh_index = 0; mesh_index < mesh_count;
				     ++mesh_index) {
					mesh_component_type mesh_type;

					if (object_type <
					    FIRST_DYNAMIC_MODEL_TYPE) {
						mesh_type =
							model_mesh_get_cached_object_type_mesh_type(
								object_type,
								mesh_index);
					} else {
						mesh_type =
							model_mesh_get_object_type_mesh_type(
								object_type,
								mesh_index);
					}
					if (mesh_type ==
					    MESH_COMPONENT_03_FUSELAGE) {
						uint8_t *animation_state;

						animation_state =
							&craft->component_state
								 [mesh_count];
						g_billboard_texture_frame_sequence =
							g_fuselage_damage_texture_frame_sequence;
						g_billboard_texture_sequence_index =
							*animation_state;
						flight_object_advance_texture_frame_sequence(
							object_index);
						*animation_state = (uint8_t)
							g_billboard_texture_sequence_index;
					}
					if (g_cur_craft->object_kind ==
					    CRAFT_OBJECT_KIND_BREAKING_UP) {
						if (model_mesh_has_fuselage(
							    object_type) == 0) {
							craft_spawn_main_hull_explosion_effects(
								object_index,
								0);
							mesh_index += 3;
						} else {
							craft_detach_damageable_component(
								object_index,
								0);
							if ((uint16_t)
								    game_rand() <
							    DAMAGE_FRAGMENT_CHANCE) {
								object_spawn_effect_fragment(
									object_index);
							}
						}
					}
					if (allow_system_rotation != 0 &&
					    (mesh_type ==
						     MESH_COMPONENT_11_COMM_SYS ||
					     mesh_type ==
						     MESH_COMPONENT_23_COMM_SYS ||
					     mesh_type ==
						     MESH_COMPONENT_12_BEAM_SYS ||
					     mesh_type ==
						     MESH_COMPONENT_24_BEAM_SYS ||
					     mesh_type ==
						     MESH_COMPONENT_13_COMM_SYS ||
					     mesh_type ==
						     MESH_COMPONENT_25_COMM_SYS)) {
						uint8_t rotation;

						rotation =
							g_cur_craft->mesh_rotation
								[mesh_index];
						if ((rotation & 1) != 0) {
							rotation +=
								TURRET_IDLE_ROTATION_STEP;
						} else {
							rotation -=
								TURRET_IDLE_ROTATION_STEP;
						}
						g_cur_craft->mesh_rotation
							[mesh_index] = rotation;
						if ((uint16_t)game_rand() <
						    SYSTEM_ROTATION_TOGGLE_CHANCE) {
							g_cur_craft->mesh_rotation
								[mesh_index] ^=
								1;
						}
					}
					if (mesh_type ==
						    MESH_COMPONENT_07_BRIDGE &&
					    object_type == B_WING_OBJECT_TYPE &&
					    (g_cur_craft->s_foil_state &
					     S_FOIL_TRANSITION_ACTIVE) != 0) {
						if ((g_cur_craft->s_foil_state &
						     S_FOIL_TRANSITION_CLOSING) !=
						    0) {
							if (g_cur_craft->mesh_rotation
								    [mesh_index] <
							    B_WING_MAX_ROTATION) {
								g_cur_craft->mesh_rotation
									[mesh_index] +=
									4;
							}
						} else if (
							g_cur_craft->mesh_rotation
								[mesh_index] !=
							0) {
							g_cur_craft->mesh_rotation
								[mesh_index] -=
								4;
							if (g_cur_craft->mesh_rotation
								    [mesh_index] >
							    0x80) {
								g_cur_craft->mesh_rotation
									[mesh_index] =
									0;
							}
						}
					}
					if (mesh_type ==
						    MESH_COMPONENT_20_ROTATING_WING &&
					    (g_cur_craft->s_foil_state &
					     S_FOIL_TRANSITION_ACTIVE) != 0) {
						if ((g_cur_craft->s_foil_state &
						     S_FOIL_TRANSITION_CLOSING) !=
						    0) {
							if (object_type ==
							    X_WING_OBJECT_TYPE) {
								int center_z;
								uint16_t
									max_rotation;

								center_z = model_mesh_get_center_z(
									object_type,
									mesh_index);
								max_rotation =
									X_WING_LOWER_MAX_ROTATION;
								if (center_z >=
								    0) {
									max_rotation =
										X_WING_UPPER_MAX_ROTATION;
								}
								if (g_cur_craft->mesh_rotation
									    [mesh_index] <
								    max_rotation) {
									++g_cur_craft
										  ->mesh_rotation
											  [mesh_index];
									s_foil_mesh_moved =
										1;
								}
							} else if (
								object_type ==
									B_WING_OBJECT_TYPE &&
								g_cur_craft->mesh_rotation
										[mesh_index] <
									B_WING_MAX_ROTATION) {
								g_cur_craft->mesh_rotation
									[mesh_index] +=
									4;
								s_foil_mesh_moved =
									1;
							}
						} else if (object_type ==
							   X_WING_OBJECT_TYPE) {
							if (g_cur_craft->mesh_rotation
								    [mesh_index] !=
							    0) {
								--g_cur_craft->mesh_rotation
									  [mesh_index];
								s_foil_mesh_moved =
									1;
							}
						} else if (
							object_type ==
								B_WING_OBJECT_TYPE &&
							g_cur_craft->mesh_rotation
									[mesh_index] !=
								0) {
							g_cur_craft->mesh_rotation
								[mesh_index] -=
								4;
							s_foil_mesh_moved = 1;
						}
					}
				}

				if ((g_cur_craft->s_foil_state &
				     S_FOIL_TRANSITION_ACTIVE) != 0) {
					if ((g_cur_craft->s_foil_state &
					     S_FOIL_TRANSITION_CLOSING) != 0) {
						if (s_foil_mesh_moved == 0) {
							g_cur_craft
								->s_foil_state =
								S_FOIL_TRANSITION_CLOSING;
							msg_emit_in_flight_message(
								IFMSG_129_S_FOILS_HAVE_REACHED_CLOSED_POSITION,
								g_object_table[object_index]
									.player_owner_idx);
						}
					} else if (s_foil_mesh_moved == 0) {
						g_cur_craft->s_foil_state = 0;
						msg_emit_in_flight_message(
							IFMSG_128_S_FOILS_HAVE_REACHED_OPEN_POSITION,
							g_object_table[object_index]
								.player_owner_idx);
					}
				}
				if (g_cur_craft->object_kind ==
					    CRAFT_OBJECT_KIND_ACTIVE &&
				    g_cur_craft->cm_type_id ==
					    COUNTERMEASURE_TYPE_CHAFF &&
				    g_cur_craft->chaff_active_seconds != 0) {
					object_spawn_local_effect_fragment(
						object_index);
					object_spawn_local_effect_fragment(
						object_index);
					object_spawn_local_effect_fragment(
						object_index);
				}
				break;
			}
			case CRAFT_GENUS_SMALL_DEBRIS:
			case CRAFT_GENUS_EXPLOSION:
				if (g_object_table[object_index].object_type ==
				    SPECIAL_FRAGMENT_OBJECT_TYPE) {
					g_object_table[object_index]
						.type_specific_byte[1] = 0;
					if ((uint16_t)game_rand() <
					    SPECIAL_FRAGMENT_CHANCE) {
						object_spawn_effect_fragment(
							object_index);
					}
				} else {
					g_billboard_texture_sequence_index =
						g_object_table[object_index]
							.type_specific_byte[0];
					flight_object_advance_texture_frame_sequence(
						object_index);
					g_object_table[object_index]
						.type_specific_byte
							[0] = (uint8_t)
						g_billboard_texture_sequence_index;
				}
				break;
			default:
				break;
			}
		} else {
			uint16_t object_type;

			object_type = g_object_table[object_index].object_type;
			if (object_type != 0) {
				g_billboard_texture_frame_sequence =
					g_object_type_table[object_type]
						.texture_frame_sequence;
				if (g_billboard_texture_frame_sequence !=
				    NULL) {
					g_billboard_texture_sequence_index =
						g_object_table[object_index]
							.type_specific_byte[0];
					flight_object_advance_texture_frame_sequence(
						object_index);
					g_object_table[object_index]
						.type_specific_byte
							[0] = (uint8_t)
						g_billboard_texture_sequence_index;
				}
			}
		}
	}
#ifdef XVT_MODERN
	xvt_flight_timing_restore_clock(animation_clock);
#endif
}

/* Steps g_billboard_texture_sequence_index one entry along
 * g_billboard_texture_frame_sequence and acts on the entry it lands on: -1 frees
 * the object (objectType 0, and for a craft slot its linked objects too); -3
 * starts over at index 0; -2 and any entry below 0xFF00 stay; any other entry
 * from 0xFF00 up sets the index to the entry plus 0x100. Does nothing when the
 * sequence is NULL. Only this file calls it. */
// FUNCTION: XVT 0x4021A0
void flight_object_advance_texture_frame_sequence(unsigned int object_idx)
{
	int16_t sequence_value;
	struct mobile_object *mobile_object;

	if (g_billboard_texture_frame_sequence == NULL) {
		return;
	}

	++g_billboard_texture_sequence_index;
	sequence_value = g_billboard_texture_frame_sequence
		[g_billboard_texture_sequence_index];
	if (sequence_value == -1) {
		g_object_table[object_idx].object_type = 0;
		if ((unsigned int)g_active_region_craft_object_slot_end >
		    object_idx) {
			mobile_object = g_object_table[object_idx].mobj;
			if (mobile_object->p_craft != NULL) {
				craft_free_linked_objects(
					mobile_object->p_craft);
			}
		}
	} else if (sequence_value == -3) {
		g_billboard_texture_sequence_index = 0;
	} else if ((uint16_t)sequence_value >= 0xFF00u &&
		   sequence_value != -2) {
		g_billboard_texture_sequence_index =
			(uint16_t)(sequence_value + 0x100);
	}
}

/* Runs a player's hyperspace jump, adding g_elapsed_ticks to
 * hyperspace_runtime.phase_elapsed_ticks first. A craft no longer active drops
 * hyperspace_phase to 0. Stage 1 turns the craft toward roll 0, yaw 0 and pitch
 * 0x4000 at 16 angle units per elapsed tick (roll at twice that) and slows it;
 * once there and 0x49C ticks into the stage it moves to stage 2 with
 * IFMSG_108_ENTERING_HYPERSPACE. Stage 2: on its first update, any craft or
 * static object within 0x40000 units ahead along world +Y and close enough
 * sideways aborts the jump (IFMSG_112, a warning sound, hyperspace_phase 0,
 * speed 10). Until 0x49C ticks it then speeds up in steps every 0xEC ticks and
 * moves along +Y. At 0x49C ticks the craft leaves: the outcome is recorded, the
 * player may score 40 times the craft's point value, the object and its links
 * are freed, the player's settings saved, and the player bound to another craft
 * of theirs, or with none left, ended. At any other hyperspace_phase it does
 * nothing more. */
// FUNCTION: XVT 0x402240
void flight_object_update_player_hyperspace_transition(int player_idx)
{
	enum {
		HYPERSPACE_PHASE_NONE = 0,
		HYPERSPACE_PHASE_ALIGN = 1,
		HYPERSPACE_PHASE_DEPART = 2,
		HYPERSPACE_PHASE_DURATION = 0x49C,
		HYPERSPACE_ACCELERATION_INTERVAL = 0xEC,
		HYPERSPACE_CLEARANCE_DISTANCE = 0x40000,
		HYPERSPACE_FORWARD_STEP = 224,
		HYPERSPACE_ABORT_SPEED = 10,
		HYPERSPACE_ALIGN_DECELERATION = 200,
		PLAYER_WAVE_MODE_PRESERVE = 1,
		UNLIMITED_WAVES = 99,
		MISSION_SCORE_POINT_SCALE = 40,
		ANGLE_HALF_TURN = 0x8000,
		ANGLE_FORWARD = 0x4000,
		ANGLE_REVERSE = 0xC000,
	};

	int object_idx;
	struct object_record *player_object;
	struct craft_data *craft;
	uint16_t tick_delta;
	unsigned int phase_elapsed_ticks;
	int hyperspace_phase;

	object_idx = g_players[player_idx].object_index;
	player_object = &g_object_table[object_idx];
	craft = player_object->mobj->p_craft;
	if (craft->object_kind != CRAFT_OBJECT_KIND_ACTIVE) {
		g_players[player_idx].hyperspace_phase = HYPERSPACE_PHASE_NONE;
	}
	tick_delta = g_elapsed_ticks;
	phase_elapsed_ticks =
		g_players[player_idx].hyperspace_runtime.phase_elapsed_ticks +
		tick_delta;
	g_players[player_idx].hyperspace_runtime.phase_elapsed_ticks =
		phase_elapsed_ticks;
	hyperspace_phase = g_players[player_idx].hyperspace_phase;

	switch (hyperspace_phase) {
	case HYPERSPACE_PHASE_ALIGN: {
		int16_t yaw;
		int16_t roll;
		int16_t pitch;

		roll = player_object->roll;
		yaw = player_object->yaw;
		pitch = player_object->pitch;
		if (roll == 0 && pitch == ANGLE_FORWARD && yaw == 0) {
			if (phase_elapsed_ticks >= HYPERSPACE_PHASE_DURATION) {
				g_players[player_idx].hyperspace_phase =
					HYPERSPACE_PHASE_DEPART;
				msg_emit_in_flight_message(
					IFMSG_108_ENTERING_HYPERSPACE,
					player_idx);
				g_players[player_idx]
					.hyperspace_runtime
					.phase_elapsed_ticks = 0;
			}
		} else {
			int16_t angle_step;

			angle_step = (int16_t)(16 * g_elapsed_ticks);
			if ((uint16_t)roll < ANGLE_HALF_TURN) {
				roll = (int16_t)(roll - 2 * angle_step);
				if ((uint16_t)roll >= ANGLE_HALF_TURN) {
					roll = 0;
				}
			} else {
				roll = (int16_t)(roll + 2 * angle_step);
				if ((uint16_t)roll < ANGLE_HALF_TURN) {
					roll = 0;
				}
			}
			if ((uint16_t)yaw < ANGLE_HALF_TURN) {
				yaw = (int16_t)(yaw - angle_step);
				if ((uint16_t)yaw >= ANGLE_HALF_TURN) {
					yaw = 0;
				}
			} else {
				yaw = (int16_t)(yaw + angle_step);
				if ((uint16_t)yaw < ANGLE_HALF_TURN) {
					yaw = 0;
				}
			}
			if ((uint16_t)pitch >= ANGLE_REVERSE ||
			    (uint16_t)pitch <= ANGLE_FORWARD) {
				if ((uint16_t)pitch != ANGLE_FORWARD) {
					pitch = (int16_t)(pitch + angle_step);
					if ((uint16_t)pitch > ANGLE_FORWARD &&
					    (uint16_t)pitch < ANGLE_REVERSE) {
						pitch = ANGLE_FORWARD;
					}
				}
			} else {
				pitch = (int16_t)(pitch - angle_step);
				if ((uint16_t)pitch < ANGLE_FORWARD) {
					pitch = ANGLE_FORWARD;
				}
			}
		}
		g_object_table[object_idx].roll = roll;
		g_object_table[object_idx].yaw = yaw;
		g_object_table[object_idx].pitch = pitch;
		g_object_table[object_idx].mobj->orient_matrix_dirty = 1;
		g_object_table[object_idx].mobj->move_vector_dirty =
			g_object_table[object_idx].mobj->orient_matrix_dirty;
		g_object_table[object_idx].mobj->p_craft->pitch =
			(uint16_t)pitch;
		flight_decelerate_object_speed(object_idx,
					       HYPERSPACE_ALIGN_DECELERATION);
		return;
	}
	case HYPERSPACE_PHASE_DEPART:
		break;
	default:
		return;
	}

	{
		if (phase_elapsed_ticks < HYPERSPACE_PHASE_DURATION) {
			int16_t obstruction_detected;
			int candidate_object_type;

			obstruction_detected = 0;
			object_idx = g_players[player_idx].object_index;
			if (phase_elapsed_ticks <= tick_delta) {
				int16_t candidate_idx;

				for (candidate_idx = (int16_t)
					     g_active_region_object_slot_start;
				     candidate_idx <
				     g_active_region_craft_object_slot_end;
				     ++candidate_idx) {
					struct object_record *candidate;
					struct object_record
						*player_craft_object;
					int delta_x;
					int delta_y;
					int delta_z;
					int candidate_extent;
					int player_extent;

					candidate =
						&g_object_table[candidate_idx];
					if (candidate->object_type != 0) {
						player_craft_object =
							&g_object_table
								[object_idx];
						delta_x = candidate->world_x -
							  player_craft_object
								  ->world_x;
						delta_y = candidate->world_y -
							  player_craft_object
								  ->world_y;
						delta_z = candidate->world_z -
							  player_craft_object
								  ->world_z;
						if (delta_x < 0) {
							delta_x = -delta_x;
						}
						if (delta_z < 0) {
							delta_z = -delta_z;
						}
						candidate_object_type =
							candidate->object_type;
						candidate_extent =
							g_object_type_table
								[candidate_object_type]
									.max_bounds_extent;
						delta_x -= candidate_extent;
						delta_z -= candidate_extent;
						player_extent =
							g_object_type_table
								[player_craft_object
									 ->object_type]
									.max_bounds_extent;
						if (player_extent > delta_x &&
						    player_extent > delta_z &&
						    delta_y > 0 &&
						    delta_y <
							    HYPERSPACE_CLEARANCE_DISTANCE) {
							obstruction_detected =
								1;
							break;
						}
					}
				}
				if (obstruction_detected == 0) {
					int static_object_slot_end;

					candidate_idx = (int16_t)
						g_region_main_object_slot_end;
					static_object_slot_end =
						g_region_main_object_slot_end +
						g_region_static_object_slot_count;
					for (; candidate_idx <
					       static_object_slot_end;
					     ++candidate_idx) {
						struct object_record *candidate;
						struct object_record
							*player_craft_object;
						int delta_x;
						int delta_y;
						int delta_z;
						int candidate_extent;
						int player_extent;

						candidate =
							&g_object_table
								[candidate_idx];
						if (candidate->object_type !=
						    0) {
							player_craft_object =
								&g_object_table
									[object_idx];
							delta_x =
								candidate
									->world_x -
								player_craft_object
									->world_x;
							delta_y =
								candidate
									->world_y -
								player_craft_object
									->world_y;
							delta_z =
								candidate
									->world_z -
								player_craft_object
									->world_z;
							if (delta_x < 0) {
								delta_x =
									-delta_x;
							}
							if (delta_z < 0) {
								delta_z =
									-delta_z;
							}
							candidate_object_type =
								candidate
									->object_type;
							candidate_extent =
								g_object_type_table
									[candidate_object_type]
										.max_bounds_extent;
							delta_x -=
								candidate_extent;
							delta_z -=
								candidate_extent;
							player_extent =
								g_object_type_table
									[player_craft_object
										 ->object_type]
										.max_bounds_extent;
							if (player_extent >
								    delta_x &&
							    player_extent >
								    delta_z &&
							    delta_y > 0 &&
							    delta_y <
								    HYPERSPACE_CLEARANCE_DISTANCE) {
								obstruction_detected =
									1;
								break;
							}
						}
					}
				}
			}

			if (obstruction_detected != 0) {
				int current_object_idx;
				uint8_t object_type;
				int use_rebel_craft_sound;

				msg_emit_in_flight_message(
					IFMSG_112_OBJECT_DETECTED_IN_JUMP_PATH_HYPERSPACE_JUMP_ABORTED,
					player_idx);
				current_object_idx =
					g_players[player_idx].object_index;
				use_rebel_craft_sound = 0;
				if (current_object_idx != -1) {
					object_type =
						g_object_table
							[current_object_idx]
								.object_type;
					if (object_type ==
						    CRAFT_SPECIES_X_WING ||
					    object_type ==
						    CRAFT_SPECIES_Y_WING ||
					    object_type ==
						    CRAFT_SPECIES_A_WING ||
					    object_type ==
						    CRAFT_SPECIES_Z_95_HEADHUNTER ||
					    object_type ==
						    CRAFT_SPECIES_B_WING) {
						use_rebel_craft_sound = 1;
					}
				}
				if (use_rebel_craft_sound != 0) {
					fsfx_play_sound(FLIGHT_SOUND_R2_WARNING,
							-1, player_idx);
				} else {
					fsfx_play_sound(
						FLIGHT_SOUND_GENERAL_WARNING,
						-1, player_idx);
				}
				g_players[player_idx].hyperspace_phase =
					HYPERSPACE_PHASE_NONE;
				g_object_table[object_idx].mobj->speed =
					HYPERSPACE_ABORT_SPEED;
				return;
			}

			{
				uint16_t acceleration_stage;

				acceleration_stage =
					(uint16_t)(phase_elapsed_ticks /
						   HYPERSPACE_ACCELERATION_INTERVAL);
				if (acceleration_stage == 0) {
					flight_accelerate_object_speed(
						object_idx, 10);
				} else if (acceleration_stage == 1) {
					flight_accelerate_object_speed(
						object_idx, 25);
				} else if (acceleration_stage == 2) {
					flight_accelerate_object_speed(
						object_idx, 50);
				} else if (acceleration_stage == 3) {
					flight_accelerate_object_speed(
						object_idx, 100);
				} else {
					flight_accelerate_object_speed(
						object_idx, 1000);
				}
				g_object_table[object_idx].world_y +=
					HYPERSPACE_FORWARD_STEP *
					g_elapsed_ticks * acceleration_stage;
			}
			return;
		}

		{
			int flight_group_idx;

			flight_group_idx = player_object->flight_group_idx;
			mission_record_craft_outcome(
				(uint16_t)object_idx,
				(uint16_t)flight_group_idx,
				FLIGHT_GROUP_OUTCOME_LEFT_REGION);
			if (g_mission_header.mission_type !=
				    MISSION_TYPE_MELEE &&
			    g_flight_mission_state
					    .player_flight_group_wave_mode ==
				    PLAYER_WAVE_MODE_PRESERVE &&
			    g_mission_flight_groups[flight_group_idx]
					    .fg.number_of_waves !=
				    UNLIMITED_WAVES) {
				model_index model_index;

				model_index = get_model_index_from_type(
					g_object_table[object_idx].object_type);
				g_players[player_idx]
					.mission_stats.mission_score +=
					MISSION_SCORE_POINT_SCALE *
					g_model_defs[model_index]
						.craft_point_value;
			}
			if (g_players[player_idx].object_index != -1) {
				fsfx_update_beam_system_loop(0, player_idx);
				fsfx_update_incoming_missile_warning(0);
				fsfx_stop_hyperspace_exit_sounds(player_idx);
			}
			g_object_table[object_idx].object_type = 0;
			player_save_craft_settings(player_idx);
			craft_free_linked_objects(craft);
			g_players[player_idx].hyperspace_phase =
				HYPERSPACE_PHASE_NONE;
			mission_process_flight_group_wave_completion(
				flight_group_idx);
			if (player_bind_to_available_craft(
				    player_idx, UINT32_MAX, 0, 0) != 0) {
				player_end_flight_participation(player_idx);
				player_emit_remote_player_departed_messages(
					player_idx);
			} else if (player_idx == g_local_player) {
				msg_emit_local_player_craft_message(
					IFMSG_291_PREVIOUS_CRAFT_HYPERSPACED_NOW_PILOTING_ARG_ARG_ARG);
			}
		}
	}
}

/* Looks at one slot of the local debris range per call, advancing
 * g_local_debris_recycle_slot_cursor. When that slot lies more than 0x800 units
 * (rough distance) from the local player's craft, it turns the slot into a
 * fresh small debris object (type 110 to 113, genus 11) placed near the craft:
 * random amounts from -512 to 511 along its side and up axes, plus its forward
 * axis / 16. Does nothing when the local player has no craft; does not check
 * whether the slot held an object. */
// FUNCTION: XVT 0x459850
void flight_object_recycle_local_debris_near_player(void)
{
	int object_index;
	uint16_t debris_index;
	int delta_x;
	int delta_y;
	int delta_z;
	int16_t random_offset;
	struct object_record *player_object;
	struct mobile_object *mobile_object;
	int world_z;
	int16_t offset_x;
	int16_t offset_y;
	int16_t offset_z;

	object_index = g_players[g_local_player].object_index;
	if (object_index == -1) {
		return;
	}
	debris_index = g_local_debris_recycle_slot_cursor++;
	if (g_local_debris_recycle_slot_cursor == g_local_debris_slot_end) {
		g_local_debris_recycle_slot_cursor =
			g_local_transient_slot_start;
	}
	delta_x = g_object_table[debris_index].world_x -
		  g_object_table[object_index].world_x;
	delta_y = g_object_table[debris_index].world_y -
		  g_object_table[object_index].world_y;
	delta_z = g_object_table[debris_index].world_z -
		  g_object_table[object_index].world_z;
	if (delta_x < 0) {
		delta_x = -delta_x;
	}
	if (delta_y < 0) {
		delta_y = -delta_y;
	}
	if (delta_z < 0) {
		delta_z = -delta_z;
	}
	if (collide_roughdistance3du((unsigned int)delta_x,
				     (unsigned int)delta_y,
				     (unsigned int)delta_z) > 0x800) {
#ifdef XVT_MODERN
		xvt_flight_integration_reset_slot_and_motion(debris_index);
#endif
		g_object_table[debris_index].object_type =
			(game_rand2() & 3) + 110;
		g_object_table[debris_index].genus_id = 11;
		g_object_table[debris_index].mobj->family = 3;
		g_object_table[debris_index].flight_group_idx = -1;
		if (g_object_table[object_index].mobj->orient_matrix_dirty !=
		    0) {
			fview_calcrotatemove(g_object_table[object_index].pitch,
					     g_object_table[object_index].yaw,
					     &g_object_table[object_index]);
			fview_calcrotateorient(
				g_object_table[object_index].roll, 0,
				&g_object_table[object_index]);
		}
		random_offset = (int16_t)((game_rand2() & 0x3FF) - 512);
		offset_x = (int16_t)math_mul_q15(
			random_offset,
			g_object_table[object_index].mobj->cached_side_x);
		offset_y = (int16_t)math_mul_q15(
			random_offset,
			g_object_table[object_index].mobj->cached_side_y);
		offset_z = (int16_t)math_mul_q15(
			random_offset,
			g_object_table[object_index].mobj->cached_side_z);
		random_offset = (int16_t)((game_rand2() & 0x3FF) - 512);
		player_object = &g_object_table[object_index];
		offset_x += (int16_t)math_mul_q15(
			random_offset, player_object->mobj->cached_up_x);
		offset_y += (int16_t)math_mul_q15(
			random_offset, player_object->mobj->cached_up_y);
		offset_z += (int16_t)math_mul_q15(
			random_offset, player_object->mobj->cached_up_z);
		mobile_object = player_object->mobj;
		offset_x += mobile_object->cached_fwd_x >> 4;
		offset_y += mobile_object->cached_fwd_y >> 4;
		world_z = player_object->world_z;
		offset_z += mobile_object->cached_fwd_z >> 4;
		g_object_table[debris_index].world_x =
			player_object->world_x + offset_x;
		g_object_table[debris_index].world_y =
			player_object->world_y + offset_y;
		g_object_table[debris_index].world_z = world_z + offset_z;
		g_object_table[debris_index].type_specific_byte[0] = 2;
	}
}

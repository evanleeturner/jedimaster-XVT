#include "xvt/flight/object/static.h"

#include "xvt/assets/object_type.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Tests whether a moving object, swept from g_collisionSegmentStartWorld* to
 * g_collisionProbeWorld*, hits the static object in slot static_obj_idx (one
 * of the slots past g_region_main_object_slot_end, which have no mobile record).
 * Returns 0 for no hit, else the finer test's result: the 1-based mesh
 * ordinal from collide_check_swept_model_collision when the static's
 * max_bounds_extent is LARGE_MODEL_EXTENT (1,095) or more or it is a Container
 * Class H, else 0xFFFF from the box test of collide_checkboxcollision.
 * Returns 0 early when the moving object's source slot (mobj->source_obj_idx)
 * is not below static_obj_idx; when the static is an obstacle or small debris;
 * when the static is not normal debris and the source is an AI craft (no
 * player owner) whose target is not this static; when the static lies more
 * than MAX_DISTANCE (0x20000) world units away on an axis or by
 * collide_roughdistance3du; and when the sweep plus the hit radius cannot
 * reach it. Writes g_collisionSweepStart* and g_collisionSweepEnd* (both set
 * to the static's position), g_worldLoc* through
 * mission_resolve_object_or_mission_point_world_loc, and g_collisionHitOffset* on
 * a hit. Does not check that static_obj_idx holds an object. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x446700
uint16_t static_test_swept_static_collision(uint16_t source_obj_idx,
					    uint16_t static_obj_idx)
{
	enum { MAX_DISTANCE = 0x20000, LARGE_MODEL_EXTENT = 1095 };

	int source_source_obj_idx =
		(uint16_t)g_object_table[source_obj_idx].mobj->source_obj_idx;
	if (source_source_obj_idx >= static_obj_idx) {
		return 0;
	}

	unsigned int static_genus_id = g_object_table[static_obj_idx].genus_id;
	if (static_genus_id == CRAFT_GENUS_OBSTACLE) {
		return 0;
	}
	if (static_genus_id == CRAFT_GENUS_SMALL_DEBRIS) {
		return 0;
	}
	if (static_genus_id != CRAFT_GENUS_NORMAL_DEBRIS &&
	    g_active_region_craft_object_slot_end >
		    (int)source_source_obj_idx) {
		if (g_object_table[source_source_obj_idx].player_owner_idx ==
			    -1 &&
		    g_object_table[source_source_obj_idx]
				    .mobj->p_craft->ai_controller
				    .target_obj_idx != static_obj_idx) {
			return 0;
		}
	}

	int static_object_type = g_object_table[static_obj_idx].object_type;
	mission_resolve_object_or_mission_point_world_loc(static_obj_idx, 0);
	g_collision_sweep_start_x = g_world_loc_x;
	g_collision_sweep_end_x = g_world_loc_x;
	g_collision_sweep_start_y = g_world_loc_y;
	g_collision_sweep_end_y = g_world_loc_y;
	g_collision_sweep_start_z = g_world_loc_z;
	g_collision_sweep_end_z = g_world_loc_z;

	int hit_radius =
		g_object_type_table[static_object_type].max_bounds_extent;
	int dx = g_collision_probe_world_x - g_world_loc_x;
	if (dx < 0) {
		dx = -dx;
	}
	if (dx > MAX_DISTANCE) {
		return 0;
	}
	int dy = g_collision_probe_world_y - g_world_loc_y;
	if (dy < 0) {
		dy = -dy;
	}
	if (dy > MAX_DISTANCE) {
		return 0;
	}
	int dz = g_collision_probe_world_z - g_world_loc_z;
	if (dz < 0) {
		dz = -dz;
	}
	if (dz > MAX_DISTANCE) {
		return 0;
	}

	unsigned int source_to_static_distance = collide_roughdistance3du(
		(unsigned int)dx, (unsigned int)dy, (unsigned int)dz);
	if ((int)source_to_static_distance > MAX_DISTANCE) {
		return 0;
	}

	dx = g_collision_probe_world_x - g_collision_segment_start_world_x;
	if (dx < 0) {
		dx = -dx;
	}
	dy = g_collision_probe_world_y - g_collision_segment_start_world_y;
	if (dy < 0) {
		dy = -dy;
	}
	dz = g_collision_probe_world_z - g_collision_segment_start_world_z;
	if (dz < 0) {
		dz = -dz;
	}

	int static_sweep_abs =
		g_collision_sweep_end_x - g_collision_sweep_start_x;
	if (static_sweep_abs < 0) {
		static_sweep_abs = -static_sweep_abs;
	}
	dx += static_sweep_abs;
	static_sweep_abs = g_collision_sweep_end_y - g_collision_sweep_start_y;
	if (static_sweep_abs < 0) {
		static_sweep_abs = -static_sweep_abs;
	}
	dy += static_sweep_abs;
	static_sweep_abs = g_collision_sweep_end_z - g_collision_sweep_start_z;
	if (static_sweep_abs < 0) {
		static_sweep_abs = -static_sweep_abs;
	}
	dz += static_sweep_abs;

	dx += hit_radius;
	dy += hit_radius;
	dz += hit_radius;
	if (collide_roughdistance3du((unsigned int)dx, (unsigned int)dy,
				     (unsigned int)dz) <
	    source_to_static_distance) {
		return 0;
	}

	if (hit_radius >= LARGE_MODEL_EXTENT ||
	    g_object_table[static_obj_idx].object_type ==
		    CRAFT_SPECIES_CONTAINER_CLASS_H) {
		return (uint16_t)collide_check_swept_model_collision(
			source_obj_idx, static_obj_idx);
	}
	hit_radius >>= 2;
	return (uint16_t)collide_checkboxcollision(hit_radius +
						   (hit_radius >> 1));
}

/* Resolves a hit by source_obj_idx, a craft or a projectile, on the static
 * object victim_obj_idx and leaves an impact effect at the hit point. Normal
 * debris survives, and the effect is EFFECT_TYPE_LASER_IMPACT (0x83) or, for
 * an ion shot, EFFECT_TYPE_ION_IMPACT (0x84). A craft destroys any other
 * static: it adds a destroyed outcome to the victim's flight group in
 * g_mission_fg_stats, empties the victim's slot (objectType 0) and credits the
 * craft through mission_credit_destruction_damage_contributors. An ion shot
 * disables the victim instead (type_specific_word 0, a disabled outcome); any
 * other shot destroys it as a craft does, crediting the shot's source, and a
 * Mine Type C first fires a warhead back at that source through
 * laser_createprojectilefromstatic. The effect takes a new explosion slot
 * for a craft source (when none is free there is no effect) and takes over
 * the projectile's own slot otherwise; it is placed at
 * g_collisionSegmentStartWorld* plus g_collisionHitOffset*, is
 * EFFECT_TYPE_DEFAULT (0x81) for a destroyed victim or a warhead, and plays
 * a laser-impact sound or one of four small-explosion sounds. The source
 * craft takes no damage here. */
// FUNCTION: XVT 0x446960
void static_apply_static_hit(uint16_t source_obj_idx, int victim_obj_idx)
{
	enum {
		EFFECT_TYPE_DEFAULT = 0x81,
		EFFECT_TYPE_LASER_IMPACT = 0x83,
		EFFECT_TYPE_ION_IMPACT = 0x84,
		TEMPORARY_PROJECTILE_OBJECT_TYPE =
			PROJECTILE_OBJECT_TYPE_REBEL_LASER,
		RANDOM_IMPACT_SOUND_COUNT = 4,
		EXPLOSION_FAMILY = 5,
		IMPACT_VARIANT = 2,
	};

	uint16_t effect_type;
	uint16_t victim_index = (uint16_t)victim_obj_idx;
	XVT_LOG_DEBUG(
		"combat.object_hit object=%d type=%d genus=%d fg=%d source=%d source_type=%d predicted=%d",
		victim_obj_idx, (int)g_object_table[victim_index].object_type,
		(int)g_object_table[victim_index].genus_id,
		(int)g_object_table[victim_index].flight_group_idx,
		(int)source_obj_idx,
		(int)g_object_table[source_obj_idx].object_type,
		g_flight_sim_side_effects_suppressed);

	if (g_object_table[victim_index].genus_id ==
	    CRAFT_GENUS_NORMAL_DEBRIS) {
		if (g_object_table[source_obj_idx].object_type !=
			    PROJECTILE_OBJECT_TYPE_ION_LASER &&
		    g_object_table[source_obj_idx].object_type !=
			    PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
			effect_type = EFFECT_TYPE_LASER_IMPACT;
		} else {
			effect_type = EFFECT_TYPE_ION_IMPACT;
		}
	} else if (g_active_region_craft_object_slot_end > source_obj_idx) {
		++g_mission_fg_stats[g_object_table[victim_index]
					     .flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED];
		effect_type = EFFECT_TYPE_DEFAULT;
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"combat.object_destroyed object=%d fg=%d type=%d attacker=%d slot=%d tick=%d",
				victim_obj_idx,
				(int)g_object_table[victim_index]
					.flight_group_idx,
				(int)g_object_table[victim_index].object_type,
				(int)source_obj_idx,
				g_object_table[source_obj_idx].player_owner_idx,
				g_game_time);
			XVT_LOG_INFO(
				"battle.killing_blow victim=%d by=%d attacker=%d weapon=\"%s\" type=%d tick=%d",
				victim_obj_idx,
				g_object_table[source_obj_idx].player_owner_idx,
				(int)source_obj_idx, "collision",
				(int)g_object_table[source_obj_idx].object_type,
				g_game_time);
		}
		g_object_table[victim_index].object_type = 0;
		mission_credit_destruction_damage_contributors(source_obj_idx,
							       victim_obj_idx);
	} else {
		if (g_object_table[source_obj_idx].object_type ==
			    PROJECTILE_OBJECT_TYPE_ION_LASER ||
		    g_object_table[source_obj_idx].object_type ==
			    PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
			g_object_table[victim_index].type_specific_word = 0;
			++g_mission_fg_stats[g_object_table[victim_index]
						     .flight_group_idx]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_DISABLED];
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"combat.object_disabled object=%d fg=%d type=%d attacker=%d slot=%d tick=%d",
					victim_obj_idx,
					(int)g_object_table[victim_index]
						.flight_group_idx,
					(int)g_object_table[victim_index]
						.object_type,
					(int)g_object_table[source_obj_idx]
						.mobj->source_obj_idx,
					g_object_table
						[g_object_table[source_obj_idx]
							 .mobj->source_obj_idx]
							.player_owner_idx,
					g_game_time);
			}
			effect_type = EFFECT_TYPE_ION_IMPACT;
		} else {
			++g_mission_fg_stats[g_object_table[victim_index]
						     .flight_group_idx]
				  .outcome_count
					  [FLIGHT_GROUP_OUTCOME_DESTROYED];
			effect_type = EFFECT_TYPE_DEFAULT;
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"combat.object_destroyed object=%d fg=%d type=%d attacker=%d slot=%d tick=%d",
					victim_obj_idx,
					(int)g_object_table[victim_index]
						.flight_group_idx,
					(int)g_object_table[victim_index]
						.object_type,
					(int)g_object_table[source_obj_idx]
						.mobj->source_obj_idx,
					g_object_table
						[g_object_table[source_obj_idx]
							 .mobj->source_obj_idx]
							.player_owner_idx,
					g_game_time);
				XVT_LOG_INFO(
					"battle.killing_blow victim=%d by=%d attacker=%d weapon=\"%s\" type=%d tick=%d",
					victim_obj_idx,
					g_object_table
						[g_object_table[source_obj_idx]
							 .mobj->source_obj_idx]
							.player_owner_idx,
					(int)g_object_table[source_obj_idx]
						.mobj->source_obj_idx,
					g_projectile_type_data.warhead_class
								[g_object_table[source_obj_idx]
									 .object_type -
								 PROJECTILE_OBJECT_TYPE_FIRST] !=
							0
						? "warhead"
						: "cannon",
					(int)g_object_table[source_obj_idx]
						.object_type,
					g_game_time);
			}
			if (g_object_table[victim_index].object_type ==
			    CRAFT_SPECIES_MINE_TYPE_C) {
				laser_createprojectilefromstatic(
					victim_obj_idx,
					g_object_table[source_obj_idx]
						.mobj->source_obj_idx);
				XVT_LOG_DEBUG(
					"combat.mine_fired_back object=%d target=%d predicted=%d",
					victim_obj_idx,
					(int)g_object_table[source_obj_idx]
						.mobj->source_obj_idx,
					g_flight_sim_side_effects_suppressed);
			}
			g_object_table[victim_index].object_type = 0;
			mission_credit_destruction_damage_contributors(
				g_object_table[source_obj_idx]
					.mobj->source_obj_idx,
				victim_obj_idx);
		}
	}

	/* From here source_obj_idx is the impact effect: a craft source gets a
	 * newly allocated explosion slot in its place, while a projectile
	 * source is turned into the effect itself. */
	if (source_obj_idx < g_active_region_craft_object_slot_end) {
		source_obj_idx =
			object_alloc_slot_for_genus(CRAFT_GENUS_EXPLOSION);
		if (source_obj_idx == UINT16_MAX) {
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_WARN(
					"combat.effect_slots_full object=%d",
					victim_obj_idx);
			}
			return;
		}
		g_object_table[source_obj_idx].object_type =
			TEMPORARY_PROJECTILE_OBJECT_TYPE;
	}

	g_object_table[source_obj_idx].world_x =
		g_collision_segment_start_world_x + g_collision_hit_offset_x;
	g_object_table[source_obj_idx].world_y =
		g_collision_segment_start_world_y + g_collision_hit_offset_y;
	g_object_table[source_obj_idx].world_z =
		g_collision_segment_start_world_z + g_collision_hit_offset_z;
	if (g_projectile_type_data
		    .warhead_class[g_object_table[source_obj_idx].object_type -
				   PROJECTILE_OBJECT_TYPE_FIRST] != 0) {
		effect_type = EFFECT_TYPE_DEFAULT;
	}
	g_object_table[source_obj_idx].object_type = (uint8_t)effect_type;
	g_object_table[source_obj_idx].genus_id = CRAFT_GENUS_EXPLOSION;
	g_object_table[source_obj_idx].mobj->family = EXPLOSION_FAMILY;
	g_object_table[source_obj_idx].type_specific_byte[0] = IMPACT_VARIANT;
	g_object_table[source_obj_idx].mobj->speed = 0;
	g_object_table[source_obj_idx].mobj->effect_size = 0;
	g_object_table[source_obj_idx].mobj->seconds_alive = 0;
	g_object_table[source_obj_idx].mobj->lifetime_timer = 0;
	g_object_table[source_obj_idx].pitch = 0;
	g_object_table[source_obj_idx].yaw = 0;
	g_object_table[source_obj_idx].roll = 0;
	g_object_table[source_obj_idx].mobj->orient_matrix_dirty = 1;
	g_object_table[source_obj_idx].mobj->move_vector_dirty =
		g_object_table[source_obj_idx].mobj->orient_matrix_dirty;
	if (effect_type == EFFECT_TYPE_LASER_IMPACT ||
	    effect_type == EFFECT_TYPE_ION_IMPACT) {
		fsfx_play_sound(FLIGHT_SOUND_LASER_IMPACT, source_obj_idx,
				g_local_player);
	} else {
		fsfx_play_sound((uint16_t)((game_rand2() &
					    (RANDOM_IMPACT_SOUND_COUNT - 1)) +
					   FLIGHT_SOUND_SMALL_EXPLOSION_FIRST),
				source_obj_idx, g_local_player);
	}
}

#include "xvt/flight/object/collide.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/assets/opt_native.h"
#endif

#include <string.h>

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/object/static.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/frontend/config.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/math3d.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"

/* The payload of an OPT_ROTSCALE node as collide_test_sweep_against_opt_node
 * reads it. */
struct collide_opt_rotation_scale {
	struct opt_vector origin; /* Point the mesh turns about. */
	/* Axis it turns about; multiplied by g_collide_opt_axis_q15_to_float_scale
	 * before use. */
	struct opt_vector axis;
};

/* The collision globals that collide_would_shot_hit_target saves before its
 * test and puts back after it, one field each. */
struct collision_target_range_scratch {
	int segment_start_world_x; /* g_collision_segment_start_world_x. */
	int segment_start_world_y; /* g_collision_segment_start_world_y. */
	int segment_start_world_z; /* g_collision_segment_start_world_z. */
	int probe_world_x;	   /* g_collision_probe_world_x. */
	int probe_world_y;	   /* g_collision_probe_world_y. */
	int probe_world_z;	   /* g_collision_probe_world_z. */
	int sweep_start_x;	   /* g_collision_sweep_start_x. */
	int sweep_start_y;	   /* g_collision_sweep_start_y. */
	int sweep_start_z;	   /* g_collision_sweep_start_z. */
	int sweep_end_x;	   /* g_collision_sweep_end_x. */
	int sweep_end_y;	   /* g_collision_sweep_end_y. */
	int sweep_end_z;	   /* g_collision_sweep_end_z. */
	int hit_offset_x;	   /* g_collision_hit_offset_x. */
	int hit_offset_y;	   /* g_collision_hit_offset_y. */
	int hit_offset_z;	   /* g_collision_hit_offset_z. */
};

/* The constant 0.0f the float tests compare against. */
// GLOBAL: XVT 0x5181FC
const float g_collide_zero_float = 0.0f;
/* 1/32,768, the factor collide_test_sweep_against_opt_node applies to an
 * OPT_ROTSCALE node's axis. */
// GLOBAL: XVT 0x518208
static const float g_collide_opt_axis_q15_to_float_scale = 0.000030517578125f;
/* 1 while laser_fireturretslot tests a Super Star Destroyer's shot against
 * the ship's own hull, which makes collide_test_sweep_against_opt_node ignore
 * hits less than a tenth of the way along the sweep; only that function
 * sets it, and it sets 0 again after the test. */
// GLOBAL: XVT 0x527E84
int g_collide_sweep_reject_near_start_hits = 0;
/* The OPT_MESHVERTS node met last in a model walk, whose vertices the face
 * tests that follow read; collide_check_swept_model_collision sets NULL
 * before each model. */
// GLOBAL: XVT 0x527E88
static struct opt_node *g_collide_current_mesh_verts_node = NULL;
/* Start of the sweep in the target model's own axes, as floats; an OPT_ROTSCALE
 * node turns it for a rotating mesh, and collide_check_swept_model_collision puts
 * it back from g_collide_sweep_walker_start_saved after each mesh. */
// GLOBAL: XVT 0x622C50
static struct opt_vector g_collide_sweep_walker_start = {0};
/* End of the sweep in the target model's frame, kept like
 * g_collide_sweep_walker_start. */
// GLOBAL: XVT 0x622C60
static struct opt_vector g_collide_sweep_walker_end = {0};
/* g_collide_sweep_walker_start before any turning, set once per model by
 * collide_check_swept_model_collision. */
// GLOBAL: XVT 0x622C70
static struct opt_vector g_collide_sweep_walker_start_saved = {0};
/* g_collide_sweep_walker_end before any turning, set once per model by
 * collide_check_swept_model_collision. */
// GLOBAL: XVT 0x622C80
static struct opt_vector g_collide_sweep_walker_end_saved = {0};
/* 1-based ordinal of the mesh of the nearest hit so far in the current
 * model test, 0 for none; collide_check_swept_model_collision returns it. */
// GLOBAL: XVT 0x622C6C
static int g_collide_sweep_hit_mesh_ordinal = 0;
/* 1-based ordinal of the root mesh being walked, texture roots not
 * counted; only collide_check_swept_model_collision writes it. */
// GLOBAL: XVT 0x622C8C
static int g_collide_sweep_current_mesh_ordinal = 0;
/* Mesh index of the Super Star Destroyer hull a turret shot starts from;
 * collide_check_swept_model_collision skips that mesh while testing the
 * ship's shot against the ship. Only laser_fireturretslot writes it. */
// GLOBAL: XVT 0x622C94
int g_turret_fire_hull_mesh_ordinal = 0;
/* Turn of the current mesh in radians (mesh_rotation times 2 pi / 256), 0
 * for none, applied at OPT_ROTSCALE nodes; only
 * collide_check_swept_model_collision writes it. */
// GLOBAL: XVT 0x622C90
static float g_collide_current_mesh_rotation_angle = 0.0f;
/* Fraction along the sweep, 0 to 1, of the nearest hit so far, 2.0 for
 * none; collide_check_swept_model_collision takes 0.1 off it (not below 0)
 * before it sets g_collisionHitOffset*. */
// GLOBAL: XVT 0x622C98
static float g_collide_sweep_hit_fraction = 0.0f;
/* End point, X, of the sweep of the object under test (the source of
 * collide_test_swept_pair_collision): its current position, or where it will
 * be for a test ahead of time. Set before each test by
 * collide_collisions, collide_would_shot_hit_target,
 * collide_craftstarshipcollision, laser_fireturretslot,
 * paifight_gunnerselfdefenseorder,
 * paifight_find_nearest_gunner_target_in_candidate_set and
 * paifight_find_nearest_matching_target_from_origin. */
// GLOBAL: XVT 0x9A1FD4
int g_collision_probe_world_x = 0;
/* End point, Y; see g_collision_probe_world_x. */
// GLOBAL: XVT 0x9A1FD8
int g_collision_probe_world_y = 0;
/* End point, Z; see g_collision_probe_world_x. */
// GLOBAL: XVT 0x9A1FD0
int g_collision_probe_world_z = 0;
/* Start point, X, of the sweep of the object under test: its previous
 * position or its launch point. Set before each test by
 * collide_collisions, collide_would_shot_hit_target,
 * collide_craftstarshipcollision, laser_fireturretslot,
 * paifight_gunnerselfdefenseorder and paifight_gunneroffenseorder. */
// GLOBAL: XVT 0x9EC604
int g_collision_segment_start_world_x = 0;
/* Start point, Y; see g_collision_segment_start_world_x. */
// GLOBAL: XVT 0xA081E8
int g_collision_segment_start_world_y = 0;
/* Start point, Z; see g_collision_segment_start_world_x. */
// GLOBAL: XVT 0xA08298
int g_collision_segment_start_world_z = 0;
/* Rough distance scratch. collide_collisions computes a player's distance
 * to its target into it for the inspection test and reads it back;
 * collide_test_swept_pair_collision writes its reach limit and then the
 * pair's distance, which nothing reads. */
// GLOBAL: XVT 0x9A7398
int g_approx_dist = 0;
/* 1 while collide_would_shot_hit_target runs a test ahead of time; it makes
 * collide_test_swept_pair_collision run a large model's polygon test only on
 * a target slower than 40. collide_test_swept_pair_collision clears it on
 * that path, and collide_would_shot_hit_target sets 0 when done. */
// GLOBAL: XVT 0x51BF58
int g_collision_is_aim_prediction = 0;
/* Start point, X, of the other object's sweep: its previous position, or
 * a static object's position. Set before each test by collide_collisions,
 * collide_would_shot_hit_target, collide_craftstarshipcollision and
 * static_test_swept_static_collision. */
// GLOBAL: XVT 0x9D80C0
int g_collision_sweep_start_x = 0;
/* Start point, Y; see g_collision_sweep_start_x. */
// GLOBAL: XVT 0x9D8C18
int g_collision_sweep_start_y = 0;
/* Start point, Z; see g_collision_sweep_start_x. */
// GLOBAL: XVT 0x9D1150
int g_collision_sweep_start_z = 0;
/* End point, X, of the other object's sweep: its current position, or
 * where it will be. Written by the same functions as
 * g_collision_sweep_start_x. */
// GLOBAL: XVT 0xA07BDC
int g_collision_sweep_end_x = 0;
/* End point, Y; see g_collision_sweep_end_x. */
// GLOBAL: XVT 0xA07C54
int g_collision_sweep_end_y = 0;
/* End point, Z; see g_collision_sweep_end_x. */
// GLOBAL: XVT 0xA07C58
int g_collision_sweep_end_z = 0;
/* Offset, X, from g_collision_segment_start_world_x to the point of the last
 * hit. collide_checkboxcollision and collide_check_swept_model_collision write
 * it on a hit, and collide_would_shot_hit_target puts back the value it saved;
 * collide_laserhitcraft and static_apply_static_hit place impact effects
 * with it. */
// GLOBAL: XVT 0x9D77FC
int g_collision_hit_offset_x = 0;
/* Offset, Y; see g_collision_hit_offset_x. */
// GLOBAL: XVT 0x9D6828
int g_collision_hit_offset_y = 0;
/* Offset, Z; see g_collision_hit_offset_x. */
// GLOBAL: XVT 0x9CD260
int g_collision_hit_offset_z = 0;

/* Fills list, the proximity list of owner_obj_idx, through
 * collide_insert_mobile_object_proximity_candidate, by the owner's kind. A
 * player's craft: with working systems, every live craft of another flight
 * group except explosions; outside proving grounds, every static object.
 * An AI starfighter, transport or utility vehicle (not in proving grounds):
 * it adds itself to the lists of player craft and of freighters,
 * starships and platforms (not dropping off or carried) in other flight
 * groups, and adds static objects of types 100 to 105 to its own. An AI
 * freighter, starship or platform not dropping off or carried: it adds
 * itself to player craft's lists and to other such large craft's lists,
 * and adds every other AI craft except explosions to its own. A shot:
 * from g_active_region_object_slot_start to g_projectile_object_slot_end, every
 * craft and every warhead from another source, not itself, its source or
 * an explosion, when the shot's target or source belongs to a player or
 * the candidate is its target (the modern build also passes over effects
 * left in shot slots); outside proving grounds, every static object. Other
 * genera get nothing. Does not empty the list first. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x419890
void collide_populate_mobile_object_proximity_candidates(
	struct mobile_object_proximity_list *list, uint16_t owner_obj_idx)
{
	struct object_record *owner_object;
	struct mobile_object *owner_mobile_object;
	uint16_t candidate_obj_idx;
	uint16_t source_obj_idx;

	owner_object = &g_object_table[owner_obj_idx];
	if (owner_object->player_owner_idx != -1) {
		if (owner_object->mobj->p_craft->working_subsystems != 0) {
			for (candidate_obj_idx = (uint16_t)
				     g_active_region_object_slot_start;
			     candidate_obj_idx <
			     g_active_region_craft_object_slot_end;
			     ++candidate_obj_idx) {
				struct object_record *candidate_object =
					&g_object_table[candidate_obj_idx];

				if (candidate_object->object_type != 0 &&
				    candidate_obj_idx != owner_obj_idx &&
				    candidate_object->genus_id !=
					    CRAFT_GENUS_EXPLOSION &&
				    candidate_object->flight_group_idx !=
					    owner_object->flight_group_idx) {
					collide_insert_mobile_object_proximity_candidate(
						list, owner_obj_idx,
						candidate_obj_idx);
				}
			}
		}

		if (g_flight_mission_state.proving_grounds_mode_active == 0) {
			for (candidate_obj_idx =
				     (uint16_t)g_region_main_object_slot_end;
			     candidate_obj_idx <
			     g_region_static_object_slot_count +
				     g_region_main_object_slot_end;
			     ++candidate_obj_idx) {
				if (g_object_table[candidate_obj_idx]
					    .object_type != 0) {
					collide_insert_mobile_object_proximity_candidate(
						list, owner_obj_idx,
						candidate_obj_idx);
				}
			}
		}
		return;
	}

	owner_mobile_object = owner_object->mobj;
	source_obj_idx = owner_mobile_object->source_obj_idx;
	switch (owner_object->genus_id) {
	case CRAFT_GENUS_STARFIGHTER:
	case CRAFT_GENUS_TRANSPORT:
	case CRAFT_GENUS_UTILITY_VEHICLE:
		if (g_flight_mission_state.proving_grounds_mode_active != 0) {
			return;
		}

		for (candidate_obj_idx =
			     (uint16_t)g_active_region_object_slot_start;
		     candidate_obj_idx < g_active_region_craft_object_slot_end;
		     ++candidate_obj_idx) {
			struct object_record *candidate_object =
				&g_object_table[candidate_obj_idx];

			if (candidate_object->flight_group_idx ==
				    owner_object->flight_group_idx ||
			    candidate_object->object_type == 0) {
				continue;
			}

			if (candidate_object->player_owner_idx != -1 &&
			    candidate_object->mobj != NULL) {
				collide_insert_mobile_object_proximity_candidate(
					&candidate_object->mobj->proximity_list,
					candidate_obj_idx, owner_obj_idx);
			}

			if (candidate_object->genus_id !=
				    CRAFT_GENUS_STARSHIP &&
			    candidate_object->genus_id !=
				    CRAFT_GENUS_FREIGHTER &&
			    candidate_object->genus_id !=
				    CRAFT_GENUS_PLATFORM) {
				continue;
			}
			if (candidate_object->mobj == NULL) {
				continue;
			}

			if (candidate_object->mobj->p_craft != NULL &&
			    candidate_object->mobj->p_craft->ai_controller
					    .maneuver_mode !=
				    AI_MANEUVER_MODE_DROPOFF &&
			    candidate_object->mobj->p_craft->carrier_obj_idx ==
				    UINT16_MAX) {
				collide_insert_mobile_object_proximity_candidate(
					&candidate_object->mobj->proximity_list,
					candidate_obj_idx, owner_obj_idx);
			}
		}

		for (candidate_obj_idx =
			     (uint16_t)g_region_main_object_slot_end;
		     candidate_obj_idx < g_region_static_object_slot_count +
						 g_region_main_object_slot_end;
		     ++candidate_obj_idx) {
			uint8_t object_type =
				g_object_table[candidate_obj_idx].object_type;

			/* Static proximity hazards: mines, probes, and navigation buoys. */
			if (object_type >= 100 && object_type <= 105) {
				collide_insert_mobile_object_proximity_candidate(
					list, owner_obj_idx, candidate_obj_idx);
			}
		}
		return;

	case CRAFT_GENUS_FREIGHTER:
	case CRAFT_GENUS_STARSHIP:
	case CRAFT_GENUS_PLATFORM:
		if (owner_mobile_object->p_craft->ai_controller.maneuver_mode ==
			    AI_MANEUVER_MODE_DROPOFF ||
		    owner_mobile_object->p_craft->carrier_obj_idx !=
			    UINT16_MAX) {
			return;
		}

		for (candidate_obj_idx =
			     (uint16_t)g_active_region_object_slot_start;
		     candidate_obj_idx < g_active_region_craft_object_slot_end;
		     ++candidate_obj_idx) {
			struct object_record *candidate_object;

			if (candidate_obj_idx == owner_obj_idx) {
				continue;
			}
			candidate_object = &g_object_table[candidate_obj_idx];
			if (candidate_object->object_type == 0) {
				continue;
			}

			if (candidate_object->player_owner_idx != -1) {
				if (candidate_object->mobj != NULL) {
					collide_insert_mobile_object_proximity_candidate(
						&candidate_object->mobj
							 ->proximity_list,
						candidate_obj_idx,
						owner_obj_idx);
				}
				continue;
			}
			if (candidate_object->genus_id ==
			    CRAFT_GENUS_EXPLOSION) {
				continue;
			}

			collide_insert_mobile_object_proximity_candidate(
				list, owner_obj_idx, candidate_obj_idx);
			if (candidate_object->genus_id !=
				    CRAFT_GENUS_STARSHIP &&
			    candidate_object->genus_id !=
				    CRAFT_GENUS_FREIGHTER &&
			    candidate_object->genus_id !=
				    CRAFT_GENUS_PLATFORM) {
				continue;
			}
			if (candidate_object->mobj == NULL) {
				continue;
			}

			if (candidate_object->mobj->p_craft != NULL &&
			    candidate_object->mobj->p_craft->ai_controller
					    .maneuver_mode !=
				    AI_MANEUVER_MODE_DROPOFF &&
			    candidate_object->mobj->p_craft->carrier_obj_idx ==
				    UINT16_MAX) {
				collide_insert_mobile_object_proximity_candidate(
					&candidate_object->mobj->proximity_list,
					candidate_obj_idx, owner_obj_idx);
			}
		}
		return;

	case CRAFT_GENUS_PLAYER_PROJECTILE:
	case CRAFT_GENUS_OTHER_PROJECTILE: {
		uint16_t target_obj_idx =
			g_projectile_guidance_states
				[owner_obj_idx - g_projectile_object_slot_start]
					.target_obj_idx;

		for (candidate_obj_idx =
			     (uint16_t)g_active_region_object_slot_start;
		     candidate_obj_idx < g_projectile_object_slot_end;
		     ++candidate_obj_idx) {
			struct object_record *candidate_object =
				&g_object_table[candidate_obj_idx];
			int target_is_player_owned;

			if (candidate_object->object_type == 0) {
				continue;
			}
#ifdef XVT_MODERN
			/* Impact effects remain in projectile slots after a hit. */
			if (candidate_obj_idx >=
				    g_active_region_craft_object_slot_end &&
			    (candidate_object->object_type <
				     PROJECTILE_OBJECT_TYPE_FIRST ||
			     candidate_object->object_type >=
				     PROJECTILE_OBJECT_TYPE_FIRST +
					     PROJECTILE_OBJECT_TYPE_COUNT)) {
				continue;
			}
#endif
			if (candidate_obj_idx >=
				    g_active_region_craft_object_slot_end &&
			    (g_projectile_type_data.warhead_class
					     [candidate_object->object_type -
					      PROJECTILE_OBJECT_TYPE_FIRST] ==
				     0 ||
			     candidate_obj_idx == owner_obj_idx ||
			     candidate_object->mobj->source_obj_idx ==
				     source_obj_idx)) {
				continue;
			}
			if (candidate_obj_idx == source_obj_idx ||
			    candidate_object->genus_id ==
				    CRAFT_GENUS_EXPLOSION) {
				continue;
			}

			target_is_player_owned =
				g_active_region_craft_object_slot_end >
					target_obj_idx &&
				g_active_region_object_slot_start <=
					target_obj_idx &&
				g_object_table[target_obj_idx]
						.player_owner_idx != -1;
			if (target_is_player_owned ||
			    g_object_table[source_obj_idx].player_owner_idx !=
				    -1 ||
			    target_obj_idx == candidate_obj_idx) {
				collide_insert_mobile_object_proximity_candidate(
					list, owner_obj_idx, candidate_obj_idx);
			}
		}

		if (g_flight_mission_state.proving_grounds_mode_active == 0) {
			for (candidate_obj_idx =
				     (uint16_t)g_region_main_object_slot_end;
			     candidate_obj_idx <
			     g_region_static_object_slot_count +
				     g_region_main_object_slot_end;
			     ++candidate_obj_idx) {
				if (g_object_table[candidate_obj_idx]
					    .object_type != 0) {
					collide_insert_mobile_object_proximity_candidate(
						list, owner_obj_idx,
						candidate_obj_idx);
				}
			}
		}
		return;
	}

	default:
		return;
	}
}

/* Runs the collision and contact checks of one step for every live object
 * from g_active_region_object_slot_start up to g_projectile_object_slot_end,
 * explosions aside. A player's craft is skipped while its hyperspace_phase
 * is 2. Otherwise, every ENGINE_WASH_UPDATE_TICKS (29) ticks it clears the
 * player's engine wash and rescans every freighter and starship with
 * working systems (collide_apply_engine_wash_damage); it inspects the current
 * target, once per team, when within 4 times the target's max_bounds_extent
 * (halved above 3,000), scoring and counting the inspection and sending
 * the messages; and after 45 simulated seconds of life it offers, through
 * pending_action_id, the hangar of its flight group's departure or alternate
 * mothership when the inside hangar point is within 0x2000 (0x4000 for a
 * starship while the team's first goal status is 1). Then, for every
 * object, it rebuilds the proximity list when rebuild_ticks has run out,
 * drops gone candidates, and tests each candidate whose contact_ticks has
 * run out. A player's craft against a mobile object uses
 * collide_test_swept_pair_collision: in proving grounds a hit puts the craft
 * back to the pose recorded three entries earlier and stops it; otherwise
 * a hit on a starfighter, transport or utility vehicle bounces both, and,
 * when collisions are on or the candidate is a freighter, starship or
 * platform, collide_damagecraft damages both. Against a static object
 * (outside proving grounds) it uses static_test_swept_static_collision, then
 * collide_apply_craft_impact_bounce and, when collisions are on,
 * static_apply_static_hit (unless either flight group has status 20) and
 * damage to the craft. An AI starfighter, transport or utility vehicle is
 * tested only against static objects: damage when collisions are on, else
 * a bounce. An AI freighter, starship or platform is tested against craft
 * that are not docking, launching, entering a hangar, boarding, jumping or
 * dropping off, and a hit damages both. A shot that hits another shot
 * explodes along with it (a cannon shot as an ion or laser impact), with
 * hit stats recorded through mission_record_projectile_hit_stats; one that
 * hits a craft goes to collide_laserhitcraft; one that hits a static
 * object to static_apply_static_hit. A pair where either object is younger
 * than 3 simulated seconds is passed over unless a shot is in it, and
 * pairs the boarding, hangar and hyperspace plans want left alone go back
 * in the list untested. Writes the g_collision* sweep globals,
 * g_approx_dist, the engine wash, pending action and inspection fields of
 * g_players, and the mission stats. */
// FUNCTION: XVT 0x419DF0
void collide_collisions(void)
{
	enum {
		ENGINE_WASH_UPDATE_TICKS = 29,
		NEW_OBJECT_COLLISION_GRACE_SECONDS = 3,
		INSPECTION_LARGE_MODEL_EXTENT = 3000,
		INSPECTION_RANGE_SCALE = 4,
		TEAM_COUNT = 10,
		MISSION_GOAL_COUNT = 8,
		TARGET_DESCRIPTION_REFRESH_TICKS = 944,
		ACTION_PROMPT_TICKS = 1888,
		MIN_DOCK_PROMPT_SECONDS = 45,
		HANGAR_RANGE = 0x2000,
		LARGE_STARSHIP_HANGAR_RANGE = 0x4000,
		AI_MANEUVER_PHASE_APPROACH = 2,
		PROXIMITY_LIST_CAPACITY = 16,
		SMALL_DISTANCE_SQUARED = 50,
		BOUNCE_DIRECTION_SCALE = 100,
		BOUNCE_IMPULSE_SCALE = 1000,
		/* Besides a half turn, 0x8000 is used in this function as the 16-bit sign bit: a game_rand() coin
		 * flip, a sign-change test on move components, the int16 bound of a roll impulse, and the sign of a
		 * dot product. */
		ANGLE_HALF_TURN = 0x8000,
		ANGLE_QUARTER_TURN = 0x4000,
		MAX_ROLL_IMPULSE = 0x7FFF,
		INVALID_MESH_INDEX = -1,
		FLIGHT_GROUP_STATUS_PROTECTED = 20,
		PENDING_ACTION_ENTER_HANGAR = 2,
		CRAFT_MESSAGE_INSPECTED = 142,
		INSPECTION_PLAYER_ARG_BASE = 293,
		EXPLOSION_OBJECT_TYPE_PROJECTILE = 129,
		EXPLOSION_OBJECT_TYPE_LASER = 131,
		EXPLOSION_OBJECT_TYPE_ION = 132,
	};

	uint16_t owner_obj_idx;

	for (owner_obj_idx = (uint16_t)g_active_region_object_slot_start;
	     owner_obj_idx < g_projectile_object_slot_end; ++owner_obj_idx) {
		struct mobile_object_proximity_list *list;
		int player_idx;
		uint16_t candidate_slot;

		if (g_object_table[owner_obj_idx].object_type == 0 ||
		    g_object_table[owner_obj_idx].genus_id ==
			    CRAFT_GENUS_EXPLOSION) {
			continue;
		}

		player_idx = g_object_table[owner_obj_idx].player_owner_idx;
		if (player_idx != -1) {
			uint16_t target_obj_idx;

			if (g_players[player_idx].hyperspace_phase == 2) {
				continue;
			}
			if (g_players[player_idx].next_engine_wash_check_time <
				    g_game_time
#ifdef XVT_MODERN
			    && xvt_flight_timing_reference_due()
#endif
			) {
				uint16_t source_obj_idx;

				g_players[player_idx]
					.engine_wash_source_obj_idx = -1;
				g_players[player_idx].engine_wash_strength = 0;
				g_players[player_idx]
					.next_engine_wash_check_time =
					g_game_time + ENGINE_WASH_UPDATE_TICKS;
				for (source_obj_idx = (uint16_t)
					     g_active_region_object_slot_start;
				     source_obj_idx <
				     g_active_region_craft_object_slot_end;
				     ++source_obj_idx) {
					struct object_record *source_object =
						&g_object_table[source_obj_idx];
					struct craft_data *source_craft;

					if (source_object->object_type == 0) {
						continue;
					}
					source_craft =
						source_object->mobj->p_craft;
					if (source_craft != NULL &&
					    source_craft->working_subsystems !=
						    0 &&
					    source_object->genus_id >=
						    CRAFT_GENUS_FREIGHTER &&
					    source_object->genus_id <=
						    CRAFT_GENUS_STARSHIP) {
						collide_apply_engine_wash_damage(
							owner_obj_idx,
							source_obj_idx);
					}
				}
			}

			g_collision_probe_world_x =
				g_object_table[owner_obj_idx].world_x;
			g_collision_probe_world_y =
				g_object_table[owner_obj_idx].world_y;
			g_collision_probe_world_z =
				g_object_table[owner_obj_idx].world_z;
			g_collision_segment_start_world_x =
				g_object_table[owner_obj_idx]
					.mobj->prev_world_x;
			g_collision_segment_start_world_y =
				g_object_table[owner_obj_idx]
					.mobj->prev_world_y;
			g_collision_segment_start_world_z =
				g_object_table[owner_obj_idx]
					.mobj->prev_world_z;

			target_obj_idx = (uint16_t)g_players[player_idx]
						 .current_target_object_idx;
			if (target_obj_idx >=
				    g_active_region_object_slot_start &&
			    target_obj_idx <
				    g_active_region_craft_object_slot_end) {
				struct object_record *target_object =
					&g_object_table[target_obj_idx];

				if (target_object->object_type != 0 &&
				    target_object->genus_id !=
					    CRAFT_GENUS_EXPLOSION) {
					struct craft_data *target_craft;
					int max_bounds_extent;

					g_collision_sweep_end_x =
						target_object->world_x;
					g_collision_sweep_end_y =
						target_object->world_y;
					g_collision_sweep_end_z =
						target_object->world_z;
					g_collision_sweep_start_x =
						target_object->mobj
							->prev_world_x;
					g_collision_sweep_start_y =
						target_object->mobj
							->prev_world_y;
					g_collision_sweep_start_z =
						target_object->mobj
							->prev_world_z;
					g_approx_dist = collide_roughdistance3d(
						g_collision_probe_world_x -
							g_collision_sweep_end_x,
						g_collision_probe_world_y -
							g_collision_sweep_end_y,
						g_collision_probe_world_z -
							g_collision_sweep_end_z);
					target_craft =
						target_object->mobj->p_craft;
					if (target_craft->object_kind !=
						    CRAFT_OBJECT_KIND_BREAKING_UP &&
					    target_craft->object_kind !=
						    CRAFT_OBJECT_KIND_EXPLODING &&
					    target_craft->identified_order_by_team
							    [g_object_table[owner_obj_idx]
								     .mobj
								     ->team] ==
						    0) {
						max_bounds_extent =
							g_object_type_table
								[target_object
									 ->object_type]
									.max_bounds_extent;
						if (max_bounds_extent >
						    INSPECTION_LARGE_MODEL_EXTENT) {
							max_bounds_extent >>= 1;
						}
						if (INSPECTION_RANGE_SCALE *
							    max_bounds_extent >
						    g_approx_dist) {
							uint16_t
								inspection_order =
									0;
							uint16_t player_team =
								(uint16_t)g_players
									[player_idx]
										.team;
							uint8_t flight_group_idx =
								target_object
									->flight_group_idx;
							int special_cargo_flag =
								0;
							int goal_message_required =
								0;
							int player_scored;
							uint16_t team_index;
							int goal_index;

							for (team_index = 0;
							     team_index <
							     TEAM_COUNT;
							     ++team_index) {
								if (inspection_order <
								    target_craft->identified_order_by_team
									    [team_index]) {
									inspection_order =
										target_craft
											->identified_order_by_team
												[team_index];
								}
							}
							++inspection_order;
							target_craft->identified_order_by_team
								[player_team] =
								(uint8_t)
									inspection_order;
							++g_flight_mission_state
								  .runtime
								  .team_fg_inspected_captured_counts
									  [0]
									  [player_team]
									  [flight_group_idx];
							++g_players[player_idx]
								  .per_mission_kills
								  .num_craft_inspected;
							++g_mission_fg_stats[flight_group_idx]
								  .outcome_count
									  [FLIGHT_GROUP_OUTCOME_INSPECTED];
							++g_mission_fg_stats[flight_group_idx]
								  .team_inspected
									  [player_team];
							if (g_mission_flight_groups
								    [flight_group_idx]
									    .fg
									    .special_cargo_craft ==
							    target_craft
								    ->craft_ordinal) {
								g_mission_fg_stats[flight_group_idx]
									.special_cargo_outcome
										[FLIGHT_GROUP_OUTCOME_INSPECTED] =
									1;
								++g_players[player_idx]
									  .per_mission_kills
									  .num_special_inspected;
								special_cargo_flag =
									1;
								++g_mission_fg_stats[flight_group_idx]
									  .team_special_cargo_inspected
										  [player_team];
							}
							player_scored = mission_apply_flight_group_goal_score(
								MISSION_COND_INSPECTED,
								flight_group_idx,
								player_idx,
								inspection_order -
									1,
								special_cargo_flag,
								player_team);
							mission_apply_flight_group_goal_score(
								MISSION_COND_INSPECTED,
								flight_group_idx,
								-1,
								inspection_order -
									1,
								special_cargo_flag,
								player_team);
							for (goal_index = 0;
							     goal_index <
							     MISSION_GOAL_COUNT;
							     ++goal_index) {
								struct flight_group_goal *goal =
									&g_mission_flight_groups[flight_group_idx]
										 .fg
										 .goals[goal_index];

								if (goal->enabled_teams
										    [player_team] !=
									    0 &&
								    (goal->event_condition ==
									     MISSION_COND_INSPECTED ||
								     (goal->amount ==
									      GOAL_AMT_ALL_SPECIAL_CARGO &&
								      (special_cargo_flag !=
									       0 ||
								       g_mission_fg_stats[flight_group_idx]
										       .special_cargo_outcome
											       [FLIGHT_GROUP_OUTCOME_INSPECTED] ==
									       0))) &&
								    (goal->goal_kind ==
									     0 ||
								     goal->goal_kind ==
									     2)) {
									goal_message_required =
										1;
								}
							}
							if (goal_message_required !=
								    0 &&
							    player_idx ==
								    g_local_player) {
								if (special_cargo_flag !=
								    0) {
									msg_emit_in_flight_message(
										IFMSG_358_INSPECTION_COMPLETED_SPECIAL_CARGO_FOUND,
										g_local_player);
								} else {
									msg_emit_in_flight_message(
										IFMSG_357_INSPECTION_ASSIGNMENT_COMPLETED,
										g_local_player);
								}
								g_player_flight_transient_timers
									[g_local_player]
										.target_description_refresh_timer =
									TARGET_DESCRIPTION_REFRESH_TICKS;
							}
							g_hud_cached_target_object_idx =
								-3;
							if ((goal_message_required !=
								     0 ||
							     target_object->genus_id !=
								     CRAFT_GENUS_STARFIGHTER ||
							     (g_flight_mission_state
									      .locate_players_enabled ==
								      0 &&
							      target_object->player_owner_idx !=
								      -1)) &&
							    player_idx ==
								    g_local_player) {
								msg_emit_craft_message(
									target_obj_idx,
									target_craft,
									CRAFT_MESSAGE_INSPECTED);
								if (player_scored !=
									    0 &&
								    g_flight_player_count >
									    1 &&
								    g_mission_header.mission_type ==
									    MISSION_TYPE_MELEE) {
									g_msg_arg_table[0] =
										(uint16_t)(inspection_order +
											   INSPECTION_PLAYER_ARG_BASE);
									msg_emit_in_flight_message(
										IFMSG_308_YOU_ARE_THE_ARG_TO_INSPECT_THIS_CRAFT,
										g_local_player);
								}
							}
						}
					}
				}
			}

			if (g_object_table[owner_obj_idx].mobj->seconds_alive >=
			    MIN_DOCK_PROMPT_SECONDS) {
				uint16_t mothership_pass;

				for (mothership_pass = 0; mothership_pass < 2;
				     ++mothership_pass) {
					uint8_t mothership_flight_group = 0;
					int has_mothership = 0;
					struct xvt_flight_group *owner_flight_group =
						&g_mission_flight_groups
							 [g_object_table[owner_obj_idx]
								  .flight_group_idx]
								 .fg;

					if (mothership_pass == 0) {
						if (owner_flight_group
							    ->departure_method !=
						    0) {
							mothership_flight_group =
								owner_flight_group
									->departure_mothership;
							has_mothership = 1;
						}
					} else if (
						owner_flight_group
							->alternate_mothership_used !=
						0) {
						mothership_flight_group =
							owner_flight_group
								->alternate_mothership;
						has_mothership = 1;
					}
					if (has_mothership != 0) {
						uint16_t mothership_obj_idx;

						for (mothership_obj_idx = (uint16_t)
							     g_active_region_object_slot_start;
						     mothership_obj_idx <
						     g_active_region_craft_object_slot_end;
						     ++mothership_obj_idx) {
							struct object_record *mothership =
								&g_object_table
									[mothership_obj_idx];
							struct craft_data *
								mothership_craft;
							unsigned int
								prompt_range;

							if (mothership->object_type ==
								    0 ||
							    mothership->genus_id ==
								    CRAFT_GENUS_EXPLOSION ||
							    mothership->flight_group_idx !=
								    mothership_flight_group) {
								continue;
							}
							g_collision_sweep_end_x =
								mothership
									->world_x;
							g_collision_sweep_end_y =
								mothership
									->world_y;
							g_collision_sweep_end_z =
								mothership
									->world_z;
							g_collision_sweep_start_x =
								mothership->mobj
									->prev_world_x;
							g_collision_sweep_start_y =
								mothership->mobj
									->prev_world_y;
							g_collision_sweep_start_z =
								mothership->mobj
									->prev_world_z;
							mothership_craft =
								mothership->mobj
									->p_craft;
							if (mothership_craft
								    ->object_kind !=
							    CRAFT_OBJECT_KIND_ACTIVE) {
								continue;
							}
							pai_rotate_local_vector_to_world_scratch(
								mothership,
								g_model_defs[mothership_craft
										     ->model_index]
									.hangar_points
									.inside
									.side,
								g_model_defs[mothership_craft
										     ->model_index]
									.hangar_points
									.inside
									.up,
								g_model_defs[mothership_craft
										     ->model_index]
									.hangar_points
									.inside
									.forward);
							g_collision_sweep_end_x +=
								g_rotated_x;
							g_collision_sweep_end_y +=
								g_rotated_y;
							g_collision_sweep_end_z +=
								g_rotated_z;
							prompt_range =
								HANGAR_RANGE;
							if (mothership->genus_id ==
								    CRAFT_GENUS_STARSHIP &&
							    g_flight_mission_state
									    .runtime
									    .team_goal_status
										    [(uint16_t)g_players
											     [player_idx]
												     .team]
										    [0] ==
								    1) {
								prompt_range =
									LARGE_STARSHIP_HANGAR_RANGE;
							}
							if ((unsigned int)collide_roughdistance3d(
								    g_collision_sweep_end_x -
									    g_collision_probe_world_x,
								    g_collision_sweep_end_y -
									    g_collision_probe_world_y,
								    g_collision_sweep_end_z -
									    g_collision_probe_world_z) <
								    prompt_range &&
							    g_players[player_idx]
									    .pending_action_id ==
								    0) {
								if (player_idx ==
								    g_local_player) {
									msg_emit_in_flight_message(
										IFMSG_214_HIT_SPACE_TO_ACTIVATE_TRACTOR_BEAM_AND_ENTER_HANGAR,
										g_local_player);
								}
								g_players[player_idx]
									.pending_action_id =
									PENDING_ACTION_ENTER_HANGAR;
								g_players[player_idx]
									.pending_action_param =
									(int16_t)
										mothership_obj_idx;
								g_players[player_idx]
									.pending_action_timer =
									ACTION_PROMPT_TICKS;
							}
						}
					}
				}
			}
		}

		if (g_object_table[owner_obj_idx].mobj == NULL) {
			continue;
		}
		list = &g_object_table[owner_obj_idx].mobj->proximity_list;
		list->rebuild_ticks -= g_elapsed_ticks;
		if (list->rebuild_ticks <= 0) {
			list->rebuild_ticks = 0x7FFF;
			collide_populate_mobile_object_proximity_candidates(
				list, owner_obj_idx);
		}

		for (candidate_slot = 0; candidate_slot < list->count;
		     ++candidate_slot) {
			uint16_t candidate_obj_idx =
				list->obj_idx[candidate_slot];

			if (g_object_table[candidate_obj_idx].object_type ==
				    0 ||
			    g_object_table[candidate_obj_idx].genus_id ==
				    CRAFT_GENUS_EXPLOSION) {
				collide_remove_mobile_object_proximity_candidate(
					list, candidate_obj_idx);
				--candidate_slot;
				continue;
			}
			if (g_object_table[candidate_obj_idx].mobj != NULL &&
			    g_object_table[candidate_obj_idx]
					    .mobj->seconds_alive <
				    NEW_OBJECT_COLLISION_GRACE_SECONDS &&
			    g_object_table[candidate_obj_idx].genus_id !=
				    CRAFT_GENUS_PLAYER_PROJECTILE &&
			    g_object_table[candidate_obj_idx].genus_id !=
				    CRAFT_GENUS_OTHER_PROJECTILE &&
			    g_object_table[owner_obj_idx].genus_id !=
				    CRAFT_GENUS_PLAYER_PROJECTILE &&
			    g_object_table[owner_obj_idx].genus_id !=
				    CRAFT_GENUS_OTHER_PROJECTILE) {
				continue;
			}
			list->contact_ticks[candidate_slot] -= g_elapsed_ticks;
			if (list->contact_ticks[candidate_slot] > 0) {
				continue;
			}
			if (g_object_table[owner_obj_idx].mobj->seconds_alive <
				    NEW_OBJECT_COLLISION_GRACE_SECONDS &&
			    g_object_table[owner_obj_idx].genus_id !=
				    CRAFT_GENUS_PLAYER_PROJECTILE &&
			    g_object_table[owner_obj_idx].genus_id !=
				    CRAFT_GENUS_OTHER_PROJECTILE &&
			    g_object_table[candidate_obj_idx].genus_id !=
				    CRAFT_GENUS_PLAYER_PROJECTILE &&
			    g_object_table[candidate_obj_idx].genus_id !=
				    CRAFT_GENUS_OTHER_PROJECTILE) {
				collide_insert_mobile_object_proximity_candidate(
					list, owner_obj_idx, candidate_obj_idx);
				continue;
			}

			if (g_object_table[owner_obj_idx].player_owner_idx !=
			    -1) {
				struct craft_data *owner_craft =
					g_object_table[owner_obj_idx]
						.mobj->p_craft;

				if (owner_craft->ai_flight.impact_obj_idx ==
				    candidate_obj_idx) {
					collide_insert_mobile_object_proximity_candidate(
						list, owner_obj_idx,
						candidate_obj_idx);
					continue;
				}
				g_collision_probe_world_x =
					g_object_table[owner_obj_idx].world_x;
				g_collision_probe_world_y =
					g_object_table[owner_obj_idx].world_y;
				g_collision_probe_world_z =
					g_object_table[owner_obj_idx].world_z;
				g_collision_segment_start_world_x =
					g_object_table[owner_obj_idx]
						.mobj->prev_world_x;
				g_collision_segment_start_world_y =
					g_object_table[owner_obj_idx]
						.mobj->prev_world_y;
				g_collision_segment_start_world_z =
					g_object_table[owner_obj_idx]
						.mobj->prev_world_z;
				if (g_object_table[candidate_obj_idx].mobj !=
				    NULL) {
					struct craft_data *candidate_craft =
						g_object_table
							[candidate_obj_idx]
								.mobj->p_craft;
					int16_t hit_mesh_index;

					if (strcmp(g_plan_table
							   [candidate_craft
								    ->ai_controller
								    .current_plan_id]
								   .name,
						   "boardtogivepln") == 0 &&
					    candidate_craft->ai_controller
							    .target_obj_idx ==
						    owner_obj_idx) {
						collide_insert_mobile_object_proximity_candidate(
							list, owner_obj_idx,
							candidate_obj_idx);
						continue;
					}
					g_collision_sweep_end_x =
						g_object_table
							[candidate_obj_idx]
								.world_x;
					g_collision_sweep_end_y =
						g_object_table
							[candidate_obj_idx]
								.world_y;
					g_collision_sweep_end_z =
						g_object_table
							[candidate_obj_idx]
								.world_z;
					g_collision_sweep_start_x =
						g_object_table
							[candidate_obj_idx]
								.mobj
								->prev_world_x;
					g_collision_sweep_start_y =
						g_object_table
							[candidate_obj_idx]
								.mobj
								->prev_world_y;
					g_collision_sweep_start_z =
						g_object_table
							[candidate_obj_idx]
								.mobj
								->prev_world_z;
					hit_mesh_index =
						collide_test_swept_pair_collision(
							owner_obj_idx,
							candidate_obj_idx);
					if (hit_mesh_index != 0) {
						if (g_flight_mission_state
							    .proving_grounds_mode_active !=
						    0) {
							g_object_table
								[owner_obj_idx]
									.mobj
									->speed =
								0;
							owner_craft
								->throttle_speed =
								0;
							g_object_table
								[owner_obj_idx]
									.world_x =
								g_proving_grounds_local_player_world_x_history
									[3];
							g_object_table[owner_obj_idx]
								.mobj
								->prev_world_x =
								g_object_table[owner_obj_idx]
									.world_x;
							g_object_table
								[owner_obj_idx]
									.world_y =
								g_proving_grounds_local_player_world_y_history
									[3];
							g_object_table[owner_obj_idx]
								.mobj
								->prev_world_y =
								g_object_table[owner_obj_idx]
									.world_y;
							g_object_table
								[owner_obj_idx]
									.world_z =
								g_proving_grounds_local_player_world_z_history
									[3];
							g_object_table[owner_obj_idx]
								.mobj
								->prev_world_z =
								g_object_table[owner_obj_idx]
									.world_z;
							g_object_table
								[owner_obj_idx]
									.roll =
								g_proving_grounds_local_player_roll_history
									[3];
							g_object_table
								[owner_obj_idx]
									.pitch =
								g_proving_grounds_local_player_pitch_history
									[3];
							owner_craft->pitch =
								g_object_table[owner_obj_idx]
									.pitch;
							g_object_table
								[owner_obj_idx]
									.yaw =
								g_proving_grounds_local_player_yaw_history
									[3];
#ifdef XVT_MODERN
							if (xvt_flight_timing_is_unlocked()) {
								xvt_player_timing_recover(
									g_local_player);
							}
#endif
							g_object_table[owner_obj_idx]
								.mobj
								->move_vector_dirty =
								1;
							g_object_table[owner_obj_idx]
								.mobj
								->orient_matrix_dirty =
								g_object_table[owner_obj_idx]
									.mobj
									->move_vector_dirty;
							fsfx_play_sound(
								(game_rand() &
								 ANGLE_HALF_TURN) ==
										0
									? FLIGHT_SOUND_HULL_HIT_2
									: FLIGHT_SOUND_HULL_HIT_1,
								owner_obj_idx,
								g_object_table[owner_obj_idx]
									.player_owner_idx);
						} else {
							if (g_object_table[candidate_obj_idx]
								    .genus_id <=
							    CRAFT_GENUS_UTILITY_VEHICLE) {
								int16_t angle_difference;
								uint16_t
									candidate_speed;
								int16_t relative_speed;
								int delta_x;
								int delta_y;
								int delta_z;
								int distance_squared;
								int impulse_x;
								int impulse_y;
								int16_t adjusted_move_x;
								int16_t adjusted_move_y;
								int16_t roll_impulse;
								uint16_t
									saved_yaw;

								owner_craft
									->ai_flight
									.impact_obj_idx =
									candidate_obj_idx;
								angle_difference =
									g_object_table[candidate_obj_idx]
										.yaw -
									g_object_table[owner_obj_idx]
										.yaw;
								if ((uint16_t)
									    angle_difference >=
								    ANGLE_HALF_TURN) {
									angle_difference =
										g_object_table[owner_obj_idx]
											.yaw -
										g_object_table[candidate_obj_idx]
											.yaw;
								}
								if ((uint16_t)
									    angle_difference >
								    ANGLE_QUARTER_TURN) {
									angle_difference =
										ANGLE_HALF_TURN -
										angle_difference;
								}
								candidate_speed =
									g_object_table[candidate_obj_idx]
										.mobj
										->speed;
								if ((uint16_t)
									    angle_difference <
								    ANGLE_QUARTER_TURN) {
									relative_speed =
										g_object_table[owner_obj_idx]
											.mobj
											->speed -
										trig2_cosinewordmult(
											candidate_speed,
											angle_difference);
								} else {
									relative_speed =
										g_object_table[owner_obj_idx]
											.mobj
											->speed +
										trig2_cosinewordmult(
											candidate_speed,
											angle_difference);
								}
								if (relative_speed <
								    0) {
									relative_speed =
										-relative_speed;
								}
								delta_x =
									g_object_table[owner_obj_idx]
										.world_x -
									g_object_table[candidate_obj_idx]
										.world_x;
								delta_y =
									g_object_table[owner_obj_idx]
										.world_y -
									g_object_table[candidate_obj_idx]
										.world_y;
								delta_z =
									g_object_table[owner_obj_idx]
										.world_z -
									g_object_table[candidate_obj_idx]
										.world_z;
								/* Preserve 32-bit wrapping before signed comparisons and division. */
								distance_squared =
									(int32_t)((uint32_t)delta_x *
											  (uint32_t)
												  delta_x +
										  (uint32_t)delta_y *
											  (uint32_t)
												  delta_y +
										  (uint32_t)delta_z *
											  (uint32_t)
												  delta_z);
								if (distance_squared <=
								    SMALL_DISTANCE_SQUARED) {
									impulse_x =
										0;
									impulse_y =
										BOUNCE_DIRECTION_SCALE;
								} else {
									/* From here deltaX and deltaY hold the offsets times BOUNCE_IMPULSE_SCALE
									 * times relative_speed; the candidate's bounce below multiplies them by
									 * both again. */
									delta_x =
										(int32_t)((uint32_t)
												  delta_x *
											  BOUNCE_IMPULSE_SCALE *
											  (uint32_t)
												  relative_speed);
									impulse_x =
										delta_x /
										distance_squared;
									delta_y =
										(int32_t)((uint32_t)
												  delta_y *
											  BOUNCE_IMPULSE_SCALE *
											  (uint32_t)
												  relative_speed);
									impulse_y =
										delta_y /
										distance_squared;
								}
								if (g_object_table[owner_obj_idx]
									    .mobj
									    ->move_vector_dirty !=
								    0) {
									fview_calcrotatemove(
										g_object_table[owner_obj_idx]
											.pitch,
										g_object_table[owner_obj_idx]
											.yaw,
										&g_object_table
											[owner_obj_idx]);
								}
								adjusted_move_x =
									g_object_table[owner_obj_idx]
										.mobj
										->move_x +
									(int16_t)
										impulse_x;
								if (((uint16_t)(g_object_table[owner_obj_idx]
											.mobj
											->move_x ^
										adjusted_move_x) &
								     ANGLE_HALF_TURN) !=
								    0) {
									adjusted_move_x =
										g_object_table[owner_obj_idx]
											.mobj
											->move_x;
								}
								adjusted_move_y =
									g_object_table[owner_obj_idx]
										.mobj
										->move_y +
									(int16_t)
										impulse_y;
								if (((uint16_t)(g_object_table[owner_obj_idx]
											.mobj
											->move_y ^
										adjusted_move_y) &
								     ANGLE_HALF_TURN) !=
								    0) {
									adjusted_move_y =
										g_object_table[owner_obj_idx]
											.mobj
											->move_y;
								}
								saved_yaw =
									g_object_table[owner_obj_idx]
										.yaw;
								g_object_table[owner_obj_idx]
									.yaw = trig2_arctan(
									adjusted_move_x,
									adjusted_move_y);
								g_object_table[candidate_obj_idx]
									.pitch = trig2_w_arccos(
									adjusted_move_x);
								candidate_craft
									->pitch =
									g_object_table[candidate_obj_idx]
										.pitch;
								roll_impulse =
									BOUNCE_DIRECTION_SCALE *
									relative_speed;
								if ((uint16_t)
									    roll_impulse >=
								    ANGLE_HALF_TURN) {
									roll_impulse =
										MAX_ROLL_IMPULSE;
								}
								if (g_object_table[candidate_obj_idx]
									    .yaw >
								    saved_yaw) {
									roll_impulse =
										-roll_impulse;
								}
								g_object_table[candidate_obj_idx]
									.mobj
									->roll_impulse_rate =
									roll_impulse;
								fview_calcrotatemove(
									g_object_table[candidate_obj_idx]
										.pitch,
									g_object_table[candidate_obj_idx]
										.yaw,
									&g_object_table
										[candidate_obj_idx]);
								fview_calcrotateorient(
									g_object_table[candidate_obj_idx]
										.roll,
									0,
									&g_object_table
										[candidate_obj_idx]);
								candidate_craft
									->ai_flight
									.impact_obj_idx =
									owner_obj_idx;

								if (distance_squared <=
								    SMALL_DISTANCE_SQUARED) {
									impulse_x =
										0;
									impulse_y =
										BOUNCE_DIRECTION_SCALE;
								} else {
									impulse_x =
										(int32_t)(BOUNCE_IMPULSE_SCALE *
											  (uint32_t)
												  delta_x *
											  (uint32_t)
												  relative_speed) /
										distance_squared;
									impulse_y =
										(int32_t)(BOUNCE_IMPULSE_SCALE *
											  (uint32_t)
												  delta_y *
											  (uint32_t)
												  relative_speed) /
										distance_squared;
								}
								if (g_object_table[candidate_obj_idx]
									    .mobj
									    ->move_vector_dirty !=
								    0) {
									fview_calcrotatemove(
										g_object_table[candidate_obj_idx]
											.pitch,
										g_object_table[candidate_obj_idx]
											.yaw,
										&g_object_table
											[candidate_obj_idx]);
								}
								adjusted_move_x =
									g_object_table[candidate_obj_idx]
										.mobj
										->move_x +
									(int16_t)
										impulse_x;
								if (((uint16_t)(g_object_table[candidate_obj_idx]
											.mobj
											->move_x ^
										adjusted_move_x) &
								     ANGLE_HALF_TURN) !=
								    0) {
									adjusted_move_x =
										g_object_table[candidate_obj_idx]
											.mobj
											->move_x;
								}
								adjusted_move_y =
									g_object_table[candidate_obj_idx]
										.mobj
										->move_y +
									(int16_t)
										impulse_y;
								if (((uint16_t)(g_object_table[candidate_obj_idx]
											.mobj
											->move_y ^
										adjusted_move_y) &
								     ANGLE_HALF_TURN) !=
								    0) {
									adjusted_move_y =
										g_object_table[candidate_obj_idx]
											.mobj
											->move_y;
								}
								saved_yaw =
									g_object_table[candidate_obj_idx]
										.yaw;
								g_object_table[candidate_obj_idx]
									.yaw = trig2_arctan(
									adjusted_move_x,
									adjusted_move_y);
								g_object_table[owner_obj_idx]
									.pitch = trig2_w_arccos(
									adjusted_move_x);
								owner_craft
									->pitch =
									g_object_table[owner_obj_idx]
										.pitch;
								g_object_table[candidate_obj_idx]
									.mobj
									->orient_matrix_dirty =
									1;
								g_object_table[candidate_obj_idx]
									.mobj
									->move_vector_dirty =
									1;
								roll_impulse =
									BOUNCE_DIRECTION_SCALE *
									relative_speed;
								if ((uint16_t)
									    roll_impulse >=
								    ANGLE_HALF_TURN) {
									roll_impulse =
										MAX_ROLL_IMPULSE;
								}
								if (g_object_table[owner_obj_idx]
									    .yaw >
								    saved_yaw) {
									roll_impulse =
										-roll_impulse;
								}
								g_object_table[owner_obj_idx]
									.mobj
									->roll_impulse_rate =
									roll_impulse;
								fview_calcrotatemove(
									g_object_table[owner_obj_idx]
										.pitch,
									g_object_table[owner_obj_idx]
										.yaw,
									&g_object_table
										[owner_obj_idx]);
								fview_calcrotateorient(
									g_object_table[owner_obj_idx]
										.roll,
									0,
									&g_object_table
										[owner_obj_idx]);
								msg_emit_in_flight_message(
									IFMSG_220_COLLISION_WITH_ANOTHER_CRAFT_HAS_OCCURRED,
									g_object_table[owner_obj_idx]
										.player_owner_idx);
								fsfx_play_sound(
									FLIGHT_SOUND_HULL_HIT_1,
									owner_obj_idx,
									g_object_table[owner_obj_idx]
										.player_owner_idx);
								fsfx_play_sound(
									FLIGHT_SOUND_HULL_HIT_2,
									candidate_obj_idx,
									g_object_table[owner_obj_idx]
										.player_owner_idx);
							}
							if (g_flight_mission_state
									    .collisions_enabled !=
								    0 ||
							    g_object_table[candidate_obj_idx]
									    .genus_id ==
								    CRAFT_GENUS_STARSHIP ||
							    g_object_table[candidate_obj_idx]
									    .genus_id ==
								    CRAFT_GENUS_FREIGHTER ||
							    g_object_table[candidate_obj_idx]
									    .genus_id ==
								    CRAFT_GENUS_PLATFORM) {
								int dot_product;

								collide_damagecraft(
									candidate_obj_idx,
									hit_mesh_index,
									owner_obj_idx,
									0);
								if (g_object_table[owner_obj_idx]
									    .mobj
									    ->orient_matrix_dirty !=
								    0) {
									fview_calcrotatemove(
										g_object_table[owner_obj_idx]
											.pitch,
										g_object_table[owner_obj_idx]
											.yaw,
										&g_object_table
											[owner_obj_idx]);
									fview_calcrotateorient(
										g_object_table[owner_obj_idx]
											.roll,
										0,
										&g_object_table
											[owner_obj_idx]);
								}
								dot_product = math_dot3q15_wrapped(
									(int16_t)(g_collision_sweep_end_x -
										  g_collision_sweep_start_x),
									(int16_t)(g_collision_sweep_end_y -
										  g_collision_sweep_start_y),
									(int16_t)(g_collision_sweep_end_z -
										  g_collision_sweep_start_z),
									g_object_table[owner_obj_idx]
										.mobj
										->cached_fwd_x,
									g_object_table[owner_obj_idx]
										.mobj
										->cached_fwd_y,
									g_object_table[owner_obj_idx]
										.mobj
										->cached_fwd_z);
								collide_damagecraft(
									owner_obj_idx,
									INVALID_MESH_INDEX,
									candidate_obj_idx,
									(dot_product &
									 ANGLE_HALF_TURN) !=
										0);
							}
						}
					}
				} else if (
					g_flight_mission_state
							.proving_grounds_mode_active ==
						0 &&
					static_test_swept_static_collision(
						owner_obj_idx,
						candidate_obj_idx) != 0) {
					collide_apply_craft_impact_bounce(
						owner_obj_idx,
						candidate_obj_idx);
					if (g_flight_mission_state
						    .collisions_enabled != 0) {
						struct xvt_flight_group *candidate_group =
							&g_mission_flight_groups
								 [g_object_table[candidate_obj_idx]
									  .flight_group_idx]
									 .fg;
						struct xvt_flight_group *owner_group =
							&g_mission_flight_groups
								 [g_object_table[owner_obj_idx]
									  .flight_group_idx]
									 .fg;

						if (candidate_group->status1 !=
							    FLIGHT_GROUP_STATUS_PROTECTED &&
						    candidate_group->status2 !=
							    FLIGHT_GROUP_STATUS_PROTECTED &&
						    owner_group->status1 !=
							    FLIGHT_GROUP_STATUS_PROTECTED &&
						    owner_group->status2 !=
							    FLIGHT_GROUP_STATUS_PROTECTED) {
							static_apply_static_hit(
								owner_obj_idx,
								candidate_obj_idx);
						}
						collide_damagecraft(
							owner_obj_idx,
							INVALID_MESH_INDEX,
							candidate_obj_idx, 0);
					}
				}
			} else {
				switch (g_object_table[owner_obj_idx]
						.genus_id) {
				case CRAFT_GENUS_STARFIGHTER:
				case CRAFT_GENUS_TRANSPORT:
				case CRAFT_GENUS_UTILITY_VEHICLE:
					g_collision_probe_world_x =
						g_object_table[owner_obj_idx]
							.world_x;
					g_collision_probe_world_y =
						g_object_table[owner_obj_idx]
							.world_y;
					g_collision_probe_world_z =
						g_object_table[owner_obj_idx]
							.world_z;
					g_collision_segment_start_world_x =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_x;
					g_collision_segment_start_world_y =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_y;
					g_collision_segment_start_world_z =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_z;
					if (candidate_obj_idx >=
						    g_region_main_object_slot_end &&
					    static_test_swept_static_collision(
						    owner_obj_idx,
						    candidate_obj_idx) != 0) {
						if (g_flight_mission_state
							    .collisions_enabled !=
						    0) {
							collide_damagecraft(
								owner_obj_idx,
								INVALID_MESH_INDEX,
								candidate_obj_idx,
								0);
						} else {
							collide_apply_craft_impact_bounce(
								owner_obj_idx,
								candidate_obj_idx);
						}
					}
					break;

				case CRAFT_GENUS_FREIGHTER:
				case CRAFT_GENUS_STARSHIP:
				case CRAFT_GENUS_PLATFORM: {
					struct craft_data *owner_craft =
						g_object_table[owner_obj_idx]
							.mobj->p_craft;
					struct craft_data *candidate_craft;
					uint8_t maneuver_mode =
						owner_craft->ai_controller
							.maneuver_mode;

					if (maneuver_mode ==
						    AI_MANEUVER_MODE_DROPOFF ||
					    owner_craft->carrier_obj_idx !=
						    UINT16_MAX) {
						candidate_slot =
							PROXIMITY_LIST_CAPACITY;
						continue;
					}
					if (owner_craft->carried_object_index ==
						    candidate_obj_idx ||
					    (maneuver_mode ==
						     AI_MANEUVER_MODE_BOARD &&
					     owner_craft->ai_controller
							     .target_obj_idx ==
						     candidate_obj_idx)) {
						collide_insert_mobile_object_proximity_candidate(
							list, owner_obj_idx,
							candidate_obj_idx);
						continue;
					}
					candidate_craft =
						g_object_table
							[candidate_obj_idx]
								.mobj->p_craft;
					if (strcmp(g_plan_table
							   [candidate_craft
								    ->ai_controller
								    .running_plan_id]
								   .name,
						   "exithangarpln") == 0 ||
					    strcmp(g_plan_table
							   [candidate_craft
								    ->ai_controller
								    .running_plan_id]
								   .name,
						   "outofhyperspacepln") == 0 ||
					    strcmp(g_plan_table
							   [candidate_craft
								    ->ai_controller
								    .running_plan_id]
								   .name,
						   "enterhangarpln") == 0) {
						collide_insert_mobile_object_proximity_candidate(
							list, owner_obj_idx,
							candidate_obj_idx);
						continue;
					}
					if (strcmp(g_plan_table
							   [candidate_craft
								    ->ai_controller
								    .running_plan_id]
								   .name,
						   "followhomeevadepln") == 0) {
						struct craft_data *leader_craft =
							g_object_table
								[candidate_craft
									 ->leader_obj_idx]
									.mobj
									->p_craft;
						if (strcmp(g_plan_table
								   [leader_craft
									    ->ai_controller
									    .running_plan_id]
									   .name,
							   "exithangarpln") ==
							    0 ||
						    strcmp(g_plan_table
								   [leader_craft
									    ->ai_controller
									    .running_plan_id]
									   .name,
							   "enterhangarpln") ==
							    0) {
							collide_insert_mobile_object_proximity_candidate(
								list,
								owner_obj_idx,
								candidate_obj_idx);
							continue;
						}
					}
					maneuver_mode =
						candidate_craft->ai_controller
							.maneuver_mode;
					if (candidate_craft->carrier_obj_idx !=
						    UINT16_MAX ||
					    maneuver_mode ==
						    AI_MANEUVER_MODE_BOARD ||
					    maneuver_mode ==
						    AI_MANEUVER_MODE_INTO_HYPERSPACE ||
					    maneuver_mode ==
						    AI_MANEUVER_MODE_DROPOFF) {
						collide_insert_mobile_object_proximity_candidate(
							list, owner_obj_idx,
							candidate_obj_idx);
						continue;
					}
					g_collision_sweep_end_x =
						g_object_table[owner_obj_idx]
							.world_x;
					g_collision_sweep_end_y =
						g_object_table[owner_obj_idx]
							.world_y;
					g_collision_sweep_end_z =
						g_object_table[owner_obj_idx]
							.world_z;
					g_collision_sweep_start_x =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_x;
					g_collision_sweep_start_y =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_y;
					g_collision_sweep_start_z =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_z;
					g_collision_probe_world_x =
						g_object_table
							[candidate_obj_idx]
								.world_x;
					g_collision_probe_world_y =
						g_object_table
							[candidate_obj_idx]
								.world_y;
					g_collision_probe_world_z =
						g_object_table
							[candidate_obj_idx]
								.world_z;
					g_collision_segment_start_world_x =
						g_object_table
							[candidate_obj_idx]
								.mobj
								->prev_world_x;
					g_collision_segment_start_world_y =
						g_object_table
							[candidate_obj_idx]
								.mobj
								->prev_world_y;
					g_collision_segment_start_world_z =
						g_object_table
							[candidate_obj_idx]
								.mobj
								->prev_world_z;
					{
						int16_t hit_mesh_index =
							collide_test_swept_pair_collision(
								candidate_obj_idx,
								owner_obj_idx);
						if (hit_mesh_index != 0) {
							collide_damagecraft(
								owner_obj_idx,
								hit_mesh_index,
								candidate_obj_idx,
								0);
							collide_damagecraft(
								candidate_obj_idx,
								INVALID_MESH_INDEX,
								owner_obj_idx,
								0);
						}
					}
					break;
				}

				case CRAFT_GENUS_PLAYER_PROJECTILE:
				case CRAFT_GENUS_OTHER_PROJECTILE: {
					if (g_object_table[g_object_table[owner_obj_idx]
								   .mobj
								   ->source_obj_idx]
							    .player_owner_idx !=
						    -1 &&
					    candidate_obj_idx <
						    g_active_region_craft_object_slot_end) {
						struct craft_data *candidate_craft =
							g_object_table
								[candidate_obj_idx]
									.mobj
									->p_craft;
						if (strcmp(g_plan_table
								   [candidate_craft
									    ->ai_controller
									    .current_plan_id]
									   .name,
							   "boardtogivepln") ==
							    0 &&
						    candidate_craft->ai_controller
								    .maneuver_mode ==
							    AI_MANEUVER_MODE_BOARD &&
						    candidate_craft->ai_controller
								    .maneuver_phase ==
							    AI_MANEUVER_PHASE_APPROACH &&
						    candidate_craft->ai_controller
								    .target_obj_idx ==
							    owner_obj_idx) {
							collide_insert_mobile_object_proximity_candidate(
								list,
								owner_obj_idx,
								candidate_obj_idx);
							continue;
						}
					}
					g_collision_probe_world_x =
						g_object_table[owner_obj_idx]
							.world_x;
					g_collision_probe_world_y =
						g_object_table[owner_obj_idx]
							.world_y;
					g_collision_probe_world_z =
						g_object_table[owner_obj_idx]
							.world_z;
					g_collision_segment_start_world_x =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_x;
					g_collision_segment_start_world_y =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_y;
					g_collision_segment_start_world_z =
						g_object_table[owner_obj_idx]
							.mobj->prev_world_z;
					if (g_object_table[candidate_obj_idx]
						    .mobj != NULL) {
						int16_t hit_mesh_index;

						g_collision_sweep_end_x =
							g_object_table
								[candidate_obj_idx]
									.world_x;
						g_collision_sweep_end_y =
							g_object_table
								[candidate_obj_idx]
									.world_y;
						g_collision_sweep_end_z =
							g_object_table
								[candidate_obj_idx]
									.world_z;
						g_collision_sweep_start_x =
							g_object_table[candidate_obj_idx]
								.mobj
								->prev_world_x;
						g_collision_sweep_start_y =
							g_object_table[candidate_obj_idx]
								.mobj
								->prev_world_y;
						g_collision_sweep_start_z =
							g_object_table[candidate_obj_idx]
								.mobj
								->prev_world_z;
						hit_mesh_index =
							collide_test_swept_pair_collision(
								owner_obj_idx,
								candidate_obj_idx);
						if (hit_mesh_index != 0) {
							if (candidate_obj_idx >=
							    g_active_region_craft_object_slot_end) {
								if (g_projectile_type_data
									    .warhead_class
										    [g_object_table[owner_obj_idx]
											     .object_type -
										     PROJECTILE_OBJECT_TYPE_FIRST] !=
								    0) {
									if (g_projectile_type_data
										    .warhead_class
											    [g_object_table[candidate_obj_idx]
												     .object_type -
											     PROJECTILE_OBJECT_TYPE_FIRST] ==
									    0) {
										mission_record_projectile_hit_stats(
											candidate_obj_idx);
									} else if (
										g_object_table[owner_obj_idx]
											.mobj
											->p_warhead_guidance
											->target_obj_idx ==
										candidate_obj_idx) {
										mission_record_projectile_hit_stats(
											owner_obj_idx);
									} else if (
										g_object_table[candidate_obj_idx]
											.mobj
											->p_warhead_guidance
											->target_obj_idx ==
										owner_obj_idx) {
										mission_record_projectile_hit_stats(
											candidate_obj_idx);
									}
									collide_convert_object_to_explosion(
										owner_obj_idx,
										EXPLOSION_OBJECT_TYPE_PROJECTILE);
								} else {
									mission_record_projectile_hit_stats(
										owner_obj_idx);
									if (g_object_table[owner_obj_idx]
											    .object_type ==
										    PROJECTILE_OBJECT_TYPE_ION_LASER ||
									    g_object_table[owner_obj_idx]
											    .object_type ==
										    PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
										collide_convert_object_to_explosion(
											owner_obj_idx,
											EXPLOSION_OBJECT_TYPE_ION);
									} else {
										collide_convert_object_to_explosion(
											owner_obj_idx,
											EXPLOSION_OBJECT_TYPE_LASER);
									}
								}
								collide_convert_object_to_explosion(
									candidate_obj_idx,
									EXPLOSION_OBJECT_TYPE_PROJECTILE);
							} else {
								mission_record_projectile_hit_stats(
									owner_obj_idx);
								collide_laserhitcraft(
									owner_obj_idx,
									candidate_obj_idx,
									hit_mesh_index);
							}
						}
					} else if (
						g_flight_mission_state
								.proving_grounds_mode_active ==
							0 &&
						static_test_swept_static_collision(
							owner_obj_idx,
							candidate_obj_idx) !=
							0) {
						static_apply_static_hit(
							owner_obj_idx,
							candidate_obj_idx);
						mission_record_projectile_hit_stats(
							owner_obj_idx);
					}
					break;
				}

				default:
					break;
				}
			}

			if (g_object_table[owner_obj_idx].object_type == 0 ||
			    g_object_table[owner_obj_idx].genus_id ==
				    CRAFT_GENUS_EXPLOSION) {
				list->rebuild_ticks = 0;
				list->count = 0;
				break;
			}
			if (g_object_table[candidate_obj_idx].object_type ==
				    0 ||
			    g_object_table[candidate_obj_idx].genus_id ==
				    CRAFT_GENUS_EXPLOSION) {
				collide_remove_mobile_object_proximity_candidate(
					list, candidate_obj_idx);
				--candidate_slot;
			}
		}
	}
}

/* Puts candidate_obj_idx into list, the proximity list of owner_obj_idx, or
 * moves it there, in rising order of contact_ticks: 13,275 times the
 * clearance between the two objects' bounds (rough distance less both
 * max_bounds_extent) over 256, divided by the sum of both speeds from
 * collide_get_mobile_object_proximity_speed_q12 over 256; 0 when the bounds
 * overlap. Does nothing when the bounds are apart and that speed sum comes
 * to 0. In a full list a candidate later than every entry is left out, or
 * the last entry is dropped to make room, and rebuild_ticks is lowered to
 * the contact_ticks of the one left out when that is sooner. */
// FUNCTION: XVT 0x41B830
void collide_insert_mobile_object_proximity_candidate(
	struct mobile_object_proximity_list *list, uint16_t owner_obj_idx,
	uint16_t candidate_obj_idx)
{
	int clearance;
	int candidate_speed;
	int combined_speed;
	int contact_ticks;
	int index;
	uint8_t count;
	int move_index;
	int displaced_score;
	int *score;
	uint16_t *object_index;

	clearance = collide_roughdistance3d(
		g_object_table[owner_obj_idx].world_x -
			g_object_table[candidate_obj_idx].world_x,
		g_object_table[owner_obj_idx].world_y -
			g_object_table[candidate_obj_idx].world_y,
		g_object_table[owner_obj_idx].world_z -
			g_object_table[candidate_obj_idx].world_z);
	clearance -=
		g_object_type_table[g_object_table[owner_obj_idx].object_type]
			.max_bounds_extent;
	clearance -= g_object_type_table[g_object_table[candidate_obj_idx]
						 .object_type]
			     .max_bounds_extent;
	if (clearance < 0) {
		contact_ticks = 0;
	} else {
		candidate_speed = collide_get_mobile_object_proximity_speed_q12(
			candidate_obj_idx);
		clearance >>= 8;
		combined_speed = (candidate_speed +
				  collide_get_mobile_object_proximity_speed_q12(
					  owner_obj_idx)) >>
				 8;
		if (combined_speed == 0) {
			return;
		}
		contact_ticks = 13275 * clearance / combined_speed;
	}

	index = 0;
	count = list->count;
	if (count != 0) {
		score = list->contact_ticks;
		object_index = list->obj_idx;
		for (;;) {
			if (*object_index == candidate_obj_idx) {
				list->contact_ticks[index] = contact_ticks;
				move_index = index + 1;
				while (move_index < list->count &&
				       contact_ticks > list->contact_ticks
							       [move_index]) {
					list->contact_ticks[move_index - 1] =
						list->contact_ticks[move_index];
					list->obj_idx[move_index - 1] =
						list->obj_idx[move_index];
					list->contact_ticks[move_index] =
						contact_ticks;
					list->obj_idx[move_index] =
						candidate_obj_idx;
					++move_index;
				}
				index = move_index - 1;
				if (index > 0) {
					move_index = index - 1;
					do {
						if (contact_ticks >=
						    list->contact_ticks
							    [move_index]) {
							break;
						}
						list->contact_ticks[move_index +
								    1] =
							list->contact_ticks
								[move_index];
						list->obj_idx[move_index + 1] =
							list->obj_idx
								[move_index];
						--move_index;
						list->contact_ticks[move_index +
								    1] =
							contact_ticks;
						list->obj_idx[move_index + 1] =
							candidate_obj_idx;
					} while (move_index >= 0);
				}
				return;
			}
			if (*score > contact_ticks) {
				break;
			}
			++score;
			++object_index;
			++index;
			if (index >= count) {
				break;
			}
		}
	}

	if (index == count) {
		if (count == 16) {
			if (list->rebuild_ticks > contact_ticks) {
				list->rebuild_ticks = contact_ticks;
			}
			return;
		}
		list->contact_ticks[index] = contact_ticks;
		list->obj_idx[index] = candidate_obj_idx;
		++list->count;
		return;
	}

	move_index = count;
	if (count == 16) {
		displaced_score = list->contact_ticks[count - 1];
		if (list->rebuild_ticks > displaced_score) {
			list->rebuild_ticks = displaced_score;
		}
		--count;
		move_index = count;
		list->count = count;
	}
	while (move_index > index) {
		list->contact_ticks[move_index] =
			list->contact_ticks[move_index - 1];
		list->obj_idx[move_index] = list->obj_idx[move_index - 1];
		--move_index;
	}
	list->contact_ticks[index] = contact_ticks;
	list->obj_idx[index] = candidate_obj_idx;
	++list->count;
}

/* Returns the speed the proximity lists assume for obj_idx, shifted left
 * 12: for a space craft (family 0) the larger of its model's max_speed and
 * its speed; for a weapon (family 1) its guidance cruise_speed, or its speed
 * without a guidance record; 0 for any other family or without a
 * mobile_object. */
// FUNCTION: XVT 0x41BA60
int collide_get_mobile_object_proximity_speed_q12(uint16_t obj_idx)
{
	struct mobile_object *mobile_object;
	int speed;
	uint16_t current_speed;

	mobile_object = g_object_table[obj_idx].mobj;
	if (mobile_object == NULL) {
		return 0;
	}

	switch (mobile_object->family) {
	case 0:
		speed = g_model_defs[get_model_index_from_type(
					     g_object_table[obj_idx]
						     .object_type)]
				.max_speed;
		mobile_object = g_object_table[obj_idx].mobj;
		current_speed = mobile_object->speed;
		if (speed < (uint16_t)current_speed) {
			speed = current_speed;
		}
		break;

	case 1:
		if (mobile_object->p_warhead_guidance != NULL) {
			return mobile_object->p_warhead_guidance->cruise_speed
			       << 12;
		}
		speed = mobile_object->speed;
		break;

	default:
		return 0;
	}

	return speed << 12;
}

/* Empties the proximity list of obj_idx (count and rebuild_ticks 0, so it is
 * rebuilt on the next pass). For a slot without a mobile_object, a static
 * object, it instead adds obj_idx to the list of every player's craft. */
// FUNCTION: XVT 0x41BB00
void collide_reset_object_proximity_for_slot(uint16_t obj_idx)
{
	struct mobile_object *mobile_object;
	int owner_obj_idx;
	int object_index;
	struct object_record *object;

	mobile_object = g_object_table[obj_idx].mobj;
	if (mobile_object != NULL) {
		mobile_object->proximity_list.rebuild_ticks = 0;
		g_object_table[obj_idx].mobj->proximity_list.count = 0;
		return;
	}

	owner_obj_idx = g_active_region_object_slot_start;
	if (g_active_region_craft_object_slot_end <= owner_obj_idx) {
		return;
	}
	object_index = g_active_region_object_slot_start;
	do {
		object = &g_object_table[object_index];
		if (object->object_type != 0 &&
		    object->player_owner_idx != -1) {
			mobile_object = object->mobj;
			if (mobile_object != NULL) {
				collide_insert_mobile_object_proximity_candidate(
					&mobile_object->proximity_list,
					(uint16_t)owner_obj_idx, obj_idx);
			}
		}
		++object_index;
		++owner_obj_idx;
	} while (owner_obj_idx < g_active_region_craft_object_slot_end);
}

/* Calls collide_reset_object_proximity_for_slot for every object in the
 * proximity list of object_index, so each rebuilds its own; leaves
 * object_index's list as it is. Does nothing without a mobile_object. */
// FUNCTION: XVT 0x41BBA0
void collide_reset_neighbor_proximity_lists(uint16_t object_index)
{
	struct mobile_object *mobile_object;
	int count;
	int proximity_index;

	mobile_object = g_object_table[object_index].mobj;
	if (mobile_object == NULL) {
		return;
	}

	count = mobile_object->proximity_list.count;
	if (count <= 0) {
		return;
	}

	proximity_index = 0;
	do {
		collide_reset_object_proximity_for_slot(
			g_object_table[object_index]
				.mobj->proximity_list.obj_idx[proximity_index]);
		++proximity_index;
		--count;
	} while (count != 0);
}

/* Takes candidate_obj_idx out of list, moving later entries up; does nothing
 * when it is not there. */
// FUNCTION: XVT 0x41BBF0
void collide_remove_mobile_object_proximity_candidate(
	struct mobile_object_proximity_list *list, uint16_t candidate_obj_idx)
{
	int index;

	index = 0;
	if (list->count != 0) {
		do {
			if (list->obj_idx[index] == candidate_obj_idx) {
				break;
			}
			++index;
		} while (index < list->count);
	}
	if (index == list->count) {
		return;
	}
	++index;
	if (index < list->count) {
		do {
			list->obj_idx[index - 1] = list->obj_idx[index];
			list->contact_ticks[index - 1] =
				list->contact_ticks[index];
			++index;
		} while (list->count > index);
	}
	--list->count;
}

/* Bounces craft_obj_idx off other_obj_idx; does nothing unless craft_obj_idx is
 * a starfighter, transport or utility vehicle. The closing speed is the
 * size of the craft's speed less the other's speed times the cosine of
 * their heading difference folded into a quarter turn, counting the
 * other's only when it is a craft. The craft's yaw turns toward its move
 * vector plus a push away from the other along the line between them
 * (from prevWorld* when both have MobileObjects), and a shot turns it
 * further, the same way, by 8 times the shot's speed. When the other is a
 * craft it bounces the same way and records craft_obj_idx in its
 * ai_flight.impact_obj_idx; its pitch is set from the craft's new move X and
 * the craft's pitch from the other's. Each craft that bounces gets a roll
 * spin (roll_impulse_rate) of 100 times the closing speed, at most 0x7FFF.
 * Sets the craft's ai_flight.impact_obj_idx, recomputes the axes, and plays
 * hull-hit sounds. */
// FUNCTION: XVT 0x41BC50
void collide_apply_craft_impact_bounce(uint16_t craft_obj_idx,
				       uint16_t other_obj_idx)
{
	struct craft_data *craft;
	int16_t speed;
	int16_t angle;
	int impulse_x;
	int impulse_y;
	int delta_x;
	int delta_y;
	int delta_z;
	int distance_squared;
	uint16_t saved_yaw;
	int16_t force_x;
	int16_t force_y;
	int16_t force_z;
	int16_t move_x;
	int16_t move_y;

	if (g_object_table[craft_obj_idx].genus_id != CRAFT_GENUS_STARFIGHTER &&
	    g_object_table[craft_obj_idx].genus_id != CRAFT_GENUS_TRANSPORT &&
	    g_object_table[craft_obj_idx].genus_id !=
		    CRAFT_GENUS_UTILITY_VEHICLE) {
		return;
	}

	craft = g_object_table[craft_obj_idx].mobj->p_craft;
	craft->ai_flight.impact_obj_idx = other_obj_idx;
	speed = (int16_t)g_object_table[craft_obj_idx].mobj->speed;
	angle = (int16_t)(g_object_table[other_obj_idx].yaw -
			  g_object_table[craft_obj_idx].yaw);
	if ((uint16_t)angle >= 0x8000) {
		angle = (int16_t)-angle;
	}
	if ((uint16_t)angle > 0x4000) {
		angle = (int16_t)(0x8000 - angle);
	}
	if (g_active_region_craft_object_slot_end > other_obj_idx) {
		if ((uint16_t)angle < 0x4000) {
			speed = (int16_t)(speed -
					  trig2_cosinewordmult(
						  g_object_table[other_obj_idx]
							  .mobj->speed,
						  angle));
		} else {
			speed = (int16_t)(speed +
					  trig2_cosinewordmult(
						  g_object_table[other_obj_idx]
							  .mobj->speed,
						  angle));
		}
	}
	if (speed < 0) {
		speed = (int16_t)-speed;
	}

	if (g_object_table[craft_obj_idx].mobj != NULL &&
	    g_object_table[other_obj_idx].mobj != NULL) {
		delta_x = g_object_table[craft_obj_idx].mobj->prev_world_x -
			  g_object_table[other_obj_idx].mobj->prev_world_x;
		delta_y = g_object_table[craft_obj_idx].mobj->prev_world_y -
			  g_object_table[other_obj_idx].mobj->prev_world_y;
		impulse_x = delta_x;
		impulse_y = delta_y;
		delta_z = g_object_table[craft_obj_idx].mobj->prev_world_z -
			  g_object_table[other_obj_idx].mobj->prev_world_z;
	} else {
		delta_x = g_object_table[craft_obj_idx].world_x -
			  g_object_table[other_obj_idx].world_x;
		delta_y = g_object_table[craft_obj_idx].world_y -
			  g_object_table[other_obj_idx].world_y;
		impulse_x = delta_x;
		impulse_y = delta_y;
		delta_z = g_object_table[craft_obj_idx].world_z -
			  g_object_table[other_obj_idx].world_z;
	}
	/* Preserve 32-bit wrapping before signed comparisons and division. */
	distance_squared = (int32_t)((uint32_t)delta_z * (uint32_t)delta_z +
				     (uint32_t)impulse_y * (uint32_t)impulse_y +
				     (uint32_t)delta_x * (uint32_t)delta_x);
	if (distance_squared > 50) {
		/* From here impulse_x and impulse_y hold the offsets times 1000 times speed, before the division by
		 * distance_squared; the other craft's bounce below multiplies them by 1000 and speed again. */
		impulse_x = (int32_t)(1000u * (uint32_t)speed *
				      (uint32_t)impulse_x);
		force_x = (int16_t)(impulse_x / distance_squared);
		impulse_y = (int32_t)(1000u * (uint32_t)speed *
				      (uint32_t)impulse_y);
		force_y = (int16_t)(impulse_y / distance_squared);
		force_z = (int16_t)((int32_t)(1000u * (uint32_t)speed *
					      (uint32_t)delta_z) /
				    distance_squared);
	} else {
		force_x = 0;
		force_y = 100;
	}

	if (g_object_table[craft_obj_idx].mobj->move_vector_dirty != 0) {
		fview_calcrotatemove(g_object_table[craft_obj_idx].pitch,
				     g_object_table[craft_obj_idx].yaw,
				     &g_object_table[craft_obj_idx]);
	}
	move_x = g_object_table[craft_obj_idx].mobj->move_x;
	force_x = (int16_t)(force_x + move_x);
	if (((force_x ^ move_x) & 0x8000) != 0) {
		force_x = move_x;
	}
	move_y = g_object_table[craft_obj_idx].mobj->move_y;
	force_y = (int16_t)(force_y + move_y);
	if (((force_y ^ move_y) & 0x8000) != 0) {
		force_y = move_y;
	}
	saved_yaw = g_object_table[craft_obj_idx].yaw;
	g_object_table[craft_obj_idx].yaw = trig2_arctan(force_x, force_y);

	if (other_obj_idx >= g_projectile_object_slot_start &&
	    other_obj_idx < g_projectile_object_slot_end) {
		int16_t yaw_step =
			(int16_t)(8 *
				  g_object_table[other_obj_idx].mobj->speed);
		if (saved_yaw > g_object_table[craft_obj_idx].yaw) {
			yaw_step = (int16_t)-yaw_step;
		}
		g_object_table[craft_obj_idx].yaw =
			(int16_t)(g_object_table[craft_obj_idx].yaw + yaw_step);
	}

	if (g_active_region_craft_object_slot_end > other_obj_idx) {
		struct craft_data *other_craft =
			g_object_table[other_obj_idx].mobj->p_craft;
		int16_t roll_impulse;

		g_object_table[other_obj_idx].pitch = trig2_w_arccos(force_x);
		other_craft->pitch = g_object_table[other_obj_idx].pitch;
		roll_impulse = (int16_t)(speed * 100);
		if ((uint16_t)roll_impulse >= 0x8000) {
			roll_impulse = 0x7FFF;
		}
		if (g_object_table[other_obj_idx].yaw > saved_yaw) {
			roll_impulse = (int16_t)-roll_impulse;
		}
		g_object_table[other_obj_idx].mobj->roll_impulse_rate =
			roll_impulse;
		fview_calcrotatemove(g_object_table[other_obj_idx].pitch,
				     g_object_table[other_obj_idx].yaw,
				     &g_object_table[other_obj_idx]);
		fview_calcrotateorient(g_object_table[other_obj_idx].roll, 0,
				       &g_object_table[other_obj_idx]);
	}

	if (g_active_region_craft_object_slot_end > other_obj_idx) {
		int16_t bounce_x;
		int16_t bounce_y;

		g_object_table[other_obj_idx]
			.mobj->p_craft->ai_flight.impact_obj_idx =
			craft_obj_idx;
		distance_squared =
			(int32_t)((uint32_t)delta_z * (uint32_t)delta_z +
				  (uint32_t)delta_y * (uint32_t)delta_y +
				  (uint32_t)delta_x * (uint32_t)delta_x);
		if (distance_squared > 50) {
			bounce_x = (int16_t)((int32_t)(1000u * (uint32_t)speed *
						       (uint32_t)impulse_x) /
					     distance_squared);
			bounce_y = (int16_t)((int32_t)(1000u * (uint32_t)speed *
						       (uint32_t)impulse_y) /
					     distance_squared);
		} else {
			bounce_x = 0;
			bounce_y = 100;
		}
		if (g_object_table[other_obj_idx].mobj->move_vector_dirty !=
		    0) {
			fview_calcrotatemove(
				g_object_table[other_obj_idx].pitch,
				g_object_table[other_obj_idx].yaw,
				&g_object_table[other_obj_idx]);
		}
		bounce_x =
			(int16_t)(bounce_x +
				  g_object_table[other_obj_idx].mobj->move_x);
		bounce_y =
			(int16_t)(bounce_y +
				  g_object_table[other_obj_idx].mobj->move_y);
		saved_yaw = g_object_table[other_obj_idx].yaw;
		g_object_table[other_obj_idx].yaw =
			trig2_arctan(bounce_x, bounce_y);
		g_object_table[craft_obj_idx].pitch = trig2_w_arccos(bounce_x);
		craft->pitch = g_object_table[craft_obj_idx].pitch;
	}

	/* From here speed holds this craft's roll impulse: the closing speed times 100, capped at 0x7FFF and
	 * negated when the new yaw is above the saved one, as roll_impulse is for the other craft above. */
	speed = (int16_t)(speed * 100);
	if ((uint16_t)speed >= 0x8000) {
		speed = 0x7FFF;
	}
	if (g_object_table[craft_obj_idx].yaw > saved_yaw) {
		speed = (int16_t)-speed;
	}
	g_object_table[craft_obj_idx].mobj->roll_impulse_rate = speed;
	fview_calcrotatemove(g_object_table[craft_obj_idx].pitch,
			     g_object_table[craft_obj_idx].yaw,
			     &g_object_table[craft_obj_idx]);
	fview_calcrotateorient(g_object_table[craft_obj_idx].roll, 0,
			       &g_object_table[craft_obj_idx]);

	if (other_obj_idx < g_projectile_object_slot_end) {
		fview_calcrotatemove(g_object_table[other_obj_idx].pitch,
				     g_object_table[other_obj_idx].yaw,
				     &g_object_table[other_obj_idx]);
		fview_calcrotateorient(g_object_table[other_obj_idx].roll, 0,
				       &g_object_table[other_obj_idx]);
	}

	fsfx_play_sound(FLIGHT_SOUND_HULL_HIT_1, craft_obj_idx,
			g_object_table[craft_obj_idx].player_owner_idx);
	if (g_active_region_craft_object_slot_end > other_obj_idx) {
		fsfx_play_sound(FLIGHT_SOUND_HULL_HIT_2, other_obj_idx,
				g_object_table[craft_obj_idx].player_owner_idx);
	}
}

/* Tests whether source_obj_idx, swept from g_collisionSegmentStartWorld* to
 * g_collisionProbeWorld*, meets target_obj_idx, swept from
 * g_collisionSweepStart* to g_collisionSweepEnd*. Returns 0 for no hit, -1
 * for a hit by the box test, or the 1-based mesh ordinal of a polygon hit.
 * Rejects when the end points lie farther apart, on an axis or by rough
 * distance, than both max_bounds_extent plus COLLISION_MARGIN (0x20000), or
 * when the two sweeps plus the target's extent cannot close the gap. The
 * target's extent is halved for a shot other than a countermeasure; for a
 * starfighter hit by a shot it is half again larger while its shields are
 * up (set to 1,094 when that passes 1,095), and in internet play, for a
 * player's cannon shot on a player's starfighter, it grows by craft type
 * (doubled for an A-wing, TIE Interceptor or TIE Advanced, 1.75 times for
 * a TIE Fighter, unchanged for a Y-wing or B-wing, else 1.5 times) and the
 * polygon test is skipped. A target extent of LARGE_MODEL_EXTENT (1,095)
 * or more, or a Container Class H, gets the polygon test
 * (collide_check_swept_model_collision): under g_collision_is_aim_prediction,
 * which it clears, only for a target slower than 40, falling back to the
 * box test; otherwise a source that has not moved gets 0. Every other case
 * gets the box test, collide_checkboxcollision with 3/8 of the extent.
 * Writes g_approx_dist. */
// FUNCTION: XVT 0x41C1D0
int16_t collide_test_swept_pair_collision(uint16_t source_obj_idx,
					  uint16_t target_obj_idx)
{
	enum { COLLISION_MARGIN = 0x20000, LARGE_MODEL_EXTENT = 1095 };

	int max_distance;
	int dx;
	int dy;
	int dz;
	unsigned int target_distance;
	int source_distance;
	int use_detailed_collision = 1;
	int sweep_distance;
	struct object_record *target;
	struct object_record *source;
	unsigned int target_object_type;
	int source_object_type;
	uint8_t source_genus;
	int max_extent;

	max_distance =
		g_object_type_table[g_object_table[target_obj_idx].object_type]
			.max_bounds_extent +
		COLLISION_MARGIN;
	max_distance +=
		g_object_type_table[g_object_table[source_obj_idx].object_type]
			.max_bounds_extent;
	g_approx_dist = max_distance;
	dx = g_collision_probe_world_x - g_collision_sweep_end_x;

	if (dx < 0) {
		dx = -dx;
	}
	if (dx > max_distance) {
		return 0;
	}
	dy = g_collision_probe_world_y - g_collision_sweep_end_y;
	if (dy < 0) {
		dy = -dy;
	}
	if (dy > max_distance) {
		return 0;
	}
	dz = g_collision_probe_world_z - g_collision_sweep_end_z;
	if (dz < 0) {
		dz = -dz;
	}
	if (dz > max_distance) {
		return 0;
	}
	target_distance = collide_roughdistance3du(
		(unsigned int)dx, (unsigned int)dy, (unsigned int)dz);
	g_approx_dist = target_distance;
	if ((int)target_distance > max_distance) {
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
	source_distance = dx + dy + dz;

	sweep_distance = g_collision_sweep_end_x - g_collision_sweep_start_x;
	if (sweep_distance < 0) {
		sweep_distance = -sweep_distance;
	}
	dx += sweep_distance;
	sweep_distance = g_collision_sweep_end_y - g_collision_sweep_start_y;
	if (sweep_distance < 0) {
		sweep_distance = -sweep_distance;
	}
	dy += sweep_distance;
	sweep_distance = g_collision_sweep_end_z - g_collision_sweep_start_z;
	if (sweep_distance < 0) {
		sweep_distance = -sweep_distance;
	}
	dz += sweep_distance;

	target = &g_object_table[target_obj_idx];
	target_object_type = target->object_type;
	max_extent = g_object_type_table[target_object_type].max_bounds_extent;
	if (target->genus_id == CRAFT_GENUS_OTHER_PROJECTILE ||
	    target->genus_id == CRAFT_GENUS_PLAYER_PROJECTILE) {
		if (target->object_type !=
		    COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) {
			max_extent >>= 1;
		}
	} else if (target->genus_id == CRAFT_GENUS_STARFIGHTER) {
		source = &g_object_table[source_obj_idx];
		source_genus = source->genus_id;
		if (source_genus == CRAFT_GENUS_OTHER_PROJECTILE ||
		    source_genus == CRAFT_GENUS_PLAYER_PROJECTILE) {
			if (target->mobj->p_craft->shield_energy[0] +
				    target->mobj->p_craft->shield_energy[1] !=
			    0) {
				max_extent += max_extent >> 1;
				if (max_extent > LARGE_MODEL_EXTENT) {
					max_extent = LARGE_MODEL_EXTENT - 1;
				}
			}
			if (g_internet_play_enabled != 0 &&
			    target->player_owner_idx != -1 &&
			    source_genus == CRAFT_GENUS_PLAYER_PROJECTILE) {
				source_object_type = source->object_type;
				if (g_projectile_type_data.warhead_class
					    [source_object_type -
					     PROJECTILE_OBJECT_TYPE_FIRST] ==
				    0) {
					switch (target_object_type) {
					case CRAFT_SPECIES_Y_WING:
					case CRAFT_SPECIES_B_WING:
						break;
					case CRAFT_SPECIES_A_WING:
					case CRAFT_SPECIES_TIE_INTERCEPTOR:
					case CRAFT_SPECIES_TIE_ADVANCED:
						max_extent *= 2;
						break;
					case CRAFT_SPECIES_TIE_FIGHTER:
						max_extent += max_extent / 4 +
							      max_extent / 2;
						break;
					default:
						max_extent += max_extent / 2;
						break;
					}
					use_detailed_collision = 0;
				}
			}
		}
	}
	if (collide_roughdistance3du((unsigned int)(max_extent + dx),
				     (unsigned int)(max_extent + dy),
				     (unsigned int)(max_extent + dz)) <
	    target_distance) {
		return 0;
	}
	if ((use_detailed_collision != 0 && max_extent >= LARGE_MODEL_EXTENT) ||
	    target->object_type == CRAFT_SPECIES_CONTAINER_CLASS_H) {
		if (g_collision_is_aim_prediction != 0) {
			g_collision_is_aim_prediction = 0;
			if (target->mobj->speed < 40) {
				return (int16_t)
					collide_check_swept_model_collision(
						source_obj_idx, target_obj_idx);
			}
		} else {
			if (source_distance == 0) {
				return 0;
			}
			return (int16_t)collide_check_swept_model_collision(
				source_obj_idx, target_obj_idx);
		}
	}
	max_extent >>= 2;
	return (int16_t)collide_checkboxcollision(max_extent +
						  (max_extent >> 1));
}

/* Box test of a sweep pair: does the source, moving from
 * g_collisionSegmentStartWorld* to g_collisionProbeWorld*, enter the
 * axis-aligned box of half-size radius around the target, moving from
 * g_collisionSweepStart* to g_collisionSweepEnd*, during the step? Works
 * on the target's motion relative to the source, with times in 256ths of
 * the step. Returns -1 on a hit and writes g_collisionHitOffset*, the
 * source's travel up to the entry time; else returns 0. */
// FUNCTION: XVT 0x41C570
int16_t collide_checkboxcollision(int radius)
{
	int slope;
	uint16_t scale_q15;
	int t_enter;
	int t_exit;
	int t_candidate;
	int end_rel;
	int sweep_delta_x;
	int sweep_delta_y;
	int sweep_delta_z;
	int probe_delta_x;
	int probe_delta_y;
	int probe_delta_z;
	int start_rel_x;
	int start_rel_y;
	int start_rel_z;
	int delta_x;
	int delta_y;
	int delta_z;
	int x_far_numerator;
	int y_far_numerator;
	int y_near_numerator;
	int z_far_numerator;
	int z_near_numerator;

	probe_delta_x =
		g_collision_probe_world_x - g_collision_segment_start_world_x;
	probe_delta_y =
		g_collision_probe_world_y - g_collision_segment_start_world_y;
	probe_delta_z =
		g_collision_probe_world_z - g_collision_segment_start_world_z;
	sweep_delta_x = g_collision_sweep_end_x - g_collision_sweep_start_x;
	sweep_delta_y = g_collision_sweep_end_y - g_collision_sweep_start_y;
	sweep_delta_z = g_collision_sweep_end_z - g_collision_sweep_start_z;

	start_rel_x =
		g_collision_sweep_start_x - g_collision_segment_start_world_x;
	if (start_rel_x > radius) {
		x_far_numerator = radius - start_rel_x;
		delta_x = sweep_delta_x - probe_delta_x;
		if (delta_x >= 0) {
			return 0;
		}
		if (x_far_numerator < delta_x) {
			return 0;
		}
		t_exit = -(start_rel_x + radius);
	} else if (start_rel_x < -radius) {
		t_exit = -(start_rel_x + radius);
		delta_x = sweep_delta_x - probe_delta_x;
		if (delta_x < 0) {
			return 0;
		}
		if (t_exit >= delta_x) {
			return 0;
		}
		x_far_numerator = radius - start_rel_x;
	} else {
		x_far_numerator = radius - start_rel_x;
		t_exit = -(start_rel_x + radius);
		delta_x = 0;
	}

	start_rel_y =
		g_collision_sweep_start_y - g_collision_segment_start_world_y;
	if (start_rel_y > radius) {
		y_far_numerator = radius - start_rel_y;
		delta_y = sweep_delta_y - probe_delta_y;
		if (delta_y >= 0) {
			return 0;
		}
		if (y_far_numerator < delta_y) {
			return 0;
		}
		y_near_numerator = -(start_rel_y + radius);
	} else if (start_rel_y < -radius) {
		y_near_numerator = -(start_rel_y + radius);
		delta_y = sweep_delta_y - probe_delta_y;
		if (delta_y < 0) {
			return 0;
		}
		if (y_near_numerator >= delta_y) {
			return 0;
		}
		y_far_numerator = radius - start_rel_y;
	} else {
		delta_y = 0;
		y_far_numerator = radius - start_rel_y;
		y_near_numerator = -(start_rel_y + radius);
	}

	start_rel_z =
		g_collision_sweep_start_z - g_collision_segment_start_world_z;
	if (start_rel_z > radius) {
		z_far_numerator = radius - start_rel_z;
		delta_z = sweep_delta_z - probe_delta_z;
		if (delta_z >= 0) {
			return 0;
		}
		if (z_far_numerator < delta_z) {
			return 0;
		}
		z_near_numerator = -(start_rel_z + radius);
	} else if (start_rel_z < -radius) {
		z_near_numerator = -(start_rel_z + radius);
		delta_z = sweep_delta_z - probe_delta_z;
		if (delta_z < 0) {
			return 0;
		}
		if (z_near_numerator >= delta_z) {
			return 0;
		}
		z_far_numerator = radius - start_rel_z;
	} else {
		delta_z = 0;
		z_far_numerator = radius - start_rel_z;
		z_near_numerator = -(start_rel_z + radius);
	}

	x_far_numerator = (int32_t)((uint32_t)x_far_numerator << 8);
	t_exit = (int32_t)((uint32_t)t_exit << 8);
	y_far_numerator = (int32_t)((uint32_t)y_far_numerator << 8);
	y_near_numerator = (int32_t)((uint32_t)y_near_numerator << 8);
	z_far_numerator = (int32_t)((uint32_t)z_far_numerator << 8);
	z_near_numerator = (int32_t)((uint32_t)z_near_numerator << 8);

	/* Until here t_exit held the X near-face numerator, the twin of y_near_numerator and z_near_numerator; from
	 * here it holds the exit time along the sweep, in 256ths of the segment like t_enter. */
	if (delta_x == 0) {
		slope = sweep_delta_x - probe_delta_x;
		end_rel = g_collision_sweep_end_x - g_collision_probe_world_x;
		t_enter = 0;
		if (end_rel > radius) {
			t_exit = x_far_numerator / slope;
		} else if (end_rel < -radius) {
			t_exit = t_exit / slope;
		} else {
			t_exit = 255;
		}
	} else {
		t_enter = x_far_numerator / delta_x;
		t_exit = t_exit / delta_x;
		if (delta_x >= 0) {
			t_candidate = t_enter;
			t_enter = t_exit;
			t_exit = t_candidate;
		}
	}

	if (delta_y == 0) {
		slope = sweep_delta_y - probe_delta_y;
		end_rel = g_collision_sweep_end_y - g_collision_probe_world_y;
		if (end_rel > radius) {
			t_candidate = y_far_numerator / slope;
		} else if (end_rel < -radius) {
			t_candidate = y_near_numerator / slope;
		} else {
			t_candidate = 255;
		}
		if (delta_y > t_exit) {
			return 0;
		}
		if (delta_y > t_enter) {
			t_enter = delta_y;
		}
		if (t_candidate < t_enter) {
			return 0;
		}
		if (t_candidate < t_exit) {
			t_exit = t_candidate;
		}
	} else {
		t_candidate = y_far_numerator / delta_y;
		if (delta_y >= 0) {
			if (t_candidate < t_enter) {
				return 0;
			}
			if (t_candidate < t_exit) {
				t_exit = t_candidate;
			}
			t_candidate = y_near_numerator / delta_y;
			if (t_candidate > t_exit) {
				return 0;
			}
			if (t_candidate > t_enter) {
				t_enter = t_candidate;
			}
		} else {
			if (t_candidate > t_exit) {
				return 0;
			}
			if (t_candidate > t_enter) {
				t_enter = t_candidate;
			}
			t_candidate = y_near_numerator / delta_y;
			if (t_candidate < t_enter) {
				return 0;
			}
			if (t_candidate < t_exit) {
				t_exit = t_candidate;
			}
		}
	}

	if (delta_z == 0) {
		slope = sweep_delta_z - probe_delta_z;
		end_rel = g_collision_sweep_end_z - g_collision_probe_world_z;
		if (end_rel > radius) {
			t_candidate = z_far_numerator / slope;
		} else if (end_rel < -radius) {
			t_candidate = z_near_numerator / slope;
		} else {
			t_candidate = 255;
		}
		if (delta_z > t_exit) {
			return 0;
		}
		if (delta_z > t_enter) {
			t_enter = delta_z;
		}
		if (t_candidate < t_enter) {
			return 0;
		}
	} else {
		t_candidate = z_far_numerator / delta_z;
		if (delta_z >= 0) {
			if (t_candidate < t_enter) {
				return 0;
			}
			if (t_candidate < t_exit) {
				t_exit = t_candidate;
			}
			t_candidate = z_near_numerator / delta_z;
			if (t_candidate > t_exit) {
				return 0;
			}
			if (t_candidate > t_enter) {
				t_enter = t_candidate;
			}
		} else {
			if (t_candidate > t_exit) {
				return 0;
			}
			if (t_candidate > t_enter) {
				t_enter = t_candidate;
			}
			t_candidate = z_near_numerator / delta_z;
			if (t_candidate < t_enter) {
				return 0;
			}
		}
	}

	if (t_enter > 255) {
		return 0;
	}

	scale_q15 = (uint16_t)((uint16_t)t_enter << 7);
	g_collision_hit_offset_x = math_mul_q15((int)scale_q15, probe_delta_x);
	g_collision_hit_offset_y = math_mul_q15((int)scale_q15, probe_delta_y);
	g_collision_hit_offset_z = math_mul_q15((int)scale_q15, probe_delta_z);
	return -1;
}

/* Predicts whether a cannon shot from hardpoint hardpoint_index of
 * source_obj_idx would hit target_obj_idx; hud_draw_laser_cannon_indicators asks it
 * for the local player. The shot type is the model's weapon for the local
 * player's selected bank, one type higher when the slot's charge is 64 or more.
 * It sweeps the shot from the hardpoint (twice as far out on an Imperial Star
 * Destroyer) along the source's move vector for the shot's whole life, at
 * the shot's speed plus the source's, against the target (at the center of
 * the local player's selected target component, if any) moving along its
 * own move vector for the same time: collide_test_swept_pair_collision under
 * g_collision_is_aim_prediction, or static_test_swept_static_collision for a
 * static target. Returns 0 for a miss and nonzero for a hit. Saves and
 * puts back every g_collision* sweep and hit global, and leaves
 * g_collision_is_aim_prediction 0. Writes g_rotated*. */
// FUNCTION: XVT 0x41CA60
int collide_would_shot_hit_target(uint16_t source_obj_idx,
				  uint16_t target_obj_idx,
				  uint16_t hardpoint_index)
{
	enum {
		CHARGED_PROJECTILE_THRESHOLD = 64,
		PROJECTILE_SPEED_SCALE = 4660,
		PROJECTILE_SPEED_ROUNDING = 128,
		PROJECTILE_SPEED_SHIFT = 8,
		MOVE_VECTOR_SHIFT = 15,
	};

	struct collision_target_range_scratch saved_collision;
	struct object_record *source_object;
	struct craft_data *source_craft;
	struct mobile_object *source_mobile_object;
	model_index source_model_index;
	uint16_t projectile_type;
	uint16_t projectile_speed;
	int lifetime_ticks;
	int projectile_distance;
	struct object_record *target_object;
	int result;
	int source_move_x;
	int source_move_y;
	int source_move_z;
	int target_move_x;
	int target_move_y;
	int target_move_z;

	saved_collision.segment_start_world_x =
		g_collision_segment_start_world_x;
	saved_collision.segment_start_world_y =
		g_collision_segment_start_world_y;
	saved_collision.segment_start_world_z =
		g_collision_segment_start_world_z;
	saved_collision.probe_world_x = g_collision_probe_world_x;
	saved_collision.probe_world_y = g_collision_probe_world_y;
	saved_collision.probe_world_z = g_collision_probe_world_z;
	saved_collision.sweep_start_x = g_collision_sweep_start_x;
	saved_collision.sweep_start_y = g_collision_sweep_start_y;
	saved_collision.sweep_start_z = g_collision_sweep_start_z;
	saved_collision.sweep_end_x = g_collision_sweep_end_x;
	saved_collision.sweep_end_y = g_collision_sweep_end_y;
	saved_collision.sweep_end_z = g_collision_sweep_end_z;
	saved_collision.hit_offset_x = g_collision_hit_offset_x;
	saved_collision.hit_offset_y = g_collision_hit_offset_y;
	saved_collision.hit_offset_z = g_collision_hit_offset_z;

	source_object = &g_object_table[source_obj_idx];
	source_craft = source_object->mobj->p_craft;
	source_model_index = source_craft->model_index;
	projectile_type =
		g_model_defs[source_model_index].laser_group_weapon_type
			[g_players[g_local_player].selected_weapon_bank];
	if (source_craft->weapon_slots[hardpoint_index].laser_charge >=
	    CHARGED_PROJECTILE_THRESHOLD) {
		++projectile_type;
	}
	projectile_speed =
		g_projectile_type_data
			.speed[projectile_type - PROJECTILE_OBJECT_TYPE_FIRST];
	lifetime_ticks =
		SIMULATION_TICKS_PER_SECOND *
		g_projectile_type_data
			.lifetime_seconds[projectile_type -
					  PROJECTILE_OBJECT_TYPE_FIRST];
	lifetime_ticks += (uint16_t)math2_fraction(
		SIMULATION_TICKS_PER_SECOND,
		g_projectile_type_data
			.lifetime_frac_q16[projectile_type -
					   PROJECTILE_OBJECT_TYPE_FIRST]);

	g_collision_segment_start_world_x = source_object->world_x;
	g_collision_segment_start_world_y = source_object->world_y;
	g_collision_segment_start_world_z = source_object->world_z;
	pai_calcrotatedpoint(source_object,
			     g_model_defs[source_model_index]
				     .weapon_hardpoints[hardpoint_index]
				     .x,
			     g_model_defs[source_model_index]
				     .weapon_hardpoints[hardpoint_index]
				     .z,
			     g_model_defs[source_model_index]
				     .weapon_hardpoints[hardpoint_index]
				     .y);
	if (source_object->object_type ==
	    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER) {
		g_rotated_x *= 2;
		g_rotated_y *= 2;
		g_rotated_z *= 2;
	}
	g_collision_segment_start_world_x += g_rotated_x;
	g_collision_segment_start_world_y += g_rotated_y;
	g_collision_segment_start_world_z += g_rotated_z;

	source_mobile_object = source_object->mobj;
	projectile_distance =
		lifetime_ticks *
		((PROJECTILE_SPEED_SCALE *
			  (projectile_speed + source_mobile_object->speed) +
		  PROJECTILE_SPEED_ROUNDING) >>
		 PROJECTILE_SPEED_SHIFT) /
		SIMULATION_TICKS_PER_SECOND;
	if (source_mobile_object->move_vector_dirty != 0) {
		fview_calcrotatemove(source_object->pitch, source_object->yaw,
				     source_object);
	}
	source_move_x = source_object->mobj->move_x;
	source_move_x = math_mul_q15(source_move_x, projectile_distance);
	g_collision_probe_world_x =
		g_collision_segment_start_world_x + source_move_x;
	source_move_y = source_object->mobj->move_y;
	source_move_y = math_mul_q15(source_move_y, projectile_distance);
	g_collision_probe_world_y =
		g_collision_segment_start_world_y + source_move_y;
	source_move_z = source_object->mobj->move_z;
	source_move_z = math_mul_q15(source_move_z, projectile_distance);
	g_collision_probe_world_z =
		g_collision_segment_start_world_z + source_move_z;

	target_object = &g_object_table[target_obj_idx];
	if (target_object->mobj != NULL) {
		struct mobile_object **target_mobile_object_link =
			&target_object->mobj;
		int target_travel_distance;

		g_collision_sweep_start_x = target_object->world_x;
		g_collision_sweep_start_y = target_object->world_y;
		g_collision_sweep_start_z = target_object->world_z;
		if (g_players[g_local_player].selected_target_component != 0) {
			uint16_t component_index =
				(uint16_t)g_players[g_local_player]
					.selected_target_component;
			int object_type = target_object->object_type;

			pai_rotate_local_vector_to_world_scratch(
				target_object,
				model_mesh_get_center_x(object_type,
							component_index),
				model_mesh_get_center_z(object_type,
							component_index),
				-model_mesh_get_center_y(object_type,
							 component_index));
			g_collision_sweep_start_x += g_rotated_x;
			g_collision_sweep_start_y += g_rotated_y;
			g_collision_sweep_start_z += g_rotated_z;
		}

		target_travel_distance =
			lifetime_ticks *
			((PROJECTILE_SPEED_SCALE *
				  (*target_mobile_object_link)->speed +
			  PROJECTILE_SPEED_ROUNDING) >>
			 PROJECTILE_SPEED_SHIFT) /
			SIMULATION_TICKS_PER_SECOND;
		if ((*target_mobile_object_link)->move_vector_dirty != 0) {
			fview_calcrotatemove(target_object->pitch,
					     target_object->yaw, target_object);
		}
		target_move_x = (*target_mobile_object_link)->move_x;
		target_move_x =
			math_mul_q15(target_move_x, target_travel_distance);
		g_collision_sweep_end_x =
			g_collision_sweep_start_x + target_move_x;
		target_move_y = (*target_mobile_object_link)->move_y;
		target_move_y =
			math_mul_q15(target_move_y, target_travel_distance);
		g_collision_sweep_end_y =
			g_collision_sweep_start_y + target_move_y;
		target_move_z = (*target_mobile_object_link)->move_z;
		target_move_z =
			math_mul_q15(target_move_z, target_travel_distance);
		g_collision_sweep_end_z =
			g_collision_sweep_start_z + target_move_z;
		g_collision_is_aim_prediction = 1;
		result = (uint16_t)collide_test_swept_pair_collision(
			source_obj_idx, target_obj_idx);
	} else {
		g_collision_is_aim_prediction = 1;
		result = (int16_t)static_test_swept_static_collision(
			source_obj_idx, target_obj_idx);
	}

	g_collision_segment_start_world_x =
		saved_collision.segment_start_world_x;
	g_collision_segment_start_world_y =
		saved_collision.segment_start_world_y;
	g_collision_segment_start_world_z =
		saved_collision.segment_start_world_z;
	g_collision_probe_world_x = saved_collision.probe_world_x;
	g_collision_probe_world_y = saved_collision.probe_world_y;
	g_collision_probe_world_z = saved_collision.probe_world_z;
	g_collision_sweep_start_x = saved_collision.sweep_start_x;
	g_collision_sweep_start_y = saved_collision.sweep_start_y;
	g_collision_sweep_start_z = saved_collision.sweep_start_z;
	g_collision_sweep_end_x = saved_collision.sweep_end_x;
	g_collision_sweep_end_y = saved_collision.sweep_end_y;
	g_collision_sweep_end_z = saved_collision.sweep_end_z;
	g_collision_hit_offset_x = saved_collision.hit_offset_x;
	g_collision_hit_offset_y = saved_collision.hit_offset_y;
	g_collision_hit_offset_z = saved_collision.hit_offset_z;
	g_collision_is_aim_prediction = 0;
	return result;
}

/* Looks lookahead_seconds ahead for source_obj_idx, for
 * paiorder_avoidstarshiporder: sweeps it from its position along its move
 * vector by one step's travel times g_sim_steps_per_second times
 * lookahead_seconds, against each freighter, starship and platform swept
 * the same way, then against the static objects in its proximity list.
 * Returns the first object it would hit, or UINT16_MAX. Writes the
 * g_collision* sweep globals. */
// FUNCTION: XVT 0x41CFD0
uint16_t collide_craftstarshipcollision(uint16_t source_obj_idx,
					int16_t lookahead_seconds)
{
	struct object_record *source = &g_object_table[source_obj_idx];
	int16_t lookahead_frames =
		(int16_t)(g_sim_steps_per_second * lookahead_seconds);
	uint16_t movement_step;
	uint16_t object_index;
	struct mobile_object_proximity_list *proximity_list;

	g_collision_segment_start_world_x = source->world_x;
	g_collision_segment_start_world_y = source->world_y;
	g_collision_segment_start_world_z = source->world_z;
	movement_step = (uint16_t)(g_elapsed_ticks *
				   ((4660 * source->mobj->speed + 128) >> 8) /
				   SIMULATION_TICKS_PER_SECOND);
	if (source->mobj->move_vector_dirty != 0) {
		fview_calcrotatemove(source->pitch, source->yaw, source);
	}
	g_collision_probe_world_x =
		g_collision_segment_start_world_x +
		math_mul_q15(source->mobj->move_x, (int)movement_step) *
			lookahead_frames;
	g_collision_probe_world_y =
		g_collision_segment_start_world_y +
		math_mul_q15(source->mobj->move_y, (int)movement_step) *
			lookahead_frames;
	g_collision_probe_world_z =
		g_collision_segment_start_world_z +
		math_mul_q15(source->mobj->move_z, (int)movement_step) *
			lookahead_frames;
	for (object_index = (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		struct object_record *candidate = &g_object_table[object_index];
		if (candidate->object_type != 0 &&
		    object_index != source_obj_idx &&
		    (candidate->genus_id == CRAFT_GENUS_STARSHIP ||
		     candidate->genus_id == CRAFT_GENUS_PLATFORM ||
		     candidate->genus_id == CRAFT_GENUS_FREIGHTER)) {
			uint16_t candidate_step;

			g_collision_sweep_start_x = candidate->world_x;
			g_collision_sweep_start_y = candidate->world_y;
			g_collision_sweep_start_z = candidate->world_z;
			candidate_step = (uint16_t)math2_mphconvert(
				candidate->mobj->speed, g_sim_steps_per_second);
			if (candidate->mobj->move_vector_dirty != 0) {
				fview_calcrotatemove(candidate->pitch,
						     candidate->yaw, candidate);
			}
			g_collision_sweep_end_x =
				g_collision_sweep_start_x +
				math_mul_q15(candidate->mobj->move_x,
					     (int)candidate_step) *
					lookahead_frames;
			g_collision_sweep_end_y =
				g_collision_sweep_start_y +
				math_mul_q15(candidate->mobj->move_y,
					     (int)candidate_step) *
					lookahead_frames;
			g_collision_sweep_end_z =
				g_collision_sweep_start_z +
				math_mul_q15(candidate->mobj->move_z,
					     (int)candidate_step) *
					lookahead_frames;
			if (collide_test_swept_pair_collision(
				    source_obj_idx, object_index) != 0) {
				return object_index;
			}
		}
	}
	proximity_list = &g_object_table[source_obj_idx].mobj->proximity_list;
	for (object_index = 0; object_index < proximity_list->count;
	     ++object_index) {
		uint16_t candidate_index =
			proximity_list->obj_idx[object_index];
		if (candidate_index >= g_region_main_object_slot_end &&
		    candidate_index < g_region_static_object_slot_count +
					      g_region_main_object_slot_end &&
		    static_test_swept_static_collision(source_obj_idx,
						       candidate_index) != 0) {
			return candidate_index;
		}
	}
	return UINT16_MAX;
}

/* Applies a hit by shot projectile_obj_idx on craft craft_obj_idx at mesh
 * hit_mesh_index and turns the shot into an impact effect. Does nothing when
 * the shot came from the craft itself. The first hit from a team marks the
 * craft in attacked_by_team, counts an attacked outcome for its flight group
 * and, at tactical officer voice level 2, names the attacker when the
 * craft is on the local player's team and not a starfighter; a player's
 * shot scores the attacked goal once per team. With no attacker recorded,
 * a firing craft other than a freighter, starship or platform becomes
 * last_attacker_obj_idx with last_hit_mission_second (a friendly player's shots
 * on a craft other than a starfighter count only once warhead hits fill
 * the 3-bit count in attacked_by_team); with one recorded, a player's craft
 * takes each new attacker. Sets ai_flight.threat_obj_idx and counts
 * hits_this_maneuver. A warhead bounces an active craft when
 * craft_impact_bounce_enabled is set. A magnetic pulse drains a player's
 * cannons (knocking out the cannon system if it worked) or adds 3,540
 * ticks (7,080 for a craft other than a starfighter, transport or utility
 * vehicle) to an AI craft's weapon_fire_inhibit_timer; active chaff stops a
 * warhead coming from behind, with a message; any other hit goes to
 * collide_damagecraft, with the hit side for a player's craft (1, the
 * rear, when the shot travels the way the craft faces). The shot becomes
 * object type 129 (from a warhead), 132 (ion) or 131 (laser), placed at
 * g_collisionSegmentStartWorld* plus g_collisionHitOffset* and moving with
 * the craft; a hit sound plays when collide_damagecraft returned 1 or the
 * shot was a magnetic pulse. */
// FUNCTION: XVT 0x41D320
void collide_laserhitcraft(uint16_t projectile_obj_idx, uint16_t craft_obj_idx,
			   int16_t hit_mesh_index)
{
	enum {
		ATTACK_COUNT_MASK = 0x70,
		ATTACK_COUNT_SHIFT = 4,
		ATTACK_COUNT_MAX = 7,
		ATTACK_COUNT_LASER_LIMIT = 5,
		ATTACK_COUNT_WARHEAD_INCREMENT = 4,
		ATTACKED_GOAL_SCORED_MASK = 0x80,
		ATTACKED_PRESERVE_MASK = 0x8F,
		GOAL_EVENT_ATTACKED = 3,
		GOAL_SCORE_NO_REDUCTION = 1,
		MAGNETIC_PULSE_SHORT_INHIBIT_TICKS = 3540,
		MAGNETIC_PULSE_LONG_INHIBIT_TICKS = 7080,
		LASER_SYSTEM_MESSAGE_ARG = 92,
		SYSTEM_FAILED_MESSAGE_ARG = 87,
		EXPLOSION_GENUS = 13,
		EXPLOSION_FAMILY = 5,
		IMPACT_EFFECT_SUBTYPE = 2,
		WARHEAD_IMPACT_EFFECT_TYPE = 129,
		LASER_IMPACT_EFFECT_TYPE = 131,
		ION_IMPACT_EFFECT_TYPE = 132,
	};

	uint16_t source_obj_idx =
		g_object_table[projectile_obj_idx].mobj->source_obj_idx;
	uint16_t attacker_team =
		g_mission_flight_groups[g_object_table[source_obj_idx]
						.flight_group_idx]
			.fg.team;
	struct craft_data *craft;
	int8_t *attacked_by_team;
	int16_t forward_dot;
	uint16_t forward_positive;
	uint16_t hit_side;
	int8_t hit_registered;
	uint8_t projectile_object_type;

	if (source_obj_idx == craft_obj_idx) {
		return;
	}

	craft = g_object_table[craft_obj_idx].mobj->p_craft;
	attacked_by_team = &craft->attacked_by_team[attacker_team];
	if (*attacked_by_team == 0) {
		*attacked_by_team |= 1;
		++g_mission_fg_stats[g_object_table[craft_obj_idx]
					     .flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_ATTACKED];
		if (g_mission_flight_groups[g_object_table[craft_obj_idx]
						    .flight_group_idx]
			    .fg.special_cargo_craft == craft->craft_ordinal) {
			g_mission_fg_stats[g_object_table[craft_obj_idx]
						   .flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_ATTACKED] = 1;
		}

		if (g_game_config.voice_tactical_officer_level == 2 &&
		    g_mission_flight_groups[g_object_table[craft_obj_idx]
						    .flight_group_idx]
				    .fg.team ==
			    (uint16_t)g_players[g_local_player].team &&
		    g_object_table[craft_obj_idx].genus_id !=
			    CRAFT_GENUS_STARFIGHTER) {
			if (source_obj_idx <
			    g_active_region_craft_object_slot_end) {
				int projectile_type =
					g_object_table[projectile_obj_idx]
						.object_type;

				if (projectile_type ==
					    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE ||
				    projectile_type ==
					    WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE) {
					fsfx_speak_tactical_officer_event(
						TACTICAL_VOICE_STATUS,
						TACTICAL_MSG_MISSILE_ATTACKER,
						craft_obj_idx, UINT16_MAX);
				} else if (
					projectile_type ==
						WARHEAD_OBJECT_TYPE_PROTON_TORPEDO ||
					projectile_type ==
						WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO) {
					fsfx_speak_tactical_officer_event(
						TACTICAL_VOICE_STATUS,
						TACTICAL_MSG_TORPEDO_ATTACKER,
						craft_obj_idx, UINT16_MAX);
				} else if (projectile_type ==
					   WARHEAD_OBJECT_TYPE_HEAVY_ROCKET) {
					fsfx_speak_tactical_officer_event(
						TACTICAL_VOICE_STATUS,
						TACTICAL_MSG_ROCKET_ATTACKER,
						craft_obj_idx, UINT16_MAX);
				} else if (projectile_type ==
					   WARHEAD_OBJECT_TYPE_SPACE_BOMB) {
					fsfx_speak_tactical_officer_event(
						TACTICAL_VOICE_STATUS,
						TACTICAL_MSG_SPACE_BOMB_ATTACKER,
						craft_obj_idx, UINT16_MAX);
				} else {
					if (g_object_table[source_obj_idx]
						    .genus_id ==
					    CRAFT_GENUS_STARFIGHTER) {
						fsfx_speak_tactical_officer_event(
							TACTICAL_VOICE_STATUS,
							TACTICAL_MSG_STARFIGHTER_ATTACKER,
							craft_obj_idx,
							UINT16_MAX);
					} else if (g_object_table
							   [source_obj_idx]
								   .genus_id ==
						   CRAFT_GENUS_STARSHIP) {
						fsfx_speak_tactical_officer_event(
							TACTICAL_VOICE_STATUS,
							TACTICAL_MSG_STARSHIP_ATTACKER,
							craft_obj_idx,
							UINT16_MAX);
					} else {
						fsfx_speak_tactical_officer_event(
							TACTICAL_VOICE_STATUS,
							TACTICAL_MSG_UNKNOWN_ATTACKER,
							craft_obj_idx,
							UINT16_MAX);
					}
				}
			} else {
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_STATUS,
					TACTICAL_MSG_UNKNOWN_ATTACKER,
					craft_obj_idx, UINT16_MAX);
			}
		}
	}

	{
		int source_player_idx =
			g_object_table[source_obj_idx].player_owner_idx;

		if (source_player_idx != -1) {
			if (((uint8_t)*attacked_by_team &
			     ATTACKED_GOAL_SCORED_MASK) == 0) {
				uint16_t special_cargo_flag =
					g_mission_flight_groups
						[g_object_table[craft_obj_idx]
							 .flight_group_idx]
							.fg
							.special_cargo_craft ==
					craft->craft_ordinal;
				uint16_t source_team =
					(uint16_t)g_players[source_player_idx]
						.team;

				mission_apply_flight_group_goal_score(
					GOAL_EVENT_ATTACKED,
					g_object_table[craft_obj_idx]
						.flight_group_idx,
					source_player_idx,
					GOAL_SCORE_NO_REDUCTION,
					special_cargo_flag, source_team);
				mission_apply_flight_group_goal_score(
					GOAL_EVENT_ATTACKED,
					g_object_table[craft_obj_idx]
						.flight_group_idx,
					-1, GOAL_SCORE_NO_REDUCTION,
					special_cargo_flag,
					(uint16_t)g_players
						[g_object_table[source_obj_idx]
							 .player_owner_idx]
							.team);
			}
			*attacked_by_team |= ATTACKED_GOAL_SCORED_MASK;
		}
	}

	if (craft->last_attacker_obj_idx == UINT16_MAX) {
		if (source_obj_idx < g_active_region_craft_object_slot_end) {
			struct object_record *source_object =
				&g_object_table[source_obj_idx];
			uint8_t source_genus = source_object->genus_id;

			if (source_genus != CRAFT_GENUS_STARSHIP &&
			    source_genus != CRAFT_GENUS_PLATFORM &&
			    source_genus != CRAFT_GENUS_FREIGHTER) {
				uint8_t record_attacker = 1;

				if (source_object->player_owner_idx != -1) {
					int teams_hostile =
						source_object->mobj->team ==
								g_object_table[craft_obj_idx]
									.mobj
									->team
							? 0
							: g_mission_teams[source_object
										  ->mobj
										  ->team]
									  .allies[g_object_table[craft_obj_idx]
											  .mobj
											  ->team] ==
								  0;

					if (!teams_hostile &&
					    g_object_table[craft_obj_idx]
							    .genus_id !=
						    CRAFT_GENUS_STARFIGHTER) {
						uint8_t attack_count =
							((uint8_t)*attacked_by_team &
							 ATTACK_COUNT_MASK) >>
							ATTACK_COUNT_SHIFT;

						if (attack_count <
						    ATTACK_COUNT_LASER_LIMIT) {
							attack_count +=
								g_projectile_type_data.warhead_class
											[g_object_table[projectile_obj_idx]
												 .object_type -
											 PROJECTILE_OBJECT_TYPE_FIRST] !=
										0
									? ATTACK_COUNT_WARHEAD_INCREMENT
									: 0;
							if (attack_count >
							    ATTACK_COUNT_MAX) {
								attack_count =
									ATTACK_COUNT_MAX;
							}
							*attacked_by_team =
								(int8_t)(((uint8_t)*attacked_by_team &
									  ATTACKED_PRESERVE_MASK) |
									 (attack_count
									  << ATTACK_COUNT_SHIFT));
						}
						if (attack_count <
						    ATTACK_COUNT_MAX) {
							record_attacker = 0;
						}
					}
				}
				if (record_attacker == 1) {
					craft->last_attacker_obj_idx =
						source_obj_idx;
					craft->last_hit_mission_second =
						(uint16_t)mission_clock_to_seconds(
							g_mission_elapsed_clock
								.hours,
							g_mission_elapsed_clock
								.minutes,
							g_mission_elapsed_clock
								.seconds);
				}
			}
		}
	} else if (g_object_table[craft_obj_idx].player_owner_idx != -1) {
		craft->last_attacker_obj_idx = source_obj_idx;
		craft->last_hit_mission_second =
			(uint16_t)mission_clock_to_seconds(
				g_mission_elapsed_clock.hours,
				g_mission_elapsed_clock.minutes,
				g_mission_elapsed_clock.seconds);
	}

	craft->ai_flight.threat_obj_idx = source_obj_idx;
	++craft->ai_flight.hits_this_maneuver;
	if (g_object_table[craft_obj_idx].mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(g_object_table[craft_obj_idx].pitch,
				     g_object_table[craft_obj_idx].yaw,
				     &g_object_table[craft_obj_idx]);
		fview_calcrotateorient(g_object_table[craft_obj_idx].roll, 0,
				       &g_object_table[craft_obj_idx]);
	}
	forward_dot = (int16_t)math_dot3q15_wrapped(
		g_object_table[craft_obj_idx].mobj->cached_fwd_x,
		g_object_table[craft_obj_idx].mobj->cached_fwd_y,
		g_object_table[craft_obj_idx].mobj->cached_fwd_z,
		(int16_t)(g_collision_probe_world_x -
			  g_collision_segment_start_world_x),
		(int16_t)(g_collision_probe_world_y -
			  g_collision_segment_start_world_y),
		(int16_t)(g_collision_probe_world_z -
			  g_collision_segment_start_world_z));
	forward_positive = forward_dot >= 0;
	hit_side = 0;
	if (g_object_table[craft_obj_idx].player_owner_idx != -1) {
		hit_side = forward_dot >= 0;
	}

	if (g_flight_mission_state.craft_impact_bounce_enabled != 0 &&
	    craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE &&
	    g_projectile_type_data.warhead_class
			    [g_object_table[projectile_obj_idx].object_type -
			     PROJECTILE_OBJECT_TYPE_FIRST] != 0) {
		collide_apply_craft_impact_bounce(craft_obj_idx,
						  projectile_obj_idx);
	}

	projectile_object_type = g_object_table[projectile_obj_idx].object_type;
	if (projectile_object_type != WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE) {
		int chaff_intercepted = 0;

		if (craft->cm_type_id == COUNTERMEASURE_TYPE_CHAFF &&
		    craft->chaff_active_seconds != 0 &&
		    g_projectile_type_data.warhead_class
				    [projectile_object_type -
				     PROJECTILE_OBJECT_TYPE_FIRST] != 0 &&
		    forward_positive) {
			chaff_intercepted = 1;
			msg_emit_in_flight_message(
				IFMSG_369_WARHEAD_SCATTERED_BY_CHAFF_NO_DAMAGE,
				g_object_table[craft_obj_idx].player_owner_idx);
		}
		if (chaff_intercepted == 0) {
			hit_registered = (uint8_t)collide_damagecraft(
				craft_obj_idx, hit_mesh_index,
				projectile_obj_idx, hit_side);
		} else {
			hit_registered = 0;
		}
	} else {
		uint16_t previous_inhibit_timer =
			(uint16_t)craft->weapon_fire_inhibit_timer;

		if (g_object_table[craft_obj_idx].player_owner_idx != -1) {
			uint16_t weapon_slot_index;

			for (weapon_slot_index = 0;
			     weapon_slot_index < craft->laser_slot_count;
			     ++weapon_slot_index) {
				craft->weapon_slots[weapon_slot_index]
					.laser_charge = 0;
			}
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				craft->working_subsystems &=
					CRAFT_SUBSYSTEM_FLAG_CANNONS ^
					CRAFT_SUBSYSTEM_FLAGS_ALL;
				craft->system_health
					[DAMAGE_SYSTEM_03_CANNON_SYSTEM] = 0;
				craft->system_repair_seconds
					[DAMAGE_SYSTEM_03_CANNON_SYSTEM] =
					g_subsystem_repair_duration
						[DAMAGE_SYSTEM_03_CANNON_SYSTEM];
				if (g_object_table[craft_obj_idx]
						    .player_owner_idx ==
					    g_local_player &&
				    g_players[g_local_player]
						    .awaiting_new_craft == 0) {
					g_msg_arg_table[0] =
						LASER_SYSTEM_MESSAGE_ARG;
					g_msg_arg_table[1] =
						SYSTEM_FAILED_MESSAGE_ARG;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						g_local_player);
				}
			} else {
				msg_emit_in_flight_message(
					IFMSG_283_WARHEAD_IMPACT_DRAINED_ALL_CANNON_ENERGY,
					g_object_table[craft_obj_idx]
						.player_owner_idx);
			}
		} else if (g_object_table[craft_obj_idx].genus_id ==
				   CRAFT_GENUS_STARFIGHTER ||
			   g_object_table[craft_obj_idx].genus_id ==
				   CRAFT_GENUS_TRANSPORT ||
			   g_object_table[craft_obj_idx].genus_id ==
				   CRAFT_GENUS_UTILITY_VEHICLE) {
			craft->weapon_fire_inhibit_timer =
				(int16_t)(previous_inhibit_timer +
					  MAGNETIC_PULSE_SHORT_INHIBIT_TICKS);
		} else {
			craft->weapon_fire_inhibit_timer =
				(int16_t)(previous_inhibit_timer +
					  MAGNETIC_PULSE_LONG_INHIBIT_TICKS);
		}
		if ((uint16_t)craft->weapon_fire_inhibit_timer <
		    previous_inhibit_timer) {
			craft->weapon_fire_inhibit_timer = -1;
		}
		hit_registered = 1;
	}

	g_object_table[projectile_obj_idx].world_x =
		g_collision_segment_start_world_x + g_collision_hit_offset_x;
	g_object_table[projectile_obj_idx].world_y =
		g_collision_segment_start_world_y + g_collision_hit_offset_y;
	g_object_table[projectile_obj_idx].world_z =
		g_collision_segment_start_world_z + g_collision_hit_offset_z;
	projectile_object_type = g_object_table[projectile_obj_idx].object_type;
	if (g_projectile_type_data
		    .warhead_class[projectile_object_type -
				   PROJECTILE_OBJECT_TYPE_FIRST] != 0) {
		g_object_table[projectile_obj_idx].object_type =
			WARHEAD_IMPACT_EFFECT_TYPE;
	} else if (projectile_object_type == PROJECTILE_OBJECT_TYPE_ION_LASER ||
		   projectile_object_type ==
			   PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
		g_object_table[projectile_obj_idx].object_type =
			ION_IMPACT_EFFECT_TYPE;
	} else {
		g_object_table[projectile_obj_idx].object_type =
			LASER_IMPACT_EFFECT_TYPE;
	}
	g_object_table[projectile_obj_idx].genus_id = EXPLOSION_GENUS;
	g_object_table[projectile_obj_idx].mobj->family = EXPLOSION_FAMILY;
	g_object_table[projectile_obj_idx].type_specific_byte[0] =
		IMPACT_EFFECT_SUBTYPE;
	g_object_table[projectile_obj_idx].mobj->seconds_alive = 0;
	g_object_table[projectile_obj_idx].mobj->lifetime_timer = 0;
	g_object_table[projectile_obj_idx].mobj->effect_size = 0;
	g_object_table[projectile_obj_idx].mobj->speed =
		g_object_table[craft_obj_idx].mobj->speed;
	g_object_table[projectile_obj_idx].pitch =
		g_object_table[craft_obj_idx].pitch;
	g_object_table[projectile_obj_idx].yaw =
		g_object_table[craft_obj_idx].yaw;
	g_object_table[projectile_obj_idx].roll = 0;
	g_object_table[projectile_obj_idx].mobj->orient_matrix_dirty = 1;
	g_object_table[projectile_obj_idx].mobj->move_vector_dirty =
		g_object_table[projectile_obj_idx].mobj->orient_matrix_dirty;

	if (hit_registered != 0) {
		if (g_object_table[craft_obj_idx].player_owner_idx ==
		    g_local_player) {
			fsfx_play_sound(FLIGHT_SOUND_SHIELD_HIT,
					projectile_obj_idx, g_local_player);
		} else if (g_object_table[projectile_obj_idx].object_type ==
				   LASER_IMPACT_EFFECT_TYPE ||
			   g_object_table[projectile_obj_idx].object_type ==
				   ION_IMPACT_EFFECT_TYPE) {
			fsfx_play_sound(FLIGHT_SOUND_LASER_IMPACT,
					projectile_obj_idx, g_local_player);
		} else {
			fsfx_play_sound(
				(game_rand2() & 3) +
					FLIGHT_SOUND_SMALL_EXPLOSION_FIRST,
				projectile_obj_idx, g_local_player);
		}
	}
}

/* Deals damage to craft victim_obj_idx from source_obj_idx, hit on mesh
 * hit_mesh_index (1-based; -1 for none). hit_side_or_damage_amount is the shield side
 * hit (0 front, 1 rear), except with source_obj_idx UINT16_MAX - 1 (engine wash),
 * where it is the damage and the front shield takes it. Returns 1 at once while
 * g_flight_sim_side_effects_suppressed is set, or when the victim's flight group or
 * a source craft's has status 20. The damage is: engine wash as given; 0x20000
 * with no source (UINT16_MAX); collide_compute_craft_damage_amount for a craft;
 * damage_amount for another mobile object; 4 times max_bounds_extent for a static
 * object (0x20000 from 0x8000 up); divided by 16 for a starship or platform and
 * by 4 for a freighter (and, in mission file version 14, for the Muurian and
 * Corellian transports). It is tallied in damage_stats by source. A hit on a
 * mesh whose explosion type has bit 0 set goes first to craft_damage_component
 * (on difficulty 0 always, otherwise only with both shields down); an obstacle
 * takes nothing. The shield on the hit side takes what it can, and a player's
 * craft then evens its two shields. What gets through: an ion shot adds 1, 2 or
 * 4 to subsystem_damage (while under 1,000) and, once the model's system_strength
 * is within 10 of it, knocks out one working system per 200 damage, and a craft
 * left with none is disabled (shields 0, a disabled outcome, message and
 * voice); other damage adds to hull_damage, with tactical officer reports as it
 * passes thresholds, a hull-hit flash and a 1 in 4 chance of knocking out a
 * random system on a player's craft; once hull_damage has reached
 * system_damage_hull_threshold a hit can knock out HUD features instead of playing
 * the hull sound. When hull_damage reaches hull_max on a craft that is active or
 * entering hyperspace, it credits the kill, records the loss and the destroyed
 * outcome, clears carried and carrier links to it, sends messages and voice
 * lines, and starts the end: a craft without a fuselage tumbles and breaks up
 * after a time; a moving craft hit by something small or not a craft breaks up,
 * tearing off a wing when one is left on a randomly chosen side, or for a
 * player one time in 4 explodes at once; anything else explodes at once. Its
 * last branch, which holds hull_damage just under hull_max, needs
 * g_flight_sim_side_effects_suppressed set, so it never runs. Runs on the craft's
 * own random seed (ai_controller.saved_rand_seed), putting the shared one back.
 * Returns 0 when it played a hull, internal, breakup or explosion sound, else
 * 1. */
// FUNCTION: XVT 0x41DC30
int16_t collide_damagecraft(uint16_t victim_obj_idx, int16_t hit_mesh_index,
			    uint16_t source_obj_idx,
			    uint16_t hit_side_or_damage_amount)
{
	enum {
		PLAYER_COUNT = 8,
		OBJECT_TYPE_MESH_CACHE_COUNT = 73,
		MISSION_STATUS_INVULNERABLE = 20,
		SYNTHETIC_STARSHIP_SOURCE = UINT16_MAX - 1,
		DEFAULT_COLLISION_OBJECT_TYPE = 53,
		CONTAINER_CLASS_H_OBJECT_TYPE = 58,
		MAX_MODEL_BOUNDS_EXTENT = 0x8000,
		DEFAULT_COLLISION_DAMAGE = 0x20000,
		STARSHIP_DAMAGE_SCALE = 16,
		FREIGHTER_DAMAGE_SCALE = 4,
		SYSTEM_DAMAGE_LIMIT = 1000,
		SYSTEM_DISABLE_THRESHOLD = 10,
		SYSTEM_DISABLE_DAMAGE_STEP = 200,
		SHIELD_HIT_FLASH_TICKS = 59,
		HULL_HIT_FLASH_TICKS = 59,
		PLAYER_DEATH_LIFETIME_TICKS = 708,
		CRAFT_EXPLOSION_TIME_STEP_TICKS = 1180,
		CRAFT_EXPLOSION_TIME_BIAS = 1179,
		DETACH_COMPONENT_STATE = 4,
		BREAKUP_ANIMATION_STATE = 2,
		GENERIC_IMPACT_ORDER_PROBABILITY = 0x2000,
		HULL_IMPACT_ORDER_PROBABILITY = 0x4000,
		SYSTEM_FAILURE_ORDER_PROBABILITY = 0x6000,
		FRIENDLY_LOSS_ORDER_PROBABILITY = 0xC000,
		WINGMAN_KILL_ORDER_PROBABILITY = 0xA000,
	};

	int synthetic_starship_damage;
	uint8_t result;
	uint16_t saved_rand_state;
	uint16_t damage_object_type;
	uint8_t cockpit_status_dirty;
	int *shield_energy;
	uint8_t source_family;
	struct ai_controller *ai_controller;
	struct craft_data *craft;
	unsigned int damage_amount;
	int damage;
	uint16_t attacker_source_obj_idx;

	cockpit_status_dirty = 0;
	synthetic_starship_damage = 0;
	result = 1;
	if (g_flight_sim_side_effects_suppressed != 0) {
		return 1;
	}

	if (g_mission_flight_groups[g_object_table[victim_obj_idx]
					    .flight_group_idx]
			    .fg.status1 == MISSION_STATUS_INVULNERABLE ||
	    g_mission_flight_groups[g_object_table[victim_obj_idx]
					    .flight_group_idx]
			    .fg.status2 == MISSION_STATUS_INVULNERABLE) {
		return 1;
	}
	if (g_active_region_object_slot_start <= source_obj_idx &&
	    source_obj_idx < g_active_region_craft_object_slot_end) {
		int source_flight_group_idx =
			g_object_table[source_obj_idx].flight_group_idx;

		if (g_mission_flight_groups[source_flight_group_idx]
				    .fg.status1 ==
			    MISSION_STATUS_INVULNERABLE ||
		    g_mission_flight_groups[source_flight_group_idx]
				    .fg.status2 ==
			    MISSION_STATUS_INVULNERABLE) {
			return 1;
		}
	}

	craft = g_object_table[victim_obj_idx].mobj->p_craft;
	saved_rand_state = (uint16_t)g_game_rand_feedback_state;
	ai_controller = &craft->ai_controller;
	g_game_rand_feedback_state = ai_controller->saved_rand_seed;

	if (source_obj_idx == SYNTHETIC_STARSHIP_SOURCE) {
		synthetic_starship_damage = 1;
		source_obj_idx = UINT16_MAX;
		damage_object_type = DEFAULT_COLLISION_OBJECT_TYPE;
		damage_amount = hit_side_or_damage_amount;
		hit_side_or_damage_amount = 0;
	} else if (source_obj_idx == UINT16_MAX) {
		damage_object_type = DEFAULT_COLLISION_OBJECT_TYPE;
		damage_amount = DEFAULT_COLLISION_DAMAGE;
	} else {
		if (g_object_table[source_obj_idx].mobj != NULL) {
			damage_object_type =
				g_object_table[source_obj_idx].object_type;
			if (g_object_table[source_obj_idx].mobj->p_craft !=
			    NULL) {
				damage_amount =
					collide_compute_craft_damage_amount(
						victim_obj_idx, source_obj_idx);
			} else {
				damage_amount = g_object_table[source_obj_idx]
							.mobj->damage_amount;
			}
		} else {
			unsigned int max_bounds_extent;

			damage_object_type =
				g_object_table[source_obj_idx].object_type;
			max_bounds_extent =
				(unsigned int)
					g_object_type_table[damage_object_type]
						.max_bounds_extent;
			if (max_bounds_extent < MAX_MODEL_BOUNDS_EXTENT) {
				damage_amount = 4 * max_bounds_extent;
			} else {
				damage_amount = DEFAULT_COLLISION_DAMAGE;
			}
		}
	}

	if (g_object_table[victim_obj_idx].genus_id == CRAFT_GENUS_STARSHIP ||
	    g_object_table[victim_obj_idx].genus_id == CRAFT_GENUS_PLATFORM) {
		damage_amount /= STARSHIP_DAMAGE_SCALE;
	}
	if (g_object_table[victim_obj_idx].genus_id == CRAFT_GENUS_FREIGHTER) {
		damage_amount /= FREIGHTER_DAMAGE_SCALE;
	}
	if (g_mission_file_version == 14 &&
	    (g_object_table[victim_obj_idx].object_type ==
		     CRAFT_SPECIES_MUURIAN_TRANSPORT ||
	     g_object_table[victim_obj_idx].object_type ==
		     CRAFT_SPECIES_CORELLIAN_TRANSPORT)) {
		damage_amount /= FREIGHTER_DAMAGE_SCALE;
	}

	damage = (int)damage_amount;
	attacker_source_obj_idx = UINT16_MAX;
	craft->damage_stats.damage_received_total += damage_amount;
	if (source_obj_idx != attacker_source_obj_idx &&
	    g_object_table[source_obj_idx].mobj != NULL) {
		int source_player_idx;

		source_family = g_object_table[source_obj_idx].mobj->family;
		if (source_family == 0) {
			attacker_source_obj_idx = source_obj_idx;
			source_player_idx =
				g_object_table[source_obj_idx].player_owner_idx;
		} else {
			attacker_source_obj_idx = g_object_table[source_obj_idx]
							  .mobj->source_obj_idx;
			source_player_idx =
				g_object_table[attacker_source_obj_idx]
					.player_owner_idx;
			if (source_family == 1 &&
			    g_object_table[source_obj_idx]
					    .mobj->p_warhead_guidance != NULL) {
				source_player_idx =
					g_object_table[source_obj_idx]
						.mobj->p_warhead_guidance
						->source_player_idx;
			}
		}

		if (source_player_idx != -1) {
			craft->damage_stats
				.damage_from_player[source_player_idx] +=
				damage_amount;
			if (g_object_table[source_obj_idx].mobj->family == 0) {
				craft->damage_stats.damage_from_collision +=
					damage_amount;
			}
		} else {
			if (source_family == 0) {
				craft->damage_stats.damage_from_collision +=
					damage_amount;
			} else {
				if (g_object_table[attacker_source_obj_idx]
					    .genus_id ==
				    CRAFT_GENUS_STARFIGHTER) {
					int ai_skill =
						g_mission_flight_groups
							[g_object_table[attacker_source_obj_idx]
								 .flight_group_idx]
								.fg.group_ai;

					craft->damage_stats.damage_from_ai_skill
						[ai_skill] += damage_amount;
				} else {
					if (g_region_main_object_slot_end >
						    attacker_source_obj_idx ||
					    g_region_main_object_slot_end +
							    g_region_static_object_slot_count <=
						    attacker_source_obj_idx) {
						craft->damage_stats
							.damage_from_starship +=
							damage_amount;
					} else {
						craft->damage_stats
							.damage_from_mine +=
							damage_amount;
					}
				}
			}
		}
		craft->damage_stats.damage_from_flight_group_amount
			[g_object_table[attacker_source_obj_idx]
				 .flight_group_idx] += damage_amount;
	} else if (source_obj_idx != UINT16_MAX &&
		   g_object_table[source_obj_idx].mobj == NULL) {
		if (g_object_table[source_obj_idx].genus_id ==
		    CRAFT_GENUS_NORMAL_DEBRIS) {
			craft->damage_stats.damage_from_collision +=
				damage_amount;
		}
	} else if (synthetic_starship_damage != 0) {
		craft->damage_stats.damage_from_starship += damage_amount;
	}
	if (g_object_table[victim_obj_idx].player_owner_idx != -1) {
		craft->damage_stats.damage_received_by_player_owned_craft +=
			damage_amount;
	}

	if (hit_mesh_index != -1) {
		if (craft->shield_energy[0] + craft->shield_energy[1] == 0 &&
		    g_flight_mission_state.difficulty != 0) {
			if (model_mesh_has_explosion_type_bit0(
				    g_object_table[victim_obj_idx].object_type,
				    (uint16_t)hit_mesh_index - 1) != 0) {
				damage = craft_damage_component(
					victim_obj_idx, hit_mesh_index, damage,
					attacker_source_obj_idx);
			}
		} else if (g_flight_mission_state.difficulty == 0 &&
			   model_mesh_has_explosion_type_bit0(
				   g_object_table[victim_obj_idx].object_type,
				   (uint16_t)hit_mesh_index - 1) != 0) {
			damage = craft_damage_component(
				victim_obj_idx, hit_mesh_index, damage,
				attacker_source_obj_idx);
		}
	}
	if (g_object_table[victim_obj_idx].genus_id == CRAFT_GENUS_OBSTACLE) {
		damage = 0;
	}

	shield_energy = &craft->shield_energy[hit_side_or_damage_amount];
	if (damage < *shield_energy) {
		*shield_energy = *shield_energy - (int)damage;
		if (g_object_table[victim_obj_idx].player_owner_idx != -1) {
			int other_shield_energy;

			g_player_flight_transient_timers
				[g_object_table[victim_obj_idx]
					 .player_owner_idx]
					.shield_hit_flash_timer =
				SHIELD_HIT_FLASH_TICKS;
			g_last_shield_damage_side =
				(uint8_t)hit_side_or_damage_amount;
			other_shield_energy = craft->shield_energy[(
				uint16_t)(hit_side_or_damage_amount ^ 1)];
			if (*shield_energy < other_shield_energy) {
				int redistributed_energy =
					(other_shield_energy - *shield_energy) /
					2;

				craft->shield_energy[(
					uint16_t)(hit_side_or_damage_amount ^
						  1)] = other_shield_energy -
							redistributed_energy;
				*shield_energy += redistributed_energy;
				craft->shield_distrib_mode =
					SHIELD_DISTRIBUTION_EVEN;
			}
		}
		if (g_players[g_local_player].object_index != victim_obj_idx) {
			fsfx_speak_wingman_event(
				g_local_player, victim_obj_idx, 3, -1,
				victim_obj_idx,
				GENERIC_IMPACT_ORDER_PROBABILITY);
		}
	} else {
		if (g_game_config.voice_tactical_officer_level == 2 &&
		    g_object_table[victim_obj_idx].genus_id !=
			    CRAFT_GENUS_STARFIGHTER &&
		    *shield_energy != 0 && craft->hull_damage == 0) {
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS, TACTICAL_MSG_SHIELDS_OUT,
				victim_obj_idx, UINT16_MAX);
		}
		damage -= *shield_energy;
		*shield_energy = 0;
		if (g_object_table[victim_obj_idx].player_owner_idx != -1) {
			int other_shield_energy;

			if (hit_side_or_damage_amount == 0 &&
			    synthetic_starship_damage == 0 &&
			    (uint16_t)game_rand() < 0x1000u) {
				fsfx_play_sound(FLIGHT_SOUND_R2_HIT, -1,
						g_object_table[victim_obj_idx]
							.player_owner_idx);
			}
			other_shield_energy = craft->shield_energy[(
				uint16_t)(hit_side_or_damage_amount ^ 1)];
			if (other_shield_energy != 0) {
				craft->shield_energy[(
					uint16_t)(hit_side_or_damage_amount ^
						  1)] -=
					other_shield_energy / 2;
				*shield_energy += other_shield_energy / 2;
				craft->shield_distrib_mode =
					SHIELD_DISTRIBUTION_EVEN;
			}
		}

		if (damage != 0) {
			if (damage_object_type ==
				    PROJECTILE_OBJECT_TYPE_ION_LASER ||
			    damage_object_type ==
				    PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER ||
			    damage_object_type ==
				    PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER_2) {
				int16_t system_strength_remaining;

				if ((uint16_t)craft->subsystem_damage <
				    SYSTEM_DAMAGE_LIMIT) {
					if (damage_object_type ==
					    PROJECTILE_OBJECT_TYPE_ION_LASER) {
						craft->subsystem_damage += 1;
					}
					if (damage_object_type ==
					    PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
						craft->subsystem_damage += 2;
					}
					if (damage_object_type ==
					    PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER_2) {
						craft->subsystem_damage += 4;
					}
				}
				system_strength_remaining =
					(int16_t)(g_model_defs[craft->model_index]
							  .system_strength -
						  craft->subsystem_damage);
				if (craft->working_subsystems != 0 &&
				    system_strength_remaining <=
					    SYSTEM_DISABLE_THRESHOLD) {
					if (damage > 0) {
						unsigned int disable_count =
							(unsigned int)(damage +
								       SYSTEM_DISABLE_DAMAGE_STEP -
								       1) /
							SYSTEM_DISABLE_DAMAGE_STEP;

						do {
							uint16_t
								subsystem_index;
							uint16_t working_subsystems =
								craft->working_subsystems;

							for (subsystem_index =
								     0;
							     subsystem_index <
							     CRAFT_SUBSYSTEM_COUNT;
							     ++subsystem_index) {
								uint16_t subsystem_flag = g_subsystem_id_to_flag
									[subsystem_index];

								if ((working_subsystems &
								     subsystem_flag) !=
								    0) {
									cockpit_status_dirty =
										1;
									craft->working_subsystems =
										working_subsystems &
										(subsystem_flag ^
										 CRAFT_SUBSYSTEM_FLAGS_ALL);
									break;
								}
							}
							if (subsystem_index <
								    CRAFT_SUBSYSTEM_COUNT &&
							    g_object_table[victim_obj_idx]
									    .player_owner_idx !=
								    -1) {
								craft->system_health
									[subsystem_index] =
									0;
								craft->system_repair_seconds
									[subsystem_index] = g_subsystem_repair_duration
									[subsystem_index];
							}
						} while (--disable_count != 0);
					}
					if (system_strength_remaining <= 0 &&
					    g_object_table[victim_obj_idx]
							    .player_owner_idx !=
						    -1) {
						uint16_t subsystem_index;

						for (subsystem_index = 0;
						     subsystem_index <
						     CRAFT_SUBSYSTEM_COUNT;
						     ++subsystem_index) {
							uint16_t subsystem_flag = g_subsystem_id_to_flag
								[subsystem_index];

							if ((craft->working_subsystems &
							     subsystem_flag) !=
							    0) {
								craft->working_subsystems &=
									subsystem_flag ^
									CRAFT_SUBSYSTEM_FLAGS_ALL;
								craft->system_health
									[subsystem_index] =
									0;
								craft->system_repair_seconds
									[subsystem_index] = g_subsystem_repair_duration
									[subsystem_index];
							}
						}
						craft->working_subsystems = 0;
					}
					{
						int enemy_craft = 0;

						/* enemy_craft is still 0 here: the test checks for no working subsystems and the two
						 * stores clear the shields. Only after the team check below does it say whether the
						 * victim is hostile to the local player's team. */
						if (craft->working_subsystems ==
						    enemy_craft) {
							craft->subsystem_damage =
								g_model_defs[craft->model_index]
									.system_strength;
							craft->shield_energy
								[0] =
								enemy_craft;
							craft->shield_energy
								[1] =
								enemy_craft;
							msg_emit_craft_message(
								victim_obj_idx,
								craft, 137);
							if (fsfx_speak_tactical_officer_event(
								    TACTICAL_VOICE_STATUS,
								    TACTICAL_MSG_DISABLED,
								    victim_obj_idx,
								    UINT16_MAX) !=
							    0) {
								int victim_team =
									g_mission_flight_groups
										[g_object_table[victim_obj_idx]
											 .flight_group_idx]
											.fg
											.team;
								int player_team =
									(uint16_t)g_players
										[g_local_player]
											.team;

								if (victim_team !=
								    player_team) {
									enemy_craft =
										g_mission_teams[player_team]
											.allies[victim_team] ==
										0;
								}
								if (enemy_craft ==
								    0) {
									int local_object_idx =
										g_players[g_local_player]
											.object_index;

									if (local_object_idx !=
									    -1) {
										uint8_t local_object_type =
											g_object_table[local_object_idx]
												.object_type;

										fsfx_play_sound(
											(local_object_type ==
												 1 ||
											 local_object_type ==
												 2 ||
											 local_object_type ==
												 3 ||
											 local_object_type ==
												 14 ||
											 local_object_type ==
												 4)
												? FLIGHT_SOUND_R2_WARNING
												: FLIGHT_SOUND_GENERAL_WARNING,
											-1,
											g_local_player);
									}
								}
							}
							if (g_object_table[victim_obj_idx]
								    .player_owner_idx ==
							    -1) {
								unsigned int
									slot_index;

								for (slot_index =
									     0;
								     slot_index <
								     craft->laser_slot_count;
								     ++slot_index) {
									if (craft->weapon_slots
										    [slot_index]
											    .projectile_type_id ==
									    2) {
										craft->turret_target_states
											[slot_index]
												.target_obj_idx =
											UINT16_MAX;
									}
								}
								for (slot_index =
									     0;
								     slot_index <
								     craft->cannon_group_count;
								     ++slot_index) {
									craft->laser_state
										.link_mode
											[slot_index] =
										0;
								}
							}
							{
								int victim_flight_group_idx =
									g_object_table[victim_obj_idx]
										.flight_group_idx;

								++g_mission_fg_stats[victim_flight_group_idx]
									  .outcome_count
										  [FLIGHT_GROUP_OUTCOME_DISABLED];
								if (g_mission_flight_groups
									    [victim_flight_group_idx]
										    .fg
										    .special_cargo_craft ==
								    craft->craft_ordinal) {
									g_mission_fg_stats[victim_flight_group_idx]
										.special_cargo_outcome
											[FLIGHT_GROUP_OUTCOME_DISABLED] =
										1;
								}
							}
						}
					}
				}
				if (g_object_table[victim_obj_idx]
					    .player_owner_idx ==
				    g_local_player) {
					fsfx_play_sound(FLIGHT_SOUND_SYSTEM_HIT,
							victim_obj_idx,
							g_local_player);
				}
			} else {
				unsigned int hull_damage_before;

				if (hit_mesh_index != -1 &&
				    model_mesh_is_object_type_mesh_damageable(
					    g_object_table[victim_obj_idx]
						    .object_type,
					    (uint16_t)hit_mesh_index - 1) !=
					    0) {
					damage = craft_damage_component(
						victim_obj_idx, hit_mesh_index,
						(unsigned int)damage,
						attacker_source_obj_idx);
				}
				hull_damage_before = craft->hull_damage;
				craft->hull_damage = hull_damage_before +
						     (unsigned int)damage;
				if (g_game_config.voice_tactical_officer_level ==
					    2 &&
				    g_object_table[victim_obj_idx].genus_id !=
					    CRAFT_GENUS_STARFIGHTER) {
					unsigned int threshold =
						math2_longfraction(
							craft->hull_max,
							0xF333);

					if (hull_damage_before >= threshold ||
					    craft->hull_damage < threshold) {
						threshold = math2_longfraction(
							craft->hull_max,
							0xC000);
						if (hull_damage_before >=
							    threshold ||
						    craft->hull_damage <
							    threshold) {
							threshold = math2_longfraction(
								craft->hull_max,
								0x4000);
							if (hull_damage_before <
								    threshold &&
							    craft->hull_damage >=
								    threshold) {
								fsfx_speak_tactical_officer_event(
									TACTICAL_VOICE_STATUS,
									TACTICAL_MSG_HULL_AT_75_PERCENT,
									victim_obj_idx,
									UINT16_MAX);
							}
						} else {
							fsfx_speak_tactical_officer_event(
								TACTICAL_VOICE_STATUS,
								TACTICAL_MSG_HULL_AT_25_PERCENT,
								victim_obj_idx,
								UINT16_MAX);
						}
					} else {
						fsfx_speak_tactical_officer_event(
							TACTICAL_VOICE_STATUS,
							TACTICAL_MSG_HULL_CRITICAL,
							victim_obj_idx,
							UINT16_MAX);
					}
				}
				if (g_object_table[victim_obj_idx]
						    .player_owner_idx != -1 &&
				    synthetic_starship_damage == 0) {
					g_player_flight_transient_timers
						[g_object_table[victim_obj_idx]
							 .player_owner_idx]
							.hull_hit_flash_timer +=
						HULL_HIT_FLASH_TICKS;
					if ((uint16_t)game_rand() < 0x4000u) {
						uint16_t subsystem_index =
							game_rand() & 7;
						uint16_t subsystem_flag;

						subsystem_index +=
							game_rand() & 1;
						subsystem_index +=
							game_rand() & 1;
						subsystem_flag =
							g_subsystem_id_to_flag
								[subsystem_index];

						if ((craft->working_subsystems &
						     subsystem_flag) != 0) {
							craft->working_subsystems &=
								subsystem_flag ^
								CRAFT_SUBSYSTEM_FLAGS_ALL;
							g_msg_arg_table[0] = g_subsystem_message_arg_by_id
								[subsystem_index];
							g_msg_arg_table[1] = 87;
							if (g_object_table[victim_obj_idx]
									    .player_owner_idx ==
								    g_local_player &&
							    g_players[g_local_player]
									    .awaiting_new_craft ==
								    0) {
								msg_emit_in_flight_message(
									IFMSG_086_ARG_SYSTEM_IS_ARG,
									g_local_player);
							}
							craft->system_health
								[subsystem_index] =
								0;
							craft->system_repair_seconds
								[subsystem_index] = g_subsystem_repair_duration
								[subsystem_index];
						}
					}
				}
			}

			if (craft->hull_damage <
				    craft->system_damage_hull_threshold ||
			    synthetic_starship_damage != 0) {
				if (g_object_table[victim_obj_idx]
						    .player_owner_idx ==
					    g_local_player &&
				    synthetic_starship_damage == 0) {
					if (((game_rand2() >> 8) & 0x80u) !=
					    0) {
						fsfx_play_sound(
							FLIGHT_SOUND_HULL_HIT_1,
							victim_obj_idx,
							g_local_player);
					} else {
						fsfx_play_sound(
							FLIGHT_SOUND_HULL_HIT_2,
							victim_obj_idx,
							g_local_player);
					}
					result = 0;
				}
				if (g_players[g_local_player].object_index !=
				    victim_obj_idx) {
					fsfx_speak_wingman_event(
						g_local_player, victim_obj_idx,
						3, -1, victim_obj_idx,
						HULL_IMPACT_ORDER_PROBABILITY);
				}
			} else {
				uint16_t feature_mask =
					g_subsystem_failure_hud_mask_by_random_slot
						[game_rand() & 0xF];

				if (!((g_flight_mission_state
						       .proving_grounds_mode_active !=
					       0 &&
				       feature_mask == 1) ||
				      (feature_mask &
				       craft->damage_stats
					       .installed_hud_feature_mask) ==
					      0)) {
					if (g_object_table[victim_obj_idx]
						    .player_owner_idx != -1) {
						uint8_t victim_object_type =
							g_object_table
								[victim_obj_idx]
									.object_type;

						fsfx_play_sound(
							FLIGHT_SOUND_INTERNAL_HIT,
							victim_obj_idx,
							g_object_table[victim_obj_idx]
								.player_owner_idx);
						result = 0;
						if (victim_object_type == 1 ||
						    victim_object_type == 2 ||
						    victim_object_type == 3 ||
						    victim_object_type == 14 ||
						    victim_object_type == 4) {
							switch (feature_mask) {
							case 0x2:
							case 0x4:
							case 0x8:
								feature_mask =
									0xE;
								break;
							case 0x80:
							case 0x100:
								feature_mask =
									0x180;
								break;
							case 0x200:
							case 0x400:
							case 0x800:
							case 0x1000:
								feature_mask =
									0xE00;
								break;
							}
						}
					}
					cockpit_status_dirty = 1;
					craft->damage_stats
						.active_hud_feature_mask &=
						(uint16_t)~feature_mask;
				}
				if (craft->object_kind ==
					    CRAFT_OBJECT_KIND_ACTIVE &&
				    g_players[g_local_player].object_index !=
					    victim_obj_idx) {
					fsfx_speak_wingman_event(
						g_local_player, victim_obj_idx,
						5, -1, victim_obj_idx,
						SYSTEM_FAILURE_ORDER_PROBABILITY);
				}
			}

			if (cockpit_status_dirty != 0 &&
			    g_object_table[victim_obj_idx].player_owner_idx ==
				    g_local_player &&
			    g_replay_view_mode == 0 &&
			    g_flight_sim_side_effects_suppressed == 0) {
				flight_surface_lock();
				hud_update_craft_system_status_indicators();
				flight_surface_unlock();
			}
		}
	}

	if (g_flight_sim_side_effects_suppressed == 0 &&
	    (craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE ||
	     craft->object_kind == CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE) &&
	    craft->hull_damage >= craft->hull_max) {
		int destruction_source_player_idx;
		uint16_t object_index;

		if (source_obj_idx != UINT16_MAX &&
		    g_object_table[source_obj_idx].mobj != NULL) {
			uint16_t destruction_source_obj_idx;

			if (g_object_table[source_obj_idx].mobj->family == 0) {
				destruction_source_obj_idx = source_obj_idx;
			} else {
				destruction_source_obj_idx =
					g_object_table[source_obj_idx]
						.mobj->source_obj_idx;
			}
			mission_credit_destruction_damage_contributors(
				destruction_source_obj_idx, victim_obj_idx);
		}
		destruction_source_player_idx =
			mission_record_player_craft_loss(victim_obj_idx, 0);
		if (g_object_table[victim_obj_idx].player_owner_idx != -1) {
			player_save_craft_settings(
				g_object_table[victim_obj_idx]
					.player_owner_idx);
		}
		if (g_flight_mission_state.proving_grounds_mode_active != 0) {
			if (g_object_table[victim_obj_idx].player_owner_idx ==
			    g_local_player) {
				g_flight_mission_state.mission_end_pending = 1;
			}
		} else if (g_object_table[victim_obj_idx].player_owner_idx !=
			   -1) {
			player_start_post_destruction_state(
				g_object_table[victim_obj_idx].player_owner_idx,
				attacker_source_obj_idx,
				destruction_source_player_idx);
		}
		if (g_object_table[victim_obj_idx].player_owner_idx != -1) {
			g_flight_global_countdown_timers
				.mission_arrival_trigger_scan_timer = 0;
		}

		mission_record_craft_outcome(
			victim_obj_idx,
			(uint16_t)g_object_table[victim_obj_idx]
				.flight_group_idx,
			FLIGHT_GROUP_OUTCOME_DESTROYED);
		for (object_index = g_active_region_object_slot_start;
		     object_index < g_active_region_craft_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].object_type != 0 &&
			    g_object_table[object_index]
					    .mobj->p_craft
					    ->carried_object_index ==
				    victim_obj_idx) {
				g_object_table[object_index]
					.mobj->p_craft->carried_object_index =
					UINT16_MAX;
			}
		}
		for (object_index = g_active_region_object_slot_start;
		     object_index < g_active_region_craft_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].object_type != 0 &&
			    g_object_table[object_index]
					    .mobj->p_craft->carrier_obj_idx ==
				    victim_obj_idx) {
				g_object_table[object_index]
					.mobj->p_craft->carrier_obj_idx =
					UINT16_MAX;
			}
		}
		msg_emit_craft_message(victim_obj_idx, craft, 136);

		if (g_object_table[victim_obj_idx].flight_group_idx ==
		    g_players[g_local_player].bound_flight_group_idx) {
			if (fsfx_speak_wingman_event(
				    g_local_player, victim_obj_idx, 6, -1,
				    victim_obj_idx,
				    FRIENDLY_LOSS_ORDER_PROBABILITY) == 0) {
				fsfx_speak_wingman_event(
					g_local_player, -1, 16, -1,
					victim_obj_idx,
					FRIENDLY_LOSS_ORDER_PROBABILITY);
			}
		} else if (attacker_source_obj_idx <
				   g_active_region_craft_object_slot_end &&
			   g_players[g_local_player].object_index !=
				   attacker_source_obj_idx &&
			   g_object_table[attacker_source_obj_idx]
					   .flight_group_idx ==
				   g_players[g_local_player]
					   .bound_flight_group_idx) {
			if (g_object_table[victim_obj_idx].genus_id ==
				    CRAFT_GENUS_STARFIGHTER ||
			    g_object_table[victim_obj_idx].genus_id ==
				    CRAFT_GENUS_TRANSPORT) {
				fsfx_speak_wingman_event(
					g_local_player, attacker_source_obj_idx,
					9, -1, victim_obj_idx, UINT16_MAX);
			} else {
				fsfx_speak_wingman_event(
					g_local_player, attacker_source_obj_idx,
					10, -1, victim_obj_idx,
					WINGMAN_KILL_ORDER_PROBABILITY);
			}
		}

		if (fsfx_speak_tactical_officer_event(
			    TACTICAL_VOICE_STATUS, TACTICAL_MSG_DESTROYED,
			    victim_obj_idx, UINT16_MAX) != 0) {
			int victim_team =
				g_mission_flight_groups
					[g_object_table[victim_obj_idx]
						 .flight_group_idx]
						.fg.team;
			int player_team =
				(uint16_t)g_players[g_local_player].team;
			int enemy_craft = 0;

			if (victim_team != player_team) {
				enemy_craft = g_mission_teams[player_team]
						      .allies[victim_team] == 0;
			}
			if (enemy_craft == 0) {
				int local_object_idx =
					g_players[g_local_player].object_index;

				if (local_object_idx != -1) {
					uint8_t local_object_type =
						g_object_table[local_object_idx]
							.object_type;

					fsfx_play_sound(
						(local_object_type == 1 ||
						 local_object_type == 2 ||
						 local_object_type == 3 ||
						 local_object_type == 14 ||
						 local_object_type == 4)
							? FLIGHT_SOUND_R2_WARNING
							: FLIGHT_SOUND_GENERAL_WARNING,
						-1, g_local_player);
				}
			}
		}

		{
			uint16_t victim_object_type =
				g_object_table[victim_obj_idx].object_type;

			if (model_mesh_has_fuselage(victim_object_type) == 0) {
				int model_index = craft->model_index;
				uint16_t breakup_roll_rate =
					(uint16_t)((game_rand() & 0x3FFF) +
						   0x2000);
				uint16_t max_tumble_rate =
					g_model_defs[model_index]
						.max_tumble_rate;

				while (breakup_roll_rate > max_tumble_rate) {
					breakup_roll_rate >>= 1;
				}
				if ((uint16_t)game_rand() < 0x8000u) {
					breakup_roll_rate = (uint16_t)-(
						int16_t)breakup_roll_rate;
				}
				g_object_table[victim_obj_idx]
					.mobj->roll_impulse_rate =
					(int16_t)breakup_roll_rate;
				if (g_mission_flight_groups
					    [g_object_table[victim_obj_idx]
						     .flight_group_idx]
						    .fg.craft_explosion_time !=
				    0) {
					g_object_table[victim_obj_idx]
						.mobj->lifetime_timer =
						CRAFT_EXPLOSION_TIME_STEP_TICKS *
							g_mission_flight_groups
								[g_object_table[victim_obj_idx]
									 .flight_group_idx]
									.fg
									.craft_explosion_time -
						CRAFT_EXPLOSION_TIME_BIAS;
				} else {
					g_object_table[victim_obj_idx]
						.mobj->lifetime_timer =
						SIMULATION_TICKS_PER_SECOND *
						((game_rand() & 7) + 8);
				}
				craft->object_kind =
					CRAFT_OBJECT_KIND_BREAKING_UP;
			} else if ((g_object_type_table[damage_object_type]
						    .max_bounds_extent <=
					    1095 ||
				    g_object_type_table[damage_object_type]
						    .family_id !=
					    CRAFT_FAMILY_SPACE_CRAFT) &&
				   damage_object_type !=
					   CONTAINER_CLASS_H_OBJECT_TYPE &&
				   g_object_table[victim_obj_idx].mobj->speed !=
					   0) {
				if ((uint16_t)game_rand() < 0x4000u &&
				    g_object_table[victim_obj_idx]
						    .player_owner_idx != -1) {
					g_object_table[victim_obj_idx]
						.mobj->lifetime_timer = 1;
					fsfx_play_sound(
						FLIGHT_SOUND_LARGE_EXPLOSION,
						victim_obj_idx, g_local_player);
					result = 0;
					craft->object_kind =
						CRAFT_OBJECT_KIND_EXPLODING;
				} else {
					uint16_t detached_yaw_offset;
					uint16_t mesh_count;
					uint16_t mesh_index;
					int16_t side;
					int16_t detached_roll_rate;

					if (victim_object_type <
					    OBJECT_TYPE_MESH_CACHE_COUNT) {
						mesh_count =
							(uint16_t)g_object_type_mesh_cache
								[victim_object_type]
									.mesh_count;
					} else {
						mesh_count = (uint16_t)
							model_mesh_get_object_type_mesh_count(
								victim_object_type);
					}
					detached_yaw_offset = 0;
					detached_roll_rate = 0;
					side = 0;
					if (mesh_count > 1) {
						side = (int16_t)(game_rand() &
								 1);
						for (mesh_index = 0;
						     (uint16_t)mesh_index <
						     mesh_count;
						     ++mesh_index) {
							int component_index =
								mesh_index;
							mesh_component_type
								mesh_type;

							if (victim_object_type <
							    OBJECT_TYPE_MESH_CACHE_COUNT) {
								mesh_type = model_mesh_get_cached_object_type_mesh_type(
									victim_object_type,
									component_index);
							} else {
								mesh_type = model_mesh_get_object_type_mesh_type(
									victim_object_type,
									component_index);
							}

							if (craft->component_state
									    [component_index] ==
								    0 &&
							    (mesh_type ==
								     MESH_COMPONENT_02_WING ||
							     mesh_type ==
								     MESH_COMPONENT_20_ROTATING_WING)) {
								if (side != 0) {
									if (model_mesh_get_center_x(
										    victim_object_type,
										    component_index) <
									    0) {
										break;
									}
								} else if (
									model_mesh_get_center_x(
										victim_object_type,
										component_index) >
									0) {
									break;
								}
							}
						}
						if ((uint16_t)mesh_index <
						    mesh_count) {
							uint16_t detached_component_obj_idx =
								object_spawn_detached_component(
									victim_obj_idx,
									(int16_t)
										mesh_index);
							if (detached_component_obj_idx !=
							    UINT16_MAX) {
								int16_t wing_yaw_offset;

								detached_roll_rate =
									(int16_t)((game_rand() &
										   0x3FFF) +
										  0x4000);
								wing_yaw_offset =
									(int16_t)((game_rand() &
										   0x7FF) +
										  2048);
								detached_yaw_offset =
									(uint16_t)
										wing_yaw_offset;
								if (side != 0) {
									wing_yaw_offset =
										-wing_yaw_offset;
									detached_roll_rate =
										-detached_roll_rate;
								}
								g_object_table[detached_component_obj_idx]
									.mobj
									->roll_impulse_rate =
									detached_roll_rate;
								g_object_table[detached_component_obj_idx]
									.yaw +=
									wing_yaw_offset;
								g_object_table[detached_component_obj_idx]
									.mobj
									->orient_matrix_dirty =
									1;
								g_object_table[detached_component_obj_idx]
									.mobj
									->move_vector_dirty =
									g_object_table[detached_component_obj_idx]
										.mobj
										->orient_matrix_dirty;
								g_object_table[detached_component_obj_idx]
									.type_specific_byte
										[1] =
									BREAKUP_ANIMATION_STATE;
								craft->component_state
									[mesh_index] =
									DETACH_COMPONENT_STATE;
								if (((game_rand2() >>
								      8) &
								     0x80u) !=
								    0) {
									fsfx_play_sound(
										FLIGHT_SOUND_BREAKUP_1,
										victim_obj_idx,
										g_local_player);
								} else {
									fsfx_play_sound(
										FLIGHT_SOUND_BREAKUP_2,
										victim_obj_idx,
										g_local_player);
								}
								result = 0;
							}
						}
					}

					{
						int model_index =
							craft->model_index;
						uint16_t breakup_roll_rate =
							(uint16_t)((game_rand() &
								    0x3FFF) +
								   0x2000);
						uint16_t max_tumble_rate =
							g_model_defs[model_index]
								.max_tumble_rate;

						while (breakup_roll_rate >
						       max_tumble_rate) {
							breakup_roll_rate >>= 1;
						}
						if ((uint16_t)
							    detached_roll_rate <
						    0x8000u) {
							breakup_roll_rate =
								(uint16_t)-(
									int16_t)
									breakup_roll_rate;
						}
						g_object_table[victim_obj_idx]
							.mobj
							->roll_impulse_rate =
							(int16_t)
								breakup_roll_rate;
					}
					/* Parent recoil uses half the unsigned wing deflection, opposite its direction. */
					if (detached_yaw_offset != 0) {
						detached_yaw_offset =
							(int16_t)((uint16_t)
									  detached_yaw_offset >>
								  1);
						if (side == 0) {
							detached_yaw_offset =
								-detached_yaw_offset;
						}
						g_object_table[victim_obj_idx]
							.yaw +=
							detached_yaw_offset;
						g_object_table[victim_obj_idx]
							.mobj
							->orient_matrix_dirty =
							1;
						g_object_table[victim_obj_idx]
							.mobj
							->move_vector_dirty =
							g_object_table[victim_obj_idx]
								.mobj
								->orient_matrix_dirty;
					}
					craft->object_kind =
						CRAFT_OBJECT_KIND_BREAKING_UP;
					if (g_object_table[victim_obj_idx]
						    .player_owner_idx != -1) {
						g_object_table[victim_obj_idx]
							.mobj->lifetime_timer =
							PLAYER_DEATH_LIFETIME_TICKS;
					} else {
						uint16_t random_bias =
							game_rand() & 3;

						g_object_table[victim_obj_idx]
							.mobj->lifetime_timer =
							SIMULATION_TICKS_PER_SECOND *
							(random_bias +
							 (game_rand() & 7) + 1);
					}
					craft->component_state[mesh_count] =
						BREAKUP_ANIMATION_STATE;
				}
			} else {
				g_object_table[victim_obj_idx]
					.mobj->lifetime_timer = 1;
				fsfx_play_sound(FLIGHT_SOUND_LARGE_EXPLOSION,
						victim_obj_idx, g_local_player);
				result = 0;
				craft->object_kind =
					CRAFT_OBJECT_KIND_EXPLODING;
			}
		}
	} else if ((craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE ||
		    craft->object_kind ==
			    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE) &&
		   craft->hull_damage >= craft->hull_max) {
		craft->hull_damage = craft->hull_max - 1;
	}

	ai_controller->saved_rand_seed = g_game_rand_feedback_state;
	g_game_rand_feedback_state = (int16_t)saved_rand_state;
	return (int16_t)result;
}

/* Turns the object in object_index into an explosion of explosion_object_type
 * in place: genus 13, family 5, type_specific_byte[0] 2; effect_size, speed,
 * seconds_alive, lifetime_timer, roll and roll_impulse_rate 0; axes marked for
 * recomputing. Plays one of four small-explosion sounds and returns what
 * fsfx_play_sound returns. Does not check that the slot has a
 * mobile_object. */
// FUNCTION: XVT 0x41F300
int collide_convert_object_to_explosion(unsigned int object_index,
					uint8_t explosion_object_type)
{
	g_object_table[object_index].object_type = explosion_object_type;
	g_object_table[object_index].genus_id = 13;
	g_object_table[object_index].mobj->family = 5;
	g_object_table[object_index].type_specific_byte[0] = 2;
	g_object_table[object_index].mobj->effect_size = 0;
	g_object_table[object_index].mobj->speed = 0;
	g_object_table[object_index].mobj->seconds_alive = 0;
	g_object_table[object_index].mobj->lifetime_timer = 0;
	g_object_table[object_index].roll = 0;
	g_object_table[object_index].mobj->roll_impulse_rate = 0;
	g_object_table[object_index].mobj->orient_matrix_dirty = 1;
	return fsfx_play_sound((game_rand2() & 3) +
				       FLIGHT_SOUND_SMALL_EXPLOSION_FIRST,
			       object_index, g_local_player);
}

/* Returns a quick estimate of the length of (abs_dx, abs_dy, abs_dz): the
 * largest plus a quarter of each of the others. On a tie for the largest
 * it takes abs_dz as the base, which comes out short: (100, 100, 0) gives
 * 50. Expects the values already made positive. */
// FUNCTION: XVT 0x41F3D0
unsigned int collide_roughdistance3du(unsigned int abs_dx, unsigned int abs_dy,
				      unsigned int abs_dz)
{
	if (abs_dx > abs_dy && abs_dx > abs_dz) {
		return abs_dx + (abs_dy >> 2) + (abs_dz >> 2);
	}
	if (abs_dy > abs_dx && abs_dy > abs_dz) {
		return abs_dy + (abs_dx >> 2) + (abs_dz >> 2);
	}
	return abs_dz + (abs_dx >> 2) + (abs_dy >> 2);
}

/* collide_roughdistance3du of the absolute values of dx, dy and dz, with
 * the same short result on a tie. */
// FUNCTION: XVT 0x41F410
int collide_roughdistance3d(int dx, int dy, int dz)
{
	int abs_dx;
	int abs_dy;
	int abs_dz;

	abs_dx = dx;
	if (abs_dx < 0) {
		abs_dx = (int)(0u - (unsigned int)abs_dx);
	}
	abs_dy = dy;
	if (abs_dy < 0) {
		abs_dy = (int)(0u - (unsigned int)abs_dy);
	}
	abs_dz = dz;
	if (abs_dz < 0) {
		abs_dz = (int)(0u - (unsigned int)abs_dz);
	}

	if (abs_dx > abs_dy && abs_dx > abs_dz) {
		return (int)((unsigned int)abs_dx +
			     (unsigned int)(abs_dy >> 2) +
			     (unsigned int)(abs_dz >> 2));
	}
	if (abs_dy > abs_dx && abs_dy > abs_dz) {
		return (int)((unsigned int)abs_dy +
			     (unsigned int)(abs_dx >> 2) +
			     (unsigned int)(abs_dz >> 2));
	}
	return (int)((unsigned int)abs_dz + (unsigned int)(abs_dx >> 2) +
		     (unsigned int)(abs_dy >> 2));
}

/* Nothing calls this. Tests the segment from start to end against the
 * faces of a packed model node (node_data: counts, a box, 6-byte vertices
 * whose components can point back to earlier ones, and face records with
 * Q15 normals). Returns 0 when the segment misses the box or every face,
 * else the hit nearest the end as a Q15 fraction measured from the end (0
 * at the end, 0x7FFF at the start) with its low bit set. Points within 10
 * of a face's plane count as on it. In proving grounds, with
 * stop_on_first_hit, it returns the first face plane crossed without checking
 * that the crossing lies inside the face. */
// FUNCTION: XVT 0x41F470
unsigned int collide_test_segment_against_legacy_packed_opt_node(
	const uint8_t *node_data, int start_x, int start_y, int start_z,
	int end_x, int end_y, int end_z, int stop_on_first_hit)
{
	const int16_t *node_bounds;
	int bound;
	const uint8_t *packed_vertex_data;
	const int16_t *face_record;
	const uint8_t *vertex;
	const uint8_t *component;
	unsigned int face_record_count;
	unsigned int face_index;
	unsigned int nearest_hit_fraction_q15;
	unsigned int face_vertex_count;
	int plane_signs;
	unsigned int vertex_record_count;
	int vertex_index;
	int normal_x;
	int normal_y;
	int normal_z;
	int point_x;
	int point_y;
	int point_z;
	int delta_start_x;
	int delta_start_y;
	int delta_start_z;
	int delta_end_x;
	int delta_end_y;
	int delta_end_z;
	int start_plane_distance;
	int end_plane_distance;
	int hit_fraction_q15;
	int projected_point_u;
	int projected_point_v;
	int projection_axis_u;
	int projection_axis_v;
	int point_inside_face;

	vertex_record_count = node_data[2];
	face_record_count = node_data[4];
	node_bounds = (const int16_t *)(node_data + face_record_count + 5);
	bound = node_bounds[0];
	if (start_x < node_bounds[0] && bound > end_x) {
		return 0;
	}
	bound = node_bounds[1];
	if (bound > start_y && bound > end_y) {
		return 0;
	}
	bound = node_bounds[2];
	if (bound > start_z && bound > end_z) {
		return 0;
	}
	bound = node_bounds[3];
	if (start_x > node_bounds[3] && bound < end_x) {
		return 0;
	}
	bound = node_bounds[4];
	if (bound < start_y && bound < end_y) {
		return 0;
	}
	bound = node_bounds[5];
	if (bound < start_z && bound < end_z) {
		return 0;
	}

	packed_vertex_data = (const uint8_t *)(node_bounds + 6);
	face_record = (const int16_t *)(packed_vertex_data +
					12 * vertex_record_count);
	nearest_hit_fraction_q15 = 0x7FFFFFFF;
	for (face_index = 0; face_record_count > face_index; face_index++) {
		const uint8_t *face_indices;

		normal_x = face_record[0];
		normal_y = face_record[1];
		normal_z = face_record[2];
		face_indices = (const uint8_t *)face_record + face_record[3];
		face_record += 4;
		face_vertex_count = face_indices[0] & 0x3F;
		if (face_vertex_count == 2) {
			continue;
		}
		++face_indices;

		vertex_index = face_indices[0];
		vertex = packed_vertex_data + 6 * vertex_index;
		component = vertex;
		while ((*(const uint16_t *)component & 0xFF00) == 0x7F00) {
			component -= 3 * (*(const uint16_t *)component & 0xFE);
		}
		point_x = *(const int16_t *)component;
		delta_start_x = start_x - point_x;
		delta_end_x = end_x - point_x;
		component = vertex + 2;
		while ((*(const uint16_t *)component & 0xFF00) == 0x7F00) {
			component -= 3 * (*(const uint16_t *)component & 0xFE);
		}
		point_y = *(const int16_t *)component;
		delta_start_y = start_y - point_y;
		delta_end_y = end_y - point_y;
		component = vertex + 4;
		while ((*(const uint16_t *)component & 0xFF00) == 0x7F00) {
			component -= 3 * (*(const uint16_t *)component & 0xFE);
		}
		point_z = *(const int16_t *)component;
		delta_start_z = start_z - point_z;
		delta_end_z = end_z - point_z;

		start_plane_distance = math_dot3q15(
			normal_x, normal_y, normal_z, delta_start_x,
			delta_start_y, delta_start_z);
		if (start_plane_distance > -10 && start_plane_distance < 10) {
			start_plane_distance = 0;
		}
		end_plane_distance =
			math_dot3q15(normal_x, normal_y, normal_z, delta_end_x,
				     delta_end_y, delta_end_z);
		if (end_plane_distance > -10 && end_plane_distance < 10) {
			end_plane_distance = 0;
		}
		plane_signs = start_plane_distance ^ end_plane_distance;
		if (start_plane_distance == 0 || end_plane_distance == 0) {
			plane_signs = -1;
		}
		if (plane_signs >= 0) {
			continue;
		}

		// Restore node-space endpoints after the plane-distance calculation.
		delta_start_x += point_x;
		delta_end_x += point_x;
		delta_start_y += point_y;
		delta_end_y += point_y;
		delta_start_z += point_z;
		delta_end_z += point_z;

		if (start_plane_distance == 0) {
			point_x = delta_start_x;
			projected_point_u = delta_start_y;
			projected_point_v = delta_start_z;
			hit_fraction_q15 = 0x7FFF;
		} else if (end_plane_distance == 0) {
			point_x = delta_end_x;
			projected_point_u = delta_end_y;
			projected_point_v = delta_end_z;
			hit_fraction_q15 = 0;
		} else {
			hit_fraction_q15 =
				(int)((unsigned int)end_plane_distance << 15) /
				(end_plane_distance - start_plane_distance);
			point_x = math_mul_q15(delta_start_x - delta_end_x,
					       hit_fraction_q15);
			point_x += delta_end_x;
			projected_point_u = math_mul_q15(
				delta_start_y - delta_end_y, hit_fraction_q15);
			projected_point_u += delta_end_y;
			projected_point_v = math_mul_q15(
				delta_start_z - delta_end_z, hit_fraction_q15);
			projected_point_v += delta_end_z;
		}

		if (normal_x < 0) {
			normal_x = -normal_x;
		}
		if (normal_y < 0) {
			normal_y = -normal_y;
		}
		if (normal_z < 0) {
			normal_z = -normal_z;
		}
		if (normal_z >= normal_y && normal_z >= normal_x) {
			projection_axis_u = 0;
			projection_axis_v = 1;
			projected_point_v = projected_point_u;
			projected_point_u = point_x;
		} else if (normal_y >= normal_x && normal_z <= normal_y) {
			projection_axis_u = 0;
			projection_axis_v = 2;
			projected_point_u = point_x;
		} else {
			projection_axis_u = 1;
			projection_axis_v = 2;
		}

		{
			int previous_u;
			int previous_v;
			int current_u;
			int current_v;
			int first_edge_is_nonpositive;

			vertex_index = face_indices[0];
			component = packed_vertex_data + 6 * vertex_index +
				    2 * projection_axis_u;
			while ((*(const uint16_t *)component & 0xFF00) ==
			       0x7F00) {
				component -= 3 * (*(const uint16_t *)component &
						  0xFE);
			}
			previous_u = *(const int16_t *)component;
			component = packed_vertex_data + 6 * vertex_index +
				    2 * projection_axis_v;
			while ((*(const uint16_t *)component & 0xFF00) ==
			       0x7F00) {
				component -= 3 * (*(const uint16_t *)component &
						  0xFE);
			}
			previous_v = *(const int16_t *)component;
			vertex_index = face_indices[2];
			component = packed_vertex_data + 6 * vertex_index +
				    2 * projection_axis_u;
			while ((*(const uint16_t *)component & 0xFF00) ==
			       0x7F00) {
				component -= 3 * (*(const uint16_t *)component &
						  0xFE);
			}
			current_u = *(const int16_t *)component;
			component = packed_vertex_data + 6 * vertex_index +
				    2 * projection_axis_v;
			while ((*(const uint16_t *)component & 0xFF00) ==
			       0x7F00) {
				component -= 3 * (*(const uint16_t *)component &
						  0xFE);
			}
			current_v = *(const int16_t *)component;

			first_edge_is_nonpositive =
				collide_is_legacy_projected_edge_cross_nonpositive(
					projected_point_u - previous_u,
					current_v - previous_v,
					projected_point_v - previous_v,
					current_u - previous_u);
			point_inside_face = 1;
			do {
				previous_u = current_u;
				previous_v = current_v;
				vertex_index = face_indices[4];
				component = packed_vertex_data +
					    6 * vertex_index +
					    2 * projection_axis_u;
				while ((*(const uint16_t *)component &
					0xFF00) == 0x7F00) {
					component -=
						3 *
						(*(const uint16_t *)component &
						 0xFE);
				}
				current_u = *(const int16_t *)component;
				component = packed_vertex_data +
					    6 * vertex_index +
					    2 * projection_axis_v;
				while ((*(const uint16_t *)component &
					0xFF00) == 0x7F00) {
					component -=
						3 *
						(*(const uint16_t *)component &
						 0xFE);
				}
				current_v = *(const int16_t *)component;
				if (collide_is_legacy_projected_edge_cross_nonpositive(
					    projected_point_u - previous_u,
					    current_v - previous_v,
					    projected_point_v - previous_v,
					    current_u - previous_u) !=
				    first_edge_is_nonpositive) {
					point_inside_face = 0;
					break;
				}
				face_indices += 2;
			} while (--face_vertex_count != 0);
		}

		if (g_flight_mission_state.proving_grounds_mode_active != 0 &&
		    stop_on_first_hit != 0) {
			return (unsigned int)(hit_fraction_q15 | 1);
		}
		if (point_inside_face != 0) {
			hit_fraction_q15 |= 1;
			if (nearest_hit_fraction_q15 >
			    (unsigned int)hit_fraction_q15) {
				nearest_hit_fraction_q15 =
					(unsigned int)hit_fraction_q15;
			}
		}
	}
	return nearest_hit_fraction_q15 == 0x7FFFFFFF
		       ? 0
		       : nearest_hit_fraction_q15;
}

/* Returns the damage the craft source_obj_idx deals by ramming victim_obj_idx:
 * its damage_amount; in mission file version 14, against a starship or
 * platform, plus, per warhead launcher, an eighth of the rounds in its
 * first and last slots times the warhead's damage, or 4,800,000 when the
 * source is a Dreadnaught (object type 48, genus 4) not breaking up, with
 * a space bomb launcher, hitting a Super Star Destroyer (54); then times
 * 16 when the source is a Super Star Destroyer. */
// FUNCTION: XVT 0x41FAC0
unsigned int collide_compute_craft_damage_amount(uint16_t victim_obj_idx,
						 uint16_t source_obj_idx)
{
	struct mobile_object *mobile_object;
	int projectile_type;
	unsigned int loaded_warhead_count;
	unsigned int launcher_damage;
	int weapon_slot_index;
	unsigned int launcher_index;
	int model_index;
	unsigned int damage_amount;
	struct craft_data *craft;

	mobile_object = g_object_table[source_obj_idx].mobj;
	damage_amount = mobile_object->damage_amount;
	if (g_mission_file_version == 14) {
		if (g_object_table[victim_obj_idx].genus_id == 4 ||
		    g_object_table[victim_obj_idx].genus_id == 5) {
			craft = mobile_object->p_craft;
			model_index = get_model_index_from_type(
				g_object_table[source_obj_idx].object_type);
			for (launcher_index = 0;
			     launcher_index < craft->warhead_launcher_count;
			     ++launcher_index) {
				weapon_slot_index =
					g_model_defs[model_index]
						.warhead_launcher_first_slot
							[launcher_index];
				loaded_warhead_count =
					craft->weapon_slots[weapon_slot_index]
						.ammo_count;
				weapon_slot_index =
					g_model_defs[model_index]
						.warhead_launcher_last_slot
							[launcher_index];
				loaded_warhead_count +=
					craft->weapon_slots[weapon_slot_index]
						.ammo_count;
				projectile_type = craft->warhead_slot_type_ids
							  [launcher_index];
				if (g_object_table[source_obj_idx].genus_id ==
					    4 &&
				    g_object_table[source_obj_idx]
						    .object_type == 48 &&
				    g_object_table[victim_obj_idx]
						    .object_type == 54 &&
				    craft->object_kind !=
					    CRAFT_OBJECT_KIND_BREAKING_UP &&
				    (uint8_t)projectile_type ==
					    WARHEAD_OBJECT_TYPE_SPACE_BOMB) {
					launcher_damage = 4800000;
				} else {
					launcher_damage =
						(loaded_warhead_count *
						 g_projectile_type_data.damage
							 [projectile_type -
							  PROJECTILE_OBJECT_TYPE_FIRST]) >>
						3;
				}
				damage_amount += launcher_damage;
			}
		}
	}
	if (g_object_table[source_obj_idx].object_type == 54) {
		damage_amount *= 16;
	}

	return damage_amount;
}

/* Returns 1 when edge_delta_u * point_delta_v - edge_delta_v * point_delta_u is 0
 * or more (the point-by-edge cross product is 0 or less), else 0. Only
 * collide_test_segment_against_legacy_packed_opt_node calls it, and nothing calls
 * that. */
// FUNCTION: XVT 0x426060
int collide_is_legacy_projected_edge_cross_nonpositive(int point_delta_u,
						       int edge_delta_v,
						       int point_delta_v,
						       int edge_delta_u)
{
	return point_delta_v * edge_delta_u - edge_delta_v * point_delta_u >= 0;
}

/* Polygon test of the sweep from g_collisionSegmentStartWorld* to
 * g_collisionProbeWorld* against the model of target_obj_idx. Moves the sweep
 * into the model's own axes and walks each root mesh whose box the sweep does
 * not lie wholly beside, with collide_test_sweep_against_opt_node. Skips a craft's
 * destroyed meshes (component_hp 0); when source_obj_idx is the target, a turret
 * firing past its own ship, it also skips laser turret and gun meshes and, on a
 * Super Star Destroyer, the mesh at g_turret_fire_hull_mesh_ordinal. A rotating
 * mesh is tested turned by its mesh_rotation in proving grounds or for a source
 * no player owns, and unturned for a player's source. Returns the 1-based
 * ordinal of the mesh with the nearest hit, or 0; on a hit it sets
 * g_collisionHitOffset* to the source's travel up to the hit fraction less 0.1
 * (not below 0). Returns 0 when the model will not lock, and in the modern
 * build for an object type whose asset_flags bit 0 is clear. Writes the
 * g_collideSweep* and g_collideCurrent* globals. */
/* Besides the test, this sets g_cur_craft to the target's craft when the target has one and does not restore
 * it; nothing in this function or the functions it calls reads g_cur_craft. */
// FUNCTION: XVT 0x4A5490
int collide_check_swept_model_collision(uint16_t source_obj_idx,
					uint16_t target_obj_idx)
{
	enum {
		OBJECT_TYPE_MESH_CACHE_COUNT = 73,
		SUPER_STAR_DESTROYER_OBJECT_TYPE = 54
	};

	struct object_record *target = &g_object_table[target_obj_idx];
	int world_x;
	int world_y;
	int world_z;
	int end_x;
	int start_x;
	int end_y;
	int start_y;
	int start_y_copy;
	int end_z;
	int start_z;
	int side_start;
	int fwd_start;
	int up_start;
	int side_end;
	int fwd_end;
	int up_end;
	uint16_t model_handle;
	struct optimized_poly_object *model;
	unsigned int root_index;
	struct opt_node *root;
	struct mesh_descriptor *descriptor;
	int descriptor_index;
	unsigned int descriptor_type;

	if (target->mobj != NULL) {
		g_cur_craft = target->mobj->p_craft;
	}
	world_x = target->world_x;
	end_x = g_collision_probe_world_x - world_x;
	start_x = g_collision_segment_start_world_x - world_x;
	world_y = target->world_y;
	end_y = g_collision_probe_world_y - world_y;
	start_y = g_collision_segment_start_world_y - world_y;
	start_y_copy = start_y;
	world_z = target->world_z;
	end_z = g_collision_probe_world_z - world_z;
	start_z = g_collision_segment_start_world_z - world_z;
	{
		if (target->mobj != NULL) {
			if (target->mobj->orient_matrix_dirty != 0) {
				fview_calcrotatemove(target->pitch, target->yaw,
						     target);
				fview_calcrotateorient(target->roll, 0, target);
			}
			side_end = math_dot3q15(target->mobj->cached_side_x,
						target->mobj->cached_side_y,
						target->mobj->cached_side_z,
						end_x, end_y, end_z);
			fwd_end = -math_dot3q15(target->mobj->cached_fwd_x,
						target->mobj->cached_fwd_y,
						target->mobj->cached_fwd_z,
						end_x, end_y, end_z);
			up_end = math_dot3q15(target->mobj->cached_up_x,
					      target->mobj->cached_up_y,
					      target->mobj->cached_up_z, end_x,
					      end_y, end_z);
			side_start =
				math_dot3q15(target->mobj->cached_side_x,
					     target->mobj->cached_side_y,
					     target->mobj->cached_side_z,
					     start_x, start_y_copy, start_z);
			fwd_start =
				-math_dot3q15(target->mobj->cached_fwd_x,
					      target->mobj->cached_fwd_y,
					      target->mobj->cached_fwd_z,
					      start_x, start_y_copy, start_z);
			up_start = math_dot3q15(target->mobj->cached_up_x,
						target->mobj->cached_up_y,
						target->mobj->cached_up_z,
						start_x, start_y_copy, start_z);
		} else {
			fview_calcrotatemove(target->pitch, target->yaw, NULL);
			fview_calcrotateorient(target->roll, 0, NULL);
			side_end = math_dot3q15(
				g_fview_side_x_q15, g_fview_side_y_q15,
				g_fview_side_z_q15, end_x, end_y, end_z);
			fwd_end = -math_dot3q15(
				g_fview_forward_x_q15, g_fview_forward_y_q15,
				g_fview_forward_z_q15, end_x, end_y, end_z);
			up_end = math_dot3q15(
				g_fview_up_x_q15, g_fview_up_y_q15,
				g_fview_up_z_q15, end_x, end_y, end_z);
			side_start = math_dot3q15(g_fview_side_x_q15,
						  g_fview_side_y_q15,
						  g_fview_side_z_q15, start_x,
						  start_y_copy, start_z);
			fwd_start = -math_dot3q15(
				g_fview_forward_x_q15, g_fview_forward_y_q15,
				g_fview_forward_z_q15, start_x, start_y_copy,
				start_z);
			up_start =
				math_dot3q15(g_fview_up_x_q15, g_fview_up_y_q15,
					     g_fview_up_z_q15, start_x,
					     start_y_copy, start_z);
		}
		g_collide_sweep_walker_start.x = (float)side_start;
		g_collide_sweep_walker_start.y = (float)fwd_start;
		g_collide_sweep_walker_start.z = (float)up_start;
		g_collide_sweep_walker_end.x = (float)side_end;
		g_collide_sweep_walker_end.y = (float)fwd_end;
		g_collide_sweep_walker_end.z = (float)up_end;
		g_collide_sweep_walker_start_saved =
			g_collide_sweep_walker_start;
		g_collide_sweep_walker_end_saved = g_collide_sweep_walker_end;
	}
	g_collide_sweep_hit_mesh_ordinal = 0;
	g_collide_sweep_hit_fraction = 2.0f;
#ifdef XVT_MODERN
	/* Gunner obstruction checks can include ACT explosions left in craft slots.
	 * Relocating their data as a native OPT corrupts the sprite frame table. */
	if ((g_object_type_table[target->object_type].asset_flags & 1) == 0) {
		g_collide_sweep_current_mesh_ordinal = 0;
		g_collide_current_mesh_verts_node = NULL;
		return 0;
	}
#endif
	model_handle = g_loaded_models[target->object_type];
	model = (struct optimized_poly_object *)memory_get_handle_block(
		model_handle);
	if (model == NULL) {
		return 0;
	}
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	g_collide_sweep_current_mesh_ordinal = 0;
	g_collide_current_mesh_verts_node = NULL;
	for (root_index = 0; root_index < (unsigned int)model->root_node_count;
	     ++root_index) {
		g_collide_current_mesh_rotation_angle = 0.0f;
		root = model->root_nodes[root_index];
		if (root->node_type == OPT_TEXTURE) {
			continue;
		}
		++g_collide_sweep_current_mesh_ordinal;
		if (source_obj_idx == target_obj_idx) {
			int target_type =
				g_object_table[target_obj_idx].object_type;
			int component_index =
				g_collide_sweep_current_mesh_ordinal - 1;
			int component_type;
			if (target_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
				if (component_index < 0) {
					component_type =
						MESH_COMPONENT_00_DEFAULT;
				} else {
					int count = g_object_type_mesh_cache
							    [target_type]
								    .mesh_count;
					if (component_index >= count) {
						component_index = count - 1;
					}
					component_type =
						g_object_type_mesh_cache[target_type]
							.mesh_types
								[component_index];
				}
			} else {
				component_type =
					model_mesh_get_object_type_mesh_type(
						target_type, component_index);
			}
			if (component_type == MESH_COMPONENT_04_LASR_TUR ||
			    component_type == MESH_COMPONENT_05_LASR_GUN ||
			    component_type ==
				    MESH_COMPONENT_21_ROTATING_LASR_TUR ||
			    (g_object_table[target_obj_idx].object_type ==
				     SUPER_STAR_DESTROYER_OBJECT_TYPE &&
			     g_collide_sweep_current_mesh_ordinal -
					     g_turret_fire_hull_mesh_ordinal ==
				     1)) {
				continue;
			}
		}
		if (target->mobj != NULL && target->mobj->p_craft != NULL) {
			if (target->mobj->p_craft->component_hp
				    [g_collide_sweep_current_mesh_ordinal -
				     1] == 0) {
				continue;
			}
			if (target->mobj->p_craft->mesh_rotation
					    [g_collide_sweep_current_mesh_ordinal -
					     1] == 0 ||
			    g_flight_mission_state
					    .proving_grounds_mode_active != 0 ||
			    g_object_table[source_obj_idx].player_owner_idx ==
				    -1) {
				g_collide_current_mesh_rotation_angle =
					(float)target->mobj->p_craft->mesh_rotation
						[g_collide_sweep_current_mesh_ordinal -
						 1] *
					0.024543673f;
			}
		}
		descriptor_index = g_collide_sweep_current_mesh_ordinal - 1;
		descriptor_type = g_object_table[target_obj_idx].object_type;
		if (descriptor_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
			if (descriptor_index < 0) {
				descriptor = NULL;
			} else {
				int count = g_object_type_mesh_cache
						    [descriptor_type]
							    .mesh_count;
				if (descriptor_index >= count) {
					descriptor_index = count - 1;
				}
				descriptor =
					g_object_type_mesh_cache[descriptor_type]
						.mesh_descriptors
							[descriptor_index];
			}
		} else {
			descriptor = model_mesh_get_descriptor(
				descriptor_type,
				g_collide_sweep_current_mesh_ordinal - 1);
		}
		if (descriptor != NULL &&
		    ((side_end >= (int)descriptor->box_min.x ||
		      side_start >= (int)descriptor->box_min.x) &&
		     (fwd_end >= (int)descriptor->box_min.y ||
		      fwd_start >= (int)descriptor->box_min.y) &&
		     (up_end >= (int)descriptor->box_min.z ||
		      up_start >= (int)descriptor->box_min.z) &&
		     (side_end <= (int)descriptor->box_max.x ||
		      side_start <= (int)descriptor->box_max.x) &&
		     (fwd_end <= (int)descriptor->box_max.y ||
		      fwd_start <= (int)descriptor->box_max.y) &&
		     (up_end <= (int)descriptor->box_max.z ||
		      up_start <= (int)descriptor->box_max.z))) {
			collide_test_sweep_against_opt_node(model, root);
			g_collide_sweep_walker_start =
				g_collide_sweep_walker_start_saved;
			g_collide_sweep_walker_end =
				g_collide_sweep_walker_end_saved;
		}
	}
	memory_handle_block_done_stub(model_handle);
	if (g_collide_sweep_hit_mesh_ordinal != 0) {
		g_collide_sweep_hit_fraction -= 0.1f;
		if (g_collide_sweep_hit_fraction < 0.0f) {
			g_collide_sweep_hit_fraction = 0.0f;
		}
		g_collision_hit_offset_x =
			(int)((g_collision_probe_world_x -
			       g_collision_segment_start_world_x) *
			      g_collide_sweep_hit_fraction);
		g_collision_hit_offset_y =
			(int)((g_collision_probe_world_y -
			       g_collision_segment_start_world_y) *
			      g_collide_sweep_hit_fraction);
		g_collision_hit_offset_z =
			(int)((g_collision_probe_world_z -
			       g_collision_segment_start_world_z) *
			      g_collide_sweep_hit_fraction);
	}
	return g_collide_sweep_hit_mesh_ordinal;
}

/* Walks node and its children: OPT_NODEREF links are resolved by name
 * (kept in the node once resolved while g_cache_resolved_opt_node_refs is set;
 * the modern build uses xvt_opt_resolve_cached); an OPT_MESHVERTS node
 * becomes the vertex source for the faces after it and stops the walk
 * when the sweep lies wholly beside its box; an OPT_ROTSCALE node turns
 * the sweep about its origin and axis by g_collide_current_mesh_rotation_angle;
 * an OPT_FACEGROUP node walks its first child only. A face whose plane the
 * sweep crosses, inside the face, nearer than the best so far (and, under
 * g_collide_sweep_reject_near_start_hits, at least 0.1 along) becomes the best
 * hit. Returns 0 at the end of a branch or on a missing node. */
/* Walks the model tree under node for the sweep segment. Hits are reported only through
 * g_collide_sweep_hit_mesh_ordinal and g_collide_sweep_hit_fraction. The return value is not a hit: it is 1 when the
 * segment misses a mesh's bounding box, and that 1 passes up through every parent to stop the walk. */
// FUNCTION: XVT 0x4A6080
int collide_test_sweep_against_opt_node(struct optimized_poly_object *object,
					struct opt_node *node)
{
	int child_selection;
	struct opt_packed_face_data *face_data;
	const struct opt_packed_face_record *face;
	const struct opt_vector *face_normal;
	const struct opt_vector *mesh_vertices;
	const struct opt_vector *bounds;
	int face_index;
	float hit_fraction;
	float projected_point[3];
	const struct collide_opt_rotation_scale *rotation_scale;
	uint32_t rotation_angle_bits;
	float axis_angle[4];
	float rotation_matrix[16];
	int child_index;

	for (;;) {
		if (node == NULL) {
			return 0;
		}

		child_selection = 0;
		while (node->node_type == OPT_NODEREF) {
			if (g_cache_resolved_opt_node_refs != 0) {
#ifdef XVT_MODERN
				node = xvt_opt_resolve_cached(object, node);
#else

				if (*(char *)node->payload != '\0') {
					node->p_name = (char *)
						opt_model_resolve_node_ref(
							object,
							(const char *)
								node->payload);
					*(char *)node->payload = '\0';
				}
				node = (struct opt_node *)node->p_name;
#endif
			} else {
				node = opt_model_resolve_node_ref(
					object, (const char *)node->payload);
			}
			if (node == NULL) {
				return 0;
			}
		}

		switch (node->node_type) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET:
			face_data =
				(struct opt_packed_face_data *)node->payload;
			face = face_data->records;
			face_normal = (const struct opt_vector *)&face_data
					      ->records[node->payload_count];
			mesh_vertices =
				(const struct opt_vector *)
					g_collide_current_mesh_verts_node
						->payload;
			face_index = 0;
			if (node->payload_count > 0) {
				do {
					if (collide_intersect_segment_with_face_plane(
						    &face_normal->x,
						    &mesh_vertices
							     [face->vertex_indices
								      [0]]
								     .x,
						    &g_collide_sweep_walker_start
							     .x,
						    &g_collide_sweep_walker_end
							     .x,
						    &hit_fraction) != 0 &&
					    (g_collide_sweep_reject_near_start_hits ==
						     0 ||
					     hit_fraction >= 0.1f) &&
					    g_collide_sweep_hit_fraction >
						    hit_fraction) {
						projected_point[0] =
							(g_collide_sweep_walker_end
								 .x -
							 g_collide_sweep_walker_start
								 .x) *
								hit_fraction +
							g_collide_sweep_walker_start
								.x;
						projected_point[1] =
							(g_collide_sweep_walker_end
								 .y -
							 g_collide_sweep_walker_start
								 .y) *
								hit_fraction +
							g_collide_sweep_walker_start
								.y;
						projected_point[2] =
							(g_collide_sweep_walker_end
								 .z -
							 g_collide_sweep_walker_start
								 .z) *
								hit_fraction +
							g_collide_sweep_walker_start
								.z;
						if (collide_point_in_face_polygon(
							    &face_normal->x,
							    &mesh_vertices->x,
							    face->vertex_indices,
							    projected_point) !=
						    0) {
							g_collide_sweep_hit_fraction =
								hit_fraction;
							g_collide_sweep_hit_mesh_ordinal =
								g_collide_sweep_current_mesh_ordinal;
						}
					}
					++face_normal;
					++face;
					++face_index;
				} while (node->payload_count > face_index);
			}
			break;

		case OPT_MESHVERTS:
			g_collide_current_mesh_verts_node = node;
			mesh_vertices =
				(const struct opt_vector *)node->payload;
			bounds = &mesh_vertices[node->payload_count - 2];
			if (bounds[0].x > g_collide_sweep_walker_start.x &&
			    bounds[0].x > g_collide_sweep_walker_end.x) {
				return 1;
			}
			if (bounds[0].y > g_collide_sweep_walker_start.y &&
			    bounds[0].y > g_collide_sweep_walker_end.y) {
				return 1;
			}
			if (bounds[0].z > g_collide_sweep_walker_start.z &&
			    bounds[0].z > g_collide_sweep_walker_end.z) {
				return 1;
			}
			++bounds;
			if (bounds->x < g_collide_sweep_walker_start.x &&
			    bounds->x < g_collide_sweep_walker_end.x) {
				return 1;
			}
			if (bounds->y < g_collide_sweep_walker_start.y &&
			    bounds->y < g_collide_sweep_walker_end.y) {
				return 1;
			}
			if (bounds->z < g_collide_sweep_walker_start.z &&
			    bounds->z < g_collide_sweep_walker_end.z) {
				return 1;
			}
			break;

		case OPT_FACEGROUP:
			child_selection = 1;
			break;

		case OPT_ROTSCALE:
			memcpy(&rotation_angle_bits,
			       &g_collide_current_mesh_rotation_angle,
			       sizeof(rotation_angle_bits));
			if ((rotation_angle_bits & 0x7FFFFFFFu) != 0) {
				rotation_scale =
					(const struct collide_opt_rotation_scale
						 *)node->payload;
				g_collide_sweep_walker_start.x -=
					rotation_scale->origin.x;
				g_collide_sweep_walker_start.y -=
					rotation_scale->origin.y;
				g_collide_sweep_walker_start.z -=
					rotation_scale->origin.z;
				g_collide_sweep_walker_end.x -=
					rotation_scale->origin.x;
				g_collide_sweep_walker_end.y -=
					rotation_scale->origin.y;
				g_collide_sweep_walker_end.z -=
					rotation_scale->origin.z;
				axis_angle[0] =
					rotation_scale->axis.x *
					g_collide_opt_axis_q15_to_float_scale;
				axis_angle[1] =
					rotation_scale->axis.y *
					g_collide_opt_axis_q15_to_float_scale;
				axis_angle[2] =
					rotation_scale->axis.z *
					g_collide_opt_axis_q15_to_float_scale;
				axis_angle[3] =
					g_collide_current_mesh_rotation_angle;
				math3d_build_axis_angle_matrix(rotation_matrix,
							       axis_angle);
				math3d_rotate_vec3(
					&g_collide_sweep_walker_start.x,
					rotation_matrix);
				math3d_rotate_vec3(
					&g_collide_sweep_walker_end.x,
					rotation_matrix);
				g_collide_sweep_walker_start.x +=
					rotation_scale->origin.x;
				g_collide_sweep_walker_start.y +=
					rotation_scale->origin.y;
				g_collide_sweep_walker_start.z +=
					rotation_scale->origin.z;
				g_collide_sweep_walker_end.x +=
					rotation_scale->origin.x;
				g_collide_sweep_walker_end.y +=
					rotation_scale->origin.y;
				g_collide_sweep_walker_end.z +=
					rotation_scale->origin.z;
			}
			break;

		default:
			break;
		}

		if (node->child_count == 0) {
			return 0;
		}
		if (child_selection != 0) {
			if (child_selection == -1) {
				return 0;
			}
			node = node->p_children[child_selection - 1];
			continue;
		}
		if (node->child_count <= 0) {
			return 0;
		}
		for (child_index = 0; child_index < node->child_count;
		     ++child_index) {
			if (collide_test_sweep_against_opt_node(
				    object, node->p_children[child_index]) !=
			    0) {
				return 1;
			}
		}
		return 0;
	}
}

/* Finds where the segment from segment_start to segment_end crosses the
 * plane through face_vertex with normal face_normal; a distance within 10 of
 * the plane counts as on it. Returns 1 with *out_t 0 when the start is on
 * the plane, 1 when the end is (the start is checked first), or the
 * crossing's fraction from the start when the ends lie on opposite sides;
 * returns 0, leaving *out_t alone, when both lie on one side. */
// FUNCTION: XVT 0x4A6560
int collide_intersect_segment_with_face_plane(const float *face_normal,
					      const float *face_vertex,
					      const float *segment_start,
					      const float *segment_end,
					      float *out_t)
{
	float start_distance;
	float end_distance;

	start_distance = (segment_start[2] - face_vertex[2]) * face_normal[2] +
			 ((segment_start[1] - face_vertex[1]) * face_normal[1] +
			  (segment_start[0] - face_vertex[0]) * face_normal[0]);
	end_distance = (segment_end[2] - face_vertex[2]) * face_normal[2] +
		       ((segment_end[1] - face_vertex[1]) * face_normal[1] +
			(segment_end[0] - face_vertex[0]) * face_normal[0]);

	if (start_distance < 10.0f && start_distance > -10.0f) {
		start_distance = 0.0f;
	}
	if (end_distance < 10.0f && end_distance > -10.0f) {
		end_distance = 0.0f;
	}
	if (start_distance == 0.0f) {
		*out_t = 0.0f;
		return 1;
	}
	if (end_distance == 0.0f) {
		*out_t = 1.0f;
		return 1;
	}
	if (start_distance < 0.0f && end_distance > 0.0f) {
		*out_t = start_distance;
		*out_t = start_distance / (end_distance - start_distance);
		if (*out_t < g_collide_zero_float) {
			*out_t = -*out_t;
		}
		return 1;
	}
	if (end_distance < 0.0f && start_distance > 0.0f) {
		*out_t = start_distance;
		*out_t = start_distance / (start_distance - end_distance);
		if (*out_t < g_collide_zero_float) {
			*out_t = -*out_t;
		}
		return 1;
	}
	return 0;
}

/* Tests whether projected_point lies inside a face, a triangle or, when
 * face_vertex_indices[3] is not -1, a quad, seen along the axis of the
 * normal's largest component. Returns 1 when the point is on the same side
 * of every edge as of the first, else 0. Overwrites projected_point[1] and
 * [2] with the two coordinates kept. */
// FUNCTION: XVT 0x4A66D0
int collide_point_in_face_polygon(const float *face_normal,
				  const float *vertex_coords,
				  const int32_t *face_vertex_indices,
				  float *projected_point)
{
	float abs_x;
	float abs_y;
	float abs_z;
#ifdef XVT_MODERN
	uint32_t sign_bits;
#endif
	int axis_u;
	int axis_v;
	int vertex_base;
	const float *vertex0u;
	const float *vertex0v;
	float vertex_u;
	float vertex_v;
	float previous_u;
	float previous_v;
	float edge_cross;
	int first_edge_negative;
	int vertex_index;

	abs_x = face_normal[0];
	abs_y = face_normal[1];
	abs_z = face_normal[2];
#ifdef XVT_MODERN
	memcpy(&sign_bits, &abs_x, sizeof(sign_bits));
	if (sign_bits > 0x80000000u)
#else
	if (*(const uint32_t *)&abs_x > 0x80000000u)
#endif
		abs_x = -abs_x;
#ifdef XVT_MODERN
	memcpy(&sign_bits, &abs_y, sizeof(sign_bits));
	if (sign_bits > 0x80000000u)
#else
	if (*(const uint32_t *)&abs_y > 0x80000000u)
#endif
		abs_y = -abs_y;
#ifdef XVT_MODERN
	memcpy(&sign_bits, &abs_z, sizeof(sign_bits));
	if (sign_bits > 0x80000000u)
#else
	if (*(const uint32_t *)&abs_z > 0x80000000u)
#endif
		abs_z = -abs_z;

	if (abs_z >= abs_y && abs_z >= abs_x) {
		axis_u = 0;
		axis_v = 1;
		projected_point[2] = projected_point[1];
		projected_point[1] = projected_point[0];
	} else if (abs_y >= abs_x && abs_z <= abs_y) {
		axis_u = 0;
		axis_v = 2;
		projected_point[1] = projected_point[0];
	} else {
		axis_u = 1;
		axis_v = 2;
	}

	vertex0u = &vertex_coords[axis_u + 3 * face_vertex_indices[0]];
	vertex0v = &vertex_coords[axis_v + 3 * face_vertex_indices[0]];
	previous_u = *vertex0u;
	previous_v = *vertex0v;
	vertex_base = 3 * face_vertex_indices[1];
	vertex_u = vertex_coords[axis_u + vertex_base];
	vertex_v = vertex_coords[axis_v + vertex_base];
	if ((projected_point[1] - previous_u) * (vertex_v - previous_v) -
		    (projected_point[2] - previous_v) *
			    (vertex_u - previous_u) <
	    g_collide_zero_float) {
		first_edge_negative = 1;
	} else {
		first_edge_negative = 0;
	}

	previous_u = vertex_u;
	previous_v = vertex_v;
	vertex_base = 3 * face_vertex_indices[2];
	vertex_u = vertex_coords[axis_u + vertex_base];
	vertex_v = vertex_coords[axis_v + vertex_base];
	edge_cross =
		(projected_point[1] - previous_u) * (vertex_v - previous_v) -
		(projected_point[2] - previous_v) * (vertex_u - previous_u);
	if (edge_cross < g_collide_zero_float && !first_edge_negative) {
		return 0;
	}
#ifdef XVT_MODERN
	memcpy(&sign_bits, &edge_cross, sizeof(sign_bits));
	if (sign_bits <= 0x80000000u && first_edge_negative)
#else
	if (*(const uint32_t *)&edge_cross <= 0x80000000u &&
	    first_edge_negative)
#endif
		return 0;

	vertex_index = face_vertex_indices[3];
	if (vertex_index != -1) {
		previous_u = vertex_u;
		previous_v = vertex_v;
		vertex_base = 3 * vertex_index;
		vertex_u = vertex_coords[axis_u + vertex_base];
		vertex_v = vertex_coords[axis_v + vertex_base];
		edge_cross = (projected_point[1] - previous_u) *
				     (vertex_v - previous_v) -
			     (projected_point[2] - previous_v) *
				     (vertex_u - previous_u);
		if (edge_cross < g_collide_zero_float && !first_edge_negative) {
			return 0;
		}
#ifdef XVT_MODERN
		memcpy(&sign_bits, &edge_cross, sizeof(sign_bits));
		if (sign_bits <= 0x80000000u && first_edge_negative)
#else
		if (*(const uint32_t *)&edge_cross <= 0x80000000u &&
		    first_edge_negative)
#endif
			return 0;
	}

	edge_cross = (*vertex0v - vertex_v) * (projected_point[1] - vertex_u) -
		     (projected_point[2] - vertex_v) * (*vertex0u - vertex_u);
	if (edge_cross < g_collide_zero_float && !first_edge_negative) {
		return 0;
	}
#ifdef XVT_MODERN
	memcpy(&sign_bits, &edge_cross, sizeof(sign_bits));
	if (sign_bits <= 0x80000000u && first_edge_negative)
#else
	if (*(const uint32_t *)&edge_cross <= 0x80000000u &&
	    first_edge_negative)
#endif
		return 0;
	return 1;
}

/* Applies engine wash from the craft source_obj_idx to victim_obj_idx when the
 * victim is within 3 times the source's max_bounds_extent and no more than
 * that extent ahead of it. Behind each engine mesh of the source a wash
 * reaches 8 times the mesh's largest size (at most twice the source's
 * extent), widening with depth; a victim inside takes damage that falls
 * off with depth and with distance from the wash's center, at most 64 (a
 * Super Star Destroyer's wash is placed differently and cut to 3/32; a
 * Calamari Cruiser's lower engines pass over victims above them). Each
 * engine's wash goes through collide_damagecraft as engine wash (source
 * UINT16_MAX - 1), an eighth of it when the victim's shields are down and
 * at least 1. A player's engine_wash_source_obj_idx and engine_wash_strength in
 * g_players keep the strongest wash. Does nothing when the source has no
 * mobile_object. */
// FUNCTION: XVT 0x4A8740
void collide_apply_engine_wash_damage(int victim_obj_idx, int source_obj_idx)
{
	enum {
		OBJECT_TYPE_MESH_CACHE_COUNT = 73,
		SUPER_STAR_DESTROYER_OBJECT_TYPE = 54,
		CALAMARI_CRUISER_OBJECT_TYPE = 49,
		ENGINE_WASH_RANGE_SCALE = 3,
		ENGINE_WASH_LENGTH_SCALE = 8,
		ENGINE_WASH_MAX_DAMAGE = 64,
		ENGINE_WASH_PERCENT_SCALE = 100,
	};

	struct object_record *source = &g_object_table[source_obj_idx];
	int side_extent;
	int up_extent;
	int mesh_index;
	int depth_into_wash;
	uint8_t source_object_type;
	int local_up;
	int mesh_count;
	int local_side;
	int local_forward;
	int source_bounds_extent =
		g_object_type_table[source->object_type].max_bounds_extent;
	int victim_object_index = victim_obj_idx;
	int delta_x =
		g_object_table[victim_object_index].world_x - source->world_x;
	int delta_y =
		g_object_table[victim_object_index].world_y - source->world_y;
	int delta_z =
		g_object_table[victim_object_index].world_z - source->world_z;
	unsigned int object_type;

	if (ENGINE_WASH_RANGE_SCALE * source_bounds_extent <
	    collide_roughdistance3d(delta_x, delta_y, delta_z)) {
		return;
	}

	if (source->mobj == NULL) {
		return;
	}
	if (source->mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(source->pitch, source->yaw, source);
		fview_calcrotateorient(source->roll, 0, source);
	}

	local_side = math_dot3q15(
		source->mobj->cached_side_x, source->mobj->cached_side_y,
		source->mobj->cached_side_z, delta_x, delta_y, delta_z);
	local_forward = -math_dot3q15(
		source->mobj->cached_fwd_x, source->mobj->cached_fwd_y,
		source->mobj->cached_fwd_z, delta_x, delta_y, delta_z);
	if (local_forward < -source_bounds_extent) {
		return;
	}
	local_up = math_dot3q15(
		source->mobj->cached_up_x, source->mobj->cached_up_y,
		source->mobj->cached_up_z, delta_x, delta_y, delta_z);

	object_type = source->object_type;
	if (object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
		mesh_count = g_object_type_mesh_cache[object_type].mesh_count;
	} else {
		mesh_count =
			model_mesh_get_object_type_mesh_count((int)object_type);
	}

	for (mesh_index = 0; mesh_count > mesh_index; ++mesh_index) {
		struct mesh_descriptor *descriptor;
		unsigned int descriptor_object_type = source->object_type;
		int engine_mesh_extent;
		int wash_length;
		int side_offset;
		int up_offset;
		int wash_damage;
		struct object_record *victim;
		int player_owner_idx;

		if (descriptor_object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
			if (mesh_index < 0) {
				descriptor = NULL;
			} else {
				int descriptor_index = mesh_index;

				if (descriptor_index >=
				    g_object_type_mesh_cache
					    [descriptor_object_type]
						    .mesh_count) {
					descriptor_index =
						g_object_type_mesh_cache
							[descriptor_object_type]
								.mesh_count -
						1;
				}
				descriptor =
					g_object_type_mesh_cache
						[descriptor_object_type]
							.mesh_descriptors
								[descriptor_index];
			}
		} else {
			descriptor = model_mesh_get_descriptor(
				(int)descriptor_object_type, mesh_index);
		}
		if (descriptor == NULL ||
		    descriptor->mesh_type != MESH_COMPONENT_06_ENGINE) {
			continue;
		}

		engine_mesh_extent =
			(int)(descriptor->box_max.y - descriptor->box_min.y);
		source_object_type = source->object_type;
		if (source_object_type == SUPER_STAR_DESTROYER_OBJECT_TYPE) {
			engine_mesh_extent >>= 6;
		}
		side_extent =
			(int)(descriptor->box_max.x - descriptor->box_min.x);
		up_extent =
			(int)(descriptor->box_max.z - descriptor->box_min.z);
		if (source_object_type == SUPER_STAR_DESTROYER_OBJECT_TYPE &&
		    local_up > descriptor->box_max.z) {
			continue;
		}
		if (source_object_type == CALAMARI_CRUISER_OBJECT_TYPE &&
		    descriptor->center.z < g_collide_zero_float &&
		    local_up > descriptor->box_max.z) {
			continue;
		}

		if (engine_mesh_extent < side_extent) {
			engine_mesh_extent = side_extent;
		}
		if (engine_mesh_extent < up_extent) {
			engine_mesh_extent = up_extent;
		}
		wash_length = ENGINE_WASH_LENGTH_SCALE * engine_mesh_extent;
		if (wash_length > 2 * source_bounds_extent) {
			wash_length = 2 * source_bounds_extent;
		}
		if (source_object_type == SUPER_STAR_DESTROYER_OBJECT_TYPE) {
			depth_into_wash = (wash_length >> 8) -
					  (int)descriptor->box_max.y +
					  local_forward;
		} else {
			depth_into_wash =
				local_forward - (int)descriptor->box_min.y;
		}
		if (depth_into_wash <= 0 || wash_length < depth_into_wash) {
			continue;
		}

		side_offset = local_side - (int)descriptor->center.x;
		up_offset = local_up - (int)descriptor->center.z;
		side_extent += side_extent >> 1;
		side_extent += depth_into_wash * side_extent / wash_length;
		up_extent += up_extent >> 1;
		up_extent += depth_into_wash * up_extent / wash_length;
		if (side_offset < 0) {
			side_offset = -side_offset;
		}
		if (up_offset < 0) {
			up_offset = -up_offset;
		}
		if (side_extent < side_offset || up_extent < up_offset) {
			continue;
		}

		wash_damage =
			(wash_length >> 8) *
			(ENGINE_WASH_PERCENT_SCALE *
			 (wash_length - depth_into_wash) / wash_length *
			 (ENGINE_WASH_PERCENT_SCALE *
			  (side_extent + up_extent - up_offset - side_offset) /
			  (side_extent + up_extent)) /
			 ENGINE_WASH_PERCENT_SCALE) /
			ENGINE_WASH_PERCENT_SCALE;
		if (source_object_type == SUPER_STAR_DESTROYER_OBJECT_TYPE) {
			wash_damage >>= 4;
			wash_damage += wash_damage >> 1;
		}
		if (wash_damage > ENGINE_WASH_MAX_DAMAGE) {
			wash_damage = ENGINE_WASH_MAX_DAMAGE;
		}

		victim = &g_object_table[victim_object_index];
		player_owner_idx = victim->player_owner_idx;
		if (player_owner_idx != -1 &&
		    g_players[player_owner_idx].engine_wash_strength <
			    wash_damage) {
			g_players[player_owner_idx].engine_wash_source_obj_idx =
				(uint16_t)source_obj_idx;
			g_players[player_owner_idx].engine_wash_strength =
				(uint16_t)wash_damage;
		}
		if (victim->mobj != NULL && victim->mobj->p_craft != NULL &&
		    victim->mobj->p_craft->shield_energy[0] +
				    victim->mobj->p_craft->shield_energy[1] ==
			    0) {
			wash_damage >>= 3;
		}
		if (wash_damage < 1) {
			wash_damage = 1;
		}
		collide_damagecraft((uint16_t)victim_obj_idx, -1,
				    UINT16_MAX - 1, (uint16_t)wash_damage);
	}
}

/* Jams the weapons of the craft owner_obj_idx (beam_effect_accum[2] set to
 * 163,840, chaff ended) when it is at the hangar of the hostile craft
 * hostile_obj_idx: within 4,096 world units of an X/7 factory; near a Super
 * Star Destroyer's inside hangar point (within an eighth of its extent,
 * and level with it to three quarters of the point's height); within a
 * sixth of a repair yard's extent of its inside hangar point, or of a
 * hangar mesh center turned into world axes (against the craft's offset
 * in the yard's own axes); within a sixth of any other's extent of its
 * inside hangar point, but never above an Imperial or Victory Star
 * Destroyer or an Interdictor. Does nothing beyond twice the hostile's
 * extent, or when the hostile has no mobile_object or no working systems. */
// FUNCTION: XVT 0x4A8CC0
void collide_apply_hostile_proximity_weapon_disruption(int owner_obj_idx,
						       int hostile_obj_idx)
{
	struct object_record *hostile = &g_object_table[hostile_obj_idx];
	int hostile_bounds_extent =
		g_object_type_table[hostile->object_type].max_bounds_extent;
	int delta_x = g_object_table[owner_obj_idx].world_x - hostile->world_x;
	int delta_y = g_object_table[owner_obj_idx].world_y - hostile->world_y;
	int delta_z = g_object_table[owner_obj_idx].world_z - hostile->world_z;
	int rough_distance = collide_roughdistance3d(delta_x, delta_y, delta_z);
	struct mobile_object *owner_mobj;

	if (2 * hostile_bounds_extent < rough_distance) {
		return;
	}
	if (hostile->mobj == NULL ||
	    hostile->mobj->p_craft->working_subsystems == 0) {
		return;
	}

	if (hostile->object_type == CRAFT_SPECIES_X7_FACTORY) {
		if (rough_distance > 4096) {
			return;
		}
	} else {
		int local_side;
		int local_fwd;
		int local_up;
		struct craft_data *hostile_craft;
		uint8_t model_index;
		int inside_side;
		int inside_up;
		int inside_fwd;

		if (hostile->mobj->orient_matrix_dirty != 0) {
			fview_calcrotatemove(hostile->pitch, hostile->yaw,
					     hostile);
			fview_calcrotateorient(hostile->roll, 0, hostile);
		}

		local_side = math_dot3q15(hostile->mobj->cached_side_x,
					  hostile->mobj->cached_side_y,
					  hostile->mobj->cached_side_z, delta_x,
					  delta_y, delta_z);
		local_fwd = math_dot3q15(hostile->mobj->cached_fwd_x,
					 hostile->mobj->cached_fwd_y,
					 hostile->mobj->cached_fwd_z, delta_x,
					 delta_y, delta_z);
		local_up = math_dot3q15(
			hostile->mobj->cached_up_x, hostile->mobj->cached_up_y,
			hostile->mobj->cached_up_z, delta_x, delta_y, delta_z);

		hostile_craft = hostile->mobj->p_craft;
		if (hostile_craft == NULL) {
			return;
		}

		model_index = hostile_craft->model_index;
		inside_side =
			g_model_defs[model_index].hangar_points.inside.side;
		inside_up = g_model_defs[model_index].hangar_points.inside.up;
		inside_fwd =
			g_model_defs[model_index].hangar_points.inside.forward;

		if ((hostile->object_type ==
			     CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER ||
		     hostile->object_type == CRAFT_SPECIES_INTERDICTOR ||
		     hostile->object_type ==
			     CRAFT_SPECIES_VICTORY_STAR_DESTROYER) &&
		    local_up > 0) {
			return;
		}

		if (hostile->object_type ==
		    CRAFT_SPECIES_SUPER_STAR_DESTROYER) {
			int up_difference = local_up - inside_up;
			if (up_difference < 0) {
				up_difference = -up_difference;
			}
			if (inside_up < 0) {
				inside_up = -inside_up;
			}
			if (inside_up - (inside_up >> 2) < up_difference ||
			    hostile_bounds_extent >> 3 <
				    collide_roughdistance3d(
					    local_side - inside_side,
					    local_fwd - inside_fwd,
					    up_difference)) {
				return;
			}
		} else if (hostile->object_type == CRAFT_SPECIES_REPAIR_YARD) {
			int mesh_extent = hostile_bounds_extent / 6;
			if (mesh_extent <
			    collide_roughdistance3d(local_side - inside_side,
						    local_fwd - inside_fwd,
						    local_up - inside_up)) {
				int object_type = hostile->object_type;
				int mesh_count;
				int mesh_index;

				/* Here and below, CRAFT_SPECIES_SAT_4 (73) stands for the size of g_object_type_mesh_cache. */
				if (object_type < CRAFT_SPECIES_SAT_4) {
					mesh_count =
						g_object_type_mesh_cache
							[object_type]
								.mesh_count;
				} else {
					mesh_count =
						model_mesh_get_object_type_mesh_count(
							object_type);
				}

				mesh_index = 0;
				if (mesh_count <= mesh_index) {
					return;
				}
				while (1) {
					mesh_component_type mesh_type;
					if (object_type < CRAFT_SPECIES_SAT_4) {
						if (mesh_index < 0) {
							mesh_type =
								MESH_COMPONENT_00_DEFAULT;
						} else {
							int mesh_type_index =
								mesh_index;
							if (mesh_index >=
							    g_object_type_mesh_cache
								    [object_type]
									    .mesh_count) {
								mesh_type_index =
									g_object_type_mesh_cache
										[object_type]
											.mesh_count -
									1;
							}
							mesh_type =
								g_object_type_mesh_cache[object_type]
									.mesh_types
										[mesh_type_index];
						}
					} else {
						mesh_type =
							model_mesh_get_object_type_mesh_type(
								object_type,
								mesh_index);
					}
					if (mesh_type ==
					    MESH_COMPONENT_16_HANGAR) {
						int center_x =
							model_mesh_get_center_x(
								object_type,
								mesh_index);
						int center_z =
							model_mesh_get_center_z(
								object_type,
								mesh_index);
						int center_y =
							model_mesh_get_center_y(
								object_type,
								mesh_index);
						pai_rotate_local_vector_to_world_scratch(
							hostile, center_x,
							center_z, -center_y);
						if (mesh_extent >
						    collide_roughdistance3d(
							    local_side -
								    g_rotated_x,
							    local_fwd -
								    g_rotated_y,
							    local_up -
								    g_rotated_z)) {
							break;
						}
					}
					if (mesh_count <= ++mesh_index) {
						return;
					}
				}
			}
		} else {
			if (hostile_bounds_extent / 6 <
			    collide_roughdistance3d(local_side - inside_side,
						    local_fwd - inside_fwd,
						    local_up - inside_up)) {
				return;
			}
		}
	}

	owner_mobj = g_object_table[owner_obj_idx].mobj;
	if (owner_mobj != NULL) {
		struct craft_data *owner_craft = owner_mobj->p_craft;
		if (owner_craft != NULL) {
			owner_craft->beam_effect_accum[2] = 163840;
			owner_craft->chaff_active_seconds = 0;
		}
	}
}

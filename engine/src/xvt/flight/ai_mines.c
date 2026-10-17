/* A mine's target search, ported from the 1997 AI's
 * paifight_find_nearest_matching_target_from_origin and
 * pai_is_object_targetable (deleted with the 1997 AI), whose only caller was
 * laser_update_mine_weapon_fire. The 1997 code measured the decoy distance
 * and the source's genus from the last craft the AI had thought for, which
 * a mine never set; here both come from the mine. The clear-sweep check
 * (both calls passed 0) is left out with its parameter. */
#include "xvt/flight/ai_mines.h"

#include <limits.h>
#include <stdint.h>

#include "xvt/assets/object_type.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/log/log.h"

enum {
	/* An object type with this behavior flag is a static target. */
	AI_MINES_TARGETABLE_STATIC_FLAG = 2,
};

/* The two target conditions of a search, the mine searching and where it is. */
struct mine_search {
	uint16_t mine;
	int origin_x;
	int origin_y;
	int origin_z;
	int16_t type_a;
	uint16_t value_a;
	int16_t joined_by_or;
	int16_t type_b;
	uint16_t value_b;
};

/* The nearest match so far. */
struct mine_best {
	uint16_t object;
	unsigned int range;
};

/* Whether the object matches both target conditions, joined by OR or AND. */
static int matches_targets(uint16_t object_index,
			   const struct mine_search *search)
{
	int16_t matches_a = mission_object_matches_trigger_variable(
		object_index, (uint16_t)search->type_a, search->value_a);
	int16_t matches_b = mission_object_matches_trigger_variable(
		object_index, (uint16_t)search->type_b, search->value_b);
	if (search->joined_by_or == 1) {
		return (matches_a | matches_b) != 0;
	}
	return (matches_a & matches_b) != 0;
}

/* Whether the mine may target the craft in target_index: not one going into
 * hyperspace, of kind UNKNOWN_1, breaking up or exploding, and not one with
 * its decoy beam on farther from the mine than AI_DECOY_RANGE_LARGE (a mine
 * is never a starfighter, transport or utility vehicle). A player's craft
 * passes the rest; an AI craft needs hull_damage at most hull_max. */
static int craft_targetable(uint16_t target_index, uint16_t mine,
			    const struct craft_data *craft)
{
	if (craft->object_kind == CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE) {
		return 0;
	}
	if (craft->object_kind == CRAFT_OBJECT_KIND_UNKNOWN_1 ||
	    craft->object_kind == CRAFT_OBJECT_KIND_BREAKING_UP ||
	    craft->object_kind == CRAFT_OBJECT_KIND_EXPLODING) {
		return 0;
	}
	if (craft->beam_type_id == BEAM_TYPE_DECOY && craft->beam_active != 0) {
		flight_geometry_rough_distance_between_refs(target_index, mine);
		if (g_last_rough_distance > AI_DECOY_RANGE_LARGE) {
			XVT_LOG_DEBUG(
				"ai.mine_decoy_refused target=%d mine=%d range=%u predicted=%d",
				(int)target_index, (int)mine,
				(unsigned)g_last_rough_distance,
				g_flight_sim_side_effects_suppressed);
			return 0;
		}
	}
	if (g_object_table[target_index].player_owner_idx != -1) {
		return 1;
	}
	return craft->hull_damage <= craft->hull_max;
}

/* Whether the mine may target the object in target_index: not an empty
 * slot; a mobile object by craft_targetable, or at once when it has no
 * craft; a static object while its type is not 0. */
static int targetable(uint16_t target_index, uint16_t mine)
{
	const struct object_record *object = &g_object_table[target_index];
	if (object->object_type == 0) {
		return 0;
	}
	if (object->mobj == NULL) {
		return 1;
	}
	if (object->mobj->p_craft == NULL) {
		return 1;
	}
	return craft_targetable(target_index, mine, object->mobj->p_craft);
}

/* Keeps object_index as the best when its position x, y, z lies nearer the
 * mine than the best so far. Sets g_last_rough_distance. */
static void keep_if_nearer(struct mine_best *best,
			   const struct mine_search *search,
			   uint16_t object_index, int x, int y, int z)
{
	unsigned int range = (unsigned int)collide_roughdistance3d(
		x - search->origin_x, y - search->origin_y,
		z - search->origin_z);
	g_last_rough_distance = (int)range;
	if (range < best->range) {
		best->object = object_index;
		best->range = range;
	}
}

/* Searches the craft slots. A Mine Type B skips a craft with no working
 * subsystems; a mobile object with no craft counts as working. */
static void search_craft(struct mine_best *best,
			 const struct mine_search *search)
{
	int type_b_mine = g_object_table[search->mine].object_type ==
			  CRAFT_SPECIES_MINE_TYPE_B;
	for (int slot = g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		const struct object_record *object = &g_object_table[slot];
		if (object->object_type == 0 ||
		    !matches_targets((uint16_t)slot, search)) {
			continue;
		}
		if (type_b_mine && object->mobj != NULL &&
		    object->mobj->p_craft != NULL &&
		    object->mobj->p_craft->working_subsystems == 0) {
			continue;
		}
		if (!targetable((uint16_t)slot, search->mine)) {
			continue;
		}
		keep_if_nearer(best, search, (uint16_t)slot, object->world_x,
			       object->world_y, object->world_z);
	}
}

/* Searches the static slots, only types with behavior flag 2. */
static void search_statics(struct mine_best *best,
			   const struct mine_search *search)
{
	int end = g_region_main_object_slot_end +
		  g_region_static_object_slot_count;
	for (int slot = g_region_main_object_slot_end; slot < end; ++slot) {
		const struct object_record *object = &g_object_table[slot];
		if (object->object_type == 0 ||
		    (g_object_type_table[object->object_type].behavior_flags &
		     AI_MINES_TARGETABLE_STATIC_FLAG) == 0 ||
		    !matches_targets((uint16_t)slot, search) ||
		    !targetable((uint16_t)slot, search->mine)) {
			continue;
		}
		mission_resolve_object_or_mission_point_world_loc(
			(uint16_t)slot, 0);
		keep_if_nearer(best, search, (uint16_t)slot, g_world_loc_x,
			       g_world_loc_y, g_world_loc_z);
	}
}

uint16_t ai_mines_find_target(uint16_t mine_obj_idx, int16_t type_a,
			      uint16_t value_a, int16_t joined_by_or,
			      int16_t type_b, uint16_t value_b)
{
	const struct object_record *mine = &g_object_table[mine_obj_idx];
	struct mine_search search = {
		.mine = mine_obj_idx,
		.type_a = type_a,
		.value_a = value_a,
		.joined_by_or = joined_by_or,
		.type_b = type_b,
		.value_b = value_b,
		.origin_x = mine->world_x,
		.origin_y = mine->world_y,
		.origin_z = mine->world_z,
	};
	struct mine_best best = {UINT16_MAX, UINT_MAX};

	search_craft(&best, &search);
	search_statics(&best, &search);
	if (best.range > AI_TARGET_RANGE_MAX) {
		best.object = UINT16_MAX;
	}
	if (best.object != UINT16_MAX) {
		XVT_LOG_DEBUG(
			"ai.mine_target_found target=%d range=%u mine=%d undisabled=%d x=%d y=%d z=%d predicted=%d",
			(int)best.object, best.range, (int)mine_obj_idx,
			(int)(mine->object_type == CRAFT_SPECIES_MINE_TYPE_B),
			search.origin_x, search.origin_y, search.origin_z,
			g_flight_sim_side_effects_suppressed);
	}
	return best.object;
}

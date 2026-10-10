/* Tests for xvt/flight/mission/mission.c, the mission's goals, triggers,
 * scoring and flight group arrivals. Each check builds the mission it needs in
 * the game's own tables: two flight groups, a few craft in a small object table
 * this file owns, and the goal and trigger records set field by field. The
 * loader checks read mission files this file writes, in the stored format of
 * versions 12 to 14 and in the TIE format, into a temporary asset folder; no
 * game data is read.
 *
 * Not checked here, in mission_load_file:
 * - A file that does not open, or comes up short, ends the program on a
 *   message box that waits on a machine with a display.
 * - The version 10 branches never run, and a message index, a flight group
 *   count, a message count or a global goal count past its table (of the TIE
 *   format) would write past the table, which the sanitizers stop.
 * - A role that fills all 16 bytes makes the upper-casing loop read the byte
 *   after its field, which the array-bounds check of the sanitizers stops.
 *
 * POSIX only, for the temporary folder (test_asset_folder.h). */
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"

enum {
	TEST_OBJECT_COUNT = 8,
	TEST_XWING = 1,	     /* Craft type 1 is object type 1, a starfighter. */
	TEST_NO_ORDINAL = 9, /* A craft ordinal no test craft has. */
};

static struct object_record g_test_objects[TEST_OBJECT_COUNT];
static struct mobile_object g_test_mobiles[TEST_OBJECT_COUNT];
static struct craft_data g_test_craft[TEST_OBJECT_COUNT];

/* Two flight groups of X-wings, group 0 on team 0 and IFF 0, group 1 on team
 * 1 and IFF 1, neither owned by a player and neither with a special cargo
 * craft. The object table is this file's eight free slots, all of them the
 * active region. No player is in the flight; the local player is slot 7. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	memset(g_mission_global_goals, 0, sizeof g_mission_global_goals);
	memset(g_mission_teams, 0, sizeof g_mission_teams);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	memset(&g_mission_elapsed_clock, 0, sizeof g_mission_elapsed_clock);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(g_players, 0, sizeof g_players);
	memset(g_plan_table, 0, sizeof g_plan_table);
	memset(g_builtin_plan_id_by_name_index, 0,
	       sizeof g_builtin_plan_id_by_name_index);
	memset(g_order_leader_builtin_plan_name_index, 0,
	       sizeof g_order_leader_builtin_plan_name_index);
	g_object_table = g_test_objects;
	for (int i = 0; i < TEST_OBJECT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].p_craft = &g_test_craft[i];
		g_test_craft[i].leader_obj_idx = UINT8_MAX;
		g_test_craft[i].last_attacker_obj_idx = UINT16_MAX;
	}
	for (int i = 0; i < 8; ++i) {
		for (int slot = 0; slot < 4; ++slot) {
			g_players[i].target_preset_slot[slot] = -1;
		}
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = TEST_OBJECT_COUNT;
	g_mission_header.num_flight_groups = 2;
	for (int fg = 0; fg < 2; ++fg) {
		g_mission_flight_groups[fg].fg.craft_type = TEST_XWING;
		g_mission_flight_groups[fg].fg.team = (uint8_t)fg;
		g_mission_flight_groups[fg].fg.iff = (uint8_t)fg;
		g_mission_flight_groups[fg].fg.special_cargo_craft =
			TEST_NO_ORDINAL;
		g_mission_flight_groups[fg].player_owner_idx = -1;
	}
	g_mission_condition_current_count = 0;
	g_mission_condition_total_count = 0;
	g_current_flight_group_idx = 0;
	g_local_player = 7;
	g_flight_sim_side_effects_suppressed = 0;
}

/* Puts a craft of flight group fg in slot obj, with the group's team. */
static struct craft_data *place_craft(int obj, int fg, int ordinal)
{
	g_test_objects[obj].object_type =
		g_craft_type_to_object_type[g_mission_flight_groups[fg]
						    .fg.craft_type];
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_mobiles[obj].team = g_mission_flight_groups[fg].fg.team;
	g_test_craft[obj].craft_ordinal = (uint8_t)ordinal;
	/* Its own team knows it from the start. */
	g_test_craft[obj]
		.identified_order_by_team[g_mission_flight_groups[fg].fg.team] =
		1;
	return &g_test_craft[obj];
}

/* The first craft type, from 1, whose object type has the static flag (0x80)
 * set or clear as asked. */
static int craft_type_with_static_flag(int is_static)
{
	for (int type = 1; type < 96; ++type) {
		uint8_t object_type = g_craft_type_to_object_type[type];
		if (object_type == 0) {
			continue;
		}
		if (((g_object_type_table[object_type].behavior_flags & 0x80) !=
		     0) == (is_static != 0)) {
			return type;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

static int16_t condition_on_group(uint16_t condition, uint16_t fg,
				  int16_t amount, int16_t departed_as_destroyed)
{
	return mission_evaluate_condition(condition, GOAL_TARGET_FLIGHT_GROUP,
					  fg, amount, departed_as_destroyed,
					  10);
}

/* mission_is_special_cargo_inspected returns the group's inspected special
 * cargo flag and ignores the craft number it is given. */
static void check_special_cargo_inspected(void)
{
	fresh_world();
	g_mission_fg_stats[1]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_INSPECTED] = 1;
	XVT_ASSERT_INT_EQ(mission_is_special_cargo_inspected(1, 0), 1);
	XVT_ASSERT_INT_EQ(mission_is_special_cargo_inspected(1, 5), 1);
	XVT_ASSERT_INT_EQ(mission_is_special_cargo_inspected(0, 0), 0);
}

/* The conditions that count nothing: always true is met, never returns 0,
 * always failed has failed, and each leaves both craft counts at 0. */
static void check_condition_fixed_results(void)
{
	fresh_world();
	g_mission_condition_current_count = 7;
	g_mission_condition_total_count = 9;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(MISSION_COND_ALWAYS_TRUE,
						     0, 0, 0, 0, 10),
			  1);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 0);
	XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 0);
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_NEVER, 0, 0, 0, 0, 10),
		0);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(MISSION_COND_ALWAYS_FAILED,
						     0, 0, 0, 0, 10),
			  2);
}

/* A condition that counts craft has failed when it names no variable. */
static void check_condition_needs_variable(void)
{
	fresh_world();
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED] = 4;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(MISSION_COND_DESTROYED,
						     GOAL_TARGET_NONE, 0,
						     GOAL_AMT_100, 0, 10),
			  2);
}

/* "Destroyed" counted over group 0's four craft: met when all four are
 * destroyed, failed for "all" once one has left the region, met for "half"
 * with two destroyed. With departed craft counted as destroyed, one that left
 * the region counts toward met instead of failed. The counts of group 1 stay
 * out of it, and the met and total counts are left behind. */
static void check_condition_destroyed_amounts(void)
{
	fresh_world();
	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	counts[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	counts[FLIGHT_GROUP_OUTCOME_ARRIVED] = 4;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 3;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_LEFT_REGION] =
		3;

	XVT_ASSERT_INT_EQ(
		condition_on_group(MISSION_COND_DESTROYED, 0, GOAL_AMT_100, 0),
		4);

	counts[FLIGHT_GROUP_OUTCOME_DESTROYED] = 4;
	XVT_ASSERT_INT_EQ(
		condition_on_group(MISSION_COND_DESTROYED, 0, GOAL_AMT_100, 0),
		1);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 4);
	XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 4);

	counts[FLIGHT_GROUP_OUTCOME_DESTROYED] = 2;
	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = 1;
	XVT_ASSERT_INT_EQ(
		condition_on_group(MISSION_COND_DESTROYED, 0, GOAL_AMT_100, 0),
		2);
	XVT_ASSERT_INT_EQ(
		condition_on_group(MISSION_COND_DESTROYED, 0, GOAL_AMT_50, 0),
		1);
	XVT_ASSERT_INT_EQ(
		condition_on_group(MISSION_COND_DESTROYED, 0, GOAL_AMT_100, 1),
		4);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 3);

	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = 2;
	XVT_ASSERT_INT_EQ(
		condition_on_group(MISSION_COND_DESTROYED, 0, GOAL_AMT_100, 1),
		1);
	XVT_ASSERT_INT_EQ(
		condition_on_group(MISSION_COND_DESTROYED, 0, GOAL_AMT_100, 0),
		2);
}

/* "At least one" is met by one craft and has failed only when every craft
 * failed. */
static void check_condition_at_least_one(void)
{
	fresh_world();
	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	counts[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = 3;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_DESTROYED, 0,
					     GOAL_AMT_AT_LEAST_1, 0),
			  4);
	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = 4;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_DESTROYED, 0,
					     GOAL_AMT_AT_LEAST_1, 0),
			  2);
	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = 3;
	counts[FLIGHT_GROUP_OUTCOME_DESTROYED] = 1;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_DESTROYED, 0,
					     GOAL_AMT_AT_LEAST_1, 0),
			  1);
}

/* The subset amounts measure against the craft arrived so far and never
 * fail: two of four arrived, both gone from the region, is still undecided;
 * both destroyed is all of the subset. */
static void check_condition_subset_never_fails(void)
{
	fresh_world();
	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	counts[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	counts[FLIGHT_GROUP_OUTCOME_ARRIVED] = 2;
	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = 2;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_DESTROYED, 0,
					     GOAL_AMT_100_OF_SUBSET, 0),
			  4);
	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = 0;
	counts[FLIGHT_GROUP_OUTCOME_DESTROYED] = 2;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_DESTROYED, 0,
					     GOAL_AMT_100_OF_SUBSET, 0),
			  1);
}

/* The goal conditions read the statuses (0 open, 1 complete, 2 failed): a
 * team's own with a team variable, the any-team flag otherwise. */
static void check_condition_goal_statuses(void)
{
	fresh_world();
	uint8_t *primary =
		&g_flight_mission_state.runtime.team_goal_status[3][0];
	*primary = 1;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_COMPLETE,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		1);
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_FAILED,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		2);
	*primary = 2;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_COMPLETE,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		2);
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_FAILED,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		1);
	*primary = 0;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_COMPLETE,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		4);
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_FAILED,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		4);

	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_COMPLETE,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		4);
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PRIMARY_GOAL_COMPLETE,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		1);
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_COMPLETE,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		4);
	g_flight_mission_state.runtime.global_bonus_goal_status = 1;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_COMPLETE,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		1);
}

/* "Reinforcements called" is met once the team has called them and has
 * failed before. */
static void check_condition_reinforcements(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_REINFORCEMENTS_CALLED,
					   GOAL_TARGET_TEAM, 2, 0, 0, 10),
		2);
	g_flight_mission_state.runtime.team_reinforcements_called[2] = 1;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_REINFORCEMENTS_CALLED,
					   GOAL_TARGET_TEAM, 2, 0, 0, 10),
		1);
}

static struct mission_trigger_pair pair_of(uint8_t first, uint8_t second,
					   uint8_t or_joined)
{
	struct mission_trigger_pair pair;
	memset(&pair, 0, sizeof pair);
	pair.triggers[0].condition = first;
	pair.triggers[1].condition = second;
	pair.trigger1_or_trigger2 = or_joined;
	return pair;
}

/* A pair ANDs or ORs its two result bit sets as trigger1_or_trigger2 says;
 * a second condition of "no condition" leaves the first's result alone. */
static void check_trigger_pair_joins(void)
{
	fresh_world();
	struct mission_trigger_pair pair = pair_of(
		MISSION_COND_ALWAYS_TRUE, MISSION_COND_ALWAYS_FAILED, 0);
	XVT_ASSERT_INT_EQ(mission_evaluate_trigger_pair(&pair, 0), 0);
	pair.trigger1_or_trigger2 = 1;
	XVT_ASSERT_INT_EQ(mission_evaluate_trigger_pair(&pair, 0), 3);
	pair = pair_of(MISSION_COND_ALWAYS_TRUE, MISSION_COND_ALWAYS_TRUE, 0);
	XVT_ASSERT_INT_EQ(mission_evaluate_trigger_pair(&pair, 0), 1);
	pair = pair_of(MISSION_COND_ALWAYS_FAILED, MISSION_COND_NO_CONDITION,
		       0);
	XVT_ASSERT_INT_EQ(mission_evaluate_trigger_pair(&pair, 0), 2);
}

/* A second trigger of "no condition" with a team variable makes that team
 * the first condition's team filter. Group 0's one captured and departed
 * craft was taken by team 5: "captured and departed" is met over all teams,
 * and undecided for team 4 alone. */
static void check_trigger_pair_team_filter(void)
{
	fresh_world();
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	g_mission_fg_stats[0].team_captured_departed_count[5] = 1;
	struct mission_trigger_pair pair =
		pair_of(MISSION_COND_CAPTURED_AND_DEPARTED,
			MISSION_COND_NO_CONDITION, 0);
	pair.triggers[0].variable_type = GOAL_TARGET_FLIGHT_GROUP;
	pair.triggers[0].variable = 0;
	pair.triggers[0].amount = GOAL_AMT_AT_LEAST_1;
	XVT_ASSERT_INT_EQ(mission_evaluate_trigger_pair(&pair, 0), 1);
	pair.triggers[1].variable_type = GOAL_TARGET_TEAM;
	pair.triggers[1].variable = 4;
	XVT_ASSERT_INT_EQ(mission_evaluate_trigger_pair(&pair, 0), 4);
	pair.triggers[1].variable = 5;
	XVT_ASSERT_INT_EQ(mission_evaluate_trigger_pair(&pair, 0), 1);
}

/* Which trigger variables select group 1 (IFF 3, team 1, global group 5, AI
 * level 2, status 4, global unit 6, no owner) and group 0 once a player owns
 * it. */
static void check_group_matches_variable(void)
{
	fresh_world();
	struct xvt_flight_group *fg = &g_mission_flight_groups[1].fg;
	fg->iff = 3;
	fg->global_group = 5;
	fg->group_ai = 2;
	fg->status1 = 4;
	fg->global_unit = 6;
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 1, 1), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 1, 0), 0);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 15, 1), 0);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 15, 0), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 5, 3), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 5, 1), 0);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 8, 5), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 9, 2), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 10, 4), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 12, 1), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 12, 0), 0);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 23, 6), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 11, 77), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 0, 1), 0);
	/* Type 7: 9 picks owned groups, 10 unowned ones, anything else all. */
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 7, 9), 0);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 7, 10), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(1, 7, 3), 1);
	g_mission_flight_groups[0].player_owner_idx = 0;
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(0, 7, 9), 1);
	XVT_ASSERT_INT_EQ(
		mission_flight_group_matches_trigger_variable(0, 7, 10), 0);
}

/* A craft's outcome is counted once: a second call for the same craft does
 * nothing. */
static void check_outcome_counted_once(void)
{
	fresh_world();
	place_craft(0, 0, 1);
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_DESTROYED], 1);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED], 1);
	XVT_ASSERT_INT_EQ(g_test_craft[0].mission_accounting_done, 1);
}

/* An untouched craft of group 0, its special cargo craft, destroyed: it
 * counts as destroyed, not captured by its destination, not disabled, not
 * captured, not attacked, not boarded and not docked, uninspected-lost and
 * uncaptured-lost for the other teams but not its own, and each mark is
 * copied to the special cargo outcomes. */
static void check_outcome_untouched_craft(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.special_cargo_craft = 1;
	place_craft(0, 0, 1);
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	const struct mission_fg_runtime_stats *stats = &g_mission_fg_stats[0];
	static const int counted[] = {
		FLIGHT_GROUP_OUTCOME_DESTROYED,
		FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION,
		FLIGHT_GROUP_OUTCOME_NOT_DISABLED,
		FLIGHT_GROUP_OUTCOME_NOT_CAPTURED,
		FLIGHT_GROUP_OUTCOME_NOT_ATTACKED,
		FLIGHT_GROUP_OUTCOME_NOT_BOARDED,
		FLIGHT_GROUP_OUTCOME_NOT_DOCKED,
	};
	for (size_t i = 0; i < sizeof counted / sizeof counted[0]; ++i) {
		XVT_ASSERT_INT_EQ(stats->outcome_count[counted[i]], 1);
		XVT_ASSERT_INT_EQ(stats->special_cargo_outcome[counted[i]], 1);
	}
	XVT_ASSERT_INT_EQ(stats->outcome_count[FLIGHT_GROUP_OUTCOME_ATTACKED],
			  0);
	XVT_ASSERT_INT_EQ(stats->outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED],
			  0);
	XVT_ASSERT_INT_EQ(stats->team_uninspected_lost[0], 0);
	XVT_ASSERT_INT_EQ(stats->team_uninspected_lost[1], 1);
	XVT_ASSERT_INT_EQ(stats->team_special_cargo_uninspected_lost[1], 1);
	XVT_ASSERT_INT_EQ(stats->team_uncaptured_lost[0], 0);
	XVT_ASSERT_INT_EQ(stats->team_uncaptured_lost[9], 1);
}

/* A craft that was hit by team 1's first shot, captured, disabled, boarded,
 * docked with and seen by every team ends without any of the "not" counts;
 * being destroyed takes back its capture. */
static void check_outcome_touched_craft(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 1);
	craft->attacked_by_team[1] = 1;
	craft->captured_by_flight_group = 0x81;
	craft->not_disabled_accounting_suppress = 1;
	craft->ai_flight.times_boarded = 1;
	craft->ai_flight.docked_target_count = 1;
	for (int team = 0; team < 10; ++team) {
		craft->identified_order_by_team[team] = 1;
	}
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED] = 1;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	const struct mission_fg_runtime_stats *stats = &g_mission_fg_stats[0];
	XVT_ASSERT_INT_EQ(stats->outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED],
			  0);
	XVT_ASSERT_INT_EQ(craft->captured_by_flight_group, 0);
	XVT_ASSERT_INT_EQ(
		stats->outcome_count[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED], 0);
	XVT_ASSERT_INT_EQ(
		stats->outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DISABLED], 0);
	XVT_ASSERT_INT_EQ(
		stats->outcome_count[FLIGHT_GROUP_OUTCOME_NOT_BOARDED], 0);
	XVT_ASSERT_INT_EQ(stats->outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DOCKED],
			  0);
	XVT_ASSERT_INT_EQ(stats->team_uninspected_lost[1], 0);
	/* Its capture was taken back, so it ends not captured. */
	XVT_ASSERT_INT_EQ(
		stats->outcome_count[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED], 1);
}

/* A destroyed craft that had a depart timer and an abort takes back its
 * not-departed and aborted counts. A player's craft that ends another way
 * with neither counts as not departed, unless its team's primary goal is
 * complete; a craft no player flies does not. */
static void check_outcome_departures(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 1);
	craft->ai_flight.depart_timer_flag = 1;
	craft->ai_flight.mission_aborted_flag = 1;
	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 2;
	counts[FLIGHT_GROUP_OUTCOME_ABORTED] = 2;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_ABORTED], 1);

	fresh_world();
	place_craft(0, 0, 1);
	place_craft(1, 0, 2);
	g_test_objects[0].player_owner_idx = 0;
	g_test_objects[1].player_owner_idx = 1;
	counts = g_mission_fg_stats[0].outcome_count;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_LEFT_REGION);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);
	g_flight_mission_state.runtime.team_goal_status[0][0] = 1;
	mission_record_craft_outcome(1, 0, FLIGHT_GROUP_OUTCOME_LEFT_REGION);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION], 2);
	g_flight_mission_state.runtime.team_goal_status[0][0] = 0;
	place_craft(2, 0, 3);
	mission_record_craft_outcome(2, 0, FLIGHT_GROUP_OUTCOME_LEFT_REGION);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);
}

/* Destroying group 0's craft closes group 1, which arrives from group 0's
 * hangar: its three craft not yet arrived count as arrived and lost with the
 * mothership. Group 2 arrives by hyperspace, and stays open although its
 * arrival_mothership field also names group 0. */
static void check_outcome_closes_arrivals(void)
{
	fresh_world();
	g_mission_header.num_flight_groups = 3;
	g_mission_fg_stats[2].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	place_craft(0, 0, 0);
	g_mission_flight_groups[1].fg.arrival_method = 1;
	g_mission_flight_groups[1].fg.arrival_mothership = 0;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] = 1;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].has_arrived, 1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  4);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].outcome_count
				  [FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP],
			  3);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[2].has_arrived, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[2]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  0);
}

/* Group 1 departs into group 0, with group 0 as its alternate and as the
 * mothership of its captured craft. When group 0's craft is destroyed, the
 * three dependent counts move to lost with mothership; when it leaves the
 * region, they move to left region. Group 2 departs by hyperspace with none
 * of the three set, though its fields name group 0, and keeps its counts. */
static void check_outcome_moves_dependents(void)
{
	static const uint16_t outcomes[2] = {
		FLIGHT_GROUP_OUTCOME_DESTROYED,
		FLIGHT_GROUP_OUTCOME_LEFT_REGION,
	};
	static const uint16_t moved_to[2] = {
		FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP,
		FLIGHT_GROUP_OUTCOME_LEFT_REGION,
	};
	for (int i = 0; i < 2; ++i) {
		fresh_world();
		place_craft(0, 0, 0);
		struct xvt_flight_group *fg = &g_mission_flight_groups[1].fg;
		fg->departure_method = 1;
		fg->departure_mothership = 0;
		fg->alternate_mothership_used = 1;
		fg->alternate_mothership = 0;
		fg->captured_depart_via_mothership = 1;
		fg->captured_departure_mothership = 0;
		uint16_t *counts = g_mission_fg_stats[1].outcome_count;
		counts[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] = 1;
		counts[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] = 2;
		counts[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] = 4;
		g_mission_header.num_flight_groups = 3;
		uint16_t *kept = g_mission_fg_stats[2].outcome_count;
		kept[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] = 1;
		kept[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] = 1;
		kept[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] = 1;
		mission_record_craft_outcome(0, 0, outcomes[i]);
		XVT_ASSERT_INT_EQ(kept[moved_to[i]], 0);
		XVT_ASSERT_INT_EQ(
			kept[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT],
			1);
		XVT_ASSERT_INT_EQ(
			kept[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT],
			1);
		XVT_ASSERT_INT_EQ(
			kept[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT],
			1);
		XVT_ASSERT_INT_EQ(counts[moved_to[i]], 7);
		XVT_ASSERT_INT_EQ(
			counts[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT],
			0);
		XVT_ASSERT_INT_EQ(
			counts[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT],
			0);
		XVT_ASSERT_INT_EQ(
			counts[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT],
			0);
	}
}

/* The ended craft is cleared from every craft's last attacker and from the
 * target presets of the players in the flight; other references stay. */
static void check_outcome_clears_references(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	place_craft(1, 1, 0);
	place_craft(2, 1, 1);
	g_test_craft[1].last_attacker_obj_idx = 0;
	g_test_craft[2].last_attacker_obj_idx = 1;
	g_players[2].participation_state = 1;
	g_players[2].target_preset_slot[3] = 0;
	g_players[2].target_preset_slot[1] = 2;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[2].last_attacker_obj_idx, 1);
	XVT_ASSERT_INT_EQ(g_players[2].target_preset_slot[3], -1);
	XVT_ASSERT_INT_EQ(g_players[2].target_preset_slot[1], 2);
}

/* Closing group 1 with 5 craft, 2 arrived, its special cargo craft not yet:
 * the 3 left count as arrived, lost with the mothership, not inspected, not
 * disabled, not captured, not attacked and not boarded, the special cargo
 * craft likewise except arrived; the group has arrived with no rounds left,
 * and group 0 is untouched. */
static void check_close_unavailable_group(void)
{
	fresh_world();
	struct mission_fg_runtime_stats *stats = &g_mission_fg_stats[1];
	stats->outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 5;
	stats->outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] = 2;
	stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
	stats->waves_remaining = 3;
	XVT_ASSERT_INT_EQ(mission_close_unavailable_flight_group_accounting(1),
			  5);
	static const int counted[] = {
		FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP,
		FLIGHT_GROUP_OUTCOME_NOT_INSPECTED,
		FLIGHT_GROUP_OUTCOME_NOT_DISABLED,
		FLIGHT_GROUP_OUTCOME_NOT_CAPTURED,
		FLIGHT_GROUP_OUTCOME_NOT_ATTACKED,
		FLIGHT_GROUP_OUTCOME_NOT_BOARDED,
	};
	for (size_t i = 0; i < sizeof counted / sizeof counted[0]; ++i) {
		XVT_ASSERT_INT_EQ(stats->outcome_count[counted[i]], 3);
		XVT_ASSERT_INT_EQ(stats->special_cargo_outcome[counted[i]], 1);
	}
	XVT_ASSERT_INT_EQ(stats->outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  5);
	XVT_ASSERT_INT_EQ(
		stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_ARRIVED], 0);
	XVT_ASSERT_INT_EQ(stats->has_arrived, 1);
	XVT_ASSERT_INT_EQ(stats->waves_remaining, 0);

	struct mission_fg_runtime_stats untouched;
	memset(&untouched, 0, sizeof untouched);
	XVT_ASSERT_TRUE(memcmp(&g_mission_fg_stats[0], &untouched,
			       sizeof untouched) == 0);
}

/* Goal slot `slot` of group 0 is a per-craft bonus goal (amount 18) on
 * destroying, worth `points`, for team `team`. */
static struct flight_group_goal *per_craft_goal(int slot, int points, int team)
{
	struct flight_group_goal *goal =
		&g_mission_flight_groups[0].fg.goals[slot];
	goal->goal_kind = 2;
	goal->event_condition = MISSION_COND_DESTROYED;
	goal->amount = 18;
	goal->points = (int8_t)points;
	goal->enabled_teams[team] = 1;
	return goal;
}

/* A per-craft bonus goal pays 250 times its points to the player, or with
 * no player to the team's mission score, and the total is returned; another
 * event, a team the goal is not for, or a passed time limit pays nothing. A
 * special-cargo goal (amount 19) pays only for the special cargo craft. */
static void check_flight_group_goal_score(void)
{
	fresh_world();
	per_craft_goal(0, 4, 0);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 0, 0, 0),
			  1000);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score, 1000);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_CAPTURED, 0, 0, 0, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 0, 0, 1),
			  0);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, -1, 0, 0, 0),
			  1000);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_scores[TEAM_SCORE_MISSION][0],
			  1000);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score, 1000);

	struct flight_group_goal *special = per_craft_goal(1, 2, 0);
	special->amount = 19;
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 0, 0, 0),
			  1000);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 0, 1, 0),
			  1500);

	/* A limit of 2 five-second units holds at 10 seconds, not at 11. */
	fresh_world();
	struct flight_group_goal *timed = per_craft_goal(0, 4, 0);
	timed->time_limit5s = 2;
	g_mission_elapsed_clock.seconds = 10;
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 0, 0, 0),
			  1000);
	g_mission_elapsed_clock.seconds = 11;
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 0, 0, 0),
			  0);
}

/* A reduction level above 1 takes level - 1 tenths off a positive score and
 * adds as many to a negative one; a level above 10 counts as 9. */
static void check_flight_group_goal_reduction(void)
{
	fresh_world();
	per_craft_goal(0, 4, 0);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 1, 0, 0),
			  1000);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 2, 0, 0),
			  900);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 3, 0, 0),
			  800);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 10, 0, 0),
			  100);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 11, 0, 0),
			  200);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 12, 0, 0),
			  200);
	fresh_world();
	per_craft_goal(0, -4, 0);
	XVT_ASSERT_INT_EQ(mission_apply_flight_group_goal_score(
				  MISSION_COND_DESTROYED, 0, 0, 3, 0, 0),
			  -1200);
}

/* A per-craft bonus goal pays 250 times its points to the bonus score of
 * each team it is enabled for; the one-team form pays only the team named,
 * and only when the goal is enabled for it. */
static void check_team_goal_score(void)
{
	fresh_world();
	struct flight_group_goal *goal = per_craft_goal(0, 3, 1);
	goal->enabled_teams[4] = 1;
	int *bonus =
		g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS];
	mission_apply_team_goal_score_all_enabled_teams(MISSION_COND_DESTROYED,
							0, 0);
	XVT_ASSERT_INT_EQ(bonus[1], 750);
	XVT_ASSERT_INT_EQ(bonus[4], 750);
	XVT_ASSERT_INT_EQ(bonus[0], 0);
	mission_apply_team_goal_score_all_enabled_teams(MISSION_COND_CAPTURED,
							0, 0);
	XVT_ASSERT_INT_EQ(bonus[1], 750);

	mission_apply_team_goal_score_for_team(MISSION_COND_DESTROYED, 0, 0, 4);
	XVT_ASSERT_INT_EQ(bonus[4], 1500);
	XVT_ASSERT_INT_EQ(bonus[1], 750);
	mission_apply_team_goal_score_for_team(MISSION_COND_DESTROYED, 0, 0, 2);
	XVT_ASSERT_INT_EQ(bonus[2], 0);

	/* A special-cargo goal pays only for the special cargo craft. */
	fresh_world();
	goal = per_craft_goal(0, 3, 1);
	goal->amount = 19;
	mission_apply_team_goal_score_all_enabled_teams(MISSION_COND_DESTROYED,
							0, 0);
	mission_apply_team_goal_score_for_team(MISSION_COND_DESTROYED, 0, 0, 1);
	XVT_ASSERT_INT_EQ(bonus[1], 0);
	mission_apply_team_goal_score_all_enabled_teams(MISSION_COND_DESTROYED,
							0, 1);
	mission_apply_team_goal_score_for_team(MISSION_COND_DESTROYED, 0, 1, 1);
	XVT_ASSERT_INT_EQ(bonus[1], 1500);
}

/* Both clock readings are hours, minutes and seconds as seconds. */
static void check_clock_seconds(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(mission_clock_to_seconds(1, 2, 3), 3723);
	XVT_ASSERT_INT_EQ(mission_clock_to_seconds(0, 0, 59), 59);
	g_mission_elapsed_clock.hours = 2;
	g_mission_elapsed_clock.minutes = 3;
	g_mission_elapsed_clock.seconds = 4;
	XVT_ASSERT_INT_EQ(mission_get_elapsed_clock_seconds(),
			  mission_clock_to_seconds(2, 3, 4));
	XVT_ASSERT_INT_EQ(mission_get_elapsed_clock_seconds(), 7384);
}

/* The model of object type `object_type`. */
static struct model_def *model_of(int object_type)
{
	return &g_model_defs[get_model_index_from_type(
		(object_type_id)object_type)];
}

/* A craft's point value: 40 times its model's craft_point_value, plus each
 * launcher's loaded warheads at their point value. An unknown warhead type is
 * skipped, and an unknown countermeasure or beam type counts as type 0. */
static void check_craft_point_value(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	struct model_def *model = model_of(g_test_objects[0].object_type);
	struct model_def saved = *model;
	int base = 40 * model->craft_point_value;
	XVT_ASSERT_TRUE(base > 0);
	XVT_ASSERT_INT_EQ(mission_compute_craft_point_value(0), base);

	/* Countermeasure types 0 to 3 and beam types 0 to 5 are known. */
	craft->cm_type_id = 4;
	craft->beam_type_id = 6;
	XVT_ASSERT_INT_EQ(mission_compute_craft_point_value(0), base);
	craft->cm_type_id = 0;
	craft->beam_type_id = 0;

	/* One launcher on slots 3 and 4, two warheads in each. */
	model->warhead_launcher_first_slot[0] = 3;
	model->warhead_launcher_last_slot[0] = 4;
	craft->warhead_launcher_count = 1;
	craft->weapon_slots[3].ammo_count = 2;
	craft->weapon_slots[4].ammo_count = 2;
	int warhead = PROJECTILE_OBJECT_TYPE_FIRST;
	while (g_projectile_type_data
		       .warhead_point_value[warhead -
					    PROJECTILE_OBJECT_TYPE_FIRST] ==
	       0) {
		++warhead;
	}
	craft->warhead_slot_type_ids[0] = (uint8_t)warhead;
	int each = g_projectile_type_data
			   .warhead_point_value[warhead -
						PROJECTILE_OBJECT_TYPE_FIRST];
	XVT_ASSERT_INT_EQ(mission_compute_craft_point_value(0),
			  base + 4 * each);
	craft->warhead_slot_type_ids[0] = 5;
	XVT_ASSERT_INT_EQ(mission_compute_craft_point_value(0), base);
	*model = saved;

	/* The super star destroyer counts its model twice. */
	g_test_objects[0].object_type = CRAFT_SPECIES_SUPER_STAR_DESTROYER;
	craft->warhead_launcher_count = 0;
	XVT_ASSERT_INT_EQ(mission_compute_craft_point_value(0),
			  80 * model_of(CRAFT_SPECIES_SUPER_STAR_DESTROYER)
					  ->craft_point_value);
}

/* The first craft type whose object type is in [low, high] and not a space
 * craft. */
static int non_craft_type_in(int low, int high)
{
	for (int type = 1; type < 96; ++type) {
		int object_type = g_craft_type_to_object_type[type];
		if (object_type >= low && object_type <= high &&
		    g_object_type_table[object_type].family_id !=
			    CRAFT_FAMILY_SPACE_CRAFT) {
			return type;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* Destroying an object that is not a space craft scores 500 for object
 * types 0x46 to 0x4A and 0x50 to 0x54, 40 for the rest, three times that in
 * a melee mission; a space craft scores its point value. */
static void check_kill_score(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(0),
			  mission_compute_craft_point_value(0));

	g_mission_flight_groups[1].fg.craft_type =
		(craft_species)non_craft_type_in(0x46, 0x4A);
	place_craft(1, 1, 0);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 500);
	g_mission_flight_groups[1].fg.craft_type =
		(craft_species)non_craft_type_in(0x4A, 0x4A);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 500);
	g_mission_flight_groups[1].fg.craft_type =
		(craft_species)non_craft_type_in(0x50, 0x54);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 500);
	g_mission_flight_groups[1].fg.craft_type =
		(craft_species)non_craft_type_in(0x54, 0x54);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 500);
	g_mission_flight_groups[1].fg.craft_type =
		(craft_species)non_craft_type_in(0x55, 0x5F);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 40);
	g_mission_flight_groups[1].fg.craft_type =
		(craft_species)non_craft_type_in(0x4B, 0x4F);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 40);
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 120);
	g_mission_flight_groups[1].fg.craft_type =
		(craft_species)non_craft_type_in(0x46, 0x4A);
	XVT_ASSERT_INT_EQ(mission_compute_kill_score_for_object(1), 1500);
}

/* Player 0 flies group 0's craft against group 1's, teams 0 and 1 not
 * allied (each allied only with itself), at the middle difficulty. The
 * player's craft is object 0 and group 1's is object 1, which has taken 100
 * damage, all from player 0. */
static void kill_credit_world(void)
{
	fresh_world();
	g_mission_teams[0].allies[0] = 1;
	g_mission_teams[1].allies[1] = 1;
	g_flight_mission_state.difficulty = 1;
	g_players[0].participation_state = 1;
	g_players[0].team = 0;
	g_players[0].bound_flight_group_idx = 0;
	g_players[0].pilot_rating = 5;
	place_craft(0, 0, 0);
	g_test_objects[0].player_owner_idx = 0;
	struct craft_data *victim = place_craft(1, 1, 0);
	victim->damage_stats.damage_received_total = 100;
	victim->damage_stats.damage_from_player[0] = 100;
}

/* A player who did all of a craft's damage, against a team not allied with
 * theirs, gets a full kill: the kill score in mission_score and rating
 * points in rating_promo_points, the victim being an AI craft whose rating
 * is not more than 4 below the player's. */
static void check_kill_credit_full_share(void)
{
	kill_credit_world();
	mission_credit_destruction_damage_contributors(0, 1);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
			  mission_compute_kill_score_for_object(1));
	XVT_ASSERT_TRUE(g_players[0].mission_stats.rating_promo_points > 0);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.worse_rating_promo_points,
			  0);
}

/* Group 1 belongs to player 1, rated 3, and player-owned craft did all of
 * its craft's damage, so the victim's rating is player 1's. Player 0's rating
 * points go to rating_promo_points when 3 is at most 4 below player 0's
 * rating (7), and to worse_rating_promo_points when it is 5 below (8). */
static void check_kill_credit_rating_split(void)
{
	for (int rating = 7; rating <= 8; ++rating) {
		kill_credit_world();
		g_mission_flight_groups[1].player_owner_idx = 1;
		g_players[1].pilot_rating = 3;
		g_test_craft[1]
			.damage_stats.damage_received_by_player_owned_craft =
			100;
		g_players[0].pilot_rating = (uint16_t)rating;
		mission_credit_destruction_damage_contributors(0, 1);
		const struct player_mission_runtime_stats *stats =
			&g_players[0].mission_stats;
		if (rating == 7) {
			XVT_ASSERT_TRUE(stats->rating_promo_points > 0);
			XVT_ASSERT_INT_EQ(stats->worse_rating_promo_points, 0);
		} else {
			XVT_ASSERT_INT_EQ(stats->rating_promo_points, 0);
			XVT_ASSERT_TRUE(stats->worse_rating_promo_points > 0);
		}
	}
}

/* A round of three fits while three craft slots of the active region are
 * free, not with two; a static type always fits. */
static void check_wave_capacity(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.number_of_craft = 3;
	for (int obj = 0; obj < 5; ++obj) {
		place_craft(obj, 1, obj);
	}
	XVT_ASSERT_INT_EQ(mission_has_capacity_for_current_flight_group_wave(),
			  1);
	place_craft(5, 1, 5);
	XVT_ASSERT_INT_EQ(mission_has_capacity_for_current_flight_group_wave(),
			  0);
	g_mission_flight_groups[0].fg.craft_type =
		(craft_species)craft_type_with_static_flag(1);
	XVT_ASSERT_INT_EQ(mission_has_capacity_for_current_flight_group_wave(),
			  1);
}

/* Group 0 arrives from group 1's hangar after the clock has started; group
 * 1 has only a craft that follows a leader. */
static void absent_mothership_world(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.arrival_method = 1;
	g_mission_flight_groups[0].fg.arrival_mothership = 1;
	g_mission_flight_groups[0].fg.number_of_craft = 2;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 6;
	g_mission_elapsed_clock.seconds = 1;
	place_craft(3, 1, 1);
	g_test_craft[3].leader_obj_idx = 2;
}

/* A whole round from a mothership whose group has no leading craft present
 * returns 0 and creates nothing. */
static void check_wave_without_mothership(void)
{
	absent_mothership_world();
	XVT_ASSERT_INT_EQ(mission_spawn_flight_group_wave_craft(UINT16_MAX), 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  0);
	for (int obj = 0; obj < TEST_OBJECT_COUNT; ++obj) {
		if (obj != 3) {
			XVT_ASSERT_INT_EQ(g_test_objects[obj].object_type, 0);
		}
	}
}

/* With group 1's leading craft present, the round starts at its inside
 * hangar point facing its outside one, and returns 1. Its model's hangar
 * points are set to 0 here, so both points are the mothership's position. No
 * craft is left to place: the group's 6 have all arrived. */
static void check_wave_from_mothership(void)
{
	absent_mothership_world();
	place_craft(2, 1, 0);
	g_test_objects[2].world_x = 5000;
	g_test_objects[2].world_y = -6000;
	g_test_objects[2].world_z = 7000;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] = 6;
	struct model_def *model = &g_model_defs[g_test_craft[2].model_index];
	struct model_def saved = *model;
	memset(&model->hangar_points, 0, sizeof model->hangar_points);
	g_world_loc_x = 0;
	g_world_loc_y = 0;
	g_world_loc_z = 0;
	int16_t result = mission_spawn_flight_group_wave_craft(UINT16_MAX);
	*model = saved;
	XVT_ASSERT_INT_EQ(result, 1);
	XVT_ASSERT_INT_EQ(g_world_loc_x, 5000);
	XVT_ASSERT_INT_EQ(g_world_loc_y, -6000);
	XVT_ASSERT_INT_EQ(g_world_loc_z, 7000);
}

/* A player-number group keeps its rounds while craft waves are unlimited,
 * and a group with no rounds left stays at 0. */
static void check_wave_rounds_kept(void)
{
	absent_mothership_world();
	g_mission_flight_groups[0].fg.player_number = 1;
	g_flight_mission_state.player_flight_group_wave_mode =
		CRAFT_WAVES_UNLIMITED;
	g_mission_fg_stats[0].waves_remaining = 2;
	mission_spawn_current_flight_group_wave();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 2);

	absent_mothership_world();
	mission_spawn_current_flight_group_wave();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 0);
}

/* A reference below 0x8000 is an object's position; 0x8000 is the group's
 * current point; 0x8000 + n is mission point n, times 256, y negated. */
static void check_resolve_world_loc(void)
{
	fresh_world();
	g_test_objects[2].world_x = 10;
	g_test_objects[2].world_y = 20;
	g_test_objects[2].world_z = 30;
	mission_resolve_object_or_mission_point_world_loc(2, 1);
	XVT_ASSERT_INT_EQ(g_world_loc_x, 10);
	XVT_ASSERT_INT_EQ(g_world_loc_y, 20);
	XVT_ASSERT_INT_EQ(g_world_loc_z, 30);

	struct xvt_flight_group *fg = &g_mission_flight_groups[1].fg;
	fg->mission_point_x[3] = 1;
	fg->mission_point_y[3] = 2;
	fg->mission_point_z[3] = 3;
	mission_resolve_object_or_mission_point_world_loc(0x8003, 1);
	XVT_ASSERT_INT_EQ(g_world_loc_x, 256);
	XVT_ASSERT_INT_EQ(g_world_loc_y, -512);
	XVT_ASSERT_INT_EQ(g_world_loc_z, 768);

	g_world_loc_x = 0;
	g_mission_fg_stats[1].current_mission_point_ref = 0x8003;
	mission_resolve_object_or_mission_point_world_loc(0x8000, 1);
	XVT_ASSERT_INT_EQ(g_world_loc_x, 256);
	XVT_ASSERT_INT_EQ(g_world_loc_y, -512);
}

/* Known failure bonus_complete_team, issue #118: the function promises 1
 * when a condition is met, and mission.h gives a team's bonus status as 1 for
 * complete. "Team 3's bonus goal is complete" with that status returns 2,
 * failed: the team branch has the two values swapped. */
static void check_bonus_complete_for_team(void)
{
	fresh_world();
	g_flight_mission_state.runtime.team_goal_status[3][2] = 1;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_COMPLETE,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		1);
}

/* Known failure bonus_failed_team, issue #119: team 3's bonus status is 2,
 * failed, so "team 3's bonus goal failed" is met and should return 1. The
 * code reads the any-team bonus flag instead, which only ever holds 0 or 1,
 * and returns 4, undecided. */
static void check_bonus_failed_for_team(void)
{
	fresh_world();
	g_flight_mission_state.runtime.team_goal_status[3][2] = 2;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_FAILED,
					   GOAL_TARGET_TEAM, 3, 0, 0, 10),
		1);
}

/* Known failure player_hit_not_attacked, issue #122: craft.h promises an
 * attacked_by_team entry is nonzero once that team has hit the craft, and the
 * function counts a craft as not attacked "as its state says". A craft that
 * team 1's player hit carries 0x81 there, and is still counted as not
 * attacked, because the code tests for exactly 1. */
static void check_player_hit_counts_as_attacked(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 1);
	craft->attacked_by_team[1] = (int8_t)0x81;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[0]
			.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED],
		0);
}

/* Known failure uncaptured_special_cargo, issue #123: mission.h gives
 * team_special_cargo_uncaptured_lost as the special cargo craft ended
 * uncaptured. Group 0's special cargo craft is number 2; craft number 1 ends
 * uncaptured, and every other team's mark is set anyway. */
static void check_uncaptured_special_cargo_mark(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.special_cargo_craft = 2;
	place_craft(0, 0, 1);
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[0].team_special_cargo_uncaptured_lost[1], 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].team_uncaptured_lost[1], 1);
}

/* Known failure dropoff_group_past_table, issue #124: a destroyed craft
 * closes the group its drop-off order delivers, named in variable2 plus 1.
 * Group 0's first order is a drop-off naming group 49 of a mission with 2;
 * mission_close_unavailable_flight_group_accounting is called for index 48,
 * past the 48 group records, and the sanitizer stops the program on the
 * write. Nothing outside the mission's groups should be touched. */
static void check_dropoff_naming_missing_group(void)
{
	fresh_world();
	g_order_leader_builtin_plan_name_index[1] = 5;
	g_builtin_plan_id_by_name_index[5] = 9;
	strcpy(g_plan_table[9].name, "dropoffldr1pln");
	g_mission_flight_groups[0].fg.orders[0].order = 1;
	g_mission_flight_groups[0].fg.orders[0].variable2 = 49;
	place_craft(0, 0, 0);
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].has_arrived, 0);
}

/* Known failure expired_team_goal_score, issue #36: two per-craft bonus
 * goals for team 1, the first worth 2 with no limit, the second worth 3 with
 * a 5-second limit, at 10 seconds. The comment over each function says what
 * the code does: the expired goal adds the first goal's score again, times
 * 250 once more. mission_apply_flight_group_goal_score skips an expired goal;
 * here too it should pay nothing, leaving 500 for each call. */
static void check_expired_team_goal_pays_nothing(void)
{
	fresh_world();
	per_craft_goal(0, 2, 1);
	per_craft_goal(1, 3, 1)->time_limit5s = 1;
	g_mission_elapsed_clock.seconds = 10;
	int *bonus =
		g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS];
	mission_apply_team_goal_score_all_enabled_teams(MISSION_COND_DESTROYED,
							0, 0);
	XVT_ASSERT_INT_EQ(bonus[1], 500);
	mission_apply_team_goal_score_for_team(MISSION_COND_DESTROYED, 0, 0, 1);
	XVT_ASSERT_INT_EQ(bonus[1], 1000);
}

/* Known failure round_spent_without_mothership, issue #137: mission.h gives
 * waves_remaining as the rounds still to come. Group 0's round cannot launch
 * because its mothership's group has no leading craft present, and
 * mission_spawn_flight_group_wave_craft says so by returning 0; the round is
 * still taken off, 2 becomes 1. */
static void check_round_kept_without_mothership(void)
{
	absent_mothership_world();
	g_mission_fg_stats[0].waves_remaining = 2;
	mission_spawn_current_flight_group_wave();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 2);
}

/* Known failure missile_boat_second_launcher, issue #127: a missile boat
 * (object type 12) with no warhead choice is spawned with launcher 0 empty,
 * launcher 1 loaded with advanced missiles (type 149) and a launcher count of
 * 1. The function promises each launcher's loaded warheads in the point
 * value, but it walks launchers 0 to count - 1 and never reaches launcher 1. */
static void check_missile_boat_launchers_counted(void)
{
	enum { MISSILE_BOAT = 12, ADVANCED_MISSILE = 149 };

	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	g_test_objects[0].object_type = MISSILE_BOAT;
	struct model_def *model = model_of(MISSILE_BOAT);
	model->warhead_launcher_first_slot[1] = 5;
	model->warhead_launcher_last_slot[1] = 6;
	craft->warhead_launcher_count = 1;
	craft->warhead_slot_type_ids[0] = 0;
	craft->warhead_slot_type_ids[1] = ADVANCED_MISSILE;
	int without = mission_compute_craft_point_value(0);
	craft->weapon_slots[5].ammo_count = 8;
	XVT_ASSERT_TRUE(without > 0);
	craft->weapon_slots[6].ammo_count = 8;
	int each = g_projectile_type_data
			   .warhead_point_value[ADVANCED_MISSILE -
						PROJECTILE_OBJECT_TYPE_FIRST];
	XVT_ASSERT_INT_EQ(mission_compute_craft_point_value(0),
			  without + 16 * each);
}

/* Known failure delivery_resets_rounds, issue #136: mission.h gives
 * waves_remaining as the rounds still to come. Group 0 has arrived, its 6
 * craft (2 craft, 2 extra rounds) all came, and no rounds are left. A drop-off
 * delivers each craft through mission_start_flight_group_arrival, and the
 * next delivery sets the rounds back to 2, rounds no craft is left for. */
static void check_delivery_keeps_rounds(void)
{
	fresh_world();
	struct xvt_flight_group *fg = &g_mission_flight_groups[0].fg;
	fg->number_of_craft = 2;
	fg->number_of_waves = 2;
	struct mission_fg_runtime_stats *stats = &g_mission_fg_stats[0];
	stats->has_arrived = 1;
	stats->outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 6;
	stats->outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] = 6;
	stats->waves_remaining = 0;
	stats->current_mission_point_ref = 0x8000;
	XVT_ASSERT_INT_EQ(mission_start_flight_group_arrival(1), 1);
	XVT_ASSERT_INT_EQ(stats->waves_remaining, 0);
}

/* Known failure attacker_weight_zero, issue #126: the function promises a
 * player rating points for a share in destroying a craft of a team not
 * allied with theirs, and opt_model.h says they are divided by the weight of
 * the attacker's craft. Group 0, the player's, flies a craft type whose model
 * has a rating weight of 0; the division by 0 stops the program. */
static void check_kill_credit_weightless_attacker(void)
{
	kill_credit_world();
	int weightless = 0;
	for (int type = 1; type < 96 && weightless == 0; ++type) {
		uint8_t object_type = g_craft_type_to_object_type[type];
		int model = g_object_type_table[object_type].model_index;
		if (object_type != 0 && model < 73 &&
		    g_model_defs[model].rating_weight == 0) {
			weightless = type;
		}
	}
	XVT_ASSERT_TRUE(weightless != 0);
	g_mission_flight_groups[0].fg.craft_type = (craft_species)weightless;
	mission_credit_destruction_damage_contributors(0, 1);
	XVT_ASSERT_TRUE(g_players[0].mission_stats.rating_promo_points > 0);
}

static struct xvt_test_assets g_assets;

static struct tie_radio_message g_tie_messages[4];
static int16_t g_tie_message_count;
/* The length of the briefing text mission_load_file skips: the first string
 * of each list has this many bytes. */
static int16_t g_tie_briefing_text_length;

/* Writes a mission file in the TIE format, version 0xFFFF: the counts of
 * flight groups, messages (g_tie_messages, none unless a check sets them) and
 * goals, the header, each flight group, each message, each goal, then the
 * briefing the loader skips, 810 bytes and 2 lists of 32 strings (empty
 * unless g_tie_briefing_text_length says so). */
static void write_tie_mission(const char *name, uint16_t version,
			      const struct e_mission_struct *header,
			      const struct efg_struct *groups,
			      int16_t group_count,
			      const struct e_mission_goal *goals,
			      int16_t goal_count)
{
	static uint8_t file[8192];
	size_t size = 0;
	int16_t message_count = g_tie_message_count;
	memset(file, 0, sizeof file);
	memcpy(file + size, &version, sizeof version);
	size += sizeof version;
	memcpy(file + size, &group_count, sizeof group_count);
	size += sizeof group_count;
	memcpy(file + size, &message_count, sizeof message_count);
	size += sizeof message_count;
	memcpy(file + size, &goal_count, sizeof goal_count);
	size += sizeof goal_count;
	memcpy(file + size, header, sizeof *header);
	size += sizeof *header;
	memcpy(file + size, groups, group_count * sizeof *groups);
	size += group_count * sizeof *groups;
	memcpy(file + size, g_tie_messages,
	       message_count * sizeof g_tie_messages[0]);
	size += message_count * sizeof g_tie_messages[0];
	memcpy(file + size, goals, goal_count * sizeof *goals);
	size += goal_count * sizeof *goals;
	size += 810;
	for (int list = 0; list < 2; ++list) {
		for (int string = 0; string < 32; ++string) {
			int16_t length =
				string == 0 ? g_tie_briefing_text_length : 0;
			memcpy(file + size, &length, sizeof length);
			size += sizeof length;
			memset(file + size, 'b', (size_t)length);
			size += (size_t)length;
		}
	}
	XVT_ASSERT_TRUE(size <= sizeof file);
	xvt_test_write_file(g_assets.asset, name, file, size);
}

static struct e_mission_struct g_tie_header;
static struct efg_struct g_tie_groups[8];
static struct e_mission_goal g_tie_goals[8];

/* A TIE mission of four groups on IFF 1, 0, 2 and 3, IFF 2's name starting
 * with '1'. Group 0 is the player's, flying its second craft; its role is
 * "aZ1m", its orders 3, 5 and 7, its goals destroy (2), capture (4), arrive
 * (1) and inspect (5) worth 7, and its waypoints 0 to 14 are at x = 10 times
 * the index (the y of waypoint 0 is 77). Its two global goals test destroyed
 * and never. */
static void build_tie_mission(void)
{
	memset(&g_tie_header, 0, sizeof g_tie_header);
	memset(g_tie_groups, 0, sizeof g_tie_groups);
	memset(g_tie_goals, 0, sizeof g_tie_goals);
	memset(g_tie_messages, 0, sizeof g_tie_messages);
	g_tie_message_count = 0;
	g_tie_briefing_text_length = 0;
	strcpy(g_tie_header.neutral_name[0], "1Pirates");
	strcpy(g_tie_header.neutral_name[1], "Traders");
	static const uint8_t sides[4] = {1, 0, 2, 3};
	for (int i = 0; i < 4; ++i) {
		strcpy(g_tie_groups[i].name, "Group");
		g_tie_groups[i].species = TEST_XWING;
		g_tie_groups[i].count = 2;
		g_tie_groups[i].side = sides[i];
	}
	struct efg_struct *group = &g_tie_groups[0];
	strcpy(group->cmdr, "aZ1m");
	group->player_flag = 2;
	group->ai[0].order = 3;
	group->ai[1].order = 5;
	group->ai[2].order = 7;
	group->ai[2].pri_id = 9;
	group->pri_win_cond = MISSION_COND_DESTROYED;
	group->secondary_win_cond = MISSION_COND_CAPTURED;
	group->loss_cond = MISSION_COND_ARRIVED;
	group->bonus_cond = MISSION_COND_INSPECTED;
	group->bonus_points = 7;
	for (int i = 0; i < 15; ++i) {
		group->way_x[i] = (int16_t)(10 * i);
		group->way_used[i] = 1;
	}
	group->way_y[0] = 77;
	for (int i = 0; i < 2; ++i) {
		g_tie_goals[i].subcond[0].cond = MISSION_COND_DESTROYED;
		g_tie_goals[i].subcond[1].cond = MISSION_COND_NEVER;
	}
}

/* Builds the TIE mission above, writes it as "tie.tie" in a fresh asset
 * folder and loads it into a fresh world, whose team 0 global goal 0 has
 * "never" in the second pair, which a TIE goal does not fill; goal 1 has
 * "always true" there. Returns the loader's result. */
static int load_tie_mission(void)
{
	fresh_world();
	struct mission_trigger_pair *second =
		&g_mission_global_goals[0][0].trigger_pairs[1];
	second->triggers[0].condition = MISSION_COND_NEVER;
	second->triggers[1].condition = MISSION_COND_NEVER;
	xvt_test_open_assets(&g_assets);
	write_tie_mission("tie.tie", UINT16_MAX, &g_tie_header, g_tie_groups, 4,
			  g_tie_goals, 2);
	int result = mission_load_file("tie.tie");
	xvt_test_close_assets(&g_assets);
	return result;
}

/* A TIE-format file loads, and each group's team follows its IFF: 0 and 4
 * on team 1; 2, 3 and 5 on team 1 when their name starts with '1'; the rest
 * on team 0. */
static void check_load_tie_teams(void)
{
	build_tie_mission();
	XVT_ASSERT_INT_EQ(load_tie_mission(), 1);
	XVT_ASSERT_INT_EQ(g_mission_file_version, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_mission_header.num_flight_groups, 4);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.iff, 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.team, 0);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.team, 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.team, 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].fg.team, 0);
}

/* A TIE group converts to player number 1 and the craft the player flies,
 * its orders' codes and targets, its goals as primary, bonus, prevent and
 * bonus, and its first 15 waypoints. */
static void check_load_tie_group(void)
{
	build_tie_mission();
	XVT_ASSERT_INT_EQ(load_tie_mission(), 1);
	const struct xvt_flight_group *fg = &g_mission_flight_groups[0].fg;
	XVT_ASSERT_INT_EQ(fg->player_number, 1);
	XVT_ASSERT_INT_EQ(fg->player_craft, 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.player_number, 0);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].player_owner_idx, -1);
	XVT_ASSERT_INT_EQ(fg->number_of_craft, 2);
	XVT_ASSERT_INT_EQ(fg->orders[0].order, 3);
	XVT_ASSERT_INT_EQ(fg->orders[1].order, 5);
	XVT_ASSERT_INT_EQ(fg->orders[2].target1, 9);
	static const uint8_t kinds[4] = {0, 2, 1, 2};
	static const uint8_t conditions[4] = {
		MISSION_COND_DESTROYED, MISSION_COND_CAPTURED,
		MISSION_COND_ARRIVED, MISSION_COND_INSPECTED};
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(fg->goals[i].goal_kind, kinds[i]);
		XVT_ASSERT_INT_EQ(fg->goals[i].event_condition, conditions[i]);
	}
	XVT_ASSERT_INT_EQ(fg->goals[3].points, 7);
	for (int i = 0; i < 15; ++i) {
		XVT_ASSERT_INT_EQ(fg->mission_point_x[i], 10 * i);
		XVT_ASSERT_INT_EQ(fg->mission_point_enabled[i], 1);
	}
	XVT_ASSERT_INT_EQ(fg->mission_point_x[15], 0);
}

/* After any version: the role is upper case, each team is allied with
 * itself, and global goal 0's trailing "never" conditions become "always
 * true" joined by AND; goal 1's "never" is not trailing, and stays. */
static void check_load_common_tail(void)
{
	build_tie_mission();
	XVT_ASSERT_INT_EQ(load_tie_mission(), 1);
	XVT_ASSERT_TRUE(
		strcmp(g_mission_flight_groups[0].fg.craft_role, "AZ1M") == 0);
	for (int team = 0; team < 10; ++team) {
		XVT_ASSERT_INT_EQ(g_mission_teams[team].allies[team], 1);
	}
	const struct mission_trigger_pair *pair =
		&g_mission_global_goals[0][0].trigger_pairs[0];
	XVT_ASSERT_INT_EQ(pair->triggers[0].condition, MISSION_COND_DESTROYED);
	XVT_ASSERT_INT_EQ(pair->triggers[1].condition,
			  MISSION_COND_ALWAYS_TRUE);
	XVT_ASSERT_INT_EQ(pair->trigger1_or_trigger2, 0);
	XVT_ASSERT_INT_EQ(pair[1].triggers[0].condition,
			  MISSION_COND_ALWAYS_TRUE);
	XVT_ASSERT_INT_EQ(pair[1].triggers[1].condition,
			  MISSION_COND_ALWAYS_TRUE);
	XVT_ASSERT_INT_EQ(g_mission_global_goals[0][1]
				  .trigger_pairs[0]
				  .triggers[1]
				  .condition,
			  MISSION_COND_NEVER);
}

/* A file whose version is not 12, 13, 14 or 0xFFFF is refused with 0. The
 * loader leaves the file open then, so the check closes it. */
static void check_load_refuses_version(void)
{
	build_tie_mission();
	fresh_world();
	xvt_test_open_assets(&g_assets);
	write_tie_mission("old.tie", 11, &g_tie_header, g_tie_groups, 4,
			  g_tie_goals, 2);
	XVT_ASSERT_INT_EQ(mission_load_file("old.tie"), 0);
	XVT_ASSERT_INT_EQ(g_mission_header.num_flight_groups, 2);
	if (g_stream != NULL) {
		fe_disk_io_close_global_stream(0);
	}
	xvt_test_close_assets(&g_assets);
}

/* Known failure tie_third_order_code, issue #38: mission.h says each TIE
 * order's code is copied to mission_order.order, and the three TIE orders to
 * orders 0 to 2. Group 0's third order, code 7, is loaded with the second
 * order's code, 5. */
static void check_tie_third_order(void)
{
	build_tie_mission();
	XVT_ASSERT_INT_EQ(load_tie_mission(), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.orders[2].order, 7);
}

/* Known failure tie_header_kept, issue #133: the loader reads a mission file
 * into g_mission_header. A TIE file has no mission type, goals setting or
 * time limit, but the header keeps those of the mission loaded before, here
 * a melee mission whose goals do not matter, with a 30-minute limit. */
static void check_tie_header_replaced(void)
{
	build_tie_mission();
	fresh_world();
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	g_mission_header.goals_unimportant = 1;
	g_mission_header.time_limit_minutes = 30;
	xvt_test_open_assets(&g_assets);
	write_tie_mission("tie.tie", UINT16_MAX, &g_tie_header, g_tie_groups, 4,
			  g_tie_goals, 2);
	XVT_ASSERT_INT_EQ(mission_load_file("tie.tie"), 1);
	xvt_test_close_assets(&g_assets);
	XVT_ASSERT_INT_EQ(g_mission_header.mission_type, 0);
	XVT_ASSERT_INT_EQ(g_mission_header.goals_unimportant, 0);
	XVT_ASSERT_INT_EQ(g_mission_header.time_limit_minutes, 0);
}

/* Known failure tie_goal_text_handles_kept, issue #129: the loader reads a
 * mission file into the goal text handles too. A TIE file has no goal texts,
 * but the handle arrays keep the numbers of the mission loaded before, whose
 * blocks the end of that flight already freed; the goals page would draw them
 * and the next flight's end would free them again. */
static void check_tie_goal_text_handles_cleared(void)
{
	build_tie_mission();
	fresh_world();
	memset(g_mission_fg_override_string_handles, 0,
	       sizeof g_mission_fg_override_string_handles);
	memset(g_global_goal_override_string_handles, 0,
	       sizeof g_global_goal_override_string_handles);
	g_mission_fg_override_string_handles[0][0][0] = 5;
	g_global_goal_override_string_handles[0][0][0][0] = 6;
	xvt_test_open_assets(&g_assets);
	write_tie_mission("tie.tie", UINT16_MAX, &g_tie_header, g_tie_groups, 4,
			  g_tie_goals, 2);
	XVT_ASSERT_INT_EQ(mission_load_file("tie.tie"), 1);
	xvt_test_close_assets(&g_assets);
	XVT_ASSERT_INT_EQ(g_mission_fg_override_string_handles[0][0][0], 0);
	XVT_ASSERT_INT_EQ(g_global_goal_override_string_handles[0][0][0][0], 0);
}

/* The engine's log lines, kept without Aeron's "xvt: " prefix; lines past the
 * capacity are dropped. */
enum {
	LOG_LINE_CAPACITY = 800,
	LOG_LINE_SIZE = 320,
};

static char g_log_lines[LOG_LINE_CAPACITY][LOG_LINE_SIZE];
static int g_log_line_count;

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	static const char prefix[] = "xvt: ";
	if (g_log_line_count >= LOG_LINE_CAPACITY) {
		return;
	}
	if (strncmp(message, prefix, sizeof prefix - 1) == 0) {
		message += sizeof prefix - 1;
	}
	snprintf(g_log_lines[g_log_line_count++], LOG_LINE_SIZE, "%s", message);
}

/* The last kept line that starts with start (an event and any of its first
 * fields), or NULL. */
static const char *find_line(const char *start)
{
	size_t length = strlen(start);
	for (int i = g_log_line_count - 1; i >= 0; --i) {
		if (strncmp(g_log_lines[i], start, length) == 0) {
			return g_log_lines[i];
		}
	}
	return NULL;
}

/* 1 when the last kept line that starts with start also holds fields. */
static int line_holds(const char *start, const char *fields)
{
	const char *line = find_line(start);
	return line != NULL && strstr(line, fields) != NULL;
}

/* The number of kept lines that start with start. */
static int count_lines(const char *start)
{
	size_t length = strlen(start);
	int count = 0;
	for (int i = 0; i < g_log_line_count; ++i) {
		if (strncmp(g_log_lines[i], start, length) == 0) {
			++count;
		}
	}
	return count;
}

/* Fills size bytes at memory with a sequence that differs at every offset and
 * with the seed, so a field copied from the wrong place shows. */
static void pattern_fill(void *memory, size_t size, unsigned int seed)
{
	uint8_t *bytes = memory;
	for (size_t i = 0; i < size; ++i) {
		bytes[i] = (uint8_t)(seed + 7 * i);
	}
}

/* A mission file of versions 12 to 14, built field by field in g_stored and
 * written by write_stored_mission in the order mission_load_file reads it. */
static struct {
	struct mission_header header;
	struct xvt_flight_group groups[48];
	int16_t message_index[MISSION_MESSAGE_COUNT];
	struct mission_message messages[MISSION_MESSAGE_COUNT];
	int16_t goal_count[10];
	struct global_goal goals[10][7];
	int16_t team_count[10];
	struct team teams[10];
	/* The length of the briefing text mission_load_file skips: the first
	 * string of each list of the odd briefings has this many bytes. A
	 * negative length is written without text. */
	int16_t briefing_text_length;
	/* Replaces the length of the last string of the last briefing. */
	int16_t last_briefing_length;
	char flight_group_text[48][8][3][64];
	char team_goal_text[10][7][4][3][64];
} g_stored;

static uint8_t g_file_image[400000];

static size_t put_bytes(size_t size, const void *data, size_t length)
{
	XVT_ASSERT_TRUE(size + length <= sizeof g_file_image);
	memcpy(g_file_image + size, data, length);
	return size + length;
}

static size_t put_fill(size_t size, uint8_t byte, size_t length)
{
	XVT_ASSERT_TRUE(size + length <= sizeof g_file_image);
	memset(g_file_image + size, byte, length);
	return size + length;
}

/* A list of 32 briefing strings, each a 16-bit length and the text: the first
 * has first_length bytes, the last last_length, the rest none. */
static size_t put_text_list(size_t size, int16_t first_length,
			    int16_t last_length)
{
	for (int string = 0; string < 32; ++string) {
		int16_t length = 0;
		if (string == 0) {
			length = first_length;
		}
		if (string == 31) {
			length = last_length;
		}
		size = put_bytes(size, &length, sizeof length);
		if (length > 0) {
			size = put_fill(size, 'b', (size_t)length);
		}
	}
	return size;
}

static void write_stored_mission(const char *name, uint16_t version)
{
	size_t size = 0;
	int groups = g_stored.header.num_flight_groups;
	size = put_bytes(size, &version, sizeof version);
	size = put_bytes(size, &g_stored.header, sizeof g_stored.header);
	size = put_bytes(size, g_stored.groups,
			 (size_t)groups * sizeof g_stored.groups[0]);
	for (int i = 0; i < (int16_t)g_stored.header.num_messages; ++i) {
		size = put_bytes(size, &g_stored.message_index[i],
				 sizeof g_stored.message_index[0]);
		size = put_bytes(size, &g_stored.messages[i],
				 sizeof g_stored.messages[0]);
	}
	for (int team = 0; team < 10; ++team) {
		size = put_bytes(size, &g_stored.goal_count[team],
				 sizeof g_stored.goal_count[0]);
		size = put_bytes(size, g_stored.goals[team],
				 (size_t)g_stored.goal_count[team] *
					 sizeof g_stored.goals[0][0]);
	}
	for (int team = 0; team < 10; ++team) {
		size = put_bytes(size, &g_stored.team_count[team],
				 sizeof g_stored.team_count[0]);
		if (g_stored.team_count[team] != 0) {
			size = put_bytes(size, &g_stored.teams[team],
					 sizeof g_stored.teams[0]);
		}
	}
	for (int player = 0; player < 8; ++player) {
		int16_t length =
			player % 2 != 0 ? g_stored.briefing_text_length : 0;
		int16_t last_length =
			player == 7 ? g_stored.last_briefing_length : 0;
		size = put_fill(size, 0, 810 + 10);
		size = put_text_list(size, length, 0);
		size = put_text_list(size, length, last_length);
	}
	size = put_bytes(size, g_stored.flight_group_text,
			 (size_t)groups * sizeof g_stored.flight_group_text[0]);
	size = put_bytes(size, g_stored.team_goal_text,
			 sizeof g_stored.team_goal_text);
	xvt_test_write_file(g_assets.asset, name, g_file_image, size);
}

/* Two groups of X-wings on teams 0 and 1, no messages, no goals, no team
 * records and no text, the briefings 5 bytes of text each. */
static void stored_reset(void)
{
	memset(&g_stored, 0, sizeof g_stored);
	g_stored.header.num_flight_groups = 2;
	g_stored.briefing_text_length = 5;
	for (int fg = 0; fg < 2; ++fg) {
		snprintf(g_stored.groups[fg].name,
			 sizeof g_stored.groups[fg].name, "Group %d", fg);
		g_stored.groups[fg].craft_type = TEST_XWING;
		g_stored.groups[fg].number_of_craft = 2;
		g_stored.groups[fg].team = (uint8_t)fg;
		g_stored.groups[fg].iff = (uint8_t)fg;
	}
}

/* The world a stored mission loads into: the two groups of fresh_world, no
 * messages, no goal texts and an empty log. */
static void stored_world(void)
{
	fresh_world();
	memset(g_mission_messages, 0, sizeof g_mission_messages);
	memset(g_mission_fg_override_string_handles, 0,
	       sizeof g_mission_fg_override_string_handles);
	memset(g_global_goal_override_string_handles, 0,
	       sizeof g_global_goal_override_string_handles);
	g_log_line_count = 0;
}

/* Fills the stack the next call will use with a byte that is not 0, so
 * what the loader leaves unwritten in a buffer of its own is not a lucky
 * run of zeros. */
__attribute__((noinline)) static void dirty_stack(void)
{
	volatile unsigned char scratch[32768];
	for (size_t i = 0; i < sizeof scratch; ++i) {
		scratch[i] = 'X';
	}
}

/* Writes g_stored as "stored.mis", of the given version, into a fresh asset
 * folder and loads it; returns the loader's result. */
static int load_stored(uint16_t version)
{
	xvt_test_open_assets(&g_assets);
	write_stored_mission("stored.mis", version);
	dirty_stack();
	int result = mission_load_file("stored.mis");
	xvt_test_close_assets(&g_assets);
	return result;
}

/* A global goal of seed-dependent bytes whose four conditions are all "destroyed",
 * so the loader's last step leaves it alone. */
static void plain_goal(struct global_goal *goal, unsigned int seed)
{
	pattern_fill(goal, sizeof *goal, seed);
	for (int pair = 0; pair < 2; ++pair) {
		for (int trigger = 0; trigger < 2; ++trigger) {
			goal->trigger_pairs[pair].triggers[trigger].condition =
				MISSION_COND_DESTROYED;
		}
	}
}

/* Versions 12, 13 and 14 read the same records: the header replaces the old
 * one whole, each flight group is stored as it is with no player owning it,
 * and the loader returns 1. */
static void check_load_stored_versions(void)
{
	static const uint16_t versions[] = {12, 13, 14};
	for (size_t i = 0; i < sizeof versions / sizeof versions[0]; ++i) {
		stored_reset();
		pattern_fill(&g_stored.header, sizeof g_stored.header, 3);
		g_stored.header.num_flight_groups = 2;
		g_stored.header.num_messages = 0;
		g_stored.header.mission_type = MISSION_TYPE_MELEE;
		g_stored.header.backdrop = 7;
		g_stored.header.time_limit_minutes = 30;
		for (int fg = 0; fg < 2; ++fg) {
			pattern_fill(&g_stored.groups[fg],
				     sizeof g_stored.groups[fg], 40 + fg);
			strcpy(g_stored.groups[fg].craft_role, "ABC1");
		}
		stored_world();
		g_mission_header.reserved[5] = 99;
		g_mission_flight_groups[0].player_owner_idx = 3;
		g_mission_flight_groups[1].player_owner_idx = 4;
		XVT_ASSERT_INT_EQ(load_stored(versions[i]), 1);
		XVT_ASSERT_INT_EQ(g_mission_file_version, versions[i]);
		XVT_ASSERT_INT_EQ(memcmp(&g_mission_header, &g_stored.header,
					 sizeof g_stored.header),
				  0);
		for (int fg = 0; fg < 2; ++fg) {
			XVT_ASSERT_INT_EQ(
				memcmp(&g_mission_flight_groups[fg].fg,
				       &g_stored.groups[fg],
				       sizeof g_stored.groups[fg]),
				0);
			XVT_ASSERT_INT_EQ(
				g_mission_flight_groups[fg].player_owner_idx,
				-1);
		}
		XVT_ASSERT_TRUE(line_holds(
			"mission.file_header",
			"groups=2 messages=0 backdrop=7 minutes=30"));
	}
}

/* A file whose version is not 12, 13, 14 or 0xFFFF is refused with 0 and
 * leaves the tables as they were. Version 10, which the loader's own branches
 * name, is refused too. */
static void check_load_refuses_other_versions(void)
{
	static const uint16_t versions[] = {0, 10, 11, 15, 0xFFFE};
	for (size_t i = 0; i < sizeof versions / sizeof versions[0]; ++i) {
		stored_reset();
		stored_world();
		g_mission_header.mission_type = MISSION_TYPE_COMBAT;
		XVT_ASSERT_INT_EQ(load_stored(versions[i]), 0);
		XVT_ASSERT_INT_EQ(g_mission_file_version, versions[i]);
		XVT_ASSERT_INT_EQ(g_mission_header.mission_type,
				  MISSION_TYPE_COMBAT);
		XVT_ASSERT_INT_EQ(g_mission_header.num_flight_groups, 2);
		if (g_stream != NULL) {
			fe_disk_io_close_global_stream(0);
		}
	}
}

/* Known failure refused_version_stays_open, issue #266: a refused file's
 * stream is closed with the loader's other ending; the loader returns 0 for a
 * version it does not know and leaves the file open. */
static void check_refused_version_closes_file(void)
{
	stored_reset();
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(11), 0);
	XVT_ASSERT_TRUE(g_stream == NULL);
}

/* Each message goes to the index the file gives it, in any order, and the
 * messages the file does not name are left alone. */
static void check_load_stored_messages(void)
{
	static const int16_t indexes[3] = {5, 0, MISSION_MESSAGE_COUNT - 1};
	stored_reset();
	g_stored.header.num_messages = 3;
	for (int i = 0; i < 3; ++i) {
		g_stored.message_index[i] = indexes[i];
		pattern_fill(&g_stored.messages[i], sizeof g_stored.messages[i],
			     90 + i);
		snprintf(g_stored.messages[i].message,
			 sizeof g_stored.messages[i].message, "Message %d", i);
	}
	stored_world();
	strcpy(g_mission_messages[1].message, "Old");
	XVT_ASSERT_INT_EQ(load_stored(13), 1);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(memcmp(&g_mission_messages[indexes[i]],
					 &g_stored.messages[i],
					 sizeof g_stored.messages[i]),
				  0);
	}
	XVT_ASSERT_TRUE(strcmp(g_mission_messages[1].message, "Old") == 0);
	XVT_ASSERT_TRUE(strcmp(g_mission_messages[2].message, "") == 0);
}

/* A message count of 0x8000 or more is over the table's size, which the loader
 * warns of; its loop treats the count as negative and reads none. */
static void check_load_stored_messages_over(void)
{
	stored_reset();
	g_stored.header.num_messages = 0x8000;
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(14), 1);
	XVT_ASSERT_TRUE(line_holds("mission.file_counts_over",
				   "groups=2 messages=32768"));
	stored_reset();
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(14), 1);
	XVT_ASSERT_INT_EQ(count_lines("mission.file_counts_over"), 0);
}

/* Each team has as many global goals as the file counts for it; a team that
 * counts none keeps what it had, and so does a goal past the count. A count
 * over 7 is warned of. */
static void check_load_stored_team_goals(void)
{
	static const int16_t counts[10] = {3, 0, 7, 1, 0, 0, 0, 0, 0, 2};
	stored_reset();
	for (int team = 0; team < 10; ++team) {
		g_stored.goal_count[team] = counts[team];
		for (int goal = 0; goal < counts[team]; ++goal) {
			plain_goal(&g_stored.goals[team][goal],
				   (unsigned int)(10 * team + goal));
		}
	}
	stored_world();
	for (int team = 0; team < 10; ++team) {
		for (int goal = 0; goal < 7; ++goal) {
			strcpy(g_mission_global_goals[team][goal].name, "Old");
		}
	}
	XVT_ASSERT_INT_EQ(load_stored(12), 1);
	for (int team = 0; team < 10; ++team) {
		for (int goal = 0; goal < 7; ++goal) {
			if (goal < counts[team]) {
				XVT_ASSERT_INT_EQ(
					memcmp(&g_mission_global_goals[team]
								      [goal],
					       &g_stored.goals[team][goal],
					       sizeof g_stored.goals[0][0]),
					0);
			} else {
				XVT_ASSERT_TRUE(strcmp(g_mission_global_goals
							       [team][goal]
								       .name,
						       "Old") == 0);
			}
		}
	}
	XVT_ASSERT_TRUE(
		line_holds("mission.team_goals_read team=2 ", "count=7"));
	XVT_ASSERT_TRUE(
		line_holds("mission.team_goals_read team=9 ", "count=2"));
	XVT_ASSERT_INT_EQ(count_lines("mission.team_goals_read"), 10);
	XVT_ASSERT_INT_EQ(count_lines("mission.goal_count_invalid"), 0);
}

/* A team that counts 8 goals is warned of and still reads them all, the
 * eighth into the next team's first place, which that team's own read then
 * replaces. */
static void check_load_stored_team_goals_over(void)
{
	stored_reset();
	g_stored.goal_count[0] = 8;
	g_stored.goal_count[1] = 1;
	for (int goal = 0; goal < 8; ++goal) {
		plain_goal(&g_stored.goals[0][goal], (unsigned int)goal);
	}
	plain_goal(&g_stored.goals[1][0], 200);
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(13), 1);
	XVT_ASSERT_TRUE(
		line_holds("mission.goal_count_invalid team=0 ", "count=8"));
	XVT_ASSERT_INT_EQ(count_lines("mission.goal_count_invalid"), 1);
	XVT_ASSERT_INT_EQ(memcmp(&g_mission_global_goals[0][6],
				 &g_stored.goals[0][6],
				 sizeof g_stored.goals[0][0]),
			  0);
	XVT_ASSERT_INT_EQ(memcmp(&g_mission_global_goals[1][0],
				 &g_stored.goals[1][0],
				 sizeof g_stored.goals[0][0]),
			  0);
}

/* A team record is read when the file's count for the team is nonzero, the
 * same record for any count; a team without one keeps its record. */
static void check_load_stored_team_records(void)
{
	static const int16_t counts[10] = {1, 0, 3, 0, 0, 0, 0, 0, 0, 255};
	stored_reset();
	for (int team = 0; team < 10; ++team) {
		g_stored.team_count[team] = counts[team];
		if (counts[team] != 0) {
			pattern_fill(&g_stored.teams[team],
				     sizeof g_stored.teams[team],
				     (unsigned int)(60 + team));
			g_stored.teams[team].allies[team] = 1;
		}
	}
	stored_world();
	strcpy(g_mission_teams[1].name, "Old");
	g_mission_teams[1].allies[3] = 1;
	XVT_ASSERT_INT_EQ(load_stored(14), 1);
	for (int team = 0; team < 10; ++team) {
		if (counts[team] != 0) {
			XVT_ASSERT_INT_EQ(memcmp(&g_mission_teams[team],
						 &g_stored.teams[team],
						 sizeof g_stored.teams[team]),
					  0);
		}
	}
	XVT_ASSERT_TRUE(strcmp(g_mission_teams[1].name, "Old") == 0);
	XVT_ASSERT_INT_EQ(g_mission_teams[1].allies[3], 1);
}

/* Counts the nonzero handles in the flight group goal text table. */
static int fg_text_handle_count(void)
{
	int count = 0;
	for (int fg = 0; fg < 48; ++fg) {
		for (int goal = 0; goal < 8; ++goal) {
			for (int text = 0; text < 3; ++text) {
				count += g_mission_fg_override_string_handles
						 [fg][goal][text] != 0;
			}
		}
	}
	return count;
}

/* Counts the nonzero handles in the global goal text table. */
static int team_text_handle_count(void)
{
	int count = 0;
	for (int team = 0; team < 10; ++team) {
		for (int goal = 0; goal < 7; ++goal) {
			for (int trigger = 0; trigger < 4; ++trigger) {
				for (int text = 0; text < 3; ++text) {
					count +=
						g_global_goal_override_string_handles
							[team][goal][trigger]
							[text] != 0;
				}
			}
		}
	}
	return count;
}

/* Each nonempty 64-byte goal text goes into a new memory handle holding the
 * text and its terminator, in the table of its place; an empty one leaves 0.
 * The briefing text the loader skips does not shift them. */
static void check_load_stored_goal_texts(void)
{
	stored_reset();
	strcpy(g_stored.flight_group_text[0][0][0], "First");
	strcpy(g_stored.flight_group_text[0][7][2], "Last of group 0");
	strcpy(g_stored.flight_group_text[1][2][1], "Destroy the freighter");
	strcpy(g_stored.team_goal_text[0][0][0][0], "Team 0 goal 0");
	strcpy(g_stored.team_goal_text[9][6][3][2], "Team 9 goal 6");
	strcpy(g_stored.team_goal_text[4][3][1][1], "Middle");
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(13), 1);
	XVT_ASSERT_INT_EQ(fg_text_handle_count(), 3);
	XVT_ASSERT_INT_EQ(team_text_handle_count(), 3);
	static const struct {
		uint16_t *handle;
		const char *text;
	} texts[] = {
		{&g_mission_fg_override_string_handles[0][0][0], "First"},
		{&g_mission_fg_override_string_handles[0][7][2],
		 "Last of group 0"},
		{&g_mission_fg_override_string_handles[1][2][1],
		 "Destroy the freighter"},
		{&g_global_goal_override_string_handles[0][0][0][0],
		 "Team 0 goal 0"},
		{&g_global_goal_override_string_handles[9][6][3][2],
		 "Team 9 goal 6"},
		{&g_global_goal_override_string_handles[4][3][1][1], "Middle"},
	};
	for (size_t i = 0; i < sizeof texts / sizeof texts[0]; ++i) {
		uint16_t handle = *texts[i].handle;
		XVT_ASSERT_TRUE(handle != 0);
		XVT_ASSERT_TRUE(strcmp(memory_get_handle_block(handle),
				       texts[i].text) == 0);
		XVT_ASSERT_INT_EQ(g_handle_tables.size_table[handle - 1],
				  strlen(texts[i].text) + 1);
	}
	mission_free_override_string_handles();
}

/* mission_free_override_string_handles frees the blocks of the flight group
 * goal texts of the mission's groups and of every global goal text, and leaves
 * the handle numbers in the tables; a handle of a group past the mission's
 * count is not freed. */
static void check_free_override_string_handles(void)
{
	stored_reset();
	strcpy(g_stored.flight_group_text[1][5][0], "Group text");
	strcpy(g_stored.team_goal_text[3][2][2][1], "Team text");
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(12), 1);
	uint16_t group_handle = g_mission_fg_override_string_handles[1][5][0];
	uint16_t team_handle =
		g_global_goal_override_string_handles[3][2][2][1];
	uint16_t beyond = memory_alloc_handle_zeroed(8, 0);
	g_mission_fg_override_string_handles[2][0][0] = beyond;
	XVT_ASSERT_TRUE(group_handle != 0);
	XVT_ASSERT_TRUE(team_handle != 0);
	XVT_ASSERT_TRUE(beyond != 0);
	mission_free_override_string_handles();
	XVT_ASSERT_TRUE(g_handle_tables.ptr_table[group_handle - 1] == NULL);
	XVT_ASSERT_TRUE(g_handle_tables.ptr_table[team_handle - 1] == NULL);
	XVT_ASSERT_TRUE(g_handle_tables.ptr_table[beyond - 1] != NULL);
	XVT_ASSERT_INT_EQ(g_mission_fg_override_string_handles[1][5][0],
			  group_handle);
	XVT_ASSERT_INT_EQ(g_global_goal_override_string_handles[3][2][2][1],
			  team_handle);
	memory_free_handle(beyond);
	g_mission_fg_override_string_handles[2][0][0] = 0;
}

/* Known failure goal_text_unterminated, issue #135: a goal text that fills
 * all 64 bytes of its place in the file is kept as those 64 bytes. The loader
 * measures it with strlen in a buffer nothing terminates after byte 64. */
static void check_goal_text_fills_place(void)
{
	stored_reset();
	memset(g_stored.flight_group_text[0][0][0], 'A', 64);
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(14), 1);
	uint16_t handle = g_mission_fg_override_string_handles[0][0][0];
	XVT_ASSERT_TRUE(handle != 0);
	XVT_ASSERT_INT_EQ(strlen(memory_get_handle_block(handle)), 64);
}

/* A seek in the skipped briefing text that fails (back past the start of the
 * file) leaves an error on the stream: the loader has read everything, then
 * returns 0 because closing the stream reports it. */
static void check_load_close_error(void)
{
	stored_reset();
	g_stored.last_briefing_length = -32768;
	g_stored.header.backdrop = 9;
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(13), 0);
	XVT_ASSERT_INT_EQ(g_mission_header.backdrop, 9);
	XVT_ASSERT_TRUE(g_stream == NULL);
	XVT_ASSERT_TRUE(line_holds("resources.file_close_failed", "remove=0"));
}

/* Takes every free memory handle, one byte each; returns how many were taken,
 * so the table is full and the next allocation fails. */
static uint16_t g_taken_handles[32768];

static int take_all_handles(void)
{
	int count = 0;
	xvt_log_set_level(AERON_LOG_WARN);
	for (;;) {
		uint16_t handle = memory_alloc_handle_zeroed(1, 0);
		if (handle == 0) {
			break;
		}
		g_taken_handles[count++] = handle;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	g_log_line_count = 0;
	return count;
}

static void give_back_handles(int count)
{
	xvt_log_set_level(AERON_LOG_WARN);
	for (int i = 0; i < count; ++i) {
		memory_free_handle(g_taken_handles[i]);
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
}

/* A goal text that cannot get a memory handle is lost with a warning naming
 * its place and size, and its place in the table stays 0; the texts the file
 * holds are still read past, so the next ones land in their places. */
static void check_load_stored_goal_text_lost(void)
{
	stored_reset();
	strcpy(g_stored.flight_group_text[1][2][1], "Destroy the freighter");
	strcpy(g_stored.team_goal_text[9][6][3][2], "Team 9 goal 6");
	stored_world();
	int taken = take_all_handles();
	XVT_ASSERT_TRUE(taken > 0);
	XVT_ASSERT_INT_EQ(load_stored(12), 1);
	give_back_handles(taken);
	XVT_ASSERT_INT_EQ(fg_text_handle_count(), 0);
	XVT_ASSERT_INT_EQ(team_text_handle_count(), 0);
	XVT_ASSERT_INT_EQ(count_lines("mission.fg_goal_text_lost"), 1);
	XVT_ASSERT_TRUE(line_holds("mission.fg_goal_text_lost",
				   "fg=1 goal=2 text=1 bytes=22"));
	XVT_ASSERT_INT_EQ(count_lines("mission.team_goal_text_lost"), 1);
	XVT_ASSERT_TRUE(line_holds("mission.team_goal_text_lost",
				   "team=9 goal=6 trigger=3 text=2 bytes=14"));
}

/* The role characters a to z become upper case, and nothing else changes:
 * not the characters next to the letters in the character set, digits, or
 * the bytes after the first 0. A role of 15 characters is made upper case
 * to its end. */
static void check_load_role_upper_case(void)
{
	stored_reset();
	strcpy(g_stored.groups[0].craft_role, "`az{AZ09@[ yz");
	strcpy(g_stored.groups[1].craft_role, "abcdefghijklmno");
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(14), 1);
	XVT_ASSERT_TRUE(strcmp(g_mission_flight_groups[0].fg.craft_role,
			       "`AZ{AZ09@[ YZ") == 0);
	XVT_ASSERT_TRUE(strcmp(g_mission_flight_groups[1].fg.craft_role,
			       "ABCDEFGHIJKLMNO") == 0);
	stored_reset();
	memcpy(g_stored.groups[0].craft_role, "\0abc", 4);
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(14), 1);
	XVT_ASSERT_INT_EQ(
		memcmp(g_mission_flight_groups[0].fg.craft_role, "\0abc", 4),
		0);
}

/* The roles of groups past the mission's count are not made upper case. */
static void check_load_role_upper_case_count(void)
{
	stored_reset();
	g_stored.header.num_flight_groups = 1;
	strcpy(g_stored.groups[0].craft_role, "ab");
	stored_world();
	strcpy(g_mission_flight_groups[1].fg.craft_role, "cd");
	XVT_ASSERT_INT_EQ(load_stored(13), 1);
	XVT_ASSERT_TRUE(
		strcmp(g_mission_flight_groups[0].fg.craft_role, "AB") == 0);
	XVT_ASSERT_TRUE(
		strcmp(g_mission_flight_groups[1].fg.craft_role, "cd") == 0);
}

/* How a global goal's trigger slots look after the loader's last step: the
 * conditions of its four slots as the file gave them (1 where the slot is
 * "never") and what each slot must be afterwards. */
struct fill_case {
	uint8_t never[4];
	uint8_t filled_slots;
};

/* The last step fills only a goal whose first condition is not "never": its
 * three trailing "never" slots (pair 1 and the second of pair 0) when all
 * three are, its two of pair 1 when the second of pair 0 is set and pair 1 is
 * not, its last when only that one is never. Any other pattern, and any goal
 * whose first slot is "never", is left as it is. */
static void check_load_goal_trigger_fill(void)
{
	static const struct fill_case cases[] = {
		{{0, 0, 0, 0}, 0}, {{0, 0, 0, 1}, 1}, {{0, 0, 1, 0}, 0},
		{{0, 0, 1, 1}, 2}, {{0, 1, 0, 0}, 0}, {{0, 1, 0, 1}, 0},
		{{0, 1, 1, 0}, 0}, {{0, 1, 1, 1}, 3}, {{1, 0, 0, 0}, 0},
		{{1, 0, 0, 1}, 0}, {{1, 0, 1, 0}, 0}, {{1, 0, 1, 1}, 0},
		{{1, 1, 0, 0}, 0}, {{1, 1, 0, 1}, 0}, {{1, 1, 1, 0}, 0},
		{{1, 1, 1, 1}, 0},
	};
	enum { CASE_COUNT = sizeof cases / sizeof cases[0] };
	stored_reset();
	for (int i = 0; i < CASE_COUNT; ++i) {
		int team = i / 3;
		int goal = i % 3;
		g_stored.goal_count[team] = (int16_t)(goal + 1);
		struct global_goal *record = &g_stored.goals[team][goal];
		for (int slot = 0; slot < 4; ++slot) {
			struct mission_trigger_pair *pair =
				&record->trigger_pairs[slot / 2];
			pair->triggers[slot % 2].condition =
				cases[i].never[slot] ? MISSION_COND_NEVER
						     : MISSION_COND_DESTROYED;
			pair->trigger1_or_trigger2 = 1;
		}
		record->trigger_pair1_or_trigger_pair2 = 1;
	}
	/* A goal past the third of its team is not touched. */
	g_stored.goal_count[9] = 4;
	for (int slot = 1; slot < 4; ++slot) {
		g_stored.goals[9][3]
			.trigger_pairs[slot / 2]
			.triggers[slot % 2]
			.condition = MISSION_COND_NEVER;
	}
	g_stored.goals[9][3].trigger_pairs[0].triggers[0].condition =
		MISSION_COND_DESTROYED;
	stored_world();
	XVT_ASSERT_INT_EQ(load_stored(13), 1);
	for (int i = 0; i < CASE_COUNT; ++i) {
		const struct global_goal *goal =
			&g_mission_global_goals[i / 3][i % 3];
		const struct global_goal *file_goal =
			&g_stored.goals[i / 3][i % 3];
		int filled = cases[i].filled_slots;
		/* The slots in the order the loader fills them: 1 is the
		 * second of pair 0, 2 and 3 are pair 1. */
		for (int slot = 0; slot < 4; ++slot) {
			int becomes_true = (filled == 3 && slot >= 1) ||
					   (filled == 2 && slot >= 2) ||
					   (filled == 1 && slot == 3);
			uint8_t expected =
				becomes_true
					? MISSION_COND_ALWAYS_TRUE
					: file_goal->trigger_pairs[slot / 2]
						  .triggers[slot % 2]
						  .condition;
			XVT_ASSERT_INT_EQ(goal->trigger_pairs[slot / 2]
						  .triggers[slot % 2]
						  .condition,
					  expected);
		}
		/* A fill joins the pairs by AND; no fill touches the joins. */
		XVT_ASSERT_INT_EQ(goal->trigger_pairs[1].trigger1_or_trigger2,
				  filled == 0 ? 1 : 0);
		XVT_ASSERT_INT_EQ(goal->trigger_pairs[0].trigger1_or_trigger2,
				  filled == 3 ? 0 : 1);
		XVT_ASSERT_INT_EQ(goal->trigger_pair1_or_trigger_pair2,
				  filled == 0 || filled == 1 ? 1 : 0);
		static const char *const events[] = {"", "slots=1", "slots=2",
						     "slots=3"};
		if (filled != 0) {
			char start[64];
			snprintf(
				start, sizeof start,
				"mission.goal_triggers_filled team=%d goal=%d ",
				i / 3, i % 3);
			XVT_ASSERT_TRUE(line_holds(start, events[filled]));
		}
	}
	XVT_ASSERT_INT_EQ(count_lines("mission.goal_triggers_filled"), 3);
	XVT_ASSERT_INT_EQ(g_mission_global_goals[9][3]
				  .trigger_pairs[1]
				  .triggers[1]
				  .condition,
			  MISSION_COND_NEVER);
}

/* The TIE header's fields go where mission.h says: the backdrop byte to the
 * time limit's minutes, the rescue and waypoint flags and variables, the six
 * win and loss texts to team 0's end-of-mission texts, the loss delay, and
 * the first neutral name to all four IFF names. The briefing text between the
 * goals and the end of the file is skipped whatever its length. */
static void check_load_tie_header(void)
{
	build_tie_mission();
	g_tie_header.backdrop = 11;
	g_tie_header.rescue = 12;
	g_tie_header.all_way_shown = 13;
	for (int i = 0; i < 8; ++i) {
		g_tie_header.mis_var[i] = (uint8_t)(20 + i);
	}
	strcpy((char *)g_tie_header.win_msg1[0], "Win one");
	strcpy((char *)g_tie_header.win_msg1[1], "Win two");
	strcpy((char *)g_tie_header.win_msg2[0], "Bonus one");
	strcpy((char *)g_tie_header.win_msg2[1], "Bonus two");
	strcpy((char *)g_tie_header.loss_msg[0], "Lose one");
	strcpy((char *)g_tie_header.loss_msg[1], "Lose two");
	g_tie_header.loss_msg_delay = 44;
	strcpy(g_tie_header.neutral_name[0], "Rebels");
	strcpy(g_tie_header.neutral_name[1], "Other");
	g_tie_briefing_text_length = 7;
	XVT_ASSERT_INT_EQ(load_tie_mission(), 1);
	XVT_ASSERT_INT_EQ(g_mission_header.time_limit_min, 11);
	XVT_ASSERT_INT_EQ(g_mission_header.rescue, 12);
	XVT_ASSERT_INT_EQ(g_mission_header.all_waypoints_shown, 13);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_mission_header.variables[i], 20 + i);
	}
	static const char *const texts[6] = {"Win one",	  "Win two",
					     "Lose one",  "Lose two",
					     "Bonus one", "Bonus two"};
	for (int i = 0; i < 6; ++i) {
		XVT_ASSERT_TRUE(
			strcmp(g_mission_teams[0].end_of_mission_messages[i],
			       texts[i]) == 0);
	}
	XVT_ASSERT_INT_EQ(g_mission_teams[0].eom_raw_delay[1], 44);
	for (int iff = 0; iff < 4; ++iff) {
		XVT_ASSERT_TRUE(
			strcmp(g_mission_header.iff_names[iff], "Rebels") == 0);
	}
	XVT_ASSERT_TRUE(line_holds("mission.tie_header",
				   "groups=4 messages=0 goals=2"));
}

/* The team of a TIE group follows its IFF: 0 and 4 are team 1, 1 and 6 up are
 * team 0, and 2, 3 and 5 are team 1 when the neutral name at the IFF's place
 * (IFF minus 2) starts with '1'. */
static void check_load_tie_team_by_iff(void)
{
	static const char *const names[2][4] = {{"1a", "0b", "0c", "1d"},
						{"0a", "1b", "1c", "0d"}};
	static const uint8_t teams[2][8] = {{1, 0, 1, 0, 1, 1, 0, 0},
					    {1, 0, 0, 1, 1, 0, 0, 0}};
	for (int variant = 0; variant < 2; ++variant) {
		build_tie_mission();
		for (int name = 0; name < 4; ++name) {
			strcpy(g_tie_header.neutral_name[name],
			       names[variant][name]);
		}
		fresh_world();
		for (int group = 0; group < 8; ++group) {
			strcpy(g_tie_groups[group].name, "Group");
			g_tie_groups[group].species = TEST_XWING;
			g_tie_groups[group].count = 1;
			g_tie_groups[group].side = (uint8_t)group;
		}
		xvt_test_open_assets(&g_assets);
		write_tie_mission("iff.tie", UINT16_MAX, &g_tie_header,
				  g_tie_groups, 8, g_tie_goals, 0);
		XVT_ASSERT_INT_EQ(mission_load_file("iff.tie"), 1);
		xvt_test_close_assets(&g_assets);
		for (int group = 0; group < 8; ++group) {
			XVT_ASSERT_INT_EQ(g_mission_flight_groups[group].fg.iff,
					  group);
			XVT_ASSERT_INT_EQ(
				g_mission_flight_groups[group].fg.team,
				teams[variant][group]);
		}
	}
}

/* Every field of a TIE flight group lands in the field mission.h names. A
 * group with no player flag has player number 0; the arrival and departure
 * fields the TIE format lacks are 0, whatever the group held before. */
static void check_load_tie_group_fields(void)
{
	build_tie_mission();
	struct efg_struct *tie = &g_tie_groups[1];
	strcpy(tie->name, "Alpha");
	strcpy(tie->cmdr, "cmd");
	strcpy(tie->contents[0], "Fuel");
	strcpy(tie->contents[1], "Secret");
	tie->special_craft = 3;
	tie->special_flag = 4;
	tie->species = TEST_XWING;
	tie->count = 5;
	tie->status = 6;
	tie->warhead = 7;
	tie->beam = 8;
	tie->side = 2;
	tie->skill = 9;
	tie->camoflage = 10;
	tie->camo_flag = 11;
	tie->camo_unused = 12;
	tie->formation = 13;
	tie->form_spacing = 14;
	tie->set = 15;
	tie->set_unused = 16;
	tie->waves = 17;
	tie->wave_delay = 18;
	tie->player_flag = 0;
	tie->heading = 19;
	tie->pitch = 20;
	tie->rotation = 21;
	tie->link_flag = 22;
	tie->link_code = 23;
	tie->link_unused = 24;
	tie->difficulty = 25;
	tie->start_cond[0] = (struct e_cond_struct){26, 27, 28, 29};
	tie->start_cond[1] = (struct e_cond_struct){30, 31, 32, 33};
	tie->start_op = 1;
	tie->start_delay_min = 34;
	tie->start_delay_sec = 35;
	tie->stop_cond = (struct e_cond_struct){36, 37, 38, 39};
	tie->stop_min = 40;
	tie->stop_sec = 41;
	tie->stop_abort = 42;
	tie->cur_start_fg = 0x2B2A;
	tie->start_fg = 43;
	tie->start_fg_used = 44;
	tie->pri_stop_fg = 45;
	tie->pri_stop_fg_used = 46;
	tie->secondary_stop_fg = 47;
	tie->secondary_stop_fg_used = 48;
	tie->capture_fg = 49;
	tie->capture_fg_used = 50;
	tie->way_shown = 51;
	tie->way_unused = 52;
	tie->way_brief_link = 53;
	tie->way_brief_shown = 54;
	for (int i = 0; i < 15; ++i) {
		tie->way_x[i] = (int16_t)(100 + i);
		tie->way_y[i] = (int16_t)(-200 - i);
		tie->way_z[i] = (int16_t)(300 + i);
		tie->way_used[i] = (int16_t)(i % 2);
	}
	fresh_world();
	struct xvt_flight_group *fg = &g_mission_flight_groups[1].fg;
	fg->stop_arriving_when = 2;
	fg->arrive_only_if_human = 1;
	fg->arrival_rand_delay_minutes = 3;
	fg->arrival_rand_delay_seconds = 4;
	xvt_test_open_assets(&g_assets);
	write_tie_mission("fields.tie", UINT16_MAX, &g_tie_header, g_tie_groups,
			  4, g_tie_goals, 0);
	XVT_ASSERT_INT_EQ(mission_load_file("fields.tie"), 1);
	xvt_test_close_assets(&g_assets);
	XVT_ASSERT_TRUE(strcmp(fg->name, "Alpha") == 0);
	XVT_ASSERT_TRUE(strcmp(fg->craft_role, "CMD") == 0);
	XVT_ASSERT_TRUE(strcmp(fg->cargo, "Fuel") == 0);
	XVT_ASSERT_TRUE(strcmp(fg->special_cargo, "Secret") == 0);
	XVT_ASSERT_INT_EQ(fg->special_cargo_craft, 3);
	XVT_ASSERT_INT_EQ(fg->random_special_cargo_craft, 4);
	XVT_ASSERT_INT_EQ(fg->craft_type, TEST_XWING);
	XVT_ASSERT_INT_EQ(fg->number_of_craft, 5);
	XVT_ASSERT_INT_EQ(fg->status1, 6);
	XVT_ASSERT_INT_EQ(fg->warhead, 7);
	XVT_ASSERT_INT_EQ(fg->beam, 8);
	XVT_ASSERT_INT_EQ(fg->iff, 2);
	XVT_ASSERT_INT_EQ(fg->group_ai, 9);
	XVT_ASSERT_INT_EQ(fg->markings, 10);
	XVT_ASSERT_INT_EQ(fg->radio, 11);
	XVT_ASSERT_INT_EQ(fg->unused5c, 12);
	XVT_ASSERT_INT_EQ(fg->formation, 13);
	XVT_ASSERT_INT_EQ(fg->formation_spacing, 14);
	XVT_ASSERT_INT_EQ(fg->global_group, 15);
	XVT_ASSERT_INT_EQ(fg->unused60, 16);
	XVT_ASSERT_INT_EQ(fg->number_of_waves, 17);
	XVT_ASSERT_INT_EQ(fg->waves_delay, 18);
	XVT_ASSERT_INT_EQ(fg->stop_arriving_when, 0);
	XVT_ASSERT_INT_EQ(fg->player_number, 0);
	XVT_ASSERT_INT_EQ(fg->arrive_only_if_human, 0);
	XVT_ASSERT_INT_EQ(fg->yaw, 19);
	XVT_ASSERT_INT_EQ(fg->pitch, 20);
	XVT_ASSERT_INT_EQ(fg->roll, 21);
	XVT_ASSERT_INT_EQ(fg->legacy_perma_death_enabled, 22);
	XVT_ASSERT_INT_EQ(fg->legacy_perma_death_id, 23);
	XVT_ASSERT_INT_EQ(fg->reserved_perma_death, 24);
	XVT_ASSERT_INT_EQ(fg->arrival_difficulty, 25);
	for (int trigger = 0; trigger < 2; ++trigger) {
		const struct mission_trigger *arrival =
			&fg->arrival_triggers[0].triggers[trigger];
		XVT_ASSERT_INT_EQ(arrival->condition, 26 + 4 * trigger);
		XVT_ASSERT_INT_EQ(arrival->variable_type, 27 + 4 * trigger);
		XVT_ASSERT_INT_EQ(arrival->variable, 28 + 4 * trigger);
		XVT_ASSERT_INT_EQ(arrival->amount, 29 + 4 * trigger);
	}
	XVT_ASSERT_INT_EQ(fg->arrival_triggers[0].trigger1_or_trigger2, 1);
	XVT_ASSERT_INT_EQ(fg->arrival_rand_delay_minutes, 0);
	XVT_ASSERT_INT_EQ(fg->arrival_rand_delay_seconds, 0);
	XVT_ASSERT_INT_EQ(fg->arrival_delay_minutes, 34);
	XVT_ASSERT_INT_EQ(fg->arrival_delay_seconds, 35);
	XVT_ASSERT_INT_EQ(fg->departure_trigger.triggers[0].condition, 36);
	XVT_ASSERT_INT_EQ(fg->departure_trigger.triggers[0].variable_type, 37);
	XVT_ASSERT_INT_EQ(fg->departure_trigger.triggers[0].variable, 38);
	XVT_ASSERT_INT_EQ(fg->departure_trigger.triggers[0].amount, 39);
	XVT_ASSERT_INT_EQ(fg->departure_delay_minutes, 40);
	XVT_ASSERT_INT_EQ(fg->departure_delay_seconds, 41);
	XVT_ASSERT_INT_EQ(fg->abort_trigger, 42);
	/* The two bytes of cur_start_fg go over the second byte of
	 * editor_mothership and reserved_mothership. */
	XVT_ASSERT_INT_EQ((fg->editor_mothership >> 8) & 0xFF, 0x2A);
	XVT_ASSERT_INT_EQ(fg->reserved_mothership, 0x2B);
	XVT_ASSERT_INT_EQ(fg->arrival_mothership, 43);
	XVT_ASSERT_INT_EQ(fg->arrival_method, 44);
	XVT_ASSERT_INT_EQ(fg->departure_mothership, 45);
	XVT_ASSERT_INT_EQ(fg->departure_method, 46);
	XVT_ASSERT_INT_EQ(fg->alternate_mothership, 47);
	XVT_ASSERT_INT_EQ(fg->alternate_mothership_used, 48);
	XVT_ASSERT_INT_EQ(fg->captured_departure_mothership, 49);
	XVT_ASSERT_INT_EQ(fg->captured_depart_via_mothership, 50);
	XVT_ASSERT_INT_EQ(fg->reserved_options_prefix[0], 51);
	XVT_ASSERT_INT_EQ(fg->reserved_options_prefix[1], 52);
	XVT_ASSERT_INT_EQ(fg->reserved_options_prefix[2], 53);
	XVT_ASSERT_INT_EQ(fg->reserved_options[0], 54);
	for (int i = 0; i < 15; ++i) {
		XVT_ASSERT_INT_EQ(fg->mission_point_x[i], 100 + i);
		XVT_ASSERT_INT_EQ(fg->mission_point_y[i], -200 - i);
		XVT_ASSERT_INT_EQ(fg->mission_point_z[i], 300 + i);
		XVT_ASSERT_INT_EQ(fg->mission_point_enabled[i], i % 2);
	}
}

/* A TIE group's three orders: each field of the TIE order lands in the
 * field of the mission order mission.h names, and the order's targets with
 * their types are copied for all three. (The third order's code is
 * check_tie_third_order.) */
static void check_load_tie_orders(void)
{
	build_tie_mission();
	struct efg_struct *tie = &g_tie_groups[1];
	for (int order = 0; order < 3; ++order) {
		struct eai_struct *ai = &tie->ai[order];
		uint8_t base = (uint8_t)(10 * (order + 1));
		ai->order = base;
		ai->speed = base + 1;
		for (int i = 0; i < 4; ++i) {
			ai->var[i] = (uint8_t)(base + 2 + i);
		}
		ai->target_type[0] = base + 6;
		ai->target_type[1] = base + 7;
		ai->target_id[0] = base + 8;
		ai->target_id[1] = base + 9;
		ai->target_op = 1;
		ai->target_unused = base + 3;
		ai->pri_type = base + 4;
		ai->pri_id = base + 5;
		ai->secondary_type = base + 6;
		ai->secondary_id = base + 7;
		ai->pri_secondary_op = 1;
		ai->pri_secondary_unused = base + 8;
	}
	XVT_ASSERT_INT_EQ(load_tie_mission(), 1);
	for (int order = 0; order < 3; ++order) {
		const struct mission_order *mission_order =
			&g_mission_flight_groups[1].fg.orders[order];
		uint8_t base = (uint8_t)(10 * (order + 1));
		if (order != 2) {
			XVT_ASSERT_INT_EQ(mission_order->order, base);
		}
		XVT_ASSERT_INT_EQ(mission_order->throttle, base + 1);
		XVT_ASSERT_INT_EQ(mission_order->variable1, base + 2);
		XVT_ASSERT_INT_EQ(mission_order->variable2, base + 3);
		XVT_ASSERT_INT_EQ(mission_order->variable3, base + 4);
		XVT_ASSERT_INT_EQ(mission_order->variable4, base + 5);
		XVT_ASSERT_INT_EQ(mission_order->secondary_target_types[0],
				  base + 6);
		XVT_ASSERT_INT_EQ(mission_order->secondary_target_types[1],
				  base + 7);
		XVT_ASSERT_INT_EQ(mission_order->secondary_targets[0],
				  base + 8);
		XVT_ASSERT_INT_EQ(mission_order->secondary_targets[1],
				  base + 9);
		XVT_ASSERT_INT_EQ(mission_order->target3_or_target4, 1);
		XVT_ASSERT_INT_EQ(mission_order->unused0, base + 3);
		XVT_ASSERT_INT_EQ(mission_order->target1_type, base + 4);
		XVT_ASSERT_INT_EQ(mission_order->target1, base + 5);
		XVT_ASSERT_INT_EQ(mission_order->target2_type, base + 6);
		XVT_ASSERT_INT_EQ(mission_order->target2, base + 7);
		XVT_ASSERT_INT_EQ(mission_order->target1_or_target2, 1);
		XVT_ASSERT_INT_EQ(mission_order->unused1, base + 8);
	}
}

/* A TIE group's four goals carry their conditions' amounts, the bonus goal its
 * points, and all four are enabled for team 0 only. */
static void check_load_tie_group_goals(void)
{
	build_tie_mission();
	struct efg_struct *tie = &g_tie_groups[1];
	tie->pri_win_pct = 31;
	tie->secondary_win_pct = 32;
	tie->loss_pct = 33;
	tie->bonus_pct = 34;
	tie->bonus_points = -5;
	XVT_ASSERT_INT_EQ(load_tie_mission(), 1);
	const struct xvt_flight_group *fg = &g_mission_flight_groups[1].fg;
	static const uint8_t amounts[4] = {31, 32, 33, 34};
	for (int goal = 0; goal < 4; ++goal) {
		XVT_ASSERT_INT_EQ(fg->goals[goal].amount, amounts[goal]);
		XVT_ASSERT_INT_EQ(fg->goals[goal].enabled_teams[0], 1);
		for (int team = 1; team < 10; ++team) {
			XVT_ASSERT_INT_EQ(fg->goals[goal].enabled_teams[team],
					  0);
		}
	}
	XVT_ASSERT_INT_EQ(fg->goals[3].points, -5);
	XVT_ASSERT_INT_EQ(fg->goals[0].points, 0);
	XVT_ASSERT_INT_EQ(fg->goals[4].enabled_teams[0], 0);
}

/* A TIE message goes to the index of its place in the file with its text, its
 * first trigger pair, voice and delay, and is sent to team 0 only. */
static void check_load_tie_messages(void)
{
	build_tie_mission();
	g_tie_message_count = 2;
	for (int i = 0; i < 2; ++i) {
		struct tie_radio_message *message = &g_tie_messages[i];
		snprintf(message->message, sizeof message->message,
			 "Hello pilot %d", i);
		message->conditions[0] = (struct e_cond_struct){
			(uint8_t)(2 + i), (uint8_t)(3 + i), (uint8_t)(4 + i),
			(uint8_t)(5 + i)};
		message->conditions[1] = (struct e_cond_struct){
			(uint8_t)(12 + i), (uint8_t)(13 + i), (uint8_t)(14 + i),
			(uint8_t)(15 + i)};
		snprintf(message->voice, sizeof message->voice, "voice%d", i);
		message->delay5s = (uint8_t)(6 + i);
		message->condition1_or_condition2 = (uint8_t)i;
	}
	fresh_world();
	memset(g_mission_messages, 0, sizeof g_mission_messages);
	xvt_test_open_assets(&g_assets);
	write_tie_mission("messages.tie", UINT16_MAX, &g_tie_header,
			  g_tie_groups, 4, g_tie_goals, 2);
	XVT_ASSERT_INT_EQ(mission_load_file("messages.tie"), 1);
	xvt_test_close_assets(&g_assets);
	XVT_ASSERT_INT_EQ(g_mission_header.num_messages, 2);
	for (int i = 0; i < 2; ++i) {
		const struct mission_message *message = &g_mission_messages[i];
		char text[32];
		snprintf(text, sizeof text, "Hello pilot %d", i);
		XVT_ASSERT_TRUE(strcmp(message->message, text) == 0);
		for (int trigger = 0; trigger < 2; ++trigger) {
			const struct mission_trigger *loaded =
				&message->trigger_pairs[0].triggers[trigger];
			XVT_ASSERT_INT_EQ(loaded->condition,
					  2 + i + 10 * trigger);
			XVT_ASSERT_INT_EQ(loaded->variable_type,
					  3 + i + 10 * trigger);
			XVT_ASSERT_INT_EQ(loaded->variable,
					  4 + i + 10 * trigger);
			XVT_ASSERT_INT_EQ(loaded->amount, 5 + i + 10 * trigger);
		}
		XVT_ASSERT_INT_EQ(
			message->trigger_pairs[0].trigger1_or_trigger2, i);
		snprintf(text, sizeof text, "voice%d", i);
		XVT_ASSERT_TRUE(strcmp(message->voice, text) == 0);
		XVT_ASSERT_INT_EQ(message->delay5s, 6 + i);
		XVT_ASSERT_INT_EQ(message->sent_to_team[0], 1);
	}
}

/* A TIE goal becomes team 0's global goal of the same index: its first
 * trigger pair, its name from the first 16 bytes of the editor name and its
 * version from the 17th, its delay from the first padding byte, and no
 * points. The seven goals a team can hold are all read, none warned of. */
static void check_load_tie_goals(void)
{
	build_tie_mission();
	for (int goal = 0; goal < 7; ++goal) {
		struct e_mission_goal *tie = &g_tie_goals[goal];
		uint8_t base = (uint8_t)(10 * goal);
		tie->subcond[0] = (struct e_cond_struct){
			MISSION_COND_DESTROYED, (uint8_t)(base + 1),
			(uint8_t)(base + 2), (uint8_t)(base + 3)};
		tie->subcond[1] = (struct e_cond_struct){
			MISSION_COND_ARRIVED, (uint8_t)(base + 4),
			(uint8_t)(base + 5), (uint8_t)(base + 6)};
		memset(tie->editor_name, 'a' + goal, 16);
		tie->editor_name[16] = (uint8_t)(base + 7);
		tie->or_joined = (uint8_t)(goal % 2);
		tie->pad[0] = (uint8_t)(base + 8);
		tie->pad[1] = 99;
	}
	fresh_world();
	g_mission_global_goals[0][2].raw_points = 5;
	xvt_test_open_assets(&g_assets);
	write_tie_mission("goals.tie", UINT16_MAX, &g_tie_header, g_tie_groups,
			  4, g_tie_goals, 7);
	XVT_ASSERT_INT_EQ(mission_load_file("goals.tie"), 1);
	xvt_test_close_assets(&g_assets);
	XVT_ASSERT_INT_EQ(count_lines("mission.goal_count_invalid"), 0);
	for (int goal = 0; goal < 7; ++goal) {
		const struct global_goal *loaded =
			&g_mission_global_goals[0][goal];
		uint8_t base = (uint8_t)(10 * goal);
		const struct mission_trigger_pair *pair =
			&loaded->trigger_pairs[0];
		XVT_ASSERT_INT_EQ(pair->triggers[0].condition,
				  MISSION_COND_DESTROYED);
		XVT_ASSERT_INT_EQ(pair->triggers[0].variable_type, base + 1);
		XVT_ASSERT_INT_EQ(pair->triggers[0].variable, base + 2);
		XVT_ASSERT_INT_EQ(pair->triggers[0].amount, base + 3);
		XVT_ASSERT_INT_EQ(pair->triggers[1].condition,
				  MISSION_COND_ARRIVED);
		XVT_ASSERT_INT_EQ(pair->triggers[1].variable_type, base + 4);
		XVT_ASSERT_INT_EQ(pair->triggers[1].variable, base + 5);
		XVT_ASSERT_INT_EQ(pair->triggers[1].amount, base + 6);
		XVT_ASSERT_INT_EQ(pair->trigger1_or_trigger2, goal % 2);
		for (int letter = 0; letter < 16; ++letter) {
			XVT_ASSERT_INT_EQ(loaded->name[letter], 'a' + goal);
		}
		XVT_ASSERT_INT_EQ(loaded->version, base + 7);
		XVT_ASSERT_INT_EQ(loaded->raw_delay, base + 8);
		XVT_ASSERT_INT_EQ(loaded->raw_points, 0);
	}
}

int main(int argc, char **argv)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"bonus_complete_team", check_bonus_complete_for_team},
			{"bonus_failed_team", check_bonus_failed_for_team},
			{"player_hit_not_attacked",
			 check_player_hit_counts_as_attacked},
			{"uncaptured_special_cargo",
			 check_uncaptured_special_cargo_mark},
			{"dropoff_group_past_table",
			 check_dropoff_naming_missing_group},
			{"expired_team_goal_score",
			 check_expired_team_goal_pays_nothing},
			{"round_spent_without_mothership",
			 check_round_kept_without_mothership},
			{"missile_boat_second_launcher",
			 check_missile_boat_launchers_counted},
			{"delivery_resets_rounds", check_delivery_keeps_rounds},
			{"attacker_weight_zero",
			 check_kill_credit_weightless_attacker},
			{"tie_third_order_code", check_tie_third_order},
			{"tie_header_kept", check_tie_header_replaced},
			{"tie_goal_text_handles_kept",
			 check_tie_goal_text_handles_cleared},
			{"refused_version_stays_open",
			 check_refused_version_closes_file},
			{"goal_text_unterminated", check_goal_text_fills_place},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		return 0;
	}
	check_special_cargo_inspected();
	check_condition_fixed_results();
	check_condition_needs_variable();
	check_condition_destroyed_amounts();
	check_condition_at_least_one();
	check_condition_subset_never_fails();
	check_condition_goal_statuses();
	check_condition_reinforcements();
	check_trigger_pair_joins();
	check_trigger_pair_team_filter();
	check_group_matches_variable();
	check_outcome_counted_once();
	check_outcome_untouched_craft();
	check_outcome_touched_craft();
	check_outcome_departures();
	check_outcome_closes_arrivals();
	check_outcome_moves_dependents();
	check_outcome_clears_references();
	check_close_unavailable_group();
	check_flight_group_goal_score();
	check_flight_group_goal_reduction();
	check_team_goal_score();
	check_clock_seconds();
	check_craft_point_value();
	check_kill_score();
	check_kill_credit_full_share();
	check_kill_credit_rating_split();
	check_wave_capacity();
	check_wave_without_mothership();
	check_wave_from_mothership();
	check_wave_rounds_kept();
	check_resolve_world_loc();
	check_load_tie_teams();
	check_load_tie_group();
	check_load_common_tail();
	check_load_refuses_version();
	check_load_stored_versions();
	check_load_refuses_other_versions();
	check_load_stored_messages();
	check_load_stored_messages_over();
	check_load_stored_team_goals();
	check_load_stored_team_goals_over();
	check_load_stored_team_records();
	check_load_stored_goal_texts();
	check_free_override_string_handles();
	check_load_close_error();
	check_load_stored_goal_text_lost();
	check_load_role_upper_case();
	check_load_role_upper_case_count();
	check_load_goal_trigger_fill();
	check_load_tie_header();
	check_load_tie_team_by_iff();
	check_load_tie_group_fields();
	check_load_tie_orders();
	check_load_tie_group_goals();
	check_load_tie_messages();
	check_load_tie_goals();
	return 0;
}

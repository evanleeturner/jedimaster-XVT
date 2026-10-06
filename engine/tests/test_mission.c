/* Tests for xvt/flight/mission/mission.c, the mission's goals, triggers,
 * scoring and flight group arrivals. Each check builds the mission it needs in
 * the game's own tables: two flight groups, a few craft in a small object table
 * this file owns, and the goal and trigger records set field by field. The
 * loader checks read TIE-format mission files this file writes into a
 * temporary asset folder; no game data is read.
 *
 * POSIX only, for the temporary folder (test_asset_folder.h). */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
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

/* Writes a mission file in the TIE format, version 0xFFFF: the counts of
 * flight groups, messages (none here) and goals, the header, each flight
 * group, each goal, then the briefing the loader skips, 810 bytes and 64
 * empty strings. */
static void write_tie_mission(const char *name, uint16_t version,
			      const struct e_mission_struct *header,
			      const struct efg_struct *groups,
			      int16_t group_count,
			      const struct e_mission_goal *goals,
			      int16_t goal_count)
{
	static uint8_t file[8192];
	size_t size = 0;
	int16_t message_count = 0;
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
	memcpy(file + size, goals, goal_count * sizeof *goals);
	size += goal_count * sizeof *goals;
	size += 810 + 64 * sizeof(int16_t);
	XVT_ASSERT_TRUE(size <= sizeof file);
	xvt_test_write_file(g_assets.asset, name, file, size);
}

static struct e_mission_struct g_tie_header;
static struct efg_struct g_tie_groups[4];
static struct e_mission_goal g_tie_goals[2];

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

int main(int argc, char **argv)
{
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
	return 0;
}

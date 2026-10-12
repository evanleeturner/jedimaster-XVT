/* Tests for xvt/flight/mission/mission.c, the mission's start, goals, triggers,
 * scoring and flight group arrivals. Each check builds the mission it needs in
 * the game's own tables: two flight groups, a few craft in a small object table
 * this file owns, and the goal and trigger records set field by field. The
 * loader checks read mission files this file writes, in the stored format of
 * versions 12 to 14 and in the TIE format, into a temporary asset folder; the
 * checks of mission_init and the flight's runtime state write such a file for
 * each mission, run the start on it and read what it made: the object table
 * and its pools, the flight groups, the players, the clocks and the backdrop
 * records. The spawn checks make a mission with mission_init, set the model of
 * the craft they send to known values, send a group's craft with
 * mission_spawn_flight_group_wave_craft and read each craft's record; the
 * static object checks place mines, satellites and debris through
 * mission_spawn_flight_group_static_objects. The checks of
 * mission_update_logic build the goals and scripted messages by hand, run the
 * pass and read the team scores, goal states and statuses, the log and, for
 * the in-flight messages, the log line each message leaves (every message
 * template is a one-character text of the system pane, which is switched off,
 * so a message is built and dropped). The checks of
 * mission_evaluate_condition set the outcome counts of a group, the live craft
 * of the object table and the players, and read the result and the craft
 * counts it leaves; the trigger variable checks compare the groups and objects
 * they select. The arrival checks make a mission with mission_init and run
 * the flight's start on it, then run mission_update_flight_group_arrivals and
 * mission_process_flight_group_wave_completion and read the groups' counts,
 * the object table and the log; the formation place checks fill the model's
 * box by hand. The kill credit checks put a killer and a victim in the object
 * table with the damage the victim took by player, computer craft, collision,
 * starship and mine, and read the players' kill tables, ratings and scores,
 * the teams' scores and the log; the shot checks use a table of 140 objects.
 * No game data is read.
 *
 * Not checked here:
 * - mission_load_file: a file that does not open, or comes up short, ends the
 *   program on a message box that waits on a machine with a display. The
 *   version 10 branches never run, and a message index, a flight group count, a
 *   message count or a global goal count (of the TIE format) past its table
 *   would write past the table, which the sanitizers stop. A role that fills
 *   all 16 bytes makes the upper-casing loop read the byte after its field,
 *   which the array-bounds check of the sanitizers stops.
 * - mission_init: pools that cannot be allocated (the handle table is full)
 *   and backdrop records past the 64 of their tables, which write past tables
 *   or read a handle that does not exist; the proving grounds course, which
 *   nothing starts (the flag is cleared before the load that precedes it).
 * - mission_init_flight_runtime_state: loading the cockpit resources, which
 *   reads game files, and the store to the frame step mirror, a static of
 *   the file that nothing outside it reads.
 * - mission_init_flight_group_object_slot: the hit points of the damageable
 *   meshes of a craft (they need a loaded model file), and the clamp on a
 *   doubled shield, which needs a shield strength past a billion.
 * - mission_spawn_flight_group_static_objects: the debris field's check for an
 *   object already at the spot, which never repeats the draw.
 * - mission_update_logic: the commander voice a goal's end-of-mission text
 *   asks for (the voice queue is a static of the sound module), and the text
 *   of a message, of which only the length shows in its log line. The result
 *   of a global goal whose second pair has no second condition depends on an
 *   unset local (a known failure); the checks leave that pair's craft counts.
 * - mission_evaluate_condition: the connected player counts of a player whose
 *   team is past the 10 teams, and a variable past the tables it indexes.
 * - mission_object_matches_trigger_variable: the opposites 11 and 14 of
 *   "attacked by another team" and "identified by another team" of type 7,
 *   which match unless every team did it.
 * - mission_update_flight_group_arrivals: the second operand of "no player
 *   owner or the waves are not unlimited" before the stop rules, which no
 *   group reaches (the loop skips a group a player owns first).
 * - mission_credit_player_kill_contribution: the voice a full kill asks for
 *   (the voice settings are off here) and a tier other than 1 to 3, which the
 *   callers never pass.
 * - mission_credit_destruction_damage_contributors: the branch for a victim
 *   with no craft record, which reads the craft it lacks and stops on a null
 *   pointer.
 * - mission_apply_team_goal_score_for_team: a goal past its time limit, which
 *   adds an unset score (a known failure).
 *
 * POSIX only, for the temporary folder (test_asset_folder.h). */
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net_session.h"
#include "xvt/render/backdrop.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt/util/game_rand.h"
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

/* A global goal of seed-dependent bytes whose four conditions are all
 * "destroyed", so the loader's last step leaves it alone. */
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

/* ---- mission_init ----------------------------------------------------- */

/* The flight's fixed buffers get a block each once, so locking them reads real
 * blocks; the five pools mission_init makes start without handles. */
static void init_world(void)
{
	static uint16_t *const fixed_buffers[8] = {
		&g_string_data_handle,
		&g_render_object_list_handle,
		&g_flight_small_font_handle,
		&g_flight_micro_font_handle,
		&g_flight_medium_font_handle,
		&g_flight_scratch_screen_buffer_handle,
		&g_flight_aux_buffer_handle,
		&g_flight_offscreen_buffer_handle,
	};
	fresh_world();
	for (size_t i = 0; i < sizeof fixed_buffers / sizeof fixed_buffers[0];
	     ++i) {
		if (*fixed_buffers[i] == 0) {
			*fixed_buffers[i] = memory_alloc_handle_zeroed(16, 0);
		}
	}
	g_object_table_handle = 0;
	g_mobile_object_pool_handle = 0;
	g_mobile_object_char_data_handle = 0;
	g_craft_data_pool_handle = 0;
	g_warhead_guidance_pool_handle = 0;
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(&g_game_config, 0, sizeof g_game_config);
	memset(&g_net_session, 0, sizeof g_net_session);
	memset(g_mission_messages, 0, sizeof g_mission_messages);
	memset(&g_mission_countdown_clock, 0, sizeof g_mission_countdown_clock);
	g_flight_mission_state.difficulty = GAME_DIFFICULTY_MEDIUM;
	g_flight_conf_no_pilot = 0;
	g_active_flight_player_count = 1;
	g_flight_player_count = 1;
	g_flight_bytes_per_pixel = 2;
	g_flight_font_tier = 0;
	g_flight_render_to_frontend = 0;
	g_flight_page_flip = 0;
	g_game_rand_feedback_state = 0x1234;
	g_log_line_count = 0;
	/* Every plan is two bytes: no target token and no first maneuver. */
	static uint8_t plan_bytes[8] = {UINT8_MAX, UINT8_MAX};
	for (int plan = 0; plan < 256; ++plan) {
		g_plan_data_ptrs[plan] = plan_bytes;
	}
	/* mission_init unlocks the surface first and locks it last. */
	while (flight_surface_get_lock_count() > 0) {
		flight_surface_unlock();
	}
	flight_surface_lock();
}

/* A stored mission of combat type with no groups yet; init_group adds them. */
static void init_mission(int kind)
{
	stored_reset();
	g_stored.header.num_flight_groups = 0;
	g_stored.header.mission_type = (int8_t)kind;
}

/* Group fg of the stored mission: two craft of the given type on the given
 * team and IFF, no special cargo craft, and a player number (0 for none). */
static void init_group(int fg, int team, int craft_type, int player_number)
{
	struct xvt_flight_group *group = &g_stored.groups[fg];
	memset(group, 0, sizeof *group);
	snprintf(group->name, sizeof group->name, "Group %d", fg);
	group->craft_type = (craft_species)craft_type;
	group->number_of_craft = 2;
	group->team = (uint8_t)team;
	group->iff = (uint8_t)team;
	group->player_number = (uint8_t)player_number;
	group->special_cargo_craft = TEST_NO_ORDINAL;
	group->group_ai = 2;
	if (g_stored.header.num_flight_groups <= fg) {
		g_stored.header.num_flight_groups = (int16_t)(fg + 1);
	}
}

/* Writes the stored mission as a file of the given version and runs
 * mission_init on it; returns what mission_init returns. */
static uint16_t run_init(uint16_t version)
{
	xvt_test_open_assets(&g_assets);
	write_stored_mission("init.mis", version);
	uint16_t result = mission_init("init.mis");
	xvt_test_close_assets(&g_assets);
	return result;
}

/* The first object type whose record_flags is nonzero (or zero) and that is
 * not the given one. */
static int type_with_record_flags(int nonzero, int not_this)
{
	for (int type = 1; type < 201; ++type) {
		if (type != not_this &&
		    (g_object_type_table[type].record_flags != 0) ==
			    (nonzero != 0)) {
			return type;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* mission_init lays the object table out as its comment says: 32 craft slots,
 * 128 player and 32 other projectile slots, 16 debris, 16 explosions, 256
 * character data and 8 local debris slots, then 64 static slots. It makes the
 * five pools of that size and clears them, the players' object links, the
 * message texts and the proving grounds state, and clears every model's
 * "needed" flag but marks the models the mission uses. */
static void check_init_layout(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	init_group(0, 0, TEST_XWING, 1);
	init_world();
	int stale_type = type_with_record_flags(1, TEST_XWING);
	int unlisted_type = type_with_record_flags(0, TEST_XWING);
	g_object_type_table[stale_type].asset_flags |= 0x10;
	g_object_type_table[unlisted_type].asset_flags |= 0x10;
	g_object_type_table[TEST_XWING].asset_flags &= (uint8_t)~0x10;
	strcpy(g_mission_messages[10].message, "Old");
	g_players[3].object_index = 5;
	g_players[3].bound_object_signature = 77;
	g_flight_mission_state.global_unit_craft_count[2] = 4;
	g_flight_mission_state.runtime.team_has_countable_craft[1] = 1;
	g_flight_mission_state.proving_grounds_craft_type = 2;
	g_flight_mission_state.proving_grounds_level = 4;
	g_flight_mission_state.proving_grounds_score = 99;
	g_flight_mission_state.proving_grounds_checkpoints_passed = 3;
	g_flight_mission_state.proving_grounds_checkpoints_remaining = 4;
	g_flight_mission_state.proving_grounds_targets_destroyed = 5;
	g_flight_mission_state.proving_grounds_time_bonus = 6;
	g_sim_steps_per_second = 15;
	XVT_ASSERT_INT_EQ(run_init(14), 1);

	XVT_ASSERT_INT_EQ(g_projectile_object_slot_start, 32);
	XVT_ASSERT_INT_EQ(g_projectile_object_slot_end, 192);
	XVT_ASSERT_INT_EQ(g_debris_object_slot_start, 192);
	XVT_ASSERT_INT_EQ(g_debris_object_slot_end, 208);
	XVT_ASSERT_INT_EQ(g_explosion_object_slot_start, 208);
	XVT_ASSERT_INT_EQ(g_explosion_object_slot_end, 224);
	XVT_ASSERT_INT_EQ(g_mobile_object_char_data_slot_start, 224);
	XVT_ASSERT_INT_EQ(g_mobile_object_char_data_slot_end, 480);
	XVT_ASSERT_INT_EQ(g_local_transient_slot_start, 480);
	XVT_ASSERT_INT_EQ(g_local_debris_slot_end, 488);
	XVT_ASSERT_INT_EQ(g_region_main_object_slot_end, 488);
	XVT_ASSERT_INT_EQ(g_region_static_object_slot_count, 64);
	XVT_ASSERT_INT_EQ(g_active_region_object_slot_start, 0);
	XVT_ASSERT_INT_EQ(g_active_region_craft_object_slot_end, 32);
	XVT_ASSERT_INT_EQ(g_craft_data_pool_capacity, 32);
	XVT_ASSERT_INT_EQ(g_mobile_object_char_data_count, 256);
	XVT_ASSERT_INT_EQ(g_projectile_object_slots_total, 160);
	XVT_ASSERT_INT_EQ(g_debris_object_slots_total, 16);
	XVT_ASSERT_INT_EQ(g_world_state_debris_slot_count, 16);
	XVT_ASSERT_INT_EQ(g_local_debris_slot_count, 8);
	for (int genus = 0; genus < 6; ++genus) {
		XVT_ASSERT_INT_EQ(g_object_slot_range_by_genus[genus].start, 0);
		XVT_ASSERT_INT_EQ(g_object_slot_range_by_genus[genus].end, 32);
	}
	static const struct {
		int genus;
		int start;
		int end;
	} ranges[] = {
		{CRAFT_GENUS_PLAYER_PROJECTILE, 32, 160},
		{CRAFT_GENUS_OTHER_PROJECTILE, 160, 192},
		{8, 0, 0},
		{9, 0, 0},
		{10, 0, 0},
		{11, 192, 208},
		{12, 0, 0},
		{CRAFT_GENUS_EXPLOSION, 208, 224},
		{14, 0, 0},
		{15, 0, 0},
		{16, 224, 480},
	};
	for (size_t i = 0; i < sizeof ranges / sizeof ranges[0]; ++i) {
		XVT_ASSERT_INT_EQ(
			g_object_slot_range_by_genus[ranges[i].genus].start,
			ranges[i].start);
		XVT_ASSERT_INT_EQ(
			g_object_slot_range_by_genus[ranges[i].genus].end,
			ranges[i].end);
	}
	XVT_ASSERT_INT_EQ(g_handle_tables.size_table[g_object_table_handle - 1],
			  552 * sizeof(struct object_record));
	XVT_ASSERT_INT_EQ(
		g_handle_tables.size_table[g_mobile_object_pool_handle - 1],
		488 * sizeof(struct mobile_object));
	XVT_ASSERT_INT_EQ(
		g_handle_tables
			.size_table[g_mobile_object_char_data_handle - 1],
		256 * sizeof(struct mobile_object_char_data));
	XVT_ASSERT_INT_EQ(
		g_handle_tables.size_table[g_craft_data_pool_handle - 1],
		32 * sizeof(struct craft_data));
	XVT_ASSERT_INT_EQ(
		g_handle_tables.size_table[g_warhead_guidance_pool_handle - 1],
		161 * sizeof(struct warhead_guidance_state));
	XVT_ASSERT_TRUE(g_object_table ==
			memory_get_handle_block(g_object_table_handle));
	for (int slot = 0; slot < 552; ++slot) {
		XVT_ASSERT_INT_EQ(g_object_table[slot].object_type, 0);
		XVT_ASSERT_INT_EQ(g_object_table[slot].player_owner_idx, -1);
		if (slot < 488) {
			XVT_ASSERT_TRUE(g_object_table[slot].mobj ==
					&g_mobile_object_pool_base[slot]);
			XVT_ASSERT_TRUE(
				g_mobile_object_pool_base[slot].p_craft ==
				NULL);
			XVT_ASSERT_INT_EQ(g_mobile_object_link_indices[slot]
						  .craft_data_idx,
					  -1);
			XVT_ASSERT_INT_EQ(g_mobile_object_link_indices[slot]
						  .char_data_idx,
					  -1);
			XVT_ASSERT_INT_EQ(g_mobile_object_link_indices[slot]
						  .warhead_guidance_idx,
					  -1);
			XVT_ASSERT_INT_EQ(
				g_spawn_object_type_by_object_slot[slot], -1);
		} else {
			XVT_ASSERT_TRUE(g_object_table[slot].mobj == NULL);
		}
		if (slot < 32) {
			XVT_ASSERT_INT_EQ(g_object_table[slot].mobj->iff,
					  UINT8_MAX);
		}
	}
	for (int player = 0; player < 8; ++player) {
		XVT_ASSERT_INT_EQ(g_players[player].object_index, -1);
		XVT_ASSERT_INT_EQ(g_players[player].bound_object_signature, 0);
	}
	XVT_ASSERT_INT_EQ(g_object_type_table[stale_type].asset_flags & 0x10,
			  0);
	XVT_ASSERT_INT_EQ(g_object_type_table[unlisted_type].asset_flags & 0x10,
			  0x10);
	XVT_ASSERT_INT_EQ(g_object_type_table[TEST_XWING].asset_flags & 0x10,
			  0x10);
	XVT_ASSERT_TRUE(strcmp(g_mission_messages[10].message, "") == 0);
	for (size_t i = 0;
	     i <
	     sizeof g_flight_mission_state.global_unit_craft_count /
		     sizeof g_flight_mission_state.global_unit_craft_count[0];
	     ++i) {
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.global_unit_craft_count[i], 0);
	}
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_has_countable_craft[1], 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_mode_active,
			  0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_craft_type, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_level, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_score, 0);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_checkpoints_passed, 0);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_checkpoints_remaining,
		0);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_targets_destroyed, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_time_bonus, 0);
	XVT_ASSERT_INT_EQ(g_sim_steps_per_second, 0);
	XVT_ASSERT_INT_EQ(flight_surface_get_lock_count(), 1);
	XVT_ASSERT_TRUE(
		line_holds("mission.prepared", "type=4 groups=1 messages=0"));
	g_flight_mission_state.proving_grounds_craft_type = 0;
}

/* When the mission file cannot be loaded, mission_init returns 0 and logs why,
 * naming a version it does not know apart from a file it could not read to the
 * end; it has made the pools by then and leaves the flight surface unlocked,
 * and the mission tables are as the refused file left them. */
static void check_init_load_fails(void)
{
	static const struct {
		uint16_t version;
		int16_t seek_back;
		const char *reason;
	} cases[] = {
		{11, 0, "unknown_version"},
		{13, -32768, "read_error"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		init_mission(MISSION_TYPE_MELEE);
		init_group(0, 0, TEST_XWING, 1);
		g_stored.last_briefing_length = cases[i].seek_back;
		init_world();
		g_mission_header.mission_type = MISSION_TYPE_COMBAT;
		XVT_ASSERT_INT_EQ(run_init(cases[i].version), 0);
		XVT_ASSERT_INT_EQ(flight_surface_get_lock_count(), 0);
		XVT_ASSERT_TRUE(g_object_table_handle != 0);
		XVT_ASSERT_TRUE(g_warhead_guidance_pool_handle != 0);
		char reason[40];
		snprintf(reason, sizeof reason, "reason=\"%s\"",
			 cases[i].reason);
		XVT_ASSERT_TRUE(line_holds("mission.load_failed", reason));
		XVT_ASSERT_INT_EQ(count_lines("mission.prepared"), 0);
		if (g_stream != NULL) {
			fe_disk_io_close_global_stream(0);
		}
	}
	XVT_ASSERT_INT_EQ(g_mission_header.mission_type, MISSION_TYPE_MELEE);
}

/* A pilot record's network player with every choice unset (-1) and the given
 * session id, flight group and craft type (0 for the group's own). */
static void set_network_player(int entry, int direct_play_id, int fg,
			       int craft_id)
{
	struct pilot_network_player *player =
		&g_pilot_data.network_players[entry];
	memset(player, 0, sizeof *player);
	player->direct_play_id = direct_play_id;
	player->flight_group_id = fg;
	player->craft_id = craft_id;
	player->craft_option = -1;
	player->warhead_option = -1;
	player->beam_option = -1;
	player->countermeasure_option = -1;
}

/* Puts a player of the given session id in roster slot slot. */
static void set_roster_slot(int slot, int direct_play_id)
{
	g_net_session.players[slot].direct_play_id = direct_play_id;
	if (g_net_session.player_count <= slot) {
		g_net_session.player_count = slot + 1;
	}
}

/* With a pilot record, mission_init binds each network player to the flight
 * group the record names, at the player's roster slot, with the group's IFF
 * and team, and applies the craft, warhead, beam and countermeasure the
 * player chose. A craft type overrides a craft option; a starfighter gets no
 * beam; an option at or past the count of its list, and a choice of -1, is
 * ignored. A player the roster does not hold is warned of and binds nothing. */
static void check_init_binds_network_players(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	for (int fg = 0; fg < 5; ++fg) {
		init_group(fg, fg < 2 ? 0 : 1, TEST_XWING, fg + 1);
		g_stored.groups[fg].iff = (uint8_t)(fg + 3);
	}
	struct xvt_flight_group *groups = g_stored.groups;
	groups[0].optional_craft[2] = CRAFT_SPECIES_TIE_BOMBER;
	groups[0].number_of_optional_craft[2] = 4;
	groups[0].number_of_optional_craft_waves[2] = 5;
	groups[0].optional_craft[3] = CRAFT_SPECIES_Y_WING;
	groups[0].optional_warheads[1] = 3;
	groups[0].optional_beams[2] = 4;
	groups[0].optional_countermeasures[1] = 2;
	groups[1].optional_beams[1] = 3;
	for (int fg = 0; fg < 5; ++fg) {
		groups[fg].number_of_waves = 7;
		groups[fg].optional_warheads[7] = 9;
		groups[fg].optional_beams[5] = 9;
		groups[fg].optional_countermeasures[3] = 9;
		groups[fg].optional_craft[9] = CRAFT_SPECIES_SHUTTLE;
	}
	init_world();
	g_flight_mission_state.player_flight_group_wave_mode = 1;
	set_roster_slot(1, 100);
	set_roster_slot(4, 200);
	set_roster_slot(6, 300);
	set_roster_slot(7, 400);
	set_network_player(0, 100, 0, 0);
	g_pilot_data.network_players[0].craft_option = 2;
	g_pilot_data.network_players[0].warhead_option = 1;
	g_pilot_data.network_players[0].beam_option = 2;
	g_pilot_data.network_players[0].countermeasure_option = 1;
	set_network_player(1, 200, 1, CRAFT_SPECIES_TIE_FIGHTER);
	g_pilot_data.network_players[1].craft_option = 3;
	g_pilot_data.network_players[1].beam_option = 1;
	set_network_player(2, 999, 2, 0);
	set_network_player(3, 300, 3, CRAFT_SPECIES_Z_95_HEADHUNTER);
	g_pilot_data.network_players[3].beam_option = 1;
	set_network_player(4, 400, 4, 0);
	g_pilot_data.network_players[4].craft_option = 10;
	g_pilot_data.network_players[4].warhead_option = 8;
	g_pilot_data.network_players[4].beam_option = 6;
	g_pilot_data.network_players[4].countermeasure_option = 4;
	XVT_ASSERT_INT_EQ(run_init(14), 1);

	const struct mission_flight_group *group = g_mission_flight_groups;
	XVT_ASSERT_INT_EQ(group[0].player_owner_idx, 1);
	XVT_ASSERT_INT_EQ(group[1].player_owner_idx, 4);
	XVT_ASSERT_INT_EQ(group[2].player_owner_idx, -1);
	XVT_ASSERT_INT_EQ(group[3].player_owner_idx, 6);
	XVT_ASSERT_INT_EQ(group[4].player_owner_idx, 7);
	XVT_ASSERT_INT_EQ(g_players[1].iff, 3);
	XVT_ASSERT_INT_EQ(g_players[1].team, 0);
	XVT_ASSERT_INT_EQ(g_players[4].iff, 4);
	XVT_ASSERT_INT_EQ(g_players[4].team, 0);
	XVT_ASSERT_INT_EQ(g_players[6].iff, 6);
	XVT_ASSERT_INT_EQ(g_players[6].team, 1);
	/* The craft option brings its craft, count and rounds, and the other
	 * choices come from the group's lists. */
	XVT_ASSERT_INT_EQ(group[0].fg.craft_type, CRAFT_SPECIES_TIE_BOMBER);
	XVT_ASSERT_INT_EQ(group[0].fg.number_of_craft, 4);
	XVT_ASSERT_INT_EQ(group[0].fg.number_of_waves, 5);
	XVT_ASSERT_INT_EQ(group[0].fg.warhead, 3);
	XVT_ASSERT_INT_EQ(group[0].fg.beam, 4);
	XVT_ASSERT_INT_EQ(group[0].fg.countermeasures, 2);
	/* A craft type wins over a craft option, and a fighter has no beam. */
	XVT_ASSERT_INT_EQ(group[1].fg.craft_type, CRAFT_SPECIES_TIE_FIGHTER);
	XVT_ASSERT_INT_EQ(group[1].fg.number_of_craft, 2);
	XVT_ASSERT_INT_EQ(group[1].fg.beam, 0);
	XVT_ASSERT_INT_EQ(group[3].fg.craft_type,
			  CRAFT_SPECIES_Z_95_HEADHUNTER);
	XVT_ASSERT_INT_EQ(group[3].fg.beam, 0);
	/* A choice past its list changes nothing. */
	XVT_ASSERT_INT_EQ(group[4].fg.craft_type, TEST_XWING);
	XVT_ASSERT_INT_EQ(group[4].fg.warhead, 0);
	XVT_ASSERT_INT_EQ(group[4].fg.beam, 0);
	XVT_ASSERT_INT_EQ(group[4].fg.countermeasures, 0);
	XVT_ASSERT_TRUE(line_holds(
		"mission.player_bound slot=1 entry=0 player=100 fg=0 ",
		"team=0 iff=3 craft=7 count=4 warhead=3 beam=4 countermeasures=2"));
	XVT_ASSERT_TRUE(line_holds("battle.player_joined who=1 ",
				   "entry=0 player=100 fg=0 team=0"));
	XVT_ASSERT_TRUE(line_holds("mission.player_slot_missing entry=2 ",
				   "player=999"));
	XVT_ASSERT_INT_EQ(count_lines("mission.player_bound"), 4);
	XVT_ASSERT_TRUE(line_holds("mission.player_teams", "teams=2 ai=0"));
}

/* With the nopilot setting the players are bound by player number: the
 * groups numbered 1 up to the flight's player count belong to the player of
 * that number minus 1, who takes the group's IFF and team; the others have no
 * owner. */
static void check_init_binds_by_player_number(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	init_group(0, 0, TEST_XWING, 1);
	init_group(1, 1, TEST_XWING, 2);
	init_group(2, 1, TEST_XWING, 3);
	init_group(3, 0, TEST_XWING, 0);
	g_stored.groups[0].iff = 5;
	g_stored.groups[1].iff = 6;
	init_world();
	g_flight_conf_no_pilot = 1;
	g_active_flight_player_count = 2;
	g_mission_flight_groups[2].player_owner_idx = 5;
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].player_owner_idx, 0);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].player_owner_idx, 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].player_owner_idx, -1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].player_owner_idx, -1);
	XVT_ASSERT_INT_EQ(g_players[0].iff, 5);
	XVT_ASSERT_INT_EQ(g_players[0].team, 0);
	XVT_ASSERT_INT_EQ(g_players[1].iff, 6);
	XVT_ASSERT_INT_EQ(g_players[1].team, 1);
	XVT_ASSERT_TRUE(line_holds("mission.player_number_bound slot=1 ",
				   "fg=1 team=1 iff=6"));
	XVT_ASSERT_INT_EQ(count_lines("mission.player_number_bound"), 2);
}

/* In a melee flown by humans of one team the AI opponents are switched on; of
 * two teams, or in another mission type, they are left as they were. */
static void check_init_enables_ai_opponents(void)
{
	static const struct {
		int kind;
		int second_team_owned;
		int ai_expected;
	} cases[] = {
		{MISSION_TYPE_MELEE, 0, 1},
		{MISSION_TYPE_MELEE, 1, 0},
		{MISSION_TYPE_COMBAT, 0, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		init_mission(cases[i].kind);
		init_group(0, 0, TEST_XWING, 1);
		init_group(1, 1, TEST_XWING, 2);
		init_world();
		g_flight_mission_state.player_flight_group_wave_mode = 1;
		set_roster_slot(0, 100);
		set_network_player(0, 100, 0, 0);
		if (cases[i].second_team_owned) {
			set_roster_slot(1, 200);
			set_network_player(1, 200, 1, 0);
		}
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.ai_opponents_enabled,
				  cases[i].ai_expected);
	}
}

/* A seed for the game's random state whose first game_rand_range(modulus) is
 * wanted. */
static int16_t seed_for_draw(unsigned int modulus, unsigned int wanted)
{
	for (int seed = 1; seed < 65536; ++seed) {
		g_game_rand_feedback_state = (int16_t)seed;
		if (game_rand_range((uint16_t)modulus) == wanted) {
			return (int16_t)seed;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* Four player groups, numbered 1 to 4, each with its own count, rounds,
 * warhead, beam and countermeasure: group 0 of team 0 (an A-wing) that no
 * human flies, group 1 of team 0 (an X-wing) flown by the host, group 2 of team
 * 1 (a TIE fighter) flown by a second human, and group 3 of team 1 (an A-wing)
 * that no human flies. */
static void craft_selection_mission(int kind)
{
	static const int craft[4] = {CRAFT_SPECIES_A_WING, CRAFT_SPECIES_X_WING,
				     CRAFT_SPECIES_TIE_FIGHTER,
				     CRAFT_SPECIES_A_WING};
	static const int count[4] = {6, 3, 4, 5};
	init_mission(kind);
	for (int fg = 0; fg < 4; ++fg) {
		init_group(fg, fg < 2 ? 0 : 1, craft[fg], fg + 1);
		g_stored.groups[fg].number_of_craft = (uint8_t)count[fg];
		g_stored.groups[fg].number_of_waves = (uint8_t)(5 - fg);
		g_stored.groups[fg].warhead =
			(uint8_t)(6 - 2 * (fg % 2) + fg / 2);
		g_stored.groups[fg].beam = (uint8_t)(5 - fg);
		g_stored.groups[fg].countermeasures = (uint8_t)(3 - fg % 3);
	}
	init_world();
	g_flight_mission_state.player_flight_group_wave_mode = 1;
	set_roster_slot(0, 100);
	set_roster_slot(1, 200);
	set_network_player(0, 100, 1, 0);
	set_network_player(1, 200, 2, 0);
}

/* mission_init copies the host's craft to every player group when the craft
 * selection is the host's alone, or in a melee with one human in the last
 * mission (outside a sequence, or in a tournament of one human), or in a melee
 * flown with the selection off inside a sequence. In a melee with the
 * selection on it copies, inside each team, the craft of the team's human
 * group to its other player groups. Otherwise it leaves the craft alone. */
static void check_init_craft_selection(void)
{
	enum { SAME, HOST, TEAM };
	static const struct {
		int kind;
		int selection;
		unsigned int humans_last;
		int sequence_active;
		unsigned int tournament_humans;
		int expected;
	} cases[] = {
		{MISSION_TYPE_COMBAT, CRAFT_SELECTION_HOST_ONLY, 2, 0, 0, HOST},
		{MISSION_TYPE_COMBAT, CRAFT_SELECTION_ON, 2, 0, 0, SAME},
		{MISSION_TYPE_COMBAT, CRAFT_SELECTION_OFF, 1, 1, 1, SAME},
		{MISSION_TYPE_MELEE, CRAFT_SELECTION_ON, 1, 0, 0, HOST},
		{MISSION_TYPE_MELEE, CRAFT_SELECTION_ON, 1, 1, 1, HOST},
		{MISSION_TYPE_MELEE, CRAFT_SELECTION_ON, 1, 1, 2, TEAM},
		{MISSION_TYPE_MELEE, CRAFT_SELECTION_ON, 2, 0, 0, TEAM},
		{MISSION_TYPE_MELEE, CRAFT_SELECTION_OFF, 2, 1, 2, HOST},
		{MISSION_TYPE_MELEE, CRAFT_SELECTION_OFF, 2, 0, 2, SAME},
		{MISSION_TYPE_MELEE, CRAFT_SELECTION_HOST_ONLY, 2, 0, 0, HOST},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_selection_mission(cases[i].kind);
		g_game_config.craft_selection = cases[i].selection;
		g_pilot_data.num_human_players_last_mission =
			cases[i].humans_last;
		g_pilot_data.mission_sequence_active = cases[i].sequence_active;
		g_pilot_data.melee_tournament_sequence_state
			.human_player_count = cases[i].tournament_humans;
		/* The host chose another craft type, which the copy carries. */
		g_pilot_data.network_players[0].craft_id = CRAFT_SPECIES_Y_WING;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		const struct xvt_flight_group *group[4];
		for (int fg = 0; fg < 4; ++fg) {
			group[fg] = &g_mission_flight_groups[fg].fg;
		}
		XVT_ASSERT_INT_EQ(group[1]->craft_type, CRAFT_SPECIES_Y_WING);
		if (cases[i].expected == HOST) {
			for (int fg = 0; fg < 4; ++fg) {
				XVT_ASSERT_INT_EQ(group[fg]->craft_type,
						  CRAFT_SPECIES_Y_WING);
				XVT_ASSERT_INT_EQ(group[fg]->number_of_craft,
						  3);
				XVT_ASSERT_INT_EQ(group[fg]->number_of_waves,
						  4);
				XVT_ASSERT_INT_EQ(group[fg]->beam, 4);
				XVT_ASSERT_INT_EQ(group[fg]->countermeasures,
						  2);
			}
		} else if (cases[i].expected == TEAM) {
			XVT_ASSERT_INT_EQ(group[0]->craft_type,
					  CRAFT_SPECIES_Y_WING);
			XVT_ASSERT_INT_EQ(group[0]->number_of_craft, 3);
			XVT_ASSERT_INT_EQ(group[0]->number_of_waves, 4);
			XVT_ASSERT_INT_EQ(group[0]->beam, 4);
			XVT_ASSERT_INT_EQ(group[0]->countermeasures, 2);
			XVT_ASSERT_INT_EQ(group[2]->craft_type,
					  CRAFT_SPECIES_TIE_FIGHTER);
			XVT_ASSERT_INT_EQ(group[3]->craft_type,
					  CRAFT_SPECIES_TIE_FIGHTER);
			XVT_ASSERT_INT_EQ(group[3]->number_of_craft, 4);
			XVT_ASSERT_INT_EQ(group[3]->number_of_waves, 3);
			XVT_ASSERT_INT_EQ(group[3]->warhead, group[2]->warhead);
			XVT_ASSERT_INT_EQ(group[3]->beam, 3);
			XVT_ASSERT_INT_EQ(group[3]->countermeasures, 1);
		} else {
			XVT_ASSERT_INT_EQ(group[0]->craft_type,
					  CRAFT_SPECIES_A_WING);
			XVT_ASSERT_INT_EQ(group[0]->number_of_craft, 6);
			XVT_ASSERT_INT_EQ(group[2]->craft_type,
					  CRAFT_SPECIES_TIE_FIGHTER);
			XVT_ASSERT_INT_EQ(group[3]->craft_type,
					  CRAFT_SPECIES_A_WING);
			XVT_ASSERT_INT_EQ(group[3]->number_of_craft, 5);
		}
	}
}

/* The host's copy keeps the warhead of a group that flew the host's own craft
 * and warhead before the host chose: such a group takes the host's warhead,
 * another keeps its own. The host here is the second entry of the pilot
 * record, the first being empty. */
static void check_init_host_warhead(void)
{
	craft_selection_mission(MISSION_TYPE_COMBAT);
	g_stored.groups[0].craft_type = CRAFT_SPECIES_X_WING;
	g_stored.groups[0].warhead = 2;
	g_stored.groups[1].warhead = 2;
	g_stored.groups[2].warhead = 3;
	g_stored.groups[3].craft_type = CRAFT_SPECIES_X_WING;
	g_stored.groups[3].warhead = 4;
	g_stored.groups[1].optional_warheads[0] = 7;
	init_world();
	g_game_config.craft_selection = CRAFT_SELECTION_HOST_ONLY;
	set_roster_slot(0, 100);
	set_network_player(1, 100, 1, 0);
	g_pilot_data.network_players[1].warhead_option = 0;
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.warhead, 7);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.number_of_craft, 3);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.warhead, 7);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.warhead, 3);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].fg.warhead, 4);
}

/* A melee of three teams with the craft selection on and the AI opponents on:
 * team 0 (group 0, an X-wing) and team 1 (group 1, a TIE fighter) belong to
 * humans, team 2 (groups 2 and 3, A-wings) to the computer. */
static void ai_team_mission(void)
{
	static const int craft[4] = {
		CRAFT_SPECIES_X_WING, CRAFT_SPECIES_TIE_FIGHTER,
		CRAFT_SPECIES_A_WING, CRAFT_SPECIES_A_WING};
	init_mission(MISSION_TYPE_MELEE);
	for (int fg = 0; fg < 4; ++fg) {
		init_group(fg, fg < 2 ? fg : 2, craft[fg], fg + 1);
		g_stored.groups[fg].number_of_craft = (uint8_t)(3 + fg);
		g_stored.groups[fg].number_of_waves = (uint8_t)(1 + fg);
		g_stored.groups[fg].warhead = (uint8_t)(2 + fg);
		g_stored.groups[fg].beam = (uint8_t)(1 + fg);
		g_stored.groups[fg].countermeasures = (uint8_t)(1 + fg);
	}
	init_world();
	g_flight_mission_state.player_flight_group_wave_mode = 1;
	g_flight_mission_state.ai_opponents_enabled = 1;
	g_game_config.craft_selection = CRAFT_SELECTION_ON;
	g_pilot_data.num_human_players_last_mission = 2;
	set_roster_slot(0, 100);
	set_roster_slot(1, 200);
	set_network_player(0, 100, 0, 0);
	set_network_player(1, 200, 1, 0);
	for (int team = 0; team < 10; ++team) {
		g_pilot_data.melee_tournament_sequence_state
			.team_standings[team]
			.ai_opponent_source_team_and_type_flag = 0;
	}
}

/* With the AI opponents on, each team with player groups and no human flies
 * the craft of a human team picked at random, with its count, rounds, warhead,
 * beam and countermeasure. In a sequence's first melee it also stores the
 * source team, with the top bit set for a Rebel fighter, unless the team's
 * stored value is -1. */
static void check_init_ai_team_craft(void)
{
	static const struct {
		unsigned int ordinal;
		int sequence_active;
		int initial_flag;
		int32_t expected_flag;
	} cases[] = {
		{0, 0, 0, 0},
		{0, 1, 0, INT32_MIN},
		{1, 1, 0, 1},
		{1, 1, -1, -1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		ai_team_mission();
		g_pilot_data.mission_sequence_active = cases[i].sequence_active;
		g_pilot_data.melee_tournament_sequence_state.team_standings[2]
			.ai_opponent_source_team_and_type_flag =
			cases[i].initial_flag;
		g_game_rand_feedback_state = seed_for_draw(2, cases[i].ordinal);
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		const struct xvt_flight_group *source =
			&g_mission_flight_groups[cases[i].ordinal].fg;
		for (int fg = 2; fg < 4; ++fg) {
			const struct xvt_flight_group *group =
				&g_mission_flight_groups[fg].fg;
			XVT_ASSERT_INT_EQ(group->craft_type,
					  source->craft_type);
			XVT_ASSERT_INT_EQ(group->number_of_craft,
					  source->number_of_craft);
			XVT_ASSERT_INT_EQ(group->number_of_waves,
					  source->number_of_waves);
			XVT_ASSERT_INT_EQ(group->warhead, source->warhead);
			XVT_ASSERT_INT_EQ(group->beam, source->beam);
			XVT_ASSERT_INT_EQ(group->countermeasures,
					  source->countermeasures);
		}
		XVT_ASSERT_INT_EQ(
			g_pilot_data.melee_tournament_sequence_state
				.team_standings[2]
				.ai_opponent_source_team_and_type_flag,
			cases[i].expected_flag);
		char fields[40];
		snprintf(fields, sizeof fields, "fg=%d groups=4",
			 (int)cases[i].ordinal);
		XVT_ASSERT_TRUE(
			line_holds("mission.ai_team_craft team=2 ", fields));
		XVT_ASSERT_INT_EQ(count_lines("mission.ai_team_craft team="),
				  1);
	}
}

/* In a later melee of a tournament the AI team takes the craft of the human
 * team it was given in the first, from that team's human group, with no
 * draw. */
static void check_init_ai_team_craft_tournament(void)
{
	ai_team_mission();
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.melee_tournament_sequence_state.current_mission_index = 1;
	g_pilot_data.melee_tournament_sequence_state.team_standings[2]
		.ai_opponent_source_team_and_type_flag = 1;
	g_game_rand_feedback_state = 0x4321;
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.craft_type,
			  CRAFT_SPECIES_TIE_FIGHTER);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].fg.craft_type,
			  CRAFT_SPECIES_TIE_FIGHTER);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].fg.number_of_craft, 4);
	XVT_ASSERT_INT_EQ(g_game_rand_feedback_state, 0x4321);
	XVT_ASSERT_TRUE(
		line_holds("mission.ai_team_craft team=2 ", "fg=1 groups=4"));
}

/* With no human in the melee there is no team to copy: the AI team is warned
 * of and keeps its craft. */
static void check_init_ai_team_without_source(void)
{
	ai_team_mission();
	g_pilot_data.network_players[0].direct_play_id = 0;
	g_pilot_data.network_players[1].direct_play_id = 0;
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_TRUE(line_holds("mission.ai_team_source_missing team=2 ",
				   "teams=0"));
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.craft_type,
			  CRAFT_SPECIES_A_WING);
}

/* A melee of two teams in a tournament's later mission: group 0 (team 0,
 * human) and group 1 (team 1, computer) fly the given craft, and the computer
 * team's stored source is team 2, which no human flies, of the given side. */
static void swap_mission(int craft0, int craft1, int source_flew_rebel)
{
	init_mission(MISSION_TYPE_MELEE);
	init_group(0, 0, craft0, 1);
	init_group(1, 1, craft1, 2);
	init_world();
	g_flight_mission_state.player_flight_group_wave_mode = 1;
	g_flight_mission_state.ai_opponents_enabled = 1;
	g_game_config.craft_selection = CRAFT_SELECTION_ON;
	g_pilot_data.num_human_players_last_mission = 2;
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.melee_tournament_sequence_state.current_mission_index = 1;
	set_roster_slot(0, 100);
	set_network_player(0, 100, 0, 0);
	g_pilot_data.melee_tournament_sequence_state.team_standings[1]
		.ai_opponent_source_team_and_type_flag =
		source_flew_rebel ? (int)(2u | 0x80000000u) : 2;
}

/* When the source team of an AI team has no human in a later melee, the AI
 * team's player groups fly the counterpart of their craft if their craft is of
 * the other side from the source's, and keep it if it is of the same side. */
static void check_init_ai_team_swap(void)
{
	/* The computer team's TIE fighter, against a source that flew a Rebel
	 * fighter, is swapped for the counterpart, the Z-95. */
	swap_mission(CRAFT_SPECIES_TIE_FIGHTER, CRAFT_SPECIES_TIE_FIGHTER, 1);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.craft_type,
			  CRAFT_SPECIES_Z_95_HEADHUNTER);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.craft_type,
			  CRAFT_SPECIES_TIE_FIGHTER);
	XVT_ASSERT_TRUE(line_holds("mission.ai_team_craft_replaced team=1 ",
				   "craft=14"));
	/* Against a source that flew an Imperial craft it keeps it. */
	swap_mission(CRAFT_SPECIES_TIE_FIGHTER, CRAFT_SPECIES_TIE_FIGHTER, 0);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.craft_type,
			  CRAFT_SPECIES_TIE_FIGHTER);
	XVT_ASSERT_INT_EQ(count_lines("mission.ai_team_craft_replaced"), 0);
}

/* Known failure tournament_swap_wrong_group, issue #131: the side of the
 * computer team's own player group decides the swap. In the first case the
 * human team before it flies an X-wing, of the Rebel source's side, and the
 * computer team a TIE fighter, of the other side: the TIE fighter must be
 * swapped for the Z-95. In the second the human team flies an X-wing, of the
 * other side from an Imperial source, and the computer team a TIE fighter, of
 * its side: the TIE fighter must stay. */
static void check_swap_tests_the_teams_own_group(void)
{
	swap_mission(CRAFT_SPECIES_X_WING, CRAFT_SPECIES_TIE_FIGHTER, 1);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	int swapped_when_needed = g_mission_flight_groups[1].fg.craft_type ==
				  CRAFT_SPECIES_Z_95_HEADHUNTER;
	swap_mission(CRAFT_SPECIES_X_WING, CRAFT_SPECIES_TIE_FIGHTER, 0);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	int kept_when_not = g_mission_flight_groups[1].fg.craft_type ==
			    CRAFT_SPECIES_TIE_FIGHTER;
	XVT_ASSERT_TRUE(swapped_when_needed && kept_when_not);
}

/* Four groups, flown by the player's team 0 (group 0, which the player flies,
 * and group 1), by team 2, allied with team 0 (group 2), and by team 1, at
 * war with it (group 3), each at the given AI level. The player is bound by
 * player number. */
static void difficulty_mission(int kind, int start_level)
{
	init_mission(kind);
	init_group(0, 0, TEST_XWING, 1);
	init_group(1, 0, TEST_XWING, 0);
	init_group(2, 2, TEST_XWING, 0);
	init_group(3, 1, TEST_XWING, 0);
	for (int fg = 0; fg < 4; ++fg) {
		g_stored.groups[fg].group_ai = (uint8_t)start_level;
	}
	g_stored.team_count[2] = 1;
	g_stored.teams[2].allies[0] = 1;
	init_world();
	g_flight_conf_no_pilot = 1;
	g_game_config.combat_balance = COMBAT_BALANCE_NEUTRAL;
}

/* Each group's AI level changes with the difficulty. On hard, a group at war
 * with the player's team gains 2 (at most 5) and any other loses 1 (not below
 * 0), except in a melee, and except in the first mission of a one-player
 * training sequence; on easy the player's team and its allies gain 2 (at most
 * 5) and the others lose 1; on medium nothing changes. A combat mission
 * flown by two humans in the last mission is left alone. */
static void check_init_difficulty(void)
{
	enum { TRAINING = 1 };
	static const struct {
		int difficulty;
		int kind;
		unsigned int humans_last;
		int training;
		int start;
		int own;
		int ally;
		int hostile;
	} cases[] = {
		{GAME_DIFFICULTY_MEDIUM, MISSION_TYPE_COMBAT, 1, 0, 3, 3, 3, 3},
		{GAME_DIFFICULTY_HARD, MISSION_TYPE_COMBAT, 1, 0, 3, 2, 2, 5},
		{GAME_DIFFICULTY_HARD, MISSION_TYPE_COMBAT, 1, 0, 0, 0, 0, 2},
		{GAME_DIFFICULTY_HARD, MISSION_TYPE_COMBAT, 1, 0, 4, 3, 3, 5},
		{GAME_DIFFICULTY_HARD, MISSION_TYPE_MELEE, 1, 0, 3, 3, 3, 5},
		{GAME_DIFFICULTY_HARD, MISSION_TYPE_COMBAT, 1, TRAINING, 3, 3,
		 3, 5},
		{GAME_DIFFICULTY_HARD, MISSION_TYPE_COMBAT, 2, 0, 3, 3, 3, 3},
		{GAME_DIFFICULTY_EASY, MISSION_TYPE_COMBAT, 1, 0, 3, 5, 5, 2},
		{GAME_DIFFICULTY_EASY, MISSION_TYPE_COMBAT, 1, 0, 0, 2, 2, 0},
		{GAME_DIFFICULTY_EASY, MISSION_TYPE_COMBAT, 1, 0, 4, 5, 5, 3},
		{GAME_DIFFICULTY_EASY, MISSION_TYPE_MELEE, 1, 0, 3, 5, 5, 2},
		{GAME_DIFFICULTY_EASY_CHEAT, MISSION_TYPE_COMBAT, 1, 0, 3, 5, 5,
		 2},
		{GAME_DIFFICULTY_EASY, MISSION_TYPE_COMBAT, 2, 0, 3, 3, 3, 3},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		difficulty_mission(cases[i].kind, cases[i].start);
		g_flight_mission_state.difficulty =
			(uint8_t)cases[i].difficulty;
		g_pilot_data.num_human_players_last_mission =
			cases[i].humans_last;
		if (cases[i].training) {
			g_pilot_data.mission_directory_id =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			g_pilot_data.mission_sequence_active = 1;
			g_flight_player_count = 1;
		} else {
			g_pilot_data.mission_directory_id =
				MISSION_DIRECTORY_TRAINING_EXERCISES + 1;
		}
		g_players[1].team = 5;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.group_ai,
				  cases[i].own);
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.group_ai,
				  cases[i].own);
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.group_ai,
				  cases[i].ally);
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].fg.group_ai,
				  cases[i].hostile);
	}
}

/* Known failure ai_level_past_table, issue #187: an AI level over 5 in the
 * file is not brought into 0 to 5, which the tables it indexes hold. On
 * medium difficulty mission_init leaves a group at level 9. */
static void check_init_ai_level_in_range(void)
{
	difficulty_mission(MISSION_TYPE_COMBAT, 9);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	for (int fg = 0; fg < 4; ++fg) {
		XVT_ASSERT_TRUE(g_mission_flight_groups[fg].fg.group_ai <= 5);
	}
}

/* Six player groups of a melee, none of them flown but group 5 of team 0, the
 * player's: group 0 of team 4, group 1 of team 1, groups 2 and 3 of team 2
 * (group 2 already at 5) and group 4 of team 3, all at level 2 but that. */
static void boost_mission(void)
{
	init_mission(MISSION_TYPE_MELEE);
	init_group(0, 4, TEST_XWING, 6);
	init_group(1, 1, TEST_XWING, 2);
	init_group(2, 2, TEST_XWING, 3);
	init_group(3, 2, TEST_XWING, 4);
	init_group(4, 3, TEST_XWING, 5);
	init_group(5, 0, TEST_XWING, 1);
	for (int fg = 0; fg < 6; ++fg) {
		g_stored.groups[fg].group_ai = 2;
	}
	g_stored.groups[2].group_ai = 5;
	init_world();
	g_flight_conf_no_pilot = 1;
}

/* In a melee the AI levels of player groups no human flies are raised by one,
 * one boost per team in turn from a random team that has such groups: three
 * boosts on easy, two on medium, one on hard. A boost goes to the team's first
 * group below level 5. In a sequence's first melee the first team is kept for
 * the later ones, which start from it. */
static void check_init_melee_boosts(void)
{
	static const struct {
		int difficulty;
		unsigned int ordinal;
		int sequence_active;
		int current_index;
		int first_team;
		int level[6];
		int stored_team;
	} cases[] = {
		{GAME_DIFFICULTY_MEDIUM, 1, 0, 0, 0, {2, 2, 5, 3, 3, 2}, 0},
		{GAME_DIFFICULTY_EASY, 0, 0, 0, 0, {1, 2, 5, 1, 2, 4}, 0},
		{GAME_DIFFICULTY_HARD, 3, 0, 0, 0, {5, 4, 5, 4, 4, 2}, 0},
		{GAME_DIFFICULTY_MEDIUM, 1, 1, 0, 0, {2, 2, 5, 3, 3, 2}, 2},
		{GAME_DIFFICULTY_MEDIUM, 0, 1, 1, 1, {2, 3, 5, 3, 2, 2}, 1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		boost_mission();
		g_flight_mission_state.difficulty =
			(uint8_t)cases[i].difficulty;
		g_pilot_data.mission_sequence_active = cases[i].sequence_active;
		g_pilot_data.melee_tournament_sequence_state
			.current_mission_index = cases[i].current_index;
		g_pilot_data.melee_tournament_sequence_state
			.ai_boost_first_team = cases[i].first_team;
		g_game_rand_feedback_state = seed_for_draw(4, cases[i].ordinal);
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		for (int fg = 0; fg < 6; ++fg) {
			XVT_ASSERT_INT_EQ(
				g_mission_flight_groups[fg].fg.group_ai,
				cases[i].level[fg]);
		}
		XVT_ASSERT_INT_EQ(g_pilot_data.melee_tournament_sequence_state
					  .ai_boost_first_team,
				  cases[i].stored_team);
	}
}

/* Known failure melee_boost_all_teams_owned, issue #130: when every team with
 * player groups has a human, the boosts still go to a team that has such
 * groups, as the first team is meant to be a team. Here two teams of two
 * player groups each have a human, and no unowned group is boosted. */
static void check_melee_boost_without_free_team(void)
{
	init_mission(MISSION_TYPE_MELEE);
	init_group(0, 0, TEST_XWING, 1);
	init_group(1, 1, TEST_XWING, 2);
	init_group(2, 0, TEST_XWING, 3);
	init_group(3, 1, TEST_XWING, 4);
	for (int fg = 0; fg < 4; ++fg) {
		g_stored.groups[fg].group_ai = 2;
	}
	init_world();
	g_flight_conf_no_pilot = 1;
	g_active_flight_player_count = 2;
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_TRUE(g_mission_flight_groups[2].fg.group_ai +
				g_mission_flight_groups[3].fg.group_ai >
			4);
}

/* In a combat mission flown by more than one human in the last mission, the
 * balance setting raises the AI levels of the groups at war with the
 * handicapped team: by 1 for two humans and 2 for more, at most 5. The team is
 * the first player's on autobalance when one team has humans, 1 when
 * favoring the Imperials and 0 when favoring the Rebels; neutral changes
 * nothing. */
static void check_init_combat_balance(void)
{
	static const struct {
		int balance;
		unsigned int humans;
		int second_team_owned;
		int start;
		int level[3];
	} cases[] = {
		{COMBAT_BALANCE_AUTOBALANCE, 2, 0, 1, {1, 2, 1}},
		{COMBAT_BALANCE_AUTOBALANCE, 3, 0, 1, {1, 3, 1}},
		{COMBAT_BALANCE_AUTOBALANCE, 2, 1, 1, {1, 1, 1}},
		{COMBAT_BALANCE_AUTOBALANCE, 3, 0, 4, {4, 5, 4}},
		{COMBAT_BALANCE_FAVOR_IMPERIAL, 2, 0, 1, {2, 1, 2}},
		{COMBAT_BALANCE_FAVOR_IMPERIAL, 3, 0, 1, {3, 1, 3}},
		{COMBAT_BALANCE_FAVOR_IMPERIAL, 3, 0, 4, {5, 4, 5}},
		{COMBAT_BALANCE_FAVOR_REBEL, 2, 0, 1, {1, 2, 1}},
		{COMBAT_BALANCE_FAVOR_REBEL, 3, 0, 4, {4, 5, 4}},
		{COMBAT_BALANCE_NEUTRAL, 2, 0, 1, {1, 1, 1}},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		init_mission(MISSION_TYPE_COMBAT);
		init_group(0, 0, TEST_XWING, 1);
		init_group(1, 1, TEST_XWING, 2);
		init_group(2, 2, TEST_XWING, 3);
		for (int fg = 0; fg < 3; ++fg) {
			g_stored.groups[fg].group_ai = (uint8_t)cases[i].start;
		}
		/* Team 2 is allied with team 0. */
		g_stored.team_count[2] = 1;
		g_stored.teams[2].allies[0] = 1;
		init_world();
		g_flight_mission_state.player_flight_group_wave_mode = 1;
		g_game_config.combat_balance = cases[i].balance;
		g_pilot_data.num_human_players_last_mission = cases[i].humans;
		set_roster_slot(0, 100);
		set_network_player(0, 100, 0, 0);
		if (cases[i].second_team_owned) {
			set_roster_slot(1, 200);
			set_network_player(1, 200, 1, 0);
		}
		g_players[1].team = 5;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		for (int fg = 0; fg < 3; ++fg) {
			XVT_ASSERT_INT_EQ(
				g_mission_flight_groups[fg].fg.group_ai,
				cases[i].level[fg]);
		}
		XVT_ASSERT_TRUE(
			line_holds("mission.combat_balance", "humans="));
	}
}

/* Fills the stack the next call will use with one 32-bit value. */
__attribute__((noinline)) static void fill_stack(uint32_t word)
{
	volatile uint32_t scratch[16384];
	for (size_t i = 0; i < sizeof scratch / sizeof scratch[0]; ++i) {
		scratch[i] = word;
	}
}

/* Writes the stored mission and runs mission_init on it with the stack under
 * it filled with word. */
static uint16_t run_init_on_stack(uint32_t word)
{
	xvt_test_open_assets(&g_assets);
	write_stored_mission("init.mis", 14);
	fill_stack(word);
	uint16_t result = mission_init("init.mis");
	xvt_test_close_assets(&g_assets);
	return result;
}

/* Known failure nopilot_unset_team_count, issue #132: with the nopilot setting
 * the combat balance on autobalance does not read a value nothing set. The
 * AI level of the group at war with the player's team comes out the same
 * whatever the stack held under mission_init: 1 and 0 here. */
static void check_balance_without_pilot_record(void)
{
	int level[2];
	for (uint32_t word = 0; word < 2; ++word) {
		init_mission(MISSION_TYPE_COMBAT);
		init_group(0, 0, TEST_XWING, 1);
		init_group(1, 1, TEST_XWING, 0);
		g_stored.groups[1].group_ai = 1;
		init_world();
		g_flight_conf_no_pilot = 1;
		g_game_config.combat_balance = COMBAT_BALANCE_AUTOBALANCE;
		g_pilot_data.num_human_players_last_mission = 2;
		XVT_ASSERT_INT_EQ(run_init_on_stack(word), 1);
		level[word] = g_mission_flight_groups[1].fg.group_ai;
	}
	XVT_ASSERT_INT_EQ(level[0], level[1]);
}

/* A seed for the game's random state whose successive game_rand_range draws,
 * of the given moduli, are the wanted values; sets the state to it. */
static void seed_for_draws(const unsigned int *moduli,
			   const unsigned int *wanted, int count)
{
	for (int seed = 1; seed < 65536; ++seed) {
		g_game_rand_feedback_state = (int16_t)seed;
		int all = 1;
		for (int i = 0; i < count && all; ++i) {
			all = game_rand_range((uint16_t)moduli[i]) == wanted[i];
		}
		if (all) {
			g_game_rand_feedback_state = (int16_t)seed;
			return;
		}
	}
	XVT_ASSERT_TRUE(0);
}

/* With random setup on, a group no human flies whose optional craft category
 * is 4 flies one of its optional craft, or its own, by a draw, with that
 * craft's count and rounds; a group a human flies keeps its craft. Every group
 * also starts from its own mission point or one of the three alternatives
 * its file enables, by a draw. With random setup off nothing is drawn. */
static void check_init_random_variation(void)
{
	static const uint16_t point_refs[3] = {0x8000, 0x8001, 0x8003};
	static const int craft[3] = {TEST_XWING, CRAFT_SPECIES_Y_WING,
				     CRAFT_SPECIES_B_WING};
	static const int count[3] = {2, 3, 4};
	static const int waves[3] = {0, 1, 2};
	for (int off = 0; off < 2; ++off) {
		for (unsigned int craft_pick = 0; craft_pick < 3;
		     ++craft_pick) {
			for (unsigned int point_pick = 0; point_pick < 3;
			     ++point_pick) {
				init_mission(MISSION_TYPE_COMBAT);
				init_group(0, 1, TEST_XWING, 0);
				init_group(1, 0, TEST_XWING, 1);
				struct xvt_flight_group *group =
					&g_stored.groups[0];
				group->optional_craft_category = 4;
				group->optional_craft[1] = CRAFT_SPECIES_Y_WING;
				group->number_of_optional_craft[1] = 3;
				group->number_of_optional_craft_waves[1] = 1;
				group->optional_craft[6] = CRAFT_SPECIES_B_WING;
				group->number_of_optional_craft[6] = 4;
				group->number_of_optional_craft_waves[6] = 2;
				group->mission_point_enabled[1] = 1;
				group->mission_point_enabled[3] = 1;
				g_stored.groups[1].optional_craft_category = 4;
				g_stored.groups[1].optional_craft[1] =
					CRAFT_SPECIES_Y_WING;
				init_world();
				g_flight_conf_no_pilot = 1;
				g_flight_mission_state
					.player_flight_group_wave_mode = 1;
				g_flight_mission_state
					.random_variation_enabled =
					(uint8_t)!off;
				static const unsigned int moduli[3] = {3, 3, 2};
				const unsigned int wanted[3] = {craft_pick,
								point_pick, 0};
				seed_for_draws(moduli, wanted, 3);
				XVT_ASSERT_INT_EQ(run_init(14), 1);
				const struct xvt_flight_group *first =
					&g_mission_flight_groups[0].fg;
				int pick = off ? 0 : (int)craft_pick;
				XVT_ASSERT_INT_EQ(first->craft_type,
						  craft[pick]);
				XVT_ASSERT_INT_EQ(first->number_of_craft,
						  count[pick]);
				XVT_ASSERT_INT_EQ(first->number_of_waves,
						  waves[pick]);
				XVT_ASSERT_INT_EQ(
					g_mission_fg_stats[0]
						.current_mission_point_ref,
					off ? 0x8000 : point_refs[point_pick]);
				XVT_ASSERT_INT_EQ(g_mission_flight_groups[1]
							  .fg.craft_type,
						  TEST_XWING);
			}
		}
	}
}

/* A group with a random special cargo craft names one of its craft by a
 * draw: its number in the group, from 0 to the count minus 1. */
static void check_init_random_special_cargo(void)
{
	for (unsigned int pick = 0; pick < 4; ++pick) {
		init_mission(MISSION_TYPE_COMBAT);
		init_group(0, 0, TEST_XWING, 0);
		g_stored.groups[0].number_of_craft = 4;
		g_stored.groups[0].random_special_cargo_craft = 1;
		g_stored.groups[0].special_cargo_craft = 9;
		init_group(1, 1, TEST_XWING, 0);
		g_stored.groups[1].special_cargo_craft = 2;
		init_world();
		const unsigned int moduli[1] = {4};
		seed_for_draws(moduli, &pick, 1);
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		XVT_ASSERT_INT_EQ(
			g_mission_flight_groups[0].fg.special_cargo_craft,
			pick);
		XVT_ASSERT_INT_EQ(
			g_mission_flight_groups[1].fg.special_cargo_craft, 2);
	}
}

/* Four groups, three of them flown by players 1 to 3 (bound by player number)
 * and one by player number 4 that no human flies, and one by the computer,
 * each of its own count of craft, player craft and 7 rounds. */
static void waves_mission(int kind)
{
	static const int crafts[5] = {2, 1, 3, 2, 2};
	static const int player_craft[5] = {0, 1, 2, 0, 0};
	init_mission(kind);
	for (int fg = 0; fg < 5; ++fg) {
		init_group(fg, fg < 3 ? 0 : 1, TEST_XWING, fg < 4 ? fg + 1 : 0);
		g_stored.groups[fg].number_of_craft = (uint8_t)crafts[fg];
		g_stored.groups[fg].player_craft = (uint8_t)player_craft[fg];
		g_stored.groups[fg].number_of_waves = 7;
	}
	init_world();
	g_flight_conf_no_pilot = 1;
	g_active_flight_player_count = 3;
}

/* A group a human flies with several craft flies its second if the file said
 * its first, and a group of one craft its first. */
static void check_init_player_craft(void)
{
	waves_mission(MISSION_TYPE_COMBAT);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.player_craft, 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.player_craft, 0);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.player_craft, 2);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].fg.player_craft, 0);
}

/* The player wave setting rules the rounds of the player groups a human flies
 * (and, in a melee, all player groups): none left on 0, the file's rounds on 1,
 * unlimited (99) on 2. A group of the computer keeps its rounds. */
static void check_init_player_waves(void)
{
	static const struct {
		int mode;
		int kind;
		int level[3];
	} cases[] = {
		{0, MISSION_TYPE_COMBAT, {0, 7, 7}},
		{1, MISSION_TYPE_COMBAT, {7, 7, 7}},
		{2, MISSION_TYPE_COMBAT, {99, 7, 7}},
		{0, MISSION_TYPE_MELEE, {0, 0, 7}},
		{2, MISSION_TYPE_MELEE, {99, 99, 7}},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		waves_mission(cases[i].kind);
		g_flight_mission_state.player_flight_group_wave_mode =
			(uint8_t)cases[i].mode;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.number_of_waves,
				  cases[i].level[0]);
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[3].fg.number_of_waves,
				  cases[i].level[1]);
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[4].fg.number_of_waves,
				  cases[i].level[2]);
	}
}

/* Each flight group's model is marked as needed, and the model of the multipart
 * craft (type 100) brings its parts, types 101 to 105; a model no group uses
 * is not marked. */
static void check_init_models_needed(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	init_group(0, 0, CRAFT_SPECIES_X_WING, 0);
	init_group(1, 1, CRAFT_SPECIES_ASTEROID, 0);
	init_world();
	XVT_ASSERT_INT_EQ(g_craft_type_to_object_type[CRAFT_SPECIES_ASTEROID],
			  100);
	for (int type = 0; type < 201; ++type) {
		g_object_type_table[type].asset_flags &= (uint8_t)~0x10;
	}
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	for (int type = 1; type < 201; ++type) {
		int needed = type == 1 || (type >= 100 && type <= 105);
		XVT_ASSERT_INT_EQ(g_object_type_table[type].asset_flags & 0x10,
				  needed ? 0x10 : 0);
	}
}

/* In a melee the paint scheme of a player group flying a TIE fighter,
 * interceptor, bomber, advanced or defender moves on by one, back to 0 past
 * 3; other craft, other groups and other mission types keep theirs. */
static void check_init_melee_markings(void)
{
	static const int craft[7] = {
		CRAFT_SPECIES_TIE_FIGHTER,  CRAFT_SPECIES_TIE_INTERCEPTOR,
		CRAFT_SPECIES_TIE_BOMBER,   CRAFT_SPECIES_TIE_ADVANCED,
		CRAFT_SPECIES_TIE_DEFENDER, CRAFT_SPECIES_X_WING,
		CRAFT_SPECIES_TIE_FIGHTER};
	static const int before[7] = {0, 2, 3, 1, 3, 1, 1};
	static const int melee_after[7] = {1, 3, 0, 2, 0, 1, 1};
	for (int melee = 0; melee < 2; ++melee) {
		init_mission(melee ? MISSION_TYPE_MELEE : MISSION_TYPE_COMBAT);
		for (int fg = 0; fg < 7; ++fg) {
			init_group(fg, 1, craft[fg], fg < 6 ? fg + 2 : 0);
			g_stored.groups[fg].markings = (uint8_t)before[fg];
		}
		init_world();
		g_flight_conf_no_pilot = 1;
		g_active_flight_player_count = 1;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		for (int fg = 0; fg < 7; ++fg) {
			XVT_ASSERT_INT_EQ(
				g_mission_flight_groups[fg].fg.markings,
				melee ? melee_after[fg] : before[fg]);
		}
	}
}

/* In a melee, a player group no human flies on a team with no human flier is
 * held back until a human arrives (arrive_only_if_human), unless the AI
 * opponents are on or the tournament has an AI source for the team. */
static void check_init_melee_arrive_only_if_human(void)
{
	static const struct {
		int ai_opponents;
		int sequence_active;
		int source_flag;
		int expected;
	} cases[] = {
		{0, 0, 0, 1},
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 1, -1, 1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		init_mission(MISSION_TYPE_MELEE);
		init_group(0, 0, TEST_XWING, 1);
		init_group(1, 0, TEST_XWING, 2);
		init_group(2, 1, TEST_XWING, 3);
		init_group(3, 1, TEST_XWING, 0);
		init_world();
		g_flight_conf_no_pilot = 1;
		g_active_flight_player_count = 1;
		g_flight_mission_state.ai_opponents_enabled =
			(uint8_t)cases[i].ai_opponents;
		g_pilot_data.mission_sequence_active = cases[i].sequence_active;
		g_pilot_data.melee_tournament_sequence_state.team_standings[1]
			.ai_opponent_source_team_and_type_flag =
			cases[i].source_flag;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		XVT_ASSERT_INT_EQ(
			g_mission_flight_groups[1].fg.arrive_only_if_human, 0);
		XVT_ASSERT_INT_EQ(
			g_mission_flight_groups[2].fg.arrive_only_if_human,
			cases[i].expected);
		XVT_ASSERT_INT_EQ(
			g_mission_flight_groups[3].fg.arrive_only_if_human, 0);
	}
}

/* The two clocks start at 0 and the countdown at the time limit: the flight's
 * own limit in minutes, or, when that is 255, the mission file's (which the
 * flight then keeps), or 0 for none. */
static void check_init_clocks(void)
{
	static const struct {
		int flight_limit;
		int file_limit;
		int minutes;
		int kept_limit;
	} cases[] = {
		{0, 5, 0, 0},
		{255, 5, 5, 5},
		{255, 0, 0, 0},
		{3, 5, 3, 3},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		init_mission(MISSION_TYPE_COMBAT);
		init_group(0, 0, TEST_XWING, 1);
		g_stored.header.time_limit_minutes =
			(uint8_t)cases[i].file_limit;
		init_world();
		g_flight_mission_state.mission_time_limit_minutes =
			(uint8_t)cases[i].flight_limit;
		g_flight_mission_state.team_victory_time_limit_started = 1;
		g_mission_elapsed_clock.hours = 1;
		g_mission_elapsed_clock.minutes = 2;
		g_mission_elapsed_clock.seconds = 3;
		g_mission_elapsed_clock.subsecond_ticks = 4;
		g_mission_countdown_clock.hours = 5;
		g_mission_countdown_clock.seconds = 6;
		g_mission_countdown_clock.subsecond_ticks = 7;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.hours, 0);
		XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.minutes, 0);
		XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.seconds, 0);
		XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.subsecond_ticks, 0);
		XVT_ASSERT_INT_EQ(g_mission_countdown_clock.hours, 0);
		XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes,
				  cases[i].minutes);
		XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 0);
		XVT_ASSERT_INT_EQ(g_mission_countdown_clock.subsecond_ticks, 0);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.mission_time_limit_minutes,
			cases[i].kept_limit);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.team_victory_time_limit_started,
			0);
		char fields[40];
		snprintf(fields, sizeof fields, "minutes=%d", cases[i].minutes);
		XVT_ASSERT_TRUE(line_holds("mission.prepared", fields));
	}
}

/* The asteroid field's seed follows the mission file's backdrop number alone:
 * the same number gives the same seed and the same default backdrop records
 * whatever the game's random state was, a different number another seed, and
 * the random state is put back as it was. */
static void check_init_backdrop_seed(void)
{
	static const struct {
		int backdrop;
		int16_t state;
	} runs[] = {{7, 0x1111}, {7, 0x2222}, {8, 0x1111}};
	uint16_t seeds[3];
	for (size_t i = 0; i < 3; ++i) {
		init_mission(MISSION_TYPE_COMBAT);
		init_group(0, 0, TEST_XWING, 0);
		g_stored.header.backdrop = (uint8_t)runs[i].backdrop;
		init_world();
		g_game_rand_feedback_state = runs[i].state;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		XVT_ASSERT_INT_EQ(g_game_rand_feedback_state, runs[i].state);
		seeds[i] = g_asteroid_field_rand_seed;
		char fields[60];
		snprintf(fields, sizeof fields,
			 "backdrop=%d seed=%u defaults=22", runs[i].backdrop,
			 (unsigned)seeds[i]);
		XVT_ASSERT_TRUE(
			line_holds("mission.backdrops_generated", fields));
	}
	XVT_ASSERT_INT_EQ(seeds[0], seeds[1]);
	XVT_ASSERT_TRUE(seeds[0] != seeds[2]);
}

/* Where the first backdrop record of each of the six faces starts: the default
 * records are 4 on each of four faces and 3 on the last two. */
static const int g_backdrop_face_start[6] = {0, 4, 8, 12, 16, 19};

/* A mission of one backdrop group per entry of sides: craft type 87 (a
 * backdrop), its image chosen by status1, its face by the z of its first
 * mission point and its direction by the x and y of that point. */
static void backdrop_mission(int count, const int *sides, int status1)
{
	static uint8_t remap_marks[17];
	init_mission(MISSION_TYPE_COMBAT);
	for (int fg = 0; fg < count; ++fg) {
		init_group(fg, 0, CRAFT_SPECIES_PLANET, 0);
		struct xvt_flight_group *group = &g_stored.groups[fg];
		group->status1 = (uint8_t)(status1 + fg);
		group->mission_point_z[0] = (int16_t)sides[fg];
		group->mission_point_x[0] = (int16_t)(3 + fg);
		group->mission_point_y[0] = (int16_t)(2 + fg);
	}
	init_world();
	for (int i = 0; i < 17; ++i) {
		g_backdrop_palette_remap_by_flight_group_status[i] =
			&remap_marks[i];
	}
	for (int type = 114; type < 117; ++type) {
		g_object_type_table[type].asset_flags &= (uint8_t)~0x10;
		g_object_type_table[type].resource_index = 0xEE;
		g_object_type_table[type].palette = NULL;
	}
}

/* A backdrop group's record, at the first of its face's places, holds the
 * spare model type it took, and the direction packed from the group's point:
 * x in the low 4 bits and minus y in the high 4. The face is the point's z,
 * and a z over 5 is warned of and taken as 5. */
static void check_init_backdrop_placement(void)
{
	for (int side = 0; side < 7; ++side) {
		backdrop_mission(1, &side, 0);
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		int face = side > 5 ? 5 : side;
		int record = g_backdrop_face_start[face];
		XVT_ASSERT_INT_EQ(g_backdrop_model_types[record], 114);
		XVT_ASSERT_INT_EQ(g_backdrop_packed_directions[record] & 0xF,
				  3);
		XVT_ASSERT_INT_EQ(g_backdrop_packed_directions[record] >> 4,
				  (-2) & 0xF);
		XVT_ASSERT_INT_EQ(g_object_type_table[114].asset_flags & 0x10,
				  0x10);
		XVT_ASSERT_INT_EQ(count_lines("mission.backdrop_side_clamped"),
				  side > 5);
		char fields[60];
		snprintf(fields, sizeof fields,
			 "fg=0 model=114 side=%d record=%d", face, record);
		XVT_ASSERT_TRUE(line_holds("mission.backdrop_placed", fields));
	}
	/* The default records are 4, 4, 4, 4, 3 and 3. */
	XVT_ASSERT_INT_EQ(g_backdrop_positive_y_count, 4);
	XVT_ASSERT_INT_EQ(g_backdrop_negative_y_count, 4);
	XVT_ASSERT_INT_EQ(g_backdrop_positive_x_count, 4);
	XVT_ASSERT_INT_EQ(g_backdrop_negative_x_count, 4);
	XVT_ASSERT_INT_EQ(g_backdrop_positive_z_count, 3);
	XVT_ASSERT_INT_EQ(g_backdrop_negative_z_count, 3);
	/* Two groups on one face take the next two places and the two spare
	 * types. */
	const int sides[2] = {2, 2};
	backdrop_mission(2, sides, 0);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_backdrop_model_types[g_backdrop_face_start[2]],
			  114);
	XVT_ASSERT_INT_EQ(g_backdrop_model_types[g_backdrop_face_start[2] + 1],
			  115);
	XVT_ASSERT_INT_EQ(
		g_backdrop_packed_directions[g_backdrop_face_start[2] + 1] &
			0xF,
		4);
}

/* A backdrop group's spare model type is given the group's image: for a status
 * under 8 the source type's resource index plus the status, for 8 to 16 the
 * status plus 85, and the palette remap of the status; a status over 16 leaves
 * both. In a paletted display (one byte a pixel) a status of 8 or more loses
 * all but its low 3 bits first. */
static void check_init_backdrop_images(void)
{
	static const struct {
		int status1;
		int bytes_per_pixel;
		int resource_delta;
		int resource_fixed;
		int remap;
		int status_after;
	} cases[] = {
		{3, 2, 3, -1, 3, 3},	 {9, 2, 0, 94, 9, 9},
		{16, 2, 0, 101, 16, 16}, {17, 2, 0, -1, -1, 17},
		{20, 2, 0, -1, -1, 20},	 {9, 1, 1, -1, 1, 1},
		{2, 1, 2, -1, 2, 2},
	};
	static uint8_t unused_remap;
	int side = 0;
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		backdrop_mission(1, &side, cases[i].status1);
		g_flight_bytes_per_pixel = cases[i].bytes_per_pixel;
		g_object_type_table[114].palette = &unused_remap;
		XVT_ASSERT_INT_EQ(run_init(14), 1);
		uint8_t source = g_object_type_table[87].resource_index;
		const struct object_type_info *backdrop =
			&g_object_type_table[114];
		if (cases[i].remap < 0) {
			XVT_ASSERT_INT_EQ(backdrop->resource_index, 0xEE);
			XVT_ASSERT_TRUE(backdrop->palette == &unused_remap);
		} else {
			int expected =
				cases[i].resource_fixed >= 0
					? cases[i].resource_fixed
					: source + cases[i].resource_delta;
			XVT_ASSERT_INT_EQ(backdrop->resource_index, expected);
			XVT_ASSERT_TRUE(
				backdrop->palette ==
				g_backdrop_palette_remap_by_flight_group_status
					[cases[i].remap]);
		}
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.status1,
				  cases[i].status_after);
	}
	g_flight_bytes_per_pixel = 2;
}

/* Known failure backdrop_images_shared, issue #134: each backdrop group is
 * drawn with its own image. Four groups, of four different statuses, take the
 * spare model types in turn, but there are two spare types, and the third and
 * fourth groups share the last type searched, so the third draws the fourth's
 * image. */
static void check_backdrop_groups_keep_their_images(void)
{
	const int sides[4] = {0, 1, 2, 3};
	backdrop_mission(4, sides, 0);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	uint8_t source = g_object_type_table[87].resource_index;
	for (int fg = 0; fg < 4; ++fg) {
		int type = g_backdrop_model_types[g_backdrop_face_start[fg]];
		XVT_ASSERT_INT_EQ(g_object_type_table[type].resource_index,
				  source + fg);
	}
}

/* ---- mission_init_flight_runtime_state -------------------------------- */

/* Runs the flight's start on the stored mission: mission_init and then the
 * runtime state, with the cockpit resources counted as loaded. */
static void start_flight(void)
{
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	g_hud_cockpit_resources_loaded = 1;
	/* No player is the one whose cockpit is drawn, so none is. */
	g_local_player = 8;
	mission_init_flight_runtime_state();
	g_local_player = 7;
}

/* A group of the computer that arrives at the start, with the given craft
 * and team. */
static void arriving_group(int fg, int team, int craft_type, int count)
{
	init_group(fg, team, craft_type, 0);
	g_stored.groups[fg].number_of_craft = (uint8_t)count;
}

/* The flight's start sets each group's counts: the craft it will send, in all
 * rounds, is its craft count times its rounds plus one (the count squared for
 * mines), the special cargo craft's is the rounds plus one when the group has
 * one, every other outcome count is 0 and every goal state is pending (4). A
 * group that may not arrive and no human flies has none. A group arriving at
 * once is marked arrived. */
static void check_runtime_group_counts(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 0, TEST_XWING, 2);
	g_stored.groups[0].player_number = 1;
	g_stored.groups[0].number_of_waves = 3;
	arriving_group(1, 1, TEST_XWING, 3);
	g_stored.groups[1].number_of_waves = 1;
	g_stored.groups[1].special_cargo_craft = 1;
	arriving_group(2, 1, TEST_XWING, 2);
	g_stored.groups[2].random_special_cargo_craft = 1;
	g_stored.groups[2].arrival_delay_minutes = 1;
	arriving_group(3, 1, TEST_XWING, 2);
	g_stored.groups[3].arrival_difficulty = 6;
	arriving_group(4, 2, CRAFT_SPECIES_MINE_TYPE_A, 3);
	g_stored.groups[4].number_of_waves = 2;
	init_world();
	g_flight_conf_no_pilot = 1;
	g_flight_mission_state.player_flight_group_wave_mode = 1;
	start_flight();
	const struct mission_fg_runtime_stats *stats = g_mission_fg_stats;
	XVT_ASSERT_INT_EQ(stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			  8);
	XVT_ASSERT_INT_EQ(stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			  6);
	XVT_ASSERT_INT_EQ(stats[2].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			  2);
	XVT_ASSERT_INT_EQ(stats[3].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			  0);
	XVT_ASSERT_INT_EQ(stats[4].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			  27);
	XVT_ASSERT_INT_EQ(
		stats[0].special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL], 0);
	XVT_ASSERT_INT_EQ(
		stats[1].special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL], 2);
	XVT_ASSERT_INT_EQ(
		stats[2].special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL], 1);
	XVT_ASSERT_INT_EQ(
		stats[3].special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL], 0);
	for (int fg = 0; fg < 5; ++fg) {
		for (int outcome = FLIGHT_GROUP_OUTCOME_DESTROYED;
		     outcome < FLIGHT_GROUP_OUTCOME_COUNT; ++outcome) {
			XVT_ASSERT_INT_EQ(stats[fg].outcome_count[outcome], 0);
		}
		for (int state = 0; state < 80; ++state) {
			XVT_ASSERT_INT_EQ(stats[fg].goal_state[state], 4);
		}
	}
	XVT_ASSERT_INT_EQ(stats[0].has_arrived, 1);
	XVT_ASSERT_INT_EQ(stats[1].has_arrived, 1);
	XVT_ASSERT_INT_EQ(stats[2].has_arrived, 0);
	XVT_ASSERT_INT_EQ(stats[3].has_arrived, 0);
	XVT_ASSERT_INT_EQ(stats[4].has_arrived, 1);
	XVT_ASSERT_INT_EQ(stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);
	XVT_ASSERT_INT_EQ(stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  3);
	XVT_ASSERT_INT_EQ(stats[2].outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  0);
	XVT_ASSERT_INT_EQ(stats[0].waves_remaining, 3);
	XVT_ASSERT_INT_EQ(stats[1].waves_remaining, 1);
	XVT_ASSERT_INT_EQ(stats[4].waves_remaining, 0);
	XVT_ASSERT_INT_EQ(g_next_object_signature > 1, 1);
	XVT_ASSERT_TRUE(
		line_holds("mission.group_reset fg=1 ", "total=6 cargo=2"));
}

/* A group may arrive at a difficulty when its arrival difficulty setting
 * allows it: 0 all three difficulties, 1 easy, 2 medium, 3 hard, 4 medium and
 * hard, 5 easy and medium, 6 and 7 none. */
static void check_runtime_arrival_difficulty(void)
{
	static const uint8_t allowed[8] = {7, 1, 2, 4, 6, 3, 0, 0};
	for (int difficulty = GAME_DIFFICULTY_EASY;
	     difficulty <= GAME_DIFFICULTY_HARD; ++difficulty) {
		init_mission(MISSION_TYPE_COMBAT);
		for (int fg = 0; fg < 8; ++fg) {
			arriving_group(fg, 1, TEST_XWING, 1);
			g_stored.groups[fg].arrival_difficulty = (uint8_t)fg;
		}
		init_world();
		g_flight_mission_state.difficulty = (uint8_t)difficulty;
		start_flight();
		for (int fg = 0; fg < 8; ++fg) {
			int enabled = (allowed[fg] & (1 << difficulty)) != 0;
			XVT_ASSERT_INT_EQ(
				g_mission_fg_stats[fg].arrival_enabled != 0,
				enabled);
			XVT_ASSERT_INT_EQ(g_mission_fg_stats[fg].has_arrived,
					  enabled);
			XVT_ASSERT_INT_EQ(g_mission_fg_stats[fg].outcome_count
						  [FLIGHT_GROUP_OUTCOME_TOTAL],
					  enabled ? 1 : 0);
			if (!enabled) {
				char fields[40];
				snprintf(fields, sizeof fields,
					 "difficulty=%d setting=%d", difficulty,
					 fg);
				char start[40];
				snprintf(start, sizeof start,
					 "mission.arrival_masked fg=%d ", fg);
				XVT_ASSERT_TRUE(line_holds(start, fields));
			}
		}
	}
}

/* Sets one arrival trigger pair of a stored group: the conditions on its two
 * slots (for a player number variable of the first), and the join. */
static void set_arrival_pair(int fg, int pair, int condition0, int variable0,
			     int condition1, int or_join)
{
	struct mission_trigger_pair *trigger =
		&g_stored.groups[fg].arrival_triggers[pair];
	trigger->triggers[0].condition = (uint8_t)condition0;
	trigger->triggers[0].variable_type = GOAL_TARGET_PLAYER_NUMBER;
	trigger->triggers[0].variable = (uint8_t)variable0;
	trigger->triggers[1].condition = (uint8_t)condition1;
	trigger->trigger1_or_trigger2 = (uint8_t)or_join;
}

/* A group whose arrival triggers (the two pairs joined by AND, or by OR when
 * arrivals12_or_arrivals34 is 1) can no longer hold has its arrival turned
 * off at the start; only a pair of conditions on players connected or not,
 * joined by AND within the pair, is judged. Player 1 is flown by a human;
 * player 2 has a group no human flies. */
static void check_runtime_arrival_triggers_failed(void)
{
	enum {
		CONNECTED = MISSION_COND_PLAYER_CONNECTED,
		DISCONNECTED = MISSION_COND_PLAYER_DISCONNECTED,
		TRUE_ALWAYS = MISSION_COND_ALWAYS_TRUE,
	};
	static const struct {
		int condition0;
		int variable0;
		int or_within;
		int second_condition;
		int second_variable;
		int or_across;
		int enabled;
	} cases[] = {
		/* Waiting for player 1 in both pairs, who is flying: holds. */
		{CONNECTED, 0, 0, CONNECTED, 0, 0, 1},
		/* Waiting for player 1 and then for it to be gone: the first
		 * pair holds, the second fails. */
		{CONNECTED, 0, 0, DISCONNECTED, 0, 0, 0},
		/* Waiting for player 2, whose group has no human: failed. */
		{CONNECTED, 1, 0, TRUE_ALWAYS, 0, 0, 0},
		/* The same, with the pairs joined by OR and the second open. */
		{CONNECTED, 1, 0, TRUE_ALWAYS, 0, 1, 1},
		/* Both pairs failed, joined by OR: failed. */
		{CONNECTED, 1, 0, DISCONNECTED, 0, 1, 0},
		/* Waiting for player 1, who is flying: holds. */
		{CONNECTED, 0, 0, TRUE_ALWAYS, 0, 0, 1},
		{CONNECTED, 0, 0, TRUE_ALWAYS, 0, 1, 1},
		/* A pair joined by OR is not judged. */
		{CONNECTED, 1, 1, TRUE_ALWAYS, 0, 0, 1},
		/* Waiting for player 1 to be gone: failed. */
		{DISCONNECTED, 0, 0, TRUE_ALWAYS, 0, 0, 0},
		/* Waiting for player 2 to be gone: holds. */
		{DISCONNECTED, 1, 0, TRUE_ALWAYS, 0, 0, 1},
	};
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 0, TEST_XWING, 1);
	g_stored.groups[0].player_number = 1;
	arriving_group(11, 0, TEST_XWING, 1);
	g_stored.groups[11].player_number = 2;
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		int fg = (int)i + 1;
		arriving_group(fg, 1, TEST_XWING, 1);
		set_arrival_pair(fg, 0, cases[i].condition0, cases[i].variable0,
				 TRUE_ALWAYS, cases[i].or_within);
		set_arrival_pair(fg, 1, cases[i].second_condition,
				 cases[i].second_variable, TRUE_ALWAYS, 0);
		g_stored.groups[fg].arrivals12_or_arrivals34 =
			(uint8_t)cases[i].or_across;
	}
	init_world();
	g_flight_conf_no_pilot = 1;
	g_active_flight_player_count = 1;
	start_flight();
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		int fg = (int)i + 1;
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[fg].arrival_enabled != 0,
				  cases[i].enabled);
		char start[40];
		snprintf(start, sizeof start,
			 "mission.arrival_trigger_failed fg=%d ", fg);
		XVT_ASSERT_INT_EQ(find_line(start) != NULL, !cases[i].enabled);
	}
	/* The states of the two pairs the failure is logged with: 1 holds, 2
	 * fails, 4 is open. */
	XVT_ASSERT_TRUE(line_holds("mission.arrival_trigger_failed fg=2 ",
				   "first=1 second=2"));
	XVT_ASSERT_TRUE(line_holds("mission.arrival_trigger_failed fg=3 ",
				   "first=2 second=4"));
	XVT_ASSERT_TRUE(line_holds("mission.arrival_trigger_failed fg=5 ",
				   "first=2 second=2"));
	XVT_ASSERT_TRUE(line_holds("mission.arrival_trigger_failed fg=9 ",
				   "first=2 second=4"));
}

/* A group arrives at once when it is flown by a human, or its first arrival
 * condition is "always" with no delay, unless a player number it has is not a
 * human's and it waits for one (arrive_only_if_human). */
static void check_runtime_arrival_at_start(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 0, TEST_XWING, 1);
	g_stored.groups[0].player_number = 1;
	g_stored.groups[0].arrival_triggers[0].triggers[0].condition =
		MISSION_COND_ARRIVED;
	g_stored.groups[0].arrival_delay_seconds = 9;
	arriving_group(1, 1, TEST_XWING, 1);
	g_stored.groups[1].player_number = 2;
	arriving_group(2, 1, TEST_XWING, 1);
	g_stored.groups[2].player_number = 3;
	g_stored.groups[2].arrive_only_if_human = 1;
	arriving_group(3, 1, TEST_XWING, 1);
	g_stored.groups[3].arrive_only_if_human = 1;
	arriving_group(4, 1, TEST_XWING, 1);
	g_stored.groups[4].arrival_delay_seconds = 1;
	arriving_group(5, 1, TEST_XWING, 1);
	g_stored.groups[5].arrival_triggers[0].triggers[0].condition =
		MISSION_COND_ARRIVED;
	init_world();
	g_flight_conf_no_pilot = 1;
	g_active_flight_player_count = 1;
	start_flight();
	static const int arrived[6] = {1, 1, 0, 1, 0, 0};
	for (int fg = 0; fg < 6; ++fg) {
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[fg].has_arrived,
				  arrived[fg]);
	}
	/* Every group with craft has its team counted as having some; the
	 * player's craft is bound to the player. */
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_has_countable_craft[0], 1);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_has_countable_craft[1], 1);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_has_countable_craft[2], 0);
	XVT_ASSERT_TRUE(g_players[0].object_index >= 0);
	XVT_ASSERT_INT_EQ(
		g_object_table[g_players[0].object_index].player_owner_idx, 0);
	XVT_ASSERT_INT_EQ(g_players[0].bound_flight_group_idx, 0);
	XVT_ASSERT_INT_EQ(
		g_players[0].bound_object_signature,
		g_object_table[g_players[0].object_index].object_signature);
	XVT_ASSERT_INT_EQ(g_initial_spawn_bind_player_craft_slots, 0);
}

/* The designation codes of a group's roles go to the teams the role's
 * selector names: a digit 1 to 9 team 0 to 8, A every team, O the group's own
 * team, F the teams allied with it and H the others; the role names are COM,
 * BAS, STA, MIS, CON, STR, REL, PRI, SEC, TER, RES and MAN for codes 1 to 12.
 * Entries of four characters are read to the first empty selector. */
static void check_runtime_roles(void)
{
	static const struct {
		const char *role;
		int team;
	} groups[] = {
		{"1COM", 0}, {"2BAS3STA", 1},	  {"AMIS", 1},
		{"OCON", 1}, {"FSTR", 2},	  {"HREL", 1},
		{"9PRI", 4}, {"1XXXZREST5SE", 4}, {"1SEC2TER4MAN", 5},
		{"3RES", 5}, {"1COM", 6},	  {"", 6},
	};
	init_mission(MISSION_TYPE_COMBAT);
	for (size_t fg = 0; fg < sizeof groups / sizeof groups[0]; ++fg) {
		arriving_group((int)fg, groups[fg].team, TEST_XWING, 1);
		memset(g_stored.groups[fg].craft_role, 0, 16);
		memcpy(g_stored.groups[fg].craft_role, groups[fg].role,
		       strlen(groups[fg].role));
	}
	/* Team 2 is allied with team 1, as team 1 is with itself. */
	g_stored.team_count[2] = 1;
	g_stored.teams[2].allies[1] = 1;
	init_world();
	/* The loader upper-cases the roles; these already are. */
	start_flight();
	int codes[10][12];
	memset(codes, 0, sizeof codes);
	codes[0][0] = 1;
	codes[1][1] = 2;
	codes[2][1] = 3;
	for (int team = 0; team < 10; ++team) {
		codes[team][2] = 4;
		if (team != 1) {
			codes[team][5] = 7;
		}
	}
	codes[1][3] = 5;
	codes[1][4] = 6;
	codes[2][4] = 6;
	codes[8][6] = 8;
	codes[0][8] = 9;
	codes[1][8] = 10;
	codes[3][8] = 12;
	codes[2][9] = 11;
	codes[0][10] = 1;
	for (int team = 0; team < 10; ++team) {
		for (int fg = 0; fg < 12; ++fg) {
			XVT_ASSERT_INT_EQ(
				g_flight_mission_state.runtime
					.team_fg_designation_code[team][fg],
				codes[team][fg]);
		}
	}
}

/* Counts the memory handles in use. */
static int used_handle_count(void)
{
	int count = 0;
	for (int handle = 0; handle < 32768; ++handle) {
		count += g_handle_tables.ptr_table[handle] != NULL;
	}
	return count;
}

/* A second mission_init frees the five pools the first made before it makes
 * new ones, so the handles in use do not grow, and clears the new pools. */
static void check_init_replaces_pools(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	init_group(0, 0, TEST_XWING, 1);
	init_world();
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	int used = used_handle_count();
	g_object_table[40].object_type = 9;
	g_mobile_object_pool_base[40].seconds_alive = 7;
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(used_handle_count(), used);
	XVT_ASSERT_INT_EQ(g_object_table[40].object_type, 0);
	XVT_ASSERT_INT_EQ(g_mobile_object_pool_base[40].seconds_alive, 0);
	XVT_ASSERT_TRUE(g_object_table[40].mobj ==
			&g_mobile_object_pool_base[40]);
}

/* The flight's start puts the runtime state in order: the teams' scores,
 * goal states, counts and kill stats, every player's targeting, view, timers
 * and chat, the sim step, the signature counter, and the flags and counts the
 * comment names. It leaves the current flight group at the group count. */
static void check_runtime_resets(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 0, TEST_XWING, 2);
	g_stored.groups[0].player_number = 1;
	arriving_group(1, 1, TEST_XWING, 2);
	init_world();
	g_flight_conf_no_pilot = 1;
	struct mission_flight_runtime_state *runtime =
		&g_flight_mission_state.runtime;
	for (int team = 0; team < 10; ++team) {
		runtime->team_scores[0][team] = 5;
		runtime->team_scores[1][team] = 6;
		runtime->team_reinforcements_called[team] = 1;
		runtime->team_mission_completion_time_seconds[team] = 8;
		for (int goal = 0; goal < 3; ++goal) {
			runtime->team_global_goal_state[team][goal] = 1;
			runtime->team_goal_status[team][goal] = 2;
			for (int trigger = 0; trigger < 4; ++trigger) {
				runtime->global_goal_trigger_counts[0][team]
								   [goal]
								   [trigger] =
					3;
				runtime->global_goal_trigger_counts[1][team]
								   [goal]
								   [trigger] =
					4;
			}
		}
		for (int row = 0; row < 4; ++row) {
			runtime->team_kill_stats[row][team] = 9;
		}
		runtime->team_fg_inspected_captured_counts[0][team][1] = 2;
		runtime->team_fg_inspected_captured_counts[1][team][47] = 2;
	}
	runtime->global_goal_status_unused = 5;
	runtime->global_primary_goal_status = 1;
	runtime->global_bonus_goal_status = 1;
	g_flight_mission_state.mission_end_pending = 1;
	g_flight_mission_state.max_connected_player_count_this_mission = 3;
	for (int message = 0; message < MISSION_MESSAGE_COUNT; ++message) {
		g_flight_mission_state.message_triggered[message] = 1;
		g_flight_mission_state.message_delay_countdown[message] = 9;
	}
	for (int player = 0; player < 8; ++player) {
		g_players[player].awaiting_new_craft = 1;
		g_players[player].target_box_enabled = 0;
		g_players[player].current_target_object_idx = 4;
		g_players[player].selected_target_component = 4;
		g_players[player].targeting_state = 4;
		g_players[player].engine_wash_source_obj_idx = 4;
		g_players[player].target_preset_slot[2] = 4;
		g_players[player].engine_wash_strength = 4;
		g_players[player].msg_length = 4;
		strcpy(g_players[player].msg_text, "chat");
		g_players[player].chat_recipient_mode = 3;
		g_players[player].view_state.camera_distance = 5;
		g_players[player].view_state.external_camera_active = 1;
		g_players[player].pending_action_timer = 6;
		g_players[player].beam_fire_cooldown_timer = 6;
	}
	g_sim_steps_per_second = 0;
	g_elapsed_ticks = 0;
	g_input_timestamp = 99;
	g_ready_message_queue_count = 3;
	g_target_proximity_blink_bit = 0x400;
	g_flight_runtime_state_initialized = 0;
	g_render_object_ref = 4;
	g_render_object_ref_flags = 4;
	memset(&g_flight_global_countdown_timers, 0x5A,
	       sizeof g_flight_global_countdown_timers);
	memset(g_player_flight_transient_timers, 0x5A,
	       sizeof g_player_flight_transient_timers);
	for (int fg = 0; fg < 2; ++fg) {
		memset(&g_mission_fg_stats[fg], 0x5A,
		       sizeof g_mission_fg_stats[fg]);
	}
	for (int player = 0; player < 8; ++player) {
		g_players[player].smoothed_input_yaw = 3;
		g_players[player].smoothed_input_pitch = 3;
		g_players[player].saved_key_mods = 3;
		g_players[player].key_mods_hold_timer = 3;
		g_players[player].hyperspace_phase = 3;
		g_players[player].hyperspace_runtime.phase_elapsed_ticks = 3;
		g_players[player].engine_wash_strength = 3;
		g_players[player].view_state.hud_aim_x_snap_state = 3;
		g_players[player].view_state.hud_aim_x = 3;
		g_players[player].view_state.hud_aim_y = 3;
		g_players[player].view_state.player_input_blocked = 1;
		g_players[player].view_state.target_camera_active = 1;
		g_players[player].saved_hud_view_state = 3;
		for (int slot = 0; slot < 4; ++slot) {
			g_players[player].target_preset_slot[slot] = 4;
		}
	}
	start_flight();
	for (int team = 0; team < 10; ++team) {
		XVT_ASSERT_INT_EQ(runtime->team_scores[0][team], 0);
		XVT_ASSERT_INT_EQ(runtime->team_scores[1][team], 0);
		XVT_ASSERT_INT_EQ(runtime->team_reinforcements_called[team], 0);
		XVT_ASSERT_INT_EQ(
			runtime->team_mission_completion_time_seconds[team], 0);
		for (int goal = 0; goal < 3; ++goal) {
			XVT_ASSERT_INT_EQ(
				runtime->team_global_goal_state[team][goal], 4);
			XVT_ASSERT_INT_EQ(runtime->team_goal_status[team][goal],
					  0);
			for (int trigger = 0; trigger < 4; ++trigger) {
				XVT_ASSERT_INT_EQ(
					runtime->global_goal_trigger_counts
						[0][team][goal][trigger],
					0);
				XVT_ASSERT_INT_EQ(
					runtime->global_goal_trigger_counts
						[1][team][goal][trigger],
					0);
			}
		}
		for (int row = 0; row < 4; ++row) {
			XVT_ASSERT_INT_EQ(runtime->team_kill_stats[row][team],
					  0);
		}
		XVT_ASSERT_INT_EQ(
			runtime->team_fg_inspected_captured_counts[0][team][1],
			0);
		XVT_ASSERT_INT_EQ(
			runtime->team_fg_inspected_captured_counts[1][team][47],
			0);
	}
	XVT_ASSERT_INT_EQ(runtime->global_goal_status_unused, 0);
	XVT_ASSERT_INT_EQ(runtime->global_primary_goal_status, 0);
	XVT_ASSERT_INT_EQ(runtime->global_bonus_goal_status, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.max_connected_player_count_this_mission,
		0);
	for (int message = 0; message < MISSION_MESSAGE_COUNT; ++message) {
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.message_triggered[message], 0);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.message_delay_countdown[message],
			0);
	}
	for (int player = 0; player < 8; ++player) {
		const struct player_data *data = &g_players[player];
		XVT_ASSERT_INT_EQ(data->awaiting_new_craft, 0);
		XVT_ASSERT_INT_EQ(data->target_box_enabled, 1);
		XVT_ASSERT_INT_EQ(data->current_target_object_idx, -1);
		XVT_ASSERT_INT_EQ(data->target_cycle_start, -1);
		XVT_ASSERT_INT_EQ(data->selected_target_component, -1);
		XVT_ASSERT_INT_EQ(data->targeting_state, -1);
		XVT_ASSERT_INT_EQ(data->engine_wash_source_obj_idx, -1);
		for (int slot = 0; slot < 4; ++slot) {
			XVT_ASSERT_INT_EQ(data->target_preset_slot[slot], -1);
		}
		XVT_ASSERT_INT_EQ(data->engine_wash_strength, 0);
		XVT_ASSERT_INT_EQ(data->msg_length, 0);
		XVT_ASSERT_INT_EQ(data->msg_text[0], 0);
		XVT_ASSERT_INT_EQ(data->chat_recipient_mode,
				  FLIGHT_CHAT_RECIPIENT_INACTIVE);
		XVT_ASSERT_INT_EQ(data->view_state.camera_distance, 1024);
		XVT_ASSERT_INT_EQ(data->view_state.external_camera_active, 0);
		XVT_ASSERT_INT_EQ(data->view_state.camera_focus_obj_idx,
				  (uint16_t)data->object_index);
		XVT_ASSERT_INT_EQ(data->saved_hud_view_state, HUD_VIEW_FORWARD);
		XVT_ASSERT_INT_EQ(data->pending_action_timer, 0);
		XVT_ASSERT_INT_EQ(data->beam_fire_cooldown_timer, 0);
	}
	XVT_ASSERT_INT_EQ(g_sim_steps_per_second, 15);
	XVT_ASSERT_INT_EQ(g_elapsed_ticks, 15);
	XVT_ASSERT_INT_EQ(g_input_timestamp, 0);
	XVT_ASSERT_INT_EQ(g_ready_message_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_target_proximity_blink_bit, 0);
	XVT_ASSERT_INT_EQ(g_flight_runtime_state_initialized, 1);
	XVT_ASSERT_INT_EQ(g_render_object_ref, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_render_object_ref_flags, 0);
	XVT_ASSERT_INT_EQ(g_hud_loaded_panel_set_id, UINT8_MAX);
	XVT_ASSERT_INT_EQ(g_local_debris_recycle_slot_cursor,
			  g_local_transient_slot_start);
	XVT_ASSERT_INT_EQ(g_current_flight_group_idx, 2);
	XVT_ASSERT_INT_EQ(g_flight_initial_texture_cache_flush_pending, 1);
	XVT_ASSERT_INT_EQ(g_flight_display_rebuild_pending, 1);
	XVT_ASSERT_INT_EQ(g_next_object_signature, 5);
	for (int player = 0; player < 8; ++player) {
		const struct player_data *data = &g_players[player];
		XVT_ASSERT_INT_EQ(data->smoothed_input_yaw, 0);
		XVT_ASSERT_INT_EQ(data->smoothed_input_pitch, 0);
		XVT_ASSERT_INT_EQ(data->saved_key_mods, 0);
		XVT_ASSERT_INT_EQ(data->key_mods_hold_timer, 0);
		XVT_ASSERT_INT_EQ(data->hyperspace_phase, 0);
		XVT_ASSERT_INT_EQ(data->hyperspace_runtime.phase_elapsed_ticks,
				  0);
		XVT_ASSERT_INT_EQ(data->view_state.hud_aim_x_snap_state, 0);
		XVT_ASSERT_INT_EQ(data->view_state.hud_aim_x, 0);
		XVT_ASSERT_INT_EQ(data->view_state.hud_aim_y, 0);
		XVT_ASSERT_INT_EQ(data->view_state.player_input_blocked, 0);
		XVT_ASSERT_INT_EQ(data->view_state.target_camera_active, 0);
	}
	const uint8_t *timers =
		(const uint8_t *)&g_flight_global_countdown_timers;
	for (size_t i = 0; i < sizeof g_flight_global_countdown_timers; ++i) {
		XVT_ASSERT_INT_EQ(timers[i], 0);
	}
	const uint8_t *player_timers =
		(const uint8_t *)g_player_flight_transient_timers;
	for (size_t i = 0; i < sizeof g_player_flight_transient_timers; ++i) {
		XVT_ASSERT_INT_EQ(player_timers[i], 0);
	}
	for (int fg = 0; fg < 2; ++fg) {
		const struct mission_fg_runtime_stats *stats =
			&g_mission_fg_stats[fg];
		XVT_ASSERT_INT_EQ(stats->arrival_delay_pending, 0);
		XVT_ASSERT_INT_EQ(stats->arrival_delay_timer, 0);
		for (int outcome = FLIGHT_GROUP_OUTCOME_DESTROYED;
		     outcome < FLIGHT_GROUP_OUTCOME_COUNT; ++outcome) {
			XVT_ASSERT_INT_EQ(stats->outcome_count[outcome], 0);
			XVT_ASSERT_INT_EQ(stats->special_cargo_outcome[outcome],
					  0);
		}
		for (int team = 0; team < 10; ++team) {
			XVT_ASSERT_INT_EQ(stats->team_inspected[team], 0);
			XVT_ASSERT_INT_EQ(
				stats->team_special_cargo_inspected[team], 0);
			XVT_ASSERT_INT_EQ(stats->team_uninspected_lost[team],
					  0);
			XVT_ASSERT_INT_EQ(
				stats->team_special_cargo_uninspected_lost
					[team],
				0);
			XVT_ASSERT_INT_EQ(
				stats->team_captured_departed_count[team], 0);
			XVT_ASSERT_INT_EQ(
				stats->team_special_cargo_captured_departed
					[team],
				0);
			XVT_ASSERT_INT_EQ(stats->team_uncaptured_lost[team], 0);
			XVT_ASSERT_INT_EQ(
				stats->team_special_cargo_uncaptured_lost[team],
				0);
			for (int row = 0; row < 4; ++row) {
				XVT_ASSERT_INT_EQ(
					stats->team_event_extra[row][team], 0);
			}
		}
	}
	XVT_ASSERT_TRUE(line_holds("mission.runtime_ready", "objects=4"));
}

/* Known failure tie_global_goals_kept, issue #267: the TIE loader leaves the
 * global goals it does not convert as they were, so the goals of an earlier
 * mission stay in the second trigger pair of each converted goal, in team 0's
 * goals past the file's count and in the other teams'. A loaded mission has
 * none of them. */
static void check_tie_global_goals_cleared(void)
{
	build_tie_mission();
	fresh_world();
	g_mission_global_goals[0][0].trigger_pairs[1].triggers[0].condition =
		MISSION_COND_ARRIVED;
	g_mission_global_goals[0][3].trigger_pairs[0].triggers[0].condition =
		MISSION_COND_ARRIVED;
	strcpy(g_mission_global_goals[0][3].name, "Old");
	g_mission_global_goals[1][0].trigger_pairs[0].triggers[0].condition =
		MISSION_COND_ARRIVED;
	xvt_test_open_assets(&g_assets);
	write_tie_mission("tie.tie", UINT16_MAX, &g_tie_header, g_tie_groups, 4,
			  g_tie_goals, 2);
	XVT_ASSERT_INT_EQ(mission_load_file("tie.tie"), 1);
	xvt_test_close_assets(&g_assets);
	XVT_ASSERT_INT_EQ(g_mission_global_goals[0][0]
				  .trigger_pairs[1]
				  .triggers[0]
				  .condition,
			  MISSION_COND_ALWAYS_TRUE);
	XVT_ASSERT_INT_EQ(g_mission_global_goals[0][3]
				  .trigger_pairs[0]
				  .triggers[0]
				  .condition,
			  MISSION_COND_ALWAYS_TRUE);
	XVT_ASSERT_TRUE(g_mission_global_goals[0][3].name[0] == '\0');
	XVT_ASSERT_INT_EQ(g_mission_global_goals[1][0]
				  .trigger_pairs[0]
				  .triggers[0]
				  .condition,
			  MISSION_COND_ALWAYS_TRUE);
}

/* Known failure player_team_past_table, issue #171: a player's flight group on
 * team 10 or more, which the loader does not refuse, is counted in arrays of
 * the 10 teams. mission_init counts the teams that have human fliers in a
 * stack array of 10, which a team of 10 writes past. */
static void check_init_player_team_past_table(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	init_group(0, 10, TEST_XWING, 1);
	init_world();
	g_flight_mission_state.player_flight_group_wave_mode = 1;
	set_roster_slot(0, 100);
	set_network_player(0, 100, 0, 0);
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	XVT_ASSERT_INT_EQ(g_players[0].team, 10);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].player_owner_idx, 0);
}

/* ---- mission_init_flight_group_object_slot ---------------------------- */

/* The craft the spawn checks use: a TIE bomber, which may carry a beam. */
enum { SPAWN_TYPE = CRAFT_SPECIES_TIE_BOMBER };

/* Makes the mission in g_stored with mission_init only, so its object table is
 * empty, and counts the first group's craft as to come. */
static void spawn_mission_ready(void)
{
	init_world();
	g_flight_conf_no_pilot = 1;
	g_active_flight_player_count = 1;
	XVT_ASSERT_INT_EQ(run_init(14), 1);
	for (int fg = 0; fg < g_stored.header.num_flight_groups; ++fg) {
		memset(&g_mission_fg_stats[fg], 0,
		       sizeof g_mission_fg_stats[fg]);
		g_mission_fg_stats[fg].current_mission_point_ref = 0x8000;
		g_mission_fg_stats[fg]
			.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 99;
	}
	g_mission_elapsed_clock.seconds = 0;
	g_initial_spawn_bind_player_craft_slots = 0;
}

/* The slot of craft ordinal in flight group fg, or -1. */
static int craft_slot(int fg, int ordinal)
{
	for (int slot = 0; slot < 32; ++slot) {
		if (g_object_table[slot].object_type != 0 &&
		    g_object_table[slot].mobj->p_craft != NULL &&
		    g_object_table[slot].flight_group_idx == fg &&
		    g_object_table[slot].mobj->p_craft->craft_ordinal ==
			    ordinal) {
			return slot;
		}
	}
	return -1;
}

/* Spawns craft ordinal of flight group fg (the whole round when ordinal is
 * UINT16_MAX) and returns the return value of the spawn. */
static int16_t spawn_craft(int fg, uint16_t ordinal)
{
	g_current_flight_group_idx = (uint16_t)fg;
	return mission_spawn_flight_group_wave_craft(ordinal);
}

/* Sets the model of object type 1 (the X-wing) to known values, keeping the
 * old ones in *saved. */
static struct model_def *set_test_model(struct model_def *saved)
{
	struct model_def *model = model_of(SPAWN_TYPE);
	*saved = *model;
	model->hull_strength = 500;
	model->system_damage_hull_threshold = 50;
	model->shield_strength = 300;
	model->has_shields = 1;
	model->has_hyperdrive = 1;
	model->max_speed = 400;
	model->yaw_rate = 11;
	model->roll_rate = 12;
	model->pitch_rate = 13;
	model->engine_glow_count = 2;
	model->countermeasure_count = 12;
	model->laser_group_weapon_type[0] = 7;
	model->laser_group_first_slot[0] = 1;
	model->laser_group_last_slot[0] = 2;
	model->laser_group_slot_count[0] = 2;
	model->laser_group_mount_type[0] = 0;
	model->laser_group_weapon_type[1] = 8;
	model->laser_group_first_slot[1] = 3;
	model->laser_group_last_slot[1] = 3;
	model->laser_group_slot_count[1] = 1;
	model->laser_group_mount_type[1] = 2;
	model->warhead_launcher_first_slot[0] = 5;
	model->warhead_launcher_last_slot[0] = 6;
	model->warhead_launcher_capacity[0] = 8;
	return model;
}

/* A spawned craft takes its values from the model and the flight group: the
 * object's type, IFF, team, genus, family, paint scheme, group and signature;
 * hull and shields from the model (the shield strength doubled in the forward
 * shield for a computer's craft); lasers, warheads, beam and countermeasure
 * from the model and the group; its skill and think interval from the group's
 * AI level; its throttle from the plan of its first order. Its counts are
 * counted in the group. */
static void check_spawn_craft_values(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 2);
	struct xvt_flight_group *group = &g_stored.groups[0];
	group->iff = 4;
	group->markings = 3;
	group->group_ai = 3;
	group->warhead = 1;
	group->beam = BEAM_TYPE_JAMMING;
	group->countermeasures = COUNTERMEASURE_TYPE_FLARE;
	group->orders[0].throttle = 3;
	strcpy(group->cargo, "Spare parts");
	strcpy(group->special_cargo, "Secret plans");
	group->special_cargo_craft = 1;
	spawn_mission_ready();
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	unsigned int signature = g_next_object_signature;
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	XVT_ASSERT_INT_EQ(craft_slot(0, 0), 0);
	XVT_ASSERT_INT_EQ(craft_slot(0, 1), 1);
	for (int ordinal = 0; ordinal < 2; ++ordinal) {
		int slot = craft_slot(0, ordinal);
		const struct object_record *object = &g_object_table[slot];
		const struct craft_data *craft = object->mobj->p_craft;
		XVT_ASSERT_INT_EQ(object->object_type, SPAWN_TYPE);
		XVT_ASSERT_INT_EQ(g_spawn_object_type_by_object_slot[slot],
				  SPAWN_TYPE);
		XVT_ASSERT_INT_EQ(object->object_signature,
				  signature + (unsigned int)ordinal);
		XVT_ASSERT_INT_EQ(object->mobj->iff, 4);
		XVT_ASSERT_INT_EQ(object->mobj->team, 1);
		XVT_ASSERT_INT_EQ(object->genus_id,
				  g_object_type_table[SPAWN_TYPE].genus_id);
		XVT_ASSERT_INT_EQ(object->mobj->family,
				  g_object_type_table[SPAWN_TYPE].family_id);
		XVT_ASSERT_INT_EQ(object->mobj->node_switch_index, 3);
		XVT_ASSERT_INT_EQ(object->player_owner_idx, -1);
		XVT_ASSERT_INT_EQ(craft->hull_max, 500);
		XVT_ASSERT_INT_EQ(craft->system_damage_hull_threshold, 50);
		XVT_ASSERT_INT_EQ(craft->shield_energy[0], 600);
		XVT_ASSERT_INT_EQ(craft->shield_energy[1], 0);
		XVT_ASSERT_INT_EQ(craft->shield_distrib_mode,
				  SHIELD_DISTRIBUTION_FULLY_FORWARD);
		XVT_ASSERT_INT_EQ(craft->shield_recharge_level,
				  POWER_RECHARGE_MAINTENANCE);
		XVT_ASSERT_INT_EQ(craft->ai_flight.max_speed_cache, 400);
		XVT_ASSERT_INT_EQ(craft->ai_flight.turn_rate, 11);
		XVT_ASSERT_INT_EQ(craft->ai_flight.roll_rate, 12);
		XVT_ASSERT_INT_EQ(craft->ai_flight.pitch_rate, 13);
		XVT_ASSERT_INT_EQ(craft->laser_state.projectile_type_id[0], 7);
		XVT_ASSERT_INT_EQ(craft->laser_state.projectile_type_id[1], 8);
		XVT_ASSERT_INT_EQ(craft->laser_slot_count, 3);
		/* A cannon group counts unless it is a turret. */
		XVT_ASSERT_INT_EQ(craft->cannon_group_count, 1);
		XVT_ASSERT_INT_EQ(craft->weapon_slots[1].projectile_type_id, 7);
		XVT_ASSERT_INT_EQ(craft->weapon_slots[2].projectile_type_id, 7);
		XVT_ASSERT_INT_EQ(craft->weapon_slots[3].projectile_type_id, 2);
		XVT_ASSERT_INT_EQ(craft->warhead_launcher_count, 1);
		XVT_ASSERT_INT_EQ(craft->warhead_slot_type_ids[0],
				  g_warhead_type_ids[1]);
		unsigned int ammo = (unsigned int)math2_fraction(
			8, g_warhead_ammo_fraction_q16[1]);
		XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count,
				  ammo == 0 ? 1 : ammo);
		XVT_ASSERT_INT_EQ(craft->beam_type_id, BEAM_TYPE_JAMMING);
		XVT_ASSERT_INT_EQ(craft->beam_charge, 9999);
		XVT_ASSERT_INT_EQ(craft->cm_type_id, COUNTERMEASURE_TYPE_FLARE);
		XVT_ASSERT_INT_EQ(craft->cm_ammo_count,
				  (uint8_t)math2_fraction(12, 0xAAACu));
		XVT_ASSERT_INT_EQ(craft->ai_skill,
				  g_ai_skill_value_q16_by_level[3]);
		XVT_ASSERT_INT_EQ(craft->ai_controller.think_interval,
				  g_ai_think_interval_by_skill[3]);
		XVT_ASSERT_INT_EQ(craft->craft_ordinal, ordinal);
		XVT_ASSERT_INT_EQ(craft->craft_index_in_group, ordinal + 1);
		XVT_ASSERT_INT_EQ(craft->leader_obj_idx,
				  ordinal == 0 ? UINT8_MAX : craft_slot(0, 0));
		XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx,
				  UINT16_MAX);
		XVT_ASSERT_INT_EQ(craft->ai_controller.waypoint_index, 4);
		XVT_ASSERT_INT_EQ(craft->working_subsystems,
				  craft->system_flags);
		XVT_ASSERT_TRUE(strcmp((const char *)craft->special_cargo_name,
				       ordinal == 1 ? "Secret plans"
						    : "Spare parts") == 0);
		XVT_ASSERT_INT_EQ(craft->system_health[5], 100);
		XVT_ASSERT_INT_EQ(craft->component_hp[49], UINT8_MAX);
	}
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].spawned_craft_count, 2);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);
	XVT_ASSERT_INT_EQ(g_next_object_signature, signature + 2);
	XVT_ASSERT_INT_EQ(g_spawn_leader_obj_idx, 0);
	XVT_ASSERT_TRUE(
		line_holds("mission.craft_spawned fg=0 ordinal=1 ", "type=7"));
	*model = saved;
}

/* Spawns the first craft of a group of the given status (status1) and
 * returns its craft record; the group is group 0 of a mission of that group
 * alone. */
static const struct craft_data *spawn_with_status(int status1, int status2)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 1);
	g_stored.groups[0].warhead = 1;
	g_stored.groups[0].status1 = (uint8_t)status1;
	g_stored.groups[0].status2 = (uint8_t)status2;
	spawn_mission_ready();
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	int slot = craft_slot(0, 0);
	XVT_ASSERT_TRUE(slot >= 0);
	return g_object_table[slot].mobj->p_craft;
}

/* The status codes of a group change the craft it sends: 1 doubles and 2
 * halves its warheads (at least 1), 3 takes its shields away and turns them
 * off, 4 halves them and turns them off, 7 empties them, 12 and 18 double them
 * and 13 and 19 halve them, 6 flips the hyperdrive, and 9 and 16 keep a
 * hyperdrive the model lacks. Either of the two status fields works. */
static void check_spawn_status_codes(void)
{
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	model->warhead_launcher_capacity[0] = 8;
	const struct craft_data *craft = spawn_with_status(0, 0);
	int ammo = craft->weapon_slots[5].ammo_count;
	unsigned int system_flags = craft->system_flags;
	XVT_ASSERT_TRUE(ammo >= 1);
	XVT_ASSERT_TRUE((system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0);
	XVT_ASSERT_TRUE((system_flags & CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE) != 0);
	static const struct {
		int status;
		int ammo_factor; /* 2 doubled, 1 as it was, 0 halved */
		int shields;	 /* percent of the full forward shield */
		int shield_flag;
		int hyperdrive_flag;
	} cases[] = {
		{1, 2, 100, 1, 1}, {2, 0, 100, 1, 1},  {3, 1, 0, 0, 1},
		{4, 1, 50, 0, 1},  {7, 1, 0, 1, 1},    {12, 1, 200, 1, 1},
		{13, 1, 50, 1, 1}, {18, 1, 200, 1, 1}, {19, 1, 50, 1, 1},
		{6, 1, 100, 1, 0}, {9, 1, 100, 1, 1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		for (int field = 0; field < 2; ++field) {
			craft = spawn_with_status(field ? 0 : cases[i].status,
						  field ? cases[i].status : 0);
			int expected_ammo =
				cases[i].ammo_factor == 2
					? 2 * ammo
					: (cases[i].ammo_factor == 0
						   ? (ammo >> 1 ? ammo >> 1 : 1)
						   : ammo);
			XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count,
					  expected_ammo);
			XVT_ASSERT_INT_EQ(craft->shield_energy[0],
					  600 * cases[i].shields / 100);
			XVT_ASSERT_INT_EQ((craft->system_flags &
					   CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0,
					  cases[i].shield_flag);
			XVT_ASSERT_INT_EQ((craft->system_flags &
					   CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE) !=
						  0,
					  cases[i].hyperdrive_flag);
		}
	}
	/* A model without a hyperdrive loses the flag unless status 9 or 16
	 * keeps it; one without shields loses them unless status 8 or 16 keeps
	 * them. */
	model->has_hyperdrive = 0;
	model->has_shields = 0;
	craft = spawn_with_status(0, 0);
	XVT_ASSERT_INT_EQ(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE,
			  0);
	XVT_ASSERT_INT_EQ(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS,
			  0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 0);
	craft = spawn_with_status(9, 0);
	XVT_ASSERT_INT_EQ(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE,
			  CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE);
	craft = spawn_with_status(0, 8);
	XVT_ASSERT_INT_EQ(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS,
			  CRAFT_SUBSYSTEM_FLAG_SHIELDS);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 600);
	craft = spawn_with_status(16, 0);
	XVT_ASSERT_INT_EQ(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE,
			  CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE);
	XVT_ASSERT_INT_EQ(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS,
			  CRAFT_SUBSYSTEM_FLAG_SHIELDS);
	*model = saved;
}

/* A craft a player flies, once the spawn binds players, belongs to the
 * player: the object's owner, the player's object, signature and group, a
 * clean tally, the throttle, laser, shield and beam presets, a shield split
 * evenly with the model's strength in each bank, and linked lasers; the
 * warheads are held to 9 a launcher slot. */
static void check_spawn_player_craft(void)
{
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	model->warhead_launcher_capacity[0] = 40;
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 2);
	g_stored.groups[0].player_number = 1;
	g_stored.groups[0].player_craft = 1;
	g_stored.groups[0].warhead = 1;
	g_stored.groups[0].status1 = 1;
	spawn_mission_ready();
	struct player_data *player = &g_players[0];
	player->mission_stats.mission_score = 50;
	player->per_mission_kills.kills_full_on_flight_group[7] = 3;
	player->per_mission_kills.killed_by_ai_rating[5] = 3;
	player->per_mission_kills.losses_by_mines = 2;
	player->throttle_preset[0] = 5;
	g_initial_spawn_bind_player_craft_slots = 1;
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	int bound = craft_slot(0, 1);
	int other = craft_slot(0, 0);
	XVT_ASSERT_INT_EQ(g_players[0].object_index, bound);
	XVT_ASSERT_INT_EQ(g_object_table[bound].player_owner_idx, 0);
	XVT_ASSERT_INT_EQ(g_object_table[other].player_owner_idx, -1);
	XVT_ASSERT_INT_EQ(g_players[0].bound_object_signature,
			  g_object_table[bound].object_signature);
	XVT_ASSERT_INT_EQ(g_players[0].bound_flight_group_idx, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.mission_score, 0);
	XVT_ASSERT_INT_EQ(
		player->per_mission_kills.kills_full_on_flight_group[7], 0);
	XVT_ASSERT_INT_EQ(player->per_mission_kills.killed_by_ai_rating[5], 0);
	XVT_ASSERT_INT_EQ(player->per_mission_kills.losses_by_mines, 0);
	XVT_ASSERT_INT_EQ(player->throttle_preset[0], 21845);
	XVT_ASSERT_INT_EQ(player->throttle_preset[1], -1);
	XVT_ASSERT_INT_EQ(player->laser_preset[0], POWER_RECHARGE_MAINTENANCE);
	XVT_ASSERT_INT_EQ(player->laser_preset[1], POWER_RECHARGE_INCREASED);
	XVT_ASSERT_INT_EQ(player->shield_preset[1], POWER_RECHARGE_MAINTENANCE);
	XVT_ASSERT_INT_EQ(player->bound_craft_engine_glow_count, 2);
	const struct craft_data *craft = g_object_table[bound].mobj->p_craft;
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 300);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 300);
	XVT_ASSERT_INT_EQ(craft->shield_distrib_mode, SHIELD_DISTRIBUTION_EVEN);
	XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[0], 1);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 9);
	const struct craft_data *computer_craft =
		g_object_table[other].mobj->p_craft;
	XVT_ASSERT_INT_EQ(computer_craft->laser_state.link_mode[0], 0);
	XVT_ASSERT_INT_EQ(computer_craft->shield_energy[0], 600);
	XVT_ASSERT_INT_EQ(computer_craft->weapon_slots[5].ammo_count, 9);
	/* The group has a player, so the computer's craft is held to 9 too. */
	*model = saved;
}

/* The positions of the first n craft of group 0 after a spawn, as the
 * distance of each from the first. */
static double craft_distance(int from_slot, int to_slot)
{
	double dx = (double)g_object_table[to_slot].world_x -
		    g_object_table[from_slot].world_x;
	double dy = (double)g_object_table[to_slot].world_y -
		    g_object_table[from_slot].world_y;
	double dz = (double)g_object_table[to_slot].world_z -
		    g_object_table[from_slot].world_z;
	return sqrt(dx * dx + dy * dy + dz * dz);
}

/* The offset of the place a craft takes in its formation, in the order side,
 * forward and up: the place's offsets in model bounds, grown with the
 * formation spacing (by half a bound more at spacing 0). */
static void formation_offset(int formation, int ordinal, int spacing,
			     double *offset)
{
	int x = g_form_pos_x[formation][ordinal];
	int y = g_form_pos_y[formation][ordinal];
	int z = g_form_pos_z[formation][ordinal];
	int scale = spacing + 1;
	offset[0] = x * 40.0 * scale;
	offset[1] = y * 60.0 * scale;
	offset[2] = z * 20.0 * scale;
	if (scale == 1) {
		offset[0] += x * (40 / 2);
		offset[1] += y * (60 / 4);
		offset[2] += z * (20 / 2);
	}
}

/* Each craft of a group takes its place in the group's formation from the
 * group's first mission point (times 256, y negated): the place's offsets in
 * model bounds, grown with the formation spacing, divided by the formation's
 * divisor, and doubled for each step of the model's bound shift. Two craft are
 * as far apart as their places are; rotation keeps the distance. */
static void check_spawn_formation(void)
{
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	model->bound_size_x = 40;
	model->bound_size_y = 60;
	model->bound_size_z = 20;
	int divided = 0;
	while (g_formation_divisor[divided] == 1) {
		++divided;
	}
	static const struct {
		int formation;
		int spacing;
		int shift;
	} cases[] = {{0, 0, 0},	 {4, 1, 0}, {4, 0, 1}, {15, 2, 0}, {15, 0, 0},
		     {13, 0, 0}, {8, 1, 0}, {8, 0, 0}, {-1, 0, 0}};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		int formation = cases[i].formation;
		if (formation < 0) {
			formation = divided;
		}
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, SPAWN_TYPE, 6);
		g_stored.groups[0].formation = (uint8_t)formation;
		g_stored.groups[0].formation_spacing =
			(uint8_t)cases[i].spacing;
		g_stored.groups[0].mission_point_x[0] = 10;
		g_stored.groups[0].mission_point_y[0] = 20;
		g_stored.groups[0].mission_point_z[0] = 30;
		spawn_mission_ready();
		model->bound_size_shift = (uint8_t)cases[i].shift;
		XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
		int leader = craft_slot(0, 0);
		double first[3];
		formation_offset(formation, 0, cases[i].spacing, first);
		for (int ordinal = 1; ordinal < 6; ++ordinal) {
			double place[3];
			formation_offset(formation, ordinal, cases[i].spacing,
					 place);
			double dx = place[0] - first[0];
			double dy = place[1] - first[1];
			double dz = place[2] - first[2];
			double expected = sqrt(dx * dx + dy * dy + dz * dz) /
					  g_formation_divisor[formation] *
					  (1 << cases[i].shift);
			XVT_ASSERT_CLOSE(
				craft_distance(leader, craft_slot(0, ordinal)),
				expected, 0.08,
				"fixed-point trigonometry and whole-number division keep a distance within 8%");
		}
	}
	*model = saved;
}

/* A craft's plan and throttle come from the first order: the leader runs the
 * leader plan of the order and a follower the follower plan; every craft of a
 * group that arrives from hyperspace after the start runs the arrival plan
 * for it. A plan named nullpln, stationaryldrpln, stationaryflwpln or
 * disabledpln, for a craft no player flies, holds the throttle at 0;
 * escortldr1pln runs at full throttle (0x8000); any other takes the order's
 * throttle setting. */
static void check_spawn_plans(void)
{
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	model->max_speed = 1000;
	uint8_t follower_name = g_order_follower_builtin_plan_name_index[0];
	/* The radio announcement of a round that arrives after the start reads
	 * message templates; each is one pane-type byte and no text. */
	for (size_t message = 0;
	     message < sizeof g_str_in_flight_messages /
			       sizeof g_str_in_flight_messages[0];
	     ++message) {
		g_str_in_flight_messages[message] = "\006";
	}
	static const struct {
		const char *plan_name;
		int clock_started;
		int expected_throttle; /* -1: the order's setting */
		int follower_plan;
	} cases[] = {
		{"anyplan", 0, -1, 6},	       {"nullpln", 0, 0, 6},
		{"stationaryldrpln", 0, 0, 6}, {"stationaryflwpln", 0, 0, 6},
		{"disabledpln", 0, 0, 6},      {"escortldr1pln", 0, 0x8000, 6},
		{"anyplan", 1, -1, 9},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, SPAWN_TYPE, 2);
		g_stored.groups[0].orders[0].throttle = 2;
		spawn_mission_ready();
		g_order_leader_builtin_plan_name_index[0] = 3;
		memset(g_builtin_plan_id_by_name_index, 0,
		       sizeof g_builtin_plan_id_by_name_index);
		g_builtin_plan_id_by_name_index[3] = 5;
		g_builtin_plan_id_by_name_index[follower_name] = 6;
		g_builtin_plan_id_by_name_index[52] = 9;
		g_builtin_plan_id_by_name_index[50] = 8;
		memset(g_plan_table, 0, sizeof g_plan_table);
		strcpy(g_plan_table[5].name, cases[i].plan_name);
		g_mission_elapsed_clock.seconds =
			(uint8_t)cases[i].clock_started;
		XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
		const struct craft_data *leader =
			g_object_table[craft_slot(0, 0)].mobj->p_craft;
		const struct craft_data *follower =
			g_object_table[craft_slot(0, 1)].mobj->p_craft;
		int throttle =
			cases[i].expected_throttle >= 0
				? cases[i].expected_throttle
				: g_order_throttle_to_craft_throttle_speed[2];
		XVT_ASSERT_INT_EQ(leader->throttle_speed, throttle);
		XVT_ASSERT_INT_EQ(follower->throttle_speed, throttle);
		XVT_ASSERT_INT_EQ(leader->ai_controller.current_plan_id, 5);
		XVT_ASSERT_INT_EQ(leader->ai_controller.running_plan_id,
				  cases[i].clock_started ? 9 : 5);
		XVT_ASSERT_INT_EQ(follower->ai_controller.current_plan_id, 5);
		XVT_ASSERT_INT_EQ(follower->ai_controller.running_plan_id,
				  cases[i].follower_plan);
		XVT_ASSERT_INT_EQ(
			g_object_table[craft_slot(0, 0)].mobj->speed,
			(uint16_t)math2_fraction(1000, (uint16_t)throttle));
	}
	g_mission_elapsed_clock.seconds = 0;
	*model = saved;
}

/* In a version 14 file the craft of object types 37 and 38 are of the
 * transport genus; in another version they keep their genus. */
static void check_spawn_transport_genus(void)
{
	static const int types[2] = {CRAFT_SPECIES_MUURIAN_TRANSPORT,
				     CRAFT_SPECIES_CORELLIAN_TRANSPORT};
	for (int version = 13; version <= 14; ++version) {
		for (int i = 0; i < 2; ++i) {
			init_mission(MISSION_TYPE_COMBAT);
			arriving_group(0, 1, types[i], 1);
			init_world();
			g_flight_conf_no_pilot = 1;
			XVT_ASSERT_INT_EQ(run_init((uint16_t)version), 1);
			g_mission_fg_stats[0].current_mission_point_ref =
				0x8000;
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
			XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
			int slot = craft_slot(0, 0);
			XVT_ASSERT_INT_EQ(
				g_object_table[slot].object_type,
				g_craft_type_to_object_type[types[i]]);
			XVT_ASSERT_INT_EQ(
				g_object_table[slot].genus_id,
				version == 14
					? CRAFT_GENUS_TRANSPORT
					: g_object_type_table
						  [g_craft_type_to_object_type
							   [types[i]]]
							  .genus_id);
		}
	}
}

/* With the craft slots of the genus all taken the spawn makes no craft, warns
 * of the full table and returns 0 for the round. */
static void check_spawn_table_full(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 2);
	spawn_mission_ready();
	for (int slot = 0; slot < 32; ++slot) {
		g_object_table[slot].object_type = 9;
	}
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].spawned_craft_count, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  0);
	XVT_ASSERT_TRUE(line_holds("mission.object_table_full fg=0 ordinal=0 ",
				   "first_slot=0 end_slot=32"));
	XVT_ASSERT_INT_EQ(mission_init_flight_group_object_slot(), UINT16_MAX);
	/* The slot a craft left is the one the next craft takes. */
	g_object_table[5].object_type = 0;
	XVT_ASSERT_INT_EQ(spawn_craft(0, 1), 1);
	XVT_ASSERT_INT_EQ(craft_slot(0, 1), 5);
}

/* A starfighter (object types 1 to 5) gets no beam whatever its group says,
 * and a craft without a beam loses the beam system and its two HUD features; a
 * craft with none of the groups' countermeasures loses that system, and its
 * count is 0. */
static void check_spawn_beam_and_countermeasures(void)
{
	static const int types[2] = {CRAFT_SPECIES_X_WING, SPAWN_TYPE};
	struct model_def saved[2];
	for (int i = 0; i < 2; ++i) {
		saved[i] = *model_of(types[i]);
		model_of(types[i])->has_shields = 1;
	}
	for (int i = 0; i < 2; ++i) {
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, types[i], 1);
		g_stored.groups[0].beam = BEAM_TYPE_TRACTOR;
		g_stored.groups[0].countermeasures = COUNTERMEASURE_TYPE_NONE;
		spawn_mission_ready();
		XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
		const struct craft_data *craft =
			g_object_table[craft_slot(0, 0)].mobj->p_craft;
		XVT_ASSERT_INT_EQ(craft->beam_type_id,
				  i == 0 ? BEAM_TYPE_NONE : BEAM_TYPE_TRACTOR);
		XVT_ASSERT_INT_EQ((craft->system_flags &
				   CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0,
				  i == 1);
		XVT_ASSERT_INT_EQ(craft->beam_charge, i == 0 ? 0 : 9999);
		XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 0);
		XVT_ASSERT_INT_EQ(craft->system_flags &
					  CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES,
				  0);
		unsigned int hud_mask =
			craft->damage_stats.installed_hud_feature_mask;
		XVT_ASSERT_INT_EQ(hud_mask, i == 0 ? 0x1FFF ^ 0x1010 : 0x1FFF);
		XVT_ASSERT_INT_EQ(craft->damage_stats.active_hud_feature_mask,
				  hud_mask);
	}
	for (int i = 0; i < 2; ++i) {
		*model_of(types[i]) = saved[i];
	}
}

/* The weapons of a model with a laser group of the turret mount type (2) are
 * not counted as cannons; with none of the other kind the craft has no
 * cannons, and with no warhead launcher none of those either. */
static void check_spawn_without_cannons(void)
{
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	model->laser_group_mount_type[0] = 2;
	model->warhead_launcher_first_slot[0] = 5;
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 1);
	g_stored.groups[0].warhead = 0;
	spawn_mission_ready();
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	const struct craft_data *craft =
		g_object_table[craft_slot(0, 0)].mobj->p_craft;
	XVT_ASSERT_INT_EQ(craft->cannon_group_count, 0);
	XVT_ASSERT_INT_EQ(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_CANNONS,
			  0);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_count, 0);
	XVT_ASSERT_INT_EQ(
		craft->system_flags & CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[1].projectile_type_id, 2);
	*model = saved;
}

/* A craft that takes a slot the mission freed starts with a clean damage
 * tally: for the 10 teams' entries, and (as the comment says of the whole
 * tally) for the group entries too. */
static void check_spawn_clears_damage_tally(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 1);
	spawn_mission_ready();
	struct craft_data *stale = &g_craft_data_pool_base[0];
	for (int entry = 0; entry < 10; ++entry) {
		stale->damage_stats.damage_from_flight_group_amount[entry] = 7;
		stale->attacked_by_team[entry] = 1;
	}
	stale->damage_stats.damage_received_total = 70;
	stale->damage_stats.damage_from_player[3] = 5;
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	XVT_ASSERT_INT_EQ(craft_slot(0, 0), 0);
	for (int entry = 0; entry < 10; ++entry) {
		XVT_ASSERT_INT_EQ(
			stale->damage_stats
				.damage_from_flight_group_amount[entry],
			0);
		XVT_ASSERT_INT_EQ(stale->attacked_by_team[entry], 0);
	}
	XVT_ASSERT_INT_EQ(stale->damage_stats.damage_received_total, 0);
	XVT_ASSERT_INT_EQ(stale->damage_stats.damage_from_player[3], 0);
}

/* Known failure damage_from_groups_kept, issue #42: every entry of the damage
 * tally per flight group, all 48, is cleared when a craft takes a slot, not
 * only the first 10. */
static void check_spawn_clears_all_group_damage(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 1);
	spawn_mission_ready();
	struct craft_data *stale = &g_craft_data_pool_base[0];
	for (int entry = 0; entry < 48; ++entry) {
		stale->damage_stats.damage_from_flight_group_amount[entry] = 7;
	}
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	for (int entry = 0; entry < 48; ++entry) {
		XVT_ASSERT_INT_EQ(
			stale->damage_stats
				.damage_from_flight_group_amount[entry],
			0);
	}
}

/* Known failure seventh_craft_off_formation, issue #138: a group of more than
 * six craft places its seventh at a place of its own, not at the first place
 * of the next formation. Here the seventh craft of a group in formation 4 must
 * not stand where the first craft of another group in formation 5 stands,
 * which is the place formation 4's table reads past its end. */
static void check_spawn_seventh_craft_place(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 7);
	g_stored.groups[0].formation = 4;
	arriving_group(1, 1, SPAWN_TYPE, 1);
	g_stored.groups[1].formation = 5;
	spawn_mission_ready();
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	model->bound_size_x = 40;
	model->bound_size_y = 60;
	model->bound_size_z = 20;
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	XVT_ASSERT_INT_EQ(spawn_craft(1, UINT16_MAX), 1);
	const struct object_record *seventh = &g_object_table[craft_slot(0, 6)];
	const struct object_record *other = &g_object_table[craft_slot(1, 0)];
	XVT_ASSERT_TRUE(seventh->world_x != other->world_x ||
			seventh->world_y != other->world_y ||
			seventh->world_z != other->world_z);
	*model = saved;
}

/* The craft of a group in a global unit are numbered across the unit's
 * groups; a group that turns the numbering off, or has no unit, numbers its
 * own from 1. */
static void check_spawn_global_unit_numbers(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	for (int fg = 0; fg < 4; ++fg) {
		arriving_group(fg, 1, SPAWN_TYPE, 2);
	}
	g_stored.groups[0].global_unit = 3;
	g_stored.groups[1].global_unit = 3;
	g_stored.groups[2].global_unit = 3;
	g_stored.groups[2].disable_wave_numbering = 1;
	spawn_mission_ready();
	g_flight_mission_state.global_unit_craft_count[3] = 0;
	for (int fg = 0; fg < 4; ++fg) {
		XVT_ASSERT_INT_EQ(spawn_craft(fg, UINT16_MAX), 1);
	}
	static const int numbers[4][2] = {{1, 2}, {3, 4}, {1, 2}, {1, 2}};
	for (int fg = 0; fg < 4; ++fg) {
		for (int ordinal = 0; ordinal < 2; ++ordinal) {
			XVT_ASSERT_INT_EQ(
				g_object_table[craft_slot(fg, ordinal)]
					.mobj->p_craft->craft_index_in_group,
				numbers[fg][ordinal]);
		}
	}
	XVT_ASSERT_INT_EQ(g_flight_mission_state.global_unit_craft_count[3], 4);
}

/* The missile boat's second launcher carries the advanced missile (type 149),
 * loaded by the share of the fifth warhead, whatever warhead the group
 * chose for the first. */
static void check_spawn_missile_boat_launchers(void)
{
	struct model_def saved;
	struct model_def *model = model_of(CRAFT_SPECIES_MISSILE_BOAT);
	saved = *model;
	model->warhead_launcher_first_slot[0] = 2;
	model->warhead_launcher_last_slot[0] = 2;
	model->warhead_launcher_capacity[0] = 8;
	model->warhead_launcher_first_slot[1] = 4;
	model->warhead_launcher_last_slot[1] = 5;
	model->warhead_launcher_capacity[1] = 6;
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, CRAFT_SPECIES_MISSILE_BOAT, 1);
	g_stored.groups[0].warhead = 1;
	spawn_mission_ready();
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	const struct craft_data *craft =
		g_object_table[craft_slot(0, 0)].mobj->p_craft;
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_count, 2);
	XVT_ASSERT_INT_EQ(craft->warhead_slot_type_ids[0],
			  g_warhead_type_ids[1]);
	XVT_ASSERT_INT_EQ(craft->warhead_slot_type_ids[1], 149);
	unsigned int first =
		(unsigned int)math2_fraction(8, g_warhead_ammo_fraction_q16[1]);
	unsigned int second =
		(unsigned int)math2_fraction(6, g_warhead_ammo_fraction_q16[5]);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[2].ammo_count, first ? first : 1);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[4].ammo_count,
			  second ? second : 1);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count,
			  second ? second : 1);
	XVT_ASSERT_TRUE(
		line_holds("mission.craft_warheads ",
			   "launcher=1 type=149 first_slot=4 last_slot=5"));
	*model = saved;
}

/* A launcher's ammunition is at least 1, also after the status code halves
 * it. */
static void check_spawn_least_ammo(void)
{
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	model->warhead_launcher_capacity[0] = 0;
	const struct craft_data *craft = spawn_with_status(0, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 1);
	craft = spawn_with_status(2, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 1);
	craft = spawn_with_status(1, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 2);
	*model = saved;
}

/* The status codes 5 and 14 knock out the laser turrets and guns of a craft:
 * those meshes get no hit points and the disabled state 4; the others keep
 * theirs. */
static void check_spawn_turrets_off(void)
{
	struct model_mesh_object_type_cache saved =
		g_object_type_mesh_cache[SPAWN_TYPE];
	struct model_mesh_object_type_cache *cache =
		&g_object_type_mesh_cache[SPAWN_TYPE];
	/* Without a loaded model file no mesh can be damaged. */
	uint8_t saved_flags = g_object_type_table[SPAWN_TYPE].asset_flags;
	g_object_type_table[SPAWN_TYPE].asset_flags &= (uint8_t)~1;
	cache->mesh_count = 4;
	cache->mesh_types[0] = MESH_COMPONENT_04_LASR_TUR;
	cache->mesh_types[1] = MESH_COMPONENT_21_ROTATING_LASR_TUR;
	cache->mesh_types[2] = MESH_COMPONENT_05_LASR_GUN;
	cache->mesh_types[3] = 1;
	static const int statuses[3] = {0, 5, 14};
	for (int i = 0; i < 3; ++i) {
		const struct craft_data *craft =
			spawn_with_status(statuses[i], 0);
		for (int mesh = 0; mesh < 4; ++mesh) {
			int off = i != 0 && mesh < 3;
			XVT_ASSERT_INT_EQ(craft->component_hp[mesh],
					  off ? 0 : UINT8_MAX);
			XVT_ASSERT_INT_EQ(craft->component_state[mesh],
					  off ? 4 : 0);
		}
	}
	g_object_type_mesh_cache[SPAWN_TYPE] = saved;
	g_object_type_table[SPAWN_TYPE].asset_flags = saved_flags;
}

/* A platform with a beam has the components its beam cannot work through
 * disabled: 6 for a tractor beam, 12 for another, from the platform's 12
 * entries of the table of component numbers; one without a beam has none. */
static void check_spawn_platform_beams(void)
{
	static const int beams[3] = {BEAM_TYPE_NONE, BEAM_TYPE_TRACTOR,
				     BEAM_TYPE_JAMMING};
	int all_disabled = 0;
	for (int platform = 0; platform < 5; platform += 2) {
		for (int i = 0; i < 3; ++i) {
			init_mission(MISSION_TYPE_COMBAT);
			arriving_group(
				0, 1, CRAFT_SPECIES_XQ1_PLATFORM + platform, 1);
			g_stored.groups[0].beam = (uint8_t)beams[i];
			spawn_mission_ready();
			XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
			const struct craft_data *craft =
				g_object_table[craft_slot(0, 0)].mobj->p_craft;
			int disabled[50] = {0};
			int count =
				beams[i] == BEAM_TYPE_NONE
					? 0
					: (beams[i] == BEAM_TYPE_TRACTOR ? 6
									 : 12);
			for (int entry = 0; entry < count; ++entry) {
				uint8_t component =
					g_platform_beam_disabled_component_ids
						[platform * 12 + entry];
				if (component != UINT8_MAX) {
					disabled[component] = 1;
				}
			}
			for (int component = 0; component < 50; ++component) {
				XVT_ASSERT_INT_EQ(
					craft->component_hp[component],
					disabled[component] ? 0 : UINT8_MAX);
				XVT_ASSERT_INT_EQ(
					craft->component_state[component],
					disabled[component] ? 4 : 0);
				all_disabled += disabled[component];
			}
		}
	}
	XVT_ASSERT_TRUE(all_disabled > 0);
}

/* ---- mission_spawn_flight_group_static_objects and
 * mission_spawn_prepared_object ------------------------------------------- */

/* A mission of one group of craft_type (a static type), its first mission
 * point at x 10, y 20, z 30 and its yaw, pitch and roll bytes 11, 12 and 13,
 * after mission_init. */
static void static_mission(int craft_type, int count, int status1)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, craft_type, count);
	struct xvt_flight_group *group = &g_stored.groups[0];
	group->status1 = (uint8_t)status1;
	group->mission_point_x[0] = 10;
	group->mission_point_y[0] = 20;
	group->mission_point_z[0] = 30;
	group->yaw = 11;
	group->pitch = 12;
	group->roll = 13;
	spawn_mission_ready();
	g_current_flight_group_idx = 0;
}

/* The static slots in use, in order. */
static int static_objects(int *slots)
{
	int count = 0;
	for (int slot = 488; slot < 552; ++slot) {
		if (g_object_table[slot].object_type != 0) {
			slots[count++] = slot;
		}
	}
	return count;
}

/* A group of mines fills a square grid of its craft count a side, 64 mission
 * units apart, centered on the group's first mission point, in the plane its
 * status picks (0 x and y, 1 y and z, 2 x and z, 3 one point); each mine is
 * placed at 256 world units a mission unit, y negated, with the group's yaw,
 * pitch and roll bytes shifted up 8 bits. A mine's second type byte is 29
 * times its place in the group, modulo 8. A request for one craft places only
 * that one, and the group's total stops the grid. */
static void check_static_mine_grid(void)
{
	static const struct {
		int status;
		int x_step;
		int y_step;
		int z_row_step;
		int z_column_step;
	} planes[4] = {{0, 64, 64, 0, 0},
		       {1, 0, 64, 0, 64},
		       {6, 64, 0, 64, 0},
		       {3, 0, 0, 0, 0}};
	for (int plane = 0; plane < 4; ++plane) {
		static_mission(CRAFT_SPECIES_MINE_TYPE_A, 3,
			       planes[plane].status);
		XVT_ASSERT_INT_EQ(
			g_object_type_table[g_craft_type_to_object_type
						    [CRAFT_SPECIES_MINE_TYPE_A]]
				.genus_id,
			CRAFT_GENUS_MINE);
		mission_spawn_flight_group_static_objects(UINT16_MAX);
		int slots[64];
		XVT_ASSERT_INT_EQ(static_objects(slots), 9);
		for (int n = 0; n < 9; ++n) {
			int row = n / 3;
			int column = n % 3;
			const struct object_record *mine =
				&g_object_table[slots[n]];
			int x = 10 - planes[plane].x_step +
				column * planes[plane].x_step;
			int y = -(20 + planes[plane].y_step) +
				row * planes[plane].y_step;
			int z = 30 - planes[plane].z_row_step -
				planes[plane].z_column_step +
				row * planes[plane].z_row_step +
				column * planes[plane].z_column_step;
			XVT_ASSERT_INT_EQ(mine->world_x, x * 256);
			XVT_ASSERT_INT_EQ(mine->world_y, y * 256);
			XVT_ASSERT_INT_EQ(mine->world_z, z * 256);
			XVT_ASSERT_INT_EQ(mine->yaw, 11 << 8);
			XVT_ASSERT_INT_EQ(mine->pitch, 12 << 8);
			XVT_ASSERT_INT_EQ(mine->roll, 13 << 8);
			XVT_ASSERT_INT_EQ(mine->genus_id, CRAFT_GENUS_MINE);
			XVT_ASSERT_INT_EQ(mine->object_type,
					  g_craft_type_to_object_type
						  [CRAFT_SPECIES_MINE_TYPE_A]);
			XVT_ASSERT_INT_EQ(mine->flight_group_idx, 0);
			XVT_ASSERT_INT_EQ(mine->type_specific_word, 1023);
			XVT_ASSERT_INT_EQ(mine->type_specific_byte[0], 0);
			XVT_ASSERT_INT_EQ(mine->type_specific_byte[1],
					  29 * (n & 7));
		}
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			9);
		XVT_ASSERT_INT_EQ(g_object_table[slots[8]].object_signature,
				  g_object_table[slots[0]].object_signature +
					  8);
	}
	/* One craft of the grid, the fifth: the middle. */
	static_mission(CRAFT_SPECIES_MINE_TYPE_A, 3, 0);
	mission_spawn_flight_group_static_objects(4);
	int slots[64];
	XVT_ASSERT_INT_EQ(static_objects(slots), 1);
	XVT_ASSERT_INT_EQ(g_object_table[slots[0]].world_x, 10 * 256);
	XVT_ASSERT_INT_EQ(g_object_table[slots[0]].world_y, -20 * 256);
	/* A total of 4 stops the grid after 4 mines. */
	static_mission(CRAFT_SPECIES_MINE_TYPE_A, 3, 0);
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	XVT_ASSERT_INT_EQ(static_objects(slots), 4);
	/* A group with no craft to come, or of a craft that is not static, is
	 * left alone. */
	static_mission(CRAFT_SPECIES_MINE_TYPE_A, 3, 0);
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 0;
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	XVT_ASSERT_INT_EQ(static_objects(slots), 0);
	static_mission(SPAWN_TYPE, 3, 0);
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	XVT_ASSERT_INT_EQ(static_objects(slots), 0);
}

/* A satellite is one object at the group's first mission point. */
static void check_static_satellite(void)
{
	static_mission(CRAFT_SPECIES_COMM_SAT_1, 1, 0);
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	int slots[64];
	XVT_ASSERT_INT_EQ(static_objects(slots), 1);
	const struct object_record *satellite = &g_object_table[slots[0]];
	XVT_ASSERT_INT_EQ(satellite->world_x, 10 * 256);
	XVT_ASSERT_INT_EQ(satellite->world_y, -20 * 256);
	XVT_ASSERT_INT_EQ(satellite->world_z, 30 * 256);
	XVT_ASSERT_INT_EQ(satellite->yaw, 11 << 8);
	XVT_ASSERT_INT_EQ(satellite->pitch, 12 << 8);
	XVT_ASSERT_INT_EQ(satellite->roll, 13 << 8);
	XVT_ASSERT_INT_EQ(satellite->genus_id, CRAFT_GENUS_SATELLITE);
	XVT_ASSERT_INT_EQ(satellite->type_specific_byte[1], 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  1);
	XVT_ASSERT_TRUE(
		line_holds("mission.static_placed fg=0 ", "genus=9 type=70"));
}

/* Records the debris objects: their types and positions. */
static int debris_field(int *types, int *positions)
{
	int slots[64];
	int count = static_objects(slots);
	for (int i = 0; i < count; ++i) {
		types[i] = g_object_table[slots[i]].object_type;
		positions[3 * i] = g_object_table[slots[i]].world_x;
		positions[3 * i + 1] = g_object_table[slots[i]].world_y;
		positions[3 * i + 2] = g_object_table[slots[i]].world_z;
	}
	return count;
}

/* The first spot a debris field draws from a seed: the mission unit
 * position of its first piece, as mission_spawn_flight_group_static_objects
 * draws it for a group whose first mission point is at x 10, y 20, z 30. */
static void first_debris_spot(uint16_t seed, int *spot)
{
	int16_t saved = g_game_rand_feedback_state;
	g_game_rand_feedback_state = (int16_t)seed;
	spot[0] = (game_rand() & 0x1FF) + 10 - 256;
	spot[1] = (game_rand() & 0x1FF) - 20 - 256;
	spot[2] = (game_rand() & 0x1FF) + 30 - 256;
	g_game_rand_feedback_state = saved;
}

/* A debris field is the group's craft count of objects of types 100 to 105,
 * each within 256 mission units of the first mission point in every axis,
 * drawn from the asteroid field's seed, which it advances; the same seed
 * makes the same field, another a different one, and the game's random state
 * is put back. */
static void check_static_debris_field(void)
{
	int types[2][8];
	int positions[2][24];
	int counts[2];
	uint16_t seeds[3] = {0x2345, 0x2345, 0x5432};
	uint16_t after[3];
	for (int run = 0; run < 3; ++run) {
		static_mission(CRAFT_SPECIES_ASTEROID, 5, 0);
		g_asteroid_field_rand_seed = seeds[run];
		g_game_rand_feedback_state = 0x1234;
		mission_spawn_flight_group_static_objects(UINT16_MAX);
		XVT_ASSERT_INT_EQ(g_game_rand_feedback_state, 0x1234);
		after[run] = g_asteroid_field_rand_seed;
		XVT_ASSERT_TRUE(after[run] != seeds[run]);
		int local_types[8];
		int local_positions[24];
		int count = debris_field(local_types, local_positions);
		XVT_ASSERT_INT_EQ(count, 5);
		int spot[3];
		first_debris_spot(seeds[run], spot);
		XVT_ASSERT_INT_EQ(local_positions[0], spot[0] * 256);
		XVT_ASSERT_INT_EQ(local_positions[1], spot[1] * 256);
		XVT_ASSERT_INT_EQ(local_positions[2], spot[2] * 256);
		int slots[64];
		XVT_ASSERT_INT_EQ(static_objects(slots), 5);
		for (int piece = 0; piece < 5; ++piece) {
			XVT_ASSERT_INT_EQ(g_object_table[slots[piece]].yaw, 0);
			XVT_ASSERT_INT_EQ(g_object_table[slots[piece]].pitch,
					  0);
			XVT_ASSERT_INT_EQ(g_object_table[slots[piece]].roll, 0);
		}
		for (int i = 0; i < count; ++i) {
			XVT_ASSERT_TRUE(local_types[i] >= 100 &&
					local_types[i] <= 105);
			XVT_ASSERT_TRUE(
				local_positions[3 * i] >= (10 - 256) * 256 &&
				local_positions[3 * i] < (10 + 256) * 256);
			XVT_ASSERT_TRUE(local_positions[3 * i + 1] >=
						(-20 - 256) * 256 &&
					local_positions[3 * i + 1] <
						(-20 + 256) * 256);
			XVT_ASSERT_TRUE(local_positions[3 * i + 2] >=
						(30 - 256) * 256 &&
					local_positions[3 * i + 2] <
						(30 + 256) * 256);
		}
		if (run < 2) {
			counts[run] = count;
			memcpy(types[run], local_types, sizeof local_types);
			memcpy(positions[run], local_positions,
			       sizeof local_positions);
		} else {
			XVT_ASSERT_TRUE(memcmp(types[0], local_types,
					       sizeof local_types) != 0 ||
					memcmp(positions[0], local_positions,
					       sizeof local_positions) != 0);
		}
	}
	XVT_ASSERT_INT_EQ(counts[0], counts[1]);
	XVT_ASSERT_INT_EQ(memcmp(types[0], types[1], 5 * sizeof(int)), 0);
	XVT_ASSERT_INT_EQ(memcmp(positions[0], positions[1], 15 * sizeof(int)),
			  0);
	XVT_ASSERT_INT_EQ(after[0], after[1]);
	XVT_ASSERT_TRUE(
		line_holds("mission.debris_field fg=0 count=5 ", "x=10"));
}

/* With every static slot taken a static object is not made: the slot count
 * of the group's arrived craft does not grow and the table is warned of. */
static void check_static_table_full(void)
{
	static_mission(CRAFT_SPECIES_COMM_SAT_1, 1, 0);
	for (int slot = 488; slot < 552; ++slot) {
		g_object_table[slot].object_type = 9;
	}
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  0);
	XVT_ASSERT_TRUE(line_holds("mission.static_table_full fg=0 ",
				   "genus=9 type=70"));
	XVT_ASSERT_INT_EQ(
		mission_spawn_prepared_object(0, CRAFT_GENUS_SATELLITE, 70),
		UINT16_MAX);
}

/* ---- mission_sync_pilot_network_players_to_session_slots -------------- */

/* Each player in the session roster whose DirectPlay id a network player of
 * the pilot record holds gets that id in the network record of the player at
 * its roster slot; a roster player the record does not hold is warned of. An id
 * of 0 matches nothing. The function returns 1. */
static void check_sync_pilot_network_players(void)
{
	fresh_world();
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(&g_net_session, 0, sizeof g_net_session);
	g_log_line_count = 0;
	g_net_session.player_count = 4;
	g_net_session.players[0].direct_play_id = 11;
	g_net_session.players[1].direct_play_id = 22;
	g_net_session.players[2].direct_play_id = 33;
	g_net_session.players[3].direct_play_id = 0;
	g_pilot_data.network_players[0].direct_play_id = 22;
	g_pilot_data.network_players[2].direct_play_id = 33;
	g_pilot_data.network_players[3].direct_play_id = 99;
	XVT_ASSERT_INT_EQ(mission_sync_pilot_network_players_to_session_slots(),
			  1);
	XVT_ASSERT_INT_EQ(g_players[0].network.direct_play_id, 0);
	XVT_ASSERT_INT_EQ(g_players[1].network.direct_play_id, 22);
	XVT_ASSERT_INT_EQ(g_players[2].network.direct_play_id, 33);
	XVT_ASSERT_INT_EQ(g_players[3].network.direct_play_id, 0);
	XVT_ASSERT_INT_EQ(count_lines("mission.session_player_bound"), 2);
	XVT_ASSERT_TRUE(line_holds("mission.session_player_bound entry=0 ",
				   "player=22"));
	XVT_ASSERT_TRUE(line_holds("mission.session_player_bound entry=2 ",
				   "player=33"));
	XVT_ASSERT_INT_EQ(count_lines("mission.session_player_unmatched"), 2);
	XVT_ASSERT_TRUE(line_holds(
		"mission.session_player_unmatched player=11 ", "players=4"));
}

/* A group that arrives from a mothership's hangar after the start keeps no
 * formation spacing and its followers run the arrival plan of the hangar; the
 * same group with no mothership in the flight is not spawned, and the spawn
 * says so. */
static void check_spawn_from_mothership(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 2);
	g_stored.groups[0].arrival_method = 1;
	g_stored.groups[0].arrival_mothership = 1;
	g_stored.groups[0].formation_spacing = 4;
	arriving_group(1, 1, SPAWN_TYPE, 1);
	uint8_t follower_name = g_order_follower_builtin_plan_name_index[0];
	for (int mothership_present = 0; mothership_present < 2;
	     ++mothership_present) {
		spawn_mission_ready();
		for (size_t message = 0;
		     message < sizeof g_str_in_flight_messages /
				       sizeof g_str_in_flight_messages[0];
		     ++message) {
			g_str_in_flight_messages[message] = "\006";
		}
		memset(g_builtin_plan_id_by_name_index, 0,
		       sizeof g_builtin_plan_id_by_name_index);
		g_builtin_plan_id_by_name_index[follower_name] = 6;
		g_builtin_plan_id_by_name_index[50] = 8;
		g_log_line_count = 0;
		if (mothership_present) {
			XVT_ASSERT_INT_EQ(spawn_craft(1, UINT16_MAX), 1);
		}
		g_mission_elapsed_clock.seconds = 1;
		int16_t result = spawn_craft(0, UINT16_MAX);
		g_mission_elapsed_clock.seconds = 0;
		if (!mothership_present) {
			XVT_ASSERT_INT_EQ(result, 0);
			XVT_ASSERT_INT_EQ(craft_slot(0, 0), -1);
			XVT_ASSERT_TRUE(
				line_holds("mission.mothership_missing fg=0 ",
					   "mothership=1"));
			continue;
		}
		XVT_ASSERT_INT_EQ(result, 1);
		const struct craft_data *leader =
			g_object_table[craft_slot(0, 0)].mobj->p_craft;
		const struct craft_data *follower =
			g_object_table[craft_slot(0, 1)].mobj->p_craft;
		XVT_ASSERT_INT_EQ(leader->ai_flight.separation, 0);
		XVT_ASSERT_INT_EQ(follower->ai_flight.separation, 0);
		XVT_ASSERT_INT_EQ(follower->ai_controller.running_plan_id, 8);
		XVT_ASSERT_TRUE(line_holds("mission.wave_prepared fg=0 ",
					   "mothership=1 hyperspace=0"));
	}
}

/* A craft's object takes damage_amount from its type's largest box size:
 * 4 times the size, or the size itself once that is 0x2000 or more; type 58
 * first counts the size 8 times. */
static void check_spawn_damage_amount(void)
{
	static const struct {
		int craft_type;
		int extent;
		int expected;
	} cases[] = {
		{SPAWN_TYPE, 1000, 4000},
		{SPAWN_TYPE, 0x2000, 0x2000},
		{58, 100, 3200},
		{58, 0x500, 0x2800},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		int object_type =
			g_craft_type_to_object_type[cases[i].craft_type];
		XVT_ASSERT_INT_EQ(object_type, cases[i].craft_type);
		int saved = g_object_type_table[object_type].max_bounds_extent;
		g_object_type_table[object_type].max_bounds_extent =
			cases[i].extent;
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, cases[i].craft_type, 1);
		spawn_mission_ready();
		XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
		XVT_ASSERT_INT_EQ(
			g_object_table[craft_slot(0, 0)].mobj->damage_amount,
			cases[i].expected);
		g_object_type_table[object_type].max_bounds_extent = saved;
	}
}

/* A static group whose craft is of a genus that is neither mines, a
 * satellite nor debris (the pilot's, 11) places nothing. */
static void check_static_other_genus(void)
{
	static_mission(CRAFT_SPECIES_PILOT, 1, 0);
	int object_type = g_craft_type_to_object_type[CRAFT_SPECIES_PILOT];
	XVT_ASSERT_INT_EQ(g_object_type_table[object_type].genus_id, 11);
	XVT_ASSERT_TRUE(
		(g_object_type_table[object_type].behavior_flags & 0x80) != 0);
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	int slots[64];
	XVT_ASSERT_INT_EQ(static_objects(slots), 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  0);
}

/* A debris field made with an object already standing in a static slot (here
 * at the coordinates of the first piece's spot in mission units) has the
 * object and all its pieces, of types 100 to 105, and no piece at the
 * object's coordinates. */
static void check_static_debris_beside_object(void)
{
	static_mission(CRAFT_SPECIES_ASTEROID, 3, 0);
	g_asteroid_field_rand_seed = 0x2345;
	int spot[3];
	first_debris_spot(0x2345, spot);
	g_object_table[488].object_type = 9;
	g_object_table[488].world_x = spot[0];
	g_object_table[488].world_y = spot[1];
	g_object_table[488].world_z = spot[2];
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	int slots[64];
	XVT_ASSERT_INT_EQ(static_objects(slots), 4);
	XVT_ASSERT_INT_EQ(slots[0], 488);
	for (int i = 1; i < 4; ++i) {
		const struct object_record *piece = &g_object_table[slots[i]];
		XVT_ASSERT_TRUE(piece->object_type >= 100 &&
				piece->object_type <= 105);
		XVT_ASSERT_TRUE(piece->world_x != spot[0] ||
				piece->world_y != spot[1] ||
				piece->world_z != spot[2]);
	}
}

/* Known failure debris_spot_taken, issue #37: a debris piece is not put on the
 * spot of an object already there; a new spot is drawn. The object stands at
 * the first piece's spot as the game stores positions, 256 times the mission
 * unit position. */
static void check_static_debris_redraws_taken_spot(void)
{
	static_mission(CRAFT_SPECIES_ASTEROID, 3, 0);
	g_asteroid_field_rand_seed = 0x2345;
	int spot[3];
	first_debris_spot(0x2345, spot);
	g_object_table[488].object_type = 9;
	g_object_table[488].world_x = spot[0] * 256;
	g_object_table[488].world_y = spot[1] * 256;
	g_object_table[488].world_z = spot[2] * 256;
	mission_spawn_flight_group_static_objects(UINT16_MAX);
	int slots[64];
	int count = static_objects(slots);
	XVT_ASSERT_INT_EQ(count, 4);
	for (int i = 1; i < count; ++i) {
		const struct object_record *piece = &g_object_table[slots[i]];
		XVT_ASSERT_TRUE(piece->world_x != spot[0] * 256 ||
				piece->world_y != spot[1] * 256 ||
				piece->world_z != spot[2] * 256);
	}
}

/* 1 when all size bytes at memory are value. */
static int bytes_are(const void *memory, size_t size, uint8_t value)
{
	const uint8_t *bytes = memory;
	for (size_t i = 0; i < size; ++i) {
		if (bytes[i] != value) {
			return 0;
		}
	}
	return 1;
}

/* A craft that takes a slot starts with fresh state whatever the slot held:
 * its motion and AI state, damage and mission accounting, weapon tallies and
 * beam, and the object's own position, speed and attitude; the player who
 * flies it starts with a clean score and tally. The craft keeps the group's
 * formation spacing. */
static void check_spawn_fresh_state(void)
{
	struct model_def saved;
	struct model_def *model = set_test_model(&saved);
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 2);
	g_stored.groups[0].player_number = 1;
	g_stored.groups[0].formation_spacing = 4;
	spawn_mission_ready();
	g_object_table[1].yaw = 0x5A5A;
	g_object_table[1].pitch = 0x5A5A;
	g_object_table[1].roll = 0x5A5A;
	struct craft_data *craft = &g_craft_data_pool_base[0];
	struct object_record *object = &g_object_table[0];
	memset(craft, 0x5A, sizeof *craft);
	memset(object->mobj, 0x5A, sizeof *object->mobj);
	object->mobj->p_craft = craft;
	object->yaw = 0x5A5A;
	object->pitch = 0x5A5A;
	object->roll = 0x5A5A;
	memset(object->type_specific_byte, 0x5A,
	       sizeof object->type_specific_byte);
	struct player_data *player = &g_players[0];
	memset(&player->mission_stats, 0x5A, sizeof player->mission_stats);
	memset(&player->per_mission_kills, 0x5A,
	       sizeof player->per_mission_kills);
	player->warheads_fired = 0x5A5A;
	g_initial_spawn_bind_player_craft_slots = 1;
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	XVT_ASSERT_TRUE(craft_slot(0, 0) == 0);

	XVT_ASSERT_INT_EQ(g_object_table[1].yaw, 0);
	XVT_ASSERT_INT_EQ(g_object_table[1].pitch, 0x4000);
	XVT_ASSERT_INT_EQ(g_object_table[1].roll, 0);
	XVT_ASSERT_INT_EQ(object->yaw, 0);
	XVT_ASSERT_INT_EQ(object->pitch, 0x4000);
	XVT_ASSERT_INT_EQ(object->roll, 0);
	XVT_ASSERT_INT_EQ(object->mobj->roll_impulse_rate, 0);
	XVT_ASSERT_INT_EQ(object->mobj->speed_remainder, 0);
	XVT_ASSERT_INT_EQ(object->mobj->seconds_alive, 0);
	XVT_ASSERT_TRUE(bytes_are(object->type_specific_byte,
				  sizeof object->type_specific_byte, 0));
	XVT_ASSERT_INT_EQ(craft->yaw, 0);
	XVT_ASSERT_INT_EQ(craft->pitch, 0x4000);
	const struct ai_flight_state *flight = &craft->ai_flight;
	XVT_ASSERT_INT_EQ(flight->roll_state, 0);
	XVT_ASSERT_INT_EQ(flight->pitch_state, 0);
	XVT_ASSERT_INT_EQ(flight->turn_state, 0);
	XVT_ASSERT_INT_EQ(flight->climb_state, 0);
	XVT_ASSERT_INT_EQ(flight->dive_state, 0);
	XVT_ASSERT_INT_EQ(flight->pitch_through_loop, 0);
	XVT_ASSERT_INT_EQ(flight->motion_scale, -1);
	XVT_ASSERT_INT_EQ(flight->roll_accel, -1);
	XVT_ASSERT_INT_EQ(flight->pitch_accel, -1);
	XVT_ASSERT_INT_EQ(flight->turn_accel, -1);
	XVT_ASSERT_INT_EQ(flight->separation, 4);
	XVT_ASSERT_INT_EQ(flight->go_home_flag, 0);
	XVT_ASSERT_INT_EQ(flight->mission_aborted_flag, 0);
	XVT_ASSERT_INT_EQ(flight->depart_timer_flag, 0);
	XVT_ASSERT_INT_EQ(flight->depart_clock_hours, 0);
	XVT_ASSERT_INT_EQ(flight->depart_clock_minutes, 0);
	XVT_ASSERT_INT_EQ(flight->depart_clock_seconds, 0);
	XVT_ASSERT_INT_EQ(flight->hits_this_maneuver, 0);
	XVT_ASSERT_INT_EQ(flight->boarded_accounting_done, 0);
	XVT_ASSERT_INT_EQ(flight->times_boarded, 0);
	XVT_ASSERT_INT_EQ(flight->docking_accounting_done, 0);
	XVT_ASSERT_INT_EQ(flight->docked_target_count, 0);
	XVT_ASSERT_INT_EQ(flight->impact_obj_idx, UINT16_MAX);
	const struct ai_controller *controller = &craft->ai_controller;
	XVT_ASSERT_INT_EQ(controller->target_signature, 0);
	XVT_ASSERT_INT_EQ(controller->has_live_target, 0);
	XVT_ASSERT_INT_EQ(controller->target_component, UINT16_MAX);
	XVT_ASSERT_INT_EQ(controller->escort_target_fg, UINT8_MAX);
	XVT_ASSERT_INT_EQ(controller->aim_point_x, 0);
	XVT_ASSERT_INT_EQ(controller->aim_point_y, 0);
	XVT_ASSERT_INT_EQ(controller->aim_point_z, 0);
	XVT_ASSERT_INT_EQ(controller->target_z_angle, 0x4000);
	XVT_ASSERT_INT_EQ(controller->target_roll, 0);
	XVT_ASSERT_INT_EQ(controller->target_xy_angle, 0);
	XVT_ASSERT_INT_EQ(controller->saved_plan_id, 0);
	XVT_ASSERT_INT_EQ(controller->maneuver_phase, 0);
	XVT_ASSERT_INT_EQ(controller->maneuver_timer, 0);
	XVT_ASSERT_INT_EQ(controller->current_order_slot, 0);
	XVT_ASSERT_INT_EQ(controller->skipped_to_order4, 0);
	for (int goal = 0; goal < 4; ++goal) {
		XVT_ASSERT_INT_EQ(
			controller->order_progress.completion_state[goal], 0);
		XVT_ASSERT_INT_EQ(
			controller->order_progress.goal_progress[goal], 0);
	}
	XVT_ASSERT_INT_EQ(craft->weapon_stats.laser_hits_scored, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.laser_shots_fired, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.ion_hits_scored, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.ion_shots_fired, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.warhead_hits_scored, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.warheads_fired, 0);
	XVT_ASSERT_INT_EQ(craft->warhead_lock_ticks, 0);
	XVT_ASSERT_INT_EQ(craft->hull_damage, 0);
	XVT_ASSERT_INT_EQ(craft->subsystem_damage, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_fire_inhibit_timer, 0);
	XVT_ASSERT_INT_EQ(craft->mission_accounting_done, 0);
	XVT_ASSERT_INT_EQ(craft->unused_mission_flag, 0);
	XVT_ASSERT_INT_EQ(craft->not_disabled_accounting_suppress, 0);
	XVT_ASSERT_INT_EQ(craft->captured_by_flight_group, 0);
	XVT_ASSERT_INT_EQ(craft->s_foil_state, 0);
	XVT_ASSERT_INT_EQ(craft->carried_object_index, UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->carrier_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->beam_active, 0);
	XVT_ASSERT_INT_EQ(craft->beam_output, 0);
	XVT_ASSERT_INT_EQ(craft->beam_target_obj_idx, -1);
	for (int effect = 0; effect < 5; ++effect) {
		XVT_ASSERT_INT_EQ(craft->beam_effect_accum[effect], 0);
	}
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 0);
	XVT_ASSERT_INT_EQ(craft->cm_fire_cooldown_timer, 0);
	XVT_ASSERT_INT_EQ(craft->player_command_avoid_target_obj_idx,
			  UINT16_MAX);
	for (int team = 0; team < 10; ++team) {
		XVT_ASSERT_INT_EQ(craft->identified_order_by_team[team],
				  team == 1);
	}
	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		XVT_ASSERT_INT_EQ(craft->system_display_slot_by_system[system],
				  system);
		XVT_ASSERT_INT_EQ(craft->system_health[system], 100);
		XVT_ASSERT_INT_EQ(craft->system_repair_seconds[system], 0);
	}
	const struct craft_damage_stats *damage = &craft->damage_stats;
	XVT_ASSERT_INT_EQ(damage->last_system_hit_time, 0);
	XVT_ASSERT_INT_EQ(damage->damage_received_total, 0);
	XVT_ASSERT_INT_EQ(damage->damage_received_by_player_owned_craft, 0);
	XVT_ASSERT_INT_EQ(damage->damage_from_collision, 0);
	XVT_ASSERT_INT_EQ(damage->damage_from_starship, 0);
	XVT_ASSERT_INT_EQ(damage->damage_from_mine, 0);
	for (int slot = 0; slot < 8; ++slot) {
		XVT_ASSERT_INT_EQ(damage->damage_from_player[slot], 0);
	}
	for (int slot = 0; slot < 6; ++slot) {
		XVT_ASSERT_INT_EQ(damage->damage_from_ai_skill[slot], 0);
	}
	/* The bound player's tallies are clean. */
	XVT_ASSERT_TRUE(bytes_are(&player->mission_stats.mission_score,
				  sizeof player->mission_stats.mission_score,
				  0));
	XVT_ASSERT_INT_EQ(player->mission_stats.laser_hits_scored, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.laser_shots_fired, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.ion_hits_scored, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.ion_shots_fired, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.rating_promo_points, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.worse_rating_promo_points, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.field_0c, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.field_10, 0);
	XVT_ASSERT_INT_EQ(player->mission_stats.primary_goal_finish_place, 0);
	XVT_ASSERT_INT_EQ(player->warheads_fired, 0);
	XVT_ASSERT_TRUE(bytes_are(&player->per_mission_kills,
				  sizeof player->per_mission_kills, 0));
	*model = saved;
}

/* Known failure craft_type_past_table, issue #187: the craft type of a flight
 * group is checked against the 96 entries of the table of object types before
 * it is used to index it. mission_init either refuses a mission with a craft
 * type of 100 or brings the type into the table. */
static void check_init_craft_type_in_range(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	init_group(0, 0, 100, 0);
	init_world();
	uint16_t result = run_init(14);
	XVT_ASSERT_TRUE(result == 0 ||
			g_mission_flight_groups[0].fg.craft_type < 96);
}

/* Known failure ai_skill_past_table, issue #187: the skill value of a spawned
 * craft is an entry of the 8-entry skill table whatever the group's AI level:
 * a group at level 9 takes the skill of the highest level the start allows
 * (5), not a value read past the table. */
static void check_spawn_ai_skill_in_table(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 1);
	g_stored.groups[0].group_ai = 9;
	spawn_mission_ready();
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	const struct craft_data *craft =
		g_object_table[craft_slot(0, 0)].mobj->p_craft;
	XVT_ASSERT_INT_EQ(craft->ai_skill, g_ai_skill_value_q16_by_level[5]);
}

/* ---- mission_update_logic ---------------------------------------------- */

/* The template of every in-flight message: pane type 3 (the system pane,
 * which the checks leave switched off, so a message is built and dropped) and
 * one character. Messages 196 and 207 take the text a caller adds, so the
 * length in the message's log line tells which text was sent. */
static const char g_plain_template[] = "\003-";
static const char g_text_template[] = "\003*";

/* A mission of combat type with the two groups of fresh_world, both able to
 * arrive, no goals, no messages and no player, every in-flight message
 * template in place, and both of the timers run out except the message scan,
 * which waits. */
static void logic_world(void)
{
	fresh_world();
	g_log_line_count = 0;
	memset(&g_flight_global_countdown_timers, 0,
	       sizeof g_flight_global_countdown_timers);
	g_flight_global_countdown_timers.mission_message_scan_timer = 1;
	memset(g_mission_messages, 0, sizeof g_mission_messages);
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(&g_game_config, 0, sizeof g_game_config);
	memset(&g_net_session, 0, sizeof g_net_session);
	memset(g_msg_arg_table, 0, sizeof g_msg_arg_table);
	g_system_message_display_enabled = 0;
	g_replay_view_mode = 0;
	g_flight_player_count = 1;
	g_game_time = 4000;
	g_msg_sender_iff = 99;
	g_pending_hud_message_voice_sfx_id = 0;
	for (size_t i = 0; i < sizeof g_str_in_flight_messages /
				       sizeof g_str_in_flight_messages[0];
	     ++i) {
		g_str_in_flight_messages[i] = g_plain_template;
	}
	g_str_in_flight_messages[IFMSG_196_CODE_02_ARGUMENT] = g_text_template;
	g_str_in_flight_messages[IFMSG_207_CODE_01_ARGUMENT] = g_text_template;
	g_mission_header.mission_type = MISSION_TYPE_COMBAT;
	for (int fg = 0; fg < 2; ++fg) {
		g_mission_fg_stats[fg].arrival_enabled = 1;
	}
}

/* Runs the goals pass: the evaluation timer has run out. */
static void run_logic(void)
{
	g_flight_global_countdown_timers.mission_goal_evaluation_timer = 0;
	mission_update_logic();
}

static void set_elapsed(int hours, int minutes, int seconds)
{
	g_mission_elapsed_clock.hours = (uint8_t)hours;
	g_mission_elapsed_clock.minutes = (uint8_t)minutes;
	g_mission_elapsed_clock.seconds = (uint8_t)seconds;
}

/* Makes the local player (slot 7) a connected player of the given team and
 * IFF. */
static void connect_local_player(int team, int iff)
{
	g_players[g_local_player].participation_state = 1;
	g_players[g_local_player].team = (uint8_t)team;
	g_players[g_local_player].iff = (uint8_t)iff;
}

/* Goal `slot` of flight group fg: its kind (0 primary, 1 prevent, 2 bonus),
 * event, amount and points, for the teams in the bit set; every team's state
 * of it is pending. */
static struct flight_group_goal *set_fg_goal(int fg, int slot, int kind,
					     int condition, int amount,
					     int points, unsigned int teams)
{
	struct flight_group_goal *goal =
		&g_mission_flight_groups[fg].fg.goals[slot];
	memset(goal, 0, sizeof *goal);
	goal->goal_kind = (uint8_t)kind;
	goal->event_condition = (uint8_t)condition;
	goal->amount = (mission_goal_amount)amount;
	goal->points = (int8_t)points;
	for (int team = 0; team < 10; ++team) {
		goal->enabled_teams[team] = (teams >> team) & 1u;
		g_mission_fg_stats[fg].goal_state[8 * team + slot] = 4;
	}
	return goal;
}

/* Flight group fg has two craft: the given number destroyed and the given
 * number gone from the region. All destroyed meets the condition destroyed,
 * all of them; one gone from the region has failed it; neither leaves it
 * undecided. */
static void group_craft(int fg, int destroyed, int left)
{
	uint16_t *counts = g_mission_fg_stats[fg].outcome_count;
	memset(counts, 0, sizeof g_mission_fg_stats[fg].outcome_count);
	counts[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	counts[FLIGHT_GROUP_OUTCOME_DESTROYED] = (uint16_t)destroyed;
	counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] = (uint16_t)left;
}

static int team_goal_state(int fg, int team, int slot)
{
	return g_mission_fg_stats[fg].goal_state[8 * team + slot];
}

static int bonus_score(int team)
{
	return g_flight_mission_state.runtime
		.team_scores[TEAM_SCORE_BONUS][team];
}

static int goal_status(int team, int kind)
{
	return g_flight_mission_state.runtime.team_goal_status[team][kind];
}

/* mission_update_logic does nothing in proving grounds mode, however far the
 * timers have run out. */
static void check_logic_proving_grounds(void)
{
	logic_world();
	g_flight_mission_state.proving_grounds_mode_active = 1;
	g_players[0].participation_state = 1;
	run_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.connected_player_count, 0);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_goal_evaluation_timer,
		0);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_message_scan_timer, 1);
}

/* The goals pass counts the connected players, raises the peak (and logs the
 * new peak) only when the count passes it, and sets the evaluation timer to
 * 236 ticks. While the timer runs and no mission end is pending, the pass
 * does not run; a pending mission end runs it whatever the timer says. */
static void check_logic_connected_players(void)
{
	logic_world();
	g_players[0].participation_state = 1;
	g_players[2].participation_state = 2;
	g_players[5].participation_state = 1;
	run_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.connected_player_count, 3);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.max_connected_player_count_this_mission,
		3);
	XVT_ASSERT_TRUE(
		line_holds("mission.player_peak", "players=3 tick=4000"));
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_goal_evaluation_timer,
		SIMULATION_TICKS_PER_SECOND);

	/* Fewer players: the count follows, the peak stays and logs nothing. */
	g_players[2].participation_state = 0;
	g_players[5].participation_state = 0;
	g_log_line_count = 0;
	run_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.connected_player_count, 1);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.max_connected_player_count_this_mission,
		3);
	XVT_ASSERT_INT_EQ(count_lines("mission.player_peak"), 0);

	/* A running timer keeps the count as it was. */
	g_players[3].participation_state = 1;
	g_players[4].participation_state = 1;
	g_flight_global_countdown_timers.mission_goal_evaluation_timer = 5;
	mission_update_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.connected_player_count, 1);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_goal_evaluation_timer,
		5);

	/* A pending mission end runs the pass: 3 connected is not above the
	 * peak of 3. */
	g_flight_mission_state.mission_end_pending = 1;
	g_log_line_count = 0;
	mission_update_logic();
	XVT_ASSERT_INT_EQ(count_lines("mission.player_peak"), 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.connected_player_count, 3);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_goal_evaluation_timer,
		SIMULATION_TICKS_PER_SECOND);
	g_players[6].participation_state = 1;
	mission_update_logic();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.max_connected_player_count_this_mission,
		4);
}

/* A pending goal of a flight group, for a team it is enabled for, is decided
 * by its event on the group: met pays 250 times its points to the team's
 * bonus score and is state 1; failed is state 2 and pays nothing; undecided
 * stays pending and says nothing. A team the goal is not enabled for has
 * state 0. A goal already decided is not looked at again. */
static void check_logic_fg_goal_decided(void)
{
	logic_world();
	/* Prevent goals, whose status adds nothing to the score; goal 4 is of
	 * a kind no status counts. */
	set_fg_goal(0, 0, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, 4, 0x3);
	set_fg_goal(0, 1, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, -2, 0x2);
	set_fg_goal(0, 2, 1, MISSION_COND_DEPARTED, GOAL_AMT_100, 1, 0x1);
	set_fg_goal(0, 4, 3, MISSION_COND_DESTROYED, GOAL_AMT_100, 3, 0x8);

	group_craft(0, 0, 0);
	run_logic();
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 0), 4);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 1, 1), 4);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 2, 0), 0);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 1), 0);
	XVT_ASSERT_INT_EQ(count_lines("mission.fg_goal_decided"), 0);

	group_craft(0, 2, 0);
	run_logic();
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 0), 1);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 1, 0), 1);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 1, 1), 1);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 2, 0), 0);
	/* Team 0: 4 points of goal 0; team 1: 4 of goal 0 less 2 of goal 1. */
	XVT_ASSERT_INT_EQ(bonus_score(0), 1000);
	XVT_ASSERT_INT_EQ(bonus_score(1), 500);
	XVT_ASSERT_INT_EQ(bonus_score(2), 0);
	XVT_ASSERT_INT_EQ(bonus_score(3), 750);
	XVT_ASSERT_TRUE(line_holds(
		"mission.fg_goal_decided fg=0 goal=0 team=0",
		"kind=\"prevent\" state=\"met\" points=1000 limit=0 seconds=0 tick=4000"));
	XVT_ASSERT_TRUE(
		line_holds("mission.fg_goal_decided fg=0 goal=1 team=1",
			   "kind=\"prevent\" state=\"met\" points=-500"));
	XVT_ASSERT_TRUE(line_holds("mission.fg_goal_decided fg=0 goal=4 team=3",
				   "kind=\"other\" state=\"met\" points=750"));

	/* Decided goals stay as they are, and pay once. */
	group_craft(0, 0, 1);
	run_logic();
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 0), 1);
	XVT_ASSERT_INT_EQ(bonus_score(0), 1000);

	/* One craft left the region: "all destroyed" has failed. */
	set_fg_goal(0, 3, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, 9, 0x4);
	run_logic();
	XVT_ASSERT_INT_EQ(team_goal_state(0, 2, 3), 2);
	XVT_ASSERT_INT_EQ(bonus_score(2), 0);
	XVT_ASSERT_TRUE(
		line_holds("mission.fg_goal_decided fg=0 goal=3 team=2",
			   "kind=\"prevent\" state=\"failed\" points=0"));
}

/* An event of always true or never is not a goal anyone can meet: its state
 * is 0 and it pays nothing. */
static void check_logic_fg_goal_fixed_events(void)
{
	logic_world();
	set_fg_goal(0, 0, 2, MISSION_COND_ALWAYS_TRUE, GOAL_AMT_100, 5, 0x1);
	set_fg_goal(0, 1, 2, MISSION_COND_NEVER, GOAL_AMT_100, 5, 0x1);
	run_logic();
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 0), 0);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 1), 0);
	XVT_ASSERT_INT_EQ(bonus_score(0), 0);
	XVT_ASSERT_INT_EQ(count_lines("mission.fg_goal_decided"), 0);
}

/* A goal with a time limit of 2 five-second units is met at 10 seconds
 * elapsed and fails at 11. */
static void check_logic_fg_goal_time_limit(void)
{
	static const struct {
		int seconds;
		int state;
		int score;
	} rows[] = {{10, 1, 750}, {11, 2, 0}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		struct flight_group_goal *goal = set_fg_goal(
			0, 0, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, 3, 0x1);
		goal->time_limit5s = 2;
		group_craft(0, 2, 0);
		set_elapsed(0, 0, rows[i].seconds);
		run_logic();
		XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 0), rows[i].state);
		XVT_ASSERT_INT_EQ(bonus_score(0), rows[i].score);
	}
	XVT_ASSERT_TRUE(
		line_holds("mission.fg_goal_decided fg=0 goal=0 team=0",
			   "state=\"failed\" points=0 limit=10 seconds=11"));
}

/* A group that cannot arrive and no player owns has no goal states at all; a
 * group a player owns keeps its goals though it cannot arrive. */
static void check_logic_fg_goal_unavailable_group(void)
{
	logic_world();
	set_fg_goal(0, 0, 2, MISSION_COND_DESTROYED, GOAL_AMT_100, 2, 0x1);
	set_fg_goal(1, 0, 2, MISSION_COND_ALWAYS_FAILED, GOAL_AMT_100, 2, 0x1);
	group_craft(0, 2, 0);
	g_mission_fg_stats[0].arrival_enabled = 0;
	g_mission_fg_stats[1].arrival_enabled = 0;
	g_mission_flight_groups[1].player_owner_idx = 3;
	run_logic();
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 0), 0);
	XVT_ASSERT_INT_EQ(bonus_score(0), 0);
	XVT_ASSERT_INT_EQ(team_goal_state(1, 0, 0), 2);
}

/* A per-craft bonus goal (amount 18 or 19) that is met stays state 8 and pays
 * nothing here; the same amounts on a primary goal pay as any goal. */
static void check_logic_fg_goal_per_craft(void)
{
	logic_world();
	/* The primary goal complete is met for every team at once. */
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	set_fg_goal(0, 0, 2, MISSION_COND_PRIMARY_GOAL_COMPLETE, 18, 4, 0x1);
	set_fg_goal(0, 1, 2, MISSION_COND_PRIMARY_GOAL_COMPLETE, 19, 4, 0x1);
	set_fg_goal(0, 2, 0, MISSION_COND_PRIMARY_GOAL_COMPLETE, 18, 4, 0x1);
	set_fg_goal(0, 3, 2, MISSION_COND_PRIMARY_GOAL_COMPLETE, 0, 4, 0x1);
	run_logic();
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 0), 8);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 1), 8);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 2), 1);
	XVT_ASSERT_INT_EQ(team_goal_state(0, 0, 3), 1);
	/* 2,000 for the two goals, 10,000 for the primary goal they complete
	 * and 2,500 for the bonus goal. */
	XVT_ASSERT_INT_EQ(bonus_score(0), 14500);
	XVT_ASSERT_TRUE(line_holds("mission.fg_goal_decided fg=0 goal=0 team=0",
				   "state=\"met_per_craft\" points=0"));
}

/* The conditions that stand in for a trigger result in the global goal checks:
 * 1 met (the primary goal complete, which holds once the global flag is
 * set), 2 failed (the primary goal failed), 4 undecided (the bonus goal
 * complete, which is open), 0 never, and NO_TRIGGER for the second trigger of
 * a pair that has none. */
enum { NO_TRIGGER = -1 };

static void set_trigger(struct mission_trigger *trigger, int result)
{
	memset(trigger, 0, sizeof *trigger);
	switch (result) {
	case 1:
		trigger->condition = MISSION_COND_PRIMARY_GOAL_COMPLETE;
		break;
	case 2:
		trigger->condition = MISSION_COND_PRIMARY_GOAL_FAILED;
		break;
	case 4:
		trigger->condition = MISSION_COND_BONUS_GOAL_COMPLETE;
		break;
	case 0:
		trigger->condition = MISSION_COND_NEVER;
		break;
	default:
		trigger->condition = MISSION_COND_NO_CONDITION;
		break;
	}
}

/* Global goal `kind` of team `team`: each pair is its two triggers' results
 * (see set_trigger) and 1 to join them with OR, and `join` is 1 to join the
 * pairs with OR; the goal pays `points`. */
static struct global_goal *set_global_goal(int team, int kind, int first,
					   int second, int or1, int third,
					   int fourth, int or2, int join,
					   int points)
{
	struct global_goal *goal = &g_mission_global_goals[team][kind];
	memset(goal, 0, sizeof *goal);
	set_trigger(&goal->trigger_pairs[0].triggers[0], first);
	set_trigger(&goal->trigger_pairs[0].triggers[1], second);
	goal->trigger_pairs[0].trigger1_or_trigger2 = (uint8_t)or1;
	set_trigger(&goal->trigger_pairs[1].triggers[0], third);
	set_trigger(&goal->trigger_pairs[1].triggers[1], fourth);
	goal->trigger_pairs[1].trigger1_or_trigger2 = (uint8_t)or2;
	goal->trigger_pair1_or_trigger_pair2 = (uint8_t)join;
	goal->raw_points = (int8_t)points;
	return goal;
}

/* The state of team 3's prevent goal after a pass over the stated goal. */
static int global_state_of(int first, int second, int or1, int third,
			   int fourth, int or2, int join)
{
	logic_world();
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	set_global_goal(3, 1, first, second, or1, third, fourth, or2, join, 0);
	run_logic();
	return g_flight_mission_state.runtime.team_global_goal_state[3][1];
}

/* A pair of two triggers joined by OR is met when either is met and has
 * failed when both have; joined by AND it is met when both are and has failed
 * when either has; otherwise it is undecided (4). A pair whose second trigger
 * is no condition takes its first trigger's result. Never counts as neither
 * met nor failed. The global goal's state is its pairs' results joined the
 * same way. The rows are (first result, second result, joined by OR, result);
 * each pair is tried as the goal's first and second pair beside a pair that
 * is met, and the same rows join two pairs of one result each. */
static void check_logic_global_goal_pairs(void)
{
	static const struct {
		int first;
		int second;
		int or_joined;
		int state;
	} rows[] = {
		{1, 1, 0, 1}, {1, 2, 0, 2}, {1, 4, 0, 4}, {2, 1, 0, 2},
		{2, 2, 0, 2}, {2, 4, 0, 2}, {4, 1, 0, 4}, {4, 2, 0, 2},
		{4, 4, 0, 4}, {1, 1, 1, 1}, {1, 2, 1, 1}, {1, 4, 1, 1},
		{2, 1, 1, 1}, {2, 2, 1, 2}, {2, 4, 1, 4}, {4, 1, 1, 1},
		{4, 2, 1, 4}, {4, 4, 1, 4}, {0, 1, 0, 4}, {0, 2, 0, 2},
		{0, 1, 1, 1}, {0, 2, 1, 4}, {0, 0, 1, 4},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		XVT_ASSERT_INT_EQ(global_state_of(rows[i].first, rows[i].second,
						  rows[i].or_joined, 1, 1, 0,
						  0),
				  rows[i].state);
		XVT_ASSERT_INT_EQ(global_state_of(1, 1, 0, rows[i].first,
						  rows[i].second,
						  rows[i].or_joined, 0),
				  rows[i].state);
		if (i < 18) {
			XVT_ASSERT_INT_EQ(
				global_state_of(rows[i].first, rows[i].first, 0,
						rows[i].second, rows[i].second,
						0, rows[i].or_joined),
				rows[i].state);
		}
	}
	static const int singles[][2] = {{1, 1}, {2, 2}, {4, 4}, {0, 4}};
	for (size_t i = 0; i < sizeof singles / sizeof singles[0]; ++i) {
		XVT_ASSERT_INT_EQ(global_state_of(singles[i][0], NO_TRIGGER, 0,
						  1, 1, 0, 0),
				  singles[i][1]);
		/* The OR join of the first pair makes no difference. */
		XVT_ASSERT_INT_EQ(global_state_of(singles[i][0], NO_TRIGGER, 1,
						  1, 1, 0, 0),
				  singles[i][1]);
	}
}

/* A goal whose four conditions are all always true or never has no state
 * (0), whatever its pairs are joined by, and logs nothing; one condition that
 * is anything else gives it a state. */
static void check_logic_global_goal_fixed_conditions(void)
{
	logic_world();
	struct global_goal *goal = &g_mission_global_goals[2][0];
	memset(goal, 0, sizeof *goal);
	goal->trigger_pairs[0].triggers[1].condition = MISSION_COND_NEVER;
	goal->trigger_pairs[1].triggers[0].condition = MISSION_COND_NEVER;
	goal->trigger_pair1_or_trigger_pair2 = 1;
	goal->raw_points = 9;
	g_flight_mission_state.runtime.team_global_goal_state[2][0] = 4;
	run_logic();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_global_goal_state[2][0], 0);
	XVT_ASSERT_INT_EQ(bonus_score(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("mission.global_goal_state"), 0);
	goal->trigger_pairs[1].triggers[1].condition =
		MISSION_COND_ALWAYS_FAILED;
	run_logic();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_global_goal_state[2][0], 4);
}

/* The global goal's triggers leave the craft that met each and the craft
 * counted in global_goal_trigger_counts, rows 0 and 1, by team, kind and
 * trigger (1 to 4); a trigger that is no condition leaves 0. */
static void check_logic_global_goal_trigger_counts(void)
{
	logic_world();
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED] = 3;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 5;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED] = 1;
	struct global_goal *goal =
		set_global_goal(4, 2, 4, 4, 0, 4, 4, 0, 0, 0);
	for (int pair = 0; pair < 2; ++pair) {
		for (int trigger = 0; trigger < 2; ++trigger) {
			struct mission_trigger *t =
				&goal->trigger_pairs[pair].triggers[trigger];
			t->condition = MISSION_COND_DESTROYED;
			t->variable_type = GOAL_TARGET_FLIGHT_GROUP;
			t->variable = (uint8_t)trigger;
			t->amount = GOAL_AMT_100;
		}
	}
	/* Stale numbers from a pass before are cleared. */
	memset(g_flight_mission_state.runtime.global_goal_trigger_counts, 0xAB,
	       sizeof g_flight_mission_state.runtime
		       .global_goal_trigger_counts);
	run_logic();
	uint16_t (*counts)[10][3][4] =
		g_flight_mission_state.runtime.global_goal_trigger_counts;
	XVT_ASSERT_INT_EQ(counts[0][4][2][0], 3);
	XVT_ASSERT_INT_EQ(counts[1][4][2][0], 4);
	XVT_ASSERT_INT_EQ(counts[0][4][2][1], 1);
	XVT_ASSERT_INT_EQ(counts[1][4][2][1], 5);
	XVT_ASSERT_INT_EQ(counts[0][4][2][2], 3);
	XVT_ASSERT_INT_EQ(counts[1][4][2][2], 4);
	XVT_ASSERT_INT_EQ(counts[0][4][2][3], 1);
	XVT_ASSERT_INT_EQ(counts[1][4][2][3], 5);
	XVT_ASSERT_INT_EQ(counts[0][5][2][0], 0);
	XVT_ASSERT_INT_EQ(counts[1][4][1][0], 0);

	/* The first pair's second trigger is no condition: its counts are 0. */
	goal->trigger_pairs[0].triggers[1].condition =
		MISSION_COND_NO_CONDITION;
	run_logic();
	XVT_ASSERT_INT_EQ(counts[0][4][2][0], 3);
	XVT_ASSERT_INT_EQ(counts[0][4][2][1], 0);
	XVT_ASSERT_INT_EQ(counts[1][4][2][1], 0);
	XVT_ASSERT_INT_EQ(counts[1][4][2][3], 5);
}

/* The team filter of a pair whose second trigger is no condition with a team
 * variable limits the first trigger's craft to that team, in the first pair
 * of a global goal. Group 0's one captured-and-departed craft was taken by
 * team 5. */
static void check_logic_global_goal_team_filter(void)
{
	logic_world();
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	g_mission_fg_stats[0].team_captured_departed_count[5] = 1;
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	struct global_goal *goal =
		set_global_goal(0, 1, 1, NO_TRIGGER, 0, 1, 1, 0, 0, 0);
	struct mission_trigger *first = &goal->trigger_pairs[0].triggers[0];
	first->condition = MISSION_COND_CAPTURED_AND_DEPARTED;
	first->variable_type = GOAL_TARGET_FLIGHT_GROUP;
	first->variable = 0;
	first->amount = GOAL_AMT_AT_LEAST_1;
	struct mission_trigger *filter = &goal->trigger_pairs[0].triggers[1];
	filter->variable_type = GOAL_TARGET_TEAM;
	filter->variable = 4;
	run_logic();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_global_goal_state[0][1], 4);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .global_goal_trigger_counts[0][0][1][0],
			  0);
	filter->variable = 5;
	run_logic();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_global_goal_state[0][1], 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .global_goal_trigger_counts[0][0][1][0],
			  1);

	/* The second pair filters its first trigger the same way; only the
	 * craft counted of its third trigger are looked at here (the goal's
	 * state with this pair is the one of issue #34). */
	struct mission_trigger *third = &goal->trigger_pairs[1].triggers[0];
	*third = *first;
	struct mission_trigger *second_filter =
		&goal->trigger_pairs[1].triggers[1];
	set_trigger(second_filter, NO_TRIGGER);
	second_filter->variable_type = GOAL_TARGET_TEAM;
	second_filter->variable = 4;
	run_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .global_goal_trigger_counts[0][0][1][2],
			  0);
	second_filter->variable = 5;
	run_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .global_goal_trigger_counts[0][0][1][2],
			  1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .global_goal_trigger_counts[1][0][1][2],
			  2);
}

/* A goal pays 250 times its raw points when its state changes to met, once,
 * for a prevent goal as for the others; a goal that fails or stays undecided
 * pays nothing. The change is logged with the old and the new state, the craft
 * counts of the first trigger and the points. */
static void check_logic_global_goal_pays(void)
{
	logic_world();
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	set_global_goal(6, 1, 1, NO_TRIGGER, 0, 1, 1, 0, 0, 6);
	set_global_goal(6, 2, 2, NO_TRIGGER, 0, 1, 1, 0, 0, 6);
	set_global_goal(7, 1, 1, NO_TRIGGER, 0, 1, 1, 0, 0, -3);
	set_global_goal(8, 1, 4, NO_TRIGGER, 0, 1, 1, 0, 0, 5);
	run_logic();
	XVT_ASSERT_INT_EQ(bonus_score(6), 1500);
	XVT_ASSERT_INT_EQ(bonus_score(7), -750);
	XVT_ASSERT_INT_EQ(bonus_score(8), 0);
	XVT_ASSERT_TRUE(line_holds(
		"mission.global_goal_state team=6 kind=\"prevent\"",
		"was=0 now=1 met=0 total=0 points=1500 score=1500 tick=4000"));
	XVT_ASSERT_INT_EQ(
		count_lines(
			"mission.global_goal_state team=6 kind=\"prevent\""),
		1);
	XVT_ASSERT_INT_EQ(count_lines("mission.global_goal_state team=8"), 1);
	XVT_ASSERT_TRUE(
		line_holds("mission.global_goal_state team=8", "was=0 now=4 "));
	XVT_ASSERT_TRUE(line_holds(
		"mission.global_goal_state team=6 kind=\"bonus\"", "now=2 "));

	/* Run again: no change, no second payment, no second line. */
	g_log_line_count = 0;
	run_logic();
	XVT_ASSERT_INT_EQ(bonus_score(6), 1500);
	XVT_ASSERT_INT_EQ(count_lines("mission.global_goal_state"), 0);

	/* A bonus goal met also earns the 2,500 of the bonus status. */
	set_global_goal(9, 2, 1, NO_TRIGGER, 0, 1, 1, 0, 0, 2);
	run_logic();
	XVT_ASSERT_INT_EQ(bonus_score(9), 3000);
	XVT_ASSERT_INT_EQ(goal_status(9, 2), 1);
}

/* The log line of the last in-flight message of this id that was built, or
 * NULL. Only messages for the local player are built. */
static const char *built_message(int id)
{
	char start[48];
	snprintf(start, sizeof start, "msg.message_built message=%d ", id);
	return find_line(start);
}

static int built_count(int id)
{
	char start[48];
	snprintf(start, sizeof start, "msg.message_built message=%d ", id);
	return count_lines(start);
}

/* 1 when the last message of this id was built with these log fields. */
static int built_with(int id, const char *fields)
{
	const char *line = built_message(id);
	return line != NULL && strstr(line, fields) != NULL;
}

/* A primary goal of team `team`, in group `fg`, met when the group's craft
 * are destroyed; it pays `points`. */
static void primary_goal(int fg, int team, int points)
{
	set_fg_goal(fg, 0, 0, MISSION_COND_DESTROYED, GOAL_AMT_100, points,
		    1u << team);
}

/* The pass over a goal of a flight group decides the team's goal status. The
 * rows are the states of two goals of the kind (4 pending, 1 met, 2 failed,
 * 0 none). Primary and bonus goals are met when every goal is met and at
 * least one is, failed when any has failed, and open while one is pending. A
 * prevent goal is met when any one is, whatever the others are. */
static void check_logic_team_goal_status(void)
{
	static const struct {
		int kind;
		int first;
		int second;
		int status;
	} rows[] = {
		{0, 1, 1, 1}, {0, 1, 4, 0}, {0, 4, 4, 0}, {0, 1, 2, 2},
		{0, 2, 4, 2}, {0, 4, 2, 2}, {0, 2, 2, 2}, {0, 0, 1, 1},
		{0, 0, 0, 0}, {0, 8, 0, 0}, {2, 1, 1, 1}, {2, 4, 1, 0},
		{2, 2, 1, 2}, {2, 0, 0, 0}, {1, 1, 4, 1}, {1, 4, 1, 1},
		{1, 2, 1, 1}, {1, 2, 2, 0}, {1, 4, 4, 0}, {1, 0, 0, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		group_craft(0, 0, 0);
		set_fg_goal(0, 0, rows[i].kind, MISSION_COND_DESTROYED,
			    GOAL_AMT_100, 0, 0x1);
		set_fg_goal(0, 1, rows[i].kind, MISSION_COND_DESTROYED,
			    GOAL_AMT_100, 0, 0x1);
		g_mission_fg_stats[0].goal_state[0] = (uint8_t)rows[i].first;
		g_mission_fg_stats[0].goal_state[1] = (uint8_t)rows[i].second;
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(0, rows[i].kind), rows[i].status);
		XVT_ASSERT_INT_EQ(goal_status(1, rows[i].kind), 0);
	}
	/* The status of other kinds is not touched. */
	logic_world();
	set_fg_goal(0, 0, 0, MISSION_COND_DESTROYED, GOAL_AMT_100, 0, 0x1);
	g_mission_fg_stats[0].goal_state[0] = 1;
	run_logic();
	XVT_ASSERT_INT_EQ(goal_status(0, 0), 1);
	XVT_ASSERT_INT_EQ(goal_status(0, 1), 0);
	XVT_ASSERT_INT_EQ(goal_status(0, 2), 0);
}

/* A team that has met a goal keeps it, though the pass then finds the goal
 * failed; a team whose goal failed can still meet it, and the log says so.
 * Nothing changes while the goals are called unimportant. */
static void check_logic_team_goal_kept(void)
{
	logic_world();
	set_fg_goal(0, 0, 0, MISSION_COND_DESTROYED, GOAL_AMT_100, 0, 0x1);
	g_mission_fg_stats[0].goal_state[0] = 1;
	run_logic();
	XVT_ASSERT_INT_EQ(goal_status(0, 0), 1);
	g_mission_fg_stats[0].goal_state[0] = 2;
	run_logic();
	XVT_ASSERT_INT_EQ(goal_status(0, 0), 1);

	logic_world();
	set_fg_goal(0, 0, 0, MISSION_COND_DESTROYED, GOAL_AMT_100, 0, 0x1);
	g_mission_fg_stats[0].goal_state[0] = 2;
	run_logic();
	XVT_ASSERT_INT_EQ(goal_status(0, 0), 2);
	XVT_ASSERT_TRUE(line_holds(
		"mission.goal_failed team=0 kind=\"primary\"", "tick=4000"));
	XVT_ASSERT_TRUE(line_holds("battle.goal_failed team=0 kind=\"primary\"",
				   "tick=4000"));
	g_mission_fg_stats[0].goal_state[0] = 1;
	run_logic();
	XVT_ASSERT_INT_EQ(goal_status(0, 0), 1);
	XVT_ASSERT_TRUE(line_holds("mission.goal_met team=0 kind=\"primary\"",
				   "rank=1 was=\"failed\" tick=4000"));

	for (int type = MISSION_TYPE_COMBAT; type >= MISSION_TYPE_MELEE;
	     type -= 2) {
		logic_world();
		g_mission_header.mission_type = (int8_t)type;
		g_mission_header.goals_unimportant = 1;
		set_fg_goal(0, 0, 0, MISSION_COND_DESTROYED, GOAL_AMT_100, 0,
			    0x1);
		g_mission_fg_stats[0].goal_state[0] = 1;
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
					  .global_primary_goal_status,
				  0);
	}
}

/* A team's primary goal met: it is complete, the team's bonus score takes
 * 10,000, the completion time is the elapsed seconds, the any-team flag is
 * set, each player of the team gets finish place 1 (the count of teams that
 * have completed), and the local player is told: the message of excellent
 * work with the sender's IFF, then each end-of-mission text of the team that
 * is not empty, the first with voice 111 and the second with none, as code 1
 * to 6 (207) or other (196). */
static void check_logic_primary_met(void)
{
	logic_world();
	set_elapsed(1, 2, 3);
	connect_local_player(0, 2);
	g_players[1].participation_state = 1;
	g_players[1].team = 0;
	g_players[2].participation_state = 1;
	g_players[2].team = 1;
	strcpy(g_mission_teams[0].end_of_mission_messages[0], "Done");
	strcpy(g_mission_teams[0].end_of_mission_messages[1], "3 more");
	strcpy(g_mission_teams[0].end_of_mission_messages[2], "unused");
	primary_goal(0, 0, 2);
	group_craft(0, 2, 0);
	run_logic();
	XVT_ASSERT_INT_EQ(goal_status(0, 0), 1);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.global_primary_goal_status, 1);
	XVT_ASSERT_INT_EQ(bonus_score(0), 10500);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_mission_completion_time_seconds[0],
			  3723);
	XVT_ASSERT_INT_EQ(g_players[7].mission_stats.primary_goal_finish_place,
			  1);
	XVT_ASSERT_INT_EQ(g_players[1].mission_stats.primary_goal_finish_place,
			  1);
	XVT_ASSERT_INT_EQ(g_players[2].mission_stats.primary_goal_finish_place,
			  0);
	XVT_ASSERT_TRUE(line_holds("mission.goal_met team=0 kind=\"primary\"",
				   "rank=1 was=\"open\" tick=4000"));
	XVT_ASSERT_TRUE(line_holds("battle.goal_met team=0 kind=\"primary\"",
				   "rank=1 tick=4000"));
	XVT_ASSERT_TRUE(line_holds(
		"mission.primary_scored team=0",
		"hostile=0 points=10000 seconds=3723 score=10500 tick=4000"));
	XVT_ASSERT_TRUE(line_holds("mission.primary_rank slot=7",
				   "team=0 rank=1 victory=1 local=1"));
	XVT_ASSERT_TRUE(line_holds("mission.primary_rank slot=1",
				   "team=0 rank=1 victory=1 local=0"));
	XVT_ASSERT_INT_EQ(count_lines("mission.primary_rank"), 2);
	XVT_ASSERT_INT_EQ(built_count(193), 1);
	XVT_ASSERT_TRUE(built_with(193, "sender=2 "));
	XVT_ASSERT_INT_EQ(built_count(196), 1);
	XVT_ASSERT_TRUE(built_with(196, "length=4 sender=2 voice=111 "));
	XVT_ASSERT_INT_EQ(built_count(207), 1);
	XVT_ASSERT_TRUE(built_with(207, "length=6 sender=2 voice=0 "));
	XVT_ASSERT_INT_EQ(built_count(305), 0);
	XVT_ASSERT_INT_EQ(count_lines("battle.goal_failed"), 0);

	/* The completed goal is not counted again. */
	g_log_line_count = 0;
	run_logic();
	XVT_ASSERT_INT_EQ(bonus_score(0), 10500);
	XVT_ASSERT_INT_EQ(count_lines("mission.primary_scored"), 0);
}

/* A second team's primary goal met takes finish place 2. The 10,000 points are
 * not paid in a combat mission when a hostile team has completed already: a
 * team whose allies include this one is not hostile to it. Any other mission
 * type pays them whoever completed first. */
static void check_logic_primary_second_team(void)
{
	static const struct {
		int mission_type;
		int allied;
		int hostile;
		int points;
	} rows[] = {
		{MISSION_TYPE_COMBAT, 0, 1, 0},
		{MISSION_TYPE_COMBAT, 1, 0, 10000},
		{MISSION_TYPE_MELEE, 0, 0, 10000},
		{MISSION_TYPE_TRAINING, 0, 0, 10000},
		{MISSION_TYPE_SIMULATOR_2, 0, 0, 10000},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		g_mission_header.mission_type = (int8_t)rows[i].mission_type;
		g_mission_teams[0].allies[1] = (uint8_t)rows[i].allied;
		g_players[0].participation_state = 1;
		g_players[0].team = 0;
		g_players[3].participation_state = 1;
		g_players[3].team = 1;
		primary_goal(0, 0, 0);
		primary_goal(1, 1, 0);
		group_craft(0, 2, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(bonus_score(0), 10000);
		XVT_ASSERT_INT_EQ(
			g_players[0].mission_stats.primary_goal_finish_place,
			1);
		group_craft(1, 2, 0);
		set_elapsed(0, 5, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(1, 0), 1);
		XVT_ASSERT_INT_EQ(bonus_score(1), rows[i].points);
		XVT_ASSERT_INT_EQ(
			g_players[3].mission_stats.primary_goal_finish_place,
			2);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime
				.team_mission_completion_time_seconds[1],
			300);
		XVT_ASSERT_TRUE(line_holds(
			"mission.goal_met team=1 kind=\"primary\"", "rank=2 "));
		char expected[40];
		snprintf(expected, sizeof expected, "hostile=%d points=%d ",
			 rows[i].hostile, rows[i].points);
		XVT_ASSERT_TRUE(
			line_holds("mission.primary_scored team=1", expected));
	}
}

/* A primary goal met in a melee with more than one player tells the local
 * player of the team its place too (message 305, the place as an argument);
 * a local player of another team in a melee or simulator is told whose place
 * it is (306, the place as the second argument); in a combat mission another
 * team's player is told nothing. */
static void check_logic_primary_places(void)
{
	static const struct {
		int mission_type;
		int own_team;
		int sent_305;
		int sent_306;
		int players;
	} rows[] = {
		{MISSION_TYPE_MELEE, 1, 1, 0, 2},
		{MISSION_TYPE_MELEE, 0, 0, 1, 2},
		{MISSION_TYPE_SIMULATOR_1, 0, 0, 1, 2},
		{MISSION_TYPE_COMBAT, 0, 0, 0, 2},
		{MISSION_TYPE_SIMULATOR_2, 0, 0, 0, 2},
		{MISSION_TYPE_MELEE, 1, 0, 0, 1},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		g_mission_header.mission_type = (int8_t)rows[i].mission_type;
		g_flight_player_count = rows[i].players;
		g_net_session.player_count = 1;
		strcpy(g_net_session.local_player_info.player_name, "Ace");
		/* Team 1 completes; the local player is of the team in the
		 * row, and a player of team 1 is connected. */
		g_players[2].participation_state = 1;
		g_players[2].team = 1;
		connect_local_player(rows[i].own_team, 3);
		primary_goal(0, 1, 0);
		group_craft(0, 2, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(1, 0), 1);
		XVT_ASSERT_INT_EQ(built_count(305), rows[i].sent_305);
		XVT_ASSERT_INT_EQ(built_count(306), rows[i].sent_306);
		if (rows[i].sent_305 != 0) {
			XVT_ASSERT_INT_EQ(g_msg_arg_table[0], 295);
		}
		if (rows[i].sent_306 != 0) {
			XVT_ASSERT_INT_EQ(g_msg_arg_table[1], 295);
		}
	}
}

/* A primary goal that has failed is told to the local player of the team:
 * the message that the objectives cannot be finished (206, with the sender's
 * IFF), then each failure text of the team that is not empty, the first with
 * voice 113 and the second with none. Nothing is told to another team's
 * player, nor while the team's prevent goal is already met. */
static void check_logic_primary_failed(void)
{
	static const struct {
		int local_team;
		int prevent_status;
		int told;
		int mission_type;
	} rows[] = {{0, 0, 1, MISSION_TYPE_COMBAT},
		    {1, 0, 0, MISSION_TYPE_COMBAT},
		    {0, 1, 0, MISSION_TYPE_COMBAT},
		    {0, 0, 1, MISSION_TYPE_MELEE}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		g_mission_header.mission_type = (int8_t)rows[i].mission_type;
		connect_local_player(rows[i].local_team, 4);
		strcpy(g_mission_teams[0].end_of_mission_messages[2], "Lost");
		strcpy(g_mission_teams[0].end_of_mission_messages[3], "5x");
		strcpy(g_mission_teams[0].end_of_mission_messages[0], "Won");
		g_flight_mission_state.runtime.team_goal_status[0][1] =
			(uint8_t)rows[i].prevent_status;
		primary_goal(0, 0, 0);
		group_craft(0, 0, 1);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(0, 0), 2);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
					  .global_primary_goal_status,
				  0);
		XVT_ASSERT_INT_EQ(bonus_score(0), 0);
		XVT_ASSERT_INT_EQ(built_count(206), rows[i].told);
		XVT_ASSERT_INT_EQ(built_count(196), rows[i].told);
		XVT_ASSERT_INT_EQ(built_count(207), rows[i].told);
		XVT_ASSERT_INT_EQ(built_count(193), 0);
		if (rows[i].told != 0) {
			XVT_ASSERT_TRUE(built_with(206, "sender=4 "));
			XVT_ASSERT_TRUE(built_with(
				196, "length=4 sender=4 voice=113 "));
			XVT_ASSERT_TRUE(
				built_with(207, "length=2 sender=4 voice=0 "));
		}
	}

	/* An empty first text is skipped; the second keeps voice 0. */
	logic_world();
	connect_local_player(0, 4);
	strcpy(g_mission_teams[0].end_of_mission_messages[3], "Last");
	primary_goal(0, 0, 0);
	group_craft(0, 0, 1);
	run_logic();
	XVT_ASSERT_INT_EQ(built_count(206), 1);
	XVT_ASSERT_INT_EQ(built_count(207), 0);
	XVT_ASSERT_INT_EQ(built_count(196), 1);
	XVT_ASSERT_TRUE(built_with(196, "length=4 sender=4 voice=0 "));
}

/* A prevent goal met is told to the local player of the team as a failure
 * (206 and the failure texts), unless the team's primary goal has failed
 * already; in a combat mission it is not told when the primary goal was met
 * first. The decision is logged. The goal is logged as met and, for the
 * battle, as a failed goal. */
static void check_logic_prevent_met(void)
{
	static const struct {
		int mission_type;
		int primary_status;
		int abort;
		int told;
	} rows[] = {
		{MISSION_TYPE_COMBAT, 0, 1, 1},
		{MISSION_TYPE_COMBAT, 1, 0, 0},
		{MISSION_TYPE_MELEE, 0, 1, 1},
		{MISSION_TYPE_COMBAT, 2, -1, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		g_mission_header.mission_type = (int8_t)rows[i].mission_type;
		connect_local_player(0, 4);
		strcpy(g_mission_teams[0].end_of_mission_messages[2], "Lost");
		strcpy(g_mission_teams[0].end_of_mission_messages[3], "6 gone");
		g_flight_mission_state.runtime.team_goal_status[0][0] =
			(uint8_t)rows[i].primary_status;
		set_fg_goal(0, 0, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, 0,
			    1);
		group_craft(0, 2, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(0, 1), 1);
		XVT_ASSERT_TRUE(
			line_holds("mission.goal_met team=0 kind=\"prevent\"",
				   "rank=1 was=\"open\" tick=4000"));
		XVT_ASSERT_TRUE(
			line_holds("battle.goal_failed team=0 kind=\"prevent\"",
				   "tick=4000"));
		XVT_ASSERT_INT_EQ(count_lines("battle.goal_met"), 0);
		XVT_ASSERT_INT_EQ(built_count(206), rows[i].told);
		XVT_ASSERT_INT_EQ(built_count(196), rows[i].told);
		XVT_ASSERT_INT_EQ(built_count(207), rows[i].told);
		if (rows[i].told != 0) {
			XVT_ASSERT_TRUE(built_with(
				196, "length=4 sender=4 voice=113 "));
			XVT_ASSERT_TRUE(
				built_with(207, "length=6 sender=4 voice=0 "));
		}
		XVT_ASSERT_INT_EQ(count_lines("mission.prevent_told"),
				  rows[i].abort < 0 ? 0 : 1);
		if (rows[i].abort >= 0) {
			char fields[40];
			snprintf(fields, sizeof fields, "team=0 abort=%d",
				 rows[i].abort);
			XVT_ASSERT_TRUE(line_holds(
				"mission.prevent_told slot=7", fields));
		}
	}
}

/* A primary goal met after the team's prevent goal in a combat mission (or
 * in the training exercises' sequence, in any type) is not told as a
 * victory: the rebels (team 0) are told of a draw (208), any other team
 * too (209). */
static void check_logic_primary_draw(void)
{
	static const struct {
		int mission_type;
		int team;
		int directory;
		int sequence;
		int victory;
		int draw_message;
	} rows[] = {
		{MISSION_TYPE_COMBAT, 0, 5, 0, 0, 208},
		{MISSION_TYPE_COMBAT, 1, 5, 0, 0, 209},
		{MISSION_TYPE_MELEE, 0, 5, 0, 1, 0},
		{MISSION_TYPE_MELEE, 1, 0, 1, 0, 209},
		{MISSION_TYPE_MELEE, 0, 0, 1, 0, 208},
		{MISSION_TYPE_MELEE, 0, 0, 0, 1, 0},
		{MISSION_TYPE_MELEE, 0, 5, 1, 1, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		g_mission_header.mission_type = (int8_t)rows[i].mission_type;
		g_pilot_data.mission_directory_id =
			(mission_directory_id)rows[i].directory;
		g_pilot_data.mission_sequence_active = rows[i].sequence;
		connect_local_player(rows[i].team, 2);
		g_flight_mission_state.runtime
			.team_goal_status[rows[i].team][1] = 1;
		primary_goal(0, rows[i].team, 0);
		group_craft(0, 2, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(rows[i].team, 0), 1);
		char fields[40];
		snprintf(fields, sizeof fields, "victory=%d local=1",
			 rows[i].victory);
		XVT_ASSERT_TRUE(
			line_holds("mission.primary_rank slot=7", fields));
		XVT_ASSERT_INT_EQ(built_count(193), rows[i].victory);
		XVT_ASSERT_INT_EQ(built_count(208),
				  rows[i].draw_message == 208);
		XVT_ASSERT_INT_EQ(built_count(209),
				  rows[i].draw_message == 209);
	}
}

/* A prevent goal met after the team's primary goal in a combat mission is
 * told as the other side of the draw: 210 for the rebels (team 0), 211 for
 * any other team, and the abort message is not sent. */
static void check_logic_prevent_draw(void)
{
	static const struct {
		int team;
		int message;
	} rows[] = {{0, 210}, {1, 211}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		connect_local_player(rows[i].team, 2);
		g_flight_mission_state.runtime
			.team_goal_status[rows[i].team][0] = 1;
		set_fg_goal(0, 0, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, 0,
			    1u << rows[i].team);
		group_craft(0, 2, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(rows[i].team, 1), 1);
		XVT_ASSERT_INT_EQ(built_count(rows[i].message), 1);
		XVT_ASSERT_INT_EQ(built_count(206), 0);
	}
}

/* A bonus goal met: the team's bonus score takes 2,500, the any-team bonus
 * flag is set, the local player of the team is told the team's bonus texts
 * (the first with voice 112, the second with none), and the sender is its
 * IFF; another team's player is told nothing. */
static void check_logic_bonus_met(void)
{
	/* An empty text is skipped, and the next one takes the voice of the
	 * second text, none. */
	logic_world();
	connect_local_player(0, 5);
	strcpy(g_mission_teams[0].end_of_mission_messages[5], "Only");
	set_fg_goal(0, 0, 2, MISSION_COND_DESTROYED, GOAL_AMT_100, 1, 0x1);
	group_craft(0, 2, 0);
	run_logic();
	XVT_ASSERT_INT_EQ(built_count(207), 0);
	XVT_ASSERT_INT_EQ(built_count(196), 1);
	XVT_ASSERT_TRUE(built_with(196, "length=4 sender=5 voice=0 "));

	for (int local_team = 0; local_team < 2; ++local_team) {
		logic_world();
		connect_local_player(local_team, 5);
		strcpy(g_mission_teams[0].end_of_mission_messages[4], "Extra");
		strcpy(g_mission_teams[0].end_of_mission_messages[5], "2 more");
		set_fg_goal(0, 0, 2, MISSION_COND_DESTROYED, GOAL_AMT_100, 1,
			    0x1);
		group_craft(0, 2, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(0, 2), 1);
		XVT_ASSERT_INT_EQ(bonus_score(0), 2750);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime.global_bonus_goal_status,
			1);
		XVT_ASSERT_TRUE(line_holds("mission.bonus_scored team=0",
					   "points=2500 score=2750 tick=4000"));
		XVT_ASSERT_TRUE(
			line_holds("battle.goal_met team=0 kind=\"bonus\"",
				   "rank=1 tick=4000"));
		XVT_ASSERT_INT_EQ(built_count(193), 0);
		XVT_ASSERT_INT_EQ(built_count(196), local_team == 0);
		XVT_ASSERT_INT_EQ(built_count(207), local_team == 0);
		if (local_team == 0) {
			XVT_ASSERT_TRUE(built_with(
				196, "length=5 sender=5 voice=112 "));
			XVT_ASSERT_TRUE(
				built_with(207, "length=6 sender=5 voice=0 "));
		}
	}
}

/* In a melee the prevent goal of a team whose primary goal is met does not
 * count; in a combat mission it does. */
static void check_logic_melee_prevent_ignored(void)
{
	static const struct {
		int mission_type;
		int primary_status;
		int status;
	} rows[] = {
		{MISSION_TYPE_MELEE, 1, 0},
		{MISSION_TYPE_MELEE, 0, 1},
		{MISSION_TYPE_COMBAT, 1, 1},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		logic_world();
		g_mission_header.mission_type = (int8_t)rows[i].mission_type;
		g_flight_mission_state.runtime.team_goal_status[0][0] =
			(uint8_t)rows[i].primary_status;
		set_fg_goal(0, 0, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, 0,
			    1);
		group_craft(0, 2, 0);
		run_logic();
		XVT_ASSERT_INT_EQ(goal_status(0, 1), rows[i].status);
	}
}

/* ---- the scripted messages --------------------------------------------- */

/* Message `index` of the mission: its text, the teams that receive it (the
 * bit set), the delay in 5-second units and whether its first trigger is
 * met (1) or failed (-1); its other triggers are always true. */
static struct mission_message *set_message(int index, const char *text,
					   unsigned int teams, int delay,
					   int first_result)
{
	struct mission_message *message = &g_mission_messages[index];
	memset(message, 0, sizeof *message);
	snprintf(message->message, sizeof message->message, "%s", text);
	for (int team = 0; team < 10; ++team) {
		message->sent_to_team[team] = (teams >> team) & 1u;
	}
	message->delay5s = (uint8_t)delay;
	if (first_result < 0) {
		message->trigger_pairs[0].triggers[0].condition =
			MISSION_COND_ALWAYS_FAILED;
	}
	return message;
}

static void run_scan(void)
{
	g_flight_global_countdown_timers.mission_message_scan_timer = 0;
	g_flight_global_countdown_timers.mission_goal_evaluation_timer = 99;
	mission_update_logic();
}

/* The message scan runs only when its timer is 0 or less and sets it to 1,180
 * ticks; a pass of the goals alone leaves it be. */
static void check_logic_message_scan_timer(void)
{
	logic_world();
	g_mission_header.num_messages = 1;
	set_message(0, "Hello", 0x1, 0, 1);
	g_flight_global_countdown_timers.mission_message_scan_timer = 1;
	g_flight_global_countdown_timers.mission_goal_evaluation_timer = 99;
	mission_update_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_triggered[0], 0);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_message_scan_timer, 1);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_goal_evaluation_timer,
		99);
	/* A timer past 0x7FFF is below 0 as a signed number. */
	g_flight_global_countdown_timers.mission_message_scan_timer = 0xFFFF;
	mission_update_logic();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_triggered[0], 1);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_message_scan_timer,
		1180);
}

/* A message whose trigger pairs are met is marked triggered, takes its delay
 * as its countdown and is logged; with no delay and the local player's team
 * among its recipients it is shown at once: message 207 with the message as
 * its text and the voice of its number plus 95 for the first 16 messages,
 * none for later ones. */
static void check_logic_message_shown(void)
{
	logic_world();
	connect_local_player(1, 2);
	g_mission_header.num_messages = 18;
	for (int i = 0; i < 18; ++i) {
		set_message(i, "unused", 0, 0, -1);
	}
	set_message(0, "Hello", 0x2, 0, 1);
	set_message(15, "Sixteenth", 0x2, 0, 1);
	set_message(16, "Late", 0x3, 0, 1);
	set_message(3, "Other team", 0x1, 0, 1);
	run_scan();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_triggered[0], 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_triggered[1], 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_triggered[3], 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_delay_countdown[0], 0);
	XVT_ASSERT_TRUE(line_holds(
		"mission.message_triggered message=0 pair1=1 pair2=1 delay=0",
		"recipient=1 tick=4000"));
	XVT_ASSERT_TRUE(line_holds("mission.message_triggered message=3 ",
				   "recipient=0 "));
	XVT_ASSERT_INT_EQ(built_count(207), 3);
	XVT_ASSERT_INT_EQ(
		count_lines(
			"msg.message_built message=207 type=3 length=5 sender=99 voice=95 "),
		1);
	XVT_ASSERT_INT_EQ(
		count_lines(
			"msg.message_built message=207 type=3 length=9 sender=99 voice=110 "),
		1);
	XVT_ASSERT_INT_EQ(
		count_lines(
			"msg.message_built message=207 type=3 length=4 sender=99 voice=0 "),
		1);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[0], 0x8000);
}

/* A message with a delay of 2 shows on the third scan, once: the first scan
 * triggers it, each later scan takes one off its countdown and logs it, and
 * it is shown when the countdown reaches 0; a countdown at 0 is left alone. A
 * team that is not a recipient never sees it. A message after the 16th has no
 * voice. */
static void check_logic_message_delay(void)
{
	logic_world();
	connect_local_player(1, 2);
	g_mission_header.num_messages = 18;
	for (int i = 0; i < 18; ++i) {
		set_message(i, "unused", 0, 0, -1);
	}
	set_message(0, "Slow", 0x2, 2, 1);
	set_message(1, "Not for us", 0x1, 1, 1);
	set_message(16, "Delayed", 0x2, 1, 1);
	run_scan();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_delay_countdown[0], 2);
	XVT_ASSERT_INT_EQ(built_count(207), 0);
	XVT_ASSERT_INT_EQ(count_lines("mission.message_delay"), 0);
	run_scan();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_delay_countdown[0], 1);
	XVT_ASSERT_TRUE(line_holds("mission.message_delay message=0 left=1",
				   "recipient=1 tick=4000"));
	XVT_ASSERT_TRUE(line_holds("mission.message_delay message=1 left=0",
				   "recipient=0 "));
	XVT_ASSERT_INT_EQ(built_count(207), 1);
	XVT_ASSERT_TRUE(built_with(207, "length=7 sender=99 voice=0 "));
	run_scan();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.message_delay_countdown[0], 0);
	XVT_ASSERT_INT_EQ(built_count(207), 2);
	XVT_ASSERT_TRUE(built_with(207, "length=4 sender=99 voice=95 "));
	run_scan();
	XVT_ASSERT_INT_EQ(built_count(207), 2);
	XVT_ASSERT_INT_EQ(count_lines("mission.message_delay message=0 "), 2);
	XVT_ASSERT_INT_EQ(count_lines("mission.message_delay message=1 "), 1);
}

/* A message's two trigger pairs are joined by OR when its join is 1, else by
 * AND: with the first pair failed and the second met, OR triggers it and AND
 * does not. */
static void check_logic_message_pair_join(void)
{
	for (int join = 0; join < 2; ++join) {
		logic_world();
		g_mission_header.num_messages = 1;
		struct mission_message *message =
			set_message(0, "Joined", 0x1, 0, -1);
		message->trigger_pair1_or_trigger_pair2 = (uint8_t)join;
		run_scan();
		XVT_ASSERT_INT_EQ(g_flight_mission_state.message_triggered[0],
				  join);
	}
}

/* Known failure global_goal_fourth_condition, issue #34: when a global goal's
 * second pair has no second condition, the pair's result is the result of its
 * first condition, and the goal's state is the two pairs' results joined as
 * the goal says. Here both pairs have failed, joined by OR: the goal has
 * failed. The code stores the second pair's result in the first pair's place
 * and reads the second pair's result without setting it, so the state depends
 * on what the earlier goal left in that place (a met pair, here, which makes
 * the OR join met). */
static void check_logic_global_goal_fourth_condition(void)
{
	logic_world();
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	/* Team 0's primary goal runs first and leaves a met second pair. */
	set_global_goal(0, 0, 4, NO_TRIGGER, 0, 1, 1, 0, 0, 0);
	set_global_goal(0, 1, 2, NO_TRIGGER, 0, 2, NO_TRIGGER, 0, 1, 0);
	run_logic();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_global_goal_state[0][1], 2);
}

/* ---- mission_evaluate_condition and the trigger variables -------------- */

#define OUTCOME_BIT(outcome) (1u << (outcome))

/* The craft counts of the conditions that count craft: the outcomes whose
 * craft are counted as meeting the condition and as failing it. */
static const struct {
	int condition;
	int departed_as_destroyed;
	unsigned int met;
	unsigned int failed;
} g_condition_outcomes[] = {
	{MISSION_COND_ARRIVED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_ARRIVED), 0},
	{MISSION_COND_DESTROYED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DESTROYED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_LEFT_REGION)},
	{MISSION_COND_DESTROYED, 1,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DESTROYED) |
		 OUTCOME_BIT(
			 FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT) |
		 OUTCOME_BIT(
			 FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT) |
		 OUTCOME_BIT(
			 FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_LEFT_REGION),
	 0},
	{MISSION_COND_ATTACKED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_ATTACKED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_ATTACKED)},
	{MISSION_COND_CAPTURED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_CAPTURED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_CAPTURED)},
	{MISSION_COND_INSPECTED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_INSPECTED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_INSPECTED)},
	{MISSION_COND_BOARDED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_BOARDED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_BOARDED)},
	{MISSION_COND_DOCKED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DOCKED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_DOCKED)},
	{MISSION_COND_DISABLED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DISABLED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_DISABLED)},
	{MISSION_COND_DEPARTED, 0, OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DEPARTED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DESTROYED) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_ABORTED) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_DEPARTED)},
	{MISSION_COND_NOT_ARRIVED, 0, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_ARRIVED)},
	{MISSION_COND_NOT_ATTACKED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_ATTACKED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_ATTACKED)},
	{MISSION_COND_NOT_DISABLED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_DISABLED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DISABLED)},
	{MISSION_COND_NOT_CAPTURED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_CAPTURED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_CAPTURED)},
	{MISSION_COND_NOT_INSPECTED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_INSPECTED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_INSPECTED)},
	{MISSION_COND_COMPLETED_MISSION, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION), 0},
	{MISSION_COND_NOT_BOARDED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_BOARDED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_BOARDED)},
	{MISSION_COND_FAILED_MISSION, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_FAILED_MISSION), 0},
	{MISSION_COND_NOT_DOCKED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_DOCKED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DOCKED)},
	{MISSION_COND_DESTROYED_OR_DEPARTED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DESTROYED) |
		 OUTCOME_BIT(
			 FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT) |
		 OUTCOME_BIT(
			 FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT) |
		 OUTCOME_BIT(
			 FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_LEFT_REGION),
	 0},
	{MISSION_COND_NOT_DEPARTED, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DESTROYED) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_ABORTED) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_DEPARTED),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE) |
		 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_DEPARTED)},
	{MISSION_COND_CAPTURED_BY_DESTINATION, 0,
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION),
	 OUTCOME_BIT(FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION)},
};

/* Each craft-counting condition counts the outcomes the table above lists as
 * met and as failed, and no others: one craft of a group of one that ended
 * in outcome O has met the condition (1), failed it (2) or neither (4), for
 * "at least one" of the group's craft. The same holds for the special cargo
 * craft's own outcomes, with the amount "all the special cargo". The counts
 * of the met craft are left behind. */
static void check_condition_outcome_table(void)
{
	for (size_t row = 0;
	     row < sizeof g_condition_outcomes / sizeof g_condition_outcomes[0];
	     ++row) {
		for (int outcome = 1; outcome < FLIGHT_GROUP_OUTCOME_COUNT;
		     ++outcome) {
			int expected =
				(g_condition_outcomes[row].met >> outcome) & 1u
					? 1
					: ((g_condition_outcomes[row].failed >>
					    outcome) & 1u
						   ? 2
						   : 4);
			fresh_world();
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
			g_mission_fg_stats[0].outcome_count[outcome] = 1;
			XVT_ASSERT_INT_EQ(
				condition_on_group(
					g_condition_outcomes[row].condition, 0,
					GOAL_AMT_AT_LEAST_1,
					g_condition_outcomes[row]
						.departed_as_destroyed),
				expected);
			XVT_ASSERT_INT_EQ(g_mission_condition_current_count,
					  expected == 1 ? 1 : 0);
			XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 1);

			fresh_world();
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
			g_mission_fg_stats[0].special_cargo_outcome
				[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
			g_mission_fg_stats[0].special_cargo_outcome[outcome] =
				1;
			XVT_ASSERT_INT_EQ(
				condition_on_group(
					g_condition_outcomes[row].condition, 0,
					GOAL_AMT_ALL_SPECIAL_CARGO,
					g_condition_outcomes[row]
						.departed_as_destroyed),
				expected);
		}
	}
}

/* "Survived" counts the craft that did not end destroyed or lost with their
 * mothership, over every group the variable selects (here all of them); the
 * craft that did end so have failed it. The special cargo craft is counted
 * the same way by its own outcomes. */
static void check_condition_survived(void)
{
	fresh_world();
	uint16_t *first = g_mission_fg_stats[0].outcome_count;
	first[FLIGHT_GROUP_OUTCOME_TOTAL] = 5;
	first[FLIGHT_GROUP_OUTCOME_DESTROYED] = 1;
	first[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] = 1;
	uint16_t *second = g_mission_fg_stats[1].outcome_count;
	second[FLIGHT_GROUP_OUTCOME_TOTAL] = 4;
	second[FLIGHT_GROUP_OUTCOME_DESTROYED] = 2;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(MISSION_COND_SURVIVED, 11,
						     0, GOAL_AMT_AT_LEAST_1, 0,
						     10),
			  1);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 5);
	XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 9);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(MISSION_COND_SURVIVED, 11,
						     0, GOAL_AMT_100, 0, 10),
			  2);

	/* Two special cargo craft in the first group, one destroyed. */
	g_mission_fg_stats[0]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	g_mission_fg_stats[0]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_DESTROYED] = 1;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SURVIVED, 0,
					     GOAL_AMT_ALL_SPECIAL_CARGO, 0),
			  2);
	g_mission_fg_stats[0]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_DESTROYED] = 0;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SURVIVED, 0,
					     GOAL_AMT_ALL_SPECIAL_CARGO, 0),
			  1);
	g_mission_fg_stats[0].special_cargo_outcome
		[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] = 1;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SURVIVED, 0,
					     GOAL_AMT_ALL_SPECIAL_CARGO, 0),
			  2);

	/* A group whose craft all ended destroyed or lost has none surviving
	 * and every craft failed it; the same for its special cargo craft. */
	fresh_world();
	uint16_t *gone = g_mission_fg_stats[0].outcome_count;
	gone[FLIGHT_GROUP_OUTCOME_TOTAL] = 3;
	gone[FLIGHT_GROUP_OUTCOME_DESTROYED] = 1;
	gone[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] = 2;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SURVIVED, 0,
					     GOAL_AMT_AT_LEAST_1, 0),
			  2);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 0);
	uint8_t *gone_special = g_mission_fg_stats[0].special_cargo_outcome;
	gone_special[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	gone_special[FLIGHT_GROUP_OUTCOME_DESTROYED] = 1;
	gone_special[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] = 1;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SURVIVED, 0,
					     GOAL_AMT_ALL_SPECIAL_CARGO, 0),
			  2);
}

/* A group with no craft type is not counted, whatever it matches. */
static void check_condition_skips_unknown_group(void)
{
	fresh_world();
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 3;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED] = 3;
	g_mission_flight_groups[1].fg.craft_type = CRAFT_SPECIES_UNKNOWN;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(MISSION_COND_DESTROYED, 11,
						     0, GOAL_AMT_100, 0, 10),
			  4);
	XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 0);
	g_mission_flight_groups[1].fg.craft_type = TEST_XWING;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(MISSION_COND_DESTROYED, 11,
						     0, GOAL_AMT_100, 0, 10),
			  1);
	XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 3);
}

/* How the amount judges the counts: the rows are the amount, the group's
 * total, craft arrived, craft that met "destroyed" and craft that have failed
 * it (left the region), and the result. The percentage amounts compare the
 * craft with the total as a fraction of 65,536; the subset amounts compare the
 * met craft with those arrived and never fail; an amount no row names leaves
 * the condition undecided. */
static void check_condition_amounts(void)
{
	static const struct {
		int amount;
		int total;
		int arrived;
		int met;
		int failed;
		int result;
	} rows[] = {
		{GOAL_AMT_100, 4, 4, 4, 0, 1},
		{GOAL_AMT_100, 4, 4, 3, 0, 4},
		{GOAL_AMT_100, 4, 4, 3, 1, 2},
		{GOAL_AMT_75, 4, 4, 3, 0, 1},
		{GOAL_AMT_75, 4, 4, 2, 1, 4},
		{GOAL_AMT_75, 4, 4, 1, 2, 2},
		{GOAL_AMT_50, 4, 4, 2, 0, 1},
		{GOAL_AMT_50, 4, 4, 1, 2, 4},
		{GOAL_AMT_50, 4, 4, 1, 3, 2},
		{GOAL_AMT_25, 4, 4, 1, 0, 1},
		{GOAL_AMT_25, 4, 4, 0, 3, 4},
		{GOAL_AMT_25, 4, 4, 0, 4, 2},
		{GOAL_AMT_AT_LEAST_1, 4, 4, 1, 0, 1},
		{GOAL_AMT_AT_LEAST_1, 4, 4, 0, 3, 4},
		{GOAL_AMT_AT_LEAST_1, 4, 4, 0, 4, 2},
		{GOAL_AMT_AT_LEAST_1_ALT, 4, 4, 1, 0, 1},
		{GOAL_AMT_AT_LEAST_1_ALT, 4, 4, 0, 4, 2},
		{GOAL_AMT_ALL_BUT_1, 4, 4, 3, 0, 1},
		{GOAL_AMT_ALL_BUT_1, 4, 4, 2, 1, 4},
		{GOAL_AMT_ALL_BUT_1, 4, 4, 2, 2, 2},
		{GOAL_AMT_100_OF_SUBSET, 4, 2, 2, 0, 1},
		{GOAL_AMT_100_OF_SUBSET, 4, 2, 1, 1, 4},
		{GOAL_AMT_75_OF_SUBSET, 8, 4, 3, 4, 1},
		{GOAL_AMT_75_OF_SUBSET, 8, 4, 2, 4, 4},
		{GOAL_AMT_50_OF_SUBSET, 8, 4, 2, 4, 1},
		{GOAL_AMT_50_OF_SUBSET, 8, 4, 1, 4, 4},
		{GOAL_AMT_25_OF_SUBSET, 8, 4, 1, 4, 1},
		{GOAL_AMT_25_OF_SUBSET, 8, 4, 0, 4, 4},
		{GOAL_AMT_ALL_BUT_1_OF_SUBSET, 8, 4, 3, 4, 1},
		{GOAL_AMT_ALL_BUT_1_OF_SUBSET, 8, 4, 2, 4, 4},
		{GOAL_AMT_66, 6, 6, 4, 0, 1},
		{GOAL_AMT_66, 6, 6, 3, 2, 4},
		{GOAL_AMT_66, 6, 6, 1, 3, 2},
		{GOAL_AMT_33, 6, 6, 2, 0, 1},
		{GOAL_AMT_33, 6, 6, 1, 4, 4},
		{GOAL_AMT_33, 6, 6, 0, 5, 2},
		{GOAL_AMT_ALL_EXCEPT_PLAYER, 4, 4, 4, 0, 4},
		{GOAL_AMT_PLAYER_FG, 4, 4, 4, 0, 4},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		fresh_world();
		uint16_t *counts = g_mission_fg_stats[0].outcome_count;
		counts[FLIGHT_GROUP_OUTCOME_TOTAL] = (uint16_t)rows[i].total;
		counts[FLIGHT_GROUP_OUTCOME_ARRIVED] =
			(uint16_t)rows[i].arrived;
		counts[FLIGHT_GROUP_OUTCOME_DESTROYED] = (uint16_t)rows[i].met;
		counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] =
			(uint16_t)rows[i].failed;
		XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_DESTROYED, 0,
						     (int16_t)rows[i].amount,
						     0),
				  rows[i].result);
		XVT_ASSERT_INT_EQ(g_mission_condition_current_count,
				  rows[i].met);
		XVT_ASSERT_INT_EQ(g_mission_condition_total_count,
				  rows[i].total);
	}
}

/* The special cargo amounts: "all the special cargo" is met when every
 * special cargo craft met the condition, failed when one has failed, and
 * undecided when the group has none; "all but the special cargo" is met when
 * the craft that did not meet the condition are the special cargo craft, and
 * has failed when a craft failed without a special cargo craft meeting it, or
 * two did. */
static void check_condition_special_cargo_amounts(void)
{
	static const struct {
		int amount;
		int total;
		int met;
		int failed;
		int special_total;
		int special_met;
		int special_failed;
		int result;
	} rows[] = {
		{GOAL_AMT_ALL_SPECIAL_CARGO, 4, 1, 0, 2, 2, 0, 1},
		{GOAL_AMT_ALL_SPECIAL_CARGO, 4, 1, 0, 2, 1, 0, 4},
		{GOAL_AMT_ALL_SPECIAL_CARGO, 4, 1, 0, 2, 1, 1, 2},
		{GOAL_AMT_ALL_SPECIAL_CARGO, 4, 1, 0, 0, 0, 0, 4},
		{GOAL_AMT_ALL_NON_SPECIAL, 4, 3, 0, 1, 0, 0, 1},
		{GOAL_AMT_ALL_NON_SPECIAL, 4, 2, 0, 1, 0, 0, 4},
		{GOAL_AMT_ALL_NON_SPECIAL, 4, 2, 1, 1, 0, 0, 2},
		{GOAL_AMT_ALL_NON_SPECIAL, 4, 2, 1, 1, 1, 0, 4},
		{GOAL_AMT_ALL_NON_SPECIAL, 4, 2, 2, 1, 1, 0, 2},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		fresh_world();
		uint16_t *counts = g_mission_fg_stats[0].outcome_count;
		counts[FLIGHT_GROUP_OUTCOME_TOTAL] = (uint16_t)rows[i].total;
		counts[FLIGHT_GROUP_OUTCOME_DESTROYED] = (uint16_t)rows[i].met;
		counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION] =
			(uint16_t)rows[i].failed;
		uint8_t *special = g_mission_fg_stats[0].special_cargo_outcome;
		special[FLIGHT_GROUP_OUTCOME_TOTAL] =
			(uint8_t)rows[i].special_total;
		special[FLIGHT_GROUP_OUTCOME_DESTROYED] =
			(uint8_t)rows[i].special_met;
		special[FLIGHT_GROUP_OUTCOME_LEFT_REGION] =
			(uint8_t)rows[i].special_failed;
		XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_DESTROYED, 0,
						     (int16_t)rows[i].amount,
						     0),
				  rows[i].result);
	}
}

/* The craft of one team: with a team filter below 10, "inspected" counts the
 * craft that team inspected and the ones that ended before it identified
 * them; "not inspected" the other way round; "captured and departed" the
 * craft that team captured and that left, and the ones that ended without
 * that team capturing them. Without a team filter, "captured and departed"
 * adds every team's captured craft, and counts the uncaptured losses of the
 * teams that have craft only. The special cargo craft are counted the same
 * way. */
static void check_condition_team_filter(void)
{
	static const struct {
		int condition;
		int filter;
		int field;
		int result;
	} rows[] = {
		/* field: 0 inspected, 1 uninspected lost, 2 captured and
		 * departed, 3 uncaptured lost. */
		{MISSION_COND_INSPECTED, 3, 0, 1},
		{MISSION_COND_INSPECTED, 3, 1, 2},
		{MISSION_COND_INSPECTED, 4, 0, 4},
		{MISSION_COND_NOT_INSPECTED, 3, 1, 1},
		{MISSION_COND_NOT_INSPECTED, 3, 0, 2},
		{MISSION_COND_NOT_INSPECTED, 4, 1, 4},
		{MISSION_COND_CAPTURED_AND_DEPARTED, 3, 2, 1},
		{MISSION_COND_CAPTURED_AND_DEPARTED, 3, 3, 2},
		{MISSION_COND_CAPTURED_AND_DEPARTED, 4, 2, 4},
		{MISSION_COND_CAPTURED_AND_DEPARTED, 10, 2, 1},
		{MISSION_COND_CAPTURED_AND_DEPARTED, 10, 3, 2},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		for (int special = 0; special < 2; ++special) {
			fresh_world();
			struct mission_fg_runtime_stats *stats =
				&g_mission_fg_stats[0];
			stats->outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
			g_flight_mission_state.runtime
				.team_has_countable_craft[3] = 1;
			uint8_t *counts = NULL;
			switch (rows[i].field) {
			case 0:
				counts =
					special ? stats->team_special_cargo_inspected
						: stats->team_inspected;
				break;
			case 1:
				counts =
					special ? stats->team_special_cargo_uninspected_lost
						: stats->team_uninspected_lost;
				break;
			case 2:
				counts =
					special ? stats->team_special_cargo_captured_departed
						: stats->team_captured_departed_count;
				break;
			default:
				counts =
					special ? stats->team_special_cargo_uncaptured_lost
						: stats->team_uncaptured_lost;
				break;
			}
			counts[3] = 1;
			if (special) {
				stats->special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
			}
			XVT_ASSERT_INT_EQ(
				mission_evaluate_condition(
					(uint16_t)rows[i].condition,
					GOAL_TARGET_FLIGHT_GROUP, 0,
					special ? GOAL_AMT_ALL_SPECIAL_CARGO
						: GOAL_AMT_AT_LEAST_1,
					0, (uint16_t)rows[i].filter),
				rows[i].result);
			XVT_ASSERT_INT_EQ(g_mission_condition_current_count,
					  !special && rows[i].result == 1);
		}
	}
	/* The uncaptured losses of a team with no craft are left out. */
	fresh_world();
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
	g_mission_fg_stats[0].team_uncaptured_lost[3] = 1;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_CAPTURED_AND_DEPARTED,
					   GOAL_TARGET_FLIGHT_GROUP, 0,
					   GOAL_AMT_AT_LEAST_1, 0, 10),
		4);
}

/* Craft conditions read the group's live craft in the active region: the
 * rows are the condition, the craft's shields (the two shield arrays added),
 * its hull damage with a hull of 400, its working subsystems and how many of
 * its two warhead places are loaded, and the result for "at least one" craft: 1
 * when the craft meets the condition, 2 when it does not. The model's shield
 * strength is 100, so the craft's full shields are 200. */
static void check_condition_live_craft(void)
{
	enum { ALL_SYSTEMS = 0xFFFF };
	static const struct {
		int condition;
		int shield_front;
		int shield_back;
		int hull_damage;
		int systems;
		int warheads;
		int result;
	} rows[] = {
		{MISSION_COND_SHIELDS_DEPLETED, 0, 0, 0, ALL_SYSTEMS, 1, 1},
		{MISSION_COND_SHIELDS_DEPLETED, 1, 0, 0, ALL_SYSTEMS, 1, 2},
		{MISSION_COND_SHIELDS_BELOW_25, 25, 25, 0, ALL_SYSTEMS, 1, 1},
		{MISSION_COND_SHIELDS_BELOW_25, 26, 25, 0, ALL_SYSTEMS, 1, 2},
		{MISSION_COND_SHIELDS_BELOW_50, 50, 50, 0, ALL_SYSTEMS, 1, 1},
		{MISSION_COND_SHIELDS_BELOW_50, 51, 50, 0, ALL_SYSTEMS, 1, 2},
		{MISSION_COND_HULL_DAMAGE_ABOVE_25, 100, 100, 101, ALL_SYSTEMS,
		 1, 1},
		{MISSION_COND_HULL_DAMAGE_ABOVE_25, 100, 100, 100, ALL_SYSTEMS,
		 1, 2},
		{MISSION_COND_HULL_DAMAGE_ABOVE_50, 100, 100, 201, ALL_SYSTEMS,
		 1, 1},
		{MISSION_COND_HULL_DAMAGE_ABOVE_50, 100, 100, 200, ALL_SYSTEMS,
		 1, 2},
		{MISSION_COND_HULL_DAMAGE_ABOVE_75, 100, 100, 301, ALL_SYSTEMS,
		 1, 1},
		{MISSION_COND_HULL_DAMAGE_ABOVE_75, 100, 100, 300, ALL_SYSTEMS,
		 1, 2},
		{MISSION_COND_NO_WARHEADS, 100, 100, 0, ALL_SYSTEMS, 0, 1},
		{MISSION_COND_NO_WARHEADS, 100, 100, 0, ALL_SYSTEMS, 1, 2},
		{MISSION_COND_NO_WARHEADS, 100, 100, 0, ALL_SYSTEMS, 2, 2},
		{MISSION_COND_CANNONS_DISABLED, 100, 100, 0,
		 ALL_SYSTEMS & ~CRAFT_SUBSYSTEM_FLAG_CANNONS, 1, 1},
		{MISSION_COND_CANNONS_DISABLED, 100, 100, 0, ALL_SYSTEMS, 1, 2},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		fresh_world();
		struct craft_data *craft = place_craft(0, 0, 4);
		craft->model_index = (uint16_t)get_model_index_from_type(
			(object_type_id)g_test_objects[0].object_type);
		struct model_def *model = &g_model_defs[craft->model_index];
		struct model_def saved = *model;
		model->shield_strength = 100;
		model->warhead_launcher_first_slot[0] = 2;
		model->warhead_launcher_last_slot[0] = 3;
		craft->warhead_launcher_count = 1;
		craft->shield_energy[0] = (int16_t)rows[i].shield_front;
		craft->shield_energy[1] = (int16_t)rows[i].shield_back;
		craft->hull_max = 400;
		craft->hull_damage = (unsigned int)rows[i].hull_damage;
		craft->working_subsystems =
			(craft_subsystem_flag)rows[i].systems;
		craft->weapon_slots[2].ammo_count =
			(uint8_t)(rows[i].warheads > 0 ? 1 : 0);
		craft->weapon_slots[3].ammo_count =
			(uint8_t)(rows[i].warheads > 1 ? 1 : 0);
		g_mission_fg_stats[0]
			.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
		XVT_ASSERT_INT_EQ(
			condition_on_group((uint16_t)rows[i].condition, 0,
					   GOAL_AMT_AT_LEAST_1, 0),
			rows[i].result);
		XVT_ASSERT_INT_EQ(g_mission_condition_current_count,
				  rows[i].result == 1 ? 1 : 0);
		*model = saved;
	}
}

/* The live craft of other groups, empty slots and slots outside the active
 * region are not counted; the group's special cargo craft is counted for the
 * special cargo amount. */
static void check_condition_live_craft_scope(void)
{
	fresh_world();
	struct model_def *model = model_of(TEST_XWING);
	struct model_def saved = *model;
	model->shield_strength = 100;
	g_mission_flight_groups[0].fg.special_cargo_craft = 1;
	/* Group 0's craft 0 and 1 (the special cargo) have no shields; group
	 * 1's craft 2 has none either; slot 3 is outside the region. */
	for (int slot = 0; slot < 4; ++slot) {
		int fg = slot == 2 ? 1 : 0;
		struct craft_data *craft = place_craft(slot, fg, slot % 2);
		craft->model_index = (uint16_t)get_model_index_from_type(
			(object_type_id)g_test_objects[slot].object_type);
		craft->shield_energy[0] = slot == 0 ? 9 : 0;
	}
	g_active_region_craft_object_slot_end = 3;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	g_mission_fg_stats[0]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SHIELDS_DEPLETED, 0,
					     GOAL_AMT_100, 0),
			  2);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 1);
	g_test_craft[0].shield_energy[0] = 0;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SHIELDS_DEPLETED, 0,
					     GOAL_AMT_100, 0),
			  1);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 2);
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SHIELDS_DEPLETED, 0,
					     GOAL_AMT_ALL_SPECIAL_CARGO, 0),
			  1);
	*model = saved;
}

/* The conditions on the players: group `fg` is flown by player number `number`
 * (1 to 8) on a team and IFF. */
static void player_group(int fg, int number, int team, int iff)
{
	struct xvt_flight_group *group = &g_mission_flight_groups[fg].fg;
	group->player_number = (uint8_t)number;
	group->team = (uint8_t)team;
	group->iff = (uint8_t)iff;
}

/* "Player connected" and "player disconnected" by team or IFF compare the
 * players connected with the groups a player number is set for: four groups
 * on team 1 and IFF 2, two players connected on team 1 and IFF 2. The rows are
 * the condition, the variable type, the amount and the result; an amount that
 * is not listed leaves the condition failed (2), and so does a variable
 * other than a team or IFF. */
static void check_condition_players_by_group(void)
{
	static const struct {
		int condition;
		int variable_type;
		int amount;
		int result;
	} rows[] = {
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM, GOAL_AMT_100,
		 2},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM, GOAL_AMT_50,
		 1},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM, GOAL_AMT_75,
		 2},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM, GOAL_AMT_25,
		 1},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_AT_LEAST_1, 1},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_ALL_BUT_1, 2},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM, GOAL_AMT_66,
		 2},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM, GOAL_AMT_33,
		 1},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_100_OF_SUBSET, 2},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_100, 2},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_50, 1},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_75, 2},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_25, 1},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_AT_LEAST_1, 1},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_TEAM,
		 GOAL_AMT_ALL_BUT_1, 2},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_IFF, GOAL_AMT_50,
		 1},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_IFF, GOAL_AMT_75,
		 2},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_IFF, GOAL_AMT_50,
		 1},
		{MISSION_COND_PLAYER_DISCONNECTED, GOAL_TARGET_IFF,
		 GOAL_AMT_100, 2},
		{MISSION_COND_PLAYER_CONNECTED, GOAL_TARGET_SPECIES,
		 GOAL_AMT_AT_LEAST_1, 2},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		fresh_world();
		g_mission_header.num_flight_groups = 4;
		for (int fg = 0; fg < 4; ++fg) {
			g_mission_flight_groups[fg].fg.craft_type = TEST_XWING;
			player_group(fg, fg + 1, 1, 2);
		}
		g_players[0].participation_state = 1;
		g_players[0].team = 1;
		g_players[0].iff = 2;
		g_players[4].participation_state = 2;
		g_players[4].team = 1;
		g_players[4].iff = 2;
		g_players[5].participation_state = 0;
		g_players[5].team = 1;
		g_players[5].iff = 2;
		XVT_ASSERT_INT_EQ(
			mission_evaluate_condition(
				(uint16_t)rows[i].condition,
				(int16_t)rows[i].variable_type,
				rows[i].variable_type == GOAL_TARGET_IFF ? 2
									 : 1,
				(int16_t)rows[i].amount, 0, 10),
			rows[i].result);
	}
	/* Three of the four connected: "all" is not met, "75 percent" and "all
	 * but one" are; for the disconnected one, "all but one" is not. */
	fresh_world();
	g_mission_header.num_flight_groups = 4;
	for (int fg = 0; fg < 4; ++fg) {
		player_group(fg, fg + 1, 1, 2);
		g_players[fg].participation_state = fg < 3 ? 1 : 0;
		g_players[fg].team = 1;
	}
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_CONNECTED,
				  GOAL_TARGET_TEAM, 1, GOAL_AMT_75, 0, 10),
			  1);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_CONNECTED,
				  GOAL_TARGET_TEAM, 1, GOAL_AMT_100, 0, 10),
			  2);
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PLAYER_CONNECTED,
					   GOAL_TARGET_TEAM, 1,
					   GOAL_AMT_ALL_BUT_1, 0, 10),
		1);
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PLAYER_DISCONNECTED,
					   GOAL_TARGET_TEAM, 1,
					   GOAL_AMT_ALL_BUT_1, 0, 10),
		2);
	g_players[2].participation_state = 0;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_PLAYER_CONNECTED,
					   GOAL_TARGET_TEAM, 1,
					   GOAL_AMT_ALL_BUT_1, 0, 10),
		2);
}

/* "Player connected" by player number is met when the group that has the
 * number (variable + 1) is owned by a player, and "player disconnected" when
 * that group has no owner; a number no group has leaves them failed. */
static void check_condition_players_by_number(void)
{
	fresh_world();
	player_group(0, 3, 0, 0);
	player_group(1, 0, 1, 1);
	g_mission_flight_groups[0].player_owner_idx = 2;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_CONNECTED,
				  GOAL_TARGET_PLAYER_NUMBER, 2, 0, 0, 10),
			  1);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_DISCONNECTED,
				  GOAL_TARGET_PLAYER_NUMBER, 2, 0, 0, 10),
			  2);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_CONNECTED,
				  GOAL_TARGET_PLAYER_NUMBER, 3, 0, 0, 10),
			  2);
	g_mission_flight_groups[0].player_owner_idx = -1;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_CONNECTED,
				  GOAL_TARGET_PLAYER_NUMBER, 2, 0, 0, 10),
			  2);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_DISCONNECTED,
				  GOAL_TARGET_PLAYER_NUMBER, 2, 0, 0, 10),
			  1);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_DISCONNECTED,
				  GOAL_TARGET_PLAYER_NUMBER, 4, 0, 0, 10),
			  2);
}

/* The bonus goal failed is met when the any-team bonus status is failed (2)
 * and has failed when it is complete (1); a condition number the function
 * does not list (15 here) is undecided and leaves both craft counts at 0. */
static void check_condition_bonus_failed_and_unlisted(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_FAILED,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		4);
	g_flight_mission_state.runtime.global_bonus_goal_status = 2;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_FAILED,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		1);
	g_flight_mission_state.runtime.global_bonus_goal_status = 1;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_FAILED,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		2);
	g_mission_condition_current_count = 5;
	g_mission_condition_total_count = 6;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(15, GOAL_TARGET_NONE, 0, 0, 0, 10),
		4);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 0);
	XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 0);
}

/* ---- the trigger variables --------------------------------------------- */

/* The value of g_genus_convert that is genus `genus` or 0xFF. */
static int variable_for_genus(int genus)
{
	for (int variable = 0; variable < 12; ++variable) {
		if (g_genus_convert[variable] == genus) {
			return variable;
		}
	}
	return -1;
}

static int variable_for_family(int family)
{
	for (int variable = 0; variable < 4; ++variable) {
		if (g_family_convert[variable] == family) {
			return variable;
		}
	}
	return -1;
}

/* A flight group matches a variable of type species, genus or family when its
 * craft type's object type has that species, genus or family; the opposite
 * types (16 to 18) match the groups that do not. A type 6 variable is a first
 * order; the opposite types of IFF (19), global group (20), team (21) and
 * global unit (24) match when the group's value differs. Group 0 is an X-wing
 * on IFF 3, team 4, global group 5, global unit 6 with the order 7; group 1 is
 * another craft of another genus and family. */
static void check_group_matches_variable_types(void)
{
	fresh_world();
	/* A craft type other than group 0's with a different genus and family
	 * when there is one. */
	int other_type = TEST_XWING;
	for (int type = 1; type < 96; ++type) {
		uint8_t object_type = g_craft_type_to_object_type[type];
		uint8_t mine = g_craft_type_to_object_type[TEST_XWING];
		if (object_type != 0 && object_type != mine &&
		    g_object_type_table[object_type].genus_id !=
			    g_object_type_table[mine].genus_id &&
		    (uint8_t)g_object_type_table[object_type].family_id !=
			    (uint8_t)g_object_type_table[mine].family_id &&
		    variable_for_genus(
			    g_object_type_table[object_type].genus_id) >= 0 &&
		    variable_for_family(
			    (uint8_t)g_object_type_table[object_type]
				    .family_id) >= 0) {
			other_type = type;
			break;
		}
	}
	XVT_ASSERT_TRUE(other_type != TEST_XWING);
	g_mission_flight_groups[1].fg.craft_type = (craft_species)other_type;
	struct xvt_flight_group *group = &g_mission_flight_groups[0].fg;
	group->iff = 3;
	group->team = 4;
	group->global_group = 5;
	group->global_unit = 6;
	group->orders[0].order = 7;
	uint8_t my_object = g_craft_type_to_object_type[TEST_XWING];
	int my_genus =
		variable_for_genus(g_object_type_table[my_object].genus_id);
	int my_family = variable_for_family(
		(uint8_t)g_object_type_table[my_object].family_id);
	XVT_ASSERT_TRUE(my_genus >= 0);
	XVT_ASSERT_TRUE(my_family >= 0);
	uint8_t other_object = g_craft_type_to_object_type[other_type];
	int other_genus =
		variable_for_genus(g_object_type_table[other_object].genus_id);
	int other_family = variable_for_family(
		(uint8_t)g_object_type_table[other_object].family_id);

	static const struct {
		int type;
		int variable_of;
		int expected;
	} rows[] = {
		/* variable_of: 0 group 0's value, 1 group 1's. */
		{2, 0, 1}, {2, 1, 0}, {16, 0, 0}, {16, 1, 1},
		{3, 0, 1}, {3, 1, 0}, {17, 0, 0}, {17, 1, 1},
		{4, 0, 1}, {4, 1, 0}, {18, 0, 0}, {18, 1, 1},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		int variable = 0;
		switch (rows[i].type) {
		case 2:
		case 16:
			variable = (rows[i].variable_of == 0 ? TEST_XWING
							     : other_type) -
				   1;
			break;
		case 3:
		case 17:
			variable = rows[i].variable_of == 0 ? my_genus
							    : other_genus;
			break;
		default:
			variable = rows[i].variable_of == 0 ? my_family
							    : other_family;
			break;
		}
		XVT_ASSERT_INT_EQ(
			mission_flight_group_matches_trigger_variable(
				0, (int16_t)rows[i].type, (uint16_t)variable),
			rows[i].expected);
	}
	static const struct {
		int type;
		int variable;
		int expected;
	} value_rows[] = {
		{6, 7, 1},  {6, 8, 0},	{19, 3, 0}, {19, 2, 1}, {20, 5, 0},
		{20, 4, 1}, {21, 4, 0}, {21, 3, 1}, {24, 6, 0}, {24, 7, 1},
		{5, 3, 1},  {12, 4, 1}, {23, 6, 1}, {8, 5, 1},	{9, 0, 1},
		{10, 0, 1}, {13, 0, 0}, {14, 0, 0}, {22, 0, 0},
	};
	for (size_t i = 0; i < sizeof value_rows / sizeof value_rows[0]; ++i) {
		XVT_ASSERT_INT_EQ(mission_flight_group_matches_trigger_variable(
					  0, (int16_t)value_rows[i].type,
					  (uint16_t)value_rows[i].variable),
				  value_rows[i].expected);
	}
}

/* The same variables for one object (a craft in the object table, with its
 * mobile record or without it): its group, species, genus, family, IFF and
 * team come through its flight group, except IFF and team, which a mobile
 * record gives. Types 15 and 16 to 24 are the opposites. Type 13 is the player
 * number of the owner's group, type 14 the elapsed time at or before 5 seconds
 * times the variable, and type 22 an owner other than the variable. */
static void check_object_matches_variable(void)
{
	fresh_world();
	struct xvt_flight_group *group = &g_mission_flight_groups[0].fg;
	group->iff = 3;
	group->team = 4;
	group->global_group = 5;
	group->global_unit = 6;
	group->group_ai = 2;
	group->status1 = 1;
	group->orders[0].order = 7;
	g_mission_flight_groups[1] = g_mission_flight_groups[0];
	place_craft(0, 0, 0);
	g_test_mobiles[0].team = 1;
	g_test_mobiles[0].iff = 2;
	uint8_t my_object = g_craft_type_to_object_type[TEST_XWING];
	int my_genus =
		variable_for_genus(g_object_type_table[my_object].genus_id);
	int my_family = variable_for_family(
		(uint8_t)g_object_type_table[my_object].family_id);
	XVT_ASSERT_TRUE(my_genus >= 0);
	XVT_ASSERT_TRUE(my_family >= 0);

	static const struct {
		int type;
		int variable;
		int expected; /* with the mobile record */
		int bare;     /* without it */
	} rows[] = {
		{0, 0, 0, 0},  {1, 0, 1, 1},  {1, 1, 0, 0},  {15, 0, 0, 0},
		{15, 1, 1, 1}, {5, 2, 1, 0},  {5, 3, 0, 1},  {19, 2, 0, 1},
		{19, 3, 1, 0}, {6, 7, 1, 1},  {6, 8, 0, 0},  {8, 5, 1, 1},
		{20, 5, 0, 0}, {20, 6, 1, 1}, {9, 2, 1, 1},  {10, 1, 1, 1},
		{11, 0, 1, 1}, {12, 1, 1, 0}, {12, 4, 0, 1}, {21, 1, 0, 1},
		{21, 4, 1, 0}, {23, 6, 1, 1}, {24, 6, 0, 0}, {24, 7, 1, 1},
		{7, 0, 0, 0},  {99, 0, 0, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
					  0, (uint16_t)rows[i].type,
					  (uint16_t)rows[i].variable),
				  rows[i].expected);
		/* Without a mobile record the object's group gives it all; here
		 * it is group 1, a copy of group 0. */
		struct mobile_object *mobile = g_test_objects[0].mobj;
		g_test_objects[0].mobj = NULL;
		g_test_objects[0].flight_group_idx = 1;
		int bare = rows[i].bare;
		if ((rows[i].type == 1 || rows[i].type == 15) &&
		    rows[i].variable <= 1) {
			bare = !bare;
		}
		XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
					  0, (uint16_t)rows[i].type,
					  (uint16_t)rows[i].variable),
				  bare);
		g_test_objects[0].flight_group_idx = 0;
		g_test_objects[0].mobj = mobile;
	}
	/* Species, genus and family. */
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 2, (uint16_t)(TEST_XWING - 1)),
			  1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 16, (uint16_t)(TEST_XWING - 1)),
			  0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 3, (uint16_t)my_genus),
			  1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 17, (uint16_t)my_genus),
			  0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 4, (uint16_t)my_family),
			  1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 18, (uint16_t)my_family),
			  0);

	/* Type 13: the owner's bound group has player number variable + 1. */
	g_mission_flight_groups[1].fg.player_number = 4;
	g_players[2].bound_flight_group_idx = 1;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 13, 3), 0);
	g_test_objects[0].player_owner_idx = 2;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 13, 3), 1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 13, 2), 0);
	/* Type 22: not owned by player variable. */
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 22, 2), 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 22, 5), 1);
	g_test_objects[0].player_owner_idx = -1;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 22, 2), 1);

	/* Type 14: the elapsed minutes and seconds, ignoring the hours, are at
	 * or before 5 seconds times the variable (here 90 seconds). */
	set_elapsed(7, 1, 30);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 14, 18),
			  1);
	set_elapsed(7, 1, 31);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 14, 18),
			  0);
	set_elapsed(0, 0, 59);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 14, 18),
			  1);
	set_elapsed(0, 2, 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 14, 18),
			  0);
}

/* The craft states of type 7: each variable matches a craft in the state it
 * names and not in the other. The rows are the variable, then whether the
 * state is set by the craft being in it (1) or by it being out of it (0). */
static void set_craft_state(struct craft_data *craft, int variable, int on)
{
	switch (variable) {
	case 0:
		craft->captured_by_flight_group = on ? 3 : 0;
		break;
	case 1:
		craft->identified_order_by_team[4] = on ? 1 : 0;
		break;
	case 2:
		craft->ai_flight.times_boarded = on ? 1 : 0;
		break;
	case 3:
		craft->ai_flight.docked_target_count = on ? 1 : 0;
		break;
	case 4:
		craft->working_subsystems = on ? 0 : 0xFFFF;
		break;
	case 5:
		craft->attacked_by_team[4] = on ? 1 : 0;
		break;
	case 6:
		craft->hull_damage = on ? 1 : 0;
		break;
	case 7:
		craft->craft_ordinal = on ? 3 : 2;
		break;
	case 8:
		craft->craft_ordinal = on ? 2 : 3;
		break;
	case 12:
		craft->working_subsystems = on ? 0xFFFF : 0;
		break;
	case 13:
		craft->captured_by_flight_group = on ? 0 : 3;
		break;
	case 16:
		craft->ai_flight.times_boarded = on ? 0 : 1;
		break;
	case 18:
		craft->ai_flight.docked_target_count = on ? 0 : 1;
		break;
	case 22:
		craft->hull_damage = on ? 100 : 99;
		break;
	case 23:
		craft->hull_damage = on ? 200 : 199;
		break;
	case 24:
		craft->hull_damage = on ? 300 : 299;
		break;
	default:
		break;
	}
}

/* A craft's state variables (type 7) match a craft that is in the state and
 * not one that is out of it: captured, identified by another team, boarded,
 * docked, no working subsystems, attacked by another team, hull damaged,
 * special cargo or not, and the opposites of those that have one. The hull
 * fractions are at least a quarter, half and three quarters of the hull
 * (400), and "no warheads left" counts the loaded launchers. A variable 15 or
 * 17 never matches. */
static void check_object_craft_states(void)
{
	static const int variables[] = {0, 1,  2,  3,  4,  5,  6,  7,
					8, 12, 13, 16, 18, 22, 23, 24};
	for (size_t i = 0; i < sizeof variables / sizeof variables[0]; ++i) {
		for (int on = 0; on < 2; ++on) {
			fresh_world();
			g_mission_flight_groups[0].fg.special_cargo_craft = 3;
			struct craft_data *craft = place_craft(0, 0, 0);
			craft->hull_max = 400;
			craft->working_subsystems = 0xFFFF;
			g_test_mobiles[0].team = 0;
			set_craft_state(craft, variables[i], on);
			XVT_ASSERT_INT_EQ(
				mission_object_matches_trigger_variable(
					0, 7, (uint16_t)variables[i]),
				on);
		}
	}
	/* Owned or not. */
	fresh_world();
	place_craft(0, 0, 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 9), 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 10), 1);
	g_test_objects[0].player_owner_idx = 1;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 9), 1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 10), 0);
	/* Never matching variables and one past the list. */
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 15), 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 17), 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 30), 0);

	/* No warheads left: the ammunition of each loaded launcher's first
	 * and last slot is added up. */
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	craft->model_index = (uint16_t)get_model_index_from_type(
		(object_type_id)g_test_objects[0].object_type);
	struct model_def *model = &g_model_defs[craft->model_index];
	struct model_def saved = *model;
	model->warhead_launcher_first_slot[0] = 2;
	model->warhead_launcher_last_slot[0] = 3;
	model->warhead_launcher_first_slot[1] = 4;
	model->warhead_launcher_last_slot[1] = 5;
	craft->warhead_launcher_count = 2;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 25), 1);
	craft->weapon_slots[3].ammo_count = 1;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 25), 0);
	craft->weapon_slots[3].ammo_count = 0;
	craft->weapon_slots[4].ammo_count = 2;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 25), 0);
	craft->weapon_slots[4].ammo_count = 0;
	craft->weapon_slots[5].ammo_count = 1;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 25), 0);
	craft->warhead_launcher_count = 1;
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 25), 1);
	*model = saved;
}

/* ---- mission_update_flight_group_arrivals ------------------------------ */

/* Condition numbers that stand in for an arrival or departure trigger that
 * holds and one that has failed, once the any-team primary goal flag is set
 * (it is open at the start, so both are undecided then). */
enum {
	TRIGGER_HOLDS = MISSION_COND_PRIMARY_GOAL_COMPLETE,
	TRIGGER_FAILED = MISSION_COND_PRIMARY_GOAL_FAILED,
};

/* Group fg of the stored mission waits for a trigger: the first arrival pair's
 * first condition, joined by AND to an always-true one. */
static void arrival_waits_for(int fg, int condition)
{
	struct xvt_flight_group *group = &g_stored.groups[fg];
	memset(group->arrival_triggers, 0, sizeof group->arrival_triggers);
	group->arrival_triggers[0].triggers[0].condition = (uint8_t)condition;
}

/* Starts the flight of the stored mission with the sound and cockpit
 * resources counted as loaded, the log empty and every timer run out. */
static void begin_arrivals(void)
{
	init_world();
	g_flight_conf_no_pilot = 1;
	g_active_flight_player_count = 1;
	start_flight();
	g_log_line_count = 0;
	memset(&g_flight_global_countdown_timers, 0,
	       sizeof g_flight_global_countdown_timers);
}

/* The mission of the arrival checks: group 0 is two craft of team 0 that
 * arrive at once with one more round to come; the groups from 1 on are two
 * craft of team 1 each, set up by the check. */
static void arrivals_mission(int waiting_groups)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 0, TEST_XWING, 2);
	g_stored.groups[0].number_of_waves = 1;
	for (int fg = 1; fg <= waiting_groups; ++fg) {
		arriving_group(fg, 1, TEST_XWING, 2);
		arrival_waits_for(fg, TRIGGER_HOLDS);
	}
}

static int craft_count_in_group(int fg)
{
	int count = 0;
	for (int slot = 0; slot < 32; ++slot) {
		if (g_object_table[slot].object_type != 0 &&
		    g_object_table[slot].flight_group_idx == fg) {
			++count;
		}
	}
	return count;
}

/* The arrival trigger scan runs only when its timer is 0 and sets it to 236;
 * the delay scan likewise. A group that has not arrived, whose triggers do
 * not hold, is left as it is. */
static void check_arrivals_scan_timers(void)
{
	arrivals_mission(1);
	begin_arrivals();
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 9;
	g_flight_global_countdown_timers.mission_arrival_delay_scan_timer = 8;
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].arrival_delay_pending, 0);
	XVT_ASSERT_INT_EQ(g_flight_global_countdown_timers
				  .mission_arrival_trigger_scan_timer,
			  9);
	XVT_ASSERT_INT_EQ(g_flight_global_countdown_timers
				  .mission_arrival_delay_scan_timer,
			  8);
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].arrival_delay_pending, 1);
	XVT_ASSERT_INT_EQ(g_flight_global_countdown_timers
				  .mission_arrival_trigger_scan_timer,
			  SIMULATION_TICKS_PER_SECOND);
	XVT_ASSERT_INT_EQ(g_flight_global_countdown_timers
				  .mission_arrival_delay_scan_timer,
			  8);
	g_flight_global_countdown_timers.mission_arrival_delay_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_flight_global_countdown_timers
				  .mission_arrival_delay_scan_timer,
			  SIMULATION_TICKS_PER_SECOND);
}

/* A waiting group whose two arrival pairs are joined by OR needs one to hold,
 * joined by AND needs both; it then waits out its delay, and the log gives
 * both pairs' results. */
static void check_arrivals_trigger_joins(void)
{
	static const struct {
		int first;
		int second;
		int or_joined;
		int scheduled;
	} rows[] = {
		{TRIGGER_HOLDS, TRIGGER_HOLDS, 0, 1},
		{TRIGGER_HOLDS, TRIGGER_FAILED, 0, 0},
		{TRIGGER_FAILED, TRIGGER_HOLDS, 0, 0},
		{TRIGGER_FAILED, TRIGGER_FAILED, 0, 0},
		{TRIGGER_HOLDS, TRIGGER_HOLDS, 1, 1},
		{TRIGGER_HOLDS, TRIGGER_FAILED, 1, 1},
		{TRIGGER_FAILED, TRIGGER_HOLDS, 1, 1},
		{TRIGGER_FAILED, TRIGGER_FAILED, 1, 0},
	};
	arrivals_mission(8);
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		int fg = (int)i + 1;
		arrival_waits_for(fg, rows[i].first);
		g_stored.groups[fg].arrival_triggers[1].triggers[0].condition =
			(uint8_t)rows[i].second;
		g_stored.groups[fg].arrivals12_or_arrivals34 =
			(uint8_t)rows[i].or_joined;
	}
	begin_arrivals();
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	mission_update_flight_group_arrivals();
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[i + 1].arrival_delay_pending,
			rows[i].scheduled);
	}
	/* A pair that has failed reads 0, not met. */
	XVT_ASSERT_TRUE(line_holds("mission.arrival_scheduled fg=1 ",
				   "first=1 second=1 either=0"));
	XVT_ASSERT_TRUE(line_holds("mission.arrival_scheduled fg=6 ",
				   "first=1 second=0 either=1"));
	XVT_ASSERT_TRUE(line_holds("mission.arrival_scheduled fg=7 ",
				   "first=0 second=1 either=1"));
	XVT_ASSERT_INT_EQ(count_lines("mission.arrival_scheduled"), 4);
}

/* A group's arrival delay is its fixed delay in minutes and seconds plus
 * half its random delay; with random variation on, a draw below the random
 * delay instead. A group that waits for a human (arrive_only_if_human) and
 * has none is not scheduled; one a player owns is. A group that cannot
 * arrive, or has no craft to send, is left alone. */
static void check_arrivals_delay_set(void)
{
	arrivals_mission(6);
	struct xvt_flight_group *plain = &g_stored.groups[1];
	plain->arrival_delay_minutes = 1;
	plain->arrival_delay_seconds = 5;
	plain->arrival_rand_delay_minutes = 0;
	plain->arrival_rand_delay_seconds = 10;
	struct xvt_flight_group *varied = &g_stored.groups[2];
	varied->arrival_delay_seconds = 3;
	varied->arrival_rand_delay_minutes = 1;
	varied->arrival_rand_delay_seconds = 40;
	g_stored.groups[3].arrive_only_if_human = 1;
	g_stored.groups[4].arrive_only_if_human = 1;
	g_stored.groups[4].player_number = 5;
	begin_arrivals();
	g_flight_mission_state.runtime.global_primary_goal_status = 1;
	g_mission_flight_groups[4].player_owner_idx = 1;
	g_mission_fg_stats[5].arrival_enabled = 0;
	g_mission_fg_stats[6].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 0;
	g_flight_mission_state.random_variation_enabled = 0;
	/* The delay scan waits, so the delay is read as it was set. */
	g_flight_global_countdown_timers.mission_arrival_delay_scan_timer = 99;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].arrival_delay_timer, 70);
	XVT_ASSERT_TRUE(
		line_holds("mission.arrival_scheduled fg=1 ",
			   "delay=70 fixed=65 random=5 range=10 varied=0"));
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[2].arrival_delay_timer, 53);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[3].arrival_delay_pending, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[4].arrival_delay_pending, 1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[5].arrival_delay_pending, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[6].arrival_delay_pending, 0);

	/* With variation on, the draw is below the random delay: 100. */
	for (int fg = 1; fg <= 2; ++fg) {
		g_mission_fg_stats[fg].arrival_delay_pending = 0;
	}
	g_flight_mission_state.random_variation_enabled = 1;
	const unsigned int moduli[] = {10};
	const unsigned int wanted[] = {7};
	seed_for_draws(moduli, wanted, 1);
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].arrival_delay_timer, 72);
	XVT_ASSERT_TRUE(
		line_holds("mission.arrival_scheduled fg=1 ",
			   "delay=72 fixed=65 random=7 range=10 varied=1"));
	XVT_ASSERT_TRUE(g_mission_fg_stats[2].arrival_delay_timer >= 3 &&
			g_mission_fg_stats[2].arrival_delay_timer < 103);
}

/* A scheduled arrival counts down by one a second; at 0 it starts when the
 * region has room for the group's craft: the group has arrived, its craft
 * are in the table, and the rounds left are its number of waves. With no
 * room it waits at 0 and says so. */
static void check_arrivals_delay_runs_out(void)
{
	arrivals_mission(1);
	g_stored.groups[1].number_of_waves = 2;
	begin_arrivals();
	g_mission_fg_stats[1].arrival_delay_pending = 1;
	g_mission_fg_stats[1].arrival_delay_timer = 2;
	for (int left = 1; left >= 0; --left) {
		g_flight_global_countdown_timers
			.mission_arrival_delay_scan_timer = 0;
		g_flight_global_countdown_timers
			.mission_arrival_trigger_scan_timer = 5;
		mission_update_flight_group_arrivals();
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].arrival_delay_timer,
				  left);
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].has_arrived, 0);
	}
	XVT_ASSERT_TRUE(
		line_holds("mission.arrival_countdown fg=1 ", "left=0"));
	XVT_ASSERT_INT_EQ(count_lines("mission.arrival_countdown fg=1 "), 2);
	/* No room: the group needs 100 places. */
	g_mission_flight_groups[1].fg.number_of_craft = 100;
	g_flight_global_countdown_timers.mission_arrival_delay_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].has_arrived, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].arrival_delay_timer, 0);
	XVT_ASSERT_TRUE(
		line_holds("mission.wave_no_room fg=1 needed=100 ", ""));
	g_mission_flight_groups[1].fg.number_of_craft = 2;
	g_flight_global_countdown_timers.mission_arrival_delay_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].has_arrived, 1);
	XVT_ASSERT_INT_EQ(craft_count_in_group(1), 2);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].waves_remaining, 2);
	XVT_ASSERT_TRUE(line_holds("mission.group_arrived fg=1 ",
				   "kind=\"craft\" ordinal=-1 waves=2"));
}

/* Remove the craft of flight group fg from the table, as if they were gone. */
static void remove_group_craft(int fg)
{
	for (int slot = 0; slot < 32; ++slot) {
		if (g_object_table[slot].object_type != 0 &&
		    g_object_table[slot].flight_group_idx == fg) {
			g_object_table[slot].object_type = 0;
		}
	}
}

/* A group that has arrived, has rounds to come and has none of its craft left
 * sends its next round when there is room, and a round fewer remain; a craft
 * that is breaking up or exploding no longer counts; a group with a craft in
 * flight, no rounds left, a player owner or no room waits. */
static void check_arrivals_next_round(void)
{
	arrivals_mission(0);
	begin_arrivals();
	XVT_ASSERT_INT_EQ(craft_count_in_group(0), 2);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);

	/* A craft breaking up does not count, but the other one still does. */
	int first = craft_slot(0, 0);
	int second = craft_slot(0, 1);
	g_object_table[first].mobj->p_craft->object_kind =
		CRAFT_OBJECT_KIND_BREAKING_UP;
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	g_object_table[second].mobj->p_craft->object_kind =
		CRAFT_OBJECT_KIND_EXPLODING;

	/* Both gone: a player owner, no room and no rounds left each hold the
	 * group back. */
	g_mission_flight_groups[0].player_owner_idx = 0;
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	g_mission_flight_groups[0].player_owner_idx = -1;
	g_mission_flight_groups[0].fg.number_of_craft = 100;
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	g_mission_flight_groups[0].fg.number_of_craft = 2;
	g_mission_fg_stats[0].waves_remaining = 0;
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);
	g_mission_fg_stats[0].waves_remaining = 1;

	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  4);
	XVT_ASSERT_INT_EQ(craft_count_in_group(0), 4);
	XVT_ASSERT_TRUE(line_holds("mission.wave_arrived fg=0 ",
				   "kind=\"craft\" waves=0 arrived=4"));
}

/* A group a player number is set for, that no player flies, does not send
 * another round while its team's primary and prevent goals are met and met,
 * met and failed, or failed and met; with other states it does. */
static void check_arrivals_player_group_held(void)
{
	static const struct {
		int primary;
		int prevent;
		int sent;
	} rows[] = {
		{1, 1, 0}, {1, 2, 0}, {2, 1, 0}, {1, 0, 1},
		{0, 1, 1}, {2, 2, 1}, {0, 0, 1},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		arrivals_mission(0);
		g_stored.groups[0].player_number = 3;
		begin_arrivals();
		g_flight_mission_state.runtime.team_goal_status[0][0] =
			(uint8_t)rows[i].primary;
		g_flight_mission_state.runtime.team_goal_status[0][1] =
			(uint8_t)rows[i].prevent;
		remove_group_craft(0);
		mission_update_flight_group_arrivals();
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			rows[i].sent ? 4 : 2);
	}
}

/* The rule that stops a group's arrivals: the departure trigger holding, or
 * the group's stop_arriving_when rule (1 a craft of the group departed, 2 the
 * team's primary goal complete, 3 its primary failed or its prevent goal
 * complete). Craft that never arrived are counted as arrived, not departed,
 * not inspected, not disabled, not captured, not attacked, not boarded, and
 * as left the region, or as dependent on the primary mothership when the
 * group departs into one; no round is left, and the special cargo craft are
 * counted the same way. */
static void check_arrivals_stop_rules(void)
{
	static const struct {
		int rule;
		int departed;
		int primary;
		int prevent;
		int departure_trigger;
		int stops;
	} rows[] = {
		{0, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 0},
		{2, 0, 1, 0, 0, 1}, {2, 0, 2, 0, 0, 0}, {3, 0, 2, 0, 0, 1},
		{3, 0, 0, 1, 0, 1}, {3, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 1, 1},
		{0, 0, 0, 0, 2, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		for (int mothership = 0; mothership < 2; ++mothership) {
			arrivals_mission(0);
			g_stored.groups[0].stop_arriving_when =
				(uint8_t)rows[i].rule;
			g_stored.groups[0].departure_method =
				(uint8_t)mothership;
			begin_arrivals();
			g_flight_mission_state.runtime
				.global_primary_goal_status = 1;
			/* The waves of a player's group being unlimited makes
			 * no difference to a group no player owns. */
			g_flight_mission_state.player_flight_group_wave_mode =
				CRAFT_WAVES_UNLIMITED;
			g_mission_flight_groups[0].fg.special_cargo_craft = 1;
			g_mission_fg_stats[0].special_cargo_outcome
				[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
			g_mission_fg_stats[0].special_cargo_outcome
				[FLIGHT_GROUP_OUTCOME_ARRIVED] = 1;
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DEPARTED] =
				(uint16_t)rows[i].departed;
			g_flight_mission_state.runtime.team_goal_status[0][0] =
				(uint8_t)rows[i].primary;
			g_flight_mission_state.runtime.team_goal_status[0][1] =
				(uint8_t)rows[i].prevent;
			if (rows[i].departure_trigger != 0) {
				g_mission_flight_groups[0]
					.fg.departure_trigger.triggers[0]
					.condition =
					rows[i].departure_trigger == 1
						? TRIGGER_HOLDS
						: TRIGGER_FAILED;
			}
			remove_group_craft(0);
			uint16_t *counts = g_mission_fg_stats[0].outcome_count;
			uint8_t *special =
				g_mission_fg_stats[0].special_cargo_outcome;
			uint16_t total = counts[FLIGHT_GROUP_OUTCOME_TOTAL];
			uint16_t arrived = counts[FLIGHT_GROUP_OUTCOME_ARRIVED];
			mission_update_flight_group_arrivals();
			uint16_t missing = (uint16_t)(total - arrived);
			/* Stopped, the missing craft count as arrived; not
			 * stopped, the next round arrives: the same total. */
			XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_ARRIVED],
					  total);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
				rows[i].stops ? missing : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED],
				rows[i].stops ? missing : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_DISABLED],
				rows[i].stops ? missing : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED],
				rows[i].stops ? missing : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED],
				rows[i].stops ? missing : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_BOARDED],
				rows[i].stops ? missing : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT],
				rows[i].stops && mothership ? missing : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION],
				rows[i].stops && !mothership ? missing : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
				rows[i].stops ? 1 : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED],
				rows[i].stops ? 1 : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_DISABLED],
				rows[i].stops ? 1 : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED],
				rows[i].stops ? 1 : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED],
				rows[i].stops ? 1 : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_BOARDED],
				rows[i].stops ? 1 : 0);
			XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining,
					  0);
			XVT_ASSERT_INT_EQ(
				count_lines("mission.group_arrivals_stopped"),
				rows[i].stops);
			if (rows[i].stops != 0) {
				char fields[48];
				snprintf(fields, sizeof fields,
					 "rule=%d count=%d special=1",
					 rows[i].rule, (int)missing);
				XVT_ASSERT_TRUE(line_holds(
					"mission.group_arrivals_stopped fg=0 ",
					fields));
				XVT_ASSERT_TRUE(line_holds(
					"mission.arrivals_stop_reason fg=0 ",
					mothership
						? "counted=\"mothership_dependent\""
						: "counted=\"left_region\""));
			}
		}
	}
}

/* ---- mission_process_flight_group_wave_completion ---------------------- */

/* Sends group 0's next round once its current one is gone: nothing happens
 * with no rounds or no craft to come, or while a craft of the group is flying
 * (one breaking up or exploding does not count). */
static void check_wave_completion_waits(void)
{
	arrivals_mission(0);
	begin_arrivals();
	uint16_t *arrived =
		&g_mission_fg_stats[0]
			 .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED];
	mission_process_flight_group_wave_completion(0);
	XVT_ASSERT_INT_EQ(*arrived, 2);

	int first = craft_slot(0, 0);
	int second = craft_slot(0, 1);
	g_object_table[first].mobj->p_craft->object_kind =
		CRAFT_OBJECT_KIND_EXPLODING;
	mission_process_flight_group_wave_completion(0);
	XVT_ASSERT_INT_EQ(*arrived, 2);
	g_object_table[second].mobj->p_craft->object_kind =
		CRAFT_OBJECT_KIND_BREAKING_UP;

	g_mission_fg_stats[0].waves_remaining = 0;
	mission_process_flight_group_wave_completion(0);
	XVT_ASSERT_INT_EQ(*arrived, 2);
	g_mission_fg_stats[0].waves_remaining = 1;
	uint16_t total =
		g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL];
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 0;
	mission_process_flight_group_wave_completion(0);
	XVT_ASSERT_INT_EQ(*arrived, 2);
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = total;

	mission_process_flight_group_wave_completion(0);
	XVT_ASSERT_INT_EQ(*arrived, 4);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 0);
	XVT_ASSERT_INT_EQ(g_current_flight_group_idx, 0);
	XVT_ASSERT_INT_EQ(craft_count_in_group(0), 4);
	XVT_ASSERT_INT_EQ(count_lines("mission.wave_room_made"), 0);
}

/* The next round is held back while the team's primary and prevent goals are
 * met and met, met and failed, or failed and met, and the hold is logged. */
static void check_wave_completion_held(void)
{
	static const struct {
		int primary;
		int prevent;
		int held;
	} rows[] = {{1, 1, 1}, {1, 2, 1}, {2, 1, 1}, {1, 0, 0}, {2, 2, 0}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		arrivals_mission(0);
		begin_arrivals();
		remove_group_craft(0);
		g_flight_mission_state.runtime.team_goal_status[0][0] =
			(uint8_t)rows[i].primary;
		g_flight_mission_state.runtime.team_goal_status[0][1] =
			(uint8_t)rows[i].prevent;
		mission_process_flight_group_wave_completion(0);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			rows[i].held ? 2 : 4);
		XVT_ASSERT_INT_EQ(count_lines("mission.wave_held fg=0 "),
				  rows[i].held);
		if (rows[i].held != 0) {
			char fields[40];
			snprintf(fields, sizeof fields,
				 "team=0 primary=%d prevent=%d",
				 rows[i].primary, rows[i].prevent);
			XVT_ASSERT_TRUE(
				line_holds("mission.wave_held fg=0 ", fields));
		}
	}
}

/* The rules that stop a group's arrivals hold for the next round as for the
 * first (see the arrivals checks): the round does not come, the craft that
 * never arrived are counted as arrived, not departed and left the region (or
 * dependent on the mothership), and the stop is logged. In the mode where a
 * player's group has unlimited rounds the rules are not looked at. */
static void check_wave_completion_stops(void)
{
	static const struct {
		int rule;
		int departed;
		int primary;
		int prevent;
		int departure_trigger;
		int unlimited;
		int stops;
	} rows[] = {
		{1, 1, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 0, 0},
		{2, 0, 1, 0, 0, 0, 1}, {2, 0, 2, 0, 0, 0, 0},
		{3, 0, 2, 0, 0, 0, 1}, {3, 0, 0, 1, 0, 0, 1},
		{3, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 1},
		{0, 0, 0, 0, 2, 0, 0}, {2, 0, 1, 0, 1, 1, 0},
		{3, 0, 0, 1, 1, 1, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		for (int mothership = 0; mothership < 2; ++mothership) {
			arrivals_mission(0);
			g_stored.groups[0].stop_arriving_when =
				(uint8_t)rows[i].rule;
			g_stored.groups[0].alternate_mothership_used =
				(uint8_t)mothership;
			begin_arrivals();
			g_flight_mission_state.runtime
				.global_primary_goal_status = 1;
			g_flight_mission_state.player_flight_group_wave_mode =
				rows[i].unlimited ? CRAFT_WAVES_UNLIMITED
						  : CRAFT_WAVES_DEFAULT;
			g_mission_flight_groups[0].fg.special_cargo_craft = 1;
			g_mission_fg_stats[0].special_cargo_outcome
				[FLIGHT_GROUP_OUTCOME_TOTAL] = 3;
			g_mission_fg_stats[0].special_cargo_outcome
				[FLIGHT_GROUP_OUTCOME_ARRIVED] = 1;
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DEPARTED] =
				(uint16_t)rows[i].departed;
			g_flight_mission_state.runtime.team_goal_status[0][0] =
				(uint8_t)rows[i].primary;
			g_flight_mission_state.runtime.team_goal_status[0][1] =
				(uint8_t)rows[i].prevent;
			if (rows[i].departure_trigger != 0) {
				g_mission_flight_groups[0]
					.fg.departure_trigger.triggers[0]
					.condition =
					rows[i].departure_trigger == 1
						? TRIGGER_HOLDS
						: TRIGGER_FAILED;
			}
			remove_group_craft(0);
			uint16_t *counts = g_mission_fg_stats[0].outcome_count;
			uint8_t *special =
				g_mission_fg_stats[0].special_cargo_outcome;
			mission_process_flight_group_wave_completion(0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_DISABLED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_NOT_BOARDED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT],
				rows[i].stops && mothership ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				counts[FLIGHT_GROUP_OUTCOME_LEFT_REGION],
				rows[i].stops && !mothership ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(
				special[FLIGHT_GROUP_OUTCOME_NOT_BOARDED],
				rows[i].stops ? 2 : 0);
			XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_ARRIVED],
					  4);
			XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining,
					  0);
			XVT_ASSERT_INT_EQ(craft_count_in_group(0),
					  rows[i].stops ? 0 : 2);
			XVT_ASSERT_INT_EQ(
				count_lines("mission.group_arrivals_stopped"),
				rows[i].stops);
			if (rows[i].stops != 0) {
				char fields[48];
				snprintf(fields, sizeof fields,
					 "rule=%d count=2 special=2",
					 rows[i].rule);
				XVT_ASSERT_TRUE(line_holds(
					"mission.group_arrivals_stopped fg=0 ",
					fields));
				XVT_ASSERT_TRUE(line_holds(
					"mission.arrivals_stop_reason fg=0 ",
					mothership
						? "counted=\"mothership_dependent\""
						: "counted=\"left_region\""));
			}
		}
	}
}

/* Make room for a round with more craft than the table has free places:
 * the objects of the explosion genus that no player owns go first, then the
 * craft that are breaking up or exploding and that no player owns, each
 * counted as destroyed in its group; the round then comes. Here the group of
 * 32 craft finds 30 places free, and its two old craft are an explosion and
 * a craft breaking up. When too few can be freed the log counts what is
 * missing. */
static void check_wave_completion_makes_room(void)
{
	static const struct {
		int craft;
		int free;
		int freed;
		int missing;
	} rows[] = {{32, 29, 2, 1},
		    {40, 29, 2, 9},
		    {31, 29, 2, 0},
		    {30, 29, 1, 0},
		    {29, 29, 0, 0}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		arrivals_mission(0);
		/* Another group's craft is flying: it is never taken. */
		arriving_group(1, 1, TEST_XWING, 1);
		begin_arrivals();
		int first = craft_slot(0, 0);
		int second = craft_slot(0, 1);
		g_object_table[first].mobj->p_craft->object_kind =
			CRAFT_OBJECT_KIND_EXPLODING;
		g_object_table[first].genus_id = CRAFT_GENUS_EXPLOSION;
		g_object_table[second].mobj->p_craft->object_kind =
			CRAFT_OBJECT_KIND_BREAKING_UP;
		g_mission_flight_groups[0].fg.number_of_craft =
			(uint8_t)rows[i].craft;
		g_log_line_count = 0;
		mission_process_flight_group_wave_completion(0);
		if (rows[i].craft > rows[i].free) {
			char fields[64];
			snprintf(fields, sizeof fields,
				 "needed=%d free=%d freed=%d missing=%d",
				 rows[i].craft, rows[i].free, rows[i].freed,
				 rows[i].missing);
			XVT_ASSERT_TRUE(line_holds(
				"mission.wave_room_made fg=0 ", fields));
		} else {
			XVT_ASSERT_INT_EQ(count_lines("mission.wave_room_made"),
					  0);
		}
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
			rows[i].freed);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			4);
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 0);
		XVT_ASSERT_INT_EQ(craft_count_in_group(1), 1);
	}
	/* A player-owned explosion or craft is not taken. */
	arrivals_mission(0);
	begin_arrivals();
	int owned = craft_slot(0, 0);
	g_object_table[owned].mobj->p_craft->object_kind =
		CRAFT_OBJECT_KIND_EXPLODING;
	g_object_table[owned].player_owner_idx = 0;
	g_object_table[craft_slot(0, 1)].mobj->p_craft->object_kind =
		CRAFT_OBJECT_KIND_BREAKING_UP;
	g_mission_flight_groups[0].fg.number_of_craft = 31;
	mission_process_flight_group_wave_completion(0);
	XVT_ASSERT_INT_EQ(g_object_table[owned].object_type != 0, 1);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[0]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
		1);
}

/* ---- mission_resolve_formation_slot_world_loc -------------------------- */

/* Gives the craft type of the group an object type whose largest box corner
 * has z = 12 without reading a model: the box cache is filled by hand. */
static void set_box_cache(int object_type)
{
	g_model_bounds_cached[object_type] = 1;
	memset(&g_model_bounds_max[object_type], 0,
	       sizeof g_model_bounds_max[object_type]);
	g_model_bounds_max[object_type].z = 12.0f;
}

/* The offsets of formation place `slot` in model bounds of 40, 64 and 20: the
 * place's offsets grown with the formation spacing, by half a bound (a quarter
 * for the forward one) more at spacing 0. */
static void place_offsets(int formation, int slot, int spacing, int *side,
			  int *up, int *forward)
{
	int x = g_form_pos_x[formation][slot];
	int y = g_form_pos_y[formation][slot];
	int z = g_form_pos_z[formation][slot];
	int scale = spacing + 1;
	*side = scale * x * 40;
	*forward = scale * y * 64;
	*up = scale * z * 20;
	if (scale == 1) {
		*side += x * (40 / 2);
		*forward += y * (64 / 4);
		*up += z * (20 / 2);
	}
}

/* A craft of formation place `slot` stands at the group's current mission
 * point plus the place's offsets (see check_spawn_formation), turned by the
 * basis object's orientation, and raised by the model's largest box z. With
 * no basis (0xFFFF) the offset is 0. The basis here halves each axis, so the
 * side offset ends in x, the forward one in y and the up one in z; a bound
 * shift doubles the turned offset per step. */
static void check_formation_slot_world_loc(void)
{
	static const struct {
		int formation;
		int slot;
		int spacing;
		int shift;
	} rows[] = {
		{0, 0, 0, 0}, {4, 2, 0, 0}, {4, 5, 1, 0},  {15, 3, 2, 0},
		{4, 2, 0, 1}, {8, 4, 1, 2}, {13, 1, 0, 0}, {33, 5, 3, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		fresh_world();
		struct xvt_flight_group *group = &g_mission_flight_groups[0].fg;
		group->mission_point_x[0] = 10;
		group->mission_point_y[0] = 20;
		group->mission_point_z[0] = 30;
		group->formation = (uint8_t)rows[i].formation;
		group->formation_spacing = (uint8_t)rows[i].spacing;
		g_mission_fg_stats[0].current_mission_point_ref = 0x8000;
		uint8_t object_type = g_craft_type_to_object_type[TEST_XWING];
		set_box_cache(object_type);
		struct model_def *model = model_of(object_type);
		struct model_def saved = *model;
		model->bound_size_x = 40;
		model->bound_size_y = 64;
		model->bound_size_z = 20;
		model->bound_size_shift = (uint8_t)rows[i].shift;
		struct mobile_object *basis = &g_test_mobiles[3];
		basis->orient_matrix_dirty = 0;
		basis->cached_side_x = 0x4000;
		basis->cached_fwd_y = 0x4000;
		basis->cached_up_z = 0x4000;

		mission_resolve_formation_slot_world_loc(
			0, (uint16_t)rows[i].slot, UINT16_MAX);
		XVT_ASSERT_INT_EQ(g_world_loc_x, 10 * 256);
		XVT_ASSERT_INT_EQ(g_world_loc_y, -20 * 256);
		XVT_ASSERT_INT_EQ(g_world_loc_z, 30 * 256 + 12);

		int side;
		int up;
		int forward;
		place_offsets(rows[i].formation, rows[i].slot, rows[i].spacing,
			      &side, &up, &forward);
		mission_resolve_formation_slot_world_loc(
			0, (uint16_t)rows[i].slot, 3);
		int doubled = 1 << rows[i].shift;
		XVT_ASSERT_INT_EQ(g_rotated_x, side / 2 * doubled);
		XVT_ASSERT_INT_EQ(g_rotated_y, forward / 2 * doubled);
		XVT_ASSERT_INT_EQ(g_rotated_z, up / 2 * doubled);
		XVT_ASSERT_INT_EQ(g_world_loc_x, 10 * 256 + side / 2 * doubled);
		XVT_ASSERT_INT_EQ(g_world_loc_y,
				  -20 * 256 + forward / 2 * doubled);
		XVT_ASSERT_INT_EQ(g_world_loc_z,
				  30 * 256 + up / 2 * doubled + 12);
		*model = saved;
	}
}

/* Known failure formation_slot_past_six, issue #138: a formation has six
 * places, and the seventh craft of a group (place 6) takes a place of its own,
 * not the first place of the next formation. Here the seventh place of a group
 * in formation 4 must not be where the first place of a group in formation 5
 * is, the two groups starting from the same mission point. */
static void check_formation_slot_past_six(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.formation = 4;
	g_mission_flight_groups[1].fg.formation = 5;
	for (int fg = 0; fg < 2; ++fg) {
		g_mission_fg_stats[fg].current_mission_point_ref = 0x8000;
	}
	uint8_t object_type = g_craft_type_to_object_type[TEST_XWING];
	set_box_cache(object_type);
	struct model_def *model = model_of(object_type);
	struct model_def saved = *model;
	model->bound_size_x = 40;
	model->bound_size_y = 60;
	model->bound_size_z = 20;
	struct mobile_object *basis = &g_test_mobiles[3];
	basis->orient_matrix_dirty = 0;
	basis->cached_side_x = 0x4000;
	basis->cached_fwd_y = 0x4000;
	basis->cached_up_z = 0x4000;
	mission_resolve_formation_slot_world_loc(0, 6, 3);
	int seventh[3] = {g_world_loc_x, g_world_loc_y, g_world_loc_z};
	mission_resolve_formation_slot_world_loc(1, 0, 3);
	*model = saved;
	XVT_ASSERT_TRUE(seventh[0] != g_world_loc_x ||
			seventh[1] != g_world_loc_y ||
			seventh[2] != g_world_loc_z);
}

/* The first craft type of a static type, from 1, whose object type has the
 * given genus (or, for -1, a genus other than a mine's or a satellite's), or
 * 0. */
static int static_craft_of_genus(int genus)
{
	for (int type = 1; type < 96; ++type) {
		uint8_t object_type = g_craft_type_to_object_type[type];
		if (object_type != 0 &&
		    (g_object_type_table[object_type].behavior_flags & 0x80) !=
			    0 &&
		    (genus < 0 ? (g_object_type_table[object_type].genus_id !=
					  CRAFT_GENUS_MINE &&
				  g_object_type_table[object_type].genus_id !=
					  CRAFT_GENUS_SATELLITE)
			       : g_object_type_table[object_type].genus_id ==
					 genus)) {
			return type;
		}
	}
	return 0;
}

/* A static group stands where its kind puts it: genus 9 at the group's first
 * mission point, a mine at its cell of the grid (the cells of
 * mission_spawn_flight_group_static_objects, here 3 a side in the plane the
 * status picks), any other kind, and a mine's cell past the grid, at the
 * group's current mission point; each raised by the box's z. */
static void check_formation_slot_static(void)
{
	static const struct {
		int status;
		int x_step;
		int y_step;
		int z_row_step;
		int z_column_step;
	} planes[4] = {{0, 64, 64, 0, 0},
		       {1, 0, 64, 0, 64},
		       {2, 64, 0, 64, 0},
		       {3, 0, 0, 0, 0}};
	for (int plane = 0; plane < 4; ++plane) {
		static_mission(CRAFT_SPECIES_MINE_TYPE_A, 3,
			       planes[plane].status);
		uint8_t object_type =
			g_craft_type_to_object_type[CRAFT_SPECIES_MINE_TYPE_A];
		set_box_cache(object_type);
		for (int cell = 0; cell < 9; ++cell) {
			int row = cell / 3;
			int column = cell % 3;
			mission_resolve_formation_slot_world_loc(
				0, (uint16_t)cell, UINT16_MAX);
			XVT_ASSERT_INT_EQ(g_world_loc_x,
					  (10 - planes[plane].x_step +
					   column * planes[plane].x_step) *
						  256);
			XVT_ASSERT_INT_EQ(g_world_loc_y,
					  (-(20 + planes[plane].y_step) +
					   row * planes[plane].y_step) *
						  256);
			XVT_ASSERT_INT_EQ(
				g_world_loc_z,
				(30 - planes[plane].z_row_step -
				 planes[plane].z_column_step +
				 row * planes[plane].z_row_step +
				 column * planes[plane].z_column_step) *
						256 +
					12);
		}
	}
	/* A cell past the grid: the current mission point (here point 2). */
	static_mission(CRAFT_SPECIES_MINE_TYPE_A, 3, 0);
	set_box_cache(g_craft_type_to_object_type[CRAFT_SPECIES_MINE_TYPE_A]);
	g_mission_flight_groups[0].fg.mission_point_x[1] = 7;
	g_mission_flight_groups[0].fg.mission_point_y[1] = 8;
	g_mission_flight_groups[0].fg.mission_point_z[1] = 9;
	g_mission_fg_stats[0].current_mission_point_ref = 0x8001;
	g_log_line_count = 0;
	mission_resolve_formation_slot_world_loc(0, 9, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_world_loc_x, 7 * 256);
	XVT_ASSERT_INT_EQ(g_world_loc_y, -8 * 256);
	XVT_ASSERT_INT_EQ(g_world_loc_z, 9 * 256 + 12);
	XVT_ASSERT_TRUE(line_holds("mission.mine_slot_outside_grid fg=0 ",
				   "cell=9 size=3"));

	/* Genus 9 stands at the first point, though the current one is the
	 * second; another static kind at the current one. */
	int type9 = static_craft_of_genus(CRAFT_GENUS_SATELLITE);
	int other = static_craft_of_genus(-1);
	if (type9 != 0) {
		static_mission(type9, 1, 0);
		set_box_cache(g_craft_type_to_object_type[type9]);
		g_mission_flight_groups[0].fg.mission_point_x[1] = 7;
		g_mission_fg_stats[0].current_mission_point_ref = 0x8001;
		mission_resolve_formation_slot_world_loc(0, 0, UINT16_MAX);
		XVT_ASSERT_INT_EQ(g_world_loc_x, 10 * 256);
		XVT_ASSERT_INT_EQ(g_world_loc_y, -20 * 256);
		XVT_ASSERT_INT_EQ(g_world_loc_z, 30 * 256 + 12);
	}
	if (other != 0) {
		static_mission(other, 1, 0);
		set_box_cache(g_craft_type_to_object_type[other]);
		g_mission_flight_groups[0].fg.mission_point_x[1] = 7;
		g_mission_flight_groups[0].fg.mission_point_y[1] = 8;
		g_mission_flight_groups[0].fg.mission_point_z[1] = 9;
		g_mission_fg_stats[0].current_mission_point_ref = 0x8001;
		mission_resolve_formation_slot_world_loc(0, 0, UINT16_MAX);
		XVT_ASSERT_INT_EQ(g_world_loc_x, 7 * 256);
		XVT_ASSERT_INT_EQ(g_world_loc_z, 9 * 256 + 12);
	}
	XVT_ASSERT_TRUE(type9 != 0 || other != 0);
}

/* ---- the arrival of a static group and the next round ------------------ */

/* A static group that arrives has arrived and is placed; its rounds left are
 * its number of waves in a melee mission and none in any other. The next
 * round of a static group is placed the same way, and takes one off its
 * rounds. A group a player number is set for does not use up a round while
 * its waves are unlimited. */
static void check_static_group_arrival_and_rounds(void)
{
	for (int melee = 0; melee < 2; ++melee) {
		static_mission(CRAFT_SPECIES_MINE_TYPE_A, 2, 0);
		g_mission_header.mission_type =
			melee ? MISSION_TYPE_MELEE : MISSION_TYPE_COMBAT;
		g_mission_flight_groups[0].fg.number_of_waves = 2;
		g_mission_fg_stats[0].has_arrived = 0;
		XVT_ASSERT_INT_EQ(
			mission_start_flight_group_arrival(UINT16_MAX), 1);
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].has_arrived, 1);
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining,
				  melee ? 2 : 0);
		int slots[64];
		XVT_ASSERT_INT_EQ(static_objects(slots), 4);
		XVT_ASSERT_TRUE(line_holds("mission.group_arrived fg=0 ",
					   "kind=\"static\" ordinal=-1"));
	}
	/* The next round of a static group. */
	static_mission(CRAFT_SPECIES_MINE_TYPE_A, 2, 0);
	g_mission_fg_stats[0].waves_remaining = 2;
	mission_spawn_current_flight_group_wave();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	/* A player group with unlimited waves keeps its round count. */
	g_mission_flight_groups[0].fg.player_number = 1;
	g_flight_mission_state.player_flight_group_wave_mode =
		CRAFT_WAVES_UNLIMITED;
	mission_spawn_current_flight_group_wave();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	g_flight_mission_state.player_flight_group_wave_mode =
		CRAFT_WAVES_DEFAULT;
	mission_spawn_current_flight_group_wave();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 0);
	mission_spawn_current_flight_group_wave();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 0);
}

/* ---- mission_spawn_flight_group_wave_craft after the start ------------- */

/* Marks the mission as started (the clock reads a second) and makes every
 * in-flight message a plain one, for the announcement of an arriving round. */
static void start_the_clock(void)
{
	for (size_t message = 0;
	     message < sizeof g_str_in_flight_messages /
			       sizeof g_str_in_flight_messages[0];
	     ++message) {
		g_str_in_flight_messages[message] = "\006";
	}
	g_mission_elapsed_clock.seconds = 1;
	g_log_line_count = 0;
}

/* The place of craft `ordinal` of group fg, from the table. */
static void craft_place(int fg, int ordinal, int *x, int *y, int *z)
{
	const struct object_record *craft =
		&g_object_table[craft_slot(fg, ordinal)];
	*x = craft->world_x;
	*y = craft->world_y;
	*z = craft->world_z;
}

/* A whole round that comes from hyperspace after the start (a group with no
 * mothership, that no player owns) is put 8 times 65,535 units back along
 * its heading, which is yaw 0 and pitch 0x4000 when the group has no
 * fifth mission point; the spawn says so. A single craft asked for, a round
 * before the start and a round a player owns come at the mission point. */
static void check_spawn_hyperspace_arrival(void)
{
	static const struct {
		int ordinal;
		int started;
		int owner;
		int hyperspace;
	} rows[] = {
		{UINT16_MAX, 1, -1, 1},
		{0, 1, -1, 0},
		{UINT16_MAX, 0, -1, 0},
		{UINT16_MAX, 1, 2, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, SPAWN_TYPE, 2);
		g_stored.groups[0].mission_point_x[0] = 10;
		g_stored.groups[0].mission_point_y[0] = 20;
		g_stored.groups[0].mission_point_z[0] = 30;
		spawn_mission_ready();
		if (rows[i].started != 0) {
			start_the_clock();
		}
		g_mission_flight_groups[0].player_owner_idx = rows[i].owner;
		XVT_ASSERT_INT_EQ(spawn_craft(0, (uint16_t)rows[i].ordinal), 1);
		g_mission_elapsed_clock.seconds = 0;
		int x;
		int y;
		int z;
		craft_place(0, 0, &x, &y, &z);
		int expected_x = 10 * 256;
		int expected_y = -20 * 256;
		int expected_z = 30 * 256;
		if (rows[i].hyperspace != 0) {
			trig2_movexyz(UINT16_MAX, (int16_t)0x8000, 0x4000);
			expected_x += 8 * trig2_xmovedist;
			expected_y += 8 * trig2_ymovedist;
			expected_z += 8 * trig2_zmovedist;
			XVT_ASSERT_TRUE(expected_x != 10 * 256 ||
					expected_y != -20 * 256 ||
					expected_z != 30 * 256);
		}
		XVT_ASSERT_INT_EQ(x, expected_x);
		XVT_ASSERT_INT_EQ(y, expected_y);
		XVT_ASSERT_INT_EQ(z, expected_z);
		char fields[40];
		snprintf(fields, sizeof fields,
			 "started=%d mothership=0 hyperspace=%d",
			 rows[i].started, rows[i].hyperspace);
		XVT_ASSERT_TRUE(
			line_holds("mission.wave_prepared fg=0 ", fields));
		XVT_ASSERT_TRUE(line_holds("mission.wave_prepared fg=0 ",
					   "yaw=0 pitch=16384"));
	}
}

/* A group with a fifth mission point faces it from where it starts, and one
 * without faces yaw 0 and pitch 0x4000. */
static void check_spawn_faces_fifth_point(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 1);
	struct xvt_flight_group *group = &g_stored.groups[0];
	group->mission_point_x[0] = 10;
	group->mission_point_y[0] = 20;
	group->mission_point_z[0] = 30;
	group->mission_point_x[4] = 100;
	group->mission_point_y[4] = 50;
	group->mission_point_z[4] = -40;
	group->mission_point_enabled[4] = 1;
	spawn_mission_ready();
	XVT_ASSERT_INT_EQ(spawn_craft(0, 0), 1);
	trig2_ctop((100 - 10) * 256, -(50 - 20) * 256, (-40 - 30) * 256);
	char fields[48];
	snprintf(fields, sizeof fields, "yaw=%u pitch=%u",
		 (unsigned)(uint16_t)trig2_xyangle,
		 (unsigned)(uint16_t)trig2_pitch);
	XVT_ASSERT_TRUE((uint16_t)trig2_xyangle != 0);
	XVT_ASSERT_TRUE(line_holds("mission.wave_prepared fg=0 ", fields));
}

/* In a version 14 mission, after the start, a player's group with a second
 * mission point starts there, not at the first; before the start it starts at
 * the first. */
static void check_spawn_second_point(void)
{
	for (int started = 0; started < 2; ++started) {
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, SPAWN_TYPE, 1);
		g_stored.groups[0].player_number = 3;
		g_stored.groups[0].mission_point_x[0] = 10;
		g_stored.groups[0].mission_point_y[0] = 20;
		g_stored.groups[0].mission_point_z[0] = 30;
		g_stored.groups[0].mission_point_x[1] = 77;
		g_stored.groups[0].mission_point_y[1] = 88;
		g_stored.groups[0].mission_point_z[1] = 99;
		g_stored.groups[0].mission_point_enabled[1] = 1;
		spawn_mission_ready();
		if (started != 0) {
			start_the_clock();
		}
		XVT_ASSERT_INT_EQ(g_mission_file_version, 14);
		XVT_ASSERT_INT_EQ(spawn_craft(0, 0), 1);
		g_mission_elapsed_clock.seconds = 0;
		int x;
		int y;
		int z;
		craft_place(0, 0, &x, &y, &z);
		XVT_ASSERT_INT_EQ(x, (started ? 77 : 10) * 256);
		XVT_ASSERT_INT_EQ(y, -(started ? 88 : 20) * 256);
		XVT_ASSERT_INT_EQ(z, (started ? 99 : 30) * 256);
	}
}

/* In a melee, after the start, when team 0 has one group a player flies, that
 * group's round starts at the first mission point of the player group its
 * round and player number pick: for player number p and round r (the craft
 * sent so far over the group's craft), player (3 * d + p) mod 8 + 1, where d
 * is r - 3 for an odd p and -3 - r for an even one. A group of another team
 * starts at its own point. */
static void check_spawn_melee_start_points(void)
{
	static const struct {
		int player_number;
		int craft_sent;
		int team;
		int target;
	} rows[] = {
		{1, 0, 0, 1}, {1, 2, 0, 4}, {2, 2, 0, 7},
		{2, 0, 0, 2}, {1, 2, 1, 1},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		init_mission(MISSION_TYPE_MELEE);
		arriving_group(0, rows[i].team, SPAWN_TYPE, 2);
		g_stored.groups[0].player_number =
			(uint8_t)rows[i].player_number;
		/* The other player numbers, on team 1 (and team 2). */
		int group = 1;
		for (int number = 1; number <= 8; ++number) {
			if (number == rows[i].player_number) {
				continue;
			}
			arriving_group(group, 1 + group % 2, SPAWN_TYPE, 1);
			g_stored.groups[group].player_number = (uint8_t)number;
			g_stored.groups[group].mission_point_x[0] =
				(int16_t)(100 * number);
			++group;
		}
		g_stored.groups[0].mission_point_x[0] =
			(int16_t)(100 * rows[i].player_number);
		spawn_mission_ready();
		start_the_clock();
		/* No player owns any of the groups here. */
		for (int fg = 0; fg < 8; ++fg) {
			g_mission_flight_groups[fg].player_owner_idx = -1;
		}
		g_mission_fg_stats[0].spawned_craft_count =
			(uint16_t)rows[i].craft_sent;
		XVT_ASSERT_INT_EQ(spawn_craft(0, 0), 1);
		g_mission_elapsed_clock.seconds = 0;
		int x;
		int y;
		int z;
		craft_place(0, 0, &x, &y, &z);
		XVT_ASSERT_INT_EQ(x, 100 * rows[i].target * 256);
	}
}

/* In a melee, the round of a player's group starts within 0x7FFF units of its
 * point in x and y, by draws of the game's random numbers: an amount, and a
 * sign taken from the next draw's low bit, for x and then for y. */
static void check_spawn_melee_random_offset(void)
{
	for (int combination = 0; combination < 4; ++combination) {
		/* A seed whose draws have the wanted signs: the low bit of the
		 * second draw for x and of the fourth for y. */
		int seed = 1;
		int draws[4];
		for (; seed < 1000; ++seed) {
			g_game_rand_feedback_state = (int16_t)seed;
			for (int i = 0; i < 4; ++i) {
				draws[i] = game_rand();
			}
			if ((draws[1] & 1) == (combination & 1) &&
			    (draws[3] & 1) == (combination >> 1)) {
				break;
			}
		}
		XVT_ASSERT_TRUE(seed < 1000);
		init_mission(MISSION_TYPE_MELEE);
		arriving_group(0, 1, SPAWN_TYPE, 1);
		g_stored.groups[0].player_number = 5;
		g_stored.groups[0].mission_point_x[0] = 10;
		g_stored.groups[0].mission_point_y[0] = 20;
		g_stored.groups[0].mission_point_z[0] = 30;
		spawn_mission_ready();
		start_the_clock();
		g_mission_flight_groups[0].player_owner_idx = 4;
		int expected_x = 10 * 256 + ((draws[1] & 1) != 0 ? 1 : -1) *
						    (draws[0] & 0x7FFF);
		int expected_y = -20 * 256 + ((draws[3] & 1) != 0 ? 1 : -1) *
						     (draws[2] & 0x7FFF);
		g_game_rand_feedback_state = (int16_t)seed;
		XVT_ASSERT_INT_EQ(spawn_craft(0, 0), 1);
		g_mission_elapsed_clock.seconds = 0;
		int x;
		int y;
		int z;
		craft_place(0, 0, &x, &y, &z);
		XVT_ASSERT_INT_EQ(x, expected_x);
		XVT_ASSERT_INT_EQ(y, expected_y);
		XVT_ASSERT_INT_EQ(z, 30 * 256);
	}
}

/* A whole round from a mothership with more than 3 craft keeps formation 6,
 * and with 3 or fewer formation 0. */
static void check_spawn_mothership_formation(void)
{
	for (int count = 3; count <= 4; ++count) {
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, SPAWN_TYPE, count);
		g_stored.groups[0].arrival_method = 1;
		g_stored.groups[0].arrival_mothership = 1;
		g_stored.groups[0].formation = 2;
		arriving_group(1, 1, SPAWN_TYPE, 1);
		spawn_mission_ready();
		/* The followers run the plans the hangar's arrival names. */
		memset(g_builtin_plan_id_by_name_index, 0,
		       sizeof g_builtin_plan_id_by_name_index);
		g_builtin_plan_id_by_name_index
			[g_order_follower_builtin_plan_name_index[0]] = 6;
		g_builtin_plan_id_by_name_index[50] = 8;
		XVT_ASSERT_INT_EQ(spawn_craft(1, UINT16_MAX), 1);
		start_the_clock();
		XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
		g_mission_elapsed_clock.seconds = 0;
		char fields[40];
		snprintf(fields, sizeof fields, "mothership=1 hyperspace=0");
		XVT_ASSERT_TRUE(
			line_holds("mission.wave_prepared fg=0 ", fields));
		snprintf(fields, sizeof fields, "formation=%d spacing=0",
			 count > 3 ? 6 : 0);
		XVT_ASSERT_TRUE(
			line_holds("mission.wave_prepared fg=0 ", fields));
	}
}

/* ---- kill credit, losses and projectile hits --------------------------- */

/* Every in-flight message a plain one of the system pane, which is off. */
static void plain_messages(void)
{
	g_system_message_display_enabled = 0;
	g_replay_view_mode = 0;
	for (size_t i = 0; i < sizeof g_str_in_flight_messages /
				       sizeof g_str_in_flight_messages[0];
	     ++i) {
		g_str_in_flight_messages[i] = g_plain_template;
	}
}

/* kill_credit_world with the game's settings clear (no voices), the in-flight
 * messages plain, an empty log, and the victim's group at AI level 2. Player 0
 * flies group 0 and is not the local player; the victim has taken 100 damage,
 * all from player 0. */
static void credit_world(void)
{
	kill_credit_world();
	memset(&g_game_config, 0, sizeof g_game_config);
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(g_msg_arg_table, 0, sizeof g_msg_arg_table);
	g_log_line_count = 0;
	plain_messages();
	g_msg_sender_iff = 99;
	g_mission_flight_groups[1].fg.group_ai = 2;
	g_players[0].pilot_rating = 1;
}

/* The victim has taken `total` damage, `by_player` of it from player 0. */
static void victim_damage(int total, int by_player)
{
	g_test_craft[1].damage_stats.damage_received_total = total;
	g_test_craft[1].damage_stats.damage_from_player[0] = by_player;
}

/* A player's share of a craft's damage sets the credit's tier: two thirds or
 * more is a full kill (3), about 35 percent a shared one (2), about 5 percent
 * an assist (1), less nothing. A kill counts by the victim's flight group and
 * by the AI level of its group, and adds the kill score (half for a shared
 * kill, a tenth for an assist) to the player's score, doubled at difficulty 2
 * and halved at 0. The rating points of the kill are half and a tenth of the
 * full kill's for a shared kill and an assist. */
static void check_credit_tiers(void)
{
	static const struct {
		int damage;
		int tier;
	} rows[] = {{3, 0},  {4, 0},  {5, 1},  {34, 1},
		    {35, 2}, {66, 2}, {67, 3}, {100, 3}};
	/* The rating points of a full kill. */
	credit_world();
	victim_damage(100, 100);
	mission_credit_destruction_damage_contributors(0, 1);
	int full_points = g_players[0].mission_stats.rating_promo_points;
	XVT_ASSERT_TRUE(full_points > 0);
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		for (int difficulty = 0; difficulty < 3; ++difficulty) {
			credit_world();
			g_flight_mission_state.difficulty = (uint8_t)difficulty;
			victim_damage(100, rows[i].damage);
			int score = mission_compute_kill_score_for_object(1);
			XVT_ASSERT_TRUE(score > 20);
			mission_credit_destruction_damage_contributors(0, 1);
			const struct player_data *player = &g_players[0];
			const struct per_mission_kills *kills =
				&player->per_mission_kills;
			int expected = rows[i].tier == 3   ? score
				       : rows[i].tier == 2 ? score / 2
				       : rows[i].tier == 1 ? score / 10
							   : 0;
			if (rows[i].tier != 0) {
				expected = difficulty == 2   ? expected * 2
					   : difficulty == 0 ? expected / 2
							     : expected;
			}
			XVT_ASSERT_INT_EQ(player->mission_stats.mission_score,
					  expected);
			XVT_ASSERT_INT_EQ(kills->kills_full_on_flight_group[1],
					  rows[i].tier == 3);
			XVT_ASSERT_INT_EQ(
				kills->kills_shared_on_flight_group[1],
				rows[i].tier == 2);
			XVT_ASSERT_INT_EQ(
				kills->kills_assist_on_flight_group[1],
				rows[i].tier == 1);
			XVT_ASSERT_INT_EQ(kills->kills_full_on_ai_rating[2],
					  rows[i].tier == 3);
			XVT_ASSERT_INT_EQ(kills->kills_shared_on_ai_rating[2],
					  rows[i].tier == 2);
			XVT_ASSERT_INT_EQ(kills->kills_assist_on_ai_rating[2],
					  rows[i].tier == 1);
			XVT_ASSERT_INT_EQ(count_lines("battle.kill by=0 "),
					  rows[i].tier != 0);
			if (difficulty != 1) {
				continue;
			}
			int points = player->mission_stats.rating_promo_points;
			if (rows[i].tier == 3) {
				XVT_ASSERT_INT_EQ(points, full_points);
			} else if (rows[i].tier == 2) {
				XVT_ASSERT_INT_EQ(points, full_points / 2);
			} else if (rows[i].tier == 1) {
				XVT_ASSERT_INT_EQ(points, full_points / 10);
			} else if (rows[i].tier == 0) {
				XVT_ASSERT_INT_EQ(points, 0);
			}
		}
	}
	XVT_ASSERT_TRUE(line_holds("battle.kill by=0 victim=1 owner=-1 fg=1 ",
				   "share=\"full\""));
}

/* A per-craft bonus goal for destroying the victim's group is paid with the
 * kill: in full for a full kill, half for a shared one and a tenth for an
 * assist; the kill score is added unless the goal paid less than 0, and then
 * only the goal's penalty counts. */
static void check_credit_goal_scores(void)
{
	static const struct {
		int damage;
		int points;
		int score_share; /* the goal's pay in tenths of its points */
		int kill_share;	 /* the kill score's divisor, 0 for none */
	} rows[] = {
		{100, 4, 10, 1},
		{40, 4, 5, 2},
		{10, 4, 1, 10},
		{100, -3, 10, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		credit_world();
		victim_damage(100, rows[i].damage);
		set_fg_goal(1, 0, 2, MISSION_COND_DESTROYED, 18, rows[i].points,
			    0x1);
		int score = mission_compute_kill_score_for_object(1);
		mission_credit_destruction_damage_contributors(0, 1);
		int goal_pay = 250 * rows[i].points;
		if (goal_pay > 0) {
			goal_pay = goal_pay * rows[i].score_share / 10;
		}
		int expected = goal_pay;
		if (rows[i].kill_share != 0) {
			expected += score / rows[i].kill_share;
		}
		XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
				  expected);
		XVT_ASSERT_INT_EQ(count_lines("mission.kill_score_withheld "),
				  rows[i].kill_share == 0);
	}
}

/* A kill of a craft of an allied team counts as a friendly kill from a shared
 * share up: the kill score comes off the score, 500 off the promotion points,
 * a friendly kill is counted, and the local player is told (281, and 200 for
 * the points); an assist and a kill of an enemy do not. */
static void check_credit_friendly_kill(void)
{
	static const struct {
		int damage;
		int allied;
		int friendly;
	} rows[] = {{100, 1, 1}, {40, 1, 1}, {10, 1, 0}, {100, 0, 0}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		credit_world();
		g_local_player = 0;
		g_players[0].iff = 3;
		g_mission_teams[0].allies[1] = (uint8_t)rows[i].allied;
		victim_damage(100, rows[i].damage);
		int score = mission_compute_kill_score_for_object(1);
		mission_credit_destruction_damage_contributors(0, 1);
		const struct player_data *player = &g_players[0];
		XVT_ASSERT_INT_EQ(player->per_mission_kills.friendlies_killed,
				  rows[i].friendly);
		if (rows[i].allied != 0) {
			XVT_ASSERT_INT_EQ(player->mission_stats.mission_score,
					  rows[i].friendly ? -score : 0);
			XVT_ASSERT_INT_EQ(
				player->mission_stats.rating_promo_points,
				rows[i].friendly ? -500 : 0);
		} else {
			/* An enemy: the kill is worth its score. */
			XVT_ASSERT_INT_EQ(player->mission_stats.mission_score,
					  score);
			XVT_ASSERT_TRUE(
				player->mission_stats.rating_promo_points > 0);
		}
		XVT_ASSERT_INT_EQ(built_count(281), rows[i].friendly);
		XVT_ASSERT_INT_EQ(built_count(200), rows[i].friendly);
		if (rows[i].friendly != 0) {
			XVT_ASSERT_INT_EQ(g_msg_arg_table[0], score);
			XVT_ASSERT_TRUE(built_with(281, "sender=3 "));
			XVT_ASSERT_TRUE(line_holds(
				"battle.friendly_kill by=0 victim=1 ",
				rows[i].damage == 100 ? "share=\"full\""
						      : "share=\"shared\""));
		}
		if (rows[i].allied != 0) {
			XVT_ASSERT_INT_EQ(
				player->per_mission_kills
						.kills_full_on_flight_group[1] +
					player->per_mission_kills
						.kills_shared_on_flight_group
							[1] +
					player->per_mission_kills
						.kills_assist_on_flight_group
							[1],
				0);
		}
	}
}

/* A victim flown by a player (the player-owned craft did at least half its
 * damage) is counted by the owner's rating and by owner as well: full kills
 * and shared kills of the player, the kills from the killer in the owner's
 * counts, and the kill table of the victim's rating. */
static void check_credit_player_victim(void)
{
	static const struct {
		int damage;
		int tier;
	} rows[] = {{100, 3}, {40, 2}, {10, 1}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		credit_world();
		g_mission_flight_groups[1].player_owner_idx = 1;
		g_players[1].pilot_rating = 9;
		g_players[1].participation_state = 0;
		g_test_craft[1]
			.damage_stats.damage_received_by_player_owned_craft =
			100;
		victim_damage(100, rows[i].damage);
		mission_credit_destruction_damage_contributors(0, 1);
		const struct per_mission_kills *kills =
			&g_players[0].per_mission_kills;
		const struct per_mission_kills *victim_kills =
			&g_players[1].per_mission_kills;
		XVT_ASSERT_INT_EQ(kills->kills_full_on_player[1],
				  rows[i].tier == 3);
		XVT_ASSERT_INT_EQ(kills->kills_shared_on_player[1],
				  rows[i].tier == 2);
		XVT_ASSERT_INT_EQ(kills->kills_full_on_player_rating[9],
				  rows[i].tier == 3);
		XVT_ASSERT_INT_EQ(kills->kills_shared_on_player_rating[9],
				  rows[i].tier == 2);
		XVT_ASSERT_INT_EQ(kills->kills_assist_on_player_rating[9],
				  rows[i].tier == 1);
		XVT_ASSERT_INT_EQ(victim_kills->kills_full_from_player[0],
				  rows[i].tier == 3);
		XVT_ASSERT_INT_EQ(victim_kills->kills_shared_from_player[0],
				  rows[i].tier == 2);
		XVT_ASSERT_INT_EQ(kills->kills_full_on_ai_rating[2], 0);
		XVT_ASSERT_TRUE(
			line_holds("mission.kill_damage object=1 source=0 ",
				   "owner=1 rating=9 "));
	}
}

/* The object type of a craft whose model is not the X-wing's. */
static int craft_of_other_model(void)
{
	int mine = g_object_type_table[g_craft_type_to_object_type[TEST_XWING]]
			   .model_index;
	for (int type = 1; type < 96; ++type) {
		uint8_t object_type = g_craft_type_to_object_type[type];
		int model = g_object_type_table[object_type].model_index;
		if (object_type != 0 && model < 73 && model != mine &&
		    g_model_defs[model].rating_weight != 0) {
			return object_type;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* The rating points of a kill go to the better total when the victim's rating
 * is at most 4 below the player's, and to the worse total otherwise, always
 * for a victim with a rating weight of 0 and for a computer's craft on a
 * resting plan (null, stationary, formation, hangar, disabled or
 * self-destruct); a kill of a mine adds 4 to the worse total when it has no
 * damage record. */
static void check_credit_worse_rating(void)
{
	static const char *const resting_plans[] = {
		"nullpln",	  "stationaryldrpln", "formldr1pln",
		"formflw1pln",	  "formevadeldr1pln", "formevadeflw1pln",
		"exithangarpln",  "enterhangarpln",   "disabledpln",
		"selfdestroypln",
	};
	for (size_t i = 0; i < sizeof resting_plans / sizeof resting_plans[0];
	     ++i) {
		credit_world();
		strcpy(g_plan_table[3].name, resting_plans[i]);
		g_test_craft[1].ai_controller.current_plan_id = 3;
		mission_credit_destruction_damage_contributors(0, 1);
		XVT_ASSERT_INT_EQ(
			g_players[0].mission_stats.rating_promo_points, 0);
		XVT_ASSERT_TRUE(
			g_players[0].mission_stats.worse_rating_promo_points >
			0);
	}
	/* Another plan: better points. */
	credit_world();
	strcpy(g_plan_table[3].name, "attackpln");
	g_test_craft[1].ai_controller.current_plan_id = 3;
	mission_credit_destruction_damage_contributors(0, 1);
	XVT_ASSERT_TRUE(g_players[0].mission_stats.rating_promo_points > 0);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.worse_rating_promo_points,
			  0);
	/* A victim whose model weighs nothing. */
	credit_world();
	g_test_objects[1].object_type = (uint8_t)craft_of_other_model();
	struct model_def *model = model_of(g_test_objects[1].object_type);
	struct model_def saved = *model;
	model->rating_weight = 0;
	mission_credit_destruction_damage_contributors(0, 1);
	*model = saved;
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.rating_promo_points, 0);
	XVT_ASSERT_TRUE(g_players[0].mission_stats.worse_rating_promo_points >
			0);
	/* A mine with no damage record: 4 worse points to the source's
	 * player, and the source's team is credited. */
	credit_world();
	victim_damage(0, 0);
	g_test_objects[1].genus_id = CRAFT_GENUS_MINE;
	mission_credit_destruction_damage_contributors(0, 1);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.worse_rating_promo_points,
			  4);
	XVT_ASSERT_TRUE(
		line_holds("mission.mine_rating_awarded slot=0 ", "total=4"));
}

/* A craft with no damage recorded is credited to its destroyer in full: the
 * player who owns the source object gets a full kill, and the source's team
 * gets a full team kill; a source no player owns gives its team the kill. */
static void check_credit_without_damage(void)
{
	credit_world();
	victim_damage(0, 0);
	mission_credit_destruction_damage_contributors(0, 1);
	XVT_ASSERT_INT_EQ(
		g_players[0].per_mission_kills.kills_full_on_flight_group[1],
		1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime.team_kill_stats[0][0],
			  1);
	XVT_ASSERT_TRUE(
		line_holds("mission.kill_credit_direct object=1 source=0 ",
			   "slot=0 team=0 record=1"));
	credit_world();
	victim_damage(0, 0);
	g_test_objects[0].player_owner_idx = -1;
	mission_credit_destruction_damage_contributors(0, 1);
	XVT_ASSERT_INT_EQ(
		g_players[0].per_mission_kills.kills_full_on_flight_group[1],
		0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime.team_kill_stats[0][0],
			  1);
}

/* The team's share of the damage comes from its flight groups. A team that
 * did a full share (two thirds) without a player doing it is counted a full
 * kill, and its most damaging group is attributed the kill (for a player's
 * victim, in the owner's kills from flight groups); with a share of 35 percent
 * a shared one; a team whose players already took the credit (full: 2,
 * shared: 1) gets no more. */
static void check_credit_team_share(void)
{
	static const struct {
		int group_damage;
		int player_damage;
		int full_from_group;
		int shared_from_group;
		int team_full;
		int team_shared;
		int team_assist;
	} rows[] = {
		{100, 0, 1, 0, 1, 0, 0},   {40, 0, 0, 1, 0, 1, 0},
		{100, 100, 0, 0, 1, 0, 0}, {40, 40, 0, 0, 0, 1, 0},
		{10, 0, 0, 0, 0, 0, 1},	   {70, 40, 0, 1, 1, 0, 0},
		{5, 0, 0, 0, 0, 0, 1},	   {4, 0, 0, 0, 0, 0, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		credit_world();
		g_mission_flight_groups[1].player_owner_idx = 1;
		g_players[1].participation_state = 0;
		g_test_craft[1]
			.damage_stats.damage_received_by_player_owned_craft =
			100;
		victim_damage(100, rows[i].player_damage);
		g_test_craft[1]
			.damage_stats.damage_from_flight_group_amount[0] =
			rows[i].group_damage;
		mission_credit_destruction_damage_contributors(0, 1);
		const struct per_mission_kills *victim_kills =
			&g_players[1].per_mission_kills;
		XVT_ASSERT_INT_EQ(victim_kills->kills_full_from_flight_group[0],
				  rows[i].full_from_group);
		XVT_ASSERT_INT_EQ(
			victim_kills->kills_shared_from_flight_group[0],
			rows[i].shared_from_group);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime.team_kill_stats[0][0],
			rows[i].team_full);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime.team_kill_stats[1][0],
			rows[i].team_shared);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime.team_kill_stats[2][0],
			rows[i].team_assist);
	}
}

/* Known failure ai_rating_past_table, issue #125: a kill of a craft of a group
 * with an AI level past the 6 levels is counted in the six-entry table of AI
 * levels, not past it (where it lands in the next table). Here the group is at
 * level 9. */
static void check_credit_ai_level_past_table(void)
{
	credit_world();
	g_mission_flight_groups[1].fg.group_ai = 9;
	mission_credit_destruction_damage_contributors(0, 1);
	const struct per_mission_kills *kills = &g_players[0].per_mission_kills;
	int counted = 0;
	int spilled = 0;
	for (int level = 0; level < 6; ++level) {
		counted += kills->kills_full_on_ai_rating[level];
		spilled += kills->kills_shared_on_ai_rating[level] +
			   kills->kills_assist_on_ai_rating[level];
	}
	XVT_ASSERT_INT_EQ(counted, 1);
	XVT_ASSERT_INT_EQ(spilled, 0);
}

/* ---- mission_credit_team_kill_contribution ----------------------------- */

/* A team's share counts a kill in its team kill stats (rows full, shared and
 * assist), adds the kill score (a half, a tenth) to the team's mission score at
 * the difficulty, and pays a per-craft bonus goal of the victim's group into
 * the same score, reduced for a shared kill; a goal that pays a penalty
 * withholds the kill score. */
static void check_team_kill_tiers(void)
{
	for (int tier = 1; tier <= 3; ++tier) {
		for (int difficulty = 0; difficulty < 3; ++difficulty) {
			credit_world();
			g_flight_mission_state.difficulty = (uint8_t)difficulty;
			int score = mission_compute_kill_score_for_object(1);
			mission_credit_team_kill_contribution(1, 0, tier, 0);
			int expected = tier == 3   ? score
				       : tier == 2 ? score / 2
						   : score / 10;
			expected = difficulty == 2   ? expected * 2
				   : difficulty == 0 ? expected / 2
						     : expected;
			XVT_ASSERT_INT_EQ(
				g_flight_mission_state.runtime
					.team_scores[TEAM_SCORE_MISSION][0],
				expected);
			XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
						  .team_kill_stats[3 - tier][0],
					  1);
			XVT_ASSERT_INT_EQ(
				g_flight_mission_state.runtime
						.team_kill_stats[0][0] +
					g_flight_mission_state.runtime
						.team_kill_stats[1][0] +
					g_flight_mission_state.runtime
						.team_kill_stats[2][0],
				1);
		}
	}
	/* The goal of the victim's group pays the team's bonus score as well,
	 * and a penalty withholds the kill score. */
	credit_world();
	set_fg_goal(1, 0, 2, MISSION_COND_DESTROYED, 18, 4, 0x1);
	int score = mission_compute_kill_score_for_object(1);
	mission_credit_team_kill_contribution(1, 0, 2, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_scores[TEAM_SCORE_MISSION][0],
			  500 + score / 2);
	credit_world();
	set_fg_goal(1, 0, 2, MISSION_COND_DESTROYED, 18, -2, 0x1);
	mission_credit_team_kill_contribution(1, 0, 3, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_scores[TEAM_SCORE_MISSION][0],
			  -500);
	XVT_ASSERT_TRUE(line_holds("mission.team_kill_score_withheld team=0 ",
				   "tier=3"));
}

/* A team's share of the kill of an allied craft, from a shared share up, takes
 * the kill score off the team's mission score and counts no kill. */
static void check_team_kill_friendly(void)
{
	for (int tier = 1; tier <= 3; ++tier) {
		credit_world();
		g_mission_teams[0].allies[1] = 1;
		int score = mission_compute_kill_score_for_object(1);
		mission_credit_team_kill_contribution(1, 0, tier, 0);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
					  .team_scores[TEAM_SCORE_MISSION][0],
				  tier == 1 ? 0 : -score);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime.team_kill_stats[0][0] +
				g_flight_mission_state.runtime
					.team_kill_stats[1][0] +
				g_flight_mission_state.runtime
					.team_kill_stats[2][0],
			0);
	}
}

/* ---- mission_record_player_craft_loss and its attribution -------------- */

/* Player 0 flies group 0's craft (object 0, hull 100, shields 0), and the
 * other players, 2 and 3, are connected, rated 7 and 9; the craft has taken
 * no damage. The game's settings are clear and the messages plain. */
static void loss_world(void)
{
	credit_world();
	g_test_objects[0].player_owner_idx = 0;
	g_mission_flight_groups[0].player_owner_idx = 0;
	g_players[2].participation_state = 1;
	g_players[2].team = 1;
	g_players[2].pilot_rating = 7;
	g_players[3].participation_state = 1;
	g_players[3].team = 1;
	g_players[3].pilot_rating = 9;
	g_test_craft[0].hull_max = 100;
	g_test_craft[0].hull_damage = 0;
}

/* The damage a craft took, by source, all of it to a flown craft. */
static void craft_damage(int from_player_2, int from_player_3, int from_ai,
			 int collision, int starship, int mine)
{
	struct craft_damage_stats *stats = &g_test_craft[0].damage_stats;
	memset(stats, 0, sizeof *stats);
	stats->damage_from_player[2] = from_player_2;
	stats->damage_from_player[3] = from_player_3;
	stats->damage_from_ai_skill[1] = from_ai;
	stats->damage_from_collision = collision;
	stats->damage_from_starship = starship;
	stats->damage_from_mine = mine;
	int total = from_player_2 + from_player_3 + from_ai + collision +
		    starship + mine;
	stats->damage_received_total = total;
	stats->damage_received_by_player_owned_craft = total;
}

/* A craft's loss takes half its point value from its team's mission score and
 * from its owner's, counts a loss for the team, and returns the credit: when
 * the craft was flown, the player who did the most damage if players did the
 * most, else -1; with no connected owner, the points taken. Then the loss is
 * sorted by who did the most damage (collisions, starships, mines, players,
 * computer craft; ties in that order), counted in the owner's collisions,
 * starships or mines, and logged with the cause. */
static void check_craft_loss_classes(void)
{
	static const struct {
		int p2;
		int p3;
		int ai;
		int collision;
		int starship;
		int mine;
		const char *cause;
		int credited;
		int by_collision;
		int by_starship;
		int by_mine;
	} rows[] = {
		{0, 0, 0, 40, 10, 10, "collision", -1, 1, 0, 0},
		{0, 0, 0, 10, 40, 10, "starship", -1, 0, 1, 0},
		{0, 0, 0, 10, 10, 40, "mine", -1, 0, 0, 1},
		{40, 0, 0, 10, 10, 10, "player", 2, 0, 0, 0},
		{10, 50, 0, 10, 10, 10, "player", 3, 0, 0, 0},
		{0, 0, 40, 10, 10, 10, "computer", -1, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, "unknown", -1, 0, 0, 0},
		{0, 0, 0, 20, 20, 20, "collision", -1, 1, 0, 0},
		{0, 0, 0, 0, 20, 20, "starship", -1, 0, 1, 0},
		{20, 0, 20, 0, 0, 0, "player", 2, 0, 0, 0},
		{0, 0, 0, 0, 0, 20, "mine", -1, 0, 0, 1},
		{10, 0, 40, 10, 10, 10, "computer", -1, 0, 0, 0},
		{30, 30, 0, 10, 10, 10, "player", 2, 0, 0, 0},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		loss_world();
		craft_damage(rows[i].p2, rows[i].p3, rows[i].ai,
			     rows[i].collision, rows[i].starship, rows[i].mine);
		int value = mission_compute_craft_point_value(0) / 2;
		XVT_ASSERT_TRUE(value > 0);
		XVT_ASSERT_INT_EQ(mission_record_player_craft_loss(0, 0),
				  rows[i].credited);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
					  .team_scores[TEAM_SCORE_MISSION][0],
				  -value);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime.team_kill_stats[3][0],
			1);
		XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
				  -value);
		const struct per_mission_kills *kills =
			&g_players[0].per_mission_kills;
		XVT_ASSERT_INT_EQ(kills->total_craft_losses, 1);
		XVT_ASSERT_INT_EQ(kills->losses_by_collisions,
				  rows[i].by_collision);
		XVT_ASSERT_INT_EQ(kills->losses_by_starships,
				  rows[i].by_starship);
		XVT_ASSERT_INT_EQ(kills->losses_by_mines, rows[i].by_mine);
		char fields[48];
		snprintf(fields, sizeof fields, "cause=\"%s\" by=%d eject=0",
			 rows[i].cause, rows[i].credited);
		XVT_ASSERT_TRUE(line_holds("battle.loss who=0 ", fields));
	}
}

/* A craft that the players and computer craft did half or more of the
 * hull and shields it had left in damage to is credited to them first when
 * the caller allows it (the player ejected); less than that is not. A loss
 * where players did not do at least half the damage is not counted for the
 * owner. A craft that has no owner of its own, in a group whose player is
 * not there, only costs its team. */
static void check_craft_loss_pending_and_owner(void)
{
	static const struct {
		int damage;
		int allow;
		int credited;
	} pending[] = {{51, 1, 1}, {50, 1, 0}, {51, 0, 0}};
	for (size_t i = 0; i < sizeof pending / sizeof pending[0]; ++i) {
		loss_world();
		craft_damage(pending[i].damage, 0, 0, 0, 0, 0);
		g_test_craft[0].hull_damage = 0;
		g_test_craft[0].shield_energy[0] = 0;
		/* The 100 points of hull left are halved: 50. */
		mission_record_player_craft_loss(0, pending[i].allow);
		XVT_ASSERT_INT_EQ(
			g_players[2].per_mission_kills.kills_full_on_player[0] +
				g_players[2]
					.per_mission_kills
					.kills_shared_on_player[0],
			pending[i].credited);
		char fields[48];
		snprintf(fields, sizeof fields,
			 "pending=%d remaining=50 credited=%d",
			 pending[i].damage, pending[i].credited);
		XVT_ASSERT_INT_EQ(count_lines("mission.pending_damage "),
				  pending[i].allow);
		if (pending[i].allow != 0) {
			XVT_ASSERT_TRUE(line_holds(
				"mission.pending_damage object=0 ", fields));
		}
	}
	/* A shield of 1 counts for what is left: two of them are 2, halved 1,
	 * and one damage is not more than that. */
	loss_world();
	craft_damage(1, 0, 0, 0, 0, 0);
	g_test_craft[0].hull_damage = 100;
	g_test_craft[0].shield_energy[0] = 1;
	g_test_craft[0].shield_energy[1] = 1;
	mission_record_player_craft_loss(0, 1);
	XVT_ASSERT_TRUE(line_holds("mission.pending_damage object=0 ",
				   "pending=1 remaining=1 credited=0"));
	/* Shields count for what is left, the hull when it is not spent. */
	loss_world();
	craft_damage(60, 0, 0, 0, 0, 0);
	g_test_craft[0].hull_damage = 30;
	g_test_craft[0].shield_energy[0] = 20;
	g_test_craft[0].shield_energy[1] = 10;
	mission_record_player_craft_loss(0, 1);
	XVT_ASSERT_TRUE(line_holds("mission.pending_damage object=0 ",
				   "pending=60 remaining=50 credited=1"));
	loss_world();
	craft_damage(5, 0, 0, 0, 0, 0);
	g_test_craft[0].hull_damage = 150;
	g_test_craft[0].shield_energy[0] = -9;
	mission_record_player_craft_loss(0, 1);
	XVT_ASSERT_TRUE(line_holds("mission.pending_damage object=0 ",
				   "pending=5 remaining=0 credited=1"));

	/* Less than half of the damage done to the flown craft: the loss is
	 * not counted. */
	loss_world();
	craft_damage(40, 0, 0, 0, 0, 0);
	g_test_craft[0].damage_stats.damage_received_by_player_owned_craft = 10;
	XVT_ASSERT_INT_EQ(mission_record_player_craft_loss(0, 0), -1);
	XVT_ASSERT_INT_EQ(g_players[0].per_mission_kills.total_craft_losses, 0);
	XVT_ASSERT_TRUE(line_holds("mission.loss_not_counted slot=0 object=0 ",
				   "flown=10 total=40"));

	/* The owner is the group's player when the object has none. */
	loss_world();
	craft_damage(40, 0, 0, 0, 0, 0);
	g_test_objects[0].player_owner_idx = -1;
	XVT_ASSERT_INT_EQ(mission_record_player_craft_loss(0, 0), 2);
	XVT_ASSERT_INT_EQ(g_players[0].per_mission_kills.total_craft_losses, 1);

	/* No owner, and an owner who is not there: the team's points back. */
	loss_world();
	craft_damage(40, 0, 0, 0, 0, 0);
	int value = mission_compute_craft_point_value(0) / 2;
	g_test_objects[0].player_owner_idx = -1;
	g_mission_flight_groups[0].player_owner_idx = -1;
	XVT_ASSERT_INT_EQ(mission_record_player_craft_loss(0, 0), value);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime.team_kill_stats[3][0],
			  1);
	loss_world();
	g_test_objects[0].player_owner_idx = -1;
	g_players[0].participation_state = 0;
	XVT_ASSERT_INT_EQ(mission_record_player_craft_loss(0, 0), value);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score, 0);
	XVT_ASSERT_INT_EQ(g_players[0].per_mission_kills.total_craft_losses, 0);
}

/* The loss of a flown craft is attributed, at a full kill (tier 3) only, to
 * the killer's pilot rating when a player flies the attacking group and to
 * its AI level when none does; a shared kill (2) and a victim whose damage was
 * not mostly done to a flown craft are not counted; neither is another tier. A
 * victim with no owner of its own is the group's player's. */
static void check_loss_attribution(void)
{
	static const struct {
		int tier;
		int attacker_player; /* player of the attacking group or -1 */
		int owner_on_object;
		int flown;
		int counted;
	} rows[] = {
		{3, 2, 1, 100, 1}, {3, -1, 1, 100, 1}, {3, 2, 0, 100, 1},
		{2, 2, 1, 100, 0}, {1, 2, 1, 100, 0},  {4, 2, 1, 100, 0},
		{3, 2, 1, 49, 0},  {3, 2, 1, 50, 1},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		loss_world();
		craft_damage(40, 0, 0, 0, 0, 0);
		g_test_craft[0].damage_stats.damage_received_total = 100;
		g_test_craft[0]
			.damage_stats.damage_received_by_player_owned_craft =
			rows[i].flown;
		g_test_objects[0].player_owner_idx =
			rows[i].owner_on_object ? 0 : -1;
		g_mission_flight_groups[1].player_owner_idx =
			rows[i].attacker_player;
		g_mission_flight_groups[1].fg.group_ai = 4;
		mission_record_player_craft_loss_attribution(1, 0,
							     rows[i].tier);
		const struct per_mission_kills *kills =
			&g_players[0].per_mission_kills;
		int by_player = 0;
		int by_ai = 0;
		for (int rating = 0; rating < 25; ++rating) {
			by_player += kills->killed_by_player_rating[rating];
		}
		for (int level = 0; level < 6; ++level) {
			by_ai += kills->killed_by_ai_rating[level];
		}
		int player_attacker = rows[i].attacker_player != -1;
		XVT_ASSERT_INT_EQ(by_player,
				  rows[i].counted && player_attacker);
		XVT_ASSERT_INT_EQ(by_ai, rows[i].counted && !player_attacker);
		if (rows[i].counted != 0 && player_attacker) {
			XVT_ASSERT_INT_EQ(kills->killed_by_player_rating[7], 1);
		}
		if (rows[i].counted != 0 && !player_attacker) {
			XVT_ASSERT_INT_EQ(kills->killed_by_ai_rating[4], 1);
		}
	}
}

/* ---- mission_record_projectile_hit_stats ------------------------------- */

enum { HIT_OBJECT_COUNT = 140 };

static struct object_record g_hit_objects[HIT_OBJECT_COUNT];
static struct mobile_object g_hit_mobiles[HIT_OBJECT_COUNT];
static struct craft_data g_hit_craft;

/* An object table of 140 slots: object 1 is a craft of group 0 (flown by
 * player 3 when asked), objects 5 and 130 are shots it fired, and the
 * projectile slots start at 0 (so the first 128 are a player's). */
static void hit_world(int owner_player, int projectile_type)
{
	credit_world();
	memset(g_hit_objects, 0, sizeof g_hit_objects);
	memset(g_hit_mobiles, 0, sizeof g_hit_mobiles);
	memset(&g_hit_craft, 0, sizeof g_hit_craft);
	for (int i = 0; i < HIT_OBJECT_COUNT; ++i) {
		g_hit_objects[i].mobj = &g_hit_mobiles[i];
		g_hit_objects[i].player_owner_idx = -1;
		g_hit_mobiles[i].p_craft = &g_hit_craft;
	}
	g_object_table = g_hit_objects;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = 8;
	g_projectile_object_slot_start = 0;
	g_hit_objects[1].object_type = TEST_XWING;
	g_hit_objects[1].flight_group_idx = 0;
	g_mission_flight_groups[0].player_owner_idx = owner_player;
	g_mission_flight_groups[0].fg.team = 4;
	for (int shot = 5; shot <= 130; shot += 125) {
		g_hit_objects[shot].object_type = (uint8_t)projectile_type;
		g_hit_mobiles[shot].source_obj_idx = 1;
	}
}

/* A shot's hit is counted for the craft that fired it: a laser (or turbo
 * laser) hit in its laser hits, an ion hit in its ion hits, a warhead hit in
 * its warhead hits; the player flying it, when the shot is one of the 128
 * player shots, has the same counted (a warhead hit in the player's warhead
 * hits and the warhead's points in the player's score); every warhead hit adds
 * its points to the firing team's mission score. Other shots, and shots of a
 * craft no longer there or not in the craft slots, count nothing. */
static void check_projectile_hits(void)
{
	static const struct {
		int type;
		int kind; /* 1 laser, 2 ion, 3 warhead */
	} types[] = {
		{PROJECTILE_OBJECT_TYPE_REBEL_LASER, 1},
		{PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER, 1},
		{PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER, 1},
		{PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER, 1},
		{PROJECTILE_OBJECT_TYPE_ION_LASER, 2},
		{PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER, 2},
		{WARHEAD_OBJECT_TYPE_PROTON_TORPEDO, 3},
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 3},
		{WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO, 3},
		{WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE, 3},
		{WARHEAD_OBJECT_TYPE_SPACE_BOMB, 3},
		{WARHEAD_OBJECT_TYPE_HEAVY_ROCKET, 3},
		{WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE, 3},
		{WARHEAD_OBJECT_TYPE_ION_PULSE, 3},
		{WARHEAD_OBJECT_TYPE_LASER_3, 3},
		{129, 0},
	};
	for (size_t i = 0; i < sizeof types / sizeof types[0]; ++i) {
		for (int owner = -1; owner <= 3; owner += 4) {
			for (int shot = 5; shot <= 130; shot += 125) {
				hit_world(owner, types[i].type);
				int points =
					types[i].kind == 3
						? g_projectile_type_data.warhead_point_value
							  [types[i].type -
							   PROJECTILE_OBJECT_TYPE_FIRST]
						: 0;
				mission_record_projectile_hit_stats(
					(uint16_t)shot);
				int player_shot = shot < 128 && owner != -1;
				XVT_ASSERT_INT_EQ(g_hit_craft.weapon_stats
							  .laser_hits_scored,
						  types[i].kind == 1);
				XVT_ASSERT_INT_EQ(g_hit_craft.weapon_stats
							  .ion_hits_scored,
						  types[i].kind == 2);
				XVT_ASSERT_INT_EQ(g_hit_craft.weapon_stats
							  .warhead_hits_scored,
						  types[i].kind == 3);
				const struct player_data *player =
					&g_players[3];
				XVT_ASSERT_INT_EQ(
					player->mission_stats.laser_hits_scored,
					types[i].kind == 1 && player_shot);
				XVT_ASSERT_INT_EQ(
					player->mission_stats.ion_hits_scored,
					types[i].kind == 2 && player_shot);
				XVT_ASSERT_INT_EQ(
					player->per_mission_kills.warhead_hits,
					types[i].kind == 3 && player_shot);
				XVT_ASSERT_INT_EQ(
					player->mission_stats.mission_score,
					player_shot ? points : 0);
				XVT_ASSERT_INT_EQ(
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_MISSION]
							    [4],
					points);
				XVT_ASSERT_INT_EQ(
					count_lines("battle.warhead_hit "),
					types[i].kind == 3 && player_shot);
				XVT_ASSERT_INT_EQ(
					count_lines("mission.hit_uncounted "),
					types[i].kind == 0);
			}
		}
	}
	XVT_ASSERT_TRUE(
		line_holds("mission.hit_uncounted ", "reason=\"type\""));

	/* A shot of a craft past the craft slots, or one that is gone. */
	hit_world(3, PROJECTILE_OBJECT_TYPE_REBEL_LASER);
	g_hit_mobiles[5].source_obj_idx = 8;
	mission_record_projectile_hit_stats(5);
	XVT_ASSERT_TRUE(
		line_holds("mission.hit_uncounted projectile=5 source=8 ",
			   "reason=\"not_craft\""));
	hit_world(3, PROJECTILE_OBJECT_TYPE_REBEL_LASER);
	g_hit_objects[1].object_type = 0;
	mission_record_projectile_hit_stats(5);
	XVT_ASSERT_TRUE(
		line_holds("mission.hit_uncounted projectile=5 source=1 ",
			   "reason=\"gone\""));
	XVT_ASSERT_INT_EQ(g_hit_craft.weapon_stats.laser_hits_scored, 0);
	XVT_ASSERT_INT_EQ(g_players[3].mission_stats.laser_hits_scored, 0);
}

/* ---- the goal score of a kill, told to the local player ---------------- */

/* A per-craft bonus goal paid to the local player tells them of the points
 * (message 199) or of a penalty (200), with the amount as the argument; a
 * goal paid to another player tells nothing. A team's bonus for the goal is
 * paid inside its time limit and logged. */
static void check_goal_score_messages(void)
{
	static const struct {
		int points;
		int message;
		int amount;
	} rows[] = {{4, 199, 1000}, {-3, 200, 750}};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		credit_world();
		g_local_player = 0;
		set_fg_goal(1, 0, 2, MISSION_COND_DESTROYED, 18, rows[i].points,
			    0x1);
		XVT_ASSERT_INT_EQ(
			mission_apply_flight_group_goal_score(
				MISSION_COND_DESTROYED, 1, 0, 0, 0, 0),
			250 * rows[i].points);
		XVT_ASSERT_INT_EQ(built_count(rows[i].message), 1);
		XVT_ASSERT_INT_EQ(g_msg_arg_table[0], rows[i].amount);
		g_local_player = 5;
		g_log_line_count = 0;
		mission_apply_flight_group_goal_score(MISSION_COND_DESTROYED, 1,
						      0, 0, 0, 0);
		XVT_ASSERT_INT_EQ(built_count(rows[i].message), 0);
	}
	/* The team's bonus, paid inside the time limit. */
	credit_world();
	struct flight_group_goal *goal =
		set_fg_goal(1, 0, 2, MISSION_COND_DESTROYED, 18, 2, 0x3);
	goal->time_limit5s = 2;
	set_elapsed(0, 0, 10);
	mission_apply_team_goal_score_for_team(MISSION_COND_DESTROYED, 1, 0, 0);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS][0],
		500);
	XVT_ASSERT_INT_EQ(count_lines("mission.bonus_score_stale"), 0);
	XVT_ASSERT_TRUE(
		line_holds("mission.team_bonus_paid team=0 fg=1 goal=0 ",
			   "score=500 total=500 late=0"));
}

/* A destroyed special cargo craft gives back its capture, its departure and
 * its abort in the special cargo counts as in the group's; a special cargo
 * craft of a player that ended without a departure counts as not departed. */
static void check_outcome_special_cargo_takebacks(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.special_cargo_craft = 3;
	struct craft_data *craft = place_craft(0, 0, 3);
	craft->captured_by_flight_group = 0x81;
	craft->ai_flight.depart_timer_flag = 1;
	craft->ai_flight.mission_aborted_flag = 1;
	struct mission_fg_runtime_stats *stats = &g_mission_fg_stats[0];
	stats->outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED] = 1;
	stats->outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 1;
	stats->outcome_count[FLIGHT_GROUP_OUTCOME_ABORTED] = 1;
	stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_CAPTURED] = 1;
	stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 1;
	stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_ABORTED] = 1;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(
		stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_CAPTURED], 0);
	XVT_ASSERT_INT_EQ(
		stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
		0);
	XVT_ASSERT_INT_EQ(
		stats->special_cargo_outcome[FLIGHT_GROUP_OUTCOME_ABORTED], 0);
	XVT_ASSERT_INT_EQ(stats->outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED],
			  0);
	XVT_ASSERT_INT_EQ(
		stats->special_cargo_outcome
			[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION],
		1);

	/* A player's special cargo craft, ending in another way. */
	fresh_world();
	g_mission_flight_groups[0].fg.special_cargo_craft = 3;
	place_craft(0, 0, 3);
	g_test_objects[0].player_owner_idx = 1;
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_LEFT_REGION);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].special_cargo_outcome
				  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
			  1);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[0]
			.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
		1);
}

/* ---- more of the running mission --------------------------------------- */

/* The "better" rating points of a kill take the rating factor of the victim's
 * rating over the killer's plus 4, but at least 4 for a killer rated below 15
 * and at least 3 from 15 up. The victim here is flown by player 1 and did
 * not make the kill any shorter: a full share of the damage. */
static void check_credit_rating_factor(void)
{
	static const struct {
		int killer;
		int victim;
		int factor;
	} rows[] = {
		{10, 8, 4},  {10, 12, 6}, {14, 10, 4},
		{20, 17, 3}, {20, 22, 6}, {15, 12, 3},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		credit_world();
		g_players[0].pilot_rating = (uint16_t)rows[i].killer;
		g_players[1].pilot_rating = (uint16_t)rows[i].victim;
		g_mission_flight_groups[1].player_owner_idx = 1;
		g_test_craft[1]
			.damage_stats.damage_received_by_player_owned_craft =
			100;
		mission_credit_destruction_damage_contributors(0, 1);
		char fields[64];
		snprintf(fields, sizeof fields, "kind=\"better\" ");
		XVT_ASSERT_TRUE(
			line_holds("mission.rating_awarded slot=0 ", fields));
		snprintf(fields, sizeof fields, "award=%d factor=%d ",
			 rows[i].victim, rows[i].factor);
		XVT_ASSERT_TRUE(
			line_holds("mission.rating_awarded slot=0 ", fields));
	}
}

/* The rating points of the "worse" total: always at least 1; half for a shared
 * kill and a tenth for an assist, but at least 1. A victim on a resting plan
 * counts for 1 point of rating. */
static void check_credit_worse_tiers(void)
{
	int base = 0;
	static const struct {
		int damage;
		int divisor;
	} rows[] = {{100, 1}, {40, 2}, {10, 10}};
	XVT_ASSERT_TRUE(craft_of_other_model() != 0);
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
		credit_world();
		strcpy(g_plan_table[3].name, "nullpln");
		g_test_craft[1].ai_controller.current_plan_id = 3;
		victim_damage(100, rows[i].damage);
		/* A victim worth a hundred times the killer. */
		g_test_objects[1].object_type = (uint8_t)craft_of_other_model();
		struct model_def *victim_model =
			model_of(g_test_objects[1].object_type);
		struct model_def *killer_model = model_of(TEST_XWING);
		struct model_def saved_victim = *victim_model;
		struct model_def saved_killer = *killer_model;
		victim_model->rating_weight = 100;
		killer_model->rating_weight = 1;
		mission_credit_destruction_damage_contributors(0, 1);
		*victim_model = saved_victim;
		*killer_model = saved_killer;
		const char *line = find_line(
			"mission.rating_awarded slot=0 kind=\"worse\"");
		XVT_ASSERT_TRUE(line != NULL);
		int weight = atoi(strstr(line, "weight=") + 7);
		int attacker = atoi(strstr(line, "attacker=") + 9);
		if (i == 0) {
			base = weight / attacker;
			if (base == 0) {
				base = 1;
			}
		}
		int expected = base / rows[i].divisor;
		if (expected == 0) {
			expected = 1;
		}
		XVT_ASSERT_INT_EQ(
			g_players[0].mission_stats.worse_rating_promo_points,
			expected);
		XVT_ASSERT_TRUE(strstr(line, "award=1 ") != NULL);
	}
}

/* A special cargo craft's kill is the special craft's: the victim here is
 * craft 0 of a group whose special cargo craft is 0, and a goal for the special
 * cargo (amount 19) pays; it does not pay for another craft. */
static void check_credit_special_cargo_victim(void)
{
	for (int special = 0; special < 2; ++special) {
		credit_world();
		g_mission_flight_groups[1].fg.special_cargo_craft =
			special ? 0 : TEST_NO_ORDINAL;
		set_fg_goal(1, 0, 2, MISSION_COND_DESTROYED, 19, 4, 0x1);
		int score = mission_compute_kill_score_for_object(1);
		mission_credit_destruction_damage_contributors(0, 1);
		XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
				  score + (special ? 1000 : 0));
		char fields[16];
		snprintf(fields, sizeof fields, "special=%d ", special);
		XVT_ASSERT_TRUE(
			line_holds("mission.kill_damage object=1 ", fields));
	}
}

/* A prevent goal met tells the local player of the team the failure texts that
 * are set, skipping an empty first one. */
static void check_logic_prevent_met_skips_empty_text(void)
{
	logic_world();
	connect_local_player(0, 4);
	strcpy(g_mission_teams[0].end_of_mission_messages[3], "Next");
	set_fg_goal(0, 0, 1, MISSION_COND_DESTROYED, GOAL_AMT_100, 0, 1);
	group_craft(0, 2, 0);
	run_logic();
	XVT_ASSERT_INT_EQ(built_count(206), 1);
	XVT_ASSERT_INT_EQ(built_count(207), 0);
	XVT_ASSERT_INT_EQ(built_count(196), 1);
	XVT_ASSERT_TRUE(built_with(196, "length=4 sender=4 voice=0 "));
}

/* More of the conditions: the bonus goal complete has failed when the bonus
 * status is failed; a counting condition number no case lists is undecided with
 * the group's total left behind; the players by team judge "all" and 66 percent
 * met when they are (all four connected, three of four), and "disconnected"
 * all is met when none is connected. */
static void check_condition_more_cases(void)
{
	fresh_world();
	g_flight_mission_state.runtime.global_bonus_goal_status = 2;
	XVT_ASSERT_INT_EQ(
		mission_evaluate_condition(MISSION_COND_BONUS_GOAL_COMPLETE,
					   GOAL_TARGET_NONE, 0, 0, 0, 10),
		2);
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 3;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED] = 3;
	XVT_ASSERT_INT_EQ(condition_on_group(11, 0, GOAL_AMT_100, 0), 4);
	XVT_ASSERT_INT_EQ(g_mission_condition_current_count, 0);
	XVT_ASSERT_INT_EQ(g_mission_condition_total_count, 3);

	fresh_world();
	g_mission_header.num_flight_groups = 4;
	for (int fg = 0; fg < 4; ++fg) {
		player_group(fg, fg + 1, 1, 2);
		g_players[fg].team = 1;
		g_players[fg].participation_state = 1;
	}
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_CONNECTED,
				  GOAL_TARGET_TEAM, 1, GOAL_AMT_100, 0, 10),
			  1);
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_DISCONNECTED,
				  GOAL_TARGET_TEAM, 1, GOAL_AMT_100, 0, 10),
			  2);
	g_players[3].participation_state = 0;
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_CONNECTED,
				  GOAL_TARGET_TEAM, 1, GOAL_AMT_66, 0, 10),
			  1);
	for (int player = 0; player < 4; ++player) {
		g_players[player].participation_state = 0;
	}
	XVT_ASSERT_INT_EQ(mission_evaluate_condition(
				  MISSION_COND_PLAYER_DISCONNECTED,
				  GOAL_TARGET_TEAM, 1, GOAL_AMT_100, 0, 10),
			  1);

	/* A special cargo craft that fails a craft condition. */
	fresh_world();
	struct model_def *model = model_of(TEST_XWING);
	struct model_def saved = *model;
	model->shield_strength = 100;
	g_mission_flight_groups[0].fg.special_cargo_craft = 0;
	struct craft_data *craft = place_craft(0, 0, 0);
	craft->model_index = (uint16_t)get_model_index_from_type(
		(object_type_id)g_test_objects[0].object_type);
	craft->shield_energy[0] = 50;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
	g_mission_fg_stats[0]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
	XVT_ASSERT_INT_EQ(condition_on_group(MISSION_COND_SHIELDS_DEPLETED, 0,
					     GOAL_AMT_ALL_SPECIAL_CARGO, 0),
			  2);
	*model = saved;
	/* Exact thresholds of the percentages: 1 of 4 is 25, 4 of 6 is 66 and 2
	 * of 6 is 33 percent; and with all four connected nobody is missing for
	 * "at least one disconnected". */
	static const struct {
		int groups;
		int connected;
		int amount;
		int condition;
		int result;
	} exact[] = {
		{4, 1, GOAL_AMT_25, MISSION_COND_PLAYER_CONNECTED, 1},
		{6, 4, GOAL_AMT_66, MISSION_COND_PLAYER_CONNECTED, 1},
		{6, 2, GOAL_AMT_33, MISSION_COND_PLAYER_CONNECTED, 1},
		{4, 4, GOAL_AMT_AT_LEAST_1, MISSION_COND_PLAYER_DISCONNECTED,
		 2},
		{4, 3, GOAL_AMT_AT_LEAST_1, MISSION_COND_PLAYER_DISCONNECTED,
		 1},
	};
	for (size_t i = 0; i < sizeof exact / sizeof exact[0]; ++i) {
		for (int by_iff = 0; by_iff < 2; ++by_iff) {
			fresh_world();
			g_mission_header.num_flight_groups =
				(uint16_t)exact[i].groups;
			for (int fg = 0; fg < exact[i].groups; ++fg) {
				player_group(fg, fg + 1, 1, 2);
				g_players[fg].team = 1;
				g_players[fg].iff = 2;
				g_players[fg].participation_state =
					fg < exact[i].connected;
			}
			XVT_ASSERT_INT_EQ(mission_evaluate_condition(
						  (uint16_t)exact[i].condition,
						  by_iff ? GOAL_TARGET_IFF
							 : GOAL_TARGET_TEAM,
						  by_iff ? 2 : 1,
						  (int16_t)exact[i].amount, 0,
						  10),
					  exact[i].result);
		}
	}
}

/* The opposites of the species, genus and family variables of an object
 * (16, 17, 18) match an object of another species, genus and family. */
static void check_object_matches_opposite_kinds(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	uint8_t mine = g_craft_type_to_object_type[TEST_XWING];
	int other_species = -1;
	int other_genus = -1;
	int other_family = -1;
	for (int type = 1; type < 96; ++type) {
		uint8_t object_type = g_craft_type_to_object_type[type];
		if (object_type == 0 || object_type >= 201) {
			continue;
		}
		if (object_type != mine && other_species < 0) {
			other_species = type;
		}
		int genus = variable_for_genus(
			g_object_type_table[object_type].genus_id);
		if (genus >= 0 && g_object_type_table[object_type].genus_id !=
					  g_object_type_table[mine].genus_id) {
			other_genus = genus;
		}
		int family = variable_for_family(
			(uint8_t)g_object_type_table[object_type].family_id);
		if (family >= 0 &&
		    (uint8_t)g_object_type_table[object_type].family_id !=
			    (uint8_t)g_object_type_table[mine].family_id) {
			other_family = family;
		}
	}
	XVT_ASSERT_TRUE(other_species >= 0 && other_genus >= 0 &&
			other_family >= 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 16, (uint16_t)(other_species - 1)),
			  1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 17, (uint16_t)other_genus),
			  1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 18, (uint16_t)other_family),
			  1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(
				  0, 2, (uint16_t)(other_species - 1)),
			  0);
}

/* A single craft asked for when the table is full is not spawned and says so;
 * a single craft that is the group's special cargo craft is counted as that. */
static void check_spawn_single_craft_counts(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 2);
	g_stored.groups[0].special_cargo_craft = 1;
	spawn_mission_ready();
	XVT_ASSERT_INT_EQ(spawn_craft(0, 0), 1);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[0]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_ARRIVED],
		0);
	XVT_ASSERT_INT_EQ(spawn_craft(0, 1), 1);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[0]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_ARRIVED],
		1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);
	/* A whole round counts its special cargo craft once; one with none
	 * counts none. */
	for (int special = 0; special < 2; ++special) {
		init_mission(MISSION_TYPE_COMBAT);
		arriving_group(0, 1, SPAWN_TYPE, 3);
		g_stored.groups[0].special_cargo_craft =
			special ? 1 : TEST_NO_ORDINAL;
		spawn_mission_ready();
		XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].special_cargo_outcome
					  [FLIGHT_GROUP_OUTCOME_ARRIVED],
				  special);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[0]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			3);
	}
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 2);
	g_stored.groups[0].special_cargo_craft = 1;
	spawn_mission_ready();
	XVT_ASSERT_INT_EQ(spawn_craft(0, 0), 1);
	XVT_ASSERT_INT_EQ(spawn_craft(0, 1), 1);
	for (int slot = 0; slot < 32; ++slot) {
		g_object_table[slot].object_type = 9;
	}
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 5;
	XVT_ASSERT_INT_EQ(spawn_craft(0, 1), 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);
}

/* A static group that has arrived in a melee and has rounds to come sends its
 * next round when none of its static objects is left. */
static void check_arrivals_static_group_next_round(void)
{
	init_mission(MISSION_TYPE_MELEE);
	arriving_group(0, 1, CRAFT_SPECIES_MINE_TYPE_A, 2);
	g_stored.groups[0].number_of_waves = 1;
	begin_arrivals();
	int slots[64];
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].has_arrived, 1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	int placed = static_objects(slots);
	XVT_ASSERT_TRUE(placed > 0);
	/* While one is left, nothing comes. */
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 1);
	XVT_ASSERT_INT_EQ(static_objects(slots), placed);
	for (int i = 0; i < placed; ++i) {
		g_object_table[slots[i]].object_type = 0;
	}
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer = 0;
	mission_update_flight_group_arrivals();
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].waves_remaining, 0);
	XVT_ASSERT_INT_EQ(static_objects(slots), placed);
}

/* A craft that ends while its group's order says to drop off at another group
 * closes that group's arrival, as a craft that never comes. */
static void check_outcome_dropoff_closes_group(void)
{
	fresh_world();
	g_order_leader_builtin_plan_name_index[1] = 5;
	g_builtin_plan_id_by_name_index[5] = 9;
	strcpy(g_plan_table[9].name, "dropoffldr1pln");
	g_mission_flight_groups[0].fg.orders[0].order = 1;
	g_mission_flight_groups[0].fg.orders[0].variable2 = 2;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 3;
	place_craft(0, 0, 0);
	mission_record_craft_outcome(0, 0, FLIGHT_GROUP_OUTCOME_DESTROYED);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].has_arrived, 1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  3);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0].has_arrived, 0);
}

/* A primary goal on the victim's group, for the killer's team, does not change
 * what the kill is worth: the same kill counts and the same score is added,
 * whatever the goal's event (a kill by the local player also asks the wingman
 * for a word, which the voice settings here leave off). */
static void check_credit_with_primary_goal(void)
{
	static const int events[] = {MISSION_COND_ALWAYS_TRUE,
				     MISSION_COND_DESTROYED, MISSION_COND_NEVER,
				     MISSION_COND_DISABLED};
	for (size_t i = 0; i < sizeof events / sizeof events[0]; ++i) {
		credit_world();
		g_local_player = 0;
		set_fg_goal(1, 0, 0, events[i], GOAL_AMT_100, 0, 0x1);
		int score = mission_compute_kill_score_for_object(1);
		mission_credit_destruction_damage_contributors(0, 1);
		XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
				  score);
		XVT_ASSERT_INT_EQ(g_players[0]
					  .per_mission_kills
					  .kills_full_on_flight_group[1],
				  1);
	}
}

/* A victim whose rating is more than 4 below the killer's, and whose rating is
 * 0, gives the worse total a rating of 1 for the kill. */
static void check_credit_worse_rating_of_zero(void)
{
	credit_world();
	g_players[0].pilot_rating = 6;
	g_players[1].pilot_rating = 0;
	g_mission_flight_groups[1].player_owner_idx = 1;
	g_test_craft[1].damage_stats.damage_received_by_player_owned_craft =
		100;
	mission_credit_destruction_damage_contributors(0, 1);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.rating_promo_points, 0);
	XVT_ASSERT_TRUE(g_players[0].mission_stats.worse_rating_promo_points >
			0);
	XVT_ASSERT_TRUE(line_holds(
		"mission.rating_awarded slot=0 kind=\"worse\" ", "award=1 "));
}

/* The craft states 11 and 14 of type 7 (the opposites of "attacked by another
 * team" and "identified by another team") match a craft that no team attacked
 * or identified, and not one that every team did. */
static void check_object_states_untouched_and_all(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	memset(craft->identified_order_by_team, 0,
	       sizeof craft->identified_order_by_team);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 11), 1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 5), 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 14), 1);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 1), 0);
	for (int team = 0; team < 10; ++team) {
		craft->attacked_by_team[team] = 1;
		craft->identified_order_by_team[team] = 1;
	}
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 11), 0);
	XVT_ASSERT_INT_EQ(mission_object_matches_trigger_variable(0, 7, 14), 0);
}

/* A round that comes from hyperspace heads for the group's fifth mission
 * point and starts 8 times 65,535 units back along that heading, in all
 * three axes. */
static void check_spawn_hyperspace_toward_fifth_point(void)
{
	init_mission(MISSION_TYPE_COMBAT);
	arriving_group(0, 1, SPAWN_TYPE, 1);
	struct xvt_flight_group *group = &g_stored.groups[0];
	group->mission_point_x[0] = 10;
	group->mission_point_y[0] = 20;
	group->mission_point_z[0] = 30;
	group->mission_point_x[4] = 100;
	group->mission_point_y[4] = 50;
	group->mission_point_z[4] = -40;
	group->mission_point_enabled[4] = 1;
	spawn_mission_ready();
	start_the_clock();
	XVT_ASSERT_INT_EQ(spawn_craft(0, UINT16_MAX), 1);
	g_mission_elapsed_clock.seconds = 0;
	trig2_ctop((100 - 10) * 256, -(50 - 20) * 256, (-40 - 30) * 256);
	trig2_xyangle += 0x8000;
	trig2_pitch = 0x8000 - trig2_pitch;
	trig2_movexyz(UINT16_MAX, trig2_xyangle, (uint16_t)trig2_pitch);
	XVT_ASSERT_TRUE(trig2_xmovedist != 0 && trig2_ymovedist != 0 &&
			trig2_zmovedist != 0);
	int x;
	int y;
	int z;
	craft_place(0, 0, &x, &y, &z);
	XVT_ASSERT_INT_EQ(x, 10 * 256 + 8 * trig2_xmovedist);
	XVT_ASSERT_INT_EQ(y, -20 * 256 + 8 * trig2_ymovedist);
	XVT_ASSERT_INT_EQ(z, 30 * 256 + 8 * trig2_zmovedist);
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
			{"tournament_swap_wrong_group",
			 check_swap_tests_the_teams_own_group},
			{"ai_level_past_table", check_init_ai_level_in_range},
			{"melee_boost_all_teams_owned",
			 check_melee_boost_without_free_team},
			{"nopilot_unset_team_count",
			 check_balance_without_pilot_record},
			{"backdrop_images_shared",
			 check_backdrop_groups_keep_their_images},
			{"tie_global_goals_kept",
			 check_tie_global_goals_cleared},
			{"player_team_past_table",
			 check_init_player_team_past_table},
			{"damage_from_groups_kept",
			 check_spawn_clears_all_group_damage},
			{"seventh_craft_off_formation",
			 check_spawn_seventh_craft_place},
			{"debris_spot_taken",
			 check_static_debris_redraws_taken_spot},
			{"craft_type_past_table",
			 check_init_craft_type_in_range},
			{"ai_skill_past_table", check_spawn_ai_skill_in_table},
			{"global_goal_fourth_condition",
			 check_logic_global_goal_fourth_condition},
			{"formation_slot_past_six",
			 check_formation_slot_past_six},
			{"ai_rating_past_table",
			 check_credit_ai_level_past_table},
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
	check_init_layout();
	check_init_load_fails();
	check_init_binds_network_players();
	check_init_binds_by_player_number();
	check_init_enables_ai_opponents();
	check_init_craft_selection();
	check_init_host_warhead();
	check_init_ai_team_craft();
	check_init_ai_team_craft_tournament();
	check_init_ai_team_without_source();
	check_init_ai_team_swap();
	check_init_difficulty();
	check_init_melee_boosts();
	check_init_combat_balance();
	check_init_random_variation();
	check_init_random_special_cargo();
	check_init_player_craft();
	check_init_player_waves();
	check_init_models_needed();
	check_init_melee_markings();
	check_init_melee_arrive_only_if_human();
	check_init_clocks();
	check_init_backdrop_seed();
	check_init_backdrop_placement();
	check_init_backdrop_images();
	check_runtime_group_counts();
	check_runtime_arrival_difficulty();
	check_runtime_arrival_triggers_failed();
	check_runtime_arrival_at_start();
	check_runtime_roles();
	check_init_replaces_pools();
	check_runtime_resets();
	check_spawn_craft_values();
	check_spawn_status_codes();
	check_spawn_player_craft();
	check_spawn_formation();
	check_spawn_plans();
	check_spawn_transport_genus();
	check_spawn_table_full();
	check_spawn_beam_and_countermeasures();
	check_spawn_without_cannons();
	check_spawn_clears_damage_tally();
	check_spawn_global_unit_numbers();
	check_spawn_missile_boat_launchers();
	check_spawn_least_ammo();
	check_spawn_turrets_off();
	check_spawn_platform_beams();
	check_static_mine_grid();
	check_static_satellite();
	check_static_debris_field();
	check_static_table_full();
	check_sync_pilot_network_players();
	check_spawn_fresh_state();
	check_spawn_damage_amount();
	check_static_debris_beside_object();
	check_static_other_genus();
	check_spawn_from_mothership();
	check_logic_proving_grounds();
	check_logic_connected_players();
	check_logic_fg_goal_decided();
	check_logic_fg_goal_fixed_events();
	check_logic_fg_goal_time_limit();
	check_logic_fg_goal_unavailable_group();
	check_logic_fg_goal_per_craft();
	check_logic_global_goal_pairs();
	check_logic_global_goal_fixed_conditions();
	check_logic_global_goal_trigger_counts();
	check_logic_global_goal_team_filter();
	check_logic_global_goal_pays();
	check_logic_team_goal_status();
	check_logic_team_goal_kept();
	check_logic_primary_met();
	check_logic_primary_second_team();
	check_logic_primary_places();
	check_logic_primary_failed();
	check_logic_prevent_met();
	check_logic_primary_draw();
	check_logic_prevent_draw();
	check_logic_bonus_met();
	check_logic_melee_prevent_ignored();
	check_logic_message_scan_timer();
	check_logic_message_shown();
	check_logic_message_delay();
	check_logic_message_pair_join();
	check_condition_outcome_table();
	check_condition_survived();
	check_condition_skips_unknown_group();
	check_condition_amounts();
	check_condition_special_cargo_amounts();
	check_condition_team_filter();
	check_condition_live_craft();
	check_condition_live_craft_scope();
	check_condition_players_by_group();
	check_condition_players_by_number();
	check_condition_bonus_failed_and_unlisted();
	check_group_matches_variable_types();
	check_object_matches_variable();
	check_object_craft_states();
	check_arrivals_scan_timers();
	check_arrivals_trigger_joins();
	check_arrivals_delay_set();
	check_arrivals_delay_runs_out();
	check_arrivals_next_round();
	check_arrivals_player_group_held();
	check_arrivals_stop_rules();
	check_wave_completion_waits();
	check_wave_completion_held();
	check_wave_completion_stops();
	check_wave_completion_makes_room();
	check_formation_slot_world_loc();
	check_formation_slot_static();
	check_static_group_arrival_and_rounds();
	check_spawn_hyperspace_arrival();
	check_spawn_faces_fifth_point();
	check_spawn_second_point();
	check_spawn_melee_start_points();
	check_spawn_melee_random_offset();
	check_spawn_mothership_formation();
	check_credit_tiers();
	check_credit_goal_scores();
	check_credit_friendly_kill();
	check_credit_player_victim();
	check_credit_worse_rating();
	check_credit_without_damage();
	check_credit_team_share();
	check_team_kill_tiers();
	check_team_kill_friendly();
	check_craft_loss_classes();
	check_craft_loss_pending_and_owner();
	check_loss_attribution();
	check_projectile_hits();
	check_goal_score_messages();
	check_outcome_special_cargo_takebacks();
	check_credit_rating_factor();
	check_credit_worse_tiers();
	check_credit_special_cargo_victim();
	check_logic_prevent_met_skips_empty_text();
	check_condition_more_cases();
	check_object_matches_opposite_kinds();
	check_spawn_single_craft_counts();
	check_arrivals_static_group_next_round();
	check_outcome_dropoff_closes_group();
	check_credit_with_primary_goal();
	check_credit_worse_rating_of_zero();
	check_object_states_untouched_and_all();
	check_spawn_hyperspace_toward_fifth_point();
	return 0;
}

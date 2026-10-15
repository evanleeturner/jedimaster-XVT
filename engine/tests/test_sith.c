/* Checks the Sith setting and the promotion at mission start
 * (xvt_runtime/runtime/sith.h): none at start, the three values the setting
 * keeps, a value that is none of them read as none, the launch description's
 * unset value read as none, and the words the logs print; then the promotion
 * on flight-group tables this file seeds: which group is chosen (hostile,
 * flown by no player, starfighters, the highest level, a tie to the lowest
 * index), what each setting does to it, and that nothing is touched or logged
 * with the setting none. No game data is read. */
#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/launch_description.h"
#include "xvt_runtime/runtime/sith.h"

enum {
	LINE_CAPACITY = 16,
	LINE_SIZE = 400,
	/* Craft types the seeded tables know: a starfighter and a starship. */
	FIGHTER_CRAFT = 1,
	STARSHIP_CRAFT = 2,
	FIGHTER_OBJECT = 5,
	STARSHIP_OBJECT = 6,
};

static char g_lines[LINE_CAPACITY][LINE_SIZE];
static int g_line_count;

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	if (g_line_count < LINE_CAPACITY) {
		snprintf(g_lines[g_line_count++], LINE_SIZE, "%s", message);
	}
}

/* The number of caught lines holding text. */
static int lines_with(const char *text)
{
	int count = 0;
	for (int i = 0; i < g_line_count; ++i) {
		count += strstr(g_lines[i], text) != NULL;
	}
	return count;
}

static void check_none_at_start(void)
{
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
}

static void check_values_kept(void)
{
	xvt_sith_set_setting(XVT_SITH_LORD);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_LORD);
	xvt_sith_set_setting(XVT_SITH_APPRENTICES);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_APPRENTICES);
	xvt_sith_set_setting(XVT_SITH_NONE);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
}

/* A value outside the three is none, not kept. */
static void check_other_values_are_none(void)
{
	static const int others[] = {-1, 3, 4, 255, 1000000};
	for (size_t index = 0; index < sizeof(others) / sizeof(others[0]);
	     ++index) {
		xvt_sith_set_setting(XVT_SITH_LORD);
		xvt_sith_set_setting(others[index]);
		XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
	}
}

/* A description's value sets the setting; a description that leaves it out
 * sets none, so a machine never keeps an earlier battle's setting. */
static void check_from_description(void)
{
	xvt_sith_set_from_description(2);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_LORD);
	xvt_sith_set_from_description(1);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_APPRENTICES);
	xvt_sith_set_from_description(2);
	xvt_sith_set_from_description(XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
	xvt_sith_set_from_description(0);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
}

static void check_names(void)
{
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(XVT_SITH_NONE), "none"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(XVT_SITH_APPRENTICES),
				 "apprentices"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(XVT_SITH_LORD), "lord"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(7), "none"), 0);
}

/* ---- the promotion ----------------------------------------------------- */

/* Eight flight groups, none flown by a player but group 0, which team 0
 * (player 0's team) flies:
 *   0 team 0 level 3, flown by a player, starfighters
 *   1 team 1 level 2, starfighters, hostile
 *   2 team 1 level 4, starfighters, hostile
 *   3 team 2 level 5, starships, hostile
 *   4 team 0 level 5, starfighters, the player's own side
 *   5 team 3 level 5, starfighters, an ally of team 0
 *   6 team 1 level 5, starfighters, hostile, a player can fly it
 *   7 team 1 level 1, craft type 96, the first past the table, hostile
 * Group 2 is the one to choose. */
static void seed_groups(void)
{
	memset(g_mission_flight_groups, 0,
	       8 * sizeof g_mission_flight_groups[0]);
	memset(g_mission_teams, 0, sizeof g_mission_teams);
	memset(g_players, 0, sizeof g_players[0]);
	g_craft_type_to_object_type[FIGHTER_CRAFT] = FIGHTER_OBJECT;
	g_craft_type_to_object_type[STARSHIP_CRAFT] = STARSHIP_OBJECT;
	g_object_type_table[FIGHTER_OBJECT].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_object_type_table[STARSHIP_OBJECT].genus_id = CRAFT_GENUS_STARSHIP;
	g_mission_teams[3].allies[0] = 1;
	static const struct {
		uint8_t team;
		uint8_t level;
		uint8_t craft;
		uint8_t player_number;
	} groups[8] = {{0, 3, FIGHTER_CRAFT, 1}, {1, 2, FIGHTER_CRAFT, 0},
		       {1, 4, FIGHTER_CRAFT, 0}, {2, 5, STARSHIP_CRAFT, 0},
		       {0, 5, FIGHTER_CRAFT, 0}, {3, 5, FIGHTER_CRAFT, 0},
		       {1, 5, FIGHTER_CRAFT, 1}, {1, 1, 96, 0}};
	for (int i = 0; i < 8; ++i) {
		g_mission_flight_groups[i].fg.team = groups[i].team;
		g_mission_flight_groups[i].fg.group_ai = groups[i].level;
		g_mission_flight_groups[i].fg.craft_type = groups[i].craft;
		g_mission_flight_groups[i].fg.player_number =
			groups[i].player_number;
		g_mission_flight_groups[i].fg.number_of_craft = 4;
	}
	g_mission_header.num_flight_groups = 8;
	g_players[0].team = 0;
	g_line_count = 0;
}

static void expect_levels(const int *levels)
{
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[i].fg.group_ai,
				  levels[i]);
	}
}

/* With the setting none nothing is touched or logged, and no Lord is named. */
static void check_none_changes_nothing(void)
{
	static const int levels[8] = {3, 2, 4, 5, 5, 5, 5, 1};
	seed_groups();
	xvt_sith_set_setting(XVT_SITH_NONE);
	xvt_sith_promote_flight_groups();
	expect_levels(levels);
	XVT_ASSERT_INT_EQ(g_line_count, 0);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
}

/* Apprentices promotes group 2 to level 6 and names no Lord. */
static void check_apprentices(void)
{
	static const int levels[8] = {3, 2, 6, 5, 5, 5, 5, 1};
	seed_groups();
	xvt_sith_set_setting(XVT_SITH_APPRENTICES);
	xvt_sith_promote_flight_groups();
	expect_levels(levels);
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	XVT_ASSERT_TRUE(
		strstr(g_lines[0],
		       "sith.promoted setting=\"apprentices\" fg=2 team=1 craft=1 count=4 level_was=4 level=6 lord_rank=0") !=
		NULL);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
	for (int craft = 0; craft < 4; ++craft) {
		XVT_ASSERT_INT_EQ(xvt_sith_craft_level(2, 6, craft), 6);
	}
}

/* Lord promotes the same group and gives level 7 to its first craft alone. */
static void check_lord(void)
{
	static const int levels[8] = {3, 2, 6, 5, 5, 5, 5, 1};
	seed_groups();
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	expect_levels(levels);
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	XVT_ASSERT_TRUE(
		strstr(g_lines[0],
		       "sith.promoted setting=\"lord\" fg=2 team=1 craft=1 count=4 level_was=4 level=6 lord_rank=7") !=
		NULL);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), 2);
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(2, 6, 0), 7);
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(2, 6, 1), 6);
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(2, 6, 3), 6);
	/* Another group's first craft, and no group, keep their level. */
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(1, 2, 0), 2);
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(0, 3, 0), 3);
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(-1, 3, 0), 3);
	/* A later call clears the Lord when the setting is none. */
	xvt_sith_set_setting(XVT_SITH_NONE);
	xvt_sith_promote_flight_groups();
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(2, 6, 0), 6);
}

/* A tie goes to the lowest group index; a friendly, an allied and a
 * player's group are never chosen however high they stand. */
static void check_tie_and_exclusions(void)
{
	seed_groups();
	g_mission_flight_groups[1].fg.group_ai = 4;
	g_mission_flight_groups[2].fg.group_ai = 4;
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.group_ai, 6);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.group_ai, 4);
	for (int fg = 3; fg < 8; ++fg) {
		XVT_ASSERT_INT_EQ(g_mission_flight_groups[fg].fg.group_ai,
				  fg == 7 ? 1 : 5);
	}
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.group_ai, 3);
}

/* Player 0's team decides who is hostile: from team 1's side, team 0's
 * starfighter group is the one promoted. */
static void check_hostile_follows_player_team(void)
{
	seed_groups();
	g_players[0].team = 1;
	xvt_sith_set_setting(XVT_SITH_APPRENTICES);
	xvt_sith_promote_flight_groups();
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[4].fg.group_ai, 6);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[1].fg.group_ai, 2);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.group_ai, 4);
}

/* Group 0 can be the one chosen, and a later group of the same level does not
 * take its place: with player 0 on a team no group flies for, every group is
 * hostile, and group 0 (made computer-flown here) ties with groups 4 and 5. */
static void check_group_zero_chosen(void)
{
	seed_groups();
	g_players[0].team = 5;
	g_mission_flight_groups[0].fg.player_number = 0;
	g_mission_flight_groups[0].fg.group_ai = 5;
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), 0);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[0].fg.group_ai, 6);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[4].fg.group_ai, 5);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[5].fg.group_ai, 5);
	XVT_ASSERT_INT_EQ(xvt_sith_craft_level(0, 6, 0), 7);
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	XVT_ASSERT_TRUE(strstr(g_lines[0],
			       "sith.promoted setting=\"lord\" fg=0 ") != NULL);
}

/* A group already past level 6 keeps its level. */
static void check_higher_level_kept(void)
{
	seed_groups();
	g_mission_flight_groups[2].fg.group_ai = 7;
	xvt_sith_set_setting(XVT_SITH_APPRENTICES);
	xvt_sith_promote_flight_groups();
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[2].fg.group_ai, 7);
}

/* Nothing to promote: the tables are untouched and the log says why. */
static void check_nothing_to_promote(void)
{
	static const int levels[8] = {3, 2, 4, 5, 5, 5, 5, 1};
	/* Hostile computer groups, none of them starfighters. */
	seed_groups();
	g_mission_flight_groups[1].fg.craft_type = STARSHIP_CRAFT;
	g_mission_flight_groups[2].fg.craft_type = STARSHIP_CRAFT;
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	expect_levels(levels);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	XVT_ASSERT_TRUE(
		strstr(g_lines[0],
		       "sith.none_to_promote setting=\"lord\" groups=8 hostile_computer=4 reason=\"none of those groups flies starfighters\"") !=
		NULL);
	/* No hostile group that no player flies. */
	seed_groups();
	for (int fg = 0; fg < 8; ++fg) {
		g_mission_flight_groups[fg].fg.team = 0;
	}
	xvt_sith_set_setting(XVT_SITH_APPRENTICES);
	xvt_sith_promote_flight_groups();
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	XVT_ASSERT_TRUE(
		strstr(g_lines[0],
		       "sith.none_to_promote setting=\"apprentices\" groups=8 hostile_computer=0 reason=\"no hostile flight group that no player can fly\"") !=
		NULL);
	/* No flight groups at all. */
	seed_groups();
	g_mission_header.num_flight_groups = 0;
	xvt_sith_promote_flight_groups();
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	XVT_ASSERT_INT_EQ(lines_with("sith.none_to_promote"), 1);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
}

/* The same tables and setting give the same choice and the same levels each
 * time: nothing in the choice reads a random number or the clock. */
static void check_same_choice_each_time(void)
{
	int chosen = -1;
	int levels[8];
	for (int run = 0; run < 3; ++run) {
		seed_groups();
		xvt_sith_set_setting(XVT_SITH_LORD);
		xvt_sith_promote_flight_groups();
		if (run == 0) {
			chosen = xvt_sith_lord_flight_group();
			for (int i = 0; i < 8; ++i) {
				levels[i] =
					g_mission_flight_groups[i].fg.group_ai;
			}
		}
		XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), chosen);
		expect_levels(levels);
	}
}

int main(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_none_at_start();
	check_values_kept();
	check_other_values_are_none();
	check_from_description();
	check_names();
	check_none_changes_nothing();
	check_apprentices();
	check_lord();
	check_tie_and_exclusions();
	check_hostile_follows_player_team();
	check_group_zero_chosen();
	check_higher_level_kept();
	check_nothing_to_promote();
	check_same_choice_each_time();
	return 0;
}

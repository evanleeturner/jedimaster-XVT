/* Checks the Sith setting and the promotion at mission start
 * (xvt_runtime/runtime/sith.h): none at start, the three values the setting
 * keeps, a value that is none of them read as none, the launch description's
 * unset value read as none, and the words the logs print; then the promotion
 * on flight-group tables this file seeds: which group is chosen (hostile,
 * flown by no player, starfighters, the highest level, a tie to the lowest
 * index), what each setting does to it, and that nothing is touched or logged
 * with the setting none; last the tag a Sith craft's name carries on the
 * cockpit's target readouts: the tag for every level, and the append for
 * rank 6, 7 and 5 craft, a slot that is no craft slot and a craft slot with
 * no mobile object or craft record, with the buffer and the log line
 * checked. No game data is read. */
#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
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

/* The battle report: with the setting none the group and craft type read -1
 * and every count 0; with a promotion it names the group and counts the craft
 * the group spawned and the ones destroyed, by level. */
static void check_report(void)
{
	struct xvt_sith_report report;
	seed_groups();
	xvt_sith_end_flight();
	xvt_sith_promote_flight_groups();
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(report.setting, XVT_SITH_NONE);
	XVT_ASSERT_INT_EQ(report.flight_group, -1);
	XVT_ASSERT_INT_EQ(report.craft_type, -1);
	XVT_ASSERT_INT_EQ(report.flight_name[0], '\0');
	XVT_ASSERT_INT_EQ(report.apprentices_flown + report.apprentices_lost +
				  report.lord_flown + report.lord_lost,
			  0);
	XVT_ASSERT_INT_EQ(report.why[0], '\0');

	/* Lord: group 2 spawned 4 craft, the Lord and 3 apprentices; two
	 * apprentices and the Lord were destroyed, and a level 5 craft that
	 * was destroyed counts for nothing. */
	seed_groups();
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	snprintf(g_mission_flight_groups[2].fg.name, 20, "%s",
		 "Beta \"One\" \\ x");
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	g_mission_fg_stats[2].spawned_craft_count = 4;
	xvt_sith_note_craft_destroyed(6);
	xvt_sith_note_craft_destroyed(6);
	xvt_sith_note_craft_destroyed(7);
	xvt_sith_note_craft_destroyed(5);
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(report.setting, XVT_SITH_LORD);
	XVT_ASSERT_INT_EQ(report.flight_group, 2);
	XVT_ASSERT_INT_EQ(report.craft_type, FIGHTER_CRAFT);
	XVT_ASSERT_INT_EQ(strcmp(report.flight_name, "Beta \"One\" \\ x"), 0);
	XVT_ASSERT_INT_EQ(report.apprentices_flown, 3);
	XVT_ASSERT_INT_EQ(report.apprentices_lost, 2);
	XVT_ASSERT_INT_EQ(report.lord_flown, 1);
	XVT_ASSERT_INT_EQ(report.lord_lost, 1);
	XVT_ASSERT_INT_EQ(report.why[0], '\0');

	/* Apprentices: every craft spawned is an apprentice. */
	seed_groups();
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	xvt_sith_set_setting(XVT_SITH_APPRENTICES);
	xvt_sith_promote_flight_groups();
	g_mission_fg_stats[2].spawned_craft_count = 4;
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(report.apprentices_flown, 4);
	XVT_ASSERT_INT_EQ(report.apprentices_lost, 0);
	XVT_ASSERT_INT_EQ(report.lord_flown, 0);

	/* Lord whose group never arrived: nothing flown. */
	seed_groups();
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(report.flight_group, 2);
	XVT_ASSERT_INT_EQ(report.lord_flown, 0);
	XVT_ASSERT_INT_EQ(report.apprentices_flown, 0);
}

/* No group promoted: the report names no group and gives the reason the log
 * line gives. */
static void check_report_nothing_promoted(void)
{
	struct xvt_sith_report report;
	seed_groups();
	g_mission_flight_groups[1].fg.craft_type = STARSHIP_CRAFT;
	g_mission_flight_groups[2].fg.craft_type = STARSHIP_CRAFT;
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(report.setting, XVT_SITH_LORD);
	XVT_ASSERT_INT_EQ(report.flight_group, -1);
	XVT_ASSERT_INT_EQ(report.craft_type, -1);
	XVT_ASSERT_INT_EQ(report.flight_name[0], '\0');
	XVT_ASSERT_INT_EQ(
		strcmp(report.why, "none of those groups flies starfighters"),
		0);
	seed_groups();
	g_mission_header.num_flight_groups = 0;
	xvt_sith_promote_flight_groups();
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(
		strcmp(report.why,
		       "no hostile flight group that no player can fly"),
		0);
}

/* The flight's end returns the setting to none and clears the Lord's group,
 * the promoted group and the counts. */
static void check_end_flight(void)
{
	struct xvt_sith_report report;
	seed_groups();
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	xvt_sith_note_craft_destroyed(7);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), 2);
	xvt_sith_end_flight();
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(report.setting, XVT_SITH_NONE);
	XVT_ASSERT_INT_EQ(report.flight_group, -1);
	/* The next battle starts clean even with the setting back on. */
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_get_report(&report);
	XVT_ASSERT_INT_EQ(report.lord_lost, 0);
	XVT_ASSERT_INT_EQ(report.flight_group, -1);
	xvt_sith_end_flight();
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

/* ---- the target tag ---------------------------------------------------- */

enum { TAG_SLOTS = 8, TAG_CRAFT_SLOTS = 5 };

static struct object_record g_tag_objects[TAG_SLOTS];
static struct mobile_object g_tag_mobiles[TAG_SLOTS];
static struct craft_data g_tag_crafts[TAG_SLOTS];

/* Slots 0 to 4 are craft slots and 5 to 7 are not. Slot 0 holds rank 6, 1
 * rank 7, 2 rank 5, 3 a mobile object with no craft record, 4 no mobile
 * object. Slot 6, past the craft slots, holds a full rank 7 craft record. */
static void seed_tag_slots(void)
{
	memset(g_tag_objects, 0, sizeof g_tag_objects);
	memset(g_tag_mobiles, 0, sizeof g_tag_mobiles);
	memset(g_tag_crafts, 0, sizeof g_tag_crafts);
	static const uint8_t ranks[TAG_SLOTS] = {6, 7, 5, 7, 7, 0, 7, 0};
	for (int i = 0; i < TAG_SLOTS; ++i) {
		g_tag_crafts[i].ai_rank = ranks[i];
		g_tag_mobiles[i].p_craft = &g_tag_crafts[i];
		g_tag_objects[i].mobj = &g_tag_mobiles[i];
	}
	g_tag_mobiles[3].p_craft = NULL;
	g_tag_objects[4].mobj = NULL;
	g_object_table = g_tag_objects;
	g_active_region_craft_object_slot_end = TAG_CRAFT_SLOTS;
	g_line_count = 0;
}

static void check_rank_tags(void)
{
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_rank_tag(6), " Sith"), 0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_rank_tag(7), " Sith Lord"), 0);
	static const int others[] = {-1, 0, 1, 2, 3, 4, 5, 8, 255, 1000000};
	for (size_t index = 0; index < sizeof(others) / sizeof(others[0]);
	     ++index) {
		XVT_ASSERT_INT_EQ(xvt_sith_rank_tag(others[index])[0], '\0');
	}
}

static void check_tag_appended_for_sith_ranks(void)
{
	seed_tag_slots();
	flight_text_set_scratch("Hawk");
	XVT_ASSERT_INT_EQ(xvt_sith_append_target_tag(0, 0), 1);
	XVT_ASSERT_INT_EQ(strcmp(g_flight_text_scratch_buffer, "Hawk Sith"), 0);
	flight_text_set_scratch("Hawk");
	XVT_ASSERT_INT_EQ(xvt_sith_append_target_tag(1, 0), 1);
	XVT_ASSERT_INT_EQ(
		strcmp(g_flight_text_scratch_buffer, "Hawk Sith Lord"), 0);
	XVT_ASSERT_INT_EQ(g_line_count, 0);
}

static void check_no_tag_leaves_buffer(void)
{
	seed_tag_slots();
	/* rank 5, no craft record, no mobile object, a slot past the craft
	 * slots holding rank 7, the table's far end, and a slot far outside */
	static const unsigned int slots[] = {2, 3, 4, 6, 7, 5000, 0xFFFFFFFFu};
	for (size_t index = 0; index < sizeof(slots) / sizeof(slots[0]);
	     ++index) {
		flight_text_set_scratch("Hawk");
		XVT_ASSERT_INT_EQ(xvt_sith_append_target_tag(slots[index], 1),
				  0);
		XVT_ASSERT_INT_EQ(strcmp(g_flight_text_scratch_buffer, "Hawk"),
				  0);
	}
	XVT_ASSERT_INT_EQ(g_line_count, 0);
	g_object_table = NULL;
	flight_text_set_scratch("Hawk");
	XVT_ASSERT_INT_EQ(xvt_sith_append_target_tag(0, 1), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_flight_text_scratch_buffer, "Hawk"), 0);
}

/* The log line comes once per call that asks for it, with the slot, the rank
 * and the tag; a call that does not ask writes nothing. */
static void check_tag_log_line(void)
{
	seed_tag_slots();
	flight_text_set_scratch("Hawk");
	xvt_sith_append_target_tag(1, 1);
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	XVT_ASSERT_INT_EQ(lines_with("sith.target_tag"), 1);
	XVT_ASSERT_INT_EQ(lines_with("slot=1 rank=7 tag=\" Sith Lord\""), 1);
	flight_text_set_scratch("Hawk");
	xvt_sith_append_target_tag(0, 1);
	XVT_ASSERT_INT_EQ(g_line_count, 2);
	XVT_ASSERT_INT_EQ(lines_with("slot=0 rank=6 tag=\" Sith\""), 1);
	flight_text_set_scratch("Hawk");
	xvt_sith_append_target_tag(0, 0);
	XVT_ASSERT_INT_EQ(g_line_count, 2);
	g_object_table = NULL;
}

/* ---- the steering rates ------------------------------------------------ */

static struct craft_data g_rate_craft;
static struct object_record g_rate_objects[4];

/* A craft record at rank with throttle and the model rates (turn, pitch,
 * roll), engines working unless engines is 0. The three rates differ from
 * each other so that a swapped field shows. */
static void seed_rate_craft(int rank, uint16_t throttle, int turn, int pitch,
			    int roll, int engines)
{
	memset(&g_rate_craft, 0, sizeof g_rate_craft);
	g_rate_craft.ai_rank = (uint8_t)rank;
	g_rate_craft.throttle_speed = throttle;
	g_rate_craft.working_subsystems =
		engines ? 0x03FF
			: (uint16_t)(0x03FF & ~CRAFT_SUBSYSTEM_FLAG_ENGINES);
	g_rate_craft.ai_flight.turn_rate = (int16_t)turn;
	g_rate_craft.ai_flight.pitch_rate = (int16_t)pitch;
	g_rate_craft.ai_flight.roll_rate = (int16_t)roll;
}

static void expect_rates(unsigned slot, int turn, int pitch, int roll)
{
	struct xvt_sith_rates rates = {1, 2, 3};
	xvt_sith_steering_rates(slot, &g_rate_craft, &rates);
	XVT_ASSERT_INT_EQ(rates.turn, turn);
	XVT_ASSERT_INT_EQ(rates.pitch, pitch);
	XVT_ASSERT_INT_EQ(rates.roll, roll);
}

/* An X-wing's rates (10,240 turn, 5,120 pitch, 9,216 roll) at four
 * throttles and the neighbours of a third, for ranks 6 and 7; the values
 * are the receipt's (receipt.py). */
static void check_rates_x_wing(void)
{
	static const struct {
		uint16_t throttle;
		int turn, pitch, roll;
	} want[] = {
		{0, 10240, 5120, 9216},	      {0x5554, 10240, 12700, 9797},
		{0x5555, 10240, 12700, 9797}, {0x5556, 10240, 12700, 9797},
		{0xAAAA, 10240, 10584, 9216}, {0xFFFF, 10240, 8466, 9216},
	};
	g_object_table = g_rate_objects;
	for (int rank = 6; rank <= 7; ++rank) {
		for (size_t k = 0; k < sizeof want / sizeof want[0]; ++k) {
			seed_rate_craft(rank, want[k].throttle, 10240, 5120,
					9216, 1);
			expect_rates(1, want[k].turn, want[k].pitch,
				     want[k].roll);
		}
	}
	g_object_table = NULL;
}

/* Other ranks keep the craft's three fields at every throttle, rank 8 is no
 * Sith, and a Sith craft with its engines out flies at the stopped values. */
static void check_rates_other_ranks_and_engines(void)
{
	static const uint16_t throttles[] = {0, 0x5555, 0xAAAA, 0xFFFF};
	static const int ranks[] = {0, 5, 8, 255};
	for (size_t r = 0; r < sizeof ranks / sizeof ranks[0]; ++r) {
		for (size_t t = 0; t < 4; ++t) {
			seed_rate_craft(ranks[r], throttles[t], 10240, 5120,
					9216, 1);
			expect_rates(1, 10240, 5120, 9216);
		}
	}
	seed_rate_craft(7, 0xFFFF, 10240, 5120, 9216, 0);
	expect_rates(1, 10240, 5120, 9216);
	seed_rate_craft(7, 0x5555, 10240, 5120, 9216, 0);
	expect_rates(1, 10240, 5120, 9216);
}

/* Models with odd rates: a roll rate of 0 leaves the turn at its own rate
 * (the yaw reads the roll rate, then the floor holds the turn field); a
 * pitch rate whose player value passes 32,767 is kept, not wrapped; one
 * passing 0xFFFF is capped; a rate that is an exact multiple of its
 * segment has no fractional share. */
static void check_rates_odd_models(void)
{
	seed_rate_craft(6, 0x5555, 10240, 5120, 0, 1);
	expect_rates(1, 10240, 12700, 0);
	seed_rate_craft(6, 0x5555, 10240, 16384, 9216, 1);
	expect_rates(1, 10240, 40640, 9797);
	seed_rate_craft(7, 0x5555, 10240, 0x7FFF, 9216, 1);
	expect_rates(1, 10240, 0xFFFF, 9797);
	/* A pitch rate whose player value is exactly 65,536 is capped too. */
	seed_rate_craft(7, 0x5555, 10240, 26421, 9216, 1);
	expect_rates(1, 10240, 0xFFFF, 9797);
	seed_rate_craft(7, 0x5555, 10240, 5120, 0x3800, 1);
	expect_rates(1, 15240, 12700, 15240);
}

/* One line at the first call, none on a repeat, one when the throttle's
 * scale changes, one for a new craft in the same slot, none for rank 5, and
 * one again after the flight's end cleared the memory. */
static void check_rates_log(void)
{
	g_object_table = g_rate_objects;
	memset(g_rate_objects, 0, sizeof g_rate_objects);
	g_rate_objects[1].object_signature = 100;
	xvt_sith_end_flight();
	g_line_count = 0;
	seed_rate_craft(6, 0x5555, 10240, 5120, 9216, 1);
	expect_rates(1, 10240, 12700, 9797);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), 1);
	XVT_ASSERT_INT_EQ(
		lines_with(
			"slot=1 rank=6 throttle=21845 scale=65535 turn=10240 pitch=12700 roll=9797"),
		1);
	expect_rates(1, 10240, 12700, 9797);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), 1);
	seed_rate_craft(6, 0xFFFF, 10240, 5120, 9216, 1);
	expect_rates(1, 10240, 8466, 9216);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), 2);
	XVT_ASSERT_INT_EQ(
		lines_with(
			"slot=1 rank=6 throttle=65535 scale=43690 turn=10240 pitch=8466 roll=9216"),
		1);
	g_rate_objects[1].object_signature = 101;
	expect_rates(1, 10240, 8466, 9216);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), 3);
	seed_rate_craft(5, 0xFFFF, 10240, 5120, 9216, 1);
	expect_rates(1, 10240, 5120, 9216);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), 3);
	seed_rate_craft(7, 0xFFFF, 10240, 5120, 9216, 1);
	expect_rates(1, 10240, 8466, 9216);
	XVT_ASSERT_INT_EQ(lines_with("slot=1 rank=7"), 1);
	xvt_sith_end_flight();
	expect_rates(1, 10240, 8466, 9216);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), 5);
	seed_rate_craft(7, 0xFFFF, 10240, 5120, 9216, 1);
	xvt_sith_promote_flight_groups();
	expect_rates(1, 10240, 8466, 9216);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), 6);
	/* Engines out count as throttle 0 in the line. */
	seed_rate_craft(6, 0xFFFF, 10240, 5120, 9216, 0);
	expect_rates(2, 10240, 5120, 9216);
	XVT_ASSERT_INT_EQ(
		lines_with(
			"slot=2 rank=6 throttle=0 scale=21845 turn=10240 pitch=5120 roll=9216"),
		1);
	/* Slots past the memory still get their rates, and log nothing. */
	int before = lines_with("sith.steering_rates");
	seed_rate_craft(6, 0x5555, 10240, 5120, 9216, 1);
	expect_rates(32, 10240, 12700, 9797);
	expect_rates(5000, 10240, 12700, 9797);
	XVT_ASSERT_INT_EQ(lines_with("sith.steering_rates"), before);
	g_object_table = NULL;
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
	check_report();
	check_report_nothing_promoted();
	check_end_flight();
	check_rank_tags();
	check_tag_appended_for_sith_ranks();
	check_no_tag_leaves_buffer();
	check_tag_log_line();
	check_rates_x_wing();
	check_rates_other_ranks_and_engines();
	check_rates_odd_models();
	check_rates_log();
	return 0;
}

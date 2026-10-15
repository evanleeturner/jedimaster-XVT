/* Checks the --launch door (xvt_runtime/runtime/launch_door.h) against the
 * promises in its header that hold without a window, a pilot loaded from the
 * game's files or the game's mission lists: opening a description (a missing
 * file, text that is not a description, a pilot name holding a switch word, a
 * good one with and without a result path, an AI program that cannot be
 * started), the result file each ending writes, and the exit code the door
 * asks the program to end with. The test
 * keeps its description and result files in a temporary folder and sets the
 * pilot record numbers the result is read from.
 *
 * Not checked here: the steps on the concourse and the mission setup screen.
 * They load the pilot by the game's file lists, read the mission list from the
 * installation and switch real screens; a test would need the game's files and
 * a window. They are proven by starting the same battle through the menus and
 * through the door and comparing the two recordings. */
#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/launch_door.h"
#include "xvt_runtime/runtime/launch_result.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/runtime/sith.h"
#include "xvt_runtime/timing/host_clock.h"

/* The folder for temporary folders: $TMPDIR, else the working folder. */
static const char *tmp_base(void)
{
	const char *base = getenv("TMPDIR");
	return base && base[0] ? base : ".";
}

static char g_folder[64];
/* The last launch.result_read line the door logged. */
static char g_read_line[640];

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	if (strstr(message, "launch.result_read") != NULL) {
		snprintf(g_read_line, sizeof(g_read_line), "%s", message);
	}
}

/* The run id the tests open the door with. */
#define TEST_RUN_ID 0x1a2b3c4du

/* Writes text to a file in the folder and returns its path in path. */
static void put(const char *name, const char *text, char *path, size_t size)
{
	snprintf(path, size, "%s/%s", g_folder, name);
	FILE *file = fopen(path, "wb");
	XVT_ASSERT_TRUE(file != NULL);
	fputs(text, file);
	fclose(file);
}

/* Reads a whole file into text; returns 0 when it is not there. */
static int slurp(const char *path, char *text, size_t size)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return 0;
	}
	size_t length = fread(text, 1, size - 1, file);
	fclose(file);
	text[length] = '\0';
	return 1;
}

static void check_missing_file(void)
{
	char path[256];
	char result[256];
	char text[2048];
	snprintf(path, sizeof(path), "%s/none.json", g_folder);
	snprintf(result, sizeof(result), "%s.result.json", path);
	XVT_ASSERT_INT_EQ(xvt_launch_door_open(path, TEST_RUN_ID, ""), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"refused\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"exit_code\":3") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "could not be read") != NULL);
	unlink(result);
}

/* A description whose pilot name holds a switch word. */
#define NOSFX_DESCRIPTION g_nosfx_description

static char g_nosfx_description[256];

/* Fills g_nosfx_description. */
static void make_nosfx_description(void)
{
	const char *parts[] = {
		"{\"schema_version\":1,",	"\"mode\":\"single_player\",",
		"\"game_type\":\"melee\",",	"\"side\":\"none\",",
		"\"mission\":\"melee/a.tie\",", "\"pilot\":\"Mr nosfx\"}"};
	g_nosfx_description[0] = '\0';
	for (size_t index = 0; index < sizeof(parts) / sizeof(parts[0]);
	     ++index) {
		strcat(g_nosfx_description, parts[index]);
	}
}

static void check_refusals_leave_a_result(void)
{
	make_nosfx_description();
	const struct {
		const char *text;
		const char *why;
	} cases[] = {{"not json", "not a JSON object"},
		     {"{\"schema_version\":1,\"mode\":\"multiplayer\"}",
		      "\\\"mode\\\" must be"},
		     {NOSFX_DESCRIPTION, "switch word"},
		     {"{\"schema_version\":1,\"mode\":\"single_player\","
		      "\"game_type\":\"training\",\"side\":\"rebel\","
		      "\"mission\":\"train/1ta02bz.tie\",\"pilot\":\"Ace\","
		      "\"ai_program\":{\"python\":\"/nonexistent/python\","
		      "\"path\":\"x\"}}",
		      "could not be started"}};
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]);
	     ++index) {
		char path[256];
		char result[256];
		char text[2048];
		put("bad.json", cases[index].text, path, sizeof(path));
		snprintf(result, sizeof(result), "%s.result.json", path);
		XVT_ASSERT_INT_EQ(xvt_launch_door_open(path, TEST_RUN_ID, ""),
				  0);
		XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
		XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
		XVT_ASSERT_TRUE(strstr(text, "\"status\":\"refused\"") != NULL);
		XVT_ASSERT_TRUE(strstr(text, cases[index].why) != NULL);
		unlink(result);
		unlink(path);
	}
}

/* Opens a good description, with an extra member text, and returns the
 * description's path. */
static void open_good(const char *extra, char *path, size_t size)
{
	const char *parts[] = {"{\"schema_version\":1,",
			       "\"mode\":\"single_player\",",
			       "\"game_type\":\"training\",",
			       "\"side\":\"rebel\",",
			       "\"mission\":\"train/1ta02bz.tie\",",
			       "\"pilot\":\"Ace\""};
	char text[512] = "";
	for (size_t index = 0; index < sizeof(parts) / sizeof(parts[0]);
	     ++index) {
		strcat(text, parts[index]);
	}
	strcat(text, extra);
	strcat(text, "}");
	put("good.json", text, path, size);
	XVT_ASSERT_INT_EQ(xvt_launch_door_open(path, TEST_RUN_ID, ""), 1);
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 1);
}

/* Sets the pilot record numbers the result is read from. */
static void set_battle_numbers(int local, int team)
{
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	g_local_pilot_network_player_index = local;
	g_pilot_data.team = team;
	if (local >= 0 && local < 8) {
		g_pilot_data.network_players[local].total_score = 120;
		g_pilot_data.network_players[local].kills = 4;
		g_pilot_data.network_players[local].total_losses = 1;
	}
	if (team >= 0 && team < 10) {
		g_pilot_data.teams[team].mission_time = 305;
		g_pilot_data.teams[team].is_mission_completed = 1;
	}
}

static void check_flown_result(void)
{
	char path[256];
	char result[256];
	char text[2048];
	snprintf(result, sizeof(result), "%s/flown.json", g_folder);
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s\"", result);
	open_good(extra, path, sizeof(path));
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 0);
	set_battle_numbers(0, 2);
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_port_get_exit_code(), XVT_LAUNCH_EXIT_FLOWN);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"flown\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"outcome\":\"completed\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"score\":120") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"kills\":4") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"losses\":1") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"goals_met_seconds\":305") != NULL);
	unlink(result);
	/* A finished door ignores a second ending. */
	xvt_launch_door_finish(0);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 0);
	unlink(path);
}

static void check_not_completed_and_out_of_range(void)
{
	char path[256];
	char result[256];
	char text[2048];
	snprintf(result, sizeof(result), "%s/odd.json", g_folder);
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s\"", result);
	open_good(extra, path, sizeof(path));
	set_battle_numbers(0, 2);
	g_pilot_data.teams[2].is_mission_completed = 0;
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"outcome\":\"not_completed\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"goals_met_seconds\":305") != NULL);
	unlink(result);
	/* A local player or team outside the tables reads as zeros. */
	open_good(extra, path, sizeof(path));
	set_battle_numbers(0, 2);
	g_local_pilot_network_player_index = 8;
	g_pilot_data.team = 10;
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"outcome\":\"not_completed\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"score\":0") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"goals_met_seconds\":0") != NULL);
	unlink(result);
	open_good(extra, path, sizeof(path));
	set_battle_numbers(0, 2);
	g_local_pilot_network_player_index = -1;
	g_pilot_data.team = -1;
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"kills\":0") != NULL);
	unlink(result);
	unlink(path);
}

static void check_failed_flight(void)
{
	char path[256];
	char result[256];
	char text[2048];
	snprintf(result, sizeof(result), "%s/failed.json", g_folder);
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s\"", result);
	open_good(extra, path, sizeof(path));
	xvt_launch_door_finish(0);
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_port_get_exit_code(),
			  XVT_LAUNCH_EXIT_FLIGHT_FAILED);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"failed\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "did not start") != NULL);
	unlink(result);
	unlink(path);
}

/* Seeds one hostile starfighter flight group, "Beta", for the Sith promotion. */
static void seed_sith_group(void)
{
	memset(g_mission_flight_groups, 0,
	       2 * sizeof g_mission_flight_groups[0]);
	memset(g_mission_teams, 0, sizeof g_mission_teams);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	memset(g_players, 0, sizeof g_players[0]);
	g_craft_type_to_object_type[1] = 5;
	g_object_type_table[5].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_mission_flight_groups[1].fg.team = 1;
	g_mission_flight_groups[1].fg.group_ai = 4;
	g_mission_flight_groups[1].fg.craft_type = 1;
	snprintf(g_mission_flight_groups[1].fg.name, 20, "Beta");
	g_mission_header.num_flight_groups = 2;
}

/* A flown battle with the Lord setting: the result names the flight and its
 * fate, and the setting is none again afterward. */
static void check_sith_result_and_reset(void)
{
	char path[256];
	char result[256];
	char text[2048];
	snprintf(result, sizeof(result), "%s/sith.json", g_folder);
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s\"", result);
	open_good(extra, path, sizeof(path));
	set_battle_numbers(0, 2);
	seed_sith_group();
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	g_mission_fg_stats[1].spawned_craft_count = 3;
	xvt_sith_note_craft_destroyed(7);
	xvt_sith_note_craft_destroyed(6);
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"schema_version\":3") != NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"sith\":{\"setting\":\"lord\",\"flight_group\":1,\"flight_name\":\"Beta\",\"craft_type\":1,\"apprentices_flown\":2,\"apprentices_lost\":1,\"lord_flown\":1,\"lord_lost\":1,\"why\":\"\"}") !=
		NULL);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
	XVT_ASSERT_TRUE(
		strstr(g_read_line,
		       "launch.result_read local=0 team=2 score=120 kills=4 losses=1 goals_met_seconds=305 outcome=1 sith=\"lord\" sith_group=1 sith_craft=1 apprentices_flown=2 apprentices_lost=1 lord_flown=1 lord_lost=1 sith_why=\"\"") !=
		NULL);
	unlink(result);
	unlink(path);
}

/* A failed flight ends the setting too, and its result shows none. */
static void check_sith_reset_after_failed_flight(void)
{
	char path[256];
	char result[256];
	char text[2048];
	snprintf(result, sizeof(result), "%s/sithfail.json", g_folder);
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s\"", result);
	open_good(extra, path, sizeof(path));
	seed_sith_group();
	xvt_sith_set_setting(XVT_SITH_LORD);
	xvt_sith_promote_flight_groups();
	xvt_launch_door_finish(0);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
	XVT_ASSERT_INT_EQ(xvt_sith_lord_flight_group(), -1);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(
		strstr(text, "\"setting\":\"none\",\"flight_group\":-1") !=
		NULL);
	unlink(result);
	unlink(path);
}

static void check_default_result_path(void)
{
	char path[256];
	char result[256];
	char text[2048];
	open_good("", path, sizeof(path));
	snprintf(result, sizeof(result), "%s.result.json", path);
	set_battle_numbers(0, 0);
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"flown\"") != NULL);
	unlink(result);
	unlink(path);
}

static void check_unwritable_result(void)
{
	char path[256];
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s/no/such/dir/r.json\"",
		 g_folder);
	open_good(extra, path, sizeof(path));
	set_battle_numbers(0, 0);
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_port_get_exit_code(),
			  XVT_LAUNCH_EXIT_FLIGHT_FAILED);
	unlink(path);
}

/* The door ends by itself when the battle is not reached in 180 seconds. */
static void check_the_door_gives_up_after_180_seconds(void)
{
	char path[256];
	char result[256];
	char text[2048];
	snprintf(result, sizeof(result), "%s/slow.json", g_folder);
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s\"", result);
	open_good(extra, path, sizeof(path));
	xvt_time_reset();
	xvt_launch_door_step();
	xvt_time_advance_host_clock(179 * 1000000);
	xvt_launch_door_step();
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 1);
	xvt_time_advance_host_clock(2 * 1000000);
	xvt_launch_door_step();
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_port_get_exit_code(),
			  XVT_LAUNCH_EXIT_NOT_REACHED);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"not_reached\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "within 180 seconds") != NULL);
	unlink(result);
	/* A door opened later has its own 180 seconds, not the old one's. */
	open_good(extra, path, sizeof(path));
	xvt_launch_door_step();
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 1);
	xvt_launch_door_finish(0);
	unlink(result);
	unlink(path);
}

static void check_closed_door_does_nothing(void)
{
	char text[64];
	char result[256];
	snprintf(result, sizeof(result), "%s/closed.json", g_folder);
	xvt_launch_door_step();
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 0);
}

int main(void)
{
	snprintf(g_folder, sizeof(g_folder), "%s/xvt_launch_door_XXXXXX",
		 tmp_base());
	XVT_ASSERT_TRUE(mkdtemp(g_folder) != NULL);
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_missing_file();
	check_refusals_leave_a_result();
	check_flown_result();
	check_not_completed_and_out_of_range();
	check_failed_flight();
	check_sith_result_and_reset();
	check_sith_reset_after_failed_flight();
	check_default_result_path();
	check_unwritable_result();
	check_the_door_gives_up_after_180_seconds();
	check_closed_door_does_nothing();
	rmdir(g_folder);
	return 0;
}

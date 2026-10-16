/* Checks the --launch door (xvt_runtime/runtime/launch_door.h) against the
 * promises in its header that hold without a window, a pilot loaded from the
 * game's files or the game's mission lists: opening a description (a missing
 * file, text that is not a description, a pilot name holding a switch word, a
 * good one with and without a result path, an AI program that cannot be
 * started, a network host's description and a joiner's), the result file each
 * ending writes, with every player and team and the pilot's own tables, and
 * the exit code the door asks the program to end with. The test keeps its
 * description and result files in a temporary folder and sets the pilot
 * record, flight and mission numbers the result is read from, made up and
 * each one different.
 *
 * Not checked here: the steps on the concourse and the mission setup screen,
 * a network battle's setting up, a host's room and gathering, and a joiner's
 * looking, joining, asking and answer; the parts they decide by are checked in
 * test_launch_options.c and test_door_admission.c.
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
/* Every launch.result_player, _team and _detail line since it was emptied. */
static char g_stats_lines[8192];

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	if (strstr(message, "launch.result_read") != NULL) {
		snprintf(g_read_line, sizeof(g_read_line), "%s", message);
	}
	if (strstr(message, "launch.result_player") != NULL ||
	    strstr(message, "launch.result_team") != NULL ||
	    strstr(message, "launch.result_detail") != NULL) {
		size_t used = strlen(g_stats_lines);
		snprintf(g_stats_lines + used, sizeof(g_stats_lines) - used,
			 "%s\n", message);
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

/* Seeds a battle shaped like a 1997 internet game's duty roster: seven
 * players in the pilot record's entries 0 to 6 (entry 7 an empty seat), in
 * flight places that are not their entries (entry 3 in place 6), entry 5
 * with no place, and place 4 holding a player no entry names. The numbers
 * are made up, entry e's from 1000 * (e + 1) + 1 and place-held ones from
 * 100 * (e + 1) + 6, so none equals another. Flight groups: 0 Rogue and 1
 * Gold, players' and arrived, on team 0; 2 a computer craft of team 0; 3 a
 * computer craft of team 1; 4 a players' of team 3, arrived; 5 a players'
 * of team 2 that never arrived; 6 a players' of team 6 past the mission's 6
 * groups. Teams 5 and 8 met their goals; the game writes only 0 to 7. Row 0
 * of the pilot's tables holds the battle; row 1 holds numbers that must not
 * show. */
static void seed_roster_battle(void)
{
	static const char *const names[] = {
		"Kestrel", "vx_Moth",	   "Ardent", "newpilot",
		"Quillon", "Nightw1ng_42", "Tallow"};
	static const int slots[] = {0, 1, 2, 6, 3, -1, 5};
	static const int teams[] = {0, 0, 0, 0, 3, 0, 0};
	static const int groups[] = {0, 0, 0, 0, 4, 1, 1};
	static const int ratings[] = {14, 12, 13, 0, 9, 7, 6};
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	memset(g_players, 0, sizeof g_players);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	g_local_pilot_network_player_index = 5;
	g_pilot_data.team = 0;
	for (int e = 0; e < 7; ++e) {
		struct pilot_network_player *n =
			&g_pilot_data.network_players[e];
		int base = 1000 * (e + 1);
		snprintf(n->friendly_name, sizeof(n->friendly_name), "%s",
			 names[e]);
		n->direct_play_id = 11 + e;
		n->flight_group_id = 40 + e; /* the result reads the flight's */
		n->rating = ratings[e];
		n->total_score = base + 1;
		n->kills = base + 2;
		n->kills_shared = base + 3;
		n->kills_assist = base + 4;
		n->total_losses = base + 5;
		n->has_left = e == 6;
		if (slots[e] < 0) {
			continue;
		}
		struct player_data *f = &g_players[slots[e]];
		int held = 100 * (e + 1);
		f->network.direct_play_id = 11 + e;
		f->team = (int16_t)teams[e];
		f->bound_flight_group_idx = (uint16_t)groups[e];
		f->mission_stats.laser_shots_fired = (uint16_t)(held + 4);
		f->mission_stats.ion_shots_fired = 2;
		f->mission_stats.laser_hits_scored = (uint16_t)(held + 4);
		f->mission_stats.ion_hits_scored = 3;
		f->warheads_fired = (uint16_t)(held + 8);
		f->per_mission_kills.warhead_hits = (uint16_t)(held + 9);
	}
	g_players[4].network.direct_play_id = 99;
	static const struct {
		int team, player, arrived;
	} fgs[] = {{0, 1, 1}, {0, 5, 1}, {0, 0, 1}, {1, 0, 1},
		   {3, 3, 1}, {2, 2, 0}, {6, 1, 1}};
	for (int fg = 0; fg < 7; ++fg) {
		g_mission_flight_groups[fg].fg.team = (uint8_t)fgs[fg].team;
		g_mission_flight_groups[fg].fg.player_number =
			(uint8_t)fgs[fg].player;
		g_mission_fg_stats[fg].has_arrived = (uint8_t)fgs[fg].arrived;
	}
	g_mission_header.num_flight_groups = 6;
	for (int team = 0; team < 10; ++team) {
		struct pilot_team *s = &g_pilot_data.teams[team];
		s->mission_score = 2600 + team;
		s->mission_time = 640 + team;
		s->kills = 30 + team;
		s->kills_shared = 50 + team;
		s->losses = 70 + team;
	}
	g_pilot_data.teams[0].is_mission_completed = 1;
	g_pilot_data.teams[5].is_mission_completed = 1;
	g_pilot_data.teams[8].is_mission_completed = 1;
	struct pilot_stats *s = &g_pilot_data.last_mission_stats;
	s->energy_fired_per_mt[0] = 811;
	s->energy_hits_per_mt[0] = 812;
	s->warheads_fired_per_mt[0] = 813;
	s->warheads_hits_per_mt[0] = 814;
	s->total_craft_losses_per_mt[0] = 815;
	s->losses_by_collisions_per_mt[0] = 816;
	s->losses_by_starships_per_mt[0] = 817;
	s->losses_by_mines_per_mt[0] = 818;
	s->total_friendlies_killed_per_mt[0] = 819;
	s->num_special_inspected_per_mt[0] = 820;
	s->kills_per_craft_per_mt[0][3] = 21;
	s->kills_shared_per_craft_per_mt[0][3] = 22;
	s->kills_assists_per_craft_per_mt[0][99] = 23;
	s->kills_full_on_player_rating_per_mt[0][24] = 31;
	s->kills_shared_on_player_rating_per_mt[0][2] = 32;
	s->kills_assist_on_player_rating_per_mt[0][2] = 33;
	s->kills_full_on_ai_rating_per_mt[0][5] = 41;
	s->kills_shared_on_ai_rating_per_mt[0][0] = 42;
	s->kills_assist_on_ai_rating_per_mt[0][1] = 43;
	s->killed_by_player_rating_per_mt[0][12] = 51;
	s->killed_by_ai_rating_per_mt[0][4] = 52;
	s->energy_fired_per_mt[1] = 9001;
	s->kills_per_craft_per_mt[1][7] = 9002;
	s->killed_by_ai_rating_per_mt[2][3] = 9003;
	g_pilot_data.kills_full_on_player[3] = 61;
	g_pilot_data.kills_shared_on_player[7] = 62;
	g_pilot_data.kills_full_from_player[0] = 63;
	g_pilot_data.kills_shared_from_player[6] = 64;
	g_pilot_data.kills_full_on_flight_group[3] = 71;
	g_pilot_data.kills_shared_on_flight_group[47] = 72;
	g_pilot_data.kills_full_from_flight_group[0] = 73;
	g_pilot_data.kills_shared_from_flight_group[12] = 74;
}

/* Puts back what seed_roster_battle set that the other checks read. */
static void clear_roster_battle(void)
{
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	memset(g_players, 0, sizeof g_players);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	g_mission_header.num_flight_groups = 0;
}

/* A flown battle's result holds every player of the pilot record with the
 * flight's numbers of its place, the teams the game's end of battle counts,
 * and the local pilot's own tables from row 0; the door logs what it
 * read. */
static void check_every_player_and_team(void)
{
	char path[256];
	char result[256];
	static char text[16384];
	snprintf(result, sizeof(result), "%s/roster.json", g_folder);
	char extra[300];
	snprintf(extra, sizeof(extra), ",\"result\":\"%s\"", result);
	open_good(extra, path, sizeof(path));
	seed_roster_battle();
	g_stats_lines[0] = '\0';
	xvt_launch_door_finish(1);
	XVT_ASSERT_INT_EQ(slurp(result, text, sizeof(text)), 1);
	/* The local pilot is entry 5; version 3's members read it. */
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"score\":6001,\"kills\":6002,\"losses\":6005,\"goals_met_seconds\":640,") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text, "\"local_entry\":5,\"players\":[{\"entry\":0,") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":0,\"slot\":0,\"name\":\"Kestrel\",\"team\":0,\"flight_group\":0,\"rating\":14,\"score\":1001,\"kills\":1002,\"shared\":1003,\"assists\":1004,\"losses\":1005,\"shots\":106,\"hits\":107,\"warheads\":108,\"warhead_hits\":109,\"has_left\":false},") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":3,\"slot\":6,\"name\":\"newpilot\",\"team\":0,\"flight_group\":0,\"rating\":0,\"score\":4001,\"kills\":4002,\"shared\":4003,\"assists\":4004,\"losses\":4005,\"shots\":406,\"hits\":407,\"warheads\":408,\"warhead_hits\":409,\"has_left\":false},") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":4,\"slot\":3,\"name\":\"Quillon\",\"team\":3,\"flight_group\":4,\"rating\":9,\"score\":5001,\"kills\":5002,\"shared\":5003,\"assists\":5004,\"losses\":5005,\"shots\":506,\"hits\":507,\"warheads\":508,\"warhead_hits\":509,\"has_left\":false},") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":5,\"slot\":-1,\"name\":\"Nightw1ng_42\",\"team\":-1,\"flight_group\":0,\"rating\":7,\"score\":6001,\"kills\":6002,\"shared\":6003,\"assists\":6004,\"losses\":6005,\"shots\":0,\"hits\":0,\"warheads\":0,\"warhead_hits\":0,\"has_left\":false},") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":6,\"slot\":5,\"name\":\"Tallow\",\"team\":0,\"flight_group\":1,\"rating\":6,\"score\":7001,\"kills\":7002,\"shared\":7003,\"assists\":7004,\"losses\":7005,\"shots\":706,\"hits\":707,\"warheads\":708,\"warhead_hits\":709,\"has_left\":true}],\"teams\":[") !=
		NULL);
	/* Entries 0 to 6 in order; the empty seat and the unnamed place are not
	 * players. */
	const char *at = text;
	for (int e = 0; e < 7; ++e) {
		char expected[32];
		snprintf(expected, sizeof(expected),
			 "{\"entry\":%d,\"slot\":", e);
		at = strstr(at, expected);
		XVT_ASSERT_TRUE(at != NULL);
	}
	XVT_ASSERT_TRUE(strstr(text, "\"entry\":7,\"slot\":") == NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"slot\":4,") == NULL);
	/* Team 0 flew, team 3 flew, team 5 met its goals; team 1 had no
	 * players, team 2's never arrived, team 6's is past the mission's
	 * groups, team 8 is not one the game writes. */
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"teams\":[{\"team\":0,\"completed\":true,\"score\":2600,\"kills\":30,\"shared\":50,\"losses\":70,\"seconds\":640},{\"team\":3,\"completed\":false,\"score\":2603,\"kills\":33,\"shared\":53,\"losses\":73,\"seconds\":643},{\"team\":5,\"completed\":true,\"score\":2605,\"kills\":35,\"shared\":55,\"losses\":75,\"seconds\":645}],\"detail\":{") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"detail\":{\"shots\":811,\"hits\":812,\"warheads\":813,\"warhead_hits\":814,\"losses\":815,\"losses_to_collisions\":816,\"losses_to_starships\":817,\"losses_to_mines\":818,\"friendlies_killed\":819,\"special_inspected\":820,"
		       "\"by_craft\":[{\"craft_type\":3,\"kills\":21,\"shared\":22,\"assists\":0},{\"craft_type\":99,\"kills\":0,\"shared\":0,\"assists\":23}],"
		       "\"on_pilot_ranks\":[{\"rank\":2,\"kills\":0,\"shared\":32,\"assists\":33},{\"rank\":24,\"kills\":31,\"shared\":0,\"assists\":0}],"
		       "\"on_ai_ranks\":[{\"rank\":0,\"kills\":0,\"shared\":42,\"assists\":0},{\"rank\":1,\"kills\":0,\"shared\":0,\"assists\":43},{\"rank\":5,\"kills\":41,\"shared\":0,\"assists\":0}],"
		       "\"killed_by_pilot_ranks\":[{\"rank\":12,\"times\":51}],"
		       "\"killed_by_ai_ranks\":[{\"rank\":4,\"times\":52}],"
		       "\"on_players\":[{\"entry\":3,\"kills\":61,\"shared\":0},{\"entry\":7,\"kills\":0,\"shared\":62}],"
		       "\"from_players\":[{\"entry\":0,\"kills\":63,\"shared\":0},{\"entry\":6,\"kills\":0,\"shared\":64}],"
		       "\"on_flight_groups\":[{\"flight_group\":3,\"kills\":71,\"shared\":0},{\"flight_group\":47,\"kills\":0,\"shared\":72}],"
		       "\"from_flight_groups\":[{\"flight_group\":0,\"kills\":73,\"shared\":0},{\"flight_group\":12,\"kills\":0,\"shared\":74}]}}\n") !=
		NULL);
	XVT_ASSERT_TRUE(strstr(text, "900") == NULL);
	/* What the door read, line by line. */
	XVT_ASSERT_TRUE(
		strstr(g_stats_lines,
		       "launch.result_player entry=3 slot=6 name=\"newpilot\" team=0 fg=0 rating=0 score=4001 kills=4002 shared=4003 assists=4004 losses=4005 shots=406 hits=407 warheads=408 warhead_hits=409 has_left=0\n") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(g_stats_lines,
		       "launch.result_player entry=5 slot=-1 name=\"Nightw1ng_42\" team=-1 fg=0 rating=7 score=6001 ") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(g_stats_lines,
		       "launch.result_team team=3 completed=0 score=2603 kills=33 shared=53 losses=73 seconds=643\n") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(g_stats_lines,
		       "launch.result_detail shots=811 hits=812 warheads=813 warhead_hits=814 losses=815 kills=21 shared=22 assists=23 player_kills=31 player_shared=32 ai_kills=41 ai_shared=42 lost_to_players=51 lost_to_ai=52\n") !=
		NULL);
	unlink(result);
	unlink(path);
	clear_roster_battle();
}

/* A network description opens for either role, the host's and a joiner's. */
static void check_network_descriptions_open(void)
{
	static const char *const roles[][2] = {{"host", "Ace"},
					       {"join", "Rook"}};
	for (size_t index = 0; index < sizeof(roles) / sizeof(roles[0]);
	     ++index) {
		char path[256];
		char text[512];
		snprintf(text, sizeof(text),
			 "{\"schema_version\":1,\"mode\":\"network\","
			 "\"game_type\":\"melee\",\"side\":\"none\","
			 "\"mission\":\"melee/m1.tie\",\"pilot\":\"%s\","
			 "\"seed\":7,\"network\":{\"role\":\"%s\","
			 "\"lobby\":\"https://h\","
			 "\"game\":\"0123abcd-4567-89ef-0123-456789abcdef\","
			 "\"players\":[\"Ace\",\"Rook\"]}}",
			 roles[index][1], roles[index][0]);
		put("network.json", text, path, sizeof(path));
		XVT_ASSERT_INT_EQ(xvt_launch_door_open(path, TEST_RUN_ID, ""),
				  1);
		XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 1);
		/* It ends as any door does. */
		xvt_launch_door_finish(0);
		XVT_ASSERT_INT_EQ(xvt_launch_door_is_active(), 0);
		XVT_ASSERT_INT_EQ(xvt_port_get_exit_code(),
				  XVT_LAUNCH_EXIT_FLIGHT_FAILED);
		char result[256];
		snprintf(result, sizeof(result), "%s.result.json", path);
		unlink(result);
		unlink(path);
	}
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
	XVT_ASSERT_TRUE(strstr(text, "\"local_entry\":-1,") != NULL);
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
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"local_entry\":-1,\"players\":[],\"teams\":[],") !=
		NULL);
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
	XVT_ASSERT_TRUE(strstr(text, "\"schema_version\":4") != NULL);
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
	check_every_player_and_team();
	check_network_descriptions_open();
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

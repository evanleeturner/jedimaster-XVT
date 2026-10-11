/* Checks the start-up recording (xvt_runtime/runtime/launch_record.h) against
 * the promises in its header: it is off until a path is set, writes nothing
 * when off, and when on writes the pilot record bytes as they are to
 * "<path>.pilot" and one "name size hex" line for each value of the state file.
 * The test sets made-up values in the game's globals (settings, roster, pilot
 * record, session) and reads both files back from a temporary folder. No game
 * data is read. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/launch_record.h"

/* The folder for temporary folders: $TMPDIR, else the working folder. */
static const char *tmp_base(void)
{
	const char *base = getenv("TMPDIR");
	return base && base[0] ? base : ".";
}

static char g_folder[64];

/* Reads a whole file into a malloc'd buffer; returns its length in *size. */
static char *slurp(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	XVT_ASSERT_TRUE(file != NULL);
	fseek(file, 0, SEEK_END);
	*size = (size_t)ftell(file);
	fseek(file, 0, SEEK_SET);
	char *data = malloc(*size + 1);
	XVT_ASSERT_TRUE(data != NULL);
	XVT_ASSERT_INT_EQ(fread(data, 1, *size, file), *size);
	data[*size] = '\0';
	fclose(file);
	return data;
}

/* Returns the line of text that starts with "name ", or NULL. */
static const char *line_of(const char *text, const char *name)
{
	size_t length = strlen(name);
	for (const char *at = text; at && *at;
	     at = strchr(at, '\n') ? strchr(at, '\n') + 1 : NULL) {
		if (!strncmp(at, name, length) && at[length] == ' ') {
			return at;
		}
	}
	return NULL;
}

static void set_state(void)
{
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	memset(&g_game_config, 0, sizeof(g_game_config));
	memset(g_mp_roster, 0, sizeof(g_mp_roster));
	memset(g_players, 0, sizeof(g_players));
	strcpy(g_pilot_data.name, "Ace");
	g_pilot_data.total_score = 77;
	g_game_config.difficulty = GAME_DIFFICULTY_HARD;
	g_game_config.random_seed = 0x01020304u;
	strcpy(g_game_config.last_pilot_name, "Ace");
	g_mp_roster[0].player_id = 1;
	strcpy(g_mp_roster[0].name, "Ace");
	g_mp_roster[0].craft_type_override = 6;
	g_mp_roster_ready_flags[2] = 1;
	g_players[3].participation_state = 2;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	g_mission_setup_is_host = 1;
	g_frontend_launch_human_player_count = 1;
	g_local_pilot_network_player_index = 0;
}

static void check_off_until_a_path_is_set(void)
{
	xvt_launch_record_set_path(NULL);
	XVT_ASSERT_INT_EQ(xvt_launch_record_is_enabled(), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_record_write("line"), 0);
	xvt_launch_record_set_path("");
	XVT_ASSERT_INT_EQ(xvt_launch_record_is_enabled(), 0);
	xvt_launch_record_set_path("x");
	XVT_ASSERT_INT_EQ(xvt_launch_record_is_enabled(), 1);
	XVT_ASSERT_INT_EQ(xvt_launch_record_write(NULL), 0);
	char too_long[600];
	memset(too_long, 'a', sizeof(too_long));
	too_long[sizeof(too_long) - 1] = '\0';
	xvt_launch_record_set_path(too_long);
	XVT_ASSERT_INT_EQ(xvt_launch_record_is_enabled(), 0);
}

static void check_written_files(void)
{
	char path[128];
	char pilot_path[140];
	snprintf(path, sizeof(path), "%s/rec", g_folder);
	snprintf(pilot_path, sizeof(pilot_path), "%s.pilot", path);
	set_state();
	xvt_launch_record_set_path(path);
	XVT_ASSERT_INT_EQ(xvt_launch_record_write("~train\\a.tie~ 1"), 1);
	size_t size;
	char *pilot = slurp(pilot_path, &size);
	XVT_ASSERT_INT_EQ(size, sizeof(g_pilot_data));
	XVT_ASSERT_INT_EQ(memcmp(pilot, &g_pilot_data, size), 0);
	free(pilot);
	char *state = slurp(path, &size);
	/* "~train\a.tie~ 1" in hex, 15 bytes */
	const char *launch_line =
		"launch_line 15 7e747261696e5c612e7469657e2031\n";
	XVT_ASSERT_TRUE(strstr(state, launch_line) != NULL);
	XVT_ASSERT_TRUE(strstr(state, "session_mode 4 02000000\n") != NULL);
	XVT_ASSERT_TRUE(strstr(state, "is_host 4 01000000\n") != NULL);
	XVT_ASSERT_TRUE(strstr(state, "human_player_count 4 01000000\n") !=
			NULL);
	XVT_ASSERT_TRUE(strstr(state, "local_player_index 4 00000000\n") !=
			NULL);
	XVT_ASSERT_TRUE(strstr(state, "game_config.difficulty 1 02\n") != NULL);
	XVT_ASSERT_TRUE(strstr(state, "game_config.random_seed 4 04030201\n") !=
			NULL);
	XVT_ASSERT_TRUE(
		strstr(state, "game_config.last_pilot_name 13 41636500") !=
		NULL);
	XVT_ASSERT_TRUE(strstr(state, "roster[0].player_id 4 01000000\n") !=
			NULL);
	XVT_ASSERT_TRUE(
		strstr(state, "roster[0].craft_type_override 4 06000000\n") !=
		NULL);
	XVT_ASSERT_TRUE(strstr(state, "roster_ready[2] 4 01000000\n") != NULL);
	XVT_ASSERT_TRUE(
		strstr(state, "players[3].participation_state 1 02\n") != NULL);
	XVT_ASSERT_TRUE(
		strstr(state, "players[0].participation_state 1 00\n") != NULL);
	XVT_ASSERT_TRUE(line_of(state, "team_player_ids") != NULL);
	XVT_ASSERT_TRUE(line_of(state, "assigned_player_ids") != NULL);
	/* Every member of the settings is on its own line: 59 members. */
	int settings_lines = 0;
	for (const char *at = state;
	     (at = strstr(at, "\ngame_config.")) != NULL; ++at) {
		++settings_lines;
	}
	XVT_ASSERT_INT_EQ(settings_lines, 59);
	XVT_ASSERT_TRUE(line_of(state, "game_config.backdrop") != NULL);
	free(state);
	/* A second recording replaces the first. */
	g_pilot_data.total_score = 78;
	XVT_ASSERT_INT_EQ(xvt_launch_record_write("x"), 1);
	pilot = slurp(pilot_path, &size);
	XVT_ASSERT_INT_EQ(memcmp(pilot, &g_pilot_data, size), 0);
	free(pilot);
	unlink(path);
	unlink(pilot_path);
}

/* A file that cannot be closed cleanly makes the recording fail: the full
 * disk device takes every write and refuses it when the file closes. */
static void check_full_disk(void)
{
	char path[128];
	char pilot_path[140];
	snprintf(path, sizeof(path), "%s/full", g_folder);
	snprintf(pilot_path, sizeof(pilot_path), "%s.pilot", path);
	set_state();
	xvt_launch_record_set_path(path);
	XVT_ASSERT_INT_EQ(symlink("/dev/full", pilot_path), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_record_write("line"), 0);
	unlink(pilot_path);
	unlink(path);
	XVT_ASSERT_INT_EQ(symlink("/dev/full", path), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_record_write("line"), 0);
	unlink(path);
	unlink(pilot_path);
}

static void check_unwritable_path(void)
{
	char path[160];
	snprintf(path, sizeof(path), "%s/no/such/rec", g_folder);
	xvt_launch_record_set_path(path);
	XVT_ASSERT_INT_EQ(xvt_launch_record_write("line"), 0);
}

int main(void)
{
	snprintf(g_folder, sizeof(g_folder), "%s/xvt_launch_record_XXXXXX",
		 tmp_base());
	XVT_ASSERT_TRUE(mkdtemp(g_folder) != NULL);
	check_off_until_a_path_is_set();
	check_written_files();
	check_full_disk();
	check_unwritable_path();
	xvt_launch_record_set_path(NULL);
	rmdir(g_folder);
	return 0;
}

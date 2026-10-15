#include "xvt_runtime/runtime/launch_door.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "xvt/flight/ai_brain.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/ai/ai_program.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_cleanup.h"
#include "xvt_runtime/runtime/launch_description.h"
#include "xvt_runtime/runtime/launch_result.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/runtime/sith.h"
#include "xvt_runtime/timing/host_clock.h"

enum {
	DESCRIPTION_CAPACITY = 65536,
	RESULT_SUFFIX_CAPACITY = 16,
	/* The time allowed to get from start-up to the battle. */
	REACH_LIMIT_US = 180 * 1000000
};

enum {
	PHASE_CLOSED,
	PHASE_WAIT_CONCOURSE,
	PHASE_WAIT_SETUP,
	PHASE_LAUNCHING,
	PHASE_FINISHED
};

static struct xvt_launch_description g_description;
static char g_result_path[XVT_LAUNCH_PATH_CAPACITY + RESULT_SUFFIX_CAPACITY];
static int g_phase = PHASE_CLOSED;
static uint64_t g_started_us;
static int g_timer_started;

/* Writes the result file for a refusal or a failure to start. */
static void write_failure(int exit_code, const char *why)
{
	struct xvt_launch_result result;
	memset(&result, 0, sizeof(result));
	result.exit_code = exit_code;
	snprintf(result.why, sizeof(result.why), "%s", why);
	if (!xvt_launch_result_write(&result, g_result_path)) {
		XVT_LOG_ERROR("launch.result_unwritten path=\"%s\"",
			      g_result_path);
		return;
	}
	XVT_LOG_INFO("launch.result_written path=\"%s\" exit=%d", g_result_path,
		     exit_code);
}

/* Ends the door with a failure: writes the result file, asks the program to
 * quit with exit_code and closes the door. */
static void fail(int exit_code, const char *why)
{
	XVT_LOG_INFO("launch.refused code=%d why=\"%s\"", exit_code, why);
	write_failure(exit_code, why);
	g_phase = PHASE_FINISHED;
	xvt_port_request_exit(exit_code);
}

/* Reads the whole file at path into text, which holds zeros and keeps its
 * last byte so the text ends with one (at most size - 1 bytes are read).
 * Returns 1, or 0 when it cannot be opened, read, or is too long. */
static int read_file(const char *path, char *text, size_t size)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return 0;
	}
	(void)fread(text, 1, size - 1, file);
	int ended = feof(file) && !ferror(file);
	fclose(file);
	return ended;
}

/* Ends the flight when the AI program fails: no computer craft would decide
 * anything again, and a machine that flew on without it would part from the
 * others in a network game. */
static void end_on_ai_failure(const char *why)
{
	fail(XVT_LAUNCH_EXIT_FLIGHT_FAILED, why);
}

/* Starts the description's AI program and asks it about computer craft from
 * now on. Its log names run_id, keeps the engine's level and goes to
 * ai_log_file ("" for standard error only). Returns 1, or 0 after writing the
 * refusal. */
static int start_ai_program(uint32_t run_id, const char *ai_log_file)
{
	const char *level = xvt_log_level_name(xvt_log_level());
	struct ai_program_command command;
	ai_program_fill_command(&command, g_description.ai_python,
				g_description.ai_path, run_id, level,
				ai_log_file);
	XVT_LOG_INFO(
		"launch.ai_program path=\"%s\" run=%08x level=\"%s\" log_file=\"%s\"",
		g_description.ai_path, (unsigned)run_id, level, ai_log_file);
	char why[XVT_LAUNCH_WHY_CAPACITY];
	if (!ai_program_start(command.args, AI_PROGRAM_ANSWER_LIMIT_MS,
			      end_on_ai_failure, why, sizeof(why))) {
		XVT_LOG_INFO("launch.refused code=%d why=\"%s\"",
			     XVT_LAUNCH_EXIT_REFUSED, why);
		write_failure(XVT_LAUNCH_EXIT_REFUSED, why);
		return 0;
	}
	ai_brain_set_exchange(ai_program_exchange);
	return 1;
}

int xvt_launch_door_open(const char *path, uint32_t run_id,
			 const char *ai_log_file)
{
	XVT_LOG_INFO("launch.opened path=\"%s\"", path);
	snprintf(g_result_path, sizeof(g_result_path), "%s.result.json", path);
	char *text = calloc(1, DESCRIPTION_CAPACITY);
	char why[XVT_LAUNCH_WHY_CAPACITY];
	if (!text) {
		write_failure(
			XVT_LAUNCH_EXIT_REFUSED,
			"There is not enough memory to read the description.");
		return 0;
	}
	int read = read_file(path, text, DESCRIPTION_CAPACITY);
	int parsed = 0;
	if (!read) {
		snprintf(
			why, sizeof(why),
			"The description file could not be read, or is over %d bytes.",
			DESCRIPTION_CAPACITY - 1);
	} else {
		parsed = xvt_launch_description_parse(text, &g_description, why,
						      sizeof(why));
	}
	free(text);
	if (parsed && g_description.result_path[0]) {
		snprintf(g_result_path, sizeof(g_result_path), "%s",
			 g_description.result_path);
	}
	if (!parsed) {
		XVT_LOG_INFO("launch.refused code=%d why=\"%s\"",
			     XVT_LAUNCH_EXIT_REFUSED, why);
		write_failure(XVT_LAUNCH_EXIT_REFUSED, why);
		return 0;
	}
	XVT_LOG_INFO(
		"launch.described directory=%d side=%d mission=\"%s\" pilot=\"%s\"",
		g_description.directory_id, g_description.side,
		g_description.mission_file, g_description.pilot);
	XVT_LOG_DEBUG(
		"launch.settings difficulty=%d collisions=%d craft_jumping=%d random_setup=%d battle_length=%d time_limit=%d continue=%d sith=%d seed=%u has_seed=%d",
		g_description.settings.difficulty,
		g_description.settings.collisions,
		g_description.settings.craft_jumping,
		g_description.settings.random_setup,
		g_description.settings.battle_length,
		g_description.settings.mission_time_limit,
		g_description.settings.continue_sequence,
		g_description.settings.sith, g_description.seed,
		g_description.has_seed);
	if (g_description.ai_python[0] &&
	    !start_ai_program(run_id, ai_log_file)) {
		return 0;
	}
	g_phase = PHASE_WAIT_CONCOURSE;
	g_timer_started = 0;
	return 1;
}

int xvt_launch_door_is_active(void)
{
	return g_phase != PHASE_CLOSED && g_phase != PHASE_FINISHED;
}

/* 1 when the running screen is update_fn and has run past its first frame. */
static int screen_is(frontend_screen_update_fn update_fn)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
			       .update_fn == update_fn &&
	       g_front_state.frame_counter >= 1 && !xvt_dialog_is_active();
}

/* Switches the screen as a screen's own update does when a button is pressed:
 * the callbacks change, then the screen being left runs its exit function with
 * the frame counter the switch leaves (-1). A switch made inside a screen's
 * update is followed by the frame loop's step to frame 0; the door switches
 * between frames, so it makes that step itself and the new screen's first
 * frame is frame 0. The frame loop is not in a screen slice here, so the
 * switch's pending exit call is cleared, as the launch task does when it
 * completes. */
static void switch_screen(frontend_screen_update_fn update_fn,
			  frontend_screen_exit_fn exit_fn,
			  frontend_screen_exit_fn leaving_exit_fn)
{
	frontend_screen_set_callbacks(update_fn, exit_fn);
	g_front_state.screen_callbacks_dirty = 0;
	leaving_exit_fn(g_front_state.frame_counter);
	g_front_state.frame_counter = 0;
}

/* Copies the saved choices of faction into the pilot's current ones, as the
 * pilot list and the faction buttons do. */
static void restore_faction_choices(int faction)
{
	g_pilot_data.team = g_pilot_data.faction_statistics[faction].team;
	g_pilot_data.mission_directory_id =
		g_pilot_data.faction_statistics[faction].mission_directory_id;
	memcpy(g_pilot_data.mission_description_ids,
	       g_pilot_data.faction_statistics[faction].mission_description_ids,
	       sizeof(g_pilot_data.mission_description_ids));
	g_pilot_data.mission_sequence_active =
		g_pilot_data.faction_statistics[faction]
			.mission_sequence_active;
	g_pilot_data.saved_mission_description_id =
		g_pilot_data.faction_statistics[faction]
			.saved_mission_description_id;
}

/* The concourse step: loads the pilot named, picks the side as a click on its
 * button does, and presses Fly Solo as the button does. */
static void open_solo_setup(void)
{
	char why[XVT_LAUNCH_WHY_CAPACITY];
	if (strcasecmp(g_pilot_data.name, g_description.pilot) != 0 &&
	    pilot_find_and_load_by_name(g_description.pilot)) {
		restore_faction_choices(g_pilot_data.current_faction_id);
		g_pilot_record_pages_need_rebuild = 1;
	}
	if (strcasecmp(g_pilot_data.name, g_description.pilot) != 0) {
		fail(XVT_LAUNCH_EXIT_REFUSED,
		     "The pilot named in the description is not among the game's pilots.");
		return;
	}
	if (!xvt_launch_text_check(g_pilot_data.multiplayer_game_name, why,
				   sizeof(why)) ||
	    !xvt_launch_text_check(
		    g_pilot_data
			    .network_players[g_local_pilot_network_player_index]
			    .formal_name,
		    why, sizeof(why))) {
		fail(XVT_LAUNCH_EXIT_REFUSED, why);
		return;
	}
	if (g_description.side != XVT_LAUNCH_SIDE_NONE &&
	    g_pilot_data.current_faction_id != g_description.side) {
		g_pilot_record_pages_need_rebuild = 1;
		g_pilot_data.current_faction_id = g_description.side;
		restore_faction_choices(g_description.side);
		XVT_LOG_DEBUG(
			"launch.faction_chosen faction=%d team=%d directory=%d",
			g_pilot_data.current_faction_id, g_pilot_data.team,
			(int)g_pilot_data.mission_directory_id);
	}
	g_mission_setup_is_host = 0;
	g_mission_setup_roster_authoritative = 0;
	net_shutdown_direct_play_session();
	memset(g_mp_roster, 0, sizeof(g_mp_roster));
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	switch_screen(mission_setup_update, mission_setup_exit, concourse_exit);
	g_phase = PHASE_WAIT_SETUP;
	XVT_LOG_INFO("launch.solo_opened faction=%d",
		     g_pilot_data.current_faction_id);
}

/* Sets the settings the description gives. */
static void apply_settings(void)
{
	const struct xvt_launch_settings *set = &g_description.settings;
	if (set->difficulty != XVT_LAUNCH_SETTING_UNSET) {
		g_game_config.difficulty = (game_difficulty)set->difficulty;
	}
	if (set->collisions != XVT_LAUNCH_SETTING_UNSET) {
		g_game_config.collisions = (uint8_t)set->collisions;
	}
	if (set->craft_jumping != XVT_LAUNCH_SETTING_UNSET) {
		g_game_config.craft_jumping = (uint8_t)set->craft_jumping;
	}
	if (set->random_setup != XVT_LAUNCH_SETTING_UNSET) {
		g_game_config.random_setup = (uint8_t)set->random_setup;
	}
	if (set->battle_length != XVT_LAUNCH_SETTING_UNSET) {
		g_game_config.battle_length_index =
			(battle_length)set->battle_length;
	}
	if (set->mission_time_limit != XVT_LAUNCH_SETTING_UNSET) {
		g_game_config.mission_time_limit =
			(uint8_t)set->mission_time_limit;
	}
	if (set->continue_sequence != XVT_LAUNCH_SETTING_UNSET) {
		g_game_config.continue_battle_or_campaign =
			(sequence_continuation_choice)set->continue_sequence;
	}
	xvt_sith_set_from_description(set->sith);
}

/* Picks the described mission as a click on its mission type button and on
 * its list entry does. Returns 1, or 0 after failing when the folder's list
 * does not hold the mission open to this pilot. */
static int choose_mission(void)
{
	int directory = g_description.directory_id;
	mission_setup_load_mission_list(directory);
	unsigned int index;
	for (index = 0; g_mission_list && index < g_mission_count; ++index) {
		if (!strcasecmp(g_mission_list[index].file_name,
				g_description.mission_file)) {
			break;
		}
	}
	if (!g_mission_list || index == g_mission_count) {
		fail(XVT_LAUNCH_EXIT_REFUSED,
		     "The mission is not in the game's list for this game type, or is not open to this pilot.");
		return 0;
	}
	if (g_pilot_data.mission_directory_id != directory) {
		g_pilot_data.mission_directory_id = directory;
		g_game_config.continue_battle_or_campaign = SEQUENCE_CONTINUE;
		if (g_game_config.difficulty == GAME_DIFFICULTY_EASY_CHEAT &&
		    directory != MISSION_DIRECTORY_CAMPAIGNS) {
			g_game_config.difficulty = GAME_DIFFICULTY_EASY;
		}
		if (g_game_config.random_setup == 2 &&
		    directory != MISSION_DIRECTORY_BATTLES) {
			g_game_config.random_setup = 0;
		}
		if (directory == MISSION_DIRECTORY_CAMPAIGNS) {
			g_game_config.random_setup = 0;
		}
	}
	g_selected_mission_list_index = (int)index;
	g_pilot_data.mission_description_ids[directory] =
		g_mission_list[index].mission_idx;
	XVT_LOG_DEBUG("launch.mission_chosen directory=%d mission=%d index=%d",
		      directory, g_mission_list[index].mission_idx, (int)index);
	frontend_mission_load_current();
	return 1;
}

/* The mission setup step: sets the settings, picks the mission and presses
 * Quick Start as the button does. */
static void start_battle(void)
{
	apply_settings();
	if (g_description.has_seed) {
		g_game_config.random_seed = g_description.seed;
	}
	if (!choose_mission()) {
		return;
	}
	int directory = g_description.directory_id;
	if ((directory == MISSION_DIRECTORY_TOURNAMENTS ||
	     directory == MISSION_DIRECTORY_BATTLES ||
	     directory == MISSION_DIRECTORY_CAMPAIGNS) &&
	    mission_setup_select_first_sequence_mission() == 0) {
		fail(XVT_LAUNCH_EXIT_REFUSED,
		     "The sequence file could not be read.");
		return;
	}
	g_frontend_quick_start_launch_flag = 1;
	switch_screen(mission_setup_team_assignment_update,
		      xvt_frontend_cleanup_mission_resources,
		      mission_setup_exit);
	g_phase = PHASE_LAUNCHING;
	XVT_LOG_INFO("launch.started directory=%d mission=\"%s\"", directory,
		     g_description.mission_file);
}

void xvt_launch_door_step(void)
{
	if (!xvt_launch_door_is_active()) {
		return;
	}
	if (!g_timer_started) {
		g_timer_started = 1;
		g_started_us = xvt_time_get_elapsed_us();
	}
	if (xvt_time_get_elapsed_us() - g_started_us > REACH_LIMIT_US) {
		fail(XVT_LAUNCH_EXIT_NOT_REACHED,
		     "The battle did not start within 180 seconds.");
		return;
	}
	if (g_phase == PHASE_WAIT_CONCOURSE && screen_is(concourse_update)) {
		open_solo_setup();
	} else if (g_phase == PHASE_WAIT_SETUP &&
		   screen_is(mission_setup_update)) {
		start_battle();
	}
}

void xvt_launch_door_finish(int flown)
{
	if (!xvt_launch_door_is_active()) {
		return;
	}
	if (!flown) {
		fail(XVT_LAUNCH_EXIT_FLIGHT_FAILED,
		     "The flight did not start or ended in an error.");
		return;
	}
	struct xvt_launch_result result;
	memset(&result, 0, sizeof(result));
	int local = g_local_pilot_network_player_index;
	int team = g_pilot_data.team;
	if (local >= 0 && local < 8) {
		result.score = g_pilot_data.network_players[local].total_score;
		result.kills = g_pilot_data.network_players[local].kills;
		result.losses =
			g_pilot_data.network_players[local].total_losses;
	}
	result.outcome = XVT_LAUNCH_OUTCOME_NOT_COMPLETED;
	if (team >= 0 && team < 10) {
		result.goals_met_seconds =
			g_pilot_data.teams[team].mission_time;
		if (g_pilot_data.teams[team].is_mission_completed) {
			result.outcome = XVT_LAUNCH_OUTCOME_COMPLETED;
		}
	}
	/* The zeroed exit code is XVT_LAUNCH_EXIT_FLOWN. */
	XVT_LOG_DEBUG(
		"launch.result_read local=%d team=%d score=%d kills=%d losses=%d goals_met_seconds=%d outcome=%d",
		local, team, result.score, result.kills, result.losses,
		result.goals_met_seconds, result.outcome);
	if (!xvt_launch_result_write(&result, g_result_path)) {
		XVT_LOG_ERROR("launch.result_unwritten path=\"%s\"",
			      g_result_path);
		g_phase = PHASE_FINISHED;
		xvt_port_request_exit(XVT_LAUNCH_EXIT_FLIGHT_FAILED);
		return;
	}
	XVT_LOG_INFO("launch.result_written path=\"%s\" exit=%d", g_result_path,
		     result.exit_code);
	g_phase = PHASE_FINISHED;
	xvt_port_request_exit(XVT_LAUNCH_EXIT_FLOWN);
}

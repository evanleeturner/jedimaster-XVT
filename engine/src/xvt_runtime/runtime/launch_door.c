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
#include "xvt/net/net_send.h"
#include "xvt_runtime/ai/ai_program.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/door_admission.h"
#include "xvt_runtime/runtime/door_battle.h"
#include "xvt_runtime/runtime/frontend_cleanup.h"
#include "xvt_runtime/runtime/launch_description.h"
#include "xvt_runtime/runtime/launch_options.h"
#include "xvt_runtime/runtime/launch_result.h"
#include "xvt_runtime/runtime/launch_stats.h"
#include "xvt_runtime/runtime/network_metadata.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/runtime/sith.h"
#include "xvt_runtime/timing/host_clock.h"

enum {
	DESCRIPTION_CAPACITY = 65536,
	RESULT_SUFFIX_CAPACITY = 16,
	/* The time allowed to get from start-up to the battle. */
	REACH_LIMIT_US = 180 * 1000000,
	/* Packets the door reads per step while a host gathers its seats or a
	 * joiner waits for its answer. */
	PACKETS_PER_STEP = 32,
	/* How often a joiner asks the matchmaker for its rooms while the
	 * battle's is not listed; the browser screen asks every ten seconds. */
	ROOM_REFRESH_US = 2 * 1000000,
	/* The pilot record's entry for the combat simulator, which a network
	 * game's mission setup screen reads and saves. */
	COMBAT_SIM_FACTION = 2
};

enum {
	PHASE_CLOSED,
	PHASE_WAIT_CONCOURSE,
	PHASE_WAIT_SETUP,
	PHASE_OPEN_ROOM,
	PHASE_GATHER,
	PHASE_FIND_ROOM,
	PHASE_JOINING,
	PHASE_ASKED,
	PHASE_SEATED,
	PHASE_STARTING,
	PHASE_LAUNCHING,
	PHASE_FINISHED
};

static struct xvt_launch_description g_description;
/* A network host's seats and the player id that holds each (0 while free). */
static struct xvt_door_seats g_seats;
static DPID g_seat_players[XVT_LAUNCH_SEAT_CAPACITY];
/* The battle's seats as the host started it, and a joiner's seat. */
static struct xvt_door_start g_start;
static int g_joiner_seat;
/* A network joiner's copy of the matchmaker's rooms, whether a refresh of it
 * is under way, when the next one may start, and the last refresh's error. */
static AeronDplayDirectorySnapshot g_rooms;
static int g_rooms_pending;
static uint64_t g_next_refresh_us;
static AeronDplayDirectoryError g_rooms_error;
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
	result.local_entry = -1;
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
	xvt_sith_end_flight();
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
	if (g_description.is_network) {
		XVT_LOG_INFO(
			"launch.network_described role=%d seats=%d seat=%d setup=%08x",
			g_description.network.role,
			g_description.network.player_count,
			g_description.network.seat,
			(unsigned)xvt_launch_setup_fingerprint(&g_description));
	}
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

/* Loads the pilot named, as the pilot list does, and checks the names the
 * launch line will carry. Returns 1, or 0 after failing. */
static int load_pilot(void)
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
		return 0;
	}
	if (!xvt_launch_text_check(g_pilot_data.multiplayer_game_name, why,
				   sizeof(why)) ||
	    !xvt_launch_text_check(
		    g_pilot_data
			    .network_players[g_local_pilot_network_player_index]
			    .formal_name,
		    why, sizeof(why))) {
		fail(XVT_LAUNCH_EXIT_REFUSED, why);
		return 0;
	}
	return 1;
}

/* The concourse step: loads the pilot named, picks the side as a click on its
 * button does, and presses Fly Solo as the button does. */
static void open_solo_setup(void)
{
	if (!load_pilot()) {
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
	xvt_launch_options_apply_settings(&g_game_config,
					  &g_description.settings);
	xvt_sith_set_from_description(g_description.settings.sith);
}

/* Picks the described mission as a click on its mission type button and on
 * its list entry does, for any mission the list holds: one the pilot has not
 * opened flies too, which the 1997 list screens did not offer. A network
 * battle's options went through the type's rules already, alike on every
 * machine. Returns 1, or 0 after failing when the folder's list does not
 * hold the mission. */
static int choose_mission(void)
{
	int directory = g_description.directory_id;
	int network = g_description.is_network;
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
		     "The mission is not in the game's list for this game type.");
		return 0;
	}
	if (g_pilot_data.mission_directory_id != directory) {
		g_pilot_data.mission_directory_id = directory;
		if (!network) {
			xvt_launch_options_type_rules(&g_game_config,
						      directory);
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

/* Refuses, as the 1997 host's Begin did, a battle with more seats than the
 * chosen mission takes: the digit its file name starts with. Runs on every
 * machine, before any network work. Returns 1, or 0 after failing. */
static int mission_takes_seats(void)
{
	int seats = g_description.network.player_count;
	int most = (int)(uint8_t)g_mission_list[g_selected_mission_list_index]
			   .file_name[0] -
		   '0';
	if (most >= seats) {
		return 1;
	}
	char why[XVT_LAUNCH_WHY_CAPACITY];
	snprintf(
		why, sizeof(why),
		"The mission takes at most %d players, and the battle has %d seats.",
		most < 0 ? 0 : most, seats);
	fail(XVT_LAUNCH_EXIT_REFUSED, why);
	return 0;
}

/* Sets up a network battle as far as each machine can alone, without the
 * mission setup screen the 1997 lobby ran: the session mode, the combat
 * simulator's record as the screen's first frame reads it in a network game,
 * the options every machine computes alike (launch_options.h), the Sith setting
 * and the mission. Runs before any network work, so a battle this machine
 * cannot fly is refused before a room opens or a seat is taken. Returns 1, or
 * 0 after failing. */
static int set_up_network_battle(void)
{
	int host = g_description.network.role == XVT_LAUNCH_ROLE_HOST;
	g_frontend_mission_session_mode =
		host ? FRONTEND_MISSION_SESSION_NET_HOST
		     : FRONTEND_MISSION_SESSION_NET_CLIENT;
	g_mission_setup_is_host = host;
	restore_faction_choices(COMBAT_SIM_FACTION);
	g_pilot_data.faction_statistics[COMBAT_SIM_FACTION]
		.mission_sequence_active = 0;
	g_mission_setup_use_combat_sim_pilot_state = 1;
	static struct game_config shipped;
	shipped = g_game_config;
	char error[XVT_LAUNCH_WHY_CAPACITY];
	if (!xvt_config_apply_shipped(&shipped, error, sizeof(error))) {
		XVT_LOG_ERROR("launch.shipped_unread error=\"%s\"", error);
		fail(XVT_LAUNCH_EXIT_REFUSED,
		     "The game's shipped settings could not be read, so the battle's settings are unknown.");
		return 0;
	}
	xvt_launch_options_network(&g_game_config, &shipped, &g_description);
	xvt_sith_set_from_description(g_description.settings.sith);
	XVT_LOG_DEBUG(
		"launch.network_options difficulty=%u collisions=%u jumping=%u random=%u length=%u join=%u craft=%u locate=%u waves=%u time_limit=%u team_limit=%u seed=%u internet=%u ai=%u rate=%u balance=%u continue=%u sith=%d",
		(unsigned)g_game_config.difficulty,
		(unsigned)g_game_config.collisions,
		(unsigned)g_game_config.craft_jumping,
		(unsigned)g_game_config.random_setup,
		(unsigned)g_game_config.battle_length_index,
		(unsigned)g_game_config.in_progress_join,
		(unsigned)g_game_config.craft_selection,
		(unsigned)g_game_config.locate_players,
		(unsigned)g_game_config.craft_waves,
		(unsigned)g_game_config.mission_time_limit,
		(unsigned)g_game_config.last_team_time_limit_minutes,
		g_game_config.random_seed,
		(unsigned)g_game_config.internet_play,
		(unsigned)g_game_config.ai_opponents,
		(unsigned)g_game_config.server_update_rate,
		(unsigned)g_game_config.combat_balance,
		(unsigned)g_game_config.continue_battle_or_campaign,
		xvt_sith_get_setting());
	return choose_mission() && mission_takes_seats();
}

/* Sets the battle up from g_start on this machine and goes to the launch
 * screen, as the 1997 lobby's screens did from the host's packets: the roster;
 * what the host's Begin did (a sequence's first mission, the roster made
 * final, the mission setup screen's record saved by its exit); the first
 * frames of the team and flight screens; the craft screen's with nothing
 * chosen; then the players' state. The team screen's offer to continue a saved
 * campaign or battle is not made: a door battle starts its sequence anew. */
static void start_network_battle(void)
{
	if (!xvt_door_battle_fill_roster(&g_start)) {
		fail(XVT_LAUNCH_EXIT_NOT_REACHED,
		     "A player of the battle left before it started.");
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
	g_mission_setup_roster_authoritative = 1;
	g_frontend_quick_start_launch_flag = 0;
	(void)mission_setup_exit(-1);
	mission_setup_load_mission_list(directory);
	for (g_selected_mission_list_index = 0;
	     g_mission_list &&
	     (unsigned int)g_selected_mission_list_index < g_mission_count &&
	     g_mission_list[g_selected_mission_list_index].mission_idx !=
		     g_pilot_data.mission_description_ids[directory];
	     ++g_selected_mission_list_index) {
	}
	frontend_mission_load_current();
	mission_setup_update_team_counts();
	if (xvt_door_battle_deal_teams(g_start.count) < g_start.count) {
		fail(XVT_LAUNCH_EXIT_REFUSED,
		     "The mission has fewer player places than the battle has seats.");
		return;
	}
	xvt_door_battle_assign_flights();
	mission_setup_init_craft_loadout();
	frontend_mission_init_player_state();
	switch_screen(flight_loading_update_ready_screen, NULL, concourse_exit);
	g_phase = PHASE_LAUNCHING;
	XVT_LOG_INFO(
		"launch.network_started seats=%d team=%d local=%d fg=%d mission=%d",
		g_start.count, g_pilot_data.team,
		g_local_pilot_network_player_index,
		g_mission_setup_selected_flight_group_index,
		(int)g_pilot_data.mission_description_ids[directory]);
}

/* Lists and finds rooms at the description's matchmaker under the battle's
 * own application id from now on, in place of the settings' lobby and the
 * game's fixed id. */
static void use_battle_lobby(void)
{
	const struct xvt_launch_game_id *id = &g_description.network.game;
	GUID game;
	game.Data1 = id->data1;
	game.Data2 = id->data2;
	game.Data3 = id->data3;
	memcpy(game.Data4, id->data4, sizeof(game.Data4));
	/* The description holds at most 511 characters and one key pin, which
	 * the directory takes. */
	(void)xvt_network_session_use_door(g_description.network.lobby,
					   g_description.network.lobby_pin,
					   &game);
}

/* The concourse step of a network host: loads the pilot, sets up the battle
 * and begins hosting at the battle's lobby, as the Host button does with the
 * game's fixed id and the settings' lobby. */
static void open_network_room(void)
{
	if (!load_pilot() || !set_up_network_battle()) {
		return;
	}
	use_battle_lobby();
	char rating_text[2] = {(char)(g_pilot_data.rating + 1), 0};
	xvt_network_session_begin_host(rating_text, g_pilot_data.name,
				       g_pilot_data.multiplayer_game_name, 1);
	g_phase = PHASE_OPEN_ROOM;
	XVT_LOG_INFO("launch.room_opening seats=%d",
		     g_description.network.player_count);
}

/* The host's step while its room opens: drives the session, as the network
 * task does for the Host button, until the room is listed; then marks it
 * accepting and seats the host. A session that fails ends the door. */
static void wait_for_room(void)
{
	(void)xvt_network_session_update();
	struct xvt_network_session_status status =
		xvt_network_session_get_status();
	if (status.state == XVT_NETWORK_SESSION_FAILED) {
		char why[XVT_LAUNCH_WHY_CAPACITY];
		snprintf(
			why, sizeof(why),
			"The battle's room could not be opened at the matchmaker (directory error %u).",
			(unsigned)status.error);
		fail(XVT_LAUNCH_EXIT_NOT_REACHED, why);
		return;
	}
	if (status.state != XVT_NETWORK_SESSION_ESTABLISHED) {
		return;
	}
	xvt_door_seats_fill(&g_seats, &g_description);
	memset(g_seat_players, 0, sizeof(g_seat_players));
	g_seat_players[0] = (DPID)net_get_local_player_id();
	xvt_network_session_set_door_accepting(1);
	g_phase = PHASE_GATHER;
	XVT_LOG_INFO("launch.room_open seats=%d setup=%08x player=%u",
		     g_seats.count, (unsigned)g_seats.setup,
		     (unsigned)g_seat_players[0]);
}

/* Answers one asking from sender, a player of the session: admits it to its
 * seat or refuses it, by the seats and the name its game plays as. */
static void answer_asking(DPID sender, int protocol, uint32_t setup)
{
	const struct net_player_info *player = net_find_player((int)sender);
	int seat = -1;
	enum xvt_door_verdict verdict =
		player ? xvt_door_admit(&g_seats, player->player_name, protocol,
					setup, &seat)
		       : XVT_DOOR_REFUSE_NOT_LISTED;
	int32_t answer[XVT_DOOR_PACKET_WORDS];
	xvt_door_make_answer(answer, verdict, seat);
	net_send_packet_and_flush((int)sender, answer, sizeof(answer));
	if (verdict != XVT_DOOR_ADMIT) {
		XVT_LOG_WARN(
			"launch.door_refused player=%u name=\"%s\" verdict=%d protocol=%d setup=%08x",
			(unsigned)sender, player ? player->player_name : "",
			(int)verdict, protocol, (unsigned)setup);
		return;
	}
	g_seat_players[seat] = sender;
	net_set_player_ready((int)sender);
	XVT_LOG_INFO("launch.door_admitted player=%u seat=%d name=\"%s\"",
		     (unsigned)sender, seat, player->player_name);
}

/* Frees the seat of each admitted game the session no longer lists: it left
 * before the start, and another game of that pilot may take it. */
static void free_left_seats(void)
{
	for (int seat = 1; seat < g_seats.count; ++seat) {
		if (g_seat_players[seat] &&
		    !net_find_player((int)g_seat_players[seat])) {
			XVT_LOG_INFO("launch.door_seat_freed player=%u seat=%d",
				     (unsigned)g_seat_players[seat], seat);
			xvt_door_seat_free(&g_seats, seat);
			g_seat_players[seat] = 0;
		}
	}
}

/* The host's start once every seat is in: tells each joiner the seats'
 * player ids and ratings, a rating read as the 1997 lobby state read it (the
 * first byte of the player's long name, less 1), and sets the battle up. */
static void send_start(void)
{
	memset(&g_start, 0, sizeof(g_start));
	g_start.count = g_seats.count;
	for (int seat = 0; seat < g_start.count; ++seat) {
		const struct net_player_info *player =
			net_find_player((int)g_seat_players[seat]);
		g_start.players[seat] = (uint32_t)g_seat_players[seat];
		g_start.ratings[seat] =
			player ? (int32_t)(uint8_t)player->long_name[0] - 1 : 0;
	}
	int32_t packet[XVT_DOOR_START_WORDS];
	xvt_door_make_start(packet, &g_start);
	for (int seat = 1; seat < g_start.count; ++seat) {
		net_send_packet_and_flush((int)g_seat_players[seat], packet,
					  sizeof(packet));
	}
	XVT_LOG_INFO("launch.door_start_sent seats=%d", g_start.count);
	start_network_battle();
}

/* The host's step while it gathers: answers the askings that came, frees the
 * seats of games that left, and once every seat is in, closes the room and
 * starts the battle. A packet other than an asking is dropped: no lobby runs
 * to read it. A session that fails ends the door. */
static void gather_seats(void)
{
	struct xvt_network_session_status status =
		xvt_network_session_get_status();
	if (status.state == XVT_NETWORK_SESSION_FAILED) {
		char why[XVT_LAUNCH_WHY_CAPACITY];
		snprintf(
			why, sizeof(why),
			"The battle's room closed before every seat was in (directory error %u).",
			(unsigned)status.error);
		fail(XVT_LAUNCH_EXIT_NOT_REACHED, why);
		return;
	}
	for (int count = 0; count < PACKETS_PER_STEP; ++count) {
		DPID sender;
		uint32_t size;
		const int32_t *packet = net_get_next_app_packet(&sender, &size);
		if (!packet) {
			break;
		}
		int protocol;
		uint32_t setup;
		if (xvt_door_read_ask(packet, size, &protocol, &setup)) {
			answer_asking(sender, protocol, setup);
		} else {
			XVT_LOG_DEBUG(
				"launch.door_packet_dropped player=%u type=%d bytes=%u",
				(unsigned)sender, size >= 4 ? packet[0] : -1,
				(unsigned)size);
		}
	}
	free_left_seats();
	if (!xvt_door_seats_full(&g_seats)) {
		return;
	}
	xvt_network_session_set_door_accepting(0);
	XVT_LOG_INFO("launch.door_seated seats=%d", g_seats.count);
	send_start();
}

/* The concourse step of a network joiner: loads the pilot, sets up the battle
 * and starts looking for the host's room at the battle's lobby. */
static void look_for_room(void)
{
	if (!load_pilot() || !set_up_network_battle()) {
		return;
	}
	use_battle_lobby();
	g_rooms_pending = 0;
	g_rooms_error = AERON_DPLAY_DIRECTORY_ERROR_NONE;
	g_next_refresh_us = xvt_time_get_elapsed_us();
	g_phase = PHASE_FIND_ROOM;
	XVT_LOG_INFO("launch.room_looking host=\"%s\"",
		     g_description.network.players[0]);
}

/* Joins room as the Join button does for the browser's selected room. */
static void join_room(const AeronDplayDirectoryRoom *room)
{
	xvt_network_metadata_from_utf8(
		g_pilot_data.multiplayer_game_name,
		sizeof(g_pilot_data.multiplayer_game_name),
		room->metadata.name);
	char rating_text[2] = {(char)(g_pilot_data.rating + 1), 0};
	xvt_network_session_begin_join(rating_text, g_pilot_data.name,
				       &room->room_id);
	g_phase = PHASE_JOINING;
	XVT_LOG_INFO("launch.room_found players=%u rooms=%u",
		     (unsigned)room->metadata.players,
		     (unsigned)g_rooms.room_count);
}

/* The joiner's step while it looks: once a refresh of the matchmaker's rooms
 * completes, joins the battle's room if it is listed; asks again every
 * ROOM_REFRESH_US while it is not, since the host may not have listed it yet.
 * A refresh that fails is asked again the same way. */
static void find_room(void)
{
	if (g_rooms_pending) {
		AeronDplayDirectory_GetSnapshot(&g_rooms);
		if (g_rooms.refresh.state == AERON_DPLAY_DIRECTORY_SUCCEEDED) {
			g_rooms_pending = 0;
			int index = xvt_door_find_room(
				&g_rooms, g_description.network.players[0]);
			XVT_LOG_DEBUG("launch.rooms_read rooms=%u found=%d",
				      (unsigned)g_rooms.room_count, index);
			if (index >= 0) {
				join_room(&g_rooms.rooms[index]);
				return;
			}
		} else if (g_rooms.refresh.state ==
				   AERON_DPLAY_DIRECTORY_FAILED ||
			   g_rooms.refresh.state ==
				   AERON_DPLAY_DIRECTORY_CANCELLED) {
			g_rooms_pending = 0;
			g_rooms_error = g_rooms.refresh.error;
			XVT_LOG_DEBUG("launch.rooms_unread error=%u",
				      (unsigned)g_rooms_error);
		}
	}
	uint64_t now = xvt_time_get_elapsed_us();
	if (g_rooms_pending || now < g_next_refresh_us) {
		return;
	}
	g_next_refresh_us = now + ROOM_REFRESH_US;
	AeronDplayDirectoryError error = xvt_network_session_configure();
	if (!error) {
		error = AeronDplayDirectory_Refresh();
	}
	if (error) {
		g_rooms_error = error;
		XVT_LOG_DEBUG("launch.rooms_unread error=%u", (unsigned)error);
		return;
	}
	g_rooms_pending = 1;
}

/* Ends the door when the joiner's session failed; returns 1 then, else 0. */
static int join_failed(void)
{
	struct xvt_network_session_status status =
		xvt_network_session_get_status();
	if (status.state != XVT_NETWORK_SESSION_FAILED) {
		return 0;
	}
	char why[XVT_LAUNCH_WHY_CAPACITY];
	snprintf(why, sizeof(why),
		 "The battle's room could not be joined (directory error %u).",
		 (unsigned)status.error);
	fail(XVT_LAUNCH_EXIT_NOT_REACHED, why);
	return 1;
}

/* The joiner's step while it joins: drives the session, as the network task
 * does for the Join button, until the host's sequence status has come; then
 * sends the door's asking to the host. */
static void join(void)
{
	(void)xvt_network_session_update();
	if (join_failed() || xvt_network_session_get_status().state !=
				     XVT_NETWORK_SESSION_ADMISSION) {
		return;
	}
	uint32_t setup = xvt_launch_setup_fingerprint(&g_description);
	int32_t ask[XVT_DOOR_PACKET_WORDS];
	xvt_door_make_ask(ask, setup);
	DPID host = g_front_state.net_host_player_id;
	net_send_packet_and_flush((int)host, ask, sizeof(ask));
	g_phase = PHASE_ASKED;
	XVT_LOG_INFO("launch.door_asked host=%u setup=%08x", (unsigned)host,
		     (unsigned)setup);
}

/* Takes the host's answer: an admission finishes the join, and the joiner
 * waits for the start; a refusal leaves the session and ends the door with the
 * host's reason. */
static void take_answer(int verdict, int seat)
{
	DPID host = g_front_state.net_host_player_id;
	if (verdict != XVT_DOOR_ADMIT) {
		XVT_LOG_WARN("launch.door_turned_away verdict=%d", verdict);
		xvt_network_session_reject();
		fail(XVT_LAUNCH_EXIT_REFUSED,
		     xvt_door_verdict_sentence(verdict));
		return;
	}
	if (!xvt_network_session_accept_admission(
		    host, (DPID)net_get_local_player_id())) {
		fail(XVT_LAUNCH_EXIT_NOT_REACHED,
		     "The host let this pilot in after the join's time ran out.");
		return;
	}
	XVT_LOG_INFO("launch.door_seat_taken seat=%d described=%d", seat,
		     g_description.network.seat);
	g_joiner_seat = seat;
	g_phase = PHASE_SEATED;
}

/* The joiner's step while it waits for the host's answer: reads the packets
 * that came and takes the first answer from the host. Any other packet is
 * dropped: no lobby runs to read it. */
static void wait_for_answer(void)
{
	(void)xvt_network_session_update();
	if (join_failed()) {
		return;
	}
	DPID host = g_front_state.net_host_player_id;
	for (int count = 0; count < PACKETS_PER_STEP; ++count) {
		DPID sender;
		uint32_t size;
		const int32_t *packet = net_get_next_app_packet(&sender, &size);
		if (!packet) {
			break;
		}
		int verdict;
		int seat;
		if (sender == host &&
		    xvt_door_read_answer(packet, size, &verdict, &seat)) {
			take_answer(verdict, seat);
			return;
		}
		XVT_LOG_DEBUG(
			"launch.door_packet_dropped player=%u type=%d bytes=%u",
			(unsigned)sender, size >= 4 ? packet[0] : -1,
			(unsigned)size);
	}
}

/* The joiner's step once the start came: sets the battle up as soon as the
 * session lists every seat's player. */
static void wait_for_players(void)
{
	if (join_failed()) {
		return;
	}
	if (xvt_door_battle_players_known(&g_start)) {
		start_network_battle();
	}
}

/* Takes the host's start: the seats must be the description's count with
 * this game at the seat the host answered; then the battle starts as soon as
 * the session lists every player. */
static void take_start(void)
{
	uint32_t seated = g_joiner_seat >= 0 && g_joiner_seat < g_start.count
				  ? g_start.players[g_joiner_seat]
				  : 0;
	if (g_start.count != g_description.network.player_count ||
	    seated != (uint32_t)net_get_local_player_id()) {
		XVT_LOG_WARN(
			"launch.door_start_mismatch seats=%d described=%d seat=%d player=%u",
			g_start.count, g_description.network.player_count,
			g_joiner_seat, (unsigned)seated);
		fail(XVT_LAUNCH_EXIT_REFUSED,
		     "The host started the battle with seats this pilot's description does not hold.");
		return;
	}
	XVT_LOG_INFO("launch.door_start_taken seats=%d seat=%d", g_start.count,
		     g_joiner_seat);
	g_phase = PHASE_STARTING;
	wait_for_players();
}

/* The joiner's step while it waits in its seat: reads the packets that came
 * and takes the host's start. Any other packet is dropped: no lobby runs to
 * read it. The session is kept by the frame loop's service, which ends it if
 * the host is lost. */
static void wait_for_start(void)
{
	if (join_failed()) {
		return;
	}
	DPID host = g_front_state.net_host_player_id;
	for (int count = 0; count < PACKETS_PER_STEP; ++count) {
		DPID sender;
		uint32_t size;
		const int32_t *packet = net_get_next_app_packet(&sender, &size);
		if (!packet) {
			break;
		}
		if (sender == host &&
		    xvt_door_read_start(packet, size, &g_start)) {
			take_start();
			return;
		}
		XVT_LOG_DEBUG(
			"launch.door_packet_dropped player=%u type=%d bytes=%u",
			(unsigned)sender, size >= 4 ? packet[0] : -1,
			(unsigned)size);
	}
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
		char why[XVT_LAUNCH_WHY_CAPACITY];
		if (g_phase == PHASE_FIND_ROOM) {
			snprintf(
				why, sizeof(why),
				"The battle's room was not listed at the host's matchmaker within 180 seconds (last directory error %u).",
				(unsigned)g_rooms_error);
		} else {
			snprintf(
				why, sizeof(why), "%s",
				"The battle did not start within 180 seconds.");
		}
		fail(XVT_LAUNCH_EXIT_NOT_REACHED, why);
		return;
	}
	if (g_phase == PHASE_WAIT_CONCOURSE && screen_is(concourse_update)) {
		if (!g_description.is_network) {
			open_solo_setup();
		} else if (g_description.network.role == XVT_LAUNCH_ROLE_HOST) {
			open_network_room();
		} else {
			look_for_room();
		}
	} else if (g_phase == PHASE_WAIT_SETUP &&
		   screen_is(mission_setup_update)) {
		start_battle();
	} else if (g_phase == PHASE_OPEN_ROOM) {
		wait_for_room();
	} else if (g_phase == PHASE_GATHER) {
		gather_seats();
	} else if (g_phase == PHASE_FIND_ROOM) {
		find_room();
	} else if (g_phase == PHASE_JOINING) {
		join();
	} else if (g_phase == PHASE_ASKED) {
		wait_for_answer();
	} else if (g_phase == PHASE_SEATED) {
		wait_for_start();
	} else if (g_phase == PHASE_STARTING) {
		wait_for_players();
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
	result.local_entry = local >= 0 && local < 8 ? local : -1;
	xvt_launch_stats_read(&result);
	result.outcome = XVT_LAUNCH_OUTCOME_NOT_COMPLETED;
	if (team >= 0 && team < 10) {
		result.goals_met_seconds =
			g_pilot_data.teams[team].mission_time;
		if (g_pilot_data.teams[team].is_mission_completed) {
			result.outcome = XVT_LAUNCH_OUTCOME_COMPLETED;
		}
	}
	/* The setting is read before the flight ends it. */
	struct xvt_sith_report sith;
	xvt_sith_get_report(&sith);
	xvt_sith_end_flight();
	result.sith.setting = sith.setting;
	result.sith.flight_group = sith.flight_group;
	snprintf(result.sith.flight_name, sizeof(result.sith.flight_name), "%s",
		 sith.flight_name);
	result.sith.craft_type = sith.craft_type;
	result.sith.apprentices_flown = sith.apprentices_flown;
	result.sith.apprentices_lost = sith.apprentices_lost;
	result.sith.lord_flown = sith.lord_flown;
	result.sith.lord_lost = sith.lord_lost;
	snprintf(result.sith.why, sizeof(result.sith.why), "%s", sith.why);
	/* The zeroed exit code is XVT_LAUNCH_EXIT_FLOWN. */
	XVT_LOG_DEBUG(
		"launch.result_read local=%d team=%d score=%d kills=%d losses=%d goals_met_seconds=%d outcome=%d sith=\"%s\" sith_group=%d sith_craft=%d apprentices_flown=%d apprentices_lost=%d lord_flown=%d lord_lost=%d sith_why=\"%s\"",
		local, team, result.score, result.kills, result.losses,
		result.goals_met_seconds, result.outcome,
		xvt_sith_setting_name(sith.setting), sith.flight_group,
		sith.craft_type, sith.apprentices_flown, sith.apprentices_lost,
		sith.lord_flown, sith.lord_lost, sith.why);
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

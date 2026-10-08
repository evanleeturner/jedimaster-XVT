/* Tests for xvt/net/frontend_net.c: the multiplayer frontend's packet handler,
 * frontend_net_process_network_packets, the screen that waits for the host's
 * answer to a join request, frontend_net_await_join_admission_screen, and the
 * chat panel, frontend_net_update_and_draw_chat_panel.
 *
 * The packet checks clear the frontend state, make this player the host or a
 * client of a lobby with no DirectPlay interface, and put each packet in the
 * lobby's receive queue as the next one expected from its sender: each
 * sender's packets are numbered from 0, and the numbering starts again
 * whenever the frontend state is cleared. The answers sent go nowhere, so the
 * checks read the answer the handler built in g_frontend_net_packet_scratch,
 * and what it stored in the game's globals.
 *
 * The screen checks run on a frontend display with no window
 * (test_frontend_display.h), in a temporary asset folder (test_asset_folder.h)
 * holding mission lists and a string table this file writes: string n reads
 * "s<n>", so a dialog's lines can be read back. The folder has no images and
 * no fonts; the chat log is drawn with a font of one-row glyphs built in
 * memory. The checks read what the screens leave in the game's globals, the
 * screen stack, the front end's scratch buffer and the lines the code logs,
 * kept by a log sink with DEBUG lines let through. No game data is read.
 *
 * Not checked here: a join request the host admits and a PLAYER_ADMITTED that
 * finishes the session's admission, which need a DirectPlay session; whom each
 * packet is sent to; the admission screen's version text and pilot banner,
 * which draw nothing here and whose names the top bar's buttons overwrite in
 * g_frontend_scratch_buffer in the same frame; and quitting from the top bar,
 * which saves the game's settings, not loaded here.
 *
 * POSIX only, for the temporary folder and for the alarm that stops a check
 * whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL_log.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "test_frontend_display.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_session.h"

enum {
	PEER_DPID = 200,
	CHAT_LOG_BYTES = 1024,
};

enum {
	/* This machine's player, listed first in the lobby, and a player the
	 * lobby does not list. */
	LOCAL_ID = 0x41,
	UNLISTED_ID = 300,
	/* Points on the admission screen's Cancel button and on the chat
	 * panel's Team and All tabs. */
	CANCEL_X = 100,
	CANCEL_Y = 460,
	TEAM_TAB_X = 560,
	ALL_TAB_X = 530,
	TAB_Y = 100,
	LINE_CAPACITY = 4096,
	LINE_SIZE = 256,
	STRING_COUNT = 830,
	SEQUENCE_SENDERS = 512,
};

static struct xvt_test_assets g_assets;
static char g_lines[LINE_CAPACITY][LINE_SIZE];
static int g_line_count;
/* The number the next packet from each sender gets. */
static uint8_t g_next_sequence[SEQUENCE_SENDERS];

/* ------------------------------------------------------------------------ */
/* The program's setups: the alarm, the log sink and the asset folder. */

static void stop_on_alarm(int signal_number)
{
	static const char message[] =
		"check failed: the call did not return within the time allowed\n";
	(void)signal_number;
	if (write(2, message, sizeof message - 1) < 0) {
		_exit(1);
	}
	_exit(1);
}

/* For a check whose call may never return: the program fails after the given
 * seconds. */
static void fail_after_seconds(unsigned int seconds)
{
	signal(SIGALRM, stop_on_alarm);
	alarm(seconds);
}

/* Keeps each line the engine writes, without Aeron's "xvt: " category
 * prefix. */
static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	static const char prefix[] = "xvt: ";
	if (g_line_count >= LINE_CAPACITY) {
		fprintf(stderr, "more than %d lines written\n", LINE_CAPACITY);
		exit(1);
	}
	if (!strncmp(message, prefix, sizeof prefix - 1)) {
		message += sizeof prefix - 1;
	}
	snprintf(g_lines[g_line_count++], LINE_SIZE, "%s", message);
}

/* Returns 1 when the kept line starts with start, followed by a space or
 * nothing. */
static int line_starts_with(int index, const char *start)
{
	size_t length = strlen(start);
	const char *line = g_lines[index];
	return strncmp(line, start, length) == 0 &&
	       (line[length] == ' ' || line[length] == '\0');
}

/* The number of kept lines that start with start: an event name, with any
 * of its first values. */
static int count_lines(const char *start)
{
	int count = 0;
	for (int i = 0; i < g_line_count; ++i) {
		if (line_starts_with(i, start)) {
			++count;
		}
	}
	return count;
}

/* The number after " key=" in the last kept line that starts with start;
 * the check fails when there is none. */
static int line_value(const char *start, const char *key)
{
	char field[64];
	snprintf(field, sizeof field, " %s=", key);
	for (int i = g_line_count - 1; i >= 0; --i) {
		if (!line_starts_with(i, start)) {
			continue;
		}
		const char *value = strstr(g_lines[i], field);
		XVT_ASSERT_TRUE(value != NULL);
		return atoi(value + strlen(field));
	}
	fprintf(stderr, "no line starts with %s\n", start);
	exit(1);
}

/* Writes the text to path in the asset folder, path written with '/', making
 * the folders on the way. */
static void put_text(const char *path, const char *text)
{
	xvt_test_add_asset(&g_assets, path);
	xvt_test_write_text(g_assets.asset, path, text);
}

/* Writes the files the code reads: the mission lists of melees, combat
 * engagements and campaigns, each holding missions 1, 3 and 5, and the string
 * table. */
static void write_assets(void)
{
	static const char *const lists[] = {
		"melee/mission.lst",	"combat/rebel.lst",
		"combat/mission.lst",	"campaign/rebel.lst",
		"campaign/mission.lst",
	};
	static char strings[STRING_COUNT * 8];
	size_t used = 0;
	xvt_test_open_assets(&g_assets);
	for (size_t i = 0; i < sizeof lists / sizeof lists[0]; ++i) {
		put_text(lists[i], "1\nm1.tie\nFirst (one)\n"
				   "3\nm3.tie\nThird (three)\n"
				   "5\nm5.tie\nFifth (five)\n");
	}
	for (int i = 0; i < STRING_COUNT; ++i) {
		used += (size_t)snprintf(strings + used, sizeof strings - used,
					 "s%d\n", i);
	}
	put_text("strings.txt", strings);
}

/* ------------------------------------------------------------------------ */
/* The lobby and its packets. */

/* A lobby with this player as host or client, no players ready, no password
 * and the roster open, and an empty receive queue. */
static void lobby_world(int host)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(g_next_sequence, 0, sizeof g_next_sequence);
	g_front_state.net_is_host = host;
	g_front_state.net_host_player_id = host ? 1 : PEER_DPID;
	g_game_config.require_password = 0;
	g_mission_setup_roster_authoritative = 0;
	memset(&g_frontend_net_packet_scratch, 0,
	       sizeof g_frontend_net_packet_scratch);
}

/* Queues a packet of size bytes from sender as the next one expected from it
 * on its direct channel. */
static void queue_from(int sender, const int *packet, uint32_t size)
{
	struct net_queued_packet *entry =
		&g_front_state.net_runtime_recv_queue
			 [g_front_state.net_runtime_recv_queue_write_index];
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = (DPID)sender;
	entry->packet_class = 1;
	entry->sequence_byte = g_next_sequence[sender % SEQUENCE_SENDERS]++;
	entry->payload_size = size;
	memcpy(entry->payload, packet, size);
	++g_front_state.net_runtime_recv_queue_write_index;
	++g_front_state.net_runtime_recv_queue_count;
}

/* Queues a packet of size bytes from sender and handles it; returns the
 * handler's result. */
static int handle_from(int sender, const int *packet, uint32_t size)
{
	queue_from(sender, packet, size);
	return frontend_net_process_network_packets();
}

/* Queues a packet of size bytes from player 200 as the next one expected on
 * its direct channel, and handles it; returns the handler's result. */
static int handle_packet(const int *packet, uint32_t size)
{
	return handle_from(PEER_DPID, packet, size);
}

/* A join request with the given protocol version and password. */
static int join_request(int version, const char *password)
{
	int packet[6];
	memset(packet, 0, sizeof packet);
	packet[0] = NET_PACKET_JOIN_REQUEST;
	packet[1] = version;
	strncpy((char *)&packet[2], password, 4 * sizeof(int));
	return handle_packet(packet, sizeof packet);
}

/* A lobby of two listed players, this one (entry 0, "Luke") and player 200
 * ("Wedge"), with this player as host or client and nobody ready. The stores
 * the packets write, the game's options and the pilot are cleared, there is no
 * chat log, and no log lines are kept. */
static void lane_world(int host)
{
	memset(&g_game_config, 0, sizeof g_game_config);
	lobby_world(host);
	g_front_state.net_host_player_id = host ? LOCAL_ID : PEER_DPID;
	g_front_state.net_player_count = 2;
	g_front_state.net_players[0].player_id = LOCAL_ID;
	g_front_state.net_players[1].player_id = PEER_DPID;
	strcpy(g_front_state.net_players[0].player_name, "Luke");
	strcpy(g_front_state.net_players[1].player_name, "Wedge");
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(g_mp_roster, 0, sizeof g_mp_roster);
	memset(g_mp_roster_ready_flags, 0, sizeof g_mp_roster_ready_flags);
	memset(&g_mission_setup_player_assignments, 0,
	       sizeof g_mission_setup_player_assignments);
	memset(g_mission_setup_player_flight_group_indices, 0,
	       sizeof g_mission_setup_player_flight_group_indices);
	memset(g_net_player_connection_stats, 0,
	       sizeof g_net_player_connection_stats);
	memset(g_movie_multiplayer_sync_players, 0,
	       sizeof g_movie_multiplayer_sync_players);
	g_frontend_chat_log_buffer = NULL;
	g_frontend_net_packet_sender_player_id = 0;
	g_frontend_net_packet_arg0 = 0;
	g_frontend_net_reserving_player_id = 0;
	g_frontend_briefing_entered_count = 0;
	g_frontend_skip_screen_entry_setup = 0;
	g_line_count = 0;
}

/* The int at word index of the scratch packet's payload. */
static int scratch_word(int index)
{
	int value;
	memcpy(&value, g_frontend_net_packet_scratch.payload + index * 4,
	       sizeof value);
	return value;
}

/* The link figures stored for the player, or NULL when none are. */
static const struct net_player_connection_stats *link_figures(int player_id)
{
	for (int i = 0; i < 40; ++i) {
		if (g_net_player_connection_stats[i].player_id == player_id) {
			return &g_net_player_connection_stats[i];
		}
	}
	return NULL;
}

/* ------------------------------------------------------------------------ */
/* Join requests. */

/* The host refuses a request with the wrong protocol version, one with the
 * wrong password, and every request while the roster is locked or 8 players
 * are admitted, each with its own answer. */
static void check_join_refusals(void)
{
	lobby_world(1);
	join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_VERSION_MISMATCH);

	lobby_world(1);
	g_game_config.require_password = 1;
	strncpy(g_game_config.password, "yoda", sizeof g_game_config.password);
	join_request(FRONTEND_NET_PROTOCOL_VERSION, "vader");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_PASSWORD_REQUIRED);
	g_game_config.require_password = 0;

	/* Full is answered before the version is looked at: with 8 players
	 * ready a request with the wrong version is refused as full, with 7 for
	 * its version. */
	lobby_world(1);
	for (int i = 0; i < 8; ++i) {
		g_front_state.net_players[i].ready_flag = 1;
	}
	join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_GAME_FULL);
	lobby_world(1);
	for (int i = 0; i < 7; ++i) {
		g_front_state.net_players[i].ready_flag = 1;
	}
	join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_VERSION_MISMATCH);

	lobby_world(1);
	g_mission_setup_roster_authoritative = 1;
	join_request(FRONTEND_NET_PROTOCOL_VERSION, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_ROSTER_LOCKED);
	g_mission_setup_roster_authoritative = 0;
}

/* A request reaching a client, or of the wrong size, is dropped unanswered. */
static void check_join_dropped(void)
{
	lobby_world(0);
	XVT_ASSERT_INT_EQ(join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, ""),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);

	lobby_world(1);
	int packet[5] = {NET_PACKET_JOIN_REQUEST, 0, 0, 0, 0};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);
}

/* With no packet waiting the handler returns NET_PACKET_NONE. */
static void check_no_packet(void)
{
	lobby_world(1);
	XVT_ASSERT_INT_EQ(frontend_net_process_network_packets(),
			  NET_PACKET_NONE);
}

/* ------------------------------------------------------------------------ */
/* The chat log. */

static char *g_chat_log;

/* Gives the frontend a chat log of 1,024 bytes, full of 'x'. */
static void hold_chat_log(void)
{
	free(g_chat_log);
	g_chat_log = malloc(CHAT_LOG_BYTES);
	XVT_ASSERT_TRUE(g_chat_log != NULL);
	memset(g_chat_log, 'x', CHAT_LOG_BYTES);
	g_frontend_chat_log_buffer = g_chat_log;
	g_frontend_chat_log_used_bytes = 0;
}

/* A chat sync chunk with the given index and text. */
static int chat_chunk(int chunk_index, const char *text, size_t length)
{
	int packet[2 + 400 / sizeof(int)];
	memset(packet, 0, sizeof packet);
	packet[0] = NET_PACKET_CHAT_SYNC_CHUNK;
	packet[1] = chunk_index;
	memcpy(&packet[2], text, length);
	return handle_packet(packet, (uint32_t)(2 * sizeof(int) + length));
}

/* Chunk 0 clears the log and starts it; chunk 1 goes 400 bytes in. Every byte
 * from 0 to 6 becomes color code 6. */
static void check_chat_chunks(void)
{
	lobby_world(0);
	hold_chat_log();
	XVT_ASSERT_INT_EQ(chat_chunk(0, "\005Hi\001", 4),
			  NET_PACKET_CHAT_SYNC_CHUNK);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log, "\006Hi\006", 4), 0);
	XVT_ASSERT_INT_EQ(g_chat_log[4], 0);
	XVT_ASSERT_INT_EQ(g_chat_log[CHAT_LOG_BYTES - 1], 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 4);
	lobby_world(0);
	XVT_ASSERT_INT_EQ(chat_chunk(1, "Yes", 3), NET_PACKET_CHAT_SYNC_CHUNK);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log + 400, "Yes", 3), 0);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log, "\006Hi\006", 4), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 7);
}

/* ------------------------------------------------------------------------ */
/* The packet handler: before the switch. */

/* When a ready player left this frame the handler first sends everyone the
 * lobby state, even with no packet waiting, and logs it; the state's last
 * packet, the game options, is left in the scratch packet. Otherwise nothing
 * is sent. */
static void check_ready_player_left(void)
{
	lane_world(1);
	g_front_state.net_ready_player_left_this_frame = 1;
	XVT_ASSERT_INT_EQ(frontend_net_process_network_packets(),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_GAME_OPTIONS);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_state_resent"), 1);

	lane_world(1);
	XVT_ASSERT_INT_EQ(frontend_net_process_network_packets(),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_state_resent"), 0);
}

/* ------------------------------------------------------------------------ */
/* The packet handler: game status and probe requests. */

/* A game-started notice or a probe answer stores its three words: the
 * mission's seconds, the protocol version and the password flag. The
 * packet's sender is kept, and its type returned. */
static void check_game_status(void)
{
	static const int types[2] = {NET_PACKET_FRONTEND_GAME_STARTED,
				     NET_PACKET_PROBE_RESPONSE};
	for (int i = 0; i < 2; ++i) {
		lane_world(0);
		int packet[4] = {types[i], 75 + i, 9, 1};
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  types[i]);
		XVT_ASSERT_INT_EQ(g_frontend_net_probe_mission_elapsed_seconds,
				  75 + i);
		XVT_ASSERT_INT_EQ(g_frontend_net_probe_version, 9);
		XVT_ASSERT_INT_EQ(g_frontend_net_probe_password_required, 1);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_sender_player_id,
				  PEER_DPID);
	}
}

/* A probe request is answered, after the lobby state, with a PROBE_RESPONSE:
 * 0 seconds, the protocol version and this game's password flag. The lobby
 * state's last packet, the game options, leaves the difficulty, 2, where the
 * seconds go. */
static void check_probe_request(void)
{
	lane_world(1);
	g_game_config.require_password = 1;
	g_game_config.difficulty = 2;
	strcpy(g_pilot_data.multiplayer_game_name, "Rogue");
	int packet[1] = {NET_PACKET_PROBE_REQUEST};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_PROBE_REQUEST);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_PROBE_RESPONSE);
	XVT_ASSERT_INT_EQ(scratch_word(0), 0);
	XVT_ASSERT_INT_EQ(scratch_word(1), FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(scratch_word(2), 1);
	XVT_ASSERT_INT_EQ(line_value("network.status_query", "player"),
			  PEER_DPID);
}

/* ------------------------------------------------------------------------ */
/* The packet handler: the lobby STATE and READY_ROSTER. */

/* The four roster entries of the lobby packets, three words each: an empty
 * place rated 3, player 200 rated 4 with latency 77, a player the lobby does
 * not list rated 5 with latency 66, and this player rated 6 with latency
 * 88. */
static const int g_roster_entries[12] = {
	0, 3, 0, PEER_DPID, 4, 77, UNLISTED_ID, 5, 66, LOCAL_ID, 6, 88,
};

/* A lobby with a third listed player, 400, who is ready, and stale setup
 * roster entries. */
static void roster_world(int host)
{
	lane_world(host);
	g_front_state.net_player_count = 3;
	g_front_state.net_players[2].player_id = 400;
	g_front_state.net_players[2].ready_flag = 1;
	g_mp_roster[0].player_id = 5;
	g_mp_roster[4].player_id = 999;
	strcpy(g_pilot_data.multiplayer_game_name, "Old");
}

/* What a lobby packet holding g_roster_entries leaves: the setup roster's
 * first four entries, the rest cleared, the ready flags of the listed players
 * named set and the others cleared, and on a client only, the latencies of the
 * listed players named. */
static void expect_roster(int host)
{
	XVT_ASSERT_INT_EQ(g_mp_roster[0].player_id, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].pilot_rating, 3);
	XVT_ASSERT_INT_EQ(g_mp_roster[1].player_id, PEER_DPID);
	XVT_ASSERT_INT_EQ(g_mp_roster[1].pilot_rating, 4);
	XVT_ASSERT_INT_EQ(strcmp(g_mp_roster[1].name, "Wedge"), 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].player_id, UNLISTED_ID);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].pilot_rating, 5);
	XVT_ASSERT_INT_EQ(strcmp(g_mp_roster[2].name, "No name"), 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[3].player_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(g_mp_roster[3].pilot_rating, 6);
	XVT_ASSERT_INT_EQ(strcmp(g_mp_roster[3].name, "Luke"), 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[4].player_id, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[2].ready_flag, 0);
	XVT_ASSERT_TRUE(link_figures(UNLISTED_ID) == NULL);
	if (host) {
		XVT_ASSERT_TRUE(link_figures(PEER_DPID) == NULL);
		XVT_ASSERT_TRUE(link_figures(LOCAL_ID) == NULL);
	} else {
		XVT_ASSERT_TRUE(link_figures(PEER_DPID) != NULL);
		XVT_ASSERT_INT_EQ(link_figures(PEER_DPID)->latency_total_ms,
				  77);
		XVT_ASSERT_TRUE(link_figures(LOCAL_ID) != NULL);
		XVT_ASSERT_INT_EQ(link_figures(LOCAL_ID)->latency_total_ms, 88);
	}
}

/* A lobby STATE rebuilds the setup roster from the ready count in word 13 and
 * the entries from word 14: an empty place keeps its rating, a listed player
 * gets its lobby name and is marked ready, and any other player is named "No
 * name". It also stores the game's name (words 1 to 8), the mission type and
 * mission (words 10 and 11), and the players still needed: word 12 less the
 * ready count. */
static void check_lobby_state(void)
{
	for (int host = 0; host < 2; ++host) {
		roster_world(host);
		int packet[14 + 12];
		memset(packet, 0, sizeof packet);
		packet[0] = NET_PACKET_STATE;
		memcpy(&packet[1], "Rogue Squadron", sizeof "Rogue Squadron");
		packet[10] = MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
		packet[11] = 5;
		packet[12] = 8;
		packet[13] = 4;
		memcpy(&packet[14], g_roster_entries, sizeof g_roster_entries);
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  NET_PACKET_STATE);
		expect_roster(host);
		XVT_ASSERT_INT_EQ(strcmp(g_pilot_data.multiplayer_game_name,
					 "Rogue Squadron"),
				  0);
		XVT_ASSERT_INT_EQ(g_frontend_net_received_mission_directory_id,
				  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
		XVT_ASSERT_INT_EQ(
			g_frontend_net_received_mission_description_id, 5);
		XVT_ASSERT_INT_EQ(g_frontend_net_probe_players_needed, 4);
	}
}

/* A READY_ROSTER rebuilds the setup roster the same way from its ready count
 * in word 2 and its entries from word 3, and stores the players still needed:
 * word 1 less the ready count. The game's name is kept. */
static void check_ready_roster(void)
{
	for (int host = 0; host < 2; ++host) {
		roster_world(host);
		int packet[3 + 12];
		packet[0] = NET_PACKET_READY_ROSTER;
		packet[1] = 7;
		packet[2] = 4;
		memcpy(&packet[3], g_roster_entries, sizeof g_roster_entries);
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  NET_PACKET_READY_ROSTER);
		expect_roster(host);
		XVT_ASSERT_INT_EQ(g_frontend_net_probe_players_needed, 3);
		XVT_ASSERT_INT_EQ(
			strcmp(g_pilot_data.multiplayer_game_name, "Old"), 0);
	}
}

/* ------------------------------------------------------------------------ */
/* The packet handler: the refusals and notices a joining player is sent. */

/* The refusals and notices a client is sent (game full, host cancelled,
 * kicked, session cancelled, version, password, roster locked, flight
 * assignments ready) are returned for the screen to act on; the handler
 * stores and sends nothing for them. An unknown type returns
 * NET_PACKET_NONE. */
static void check_notices_returned(void)
{
	static const int types[8] = {
		NET_PACKET_GAME_FULL,
		NET_PACKET_HOST_CANCELLED,
		NET_PACKET_PLAYER_KICKED,
		NET_PACKET_SESSION_CANCELLED,
		NET_PACKET_VERSION_MISMATCH,
		NET_PACKET_PASSWORD_REQUIRED,
		NET_PACKET_ROSTER_LOCKED,
		NET_PACKET_FLIGHT_ASSIGNMENTS_READY,
	};
	for (int i = 0; i < 8; ++i) {
		lane_world(0);
		int packet[4] = {types[i], 1, 2, 3};
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  types[i]);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_arg0, 0);
	}
	lane_world(0);
	int unknown[2] = {NET_PACKET_PING, 1};
	XVT_ASSERT_INT_EQ(handle_packet(unknown, sizeof unknown),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(line_value("network.packet_unknown", "type"),
			  NET_PACKET_PING);
}

/* ------------------------------------------------------------------------ */
/* The packet handler: PLAYER_ADMITTED and PLAYER_LEFT. */

/* A client takes the host's PLAYER_ADMITTED naming itself: it marks that
 * player ready and returns the type. One naming another player is ignored,
 * NET_PACKET_NONE, unless this player is already in the setup roster. */
static void check_player_admitted(void)
{
	lane_world(0);
	g_game_config.sfx_datapad_enabled = 1;
	int packet[2] = {NET_PACKET_PLAYER_ADMITTED, LOCAL_ID};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_PLAYER_ADMITTED);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 1);
	XVT_ASSERT_INT_EQ(line_value("network.admission_received", "player"),
			  LOCAL_ID);

	lane_world(0);
	packet[1] = PEER_DPID;
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.admission_ignored"), 1);

	lane_world(0);
	g_mp_roster[3].player_id = LOCAL_ID;
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_PLAYER_ADMITTED);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 1);
}

/* A PLAYER_ADMITTED shorter than two words, or not sent by the host, is
 * refused with a warning: NET_PACKET_NONE, and no one is marked ready. */
static void check_admission_refused(void)
{
	lane_world(0);
	int packet[2] = {NET_PACKET_PLAYER_ADMITTED, LOCAL_ID};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof(int)), NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 0);
	XVT_ASSERT_INT_EQ(line_value("network.admission_rejected", "bytes"),
			  sizeof(int));

	lane_world(0);
	g_front_state.net_host_player_id = UNLISTED_ID;
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.admission_rejected"), 1);
}

/* When a player among the setup roster's first ready-count entries leaves,
 * every roster ready flag is cleared and the lobby state sent; either way
 * everyone is then sent a MOVIE_SYNC removing the player: op 2 and its id. */
static void check_player_left(void)
{
	for (int ready_count = 0; ready_count < 3; ++ready_count) {
		lane_world(1);
		g_game_config.sfx_datapad_enabled = 1;
		for (int i = 0; i < ready_count; ++i) {
			g_front_state.net_players[i].ready_flag = 1;
		}
		g_mp_roster[0].player_id = LOCAL_ID;
		g_mp_roster[1].player_id = PEER_DPID;
		g_mp_roster_ready_flags[0] = 1;
		g_mp_roster_ready_flags[1] = 1;
		int packet[1] = {NET_PACKET_PLAYER_LEFT};
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  NET_PACKET_PLAYER_LEFT);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
				  NET_PACKET_MOVIE_SYNC);
		XVT_ASSERT_INT_EQ(scratch_word(0), 2);
		XVT_ASSERT_INT_EQ(scratch_word(1), PEER_DPID);
		/* Player 200 is entry 1: found once 2 players are ready. */
		XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[0],
				  ready_count == 2 ? 0 : 1);
		XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[1],
				  ready_count == 2 ? 0 : 1);
		XVT_ASSERT_INT_EQ(line_value("network.leave_notice", "index"),
				  ready_count == 2 ? 1 : ready_count);
		XVT_ASSERT_INT_EQ(line_value("network.leave_notice", "ready"),
				  ready_count);
	}
}

/* ------------------------------------------------------------------------ */
/* The packet handler: assignments ready, mission start and seeds. */

/* TEAM_ASSIGNMENTS_READY copies the ten teams' player lists from the packet,
 * 320 bytes, and keeps the assigned players. */
static void check_team_assignments_ready(void)
{
	lane_world(0);
	g_mission_setup_player_assignments.assigned_player_ids[3] = 7;
	int packet[1 + 80];
	packet[0] = NET_PACKET_TEAM_ASSIGNMENTS_READY;
	for (int i = 0; i < 80; ++i) {
		packet[1 + i] = 1000 + i;
	}
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_TEAM_ASSIGNMENTS_READY);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[0][0], 1000);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[9][7], 1079);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[3], 7);
}

/* Handles a mission start for the mission type, with the mission, the
 * sequence flag and the count, and returns the handler's result. */
static int mission_start(int directory, int mission, int sequence, int count)
{
	int packet[5] = {NET_PACKET_FRONTEND_MISSION_START, mission, directory,
			 sequence, count};
	return handle_packet(packet, sizeof packet);
}

/* Frees the mission list a mission start loaded. */
static void free_mission_list(void)
{
	free(g_mission_list);
	g_mission_list = NULL;
	g_mission_count = 0;
}

/* A mission start sets the pilot's mission type (word 2) and its mission
 * (word 1). Starting a sequence (word 3 is 1), it keeps the type's previous
 * mission as the saved one and stores word 4 as a melee's mission count, a
 * combat engagement's victories needed, or else a campaign's mission count.
 * It loads the type's mission list and selects the mission in it. */
static void check_mission_start_sequence(void)
{
	lane_world(0);
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES] = 5;
	XVT_ASSERT_INT_EQ(mission_start(MISSION_DIRECTORY_MELEES, 3, 1, 4),
			  NET_PACKET_FRONTEND_MISSION_START);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_MELEES);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_sequence_active, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.saved_mission_description_id, 5);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES],
		3);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.melee_tournament_sequence_state.mission_count, 4);
	XVT_ASSERT_INT_EQ(g_mission_count, 3);
	XVT_ASSERT_INT_EQ(g_selected_mission_list_index, 1);
	free_mission_list();

	lane_world(0);
	XVT_ASSERT_INT_EQ(
		mission_start(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS, 5, 1, 2),
		NET_PACKET_FRONTEND_MISSION_START);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.victories_needed,
			  2);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.melee_tournament_sequence_state.mission_count, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.campaign_sequence_state.mission_count,
			  0);
	XVT_ASSERT_INT_EQ(g_selected_mission_list_index, 2);
	free_mission_list();

	lane_world(0);
	XVT_ASSERT_INT_EQ(mission_start(MISSION_DIRECTORY_CAMPAIGNS, 1, 1, 6),
			  NET_PACKET_FRONTEND_MISSION_START);
	XVT_ASSERT_INT_EQ(g_pilot_data.campaign_sequence_state.mission_count,
			  6);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.victories_needed,
			  0);
	XVT_ASSERT_INT_EQ(g_selected_mission_list_index, 0);
	free_mission_list();
}

/* Outside a sequence a mission start keeps the saved mission and the
 * sequence counts; a mission not in the list leaves the selected index at the
 * list's count. */
static void check_mission_start_single(void)
{
	lane_world(0);
	g_pilot_data.saved_mission_description_id = 77;
	XVT_ASSERT_INT_EQ(mission_start(MISSION_DIRECTORY_MELEES, 9, 0, 4),
			  NET_PACKET_FRONTEND_MISSION_START);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_sequence_active, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.saved_mission_description_id, 77);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.melee_tournament_sequence_state.mission_count, 0);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES],
		9);
	XVT_ASSERT_INT_EQ(g_selected_mission_list_index, 3);
	XVT_ASSERT_INT_EQ(line_value("network.mission_start", "listed"), 1);
	free_mission_list();
}

/* The next-mission and replay packets store word 1 as the game's random
 * seed. */
static void check_seeds(void)
{
	static const int types[6] = {
		NET_PACKET_NEXT_TOURNAMENT_MISSION,
		NET_PACKET_NEXT_BATTLE_MISSION,
		NET_PACKET_REPLAY_CURRENT_MISSION,
		NET_PACKET_NEXT_CAMPAIGN_MISSION,
		NET_PACKET_REPLAY_CAMPAIGN_MISSION,
		NET_PACKET_REPLAY_MISSION,
	};
	for (int i = 0; i < 6; ++i) {
		lane_world(0);
		int packet[2] = {types[i], 4242 + i};
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  types[i]);
		XVT_ASSERT_INT_EQ(g_game_config.random_seed, 4242 + i);
	}
}

/* ------------------------------------------------------------------------ */
/* The packet handler: CHAT. */

/* Handles a chat line of the given text from sender. */
static int chat_line(int sender, const char *text)
{
	int packet[1 + 256 / sizeof(int)];
	size_t length = strlen(text);
	packet[0] = NET_PACKET_CHAT;
	memcpy(&packet[1], text, length);
	return handle_from(sender, packet, (uint32_t)(sizeof(int) + length));
}

/* A chat line is added to the log after the text there, followed by a line
 * feed and a NUL, and counted in the log's used bytes. */
static void check_chat_appended(void)
{
	lane_world(0);
	hold_chat_log();
	memcpy(g_chat_log, "\003ab\n", 5);
	g_frontend_chat_log_used_bytes = 4;
	XVT_ASSERT_INT_EQ(chat_line(PEER_DPID, "\003Wedge: hi"),
			  NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log, "\003ab\n\003Wedge: hi\n", 16), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 15);
}

/* A line this player sent comes back with its first byte, the color code,
 * turned into 5. */
static void check_chat_own_line(void)
{
	lane_world(0);
	hold_chat_log();
	XVT_ASSERT_INT_EQ(chat_line(LOCAL_ID, "\003Luke: yes"),
			  NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log, "\005Luke: yes\n", 12), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 11);
}

/* Past 1,022 bytes the oldest bytes are dropped to make room: the log keeps
 * its newest bytes, the line and its line feed, 1,023 bytes in all. */
static void check_chat_trimmed(void)
{
	char line[31];
	lane_world(0);
	hold_chat_log();
	for (int i = 0; i < 1000; ++i) {
		g_chat_log[i] = (char)('a' + i % 26);
	}
	g_chat_log[1000] = '\0';
	g_frontend_chat_log_used_bytes = 1000;
	memset(line, 'Z', 30);
	line[0] = '\003';
	line[30] = '\0';
	XVT_ASSERT_INT_EQ(chat_line(PEER_DPID, line), NET_PACKET_CHAT);
	/* 1,000 + 30 is 8 past 1,022. */
	XVT_ASSERT_INT_EQ(g_chat_log[0], 'a' + 8);
	XVT_ASSERT_INT_EQ(g_chat_log[991], 'a' + 999 % 26);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log + 992, line, 30), 0);
	XVT_ASSERT_INT_EQ(g_chat_log[1022], '\n');
	XVT_ASSERT_INT_EQ(g_chat_log[1023], '\0');
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 1023);
}

/* With no log a chat line is dropped with a warning, and its type still
 * returned. */
static void check_chat_without_log(void)
{
	lane_world(0);
	XVT_ASSERT_INT_EQ(chat_line(PEER_DPID, "\003Wedge: hi"),
			  NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(count_lines("network.chat_dropped"), 1);
	XVT_ASSERT_TRUE(g_frontend_chat_log_buffer == NULL);
}

/* ------------------------------------------------------------------------ */
/* The packet handler: team and flight assignments. */

/* Handles a TEAM_ASSIGNMENT of the player to the team at the place. */
static int team_assignment(int player_id, int team, int place)
{
	int packet[4] = {NET_PACKET_TEAM_ASSIGNMENT, player_id, team, place};
	return handle_packet(packet, sizeof packet);
}

/* A client whose team 2 lists players 501, 502 and 503, team 3 eight players
 * from 502 on, and team 4 player 502; players 501 and 502 are assigned. This
 * player is in the setup roster, and the datapad sounds are on. */
static void team_world(int host)
{
	lane_world(host);
	g_mp_roster[1].player_id = LOCAL_ID;
	g_game_config.sfx_datapad_enabled = 1;
	int *team2 = g_mission_setup_player_assignments.team_player_ids[2];
	int *team3 = g_mission_setup_player_assignments.team_player_ids[3];
	team2[0] = 501;
	team2[1] = 502;
	team2[2] = 503;
	for (int i = 0; i < 8; ++i) {
		team3[i] = 502 + i;
	}
	g_mission_setup_player_assignments.team_player_ids[4][0] = 502;
	g_mission_setup_player_assignments.assigned_player_ids[0] = 501;
	g_mission_setup_player_assignments.assigned_player_ids[1] = 502;
}

/* A client adds the player of a TEAM_ASSIGNMENT to the assigned players'
 * first free place, and to the team's list at the given place, moving the
 * later ones down. A player already there is not added again. The host
 * ignores the packet. */
static void check_team_assignment_added(void)
{
	static const int added[8] = {501, 300, 502, 503, 0, 0, 0, 0};
	static const int kept[8] = {501, 502, 503, 0, 0, 0, 0, 0};
	struct mission_setup_player_assignments *assignments =
		&g_mission_setup_player_assignments;

	team_world(0);
	XVT_ASSERT_INT_EQ(team_assignment(300, 2, 1),
			  NET_PACKET_TEAM_ASSIGNMENT);
	XVT_ASSERT_INT_EQ(
		memcmp(assignments->team_player_ids[2], added, sizeof added),
		0);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[2], 300);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[3], 0);

	team_world(0);
	XVT_ASSERT_INT_EQ(team_assignment(502, 2, 0),
			  NET_PACKET_TEAM_ASSIGNMENT);
	XVT_ASSERT_INT_EQ(
		memcmp(assignments->team_player_ids[2], kept, sizeof kept), 0);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[2], 0);

	team_world(1);
	XVT_ASSERT_INT_EQ(team_assignment(300, 2, 1),
			  NET_PACKET_TEAM_ASSIGNMENT);
	XVT_ASSERT_INT_EQ(
		memcmp(assignments->team_player_ids[2], kept, sizeof kept), 0);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[2], 0);
	XVT_ASSERT_INT_EQ(line_value("network.team_assignment", "player"), 300);
}

/* Team 10 removes the player instead: its assigned place is emptied, and in
 * every team's list the players after it move up, the last place emptied. */
static void check_team_assignment_removed(void)
{
	static const int team2[8] = {501, 503, 0, 0, 0, 0, 0, 0};
	static const int team3[8] = {503, 504, 505, 506, 507, 508, 509, 0};
	static const int team4[8] = {0};
	struct mission_setup_player_assignments *assignments =
		&g_mission_setup_player_assignments;

	team_world(0);
	XVT_ASSERT_INT_EQ(team_assignment(502, 10, 0),
			  NET_PACKET_TEAM_ASSIGNMENT);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[0], 501);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[1], 0);
	XVT_ASSERT_INT_EQ(
		memcmp(assignments->team_player_ids[2], team2, sizeof team2),
		0);
	XVT_ASSERT_INT_EQ(
		memcmp(assignments->team_player_ids[3], team3, sizeof team3),
		0);
	XVT_ASSERT_INT_EQ(
		memcmp(assignments->team_player_ids[4], team4, sizeof team4),
		0);
}

/* FLIGHT_ASSIGNMENT and FLIGHT_ASSIGNMENT_NOTIFY store word 3 as the flight
 * group of the player at word 1's team and word 2's place; other places keep
 * theirs. */
static void check_flight_assignment(void)
{
	static const int types[2] = {NET_PACKET_FLIGHT_ASSIGNMENT,
				     NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY};
	for (int i = 0; i < 2; ++i) {
		lane_world(0);
		g_game_config.sfx_datapad_enabled = 1;
		g_mission_setup_player_assignments.team_player_ids[3][1] =
			LOCAL_ID;
		for (int j = 0; j < 80; ++j) {
			g_mission_setup_player_flight_group_indices[j] = -1;
		}
		int packet[4] = {types[i], 3, 2, 5};
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  types[i]);
		XVT_ASSERT_INT_EQ(
			g_mission_setup_player_flight_group_indices[3 * 8 + 2],
			5);
		XVT_ASSERT_INT_EQ(
			g_mission_setup_player_flight_group_indices[3 * 8 + 1],
			-1);
		XVT_ASSERT_INT_EQ(
			g_mission_setup_player_flight_group_indices[3 * 8 + 3],
			-1);
	}
}

/* ------------------------------------------------------------------------ */
/* The packet handler: ready flags, the setup's tables and stored words. */

/* PLAYER_READY and PLAYER_UNREADY set and clear the setup roster's ready flag
 * of the sender's entry; for a sender not in the roster nothing changes. */
static void check_ready_flags(void)
{
	lane_world(0);
	g_mp_roster[2].player_id = PEER_DPID;
	int ready[1] = {NET_PACKET_PLAYER_READY};
	int unready[1] = {NET_PACKET_PLAYER_UNREADY};
	XVT_ASSERT_INT_EQ(handle_packet(ready, sizeof ready),
			  NET_PACKET_PLAYER_READY);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[2], 1);
	XVT_ASSERT_INT_EQ(line_value("network.ready_changed", "index"), 2);
	g_mp_roster_ready_flags[0] = 1;
	XVT_ASSERT_INT_EQ(handle_packet(unready, sizeof unready),
			  NET_PACKET_PLAYER_UNREADY);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[2], 0);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[0], 1);

	lane_world(0);
	g_mp_roster_ready_flags[7] = 1;
	XVT_ASSERT_INT_EQ(handle_packet(ready, sizeof ready),
			  NET_PACKET_PLAYER_READY);
	XVT_ASSERT_INT_EQ(line_value("network.ready_changed", "index"), 8);
	XVT_ASSERT_INT_EQ(handle_packet(unready, sizeof unready),
			  NET_PACKET_PLAYER_UNREADY);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[7], 1);
}

/* RETURN_TO_SETUP sets g_frontend_skip_screen_entry_setup; a return to the
 * mission selection is only returned. */
static void check_returns(void)
{
	lane_world(0);
	int setup[1] = {NET_PACKET_RETURN_TO_SETUP};
	XVT_ASSERT_INT_EQ(handle_packet(setup, sizeof setup),
			  NET_PACKET_RETURN_TO_SETUP);
	XVT_ASSERT_INT_EQ(g_frontend_skip_screen_entry_setup, 1);

	lane_world(0);
	int selection[1] = {NET_PACKET_RETURN_TO_MISSION_SELECTION};
	XVT_ASSERT_INT_EQ(handle_packet(selection, sizeof selection),
			  NET_PACKET_RETURN_TO_MISSION_SELECTION);
	XVT_ASSERT_INT_EQ(g_frontend_skip_screen_entry_setup, 0);
}

/* CLEAR_TEAM_ASSIGNMENTS empties every team's list and the assigned players;
 * TEAM_ASSIGNMENTS copies both from the packet, the ten lists first. */
static void check_team_tables(void)
{
	struct mission_setup_player_assignments *assignments =
		&g_mission_setup_player_assignments;
	team_world(0);
	int clear[1] = {NET_PACKET_CLEAR_TEAM_ASSIGNMENTS};
	XVT_ASSERT_INT_EQ(handle_packet(clear, sizeof clear),
			  NET_PACKET_CLEAR_TEAM_ASSIGNMENTS);
	XVT_ASSERT_INT_EQ(assignments->team_player_ids[2][0], 0);
	XVT_ASSERT_INT_EQ(assignments->team_player_ids[3][7], 0);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[0], 0);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[1], 0);

	lane_world(0);
	int packet[1 + 88];
	packet[0] = NET_PACKET_TEAM_ASSIGNMENTS;
	for (int i = 0; i < 88; ++i) {
		packet[1 + i] = 2000 + i;
	}
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_TEAM_ASSIGNMENTS);
	XVT_ASSERT_INT_EQ(assignments->team_player_ids[0][0], 2000);
	XVT_ASSERT_INT_EQ(assignments->team_player_ids[9][7], 2079);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[0], 2080);
	XVT_ASSERT_INT_EQ(assignments->assigned_player_ids[7], 2087);
}

/* CLEAR_FLIGHT_ASSIGNMENTS sets the 8 flight groups of word 1's team to -1;
 * FLIGHT_ASSIGNMENTS copies them from words 2 to 9. Other teams keep
 * theirs. */
static void check_flight_tables(void)
{
	lane_world(0);
	int clear[2] = {NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS, 2};
	XVT_ASSERT_INT_EQ(handle_packet(clear, sizeof clear),
			  NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[15], 0);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[16], -1);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[23], -1);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[24], 0);

	lane_world(0);
	int packet[10] = {NET_PACKET_FLIGHT_ASSIGNMENTS, 2};
	for (int i = 0; i < 8; ++i) {
		packet[2 + i] = 10 + i;
	}
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_FLIGHT_ASSIGNMENTS);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[15], 0);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[16], 10);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[23], 17);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[24], 0);
}

/* Mission choices, countdowns, reservation releases and pilot ratings store
 * word 1 in g_frontend_net_packet_arg0; reservations also store word 2 as the
 * reserving player. */
static void check_stored_words(void)
{
	static const int types[8] = {
		NET_PACKET_MISSION_CHOICE,
		NET_PACKET_BRIEFING_COUNTDOWN,
		NET_PACKET_ASSIGNMENT_COUNTDOWN,
		NET_PACKET_RELEASE_TEAM_RESERVATION,
		NET_PACKET_RELEASE_FLIGHT_RESERVATION,
		NET_PACKET_PILOT_RATING,
		NET_PACKET_TEAM_RESERVATION,
		NET_PACKET_FLIGHT_RESERVATION,
	};
	for (int i = 0; i < 8; ++i) {
		lane_world(0);
		int packet[3] = {types[i], 31 + i, 77};
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  types[i]);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_arg0, 31 + i);
		XVT_ASSERT_INT_EQ(g_frontend_net_reserving_player_id,
				  i >= 6 ? 77 : 0);
	}
}

/* ------------------------------------------------------------------------ */
/* The packet handler: game options and the launch and loadout rosters. */

/* A GAME_OPTIONS packet of 19 words with each word's low byte set to its
 * index and its next byte to 3, and a seed in word 13. */
static void game_options_packet(int *packet)
{
	packet[0] = NET_PACKET_GAME_OPTIONS;
	for (int word = 1; word < 19; ++word) {
		packet[word] = 0x300 + word;
	}
	packet[13] = 0x12345678;
}

/* While this player is in the setup roster GAME_OPTIONS sets the game's
 * options from the low byte of words 1 to 18 (word 6 unused), and the seed
 * from word 13. */
static void check_game_options(void)
{
	int packet[19];
	lane_world(0);
	g_mp_roster[4].player_id = LOCAL_ID;
	game_options_packet(packet);
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_GAME_OPTIONS);
	XVT_ASSERT_INT_EQ(g_game_config.difficulty, 1);
	XVT_ASSERT_INT_EQ(g_game_config.collisions, 2);
	XVT_ASSERT_INT_EQ(g_game_config.craft_jumping, 3);
	XVT_ASSERT_INT_EQ(g_game_config.random_setup, 4);
	XVT_ASSERT_INT_EQ(g_game_config.battle_length_index, 5);
	XVT_ASSERT_INT_EQ(g_game_config.in_progress_join, 7);
	XVT_ASSERT_INT_EQ(g_game_config.craft_selection, 8);
	XVT_ASSERT_INT_EQ(g_game_config.locate_players, 9);
	XVT_ASSERT_INT_EQ(g_game_config.craft_waves, 10);
	XVT_ASSERT_INT_EQ(g_game_config.mission_time_limit, 11);
	XVT_ASSERT_INT_EQ(g_game_config.last_team_time_limit_minutes, 12);
	XVT_ASSERT_INT_EQ(g_game_config.random_seed, 0x12345678);
	XVT_ASSERT_INT_EQ(g_game_config.internet_play, 14);
	XVT_ASSERT_INT_EQ(g_game_config.ai_opponents, 15);
	XVT_ASSERT_INT_EQ(g_game_config.server_update_rate, 16);
	XVT_ASSERT_INT_EQ(g_game_config.combat_balance, 17);
	XVT_ASSERT_INT_EQ(g_game_config.continue_battle_or_campaign, 18);
	XVT_ASSERT_INT_EQ(line_value("network.game_options", "applied"), 1);
}

/* Without this player in the setup roster GAME_OPTIONS changes nothing. */
static void check_game_options_not_in_roster(void)
{
	int packet[19];
	lane_world(0);
	g_mp_roster[4].player_id = PEER_DPID;
	game_options_packet(packet);
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_GAME_OPTIONS);
	XVT_ASSERT_INT_EQ(g_game_config.difficulty, 0);
	XVT_ASSERT_INT_EQ(g_game_config.random_seed, 0);
	XVT_ASSERT_INT_EQ(g_game_config.continue_battle_or_campaign, 0);
	XVT_ASSERT_INT_EQ(line_value("network.game_options", "applied"), 0);
}

/* Fills the five loadout words of each of the 8 roster entries from word 1:
 * entry e's field f is 10 * e + f + 1, except that word 13, which the loadout
 * roster's statistics take as their count, is 0. */
static void loadout_words(int *packet)
{
	for (int entry = 0; entry < 8; ++entry) {
		for (int field = 0; field < 5; ++field) {
			packet[1 + 5 * entry + field] = 10 * entry + field + 1;
		}
	}
	packet[13] = 0;
}

/* Checks that roster entries 0 and 7 hold what loadout_words put. */
static void expect_loadouts(void)
{
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_type_override, 1);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_option_index, 2);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].warhead_option_index, 3);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].beam_option_index, 4);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].countermeasure_option_index, 5);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].warhead_option_index, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[7].craft_type_override, 71);
	XVT_ASSERT_INT_EQ(g_mp_roster[7].countermeasure_option_index, 75);
}

/* LAUNCH_ROSTER_AND_ASSIGNMENTS sets each of the 8 setup roster entries'
 * craft and four loadout options from five words each, then the 80 flight
 * groups from one byte each. */
static void check_launch_roster(void)
{
	int packet[1 + 40 + 20];
	lane_world(0);
	packet[0] = NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS;
	loadout_words(packet);
	for (int i = 0; i < 80; ++i) {
		((uint8_t *)&packet[41])[i] = (uint8_t)(100 + i);
	}
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS);
	expect_loadouts();
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[0], 100);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[79], 179);
}

/* LOADOUT_ROSTER sets the same five fields of each entry. The handler then
 * goes on to LOBBY_SELECTION's statistics, taking word 13, the third entry's
 * warhead option, as their count; it is 0 here, so none are read. */
static void check_loadout_roster(void)
{
	int packet[1 + 40];
	lane_world(0);
	packet[0] = NET_PACKET_LOADOUT_ROSTER;
	loadout_words(packet);
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_LOADOUT_ROSTER);
	expect_loadouts();
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].player_id, 0);
}

/* LOBBY_SELECTION carries, from word 14, six words per player for as many
 * players as word 13 says: id, rating, latency, packets, drops and retries. A
 * client stores the last four as that player's link figures; the host stores
 * none. */
static void check_lobby_selection(void)
{
	for (int host = 0; host < 2; ++host) {
		int packet[14 + 12];
		lane_world(host);
		memset(packet, 0, sizeof packet);
		packet[0] = NET_PACKET_LOBBY_SELECTION;
		packet[13] = 2;
		for (int i = 0; i < 2; ++i) {
			int *entry = &packet[14 + 6 * i];
			entry[0] = i == 0 ? PEER_DPID : UNLISTED_ID;
			entry[1] = 3;
			entry[2] = 40 + i;
			entry[3] = 50 + i;
			entry[4] = 6 + i;
			entry[5] = 8 + i;
		}
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  NET_PACKET_LOBBY_SELECTION);
		if (host) {
			XVT_ASSERT_TRUE(link_figures(PEER_DPID) == NULL);
			XVT_ASSERT_TRUE(link_figures(UNLISTED_ID) == NULL);
			continue;
		}
		const struct net_player_connection_stats *peer =
			link_figures(PEER_DPID);
		const struct net_player_connection_stats *unlisted =
			link_figures(UNLISTED_ID);
		XVT_ASSERT_TRUE(peer != NULL && unlisted != NULL);
		XVT_ASSERT_INT_EQ(peer->latency_total_ms, 40);
		XVT_ASSERT_INT_EQ(peer->packet_count, 50);
		XVT_ASSERT_INT_EQ(peer->packet_drop_count, 6);
		XVT_ASSERT_INT_EQ(peer->packet_retry_count, 8);
		XVT_ASSERT_INT_EQ(unlisted->latency_total_ms, 41);
		XVT_ASSERT_INT_EQ(unlisted->packet_count, 51);
		XVT_ASSERT_INT_EQ(unlisted->packet_drop_count, 7);
		XVT_ASSERT_INT_EQ(unlisted->packet_retry_count, 9);
	}
}

/* ------------------------------------------------------------------------ */
/* The packet handler: craft loadouts, briefing arrivals. */

/* Handles a CRAFT_LOADOUT of the category and craft option, flight group
 * craft option 3, warhead 1, beam 2, countermeasure 1, wave count less one 2
 * and craft count 4. */
static int craft_loadout(int category, int craft_option)
{
	static const int rest[6] = {3, 1, 2, 1, 2, 4};
	int packet[9];
	packet[0] = NET_PACKET_CRAFT_LOADOUT;
	packet[1] = category;
	packet[2] = craft_option;
	memcpy(&packet[3], rest, sizeof rest);
	return handle_packet(packet, sizeof packet);
}

/* A setup roster where player 200 is entry 2 and this player entry 0, each
 * with a craft set, and the selected loadout globals at -5. */
static void loadout_world(void)
{
	lane_world(0);
	g_mp_roster[0].player_id = LOCAL_ID;
	g_mp_roster[0].craft_type_override = 55;
	g_mp_roster[2].player_id = PEER_DPID;
	g_mp_roster[2].craft_type_override = 99;
	g_mission_setup_selected_preset_craft_option_index = -5;
	g_mission_setup_selected_craft_count = -5;
}

/* A CRAFT_LOADOUT sets the sender's setup roster entry: its craft from the
 * preset table, for preset categories 1 and 2 at the option and for category
 * 3 at the option plus 5, else 0, and 0 for option 0; its craft option one
 * less than word 4, and its warhead, beam and countermeasure. Other entries
 * and the selected loadout are kept. */
static void check_craft_loadout(void)
{
	static const struct {
		int category;
		int craft_option;
		int craft_type;
	} cases[] = {
		{1, 4, 4},  {2, 2, 2},	 {3, 2, 7},  {0, 4, -1},
		{4, 4, -1}, {-1, 4, -1}, {1, 0, -1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		loadout_world();
		XVT_ASSERT_INT_EQ(
			craft_loadout(cases[i].category, cases[i].craft_option),
			NET_PACKET_CRAFT_LOADOUT);
		XVT_ASSERT_INT_EQ(
			g_mp_roster[2].craft_type_override,
			cases[i].craft_type < 0
				? 0
				: g_preset_craft_types[cases[i].craft_type]);
		XVT_ASSERT_INT_EQ(g_mp_roster[2].craft_option_index, 2);
		XVT_ASSERT_INT_EQ(g_mp_roster[2].warhead_option_index, 1);
		XVT_ASSERT_INT_EQ(g_mp_roster[2].beam_option_index, 2);
		XVT_ASSERT_INT_EQ(g_mp_roster[2].countermeasure_option_index,
				  1);
		XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_type_override, 55);
		XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_option_index, 0);
		XVT_ASSERT_INT_EQ(
			g_mission_setup_selected_preset_craft_option_index, -5);
	}
}

/* With host-only craft selection every entry is set, whoever sent the
 * packet, and words 2 to 8 become the selected craft, craft option,
 * warhead, beam, countermeasure, wave count less one and craft count. */
static void check_craft_loadout_host_only(void)
{
	loadout_world();
	g_game_config.craft_selection = CRAFT_SELECTION_HOST_ONLY;
	XVT_ASSERT_INT_EQ(craft_loadout(1, 4), NET_PACKET_CRAFT_LOADOUT);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_type_override,
			  g_preset_craft_types[4]);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].craft_type_override,
			  g_preset_craft_types[4]);
	XVT_ASSERT_INT_EQ(g_mp_roster[7].craft_option_index, 2);
	XVT_ASSERT_INT_EQ(g_mission_setup_selected_preset_craft_option_index,
			  4);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_selected_flight_group_craft_option_index, 3);
	XVT_ASSERT_INT_EQ(g_mission_setup_selected_warhead_option_index, 1);
	XVT_ASSERT_INT_EQ(g_mission_setup_selected_beam_option_index, 2);
	XVT_ASSERT_INT_EQ(g_mission_setup_selected_countermeasure_option_index,
			  1);
	XVT_ASSERT_INT_EQ(g_mission_setup_selected_wave_count_minus_one, 2);
	XVT_ASSERT_INT_EQ(g_mission_setup_selected_craft_count, 4);
}

/* Each BRIEFING_ENTERED counts one more player in the briefing. */
static void check_briefing_entered(void)
{
	lane_world(0);
	int packet[1] = {NET_PACKET_BRIEFING_ENTERED};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_BRIEFING_ENTERED);
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_BRIEFING_ENTERED);
	XVT_ASSERT_INT_EQ(g_frontend_briefing_entered_count, 2);
}

/* ------------------------------------------------------------------------ */
/* The packet handler: unavailable players and chat sync requests. */

/* PLAYER_UNAVAILABLE always returns NET_PACKET_NONE. On the host it clears
 * every setup roster ready flag and the sender's lobby ready flag, and sends
 * the lobby state; a client changes nothing. */
static void check_player_unavailable(void)
{
	for (int host = 0; host < 2; ++host) {
		lane_world(host);
		g_game_config.sfx_datapad_enabled = 1;
		g_front_state.net_players[1].ready_flag = 1;
		g_mp_roster_ready_flags[0] = 1;
		g_mp_roster_ready_flags[5] = 1;
		int packet[1] = {NET_PACKET_PLAYER_UNAVAILABLE};
		XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
				  NET_PACKET_NONE);
		XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag,
				  host ? 0 : 1);
		XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[0], host ? 0 : 1);
		XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[5], host ? 0 : 1);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
				  host ? NET_PACKET_GAME_OPTIONS : 0);
	}
}

/* A log of used bytes, byte i being 'a' + i % 26, and a chat sync request
 * for it from player 200. */
static int chat_sync_request(int used)
{
	lane_world(1);
	hold_chat_log();
	for (int i = 0; i < used; ++i) {
		g_chat_log[i] = (char)('a' + i % 26);
	}
	g_frontend_chat_log_used_bytes = used;
	memset(g_frontend_net_packet_scratch.payload, 0x7F,
	       sizeof g_frontend_net_packet_scratch.payload);
	int packet[1] = {NET_PACKET_CHAT_SYNC_REQUEST};
	return handle_packet(packet, sizeof packet);
}

/* A chat sync request is answered with the log in chunks of up to 400
 * bytes, numbered from 0; the last chunk sent is left in the scratch packet,
 * its index in word 1 and its text after it. An empty log is answered with an
 * empty chunk 0. */
static void check_chat_sync_request(void)
{
	static const struct {
		int used;
		int chunks;
		int last_start;
	} cases[] = {{900, 3, 800}, {800, 2, 400}, {401, 2, 400}, {1, 1, 0}};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		XVT_ASSERT_INT_EQ(chat_sync_request(cases[i].used),
				  NET_PACKET_CHAT_SYNC_REQUEST);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
				  NET_PACKET_CHAT_SYNC_CHUNK);
		XVT_ASSERT_INT_EQ(scratch_word(0), cases[i].chunks - 1);
		int length = cases[i].used - cases[i].last_start;
		XVT_ASSERT_INT_EQ(
			memcmp(g_frontend_net_packet_scratch.payload + 4,
			       g_chat_log + cases[i].last_start,
			       (size_t)length),
			0);
		XVT_ASSERT_INT_EQ(
			line_value("network.chat_sync_sent", "chunks"),
			cases[i].chunks);
	}

	XVT_ASSERT_INT_EQ(chat_sync_request(0), NET_PACKET_CHAT_SYNC_REQUEST);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_CHAT_SYNC_CHUNK);
	XVT_ASSERT_INT_EQ(scratch_word(0), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.payload[4], 0x7F);
}

/* With no log a chat sync request is dropped with a warning: nothing is
 * sent. */
static void check_chat_sync_without_log(void)
{
	lane_world(1);
	int packet[1] = {NET_PACKET_CHAT_SYNC_REQUEST};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_CHAT_SYNC_REQUEST);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.chat_dropped"), 1);
}

/* ------------------------------------------------------------------------ */
/* The packet handler: battle progress, movie sync and mission choices. */

/* BATTLE_PROGRESS stores its five words in the g_remote_battle globals. */
static void check_battle_progress(void)
{
	lane_world(0);
	int packet[6] = {NET_PACKET_BATTLE_PROGRESS, 1, 2, 3, 4, 5};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_BATTLE_PROGRESS);
	XVT_ASSERT_INT_EQ(g_remote_battle_continuation_active, 1);
	XVT_ASSERT_INT_EQ(g_remote_battle_sequence_continuation_choice, 2);
	XVT_ASSERT_INT_EQ(g_remote_battle_last_completed_mission_index, 3);
	XVT_ASSERT_INT_EQ(g_remote_battle_rebel_victory_count, 4);
	XVT_ASSERT_INT_EQ(g_remote_battle_imperial_victory_count, 5);
}

/* The movie's players: this player, player 200 twice, and two entries of
 * player 300, none waiting. */
static void movie_world(void)
{
	static const int players[5] = {LOCAL_ID, PEER_DPID, PEER_DPID,
				       UNLISTED_ID, UNLISTED_ID};
	lane_world(0);
	for (int i = 0; i < 5; ++i) {
		g_movie_multiplayer_sync_players[i].player_id = players[i];
	}
}

/* Handles a MOVIE_SYNC of the op and target from player 200. */
static int movie_sync(int op, int target)
{
	int packet[3] = {NET_PACKET_MOVIE_SYNC, op, target};
	return handle_packet(packet, sizeof packet);
}

/* MOVIE_SYNC op 0 marks the sender's first entry among the movie's players
 * waiting, op 1 every entry, and op 2 empties the first entry of the player
 * in word 2. Other ops change nothing. */
static void check_movie_sync(void)
{
	movie_world();
	XVT_ASSERT_INT_EQ(movie_sync(0, 0), NET_PACKET_MOVIE_SYNC);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[0].is_waiting, 0);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[1].is_waiting, 1);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[2].is_waiting, 0);

	movie_world();
	XVT_ASSERT_INT_EQ(movie_sync(1, 0), NET_PACKET_MOVIE_SYNC);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(
			g_movie_multiplayer_sync_players[i].is_waiting, 1);
	}

	movie_world();
	XVT_ASSERT_INT_EQ(movie_sync(2, UNLISTED_ID), NET_PACKET_MOVIE_SYNC);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[3].player_id, 0);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[4].player_id,
			  UNLISTED_ID);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[1].player_id,
			  PEER_DPID);
	XVT_ASSERT_INT_EQ(line_value("network.movie_sync", "target"),
			  UNLISTED_ID);

	movie_world();
	XVT_ASSERT_INT_EQ(movie_sync(3, PEER_DPID), NET_PACKET_MOVIE_SYNC);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[1].player_id,
			  PEER_DPID);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[1].is_waiting, 0);
}

/* A SUBMIT_MISSION_CHOICE is relayed to everyone as a MISSION_CHOICE of the
 * same choice. */
static void check_mission_choice_relayed(void)
{
	lane_world(1);
	int packet[2] = {NET_PACKET_SUBMIT_MISSION_CHOICE, 6};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_SUBMIT_MISSION_CHOICE);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_MISSION_CHOICE);
	XVT_ASSERT_INT_EQ(scratch_word(0), 6);
}

/* ------------------------------------------------------------------------ */
/* The screens: the world they run in. */

/* A fresh frontend display with the test's string table, and this player a
 * client of player 200's lobby, as lane_world leaves it, waiting on the
 * admission screen, which is on top of the stack. No session, no dialog, an
 * empty 1,024-byte chat log and chat line, no key typed, no log lines kept,
 * and the scratch packet's payload filled with 0x7F. */
static void screen_world(void)
{
	xvt_dialog_shutdown();
	frontend_string_unload_table();
	xvt_test_close_display();
	xvt_network_session_shutdown();
	lane_world(0);
	xvt_test_open_display();
	frontend_string_load_table("strings.txt");
	g_front_state.screen_states[0].update_fn =
		frontend_net_await_join_admission_screen;
	hold_chat_log();
	g_chat_log[0] = '\0';
	memset(g_frontend_chat_input_buffer, 0,
	       sizeof g_frontend_chat_input_buffer);
	g_frontend_chat_team_only = 0;
	g_frontend_chat_scroll_offset = 0;
	g_frontend_scratch_buffer[0] = '\0';
	g_host_cd_available = 0;
	memset(g_frontend_net_packet_scratch.payload, 0x7F,
	       sizeof g_frontend_net_packet_scratch.payload);
	g_line_count = 0;
}

/* Closes the last screen world's dialog, session, strings and display. */
static void close_screen_world(void)
{
	xvt_dialog_shutdown();
	xvt_network_session_shutdown();
	frontend_string_unload_table();
	xvt_test_close_display();
}

/* The update function of the screen on top of the stack. */
static frontend_screen_update_fn top_screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

/* Puts the cursor at (x, y) and clicks there in the next frame. */
static void click(int x, int y)
{
	g_front_state.mouse_x = x;
	g_front_state.mouse_y = y;
	g_front_state.mouse_left_click_latch = 1;
}

/* Types the text, as the keyboard would, for the next frame to read. */
static void type_text(const char *text)
{
	for (const char *c = text; *c != '\0'; ++c) {
		g_front_state.char_ring_buffer[g_front_state.char_write_idx++] =
			*c;
	}
}

/* Runs one frame of the admission screen and returns its result; a click
 * lasts that frame only. */
static int admission_frame(int frame_counter)
{
	int result = frontend_net_await_join_admission_screen(frame_counter);
	g_front_state.mouse_left_click_latch = 0;
	return result;
}

/* The state of the network session. */
static xvt_network_session_state session_state(void)
{
	return xvt_network_session_get_status().state;
}

/* ------------------------------------------------------------------------ */
/* The admission screen: frame 0, the failure report and the packet read. */

/* The first frame opens the screen's background image, which the folder lacks,
 * whether or not the host CD is in; later frames do not. With no packet the
 * screen stays, with no dialog, and returns 0. */
static void check_admission_first_frame(void)
{
	for (int cd = 0; cd < 2; ++cd) {
		screen_world();
		g_host_cd_available = cd;
		XVT_ASSERT_INT_EQ(admission_frame(0), 0);
		XVT_ASSERT_INT_EQ(count_lines("image.open_failed"), 1);
		XVT_ASSERT_INT_EQ(admission_frame(1), 0);
		XVT_ASSERT_INT_EQ(count_lines("image.open_failed"), 1);
		XVT_ASSERT_TRUE(top_screen() ==
				frontend_net_await_join_admission_screen);
		XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
		XVT_ASSERT_INT_EQ(session_state(), XVT_NETWORK_SESSION_IDLE);
	}
}

/* While the session has failed, a frame reports it in a dialog and returns 0
 * at once: the host's refusal is not read, and the version text is not
 * written. */
static void check_admission_failure_reported(void)
{
	screen_world();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host(NULL, "Luke", "", 0),
			  0);
	XVT_ASSERT_INT_EQ(session_state(), XVT_NETWORK_SESSION_FAILED);
	int packet[1] = {NET_PACKET_GAME_FULL};
	queue_from(PEER_DPID, packet, sizeof packet);
	XVT_ASSERT_INT_EQ(admission_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_sender_player_id, 0);
	XVT_ASSERT_INT_EQ(strcmp(g_frontend_scratch_buffer, ""), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(count_lines("network.refusal_received"), 0);
}

/* A packet not sent by the host is ignored, and logged: a refusal from
 * another player opens no dialog and leaves the session alone. A frame with
 * no packet logs nothing, whoever sent the last one. */
static void check_admission_other_sender(void)
{
	screen_world();
	int packet[1] = {NET_PACKET_GAME_FULL};
	queue_from(LOCAL_ID, packet, sizeof packet);
	XVT_ASSERT_INT_EQ(admission_frame(1), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(session_state(), XVT_NETWORK_SESSION_IDLE);
	XVT_ASSERT_INT_EQ(line_value("network.non_host_packet_ignored", "type"),
			  NET_PACKET_GAME_FULL);
	XVT_ASSERT_INT_EQ(admission_frame(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.non_host_packet_ignored"), 1);
}

/* ------------------------------------------------------------------------ */
/* The admission screen: the six refusals. */

/* Each refusal from the host leaves the session rejected and shows its
 * message in a dialog, the screen staying until the dialog ends, and returns
 * 0. A game already started and a locked roster share their message. */
static void check_admission_refusals(void)
{
	static const struct {
		int type;
		const char *first_line;
		const char *reason;
	} refusals[] = {
		{NET_PACKET_FRONTEND_GAME_STARTED, "s552", "started"},
		{NET_PACKET_ROSTER_LOCKED, "s552", "locked"},
		{NET_PACKET_GAME_FULL, "s543", "full"},
		{NET_PACKET_HOST_CANCELLED, "s631", "host_cancelled"},
		{NET_PACKET_VERSION_MISMATCH, "s546", "version"},
		{NET_PACKET_PASSWORD_REQUIRED, "s549", "password"},
	};
	for (size_t i = 0; i < sizeof refusals / sizeof refusals[0]; ++i) {
		char event[64];
		screen_world();
		g_game_config.network_type = 1;
		int packet[4] = {refusals[i].type, 0, 0, 0};
		queue_from(PEER_DPID, packet, sizeof packet);
		XVT_ASSERT_INT_EQ(admission_frame(1), 0);
		XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
		XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_line1_or_edit,
					 refusals[i].first_line),
				  0);
		XVT_ASSERT_INT_EQ(session_state(), XVT_NETWORK_SESSION_PENDING);
		XVT_ASSERT_INT_EQ(count_lines("network.join_rejected"), 1);
		snprintf(event, sizeof event,
			 "network.refusal_received reason=\"%s\"",
			 refusals[i].reason);
		XVT_ASSERT_INT_EQ(count_lines(event), 1);
		XVT_ASSERT_TRUE(top_screen() ==
				frontend_net_await_join_admission_screen);
		XVT_ASSERT_INT_EQ(g_frontend_skip_screen_entry_setup, 0);
	}
}

/* ------------------------------------------------------------------------ */
/* The admission screen: admitted. */

/* The host's PLAYER_ADMITTED clears g_mp_roster and moves to the mission
 * setup screen. */
static void check_admission_admitted(void)
{
	screen_world();
	g_mp_roster[2].player_id = 77;
	g_mp_roster[7].pilot_rating = 3;
	int packet[2] = {NET_PACKET_PLAYER_ADMITTED, LOCAL_ID};
	queue_from(PEER_DPID, packet, sizeof packet);
	XVT_ASSERT_INT_EQ(admission_frame(1), 0);
	XVT_ASSERT_TRUE(top_screen() == mission_setup_update);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].player_id, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[7].pilot_rating, 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
}

/* ------------------------------------------------------------------------ */
/* The admission screen: the top bar, the dialog check and Cancel. */

/* Cancel ends the connection attempt and returns to the join screen with its
 * entry setup skipped; the frame returns 0. A click elsewhere does not. The
 * pilot's banner and the help text are drawn on the way, which show
 * nothing here. */
static void check_admission_cancel(void)
{
	screen_world();
	g_game_config.help_on = 1;
	strcpy(g_pilot_data.name, "Luke");
	strcpy(g_pilot_data.rating_name, "Ace");
	click(300, 300);
	XVT_ASSERT_INT_EQ(admission_frame(1), 0);
	XVT_ASSERT_TRUE(top_screen() ==
			frontend_net_await_join_admission_screen);
	XVT_ASSERT_INT_EQ(count_lines("network.join_cancelled"), 0);

	click(CANCEL_X, CANCEL_Y);
	XVT_ASSERT_INT_EQ(admission_frame(2), 0);
	XVT_ASSERT_TRUE(top_screen() == frontend_net_join_game_screen);
	XVT_ASSERT_INT_EQ(g_frontend_skip_screen_entry_setup, 1);
	XVT_ASSERT_INT_EQ(count_lines("network.join_cancelled"), 1);
}

/* Esc opens the quit dialog through the top bar; with a dialog open the
 * frame returns 0 before Cancel, so a click on it does nothing. */
static void check_admission_dialog_open(void)
{
	screen_world();
	type_text("\033");
	click(CANCEL_X, CANCEL_Y);
	XVT_ASSERT_INT_EQ(admission_frame(1), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_line1_or_edit, "s634"), 0);
	XVT_ASSERT_TRUE(top_screen() ==
			frontend_net_await_join_admission_screen);
	XVT_ASSERT_INT_EQ(count_lines("network.join_cancelled"), 0);
}

/* ------------------------------------------------------------------------ */
/* The chat panel: frame 0 and the All and Team tabs. */

/* Runs one frame of the chat panel and returns its result; clicks last that
 * frame only. */
static int chat_frame(int frame_counter)
{
	int result = frontend_net_update_and_draw_chat_panel(frame_counter);
	g_front_state.mouse_left_click_latch = 0;
	g_front_state.mouse_right_click_latch = 0;
	return result;
}

/* Frame 0 drops the keys typed before it and scrolls the log back to its
 * newest line; later frames take typed keys into the chat line. */
static void check_chat_first_frame(void)
{
	screen_world();
	g_frontend_chat_scroll_offset = 7;
	type_text("x");
	XVT_ASSERT_INT_EQ(chat_frame(0), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_scroll_offset, 0);
	XVT_ASSERT_INT_EQ(strcmp(g_frontend_chat_input_buffer, ""), 0);
	type_text("y");
	g_frontend_chat_scroll_offset = 7;
	XVT_ASSERT_INT_EQ(chat_frame(1), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_frontend_chat_input_buffer, "y"), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_scroll_offset, 7);
}

/* Before the setup roster is authoritative there are no tabs, and chat goes
 * to everyone: g_frontend_chat_team_only is held at 0, even on a click where
 * the Team tab would be. */
static void check_chat_tabs_hidden(void)
{
	screen_world();
	g_frontend_chat_team_only = 1;
	click(TEAM_TAB_X, TAB_Y);
	XVT_ASSERT_INT_EQ(chat_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 0);
}

/* Once the roster is authoritative, a left or right click on the Team tab
 * sends chat to the team only, and one on the All tab to everyone; clicks
 * on the tab already chosen, or elsewhere, change nothing. */
static void check_chat_tabs(void)
{
	screen_world();
	g_mission_setup_roster_authoritative = 1;
	g_game_config.sfx_datapad_enabled = 1;
	click(ALL_TAB_X, TAB_Y);
	XVT_ASSERT_INT_EQ(chat_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 0);
	click(TEAM_TAB_X, TAB_Y);
	XVT_ASSERT_INT_EQ(chat_frame(2), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 1);
	XVT_ASSERT_INT_EQ(line_value("network.chat_channel", "team_only"), 1);
	click(TEAM_TAB_X, TAB_Y);
	XVT_ASSERT_INT_EQ(chat_frame(3), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 1);
	click(300, TAB_Y);
	XVT_ASSERT_INT_EQ(chat_frame(4), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 1);
	click(ALL_TAB_X, TAB_Y);
	XVT_ASSERT_INT_EQ(chat_frame(5), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 0);
	XVT_ASSERT_INT_EQ(line_value("network.chat_channel", "team_only"), 0);

	g_front_state.mouse_x = TEAM_TAB_X;
	g_front_state.mouse_y = TAB_Y;
	g_front_state.mouse_right_click_latch = 1;
	XVT_ASSERT_INT_EQ(chat_frame(6), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 1);
	g_front_state.mouse_x = ALL_TAB_X;
	g_front_state.mouse_right_click_latch = 1;
	XVT_ASSERT_INT_EQ(chat_frame(7), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_team_only, 0);
	g_game_config.sfx_datapad_enabled = 0;
	g_mission_setup_roster_authoritative = 0;
}

/* ------------------------------------------------------------------------ */
/* The chat panel: sending a line. */

/* Types the chat line and the key that sends it, runs a frame, and checks
 * the CHAT packet left in the scratch packet: the color code, "Luke: ",
 * color code 1 and the text, with no closing NUL, and the chat line
 * cleared. */
static void send_chat(const char *key, int color_code)
{
	char expected[64];
	strcpy(g_pilot_data.name, "Luke");
	strcpy(g_frontend_chat_input_buffer, "hello");
	type_text(key);
	XVT_ASSERT_INT_EQ(chat_frame(1), 0);
	snprintf(expected, sizeof expected, "%cLuke: %chello", color_code, 1);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(memcmp(g_frontend_net_packet_scratch.payload,
				 expected, strlen(expected)),
			  0);
	XVT_ASSERT_INT_EQ(
		g_frontend_net_packet_scratch.payload[strlen(expected)], 0x7F);
	XVT_ASSERT_INT_EQ(strcmp(g_frontend_chat_input_buffer, ""), 0);
}

/* Enter sends the chat line to everyone, marked with color code 3 when this
 * player is ready and 4 when not; Tab sends it too. */
static void check_chat_sent_to_everyone(void)
{
	screen_world();
	g_front_state.net_players[0].ready_flag = 1;
	send_chat("\r", 3);
	XVT_ASSERT_INT_EQ(count_lines("network.chat_sent to=\"everyone\""), 1);
	XVT_ASSERT_INT_EQ(line_value("network.chat_sent", "ready"), 1);

	screen_world();
	send_chat("\t", 4);
	XVT_ASSERT_INT_EQ(count_lines("network.chat_sent to=\"everyone\""), 1);
	XVT_ASSERT_INT_EQ(line_value("network.chat_sent", "ready"), 0);
}

/* A team message, color code 2 when ready and 4 when not, goes to the
 * players of this pilot's team, or to everyone when this player has no team
 * assignment. */
static void check_chat_sent_to_team(void)
{
	for (int ready = 0; ready < 2; ++ready) {
		screen_world();
		g_mission_setup_roster_authoritative = 1;
		g_frontend_chat_team_only = 1;
		g_front_state.net_players[0].ready_flag = ready;
		g_pilot_data.team = 3;
		g_mission_setup_player_assignments.assigned_player_ids[5] =
			LOCAL_ID;
		g_mission_setup_player_assignments.team_player_ids[3][0] =
			LOCAL_ID;
		g_mission_setup_player_assignments.team_player_ids[3][2] =
			PEER_DPID;
		send_chat("\r", ready ? 2 : 4);
		XVT_ASSERT_INT_EQ(count_lines("network.chat_sent to=\"team\""),
				  1);
		XVT_ASSERT_INT_EQ(line_value("network.chat_sent", "team"), 3);
		XVT_ASSERT_INT_EQ(line_value("network.chat_sent", "ready"),
				  ready);
	}

	screen_world();
	g_mission_setup_roster_authoritative = 1;
	g_frontend_chat_team_only = 1;
	g_front_state.net_players[0].ready_flag = 1;
	send_chat("\r", 2);
	XVT_ASSERT_INT_EQ(
		count_lines("network.chat_sent to=\"everyone_no_team\""), 1);
	g_mission_setup_roster_authoritative = 0;
}

/* Enter with an empty chat line sends nothing. */
static void check_chat_empty_line(void)
{
	screen_world();
	type_text("\r");
	XVT_ASSERT_INT_EQ(chat_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.chat_sent"), 0);
}

/* ------------------------------------------------------------------------ */
/* The chat panel: the log and its scrollbar. */

static struct bitmap_font g_font;
static uint8_t g_font_bits[16];

/* Gives the frontend a size-12 font whose every glyph is one row of 6
 * pixels, its bits a 4-byte row length then the end-of-row code, and locks
 * the back buffer for drawing. */
static void hold_font(void)
{
	memset(&g_font, 0, sizeof g_font);
	memset(g_font_bits, 0, sizeof g_font_bits);
	g_font_bits[0] = 5;
	g_font_bits[4] = 0x80;
	g_font.p_glyph_bits = g_font_bits;
	memset(g_font.glyph_height, 1, sizeof g_font.glyph_height);
	memset(g_font.glyph_width, 6, sizeof g_font.glyph_width);
	g_font.point_size = 12;
	g_front_state.font_by_size[12] = &g_font;
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
}

/* Unlocks the back buffer and drops the font. */
static void drop_font(void)
{
	frontend_display_unlock_back_buffer();
	g_front_state.font_by_size[12] = NULL;
}

/* Puts lines of "line" in the chat log. */
static void fill_chat_log(int lines)
{
	g_chat_log[0] = '\0';
	for (int i = 0; i < lines; ++i) {
		strcat(g_chat_log, "line\n");
	}
	g_frontend_chat_log_used_bytes = (int)strlen(g_chat_log);
}

/* The panel returns the log's line count. Past 20 lines the log scrolls: an
 * offset inside the scrollbar's range is kept, and one at its end, the line
 * count less 19, steps back by one. Up to 20 lines there is no scrollbar,
 * and the offset is left as it is. */
static void check_chat_log_scroll(void)
{
	static const struct {
		int lines;
		int offset;
		int kept;
	} cases[] = {
		{40, 0, 0}, {40, 5, 5}, {40, 20, 20}, {40, 21, 20},
		{21, 2, 1}, {21, 1, 1}, {20, 3, 3},   {10, 1, 1},
	};
	screen_world();
	hold_font();
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fill_chat_log(cases[i].lines);
		g_frontend_chat_scroll_offset = cases[i].offset;
		XVT_ASSERT_INT_EQ(chat_frame(1), cases[i].lines);
		XVT_ASSERT_INT_EQ(g_frontend_chat_scroll_offset, cases[i].kept);
	}
	drop_font();
}

/* Without the log's font the panel counts no lines and returns 0. */
static void check_chat_log_without_font(void)
{
	screen_world();
	fill_chat_log(40);
	g_frontend_chat_scroll_offset = 21;
	XVT_ASSERT_INT_EQ(chat_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_scroll_offset, 21);
}

/* ------------------------------------------------------------------------ */
/* Known failures. */

/* Known failure join_refused_as_full, issue #111: the comment says a join
 * request is refused as full when 8 players are admitted. None is; player 200
 * passes every check but is not in the host's player list, so admitting it
 * fails, and the host answers GAME_FULL anyway. */
static void check_join_not_full(void)
{
	lobby_world(1);
	join_request(FRONTEND_NET_PROTOCOL_VERSION, "");
	XVT_ASSERT_TRUE(g_frontend_net_packet_scratch.packet_type !=
			NET_PACKET_GAME_FULL);
}

/* Known failure chat_chunk_without_log, issue #22: the comment states the
 * handler does not check the chat log for NULL before a sync chunk. With no
 * log held, chunk 0 clears a NULL log and the program stops; a chunk should
 * be dropped when there is no log to hold it. */
static void check_chat_chunk_without_log(void)
{
	lobby_world(0);
	g_frontend_chat_log_buffer = NULL;
	chat_chunk(0, "Hi", 2);
	XVT_ASSERT_TRUE(g_frontend_chat_log_buffer == NULL);
}

/* Known failure chat_chunk_past_log, issue #22: the comment states the
 * handler does not check chunk indices. Chunk 3 of a 1,024-byte log is copied
 * 1,200 bytes in, past its end, and the sanitizer stops the program on the
 * write; nothing past the log should be written. */
static void check_chat_chunk_past_log(void)
{
	char text[400];
	memset(text, 'a', sizeof text);
	lobby_world(0);
	hold_chat_log();
	chat_chunk(3, text, sizeof text);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 0);
}

/* Known failure craft_option_past_table, issue #109: the handler does not
 * check a CRAFT_LOADOUT's craft option against the 11-entry preset table.
 * Option 11 of category 1 is read past the table's end, and the sanitizer
 * stops the program on the read; an option outside the table should choose
 * no craft. */
static void check_craft_option_in_table(void)
{
	loadout_world();
	craft_loadout(1, 11);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].craft_type_override, 0);
}

/* Known failure statistics_past_packet, issue #110: the handler does not
 * check a LOBBY_SELECTION's statistics count against the packet. A count of
 * 20 with the 19 entries a packet has room for makes a client read a
 * twentieth entry past the packet copy's 512 bytes, from the lobby's first
 * peer slot, and store link figures for the player id it finds there, this
 * player's; only the 19 players the packet names should get figures. */
static void check_statistics_in_packet(void)
{
	int packet[128];
	lane_world(0);
	memset(packet, 0, sizeof packet);
	packet[0] = NET_PACKET_LOBBY_SELECTION;
	packet[13] = 20;
	for (int i = 0; i < 19; ++i) {
		packet[14 + 6 * i] = 1000 + i;
		packet[16 + 6 * i] = 30 + i;
	}
	handle_packet(packet, sizeof packet);
	XVT_ASSERT_TRUE(link_figures(1018) != NULL);
	XVT_ASSERT_TRUE(link_figures(LOCAL_ID) == NULL);
}

int main(int argc, char **argv)
{
	fail_after_seconds(60);
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	write_assets();
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"join_refused_as_full", check_join_not_full},
			{"chat_chunk_without_log",
			 check_chat_chunk_without_log},
			{"chat_chunk_past_log", check_chat_chunk_past_log},
			{"craft_option_past_table",
			 check_craft_option_in_table},
			{"statistics_past_packet", check_statistics_in_packet},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		free(g_chat_log);
		xvt_test_close_assets(&g_assets);
		return 0;
	}
	check_no_packet();
	check_join_refusals();
	check_join_dropped();
	check_chat_chunks();

	check_ready_player_left();
	check_game_status();
	check_probe_request();
	check_lobby_state();
	check_ready_roster();
	check_notices_returned();
	check_player_admitted();
	check_admission_refused();
	check_player_left();
	check_team_assignments_ready();
	check_mission_start_sequence();
	check_mission_start_single();
	check_seeds();
	check_chat_appended();
	check_chat_own_line();
	check_chat_trimmed();
	check_chat_without_log();
	check_team_assignment_added();
	check_team_assignment_removed();
	check_flight_assignment();
	check_ready_flags();
	check_returns();
	check_team_tables();
	check_flight_tables();
	check_stored_words();
	check_game_options();
	check_game_options_not_in_roster();
	check_launch_roster();
	check_loadout_roster();
	check_lobby_selection();
	check_craft_loadout();
	check_craft_loadout_host_only();
	check_briefing_entered();
	check_player_unavailable();
	check_chat_sync_request();
	check_chat_sync_without_log();
	check_battle_progress();
	check_movie_sync();
	check_mission_choice_relayed();

	check_admission_first_frame();
	check_admission_failure_reported();
	check_admission_other_sender();
	check_admission_refusals();
	check_admission_admitted();
	check_admission_cancel();
	check_admission_dialog_open();
	check_chat_first_frame();
	check_chat_tabs_hidden();
	check_chat_tabs();
	check_chat_sent_to_everyone();
	check_chat_sent_to_team();
	check_chat_empty_line();
	check_chat_log_scroll();
	check_chat_log_without_font();
	close_screen_world();

	free(g_chat_log);
	g_frontend_chat_log_buffer = NULL;
	xvt_test_close_assets(&g_assets);
	return 0;
}

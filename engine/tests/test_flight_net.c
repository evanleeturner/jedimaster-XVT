/* Tests for xvt/net/flight_net.c, the original flight networking's packet
 * builders and the pilot record's list of network players. Each check sets the
 * game state it needs itself: the player records, the pilot record's network
 * players, the network session's ids and the input clocks. No game data is
 * read and no DirectPlay session is open, so every packet sent stays on this
 * machine: the checks read the packet each function builds in
 * g_flight_net_scratch_packet.
 *
 * Not checked here: the functions that only hand off to the newer flight
 * network code (the option exchange, the mission start wait and the packet
 * loop), which test_flight_network.c and test_flight_packets.c cover, and
 * flight_net_sample_local_input, which reads the game's input devices. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"

enum { HOST_DPID = 500 };

/* Player n has DirectPlay id 100 + n, and the pilot record lists the eight
 * players in reverse: entry e holds the player with id 107 - e. The local
 * player is 0, the host's id is 500, and no packet is waiting. */
static void flight_net_world(void)
{
	memset(g_players, 0, sizeof g_players);
	memset(g_pilot_data.network_players, 0,
	       sizeof g_pilot_data.network_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].network.direct_play_id = 100 + i;
		g_pilot_data.network_players[i].direct_play_id = 107 - i;
	}
	g_local_player = 0;
	memset(&g_net_session, 0, sizeof g_net_session);
	g_net_session.host_dplay_id = HOST_DPID;
	g_net_recv_queue_count = 0;
	g_net_recv_queue_read_index = 0;
	g_net_recv_queue_write_index = 0;
	memset(&g_flight_net_scratch_packet, 0,
	       sizeof g_flight_net_scratch_packet);
}

/* ------------------------------------------------------------------------ */
/* The pilot record's network players. */

/* Every player is found at the entry that holds its DirectPlay id. */
static void check_find_pilot_entry(void)
{
	flight_net_world();
	for (int player = 0; player < 8; ++player) {
		XVT_ASSERT_INT_EQ(
			flight_net_find_pilot_network_player_index(player),
			7 - player);
	}
}

/* A player whose id no entry holds gives 0. The same id is put just past the
 * table, where a ninth entry's id would be, so that a search running past the
 * eighth entry would find it there. */
static void check_find_pilot_entry_missing(void)
{
	int missing = 999;
	uint8_t *past_table =
		(uint8_t *)&g_pilot_data.network_players[8] +
		offsetof(struct pilot_network_player, direct_play_id);
	uint8_t saved[sizeof missing];
	flight_net_world();
	memcpy(saved, past_table, sizeof saved);
	memcpy(past_table, &missing, sizeof missing);
	g_players[2].network.direct_play_id = missing;
	XVT_ASSERT_INT_EQ(flight_net_find_pilot_network_player_index(2), 0);
	memcpy(past_table, saved, sizeof saved);
}

/* Player 3 leaves: its entry, 4, is marked, and no other entry is. */
static void check_mark_player_left(void)
{
	flight_net_world();
	flight_net_mark_pilot_network_player_left(3);
	for (int entry = 0; entry < 8; ++entry) {
		XVT_ASSERT_INT_EQ(g_pilot_data.network_players[entry].has_left,
				  entry == 4);
	}
}

/* ------------------------------------------------------------------------ */
/* The packets this player sends. */

/* The probe holds the adjusted input time, the input clock plus the steering
 * total, and this player's clock lead; the adjusted time is kept to match the
 * reply. */
static void check_clock_probe(void)
{
	flight_net_world();
	g_input_timestamp = 100;
	g_flight_net_clock_adjust_accum_ticks = 7;
	g_flight_net_clock_lead_ticks = 12;
	g_flight_net_clock_probe_timestamp = 0;
	XVT_ASSERT_INT_EQ(flight_net_send_clock_probe_to_host(), 1);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.packet_type,
			  NET_PACKET_CLOCK_PROBE);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.payload_dwords[0],
			  g_input_timestamp +
				  g_flight_net_clock_adjust_accum_ticks);
	XVT_ASSERT_INT_EQ(g_flight_net_clock_probe_timestamp,
			  g_flight_net_scratch_packet.payload_dwords[0]);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.payload_dwords[1],
			  g_flight_net_clock_lead_ticks);
	g_flight_net_clock_lead_ticks = 0;
}

/* The loading pulse, the session abort and the player abort each build their
 * own packet type; the player abort names the slot that leaves. */
static void check_notices(void)
{
	flight_net_world();
	XVT_ASSERT_INT_EQ(flight_net_broadcast_still_loading_pulse(), 1);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.packet_type,
			  NET_PACKET_STILL_LOADING);

	flight_net_world();
	flight_net_broadcast_host_session_abort();
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.packet_type,
			  NET_PACKET_SESSION_ABORT);

	flight_net_world();
	flight_net_broadcast_player_abort(5);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.packet_type,
			  NET_PACKET_PLAYER_ABORT);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.payload_dwords[0], 5);
}

/* The host's world-message schedule restarts from 0. */
static void check_reset_world_message_schedule(void)
{
	g_flight_net_world_message_turn_timestamp = 64;
	g_flight_net_last_sent_world_message_timestamp = 56;
	flight_net_reset_world_message_schedule();
	XVT_ASSERT_INT_EQ(g_flight_net_world_message_turn_timestamp, 0);
	XVT_ASSERT_INT_EQ(g_flight_net_last_sent_world_message_timestamp, 0);
}

/* Fills three checksum words and three region lengths, all different. */
static void checksum_words(int *checksum, int *lengths)
{
	for (int i = 0; i < 3; ++i) {
		checksum[i] = 1000 + i;
		lengths[i] = 2000 + i;
	}
}

/* To the host: the tick, the checksum words, the region lengths, then a zero
 * word, which asks for no resend. */
static void check_world_checksum_to_host(void)
{
	int checksum[3];
	int lengths[3];
	flight_net_world();
	checksum_words(checksum, lengths);
	g_server_tick_time = 4321;
	g_flight_net_scratch_packet.payload_dwords[7] = 1;
	flight_net_send_world_checksum_to_host(checksum, lengths, 3);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.packet_type,
			  NET_PACKET_WORLD_CHECKSUM);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.payload_dwords[0],
			  g_server_tick_time);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(
			g_flight_net_scratch_packet.payload_dwords[1 + i],
			checksum[i]);
		XVT_ASSERT_INT_EQ(
			g_flight_net_scratch_packet.payload_dwords[4 + i],
			lengths[i]);
	}
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.payload_dwords[7], 0);
	g_server_tick_time = 0;
}

/* To every player: the tick, the checksum words, then the region lengths. */
static void check_world_checksum_to_all(void)
{
	int checksum[3];
	int lengths[3];
	flight_net_world();
	checksum_words(checksum, lengths);
	g_server_tick_time = 1234;
	flight_net_broadcast_world_checksum(checksum, lengths, 3);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.packet_type,
			  NET_PACKET_SERVER_CHECKSUM);
	XVT_ASSERT_INT_EQ(g_flight_net_scratch_packet.payload_dwords[0],
			  g_server_tick_time);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(
			g_flight_net_scratch_packet.payload_dwords[1 + i],
			checksum[i]);
		XVT_ASSERT_INT_EQ(
			g_flight_net_scratch_packet.payload_dwords[4 + i],
			lengths[i]);
	}
	g_server_tick_time = 0;
}

/* ------------------------------------------------------------------------ */
/* Known failures. */

/* Known failure missing_player_marks_first_entry, issue #115: pilot_record.h
 * gives has_left as 1 once the player left the flight. Player 2's id is in no
 * entry; it leaves, and entry 0, player 7, who is still flying, is marked as
 * left. The function's comment states this, so the check rests on the field's
 * comment and the issue. */
static void check_missing_player_marks_nobody(void)
{
	flight_net_world();
	g_players[2].network.direct_play_id = 999;
	flight_net_mark_pilot_network_player_left(2);
	for (int entry = 0; entry < 8; ++entry) {
		XVT_ASSERT_INT_EQ(g_pilot_data.network_players[entry].has_left,
				  0);
	}
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
			{"missing_player_marks_first_entry",
			 check_missing_player_marks_nobody},
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
	check_find_pilot_entry();
	check_find_pilot_entry_missing();
	check_mark_player_left();
	check_clock_probe();
	check_notices();
	check_reset_world_message_schedule();
	check_world_checksum_to_host();
	check_world_checksum_to_all();
	return 0;
}

/* Checks xvt_flight_network_process_packets, the packet loop in
 * flight_packets.c, against its promises in
 * xvt_runtime/runtime/flight_network.h, for the cases that need no peer:
 * without a mission cookie or a connected local player nothing is read; with
 * both and no packet waiting, a client stops reading and a host sends world
 * messages while ShouldSend allows; and the input clock then advances by the
 * frame time read meanwhile. No game data is read: the test sets the player
 * records, the network session's ids and the flight network globals itself, and
 * drives the host clock. Every check starts with player 0 the connected local
 * player, the frame-delta clock started, and then two ticks (8 ms) of frame
 * time waiting to be read.
 *
 * Not checked here: reading, checking and dispatching packets, and the flight
 * control handler's answers, need packets from a peer on a second machine. */
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/host_clock.h"

enum { INPUT = 1000, WAITING_TICKS = 2 };

static void flight_packets_world(int host, int cookie)
{
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_player_abort_flags[i] = 0;
		g_flight_net_peer_silence_ticks[i] = 0;
	}
	g_players[0].participation_state = 1;
	g_local_player = 0;
	memset(&g_net_session, 0, sizeof g_net_session);
	for (int i = 0; i < 8; ++i) {
		g_net_session.players[i].direct_play_id = 100 + i;
	}
	g_net_session.host_dplay_id = 500;
	memset(g_input_history, 0, sizeof g_input_history);
	memset(g_input_frame_count, 0, sizeof g_input_frame_count);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	g_flight_net_pending_ack_count = 0;
	g_flight_net_clock_adjust_accum_ticks = 0;
	g_flight_net_world_message_turn_timestamp = 0;
	g_flight_net_clock_lead_ticks = 0;
	g_flight_net_last_sent_world_message_timestamp = 0;
	g_flight_net_checksum_request_accum_ticks = 0;
	xvt_resync_reset();
	xvt_flight_network_reset();
	xvt_flight_network_clear_cookies();
	xvt_flight_network_reset_mission();
	xvt_flight_network_clear_recovery_request();
	xvt_flight_checkpoint_begin(0x01);
	if (cookie) {
		/* A host flying alone agrees a cookie without a peer. */
		g_net_session.local_is_host = 1;
		g_active_flight_player_count = 1;
		XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_options(), 1);
		XVT_ASSERT_TRUE(xvt_flight_network_cookie() != 0);
	}
	g_net_session.local_is_host = host;
	g_input_timestamp = INPUT;
	xvt_time_reset();
	xvt_time_advance_host_clock(5000000);
	time_reset_elapsed_ticks();
	XVT_ASSERT_INT_EQ(time_consume_elapsed_ticks(), 0);
	xvt_time_advance_host_clock(WAITING_TICKS * 4000);
}

static void check_nothing_without_cookie(void)
{
	flight_packets_world(0, 0);
	xvt_flight_network_process_packets();
	XVT_ASSERT_INT_EQ(g_input_timestamp, INPUT);
	/* The frame time was not read. */
	XVT_ASSERT_INT_EQ(time_consume_elapsed_ticks(), WAITING_TICKS);

	flight_packets_world(1, 0);
	xvt_flight_network_process_packets();
	XVT_ASSERT_INT_EQ(g_input_timestamp, INPUT);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(time_consume_elapsed_ticks(), WAITING_TICKS);
}

static void check_nothing_without_local_player(void)
{
	flight_packets_world(0, 1);
	g_players[0].participation_state = 0;
	xvt_flight_network_process_packets();
	XVT_ASSERT_INT_EQ(g_input_timestamp, INPUT);
	XVT_ASSERT_INT_EQ(time_consume_elapsed_ticks(), WAITING_TICKS);
}

static void check_client_stops(void)
{
	/* No packet waits: a client stops reading, and the input clock takes
	 * the frame time read meanwhile. */
	flight_packets_world(0, 1);
	xvt_flight_network_process_packets();
	XVT_ASSERT_INT_EQ(g_input_timestamp, INPUT + WAITING_TICKS);
	XVT_ASSERT_INT_EQ(time_consume_elapsed_ticks(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
}

static void check_host_sends_while_allowed(void)
{
	/* A host that ShouldSend refuses stops reading too. */
	flight_packets_world(1, 1);
	g_flight_net_pending_ack_count = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(INPUT), 0);
	xvt_flight_network_process_packets();
	XVT_ASSERT_INT_EQ(g_input_timestamp, INPUT + WAITING_TICKS);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);

	/* A host far behind on world messages sends them, queued pending for
	 * its own confirmation, until ShouldSend refuses. */
	flight_packets_world(1, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(INPUT - 100),
			  0);
	xvt_flight_network_process_packets();
	XVT_ASSERT_TRUE(xvt_flight_messages_count(XVT_QUEUE_PENDING) > 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(INPUT), 0);
	XVT_ASSERT_INT_EQ(g_input_timestamp, INPUT + WAITING_TICKS);
}

int main(void)
{
	check_nothing_without_cookie();
	check_nothing_without_local_player();
	check_client_stops();
	check_host_sends_while_allowed();
	flight_packets_world(0, 0);
	return 0;
}

/* The world the flight networking checks start from, shared by
 * test_flight_network.c and test_flight_network_exchange.c. No game data is
 * read: the world sets the player records, the input histories, the network
 * session's ids and the flight network globals itself, and drives the host
 * clock. Player 0 is the local player; the roster gives player n the network
 * id 100 + n, and the host's id is 500. Every check starts from that world
 * with no mission cookie, empty queues and histories, and the host clock at
 * one second. */
#ifndef XVT_TESTS_TEST_FLIGHT_NETWORK_WORLD_H
#define XVT_TESTS_TEST_FLIGHT_NETWORK_WORLD_H

#include <stdint.h>
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
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/host_clock.h"

enum { HOST_DPID = 500, SECOND_US = 1000000 };

static inline int dpid(unsigned player) { return 100 + (int)player; }

static inline void flight_network_world(int host)
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
	g_net_session.local_is_host = host;
	g_net_session.host_dplay_id = HOST_DPID;
	for (unsigned i = 0; i < 8; ++i) {
		g_net_session.players[i].direct_play_id = dpid(i);
	}
	memset(g_input_history, 0, sizeof g_input_history);
	memset(g_input_frame_count, 0, sizeof g_input_frame_count);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	g_game_time = 0;
	g_server_tick_time = 0;
	g_active_flight_player_count = 1;
	g_flight_net_pending_ack_count = 0;
	g_flight_net_clock_adjust_accum_ticks = 0;
	g_flight_net_world_message_turn_timestamp = 0;
	g_flight_net_clock_lead_ticks = 0;
	g_flight_net_last_sent_world_message_timestamp = 0;
	g_flight_net_checksum_request_accum_ticks = 0;
	xvt_time_reset();
	xvt_time_advance_host_clock(SECOND_US);
	time_reset_elapsed_ticks();
	xvt_resync_reset();
	xvt_flight_network_reset();
	xvt_flight_network_clear_cookies();
	xvt_flight_network_reset_mission();
	xvt_flight_network_clear_recovery_request();
	xvt_flight_checkpoint_begin(0x01);
}

/* Agrees a mission cookie the way a host flying alone does, and returns it. */
static inline uint32_t agree_cookie(void)
{
	int host = g_net_session.local_is_host;
	g_net_session.local_is_host = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_options(), 1);
	g_net_session.local_is_host = host;
	XVT_ASSERT_TRUE(xvt_flight_network_cookie() != 0);
	return xvt_flight_network_cookie();
}

#endif

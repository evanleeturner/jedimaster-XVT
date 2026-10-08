/* Checks network125's exchanges before flight
 * (xvt_runtime/runtime/flight_network_exchange.h) against the promises in
 * its header, for every part that holds without a network peer: the mission
 * cookie and its counter, the single-player paths of Options, Start and the
 * roster exchange, and the exchange's timeout. Every check starts from the
 * world of test_flight_network_world.h.
 *
 * Nothing here sends a packet: without a network session the game's send path
 * loops packets back into its own receive queue. So the multiplayer
 * exchanges of Session, Options and Start need a second machine and are not
 * checked. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "test_flight_network_world.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_sw.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/timing/host_clock.h"

static void check_cookie(void)
{
	flight_network_world(1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_cookie(), 0);
	uint32_t first = agree_cookie();
	/* Each agreement takes a new cookie. */
	uint32_t second = agree_cookie();
	XVT_ASSERT_TRUE(second != first);
	/* Reset forgets the cookie and keeps the counter: the next cookie is new again. */
	xvt_flight_network_reset();
	XVT_ASSERT_INT_EQ(xvt_flight_network_cookie(), 0);
	uint32_t third = agree_cookie();
	XVT_ASSERT_TRUE(third != first && third != second);
	/* CloseSession forgets both: the counter starts over, so the cookie
	 * after it is the one after the earlier CloseSession. */
	xvt_flight_network_clear_cookies();
	XVT_ASSERT_INT_EQ(xvt_flight_network_cookie(), 0);
	XVT_ASSERT_INT_EQ(agree_cookie(), first);

	/* A client flying alone takes no cookie of its own. */
	xvt_flight_network_clear_cookies();
	g_net_session.local_is_host = 0;
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_options(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_cookie(), 0);
}

static void check_options_alone(void)
{
	/* A single player fills its own resolution, rating and taunts. */
	flight_network_world(1);
	g_flight_resolution_mode = 2;
	g_pilot_data.rating = 1234;
	for (int i = 0; i < 4; ++i) {
		snprintf(g_game_config.taunts[i],
			 sizeof g_game_config.taunts[i], "taunt %d", i);
	}
	memset(g_player_taunt_text, 0, sizeof g_player_taunt_text);
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_options(), 1);
	XVT_ASSERT_INT_EQ(g_players[0].network.flight_resolution_mode, 2);
	XVT_ASSERT_INT_EQ(g_players[0].pilot_rating, 1234);
	XVT_ASSERT_INT_EQ(memcmp(g_player_taunt_text[0], g_game_config.taunts,
				 sizeof g_game_config.taunts),
			  0);
}

static void check_start_alone(void)
{
	flight_network_world(1);
	g_game_time = 400;
	g_server_tick_time = 400;
	XVT_ASSERT_INT_EQ(xvt_flight_network_wait_for_mission_start(), 1);
	XVT_ASSERT_INT_EQ(g_game_time, 0);
	XVT_ASSERT_INT_EQ(g_server_tick_time, 0);
}

static void check_session(void)
{
	/* A host expecting no players is done at once; a client joining a
	 * flight in progress too. */
	flight_network_world(1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_begin_roster_exchange(0, 0),
			  XVT_FLIGHT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_roster(), 1);
	flight_network_world(0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_begin_roster_exchange(2, 1), 1);

	/* A host waiting for two players gives up after 60 seconds without a packet. */
	flight_network_world(1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_begin_roster_exchange(2, 0),
			  XVT_FLIGHT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_roster(),
			  XVT_FLIGHT_NETWORK_PENDING);
	xvt_time_advance_host_clock(59 * SECOND_US);
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_roster(),
			  XVT_FLIGHT_NETWORK_PENDING);
	xvt_time_advance_host_clock(2 * SECOND_US);
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_roster(), 0);

	/* So does a client waiting for the roster. */
	flight_network_world(0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_begin_roster_exchange(2, 0),
			  XVT_FLIGHT_NETWORK_PENDING);
	xvt_time_advance_host_clock(59 * SECOND_US);
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_roster(),
			  XVT_FLIGHT_NETWORK_PENDING);
	xvt_time_advance_host_clock(2 * SECOND_US);
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_roster(), 0);
}

int main(void)
{
	check_cookie();
	check_options_alone();
	check_start_alone();
	check_session();
	flight_network_world(0);
	xvt_flight_network_clear_cookies();
	return 0;
}

/* Checks XvtFlightNetwork_ProcessPackets, the packet loop in flight_packets.c, against its promises in
 * xvt_runtime/runtime/flight_network.h, for the cases that need no peer: without a mission cookie or a
 * connected local player nothing is read; with both and no packet waiting, a client stops reading and a host
 * sends world messages while ShouldSend allows; and the input clock then advances by the frame time read
 * meanwhile. No game data is read: the test sets the player records, the network session's ids and the
 * flight network globals itself, and drives the host clock. Every check starts with player 0 the connected
 * local player, the frame-delta clock started, and then two ticks (8 ms) of frame time waiting to be read.
 *
 * Not checked here: reading, checking and dispatching packets, and the flight control handler's answers,
 * need packets from a peer on a second machine. */
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
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/host_clock.h"

#include <string.h>

enum { INPUT = 1000, WAITING_TICKS = 2 };

static void World(int host, int cookie)
{
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].objectIndex = -1;
		g_playerAbortFlags[i] = 0;
		g_flightNetPeerSilenceTicks[i] = 0;
	}
	g_players[0].participationState = 1;
	g_localPlayer = 0;
	memset(&g_netSession, 0, sizeof g_netSession);
	for (int i = 0; i < 8; ++i) {
		g_netSession.players[i].directPlayId = 100 + i;
	}
	g_netSession.hostDplayId = 500;
	memset(g_inputHistory, 0, sizeof g_inputHistory);
	memset(g_inputFrameCount, 0, sizeof g_inputFrameCount);
	memset(&g_flightMissionState, 0, sizeof g_flightMissionState);
	g_flightNetPendingAckCount = 0;
	g_flightNetClockAdjustAccumTicks = 0;
	g_flightNetWorldMessageTurnTimestamp = 0;
	g_flightNetClockLeadTicks = 0;
	g_flightNetLastSentWorldMessageTimestamp = 0;
	g_flightNetChecksumRequestAccumTicks = 0;
	XvtResync_Reset();
	XvtFlightNetwork_Reset();
	XvtFlightNetwork_ClearCookies();
	XvtFlightNetwork_ResetMission();
	XvtFlightNetwork_ClearRecoveryRequest();
	XvtFlightCheckpoint_Begin(0x01);
	if (cookie) {
		/* A host flying alone agrees a cookie without a peer. */
		g_netSession.localIsHost = 1;
		g_activeFlightPlayerCount = 1;
		XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeOptions(), 1);
		XVT_ASSERT_TRUE(XvtFlightNetwork_Cookie() != 0);
	}
	g_netSession.localIsHost = host;
	g_inputTimestamp = INPUT;
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(5000000);
	Time_ResetElapsedTicks();
	XVT_ASSERT_INT_EQ(Time_ConsumeElapsedTicks(), 0);
	XvtTime_AdvanceHostClock(WAITING_TICKS * 4000);
}

static void CheckNothingWithoutCookie(void)
{
	World(0, 0);
	XvtFlightNetwork_ProcessPackets();
	XVT_ASSERT_INT_EQ(g_inputTimestamp, INPUT);
	/* The frame time was not read. */
	XVT_ASSERT_INT_EQ(Time_ConsumeElapsedTicks(), WAITING_TICKS);

	World(1, 0);
	XvtFlightNetwork_ProcessPackets();
	XVT_ASSERT_INT_EQ(g_inputTimestamp, INPUT);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(Time_ConsumeElapsedTicks(), WAITING_TICKS);
}

static void CheckNothingWithoutLocalPlayer(void)
{
	World(0, 1);
	g_players[0].participationState = 0;
	XvtFlightNetwork_ProcessPackets();
	XVT_ASSERT_INT_EQ(g_inputTimestamp, INPUT);
	XVT_ASSERT_INT_EQ(Time_ConsumeElapsedTicks(), WAITING_TICKS);
}

static void CheckClientStops(void)
{
	/* No packet waits: a client stops reading, and the input clock takes the frame time read meanwhile. */
	World(0, 1);
	XvtFlightNetwork_ProcessPackets();
	XVT_ASSERT_INT_EQ(g_inputTimestamp, INPUT + WAITING_TICKS);
	XVT_ASSERT_INT_EQ(Time_ConsumeElapsedTicks(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
}

static void CheckHostSendsWhileAllowed(void)
{
	/* A host that ShouldSend refuses stops reading too. */
	World(1, 1);
	g_flightNetPendingAckCount = 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(INPUT), 0);
	XvtFlightNetwork_ProcessPackets();
	XVT_ASSERT_INT_EQ(g_inputTimestamp, INPUT + WAITING_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);

	/* A host far behind on world messages sends them, queued pending for its own confirmation, until
	 * ShouldSend refuses. */
	World(1, 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(INPUT - 100), 0);
	XvtFlightNetwork_ProcessPackets();
	XVT_ASSERT_TRUE(XvtFlightMessages_Count(XVT_QUEUE_PENDING) > 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(INPUT), 0);
	XVT_ASSERT_INT_EQ(g_inputTimestamp, INPUT + WAITING_TICKS);
}

int main(void)
{
	CheckNothingWithoutCookie();
	CheckNothingWithoutLocalPlayer();
	CheckClientStops();
	CheckHostSendsWhileAllowed();
	World(0, 0);
	return 0;
}

/* Checks network125 state recovery (xvt_runtime/runtime/resync_task.h) against the promises in its header,
 * for the parts that hold without a peer: the idle state after Reset, deferred checksum reports and state
 * requests, RequestState on a client, on a host and outside network125, the packets ReceivePacket leaves to
 * the flight control handler, and the apply phase BeginApply starts. No game data is read: the test sets the
 * network session's ids, the clocks and the flight globals itself. Every check starts from Reset, with an
 * empty network roster, the host clock on a whole millisecond and the frame-delta clock reset.
 *
 * With no network session the game's send path loops a packet back into its own receive queue, which no
 * check reads; RequestState on a client and BeginApply send one packet each that way.
 *
 * Not checked here: a send (BeginSend, the chunks, WaitAcks) and a receive (the request, chunks, apply and
 * replay) need a peer on a second machine, and both draw the waiting box on the flight's display surface,
 * which needs a window; Update past the idle state reads packets from that peer. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/flight_frame.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { HOST_DPID = 500, PEER_DPID = 101 };

static void World(int host, XvtFlightTimingProfile profile) {
	memset(&g_netSession, 0, sizeof g_netSession);
	g_netSession.localIsHost = host;
	g_netSession.hostDplayId = HOST_DPID;
	memset(&g_flightMissionState, 0, sizeof g_flightMissionState);
	g_serverTickTime = 246;
	g_inputTimestamp = 300;
	g_flightNetWorldChecksumEpoch = 0;
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(5000000);
	Time_ResetElapsedTicks();
	XvtFlightTiming_BeginSession(profile);
	XvtResync_Reset();
}

/* Defers a checksum report from sender, flagged as a state request or not. */
static void Defer(int sender, int request) {
	XvtFlightChecksumReportWire report;
	memset(&report, 0, sizeof report);
	XvtWire_Set32(report.checksum.opcode, NET_PACKET_WORLD_CHECKSUM);
	XvtWire_Set32(report.request_state, request ? XVT_CHECKSUM_REQUEST_STATE : XVT_CHECKSUM_REPORT);
	int aligned[sizeof report / sizeof(int)];
	memcpy(aligned, &report, sizeof report);
	XvtResync_DeferChecksum(sender, aligned);
}

static void AssertIdle(void) {
	XVT_ASSERT_INT_EQ(XvtResync_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtResync_HoldsInput(), 0);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 0);
	XVT_ASSERT_INT_EQ(XvtResync_ReceiveFloor(), g_serverTickTime);
	XVT_ASSERT_TRUE(XvtResync_NextWakeDelayUs() == UINT64_MAX);
}

static void CheckIdle(void) {
	World(0, XVT_FLIGHT_TIMING_NETWORK_125);
	AssertIdle();
	World(1, XVT_FLIGHT_TIMING_NETWORK_125);
	AssertIdle();
	/* The floor follows the server tick while nothing is received. */
	g_serverTickTime = 400;
	XVT_ASSERT_INT_EQ(XvtResync_ReceiveFloor(), 400);
}

static void CheckDeferredChecksums(void) {
	World(1, XVT_FLIGHT_TIMING_NETWORK_125);
	Defer(PEER_DPID, 0);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 0);
	Defer(PEER_DPID + 1, 1);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 1);
	Defer(PEER_DPID, 0);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 1);
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 0);
	/* Reset drops the deferred reports. */
	XvtResync_Reset();
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 0);

	/* The queue fills: the report that finds it full ends the mission and is not kept. */
	unsigned deferred = 0;
	while (!g_flightMissionState.missionEndPending) {
		Defer(PEER_DPID, 0);
		XVT_ASSERT_TRUE(++deferred < 100000);
	}
	XVT_ASSERT_TRUE(deferred > 1);
	Defer(PEER_DPID, 1);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 0);
	/* After Reset there is room again. */
	XvtResync_Reset();
	g_flightMissionState.missionEndPending = 0;
	Defer(PEER_DPID, 1);
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 0);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 1);
}

static void CheckRequestStateOutsideNetwork125(void) {
	World(0, XVT_FLIGHT_TIMING_NATIVE);
	XvtResync_ServiceRecovery();
	AssertIdle();
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 0);
	World(1, XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtResync_ServiceRecovery();
	AssertIdle();
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 0);
}

static void CheckRequestStateHost(void) {
	/* The host cannot receive an image: it ends the mission, and holds no input. */
	World(1, XVT_FLIGHT_TIMING_NETWORK_125);
	XvtResync_ServiceRecovery();
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 1);
	XVT_ASSERT_INT_EQ(XvtResync_HoldsInput(), 0);
	XVT_ASSERT_INT_EQ(XvtResync_IsActive(), 0);
}

static void CheckRequestStateClient(void) {
	/* A client's request is out: input is held, and the next wake is the peer-timeout deadline. */
	World(0, XVT_FLIGHT_TIMING_NETWORK_125);
	XvtResync_ServiceRecovery();
	XVT_ASSERT_INT_EQ(XvtResync_HoldsInput(), 1);
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 0);
	XVT_ASSERT_INT_EQ(XvtResync_ReceiveFloor(), g_serverTickTime);
	uint64_t deadline = (uint64_t)XVT_PEER_TIMEOUT_TICKS * XVT_FLIGHT_TICK_US;
	uint64_t wake = XvtResync_NextWakeDelayUs();
	XVT_ASSERT_TRUE(wake > 0 && wake <= deadline);

	/* A second request before the deadline changes nothing. */
	XvtResync_ServiceRecovery();
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 0);
	XVT_ASSERT_INT_EQ(XvtResync_HoldsInput(), 1);
	XVT_ASSERT_TRUE(XvtResync_NextWakeDelayUs() <= wake);

	/* The host holds no input even with a request recorded. */
	g_netSession.localIsHost = 1;
	XVT_ASSERT_INT_EQ(XvtResync_HoldsInput(), 0);
	g_netSession.localIsHost = 0;

	/* Reset drops the request. */
	XvtResync_Reset();
	AssertIdle();
}

static void CheckReceivePacketLeavesOthers(void) {
	uint8_t bytes[sizeof(XvtFlightChecksumReportWire)];
	memset(bytes, 0, sizeof bytes);

	/* Too short to hold an opcode. */
	World(0, XVT_FLIGHT_TIMING_NETWORK_125);
	XvtWire_Set32(bytes, NET_PACKET_RESYNC_REQUEST);
	XVT_ASSERT_INT_EQ(XvtResync_ReceivePacket(HOST_DPID, bytes, 3), 0);
	/* Not a recovery packet. */
	XvtWire_Set32(bytes, NET_PACKET_ACK);
	XVT_ASSERT_INT_EQ(XvtResync_ReceivePacket(HOST_DPID, bytes, 4), 0);

	/* On the host with no send running, a peer's state request is left to the flight control handler. */
	World(1, XVT_FLIGHT_TIMING_NETWORK_125);
	XvtWire_Set32(bytes, NET_PACKET_WORLD_CHECKSUM);
	XvtWire_Set32(bytes + offsetof(XvtFlightChecksumReportWire, request_state), XVT_CHECKSUM_REQUEST_STATE);
	XVT_ASSERT_INT_EQ(XvtResync_ReceivePacket(PEER_DPID, bytes, sizeof bytes), 0);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 0);
	AssertIdle();
}

static void CheckBeginApply(void) {
	/* The host waits in the apply phase: a transfer runs, the next wake is the retry interval, and the
	 * host holds no input. */
	World(1, XVT_FLIGHT_TIMING_NETWORK_125);
	XvtResync_BeginApply(PEER_DPID, 4096);
	XVT_ASSERT_INT_EQ(XvtResync_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtResync_HoldsInput(), 0);
	XVT_ASSERT_INT_EQ(XvtResync_ReceiveFloor(), g_serverTickTime);
	XVT_ASSERT_TRUE(XvtResync_NextWakeDelayUs() == XvtFlightTime_DelayForTicks(XVT_RESYNC_RETRY_TICKS));
	/* Reset drops it. */
	XvtResync_Reset();
	AssertIdle();
}

int main(void) {
	CheckIdle();
	CheckDeferredChecksums();
	CheckRequestStateOutsideNetwork125();
	CheckRequestStateHost();
	CheckRequestStateClient();
	CheckReceivePacketLeavesOthers();
	CheckBeginApply();
	XvtResync_Reset();
	XvtFlightTiming_EndSession();
	memset(&g_netSession, 0, sizeof g_netSession);
	return 0;
}

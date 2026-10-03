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

static void resync_task_world(int host, xvt_flight_timing_profile profile)
{
	memset(&g_net_session, 0, sizeof g_net_session);
	g_net_session.local_is_host = host;
	g_net_session.host_dplay_id = HOST_DPID;
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	g_server_tick_time = 246;
	g_input_timestamp = 300;
	g_flight_net_world_checksum_epoch = 0;
	xvt_time_reset();
	xvt_time_advance_host_clock(5000000);
	time_reset_elapsed_ticks();
	xvt_flight_timing_begin_session(profile);
	xvt_resync_reset();
}

/* Defers a checksum report from sender, flagged as a state request or not. */
static void defer(int sender, int request)
{
	struct xvt_flight_checksum_report_wire report;
	memset(&report, 0, sizeof report);
	xvt_wire_set32(report.checksum.opcode, NET_PACKET_WORLD_CHECKSUM);
	xvt_wire_set32(report.request_state,
		       request ? XVT_CHECKSUM_REQUEST_STATE
			       : XVT_CHECKSUM_REPORT);
	int aligned[sizeof report / sizeof(int)];
	memcpy(aligned, &report, sizeof report);
	xvt_resync_defer_checksum(sender, aligned);
}

static void assert_idle(void)
{
	XVT_ASSERT_INT_EQ(xvt_resync_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_resync_holds_input(), 0);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 0);
	XVT_ASSERT_INT_EQ(xvt_resync_receive_floor(), g_server_tick_time);
	XVT_ASSERT_TRUE(xvt_resync_next_wake_delay_us() == UINT64_MAX);
}

static void check_idle(void)
{
	resync_task_world(0, XVT_FLIGHT_TIMING_NETWORK_125);
	assert_idle();
	resync_task_world(1, XVT_FLIGHT_TIMING_NETWORK_125);
	assert_idle();
	/* The floor follows the server tick while nothing is received. */
	g_server_tick_time = 400;
	XVT_ASSERT_INT_EQ(xvt_resync_receive_floor(), 400);
}

static void check_deferred_checksums(void)
{
	resync_task_world(1, XVT_FLIGHT_TIMING_NETWORK_125);
	defer(PEER_DPID, 0);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 0);
	defer(PEER_DPID + 1, 1);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 1);
	defer(PEER_DPID, 0);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	/* Reset drops the deferred reports. */
	xvt_resync_reset();
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 0);

	/* The queue fills: the report that finds it full ends the mission and is not kept. */
	unsigned deferred = 0;
	while (!g_flight_mission_state.mission_end_pending) {
		defer(PEER_DPID, 0);
		XVT_ASSERT_TRUE(++deferred < 100000);
	}
	XVT_ASSERT_TRUE(deferred > 1);
	defer(PEER_DPID, 1);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 0);
	/* After Reset there is room again. */
	xvt_resync_reset();
	g_flight_mission_state.mission_end_pending = 0;
	defer(PEER_DPID, 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 1);
}

static void check_request_state_outside_network125(void)
{
	resync_task_world(0, XVT_FLIGHT_TIMING_NATIVE);
	xvt_resync_service_recovery();
	assert_idle();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	resync_task_world(1, XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_resync_service_recovery();
	assert_idle();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
}

static void check_request_state_host(void)
{
	/* The host cannot receive an image: it ends the mission, and holds no input. */
	resync_task_world(1, XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_resync_service_recovery();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 1);
	XVT_ASSERT_INT_EQ(xvt_resync_holds_input(), 0);
	XVT_ASSERT_INT_EQ(xvt_resync_is_active(), 0);
}

static void check_request_state_client(void)
{
	/* A client's request is out: input is held, and the next wake is the peer-timeout deadline. */
	resync_task_world(0, XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_resync_service_recovery();
	XVT_ASSERT_INT_EQ(xvt_resync_holds_input(), 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	XVT_ASSERT_INT_EQ(xvt_resync_receive_floor(), g_server_tick_time);
	uint64_t deadline =
		(uint64_t)XVT_PEER_TIMEOUT_TICKS * XVT_FLIGHT_TICK_US;
	uint64_t wake = xvt_resync_next_wake_delay_us();
	XVT_ASSERT_TRUE(wake > 0 && wake <= deadline);

	/* A second request before the deadline changes nothing. */
	xvt_resync_service_recovery();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	XVT_ASSERT_INT_EQ(xvt_resync_holds_input(), 1);
	XVT_ASSERT_TRUE(xvt_resync_next_wake_delay_us() <= wake);

	/* The host holds no input even with a request recorded. */
	g_net_session.local_is_host = 1;
	XVT_ASSERT_INT_EQ(xvt_resync_holds_input(), 0);
	g_net_session.local_is_host = 0;

	/* Reset drops the request. */
	xvt_resync_reset();
	assert_idle();
}

static void check_receive_packet_leaves_others(void)
{
	uint8_t bytes[sizeof(struct xvt_flight_checksum_report_wire)];
	memset(bytes, 0, sizeof bytes);

	/* Too short to hold an opcode. */
	resync_task_world(0, XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_wire_set32(bytes, NET_PACKET_RESYNC_REQUEST);
	XVT_ASSERT_INT_EQ(xvt_resync_receive_packet(HOST_DPID, bytes, 3), 0);
	/* Not a recovery packet. */
	xvt_wire_set32(bytes, NET_PACKET_ACK);
	XVT_ASSERT_INT_EQ(xvt_resync_receive_packet(HOST_DPID, bytes, 4), 0);

	/* On the host with no send running, a peer's state request is left to the flight control handler. */
	resync_task_world(1, XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_wire_set32(bytes, NET_PACKET_WORLD_CHECKSUM);
	xvt_wire_set32(bytes + offsetof(struct xvt_flight_checksum_report_wire,
					request_state),
		       XVT_CHECKSUM_REQUEST_STATE);
	XVT_ASSERT_INT_EQ(
		xvt_resync_receive_packet(PEER_DPID, bytes, sizeof bytes), 0);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 0);
	assert_idle();
}

static void check_begin_apply(void)
{
	/* The host waits in the apply phase: a transfer runs, the next wake is the retry interval, and the
	 * host holds no input. */
	resync_task_world(1, XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_resync_begin_apply(PEER_DPID, 4096);
	XVT_ASSERT_INT_EQ(xvt_resync_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_resync_holds_input(), 0);
	XVT_ASSERT_INT_EQ(xvt_resync_receive_floor(), g_server_tick_time);
	XVT_ASSERT_TRUE(
		xvt_resync_next_wake_delay_us() ==
		xvt_flight_time_delay_for_ticks(XVT_RESYNC_RETRY_TICKS));
	/* Reset drops it. */
	xvt_resync_reset();
	assert_idle();
}

int main(void)
{
	check_idle();
	check_deferred_checksums();
	check_request_state_outside_network125();
	check_request_state_host();
	check_request_state_client();
	check_receive_packet_leaves_others();
	check_begin_apply();
	xvt_resync_reset();
	xvt_flight_timing_end_session();
	memset(&g_net_session, 0, sizeof g_net_session);
	return 0;
}

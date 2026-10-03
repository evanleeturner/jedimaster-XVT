/* Checks network125 flight networking (xvt_runtime/runtime/flight_network.h) against the promises in its
 * header, for every part that holds without a network peer: the mission cookie and its counter, the
 * single-player paths of Options, Start and the roster exchange and the exchange's timeout, the recovery
 * request, PlayerAbort's result, the packet budget, AdmitInput's refusals, InsertWorld, what Receive
 * consumes and where it puts it, DecodeControl, NextWakeDelayUs, ShouldSend, and SendWorld on a host flying
 * alone. No game data is read: the test sets the player records, the input histories, the network session's
 * ids and the flight network globals itself, and drives the host clock. Player 0 is the local player; the
 * roster gives player n the network id 100 + n, and the host's id is 500. Every check starts from that
 * world with no mission cookie, empty queues and histories, and the host clock at one second.
 *
 * Nothing here sends a packet: without a network session the game's send path loops packets back into its
 * own receive queue. So these need a second machine and are not checked: the multiplayer exchanges of
 * Session, Options and Start, FlushInput and FlushWorld to remote players, peer timeouts in SendWorld, and
 * that a player PlayerAbort excludes leaves later world messages. ProcessPackets is in flight_packets.c.
 * AdmitInput's staging needs the recorded controls, which need loaded settings and the game's DirectInput
 * keyboard device, so only its refusals before sampling are checked. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_sw.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_frame.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { HOST_DPID = 500, SECOND_US = 1000000 };

static struct xvt_flight_message g_message, g_out;
static uint8_t g_packet[XVT_FLIGHT_PACKET_BYTES + 8];

static int dpid(unsigned player) { return 100 + (int)player; }

static void flight_network_world(int host)
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
static uint32_t agree_cookie(void)
{
	int host = g_net_session.local_is_host;
	g_net_session.local_is_host = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_options(), 1);
	g_net_session.local_is_host = host;
	XVT_ASSERT_TRUE(xvt_flight_network_cookie() != 0);
	return xvt_flight_network_cookie();
}

static struct flight_input_frame_record controls(int8_t axis)
{
	struct flight_input_frame_record input;
	memset(&input, 0, sizeof input);
	input.key = 0x61;
	input.axis_x = axis;
	input.axis_y = (int8_t)-axis;
	input.key_mods = 1;
	return input;
}

/* Appends a frame to player's history, which the caller keeps in tick order. */
static void add_frame(unsigned player, int tick, int valid, int applied)
{
	struct input_frame *frame =
		&g_input_history[player][g_input_frame_count[player]++];
	frame->timestamp = tick;
	frame->input_source = valid;
	frame->awaiting_relay = applied;
	frame->input = controls(10);
}

static const struct input_frame *frame_at(unsigned player, int tick)
{
	for (int i = 0; i < g_input_frame_count[player]; ++i) {
		if (g_input_history[player][i].timestamp == tick) {
			return &g_input_history[player][i];
		}
	}
	return NULL;
}

static void add_record(struct xvt_flight_message *message, unsigned player,
		       int tick, int8_t axis)
{
	struct xvt_flight_world_input_wire *record =
		&message->records[message->count++];
	struct flight_input_frame_record input = controls(axis);
	record->player = (uint8_t)player;
	xvt_flight_wire_encode_input(&record->input, tick, &input);
}

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
	/* CloseSession forgets both: the counter starts over, so the cookie after it is the one after the
	 * earlier CloseSession. */
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
	/* A host expecting no players is done at once; a client joining a flight in progress too. */
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

static void check_recovery_flags(void)
{
	flight_network_world(1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
	xvt_flight_network_request_recovery();
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	xvt_flight_network_request_recovery();
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	xvt_flight_network_clear_recovery_request();
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
	xvt_flight_network_request_recovery();
	xvt_flight_network_recovered();
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
}

static void check_player_abort(void)
{
	for (int host = 0; host < 2; ++host) {
		flight_network_world(host);
		XVT_ASSERT_INT_EQ(
			xvt_flight_network_player_abort(XVT_FLIGHT_PLAYERS), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_network_player_abort(0), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_network_player_abort(1), 1);
		g_local_player = 3;
		XVT_ASSERT_INT_EQ(xvt_flight_network_player_abort(3), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_network_player_abort(0), 1);
	}
}

static void check_packet_budget(void)
{
	flight_network_world(1);
	/* A fresh iteration has a budget; it runs out, and starting the same iteration again does not refill
	 * it. */
	unsigned budget = 0;
	while (xvt_flight_network_take_packet_budget()) {
		XVT_ASSERT_TRUE(++budget < 100000);
	}
	XVT_ASSERT_TRUE(budget > 0);
	xvt_flight_network_begin_iteration();
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_packet_budget(), 0);
	/* A later host clock is a new iteration, with the whole budget again. */
	xvt_time_advance_host_clock(1);
	unsigned again = 0;
	while (xvt_flight_network_take_packet_budget()) {
		XVT_ASSERT_TRUE(++again < 100000);
	}
	XVT_ASSERT_INT_EQ(again, budget);
	/* Part of a budget stays spent across BeginIteration in the same iteration. */
	xvt_time_advance_host_clock(1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_packet_budget(), 1);
	xvt_flight_network_begin_iteration();
	unsigned rest = 0;
	while (xvt_flight_network_take_packet_budget()) {
		++rest;
	}
	XVT_ASSERT_INT_EQ(rest, budget - 1);
}

static void check_begin_mission(void)
{
	flight_network_world(1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_push(XVT_QUEUE_PENDING, "p", 1),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_push(XVT_QUEUE_REPLAY, "r", 1),
			  1);
	xvt_flight_network_reset_mission();
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_outgoing(), 0);
}

static void check_admit_input_refusals(void)
{
	/* A tick the history already holds: 1 at once, and the history is left as it was. */
	flight_network_world(0);
	add_frame(0, 8, XVT_INPUT_REAL, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_admit_input(8), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[0], 1);
	XVT_ASSERT_INT_EQ(g_input_history[0][0].input.axis_x, 10);

	/* Invalid ticks. */
	XVT_ASSERT_INT_EQ(xvt_flight_network_admit_input(0), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_admit_input(9), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_admit_input(-8), 0);

	/* During recovery, even a tick the history holds. */
	xvt_flight_network_request_recovery();
	XVT_ASSERT_INT_EQ(xvt_flight_network_admit_input(8), 0);

	/* A full history requests recovery. */
	flight_network_world(0);
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		add_frame(0, 2 * (i + 1), XVT_INPUT_REAL, 0);
	}
	XVT_ASSERT_INT_EQ(xvt_flight_network_admit_input(
				  2 * XVT_INPUT_HISTORY_CAPACITY + 2),
			  0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[0], XVT_INPUT_HISTORY_CAPACITY);
}

static void check_insert_world(void)
{
	flight_network_world(0);
	g_players[1].participation_state = 1;
	g_players[2].participation_state = 1;
	memset(&g_message, 0, sizeof g_message);
	add_record(&g_message, 1, 4, 10);
	add_record(&g_message, 1, 6, 20);
	add_record(&g_message, 2, 4, 30);
	XVT_ASSERT_INT_EQ(xvt_flight_network_insert_world(&g_message), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 2);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 1);
	const struct input_frame *frame = frame_at(1, 6);
	XVT_ASSERT_INT_EQ(frame->input_source, XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(frame->awaiting_relay, 0);
	XVT_ASSERT_INT_EQ(frame->input.axis_x, 20);
	XVT_ASSERT_INT_EQ(frame_at(2, 4)->input.axis_x, 30);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);

	/* A record that fails to decode stops the insert there. */
	flight_network_world(0);
	memset(&g_message, 0, sizeof g_message);
	add_record(&g_message, 1, 4, 10);
	add_record(&g_message, 1, 5, 20);
	add_record(&g_message, 2, 4, 30);
	XVT_ASSERT_INT_EQ(xvt_flight_network_insert_world(&g_message), 0);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 0);

	/* A record that fails to record (an authoritative frame that differs) stops it and requests
	 * recovery. */
	flight_network_world(0);
	memset(&g_message, 0, sizeof g_message);
	add_record(&g_message, 1, 4, 10);
	XVT_ASSERT_INT_EQ(xvt_flight_network_insert_world(&g_message), 1);
	memset(&g_message, 0, sizeof g_message);
	add_record(&g_message, 1, 4, 30);
	add_record(&g_message, 2, 4, 30);
	XVT_ASSERT_INT_EQ(xvt_flight_network_insert_world(&g_message), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 0);
	XVT_ASSERT_INT_EQ(frame_at(1, 4)->input.axis_x, 10);

	/* So does a record for a player out of range. */
	flight_network_world(0);
	memset(&g_message, 0, sizeof g_message);
	add_record(&g_message, XVT_FLIGHT_PLAYERS, 4, 10);
	XVT_ASSERT_INT_EQ(xvt_flight_network_insert_world(&g_message), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
}

static void check_receive_other(void)
{
	flight_network_world(0);
	agree_cookie();
	/* Too short, or not a flight data packet: not consumed. */
	xvt_wire_set32(g_packet, NET_PACKET_INPUT_BATCH);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(1), g_packet, 3), 0);
	xvt_wire_set32(g_packet, NET_PACKET_ACK);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(HOST_DPID, g_packet, 4),
			  0);
	/* Remote-input packets are consumed and dropped. */
	xvt_wire_set32(g_packet, NET_PACKET_REMOTE_INPUT);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(1), g_packet, 16), 1);
	for (unsigned player = 0; player < 8; ++player) {
		XVT_ASSERT_INT_EQ(g_input_frame_count[player], 0);
	}
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
}

static size_t batch(uint32_t cookie, const int *ticks, unsigned count)
{
	struct xvt_flight_input_wire records[XVT_INPUT_BATCH_RECORDS];
	for (unsigned i = 0; i < count; ++i) {
		struct flight_input_frame_record input =
			controls((int8_t)(2 * i + 40));
		xvt_flight_wire_encode_input(&records[i], ticks[i], &input);
	}
	size_t size = xvt_flight_messages_encode_batch(g_packet, cookie,
						       records, count);
	XVT_ASSERT_TRUE(size > 0);
	return size;
}

static void check_receive_batch(void)
{
	const int ticks[] = {4, 6};

	/* From a connected remote player: its predicted frames are replaced by the records, as real input. */
	flight_network_world(0);
	uint32_t cookie = agree_cookie();
	g_players[1].participation_state = 1;
	add_frame(1, 2, XVT_INPUT_REAL, 1);
	add_frame(1, 4, XVT_INPUT_PREDICTED, 0);
	add_frame(1, 8, XVT_INPUT_PREDICTED, 0);
	size_t size = batch(cookie, ticks, 2);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(1), g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 3);
	XVT_ASSERT_TRUE(frame_at(1, 2) != NULL);
	XVT_ASSERT_TRUE(frame_at(1, 8) == NULL);
	for (int i = 0; i < 2; ++i) {
		const struct input_frame *frame = frame_at(1, ticks[i]);
		XVT_ASSERT_TRUE(frame != NULL);
		XVT_ASSERT_INT_EQ(frame->input_source, XVT_INPUT_REAL);
		XVT_ASSERT_INT_EQ(frame->input.axis_x, 2 * i + 40);
	}

	/* Consumed but dropped: an invalid batch, an unknown sender, the local player, a player not
	 * connected. */
	flight_network_world(0);
	cookie = agree_cookie();
	g_players[1].participation_state = 1;
	size = batch(cookie + 1, ticks, 2);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(1), g_packet, size),
			  1);
	size = batch(cookie, ticks, 2);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(999, g_packet, size), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(0), g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(3), g_packet, size),
			  1);
	for (unsigned player = 0; player < 8; ++player) {
		XVT_ASSERT_INT_EQ(g_input_frame_count[player], 0);
	}

	/* A full history requests recovery. */
	flight_network_world(0);
	cookie = agree_cookie();
	g_players[1].participation_state = 1;
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		add_frame(1, 2 * (i + 1), XVT_INPUT_REAL, 1);
	}
	const int late[] = {2 * XVT_INPUT_HISTORY_CAPACITY + 2};
	size = batch(cookie, late, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(1), g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], XVT_INPUT_HISTORY_CAPACITY);
}

static size_t world_part(uint32_t cookie, unsigned target, uint8_t mask)
{
	memset(&g_message, 0, sizeof g_message);
	g_message.target_flags = target;
	g_message.participant_mask = mask;
	add_record(&g_message, 0, 2, 10);
	size_t size = xvt_flight_messages_encode_part(g_packet, &g_message,
						      cookie, 0);
	XVT_ASSERT_TRUE(size > 0);
	return size;
}

static void check_receive_world(void)
{
	/* A complete message from the host is queued pending. */
	flight_network_world(0);
	uint32_t cookie = agree_cookie();
	size_t size = world_part(cookie, 8, 0x01);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(HOST_DPID, g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_TRUE(xvt_flight_messages_peek(XVT_QUEUE_PENDING, &g_out,
						 sizeof g_out) > 0);
	XVT_ASSERT_INT_EQ(g_out.target_flags, 8);
	XVT_ASSERT_INT_EQ(g_out.participant_mask, 0x01);
	XVT_ASSERT_INT_EQ(g_out.count, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);

	/* From anyone else it is consumed and dropped. */
	size = world_part(cookie, 16, 0x01);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(dpid(1), g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);

	/* A bad part requests recovery. */
	size = world_part(cookie + 1, 16, 0x01);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(HOST_DPID, g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 1);

	/* So does a mask outside the initial players, and the message is not queued. */
	flight_network_world(0);
	cookie = agree_cookie();
	xvt_flight_checkpoint_begin(0x01);
	size = world_part(cookie, 8, 0x03);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(HOST_DPID, g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	/* The same mask within the initial players is queued. */
	flight_network_world(0);
	cookie = agree_cookie();
	xvt_flight_checkpoint_begin(0x03);
	size = world_part(cookie, 8, 0x03);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(HOST_DPID, g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 1);

	/* And so does a full queue. */
	flight_network_world(0);
	cookie = agree_cookie();
	while (xvt_flight_messages_push(XVT_QUEUE_PENDING, "x", 1))
		;
	unsigned full = xvt_flight_messages_count(XVT_QUEUE_PENDING);
	size = world_part(cookie, 8, 0x01);
	XVT_ASSERT_INT_EQ(xvt_flight_network_receive(HOST_DPID, g_packet, size),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), full);
}

static void check_decode_control(void)
{
	flight_network_world(1);
	int size;

	/* Sizes under 4 or over a packet, whatever the opcode. */
	xvt_wire_set32(g_packet, NET_PACKET_INPUT_BATCH);
	size = 3;
	XVT_ASSERT_INT_EQ(xvt_flight_network_decode_control(g_packet, &size),
			  0);
	size = XVT_FLIGHT_PACKET_BYTES + 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_decode_control(g_packet, &size),
			  0);

	/* The mission start, a control packet of the start handshake, with no cookie agreed: refused. */
	xvt_wire_set32(g_packet, NET_PACKET_FLIGHT_MISSION_START);
	xvt_wire_set32(g_packet + 4, 1);
	size = 8;
	XVT_ASSERT_INT_EQ(xvt_flight_network_decode_control(g_packet, &size),
			  0);

	/* With a cookie: the right one appended is removed from the size; a wrong or missing one is
	 * refused. */
	uint32_t cookie = agree_cookie();
	xvt_wire_set32(g_packet + 4, cookie);
	size = 8;
	XVT_ASSERT_INT_EQ(xvt_flight_network_decode_control(g_packet, &size),
			  1);
	XVT_ASSERT_INT_EQ(size, 4);
	xvt_wire_set32(g_packet + 4, cookie + 1);
	size = 8;
	XVT_ASSERT_INT_EQ(xvt_flight_network_decode_control(g_packet, &size),
			  0);
	size = 4;
	XVT_ASSERT_INT_EQ(xvt_flight_network_decode_control(g_packet, &size),
			  0);

	/* A flight data packet carries its cookie in its own header (flight_messages.h) and passes as it
	 * is, since Receive takes what DecodeControl passes. */
	struct xvt_flight_input_wire record;
	struct flight_input_frame_record input = controls(4);
	xvt_flight_wire_encode_input(&record, 4, &input);
	size = (int)xvt_flight_messages_encode_batch(g_packet, cookie, &record,
						     1);
	int batch = size;
	XVT_ASSERT_INT_EQ(xvt_flight_network_decode_control(g_packet, &size),
			  1);
	XVT_ASSERT_INT_EQ(size, batch);
}

static void check_next_wake_delay(void)
{
	/* Nothing pending, outgoing or staged: one world message interval. The host clock sits on a whole
	 * millisecond with the frame-delta clock reset, so no part of an interval has passed. */
	flight_network_world(1);
	uint64_t interval =
		(uint64_t)XVT_WORLD_MESSAGE_TICKS * XVT_FLIGHT_TICK_US;
	XVT_ASSERT_TRUE(xvt_flight_network_next_wake_delay_us(0) == interval);
	/* A pending message outside recovery: work now. */
	XVT_ASSERT_INT_EQ(xvt_flight_messages_push(XVT_QUEUE_PENDING, "p", 1),
			  1);
	XVT_ASSERT_TRUE(xvt_flight_network_next_wake_delay_us(0) == 0);
	/* During recovery pending messages do not wait for work. */
	xvt_flight_network_request_recovery();
	XVT_ASSERT_TRUE(xvt_flight_network_next_wake_delay_us(0) == interval);
}

enum { PRIME = 1000 };

/* A host whose next world message is due: the schedule was started at PRIME and every connected player
 * has applied input past the next message's tick. */
static void send_ready(void)
{
	flight_network_world(1);
	g_players[1].participation_state = 1;
	add_frame(0, 20, XVT_INPUT_REAL, 1);
	add_frame(1, 20, XVT_INPUT_REAL, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(PRIME), 0);
}

static void check_should_send(void)
{
	/* Once an interval of input time has passed it sends; not before. */
	send_ready();
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME + XVT_WORLD_MESSAGE_TICKS - 1),
			  0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME + XVT_WORLD_MESSAGE_TICKS),
			  1);

	/* The input time is clock-adjusted. */
	send_ready();
	g_flight_net_clock_adjust_accum_ticks = XVT_WORLD_MESSAGE_TICKS;
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(PRIME), 1);

	/* A connected player with no applied input blocks it, as does applied input that does not pass the
	 * next message's tick; a player not connected does not. */
	send_ready();
	g_players[2].participation_state = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME + XVT_WORLD_MESSAGE_TICKS),
			  0);
	add_frame(2, XVT_WORLD_MESSAGE_TICKS, XVT_INPUT_REAL, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME + XVT_WORLD_MESSAGE_TICKS),
			  0);
	add_frame(2, XVT_WORLD_MESSAGE_TICKS + 2, XVT_INPUT_REAL, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME + XVT_WORLD_MESSAGE_TICKS),
			  1);
	send_ready();
	add_frame(3, 2, XVT_INPUT_REAL, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME + XVT_WORLD_MESSAGE_TICKS),
			  1);

	/* Far enough behind, it sends at once, even while a player blocks it. */
	send_ready();
	g_players[2].participation_state = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME + XVT_WORLD_MESSAGE_TICKS),
			  0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(
				  PRIME +
				  (XVT_WORLD_LATE_INTERVALS + 1) *
					  XVT_WORLD_MESSAGE_TICKS +
				  1),
			  1);

	/* Refused while recovery is needed, a resync state request is pending, the pending queue has no room
	 * or start acknowledgements are pending. */
	int due = PRIME + XVT_WORLD_MESSAGE_TICKS;
	send_ready();
	xvt_flight_network_request_recovery();
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 0);
	xvt_flight_network_clear_recovery_request();
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 1);

	send_ready();
	struct xvt_flight_checksum_report_wire report;
	memset(&report, 0, sizeof report);
	xvt_wire_set32(report.checksum.opcode, NET_PACKET_WORLD_CHECKSUM);
	xvt_wire_set32(report.request_state, XVT_CHECKSUM_REQUEST_STATE);
	int aligned[sizeof report / sizeof(int)];
	memcpy(aligned, &report, sizeof report);
	xvt_resync_defer_checksum(dpid(1), aligned);
	XVT_ASSERT_INT_EQ(xvt_resync_has_state_request(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 0);
	xvt_resync_reset();
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 1);

	send_ready();
	while (xvt_flight_messages_push(XVT_QUEUE_PENDING, "x", 1))
		;
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 0);
	xvt_flight_messages_clear(XVT_QUEUE_PENDING);
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 1);

	send_ready();
	g_flight_net_pending_ack_count = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 0);
	g_flight_net_pending_ack_count = 0;
	XVT_ASSERT_INT_EQ(xvt_flight_network_take_world_send_turn(due), 1);
}

/* Takes the oldest pending message into g_out; returns 0 when there is none. */
static int take_pending(void)
{
	if (!xvt_flight_messages_peek(XVT_QUEUE_PENDING, &g_out,
				      sizeof g_out)) {
		return 0;
	}
	xvt_flight_messages_pop(XVT_QUEUE_PENDING);
	return 1;
}

static void check_send_world(void)
{
	/* A host flying alone: the message carries the applied input up to its tick, is queued pending, and
	 * that input is marked unapplied. */
	flight_network_world(1);
	g_flight_net_last_sent_world_message_timestamp = 16;
	add_frame(0, 18, XVT_INPUT_REAL, 1);
	add_frame(0, 20, XVT_INPUT_REAL, 0);
	add_frame(0, 24, XVT_INPUT_REAL, 1);
	add_frame(0, 26, XVT_INPUT_REAL, 1);
	xvt_flight_network_send_world();
	XVT_ASSERT_INT_EQ(take_pending(), 1);
	XVT_ASSERT_INT_EQ(g_out.target_flags & INT32_MAX,
			  16 + XVT_WORLD_MESSAGE_TICKS);
	XVT_ASSERT_INT_EQ(g_out.participant_mask, 0x01);
	XVT_ASSERT_INT_EQ(g_out.count, 2);
	int tick = 0;
	struct flight_input_frame_record input;
	XVT_ASSERT_INT_EQ(xvt_flight_wire_decode_input(&g_out.records[0].input,
						       &tick, &input),
			  1);
	XVT_ASSERT_INT_EQ(tick, 18);
	XVT_ASSERT_INT_EQ(xvt_flight_wire_decode_input(&g_out.records[1].input,
						       &tick, &input),
			  1);
	XVT_ASSERT_INT_EQ(tick, 24);
	XVT_ASSERT_INT_EQ(frame_at(0, 18)->awaiting_relay, 0);
	XVT_ASSERT_INT_EQ(frame_at(0, 24)->awaiting_relay, 0);
	XVT_ASSERT_INT_EQ(frame_at(0, 26)->awaiting_relay, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);

	/* The next message is XVT_WORLD_MESSAGE_TICKS after that one. */
	xvt_time_advance_host_clock(1);
	xvt_flight_network_begin_iteration();
	xvt_flight_network_send_world();
	XVT_ASSERT_INT_EQ(take_pending(), 1);
	XVT_ASSERT_INT_EQ(g_out.target_flags & INT32_MAX,
			  16 + 2 * XVT_WORLD_MESSAGE_TICKS);
	XVT_ASSERT_INT_EQ(g_out.count, 1);
}

static void check_send_world_refusals(void)
{
	/* During recovery: nothing. */
	flight_network_world(1);
	xvt_flight_network_request_recovery();
	xvt_flight_network_send_world();
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(g_flight_net_last_sent_world_message_timestamp, 0);

	/* No player to include: the only connected player has aborted. */
	flight_network_world(1);
	g_player_abort_flags[0] = 1;
	xvt_flight_network_send_world();
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(g_flight_net_last_sent_world_message_timestamp, 0);
	flight_network_world(1);
	g_players[0].participation_state = 0;
	xvt_flight_network_send_world();
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);

	/* A tick past the valid range ends the mission. */
	flight_network_world(1);
	g_flight_net_last_sent_world_message_timestamp = INT32_MAX - 1;
	xvt_flight_network_send_world();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);

	/* A full pending queue requests recovery. */
	flight_network_world(1);
	while (xvt_flight_messages_push(XVT_QUEUE_PENDING, "x", 1))
		;
	unsigned full = xvt_flight_messages_count(XVT_QUEUE_PENDING);
	xvt_flight_network_send_world();
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), full);
	XVT_ASSERT_INT_EQ(g_flight_net_last_sent_world_message_timestamp, 0);
}

static void check_checksum_flag(void)
{
	/* Messages are flagged for a checksum every XVT_WORLD_CHECKSUM_TICKS, to within one message. */
	flight_network_world(1);
	int flagged[8], count = 0;
	for (int i = 0;
	     i < 3 * XVT_WORLD_CHECKSUM_TICKS / XVT_WORLD_MESSAGE_TICKS; ++i) {
		xvt_time_advance_host_clock(1);
		xvt_flight_network_begin_iteration();
		xvt_flight_network_send_world();
		XVT_ASSERT_INT_EQ(take_pending(), 1);
		if (g_out.target_flags & XVT_WORLD_CHECKSUM_FLAG) {
			XVT_ASSERT_TRUE(count < 8);
			flagged[count++] =
				(int)(g_out.target_flags & INT32_MAX);
		}
	}
	XVT_ASSERT_TRUE(count >= 2);
	for (int i = 1; i < count; ++i) {
		int gap = flagged[i] - flagged[i - 1];
		XVT_ASSERT_TRUE(gap >= XVT_WORLD_CHECKSUM_TICKS -
					       XVT_WORLD_MESSAGE_TICKS);
		XVT_ASSERT_TRUE(gap <= XVT_WORLD_CHECKSUM_TICKS +
					       XVT_WORLD_MESSAGE_TICKS);
	}
}

int main(void)
{
	check_cookie();
	check_options_alone();
	check_start_alone();
	check_session();
	check_recovery_flags();
	check_player_abort();
	check_packet_budget();
	check_begin_mission();
	check_admit_input_refusals();
	check_insert_world();
	check_receive_other();
	check_receive_batch();
	check_receive_world();
	check_decode_control();
	check_next_wake_delay();
	check_should_send();
	check_send_world();
	check_send_world_refusals();
	check_checksum_flag();
	flight_network_world(0);
	xvt_flight_network_clear_cookies();
	return 0;
}

/* Network125 flight networking in flight (flight_network.h). The
 * exchanges before flight, and the mission cookie they agree on, are in
 * flight_network_exchange.c. */
#include "xvt_runtime/runtime/flight_network.h"

#include "xvt/net/net_session_send.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/host_clock.h"

static int xvt_flight_network_has_cookie(unsigned opcode)
{
	switch (opcode) {
	case NET_PACKET_PLAYER_DISCONNECTED:
	case NET_PACKET_WORLD_CHECKSUM:
	case NET_PACKET_SESSION_ABORT:
	case NET_PACKET_RESYNC_CHUNK_ACK:
	case NET_PACKET_FLIGHT_MISSION_START:
	case NET_PACKET_MISSION_LOADING_READY:
	case NET_PACKET_PLAYER_ABORT:
	case NET_PACKET_RESYNC_NOTICE:
	case NET_PACKET_SERVER_CHECKSUM:
	case NET_PACKET_ACK:
	case NET_PACKET_CLOCK_LEAD:
	case NET_PACKET_STILL_LOADING:
	case NET_PACKET_CLOCK_PROBE:
	case NET_PACKET_CLOCK_PROBE_REPLY:
	case NET_PACKET_RESYNC_CHECKSUMS:
	case NET_PACKET_RESYNC_REQUEST:
	case NET_PACKET_RESYNC_APPLY:
	case NET_PACKET_RESYNC_CHUNK:
		return 1;
	default:
		return 0;
	}
}

int xvt_flight_network_send_packet(int dpid, const unsigned *packet, int size)
{
	if (!xvt_flight_network_cookie() || size < 4 ||
	    !xvt_flight_network_has_cookie(packet[0])) {
		return net_session_send_packet(dpid, (unsigned *)packet, size);
	}
	if (size > XVT_FLIGHT_PACKET_BYTES - (int)sizeof(xvt_wire_u32)) {
		XVT_LOG_WARN(
			"network.packet_too_large to=\"player\" packet=%u bytes=%d limit=%d",
			packet[0], size,
			XVT_FLIGHT_PACKET_BYTES - (int)sizeof(xvt_wire_u32));
		return 0;
	}
	unsigned copy[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	memcpy(copy, packet, size);
	xvt_wire_set32((uint8_t *)copy + size, xvt_flight_network_cookie());
	return net_session_send_packet(dpid, copy,
				       size + (int)sizeof(xvt_wire_u32));
}

int xvt_flight_network_broadcast(const unsigned *packet, int size)
{
	if (!xvt_flight_network_cookie() || size < 4 ||
	    !xvt_flight_network_has_cookie(packet[0])) {
		return net_session_broadcast_packet_to_players(
			(unsigned *)packet, size);
	}
	if (size > XVT_FLIGHT_PACKET_BYTES - (int)sizeof(xvt_wire_u32)) {
		XVT_LOG_WARN(
			"network.packet_too_large to=\"all\" packet=%u bytes=%d limit=%d",
			packet[0], size,
			XVT_FLIGHT_PACKET_BYTES - (int)sizeof(xvt_wire_u32));
		return 0;
	}
	unsigned copy[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	memcpy(copy, packet, size);
	xvt_wire_set32((uint8_t *)copy + size, xvt_flight_network_cookie());
	return net_session_broadcast_packet_to_players(
		copy, size + (int)sizeof(xvt_wire_u32));
}

int xvt_flight_network_decode_control(const uint8_t *packet, int *size)
{
	if (*size < (int)sizeof(xvt_wire_u32) ||
	    *size > XVT_FLIGHT_PACKET_BYTES) {
		return 0;
	}
	unsigned opcode = xvt_wire_get32(packet);
	if (xvt_flight_network_has_cookie(opcode)) {
		if (!xvt_flight_network_cookie() ||
		    *size < (int)(2 * sizeof(xvt_wire_u32)) ||
		    xvt_wire_get32(packet + *size - sizeof(xvt_wire_u32)) !=
			    xvt_flight_network_cookie()) {
			return 0;
		}
		*size -= sizeof(xvt_wire_u32);
	}
	size_t minimum = sizeof(xvt_wire_u32);
	switch (opcode) {
	case NET_PACKET_PLAYER_DISCONNECTED:
	case NET_PACKET_RESYNC_CHUNK_ACK:
	case NET_PACKET_PLAYER_ABORT:
	case NET_PACKET_RESYNC_NOTICE:
	case NET_PACKET_CLOCK_LEAD:
	case NET_PACKET_CLOCK_PROBE_REPLY:
		minimum = 2 * sizeof(xvt_wire_u32);
		break;
	case NET_PACKET_CLOCK_PROBE:
		minimum = sizeof(struct xvt_flight_clock_probe_wire);
		break;
	case NET_PACKET_RESYNC_REQUEST:
		minimum = sizeof(struct xvt_flight_resync_request_wire);
		break;
	case NET_PACKET_RESYNC_APPLY:
		minimum = sizeof(struct xvt_flight_resync_apply_wire);
		break;
	case NET_PACKET_RESYNC_CHUNK:
		minimum = sizeof(struct xvt_flight_chunk_header) +
			  sizeof(xvt_wire_u32);
		break;
	case NET_PACKET_WORLD_CHECKSUM:
		minimum = sizeof(struct xvt_flight_checksum_report_wire);
		break;
	case NET_PACKET_SERVER_CHECKSUM:
		minimum = sizeof(struct xvt_flight_checksum_wire);
		break;
	case NET_PACKET_RESYNC_CHECKSUMS:
		minimum = sizeof(struct xvt_flight_epoch_wire);
		break;
	default:
		break;
	}
	return (size_t)*size >= minimum;
}

int xvt_flight_network_send_wire(int dpid, const void *packet, size_t size)
{
	unsigned aligned[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	if (size < sizeof(xvt_wire_u32) || size > sizeof aligned) {
		return 0;
	}
	memcpy(aligned, packet, size);
	return xvt_flight_network_send_packet(dpid, aligned, (int)size);
}

int xvt_flight_network_broadcast_wire(const void *packet, size_t size)
{
	unsigned aligned[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	if (size < sizeof(xvt_wire_u32) || size > sizeof aligned) {
		return 0;
	}
	memcpy(aligned, packet, size);
	return xvt_flight_network_broadcast(aligned, (int)size);
}

/* Flight-data staging is separate from transport sequencing. */

static struct {
	struct xvt_flight_message outgoing;
	unsigned part;
	unsigned parts;
	unsigned batch_count;
	unsigned batch_sends;
	unsigned part_sends;
	unsigned packets_received;
	struct xvt_flight_input_wire batch[XVT_INPUT_STAGED_RECORDS];
	int sampled;
	int last_flush;
	int recovery_requested;
	unsigned departures;
	uint64_t iteration_start_us;
	struct flight_input_frame_record held;
} g_io;

static int
xvt_flight_network_record_input(unsigned player, int tick,
				const struct flight_input_frame_record *input,
				int authoritative)
{
	xvt_input_insert_status result = xvt_flight_history_insert_real(
		player, tick, input, authoritative);
	if (result == XVT_INPUT_FULL || result == XVT_INPUT_INVALID ||
	    result == XVT_INPUT_CONFLICT) {
		XVT_LOG_DEBUG(
			"network.input_rejected slot=%u tick=%d result=\"%s\" authoritative=%d",
			player, tick,
			result == XVT_INPUT_FULL      ? "full"
			: result == XVT_INPUT_INVALID ? "invalid"
						      : "conflict",
			authoritative);
		xvt_flight_network_request_recovery();
		return 0;
	}
	return 1;
}

int xvt_flight_network_needs_recovery(void) { return g_io.recovery_requested; }

void xvt_flight_network_request_recovery(void)
{
	if (!g_io.recovery_requested) {
		XVT_LOG_WARN("network.recovery_needed");
	}
	g_io.recovery_requested = 1;
}

void xvt_flight_network_clear_recovery_request(void)
{
	g_io.recovery_requested = 0;
}

void xvt_flight_network_recovered(void)
{
	g_io.recovery_requested = 0;
	g_io.sampled = 0;
	unsigned retained = 0;
	for (unsigned i = 0; i < g_io.batch_count; ++i) {
		if (xvt_wire_get32(g_io.batch[i].tick) >
		    (unsigned)g_game_time) {
			g_io.batch[retained++] = g_io.batch[i];
		}
	}
	g_io.batch_count = retained;
}

void xvt_flight_network_begin_iteration(void)
{
	uint64_t now_us = xvt_time_get_elapsed_us();
	if (g_io.iteration_start_us == now_us) {
		return;
	}
	g_io.iteration_start_us = now_us;
	g_io.sampled = 0;
	g_io.batch_sends = 0;
	g_io.part_sends = 0;
	g_io.packets_received = 0;
}

void xvt_flight_network_reset_mission(void)
{
	xvt_flight_messages_reset();
	memset(&g_io, 0, sizeof g_io);
	g_io.iteration_start_us = UINT64_MAX;
}

void xvt_flight_network_flush_input(int now)
{
	while (g_io.batch_count &&
	       g_io.batch_sends < XVT_INPUT_BATCHES_PER_ITERATION &&
	       (now - g_io.last_flush >= XVT_INPUT_BATCH_TICKS ||
		g_io.batch_count >= XVT_INPUT_BATCH_RECORDS)) {
		unsigned count = g_io.batch_count < XVT_INPUT_BATCH_RECORDS
					 ? g_io.batch_count
					 : XVT_INPUT_BATCH_RECORDS;
		unsigned packet[(sizeof(struct xvt_flight_batch_header) +
				 XVT_INPUT_BATCH_RECORDS *
					 sizeof(struct xvt_flight_input_wire)) /
				sizeof(unsigned)];
		size_t size = xvt_flight_messages_encode_batch(
			(uint8_t *)packet, xvt_flight_network_cookie(),
			g_io.batch, count);
		int host = net_session_get_host_dplay_id();
		for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
			if (i == (unsigned)g_local_player ||
			    !g_players[i].participation_state) {
				continue;
			}
			int dpid = g_players[i].network.direct_play_id;
			int send =
				!g_internet_play_enabled
					? g_player_connected[i]
					: dpid == host ||
						  (g_flight_net_small_session_player_threshold >
							   g_active_flight_player_count &&
						   g_player_connected[i]);
			if (send) {
				net_session_send_packet(dpid, packet,
							(int)size);
			}
		}
		g_io.batch_count -= count;
		XVT_LOG_DEBUG("network.batch_sent records=%u left=%u", count,
			      g_io.batch_count);
		memmove(g_io.batch, g_io.batch + count,
			g_io.batch_count * sizeof g_io.batch[0]);
		++g_io.batch_sends;
		g_io.last_flush = now;
	}
}

int xvt_flight_network_admit_input(int tick)
{
	if (g_io.recovery_requested ||
	    !xvt_flight_wire_valid_tick((unsigned)tick) ||
	    g_io.batch_count == XVT_INPUT_STAGED_RECORDS) {
		return 0;
	}
	/* Replay of retained local history must not consume a second device command. */
	for (int i = 0; i < g_input_frame_count[g_local_player]; ++i) {
		if (g_input_history[g_local_player][i].timestamp == tick) {
			return 1;
		}
	}
	if (g_input_frame_count[g_local_player] >= XVT_INPUT_HISTORY_CAPACITY) {
		xvt_flight_network_request_recovery();
		return 0;
	}
	if (!g_io.sampled) {
		xvt_flight_controls_sample_recorded(&g_io.held);
		g_io.sampled = 1;
	}
	if (!xvt_flight_network_record_input(g_local_player, tick, &g_io.held,
					     0)) {
		return 0;
	}
	XVT_LOG_DEBUG(
		"network.input tick=%d key=%u flags=%u x=%d y=%d r=%d mods=%u throttle=%u",
		tick, g_io.held.key, g_io.held.flags, g_io.held.axis_x,
		g_io.held.axis_y, g_io.held.axis_r, g_io.held.key_mods,
		g_io.held.throttle);
	xvt_flight_wire_encode_input(&g_io.batch[g_io.batch_count++], tick,
				     &g_io.held);
	g_io.held.key = 0;
	g_io.held.flags = 0;
	g_io.held.throttle = 0;
	return 1;
}

int xvt_flight_network_outgoing(void) { return g_io.parts != 0; }

void xvt_flight_network_flush_world(void)
{
	while (g_io.parts && g_io.part_sends < XVT_WORLD_PARTS_PER_ITERATION) {
		unsigned packet[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
		size_t size = xvt_flight_messages_encode_part(
			(uint8_t *)packet, &g_io.outgoing,
			xvt_flight_network_cookie(), g_io.part);
		for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
			if (i != (unsigned)g_local_player &&
			    (g_io.outgoing.participant_mask & (1u << i))) {
				net_session_send_packet(
					g_players[i].network.direct_play_id,
					packet, (int)size);
			}
		}
		++g_io.part_sends;
		if (++g_io.part == g_io.parts) {
			g_io.parts = 0;
			g_io.part = 0;
		}
	}
}

void xvt_flight_network_send_world(void)
{
	if (g_io.parts || g_io.recovery_requested) {
		return;
	}
	struct xvt_flight_message *message = &g_io.outgoing;
	memset(message, 0, sizeof *message);
	if (g_flight_net_last_sent_world_message_timestamp >
	    INT32_MAX - XVT_WORLD_MESSAGE_TICKS - 1) {
		g_flight_mission_state.mission_end_pending = 1;
		return;
	}
	int tick = g_flight_net_last_sent_world_message_timestamp +
		   XVT_WORLD_MESSAGE_TICKS;
	if (!xvt_flight_wire_valid_tick((unsigned)tick)) {
		g_flight_mission_state.mission_end_pending = 1;
		return;
	}
	message->target_flags = (unsigned)tick;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (!g_players[player].participation_state ||
		    g_player_abort_flags[player] ||
		    (g_io.departures & (1u << player))) {
			continue;
		}
		message->participant_mask |= 1u << player;
		for (int i = 0; i < g_input_frame_count[player]; ++i) {
			struct input_frame *frame = &g_input_history[player][i];
			if (!frame->awaiting_relay || frame->timestamp > tick) {
				continue;
			}
			struct xvt_flight_world_input_wire *record =
				&message->records[message->count++];
			record->player = player;
			xvt_flight_wire_encode_input(&record->input,
						     frame->timestamp,
						     &frame->input);
		}
	}
	if (!message->participant_mask) {
		return;
	}
	g_flight_net_checksum_request_accum_ticks += XVT_WORLD_MESSAGE_TICKS;
	if (g_flight_net_checksum_request_accum_ticks >
	    XVT_WORLD_CHECKSUM_TICKS) {
		g_flight_net_checksum_request_accum_ticks = 0;
		message->target_flags |= XVT_WORLD_CHECKSUM_FLAG;
		memset(g_flight_net_world_checksum_peer_status, 0,
		       sizeof g_flight_net_world_checksum_peer_status);
	}
	if (!xvt_flight_messages_enqueue(message, XVT_QUEUE_PENDING)) {
		xvt_flight_network_request_recovery();
		return;
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		for (int i = 0; i < g_input_frame_count[player]; ++i) {
			struct input_frame *frame = &g_input_history[player][i];
			if ((message->participant_mask & (1u << player)) &&
			    frame->awaiting_relay && frame->timestamp <= tick) {
				frame->awaiting_relay = 0;
			}
		}
	}
	g_flight_net_last_sent_world_message_timestamp = tick;
	++g_flight_net_sent_world_message_count;
	XVT_LOG_DEBUG(
		"network.world_sent tick=%d mask=%02x records=%u checksum=%d",
		tick, message->participant_mask, message->count,
		(message->target_flags & XVT_WORLD_CHECKSUM_FLAG) != 0);
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (player == (unsigned)g_local_player ||
		    !(message->participant_mask & (1u << player)) ||
		    g_flight_net_peer_silence_ticks[player] == -1) {
			continue;
		}
		g_flight_net_peer_silence_ticks[player] +=
			XVT_WORLD_MESSAGE_TICKS;
		if (g_flight_net_peer_silence_ticks[player] >
		    XVT_PEER_TIMEOUT_TICKS) {
			XVT_LOG_WARN("network.player_silent slot=%u ticks=%d",
				     player,
				     g_flight_net_peer_silence_ticks[player]);
			flight_net_broadcast_player_abort(player);
			g_io.departures |= 1u << player;
		}
	}
	g_io.parts = xvt_flight_messages_part_count(message->count);
	xvt_flight_network_flush_world();
}

int xvt_flight_network_insert_world(const struct xvt_flight_message *message)
{
	for (unsigned i = 0; i < message->count; ++i) {
		const struct xvt_flight_world_input_wire *record =
			&message->records[i];
		int tick;
		struct flight_input_frame_record input;
		if (!xvt_flight_wire_decode_input(&record->input, &tick,
						  &input) ||
		    !xvt_flight_network_record_input(record->player, tick,
						     &input, 1)) {
			return 0;
		}
	}
	return 1;
}

int xvt_flight_network_receive(int sender, const uint8_t *bytes, size_t size)
{
	if (size < 4) {
		return 0;
	}
	unsigned opcode = xvt_wire_get32(bytes);
	if (opcode == NET_PACKET_INPUT_BATCH) {
		int player = net_session_find_player_slot_by_dpid(sender);
		if ((unsigned)player >= XVT_FLIGHT_PLAYERS ||
		    player == g_local_player ||
		    !g_players[player].participation_state ||
		    !xvt_flight_messages_validate_batch(
			    bytes, size, xvt_flight_network_cookie())) {
			XVT_LOG_DEBUG(
				"network.batch_dropped slot=%d reason=\"invalid\"",
				player);
			return 1;
		}
		if (xvt_resync_holds_input()) {
			XVT_LOG_DEBUG(
				"network.batch_dropped slot=%d reason=\"resync\"",
				player);
			return 1;
		}
		flight_sync_discard_predicted_input_frames(player);
		g_flight_net_peer_silence_ticks[player] = 0;
		struct xvt_flight_batch_header header;
		memcpy(&header, bytes, sizeof header);
		unsigned count = xvt_wire_get16(header.count);
		/* The wire records are byte arrays, so the cast needs no alignment. */
		const struct xvt_flight_input_wire *records =
			(const struct xvt_flight_input_wire *)(bytes +
							       sizeof header);
		XVT_LOG_DEBUG(
			"network.batch_received slot=%d records=%u first=%u last=%u",
			player, count,
			count ? xvt_wire_get32(records[0].tick) : 0u,
			count ? xvt_wire_get32(records[count - 1].tick) : 0u);
		for (unsigned i = 0; i < count; ++i) {
			struct xvt_flight_input_wire record;
			memcpy(&record,
			       bytes + sizeof header + i * sizeof record,
			       sizeof record);
			int tick;
			struct flight_input_frame_record input;
			xvt_flight_wire_decode_input(&record, &tick, &input);
			if (!xvt_flight_network_record_input(player, tick,
							     &input, 0)) {
				break;
			}
		}
		return 1;
	}
	if (opcode != NET_PACKET_WORLD_MESSAGE) {
		return opcode == NET_PACKET_REMOTE_INPUT;
	}
	if (sender != net_session_get_host_dplay_id()) {
		return 1;
	}
	static struct xvt_flight_message message;
	int result = xvt_flight_messages_receive_part(
		bytes, size, xvt_flight_network_cookie(),
		xvt_resync_receive_floor(), &message);
	if (result < 0) {
		XVT_LOG_DEBUG("network.world_rejected reason=\"part\"");
		xvt_flight_network_request_recovery();
	}
	if (result == 1) {
		if ((message.participant_mask &
		     ~xvt_flight_checkpoint_initial_mask()) ||
		    !xvt_flight_messages_enqueue(
			    &message,
			    xvt_resync_is_active() &&
					    !net_session_is_local_host()
				    ? XVT_QUEUE_REPLAY
				    : XVT_QUEUE_PENDING)) {
			XVT_LOG_DEBUG("network.world_rejected reason=\"%s\"",
				      (message.participant_mask &
				       ~xvt_flight_checkpoint_initial_mask())
					      ? "mask"
					      : "queue_full");
			xvt_flight_network_request_recovery();
		} else {
			XVT_LOG_DEBUG(
				"network.world_received tick=%u mask=%02x records=%u queue=\"%s\"",
				(unsigned)(message.target_flags & INT32_MAX),
				message.participant_mask, message.count,
				xvt_resync_is_active() &&
						!net_session_is_local_host()
					? "replay"
					: "pending");
			g_flight_net_host_timeout_elapsed_ticks = 0;
			++g_flight_net_received_world_message_count;
		}
	}
	return 1;
}

int xvt_flight_network_player_abort(unsigned player)
{
	if (player >= XVT_FLIGHT_PLAYERS) {
		return 0;
	}
	XVT_LOG_INFO("network.player_left slot=%u local=%d", player,
		     player == (unsigned)g_local_player);
	if (net_session_is_local_host()) {
		g_io.departures |= 1u << player;
	}
	return player != (unsigned)g_local_player;
}

uint64_t xvt_flight_network_next_wake_delay_us(int now)
{
	if (g_io.parts || (xvt_flight_messages_count(XVT_QUEUE_PENDING) &&
			   !g_io.recovery_requested)) {
		return 0;
	}
	if (g_io.batch_count) {
		int remaining = XVT_INPUT_BATCH_TICKS - (now - g_io.last_flush);
		return remaining > 0 ? xvt_flight_time_delay_for_ticks(
					       (unsigned)remaining)
				     : 0;
	}
	return xvt_flight_time_delay_for_ticks(XVT_WORLD_MESSAGE_TICKS);
}

int xvt_flight_network_take_world_send_turn(int input_timestamp)
{
	if (xvt_flight_network_outgoing() ||
	    xvt_flight_network_needs_recovery() ||
	    xvt_resync_has_state_request() ||
	    !xvt_flight_messages_has_room(XVT_QUEUE_PENDING,
					  sizeof(struct xvt_flight_message))) {
		return 0;
	}
	if (g_flight_net_pending_ack_count) {
		return 0;
	}
	int adjusted = input_timestamp + g_flight_net_clock_adjust_accum_ticks;
	if (!g_flight_net_world_message_turn_timestamp) {
		g_flight_net_world_message_turn_timestamp =
			adjusted + (g_flight_net_clock_lead_ticks >>
				    XVT_WORLD_START_LEAD_SHIFT);
	}
	int elapsed = adjusted - g_flight_net_world_message_turn_timestamp;
	int interval = XVT_WORLD_MESSAGE_TICKS;
	if (elapsed < interval) {
		return 0;
	}
	if (elapsed > XVT_WORLD_LATE_INTERVALS * interval) {
		g_flight_net_world_message_turn_timestamp += interval;
		return 1;
	}
	int oldest = INT32_MAX;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (!g_players[player].participation_state) {
			continue;
		}
		const struct input_frame *input =
			flight_sync_find_last_unrelayed_input_frame(player);
		if (!input) {
			oldest = 0;
			break;
		}
		if (input->timestamp < oldest) {
			oldest = input->timestamp;
		}
	}
	if (g_flight_net_last_sent_world_message_timestamp + interval >=
	    oldest) {
		return 0;
	}
	g_flight_net_world_message_turn_timestamp += interval;
	return 1;
}

int xvt_flight_network_take_packet_budget(void)
{
	xvt_flight_network_begin_iteration();
	if (g_io.packets_received >= XVT_NETWORK_PACKETS_PER_ITERATION) {
		return 0;
	}
	++g_io.packets_received;
	return 1;
}

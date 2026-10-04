#include "xvt_runtime/runtime/resync_task.h"

#include <stdlib.h>

#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/world_state.h"
#include "xvt_runtime/timing/flight_timing.h"

enum {
	RESYNC_IDLE,
	RESYNC_CHECKSUMS,
	RESYNC_BUILD,
	RESYNC_ACKS,
	RESYNC_APPLY,
	RESYNC_FULL_RECEIVE,
	RESYNC_REPLAY
};

static struct {
	int phase;
	int peer_dpid;
	int image_size;
	int checksum_elapsed;
	int apply_sent_tick;
	int pulse;
	int retries;
	int blink;
	int offset;
	int chunk_index;
	int free_bytes;
	int payload_offset;
	int ack_count;
	int ack_previous;
	int ack_retries;
	int ack_elapsed;
	int final_batch;
	int owns_alert;
	uint8_t *world;
	uint8_t *pinned;
	unsigned checksums[XVT_WORLD_CHECKSUM_REGIONS];
	unsigned lengths[XVT_WORLD_CHECKSUM_REGIONS];
	unsigned epoch;
	int completed_tick;
	int restart_requested;
} g_resync;

static struct {
	int sender;
	struct xvt_flight_checksum_report_wire packet;
} g_deferred_checksum_reports[XVT_DEFERRED_CHECKSUMS];

static unsigned g_checksum_read;
static unsigned g_checksum_count;

void xvt_resync_defer_checksum(int sender, const int *packet)
{
	/* Other peers' checksum continuations resume after the current transfer. */
	if (g_checksum_count == XVT_DEFERRED_CHECKSUMS) {
		XVT_LOG_ERROR("network.checksum_queue_full");
		g_flight_mission_state.mission_end_pending = 1;
		return;
	}
	unsigned index =
		(g_checksum_read + g_checksum_count++) % XVT_DEFERRED_CHECKSUMS;
	g_deferred_checksum_reports[index].sender = sender;
	memcpy(&g_deferred_checksum_reports[index].packet, packet,
	       sizeof(struct xvt_flight_checksum_report_wire));
}

/* The roster slot of the player the host is sending to; logs name slots, not network ids. */
static int xvt_resync_peer_slot(void)
{
	return net_session_find_player_slot_by_dpid(g_resync.peer_dpid);
}

static int xvt_resync_take_escape_key(void)
{
	return flight_input_has_key_ready() &&
	       flight_input_get_next_key() == FLIGHT_KEY_ESCAPE;
}

static void xvt_resync_complete_checksum(void)
{
	if (net_session_is_local_host()) {
		int index = net_session_find_player_slot_by_dpid(
			g_resync.peer_dpid);
		if ((unsigned)index < 8) {
			g_flight_net_world_checksum_peer_status[index] = 2;
		}
		int status = 3;
		for (int i = 0; i < 8; ++i) {
			if (g_players[i].participation_state) {
				status &=
					g_flight_net_world_checksum_peer_status
						[i];
			}
		}
		if (status & 1) {
			g_flight_net_buffer_world_messages_until_checksum = 0;
		}
	}
	free(g_resync.pinned);
	memset(&g_resync, 0, sizeof(g_resync));
}

static void xvt_resync_pulse(int resending)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_STILL_LOADING;
	xvt_flight_network_broadcast((unsigned *)&g_flight_net_scratch_packet,
				     sizeof(int));
	g_resync.blink = !g_resync.blink;
	flight_alert_draw_box(
		3,
		g_str_disk_io_messages
			[g_resync.blink ? DISK_IO_STR_ESC_BOOT_PLAYER
			 : resending	? DISK_IO_STR_RESENDING_PACKET_WAIT
					: DISK_IO_STR_RECOVERING_WAIT],
		0x30);
}

static void xvt_resync_end_send(int success)
{
	flight_alert_restore_box_background();
	g_resync.owns_alert = 0;
	time_consume_elapsed_ticks();
	g_flight_net_pending_ack_count = 0;
	if (success) {
		xvt_resync_begin_apply(g_resync.peer_dpid, g_resync.image_size);
	} else {
		xvt_resync_complete_checksum();
	}
}

static struct {
	unsigned checksums[XVT_WORLD_CHECKSUM_REGIONS];
	unsigned lengths[XVT_WORLD_CHECKSUM_REGIONS];
	unsigned epoch;
	int table_valid;
	int size;
	int tick;
	int request_sent;
	int restarts;
	int restart_pending;
	uint64_t deadline;
} g_receive;

static void xvt_resync_send_request(void)
{
	struct xvt_flight_checksum_wire table;
	xvt_wire_set32(table.opcode, NET_PACKET_SERVER_CHECKSUM);
	xvt_wire_set32(table.epoch, g_resync.epoch);
	for (unsigned region = 0; region < XVT_WORLD_CHECKSUM_REGIONS;
	     ++region) {
		xvt_wire_set32(table.checksums[region],
			       g_resync.checksums[region]);
		xvt_wire_set32(table.lengths[region], g_resync.lengths[region]);
	}
	xvt_flight_network_send_wire(g_resync.peer_dpid, &table, sizeof table);
	struct xvt_flight_resync_request_wire request;
	xvt_wire_set32(request.opcode, NET_PACKET_RESYNC_REQUEST);
	xvt_wire_set32(request.epoch, g_resync.epoch);
	xvt_wire_set32(request.image_bytes, g_resync.image_size);
	xvt_wire_set32(request.completed_tick, g_resync.completed_tick);
	xvt_flight_network_send_wire(g_resync.peer_dpid, &request,
				     sizeof request);
}

int xvt_resync_begin_send(int peer_dpid, uint8_t *world, int size)
{
	if (g_resync.phase) {
		return -1;
	}
	free(g_resync.pinned);
	memset(&g_resync, 0, sizeof(g_resync));
	g_resync.peer_dpid = peer_dpid;
	g_resync.world = world;
	g_resync.image_size = size;
	g_resync.phase = RESYNC_CHECKSUMS;
	g_resync.retries = XVT_RESYNC_RETRIES;
	g_input_timestamp += time_consume_elapsed_ticks();
	flight_alert_save_box_background();
	g_resync.owns_alert = 1;
	char text[256];
	strcpy(text, g_str_disk_io_messages[DISK_IO_STR_COM_FAILURE_SENDING]);
	char *name = net_session_get_player_name(
		net_session_find_player_slot_by_dpid(peer_dpid));
	if (name) {
		strcat(text, name);
	}
	flight_alert_draw_box(1, text, 0x30);
	g_flight_net_scratch_packet.packet_type = NET_PACKET_RESYNC_NOTICE;
	g_flight_net_scratch_packet.payload_dwords[0] = peer_dpid;
	xvt_flight_network_broadcast((unsigned *)&g_flight_net_scratch_packet,
				     8);
	size_t prefix;
	if (!xvt_flight_checkpoint_validate(world, size, &prefix,
					    &g_resync.completed_tick)) {
		XVT_LOG_WARN("resync.send_failed slot=%d reason=\"checkpoint\"",
			     xvt_resync_peer_slot());
		xvt_resync_end_send(0);
		return 0;
	}
	g_resync.pinned = malloc((size_t)size);
	if (!g_resync.pinned) {
		XVT_LOG_WARN("resync.send_failed slot=%d reason=\"memory\"",
			     xvt_resync_peer_slot());
		xvt_resync_end_send(0);
		return 0;
	}
	memcpy(g_resync.pinned, world, size);
	g_resync.world = g_resync.pinned;
	g_resync.epoch = (unsigned)g_resync.completed_tick;
	if (!xvt_snapshot_checksum_image(world, size, g_resync.checksums,
					 g_resync.lengths)) {
		XVT_LOG_WARN("resync.send_failed slot=%d reason=\"checksum\"",
			     xvt_resync_peer_slot());
		xvt_resync_end_send(0);
		return 0;
	}
	XVT_LOG_INFO("resync.send_begin slot=%d bytes=%d tick=%d",
		     xvt_resync_peer_slot(), size, g_resync.completed_tick);
	xvt_resync_send_request();
	g_flight_net_pending_ack_count = 1;
	g_flight_net_remote_resync_checksums_received_flag = 0;
	return -1;
}

static void xvt_resync_new_chunk(void)
{
	struct flight_net_world_state_chunk_packet *packet =
		&g_flight_net_world_state_chunk_packets[g_resync.chunk_index];
	packet->packet_type = NET_PACKET_RESYNC_CHUNK;
	packet->checksum_epoch = (int)g_resync.epoch;
	packet->chunk_index = g_resync.chunk_index;
	g_resync.free_bytes = XVT_FLIGHT_PACKET_BYTES -
			      sizeof(struct xvt_flight_chunk_header) -
			      2 * sizeof(xvt_wire_u32);
	g_resync.payload_offset = 0;
}

static void xvt_resync_send_chunk(void)
{
	struct flight_net_world_state_chunk_packet *packet =
		&g_flight_net_world_state_chunk_packets[g_resync.chunk_index];
	xvt_wire_set32(packet->payload + g_resync.payload_offset, UINT32_MAX);
	size_t packet_bytes = sizeof(struct xvt_flight_chunk_header) +
			      g_resync.payload_offset + sizeof(xvt_wire_u32);
	xvt_flight_network_send_packet(g_resync.peer_dpid, (unsigned *)packet,
				       (int)packet_bytes);
	++g_resync.chunk_index;
}

static void xvt_resync_checksums(void)
{
	int saved = g_input_timestamp;
	if (xvt_resync_take_escape_key()) {
		g_resync.retries = 1;
		g_resync.checksum_elapsed = XVT_RESYNC_RETRY_TICKS;
	} else {
		flight_net_process_incoming_packets();
		if (g_resync.restart_requested) {
			return;
		}
		g_input_timestamp += time_consume_elapsed_ticks();
		g_resync.checksum_elapsed += g_input_timestamp - saved;
		g_input_timestamp = saved;
	}
	if (g_flight_net_remote_resync_checksums_received_flag ||
	    g_resync.checksum_elapsed >= XVT_RESYNC_RETRY_TICKS) {
		g_resync.pulse += g_resync.checksum_elapsed;
		if (g_resync.pulse >= XVT_RESYNC_RETRY_TICKS) {
			g_resync.pulse = 0;
			g_flight_net_scratch_packet.packet_type =
				NET_PACKET_STILL_LOADING;
			xvt_flight_network_send_packet(
				g_resync.peer_dpid,
				(unsigned *)&g_flight_net_scratch_packet, 4);
			xvt_resync_pulse(0);
		}
		if (g_flight_net_remote_resync_checksums_received_flag) {
			memset(g_flight_net_world_state_chunk_acked, 0,
			       sizeof(g_flight_net_world_state_chunk_acked));
			xvt_resync_new_chunk();
			g_resync.phase = RESYNC_BUILD;
		} else if (!--g_resync.retries) {
			XVT_LOG_WARN(
				"resync.peer_dropped slot=%d stage=\"checksums\"",
				xvt_resync_peer_slot());
			flight_net_broadcast_player_abort(
				net_session_find_player_slot_by_dpid(
					g_resync.peer_dpid));
			xvt_resync_end_send(0);
		} else {
			XVT_LOG_DEBUG(
				"resync.retry stage=\"checksums\" left=%d",
				g_resync.retries);
			g_resync.checksum_elapsed = 0;
			g_flight_net_remote_resync_checksums_received_flag = 0;
			xvt_resync_send_request();
		}
	}
}

static void xvt_resync_begin_acks(int final)
{
	XVT_LOG_DEBUG("resync.batch_sent chunks=%d offset=%d bytes=%d final=%d",
		      g_resync.chunk_index, g_resync.offset,
		      g_resync.image_size, final);
	g_resync.final_batch = final;
	g_resync.phase = RESYNC_ACKS;
	g_resync.ack_count = g_resync.chunk_index;
	g_resync.ack_previous = 0;
	g_resync.ack_retries = XVT_RESYNC_ACK_RETRIES;
	g_resync.ack_elapsed = 0;
	g_resync.pulse = 0;
	g_resync.blink = 0;
}

static void xvt_resync_build(void)
{
	/* Send the pinned complete image in bounded batches. */
	while (g_resync.offset < g_resync.image_size) {
		struct flight_net_world_state_chunk_packet *packet =
			&g_flight_net_world_state_chunk_packets
				[g_resync.chunk_index];
		int bytes = g_resync.image_size - g_resync.offset +
			    sizeof(struct
				   flight_net_world_state_chunk_record_header);
		if (bytes > g_resync.free_bytes) {
			bytes = g_resync.free_bytes;
		}
		struct flight_net_world_state_chunk_record_header header = {
			g_resync.offset, bytes - sizeof(header)};
		memcpy(packet->payload + g_resync.payload_offset, &header,
		       sizeof(header));
		memcpy(packet->payload + g_resync.payload_offset +
			       sizeof(header),
		       g_resync.world + g_resync.offset, header.data_size);
		g_resync.offset += header.data_size;
		g_resync.free_bytes -= bytes;
		g_resync.payload_offset += bytes;
		if (g_resync.free_bytes < XVT_RESYNC_CHUNK_MIN_FREE) {
			xvt_resync_send_chunk();
			if (g_resync.chunk_index ==
			    XVT_RESYNC_CHUNKS_PER_BATCH) {
				xvt_resync_begin_acks(g_resync.offset ==
						      g_resync.image_size);
				return;
			}
			xvt_resync_new_chunk();
		}
	}
	if (g_resync.payload_offset != 0) {
		xvt_resync_send_chunk();
		xvt_resync_begin_acks(1);
	} else if (g_resync.chunk_index != 0) {
		xvt_resync_begin_acks(1);
	} else {
		xvt_resync_end_send(1);
	}
}

int xvt_resync_wait_acks(int peer_dpid, int count)
{
	int ack = 0;
	int saved = g_input_timestamp;
	if (xvt_resync_take_escape_key()) {
		g_resync.ack_elapsed = XVT_RESYNC_RETRY_TICKS;
		ack = g_resync.ack_previous;
		g_resync.ack_retries = 1;
	} else {
		flight_net_process_incoming_packets();
		if (g_resync.restart_requested) {
			return -1;
		}
		g_input_timestamp += time_consume_elapsed_ticks();
		g_resync.ack_elapsed += g_input_timestamp - saved;
		g_input_timestamp = saved;
		while (ack < count &&
		       g_flight_net_world_state_chunk_acked[ack]) {
			++ack;
		}
		if (ack == count) {
			return 1;
		}
	}
	if (g_resync.ack_elapsed < XVT_RESYNC_RETRY_TICKS) {
		return -1;
	}
	g_resync.pulse += g_resync.ack_elapsed;
	if (g_resync.pulse >= XVT_RESYNC_RETRY_TICKS) {
		g_resync.pulse = 0;
		xvt_resync_pulse(1);
	}
	if (g_resync.ack_previous == ack) {
		--g_resync.ack_retries;
	} else {
		g_resync.ack_retries = XVT_RESYNC_ACK_RETRIES;
	}
	g_resync.ack_previous = ack;
	g_resync.ack_elapsed = 0;
	if (g_resync.ack_retries) {
		XVT_LOG_DEBUG("resync.retry stage=\"acks\" left=%d",
			      g_resync.ack_retries);
		return -1;
	}
	XVT_LOG_WARN("resync.peer_dropped slot=%d stage=\"acks\"",
		     net_session_find_player_slot_by_dpid(peer_dpid));
	flight_net_broadcast_player_abort(
		net_session_find_player_slot_by_dpid(peer_dpid));
	return 0;
}

void xvt_resync_begin_apply(int peer_dpid, int size)
{
	g_resync.phase = RESYNC_APPLY;
	g_resync.peer_dpid = peer_dpid;
	g_resync.image_size = size;
	g_resync.retries = XVT_RESYNC_RETRIES;
	g_resync.pulse = 0;
	g_resync.apply_sent_tick = g_input_timestamp;
	g_flight_net_scratch_packet.packet_type = NET_PACKET_RESYNC_APPLY;
	g_flight_net_scratch_packet.payload_dwords[0] = (int)g_resync.epoch;
	g_flight_net_scratch_packet.payload_dwords[1] = size;
	g_flight_net_scratch_packet.payload_dwords[2] = g_input_timestamp;
	xvt_flight_network_send_packet(
		peer_dpid, (unsigned *)&g_flight_net_scratch_packet, 16);
	time_consume_elapsed_ticks();
	g_flight_net_pending_ack_count = 1;
}

static void xvt_resync_apply(void)
{
	if (xvt_resync_take_escape_key()) {
		g_resync.retries = 1;
		g_input_timestamp += XVT_RESYNC_RETRY_TICKS;
	} else {
		flight_net_process_incoming_packets();
		if (g_resync.restart_requested) {
			return;
		}
		g_input_timestamp += time_consume_elapsed_ticks();
		if (g_flight_net_world_state_ack_received_flag) {
			g_flight_net_world_state_ack_received_flag = 0;
			g_input_timestamp = g_resync.apply_sent_tick;
		}
	}
	if (g_flight_net_pending_ack_count &&
	    (unsigned)(g_input_timestamp - g_resync.apply_sent_tick) <
		    XVT_RESYNC_RETRY_TICKS) {
		return;
	}
	g_resync.pulse += g_input_timestamp - g_resync.apply_sent_tick;
	if (g_resync.pulse >= XVT_RESYNC_RETRY_TICKS) {
		g_resync.pulse = 0;
		g_flight_net_scratch_packet.packet_type =
			NET_PACKET_STILL_LOADING;
		xvt_flight_network_broadcast(
			(unsigned *)&g_flight_net_scratch_packet, 4);
	}
	if (g_flight_net_pending_ack_count && --g_resync.retries) {
		XVT_LOG_DEBUG("resync.retry stage=\"apply\" left=%d",
			      g_resync.retries);
		g_resync.apply_sent_tick = g_input_timestamp;
		g_flight_net_pending_ack_count = 1;
		struct xvt_flight_resync_apply_wire apply;
		xvt_wire_set32(apply.opcode, NET_PACKET_RESYNC_APPLY);
		xvt_wire_set32(apply.epoch, g_resync.epoch);
		xvt_wire_set32(apply.image_bytes, g_resync.image_size);
		xvt_wire_set32(apply.input_tick, g_input_timestamp);
		xvt_flight_network_send_wire(g_resync.peer_dpid, &apply,
					     sizeof apply);
		return;
	}
	if (g_flight_net_pending_ack_count == 1) {
		XVT_LOG_WARN("resync.peer_dropped slot=%d stage=\"apply\"",
			     xvt_resync_peer_slot());
		flight_net_broadcast_player_abort(
			net_session_find_player_slot_by_dpid(
				g_resync.peer_dpid));
		g_flight_net_pending_ack_count = 0;
	} else {
		XVT_LOG_INFO("resync.send_done slot=%d",
			     xvt_resync_peer_slot());
	}
	g_input_timestamp += time_consume_elapsed_ticks();
	g_input_timestamp = g_flight_net_clock_lead_ticks + g_server_tick_time;
	g_flight_net_scratch_packet.packet_type = NET_PACKET_RESYNC_NOTICE;
	g_flight_net_scratch_packet.payload_dwords[0] = 0;
	xvt_flight_network_broadcast((unsigned *)&g_flight_net_scratch_packet,
				     8);
	xvt_resync_complete_checksum();
}

static void xvt_resync_restart_receive(void)
{
	xvt_flight_network_request_recovery();
	if (++g_receive.restarts > XVT_RESYNC_REPLAY_RESTARTS) {
		XVT_LOG_ERROR("resync.gave_up restarts=%d", g_receive.restarts);
		flight_net_broadcast_player_abort(g_local_player);
		g_flight_mission_state.mission_end_pending = 1;
		xvt_resync_reset();
		return;
	}
	XVT_LOG_WARN("resync.restart count=%d", g_receive.restarts);
	g_resync.phase = RESYNC_IDLE;
	g_receive.request_sent = 0;
	xvt_flight_messages_clear(XVT_QUEUE_PENDING);
	xvt_flight_messages_clear(XVT_QUEUE_REPLAY);
	xvt_flight_frame_reset_replay();
	xvt_resync_service_recovery();
}

static int xvt_resync_ready_checksum(void)
{
	for (unsigned i = 0; i < g_checksum_count; ++i) {
		unsigned index = (g_checksum_read + i) % XVT_DEFERRED_CHECKSUMS;
		const struct xvt_flight_checksum_report_wire *packet =
			&g_deferred_checksum_reports[index].packet;
		int ready =
			(xvt_wire_get32(packet->request_state) ==
					 XVT_CHECKSUM_REQUEST_STATE
				 ? g_server_tick_time >=
					   g_flight_net_last_sent_world_message_timestamp
				 : xvt_wire_get32(packet->checksum.epoch) <=
					   g_flight_net_world_checksum_epoch);
		if (ready) {
			return (int)i;
		}
	}
	return -1;
}

static void xvt_resync_service_checksums(void)
{
	for (unsigned work = 0; work < XVT_NETWORK_PACKETS_PER_ITERATION &&
				g_resync.phase == RESYNC_IDLE;
	     ++work) {
		int ready = xvt_resync_ready_checksum();
		if (ready < 0) {
			return;
		}
		unsigned index = (g_checksum_read + (unsigned)ready) %
				 XVT_DEFERRED_CHECKSUMS;
		int sender = g_deferred_checksum_reports[index].sender;
		int packet[sizeof(struct xvt_flight_checksum_report_wire) /
			   sizeof(int)];
		memcpy(packet, &g_deferred_checksum_reports[index].packet,
		       sizeof packet);
		for (unsigned i = (unsigned)ready; i + 1 < g_checksum_count;
		     ++i) {
			g_deferred_checksum_reports[(g_checksum_read + i) %
						    XVT_DEFERRED_CHECKSUMS] =
				g_deferred_checksum_reports
					[(g_checksum_read + i + 1) %
					 XVT_DEFERRED_CHECKSUMS];
		}
		--g_checksum_count;
		flight_sync_handle_world_checksum_packet(sender, packet);
	}
}

void xvt_resync_update(void)
{
	if (g_resync.phase != RESYNC_IDLE && net_session_is_local_host() &&
	    g_resync.restart_requested) {
		XVT_LOG_DEBUG("resync.send_restart slot=%d",
			      xvt_resync_peer_slot());
		if (g_resync.owns_alert) {
			flight_alert_restore_box_background();
		}
		free(g_resync.pinned);
		memset(&g_resync, 0, sizeof g_resync);
		g_flight_net_pending_ack_count = 0;
	}
	if (g_receive.restart_pending) {
		g_receive.restart_pending = 0;
		xvt_resync_restart_receive();
	}
	if (g_resync.phase == RESYNC_IDLE) {
		xvt_resync_service_checksums();
	}
	if (xvt_flight_timing_is_network125()) {
		xvt_flight_network_begin_iteration();
		xvt_flight_network_flush_world();
	}
	switch (g_resync.phase) {
	case RESYNC_FULL_RECEIVE:
		flight_net_process_incoming_packets();
		if (xvt_flight_network_needs_recovery()) {
			xvt_resync_restart_receive();
			break;
		}
		if (Aeron_NowUs() >= g_receive.deadline) {
			XVT_LOG_ERROR("resync.receive_timeout");
			g_flight_mission_state.mission_end_pending = 1;
			xvt_resync_reset();
		}
		break;
	case RESYNC_REPLAY: {
		flight_net_process_incoming_packets();
		if (xvt_flight_network_needs_recovery()) {
			xvt_resync_restart_receive();
			break;
		}
		xvt_flight_replay_result replay =
			xvt_flight_frame_replay_buffered();
		if (replay == XVT_REPLAY_IDLE ||
		    replay == XVT_REPLAY_TERMINAL) {
			unsigned ack = NET_PACKET_ACK;
			xvt_flight_network_send_packet(
				net_session_get_host_dplay_id(), &ack, 4);
			g_input_timestamp = g_server_tick_time +
					    g_flight_net_clock_lead_ticks;
			if (g_resync.owns_alert) {
				flight_alert_restore_box_background();
			}
			g_resync.owns_alert = 0;
			g_resync.phase = RESYNC_IDLE;
			xvt_flight_network_recovered();
			xvt_flight_controls_recover();
			if (replay != XVT_REPLAY_TERMINAL) {
				xvt_resync_world_applied();
			}
			g_receive.request_sent = 0;
			g_receive.restarts = 0;
			XVT_LOG_INFO("resync.done terminal=%d",
				     replay == XVT_REPLAY_TERMINAL);
		}
		break;
	}
	case RESYNC_IDLE:
		break;
	case RESYNC_CHECKSUMS:
		xvt_resync_checksums();
		break;
	case RESYNC_BUILD:
		xvt_resync_build();
		break;
	case RESYNC_ACKS: {
		int result = xvt_resync_wait_acks(g_resync.peer_dpid,
						  g_resync.ack_count);
		if (result == -1) {
			break;
		}
		if (!result || g_resync.final_batch) {
			xvt_resync_end_send(result);
		} else {
			xvt_resync_pulse(0);
			g_resync.chunk_index = 0;
			memset(g_flight_net_world_state_chunk_acked, 0,
			       sizeof(g_flight_net_world_state_chunk_acked));
			xvt_resync_new_chunk();
			g_resync.phase = RESYNC_BUILD;
		}
		break;
	}
	case RESYNC_APPLY:
		xvt_resync_apply();
		break;
	default:
		break;
	}
	if (g_resync.phase != RESYNC_IDLE &&
	    g_flight_mission_state.mission_end_pending) {
		xvt_resync_reset();
	}
}

int xvt_resync_has_state_request(void)
{
	for (unsigned i = 0; i < g_checksum_count; ++i) {
		if (xvt_wire_get32(
			    g_deferred_checksum_reports[(g_checksum_read + i) %
							XVT_DEFERRED_CHECKSUMS]
				    .packet.request_state) ==
		    XVT_CHECKSUM_REQUEST_STATE) {
			return 1;
		}
	}
	return 0;
}

int xvt_resync_is_active(void) { return g_resync.phase != RESYNC_IDLE; }

int xvt_resync_holds_input(void)
{
	return !net_session_is_local_host() &&
	       (g_receive.request_sent ||
		g_resync.phase == RESYNC_FULL_RECEIVE ||
		g_resync.phase == RESYNC_REPLAY);
}

uint64_t xvt_resync_next_wake_delay_us(void)
{
	if (g_receive.restart_pending || g_resync.restart_requested ||
	    g_resync.phase == RESYNC_BUILD || g_resync.phase == RESYNC_REPLAY ||
	    (g_resync.phase == RESYNC_IDLE &&
	     xvt_resync_ready_checksum() >= 0)) {
		return 0;
	}
	if (g_resync.phase == RESYNC_FULL_RECEIVE || g_receive.request_sent) {
		uint64_t now = Aeron_NowUs();
		return g_receive.deadline > now ? g_receive.deadline - now : 0;
	}
	int elapsed = g_resync.phase == RESYNC_ACKS ? g_resync.ack_elapsed
		      : g_resync.phase == RESYNC_APPLY
			      ? g_input_timestamp - g_resync.apply_sent_tick
			      : g_resync.checksum_elapsed;
	if (g_resync.phase == RESYNC_IDLE) {
		return UINT64_MAX;
	}
	unsigned remaining = elapsed < XVT_RESYNC_RETRY_TICKS
				     ? XVT_RESYNC_RETRY_TICKS - elapsed
				     : 0;
	return xvt_flight_time_delay_for_ticks(remaining);
}

int xvt_resync_receive_floor(void)
{
	return g_resync.phase == RESYNC_FULL_RECEIVE ||
			       g_resync.phase == RESYNC_REPLAY
		       ? g_receive.tick
		       : g_server_tick_time;
}

void xvt_resync_reset(void)
{
	if (g_resync.owns_alert) {
		flight_alert_restore_box_background();
	}
	free(g_resync.pinned);
	memset(&g_resync, 0, sizeof(g_resync));
	g_checksum_read = 0;
	g_checksum_count = 0;
	memset(&g_receive, 0, sizeof g_receive);
}

void xvt_resync_world_applied(void) { xvt_render_capture_world_changed(); }

void xvt_resync_service_recovery(void)
{
	if (!xvt_flight_timing_is_network125()) {
		return;
	}
	if (g_receive.request_sent) {
		if (Aeron_NowUs() >= g_receive.deadline) {
			XVT_LOG_ERROR("resync.request_timeout");
			flight_net_broadcast_player_abort(g_local_player);
			g_flight_mission_state.mission_end_pending = 1;
		}
		return;
	}
	if (net_session_is_local_host()) {
		/* A host cannot obtain an authoritative image from a client. */
		XVT_LOG_ERROR("resync.host_desync");
		g_flight_mission_state.mission_end_pending = 1;
		flight_net_broadcast_host_session_abort();
		return;
	}
	struct xvt_flight_checksum_report_wire packet = {0};
	xvt_wire_set32(packet.checksum.opcode, NET_PACKET_WORLD_CHECKSUM);
	xvt_wire_set32(packet.checksum.epoch,
		       g_flight_net_world_checksum_epoch);
	for (unsigned region = 0; region < XVT_WORLD_CHECKSUM_REGIONS;
	     ++region) {
		xvt_wire_set32(packet.checksum.checksums[region],
			       g_world_checksum[region]);
		xvt_wire_set32(packet.checksum.lengths[region],
			       g_world_checksum_region_lengths[region]);
	}
	xvt_wire_set32(packet.request_state, XVT_CHECKSUM_REQUEST_STATE);
	xvt_flight_network_send_wire(net_session_get_host_dplay_id(), &packet,
				     sizeof packet);
	XVT_LOG_INFO("resync.requested epoch=%u",
		     g_flight_net_world_checksum_epoch);
	g_receive.request_sent = 1;
	g_receive.deadline = Aeron_NowUs() + (uint64_t)XVT_PEER_TIMEOUT_TICKS *
						     XVT_FLIGHT_TICK_US;
}

static int xvt_resync_full_request(const uint8_t *bytes, unsigned size)
{
	struct xvt_flight_resync_request_wire request;
	if (size != sizeof request || !g_receive.table_valid) {
		return 1;
	}
	memcpy(&request, bytes, sizeof request);
	if (xvt_wire_get32(request.epoch) != g_receive.epoch) {
		XVT_LOG_DEBUG("resync.request_ignored epoch=%u expected=%u",
			      xvt_wire_get32(request.epoch), g_receive.epoch);
		return 1;
	}
	size_t total = xvt_wire_get32(request.image_bytes);
	size_t sum = 0;
	unsigned tick = xvt_wire_get32(request.completed_tick);
	for (unsigned region = 0; region < XVT_WORLD_CHECKSUM_REGIONS;
	     ++region) {
		sum += g_receive.lengths[region];
	}
	if (total != sum || total < sizeof(struct xvt_state_footer) ||
	    total > xvt_snapshot_calculate_size() || tick != g_receive.epoch ||
	    tick > INT32_MAX || tick % XVT_NETWORK_STEP_TICKS) {
		XVT_LOG_DEBUG(
			"resync.request_ignored expected=%u bytes=%zu sum=%zu tick=%u",
			g_receive.epoch, total, sum, tick);
		return 1;
	}
	if (g_resync.phase != RESYNC_FULL_RECEIVE) {
		if (!xvt_flight_messages_prepare_recovery(tick)) {
			xvt_flight_network_request_recovery();
			g_receive.restart_pending = 1;
			return 1;
		}
		xvt_flight_frame_reset_replay();
		xvt_flight_network_clear_recovery_request();
		g_receive.size = (int)total;
		g_receive.tick = (int)tick;
		memset(g_world_state_dup_buffer, 0, total);
		if (!g_resync.owns_alert) {
			flight_alert_save_box_background();
			g_resync.owns_alert = 1;
		}
		flight_alert_draw_box(
			1,
			g_str_disk_io_messages
				[DISK_IO_STR_COM_FAILURE_RECEIVING],
			0x30);
		g_resync.phase = RESYNC_FULL_RECEIVE;
		XVT_LOG_INFO("resync.receive_begin bytes=%d tick=%d",
			     g_receive.size, g_receive.tick);
	}
	g_receive.deadline = Aeron_NowUs() + (uint64_t)XVT_PEER_TIMEOUT_TICKS *
						     XVT_FLIGHT_TICK_US;
	struct xvt_flight_epoch_wire ready;
	xvt_wire_set32(ready.opcode, NET_PACKET_RESYNC_CHECKSUMS);
	xvt_wire_set32(ready.epoch, g_receive.epoch);
	xvt_flight_network_send_wire(net_session_get_host_dplay_id(), &ready,
				     sizeof ready);
	return 1;
}

static int xvt_resync_full_chunk(const uint8_t *bytes, unsigned size)
{
	struct xvt_flight_chunk_header header;
	if (g_resync.phase != RESYNC_FULL_RECEIVE ||
	    size < sizeof header + sizeof(xvt_wire_u32)) {
		return 1;
	}
	memcpy(&header, bytes, sizeof header);
	unsigned chunk = xvt_wire_get32(header.index);
	if (xvt_wire_get32(header.epoch) != g_receive.epoch ||
	    chunk >= XVT_RESYNC_CHUNKS_PER_BATCH) {
		return 1;
	}
	size_t offset = sizeof header;
	/* Validate the complete datagram before modifying the candidate image. */
	while (offset + sizeof(xvt_wire_u32) <= size &&
	       xvt_wire_get32(bytes + offset) != UINT32_MAX) {
		struct xvt_flight_chunk_span span;
		if (size - offset < sizeof span) {
			return 1;
		}
		memcpy(&span, bytes + offset, sizeof span);
		size_t destination = xvt_wire_get32(span.offset);
		size_t count = xvt_wire_get32(span.bytes);
		if (!count || destination > (unsigned)g_receive.size ||
		    count > (unsigned)g_receive.size - destination ||
		    count > size - offset - sizeof span) {
			return 1;
		}
		offset += sizeof span + count;
	}
	if (offset + sizeof(xvt_wire_u32) != size ||
	    xvt_wire_get32(bytes + offset) != UINT32_MAX) {
		return 1;
	}
	for (size_t cursor = sizeof header; cursor < offset;) {
		struct xvt_flight_chunk_span span;
		memcpy(&span, bytes + cursor, sizeof span);
		size_t destination = xvt_wire_get32(span.offset);
		size_t count = xvt_wire_get32(span.bytes);
		memcpy(g_world_state_dup_buffer + destination,
		       bytes + cursor + sizeof span, count);
		cursor += sizeof span + count;
	}
	XVT_LOG_DEBUG("resync.chunk index=%u bytes=%u", chunk, size);
	struct xvt_flight_chunk_ack_wire ack;
	xvt_wire_set32(ack.opcode, NET_PACKET_RESYNC_CHUNK_ACK);
	xvt_wire_set32(ack.index, chunk);
	xvt_flight_network_send_wire(net_session_get_host_dplay_id(), &ack,
				     sizeof ack);
	g_receive.deadline = Aeron_NowUs() + (uint64_t)XVT_PEER_TIMEOUT_TICKS *
						     XVT_FLIGHT_TICK_US;
	return 1;
}

static int xvt_resync_full_apply(const uint8_t *bytes, unsigned size)
{
	struct xvt_flight_resync_apply_wire apply;
	if (g_resync.phase != RESYNC_FULL_RECEIVE || size != sizeof apply) {
		return 1;
	}
	memcpy(&apply, bytes, sizeof apply);
	if (xvt_wire_get32(apply.epoch) != g_receive.epoch ||
	    xvt_wire_get32(apply.image_bytes) != (unsigned)g_receive.size) {
		return 1;
	}
	if (xvt_flight_network_needs_recovery()) {
		g_receive.restart_pending = 1;
		return 1;
	}
	unsigned checksums[XVT_WORLD_CHECKSUM_REGIONS];
	unsigned lengths[XVT_WORLD_CHECKSUM_REGIONS];
	if (!xvt_snapshot_checksum_image(g_world_state_dup_buffer,
					 g_receive.size, checksums, lengths) ||
	    memcmp(checksums, g_receive.checksums, sizeof checksums) ||
	    memcmp(lengths, g_receive.lengths, sizeof lengths)) {
		XVT_LOG_WARN("resync.image_mismatch");
		g_receive.restart_pending = 1;
		return 1;
	}
	if (!xvt_snapshot_decode(g_world_state_dup_buffer, g_receive.size)) {
		XVT_LOG_WARN("resync.decode_failed bytes=%d", g_receive.size);
		g_receive.restart_pending = 1;
		return 1;
	}
	xvt_flight_history_recover();
	memcpy(g_world_state_buffer, g_world_state_dup_buffer, g_receive.size);
	g_world_state_dup_size = g_receive.size;
	g_world_state_size = g_world_state_dup_size;
	g_server_tick_time = g_receive.tick;
	g_flight_net_world_checksum_epoch = g_receive.epoch;
	memcpy(g_world_checksum, checksums, sizeof checksums);
	memcpy(g_world_checksum_region_lengths, lengths, sizeof lengths);
	flight_net_send_world_checksum_to_host(
		(const int *)g_world_checksum,
		(const int *)g_world_checksum_region_lengths,
		XVT_WORLD_CHECKSUM_REGIONS);
	xvt_flight_frame_reset_replay();
	xvt_flight_network_recovered();
	g_flight_net_buffer_world_messages_until_checksum = 0;
	g_resync.phase = RESYNC_REPLAY;
	XVT_LOG_INFO("resync.restored tick=%d bytes=%d epoch=%u",
		     g_receive.tick, g_receive.size, g_receive.epoch);
	return 1;
}

int xvt_resync_receive_packet(int sender, const uint8_t *bytes, unsigned size)
{
	if (size < sizeof(xvt_wire_u32)) {
		return 0;
	}
	unsigned opcode = xvt_wire_get32(bytes);
	if (opcode == NET_PACKET_WORLD_CHECKSUM &&
	    size == sizeof(struct xvt_flight_checksum_report_wire)) {
		struct xvt_flight_checksum_report_wire report;
		memcpy(&report, bytes, sizeof report);
		if (net_session_is_local_host() && g_resync.phase &&
		    sender == g_resync.peer_dpid) {
			if (xvt_wire_get32(report.request_state) ==
			    XVT_CHECKSUM_REQUEST_STATE) {
				int aligned[sizeof report / sizeof(int)];
				memcpy(aligned, &report, sizeof report);
				xvt_resync_defer_checksum(sender, aligned);
				g_resync.restart_requested = 1;
				return 1;
			}
			if (!xvt_wire_get32(report.request_state) &&
			    xvt_wire_get32(report.checksum.epoch) ==
				    g_resync.epoch &&
			    g_resync.epoch !=
				    g_flight_net_world_checksum_epoch) {
				return 1;
			}
		}
	}
	if (opcode == NET_PACKET_RESYNC_CHECKSUMS) {
		struct xvt_flight_epoch_wire ready;
		if (g_resync.phase == RESYNC_CHECKSUMS &&
		    sender == g_resync.peer_dpid && size == sizeof ready) {
			memcpy(&ready, bytes, sizeof ready);
			if (xvt_wire_get32(ready.epoch) == g_resync.epoch) {
				g_flight_net_remote_resync_checksums_received_flag =
					1;
			}
		}
		return 1;
	}
	if (sender != net_session_get_host_dplay_id()) {
		return opcode == NET_PACKET_RESYNC_REQUEST ||
		       opcode == NET_PACKET_RESYNC_CHUNK ||
		       opcode == NET_PACKET_RESYNC_APPLY;
	}
	if (opcode == NET_PACKET_SERVER_CHECKSUM &&
	    size == sizeof(struct xvt_flight_checksum_wire)) {
		if (!net_session_is_local_host() &&
		    g_resync.phase != RESYNC_FULL_RECEIVE &&
		    g_resync.phase != RESYNC_REPLAY) {
			struct xvt_flight_checksum_wire table;
			memcpy(&table, bytes, sizeof table);
			g_receive.epoch = xvt_wire_get32(table.epoch);
			for (unsigned region = 0;
			     region < XVT_WORLD_CHECKSUM_REGIONS; ++region) {
				g_receive.checksums[region] =
					xvt_wire_get32(table.checksums[region]);
				g_receive.lengths[region] =
					xvt_wire_get32(table.lengths[region]);
			}
			g_receive.table_valid = 1;
		}
		return g_resync.phase == RESYNC_FULL_RECEIVE ||
		       g_resync.phase == RESYNC_REPLAY;
	}
	if (opcode == NET_PACKET_RESYNC_REQUEST) {
		return xvt_resync_full_request(bytes, size);
	}
	if (opcode == NET_PACKET_RESYNC_CHUNK) {
		return xvt_resync_full_chunk(bytes, size);
	}
	if (opcode == NET_PACKET_RESYNC_APPLY) {
		return xvt_resync_full_apply(bytes, size);
	}
	return 0;
}

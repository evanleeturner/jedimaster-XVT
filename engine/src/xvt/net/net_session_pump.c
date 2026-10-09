#include "xvt/net/net_session_pump.h"

#include <string.h>

#include "aeron/compat/dplay.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/net/net_session_send.h"
#include "xvt_runtime/log/log.h"

enum {
	RECEIVE_QUEUE_CAPACITY = 1024,
	RECEIVE_QUEUE_LIMIT = RECEIVE_QUEUE_CAPACITY - 1,
	HISTORY_CAPACITY = 128,
	WORLD_HISTORY_CAPACITY = 256,
	MAX_PAYLOAD_SIZE = 508,
	SEQUENCE_MODULUS = 128,
	SEQUENCE_NONE = 128
};

struct net_session_wire_packet {
	uint16_t header;    /* Type, sequence and channel bits */
	uint8_t data[1022]; /* The rest; a system message fills both */
};

/* Part of net_session_pump_incoming_packets: steps g_net_recv_queue_write_index
 * past the entry just written, back to 0 at RECEIVE_QUEUE_CAPACITY, and counts
 * the entry in g_net_recv_queue_count. */
static void net_session_step_queue_write_index(void)
{
	++g_net_recv_queue_write_index;
	++g_net_recv_queue_count;
	if (g_net_recv_queue_write_index >= RECEIVE_QUEUE_CAPACITY) {
		g_net_recv_queue_write_index = 0;
	}
}

/* Part of net_session_pump_incoming_packets for a DirectPlay system message
 * (sender 0): queues wire_packet whole, wire_size cut to a queue entry's
 * payload, in g_net_session_recv_queue with g_net_recv_queue_write_index and
 * g_net_recv_queue_count. */
static void
net_session_queue_system_message(DPID from_id, uint32_t *wire_size,
				 struct net_session_wire_packet *wire_packet)
{
	XVT_LOG_DEBUG("network.system_message bytes=%u", *wire_size);
	if (*wire_size > sizeof(g_net_session_recv_queue[0].payload)) {
		XVT_LOG_WARN(
			"network.packet_truncated from=%u type=%u part=\"system\" bytes=%u",
			(unsigned)from_id, (unsigned)wire_packet->header,
			(unsigned)*wire_size);
		*wire_size = sizeof(g_net_session_recv_queue[0].payload);
	}
	g_net_session_recv_queue[g_net_recv_queue_write_index].direct_play_id =
		from_id;
	g_net_session_recv_queue[g_net_recv_queue_write_index].payload_size =
		*wire_size;
	g_net_session_recv_queue[g_net_recv_queue_write_index].is_resent_copy =
		0;
	memcpy(g_net_session_recv_queue[g_net_recv_queue_write_index].payload,
	       &wire_packet->header, *wire_size);
	net_session_step_queue_write_index();
}

/* Part of net_session_pump_incoming_packets: when the body carries a 2-byte
 * length, steps packet_data past it and takes it as packet_size; cuts
 * packet_size to MAX_PAYLOAD_SIZE. */
static void net_session_read_body_length(int has_length, int broadcast_channel,
					 int group_channel,
					 unsigned int packet_type, DPID from_id,
					 uint8_t **packet_data,
					 uint32_t *packet_size)
{
	if (has_length && !(broadcast_channel && group_channel) &&
	    net_session_get_fixed_payload_size(packet_type) == 0 &&
	    *packet_size >= sizeof(uint16_t)) {
		uint16_t encoded_size;
		memcpy(&encoded_size, *packet_data, sizeof(encoded_size));
		*packet_data += sizeof(encoded_size);
		*packet_size = encoded_size;
	}
	if (*packet_size > MAX_PAYLOAD_SIZE) {
		XVT_LOG_WARN(
			"network.packet_truncated from=%u type=%u part=\"body\" bytes=%u",
			(unsigned)from_id, packet_type, (unsigned)*packet_size);
		*packet_size = MAX_PAYLOAD_SIZE;
	}
}

/* Part of net_session_pump_incoming_packets for a PING: answers the sender with
 * a PONG, built in response_packet, and logs it. */
static void net_session_answer_ping(DPID from_id,
				    unsigned int response_packet[2])
{
	response_packet[0] = NET_PACKET_PONG;
	net_session_send_packet(from_id, response_packet, sizeof(uint32_t));
	XVT_LOG_DEBUG("network.ping_answered from=%u", (unsigned)from_id);
}

/* Part of net_session_pump_incoming_packets for a KEEPALIVE_ACK: logs that it
 * is ignored. */
static void net_session_log_keepalive_ack(DPID from_id)
{
	XVT_LOG_DEBUG("network.keepalive_ack_ignored from=%u",
		      (unsigned)from_id);
}

/* Part of net_session_pump_incoming_packets for a WORLD_NACK: resends the world
 * message found at world_cursor, or a NOP in requested_sequence when the search
 * found none. */
static void net_session_resend_world_message(unsigned int search_count,
					     unsigned int world_cursor,
					     DPID from_id, int timestamp,
					     int requested_sequence,
					     unsigned int *peer_slot,
					     unsigned int response_packet[2])
{
	if (search_count < WORLD_HISTORY_CAPACITY) {
		net_session_send_sequenced_game_packet(
			from_id, 0,
			g_net_session_sent_world_message_history[world_cursor]
				.sequence_byte,
			(const unsigned int *)
				g_net_session_sent_world_message_history
					[world_cursor]
						.payload,
			g_net_session_sent_world_message_history[world_cursor]
				.payload_size);
		XVT_LOG_DEBUG(
			"network.world_resent from=%u tick=%d sequence=%u bytes=%u drops=%d",
			(unsigned)from_id, timestamp,
			(unsigned)g_net_session_sent_world_message_history
				[world_cursor]
					.sequence_byte,
			(unsigned)g_net_session_sent_world_message_history
				[world_cursor]
					.payload_size,
			*peer_slot < 40
				? g_net_session.reliable_peer_slots[*peer_slot]
					  .packet_drop_count
				: -1);
	} else {
		response_packet[0] = NET_PACKET_NOP;
		net_session_send_sequenced_game_packet(
			from_id, 0, (uint8_t)requested_sequence,
			response_packet, sizeof(uint32_t));
		XVT_LOG_WARN(
			"network.world_resend_missing from=%u tick=%d sequence=%d",
			(unsigned)from_id, timestamp, requested_sequence);
	}
}

/* Part of net_session_pump_incoming_packets: answers a peer's WORLD_NACK with
 * the world message of that tick from g_net_session_sent_world_message_history,
 * and counts it in the peer's drop count. */
static void net_session_answer_world_nack(unsigned int packet_type,
					  uint8_t *packet_data,
					  uint32_t packet_size, DPID from_id,
					  unsigned int *peer_slot,
					  unsigned int response_packet[2])
{
	if (packet_size >= 2 * sizeof(int)) {
		int timestamp;
		memcpy(&timestamp, packet_data, sizeof(timestamp));
		int requested_sequence;
		memcpy(&requested_sequence, packet_data + sizeof(timestamp),
		       sizeof(requested_sequence));
		*peer_slot = net_reliable_find_or_create_peer_slot(from_id);
		if (*peer_slot < g_net_session.reliable_peer_slot_count &&
		    *peer_slot < 40) {
			++g_net_session.reliable_peer_slots[*peer_slot]
				  .packet_drop_count;
		}
		unsigned int world_cursor = (unsigned int)
			g_net_session_sent_world_message_write_index;
		unsigned int search_count;
		for (search_count = 0; search_count < WORLD_HISTORY_CAPACITY;
		     ++search_count) {
			if (g_net_session_sent_world_message_history
					    [world_cursor]
						    .payload_size != 0 &&
			    ((*((uint32_t
					 *)&g_net_session_sent_world_message_history
					[world_cursor]
						.payload[4]) &
			      0x7FFFFFFF) == (uint32_t)timestamp)) {
				break;
			}
			if (++world_cursor >= WORLD_HISTORY_CAPACITY) {
				world_cursor = 0;
			}
		}
		net_session_resend_world_message(
			search_count, world_cursor, from_id, timestamp,
			requested_sequence, peer_slot, response_packet);
	} else {
		XVT_LOG_WARN(
			"network.control_packet_short from=%u type=%u bytes=%u",
			(unsigned)from_id, packet_type, (unsigned)packet_size);
	}
}

/* Part of net_session_pump_incoming_packets for a NACK: finds the asked
 * sequence and channel in g_net_session_sent_history and resends it, or a NOP
 * in that sequence when it is gone. */
static void net_session_resend_from_history(int requested_sequence,
					    int requested_class, DPID from_id,
					    unsigned int *peer_slot,
					    unsigned int response_packet[2])
{
	unsigned int history_cursor =
		(unsigned int)g_net_session_sent_history_write_index;
	unsigned int search_count;
	for (search_count = 0; search_count < HISTORY_CAPACITY;
	     ++search_count) {
		if (g_net_session_sent_history[history_cursor].payload_size !=
			    0 &&
		    g_net_session_sent_history[history_cursor].sequence_byte ==
			    requested_sequence &&
		    g_net_session_sent_history[history_cursor].packet_class ==
			    requested_class &&
		    (requested_class == 0 || requested_class == 2 ||
		     g_net_session_sent_history[history_cursor]
				     .direct_play_id == from_id)) {
			break;
		}
		if (++history_cursor >= HISTORY_CAPACITY) {
			history_cursor = 0;
		}
	}
	if (search_count < HISTORY_CAPACITY) {
		net_session_send_sequenced_game_packet(
			from_id, (uint8_t)requested_class,
			g_net_session_sent_history[history_cursor]
				.sequence_byte,
			(const unsigned int *)
				g_net_session_sent_history[history_cursor]
					.payload,
			g_net_session_sent_history[history_cursor]
				.payload_size);
		XVT_LOG_DEBUG(
			"network.packet_resent from=%u channel=%d sequence=%d type=%u bytes=%u drops=%d",
			(unsigned)from_id, requested_class, requested_sequence,
			(unsigned)g_net_session_sent_history[history_cursor]
				.payload[0],
			(unsigned)g_net_session_sent_history[history_cursor]
				.payload_size,
			*peer_slot < 40
				? g_net_session.reliable_peer_slots[*peer_slot]
					  .packet_drop_count
				: -1);
	} else {
		response_packet[0] = NET_PACKET_NOP;
		net_session_send_sequenced_game_packet(
			from_id, (uint8_t)requested_class,
			(uint8_t)requested_sequence, response_packet,
			sizeof(uint32_t));
		XVT_LOG_WARN(
			"network.packet_resend_missing from=%u channel=%d sequence=%d",
			(unsigned)from_id, requested_class, requested_sequence);
	}
}

/* Part of net_session_pump_incoming_packets: answers a peer's NACK with the
 * packet it asks for, and counts it in the peer's drop count. */
static void net_session_answer_nack(unsigned int packet_type,
				    uint8_t *packet_data, uint32_t packet_size,
				    DPID from_id, unsigned int *peer_slot,
				    unsigned int response_packet[2])
{
	if (packet_size >= 2 * sizeof(int)) {
		int requested_sequence;
		memcpy(&requested_sequence, packet_data,
		       sizeof(requested_sequence));
		int requested_class;
		memcpy(&requested_class,
		       packet_data + sizeof(requested_sequence),
		       sizeof(requested_class));
		*peer_slot = net_reliable_find_or_create_peer_slot(from_id);
		if (*peer_slot < g_net_session.reliable_peer_slot_count &&
		    *peer_slot < 40) {
			++g_net_session.reliable_peer_slots[*peer_slot]
				  .packet_drop_count;
		}
		net_session_resend_from_history(requested_sequence,
						requested_class, from_id,
						peer_slot, response_packet);
	} else {
		XVT_LOG_WARN(
			"network.control_packet_short from=%u type=%u bytes=%u",
			(unsigned)from_id, packet_type, (unsigned)packet_size);
	}
}

/* Part of net_session_pump_incoming_packets for a KEEPALIVE: resends from
 * g_net_session_sent_history the packet each channel's expected sequence names,
 * setting that sequence to SEQUENCE_NONE once sent, and logs the ones it could
 * not find. */
static void net_session_resend_for_keepalive(DPID from_id,
					     int *expected_broadcast,
					     int *expected_group,
					     int *expected_directed)
{
	unsigned int history_cursor =
		(unsigned int)g_net_session_sent_history_write_index;
	for (unsigned int search_count = 0;
	     search_count < HISTORY_CAPACITY &&
	     (*expected_broadcast != SEQUENCE_NONE ||
	      *expected_group != SEQUENCE_NONE ||
	      *expected_directed != SEQUENCE_NONE);
	     ++search_count) {
		if (g_net_session_sent_history[history_cursor].payload_size !=
		    0) {
			uint8_t packet_class =
				g_net_session_sent_history[history_cursor]
					.packet_class;
			if (packet_class == 0) {
				if (g_net_session_sent_history[history_cursor]
					    .sequence_byte ==
				    *expected_broadcast) {
					net_session_send_sequenced_game_packet(
						from_id, 0, *expected_broadcast,
						(const unsigned int *)
							g_net_session_sent_history
								[history_cursor]
									.payload,
						g_net_session_sent_history
							[history_cursor]
								.payload_size);
					*expected_broadcast = SEQUENCE_NONE;
				}
			} else if (packet_class == 2) {
				if (g_net_session_sent_history[history_cursor]
					    .sequence_byte == *expected_group) {
					net_session_send_sequenced_game_packet(
						from_id, 2, *expected_group,
						(const unsigned int *)
							g_net_session_sent_history
								[history_cursor]
									.payload,
						g_net_session_sent_history
							[history_cursor]
								.payload_size);
					*expected_group = SEQUENCE_NONE;
				}
			} else if (g_net_session_sent_history[history_cursor]
						   .direct_play_id == from_id &&
				   g_net_session_sent_history[history_cursor]
						   .sequence_byte ==
					   *expected_directed) {
				net_session_send_sequenced_game_packet(
					from_id, 1, *expected_directed,
					(const unsigned int *)
						g_net_session_sent_history
							[history_cursor]
								.payload,
					g_net_session_sent_history
						[history_cursor]
							.payload_size);
				*expected_directed = SEQUENCE_NONE;
			}
		}
		if (++history_cursor >= HISTORY_CAPACITY) {
			history_cursor = 0;
		}
	}
	if (*expected_broadcast != SEQUENCE_NONE ||
	    *expected_group != SEQUENCE_NONE ||
	    *expected_directed != SEQUENCE_NONE) {
		XVT_LOG_WARN(
			"network.keepalive_unanswered from=%u broadcast=%d group=%d direct=%d",
			(unsigned)from_id, *expected_broadcast, *expected_group,
			*expected_directed);
	}
}

/* Part of net_session_pump_incoming_packets: answers a peer's KEEPALIVE with
 * the next packet on each channel that the peer still lacks. */
static void net_session_answer_keepalive(unsigned int packet_type,
					 uint8_t *packet_data,
					 uint32_t packet_size, DPID from_id,
					 unsigned int *peer_slot)
{
	if (packet_size >= 3 * sizeof(int)) {
		int expected_broadcast;
		memcpy(&expected_broadcast, packet_data,
		       sizeof(expected_broadcast));
		int expected_group;
		memcpy(&expected_group,
		       packet_data + sizeof(expected_broadcast),
		       sizeof(expected_group));
		int expected_directed;
		memcpy(&expected_directed,
		       packet_data + 2 * sizeof(expected_broadcast),
		       sizeof(expected_directed));
		if (expected_broadcast ==
		    (int)g_net_session.broadcast_seq_counter) {
			expected_broadcast = SEQUENCE_NONE;
		}
		if (expected_group == (int)g_net_session.group_seq_counter) {
			expected_group = SEQUENCE_NONE;
		}
		*peer_slot = net_reliable_find_or_create_peer_slot(from_id);
		if (*peer_slot >= g_net_session.reliable_peer_slot_count ||
		    g_net_session.reliable_peer_slots[*peer_slot].send_seq ==
			    expected_directed) {
			expected_directed = SEQUENCE_NONE;
		}
		XVT_LOG_DEBUG(
			"network.keepalive_received from=%u broadcast=%d group=%d direct=%d",
			(unsigned)from_id, expected_broadcast, expected_group,
			expected_directed);
		net_session_resend_for_keepalive(from_id, &expected_broadcast,
						 &expected_group,
						 &expected_directed);
	} else {
		XVT_LOG_WARN(
			"network.control_packet_short from=%u type=%u bytes=%u",
			(unsigned)from_id, packet_type, (unsigned)packet_size);
	}
}

/* Part of net_session_pump_incoming_packets for a resent packet: sets the queue
 * entry's channel class from channel_marker and advances that channel's receive
 * sequence in the peer's slot to sequence. */
static void net_session_note_copy_sequence(uint8_t channel_marker,
					   unsigned int *peer_slot,
					   int sequence)
{
	if (channel_marker == 0) {
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.packet_class = 0;
		if (*peer_slot < g_net_session.reliable_peer_slot_count &&
		    *peer_slot < 40) {
			net_session_advance_received_sequence(
				&g_net_session.reliable_peer_slots[*peer_slot]
					 .recv_seq_channel_a,
				sequence);
		}
	} else if (channel_marker == 2) {
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.packet_class = 2;
		if (*peer_slot < g_net_session.reliable_peer_slot_count &&
		    *peer_slot < 40) {
			net_session_advance_received_sequence(
				&g_net_session.reliable_peer_slots[*peer_slot]
					 .recv_seq_channel_b,
				sequence);
		}
	} else {
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.packet_class = 1;
		if (*peer_slot < g_net_session.reliable_peer_slot_count &&
		    *peer_slot < 40) {
			net_session_advance_received_sequence(
				&g_net_session.reliable_peer_slots[*peer_slot]
					 .recv_seq_default,
				sequence);
		}
	}
}

/* Part of net_session_pump_incoming_packets: queues a resent packet under the
 * channel its marker byte names, in g_net_session_recv_queue with
 * g_net_recv_queue_write_index and g_net_recv_queue_count. */
static void net_session_queue_resent_copy(int has_length,
					  unsigned int packet_type,
					  uint8_t *packet_data,
					  uint32_t packet_size, DPID from_id,
					  int sequence, unsigned int *peer_slot)
{
	uint8_t channel_marker = packet_size != 0 ? packet_data[0] : 0;
	const uint8_t *app_payload =
		packet_size != 0 ? packet_data + 1 : packet_data;
	uint32_t app_payload_size = packet_size != 0 ? packet_size - 1 : 0;
	if (has_length &&
	    net_session_get_fixed_payload_size(packet_type) == 0 &&
	    app_payload_size >= sizeof(uint16_t)) {
		uint16_t encoded_size;
		memcpy(&encoded_size, app_payload, sizeof(encoded_size));
		app_payload += sizeof(encoded_size);
		app_payload_size = encoded_size;
	}
	if (app_payload_size > MAX_PAYLOAD_SIZE) {
		XVT_LOG_WARN(
			"network.packet_truncated from=%u type=%u part=\"copy\" bytes=%u",
			(unsigned)from_id, packet_type,
			(unsigned)app_payload_size);
		app_payload_size = MAX_PAYLOAD_SIZE;
	}
	memcpy(g_net_session_recv_queue[g_net_recv_queue_write_index].payload,
	       &packet_type, sizeof(packet_type));
	memcpy(g_net_session_recv_queue[g_net_recv_queue_write_index].payload +
		       sizeof(packet_type),
	       app_payload, app_payload_size);
	g_net_session_recv_queue[g_net_recv_queue_write_index].direct_play_id =
		from_id;
	g_net_session_recv_queue[g_net_recv_queue_write_index].payload_size =
		app_payload_size + sizeof(packet_type);
	g_net_session_recv_queue[g_net_recv_queue_write_index].last_nack_ms = 0;
	g_net_session_recv_queue[g_net_recv_queue_write_index]
		.nack_retry_count = 0;
	g_net_session_recv_queue[g_net_recv_queue_write_index].is_resent_copy =
		1;
	*peer_slot = net_reliable_find_or_create_peer_slot(from_id);
	net_session_note_copy_sequence(channel_marker, peer_slot, sequence);
	g_net_session_recv_queue[g_net_recv_queue_write_index].sequence_byte =
		(uint8_t)sequence;
	++g_net_recv_queue_count;
	++g_net_recv_queue_write_index;
	if (g_net_recv_queue_write_index >= RECEIVE_QUEUE_CAPACITY) {
		g_net_recv_queue_write_index = 0;
	}
	XVT_LOG_DEBUG(
		"network.resent_copy_received from=%u type=%u channel=%d sequence=%d bytes=%u queued=%u",
		(unsigned)from_id, packet_type,
		channel_marker == 0   ? 0
		: channel_marker == 2 ? 2
				      : 1,
		sequence, (unsigned)app_payload_size, g_net_recv_queue_count);
}

/* Part of net_session_pump_incoming_packets when the channel's previous
 * sequence is new: counts it in the peer's drop count when the peer has a
 * reliable slot, except on the group channel. */
static void net_session_count_missing_previous(unsigned int peer_slot,
					       int group_channel)
{
	if (peer_slot < g_net_session.reliable_peer_slot_count &&
	    peer_slot < 40 && !group_channel) {
		++g_net_session.reliable_peer_slots[peer_slot]
			  .packet_drop_count;
	}
}

/* Part of net_session_pump_incoming_packets when the channel's previous
 * sequence is new: logs it and queues the copy of that packet riding behind
 * this one in wire_packet, past packet_size bytes of packet_data, unless it is
 * a NOP. */
static void net_session_recover_previous_packet(
	DPID from_id, int broadcast_channel, int group_channel,
	int previous_sequence, unsigned int peer_slot, uint8_t *packet_data,
	uint32_t packet_size, uint32_t wire_size,
	struct net_session_wire_packet *wire_packet)
{
	uint8_t *piggyback = packet_data + packet_size;
	uint32_t piggyback_size =
		wire_size -
		(unsigned int)(piggyback - (uint8_t *)&wire_packet->header);
	XVT_LOG_DEBUG(
		"network.previous_missing from=%u channel=%d sequence=%d recovered=%d drops=%d",
		(unsigned)from_id,
		broadcast_channel ? 0
		: group_channel	  ? 2
				  : 1,
		previous_sequence,
		piggyback_size > 0 && piggyback[0] != NET_PACKET_NOP,
		peer_slot < 40 ? g_net_session.reliable_peer_slots[peer_slot]
					 .packet_drop_count
			       : -1);
	if (piggyback_size > 0) {
		unsigned int piggyback_type = piggyback[0];
		if (piggyback_type != NET_PACKET_NOP) {
			if (piggyback_size - 1 > MAX_PAYLOAD_SIZE) {
				XVT_LOG_WARN(
					"network.packet_truncated from=%u type=%u part=\"trailer\" bytes=%u",
					(unsigned)from_id, piggyback_type,
					(unsigned)(piggyback_size - 1));
				piggyback_size = MAX_PAYLOAD_SIZE + 1;
			}
			memcpy(g_net_session_recv_queue
				       [g_net_recv_queue_write_index]
					       .payload,
			       &piggyback_type, sizeof(piggyback_type));
			memcpy(g_net_session_recv_queue
					       [g_net_recv_queue_write_index]
						       .payload +
				       sizeof(uint32_t),
			       piggyback + 1, piggyback_size - 1);
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.direct_play_id = from_id;
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.payload_size =
				piggyback_size + sizeof(uint32_t) - 1;
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.last_nack_ms = 0;
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.nack_retry_count = 0;
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.is_resent_copy = 0;
			if (broadcast_channel) {
				g_net_session_recv_queue
					[g_net_recv_queue_write_index]
						.packet_class = 0;
			} else {
				g_net_session_recv_queue
					[g_net_recv_queue_write_index]
						.packet_class = 2;
				if (!group_channel) {
					g_net_session_recv_queue
						[g_net_recv_queue_write_index]
							.packet_class = 1;
				}
			}
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.sequence_byte = (uint8_t)previous_sequence;
			net_session_step_queue_write_index();
		}
	}
}

/* Part of net_session_pump_incoming_packets: records the packet's sequence
 * through net_reliable_check_and_record_recv_sequence, which sets duplicate
 * when it is not new, logs the packet, and queues it with its channel and
 * sequence, marked as a resent copy when duplicate is set; a repeat on the
 * group channel is dropped. */
static void net_session_queue_received_packet(
	unsigned int packet_type, uint8_t *packet_data, uint32_t packet_size,
	DPID from_id, int broadcast_channel, int group_channel, int sequence,
	int *duplicate)
{
	*duplicate = net_reliable_check_and_record_recv_sequence(
		from_id, sequence, broadcast_channel, group_channel);
	XVT_LOG_DEBUG(
		"network.packet_received from=%u type=%u channel=%d sequence=%d bytes=%u repeat=%d queued=%u",
		(unsigned)from_id, packet_type,
		broadcast_channel ? 0
		: group_channel	  ? 2
				  : 1,
		sequence, (unsigned)packet_size, *duplicate,
		g_net_recv_queue_count);
	if (!group_channel || !*duplicate) {
		memcpy(g_net_session_recv_queue[g_net_recv_queue_write_index]
			       .payload,
		       &packet_type, sizeof(packet_type));
		memcpy(g_net_session_recv_queue[g_net_recv_queue_write_index]
				       .payload +
			       sizeof(packet_type),
		       packet_data, packet_size);
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.direct_play_id = from_id;
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.payload_size = packet_size + sizeof(packet_type);
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.last_nack_ms = 0;
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.nack_retry_count = 0;
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.is_resent_copy = 1;
		if (!*duplicate) {
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.is_resent_copy = 0;
		}
		if (broadcast_channel) {
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.packet_class = 0;
		} else {
			g_net_session_recv_queue[g_net_recv_queue_write_index]
				.packet_class = 2;
			if (!group_channel) {
				g_net_session_recv_queue
					[g_net_recv_queue_write_index]
						.packet_class = 1;
			}
		}
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.sequence_byte = (uint8_t)sequence;
		net_session_step_queue_write_index();
	}
}

/* Moves every packet DirectPlay holds for this player into the receive queue.
 * Does nothing without a DirectPlay interface; stops when DirectPlay has no
 * more, or when the queue holds 1,023 entries, after calling
 * net_reliable_keep_only_host_received_packets. System messages (sender 0) are
 * queued as they come; packets addressed to another player are skipped. It
 * answers a PING with a PONG and ignores a KEEPALIVE_ACK. A peer's WORLD_NACK
 * gets the world message with that tick from
 * g_net_session_sent_world_message_history, a NACK the asked sequence and channel
 * from g_net_session_sent_history, either one a NOP in that sequence when the
 * packet is gone, and a KEEPALIVE the next packet on each channel that the peer
 * still lacks; NACKs count in the peer's drop count. A resent packet (bit 0x80
 * set, bit 15 clear) is queued under the channel its marker byte names. Any
 * other packet is queued with its channel and sequence, marked as a resent copy
 * when net_reliable_check_and_record_recv_sequence finds the sequence not new (a
 * repeat or a stale one), except that such a packet on the group channel is
 * dropped; when net_reliable_check_and_record_recv_sequence says the
 * channel's previous sequence is new, the copy of it riding behind the packet
 * is queued too. Writes
 * g_net_session.receive_pump_state (0), the peer slots' receive sequences and drop
 * counts, g_net_session_recv_queue, g_net_recv_queue_write_index and
 * g_net_recv_queue_count. */
// FUNCTION: XVT 0x46C830
void net_session_pump_incoming_packets(void)
{
	static int receive_suppress_count;
	struct net_session_wire_packet wire_packet;
	if (g_net_session.dplay_interface == NULL) {
		return;
	}
	g_net_session.receive_pump_state = 0;
	DPID from_id;
	DPID to_id;
	unsigned int response_packet[2];
	unsigned int peer_slot;
	int duplicate;
	for (;;) {
		if ((int)g_net_recv_queue_count >= RECEIVE_QUEUE_LIMIT) {
			XVT_LOG_WARN("network.receive_queue_full queued=%u",
				     g_net_recv_queue_count);
			net_reliable_keep_only_host_received_packets();
			return;
		}
		uint32_t wire_size = sizeof(wire_packet);
		if (g_net_session.dplay_interface->lpVtbl->Receive(
			    g_net_session.dplay_interface, &from_id, &to_id, 1,
			    &wire_packet, &wire_size) != 0) {
			XVT_LOG_DEBUG("network.receive_drained queued=%u",
				      g_net_recv_queue_count);
			return;
		}
		if (from_id == 0) {
			net_session_queue_system_message(from_id, &wire_size,
							 &wire_packet);
			continue;
		}
		if (receive_suppress_count != 0) {
			--receive_suppress_count;
			continue;
		}
		if (to_id !=
		    (DPID)g_net_session.local_player_info.direct_play_id) {
			XVT_LOG_WARN(
				"network.packet_misaddressed from=%u to=%u",
				(unsigned)from_id, (unsigned)to_id);
			continue;
		}
		unsigned int packet_type = wire_packet.header & 0x7F;
		int group_channel = (wire_packet.header & 0x80) != 0;
		int broadcast_channel = (wire_packet.header & 0x8000) == 0;
		int sequence = (wire_packet.header >> 8) & 0x7F;
		uint8_t *packet_data = wire_packet.data;
		uint32_t packet_size = wire_size - sizeof(wire_packet.header);
		int has_length = packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				 packet_type > NET_PACKET_RESYNC_CHUNK;
		net_session_read_body_length(
			has_length, broadcast_channel, group_channel,
			packet_type, from_id, &packet_data, &packet_size);
		if (packet_type == NET_PACKET_PING) {
			net_session_answer_ping(from_id, response_packet);
			continue;
		}
		if (packet_type == NET_PACKET_KEEPALIVE_ACK) {
			net_session_log_keepalive_ack(from_id);
			continue;
		}
		if (packet_type == NET_PACKET_WORLD_NACK) {
			net_session_answer_world_nack(
				packet_type, packet_data, packet_size, from_id,
				&peer_slot, response_packet);
			continue;
		}
		if (packet_type == NET_PACKET_NACK) {
			net_session_answer_nack(packet_type, packet_data,
						packet_size, from_id,
						&peer_slot, response_packet);
			continue;
		}
		if (packet_type == NET_PACKET_KEEPALIVE) {
			net_session_answer_keepalive(packet_type, packet_data,
						     packet_size, from_id,
						     &peer_slot);
			continue;
		}
		peer_slot = net_reliable_find_or_create_peer_slot(from_id);
		if (broadcast_channel && group_channel) {
			net_session_queue_resent_copy(
				has_length, packet_type, packet_data,
				packet_size, from_id, sequence, &peer_slot);
			continue;
		}
		peer_slot = net_reliable_find_or_create_peer_slot(from_id);
		if (has_length) {
			int previous_sequence = sequence == 0
							? SEQUENCE_MODULUS - 1
							: sequence - 1;
			duplicate = net_reliable_check_and_record_recv_sequence(
				from_id, previous_sequence, broadcast_channel,
				group_channel);
			if (!duplicate) {
				net_session_count_missing_previous(
					peer_slot, group_channel);
				net_session_recover_previous_packet(
					from_id, broadcast_channel,
					group_channel, previous_sequence,
					peer_slot, packet_data, packet_size,
					wire_size, &wire_packet);
			}
		}
		net_session_queue_received_packet(
			packet_type, packet_data, packet_size, from_id,
			broadcast_channel, group_channel, sequence, &duplicate);
	}
}

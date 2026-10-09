#include "xvt/net/net_send.h"

#include <stdio.h>
#include <string.h>

#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net.h"
#include "xvt/net/net_peers.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"

/* A game packet as the lobby sends it through DirectPlay. */
struct net_direct_play_encoded_packet {
	/* Bits 0-6 the packet type, bits 8-14 the sequence. Bits 15 and 7 are
	 * set for the group; bit 15 alone for a sequenced packet to one
	 * player. */
	int16_t packet_type_header;
	/* Body length, present only outside types 60-63 for a type with no
	 * fixed size; otherwise the body starts here. */
	int16_t payload_size;
	/* The body, then, outside types 60-63, a trailer: a NOP byte, or the
	 * type byte and body of the previous packet on the channel. */
	uint8_t payload[1020];
};

#pragma pack(push, 1)

/* A resent packet as the lobby sends it through DirectPlay. */
struct net_direct_play_sequenced_packet {
	/* Type and sequence as in net_direct_play_encoded_packet, with bit 7 set
	 * and bit 15 clear, which marks a resend. */
	int16_t packet_type_header;
	uint8_t packet_class; /* Channel: 0 broadcast, 1 one player, 2 group. */
	/* Body length, present only outside types 60-63 for a type with no
	 * fixed size; otherwise the body starts here. */
	int16_t payload_size;
	/* The body, then, outside types 60-63, a NOP byte. */
	uint8_t payload[1019];
};

#pragma pack(pop)
typedef char xvt_size_net_direct_play_sequenced_packet
	[(sizeof(struct net_direct_play_sequenced_packet) == 1024) ? 1 : -1];

/* Sends a packet through net_send_packet_internal, then a NOP to the same
 * player, whose trailer carries a second copy of the packet at once. Returns
 * the first send's result, or 1 without DirectPlay. The back buffer is
 * unlocked meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4CEF70
int net_send_packet_and_flush(int to_player_id, const void *packet,
			      unsigned int packet_size)
{
	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}
	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int result =
		net_send_packet_internal(to_player_id, packet, packet_size);
	int flush_packet = NET_PACKET_NOP;
	net_send_packet_internal(to_player_id, &flush_packet,
				 sizeof(flush_packet));
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return result;
}

/* Sends one game packet on the channel its destination picks: id 0 the
 * broadcast channel, the group's id the group channel, any other id the
 * one-player channel, each with its own sequence counter (the one-player
 * counter in the peer's slot). Outside types 60-63, a type with no fixed
 * size gets a length word, and the previous packet sent on the same channel
 * follows as a trailer (a NOP byte after a reset); every packet then becomes
 * the next trailer. A packet not for the local player goes into the
 * 128-entry sent history. One for the local player, for everyone or for the
 * group, or any packet sent without DirectPlay, is also queued locally as
 * received from the local player (when fewer than 1024 are queued), setting
 * the local peer slot's newest received sequence. Returns 1 without
 * DirectPlay, when Send succeeds or when nothing needs sending, else 0. Does
 * not check packet_size against the 512-byte history and queue entries; with
 * the peer table full, the one-player path uses slot 40, one past the end of
 * the table. */
// FUNCTION: XVT 0x4CEFE0
int net_send_packet_internal(int to_player_id, const void *packet,
			     unsigned int packet_size)
{
	unsigned int packet_type = *(const unsigned int *)packet;
	uint8_t packet_type_byte = packet_type & 0x7F;
	uint16_t packet_header = packet_type_byte;
	int append_pending;
	if (packet_type >= NET_PACKET_RESYNC_CHECKSUMS &&
	    packet_type < NET_PACKET_PROBE_REQUEST) {
		append_pending = 0;
	} else {
		append_pending = 1;
	}
	char debug_text[256];
	struct net_direct_play_encoded_packet encoded_packet;
	uint8_t *encoded_payload;
	unsigned int encoded_size;
	if (to_player_id == 0) {
		sprintf(debug_text, "(SB %u) ",
			g_front_state.net_runtime_broadcast_seq_counter);
		packet_header |=
			(g_front_state.net_runtime_broadcast_seq_counter & 0x7F)
			<< 8;
		++g_front_state.net_runtime_broadcast_seq_counter;
		if (g_front_state.net_runtime_broadcast_seq_counter > 127) {
			g_front_state.net_runtime_broadcast_seq_counter = 0;
		}
		encoded_packet.packet_type_header = packet_header;
		encoded_payload = (uint8_t *)&encoded_packet.payload_size;
		encoded_size = 2;
		if (append_pending &&
		    net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_packet.payload_size = packet_size - 4;
			encoded_payload = encoded_packet.payload;
			encoded_size = 4;
		}
		memcpy(encoded_payload, (const uint8_t *)packet + 4,
		       packet_size - 4);
		encoded_payload += packet_size - 4;
		encoded_size += packet_size - 4;
		if (append_pending) {
			if (g_front_state.net_runtime_broadcast_pending_payload
				    .piggyback_empty != 0) {
				*encoded_payload = NET_PACKET_NOP;
				++encoded_size;
				g_front_state
					.net_runtime_broadcast_pending_payload
					.piggyback_empty = 0;
			} else {
				memcpy(encoded_payload,
				       g_front_state
					       .net_runtime_broadcast_pending_payload
					       .payload,
				       g_front_state
					       .net_runtime_broadcast_pending_payload
					       .payload_length);
				encoded_size +=
					g_front_state
						.net_runtime_broadcast_pending_payload
						.payload_length;
			}
		}
		g_front_state.net_runtime_broadcast_pending_payload.payload[0] =
			packet_type_byte;
		memcpy(g_front_state.net_runtime_broadcast_pending_payload
				       .payload +
			       1,
		       (const uint8_t *)packet + 4, packet_size - 4);
		g_front_state.net_runtime_broadcast_pending_payload
			.payload_length = packet_size - 3;
	} else if (g_front_state.net_group_dplay_id == (DPID)to_player_id) {
		sprintf(debug_text, "(SG %u) ",
			g_front_state.net_runtime_broadcast_seq_counter);
		packet_header |=
			(g_front_state.net_runtime_group_seq_counter & 0x7F)
			<< 8;
		++g_front_state.net_runtime_group_seq_counter;
		packet_header |= 0x8080;
		if (g_front_state.net_runtime_group_seq_counter > 127) {
			g_front_state.net_runtime_group_seq_counter = 0;
		}
		encoded_packet.packet_type_header = packet_header;
		encoded_payload = (uint8_t *)&encoded_packet.payload_size;
		encoded_size = 2;
		if (append_pending &&
		    net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_packet.payload_size = packet_size - 4;
			encoded_payload = encoded_packet.payload;
			encoded_size = 4;
		}
		memcpy(encoded_payload, (const uint8_t *)packet + 4,
		       packet_size - 4);
		encoded_payload += packet_size - 4;
		encoded_size += packet_size - 4;
		if (append_pending) {
			if (g_front_state.net_runtime_group_pending_payload
				    .piggyback_empty != 0) {
				*encoded_payload = NET_PACKET_NOP;
				++encoded_size;
				g_front_state.net_runtime_group_pending_payload
					.piggyback_empty = 0;
			} else {
				memcpy(encoded_payload,
				       g_front_state
					       .net_runtime_group_pending_payload
					       .payload,
				       g_front_state
					       .net_runtime_group_pending_payload
					       .payload_length);
				encoded_size +=
					g_front_state
						.net_runtime_group_pending_payload
						.payload_length;
			}
		}
		g_front_state.net_runtime_group_pending_payload.payload[0] =
			packet_type_byte;
		memcpy(g_front_state.net_runtime_group_pending_payload.payload +
			       1,
		       (const uint8_t *)packet + 4, packet_size - 4);
		g_front_state.net_runtime_group_pending_payload.payload_length =
			packet_size - 3;
	} else {
		unsigned int peer_index =
			net_find_or_create_peer_slot(to_player_id);
		if (g_front_state.net_reliable_peer_slot_count > peer_index &&
		    peer_index < 40) {
			sprintf(debug_text, "(SS %u) ",
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.send_seq);
			int send_sequence =
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.send_seq;
			packet_header |= (send_sequence++ & 0x7F) << 8;
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.send_seq = send_sequence;
			if (send_sequence > 127) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.send_seq = 0;
			}
		}
		packet_header |= 0x8000;
		encoded_packet.packet_type_header = packet_header;
		encoded_payload = (uint8_t *)&encoded_packet.payload_size;
		encoded_size = 2;
		if (append_pending &&
		    net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_packet.payload_size = packet_size - 4;
			encoded_payload = encoded_packet.payload;
			encoded_size = 4;
		}
		memcpy(encoded_payload, (const uint8_t *)packet + 4,
		       packet_size - 4);
		encoded_payload += packet_size - 4;
		encoded_size += packet_size - 4;
		if (append_pending) {
			memcpy(encoded_payload,
			       &g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_piggyback_type,
			       g_front_state
				       .net_runtime_reliable_peer_slots
					       [peer_index]
				       .piggyback_length);
			encoded_size += g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.piggyback_length;
		}
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_piggyback_type = packet_type_byte;
		memcpy(g_front_state.net_runtime_reliable_peer_slots[peer_index]
			       .piggyback_payload,
		       (const uint8_t *)packet + 4, packet_size - 4);
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.piggyback_length = packet_size - 3;
	}

	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)to_player_id) {
		memcpy(g_front_state
			       .net_runtime_sent_history
				       [g_front_state
						.net_runtime_sent_history_write_index]
			       .payload,
		       packet, packet_size);
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.direct_play_id = to_player_id;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.payload_size = packet_size;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.last_nack_ms = 0;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.nack_retry_count = 0;
		if (to_player_id == 0) {
			g_front_state
				.net_runtime_sent_history
					[g_front_state
						 .net_runtime_sent_history_write_index]
				.packet_class = 0;
		} else if (g_front_state.net_group_dplay_id ==
			   (DPID)to_player_id) {
			g_front_state
				.net_runtime_sent_history
					[g_front_state
						 .net_runtime_sent_history_write_index]
				.packet_class = 2;
		} else {
			g_front_state
				.net_runtime_sent_history
					[g_front_state
						 .net_runtime_sent_history_write_index]
				.packet_class = 1;
		}
		packet_header = encoded_packet.packet_type_header;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.sequence_byte = (packet_header & 0x7F00) >> 8;
		++g_front_state.net_runtime_sent_history_write_index;
		if (g_front_state.net_runtime_sent_history_write_index >= 128) {
			g_front_state.net_runtime_sent_history_write_index = 0;
		}
	}
	XVT_LOG_DEBUG(
		"network.lobby_packet_sent to=%u type=%u channel=%d seq=%d bytes=%u queued=%d",
		(unsigned)to_player_id, packet_type,
		to_player_id == 0					 ? 0
		: g_front_state.net_group_dplay_id == (DPID)to_player_id ? 2
									 : 1,
		(encoded_packet.packet_type_header & 0x7F00) >> 8, encoded_size,
		g_front_state.net_runtime_recv_queue_count);

	if ((g_front_state.net_runtime_local_player.player_id ==
		     (DPID)to_player_id ||
	     to_player_id == 0 || g_front_state.net_direct_play == NULL ||
	     g_front_state.net_group_dplay_id == (DPID)to_player_id) &&
	    g_front_state.net_runtime_recv_queue_count < 1024) {
		memcpy(g_front_state
			       .net_runtime_recv_queue
				       [g_front_state
						.net_runtime_recv_queue_write_index]
			       .payload,
		       packet, packet_size);
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.direct_play_id =
			g_front_state.net_runtime_local_player.player_id;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.payload_size = packet_size;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.last_nack_ms = 0;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.nack_retry_count = 0;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.is_resent_copy = 0;
		unsigned int peer_index = net_find_or_create_peer_slot(
			g_front_state.net_runtime_local_player.player_id);
		if (to_player_id == 0) {
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = 0;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    peer_index < 40) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_channel_a =
					(packet_header & 0x7F00) >> 8;
			}
		} else if (g_front_state.net_group_dplay_id ==
			   (DPID)to_player_id) {
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = 2;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    peer_index < 40) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_channel_b =
					(packet_header & 0x7F00) >> 8;
			}
		} else {
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = 1;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    peer_index < 40) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_default =
					(packet_header & 0x7F00) >> 8;
			}
		}
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.sequence_byte =
			(encoded_packet.packet_type_header & 0x7F00) >> 8;
		++g_front_state.net_runtime_recv_queue_count;
		++g_front_state.net_runtime_recv_queue_write_index;
		if (g_front_state.net_runtime_recv_queue_write_index >= 1024) {
			g_front_state.net_runtime_recv_queue_write_index = 0;
		}
	} else if (g_front_state.net_runtime_local_player.player_id ==
			   (DPID)to_player_id ||
		   to_player_id == 0 || g_front_state.net_direct_play == NULL ||
		   g_front_state.net_group_dplay_id == (DPID)to_player_id) {
		XVT_LOG_WARN(
			"network.lobby_local_copy_dropped to=%u type=%u queued=%d",
			(unsigned)to_player_id, packet_type,
			g_front_state.net_runtime_recv_queue_count);
	}

	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}
	int send_result = 0;
	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)to_player_id) {
		send_result = g_front_state.net_direct_play->lpVtbl->Send(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id,
			to_player_id, 0, &encoded_packet.packet_type_header,
			encoded_size);
	}
	if (send_result != 0) {
		char error_text[80];
		sprintf(error_text, "Send Returned: %-8x\n", send_result);
		if (send_result != DPERR_INVALIDPLAYER) {
			XVT_LOG_WARN(
				"network.lobby_send_failed to=%u type=%u result=%#x",
				(unsigned)to_player_id, packet_type,
				(unsigned)send_result);
		}
	}
	return send_result == 0;
}

/* Sends one packet outside the sequence scheme: sequence 0, no sent-history
 * entry, no local copy. A packet to the group carries the group bits; any
 * other carries its type alone, since the one-player mode (delivery_mode 1)
 * is never chosen. Outside types 60-63 a type with no fixed size gets a
 * length word, and every packet a NOP trailer. Nothing is sent to the local
 * player. Returns 1 without DirectPlay, when Send succeeds or when nothing
 * is sent, else 0. The last argument is ignored. */
// FUNCTION: XVT 0x4CF830
int net_send_direct_play_packet(int dest_player_id, const void *packet,
				int packet_size, int unused_send_mode)
{
	(void)unused_send_mode;

	HRESULT send_result = 0;
	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}

	uint32_t packet_type = *(const uint32_t *)packet;
	uint16_t packet_flags = (uint8_t)packet_type & 0x7F;
	int append_terminator = packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_PROBE_REQUEST;
	int delivery_mode = 0;
	if (dest_player_id != 0) {
		delivery_mode =
			g_front_state.net_group_dplay_id == (DPID)dest_player_id
				? 2
				: 0;
	}
	if (delivery_mode == 2) {
		packet_flags |= 0x8080;
	} else if (delivery_mode == 1) {
		packet_flags |= 0x8000;
	}

	struct net_direct_play_encoded_packet encoded_packet;
	encoded_packet.packet_type_header = packet_flags;
	uint8_t *encoded_payload = (uint8_t *)&encoded_packet.payload_size;
	int encoded_header_size = 2;
	if (append_terminator) {
		if (net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_payload = encoded_packet.payload;
			encoded_packet.payload_size = packet_size - 4;
			encoded_header_size = 4;
		}
	}
	memcpy(encoded_payload, (const uint8_t *)packet + 4, packet_size - 4);
	encoded_payload += packet_size - 4;
	int encoded_size = packet_size + encoded_header_size - 4;
	if (append_terminator) {
		++encoded_size;
		*encoded_payload = NET_PACKET_NOP;
	}
	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)dest_player_id) {
		send_result = g_front_state.net_direct_play->lpVtbl->Send(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id,
			dest_player_id, 0, &encoded_packet.packet_type_header,
			encoded_size);
	}
	XVT_LOG_DEBUG("network.lobby_control_sent to=%u type=%u bytes=%d",
		      (unsigned)dest_player_id, (unsigned)packet_type,
		      encoded_size);
	if (send_result != 0 && send_result != DPERR_INVALIDPLAYER) {
		XVT_LOG_WARN(
			"network.lobby_control_send_failed to=%u type=%u result=%#x",
			(unsigned)dest_player_id, (unsigned)packet_type,
			(unsigned)send_result);
	}
	return send_result == 0;
}

/* Resends a packet with a given channel (packet_class) and sequence: the
 * header carries the resend bits and a channel byte follows. Outside types
 * 60-63 a type with no fixed size gets a length word, and every packet a NOP
 * trailer. For the local player it queues the packet locally as a resent
 * copy instead (when fewer than 1024 are queued). Returns 1 without
 * DirectPlay, when Send succeeds, or for the local player; else 0. */
// FUNCTION: XVT 0x4CF980
int net_send_sequenced_direct_play_packet(int dest_player_id, int packet_class,
					  int sequence_id, const void *packet,
					  unsigned int packet_size)
{
	int send_result = 0;
	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}

	unsigned int packet_type = *(const uint32_t *)packet;
	uint8_t packet_type_byte = (uint8_t)packet_type & 0x7F;
	int append_terminator = packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_PROBE_REQUEST;
	char debug_text[256];
	switch (packet_class) {
	case 0:
		sprintf(debug_text, "(RSB %u) ", sequence_id);
		break;
	case 2:
		sprintf(debug_text, "(RSG %u) ", sequence_id);
		break;
	default:
		sprintf(debug_text, "(RSS %u) ", sequence_id);
	}

	struct net_direct_play_sequenced_packet encoded_packet;
	uint8_t *encoded_payload = (uint8_t *)&encoded_packet.payload_size;
	int encoded_header_size = 3;
	encoded_packet.packet_type_header =
		(int16_t)(((((sequence_id & 0x7F) << 8) | packet_type_byte) &
			   0x7F7F) |
			  0x80);
	encoded_packet.packet_class = (uint8_t)packet_class;
	if (append_terminator) {
		if (net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_payload = encoded_packet.payload;
			encoded_packet.payload_size =
				(int16_t)(packet_size - 4);
			encoded_header_size = 5;
		}
	}

	memcpy(encoded_payload, (const uint8_t *)packet + 4, packet_size - 4);
	encoded_payload += packet_size - 4;
	unsigned int encoded_size = packet_size + encoded_header_size - 4;
	if (append_terminator) {
		++encoded_size;
		*encoded_payload = NET_PACKET_NOP;
	}

	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)dest_player_id) {
		send_result = g_front_state.net_direct_play->lpVtbl->Send(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id,
			dest_player_id, 0, &encoded_packet.packet_type_header,
			encoded_size);
	} else {
		if (g_front_state.net_runtime_recv_queue_count < 1024) {
			memcpy(g_front_state
				       .net_runtime_recv_queue
					       [g_front_state
							.net_runtime_recv_queue_write_index]
				       .payload,
			       packet, packet_size);
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.direct_play_id =
				g_front_state.net_runtime_local_player
					.player_id;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.payload_size = packet_size;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = (uint8_t)packet_class;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.sequence_byte = (uint8_t)sequence_id;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.is_resent_copy = 1;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.last_nack_ms = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.nack_retry_count = 0;
			++g_front_state.net_runtime_recv_queue_count;
			++g_front_state.net_runtime_recv_queue_write_index;
			if (g_front_state.net_runtime_recv_queue_write_index >=
			    1024) {
				g_front_state
					.net_runtime_recv_queue_write_index = 0;
			}
		} else {
			XVT_LOG_WARN(
				"network.lobby_local_copy_dropped to=%u type=%u queued=%d",
				(unsigned)dest_player_id, packet_type,
				g_front_state.net_runtime_recv_queue_count);
		}
	}
	XVT_LOG_DEBUG(
		"network.lobby_packet_resent to=%u type=%u channel=%d seq=%d bytes=%u result=%#x",
		(unsigned)dest_player_id, packet_type, packet_class,
		sequence_id, encoded_size, (unsigned)send_result);
	return send_result == 0;
}

/* For each roster player but the local one whose peer slot has had no
 * delivery or keepalive for over 3,000 ms (last_activity_ms), sends a
 * NET_PACKET_KEEPALIVE outside the sequence scheme, carrying the next
 * sequence this side expects from it on the broadcast, group and one-player
 * channels and the current time in ms, and stamps last_activity_ms. Adds a
 * peer slot for any roster player that has none. Returns 1. */
// FUNCTION: XVT 0x4D1EA0
int net_send_sequence_keepalives(void)
{
	int player_count;
	int packet[128];

	unsigned int player_index = 0;
	struct net_player_info *player_roster =
		net_get_player_roster(&player_count);
	if ((unsigned int)player_count > 0) {
		do {
			unsigned int now_ms = GetTickCount();
			if (g_front_state.net_runtime_local_player.player_id !=
			    player_roster[player_index].player_id) {
				unsigned int peer_index =
					net_find_or_create_peer_slot(
						player_roster[player_index]
							.player_id);
				if (g_front_state.net_reliable_peer_slot_count >
					    peer_index &&
				    peer_index < 40) {
					if (now_ms -
						    g_front_state
							    .net_runtime_reliable_peer_slots
								    [peer_index]
							    .last_activity_ms >
					    3000) {
						packet[0] =
							NET_PACKET_KEEPALIVE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_activity_ms =
							now_ms;
						unsigned int sequence =
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_a +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[1] = sequence;
						sequence =
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_b +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[2] = sequence;
						sequence =
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_default +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[3] = sequence;
						packet[4] = GetTickCount();
						((int (*)(int, const void *,
							  int, int))
							 net_send_direct_play_packet)(
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.direct_play_id,
							packet, 20, 0);
						XVT_LOG_DEBUG(
							"network.lobby_keepalive_sent player=%u peer=%u broadcast=%d group=%d single=%d",
							(unsigned)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.direct_play_id,
							peer_index, packet[1],
							packet[2], packet[3]);
					}
				}
			}
			++player_index;
		} while (player_index < (unsigned int)player_count);
	}
	return 1;
}

#include "xvt/net/net_session_send.h"

#include <string.h>

#include "aeron/compat/dplay.h"
#include "xvt/frontend/config.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"

#pragma pack(push, 1)

struct net_session_compact_encoded_packet {
	/* Type in bits 0-6, sequence in 8-14, channel bits 0x80, 0x8000 */
	int16_t packet_type_header;
	/* Body length when the type takes one; else the body starts here */
	int16_t payload_size;
	uint8_t payload[1020]; /* Body, then the saved copy or a NOP */
};

struct net_session_sequenced_encoded_packet {
	/* Type, sequence, and bit 0x80 with bit 15 clear: a resend */
	int16_t packet_type_header;
	/* Channel class: 0 all players, 1 direct, 2 group */
	uint8_t packet_class;
	/* Body length when the type takes one; else the body starts here */
	int16_t payload_size;
	uint8_t payload[1019]; /* Body, then a NOP outside the resync types */
};

#pragma pack(pop)
typedef char xvt_size_net_session_compact_encoded_packet
	[(sizeof(struct net_session_compact_encoded_packet) == 1024) ? 1 : -1];
typedef char xvt_size_net_session_sequenced_encoded_packet
	[(sizeof(struct net_session_sequenced_encoded_packet) == 1024) ? 1
								       : -1];

/* Sends the packet with net_session_send_packet to every active roster entry with
 * a nonzero id, this player included. Returns the last send's result when the
 * last roster entry was sent to; otherwise that entry's DirectPlay id, or the
 * roster count when the roster is empty. */
// FUNCTION: XVT 0x46D300
int net_session_broadcast_packet_to_players(unsigned int *payload,
					    int payload_size)
{
	int result = g_net_session.player_count;
	if (result > 0) {
		for (int player_index = 0;
		     player_index < g_net_session.player_count;
		     ++player_index) {
			result = g_net_session.players[player_index]
					 .direct_play_id;
			if (result != 0 &&
			    g_net_session.players[player_index].active_flag !=
				    0) {
				result = net_session_send_packet(
					result, payload, payload_size);
			}
		}
	}
	return result;
}

/* Part of net_session_send_packet on the group channel: puts the next sequence
 * of group_seq_counter and bits 0x8080 in packet_header, writes the packet into
 * encoded_packet and its size into encoded_size, and makes the packet the group
 * channel's saved copy. */
static void net_session_encode_group_packet(
	unsigned int *payload, signed int payload_size,
	unsigned int packet_type, uint8_t packet_type_byte[4],
	int append_pending, uint16_t *packet_header,
	struct net_session_compact_encoded_packet *encoded_packet,
	int *encoded_size)
{
	uint8_t *encoded_payload;
	int encoded_header_size;
	*packet_header |= (g_net_session.group_seq_counter & 0x7F) << 8;
	++g_net_session.group_seq_counter;
	*packet_header |= 0x8080;
	if ((int)g_net_session.group_seq_counter > 127) {
		g_net_session.group_seq_counter = 0;
	}
	encoded_packet->packet_type_header = *packet_header;
	encoded_payload = (uint8_t *)&encoded_packet->payload_size;
	encoded_header_size = 2;
	if (append_pending &&
	    net_session_get_fixed_payload_size(packet_type) == 0) {
		encoded_packet->payload_size = payload_size - 4;
		encoded_payload = encoded_packet->payload;
		encoded_header_size = 4;
	}
	memcpy(encoded_payload, payload + 1, payload_size - 4);
	encoded_payload += payload_size - 4;
	*encoded_size = payload_size + encoded_header_size - 4;
	if (append_pending) {
		if (g_net_session.group_piggyback_empty != 0) {
			*encoded_payload = NET_PACKET_NOP;
			g_net_session.group_piggyback_empty = 0;
			++*encoded_size;
		} else {
			memcpy(encoded_payload, g_net_session.group_payload,
			       g_net_session.group_payload_length);
			*encoded_size += g_net_session.group_payload_length;
		}
	}
	g_net_session.group_payload[0] = packet_type_byte[0];
	memcpy(g_net_session.group_payload + 1, payload + 1, payload_size - 4);
	g_net_session.group_payload_length = payload_size - 3;
}

/* Part of net_session_send_packet to all players: puts the next sequence of
 * broadcast_seq_counter in packet_header, writes the packet into encoded_packet
 * and its size into encoded_size, and makes the packet the broadcast channel's
 * saved copy. */
static void net_session_encode_broadcast_packet(
	unsigned int *payload, signed int payload_size,
	unsigned int packet_type, uint8_t packet_type_byte[4],
	int append_pending, uint16_t *packet_header,
	struct net_session_compact_encoded_packet *encoded_packet,
	int *encoded_size)
{
	uint8_t *encoded_payload;
	int encoded_header_size;
	*packet_header |= (g_net_session.broadcast_seq_counter & 0x7F) << 8;
	++g_net_session.broadcast_seq_counter;
	if ((int)g_net_session.broadcast_seq_counter > 127) {
		g_net_session.broadcast_seq_counter = 0;
	}
	encoded_packet->packet_type_header = *packet_header;
	encoded_payload = (uint8_t *)&encoded_packet->payload_size;
	encoded_header_size = 2;
	if (append_pending &&
	    net_session_get_fixed_payload_size(packet_type) == 0) {
		encoded_packet->payload_size = payload_size - 4;
		encoded_payload = encoded_packet->payload;
		encoded_header_size = 4;
	}
	memcpy(encoded_payload, payload + 1, payload_size - 4);
	encoded_payload += payload_size - 4;
	*encoded_size = payload_size + encoded_header_size - 4;
	if (append_pending) {
		if (g_net_session.broadcast_piggyback_empty != 0) {
			*encoded_payload = NET_PACKET_NOP;
			g_net_session.broadcast_piggyback_empty = 0;
			++*encoded_size;
		} else {
			memcpy(encoded_payload, g_net_session.broadcast_payload,
			       g_net_session.broadcast_payload_length);
			*encoded_size += g_net_session.broadcast_payload_length;
		}
	}
	g_net_session.broadcast_payload[0] = packet_type_byte[0];
	memcpy(g_net_session.broadcast_payload + 1, payload + 1,
	       payload_size - 4);
	g_net_session.broadcast_payload_length = payload_size - 3;
}

/* Part of net_session_send_packet to one player: puts the peer's reliable slot
 * sequence and bit 0x8000 in packet_header, writes the packet into
 * encoded_packet and its size into encoded_size, and makes the packet the
 * slot's saved copy. */
static void net_session_encode_direct_packet(
	int direct_play_id, unsigned int *payload, signed int payload_size,
	unsigned int packet_type, uint8_t packet_type_byte[4],
	int append_pending, uint16_t *packet_header,
	struct net_session_compact_encoded_packet *encoded_packet,
	int *encoded_size)
{
	uint8_t *encoded_payload;
	int encoded_header_size;
	unsigned int peer_slot =
		net_reliable_find_or_create_peer_slot(direct_play_id);
	if (g_net_session.reliable_peer_slot_count > peer_slot &&
	    peer_slot < 40) {
		int send_sequence =
			g_net_session.reliable_peer_slots[peer_slot].send_seq;
		*packet_header |= (send_sequence++ & 0x7F) << 8;
		g_net_session.reliable_peer_slots[peer_slot].send_seq =
			send_sequence;
		if (send_sequence > 127) {
			g_net_session.reliable_peer_slots[peer_slot].send_seq =
				0;
		}
	}
	*packet_header |= 0x8000;
	encoded_packet->packet_type_header = *packet_header;
	encoded_payload = (uint8_t *)&encoded_packet->payload_size;
	encoded_header_size = 2;
	if (append_pending &&
	    net_session_get_fixed_payload_size(packet_type) == 0) {
		encoded_packet->payload_size = payload_size - 4;
		encoded_payload = encoded_packet->payload;
		encoded_header_size = 4;
	}
	memcpy(encoded_payload, payload + 1, payload_size - 4);
	encoded_payload += payload_size - 4;
	*encoded_size = payload_size + encoded_header_size - 4;
	if (append_pending) {
		memcpy(encoded_payload,
		       &g_net_session.reliable_peer_slots[peer_slot]
				.last_piggyback_type,
		       g_net_session.reliable_peer_slots[peer_slot]
			       .piggyback_length);
		*encoded_size += g_net_session.reliable_peer_slots[peer_slot]
					 .piggyback_length;
	}
	g_net_session.reliable_peer_slots[peer_slot].last_piggyback_type =
		packet_type_byte[0];
	memcpy(g_net_session.reliable_peer_slots[peer_slot].piggyback_payload,
	       payload + 1, payload_size - 4);
	g_net_session.reliable_peer_slots[peer_slot].piggyback_length =
		payload_size - 3;
}

/* Part of net_session_send_packet for a packet to others: keeps it in
 * g_net_session_sent_history, and a world message also in
 * g_net_session_sent_world_message_history, with its destination, size, channel
 * class and the sequence in encoded_packet's header. */
static void net_session_record_sent_packet(
	int direct_play_id, unsigned int *payload, signed int payload_size,
	unsigned int packet_type,
	struct net_session_compact_encoded_packet *encoded_packet)
{
	if (packet_type == NET_PACKET_WORLD_MESSAGE) {
		memcpy(g_net_session_sent_world_message_history
			       [g_net_session_sent_world_message_write_index]
				       .payload,
		       payload, payload_size);
		struct net_queued_packet *queued_packet =
			&g_net_session_sent_world_message_history
				[g_net_session_sent_world_message_write_index];
		queued_packet->direct_play_id = direct_play_id;
		queued_packet->payload_size = payload_size;
		queued_packet->last_nack_ms = 0;
		queued_packet->nack_retry_count = 0;
		queued_packet->packet_class = 0;
		queued_packet->sequence_byte =
			(encoded_packet->packet_type_header & 0x7F00) >> 8;
		++g_net_session_sent_world_message_write_index;
		if (g_net_session_sent_world_message_write_index >= 256) {
			g_net_session_sent_world_message_write_index = 0;
		}
	}

	{
		memcpy(g_net_session_sent_history
			       [g_net_session_sent_history_write_index]
				       .payload,
		       payload, payload_size);
		int history_index = g_net_session_sent_history_write_index;
		g_net_session_sent_history[history_index].direct_play_id =
			direct_play_id;
		g_net_session_sent_history[history_index].payload_size =
			payload_size;
		g_net_session_sent_history[history_index].last_nack_ms = 0;
		g_net_session_sent_history[history_index].nack_retry_count = 0;
		if (direct_play_id == 0) {
			g_net_session_sent_history[history_index].packet_class =
				0;
		} else if (direct_play_id == g_net_session.group_dplay_id) {
			g_net_session_sent_history[history_index].packet_class =
				2;
		} else {
			g_net_session_sent_history[history_index].packet_class =
				1;
		}
		++g_net_session_sent_history_write_index;
		g_net_session_sent_history[history_index].sequence_byte =
			(encoded_packet->packet_type_header & 0x7F00) >> 8;
	}
	if (g_net_session_sent_history_write_index >= 128) {
		g_net_session_sent_history_write_index = 0;
	}
}

/* Part of net_session_send_packet's copy for this player: sets the queue
 * entry's channel class and, when this player's peer slot exists, records the
 * sequence in packet_header as that channel's receive sequence. */
static void net_session_note_own_copy_channel(int direct_play_id,
					      unsigned int packet_type,
					      uint16_t *packet_header)
{
	unsigned int peer_slot = net_reliable_find_or_create_peer_slot(
		g_net_session.local_player_info.direct_play_id);
	int peer_slot_available;
	if (packet_type == NET_PACKET_REMOTE_INPUT &&
	    g_game_config.internet_play == 1) {
		peer_slot_available =
			peer_slot < g_net_session.reliable_peer_slot_count;
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.packet_class = 2;
		if (peer_slot_available && peer_slot < 40) {
			*packet_header &= 0x7F00;
			*packet_header >>= 8;
			g_net_session.reliable_peer_slots[peer_slot]
				.recv_seq_channel_b = *packet_header;
		}
	} else if (direct_play_id == 0) {
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.packet_class = 0;
		if (peer_slot < g_net_session.reliable_peer_slot_count &&
		    peer_slot < 40) {
			*packet_header &= 0x7F00;
			*packet_header >>= 8;
			g_net_session.reliable_peer_slots[peer_slot]
				.recv_seq_channel_a = *packet_header;
		}
	} else if (direct_play_id == g_net_session.group_dplay_id) {
		peer_slot_available =
			peer_slot < g_net_session.reliable_peer_slot_count;
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.packet_class = 2;
		if (peer_slot_available && peer_slot < 40) {
			*packet_header &= 0x7F00;
			*packet_header >>= 8;
			g_net_session.reliable_peer_slots[peer_slot]
				.recv_seq_channel_b = *packet_header;
		}
	} else {
		peer_slot_available =
			peer_slot < g_net_session.reliable_peer_slot_count;
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.packet_class = 1;
		if (peer_slot_available && peer_slot < 40) {
			*packet_header &= 0x7F00;
			*packet_header >>= 8;
			g_net_session.reliable_peer_slots[peer_slot]
				.recv_seq_default = *packet_header;
		}
	}
}

/* Part of net_session_send_packet: queues the packet for this player to
 * receive, in g_net_session_recv_queue with g_net_recv_queue_write_index and
 * g_net_recv_queue_count; a full queue first goes through
 * net_reliable_keep_only_host_received_packets. */
static void net_session_queue_own_copy(
	int direct_play_id, unsigned int *payload, signed int payload_size,
	unsigned int packet_type, uint16_t *packet_header,
	struct net_session_compact_encoded_packet *encoded_packet)
{
	if ((int)g_net_recv_queue_count >= 1024) {
		net_reliable_keep_only_host_received_packets();
		XVT_LOG_WARN(
			"network.receive_queue_purged site=\"send\" kept=%u",
			g_net_recv_queue_count);
	}
	if ((int)g_net_recv_queue_count < 1024) {
		memcpy(g_net_session_recv_queue[g_net_recv_queue_write_index]
			       .payload,
		       payload, payload_size);
		unsigned int queue_index =
			(unsigned int)g_net_recv_queue_write_index;
		g_net_session_recv_queue[queue_index].direct_play_id =
			(DPID)g_net_session.local_player_info.direct_play_id;
		g_net_session_recv_queue[queue_index].payload_size =
			payload_size;
		g_net_session_recv_queue[queue_index].last_nack_ms = 0;
		g_net_session_recv_queue[queue_index].nack_retry_count = 0;
		g_net_session_recv_queue[queue_index].is_resent_copy = 0;
		net_session_note_own_copy_channel(direct_play_id, packet_type,
						  packet_header);
		g_net_session_recv_queue[g_net_recv_queue_write_index]
			.sequence_byte =
			(encoded_packet->packet_type_header & 0x7F00) >> 8;
		++g_net_recv_queue_count;
		++g_net_recv_queue_write_index;
		if (g_net_recv_queue_write_index >= 1024) {
			g_net_recv_queue_write_index = 0;
		}
	} else {
		XVT_LOG_ERROR(
			"network.own_packet_dropped site=\"send\" type=%u queued=%u",
			packet_type, g_net_recv_queue_count);
	}
}

/* Sends one game packet whose first word is its type; payload_size counts its
 * bytes, and below 4 nothing is sent and 0 returned. The packet goes out with a
 * 2-byte header, the type in the low 7 bits and a 7-bit sequence above, on one
 * of three channels: to id 0, all players, on
 * g_net_session.broadcast_seq_counter; to the session group, or any
 * internet-play REMOTE_INPUT, on group_seq_counter (bits 0x8080); to one
 * player, on that peer's reliable slot sequence (bit 0x8000). Outside the
 * resync types (RESYNC_CHECKSUMS to RESYNC_CHUNK) the body gets a 2-byte length
 * and, behind it, the channel's previous packet (a NOP the first time), from
 * which a receiver can recover a lost one; this packet then becomes the
 * channel's saved copy. Packets to others go into g_net_session_sent_history,
 * and world messages into g_net_session_sent_world_message_history, for
 * resends; internet-play inputs do not. A packet to all, to the group, to this
 * player, or sent with no DirectPlay interface is also queued for this player
 * to receive, in g_net_session_recv_queue with g_net_recv_queue_write_index and
 * g_net_recv_queue_count. Returns 1 when DirectPlay takes it, when the
 * destination is this player, or with no interface; else 0. It records its own
 * receive sequence only when its peer slot exists. Does not check payload_size
 * against the 512-byte saved copies and history entries, or the reliable slot
 * index before it uses the slot's saved copy. */
// FUNCTION: XVT 0x46D350
int net_session_send_packet(int direct_play_id, unsigned int *payload,
			    signed int payload_size)
{
	int send_result = 0;
	if (payload_size < 4) {
		XVT_LOG_ERROR("network.send_too_short bytes=%d", payload_size);
		return 0;
	}

	unsigned int packet_type = *payload;
	uint8_t packet_type_byte[4];
	packet_type_byte[0] = packet_type & 0x7F;
	uint16_t packet_header = packet_type_byte[0];
	int append_pending;
	if (packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
	    packet_type >= NET_PACKET_RESYNC_CHUNK + 1) {
		append_pending = 1;
	} else {
		append_pending = 0;
	}

	struct net_session_compact_encoded_packet encoded_packet;
	int encoded_size;
	if (packet_type == NET_PACKET_REMOTE_INPUT &&
	    g_game_config.internet_play == 1) {
		net_session_encode_group_packet(payload, payload_size,
						packet_type, packet_type_byte,
						append_pending, &packet_header,
						&encoded_packet, &encoded_size);
	} else if (direct_play_id == 0) {
		net_session_encode_broadcast_packet(
			payload, payload_size, packet_type, packet_type_byte,
			append_pending, &packet_header, &encoded_packet,
			&encoded_size);
	} else if (direct_play_id == g_net_session.group_dplay_id) {
		net_session_encode_group_packet(payload, payload_size,
						packet_type, packet_type_byte,
						append_pending, &packet_header,
						&encoded_packet, &encoded_size);
	} else {
		net_session_encode_direct_packet(
			direct_play_id, payload, payload_size, packet_type,
			packet_type_byte, append_pending, &packet_header,
			&encoded_packet, &encoded_size);
	}

	if ((packet_type != NET_PACKET_REMOTE_INPUT ||
	     g_game_config.internet_play != 1) &&
	    g_net_session.local_player_info.direct_play_id != direct_play_id) {
		net_session_record_sent_packet(direct_play_id, payload,
					       payload_size, packet_type,
					       &encoded_packet);
	}

	if (g_net_session.local_player_info.direct_play_id == direct_play_id ||
	    direct_play_id == 0 || g_net_session.dplay_interface == NULL ||
	    direct_play_id == g_net_session.group_dplay_id) {
		net_session_queue_own_copy(direct_play_id, payload,
					   payload_size, packet_type,
					   &packet_header, &encoded_packet);
	}

	if (g_net_session.dplay_interface == NULL) {
		return 1;
	}
	if (g_net_session.local_player_info.direct_play_id != direct_play_id) {
		send_result = g_net_session.dplay_interface->lpVtbl->Send(
			g_net_session.dplay_interface,
			(DPID)g_net_session.local_player_info.direct_play_id,
			(DPID)direct_play_id, 0,
			&encoded_packet.packet_type_header, encoded_size);
	}
	XVT_LOG_DEBUG(
		"network.packet_sent to=%u type=%u channel=%d sequence=%u bytes=%d result=%#x queued=%u",
		(unsigned)direct_play_id, packet_type,
		(encoded_packet.packet_type_header & 0x80)     ? 2
		: (encoded_packet.packet_type_header & 0x8000) ? 1
							       : 0,
		((unsigned)encoded_packet.packet_type_header & 0x7F00u) >> 8,
		encoded_size, (unsigned)send_result, g_net_recv_queue_count);
	if (send_result != 0 && send_result != DPERR_INVALIDPLAYER) {
		XVT_LOG_WARN(
			"network.send_failed kind=\"packet\" to=%u type=%u sequence=%u bytes=%d result=%#x",
			(unsigned)direct_play_id, packet_type,
			((unsigned)encoded_packet.packet_type_header &
			 0x7F00u) >>
				8,
			encoded_size, (unsigned)send_result);
	}
	return send_result == 0;
}

/* Resends a packet from history to one player under its original channel class
 * and sequence: the header carries the type, the sequence and bit 0x80 with bit
 * 15 clear, which marks a resend, then the class byte; outside the resync types
 * the body gets a 2-byte length and a trailing NOP instead of a saved copy.
 * Sent to this player itself, it is queued locally as a resent copy when the
 * queue has room, in g_net_session_recv_queue with g_net_recv_queue_write_index and
 * g_net_recv_queue_count. Returns 1 when DirectPlay takes it, when it was for this
 * player, or with no DirectPlay interface; else 0. Does not check that
 * packet_size is at least 4. */
// FUNCTION: XVT 0x46DD80
int net_session_send_sequenced_game_packet(int dest_dplay_id,
					   uint8_t packet_class,
					   uint8_t sequence,
					   const unsigned int *packet,
					   unsigned int packet_size)
{
	HRESULT send_result = 0;
	if (g_net_session.dplay_interface == NULL) {
		return 1;
	}

	unsigned int packet_type = *packet;
	uint16_t packet_flags = (uint8_t)packet_type & 0x7F;
	int append_terminator = packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_RESYNC_CHUNK + 1;
	packet_flags |= (uint16_t)(sequence & 0x7F) << 8;
	packet_flags |= 0x80;
	packet_flags &= 0x7FFF;
	struct net_session_sequenced_encoded_packet encoded_packet;
	encoded_packet.packet_type_header = (int16_t)packet_flags;
	encoded_packet.packet_class = packet_class;
	uint8_t *encoded_payload = (uint8_t *)&encoded_packet.payload_size;
	int encoded_size = 3;
	unsigned int packet_data_size;
	if (append_terminator) {
		int fixed_payload_size =
			net_session_get_fixed_payload_size((int)packet_type);
		packet_data_size = packet_size;
		if (fixed_payload_size == 0) {
			encoded_payload = encoded_packet.payload;
			encoded_packet.payload_size =
				(int16_t)(packet_data_size - 4);
			encoded_size = 5;
		}
	} else {
		packet_data_size = packet_size;
	}

	memcpy(encoded_payload, packet + 1, packet_data_size - 4);
	encoded_payload += packet_data_size - 4;
	encoded_size += (int)packet_data_size - 4;
	if (append_terminator) {
		*encoded_payload = NET_PACKET_NOP;
		++encoded_size;
	}

	if (g_net_session.local_player_info.direct_play_id != dest_dplay_id) {
		send_result = g_net_session.dplay_interface->lpVtbl->Send(
			g_net_session.dplay_interface,
			(DPID)g_net_session.local_player_info.direct_play_id,
			(DPID)dest_dplay_id, 0,
			&encoded_packet.packet_type_header,
			(uint32_t)encoded_size);
		XVT_LOG_DEBUG(
			"network.resend_sent to=%u channel=%u sequence=%u type=%u bytes=%d result=%#x",
			(unsigned)dest_dplay_id, (unsigned)packet_class,
			(unsigned)sequence, packet_type, encoded_size,
			(unsigned)send_result);
		if (send_result != 0 && send_result != DPERR_INVALIDPLAYER) {
			XVT_LOG_WARN(
				"network.send_failed kind=\"resend\" to=%u type=%u sequence=%u bytes=%d result=%#x",
				(unsigned)dest_dplay_id, packet_type,
				(unsigned)sequence, encoded_size,
				(unsigned)send_result);
		}
	} else {
		if ((int)g_net_recv_queue_count >= 1024) {
			net_reliable_keep_only_host_received_packets();
			XVT_LOG_WARN(
				"network.receive_queue_purged site=\"resend\" kept=%u",
				g_net_recv_queue_count);
		}
		if ((int)g_net_recv_queue_count < 1024) {
			memcpy(g_net_session_recv_queue
				       [g_net_recv_queue_write_index]
					       .payload,
			       packet, packet_data_size);
			unsigned int queue_index =
				(unsigned int)g_net_recv_queue_write_index;
			g_net_session_recv_queue[queue_index].direct_play_id =
				(DPID)g_net_session.local_player_info
					.direct_play_id;
			g_net_session_recv_queue[queue_index].payload_size =
				packet_data_size;
			unsigned int queue_count = g_net_recv_queue_count;
			g_net_session_recv_queue[queue_index].packet_class =
				packet_class;
			++queue_count;
			g_net_session_recv_queue[queue_index].sequence_byte =
				sequence;
			g_net_recv_queue_count = queue_count;
			g_net_session_recv_queue[queue_index].is_resent_copy =
				1;
			g_net_session_recv_queue[queue_index].last_nack_ms = 0;
			g_net_session_recv_queue[queue_index].nack_retry_count =
				0;
			++g_net_recv_queue_write_index;
			if (g_net_recv_queue_write_index >= 1024) {
				g_net_recv_queue_write_index = 0;
			}
		} else {
			XVT_LOG_ERROR(
				"network.own_packet_dropped site=\"resend\" type=%u queued=%u",
				packet_type, g_net_recv_queue_count);
		}
	}

	return send_result == 0;
}

/* Sends a control packet (NACK, WORLD_NACK, KEEPALIVE) with sequence 0 and no
 * saved copy behind it: the header holds the type, with bits 0x8080 for the
 * session group or an internet-play REMOTE_INPUT; outside the resync types the
 * body gets a 2-byte length and a trailing NOP. Nothing is sent to this player
 * itself. Returns 1 when DirectPlay takes it, when the destination is this
 * player, or with no DirectPlay interface; else 0. The fourth argument, 0 or 1
 * at every call, is never read, and the delivery_mode 1 branch cannot be taken.
 * Does not check payload_size. */
// FUNCTION: XVT 0x46F470
int net_session_send_compact_game_packet(int direct_play_id,
					 unsigned int *payload,
					 int payload_size, ...)
{
	HRESULT send_result = 0;
	if (g_net_session.dplay_interface == NULL) {
		return 1;
	}
	unsigned int packet_type = *payload;
	uint16_t packet_flags = (uint8_t)packet_type & 0x7F;
	int append_terminator = packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_RESYNC_CHUNK + 1;
	int delivery_mode;
	if (packet_type == NET_PACKET_REMOTE_INPUT &&
	    g_game_config.internet_play == 1) {
		delivery_mode = 2;
	} else {
		if (direct_play_id == 0) {
			delivery_mode = 0;
		} else if (direct_play_id == g_net_session.group_dplay_id) {
			delivery_mode = 2;
		} else {
			delivery_mode = 0;
		}
	}
	if (delivery_mode == 2) {
		packet_flags |= 0x8080;
	} else if (delivery_mode == 1) {
		packet_flags |= 0x8000;
	}
	struct net_session_compact_encoded_packet encoded_packet;
	encoded_packet.packet_type_header = (int16_t)packet_flags;
	uint8_t *encoded_payload = (uint8_t *)&encoded_packet.payload_size;
	int encoded_size = 2;
	int packet_data_size;
	if (append_terminator) {
		int fixed_payload_size =
			net_session_get_fixed_payload_size((int)packet_type);
		packet_data_size = payload_size;
		if (fixed_payload_size == 0) {
			encoded_payload = encoded_packet.payload;
			encoded_packet.payload_size =
				(int16_t)(packet_data_size - 4);
			encoded_size = 4;
		}
	} else {
		packet_data_size = payload_size;
	}
	memcpy(encoded_payload, payload + 1, (size_t)(packet_data_size - 4));
	encoded_payload += packet_data_size - 4;
	encoded_size += packet_data_size - 4;
	if (append_terminator) {
		++encoded_size;
		*encoded_payload = NET_PACKET_NOP;
	}
	if (direct_play_id != g_net_session.local_player_info.direct_play_id) {
		send_result = g_net_session.dplay_interface->lpVtbl->Send(
			g_net_session.dplay_interface,
			(DPID)g_net_session.local_player_info.direct_play_id,
			(DPID)direct_play_id, 0,
			&encoded_packet.packet_type_header,
			(uint32_t)encoded_size);
	}
	XVT_LOG_DEBUG("network.control_sent to=%u type=%u bytes=%d result=%#x",
		      (unsigned)direct_play_id, packet_type, encoded_size,
		      (unsigned)send_result);
	if (send_result != 0 && send_result != DPERR_INVALIDPLAYER) {
		XVT_LOG_WARN(
			"network.send_failed kind=\"control\" to=%u type=%u sequence=%u bytes=%d result=%#x",
			(unsigned)direct_play_id, packet_type,
			((unsigned)encoded_packet.packet_type_header &
			 0x7F00u) >>
				8,
			encoded_size, (unsigned)send_result);
	}

	return send_result == 0;
}

/* Sends each other roster player a KEEPALIVE when its reliable peer slot has
 * been idle over 3,000 ms, holding the sequence after the newest this player
 * has seen from it on each channel (all players, group, direct) and the time;
 * the peer then resends the packet with that sequence on each channel, if it
 * has sent one. Only peers in the first 8 of the 40 slots get one. Creates a
 * slot for any roster player that has none, and writes
 * g_net_session_scratch_packet and the slot's activity time. Returns 1. */
// FUNCTION: XVT 0x46FAE0
int net_session_send_reliable_keepalives(void)
{
	unsigned int player_index = 0;
	int player_count;
	struct session_player_info *player_roster =
		net_session_get_player_roster(&player_count);
	if (player_count != 0) {
		struct session_player_info *player = player_roster;
		do {
			uint32_t current_time = timeGetTime();
			if (g_net_session.local_player_info.direct_play_id !=
			    player->direct_play_id) {
				unsigned int peer_slot =
					net_reliable_find_or_create_peer_slot(
						player->direct_play_id);
				if (peer_slot <
					    g_net_session
						    .reliable_peer_slot_count &&
				    peer_slot < 8) {
					unsigned int reliable_peer_index =
						peer_slot;
					if (current_time -
						    g_net_session
							    .reliable_peer_slots
								    [peer_slot]
							    .last_activity_ms >
					    3000) {
						g_net_session
							.reliable_peer_slots
								[peer_slot]
							.last_activity_ms =
							current_time;
						XVT_LOG_DEBUG(
							"network.keepalive_sent peer=%u broadcast=%d group=%d direct=%d",
							peer_slot,
							g_net_session
								.reliable_peer_slots
									[peer_slot]
								.recv_seq_channel_a,
							g_net_session
								.reliable_peer_slots
									[peer_slot]
								.recv_seq_channel_b,
							g_net_session
								.reliable_peer_slots
									[peer_slot]
								.recv_seq_default);
						g_net_session_scratch_packet
							.packet_type =
							NET_PACKET_KEEPALIVE;
						unsigned int next_sequence =
							g_net_session
								.reliable_peer_slots
									[reliable_peer_index]
								.recv_seq_channel_a +
							1;
						if (next_sequence > 127) {
							next_sequence = 0;
						}
						g_net_session_scratch_packet
							.payload_dwords[0] =
							(int)next_sequence;
						next_sequence =
							g_net_session
								.reliable_peer_slots
									[reliable_peer_index]
								.recv_seq_channel_b +
							1;
						if (next_sequence > 127) {
							next_sequence = 0;
						}
						g_net_session_scratch_packet
							.payload_dwords[1] =
							(int)next_sequence;
						next_sequence =
							g_net_session
								.reliable_peer_slots
									[reliable_peer_index]
								.recv_seq_default +
							1;
						if (next_sequence > 127) {
							next_sequence = 0;
						}
						g_net_session_scratch_packet
							.payload_dwords[2] =
							(int)next_sequence;
						g_net_session_scratch_packet
							.payload_dwords[3] =
							(int)timeGetTime();
						net_session_send_compact_game_packet(
							g_net_session
								.reliable_peer_slots
									[reliable_peer_index]
								.direct_play_id,
							(unsigned int
								 *)&g_net_session_scratch_packet,
							20, 0);
					}
				}
			}
			++player;
			++player_index;
		} while ((unsigned int)player_count > player_index);
	}
	return 1;
}

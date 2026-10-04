#include "xvt/net/net_reliable.h"

#include <stddef.h>
#include <string.h>

#include "xvt/net/net_session.h"
#include "xvt/util/time.h"

/* Index of the next free entry in g_net_session_recv_queue, 0 to 1023. Six
 * writers; chiefly net_session_pump_incoming_packets, which queues arrivals,
 * net_reliable_remove_queued_packet, which steps it back one, and
 * net_session_import_runtime_state, which copies in the lobby's index when a
 * flight session starts. */
// GLOBAL: XVT 0x527024
int g_net_recv_queue_write_index = 0;
/* Index of the oldest entry in g_net_session_recv_queue, 0 to 1023. Written by
 * net_session_receive_packet and net_reliable_remove_queued_packet as entries
 * leave, by net_reliable_reset_recv_queue_state, which sets it to the write index,
 * and by net_session_import_runtime_state. */
// GLOBAL: XVT 0x527028
int g_net_recv_queue_read_index;
/* Entries held in g_net_session_recv_queue, 0 to 1024. Nine writers; chiefly
 * net_session_pump_incoming_packets, which adds arrivals, and
 * net_reliable_remove_queued_packet, which takes them out.
 * net_session_init_game_session sets it to 0 before net_session_import_runtime_state
 * copies in the lobby's count; net_reliable_reset_recv_queue_state sets it to 0. */
// GLOBAL: XVT 0x52702C
unsigned int g_net_recv_queue_count;
/* The flight session's receive queue, a ring of 1024 packets: DirectPlay
 * system messages, packets from other players, resent copies and the local
 * player's own packets, held until net_session_receive_packet hands them out in
 * sequence order. Seven writers; chiefly net_session_pump_incoming_packets. */
// GLOBAL: XVT 0x5599A8
struct net_queued_packet g_net_session_recv_queue[1024];
/* Sequence, 0 to 127, of the last packet net_session_receive_packet delivered;
 * only that function writes it. Read only by
 * net_reliable_get_last_delivered_recv_sequence, which nothing calls. */
// GLOBAL: XVT 0x60F1BC
int g_net_last_delivered_recv_sequence;

/* Returns g_net_last_delivered_recv_sequence. Nothing in the engine calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x46F650
int net_reliable_get_last_delivered_recv_sequence(void)
{
	return g_net_last_delivered_recv_sequence;
}

/* Tells whether a flight-session packet's sequence was already received from
 * that player on its channel: broadcast when channel_a is set, else group when
 * channel_b is set, else one-player. Returns 1 when the sequence is not 1 to 63
 * ahead of the newest one received, counting modulo 128 (a duplicate or a
 * stale packet). Otherwise records it as the newest in
 * g_net_session.reliable_peer_slots and returns 0. Also returns 0, recording
 * nothing, when the call had to add a peer slot or the 40-slot table is
 * full. */
// FUNCTION: XVT 0x46FC00
int net_reliable_check_and_record_recv_sequence(int direct_play_id,
						int sequence, int channel_a,
						int channel_b)
{
	uint32_t saved_slot_count;
	unsigned int slot;
	int recv_sequence;
	int delta;

	saved_slot_count = g_net_session.reliable_peer_slot_count;
	slot = net_reliable_find_or_create_peer_slot(direct_play_id);
	if (saved_slot_count != g_net_session.reliable_peer_slot_count ||
	    slot >= 40) {
		return 0;
	}

	if (channel_a) {
		recv_sequence = g_net_session.reliable_peer_slots[slot]
					.recv_seq_channel_a;
	} else if (channel_b) {
		recv_sequence = g_net_session.reliable_peer_slots[slot]
					.recv_seq_channel_b;
	} else {
		recv_sequence = g_net_session.reliable_peer_slots[slot]
					.recv_seq_default;
	}

	delta = sequence - recv_sequence;
	if (delta >= -64 && (delta <= 0 || delta >= 64)) {
		return 1;
	}

	if (channel_a) {
		g_net_session.reliable_peer_slots[slot].recv_seq_channel_a =
			sequence;
	} else if (channel_b) {
		g_net_session.reliable_peer_slots[slot].recv_seq_channel_b =
			sequence;
	} else {
		g_net_session.reliable_peer_slots[slot].recv_seq_default =
			sequence;
	}
	return 0;
}

/* Finds a queued reliable receive packet matching sequence, channel, and peer
 * filters. Returns the ring index or -1. */
/* Searches g_net_session_recv_queue from the oldest entry and looks only at
 * resent copies. A packet matches when its sender holds slot peer_slot in
 * g_net_session.reliable_peer_slots (a sender with no slot counts as the slot
 * count), its sequence byte equals remote_seq, and its class is 0 when
 * channel_a is set, 2 when channel_b is set, else neither. The first argument
 * is ignored. */
// FUNCTION: XVT 0x46FCE0
int net_reliable_find_queued_recv_packet(int unused_search_index,
					 int remote_seq, int channel_a,
					 int channel_b, int peer_slot)
{
	unsigned int i;
	int index;
	int sequence_byte;
	int is_class0;
	int is_class2;
	unsigned int slot;
	DPID direct_play_id;
	struct net_reliable_peer_slot *peer;

	(void)unused_search_index;

	index = g_net_recv_queue_read_index;
	for (i = g_net_recv_queue_count; i != 0; --i) {
		if (g_net_session_recv_queue[index].is_resent_copy != 0) {
			direct_play_id =
				g_net_session_recv_queue[index].direct_play_id;
			sequence_byte =
				g_net_session_recv_queue[index].sequence_byte;
			is_class0 =
				g_net_session_recv_queue[index].packet_class ==
				0;
			is_class2 =
				g_net_session_recv_queue[index].packet_class ==
				2;
			slot = 0;
			if (g_net_session.reliable_peer_slot_count != 0) {
				peer = g_net_session.reliable_peer_slots;
				while (peer->direct_play_id != direct_play_id) {
					++peer;
					++slot;
					if (slot >=
					    g_net_session
						    .reliable_peer_slot_count) {
						break;
					}
				}
			}
			if (slot == (unsigned int)peer_slot) {
				if (channel_a != 0) {
					if (is_class0 &&
					    sequence_byte == remote_seq) {
						return index;
					}
				} else if (channel_b != 0) {
					if (is_class2 &&
					    sequence_byte == remote_seq) {
						return index;
					}
				} else if (!is_class0 && !is_class2 &&
					   sequence_byte == remote_seq) {
					return index;
				}
			}
		}
		if ((unsigned int)++index >= 0x400) {
			index = 0;
		}
	}
	return -1;
}

/* Takes the entry at queueIndex out of g_net_session_recv_queue and lowers
 * g_net_recv_queue_count. At the read index it advances g_net_recv_queue_read_index
 * and returns 1; anywhere else it moves every later entry down one place,
 * steps g_net_recv_queue_write_index back and returns 0. Does not check that an
 * entry is queued at queueIndex. */
// FUNCTION: XVT 0x46FDF0
int net_reliable_remove_queued_packet(unsigned int queue_index)
{
	if (g_net_recv_queue_read_index == (int)queue_index) {
		++g_net_recv_queue_read_index;
		--g_net_recv_queue_count;
		if (g_net_recv_queue_read_index >= 1024) {
			g_net_recv_queue_read_index = 0;
		}
		return 1;
	} else {
		unsigned int src_index;
		unsigned int end_index;

		src_index = queue_index + 1;
		if (src_index >= 1024u) {
			src_index = 0;
		}
		end_index =
			g_net_recv_queue_read_index + g_net_recv_queue_count;
		if (end_index >= 1024u) {
			end_index -= 1024u;
		}

		while (end_index != src_index) {
			memcpy(&g_net_session_recv_queue[queue_index],
			       &g_net_session_recv_queue[src_index],
			       sizeof(g_net_session_recv_queue[queue_index]));
			++queue_index;
			if (queue_index >= 1024u) {
				queue_index = 0;
			}
			++src_index;
			if (src_index >= 1024u) {
				src_index = 0;
			}
		}

		--g_net_recv_queue_count;
		if (g_net_recv_queue_write_index == 0) {
			g_net_recv_queue_write_index = 1023;
		} else {
			--g_net_recv_queue_write_index;
		}
		return 0;
	}
}

/* Returns the index of the flight session's peer slot for a DirectPlay id. With
 * none, it adds one at the end of g_net_session.reliable_peer_slots, raising
 * g_net_session.reliable_peer_slot_count: sequences 127 (so 0 comes next), send
 * sequence 0, a NOP trailer, packet and drop counts 0, and last_activity_ms set
 * to timeGetTime. Returns 40, one past the table, when the table is full.
 * Unlike net_find_or_create_peer_slot it leaves a new slot's last_heard_ms and
 * packet_retry_count as they were. */
// FUNCTION: XVT 0x46FEE0
unsigned int net_reliable_find_or_create_peer_slot(int direct_play_id)
{
	unsigned int slot;
	struct net_reliable_peer_slot *peer;
	unsigned int initialize_slot;
	unsigned int *peer_slot_count;

	slot = 0;
	if (g_net_session.reliable_peer_slot_count != 0) {
		peer = g_net_session.reliable_peer_slots;
		do {
			if (peer->direct_play_id == (DPID)direct_play_id) {
				return slot;
			}
			peer++;
			slot++;
		} while (slot < g_net_session.reliable_peer_slot_count);
	}

	if (g_net_session.reliable_peer_slot_count == slot && slot < 40) {
		g_net_session.reliable_peer_slots[slot].direct_play_id =
			direct_play_id;
		initialize_slot = slot;
		g_net_session.reliable_peer_slots[initialize_slot]
			.last_delivered_seq_default = 127;
		g_net_session.reliable_peer_slots[initialize_slot]
			.last_delivered_seq_channel_a = 127;
		g_net_session.reliable_peer_slots[initialize_slot]
			.last_delivered_seq_channel_b = 127;
		g_net_session.reliable_peer_slots[initialize_slot]
			.recv_seq_default = 127;
		g_net_session.reliable_peer_slots[initialize_slot]
			.recv_seq_channel_a = 127;
		g_net_session.reliable_peer_slots[initialize_slot]
			.recv_seq_channel_b = 127;
		g_net_session.reliable_peer_slots[initialize_slot].send_seq = 0;
		g_net_session.reliable_peer_slots[initialize_slot]
			.last_piggyback_type = NET_PACKET_NOP;
		g_net_session.reliable_peer_slots[initialize_slot]
			.piggyback_length = 1;
		g_net_session.reliable_peer_slots[slot].last_activity_ms =
			timeGetTime();
		g_net_session.reliable_peer_slots[slot].packet_count = 0;
		g_net_session.reliable_peer_slots[slot].packet_drop_count = 0;
		peer_slot_count = &g_net_session.reliable_peer_slot_count;
		++*peer_slot_count;
	}

	return slot;
}

/* Drops every packet queued for the flight session (read index set to the
 * write index, count 0). For every peer slot it then counts the newest
 * sequences received as delivered and sets last_activity_ms to timeGetTime.
 * Only the original build calls this. */
// FUNCTION: XVT 0x46FFC0
void net_reliable_reset_recv_queue_state(void)
{
	unsigned int slot;

	g_net_recv_queue_read_index = g_net_recv_queue_write_index;
	g_net_recv_queue_count = 0;

	for (slot = 0; slot < g_net_session.reliable_peer_slot_count; ++slot) {
		g_net_session.reliable_peer_slots[slot]
			.last_delivered_seq_default =
			g_net_session.reliable_peer_slots[slot]
				.recv_seq_default;
		g_net_session.reliable_peer_slots[slot]
			.last_delivered_seq_channel_a =
			g_net_session.reliable_peer_slots[slot]
				.recv_seq_channel_a;
		g_net_session.reliable_peer_slots[slot]
			.last_delivered_seq_channel_b =
			g_net_session.reliable_peer_slots[slot]
				.recv_seq_channel_b;
		g_net_session.reliable_peer_slots[slot].last_activity_ms =
			timeGetTime();
	}
}

/* Returns packet_drop_count of the flight session's peer slot for a DirectPlay
 * id. With no such slot the modern build returns -1, and the original build
 * reaches the end with no return statement, so the value is undefined. */
// FUNCTION: XVT 0x470020
int net_reliable_get_peer_packet_drop_count_by_dpid(int direct_play_id)
{
	unsigned int slot;
	struct net_reliable_peer_slot *peer;

	slot = 0;
	if (g_net_session.reliable_peer_slot_count != 0) {
		peer = g_net_session.reliable_peer_slots;
		do {
			if ((DPID)direct_play_id == peer->direct_play_id) {
				return g_net_session.reliable_peer_slots[slot]
					.packet_drop_count;
			}
			peer++;
			slot++;
		} while (slot < g_net_session.reliable_peer_slot_count);
	}
#ifdef XVT_MODERN
	return -1;
#endif
}

#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma function(memcpy)
#endif
/* Drops every packet in g_net_session_recv_queue except DirectPlay system
 * messages (sender 0) and packets from g_net_session.host_dplay_id, packing the
 * kept ones from the read index on and setting g_net_recv_queue_count and
 * g_net_recv_queue_write_index to match. For every peer slot but the host's it
 * then counts the newest sequences received as delivered and sets
 * last_activity_ms to timeGetTime. Returns 1. The flight session's receive and
 * send code calls it when the receive queue is full. */
// FUNCTION: XVT 0x470060
int net_reliable_keep_only_host_received_packets(void)
{
	unsigned int dst_index;
	unsigned int src_index;
	unsigned int kept_count;
	unsigned int slot;
	DPID direct_play_id;
	struct net_queued_packet *source_packet;
	struct net_reliable_peer_slot *peer;

	dst_index = (unsigned int)g_net_recv_queue_read_index;
	src_index = (unsigned int)g_net_recv_queue_read_index;
	kept_count = 0;
	while ((int)g_net_recv_queue_count > 0) {
		source_packet = &g_net_session_recv_queue[src_index];
		direct_play_id = source_packet->direct_play_id;
		if (direct_play_id == 0 ||
		    (DPID)g_net_session.host_dplay_id == direct_play_id) {
			memcpy(&g_net_session_recv_queue[dst_index],
			       source_packet, sizeof(*source_packet));
			++dst_index;
			if (dst_index >= 1024) {
				dst_index = 0;
			}
			++kept_count;
		}

		++src_index;
		if (src_index >= 1024) {
			src_index = 0;
		}
		--g_net_recv_queue_count;
	}

	g_net_recv_queue_count = kept_count;
	g_net_recv_queue_write_index = (int)dst_index;
	slot = 0;
	if (g_net_session.reliable_peer_slot_count != 0) {
		peer = g_net_session.reliable_peer_slots;
		do {
			if (peer->direct_play_id !=
			    (DPID)g_net_session.host_dplay_id) {
				peer->last_delivered_seq_default =
					peer->recv_seq_default;
				peer->last_delivered_seq_channel_a =
					peer->recv_seq_channel_a;
				peer->last_delivered_seq_channel_b =
					peer->recv_seq_channel_b;
				peer->last_activity_ms = timeGetTime();
			}
			++peer;
			++slot;
		} while (g_net_session.reliable_peer_slot_count > slot);
	}

	return 1;
}
#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma intrinsic(memcpy)
#endif

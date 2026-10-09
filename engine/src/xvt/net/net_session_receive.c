#include "xvt/net/net_session_receive.h"

#include <string.h>

#include "xvt/frontend/config.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/net/net_session_pump.h"
#include "xvt/net/net_session_send.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"

/* Returns the next game packet from net_session_receive_packet, with its sender
 * and size, or NULL when none is ready. DirectPlay system messages (sender 0)
 * met on the way go to net_session_handle_direct_play_system_message and are not
 * returned. The packet stays valid until the next receive. */
// FUNCTION: XVT 0x46E050
int *net_session_receive_game_packet(int *out_sender_dpid,
				     int *out_payload_size)
{
	for (;;) {
		int *packet = (int *)net_session_receive_packet(
			out_sender_dpid, out_payload_size);
		if (packet == NULL || *out_sender_dpid != 0) {
			return packet;
		}
		net_session_handle_direct_play_system_message(*packet, packet);
	}
}

struct net_session_receive_channels {
	int want_channel_a; /* Entry is on the all-players channel */
	int want_channel_b; /* Entry is on the group channel */
	unsigned int remaining_queue_entries; /* Entries left to scan */
};

/* Part of net_session_receive_packet: copies each reliable peer slot's last
 * delivered sequences on the broadcast, one-player and group channels into
 * last_sequences, in that order. */
static void net_session_load_delivered_sequences(uint8_t last_sequences[40][3])
{
	int peer_slot_index;
	int peer_slots_remaining;
	peer_slots_remaining = g_net_session.reliable_peer_slot_count;
	if ((int)g_net_session.reliable_peer_slot_count > 0) {
		peer_slot_index = 0;
		do {
			last_sequences[peer_slot_index][0] =
				(uint8_t)g_net_session
					.reliable_peer_slots[peer_slot_index]
					.last_delivered_seq_channel_a;
			last_sequences[peer_slot_index][1] =
				(uint8_t)g_net_session
					.reliable_peer_slots[peer_slot_index]
					.last_delivered_seq_default;
			last_sequences[peer_slot_index][2] =
				(uint8_t)g_net_session
					.reliable_peer_slots[peer_slot_index]
					.last_delivered_seq_channel_b;
			++peer_slot_index;
			--peer_slots_remaining;
		} while (peer_slots_remaining != 0);
	}
}

/* Part of net_session_receive_packet for a system message at the front of the
 * queue: copies it into g_net_session.recv_scratch_packet, describes it in
 * *out_sender_dpid and *out_payload_size, takes it off the queue and returns
 * its payload. */
static void *net_session_take_system_message(int queue_index,
					     int *out_sender_dpid,
					     int *out_payload_size)
{
	memcpy(&g_net_session.recv_scratch_packet,
	       &g_net_session_recv_queue[queue_index],
	       sizeof(g_net_session.recv_scratch_packet));
	*out_sender_dpid = g_net_session_recv_queue[queue_index].direct_play_id;
	*out_payload_size = g_net_session_recv_queue[queue_index].payload_size;
	--g_net_recv_queue_count;
	++g_net_recv_queue_read_index;
	if (g_net_recv_queue_read_index >= 1024) {
		g_net_recv_queue_read_index = 0;
	}
	return g_net_session.recv_scratch_packet.payload;
}

/* Part of net_session_receive_packet's scan of the queue: steps queue_index to
 * the next entry, back to 0 at 1,024, and lowers the count of entries left. */
static void
net_session_step_to_next_entry(int *queue_index,
			       struct net_session_receive_channels *channels)
{
	++*queue_index;
	if (*queue_index >= 1024) {
		*queue_index = 0;
	}
	--channels->remaining_queue_entries;
}

/* Part of net_session_receive_packet when an entry's sender was just given a
 * reliable peer slot: logs it and sets the slot's three sequences in
 * last_sequences to 127. */
static void net_session_note_new_peer_slot(int direct_play_id,
					   unsigned int *peer_index,
					   uint8_t last_sequences[40][3])
{
	XVT_LOG_DEBUG("network.peer_slot_added player=%u peer=%u",
		      (unsigned)direct_play_id, *peer_index);
	last_sequences[*peer_index][0] = 127;
	last_sequences[*peer_index][1] = 127;
	last_sequences[*peer_index][2] = 127;
}

/* Part of net_session_receive_packet's scan of the queue: sets
 * expected_sequence to the sequence after the last one delivered on the entry's
 * channel, and next_sequence to the one after the channel's last_sequences
 * entry, each 0 after 127. */
static void net_session_find_expected_sequences(
	struct net_session_receive_channels *channels, unsigned int *peer_index,
	int *expected_sequence, int *next_sequence,
	uint8_t last_sequences[40][3])
{
	if (channels->want_channel_a) {
		*expected_sequence =
			g_net_session.reliable_peer_slots[*peer_index]
				.last_delivered_seq_channel_a +
			1;
		if (*expected_sequence > 127) {
			*expected_sequence = 0;
		}
		*next_sequence =
			(unsigned int)last_sequences[*peer_index][0] + 1;
		if (*next_sequence > 127) {
			*next_sequence = 0;
		}
	} else if (channels->want_channel_b) {
		*expected_sequence =
			g_net_session.reliable_peer_slots[*peer_index]
				.last_delivered_seq_channel_b +
			1;
		if (*expected_sequence > 127) {
			*expected_sequence = 0;
		}
		*next_sequence =
			(unsigned int)last_sequences[*peer_index][2] + 1;
		if (*next_sequence > 127) {
			*next_sequence = 0;
		}
	} else {
		*expected_sequence =
			g_net_session.reliable_peer_slots[*peer_index]
				.last_delivered_seq_default +
			1;
		if (*expected_sequence > 127) {
			*expected_sequence = 0;
		}
		*next_sequence =
			(unsigned int)last_sequences[*peer_index][1] + 1;
		if (*next_sequence > 127) {
			*next_sequence = 0;
		}
	}
}

/* What net_session_read_entry_channel and net_session_drop_stale_entry return
 * when the scan of the queue goes on with the entry; they return 1 when it
 * moves on to the next one. */
enum { NET_SESSION_ENTRY_GOES_ON = -1 };

/* Part of net_session_receive_packet's scan of the queue: reads the entry's
 * sequence and channel, finds or makes its sender's peer slot, counts the entry
 * as examined unless it is a resent copy, and sets its expected and next
 * sequences. Once the peer's examined count passes its limit, it steps to the
 * next entry and returns 1. */
static int net_session_read_entry_channel(
	int *sequence, int *queue_index,
	struct net_session_receive_channels *channels, unsigned int *peer_index,
	int direct_play_id, uint8_t last_sequences[40][3],
	uint8_t inspection_limits[40], uint8_t inspected[40],
	int *expected_sequence, int *next_sequence)
{
	unsigned int old_peer_count;
	*sequence = g_net_session_recv_queue[*queue_index].sequence_byte;
	old_peer_count = g_net_session.reliable_peer_slot_count;
	channels->want_channel_a =
		g_net_session_recv_queue[*queue_index].packet_class == 0;
	channels->want_channel_b =
		g_net_session_recv_queue[*queue_index].packet_class == 2;
	*peer_index = net_reliable_find_or_create_peer_slot(direct_play_id);
	if (old_peer_count != g_net_session.reliable_peer_slot_count &&
	    *peer_index < 40) {
		net_session_note_new_peer_slot(direct_play_id, peer_index,
					       last_sequences);
	}
	if (*peer_index < g_net_session.reliable_peer_slot_count) {
		if (inspection_limits[*peer_index] < inspected[*peer_index]) {
			net_session_step_to_next_entry(queue_index, channels);
			return 1;
		}
		if (g_net_session_recv_queue[*queue_index].is_resent_copy ==
		    0) {
			++inspected[*peer_index];
		}

		net_session_find_expected_sequences(
			channels, peer_index, expected_sequence, next_sequence,
			last_sequences);
	} else {
		*expected_sequence = 0;
		*next_sequence = 0;
	}
	return NET_SESSION_ENTRY_GOES_ON;
}

/* Part of net_session_receive_packet's scan of the queue: when the entry's
 * sender has no reliable slot, or the entry is 1 to 28 sequence numbers behind
 * the expected one, counting around the 128 wrap, logs it, removes it from the
 * queue and returns 1. */
static int
net_session_drop_stale_entry(int sequence, int expected_sequence,
			     unsigned int peer_index, int direct_play_id,
			     struct net_session_receive_channels *channels,
			     int *queue_index)
{
	int delta;
	delta = sequence - expected_sequence;
	if (peer_index >= g_net_session.reliable_peer_slot_count ||
	    (delta >= -28 && (delta < 0 || delta >= 100))) {
		XVT_LOG_DEBUG(
			"network.stale_dropped from=%u peer=%u channel=%d sequence=%d expected=%d",
			(unsigned)direct_play_id, peer_index,
			channels->want_channel_a   ? 0
			: channels->want_channel_b ? 2
						   : 1,
			sequence, expected_sequence);
		if (net_reliable_remove_queued_packet(*queue_index) != 0) {
			++*queue_index;
			if (*queue_index >= 1024) {
				*queue_index = 0;
			}
		}
		--channels->remaining_queue_entries;
		return 1;
	}
	return NET_SESSION_ENTRY_GOES_ON;
}

/* Part of net_session_receive_packet for the entry whose sequence comes next on
 * its channel: marks it delivered in the peer's slot and counts it there unless
 * it is an internet-play REMOTE_INPUT; copies it into
 * g_net_session.recv_scratch_packet, removes it from the queue, logs it and
 * returns its payload. */
static void *
net_session_deliver_in_order(unsigned int peer_index,
			     struct net_session_receive_channels *channels,
			     int sequence, int payload_type, int queue_index,
			     int *out_sender_dpid, int *out_payload_size)
{
	g_net_session.reliable_peer_slots[peer_index].last_activity_ms =
		timeGetTime();
	if (channels->want_channel_a) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_a = sequence;
	} else if (channels->want_channel_b) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_b = sequence;
	} else {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_default = sequence;
	}
	g_net_last_delivered_recv_sequence = sequence;
	if (payload_type != NET_PACKET_REMOTE_INPUT ||
	    g_game_config.internet_play != 1) {
		++g_net_session.reliable_peer_slots[peer_index].packet_count;
	}
	memcpy(&g_net_session.recv_scratch_packet,
	       &g_net_session_recv_queue[queue_index],
	       sizeof(g_net_session.recv_scratch_packet));
	net_reliable_remove_queued_packet(queue_index);
	*out_sender_dpid = g_net_session.recv_scratch_packet.direct_play_id;
	*out_payload_size = g_net_session.recv_scratch_packet.payload_size;
	XVT_LOG_DEBUG(
		"network.packet_delivered path=\"in_order\" from=%u peer=%u channel=%u sequence=%u type=%u bytes=%u delivered=%d queued=%u",
		(unsigned)g_net_session.recv_scratch_packet.direct_play_id,
		peer_index,
		(unsigned)g_net_session.recv_scratch_packet.packet_class,
		(unsigned)g_net_session.recv_scratch_packet.sequence_byte,
		(unsigned)g_net_session.recv_scratch_packet.payload[0],
		(unsigned)g_net_session.recv_scratch_packet.payload_size,
		g_net_session.reliable_peer_slots[peer_index].packet_count,
		g_net_recv_queue_count);
	return g_net_session.recv_scratch_packet.payload;
}

/* Part of net_session_receive_packet for an internet-play REMOTE_INPUT,
 * whatever gap comes before it: marks it delivered in the peer's slot, copies
 * it into g_net_session.recv_scratch_packet, removes it from the queue, logs it
 * and returns its payload. */
static void *net_session_deliver_internet_input(
	unsigned int peer_index, struct net_session_receive_channels *channels,
	int sequence, int queue_index, int *out_sender_dpid,
	int *out_payload_size)
{
	g_net_session.reliable_peer_slots[peer_index].last_activity_ms =
		timeGetTime();
	if (channels->want_channel_a) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_a = sequence;
	} else if (channels->want_channel_b) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_b = sequence;
	} else {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_default = sequence;
	}
	g_net_last_delivered_recv_sequence = sequence;
	memcpy(&g_net_session.recv_scratch_packet,
	       &g_net_session_recv_queue[queue_index],
	       sizeof(g_net_session.recv_scratch_packet));
	net_reliable_remove_queued_packet(queue_index);
	*out_sender_dpid = g_net_session.recv_scratch_packet.direct_play_id;
	*out_payload_size = g_net_session.recv_scratch_packet.payload_size;
	XVT_LOG_DEBUG(
		"network.packet_delivered path=\"internet_input\" from=%u peer=%u channel=%u sequence=%u type=%u bytes=%u delivered=%d queued=%u",
		(unsigned)g_net_session.recv_scratch_packet.direct_play_id,
		peer_index,
		(unsigned)g_net_session.recv_scratch_packet.packet_class,
		(unsigned)g_net_session.recv_scratch_packet.sequence_byte,
		(unsigned)g_net_session.recv_scratch_packet.payload[0],
		(unsigned)g_net_session.recv_scratch_packet.payload_size,
		g_net_session.reliable_peer_slots[peer_index].packet_count,
		g_net_recv_queue_count);
	return g_net_session.recv_scratch_packet.payload;
}

/* Part of net_session_receive_packet for an entry that is not a resent copy:
 * sets remote_sequence to the sequence after the newest one seen on the entry's
 * channel, and puts the entry's there; sets sequence_distance, search_sequence
 * and missing_tick_offset from them, clears sent_retry and, while the peer's
 * examined count is under 127, adds the distance to it. */
static void net_session_start_gap(struct net_session_receive_channels *channels,
				  uint8_t last_sequences[40][3],
				  unsigned int peer_index, int sequence,
				  int *remote_sequence, int *sequence_distance,
				  int *search_sequence,
				  int *missing_tick_offset, int *sent_retry,
				  uint8_t inspected[40])
{
	extern int g_net_update_interval_ticks;
	int selected_channel = channels->want_channel_a
				       ? 0
				       : (channels->want_channel_b ? 2 : 1);
	*remote_sequence =
		(unsigned int)last_sequences[peer_index][selected_channel] + 1;
	last_sequences[peer_index][selected_channel] = (uint8_t)sequence;
	if (*remote_sequence > 127) {
		*remote_sequence = 0;
	}
	*sequence_distance = sequence - *remote_sequence;
	if (*sequence_distance < 0) {
		*sequence_distance += 128;
	}
	*search_sequence = *remote_sequence;
	*missing_tick_offset = *sequence_distance;
	*missing_tick_offset *= g_net_update_interval_ticks;
	*sent_retry = 0;
	if (inspected[peer_index] < 127) {
		inspected[peer_index] =
			(uint8_t)(inspected[peer_index] + *sequence_distance);
	}
}

/* Part of net_session_receive_packet when the queue holds the expected
 * sequence: takes the entry at queued_index, marks it delivered in the peer's
 * slot, counts and logs it; clears the NACK count and time of the entry at
 * queue_index when sequence_distance is 1 or less, and returns the payload. */
static void *
net_session_deliver_gap_filled(unsigned int peer_index, int queued_index,
			       struct net_session_receive_channels *channels,
			       int expected_sequence, int *out_sender_dpid,
			       int *out_payload_size, int *sequence_distance,
			       int queue_index)
{
	g_net_session.reliable_peer_slots[peer_index].last_activity_ms =
		timeGetTime();
	memcpy(&g_net_session.recv_scratch_packet,
	       &g_net_session_recv_queue[queued_index],
	       sizeof(g_net_session.recv_scratch_packet));
	net_reliable_remove_queued_packet(queued_index);
	if (channels->want_channel_a) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_a = expected_sequence;
	} else if (channels->want_channel_b) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_b = expected_sequence;
	} else {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_default = expected_sequence;
	}
	g_net_last_delivered_recv_sequence = expected_sequence;
	++g_net_session.reliable_peer_slots[peer_index].packet_count;
	*out_sender_dpid = g_net_session.recv_scratch_packet.direct_play_id;
	*out_payload_size = g_net_session.recv_scratch_packet.payload_size;
	XVT_LOG_DEBUG(
		"network.packet_delivered path=\"gap_filled\" from=%u peer=%u channel=%u sequence=%u type=%u bytes=%u delivered=%d queued=%u",
		(unsigned)g_net_session.recv_scratch_packet.direct_play_id,
		peer_index,
		(unsigned)g_net_session.recv_scratch_packet.packet_class,
		(unsigned)g_net_session.recv_scratch_packet.sequence_byte,
		(unsigned)g_net_session.recv_scratch_packet.payload[0],
		(unsigned)g_net_session.recv_scratch_packet.payload_size,
		g_net_session.reliable_peer_slots[peer_index].packet_count,
		g_net_recv_queue_count);
	if (*sequence_distance <= 1) {
		g_net_session_recv_queue[queue_index].nack_retry_count = 0;
		g_net_session_recv_queue[queue_index].last_nack_ms = 0;
	}
	return g_net_session.recv_scratch_packet.payload;
}

/* Part of net_session_receive_packet's requests for a missing sequence: builds
 * in retry_packet and sends to the entry's sender a WORLD_NACK for a world
 * message on the broadcast channel, holding the missing message's tick, the
 * queued one's less missing_tick_offset, and remote_sequence; or else a NACK
 * holding remote_sequence and retry_channel. */
static void net_session_send_gap_nack(
	int payload_type, struct net_session_receive_channels *channels,
	struct net_session_scratch_packet *retry_packet, uint8_t *payload,
	int *missing_tick_offset, int *remote_sequence,
	struct net_queued_packet *packet, int retry_channel)
{
	if (payload_type == NET_PACKET_WORLD_MESSAGE &&
	    channels->want_channel_a) {
		retry_packet->packet_type = NET_PACKET_WORLD_NACK;
		memcpy(&retry_packet->payload_dwords[0], payload + 4,
		       sizeof(retry_packet->payload_dwords[0]));
		retry_packet->payload_dwords[0] =
			(retry_packet->payload_dwords[0] & 0x7fffffff) -
			*missing_tick_offset;
		retry_packet->payload_dwords[1] = *remote_sequence;
		net_session_send_compact_game_packet(
			packet->direct_play_id, (unsigned int *)retry_packet,
			12, 1);
	} else {
		int retry_direct_play_id = packet->direct_play_id;
		retry_packet->payload_dwords[0] = *remote_sequence;
		retry_packet->packet_type = NET_PACKET_NACK;
		retry_packet->payload_dwords[1] = retry_channel;
		net_session_send_compact_game_packet(
			retry_direct_play_id, (unsigned int *)retry_packet, 12,
			1);
	}
}

/* Part of net_session_receive_packet's first request for a missing sequence,
 * remote_sequence: sends the NACK or WORLD_NACK, counts it in the peer's retry
 * count, logs it and sets sent_retry. */
static void net_session_send_first_nack(
	struct net_session_receive_channels *channels, int payload_type,
	uint8_t *payload, int *missing_tick_offset, int *remote_sequence,
	struct net_queued_packet *packet, unsigned int peer_index,
	int queue_index, int *sent_retry)
{
	struct net_session_scratch_packet retry_packet;
	int retry_channel = channels->want_channel_a
				    ? 0
				    : (channels->want_channel_b ? 2 : 1);
	net_session_send_gap_nack(payload_type, channels, &retry_packet,
				  payload, missing_tick_offset, remote_sequence,
				  packet, retry_channel);
	++g_net_session.reliable_peer_slots[peer_index].packet_retry_count;
	XVT_LOG_DEBUG(
		"network.nack_sent to=%u peer=%u channel=%d missing=%d kind=\"%s\" tick=%d attempt=%d gaps=%d",
		(unsigned)packet->direct_play_id, peer_index, retry_channel,
		*remote_sequence,
		retry_packet.packet_type == NET_PACKET_WORLD_NACK ? "world"
								  : "packet",
		retry_packet.packet_type == NET_PACKET_WORLD_NACK
			? retry_packet.payload_dwords[0]
			: -1,
		(int)g_net_session_recv_queue[queue_index].nack_retry_count,
		g_net_session.reliable_peer_slots[peer_index]
			.packet_retry_count);
	*sent_retry = 1;
}

/* Part of net_session_receive_packet's giving up of a gap, when its search
 * reaches the entry's own sequence: marks that sequence delivered in the peer's
 * slot and counts it, copies the entry at queue_index, removes the one at
 * unused_search_index, logs it and returns its payload. */
static void *
net_session_deliver_at_gap_end(struct net_session_receive_channels *channels,
			       unsigned int peer_index, int sequence,
			       int queue_index, int unused_search_index,
			       int *out_sender_dpid, int *out_payload_size)
{
	if (channels->want_channel_a) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_a = sequence;
	} else if (channels->want_channel_b) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_b = sequence;
	} else {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_default = sequence;
	}
	g_net_last_delivered_recv_sequence = sequence;
	++g_net_session.reliable_peer_slots[peer_index].packet_count;
	memcpy(&g_net_session.recv_scratch_packet,
	       &g_net_session_recv_queue[queue_index],
	       sizeof(g_net_session.recv_scratch_packet));
	net_reliable_remove_queued_packet((unsigned int)unused_search_index);
	*out_sender_dpid = g_net_session.recv_scratch_packet.direct_play_id;
	*out_payload_size = g_net_session.recv_scratch_packet.payload_size;
	XVT_LOG_DEBUG(
		"network.packet_delivered path=\"gave_up\" from=%u peer=%u channel=%u sequence=%u type=%u bytes=%u delivered=%d queued=%u",
		(unsigned)g_net_session.recv_scratch_packet.direct_play_id,
		peer_index,
		(unsigned)g_net_session.recv_scratch_packet.packet_class,
		(unsigned)g_net_session.recv_scratch_packet.sequence_byte,
		(unsigned)g_net_session.recv_scratch_packet.payload[0],
		(unsigned)g_net_session.recv_scratch_packet.payload_size,
		g_net_session.reliable_peer_slots[peer_index].packet_count,
		g_net_recv_queue_count);
	return g_net_session.recv_scratch_packet.payload;
}

/* Part of net_session_receive_packet's giving up of a gap: steps
 * remote_sequence on until the queue holds that sequence, leaving its entry's
 * index in queued_index, and returns NULL; when it reaches the entry's own
 * sequence first, it delivers the entry and returns its payload. */
static void *net_session_find_after_gap(
	int *queued_index, int unused_search_index, int *remote_sequence,
	struct net_session_receive_channels *channels, unsigned int peer_index,
	int sequence, int queue_index, int *out_sender_dpid,
	int *out_payload_size)
{
	for (;;) {
		*queued_index = net_reliable_find_queued_recv_packet(
			unused_search_index, *remote_sequence,
			channels->want_channel_a, channels->want_channel_b,
			(int)peer_index);
		if (*queued_index >= 0 && *queued_index <= 1024) {
			break;
		}
		if (sequence == *remote_sequence) {
			return net_session_deliver_at_gap_end(
				channels, peer_index, sequence, queue_index,
				unused_search_index, out_sender_dpid,
				out_payload_size);
		}
		++*remote_sequence;
		if (*remote_sequence > 127) {
			*remote_sequence = 0;
		}
	}
	return NULL;
}

/* Part of net_session_receive_packet's giving up of a gap: takes the entry at
 * queued_index, the first found from the gap's start on, marks remote_sequence
 * delivered in the peer's slot, counts and logs it, and returns its payload. */
static void *
net_session_deliver_found_copy(int queued_index,
			       struct net_session_receive_channels *channels,
			       unsigned int peer_index, int *remote_sequence,
			       int *out_sender_dpid, int *out_payload_size)
{
	memcpy(&g_net_session.recv_scratch_packet,
	       &g_net_session_recv_queue[queued_index],
	       sizeof(g_net_session.recv_scratch_packet));
	net_reliable_remove_queued_packet(queued_index);
	if (channels->want_channel_a) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_a = *remote_sequence;
	} else if (channels->want_channel_b) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_b = *remote_sequence;
	} else {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_default = *remote_sequence;
	}
	g_net_last_delivered_recv_sequence = *remote_sequence;
	++g_net_session.reliable_peer_slots[peer_index].packet_count;
	*out_sender_dpid = g_net_session.recv_scratch_packet.direct_play_id;
	*out_payload_size = g_net_session.recv_scratch_packet.payload_size;
	XVT_LOG_DEBUG(
		"network.packet_delivered path=\"gave_up\" from=%u peer=%u channel=%u sequence=%u type=%u bytes=%u delivered=%d queued=%u",
		(unsigned)g_net_session.recv_scratch_packet.direct_play_id,
		peer_index,
		(unsigned)g_net_session.recv_scratch_packet.packet_class,
		(unsigned)g_net_session.recv_scratch_packet.sequence_byte,
		(unsigned)g_net_session.recv_scratch_packet.payload[0],
		(unsigned)g_net_session.recv_scratch_packet.payload_size,
		g_net_session.reliable_peer_slots[peer_index].packet_count,
		g_net_recv_queue_count);
	return g_net_session.recv_scratch_packet.payload;
}

/* Part of net_session_receive_packet once the NACK count of a gap passes
 * retry_limit: logs the giving up, records the peer's activity time and returns
 * the first packet the queue holds from search_sequence on, the entry itself at
 * the latest. */
static void *net_session_give_up_gap(
	int queue_index, unsigned int peer_index,
	struct net_session_receive_channels *channels, int *remote_sequence,
	unsigned int retry_limit, int search_sequence, int unused_search_index,
	int sequence, int *out_sender_dpid, int *out_payload_size)
{
	int queued_index;
	void *delivered;
	XVT_LOG_WARN(
		"network.nack_gave_up retries=%d peer=%u channel=%d missing=%d limit=%u",
		g_net_session_recv_queue[queue_index].nack_retry_count,
		peer_index,
		channels->want_channel_a   ? 0
		: channels->want_channel_b ? 2
					   : 1,
		*remote_sequence, retry_limit);
	g_net_session.reliable_peer_slots[peer_index].last_activity_ms =
		timeGetTime();
	*remote_sequence = search_sequence;
	delivered = net_session_find_after_gap(
		&queued_index, unused_search_index, remote_sequence, channels,
		peer_index, sequence, queue_index, out_sender_dpid,
		out_payload_size);
	if (delivered != NULL) {
		return delivered;
	}
	return net_session_deliver_found_copy(
		queued_index, channels, peer_index, remote_sequence,
		out_sender_dpid, out_payload_size);
}

/* Part of net_session_receive_packet's repeat of a request for a missing
 * sequence, remote_sequence, once its wait is over: sends the NACK or
 * WORLD_NACK again, sets sent_retry and logs it. */
static void net_session_send_repeat_nack(
	struct net_session_receive_channels *channels, int payload_type,
	uint8_t *payload, int *missing_tick_offset, int *remote_sequence,
	struct net_queued_packet *packet, unsigned int peer_index,
	int queue_index, int *sent_retry)
{
	struct net_session_scratch_packet retry_packet;
	{
		int retry_channel =
			channels->want_channel_a
				? 0
				: (channels->want_channel_b ? 2 : 1);
		net_session_send_gap_nack(payload_type, channels, &retry_packet,
					  payload, missing_tick_offset,
					  remote_sequence, packet,
					  retry_channel);
		*sent_retry = 1;
		XVT_LOG_DEBUG(
			"network.nack_sent to=%u peer=%u channel=%d missing=%d kind=\"%s\" tick=%d attempt=%d gaps=%d",
			(unsigned)packet->direct_play_id, peer_index,
			retry_channel, *remote_sequence,
			retry_packet.packet_type == NET_PACKET_WORLD_NACK
				? "world"
				: "packet",
			retry_packet.packet_type == NET_PACKET_WORLD_NACK
				? retry_packet.payload_dwords[0]
				: -1,
			(int)g_net_session_recv_queue[queue_index]
				.nack_retry_count,
			g_net_session.reliable_peer_slots[peer_index]
				.packet_retry_count);
	}
}

/* Part of net_session_receive_packet for a gap already NACKed: once the wait
 * since the last NACK passes its timeout, logs it and either gives the gap up,
 * returning what that delivers, or sends the NACK again. Returns NULL when
 * nothing is delivered. */
static void *net_session_retry_gap_nack(
	int payload_type, int queue_index, unsigned int peer_index,
	struct net_session_receive_channels *channels, int *remote_sequence,
	int search_sequence, int unused_search_index, int sequence,
	uint8_t *payload, int *missing_tick_offset,
	struct net_queued_packet *packet, int *sent_retry, int *out_sender_dpid,
	int *out_payload_size)
{
	uint32_t now;
	unsigned int timeout;
	unsigned int retry_limit;
	int timeout_payload_type;
	now = timeGetTime();
	timeout_payload_type = payload_type;
	if (g_net_session.reliable_use_fixed_resend_timeouts != 0) {
		retry_limit = 0;
		timeout = timeout_payload_type == 2 ? 40000 : 3000;
	} else {
		retry_limit = timeout_payload_type == 2 ? 5 : 3;
		timeout = 1000 << g_net_session_recv_queue[queue_index]
					  .nack_retry_count;
	}
	if (now - (uint32_t)g_net_session_recv_queue[queue_index].last_nack_ms >
	    timeout) {
		XVT_LOG_DEBUG(
			"network.nack_timeout retries=%d peer=%u channel=%d missing=%d waited=%u timeout=%u",
			g_net_session_recv_queue[queue_index].nack_retry_count,
			peer_index,
			channels->want_channel_a   ? 0
			: channels->want_channel_b ? 2
						   : 1,
			*remote_sequence,
			(unsigned)(now -
				   (uint32_t)
					   g_net_session_recv_queue[queue_index]
						   .last_nack_ms),
			timeout);
		if (g_net_session_recv_queue[queue_index].nack_retry_count >
		    retry_limit) {
			return net_session_give_up_gap(
				queue_index, peer_index, channels,
				remote_sequence, retry_limit, search_sequence,
				unused_search_index, sequence, out_sender_dpid,
				out_payload_size);
		}
		net_session_send_repeat_nack(
			channels, payload_type, payload, missing_tick_offset,
			remote_sequence, packet, peer_index, queue_index,
			sent_retry);
	}
	return NULL;
}

/* Part of net_session_receive_packet for an entry not next in order: looks in
 * the queue for each missing sequence from remote_sequence on and returns the
 * expected one when found, or else asks for it, waits or gives the gap up;
 * then sets the entry's NACK count and time. Returns NULL when nothing is
 * delivered. */
static void *net_session_chase_gap(
	int sequence, int *remote_sequence, int queue_index,
	struct net_session_receive_channels *channels, unsigned int peer_index,
	int expected_sequence, int *sequence_distance, uint8_t retry_counts[40],
	int payload_type, uint8_t *payload, int *missing_tick_offset,
	struct net_queued_packet *packet, int *sent_retry, int search_sequence,
	int *out_sender_dpid, int *out_payload_size)
{
	int queued_index;
	int unused_search_index;
	void *delivered;
	extern int g_net_update_interval_ticks;
	while (sequence != *remote_sequence) {
		unused_search_index = queue_index;
		queued_index = net_reliable_find_queued_recv_packet(
			unused_search_index, *remote_sequence,
			channels->want_channel_a, channels->want_channel_b,
			(int)peer_index);
		if (queued_index < 1024 && queued_index >= 0) {
			if (expected_sequence == *remote_sequence) {
				return net_session_deliver_gap_filled(
					peer_index, queued_index, channels,
					expected_sequence, out_sender_dpid,
					out_payload_size, sequence_distance,
					queue_index);
			}
			--*sequence_distance;
		} else {
			++retry_counts[peer_index];
			if (g_net_session_recv_queue[queue_index]
				    .nack_retry_count == 0) {
				net_session_send_first_nack(
					channels, payload_type, payload,
					missing_tick_offset, remote_sequence,
					packet, peer_index, queue_index,
					sent_retry);
			} else {
				delivered = net_session_retry_gap_nack(
					payload_type, queue_index, peer_index,
					channels, remote_sequence,
					search_sequence, unused_search_index,
					sequence, payload, missing_tick_offset,
					packet, sent_retry, out_sender_dpid,
					out_payload_size);
				if (delivered != NULL) {
					return delivered;
				}
			}
		}
		*missing_tick_offset -= g_net_update_interval_ticks;
		++*remote_sequence;
		if (*remote_sequence > 127) {
			*remote_sequence = 0;
		}
	}

	if (*sequence_distance <= 0) {
		g_net_session_recv_queue[queue_index].nack_retry_count = 0;
		g_net_session_recv_queue[queue_index].last_nack_ms = 0;
	} else if (*sent_retry == 1) {
		++g_net_session_recv_queue[queue_index].nack_retry_count;
		g_net_session_recv_queue[queue_index].last_nack_ms =
			timeGetTime();
	}
	return NULL;
}

/* Part of net_session_receive_packet's scan of the queue: logs that a resent
 * copy at the queue's front is discarded. */
static void
net_session_note_discarded_copy(int direct_play_id, unsigned int peer_index,
				struct net_session_receive_channels *channels,
				int sequence, int expected_sequence)
{
	XVT_LOG_WARN(
		"network.resent_copy_discarded from=%u peer=%u channel=%d sequence=%d expected=%d",
		(unsigned)direct_play_id, peer_index,
		channels->want_channel_a   ? 0
		: channels->want_channel_b ? 2
					   : 1,
		sequence, expected_sequence);
}

/* Part of net_session_receive_packet: scans the queue from its front and
 * returns the first packet it can deliver, asking for the missing sequences of
 * any gap on the way; returns NULL when it delivers none. */
static void *net_session_scan_queue(int *out_sender_dpid, int *out_payload_size,
				    uint8_t inspected[40],
				    uint8_t retry_counts[40],
				    uint8_t last_sequences[40][3],
				    uint8_t inspection_limits[40])
{
	int sequence;
	int expected_sequence;
	int queue_index;
	struct net_session_receive_channels channels;
	int next_sequence;
	int sequence_distance;
	int payload_type;
	int missing_tick_offset;
	uint8_t *payload;
	int sent_retry;
	int remote_sequence;
	struct net_queued_packet *packet;
	unsigned int peer_index;
	int search_sequence;
	int direct_play_id;
	void *delivered;
	queue_index = g_net_recv_queue_read_index;
	channels.remaining_queue_entries = g_net_recv_queue_count;
	while ((int)channels.remaining_queue_entries > 0) {
		direct_play_id =
			g_net_session_recv_queue[queue_index].direct_play_id;
		packet = &g_net_session_recv_queue[queue_index];
		if (direct_play_id == 0) {
			if (queue_index == g_net_recv_queue_read_index) {
				return net_session_take_system_message(
					queue_index, out_sender_dpid,
					out_payload_size);
			}
			net_session_step_to_next_entry(&queue_index, &channels);
			continue;
		}
		int next = net_session_read_entry_channel(
			&sequence, &queue_index, &channels, &peer_index,
			direct_play_id, last_sequences, inspection_limits,
			inspected, &expected_sequence, &next_sequence);
		if (next != NET_SESSION_ENTRY_GOES_ON) {
			continue;
		}

		payload = g_net_session_recv_queue[queue_index].payload;
		memcpy(&payload_type, payload, sizeof(payload_type));
		next = net_session_drop_stale_entry(sequence, expected_sequence,
						    peer_index, direct_play_id,
						    &channels, &queue_index);
		if (next != NET_SESSION_ENTRY_GOES_ON) {
			continue;
		}

		if (expected_sequence == sequence &&
		    next_sequence == expected_sequence) {
			return net_session_deliver_in_order(
				peer_index, &channels, sequence, payload_type,
				queue_index, out_sender_dpid, out_payload_size);
		}

		if (payload_type == NET_PACKET_REMOTE_INPUT &&
		    g_game_config.internet_play == 1) {
			return net_session_deliver_internet_input(
				peer_index, &channels, sequence, queue_index,
				out_sender_dpid, out_payload_size);
		}

		if (g_net_session_recv_queue[queue_index].is_resent_copy == 0) {
			net_session_start_gap(
				&channels, last_sequences, peer_index, sequence,
				&remote_sequence, &sequence_distance,
				&search_sequence, &missing_tick_offset,
				&sent_retry, inspected);
			if (retry_counts[peer_index] > 25) {
				net_session_step_to_next_entry(&queue_index,
							       &channels);
				continue;
			}

			delivered = net_session_chase_gap(
				sequence, &remote_sequence, queue_index,
				&channels, peer_index, expected_sequence,
				&sequence_distance, retry_counts, payload_type,
				payload, &missing_tick_offset, packet,
				&sent_retry, search_sequence, out_sender_dpid,
				out_payload_size);
			if (delivered != NULL) {
				return delivered;
			}
		} else {
			if (g_net_recv_queue_read_index == queue_index) {
				net_session_note_discarded_copy(
					direct_play_id, peer_index, &channels,
					sequence, expected_sequence);
				if (net_reliable_remove_queued_packet(
					    queue_index) == 0) {
					--channels.remaining_queue_entries;
					continue;
				}
			}
			net_session_step_to_next_entry(&queue_index, &channels);
			continue;
		}
		net_session_step_to_next_entry(&queue_index, &channels);
	}
	return NULL;
}

/* Part of net_session_receive_packet's full-queue pass: reads the entry's
 * sequence and channel, finds or makes its sender's peer slot, and sets
 * expected_sequence to the sequence after the last one delivered on that
 * channel, 0 after 127, or to 0 when the peer has no reliable slot. */
static void
net_session_read_full_queue_entry(unsigned int full_queue_index, int *sequence,
				  struct net_session_receive_channels *channels,
				  unsigned int *peer_index,
				  int *expected_sequence)
{
	*sequence = g_net_session_recv_queue[full_queue_index].sequence_byte;
	channels->want_channel_a =
		g_net_session_recv_queue[full_queue_index].packet_class == 0;
	channels->want_channel_b =
		g_net_session_recv_queue[full_queue_index].packet_class == 2;
	*peer_index = net_reliable_find_or_create_peer_slot(
		g_net_session_recv_queue[full_queue_index].direct_play_id);
	if (*peer_index < g_net_session.reliable_peer_slot_count &&
	    *peer_index < 40) {
		if (channels->want_channel_a) {
			*expected_sequence =
				g_net_session.reliable_peer_slots[*peer_index]
					.last_delivered_seq_channel_a +
				1;
			if (*expected_sequence > 127) {
				*expected_sequence = 0;
			}
		} else if (channels->want_channel_b) {
			*expected_sequence =
				g_net_session.reliable_peer_slots[*peer_index]
					.last_delivered_seq_channel_b +
				1;
			if (*expected_sequence > 127) {
				*expected_sequence = 0;
			}
		} else {
			*expected_sequence =
				g_net_session.reliable_peer_slots[*peer_index]
					.last_delivered_seq_default +
				1;
			if (*expected_sequence > 127) {
				*expected_sequence = 0;
			}
		}
	} else {
		*expected_sequence = 0;
	}
}

/* Part of net_session_receive_packet's full-queue pass for an entry whose gap
 * was already asked for: logs the skip, marks the entry delivered in the peer's
 * slot and counts it there, copies it into g_net_session.recv_scratch_packet,
 * removes it from the queue and returns its payload. */
static void *net_session_deliver_skipping_gap(
	unsigned int full_queue_index, unsigned int peer_index,
	struct net_session_receive_channels *channels, int sequence,
	int expected_sequence, int *out_sender_dpid, int *out_payload_size)
{
	XVT_LOG_WARN(
		"network.queue_full_gap_skipped from=%u peer=%u channel=%d sequence=%d expected=%d retries=%d queued=%u",
		(unsigned)g_net_session_recv_queue[full_queue_index]
			.direct_play_id,
		peer_index,
		channels->want_channel_a   ? 0
		: channels->want_channel_b ? 2
					   : 1,
		sequence, expected_sequence,
		(int)g_net_session_recv_queue[full_queue_index]
			.nack_retry_count,
		g_net_recv_queue_count);
	if (channels->want_channel_a) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_a = sequence;
	} else if (channels->want_channel_b) {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_b = sequence;
	} else {
		g_net_session.reliable_peer_slots[peer_index]
			.last_delivered_seq_default = sequence;
	}
	g_net_last_delivered_recv_sequence = sequence;
	++g_net_session.reliable_peer_slots[peer_index].packet_count;
	memcpy(&g_net_session.recv_scratch_packet,
	       &g_net_session_recv_queue[full_queue_index],
	       sizeof(g_net_session.recv_scratch_packet));
	net_reliable_remove_queued_packet(full_queue_index);
	*out_sender_dpid = g_net_session.recv_scratch_packet.direct_play_id;
	*out_payload_size = g_net_session.recv_scratch_packet.payload_size;
	return g_net_session.recv_scratch_packet.payload;
}

/* Part of net_session_receive_packet when the queue holds 1,023 entries or more
 * and nothing was returned: walks the queue from its front, dropping stale
 * entries and passing those never NACKed, and returns the first entry already
 * NACKed, giving up its gap; with none, logs that the queue is stalled and
 * returns NULL. */
static void *net_session_take_from_full_queue(int *out_sender_dpid,
					      int *out_payload_size)
{
	int sequence;
	int expected_sequence;
	struct net_session_receive_channels channels;
	unsigned int peer_index;
	int delta;
	{
		unsigned int full_queue_index = g_net_recv_queue_read_index;
		int full_remaining = g_net_recv_queue_count;
		while (full_remaining > 0) {
			if (g_net_session_recv_queue[full_queue_index]
				    .direct_play_id != 0) {
				net_session_read_full_queue_entry(
					full_queue_index, &sequence, &channels,
					&peer_index, &expected_sequence);
				delta = sequence - expected_sequence;
				if (peer_index >=
					    g_net_session
						    .reliable_peer_slot_count ||
				    (delta >= -27 &&
				     (delta < 0 || delta >= 100))) {
					XVT_LOG_DEBUG(
						"network.stale_dropped from=%u peer=%u channel=%d sequence=%d expected=%d",
						(unsigned)g_net_session_recv_queue
							[full_queue_index]
								.direct_play_id,
						peer_index,
						channels.want_channel_a	  ? 0
						: channels.want_channel_b ? 2
									  : 1,
						sequence, expected_sequence);
					if (net_reliable_remove_queued_packet(
						    full_queue_index) == 0) {
						--full_remaining;
						continue;
					}
				} else if (g_net_session_recv_queue
						   [full_queue_index]
							   .nack_retry_count ==
					   0) {
					++full_queue_index;
					if ((int)full_queue_index >= 1024) {
						full_queue_index = 0;
					}
					--full_remaining;
					continue;
				} else {
					return net_session_deliver_skipping_gap(
						full_queue_index, peer_index,
						&channels, sequence,
						expected_sequence,
						out_sender_dpid,
						out_payload_size);
				}
			}
			++full_queue_index;
			if ((int)full_queue_index >= 1024) {
				full_queue_index = 0;
			}
			--full_remaining;
		}
	}
	XVT_LOG_DEBUG("network.receive_stalled queued=%u",
		      g_net_recv_queue_count);
	return NULL;
}

/* Returns the next packet to hand to the game, in order per peer and channel,
 * or NULL; *out_sender_dpid and *out_payload_size describe it, and the pointer,
 * into g_net_session.recv_scratch_packet, is valid until the next call. It first
 * runs net_session_pump_incoming_packets and net_session_send_reliable_keepalives. A
 * system message is returned only from the front of the queue. Entries from a
 * peer with no reliable slot (all 40 taken), or 1 to 28 sequence numbers behind
 * the next expected one (counting around the 128 wrap), are dropped. The entry
 * with the next expected sequence on its channel is returned, and an
 * internet-play REMOTE_INPUT is returned as soon as it is met, skipping any
 * gap. On a gap it looks in the queue for the missing sequences and returns the
 * expected one when found; otherwise it sends a NACK per missing sequence, or
 * for a world message a WORLD_NACK holding the missing message's tick estimated
 * from g_net_update_interval_ticks, and repeats them after a wait that doubles
 * each time, from 2,000 ms; once its NACK count passes 3 (5 for world messages)
 * it gives up the gap and returns the first packet it holds past it. Per call,
 * a peer's entries stop being examined once its examined count, which also
 * grows by the size of each gap, passes 90, or its count of missing sequences
 * not found passes 25. When the queue holds 1,023
 * entries or more and nothing was returned, it returns the first entry already
 * NACKed, giving up its gap. Writes the peer slots' delivered sequences,
 * activity times and counts, g_net_last_delivered_recv_sequence,
 * g_net_session.recv_scratch_packet, and the receive queue: g_net_recv_queue_count,
 * g_net_recv_queue_read_index and its entries, also through
 * net_reliable_remove_queued_packet. */
// FUNCTION: XVT 0x46E780
void *net_session_receive_packet(int *out_sender_dpid, int *out_payload_size)
{
	uint8_t inspected[40];
	uint8_t retry_counts[40];
	uint8_t last_sequences[40][3];
	uint8_t inspection_limits[40];
	void *delivered;

	net_session_pump_incoming_packets();
	net_session_send_reliable_keepalives();
	if (g_net_recv_queue_count == 0) {
		return NULL;
	}

	memset(retry_counts, 0, sizeof(retry_counts));
	memset(inspected, 0, sizeof(inspected));
	memset(inspection_limits, 90, sizeof(inspection_limits));
	memset(last_sequences, 0, sizeof(last_sequences));
	net_session_load_delivered_sequences(last_sequences);

	delivered = net_session_scan_queue(out_sender_dpid, out_payload_size,
					   inspected, retry_counts,
					   last_sequences, inspection_limits);
	if (delivered != NULL) {
		return delivered;
	}

	if ((int)g_net_recv_queue_count < 1023) {
		return NULL;
	}
	return net_session_take_from_full_queue(out_sender_dpid,
						out_payload_size);
}

/* Tests for xvt/net/net_reliable.c, the flight session's reliable layer: its
 * peer slots, the check on an arriving packet's sequence, the search for a
 * resent copy in the receive queue, taking an entry out of the queue, and
 * trimming the queue to the host's packets. Each check sets the flight
 * session's peer table and receive queue itself and drives the host clock; no
 * DirectPlay session is open and no game data is read. Every check starts
 * with an empty peer table, an empty queue at index 0 and the clock at one
 * second. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/timing/host_clock.h"

enum { SECOND_US = 1000000, HOST_ID = 500 };

static void fresh(void)
{
	memset(&g_net_session, 0, sizeof g_net_session);
	memset(g_net_session_recv_queue, 0, sizeof g_net_session_recv_queue);
	g_net_recv_queue_read_index = 0;
	g_net_recv_queue_write_index = 0;
	g_net_recv_queue_count = 0;
	xvt_time_reset();
	xvt_time_advance_host_clock(SECOND_US);
}

static struct net_reliable_peer_slot *peer(unsigned int slot)
{
	return &g_net_session.reliable_peer_slots[slot];
}

/* Fills queue entry index with a packet from sender on a channel, marked by
 * its sequence byte. */
static void queue_entry(int index, DPID sender, int packet_class, int sequence,
			int resent)
{
	struct net_queued_packet *entry = &g_net_session_recv_queue[index];
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = sender;
	entry->payload_size = 4;
	entry->packet_class = (uint8_t)packet_class;
	entry->sequence_byte = (uint8_t)sequence;
	entry->is_resent_copy = (uint8_t)resent;
}

/* ------------------------------------------------------------------------ */
/* Peer slots. */

/* A new id gets the next slot at the end of the table, raising the count, with
 * every sequence at 127, send sequence 0, a NOP trailer one byte long, packet
 * and drop counts 0 and last_activity_ms the current time. The slot's
 * last_heard_ms and retry count are left as they were. An id already in the
 * table gets its own slot back and adds nothing. */
static void check_new_peer_slot(void)
{
	fresh();
	struct net_reliable_peer_slot *first = peer(0);
	first->last_delivered_seq_default = 4;
	first->recv_seq_channel_b = 4;
	first->send_seq = 9;
	first->last_piggyback_type = NET_PACKET_PING;
	first->piggyback_length = 30;
	first->packet_count = 9;
	first->packet_drop_count = 9;
	first->last_heard_ms = 77;
	first->packet_retry_count = 5;
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(10), 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(first->direct_play_id, 10);
	XVT_ASSERT_INT_EQ(first->last_delivered_seq_default, 127);
	XVT_ASSERT_INT_EQ(first->recv_seq_default, 127);
	XVT_ASSERT_INT_EQ(first->last_delivered_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(first->recv_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(first->last_delivered_seq_channel_b, 127);
	XVT_ASSERT_INT_EQ(first->recv_seq_channel_b, 127);
	XVT_ASSERT_INT_EQ(first->send_seq, 0);
	XVT_ASSERT_INT_EQ(first->last_piggyback_type, NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(first->piggyback_length, 1);
	XVT_ASSERT_INT_EQ(first->last_activity_ms, timeGetTime());
	XVT_ASSERT_INT_EQ(first->packet_count, 0);
	XVT_ASSERT_INT_EQ(first->packet_drop_count, 0);
	XVT_ASSERT_INT_EQ(first->last_heard_ms, 77);
	XVT_ASSERT_INT_EQ(first->packet_retry_count, 5);

	xvt_time_advance_host_clock(SECOND_US);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(20), 1);
	XVT_ASSERT_INT_EQ(peer(1)->last_activity_ms, timeGetTime());
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(10), 0);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(20), 1);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);
}

/* With all 40 slots taken, a new id gets 40, one past the table, and the count
 * stays 40; the ids already in the table still find their slots. */
static void check_full_peer_table(void)
{
	fresh();
	for (int i = 0; i < 40; ++i) {
		XVT_ASSERT_INT_EQ(
			net_reliable_find_or_create_peer_slot(100 + i), i);
	}
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 40);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(999), 40);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 40);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(139), 39);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(100), 0);
}

/* The drop count of the id's slot, or -1 for an id with no slot, the empty
 * table included. */
static void check_drop_count_by_id(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(net_reliable_get_peer_packet_drop_count_by_dpid(10),
			  -1);
	net_reliable_find_or_create_peer_slot(10);
	net_reliable_find_or_create_peer_slot(20);
	peer(0)->packet_drop_count = 3;
	peer(1)->packet_drop_count = 8;
	XVT_ASSERT_INT_EQ(net_reliable_get_peer_packet_drop_count_by_dpid(10),
			  3);
	XVT_ASSERT_INT_EQ(net_reliable_get_peer_packet_drop_count_by_dpid(20),
			  8);
	XVT_ASSERT_INT_EQ(net_reliable_get_peer_packet_drop_count_by_dpid(30),
			  -1);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);
}

/* ------------------------------------------------------------------------ */
/* The sequence check. */

/* A sender with no slot gets one, and the call returns 0 with nothing
 * recorded: every newest sequence stays 127. With the table full, an unknown
 * sender also gets 0 and adds nothing. */
static void check_sequence_unchecked_sender(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 5, 0, 0), 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 127);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_b, 127);

	for (int i = 1; i < 40; ++i) {
		net_reliable_find_or_create_peer_slot(100 + i);
	}
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(999, 5, 0, 0), 0);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(999, 5, 0, 0), 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 40);
}

/* On the one-player channel: a sequence 1 to 63 ahead of the newest, counting
 * modulo 128, is recorded as the newest and returns 0; the newest again, an
 * older one, or one 64 or more ahead returns 1 and records nothing. */
static void check_sequence_window(void)
{
	fresh();
	net_reliable_find_or_create_peer_slot(10);
	/* 0 is one ahead of the starting 127. */
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 0, 0, 0), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 0);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 0, 0, 0), 1);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 63, 0, 0), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 63);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 62, 0, 0), 1);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 127, 0, 0), 1);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 63);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 126, 0, 0), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 126);
	/* Across the wrap: 1 is three ahead of 126; then 65 is 64 ahead. */
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 1, 0, 0), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 1);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 65, 0, 0), 1);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 64, 0, 0), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 64);
	/* 0 is 64 ahead of 64. */
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 0, 0, 0), 1);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 64);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_b, 127);
}

/* The broadcast flag picks the broadcast channel's newest sequence, ahead of
 * the group flag; the group flag alone picks the group channel's. Each
 * channel is checked and recorded on its own. */
static void check_sequence_channels(void)
{
	fresh();
	net_reliable_find_or_create_peer_slot(10);
	peer(0)->recv_seq_default = 10;
	peer(0)->recv_seq_channel_a = 20;
	peer(0)->recv_seq_channel_b = 30;
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 20, 1, 1), 1);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 21, 1, 1), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 21);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_b, 30);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 30, 0, 1), 1);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 31, 0, 1), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_b, 31);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 21);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 10);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 10, 0, 0), 1);
	XVT_ASSERT_INT_EQ(
		net_reliable_check_and_record_recv_sequence(10, 11, 0, 0), 0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 11);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 21);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_b, 31);
}

/* ------------------------------------------------------------------------ */
/* The receive queue. */

/* The search starts at the oldest entry, runs across the end of the ring, and
 * looks at the queued entries only, and among them at resent copies only. A
 * match needs the sender's slot, the sequence byte, and the class: 0 with the
 * broadcast flag, 2 with the group flag, neither 0 nor 2 otherwise. A sender
 * with no slot counts as the slot count. The first argument is ignored. */
static void check_find_resent_copy(void)
{
	fresh();
	net_reliable_find_or_create_peer_slot(10);
	net_reliable_find_or_create_peer_slot(20);
	g_net_recv_queue_read_index = 1022;
	g_net_recv_queue_count = 5;
	g_net_recv_queue_write_index = 3;
	queue_entry(1021, 20, 1, 6, 1); /* Before the oldest entry. */
	queue_entry(1022, 20, 1, 5, 0); /* Not a resent copy. */
	queue_entry(1023, 20, 1, 5, 1);
	queue_entry(0, 10, 0, 7, 1);
	queue_entry(1, 10, 2, 7, 1);
	queue_entry(2, 99, 1, 9, 1); /* A sender with no slot. */
	queue_entry(3, 20, 1, 8, 1); /* Past the queued entries. */
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 5, 0, 0, 1),
			  1023);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(77, 5, 0, 0, 1),
			  1023);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 7, 1, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 7, 0, 1, 0),
			  1);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 7, 1, 1, 0),
			  0);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 7, 0, 0, 0),
			  -1);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 9, 0, 0, 2),
			  2);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 5, 0, 0, 0),
			  -1);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 6, 0, 0, 1),
			  -1);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 8, 0, 0, 1),
			  -1);
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 5, 1, 0, 1),
			  -1);

	/* Taking the resent copy out leaves only the one that is not. */
	g_net_session_recv_queue[1023].is_resent_copy = 0;
	XVT_ASSERT_INT_EQ(net_reliable_find_queued_recv_packet(0, 5, 0, 0, 1),
			  -1);
}

/* At the read index, removal advances the read index, across the end of the
 * ring, lowers the count and returns 1; the write index stays. */
static void check_remove_oldest(void)
{
	fresh();
	g_net_recv_queue_read_index = 1023;
	g_net_recv_queue_count = 3;
	g_net_recv_queue_write_index = 2;
	XVT_ASSERT_INT_EQ(net_reliable_remove_queued_packet(1023), 1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_read_index, 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 2);
	XVT_ASSERT_INT_EQ(net_reliable_remove_queued_packet(0), 1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_read_index, 1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
}

/* Anywhere else, every later entry moves down one place, across the end of
 * the ring too, the count drops, the write index steps back, from 0 to 1023,
 * and the call returns 0. */
static void check_remove_inside(void)
{
	fresh();
	g_net_recv_queue_count = 3;
	g_net_recv_queue_write_index = 3;
	queue_entry(0, 10, 1, 1, 0);
	queue_entry(1, 10, 1, 2, 0);
	queue_entry(2, 10, 1, 3, 0);
	XVT_ASSERT_INT_EQ(net_reliable_remove_queued_packet(1), 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 2);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[0].sequence_byte, 1);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1].sequence_byte, 3);

	fresh();
	g_net_recv_queue_read_index = 1021;
	g_net_recv_queue_count = 5;
	g_net_recv_queue_write_index = 2;
	queue_entry(1021, 10, 1, 1, 0);
	queue_entry(1022, 10, 1, 2, 0);
	queue_entry(1023, 10, 1, 3, 0);
	queue_entry(0, 10, 1, 4, 0);
	queue_entry(1, 10, 1, 5, 0);
	XVT_ASSERT_INT_EQ(net_reliable_remove_queued_packet(1022), 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_read_index, 1021);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 4);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 1);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1021].sequence_byte, 1);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1022].sequence_byte, 3);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1023].sequence_byte, 4);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[0].sequence_byte, 5);

	XVT_ASSERT_INT_EQ(net_reliable_remove_queued_packet(0), 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 3);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 0);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1023].sequence_byte, 4);

	XVT_ASSERT_INT_EQ(net_reliable_remove_queued_packet(1023), 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 1023);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1021].sequence_byte, 1);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1022].sequence_byte, 3);
}

/* Only system messages (sender 0) and the host's packets stay, packed in their
 * order from the read index on; the count and the write index follow. Every
 * peer but the host then counts its newest sequences as delivered and is
 * stamped with the current time. Returns 1. */
static void check_keep_host_packets(void)
{
	fresh();
	g_net_session.host_dplay_id = HOST_ID;
	net_reliable_find_or_create_peer_slot(HOST_ID);
	net_reliable_find_or_create_peer_slot(10);
	peer(0)->recv_seq_default = 4;
	peer(1)->recv_seq_default = 5;
	peer(1)->recv_seq_channel_a = 6;
	peer(1)->recv_seq_channel_b = 7;
	uint32_t added_ms = timeGetTime();
	g_net_recv_queue_read_index = 1022;
	g_net_recv_queue_count = 5;
	g_net_recv_queue_write_index = 3;
	queue_entry(1022, 10, 1, 1, 0);
	queue_entry(1023, 0, 0, 2, 0);
	queue_entry(0, HOST_ID, 1, 3, 0);
	queue_entry(1, 10, 1, 4, 0);
	queue_entry(2, HOST_ID, 0, 5, 0);
	xvt_time_advance_host_clock(SECOND_US);
	XVT_ASSERT_INT_EQ(net_reliable_keep_only_host_received_packets(), 1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 3);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_read_index, 1022);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 1);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1022].direct_play_id, 0);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1022].sequence_byte, 2);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1023].direct_play_id,
			  HOST_ID);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1023].sequence_byte, 3);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[0].direct_play_id, HOST_ID);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[0].sequence_byte, 5);

	XVT_ASSERT_INT_EQ(peer(1)->last_delivered_seq_default, 5);
	XVT_ASSERT_INT_EQ(peer(1)->last_delivered_seq_channel_a, 6);
	XVT_ASSERT_INT_EQ(peer(1)->last_delivered_seq_channel_b, 7);
	XVT_ASSERT_INT_EQ(peer(1)->last_activity_ms, timeGetTime());
	XVT_ASSERT_INT_EQ(peer(0)->last_delivered_seq_default, 127);
	XVT_ASSERT_INT_EQ(peer(0)->last_activity_ms, added_ms);

	/* An empty queue stays empty, with the write index at the read index. */
	g_net_recv_queue_count = 0;
	XVT_ASSERT_INT_EQ(net_reliable_keep_only_host_received_packets(), 1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 1022);
}

int main(void)
{
	check_new_peer_slot();
	check_full_peer_table();
	check_drop_count_by_id();
	check_sequence_unchecked_sender();
	check_sequence_window();
	check_sequence_channels();
	check_find_resent_copy();
	check_remove_oldest();
	check_remove_inside();
	check_keep_host_packets();
	return 0;
}

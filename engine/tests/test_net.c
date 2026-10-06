/* Tests for xvt/net/net.c, the lobby's side of DirectPlay: its peer slots, the
 * check on an arriving packet's sequence, its receive queue, the sends on the
 * broadcast, group and one-player channels, the receive pump, the keepalives,
 * the roster's ready flags and the link figures kept for each player. Each
 * check sets the lobby state it needs in g_front_state and drives the host
 * clock. Where a check needs a DirectPlay session, it gives the lobby an
 * interface this file owns: Send records what the lobby sends, Receive hands
 * the pump the messages the check put in its inbox, and SetPlayerName records
 * the names and returns the result the check chose. The rename and ready
 * checks lock a back buffer on a frontend display with no window. No game
 * data is read. Every check starts from a cleared g_front_state with the
 * local player, id 1000, alone in the roster, the group id 2000, no link
 * figures, no session and the clock at one second.
 *
 * Not checked here: the lobby's opening and closing of DirectPlay, the player
 * roster's refresh, the handling of DirectPlay's system messages, the hand-over
 * of state to and from the flight session, the NACK, KEEPALIVE and
 * KEEPALIVE_ACK handling of the pump, the resends, the silent-peer drop and
 * the dequeue; they are left for later rounds.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/timing/host_clock.h"

enum {
	SECOND_US = 1000000,
	MS_US = 1000,
	LOCAL_ID = 1000,
	GROUP_ID = 2000,
	ONE_PLAYER_BIT = 0x8000,
	GROUP_BITS = 0x8080,
	FAKE_MESSAGES = 8,
};

/* ------------------------------------------------------------------------ */
/* The DirectPlay interface the checks give the lobby. */

struct fake_message {
	DPID from;
	DPID to;
	uint32_t size;
	uint8_t bytes[1024];
};

static struct fake_message g_sent[FAKE_MESSAGES];
static int g_sent_count;
static HRESULT g_send_result;
static struct fake_message g_inbox[FAKE_MESSAGES];
static int g_inbox_count;
static int g_inbox_next;
static HRESULT g_rename_result;
static int g_rename_calls;
static DPID g_renamed_player;
static char g_renamed_short[16];
static char g_renamed_long[16];

static HRESULT AERON_DXAPI fake_send(IDirectPlay2A *self, DPID from, DPID to,
				     uint32_t flags, void *data, uint32_t size)
{
	(void)self;
	(void)flags;
	XVT_ASSERT_TRUE(g_sent_count < FAKE_MESSAGES);
	XVT_ASSERT_TRUE(size <= sizeof g_sent[0].bytes);
	struct fake_message *message = &g_sent[g_sent_count++];
	message->from = from;
	message->to = to;
	message->size = size;
	memcpy(message->bytes, data, size);
	return g_send_result;
}

static HRESULT AERON_DXAPI fake_receive(IDirectPlay2A *self, DPID *from,
					DPID *to, uint32_t flags, void *data,
					uint32_t *size)
{
	(void)self;
	(void)flags;
	if (g_inbox_next >= g_inbox_count) {
		return DPERR_NOMESSAGES;
	}
	const struct fake_message *message = &g_inbox[g_inbox_next++];
	XVT_ASSERT_TRUE(message->size <= *size);
	*from = message->from;
	*to = message->to;
	*size = message->size;
	memcpy(data, message->bytes, message->size);
	return 0;
}

static HRESULT AERON_DXAPI fake_set_player_name(IDirectPlay2A *self,
						DPID player, DPNAME *name,
						uint32_t flags)
{
	(void)self;
	(void)flags;
	++g_rename_calls;
	g_renamed_player = player;
	XVT_ASSERT_INT_EQ(name->dwSize, sizeof *name);
	strncpy(g_renamed_short, name->lpszShortNameA,
		sizeof g_renamed_short - 1);
	strncpy(g_renamed_long, name->lpszLongNameA, sizeof g_renamed_long - 1);
	return g_rename_result;
}

static IDirectPlay2AVtbl g_fake_vtbl = {
	.Send = fake_send,
	.Receive = fake_receive,
	.SetPlayerName = fake_set_player_name,
};
static IDirectPlay2A g_fake_direct_play = {&g_fake_vtbl};

/* Gives the lobby the interface above. */
static void open_session(void)
{
	g_front_state.net_direct_play = &g_fake_direct_play;
}

/* Puts a game packet in the inbox as DirectPlay delivers it: the header word,
 * the body's length, the body, then a one-byte NOP trailer. */
static void inbox_packet(DPID from, DPID to, uint16_t header, const void *body,
			 uint16_t body_size)
{
	XVT_ASSERT_TRUE(g_inbox_count < FAKE_MESSAGES);
	struct fake_message *message = &g_inbox[g_inbox_count++];
	message->from = from;
	message->to = to;
	memcpy(message->bytes, &header, sizeof header);
	memcpy(message->bytes + 2, &body_size, sizeof body_size);
	if (body_size != 0) {
		memcpy(message->bytes + 4, body, body_size);
	}
	message->bytes[4 + body_size] = NET_PACKET_NOP;
	message->size = 5u + body_size;
}

static int sent_word(int message, int offset)
{
	int word;
	memcpy(&word, g_sent[message].bytes + offset, sizeof word);
	return word;
}

static int sent_header(int message)
{
	uint16_t header;
	memcpy(&header, g_sent[message].bytes, sizeof header);
	return header;
}

static int sent_length(int message)
{
	uint16_t length;
	memcpy(&length, g_sent[message].bytes + 2, sizeof length);
	return length;
}

/* ------------------------------------------------------------------------ */
/* The lobby. */

static void fresh(void)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(g_net_player_connection_stats, 0,
	       sizeof g_net_player_connection_stats);
	g_front_state.net_runtime_local_player.player_id = LOCAL_ID;
	g_front_state.net_players[0].player_id = LOCAL_ID;
	g_front_state.net_player_count = 1;
	g_front_state.net_group_dplay_id = GROUP_ID;
	g_draw_surface_ptr = NULL;
	memset(g_sent, 0, sizeof g_sent);
	g_sent_count = 0;
	g_send_result = 0;
	memset(g_inbox, 0, sizeof g_inbox);
	g_inbox_count = 0;
	g_inbox_next = 0;
	g_rename_result = 0;
	g_rename_calls = 0;
	g_renamed_player = 0;
	memset(g_renamed_short, 0, sizeof g_renamed_short);
	memset(g_renamed_long, 0, sizeof g_renamed_long);
	xvt_time_reset();
	xvt_time_advance_host_clock(SECOND_US);
}

static struct net_reliable_peer_slot *peer(unsigned int slot)
{
	return &g_front_state.net_runtime_reliable_peer_slots[slot];
}

static struct net_queued_packet *queued(int index)
{
	return &g_front_state.net_runtime_recv_queue[index];
}

static int queued_type(int index)
{
	int type;
	memcpy(&type, queued(index)->payload, sizeof type);
	return type;
}

/* Gives the 40 peer slots to players 100 to 139. */
static void fill_peer_table(void)
{
	for (int i = 0; i < 40; ++i) {
		XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(100 + i), i);
	}
}

/* Bytes of the spare field after the peer table that are not 0. */
static int spare_bytes_set(void)
{
	int count = 0;
	for (size_t i = 0; i < sizeof g_front_state.unused_net_state_c2b51;
	     ++i) {
		if (g_front_state.unused_net_state_c2b51[i] != 0) {
			++count;
		}
	}
	return count;
}

static void stop_on_alarm(int signal_number)
{
	static const char message[] =
		"check failed: the call did not return within the time allowed\n";
	(void)signal_number;
	if (write(2, message, sizeof message - 1) < 0) {
		_exit(1);
	}
	_exit(1);
}

/* For a check whose call may never return: the program fails after the given
 * seconds. */
static void fail_after_seconds(unsigned int seconds)
{
	signal(SIGALRM, stop_on_alarm);
	alarm(seconds);
}

/* ------------------------------------------------------------------------ */
/* Peer slots and the sequence check. */

/* A new id gets the next slot at the end of the table, raising the count, with
 * every sequence at 127, send sequence 0, a NOP trailer one byte long, all
 * counts 0, and both times the current time. An id already in the table gets
 * its own slot back and adds nothing. */
static void check_new_peer_slot(void)
{
	fresh();
	struct net_reliable_peer_slot *first = peer(0);
	first->recv_seq_channel_b = 4;
	first->last_delivered_seq_channel_a = 4;
	first->send_seq = 9;
	first->last_piggyback_type = NET_PACKET_PING;
	first->piggyback_length = 30;
	first->packet_count = 9;
	first->packet_drop_count = 9;
	first->packet_retry_count = 9;
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(30), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(first->direct_play_id, 30);
	XVT_ASSERT_INT_EQ(first->last_delivered_seq_default, 127);
	XVT_ASSERT_INT_EQ(first->recv_seq_default, 127);
	XVT_ASSERT_INT_EQ(first->last_delivered_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(first->recv_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(first->last_delivered_seq_channel_b, 127);
	XVT_ASSERT_INT_EQ(first->recv_seq_channel_b, 127);
	XVT_ASSERT_INT_EQ(first->send_seq, 0);
	XVT_ASSERT_INT_EQ(first->last_piggyback_type, NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(first->piggyback_length, 1);
	XVT_ASSERT_INT_EQ(first->packet_count, 0);
	XVT_ASSERT_INT_EQ(first->packet_drop_count, 0);
	XVT_ASSERT_INT_EQ(first->packet_retry_count, 0);
	XVT_ASSERT_INT_EQ(first->last_activity_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(first->last_heard_ms, GetTickCount());

	xvt_time_advance_host_clock(SECOND_US);
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(40), 1);
	XVT_ASSERT_INT_EQ(peer(1)->last_heard_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(30), 0);
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(40), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 2);
}

/* With all 40 slots taken, a new id gets 40, one past the table, and the count
 * stays 40; the ids in the table still find their slots. */
static void check_full_peer_table(void)
{
	fresh();
	fill_peer_table();
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 40);
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(999), 40);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 40);
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(139), 39);
}

/* A sender with no slot gets one and the call returns 0 with nothing recorded;
 * with the table full, an unknown sender gets 0 and adds nothing. Otherwise a
 * sequence 1 to 63 ahead of the newest, counting modulo 128, is recorded and
 * returns 0, and the newest again, an older one or one 64 or more ahead
 * returns 1. The broadcast flag picks the broadcast channel, ahead of the
 * group flag; the group flag alone picks the group channel. */
static void check_incoming_sequence(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 5, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 127);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 0, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 0);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 0, 0, 0),
			  1);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 63, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 62, 0, 0),
			  1);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 127, 0, 0),
			  1);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 63);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 126, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 1, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 64, 0, 0),
			  0);
	/* 0 is 64 ahead of 64. */
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 0, 0, 0),
			  1);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 64);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_b, 127);

	peer(0)->recv_seq_channel_a = 20;
	peer(0)->recv_seq_channel_b = 30;
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 20, 1, 1),
			  1);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 21, 1, 1),
			  0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 21);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 30, 0, 1),
			  1);
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(30, 31, 0, 1),
			  0);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_b, 31);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_channel_a, 21);
	XVT_ASSERT_INT_EQ(peer(0)->recv_seq_default, 64);

	fresh();
	fill_peer_table();
	XVT_ASSERT_INT_EQ(net_check_and_record_incoming_sequence(999, 5, 0, 0),
			  0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 40);
}

/* Frees every slot whose id is neither in the roster nor the group's, moves
 * later slots down into the gaps with their state, and recounts the slots in
 * use over all 40. Returns 1. */
static void check_compact_peer_slots(void)
{
	fresh();
	g_front_state.net_players[1].player_id = 30;
	g_front_state.net_player_count = 2;
	net_find_or_create_peer_slot(LOCAL_ID);
	net_find_or_create_peer_slot(99);
	net_find_or_create_peer_slot(GROUP_ID);
	net_find_or_create_peer_slot(30);
	net_find_or_create_peer_slot(98);
	peer(3)->send_seq = 17;
	/* A slot in use past the count is counted too. */
	peer(10)->direct_play_id = 77;
	XVT_ASSERT_INT_EQ(net_compact_reliable_peer_slots_for_roster(), 1);
	XVT_ASSERT_INT_EQ(peer(0)->direct_play_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(peer(1)->direct_play_id, GROUP_ID);
	XVT_ASSERT_INT_EQ(peer(2)->direct_play_id, 30);
	XVT_ASSERT_INT_EQ(peer(2)->send_seq, 17);
	XVT_ASSERT_INT_EQ(peer(3)->direct_play_id, 0);
	XVT_ASSERT_INT_EQ(peer(4)->direct_play_id, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 4);
}

/* ------------------------------------------------------------------------ */
/* The receive queue. */

static void queue_entry(int index, DPID sender, int packet_class, int sequence,
			int resent)
{
	struct net_queued_packet *entry = queued(index);
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = sender;
	entry->payload_size = 4;
	entry->packet_class = (uint8_t)packet_class;
	entry->sequence_byte = (uint8_t)sequence;
	entry->is_resent_copy = (uint8_t)resent;
}

/* The search starts at the oldest entry, runs across the end of the ring, and
 * looks at queued resent copies only. A match needs the sender's slot, the
 * sequence, and the class: 0 with the broadcast flag, 2 with the group flag,
 * neither 0 nor 2 otherwise. A sender with no slot counts as the slot count.
 * The first argument is ignored. */
static void check_find_resent_copy(void)
{
	fresh();
	net_find_or_create_peer_slot(30);
	net_find_or_create_peer_slot(40);
	g_front_state.net_runtime_recv_queue_read_index = 1022;
	g_front_state.net_runtime_recv_queue_count = 5;
	queue_entry(1021, 40, 1, 6, 1); /* Before the oldest entry. */
	queue_entry(1022, 40, 1, 5, 0); /* Not a resent copy. */
	queue_entry(1023, 40, 1, 5, 1);
	queue_entry(0, 30, 0, 7, 1);
	queue_entry(1, 30, 2, 7, 1);
	queue_entry(2, 99, 1, 9, 1); /* A sender with no slot. */
	queue_entry(3, 40, 1, 8, 1); /* Past the queued entries. */
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 5, 0, 0, 1),
			  1023);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(77, 5, 0, 0, 1),
			  1023);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 7, 1, 0, 0), 0);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 7, 0, 1, 0), 1);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 7, 1, 1, 0), 0);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 7, 0, 0, 0), -1);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 9, 0, 0, 2), 2);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 5, 0, 0, 0), -1);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 6, 0, 0, 1), -1);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 8, 0, 0, 1), -1);
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 5, 1, 0, 1), -1);
	queued(1023)->is_resent_copy = 0;
	XVT_ASSERT_INT_EQ(net_find_queued_sequenced_packet(0, 5, 0, 0, 1), -1);
}

/* At the read index, removal advances the read index, across the end of the
 * ring, lowers the count and returns 1. Anywhere else every later entry moves
 * down one place, the count drops, the write index steps back, from 0 to
 * 1023, and the call returns 0. */
static void check_remove_queued(void)
{
	fresh();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_count = 2;
	g_front_state.net_runtime_recv_queue_write_index = 1;
	XVT_ASSERT_INT_EQ(net_remove_incoming_packet_at_index(1023), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_read_index, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 1);
	XVT_ASSERT_INT_EQ(net_remove_incoming_packet_at_index(0), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_read_index, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);

	fresh();
	g_front_state.net_runtime_recv_queue_read_index = 1021;
	g_front_state.net_runtime_recv_queue_count = 5;
	g_front_state.net_runtime_recv_queue_write_index = 2;
	queue_entry(1021, 30, 1, 1, 0);
	queue_entry(1022, 30, 1, 2, 0);
	queue_entry(1023, 30, 1, 3, 0);
	queue_entry(0, 30, 1, 4, 0);
	queue_entry(1, 30, 1, 5, 0);
	XVT_ASSERT_INT_EQ(net_remove_incoming_packet_at_index(1022), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_read_index,
			  1021);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 4);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 1);
	XVT_ASSERT_INT_EQ(queued(1021)->sequence_byte, 1);
	XVT_ASSERT_INT_EQ(queued(1022)->sequence_byte, 3);
	XVT_ASSERT_INT_EQ(queued(1023)->sequence_byte, 4);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 5);
	XVT_ASSERT_INT_EQ(net_remove_incoming_packet_at_index(0), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 0);
	XVT_ASSERT_INT_EQ(net_remove_incoming_packet_at_index(1023), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index,
			  1023);
}

/* ------------------------------------------------------------------------ */
/* Sends. */

/* Without DirectPlay a send to everyone returns 1, takes the broadcast
 * counter's sequence and moves the counter on, from 127 back to 0. The packet
 * goes into the sent history, addressed to everyone on class 0 with its
 * sequence, and is queued locally as received from the local player on class
 * 0, which sets the local player's newest broadcast sequence. */
static void check_broadcast_send(void)
{
	fresh();
	int packet[2] = {NET_PACKET_CHAT, 0x11223344};
	g_front_state.net_runtime_broadcast_seq_counter = 127;
	g_front_state.net_runtime_sent_history_write_index = 127;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(0, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_seq_counter, 0);

	const struct net_queued_packet *sent =
		&g_front_state.net_runtime_sent_history[127];
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_sent_history_write_index,
			  0);
	XVT_ASSERT_INT_EQ(sent->direct_play_id, 0);
	XVT_ASSERT_INT_EQ(sent->payload_size, sizeof packet);
	XVT_ASSERT_INT_EQ(memcmp(sent->payload, packet, sizeof packet), 0);
	XVT_ASSERT_INT_EQ(sent->packet_class, 0);
	XVT_ASSERT_INT_EQ(sent->sequence_byte, 127);

	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 0);
	XVT_ASSERT_INT_EQ(queued(1023)->direct_play_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(queued(1023)->payload_size, sizeof packet);
	XVT_ASSERT_INT_EQ(memcmp(queued(1023)->payload, packet, sizeof packet),
			  0);
	XVT_ASSERT_INT_EQ(queued(1023)->packet_class, 0);
	XVT_ASSERT_INT_EQ(queued(1023)->sequence_byte, 127);
	XVT_ASSERT_INT_EQ(queued(1023)->is_resent_copy, 0);
	unsigned int local = net_find_or_create_peer_slot(LOCAL_ID);
	XVT_ASSERT_INT_EQ(peer(local)->recv_seq_channel_a, 127);

	XVT_ASSERT_INT_EQ(net_send_packet_internal(0, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_seq_counter, 1);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 0);
	XVT_ASSERT_INT_EQ(peer(local)->recv_seq_channel_a, 0);
	g_front_state.net_runtime_broadcast_seq_counter = 126;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(0, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_seq_counter, 127);
}

/* A send to the group takes the group counter's sequence, is kept in the
 * history on class 2 and is queued locally on class 2. A send to one player
 * takes the sequence in that player's slot, adding the slot, and is kept on
 * class 1; without DirectPlay it too is queued locally. With the queue full
 * nothing is queued, and the send still returns 1. */
static void check_group_and_one_player_send(void)
{
	fresh();
	int packet[2] = {NET_PACKET_CHAT, 5};
	g_front_state.net_runtime_group_seq_counter = 6;
	g_front_state.net_runtime_broadcast_seq_counter = 50;
	XVT_ASSERT_INT_EQ(
		net_send_packet_internal(GROUP_ID, packet, sizeof packet), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_group_seq_counter, 7);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_seq_counter, 50);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_sent_history[0].packet_class, 2);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_sent_history[0].direct_play_id,
		GROUP_ID);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_sent_history[0].sequence_byte, 6);
	XVT_ASSERT_INT_EQ(queued(0)->packet_class, 2);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 6);
	unsigned int local = net_find_or_create_peer_slot(LOCAL_ID);
	XVT_ASSERT_INT_EQ(peer(local)->recv_seq_channel_b, 6);

	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, packet, sizeof packet),
			  1);
	unsigned int slot = net_find_or_create_peer_slot(30);
	XVT_ASSERT_INT_EQ(peer(slot)->send_seq, 1);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_sent_history[1].packet_class, 1);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_sent_history[1].direct_play_id, 30);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_sent_history[1].sequence_byte, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued(1)->packet_class, 1);
	XVT_ASSERT_INT_EQ(queued(1)->direct_play_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(peer(local)->recv_seq_default, 0);

	peer(slot)->send_seq = 127;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(peer(slot)->send_seq, 0);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_sent_history[2].sequence_byte, 127);
	peer(slot)->send_seq = 126;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(peer(slot)->send_seq, 127);

	g_front_state.net_runtime_recv_queue_count = 1024;
	g_front_state.net_runtime_recv_queue_write_index = 9;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(0, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1024);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 9);

	/* The group counter runs to 127, then back to 0. */
	g_front_state.net_runtime_group_seq_counter = 126;
	XVT_ASSERT_INT_EQ(
		net_send_packet_internal(GROUP_ID, packet, sizeof packet), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_group_seq_counter, 127);
	XVT_ASSERT_INT_EQ(
		net_send_packet_internal(GROUP_ID, packet, sizeof packet), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_group_seq_counter, 0);
}

/* With DirectPlay, a send to one player goes out from the local player with
 * the one-player bit and the slot's sequence in the header, a length word, the
 * body, and as trailer the slot's last packet: a NOP for a new slot, then the
 * type byte and body of the packet sent before. Nothing is queued locally.
 * The send's result decides the return. A send to the local player is not
 * sent, is queued locally and returns 1. */
static void check_one_player_trailer(void)
{
	fresh();
	open_session();
	int first[2] = {NET_PACKET_CHAT, 0x11223344};
	int second[2] = {NET_PACKET_CHAT, 0x55667788};
	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, first, sizeof first), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(g_sent[0].from, LOCAL_ID);
	XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(0), ONE_PLAYER_BIT | NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(sent_length(0), 4);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), first[1]);
	XVT_ASSERT_INT_EQ(g_sent[0].size, 9);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[8], NET_PACKET_NOP);

	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, second, sizeof second),
			  1);
	XVT_ASSERT_INT_EQ(sent_header(1),
			  ONE_PLAYER_BIT | (1 << 8) | NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(sent_word(1, 4), second[1]);
	XVT_ASSERT_INT_EQ(g_sent[1].size, 13);
	XVT_ASSERT_INT_EQ(g_sent[1].bytes[8], NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(sent_word(1, 9), first[1]);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_sent_history_write_index,
			  2);

	g_send_result = DPERR_INVALIDPLAYER;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, first, sizeof first), 0);

	XVT_ASSERT_INT_EQ(
		net_send_packet_internal(LOCAL_ID, first, sizeof first), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 3);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_sent_history_write_index,
			  3);
}

/* Without DirectPlay, send-and-flush returns 1 and does nothing. With it, the
 * packet goes out and then a NOP to the same player whose trailer is that
 * packet; the call returns the first send's result. */
static void check_send_and_flush(void)
{
	fresh();
	int packet[2] = {NET_PACKET_CHAT, 0x0A0B0C0D};
	XVT_ASSERT_INT_EQ(net_send_packet_and_flush(30, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_sent_history_write_index,
			  0);

	open_session();
	XVT_ASSERT_INT_EQ(net_send_packet_and_flush(30, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_header(0) & 0x7F, NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(sent_header(1) & 0x7F, NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(g_sent[1].to, 30);
	XVT_ASSERT_INT_EQ(g_sent[1].bytes[4], NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(sent_word(1, 5), packet[1]);

	g_send_result = DPERR_INVALIDPLAYER;
	XVT_ASSERT_INT_EQ(net_send_packet_and_flush(30, packet, sizeof packet),
			  0);
}

/* A keepalive goes to each roster player but the local one whose slot has
 * had nothing for over 3,000 ms, outside the sequence scheme: the KEEPALIVE
 * type alone in the header, then the next sequence expected on the broadcast,
 * group and one-player channels, 127 wrapping to 0, and the time. The slot is
 * stamped. A roster player with no slot gets one. Returns 1. */
static void check_keepalives(void)
{
	fresh();
	open_session();
	g_front_state.net_players[1].player_id = 30;
	g_front_state.net_players[2].player_id = 40;
	g_front_state.net_player_count = 3;
	unsigned int slot = net_find_or_create_peer_slot(30);
	peer(slot)->recv_seq_channel_a = 5;
	peer(slot)->recv_seq_default = 9;
	xvt_time_advance_host_clock(3000 * MS_US);
	XVT_ASSERT_INT_EQ(net_send_sequence_keepalives(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 2);
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(40), 1);

	xvt_time_advance_host_clock(MS_US);
	XVT_ASSERT_INT_EQ(net_send_sequence_keepalives(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(0), NET_PACKET_KEEPALIVE);
	XVT_ASSERT_INT_EQ(sent_length(0), 16);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 6);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 0);
	XVT_ASSERT_INT_EQ(sent_word(0, 12), 10);
	XVT_ASSERT_INT_EQ(sent_word(0, 16), (int)GetTickCount());
	XVT_ASSERT_INT_EQ(peer(slot)->last_activity_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(net_send_sequence_keepalives(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
}

/* ------------------------------------------------------------------------ */
/* The receive pump and the poll. */

/* Without DirectPlay the pump reads nothing. With it, a system message is
 * queued whole up to 512 bytes, a message for another player is dropped, and
 * a game packet is queued with its sender, type word, body, channel and
 * sequence, stamping the sender's last_heard_ms; the same sequence again is
 * not queued. The pump stops when DirectPlay has nothing more. */
static void check_pump_queues(void)
{
	fresh();
	int body = 0x01020304;
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_inbox_next, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);

	fresh();
	open_session();
	struct fake_message *system = &g_inbox[g_inbox_count++];
	system->from = 0;
	system->to = LOCAL_ID;
	system->size = 600;
	memset(system->bytes, 0x5A, sizeof system->bytes);
	inbox_packet(30, 31, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	xvt_time_advance_host_clock(SECOND_US);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_inbox_next, 4);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 2);
	XVT_ASSERT_INT_EQ(queued(0)->direct_play_id, 0);
	XVT_ASSERT_INT_EQ(queued(0)->payload_size, 512);
	XVT_ASSERT_INT_EQ(queued(0)->payload[511], 0x5A);
	XVT_ASSERT_INT_EQ(queued(1)->direct_play_id, 30);
	XVT_ASSERT_INT_EQ(queued(1)->payload_size, 8);
	XVT_ASSERT_INT_EQ(queued_type(1), NET_PACKET_CHAT);
	int queued_body;
	memcpy(&queued_body, queued(1)->payload + 4, sizeof queued_body);
	XVT_ASSERT_INT_EQ(queued_body, body);
	XVT_ASSERT_INT_EQ(queued(1)->packet_class, 1);
	XVT_ASSERT_INT_EQ(queued(1)->sequence_byte, 0);
	XVT_ASSERT_INT_EQ(queued(1)->is_resent_copy, 0);
	unsigned int slot = net_find_or_create_peer_slot(30);
	XVT_ASSERT_INT_EQ(peer(slot)->last_heard_ms, GetTickCount());

	/* With 1023 entries queued the pump reads nothing. */
	fresh();
	open_session();
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	g_front_state.net_runtime_recv_queue_count = 1023;
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_inbox_next, 0);
}

/* A PING is answered with a PONG to its sender, sent and flushed, and is not
 * queued. */
static void check_pump_answers_ping(void)
{
	fresh();
	open_session();
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_PING, NULL, 0);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(0) & 0x7F, NET_PACKET_PONG);
	XVT_ASSERT_INT_EQ(g_sent[1].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(1) & 0x7F, NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
}

/* The poll returns 0 without DirectPlay. With it, after pumping, it returns 1
 * when more than 512 packets are queued or a queued packet from a player has
 * the type, else 0; a system message does not count, and nothing is taken
 * from the queue. */
static void check_poll(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_CHAT),
			  0);
	open_session();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_count = 2;
	g_front_state.net_runtime_recv_queue_write_index = 1;
	queue_entry(1023, 0, 0, 0, 0);
	memcpy(queued(1023)->payload, &(int){NET_PACKET_PING}, sizeof(int));
	queue_entry(0, 30, 1, 0, 0);
	memcpy(queued(0)->payload, &(int){NET_PACKET_CHAT}, sizeof(int));
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_CHAT),
			  1);
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_PING),
			  0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	g_front_state.net_runtime_recv_queue_count = 513;
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_PING),
			  1);
	g_front_state.net_runtime_recv_queue_count = 512;
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_PING),
			  0);
}

/* ------------------------------------------------------------------------ */
/* The roster. */

/* The getters return the roster with its count, the left-this-frame mark, the
 * host mark, the host's id, and as the local player's id that of roster
 * entry 0. */
static void check_roster_getters(void)
{
	fresh();
	g_front_state.net_player_count = 3;
	g_front_state.net_players[0].player_id = 77;
	g_front_state.net_ready_player_left_this_frame = 1;
	g_front_state.net_is_host = 1;
	g_front_state.net_host_player_id = 55;
	int count = 0;
	XVT_ASSERT_TRUE(net_get_player_roster(&count) ==
			g_front_state.net_players);
	XVT_ASSERT_INT_EQ(count, 3);
	XVT_ASSERT_INT_EQ(net_did_ready_player_leave_this_frame(), 1);
	XVT_ASSERT_INT_EQ(net_is_host(), 1);
	XVT_ASSERT_INT_EQ(net_get_host_player_id(), 55);
	XVT_ASSERT_INT_EQ(net_get_local_player_id(), 77);
}

/* Marking, asking and finding look at the entries in use; clearing one flag,
 * counting and clearing all look at all 32. Only a flag of 1 counts as ready.
 * Without DirectPlay the locked setter and clearer do nothing, the setter
 * returning 0. */
static void check_ready_flags(void)
{
	fresh();
	g_front_state.net_players[1].player_id = 30;
	g_front_state.net_players[2].player_id = 40;
	g_front_state.net_players[5].player_id = 50;
	g_front_state.net_player_count = 3;
	net_mark_player_ready_no_lock(30);
	net_mark_player_ready_no_lock(50);
	net_mark_player_ready_no_lock(99);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[5].ready_flag, 0);
	XVT_ASSERT_INT_EQ(net_is_player_ready(30), 1);
	XVT_ASSERT_INT_EQ(net_is_player_ready(40), 0);
	g_front_state.net_players[5].ready_flag = 1;
	XVT_ASSERT_INT_EQ(net_is_player_ready(50), 0);
	XVT_ASSERT_INT_EQ(net_is_player_ready(99), 0);
	XVT_ASSERT_TRUE(net_find_player(40) == &g_front_state.net_players[2]);
	XVT_ASSERT_TRUE(net_find_player(50) == NULL);

	g_front_state.net_players[6].ready_flag = 2;
	XVT_ASSERT_INT_EQ(net_count_ready_players(), 2);
	net_clear_player_ready_flag(50);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[5].ready_flag, 0);
	XVT_ASSERT_INT_EQ(net_count_ready_players(), 1);
	/* Entry 0 is searched too; an id in no entry clears nothing, the local
	 * player's own copy after the roster included. */
	g_front_state.net_players[0].ready_flag = 1;
	net_clear_player_ready_flag(LOCAL_ID);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 0);
	g_front_state.net_runtime_local_player.ready_flag = 1;
	net_clear_player_ready_flag(99);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_local_player.ready_flag, 1);

	XVT_ASSERT_INT_EQ(net_set_player_ready(40), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[2].ready_flag, 0);
	net_clear_player_ready_flag_with_lock_guard(30);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 1);

	net_clear_player_ready_flags();
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[6].ready_flag, 0);
	XVT_ASSERT_INT_EQ(net_count_ready_players(), 0);
}

/* With DirectPlay, the locked setter marks a roster player and returns 1, or
 * returns 0 for an id not in use; the locked clearer clears the flag. Each
 * locks the back buffer again when it was locked. */
static void check_ready_flags_locked(void)
{
	fresh();
	open_session();
	xvt_test_open_display();
	g_front_state.net_players[1].player_id = 30;
	g_front_state.net_players[5].player_id = 50;
	g_front_state.net_player_count = 2;
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	XVT_ASSERT_INT_EQ(net_set_player_ready(30), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(net_set_player_ready(50), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[5].ready_flag, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	net_clear_player_ready_flag_with_lock_guard(30);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	xvt_test_close_display();
}

/* With DirectPlay, the rename asks DirectPlay to give the player both names
 * and returns 1 when it succeeds, the pending mark while it is pending, and 0
 * when it fails; each time the back buffer is locked again when it was
 * locked, and left unlocked when it was not. Without DirectPlay it returns 0
 * and asks nothing. */
static void check_rename(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(net_set_player_name_with_lock_guard(30, "x", "Wedge"),
			  0);
	XVT_ASSERT_INT_EQ(g_rename_calls, 0);

	open_session();
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	XVT_ASSERT_INT_EQ(net_set_player_name_with_lock_guard(30, "x", "Wedge"),
			  1);
	XVT_ASSERT_INT_EQ(g_rename_calls, 1);
	XVT_ASSERT_INT_EQ(g_renamed_player, 30);
	XVT_ASSERT_INT_EQ(strcmp(g_renamed_short, "Wedge"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_renamed_long, "x"), 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	g_rename_result = DPERR_PENDING;
	XVT_ASSERT_INT_EQ(net_set_player_name_with_lock_guard(30, "x", "Wedge"),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	g_rename_result = DPERR_INVALIDPLAYER;
	XVT_ASSERT_INT_EQ(net_set_player_name_with_lock_guard(30, "x", "Wedge"),
			  0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	frontend_display_unlock_back_buffer();
	g_rename_result = 0;
	XVT_ASSERT_INT_EQ(net_set_player_name_with_lock_guard(30, "x", "Wedge"),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);
	xvt_test_close_display();
}

/* ------------------------------------------------------------------------ */
/* Link figures. */

/* The average is the latency total over its samples; 1 for an entry with no
 * samples, 0 for a player with no entry. Setting a latency makes it the
 * entry's only sample, claiming the first free entry when the search meets one
 * before the player's; with all 40 entries taken by others it returns 1 and
 * changes nothing. */
static void check_latency(void)
{
	fresh();
	g_net_player_connection_stats[3].player_id = 30;
	g_net_player_connection_stats[3].latency_total_ms = 300;
	g_net_player_connection_stats[3].latency_sample_count = 4;
	g_net_player_connection_stats[4].player_id = 40;
	XVT_ASSERT_INT_EQ(net_get_average_latency_ms(30), 75);
	XVT_ASSERT_INT_EQ(net_get_average_latency_ms(40), 1);
	XVT_ASSERT_INT_EQ(net_get_average_latency_ms(50), 0);

	for (int i = 0; i < 3; ++i) {
		g_net_player_connection_stats[i].player_id = 60 + i;
	}
	XVT_ASSERT_INT_EQ(net_set_player_latency_ms(30, 120), 1);
	XVT_ASSERT_INT_EQ(net_get_average_latency_ms(30), 120);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[3].latency_sample_count,
			  1);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[5].player_id, 0);
	XVT_ASSERT_INT_EQ(net_set_player_latency_ms(50, 90), 1);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[5].player_id, 50);
	XVT_ASSERT_INT_EQ(net_get_average_latency_ms(50), 90);

	/* A free entry before the player's is claimed instead. */
	g_net_player_connection_stats[1].player_id = 0;
	XVT_ASSERT_INT_EQ(net_set_player_latency_ms(40, 30), 1);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[1].player_id, 40);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[1].latency_total_ms,
			  30);

	for (int i = 0; i < 40; ++i) {
		g_net_player_connection_stats[i].player_id = 100 + i;
		g_net_player_connection_stats[i].latency_total_ms = 7;
		g_net_player_connection_stats[i].latency_sample_count = 1;
	}
	XVT_ASSERT_INT_EQ(net_set_player_latency_ms(30, 120), 1);
	for (int i = 0; i < 40; ++i) {
		XVT_ASSERT_INT_EQ(g_net_player_connection_stats[i].player_id,
				  100 + i);
		XVT_ASSERT_INT_EQ(
			g_net_player_connection_stats[i].latency_total_ms, 7);
	}
}

/* Each setter stores its count in the player's entry, wherever it is, and
 * leaves the other counts; with no entry it claims the first free one with a
 * latency total of 1 and the other counts 0. With all 40 entries taken by
 * others it returns 1 and changes nothing. */
static void check_count_setters(void)
{
	fresh();
	g_net_player_connection_stats[2].player_id = 30;
	g_net_player_connection_stats[2].packet_count = 5;
	g_net_player_connection_stats[2].packet_drop_count = 6;
	g_net_player_connection_stats[2].packet_retry_count = 7;
	g_net_player_connection_stats[2].latency_total_ms = 99;
	XVT_ASSERT_INT_EQ(net_set_player_packet_count(30, 50), 1);
	XVT_ASSERT_INT_EQ(net_set_player_packet_drop_count(30, 60), 1);
	XVT_ASSERT_INT_EQ(net_set_player_packet_retry_count(30, 70), 1);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].player_id, 0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[2].packet_count, 50);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[2].packet_drop_count,
			  60);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[2].packet_retry_count,
			  70);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[2].latency_total_ms,
			  99);

	const struct net_player_connection_stats *entry =
		&g_net_player_connection_stats[0];
	XVT_ASSERT_INT_EQ(net_set_player_packet_count(40, 8), 1);
	XVT_ASSERT_INT_EQ(entry->player_id, 40);
	XVT_ASSERT_INT_EQ(entry->packet_count, 8);
	XVT_ASSERT_INT_EQ(entry->latency_total_ms, 1);
	XVT_ASSERT_INT_EQ(entry->packet_drop_count, 0);
	XVT_ASSERT_INT_EQ(entry->packet_retry_count, 0);

	entry = &g_net_player_connection_stats[1];
	XVT_ASSERT_INT_EQ(net_set_player_packet_drop_count(50, 9), 1);
	XVT_ASSERT_INT_EQ(entry->player_id, 50);
	XVT_ASSERT_INT_EQ(entry->packet_drop_count, 9);
	XVT_ASSERT_INT_EQ(entry->latency_total_ms, 1);
	XVT_ASSERT_INT_EQ(entry->packet_count, 0);
	XVT_ASSERT_INT_EQ(entry->packet_retry_count, 0);

	entry = &g_net_player_connection_stats[3];
	g_net_player_connection_stats[3].packet_count = 4;
	g_net_player_connection_stats[3].packet_drop_count = 4;
	XVT_ASSERT_INT_EQ(net_set_player_packet_retry_count(60, 3), 1);
	XVT_ASSERT_INT_EQ(entry->player_id, 60);
	XVT_ASSERT_INT_EQ(entry->packet_retry_count, 3);
	XVT_ASSERT_INT_EQ(entry->latency_total_ms, 1);
	XVT_ASSERT_INT_EQ(entry->packet_count, 0);
	XVT_ASSERT_INT_EQ(entry->packet_drop_count, 0);

	for (int i = 0; i < 40; ++i) {
		g_net_player_connection_stats[i].player_id = 100 + i;
		g_net_player_connection_stats[i].packet_count = 1;
		g_net_player_connection_stats[i].packet_drop_count = 1;
		g_net_player_connection_stats[i].packet_retry_count = 1;
	}
	XVT_ASSERT_INT_EQ(net_set_player_packet_count(30, 50), 1);
	XVT_ASSERT_INT_EQ(net_set_player_packet_drop_count(30, 50), 1);
	XVT_ASSERT_INT_EQ(net_set_player_packet_retry_count(30, 50), 1);
	for (int i = 0; i < 40; ++i) {
		XVT_ASSERT_INT_EQ(g_net_player_connection_stats[i].player_id,
				  100 + i);
		XVT_ASSERT_INT_EQ(g_net_player_connection_stats[i].packet_count,
				  1);
		XVT_ASSERT_INT_EQ(
			g_net_player_connection_stats[i].packet_drop_count, 1);
		XVT_ASSERT_INT_EQ(
			g_net_player_connection_stats[i].packet_retry_count, 1);
	}
}

/* Each getter adds the player's slot count to its entry's count; a player with
 * no entry has the slot's alone, and asking about a player with no slot adds
 * one. */
static void check_count_getters(void)
{
	fresh();
	unsigned int slot = net_find_or_create_peer_slot(30);
	peer(slot)->packet_count = 100;
	peer(slot)->packet_drop_count = 10;
	peer(slot)->packet_retry_count = 1;
	g_net_player_connection_stats[4].player_id = 30;
	g_net_player_connection_stats[4].packet_count = 20;
	g_net_player_connection_stats[4].packet_drop_count = 2;
	g_net_player_connection_stats[4].packet_retry_count = 3;
	XVT_ASSERT_INT_EQ(net_get_player_packet_count(30), 120);
	XVT_ASSERT_INT_EQ(net_get_player_packet_drop_count(30), 12);
	XVT_ASSERT_INT_EQ(net_get_player_packet_retry_count(30), 4);
	g_net_player_connection_stats[4].player_id = 31;
	XVT_ASSERT_INT_EQ(net_get_player_packet_count(30), 100);
	XVT_ASSERT_INT_EQ(net_get_player_packet_drop_count(30), 10);
	XVT_ASSERT_INT_EQ(net_get_player_packet_retry_count(30), 1);
	XVT_ASSERT_INT_EQ(net_get_player_packet_count(31), 20);
	XVT_ASSERT_INT_EQ(net_get_player_packet_drop_count(31), 2);
	XVT_ASSERT_INT_EQ(net_get_player_packet_retry_count(31), 3);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 2);
}

/* The loss rate is drops plus twice the retries per 10,000 packets, at most
 * 10,000, with a packet count of 0 counted as 1. The host adds its slot's
 * counts to the player's entry, and asking about a player with no slot adds
 * one; another player uses the entry alone and returns 0 without one. */
static void check_drop_rate(void)
{
	fresh();
	g_front_state.net_is_host = 1;
	unsigned int slot = net_find_or_create_peer_slot(30);
	peer(slot)->packet_count = 90;
	peer(slot)->packet_drop_count = 3;
	peer(slot)->packet_retry_count = 2;
	g_net_player_connection_stats[0].player_id = 30;
	g_net_player_connection_stats[0].packet_count = 10;
	g_net_player_connection_stats[0].packet_drop_count = 1;
	g_net_player_connection_stats[0].packet_retry_count = 1;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(30), 1000);
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(40), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 2);
	g_net_player_connection_stats[1].player_id = 50;
	g_net_player_connection_stats[1].packet_drop_count = 1;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(50), 10000);
	g_net_player_connection_stats[1].packet_drop_count = 3;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(50), 10000);
	/* Rounded down: 8 drops in 9 packets. Then just over the cap. */
	g_net_player_connection_stats[2].player_id = 70;
	g_net_player_connection_stats[2].packet_count = 9;
	g_net_player_connection_stats[2].packet_drop_count = 8;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(70), 8888);
	g_net_player_connection_stats[3].player_id = 80;
	g_net_player_connection_stats[3].packet_count = 10000;
	g_net_player_connection_stats[3].packet_drop_count = 10001;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(80), 10000);

	g_front_state.net_is_host = 0;
	g_net_player_connection_stats[0].packet_count = 200;
	g_net_player_connection_stats[0].packet_drop_count = 2;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(30), 200);
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(60), 0);
	g_net_player_connection_stats[1].packet_drop_count = 1;
	g_net_player_connection_stats[1].packet_retry_count = 1;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(50), 10000);
	g_net_player_connection_stats[1].packet_count = 30000;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(50), 1);
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(70), 8888);
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(80), 10000);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 5);
}

/* Each transport has its own provider GUID, copied into one scratch copy whose
 * address is returned; an unknown transport gets NULL. The TCP/IP one is
 * DirectPlay's published TCP/IP provider. */
static void check_service_provider_guid(void)
{
	static const GUID tcp_ip = {
		0x36E95EE0,
		0x8577,
		0x11CF,
		{0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82},
	};
	GUID guids[4];
	const GUID *scratch = NULL;
	for (int transport = 0; transport < 4; ++transport) {
		const GUID *guid = net_get_direct_play_service_provider_guid(
			(network_transport_type)transport);
		XVT_ASSERT_TRUE(guid != NULL);
		if (scratch == NULL) {
			scratch = guid;
		}
		XVT_ASSERT_TRUE(guid == scratch);
		guids[transport] = *guid;
	}
	for (int i = 0; i < 4; ++i) {
		for (int j = i + 1; j < 4; ++j) {
			XVT_ASSERT_TRUE(memcmp(&guids[i], &guids[j],
					       sizeof guids[i]) != 0);
		}
	}
	XVT_ASSERT_INT_EQ(
		memcmp(&guids[NET_TRANSPORT_TCPIP], &tcp_ip, sizeof tcp_ip), 0);
	XVT_ASSERT_TRUE(net_get_direct_play_service_provider_guid(
				(network_transport_type)4) == NULL);
}

/* ------------------------------------------------------------------------ */
/* Known failures. */

/* Known failure rename_without_session_unlocked, issue #7: frontend_state.h
 * says callers of work that unlocks the back buffer lock it again after, as
 * net_set_player_name_with_lock_guard does on every path but one. Without
 * DirectPlay it returns 0 with the buffer it found locked left unlocked. Its
 * own comment describes that path as it is; the fix changes that comment. */
static void check_rename_without_session_relocks(void)
{
	fresh();
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(net_set_player_name_with_lock_guard(30, "x", "Wedge"),
			  0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	xvt_test_close_display();
}

/* Known failure one_player_send_past_peer_table, issue #11: the lobby keeps 40
 * peer slots, and the spare field after them is never read or written by
 * name (frontend_state.h). With every slot taken, a send to a 41st player gets
 * slot 40 from net_find_or_create_peer_slot, and the one-player path stores
 * that packet's trailer in slot 40, which is the spare field. The send's own
 * comment describes this as it is; the fix changes that comment. */
static void check_send_keeps_past_peer_table(void)
{
	fresh();
	fill_peer_table();
	int packet[2] = {NET_PACKET_CHAT, 7};
	XVT_ASSERT_INT_EQ(net_send_packet_internal(999, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(spare_bytes_set(), 0);
}

/* Known failure receive_past_peer_table, issue #108: as above, the spare field
 * after the 40 peer slots is never read or written by name. With every slot
 * taken, a game packet from a 41st player makes the pump stamp slot 40's
 * last_heard_ms, which is in the spare field. */
static void check_receive_keeps_past_peer_table(void)
{
	fresh();
	open_session();
	g_front_state.net_is_host = 1;
	fill_peer_table();
	int body = 1;
	inbox_packet(999, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_inbox_next, 1);
	XVT_ASSERT_INT_EQ(spare_bytes_set(), 0);
}

int main(int argc, char **argv)
{
	fail_after_seconds(30);
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"rename_without_session_unlocked",
			 check_rename_without_session_relocks},
			{"one_player_send_past_peer_table",
			 check_send_keeps_past_peer_table},
			{"receive_past_peer_table",
			 check_receive_keeps_past_peer_table},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		return 0;
	}
	check_new_peer_slot();
	check_full_peer_table();
	check_incoming_sequence();
	check_compact_peer_slots();
	check_find_resent_copy();
	check_remove_queued();
	check_broadcast_send();
	check_group_and_one_player_send();
	check_one_player_trailer();
	check_send_and_flush();
	check_keepalives();
	check_pump_queues();
	check_pump_answers_ping();
	check_poll();
	check_roster_getters();
	check_ready_flags();
	check_ready_flags_locked();
	check_rename();
	check_latency();
	check_count_setters();
	check_count_getters();
	check_drop_rate();
	check_service_provider_guid();
	return 0;
}

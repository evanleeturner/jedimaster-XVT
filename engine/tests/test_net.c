/* Tests for the lobby's side of DirectPlay, xvt/net/net.c and the files split
 * from it (net_peers.c, net_pump.c, net_receive.c, net_send.c and
 * net_system_messages.c): its peer slots, the check on an arriving packet's
 * sequence, its receive queue, the sends on the broadcast, group and
 * one-player channels, the receive pump with its answers to PING,
 * KEEPALIVE_ACK, NACK, WORLD_NACK and KEEPALIVE, the resent copies and trailers
 * it queues, the dequeue that hands packets to the game in sequence order and
 * asks for the ones missing, the keepalives, the drop of silent peers, the
 * handling of DirectPlay's system messages, the roster and its refresh, the
 * polls, the closing of the session, the hand-over of state to and from the
 * flight session, the ready flags and the link figures kept for each player.
 * Each check sets the lobby state it needs in g_front_state and drives the host
 * clock. Where a check needs a DirectPlay session, it gives the lobby an
 * interface this file owns: Send records what the lobby sends, Receive hands
 * the pump the messages the check put in its inbox, EnumPlayers lists the
 * players the check chose, and the calls that create, rename and close are
 * recorded. The packets in the inbox come from the lobby's own senders,
 * replayed as if another player had sent them, or are laid out byte by byte
 * where a sender would not make them; the dequeue's checks fill the receive
 * queue entry by entry. The checks also read the lines the code logs, kept by
 * a log sink with DEBUG lines let through. Some checks lock a back buffer on a
 * frontend display with no window. No game data is read. Every check starts
 * from a cleared g_front_state with the local player, id 1000, alone in the
 * roster, the group id 2000, no link figures, no session, the broadcast and
 * group trailers empty as a shutdown leaves them, and the clock at one
 * second.
 *
 * Not checked here: the lobby's opening of DirectPlay, which no file under test
 * holds. Nor is what the pump, the sends and the dequeue do with a resent copy
 * or the sequences of a sender that gets no peer slot because the table is
 * full, beyond the known failures for it below: the sanitizer stops the
 * program at the first write past the table. The dequeue's second pass, run
 * when 1,023 packets stay queued, is checked for the one packet it can reach,
 * the first one already asked about: every packet before it is one the first
 * pass dropped or delivered, and the stale and no-peer drops of the second
 * pass, its end with nothing delivered and its arithmetic on a packet no first
 * pass left behind run for no queue the pump can build.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL_log.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net.h"
#include "xvt/net/net_peers.h"
#include "xvt/net/net_pump.h"
#include "xvt/net/net_receive.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_send.h"
#include "xvt/net/net_system_messages.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/timing/host_clock.h"

enum {
	SECOND_US = 1000000,
	MS_US = 1000,
	LOCAL_ID = 1000,
	GROUP_ID = 2000,
	ONE_PLAYER_BIT = 0x8000,
	GROUP_BITS = 0x8080,
	FAKE_MESSAGES = 64,
	LINE_CAPACITY = 4096,
	LINE_SIZE = 256,
	/* Byte offsets in a resent packet as sent: the header word, the channel
	 * byte, the body's length, the body. */
	RESEND_CLASS = 2,
	RESEND_LENGTH = 3,
	RESEND_BODY = 5,
};

/* ------------------------------------------------------------------------ */
/* The log sink. */

static char g_lines[LINE_CAPACITY][LINE_SIZE];
static int g_line_count;

/* Keeps each line the engine writes, without Aeron's "xvt: " category
 * prefix. */
static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	static const char prefix[] = "xvt: ";
	if (g_line_count >= LINE_CAPACITY) {
		fprintf(stderr, "more than %d lines written\n", LINE_CAPACITY);
		exit(1);
	}
	if (!strncmp(message, prefix, sizeof prefix - 1)) {
		message += sizeof prefix - 1;
	}
	snprintf(g_lines[g_line_count++], LINE_SIZE, "%s", message);
}

static void forget_lines(void) { g_line_count = 0; }

/* Returns 1 when the kept line starts with start, followed by a space or
 * nothing. */
static int line_starts_with(int index, const char *start)
{
	size_t length = strlen(start);
	const char *line = g_lines[index];
	return strncmp(line, start, length) == 0 &&
	       (line[length] == ' ' || line[length] == '\0');
}

/* The number of kept lines that start with start: an event name, with any of
 * its first values. */
static int count_lines(const char *start)
{
	int count = 0;
	for (int i = 0; i < g_line_count; ++i) {
		if (line_starts_with(i, start)) {
			++count;
		}
	}
	return count;
}

/* The number after " key=" in the last kept line that starts with start; the
 * check fails when there is none. */
static int line_value(const char *start, const char *key)
{
	char field[64];
	snprintf(field, sizeof field, " %s=", key);
	for (int i = g_line_count - 1; i >= 0; --i) {
		if (!line_starts_with(i, start)) {
			continue;
		}
		const char *value = strstr(g_lines[i], field);
		XVT_ASSERT_TRUE(value != NULL);
		return atoi(value + strlen(field));
	}
	fprintf(stderr, "no line starts with %s\n", start);
	exit(1);
}

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
static int g_send_saw_locked;
static HRESULT g_send_result;
static struct fake_message g_inbox[FAKE_MESSAGES];
static int g_inbox_count;
static int g_inbox_next;
static int g_receive_calls;
static int g_receive_saw_locked;
static HRESULT g_rename_result;
static int g_rename_calls;
/* The players EnumPlayers lists, in order, and what CreatePlayer answers. */
struct listed_player {
	DPID id;
	uint32_t type;
	const char *short_name;
	const char *long_name;
};
static struct listed_player g_listed[48];
static int g_listed_count;
static int g_enum_saw_locked;
/* What the lobby asked of DirectPlay when closing, in order: 1 destroy the
 * player, 2 destroy the group, 3 close, 4 release, each with its argument. */
static int g_closing_calls[8][2];
static int g_closing_count;
static int g_closing_saw_locked;
static HRESULT g_create_result;
static DPID g_create_player_id;
static char g_create_short[16];
static char g_create_long[16];
static DPID g_renamed_player;
static char g_renamed_short[16];
static char g_renamed_long[16];

static HRESULT AERON_DXAPI fake_send(IDirectPlay2A *self, DPID from, DPID to,
				     uint32_t flags, void *data, uint32_t size)
{
	(void)self;
	XVT_ASSERT_INT_EQ(flags, 0);
	if (g_front_state.back_buffer_locked != 0) {
		g_send_saw_locked = 1;
	}
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
	XVT_ASSERT_INT_EQ(flags, DPRECEIVE_ALL);
	++g_receive_calls;
	if (g_front_state.back_buffer_locked != 0) {
		g_receive_saw_locked = 1;
	}
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

static HRESULT AERON_DXAPI fake_enum_players(IDirectPlay2A *self,
					     GUID *instance_guid,
					     DPEnumPlayersCallback2 callback,
					     void *context, uint32_t flags)
{
	(void)self;
	(void)instance_guid;
	XVT_ASSERT_INT_EQ(flags, 0);
	if (g_front_state.back_buffer_locked != 0) {
		g_enum_saw_locked = 1;
	}
	for (int i = 0; i < g_listed_count; ++i) {
		DPNAME name = {sizeof name, 0, (char *)g_listed[i].short_name,
			       (char *)g_listed[i].long_name};
		if (!callback(g_listed[i].id, g_listed[i].type, &name, 0,
			      context)) {
			break;
		}
	}
	return 0;
}

static HRESULT AERON_DXAPI fake_create_player(IDirectPlay2A *self, DPID *player,
					      DPNAME *name, void *event,
					      void *data, uint32_t size,
					      uint32_t flags)
{
	(void)self;
	(void)event;
	(void)data;
	XVT_ASSERT_INT_EQ(size, 0);
	XVT_ASSERT_INT_EQ(flags, 0);
	XVT_ASSERT_INT_EQ(name->dwSize, sizeof *name);
	XVT_ASSERT_INT_EQ(name->dwFlags, 0);
	strncpy(g_create_short, name->lpszShortNameA,
		sizeof g_create_short - 1);
	strncpy(g_create_long, name->lpszLongNameA, sizeof g_create_long - 1);
	*player = g_create_player_id;
	return g_create_result;
}

static void note_closing_call(int call, int argument)
{
	XVT_ASSERT_TRUE(g_closing_count < 8);
	if (g_front_state.back_buffer_locked != 0) {
		g_closing_saw_locked = 1;
	}
	g_closing_calls[g_closing_count][0] = call;
	g_closing_calls[g_closing_count][1] = argument;
	++g_closing_count;
}

static HRESULT AERON_DXAPI fake_destroy_player(IDirectPlay2A *self, DPID player)
{
	(void)self;
	note_closing_call(1, (int)player);
	return 0;
}

static HRESULT AERON_DXAPI fake_destroy_group(IDirectPlay2A *self, DPID group)
{
	(void)self;
	note_closing_call(2, (int)group);
	return 0;
}

static HRESULT AERON_DXAPI fake_close(IDirectPlay2A *self)
{
	(void)self;
	note_closing_call(3, 0);
	return 0;
}

static uint32_t AERON_DXAPI fake_release(IDirectPlay2A *self)
{
	(void)self;
	note_closing_call(4, 0);
	return 0;
}

static IDirectPlay2AVtbl g_fake_vtbl = {
	.Send = fake_send,
	.Receive = fake_receive,
	.SetPlayerName = fake_set_player_name,
	.EnumPlayers = fake_enum_players,
	.CreatePlayer = fake_create_player,
	.DestroyPlayer = fake_destroy_player,
	.DestroyGroup = fake_destroy_group,
	.Close = fake_close,
	.Release = fake_release,
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

/* Puts a message of the given bytes in the inbox, from and to the given ids. */
static void inbox_bytes(DPID from, DPID to, const void *bytes, uint32_t size)
{
	XVT_ASSERT_TRUE(g_inbox_count < FAKE_MESSAGES);
	XVT_ASSERT_TRUE(size <= sizeof g_inbox[0].bytes);
	struct fake_message *message = &g_inbox[g_inbox_count++];
	message->from = from;
	message->to = to;
	message->size = size;
	memcpy(message->bytes, bytes, size);
}

/* Puts send number index, which the stand-in recorded, in the inbox as if the
 * player from had sent it to the local player. */
static void replay_send(int index, DPID from)
{
	XVT_ASSERT_TRUE(index >= 0 && index < g_sent_count);
	inbox_bytes(from, LOCAL_ID, g_sent[index].bytes, g_sent[index].size);
}

/* Sends a game packet of the type with one body word, as the lobby sends one
 * to a player (0 for everyone, the group's id for the group), and returns the
 * number of that send. With no session it records nothing. */
static int send_game_packet(int to, int type, int word)
{
	int packet[2] = {type, word};
	int number = g_sent_count;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(to, packet, sizeof packet),
			  1);
	return number;
}

/* Sends a packet outside the sequence scheme, the way the keepalive and its
 * answer go, and returns the number of that send. packet[0] is the type. */
static int send_control_packet(int to, const int *packet, int size)
{
	int number = g_sent_count;
	XVT_ASSERT_INT_EQ(net_send_direct_play_packet(to, packet, size, 0), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, number + 1);
	return number;
}

/* Puts a KEEPALIVE_ACK from player 30 in the inbox: the echoed time, then its
 * packet, drop and retry counts. */
static void inbox_keepalive_ack(int echoed_ms, int packets, int drops,
				int retries)
{
	int packet[5] = {NET_PACKET_KEEPALIVE_ACK, echoed_ms, packets, drops,
			 retries};
	int number = send_control_packet(30, packet, sizeof packet);
	replay_send(number, 30);
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
	g_front_state.net_runtime_broadcast_pending_payload.piggyback_empty = 1;
	g_front_state.net_runtime_group_pending_payload.piggyback_empty = 1;
	g_draw_surface_ptr = NULL;
	memset(g_sent, 0, sizeof g_sent);
	g_sent_count = 0;
	g_send_saw_locked = 0;
	g_send_result = 0;
	memset(g_inbox, 0, sizeof g_inbox);
	g_inbox_count = 0;
	g_inbox_next = 0;
	g_receive_calls = 0;
	g_receive_saw_locked = 0;
	forget_lines();
	g_rename_result = 0;
	g_rename_calls = 0;
	g_listed_count = 0;
	g_enum_saw_locked = 0;
	g_closing_count = 0;
	g_closing_saw_locked = 0;
	memset(g_closing_calls, 0, sizeof g_closing_calls);
	g_create_result = 0;
	g_create_player_id = 0;
	memset(g_create_short, 0, sizeof g_create_short);
	memset(g_create_long, 0, sizeof g_create_long);
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

/* 1 when one of the 40 peer slots belongs to the player. */
static int has_peer_slot(DPID player)
{
	for (int i = 0; i < 40; ++i) {
		if (peer((unsigned int)i)->direct_play_id == player) {
			return 1;
		}
	}
	return 0;
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
	XVT_ASSERT_INT_EQ(line_value("network.lobby_keepalive_sent", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_keepalive_sent", "peer"),
			  (int)slot);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_keepalive_sent", "broadcast"), 6);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_keepalive_sent", "group"),
			  0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_keepalive_sent", "single"),
			  10);

	/* A new slot has every sequence at 127, so each next one is 0. */
	fresh();
	open_session();
	g_front_state.net_players[1].player_id = 40;
	g_front_state.net_player_count = 2;
	net_find_or_create_peer_slot(40);
	xvt_time_advance_host_clock(4000 * MS_US);
	peer(0)->last_activity_ms = 0;
	XVT_ASSERT_INT_EQ(net_send_sequence_keepalives(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(g_sent[0].to, 40);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 0);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 0);
	XVT_ASSERT_INT_EQ(sent_word(0, 12), 0);
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
/* net_pump_incoming_packets: the back buffer, system messages, messages for
 * others, the PING and the KEEPALIVE_ACK. */

/* The back buffer is unlocked while the pump reads and locked again when it
 * was locked, whether the pump stops because DirectPlay has nothing more,
 * because 1,023 entries are queued, which it logs with the count, or because
 * there is no DirectPlay. It is left unlocked when it was not locked. Every
 * read asks for all waiting messages. */
static void check_pump_back_buffer(void)
{
	fresh();
	open_session();
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_receive_calls, 1);
	XVT_ASSERT_INT_EQ(g_receive_saw_locked, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_TRUE(g_draw_surface_ptr != NULL);

	g_front_state.net_runtime_recv_queue_count = 1023;
	g_receive_calls = 0;
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_receive_calls, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_receive_queue_full"), 1);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_receive_queue_full", "queued"), 1023);

	frontend_display_unlock_back_buffer();
	g_front_state.net_runtime_recv_queue_count = 1022;
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_receive_calls, 1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);
	g_front_state.net_runtime_recv_queue_count = 1023;
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);

	/* Without DirectPlay the pump reads nothing and leaves the buffer as it
	 * found it. */
	g_front_state.net_direct_play = NULL;
	g_receive_calls = 0;
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_receive_calls, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	xvt_test_close_display();
}

/* A system message (sender 0) is queued as received, with sender 0, not marked
 * a resent copy whatever the entry held, and its size logged as received; one
 * over 512 bytes is cut to 512. The write index wraps from 1,023 to 0. */
static void check_pump_system_messages(void)
{
	uint8_t bytes[600];
	fresh();
	open_session();
	for (int i = 0; i < 600; ++i) {
		bytes[i] = (uint8_t)(i * 7);
	}
	inbox_bytes(0, LOCAL_ID, bytes, 600);
	inbox_bytes(0, LOCAL_ID, bytes, 512);
	inbox_bytes(0, LOCAL_ID, bytes, 12);
	for (int i = 0; i < 3; ++i) {
		queued((1023 + i) % 1024)->direct_play_id = 99;
		queued((1023 + i) % 1024)->is_resent_copy = 1;
	}
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 3);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 2);
	static const uint32_t sizes[] = {512, 512, 12};
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(queued((1023 + i) % 1024)->direct_play_id, 0);
		XVT_ASSERT_INT_EQ(queued((1023 + i) % 1024)->is_resent_copy, 0);
		XVT_ASSERT_INT_EQ(queued((1023 + i) % 1024)->payload_size,
				  sizes[i]);
		XVT_ASSERT_INT_EQ(memcmp(queued((1023 + i) % 1024)->payload,
					 bytes, sizes[i]),
				  0);
	}
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_system_received"), 3);
}

/* A message for anyone but the local player is dropped, logged with both ids,
 * and gives its sender no peer slot; the same packet to the local player is
 * queued. A PING is answered and logged but not queued. */
static void check_pump_misaddressed_and_ping(void)
{
	int body = 5;
	fresh();
	open_session();
	inbox_packet(30, 31, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_inbox_next, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(has_peer_slot(30), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_not_local", "from"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_not_local", "to"),
			  31);
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_packet_not_local"), 1);

	fresh();
	open_session();
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_PING, NULL, 0);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_ping_answered", "from"),
			  30);
}

/* The state a KEEPALIVE_ACK check starts from: player 30 has the entry at the
 * index with the given latency total and samples, other players have the other
 * entries of 0 to 2, and the clock reads ten seconds. */
static void ack_world(int total_ms, int samples, int index)
{
	fresh();
	open_session();
	xvt_time_advance_host_clock(9 * SECOND_US);
	g_net_player_connection_stats[0].player_id = 60;
	g_net_player_connection_stats[1].player_id = 61;
	g_net_player_connection_stats[2].player_id = 62;
	g_net_player_connection_stats[index].player_id = 30;
	g_net_player_connection_stats[index].latency_total_ms = total_ms;
	g_net_player_connection_stats[index].latency_sample_count = samples;
}

/* A KEEPALIVE_ACK stamps the sender's last_heard_ms and stores the three
 * counts it carries in the sender's entry. The latency sample is the time
 * since the echoed stamp less 40 ms, 1 when the stamp is not older than that.
 * It is added to the entry's total and samples when under 750 ms and no more
 * than half above the average, and left out otherwise; the counts are stored
 * either way. Nothing is queued. */
static void check_pump_keepalive_ack_latency(void)
{
	static const struct {
		int total_ms;
		int samples;
		int echo_age_ms; /* The clock less the echoed stamp. */
		int latency_ms;
		int new_total_ms;
		int new_samples;
		int entry;
	} rows[] = {
		/* The average is 100. */
		{400, 4, 140, 100, 500, 5, 2}, /* Equal to it. */
		{400, 4, 100, 60, 460, 5, 2},  /* Below it. */
		{400, 4, 190, 150, 550, 5, 2}, /* Exactly half above. */
		{400, 4, 191, 151, 400, 4, 2}, /* Just over half above. */
		{400, 4, 41, 1, 401, 5, 2},
		{400, 4, 40, 1, 401, 5, 2}, /* A stamp as old as the 40 ms. */
		{400, 4, 0, 1, 401, 5, 2},  /* A stamp from this very moment. */
		/* The average is 500. */
		{2000, 4, 789, 749, 2749, 5,
		 2}, /* The last sample under 750. */
		{2000, 4, 790, 750, 2000, 4, 2},
		{2000, 4, 5000, 4960, 2000, 4, 2},
		/* One sample, and the entry first in the table. */
		{100, 1, 190, 150, 250, 2, 0},
		{100, 1, 191, 151, 100, 1, 0},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		ack_world(rows[row].total_ms, rows[row].samples,
			  rows[row].entry);
		int now = (int)GetTickCount();
		unsigned int slot = net_find_or_create_peer_slot(30);
		peer(slot)->last_heard_ms = 0;
		inbox_keepalive_ack(now - rows[row].echo_age_ms, 11, 22, 33);
		net_pump_incoming_packets();
		const struct net_player_connection_stats *entry =
			&g_net_player_connection_stats[rows[row].entry];
		XVT_ASSERT_INT_EQ(entry->latency_total_ms,
				  rows[row].new_total_ms);
		XVT_ASSERT_INT_EQ(entry->latency_sample_count,
				  rows[row].new_samples);
		XVT_ASSERT_INT_EQ(entry->packet_count, 11);
		XVT_ASSERT_INT_EQ(entry->packet_drop_count, 22);
		XVT_ASSERT_INT_EQ(entry->packet_retry_count, 33);
		XVT_ASSERT_INT_EQ(
			g_net_player_connection_stats[rows[row].entry == 0 ? 1
									   : 0]
				.packet_count,
			0);
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  0);
		XVT_ASSERT_INT_EQ(peer(slot)->last_heard_ms, GetTickCount());
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_keepalive_ack", "from"), 30);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_keepalive_ack", "latency"),
			rows[row].latency_ms);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_keepalive_ack", "entry"),
			rows[row].entry);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_keepalive_ack", "packets"),
			11);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_keepalive_ack", "drops"), 22);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_keepalive_ack", "retries"),
			33);
	}
}

/* A KEEPALIVE_ACK from a player with no peer slot gives it one. From a player
 * with no entry it claims the first free entry for it: its id, the latency as
 * the only sample, capped at 750, and the counts. A player with no entry and
 * none free is logged as a warning and changes nothing. */
static void check_pump_keepalive_ack_new_entry(void)
{
	ack_world(0, 0, 2);
	g_net_player_connection_stats[2].player_id = 0;
	inbox_keepalive_ack((int)GetTickCount() - 140, 11, 22, 33);
	net_pump_incoming_packets();
	const struct net_player_connection_stats *entry =
		&g_net_player_connection_stats[2];
	XVT_ASSERT_INT_EQ(has_peer_slot(30), 1);
	XVT_ASSERT_INT_EQ(entry->player_id, 30);
	XVT_ASSERT_INT_EQ(entry->latency_total_ms, 100);
	XVT_ASSERT_INT_EQ(entry->latency_sample_count, 1);
	XVT_ASSERT_INT_EQ(entry->packet_count, 11);
	XVT_ASSERT_INT_EQ(entry->packet_drop_count, 22);
	XVT_ASSERT_INT_EQ(entry->packet_retry_count, 33);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[3].player_id, 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_keepalive_ack", "entry"),
			  2);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_link_stats_full"), 0);

	ack_world(0, 0, 2);
	g_net_player_connection_stats[2].player_id = 0;
	inbox_keepalive_ack((int)GetTickCount() - 940, 1, 2, 3);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(entry->player_id, 30);
	XVT_ASSERT_INT_EQ(entry->latency_total_ms, 750);
	XVT_ASSERT_INT_EQ(entry->latency_sample_count, 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_keepalive_ack", "latency"),
			  750);

	ack_world(0, 0, 2);
	g_net_player_connection_stats[2].player_id = 0;
	inbox_keepalive_ack((int)GetTickCount() - 790, 1, 2, 3);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(entry->latency_total_ms, 750);

	/* The first entry of the table is free. */
	ack_world(0, 0, 2);
	g_net_player_connection_stats[0].player_id = 0;
	g_net_player_connection_stats[2].player_id = 62;
	inbox_keepalive_ack((int)GetTickCount() - 140, 11, 22, 33);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].player_id, 30);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].latency_total_ms,
			  100);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].packet_count, 11);

	ack_world(0, 0, 2);
	for (int i = 0; i < 40; ++i) {
		g_net_player_connection_stats[i].player_id = 100 + i;
		g_net_player_connection_stats[i].latency_total_ms = 7;
		g_net_player_connection_stats[i].latency_sample_count = 1;
	}
	inbox_keepalive_ack((int)GetTickCount() - 140, 11, 22, 33);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_link_stats_full"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_link_stats_full", "player"),
			  30);
	for (int i = 0; i < 40; ++i) {
		XVT_ASSERT_INT_EQ(g_net_player_connection_stats[i].player_id,
				  100 + i);
		XVT_ASSERT_INT_EQ(
			g_net_player_connection_stats[i].latency_total_ms, 7);
		XVT_ASSERT_INT_EQ(g_net_player_connection_stats[i].packet_count,
				  0);
	}
}

/* ------------------------------------------------------------------------ */
/* net_pump_incoming_packets: WORLD_NACK, NACK and KEEPALIVE. */

/* Starts counting sends again from 0. */
static void forget_sends(void)
{
	g_sent_count = 0;
	memset(g_sent, 0, sizeof g_sent);
}

/* Drops what the lobby's own sends queued for it: the receive queue is empty
 * again. */
static void empty_recv_queue(void)
{
	g_front_state.net_runtime_recv_queue_count = 0;
	g_front_state.net_runtime_recv_queue_read_index = 0;
	g_front_state.net_runtime_recv_queue_write_index = 0;
}

/* Puts a control packet of the type with the given body words in the inbox as
 * sent by player from. */
static void inbox_control(DPID from, int type, int first, int second, int third,
			  int fourth, int words)
{
	int packet[5] = {type, first, second, third, fourth};
	int number = send_control_packet(
		(int)from, packet, (int)((unsigned)(words + 1) * sizeof(int)));
	replay_send(number, from);
}

/* The number of kept lines that start with start and hold text. */
static int count_lines_holding(const char *start, const char *text)
{
	int count = 0;
	for (int i = 0; i < g_line_count; ++i) {
		if (line_starts_with(i, start) && strstr(g_lines[i], text)) {
			++count;
		}
	}
	return count;
}

/* The header's type, sequence and channel bits (0x80 and 0x8000) of send
 * number index. */
static int sent_type(int index) { return sent_header(index) & 0x7F; }

static int sent_sequence(int index) { return (sent_header(index) >> 8) & 0x7F; }

static int sent_channel_bits(int index) { return sent_header(index) & 0x8080; }

/* The sent world messages a WORLD_NACK looks through. */
static struct net_queued_packet g_world_history[256];

/* Puts a world message of the tick, as the flight kept it with its top bit
 * set, and a mark of 0x100 plus its index, in the world history; size 0 leaves
 * the entry marked empty. */
static void world_history_entry(int index, int tick, int sequence, int size)
{
	struct net_queued_packet *entry = &g_world_history[index];
	int words[3] = {2, (int)(0x80000000u | (unsigned)tick), 0x100 + index};
	memset(entry, 0, sizeof *entry);
	memcpy(entry->payload, words, sizeof words);
	entry->payload_size = (uint32_t)size;
	entry->sequence_byte = (uint8_t)sequence;
}

/* A WORLD_NACK is answered, once a flight has kept its world messages, with
 * the message whose first body word, its top bit cleared, is the tick asked
 * for, found from the history slot to be written next on round to the others
 * and skipping empty entries: resent on the broadcast channel under its own
 * sequence. With none, a NOP in the sequence the packet names is sent instead.
 * Each counts a drop for the sender after its first 20 packets, and is logged
 * with whether the message was found. Before any flight has ended there is no
 * history: the packet is only logged, and the sender gets no slot. */
static void check_pump_world_nack(void)
{
	fresh();
	open_session();
	memset(g_world_history, 0, sizeof g_world_history);
	g_front_state.net_flight_sent_world_message_history = g_world_history;
	g_front_state.net_flight_sent_world_message_write_index = 200;
	world_history_entry(3, 0x55, 9, 12);
	world_history_entry(100, 0x57, 5, 0);
	world_history_entry(150, 0x57, 6, 12);
	world_history_entry(199, 0x58, 12, 12);
	world_history_entry(0, 0x59, 13, 12);
	world_history_entry(250, 0x5A, 14, 12);
	unsigned int slot = net_find_or_create_peer_slot(30);
	peer(slot)->packet_count = 21;
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x55, 7, 0, 0, 2);
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x57, 8, 0, 0, 2);
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x99, 11, 0, 0, 2);
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x58, 13, 0, 0, 2);
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x59, 13, 0, 0, 2);
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x5A, 13, 0, 0, 2);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 6);
	XVT_ASSERT_INT_EQ(sent_type(3), 2);
	XVT_ASSERT_INT_EQ(sent_sequence(3), 12);
	XVT_ASSERT_INT_EQ(sent_word(3, RESEND_BODY + 4), 0x100 + 199);
	XVT_ASSERT_INT_EQ(sent_sequence(4), 13);
	XVT_ASSERT_INT_EQ(sent_word(4, RESEND_BODY + 4), 0x100);
	XVT_ASSERT_INT_EQ(sent_sequence(5), 14);
	XVT_ASSERT_INT_EQ(sent_word(5, RESEND_BODY + 4), 0x100 + 250);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(g_sent[i].to, 30);
		XVT_ASSERT_INT_EQ(g_sent[i].from, LOCAL_ID);
		XVT_ASSERT_INT_EQ(sent_channel_bits(i), 0x80);
		XVT_ASSERT_INT_EQ(g_sent[i].bytes[RESEND_CLASS], 0);
	}
	XVT_ASSERT_INT_EQ(sent_type(0), 2);
	XVT_ASSERT_INT_EQ(sent_sequence(0), 9);
	XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY), (int)0x80000055u);
	XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY + 4), 0x103);
	XVT_ASSERT_INT_EQ(sent_type(1), 2);
	XVT_ASSERT_INT_EQ(sent_sequence(1), 6);
	XVT_ASSERT_INT_EQ(sent_word(1, RESEND_BODY + 4), 0x100 + 150);
	XVT_ASSERT_INT_EQ(sent_type(2), NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(sent_sequence(2), 11);
	XVT_ASSERT_INT_EQ(g_sent[2].size, 6);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_drop_count, 6);
	XVT_ASSERT_INT_EQ(
		count_lines_holding("network.lobby_world_nack", "found=1"), 5);
	XVT_ASSERT_INT_EQ(
		count_lines_holding("network.lobby_world_nack", "found=0"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_world_nack", "tick"), 0x5A);

	/* A player's 20th packet is not yet counted, its 21st is. */
	peer(slot)->packet_count = 20;
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x55, 7, 0, 0, 2);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(peer(slot)->packet_drop_count, 6);

	fresh();
	open_session();
	inbox_control(30, NET_PACKET_WORLD_NACK, 0x55, 7, 0, 0, 2);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(has_peer_slot(30), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_world_nack"), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_world_nack_ignored"), 1);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_world_nack_ignored", "tick"), 0x55);
}

/* A NACK is answered with the packet from the sent history whose sequence and
 * channel it names, resent under that channel's sequence, found from the slot
 * to be written next on round to the others: on the broadcast and group
 * channels by sequence and channel, on the one-player channel only a packet
 * that went to the player who asked. With none, a NOP in that sequence goes on
 * that channel. The sender's last_heard_ms is stamped, and a drop counted after
 * its first 20 packets. Each is logged with whether the packet was found. */
static void check_pump_nack(void)
{
	static const struct {
		int from;
		int sequence;
		int channel;
		int mark; /* 0 for a NOP. */
	} rows[] = {
		{30, 0, 1, 2}, {30, 1, 1, 5}, {30, 0, 2, 3}, {30, 0, 0, 4},
		{40, 0, 1, 1}, {30, 9, 1, 0}, {30, 1, 0, 0},
	};
	fresh();
	open_session();
	send_game_packet(40, NET_PACKET_CHAT, 1);
	send_game_packet(30, NET_PACKET_CHAT, 2);
	send_game_packet(GROUP_ID, NET_PACKET_CHAT, 3);
	send_game_packet(0, NET_PACKET_CHAT, 4);
	send_game_packet(30, NET_PACKET_CHAT, 5);
	empty_recv_queue();
	unsigned int slot = net_find_or_create_peer_slot(30);
	unsigned int quiet = net_find_or_create_peer_slot(40);
	peer(slot)->packet_count = 21;
	peer(slot)->last_heard_ms = 0;
	peer(quiet)->packet_count = 20;
	peer(quiet)->last_heard_ms = 0;
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		inbox_control((DPID)rows[row].from, NET_PACKET_NACK,
			      rows[row].sequence, rows[row].channel, 0, 0, 2);
	}
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 7);
	for (int i = 0; i < 7; ++i) {
		XVT_ASSERT_INT_EQ(g_sent[i].to, rows[i].from);
		XVT_ASSERT_INT_EQ(sent_channel_bits(i), 0x80);
		XVT_ASSERT_INT_EQ(sent_sequence(i), rows[i].sequence);
		XVT_ASSERT_INT_EQ(g_sent[i].bytes[RESEND_CLASS],
				  rows[i].channel);
		if (rows[i].mark != 0) {
			XVT_ASSERT_INT_EQ(sent_type(i), NET_PACKET_CHAT);
			XVT_ASSERT_INT_EQ(sent_word(i, RESEND_BODY),
					  rows[i].mark);
		} else {
			XVT_ASSERT_INT_EQ(sent_type(i), NET_PACKET_NOP);
		}
	}
	XVT_ASSERT_INT_EQ(peer(slot)->packet_drop_count, 6);
	XVT_ASSERT_INT_EQ(peer(quiet)->packet_drop_count, 0);
	XVT_ASSERT_INT_EQ(peer(slot)->last_heard_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(peer(quiet)->last_heard_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_nack"), 7);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_nack", "found=1"),
			  5);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_nack", "found=0"),
			  2);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack", "seq"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack", "channel"), 0);

	/* The search goes round the whole history: a packet kept past the slot
	 * to be written next is found. */
	struct net_queued_packet *kept =
		&g_front_state.net_runtime_sent_history[20];
	g_front_state.net_runtime_sent_history_write_index = 100;
	memcpy(kept, &g_front_state.net_runtime_sent_history[3], sizeof *kept);
	kept->sequence_byte = 7;
	inbox_control(30, NET_PACKET_NACK, 7, 0, 0, 0, 2);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(sent_sequence(0), 7);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY), 4);
}

/* Four KEEPALIVE packets, and what each makes the pump do. */
static void inbox_keepalive(DPID from, int broadcast, int group, int direct)
{
	inbox_control(from, NET_PACKET_KEEPALIVE, broadcast, group, direct, 777,
		      4);
}

/* A KEEPALIVE from the host is answered with a KEEPALIVE_ACK to the host that
 * echoes the time it carries and gives this side's packet, drop and retry
 * counts for it. Any KEEPALIVE makes the pump resend, on each channel whose
 * counter has moved past the sequence the sender expects next, the packet of
 * that sequence, once: on the broadcast and group channels the one sent on that
 * channel, on the one-player channel the one sent to that player. A sequence
 * asked for and not in the history is logged with what was left unanswered,
 * 128 for a channel with nothing to resend. Each is logged with whether it came
 * from the host. */
static void check_pump_keepalive(void)
{
	fresh();
	open_session();
	g_front_state.net_host_player_id = 30;
	send_game_packet(0, NET_PACKET_CHAT, 4);
	send_game_packet(0, NET_PACKET_CHAT, 5);
	send_game_packet(GROUP_ID, NET_PACKET_CHAT, 6);
	send_game_packet(GROUP_ID, NET_PACKET_CHAT, 7);
	send_game_packet(30, NET_PACKET_CHAT, 8);
	send_game_packet(30, NET_PACKET_CHAT, 9);
	send_game_packet(40, NET_PACKET_CHAT, 10);
	empty_recv_queue();
	unsigned int host = net_find_or_create_peer_slot(30);
	peer(host)->packet_count = 7;
	peer(host)->packet_drop_count = 3;
	peer(host)->packet_retry_count = 2;
	peer(host)->last_heard_ms = 0;
	/* The counters stand at 2, 2 and, for player 30, 2. */
	inbox_keepalive(30, 1, 1, 1);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(peer(host)->last_heard_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(g_sent_count, 4);
	XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(0), NET_PACKET_KEEPALIVE_ACK);
	XVT_ASSERT_INT_EQ(sent_length(0), 16);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 777);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 7);
	XVT_ASSERT_INT_EQ(sent_word(0, 12), 3);
	XVT_ASSERT_INT_EQ(sent_word(0, 16), 2);
	static const struct {
		int sequence;
		int channel;
		int mark;
	} resends[] = {{1, 0, 5}, {1, 2, 7}, {1, 1, 9}};
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(g_sent[i + 1].to, 30);
		XVT_ASSERT_INT_EQ(sent_type(i + 1), NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(sent_channel_bits(i + 1), 0x80);
		XVT_ASSERT_INT_EQ(sent_sequence(i + 1), resends[i].sequence);
		XVT_ASSERT_INT_EQ(g_sent[i + 1].bytes[RESEND_CLASS],
				  resends[i].channel);
		XVT_ASSERT_INT_EQ(sent_word(i + 1, RESEND_BODY),
				  resends[i].mark);
	}
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_resend_unavailable"), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);

	/* A sender expecting the sequence each counter stands at is sent
	 * nothing but the host's answer. */
	inbox_keepalive(30, 2, 2, 2);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(sent_header(0), NET_PACKET_KEEPALIVE_ACK);

	/* A player that is not the host gets no answer, and a packet sent to
	 * another player is not resent to it. */
	inbox_keepalive(40, 0, 0, 0);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 3);
	XVT_ASSERT_INT_EQ(sent_sequence(0), 0);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[RESEND_CLASS], 0);
	XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY), 4);
	XVT_ASSERT_INT_EQ(sent_sequence(1), 0);
	XVT_ASSERT_INT_EQ(g_sent[1].bytes[RESEND_CLASS], 2);
	XVT_ASSERT_INT_EQ(sent_word(1, RESEND_BODY), 6);
	XVT_ASSERT_INT_EQ(g_sent[2].bytes[RESEND_CLASS], 1);
	XVT_ASSERT_INT_EQ(sent_word(2, RESEND_BODY), 10);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(g_sent[i].to, 40);
	}

	/* What is not in the history stays unanswered. */
	inbox_keepalive(40, 50, 50, 50);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_resend_unavailable"), 1);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_resend_unavailable", "from"), 40);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_resend_unavailable", "broadcast"),
		50);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_resend_unavailable", "group"), 50);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_resend_unavailable", "direct"), 50);
	inbox_keepalive(40, 1, 50, 2);
	forget_sends();
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_resend_unavailable"), 2);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_resend_unavailable", "broadcast"),
		128);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_resend_unavailable", "group"), 50);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_resend_unavailable", "direct"), 2);
	/* Each channel left unanswered alone is logged too. */
	static const struct {
		int broadcast;
		int group;
		int direct;
	} alone[] = {{50, 1, 0}, {1, 50, 0}, {1, 1, 50}};
	for (int i = 0; i < 3; ++i) {
		inbox_keepalive(40, alone[i].broadcast, alone[i].group,
				alone[i].direct);
		net_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(
			count_lines("network.lobby_resend_unavailable"), 3 + i);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_resend_unavailable",
					     "broadcast"),
				  alone[i].broadcast == 50 ? 50 : 128);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_resend_unavailable", "group"),
			alone[i].group == 50 ? 50 : 128);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_resend_unavailable",
					     "direct"),
				  alone[i].direct == 50 ? 50 : 128);
	}
	XVT_ASSERT_INT_EQ(
		count_lines_holding("network.lobby_keepalive", "host=1"), 2);
	XVT_ASSERT_INT_EQ(
		count_lines_holding("network.lobby_keepalive", "host=0"), 6);
}

/* ------------------------------------------------------------------------ */
/* net_pump_incoming_packets: resent copies, the previous packet missing, and
 * the packet queued. */

/* Marks the entries from index first on as holding a resent copy that a resend
 * request was made for, on a channel and sequence of no packet, all of which a
 * packet queued over them must set again. */
static void poison_queue(int first, int count)
{
	for (int i = 0; i < count; ++i) {
		struct net_queued_packet *entry = queued((first + i) % 1024);
		entry->is_resent_copy = 1;
		entry->nack_retry_count = 3;
		entry->last_nack_ms = 5;
		entry->packet_class = 9;
		entry->sequence_byte = 99;
	}
}

/* Writes a game packet as DirectPlay delivers it into bytes, laid out as the
 * lobby's sends lay it out: the header word, the body's length, the body, and
 * then the trailer, which is its type byte and body. Returns the size. */
static uint32_t build_wire(uint8_t *bytes, uint16_t header, const void *body,
			   uint16_t body_size, int trailer_type,
			   const void *trailer_body, uint16_t trailer_size)
{
	memcpy(bytes, &header, sizeof header);
	memcpy(bytes + 2, &body_size, sizeof body_size);
	memcpy(bytes + 4, body, body_size);
	uint32_t size = 4u + body_size;
	bytes[size++] = (uint8_t)trailer_type;
	memcpy(bytes + size, trailer_body, trailer_size);
	return size + trailer_size;
}

/* The newest sequence received from the slot's player on a channel, 0 the
 * broadcast channel, 1 the one-player channel and 2 the group channel. */
static int newest_received(const struct net_reliable_peer_slot *slot,
			   int channel)
{
	return channel == 0   ? slot->recv_seq_channel_a
	       : channel == 2 ? slot->recv_seq_channel_b
			      : slot->recv_seq_default;
}

static void set_newest_received(struct net_reliable_peer_slot *slot,
				int channel, int sequence)
{
	if (channel == 0) {
		slot->recv_seq_channel_a = sequence;
	} else if (channel == 2) {
		slot->recv_seq_channel_b = sequence;
	} else {
		slot->recv_seq_default = sequence;
	}
}

/* A resent packet is queued as a resent copy with its sender, the channel its
 * marker byte names (0 the broadcast channel, 2 the group channel, any other
 * the one-player channel), its sequence and its body, whatever the entry held
 * before. The channel's newest received sequence moves only when the copy
 * carries the next one, 127 wrapping to 0, and no other channel's moves. The
 * sender's last_heard_ms is stamped and the copy is logged. */
static void check_pump_resent_copies(void)
{
	static const struct {
		int marker;
		int packet_class;
	} rows[] = {{0, 0}, {2, 2}, {1, 1}, {5, 1}};
	/* Each copy's sequence, and the newest received afterwards. */
	static const int copies[][2] = {{7, 127}, {0, 0}, {1, 1}, {3, 1}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int packet[2] = {NET_PACKET_CHAT, 0};
		fresh();
		open_session();
		unsigned int slot = net_find_or_create_peer_slot(30);
		peer(slot)->last_heard_ms = 0;
		int channel = rows[row].packet_class;
		poison_queue(0, 4);
		for (int i = 0; i < 4; ++i) {
			packet[1] = 10 + i;
			XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
						  30, rows[row].marker,
						  copies[i][0], packet,
						  sizeof packet),
					  1);
			replay_send(i, 30);
			net_pump_incoming_packets();
			XVT_ASSERT_INT_EQ(
				g_front_state.net_runtime_recv_queue_count,
				i + 1);
			XVT_ASSERT_INT_EQ(newest_received(peer(slot), channel),
					  copies[i][1]);
			for (int other = 0; other < 3; ++other) {
				if (other != channel) {
					XVT_ASSERT_INT_EQ(
						newest_received(peer(slot),
								other),
						127);
				}
			}
			XVT_ASSERT_INT_EQ(queued(i)->direct_play_id, 30);
			XVT_ASSERT_INT_EQ(queued(i)->packet_class, channel);
			XVT_ASSERT_INT_EQ(queued(i)->is_resent_copy, 1);
			XVT_ASSERT_INT_EQ(queued(i)->nack_retry_count, 0);
			XVT_ASSERT_INT_EQ(queued(i)->last_nack_ms, 0);
			XVT_ASSERT_INT_EQ(queued(i)->payload_size, 8);
			XVT_ASSERT_INT_EQ(queued(i)->sequence_byte,
					  copies[i][0]);
			XVT_ASSERT_INT_EQ(queued_type(i), NET_PACKET_CHAT);
			int mark;
			memcpy(&mark, queued(i)->payload + 4, sizeof mark);
			XVT_ASSERT_INT_EQ(mark, 10 + i);
			XVT_ASSERT_INT_EQ(peer(slot)->last_heard_ms,
					  GetTickCount());
			peer(slot)->last_heard_ms = 0;
		}
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_resent_received"),
				  4);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_resent_received", "from"),
			30);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_resent_received", "type"),
			NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_resent_received", "channel"),
			channel);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_resent_received", "seq"), 3);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_resent_received", "bytes"),
			4);

		/* The newest at 126 moves to 127, which is not a wrap. */
		set_newest_received(peer(slot), channel, 126);
		packet[1] = 14;
		XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
					  30, rows[row].marker, 127, packet,
					  sizeof packet),
				  1);
		replay_send(4, 30);
		net_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(newest_received(peer(slot), channel), 127);
	}
}

/* A resent copy's body is cut to 508 bytes: a body of 508 is queued whole,
 * its last byte too, a longer one cut, with the queue entry's size 512 either
 * way. The write index wraps from 1,023 to 0. */
static void check_pump_resent_copy_sizes(void)
{
	static const int sizes[] = {508, 509, 600};
	uint8_t packet[604];
	fresh();
	open_session();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	int type = NET_PACKET_CHAT;
	memcpy(packet, &type, sizeof type);
	for (int i = 4; i < 604; ++i) {
		packet[i] = (uint8_t)(i * 3);
	}
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
					  30, 1, 10 + i, packet,
					  (unsigned int)sizes[i] + 4),
				  1);
		replay_send(i, 30);
	}
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 3);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 2);
	for (int i = 0; i < 3; ++i) {
		struct net_queued_packet *entry = queued((1023 + i) % 1024);
		XVT_ASSERT_INT_EQ(entry->payload_size, 512);
		XVT_ASSERT_INT_EQ(entry->sequence_byte, 10 + i);
		XVT_ASSERT_INT_EQ(memcmp(entry->payload + 4, packet + 4, 508),
				  0);
	}
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_resent_received"), 3);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_resent_received", "bytes"),
			  508);
}

/* A resent copy of a resync type (60 to 63) has no length word and no
 * trailer: its body is queued after the type, and no earlier packet is looked
 * for. */
static void check_pump_resent_resync_copy(void)
{
	int packet[3] = {NET_PACKET_RESYNC_CHUNK, 0x11223344, 0x55667788};
	fresh();
	open_session();
	XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
				  30, 1, 0, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_sent[0].size, 11);
	replay_send(0, 30);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued(0)->is_resent_copy, 1);
	XVT_ASSERT_INT_EQ(queued_type(0), NET_PACKET_RESYNC_CHUNK);
	XVT_ASSERT_INT_EQ(memcmp(queued(0)->payload + 4, packet + 1, 8), 0);
}

/* When a packet's previous sequence on its channel is new, the packet riding
 * behind it as trailer is queued first, as a packet of that previous sequence
 * and the same channel, and logged with its type. The sender's drop count
 * rises after its first 20 packets, on any channel. Sequence 0's previous one
 * is 127. A NOP trailer queues nothing but still counts the drop. */
static void check_pump_previous_missed(void)
{
	static const struct {
		int to;
		int packet_class;
		int packet_count;
		int drops;
		int first_sequence;
		int trailer;
	} rows[] = {
		{30, 1, 21, 1, 0, NET_PACKET_CHAT},
		{30, 1, 20, 0, 0, NET_PACKET_CHAT},
		{0, 0, 21, 1, 0, NET_PACKET_CHAT},
		{GROUP_ID, 2, 21, 1, 0, NET_PACKET_CHAT},
		{30, 1, 21, 1, 127, NET_PACKET_CHAT},
		{30, 1, 21, 1, 5, NET_PACKET_NOP},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int first = rows[row].first_sequence;
		fresh();
		open_session();
		g_front_state.net_runtime_broadcast_seq_counter = first;
		g_front_state.net_runtime_group_seq_counter = first;
		unsigned int sender_slot =
			net_find_or_create_peer_slot(LOCAL_ID);
		peer(sender_slot)->send_seq = first;
		unsigned int sender = net_find_or_create_peer_slot(30);
		peer(sender)->send_seq = first;
		if (rows[row].trailer == NET_PACKET_CHAT) {
			send_game_packet(rows[row].to, NET_PACKET_CHAT, 10);
		}
		int number =
			send_game_packet(rows[row].to, NET_PACKET_CHAT, 11);
		empty_recv_queue();
		forget_lines();
		replay_send(number, 30);
		peer(sender)->packet_count = rows[row].packet_count;
		if (first == 127) {
			peer(sender)->recv_seq_default = 126;
		}
		g_front_state.net_runtime_recv_queue_read_index = 1023;
		g_front_state.net_runtime_recv_queue_write_index = 1023;
		poison_queue(1023, 2);
		net_pump_incoming_packets();
		int queued_count = rows[row].trailer == NET_PACKET_CHAT ? 2 : 1;
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  queued_count);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_recv_queue_write_index,
			queued_count - 1);
		int newest = queued_count - 1;
		int previous = first == 0 ? 127 : first - 1;
		if (rows[row].trailer == NET_PACKET_CHAT) {
			struct net_queued_packet *entry = queued(1023);
			XVT_ASSERT_INT_EQ(entry->direct_play_id, 30);
			XVT_ASSERT_INT_EQ(entry->packet_class,
					  rows[row].packet_class);
			XVT_ASSERT_INT_EQ(entry->sequence_byte, first);
			XVT_ASSERT_INT_EQ(entry->is_resent_copy, 0);
			XVT_ASSERT_INT_EQ(entry->nack_retry_count, 0);
			XVT_ASSERT_INT_EQ(entry->last_nack_ms, 0);
			XVT_ASSERT_INT_EQ(entry->payload_size, 8);
			XVT_ASSERT_INT_EQ(*(int *)entry->payload,
					  NET_PACKET_CHAT);
			XVT_ASSERT_INT_EQ(*(int *)(entry->payload + 4), 10);
			previous = first;
			newest = 1;
		}
		struct net_queued_packet *last = queued((1023 + newest) % 1024);
		XVT_ASSERT_INT_EQ(last->packet_class, rows[row].packet_class);
		XVT_ASSERT_INT_EQ(last->is_resent_copy, 0);
		XVT_ASSERT_INT_EQ(last->nack_retry_count, 0);
		XVT_ASSERT_INT_EQ(*(int *)(last->payload + 4), 11);
		XVT_ASSERT_INT_EQ(peer(sender)->packet_drop_count,
				  rows[row].drops);
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_packet_missed"),
				  1);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_missed", "from"), 30);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_missed", "channel"),
			rows[row].packet_class);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_missed", "trailer"),
			rows[row].trailer);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_missed", "seq"),
			rows[row].trailer == NET_PACKET_CHAT ? previous
							     : (first - 1));
	}
}

/* A type from 60 to 63 travels with no length word and no trailer, so its
 * packet is queued from the bytes after the header and no earlier packet is
 * looked for; the types on each side, 59 and 64, have both and are. For a
 * jump in sequence the trailer is read and counted as a drop. */
static void check_pump_resync_types_bare(void)
{
	static const struct {
		int type;
		int sent_size;
		int missed;
	} rows[] = {
		{NET_PACKET_SEQUENCE_STATUS, 9, 1},
		{NET_PACKET_RESYNC_CHECKSUMS, 6, 0},
		{NET_PACKET_RESYNC_CHUNK, 6, 0},
		{NET_PACKET_PROBE_REQUEST, 9, 1},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		fresh();
		open_session();
		unsigned int slot = net_find_or_create_peer_slot(30);
		peer(slot)->send_seq = 5;
		peer(slot)->packet_count = 21;
		int number = send_game_packet(30, rows[row].type, 0x1234);
		XVT_ASSERT_INT_EQ(g_sent[number].size, rows[row].sent_size);
		replay_send(number, 30);
		net_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  1);
		XVT_ASSERT_INT_EQ(queued(0)->payload_size, 8);
		XVT_ASSERT_INT_EQ(queued_type(0), rows[row].type);
		XVT_ASSERT_INT_EQ(*(int *)(queued(0)->payload + 4), 0x1234);
		XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 5);
		XVT_ASSERT_INT_EQ(peer(slot)->packet_drop_count,
				  rows[row].missed);
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_packet_missed"),
				  rows[row].missed);
	}
}

/* A packet that is not a repeat is queued with its sender, channel, sequence
 * and body, whatever the entry held before, and logged with its size and the
 * count queued; the write index wraps from 1,023 to 0. The same packet again
 * is not queued and is logged as a repeat. */
static void check_pump_packet_channels(void)
{
	static const struct {
		int to;
		int packet_class;
	} rows[] = {{30, 1}, {0, 0}, {GROUP_ID, 2}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		fresh();
		open_session();
		g_front_state.net_runtime_broadcast_seq_counter = 5;
		g_front_state.net_runtime_group_seq_counter = 5;
		unsigned int slot = net_find_or_create_peer_slot(30);
		peer(slot)->send_seq = 5;
		int number =
			send_game_packet(rows[row].to, NET_PACKET_CHAT, 20);
		empty_recv_queue();
		replay_send(number, 30);
		replay_send(number, 30);
		g_front_state.net_runtime_recv_queue_read_index = 1023;
		g_front_state.net_runtime_recv_queue_write_index = 1023;
		poison_queue(1023, 2);
		net_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  1);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_recv_queue_write_index, 0);
		struct net_queued_packet *entry = queued(1023);
		XVT_ASSERT_INT_EQ(entry->direct_play_id, 30);
		XVT_ASSERT_INT_EQ(entry->packet_class, rows[row].packet_class);
		XVT_ASSERT_INT_EQ(entry->sequence_byte, 5);
		XVT_ASSERT_INT_EQ(entry->is_resent_copy, 0);
		XVT_ASSERT_INT_EQ(entry->nack_retry_count, 0);
		XVT_ASSERT_INT_EQ(entry->last_nack_ms, 0);
		XVT_ASSERT_INT_EQ(entry->payload_size, 8);
		XVT_ASSERT_INT_EQ(*(int *)(entry->payload + 4), 20);
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_packet_received"),
				  1);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_received", "channel"),
			rows[row].packet_class);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_received", "seq"), 5);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_received", "bytes"),
			4);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_received", "queued"),
			1);
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_packet_repeat"),
				  1);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_repeat", "seq"), 5);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_repeat", "channel"),
			rows[row].packet_class);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_repeat", "type"),
			NET_PACKET_CHAT);
	}
}

/* A body over 508 bytes is cut to 508, and a body of 508 kept whole, the
 * queue entry's size 512 either way. A trailer past 508 bytes is cut to 508
 * too. The packet riding behind a long body is found where the body really
 * ends: with its previous sequence new, it is queued under its own type, not
 * made of the body's tail. */
static void check_pump_long_bodies(void)
{
	uint8_t body[600];
	uint8_t bytes[1024];
	for (int i = 0; i < 600; ++i) {
		body[i] = (uint8_t)(i * 7);
	}
	body[508] = 29;
	static const uint8_t trailer_body[4] = {0xAB, 0xCD, 0xEF, 0x01};
	uint16_t header = ONE_PLAYER_BIT | (5 << 8) | NET_PACKET_CHAT;
	static const int sizes[] = {508, 509, 600};
	for (int i = 0; i < 3; ++i) {
		fresh();
		open_session();
		uint32_t size =
			build_wire(bytes, header, body, (uint16_t)sizes[i],
				   NET_PACKET_NOP, trailer_body, 0);
		inbox_bytes(30, LOCAL_ID, bytes, size);
		net_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  1);
		XVT_ASSERT_INT_EQ(queued(0)->payload_size, 512);
		XVT_ASSERT_INT_EQ(queued_type(0), NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(memcmp(queued(0)->payload + 4, body, 508), 0);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_received", "bytes"),
			508);
	}

	fresh();
	open_session();
	uint32_t size = build_wire(bytes, header, body, 600, NET_PACKET_CHAT,
				   trailer_body, sizeof trailer_body);
	inbox_bytes(30, LOCAL_ID, bytes, size);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 4);
	XVT_ASSERT_INT_EQ(queued(0)->payload_size, 8);
	XVT_ASSERT_INT_EQ(queued_type(0), NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(memcmp(queued(0)->payload + 4, trailer_body, 4), 0);
	XVT_ASSERT_INT_EQ(queued(1)->sequence_byte, 5);
	XVT_ASSERT_INT_EQ(queued(1)->payload_size, 512);
	XVT_ASSERT_INT_EQ(memcmp(queued(1)->payload + 4, body, 508), 0);

	fresh();
	open_session();
	uint8_t long_trailer[600];
	for (int i = 0; i < 600; ++i) {
		long_trailer[i] = (uint8_t)(i * 5 + 1);
	}
	size = build_wire(bytes, header, body, 8, NET_PACKET_CHAT, long_trailer,
			  600);
	inbox_bytes(30, LOCAL_ID, bytes, size);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 4);
	XVT_ASSERT_INT_EQ(queued(0)->payload_size, 512);
	XVT_ASSERT_INT_EQ(memcmp(queued(0)->payload + 4, long_trailer, 508), 0);
	XVT_ASSERT_INT_EQ(queued(1)->payload_size, 12);
}

/* ------------------------------------------------------------------------ */
/* net_drop_silent_peers, and the keepalives and silent-peer check the pump
 * runs first. */

/* Puts a ready player in the roster entry. */
static void roster_player(int index, DPID player, int ready)
{
	g_front_state.net_players[index].player_id = player;
	g_front_state.net_players[index].ready_flag = ready;
	if (index >= g_front_state.net_player_count) {
		g_front_state.net_player_count = index + 1;
	}
}

/* On the host, a ready roster player other than the local player and the
 * group that has been silent for over 45,000 ms is sent a KICKED packet,
 * flushed, and a PLAYER_LEFT is queued as if from the player, on the one-player
 * channel with the next sequence after the newest received, 127 wrapping to 0,
 * which becomes the newest; the player's timer restarts and the drop is
 * logged. A player not ready, one silent for exactly 45,000 ms and an empty
 * entry are left alone. With 1,024 queued the drop is only logged and the timer
 * restarts. Returns 1. */
static void check_silent_peer_on_host(void)
{
	fresh();
	open_session();
	g_front_state.net_is_host = 1;
	g_front_state.net_runtime_local_player.ready_flag = 1;
	roster_player(0, LOCAL_ID, 1);
	roster_player(1, 30, 1);
	roster_player(2, 40, 0);
	roster_player(3, 50, 1);
	roster_player(4, GROUP_ID, 1);
	roster_player(6, 60, 1);
	unsigned int silent = net_find_or_create_peer_slot(30);
	unsigned int not_ready = net_find_or_create_peer_slot(40);
	unsigned int edge = net_find_or_create_peer_slot(50);
	unsigned int wraps = net_find_or_create_peer_slot(60);
	unsigned int local = net_find_or_create_peer_slot(LOCAL_ID);
	unsigned int group = net_find_or_create_peer_slot(GROUP_ID);
	xvt_time_advance_host_clock(100 * SECOND_US);
	uint32_t now = GetTickCount();
	peer(silent)->last_heard_ms = now - 46000;
	peer(not_ready)->last_heard_ms = now - 46000;
	peer(edge)->last_heard_ms = now - 45000;
	peer(wraps)->last_heard_ms = now - 45001;
	peer(local)->last_heard_ms = now - 90000;
	peer(group)->last_heard_ms = now - 90000;
	peer(wraps)->recv_seq_default = 126;
	g_front_state.net_runtime_recv_queue_read_index = 1022;
	g_front_state.net_runtime_recv_queue_write_index = 1022;
	poison_queue(1022, 2);
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);

	XVT_ASSERT_INT_EQ(g_sent_count, 4);
	XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(0) & 0x7F, NET_PACKET_PLAYER_KICKED);
	XVT_ASSERT_INT_EQ(sent_header(1) & 0x7F, NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(g_sent[2].to, 60);
	XVT_ASSERT_INT_EQ(sent_header(2) & 0x7F, NET_PACKET_PLAYER_KICKED);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 0);
	static const struct {
		int player;
		int sequence;
	} left[] = {{30, 0}, {60, 127}};
	for (int i = 0; i < 2; ++i) {
		struct net_queued_packet *entry = queued((1022 + i) % 1024);
		XVT_ASSERT_INT_EQ(*(int *)entry->payload,
				  NET_PACKET_PLAYER_LEFT);
		XVT_ASSERT_INT_EQ(entry->direct_play_id, left[i].player);
		XVT_ASSERT_INT_EQ(entry->payload_size, 4);
		XVT_ASSERT_INT_EQ(entry->packet_class, 1);
		XVT_ASSERT_INT_EQ(entry->sequence_byte, left[i].sequence);
		XVT_ASSERT_INT_EQ(entry->is_resent_copy, 0);
		XVT_ASSERT_INT_EQ(entry->nack_retry_count, 0);
		XVT_ASSERT_INT_EQ(entry->last_nack_ms, 0);
	}
	XVT_ASSERT_INT_EQ(peer(silent)->recv_seq_default, 0);
	XVT_ASSERT_INT_EQ(peer(wraps)->recv_seq_default, 127);
	XVT_ASSERT_INT_EQ(peer(silent)->last_heard_ms, now);
	XVT_ASSERT_INT_EQ(peer(wraps)->last_heard_ms, now);
	XVT_ASSERT_INT_EQ(peer(not_ready)->last_heard_ms, now - 46000);
	XVT_ASSERT_INT_EQ(peer(edge)->last_heard_ms, now - 45000);
	XVT_ASSERT_INT_EQ(peer(local)->last_heard_ms, now - 90000);
	XVT_ASSERT_INT_EQ(peer(group)->last_heard_ms, now - 90000);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_silent"), 2);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_departure_queued"), 2);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_silent", "player"),
			  60);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_silent", "ms"),
			  45001);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_departure_queued", "player"), 60);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_departure_queued", "seq"),
			  127);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_departure_queued", "queued"), 2);

	/* The timer restarted, so the next call finds nobody silent. */
	forget_sends();
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);

	/* With 1,024 queued the drop is deferred. */
	peer(silent)->last_heard_ms = now - 46000;
	g_front_state.net_runtime_recv_queue_count = 1024;
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1024);
	XVT_ASSERT_INT_EQ(peer(silent)->last_heard_ms, now);
	XVT_ASSERT_INT_EQ(peer(silent)->recv_seq_default, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_drop_deferred"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_drop_deferred", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_drop_deferred", "queued"),
			  1024);

	/* With 1,023 queued it still goes through. */
	peer(silent)->last_heard_ms = now - 46000;
	g_front_state.net_runtime_recv_queue_count = 1023;
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1024);
	XVT_ASSERT_INT_EQ(peer(silent)->recv_seq_default, 1);
}

/* On the host, a ready roster player that can get no peer slot because the
 * table is full is left alone: nothing is sent or queued, and the full table
 * is logged. */
static void check_silent_peer_without_slot(void)
{
	fresh();
	open_session();
	g_front_state.net_is_host = 1;
	fill_peer_table();
	roster_player(0, LOCAL_ID, 1);
	roster_player(1, 999, 1);
	xvt_time_advance_host_clock(100 * SECOND_US);
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_silent"), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_peer_table_full", "player"),
			  999);
}

/* On a client, a host silent for over 45,000 ms has a HOST_CANCELLED queued as
 * if from it, on the broadcast channel with the next sequence after the newest
 * received, 127 wrapping to 0, which becomes the newest; its timer restarts and
 * the drop is logged. Exactly 45,000 ms is not silent. With 1,024 queued the
 * drop is only logged and the timer restarts. With no peer slot to be had for
 * the host the call returns 0 and queues nothing; otherwise it returns 1. */
static void check_silent_host_on_client(void)
{
	fresh();
	open_session();
	g_front_state.net_host_player_id = 30;
	unsigned int host = net_find_or_create_peer_slot(30);
	xvt_time_advance_host_clock(100 * SECOND_US);
	uint32_t now = GetTickCount();
	peer(host)->last_heard_ms = now - 45000;
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_host_silent"), 0);

	peer(host)->last_heard_ms = now - 45001;
	peer(host)->recv_seq_channel_a = 127;
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	poison_queue(1023, 1);
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 0);
	struct net_queued_packet *entry = queued(1023);
	XVT_ASSERT_INT_EQ(*(int *)entry->payload, NET_PACKET_HOST_CANCELLED);
	XVT_ASSERT_INT_EQ(entry->direct_play_id, 30);
	XVT_ASSERT_INT_EQ(entry->payload_size, 4);
	XVT_ASSERT_INT_EQ(entry->packet_class, 0);
	XVT_ASSERT_INT_EQ(entry->sequence_byte, 0);
	XVT_ASSERT_INT_EQ(entry->is_resent_copy, 0);
	XVT_ASSERT_INT_EQ(entry->nack_retry_count, 0);
	XVT_ASSERT_INT_EQ(entry->last_nack_ms, 0);
	XVT_ASSERT_INT_EQ(peer(host)->recv_seq_channel_a, 0);
	XVT_ASSERT_INT_EQ(peer(host)->last_heard_ms, now);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_host_silent", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_host_silent", "ms"), 45001);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_departure_queued", "seq"),
			  0);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_departure_queued", "queued"), 1);

	peer(host)->last_heard_ms = now - 46000;
	peer(host)->recv_seq_channel_a = 126;
	g_front_state.net_runtime_recv_queue_count = 1023;
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1024);
	XVT_ASSERT_INT_EQ(peer(host)->recv_seq_channel_a, 127);

	peer(host)->last_heard_ms = now - 46000;
	g_front_state.net_runtime_recv_queue_count = 1024;
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1024);
	XVT_ASSERT_INT_EQ(peer(host)->recv_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(peer(host)->last_heard_ms, now);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_drop_deferred"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_drop_deferred", "queued"),
			  1024);

	/* A host whose slot cannot be had: the table is full of others. */
	fresh();
	open_session();
	fill_peer_table();
	g_front_state.net_host_player_id = 999;
	XVT_ASSERT_INT_EQ(net_drop_silent_peers(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
}

/* The pump sends the keepalives that are due, then checks for silent peers,
 * before it reads anything: a roster player idle for over 3,000 ms is sent a
 * KEEPALIVE and, silent for over 45,000 ms, a KICKED, and its departure is
 * queued ahead of what DirectPlay delivers. */
static void check_pump_keepalives_and_silent_peers(void)
{
	int body = 5;
	fresh();
	open_session();
	g_front_state.net_is_host = 1;
	roster_player(0, LOCAL_ID, 1);
	roster_player(1, 30, 1);
	net_find_or_create_peer_slot(30);
	xvt_time_advance_host_clock(46 * SECOND_US);
	inbox_packet(40, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	net_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(0), NET_PACKET_KEEPALIVE);
	XVT_ASSERT_INT_EQ(sent_header(1) & 0x7F, NET_PACKET_PLAYER_KICKED);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued_type(0), NET_PACKET_PLAYER_LEFT);
	XVT_ASSERT_INT_EQ(queued(0)->direct_play_id, 30);
	XVT_ASSERT_INT_EQ(queued_type(1), NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(queued(1)->direct_play_id, 40);
}

/* ------------------------------------------------------------------------ */
/* net_dequeue_incoming_packet. */

static DPID g_got_sender;
static uint32_t g_got_size;

/* Calls the dequeue and keeps the sender and size it hands back. */
static const uint8_t *dequeue(void)
{
	g_got_sender = 0;
	g_got_size = 0;
	return net_dequeue_incoming_packet(&g_got_sender, &g_got_size);
}

/* The word after the type in a packet the dequeue handed out. */
static int mark_of(const uint8_t *packet)
{
	int mark;
	memcpy(&mark, packet + 4, sizeof mark);
	return mark;
}

/* Appends a game packet to the receive queue as the pump would queue it: from
 * the sender, on the channel packet_class, with the sequence, as a resent copy
 * or not, of the type, with the mark as its one body word. */
static void push_packet(DPID sender, int packet_class, int sequence, int resent,
			int type, int mark)
{
	int index = g_front_state.net_runtime_recv_queue_write_index;
	struct net_queued_packet *entry = queued(index);
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = sender;
	entry->packet_class = (uint8_t)packet_class;
	entry->sequence_byte = (uint8_t)sequence;
	entry->is_resent_copy = (uint8_t)resent;
	entry->payload_size = 8;
	memcpy(entry->payload, &type, sizeof type);
	memcpy(entry->payload + 4, &mark, sizeof mark);
	g_front_state.net_runtime_recv_queue_write_index = (index + 1) % 1024;
	++g_front_state.net_runtime_recv_queue_count;
}

/* The last sequence delivered from the slot's player on a channel, 0 the
 * broadcast channel, 1 the one-player channel and 2 the group channel. */
static int *delivered_on(struct net_reliable_peer_slot *slot, int channel)
{
	return channel == 0   ? &slot->last_delivered_seq_channel_a
	       : channel == 2 ? &slot->last_delivered_seq_channel_b
			      : &slot->last_delivered_seq_default;
}

/* The number of NACK packets sent to player 30 so far, and the sequence the
 * last one asks for. */
static int nacks_sent(void)
{
	int count = 0;
	for (int i = 0; i < g_sent_count; ++i) {
		if (sent_type(i) == NET_PACKET_NACK) {
			++count;
		}
	}
	return count;
}

/* Packets arriving through the pump are returned in sequence: the dequeue
 * pumps first, so a packet waiting in DirectPlay comes back from one call. The
 * packet is handed out with its sender and size, the sender's last delivered
 * sequence on that channel moves to it, the delivery count rises, the activity
 * time is stamped and the delivery is logged with its channel, sequence, type,
 * size and the count still queued. The next sequence after 127 is 0. */
static void check_dequeue_in_order(void)
{
	fresh();
	open_session();
	XVT_ASSERT_TRUE(dequeue() == NULL);
	int body = 0x01020304;
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	xvt_time_advance_host_clock(SECOND_US);
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(g_got_sender, 30);
	XVT_ASSERT_INT_EQ(g_got_size, 8);
	XVT_ASSERT_INT_EQ(*(const int *)packet, NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(mark_of(packet), body);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	unsigned int slot = net_find_or_create_peer_slot(30);
	XVT_ASSERT_INT_EQ(peer(slot)->last_delivered_seq_default, 0);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_count, 1);
	XVT_ASSERT_INT_EQ(peer(slot)->last_activity_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_delivered",
					      "path=\"in_order\""),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "player"), 30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "channel"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "seq"), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "type"),
			  NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "bytes"), 8);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "queued"), 0);

	static const struct {
		int packet_class;
		int last;
		int sequence;
	} rows[] = {{0, 127, 0},   {1, 127, 0},	  {2, 127, 0},
		    {0, 5, 6},	   {1, 5, 6},	  {2, 5, 6},
		    {0, 126, 127}, {1, 126, 127}, {2, 126, 127}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		fresh();
		slot = net_find_or_create_peer_slot(30);
		for (int channel = 0; channel < 3; ++channel) {
			*delivered_on(peer(slot), channel) = 40 + channel;
		}
		*delivered_on(peer(slot), rows[row].packet_class) =
			rows[row].last;
		peer(slot)->last_heard_ms = 0;
		g_front_state.net_runtime_recv_queue_read_index = 1023;
		g_front_state.net_runtime_recv_queue_write_index = 1023;
		push_packet(30, rows[row].packet_class, rows[row].sequence, 0,
			    NET_PACKET_CHAT, 77);
		packet = dequeue();
		XVT_ASSERT_TRUE(packet != NULL);
		XVT_ASSERT_INT_EQ(mark_of(packet), 77);
		for (int channel = 0; channel < 3; ++channel) {
			XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), channel),
					  channel == rows[row].packet_class
						  ? rows[row].sequence
						  : 40 + channel);
		}
		XVT_ASSERT_INT_EQ(peer(slot)->packet_count, 1);
		XVT_ASSERT_INT_EQ(peer(slot)->last_heard_ms, GetTickCount());
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  0);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_recv_queue_read_index, 0);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_delivered", "channel"),
			rows[row].packet_class);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "seq"),
				  rows[row].sequence);
	}
}

/* The dequeue sends the keepalives that are due, after the pump: a roster
 * player idle for over 3,000 ms is sent one (stamped as sent even where there
 * is no DirectPlay to send through), and only once per call. */
static void check_dequeue_sends_keepalives(void)
{
	fresh();
	roster_player(0, LOCAL_ID, 1);
	roster_player(1, 30, 1);
	unsigned int slot = net_find_or_create_peer_slot(30);
	xvt_time_advance_host_clock(4 * SECOND_US);
	peer(slot)->last_activity_ms = 0;
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(peer(slot)->last_activity_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_keepalive_sent"), 1);

	fresh();
	open_session();
	roster_player(0, LOCAL_ID, 1);
	roster_player(1, 30, 1);
	slot = net_find_or_create_peer_slot(30);
	xvt_time_advance_host_clock(4 * SECOND_US);
	peer(slot)->last_activity_ms = 0;
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(sent_header(0), NET_PACKET_KEEPALIVE);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
}

/* A DirectPlay system message is handed out only from the head of the queue,
 * with sender 0 and its size, as queued; one further back waits, and the scan
 * goes on past it, round the end of the ring, to the packets behind. */
static void check_dequeue_system_messages(void)
{
	fresh();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	for (int i = 0; i < 2; ++i) {
		struct net_queued_packet *entry = queued((1023 + i) % 1024);
		memset(entry, 0, sizeof *entry);
		entry->payload_size = 12;
		entry->payload[0] = (uint8_t)(0x30 + i);
	}
	g_front_state.net_runtime_recv_queue_write_index = 1;
	g_front_state.net_runtime_recv_queue_count = 2;
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(g_got_sender, 0);
	XVT_ASSERT_INT_EQ(g_got_size, 12);
	XVT_ASSERT_INT_EQ(packet[0], 0x30);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_read_index, 0);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(packet[0], 0x31);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_read_index, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);

	/* A packet with a gap at the head, a system message behind it, then a
	 * packet from a player whose next one it is. */
	fresh();
	open_session();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	push_packet(30, 1, 5, 0, NET_PACKET_CHAT, 1);
	struct net_queued_packet *system = queued(0);
	memset(system, 0, sizeof *system);
	system->payload_size = 12;
	g_front_state.net_runtime_recv_queue_write_index = 1;
	++g_front_state.net_runtime_recv_queue_count;
	push_packet(40, 1, 0, 0, NET_PACKET_CHAT, 2);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(g_got_sender, 40);
	XVT_ASSERT_INT_EQ(mark_of(packet), 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);

	/* The same with the system message on the last place of the ring. */
	fresh();
	open_session();
	g_front_state.net_runtime_recv_queue_read_index = 1022;
	g_front_state.net_runtime_recv_queue_write_index = 1022;
	push_packet(30, 1, 5, 0, NET_PACKET_CHAT, 1);
	system = queued(1023);
	memset(system, 0, sizeof *system);
	system->payload_size = 12;
	g_front_state.net_runtime_recv_queue_write_index = 0;
	++g_front_state.net_runtime_recv_queue_count;
	push_packet(40, 1, 0, 0, NET_PACKET_CHAT, 2);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(g_got_sender, 40);
}

/* A packet of a type below 51, which belongs to the flight, is dropped from
 * the queue and logged with its type and whether it was a resent copy. Its
 * sequence counts as delivered on its channel unless it is a resent copy. It
 * is dropped from the head or from further back, and the scan goes on. */
static void check_dequeue_drops_flight_packets(void)
{
	static const int channels[] = {0, 1, 2};
	for (int i = 0; i < 3; ++i) {
		fresh();
		unsigned int slot = net_find_or_create_peer_slot(30);
		for (int channel = 0; channel < 3; ++channel) {
			*delivered_on(peer(slot), channel) = 40 + channel;
		}
		push_packet(30, channels[i], 7, 0, NET_PACKET_WORLD_MESSAGE, 1);
		push_packet(30, channels[i], 9, 1, NET_PACKET_REMOTE_INPUT, 2);
		push_packet(30, channels[i], 12, 0, NET_PACKET_WORLD_MESSAGE,
			    3);
		XVT_ASSERT_TRUE(dequeue() == NULL);
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  0);
		for (int channel = 0; channel < 3; ++channel) {
			XVT_ASSERT_INT_EQ(
				*delivered_on(peer(slot), channel),
				channel == channels[i] ? 12 : 40 + channel);
		}
		XVT_ASSERT_INT_EQ(
			count_lines("network.lobby_flight_packet_dropped"), 3);
		XVT_ASSERT_INT_EQ(count_lines_holding(
					  "network.lobby_flight_packet_dropped",
					  "resent=1"),
				  1);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_flight_packet_dropped",
				   "channel"),
			channels[i]);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_flight_packet_dropped",
				   "type"),
			NET_PACKET_WORLD_MESSAGE);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_flight_packet_dropped",
				   "seq"),
			12);
	}

	/* A flight packet at the head on the last place of the ring: the scan
	 * goes on round to the packet at the first place. */
	fresh();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	push_packet(30, 1, 0, 0, NET_PACKET_WORLD_MESSAGE, 1);
	push_packet(30, 1, 1, 0, NET_PACKET_CHAT, 2);
	const uint8_t *next = dequeue();
	XVT_ASSERT_TRUE(next != NULL);
	XVT_ASSERT_INT_EQ(mark_of(next), 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);

	/* Behind a packet that waits, the last type below 51 is dropped and 51
	 * is not: it is delivered, as the next one after the two dropped. */
	fresh();
	open_session();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 6;
	push_packet(40, 1, 5, 0, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 7, 0, NET_PACKET_WORLD_MESSAGE, 2);
	push_packet(30, 1, 8, 0, NET_PACKET_WORLD_NACK - 1, 3);
	push_packet(30, 1, 9, 0, NET_PACKET_WORLD_NACK, 4);
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(g_got_sender, 30);
	XVT_ASSERT_INT_EQ(mark_of(packet), 4);
	XVT_ASSERT_INT_EQ(*(const int *)packet, NET_PACKET_WORLD_NACK);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 5);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_flight_packet_dropped"),
			  2);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), 1), 9);
}

/* A packet 1 to 28 behind the sequence expected next on its channel is stale
 * and goes at once, logged as stale; one 29 behind counts as far ahead and
 * stays. A packet from a sender that gets no peer slot, the table being full,
 * goes at once too, logged as having no peer, and the packets behind it are
 * still looked at. */
static void check_dequeue_drops_stale_and_slotless(void)
{
	fresh();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 50;
	push_packet(30, 1, 50, 0, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 23, 0, NET_PACKET_CHAT, 2);
	push_packet(30, 1, 22, 0, NET_PACKET_CHAT, 3);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued(2)->sequence_byte, 22);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_packet_dropped",
					      "reason=\"stale\""),
			  2);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_dropped", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_dropped", "channel"),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_dropped", "seq"),
			  23);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_packet_dropped", "expected"), 51);

	/* A stale packet behind one that waits goes too. */
	fresh();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 50;
	push_packet(30, 1, 53, 0, NET_PACKET_CHAT, 1);
	push_packet(40, 1, 5, 0, NET_PACKET_CHAT, 2);
	push_packet(40, 1, 126, 0, NET_PACKET_CHAT, 3);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	push_packet(30, 1, 50, 0, NET_PACKET_CHAT, 4);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 53);
	XVT_ASSERT_INT_EQ(queued(1)->sequence_byte, 5);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_packet_dropped",
					      "reason=\"stale\""),
			  2);

	/* A packet 100 or more ahead is stale too; 99 ahead is not. */
	fresh();
	slot = net_find_or_create_peer_slot(30);
	push_packet(30, 1, 100, 0, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 99, 0, NET_PACKET_CHAT, 2);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued(1)->sequence_byte, 99);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_packet_dropped",
					      "reason=\"stale\""),
			  1);

	fresh();
	fill_peer_table();
	push_packet(999, 1, 0, 0, NET_PACKET_CHAT, 1);
	push_packet(100, 1, 0, 0, NET_PACKET_CHAT, 2);
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(g_got_sender, 100);
	XVT_ASSERT_INT_EQ(mark_of(packet), 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_packet_dropped",
					      "reason=\"no_peer\""),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_dropped", "player"),
			  999);
}

/* Per call the dequeue stops looking at a peer's packets once more than 90 of
 * them, less the size of each gap it found, have been seen, and logs each one
 * it passes over; packets of other peers behind them are still looked at. */
static void check_dequeue_defers_busy_peer(void)
{
	/* The gap the first packet leaves, how many are queued after it, and
	 * how many are passed over: the first packet takes the gap off the 90
	 * allowed, each other one is seen against what is left. */
	static const struct {
		int gap;
		int queued;
		int deferred;
	} rows[] = {{1, 95, 5}, {10, 95, 14}, {95, 20, 19}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int gap = rows[row].gap;
		int count = rows[row].queued;
		fresh();
		unsigned int slot = net_find_or_create_peer_slot(30);
		*delivered_on(peer(slot), 1) = 0;
		/* The packets start so that one passed over sits on the last
		 * place of the ring. */
		int first_deferred = 1 + (count - rows[row].deferred);
		int start = (1023 - (first_deferred - 1) + 1024) % 1024;
		g_front_state.net_runtime_recv_queue_read_index = start;
		g_front_state.net_runtime_recv_queue_write_index = start;
		for (int k = 1; k <= count; ++k) {
			push_packet(30, 1, gap + k, 0, NET_PACKET_CHAT, k);
		}
		push_packet(40, 1, 0, 0, NET_PACKET_CHAT, 200);
		queued((start + 1) % 1024)->nack_retry_count = 3;
		queued((start + 1) % 1024)->last_nack_ms = 9;
		const uint8_t *packet = dequeue();
		XVT_ASSERT_TRUE(packet != NULL);
		XVT_ASSERT_INT_EQ(g_got_sender, 40);
		XVT_ASSERT_INT_EQ(mark_of(packet), 200);
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  count);
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_peer_deferred"),
				  rows[row].deferred);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_peer_deferred", "player"),
			30);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_peer_deferred", "seen"),
			count - rows[row].deferred);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_peer_deferred", "allowance"),
			gap > 90 ? 0 : 90 - gap);
		/* A packet that leaves no gap of its own has no request
		 * pending, unless it was passed over unlooked at. */
		XVT_ASSERT_INT_EQ(queued((start + 1) % 1024)->nack_retry_count,
				  gap > 90 ? 3 : 0);
		XVT_ASSERT_INT_EQ(queued((start + 1) % 1024)->last_nack_ms,
				  gap > 90 ? 9 : 0);
	}
}

/* A packet that leaves a gap asks its sender for each missing sequence, with
 * a NACK carrying the sequence and the channel, once at the first sight and
 * counts a gap on the sender after its first 20 packets. It is logged with
 * the packet's resend requests so far and the sender's gap count. The same
 * packet again within a second asks for nothing; after more than a second it
 * asks again, up to 20 more times. The call returns nothing meanwhile. */
static void check_dequeue_asks_for_missing(void)
{
	fresh();
	open_session();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 2;
	peer(slot)->packet_count = 21;
	push_packet(30, 1, 5, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_INT_EQ(g_sent[i].to, 30);
		XVT_ASSERT_INT_EQ(sent_header(i), NET_PACKET_NACK);
		XVT_ASSERT_INT_EQ(sent_length(i), 8);
		XVT_ASSERT_INT_EQ(sent_word(i, 4), 3 + i);
		XVT_ASSERT_INT_EQ(sent_word(i, 8), 1);
	}
	XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 1);
	XVT_ASSERT_INT_EQ(queued(0)->last_nack_ms, (int)GetTickCount());
	XVT_ASSERT_INT_EQ(peer(slot)->packet_retry_count, 2);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_nack_sent"), 2);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "player"), 30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "channel"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "seq"), 4);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "retries"), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "gaps"), 2);

	XVT_ASSERT_TRUE(dequeue() == NULL);
	xvt_time_advance_host_clock(1000 * MS_US);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	xvt_time_advance_host_clock(MS_US);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 4);
	XVT_ASSERT_INT_EQ(sent_word(2, 4), 3);
	XVT_ASSERT_INT_EQ(sent_word(3, 4), 4);
	XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 2);
	XVT_ASSERT_INT_EQ(queued(0)->last_nack_ms, (int)GetTickCount());
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "retries"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "channel"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_sent", "seq"), 4);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_retry_count, 2);

	/* The sequences asked for count on round the end: from 126 the next
	 * is 127, from 127 it is 0. */
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 126;
	push_packet(30, 1, 1, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 127);
	XVT_ASSERT_INT_EQ(sent_word(1, 4), 0);
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	push_packet(30, 1, 2, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 0);
	XVT_ASSERT_INT_EQ(sent_word(1, 4), 1);
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 125;
	push_packet(30, 1, 1, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 3);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 126);
	XVT_ASSERT_INT_EQ(sent_word(1, 4), 127);
	XVT_ASSERT_INT_EQ(sent_word(2, 4), 0);

	/* A sender at 20 packets is not yet counted. */
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 0) = 2;
	peer(slot)->packet_count = 20;
	push_packet(30, 0, 4, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 0);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_retry_count, 0);
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 2) = 2;
	push_packet(30, 2, 4, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 2);
}

/* After its sender has been asked 20 more times, a gap is given up: the first
 * queued packet after it is delivered, the gap skipped. That is the packet
 * itself, or a resent copy of an earlier sequence. The give-up is logged with
 * the first sequence missed and the resend requests made. */
static void gives_up_on_gap_on(int packet_class)
{
	fresh();
	open_session();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), packet_class) = 2;
	push_packet(30, packet_class, 4, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	for (int round = 0; round < 20; ++round) {
		xvt_time_advance_host_clock(1001 * MS_US);
		XVT_ASSERT_TRUE(dequeue() == NULL);
	}
	XVT_ASSERT_INT_EQ(nacks_sent(), 21);
	XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 21);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_nack_gave_up"), 0);
	xvt_time_advance_host_clock(1001 * MS_US);
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 1);
	XVT_ASSERT_INT_EQ(nacks_sent(), 21);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), packet_class), 4);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_count, 1);
	XVT_ASSERT_INT_EQ(peer(slot)->last_activity_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_nack_gave_up"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_gave_up", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_gave_up", "channel"),
			  packet_class);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_gave_up", "first"), 3);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_gave_up", "retries"),
			  21);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_delivered",
					      "path=\"past_gap\""),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "seq"), 4);

	/* A resent copy of an earlier sequence than the packet's own comes
	 * first, and the packet itself next. */
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), packet_class) = 2;
	push_packet(30, packet_class, 5, 0, NET_PACKET_CHAT, 1);
	push_packet(30, packet_class, 4, 1, NET_PACKET_CHAT, 2);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(nacks_sent(), 1);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 3);
	for (int round = 0; round < 20; ++round) {
		xvt_time_advance_host_clock(1001 * MS_US);
		XVT_ASSERT_TRUE(dequeue() == NULL);
	}
	xvt_time_advance_host_clock(1001 * MS_US);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 2);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), packet_class), 4);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_count, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "seq"), 4);
	XVT_ASSERT_INT_EQ(g_got_sender, 30);
	XVT_ASSERT_INT_EQ(g_got_size, 8);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 1);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), packet_class), 5);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_delivered",
					      "path=\"in_order\""),
			  1);
}

static void check_dequeue_gives_up_on_gap(void)
{
	for (int packet_class = 0; packet_class < 3; ++packet_class) {
		gives_up_on_gap_on(packet_class);
	}
}

/* A gap that runs round the end of the sequences, 127 then 0, is given up the
 * same way: a copy of 0 queued on the first place of the ring comes first,
 * then the packet, whose sequence 1 follows it. */
static void check_dequeue_gives_up_across_wrap(void)
{
	fresh();
	open_session();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 126;
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	push_packet(30, 1, 1, 0, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 0, 1, NET_PACKET_CHAT, 2);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(nacks_sent(), 1);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 127);
	for (int round = 0; round < 20; ++round) {
		xvt_time_advance_host_clock(1001 * MS_US);
		XVT_ASSERT_TRUE(dequeue() == NULL);
	}
	xvt_time_advance_host_clock(1001 * MS_US);
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 2);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), 1), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_gave_up", "first"),
			  127);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 1);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), 1), 1);

	/* A copy of 127 between 126 and 0 is the one delivered. */
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 125;
	push_packet(30, 1, 1, 0, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 127, 1, NET_PACKET_CHAT, 2);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(nacks_sent(), 2);
	for (int round = 0; round < 20; ++round) {
		xvt_time_advance_host_clock(1001 * MS_US);
		XVT_ASSERT_TRUE(dequeue() == NULL);
	}
	xvt_time_advance_host_clock(1001 * MS_US);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 2);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), 1), 127);

	/* Without a copy the packet itself is delivered, past the wrap. */
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 126;
	push_packet(30, 1, 1, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	for (int round = 0; round < 20; ++round) {
		xvt_time_advance_host_clock(1001 * MS_US);
		XVT_ASSERT_TRUE(dequeue() == NULL);
	}
	xvt_time_advance_host_clock(1001 * MS_US);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 1);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), 1), 1);
}

/* With the long-timeout mode on, a gap's sender is asked once, and after 20
 * seconds, not before, the gap is given up and asked about no more. */
static void check_dequeue_long_timeout_mode(void)
{
	fresh();
	open_session();
	g_front_state.net_reliable_retry_long_timeout_mode = 1;
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 2;
	push_packet(30, 1, 4, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(nacks_sent(), 1);
	xvt_time_advance_host_clock(5000 * MS_US);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	xvt_time_advance_host_clock(15000 * MS_US);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_nack_gave_up"), 0);
	xvt_time_advance_host_clock(MS_US);
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 1);
	XVT_ASSERT_INT_EQ(nacks_sent(), 1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_nack_gave_up"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_nack_gave_up", "retries"),
			  1);
}

/* A resent copy that arrives in time fills the gap: the copies of the
 * sequences missing are delivered in order, one to a call, each from behind
 * the packet that revealed the gap, and the packet follows in order. The
 * sender is asked for nothing it has copies of; a copy of only the later
 * sequence leaves the earlier one asked for. The packet's resend count is
 * cleared once at most one sequence is still missing. */
static void gap_filled_on(int packet_class)
{
	fresh();
	open_session();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), packet_class) = 2;
	peer(slot)->packet_count = 21;
	push_packet(30, packet_class, 5, 0, NET_PACKET_CHAT, 1);
	push_packet(30, packet_class, 3, 1, NET_PACKET_CHAT, 2);
	push_packet(30, packet_class, 4, 1, NET_PACKET_CHAT, 3);
	queued(0)->nack_retry_count = 2;
	queued(0)->last_nack_ms = 7;
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(g_got_sender, 30);
	XVT_ASSERT_INT_EQ(mark_of(packet), 2);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), packet_class), 3);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_count, 22);
	XVT_ASSERT_INT_EQ(peer(slot)->last_activity_ms, GetTickCount());
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 2);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_delivered",
					      "path=\"gap_filled\""),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "seq"), 3);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "queued"), 2);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 3);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), packet_class), 4);
	XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 0);
	XVT_ASSERT_INT_EQ(queued(0)->last_nack_ms, 0);
	packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 1);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), packet_class), 5);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(peer(slot)->packet_retry_count, 0);
}

static void check_dequeue_gap_filled_by_copies(void)
{
	for (int packet_class = 0; packet_class < 3; ++packet_class) {
		gap_filled_on(packet_class);
	}

	/* A copy kept on the first place of the ring is found. */
	fresh();
	open_session();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 2;
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	push_packet(30, 1, 4, 0, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 3, 1, NET_PACKET_CHAT, 2);
	const uint8_t *packet = dequeue();
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(mark_of(packet), 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);

	/* A copy on another channel does not fill the gap. */
	fresh();
	open_session();
	slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 2;
	push_packet(30, 1, 5, 0, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 4, 1, NET_PACKET_CHAT, 3);
	push_packet(30, 0, 3, 1, NET_PACKET_CHAT, 2);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(nacks_sent(), 1);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 3);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 3);
	/* The copy of the next one then comes in time. */
	push_packet(30, 1, 3, 1, NET_PACKET_CHAT, 4);
	const uint8_t *filled = dequeue();
	XVT_ASSERT_TRUE(filled != NULL);
	XVT_ASSERT_INT_EQ(mark_of(filled), 4);
	XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), 1), 3);
}

/* With 1,023 packets or more queued and nothing deliverable in order, the
 * first one already asked about is delivered past its gap, whatever channel;
 * the queue's pressure is logged. With fewer queued, waiting is logged as
 * normal and the second pass does not run. */
static void check_dequeue_under_pressure(void)
{
	static const int channels[] = {0, 1, 2};
	for (int i = 0; i < 3; ++i) {
		fresh();
		unsigned int slot = net_find_or_create_peer_slot(30);
		for (int channel = 0; channel < 3; ++channel) {
			*delivered_on(peer(slot), channel) = 127;
		}
		/* The place just before the first packet holds an old packet
		 * that is no longer queued. */
		g_front_state.net_runtime_recv_queue_read_index = 500;
		g_front_state.net_runtime_recv_queue_write_index = 499;
		push_packet(30, channels[i], 10, 0, NET_PACKET_CHAT, 777);
		queued(499)->nack_retry_count = 1;
		g_front_state.net_runtime_recv_queue_write_index = 500;
		g_front_state.net_runtime_recv_queue_count = 0;
		for (int k = 0; k < 1023; ++k) {
			push_packet(30, channels[i], 10, 0, NET_PACKET_CHAT, k);
		}
		const uint8_t *packet = dequeue();
		XVT_ASSERT_TRUE(packet != NULL);
		XVT_ASSERT_INT_EQ(mark_of(packet), 0);
		XVT_ASSERT_INT_EQ(g_got_sender, 30);
		XVT_ASSERT_INT_EQ(g_got_size, 8);
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  1022);
		for (int channel = 0; channel < 3; ++channel) {
			XVT_ASSERT_INT_EQ(*delivered_on(peer(slot), channel),
					  channel == channels[i] ? 10 : 127);
		}
		XVT_ASSERT_INT_EQ(peer(slot)->packet_count, 1);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_queue_pressure", "queued"),
			1023);
		XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_delivered",
						      "path=\"queue_full\""),
				  1);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_delivered", "seq"),
				  10);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_delivered", "queued"), 1022);
	}

	/* Below the limit the waiting is logged as normal. */
	fresh();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 2;
	push_packet(30, 1, 10, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.lobby_receive_waiting",
					      "pass=\"normal\""),
			  1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_queue_pressure"), 0);
}

/* ------------------------------------------------------------------------ */
/* net_handle_direct_play_system_message, and the roster refresh it runs. */

enum {
	PLAYER_TYPE = 1,
	GROUP_TYPE = 0,
};

/* Makes EnumPlayers list one more player. */
static void list_player(DPID id, uint32_t type, const char *short_name,
			const char *long_name)
{
	XVT_ASSERT_TRUE(g_listed_count < 48);
	g_listed[g_listed_count].id = id;
	g_listed[g_listed_count].type = type;
	g_listed[g_listed_count].short_name = short_name;
	g_listed[g_listed_count].long_name = long_name;
	++g_listed_count;
}

/* Hands the handler a create or destroy message for a player or group. */
static void send_system_message(int type, int player_type, DPID player)
{
	int message[8] = {type, player_type, (int)player};
	net_handle_direct_play_system_message(type, message);
}

/* The state the system-message checks start from: a session that is not lost,
 * the local player first in the roster, listed by DirectPlay too. */
static void system_world(int host)
{
	fresh();
	xvt_network_session_on_close();
	open_session();
	g_front_state.net_is_host = host;
	g_front_state.net_host_player_id = host ? LOCAL_ID : 30;
	roster_player(0, LOCAL_ID, 1);
	list_player(LOCAL_ID, PLAYER_TYPE, "Luke", "L0");
}

/* The names in a roster entry, short then long. */
static int roster_named(int index, const char *short_name,
			const char *long_name)
{
	return strcmp(g_front_state.net_players[index].player_name,
		      short_name) == 0 &&
	       strcmp(g_front_state.net_players[index].long_name, long_name) ==
		       0;
}

/* On the host, a player created other than the host itself has the roster
 * rebuilt from DirectPlay's and is sent a SEQUENCE_STATUS, flushed: the count
 * of players, the count of peer slots, the host's time in ms and, for each
 * peer slot, its id and the sequences delivered and received on the broadcast
 * and group channels. The join is logged. The host's own creation, and a
 * group, change nothing. A client only rebuilds its roster, and nothing is
 * sent. */
static void check_system_player_created(void)
{
	system_world(1);
	list_player(40, PLAYER_TYPE, "Biggs", "L2");
	list_player(30, PLAYER_TYPE, "Wedge", "L3");
	roster_player(1, 30, 1);
	roster_player(2, 50, 1);
	unsigned int first = net_find_or_create_peer_slot(40);
	unsigned int second = net_find_or_create_peer_slot(GROUP_ID);
	peer(first)->last_delivered_seq_channel_a = 5;
	peer(first)->last_delivered_seq_channel_b = 6;
	peer(first)->recv_seq_channel_a = 7;
	peer(first)->recv_seq_channel_b = 8;
	xvt_time_advance_host_clock(SECOND_US);
	send_system_message(DPSYS_CREATEPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 3);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].player_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].player_id, 40);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[2].player_id, 30);
	XVT_ASSERT_INT_EQ(roster_named(2, "Wedge", "L3"), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[2].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
	XVT_ASSERT_INT_EQ(sent_header(0) & 0x7F, NET_PACKET_SEQUENCE_STATUS);
	XVT_ASSERT_INT_EQ(sent_length(0), 28);
	XVT_ASSERT_INT_EQ(sent_word(0, 4), 3);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 2);
	XVT_ASSERT_INT_EQ(sent_word(0, 12), (int)GetTickCount());
	XVT_ASSERT_INT_EQ(sent_word(0, 16), 40);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[20], 5);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[21], 6);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[22], 7);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[23], 8);
	XVT_ASSERT_INT_EQ(sent_word(0, 24), GROUP_ID);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[28], 127);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[31], 127);
	XVT_ASSERT_INT_EQ(sent_header(1) & 0x7F, NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(g_sent[1].to, 30);
	XVT_ASSERT_INT_EQ(second, 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_system_handled", "type"),
			  DPSYS_CREATEPLAYERORGROUP);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_system_handled", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_system_handled", "host"),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_joined", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_joined", "players"),
			  3);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_joined", "peers"),
			  3);

	/* With one peer slot its record is sent, with none there is none. */
	system_world(1);
	list_player(30, PLAYER_TYPE, "Wedge", "L3");
	unsigned int only = net_find_or_create_peer_slot(40);
	peer(only)->recv_seq_channel_b = 9;
	send_system_message(DPSYS_CREATEPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(sent_length(0), 20);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 1);
	XVT_ASSERT_INT_EQ(sent_word(0, 16), 40);
	XVT_ASSERT_INT_EQ(g_sent[0].bytes[23], 9);
	system_world(1);
	list_player(30, PLAYER_TYPE, "Wedge", "L3");
	send_system_message(DPSYS_CREATEPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(sent_length(0), 12);
	XVT_ASSERT_INT_EQ(sent_word(0, 8), 0);

	/* The host itself and a group: nothing changes. */
	system_world(1);
	list_player(30, PLAYER_TYPE, "Wedge", "L3");
	forget_sends();
	forget_lines();
	g_front_state.net_player_count = 2;
	send_system_message(DPSYS_CREATEPLAYERORGROUP, PLAYER_TYPE, LOCAL_ID);
	send_system_message(DPSYS_CREATEPLAYERORGROUP, GROUP_TYPE, 60);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 2);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_joined"), 0);

	/* A client rebuilds its roster and sends nothing. */
	g_front_state.net_is_host = 0;
	g_front_state.net_player_count = 5;
	send_system_message(DPSYS_CREATEPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 2);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_joined"), 0);
}

/* The slot's fields as net_find_or_create_peer_slot sets a new one, apart from
 * the times, which a freed slot has at 0. */
static int slot_is_cleared(const struct net_reliable_peer_slot *slot)
{
	return slot->direct_play_id == 0 &&
	       slot->last_delivered_seq_default == 127 &&
	       slot->last_delivered_seq_channel_a == 127 &&
	       slot->last_delivered_seq_channel_b == 127 &&
	       slot->recv_seq_default == 127 &&
	       slot->recv_seq_channel_a == 127 &&
	       slot->recv_seq_channel_b == 127 && slot->send_seq == 0 &&
	       slot->last_piggyback_type == NET_PACKET_NOP &&
	       slot->piggyback_length == 1 && slot->last_heard_ms == 0 &&
	       slot->last_activity_ms == 0 && slot->packet_count == 0 &&
	       slot->packet_drop_count == 0 && slot->packet_retry_count == 0;
}

/* On the host, a player destroyed leaves the roster as DirectPlay now lists
 * it, with the mark that a ready player left this frame when the leaver was
 * ready. Its peer slot is freed by moving the last slot into it, and the slot
 * left at the end is cleared, the resend-request count with it. Its link
 * figures entry is cleared. A group, or a player that has no slot or entry,
 * frees nothing. The leaving is logged with the count of slots left. */
static void check_system_player_destroyed_on_host(void)
{
	system_world(1);
	list_player(50, PLAYER_TYPE, "Biggs", "L2");
	roster_player(1, 30, 1);
	roster_player(2, 50, 0);
	net_find_or_create_peer_slot(40);
	net_find_or_create_peer_slot(30);
	unsigned int last = net_find_or_create_peer_slot(50);
	peer(last)->last_delivered_seq_channel_b = 11;
	peer(last)->send_seq = 4;
	peer(last)->packet_count = 9;
	peer(last)->packet_retry_count = 9;
	peer(last)->last_heard_ms = 77;
	g_net_player_connection_stats[0].player_id = 30;
	g_net_player_connection_stats[0].latency_total_ms = 300;
	g_net_player_connection_stats[0].latency_sample_count = 3;
	g_net_player_connection_stats[0].packet_count = 4;
	g_net_player_connection_stats[0].packet_drop_count = 5;
	g_net_player_connection_stats[0].packet_retry_count = 6;
	g_net_player_connection_stats[1].player_id = 50;
	g_net_player_connection_stats[1].latency_total_ms = 50;
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_ready_player_left_this_frame, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].player_id, 50);
	XVT_ASSERT_INT_EQ(roster_named(1, "Biggs", "L2"), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 2);
	XVT_ASSERT_INT_EQ(peer(0)->direct_play_id, 40);
	XVT_ASSERT_INT_EQ(peer(1)->direct_play_id, 50);
	XVT_ASSERT_INT_EQ(peer(1)->last_delivered_seq_channel_b, 11);
	XVT_ASSERT_INT_EQ(peer(1)->send_seq, 4);
	XVT_ASSERT_INT_EQ(peer(1)->packet_count, 9);
	XVT_ASSERT_INT_EQ(peer(1)->packet_retry_count, 9);
	XVT_ASSERT_INT_EQ(peer(1)->last_heard_ms, 77);
	XVT_ASSERT_INT_EQ(slot_is_cleared(peer(2)), 1);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].player_id, 0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].latency_total_ms, 0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].latency_sample_count,
			  0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].packet_count, 0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].packet_drop_count,
			  0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].packet_retry_count,
			  0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[1].player_id, 50);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[1].latency_total_ms,
			  50);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_left", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_left", "ready"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_left", "peers"), 2);

	/* The player holding the last slot frees that slot alone. */
	system_world(1);
	net_find_or_create_peer_slot(40);
	unsigned int leaver = net_find_or_create_peer_slot(30);
	peer(leaver)->packet_retry_count = 9;
	peer(leaver)->last_heard_ms = 77;
	roster_player(1, 30, 0);
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_ready_player_left_this_frame, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(peer(0)->direct_play_id, 40);
	XVT_ASSERT_INT_EQ(slot_is_cleared(peer(1)), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_left", "ready"), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_left", "peers"), 1);

	/* The player in the first slot and the first roster entry, the figures
	 * entry well into the table, and slots and entries past the counts
	 * that merely carry the id. */
	system_world(1);
	net_find_or_create_peer_slot(30);
	net_find_or_create_peer_slot(40);
	peer(1)->packet_count = 6;
	g_net_player_connection_stats[3].player_id = LOCAL_ID;
	g_net_player_connection_stats[3].packet_count = 8;
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(peer(0)->direct_play_id, 40);
	XVT_ASSERT_INT_EQ(peer(0)->packet_count, 6);
	XVT_ASSERT_INT_EQ(slot_is_cleared(peer(1)), 1);
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, LOCAL_ID);
	XVT_ASSERT_INT_EQ(g_front_state.net_ready_player_left_this_frame, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[3].player_id, 0);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[3].packet_count, 0);
	system_world(1);
	net_find_or_create_peer_slot(40);
	peer(1)->direct_play_id = 30;
	roster_player(1, 40, 0);
	g_front_state.net_players[2].player_id = 40;
	g_front_state.net_players[2].ready_flag = 1;
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 40);
	XVT_ASSERT_INT_EQ(g_front_state.net_ready_player_left_this_frame, 0);
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 0);
	system_world(1);
	net_find_or_create_peer_slot(40);
	peer(1)->direct_play_id = 30;
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(peer(0)->direct_play_id, 40);

	/* An entry past the roster's count does not count as the leaver. */
	system_world(1);
	g_front_state.net_players[1].player_id = 40;
	g_front_state.net_players[1].ready_flag = 1;
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 40);
	XVT_ASSERT_INT_EQ(g_front_state.net_ready_player_left_this_frame, 0);

	/* A slot freed and taken again starts from nothing. */
	system_world(1);
	net_find_or_create_peer_slot(40);
	unsigned int held = net_find_or_create_peer_slot(30);
	peer(held)->packet_retry_count = 9;
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(net_find_or_create_peer_slot(60), 1);
	XVT_ASSERT_INT_EQ(peer(1)->packet_retry_count, 0);

	/* A group, and a player with no slot or entry, free nothing. */
	system_world(1);
	net_find_or_create_peer_slot(40);
	g_net_player_connection_stats[0].player_id = 40;
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, GROUP_TYPE, 40);
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 99);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(peer(0)->direct_play_id, 40);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].player_id, 40);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_left"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_left", "player"),
			  99);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 1);
}

/* On a client, a player destroyed frees no slot and no figures; the roster is
 * rebuilt. The host's destruction marks the session lost and queues a
 * HOST_CANCELLED, with the flush NOP behind it, as received from the local
 * player: nothing is sent to DirectPlay. A player that is not the host queues
 * nothing. */
static void check_system_player_destroyed_on_client(void)
{
	system_world(0);
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
	net_find_or_create_peer_slot(40);
	g_net_player_connection_stats[0].player_id = 40;
	roster_player(1, 40, 1);
	list_player(40, PLAYER_TYPE, "Biggs", "L2");
	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 40);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(g_net_player_connection_stats[0].player_id, 40);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 2);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_left"), 0);

	send_system_message(DPSYS_DESTROYPLAYERORGROUP, GROUP_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);

	send_system_message(DPSYS_DESTROYPLAYERORGROUP, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued_type(0), NET_PACKET_HOST_CANCELLED);
	XVT_ASSERT_INT_EQ(queued(0)->direct_play_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(queued_type(1), NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_host_cancel_queued", "host"), 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 2);
}

/* A message that a player was renamed gives its roster entry the new short and
 * long names, cut to 12 characters, and logs the new name. A group's, an id not
 * in the roster and an entry past the roster's count change nothing; names the
 * message does not end are rejected with a warning and leave the entry
 * alone. */
static void check_system_player_renamed(void)
{
	static const struct {
		const char *short_name;
		const char *long_name;
		const char *new_short;
		const char *new_long;
	} rows[] = {
		{"Wedge", "Rogue Two", "Wedge", "Rogue Two"},
		{"ABCDEFGHIJKLMNOP", "0123456789ABCDEFGHI", "ABCDEFGHIJKL",
		 "0123456789AB"},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		struct net_player_name_message message;
		memset(&message, 0, sizeof message);
		message.header.dwType = DPSYS_SETPLAYERORGROUPNAME;
		message.header.dwPlayerType = PLAYER_TYPE;
		message.header.dpId = 30;
		strcpy(message.names, rows[row].short_name);
		strcpy(message.names + strlen(rows[row].short_name) + 1,
		       rows[row].long_name);
		system_world(0);
		roster_player(1, 30, 0);
		roster_player(2, 40, 0);
		roster_player(3, 30, 0);
		strcpy(g_front_state.net_players[2].player_name, "Biggs");
		strcpy(g_front_state.net_players[4].player_name, "Past");
		g_front_state.net_players[4].player_id = 30;
		net_handle_direct_play_system_message(
			DPSYS_SETPLAYERORGROUPNAME, &message);
		XVT_ASSERT_INT_EQ(roster_named(1, rows[row].new_short,
					       rows[row].new_long),
				  1);
		XVT_ASSERT_INT_EQ(roster_named(3, rows[row].new_short,
					       rows[row].new_long),
				  1);
		XVT_ASSERT_INT_EQ(
			strcmp(g_front_state.net_players[2].player_name,
			       "Biggs"),
			0);
		XVT_ASSERT_INT_EQ(
			strcmp(g_front_state.net_players[4].player_name,
			       "Past"),
			0);
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_renamed"),
				  2);
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_rename_rejected"),
				  0);
		XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 4);
	}

	struct net_player_name_message message;
	memset(&message, 0, sizeof message);
	message.header.dwType = DPSYS_SETPLAYERORGROUPNAME;
	message.header.dwPlayerType = PLAYER_TYPE;
	message.header.dpId = LOCAL_ID;
	strcpy(message.names, "Luke");
	strcpy(message.names + 5, "Skywalker");
	system_world(0);
	net_handle_direct_play_system_message(DPSYS_SETPLAYERORGROUPNAME,
					      &message);
	XVT_ASSERT_INT_EQ(roster_named(0, "Luke", "Skywalker"), 1);
	message.header.dpId = 30;
	memset(message.names, 'x', sizeof message.names);
	system_world(0);
	roster_player(1, 30, 0);
	strcpy(g_front_state.net_players[1].player_name, "Wedge");
	net_handle_direct_play_system_message(DPSYS_SETPLAYERORGROUPNAME,
					      &message);
	XVT_ASSERT_INT_EQ(
		strcmp(g_front_state.net_players[1].player_name, "Wedge"), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_rename_rejected"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_rename_rejected", "player"),
			  30);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_renamed"), 0);

	/* A group's name, and an id that is not in the roster. */
	strcpy(message.names, "Gone");
	strcpy(message.names + 5, "Gone Too");
	message.header.dwPlayerType = GROUP_TYPE;
	net_handle_direct_play_system_message(DPSYS_SETPLAYERORGROUPNAME,
					      &message);
	message.header.dwPlayerType = PLAYER_TYPE;
	message.header.dpId = 77;
	net_handle_direct_play_system_message(DPSYS_SETPLAYERORGROUPNAME,
					      &message);
	XVT_ASSERT_INT_EQ(
		strcmp(g_front_state.net_players[1].player_name, "Wedge"), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_renamed"), 0);
	/* Any other kind of message does nothing. */
	send_system_message(0x7777, PLAYER_TYPE, 30);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 2);
}

/* The roster refresh keeps the first entry, clears the others, lists what
 * DirectPlay lists except a type 0 entry and the local player, each name cut to
 * 15 characters, carries each player's ready flag over by id and logs each
 * player and the count. It returns 0 and changes nothing without DirectPlay,
 * and stops at 32 players with a warning. DirectPlay is asked with the back
 * buffer unlocked, which is locked again when it was. */
static void check_roster_refresh(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(net_refresh_player_roster(), 0);
	XVT_ASSERT_INT_EQ(net_refresh_player_roster_with_lock_guard(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_roster_skipped"), 1);

	system_world(0);
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	strcpy(g_front_state.net_players[0].player_name, "Luke");
	roster_player(1, 30, 1);
	roster_player(2, 40, 0);
	roster_player(5, 77, 1);
	g_front_state.net_players[0].ready_flag = 1;
	list_player(99, 0, "Nobody", "Nothing");
	list_player(40, PLAYER_TYPE, "Biggs", "0123456789ABCDEFGHIJ");
	list_player(30, PLAYER_TYPE, "ABCDEFGHIJKLMNOPQRST", "Rogue Two");
	XVT_ASSERT_INT_EQ(net_refresh_player_roster(), 1);
	XVT_ASSERT_INT_EQ(g_enum_saw_locked, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 3);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].player_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(
		strcmp(g_front_state.net_players[0].player_name, "Luke"), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].player_id, 40);
	XVT_ASSERT_INT_EQ(roster_named(1, "Biggs", "0123456789ABCDE"), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[2].player_id, 30);
	XVT_ASSERT_INT_EQ(roster_named(2, "ABCDEFGHIJKLMNO", "Rogue Two"), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[2].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[5].player_id, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_listed"), 2);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_listed", "index"),
			  2);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_listed", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_roster", "players"), 3);
	g_enum_saw_locked = 0;
	XVT_ASSERT_INT_EQ(net_refresh_player_roster_with_lock_guard(), 1);
	XVT_ASSERT_INT_EQ(g_enum_saw_locked, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 3);
	frontend_display_unlock_back_buffer();
	XVT_ASSERT_INT_EQ(net_refresh_player_roster_with_lock_guard(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);
	xvt_test_close_display();

	system_world(0);
	for (int i = 0; i < 40; ++i) {
		list_player(100 + i, PLAYER_TYPE, "Pilot", "Pilot");
	}
	XVT_ASSERT_INT_EQ(net_refresh_player_roster(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 32);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[31].player_id, 130);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_roster_full", "players"),
			  32);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_roster_full"), 1);
}

/* Creating the local player gives DirectPlay the long and short names and
 * returns the id it made, 0 when it fails, and the pending mark while it is
 * pending; the result is logged. */
static void check_create_player(void)
{
	fresh();
	open_session();
	g_create_player_id = 1234;
	XVT_ASSERT_INT_EQ(net_create_direct_play_player("Long", "Short"), 1234);
	XVT_ASSERT_INT_EQ(strcmp(g_create_short, "Short"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_create_long, "Long"), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_player_created", "player"),
			  1234);
	g_create_result = DPERR_PENDING;
	XVT_ASSERT_INT_EQ(net_create_direct_play_player("Long", "Short"),
			  XVT_NETWORK_PENDING);
	g_create_result = DPERR_INVALIDPLAYER;
	XVT_ASSERT_INT_EQ(net_create_direct_play_player("Long", "Short"), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_player_created"), 3);
}

/* ------------------------------------------------------------------------ */
/* The sends not outside the sequence scheme, and the resends. */

/* A resend carries the resend bit in its header with the type and the sequence,
 * the channel byte, then the body's length, the body and a NOP trailer; for a
 * type from 60 to 63 neither the length nor the trailer. It goes from the local
 * player to the player, and is logged with its size and the send's result. The
 * call returns 1 when Send succeeds and 0 when it fails. */
static void check_sequenced_send_layout(void)
{
	static const struct {
		int type;
		int bare;
	} rows[] = {{NET_PACKET_CHAT, 0},
		    {NET_PACKET_SEQUENCE_STATUS, 0},
		    {NET_PACKET_RESYNC_CHECKSUMS, 1},
		    {NET_PACKET_RESYNC_CHUNK, 1},
		    {NET_PACKET_PROBE_REQUEST, 0}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int packet[3] = {rows[row].type, 0x11223344, 0x55667788};
		fresh();
		open_session();
		XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
					  30, 2, 5, packet, sizeof packet),
				  1);
		XVT_ASSERT_INT_EQ(g_sent_count, 1);
		XVT_ASSERT_INT_EQ(g_sent[0].from, LOCAL_ID);
		XVT_ASSERT_INT_EQ(g_sent[0].to, 30);
		XVT_ASSERT_INT_EQ(sent_header(0),
				  0x80 | (5 << 8) | rows[row].type);
		XVT_ASSERT_INT_EQ(g_sent[0].bytes[RESEND_CLASS], 2);
		if (rows[row].bare) {
			XVT_ASSERT_INT_EQ(g_sent[0].size, 11);
			XVT_ASSERT_INT_EQ(sent_word(0, 3), packet[1]);
			XVT_ASSERT_INT_EQ(sent_word(0, 7), packet[2]);
		} else {
			XVT_ASSERT_INT_EQ(g_sent[0].size, 14);
			XVT_ASSERT_INT_EQ(g_sent[0].bytes[RESEND_LENGTH], 8);
			XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY), packet[1]);
			XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY + 4),
					  packet[2]);
			XVT_ASSERT_INT_EQ(g_sent[0].bytes[13], NET_PACKET_NOP);
		}
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_resent", "to"), 30);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_resent", "type"),
			rows[row].type);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_resent", "channel"),
			2);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_resent", "seq"), 5);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_resent", "bytes"),
			g_sent[0].size);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_packet_resent", "result"), 0);
		g_send_result = DPERR_INVALIDPLAYER;
		XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
					  30, 2, 5, packet, sizeof packet),
				  0);
	}
	fresh();
	int packet[2] = {NET_PACKET_CHAT, 1};
	XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
				  30, 0, 5, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
}

/* A resend to the local player is not sent: it is queued as a resent copy from
 * the local player with the packet as it is, on the channel and sequence given,
 * and the call returns 1. With 1,024 queued it is dropped with a warning. */
static void check_sequenced_send_to_self(void)
{
	int packet[2] = {NET_PACKET_CHAT, 0x1234};
	fresh();
	open_session();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	poison_queue(1023, 1);
	queued(1023)->is_resent_copy = 0;
	XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
				  LOCAL_ID, 2, 9, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 0);
	XVT_ASSERT_INT_EQ(queued(1023)->direct_play_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(queued(1023)->payload_size, sizeof packet);
	XVT_ASSERT_INT_EQ(memcmp(queued(1023)->payload, packet, sizeof packet),
			  0);
	XVT_ASSERT_INT_EQ(queued(1023)->packet_class, 2);
	XVT_ASSERT_INT_EQ(queued(1023)->sequence_byte, 9);
	XVT_ASSERT_INT_EQ(queued(1023)->is_resent_copy, 1);
	XVT_ASSERT_INT_EQ(queued(1023)->nack_retry_count, 0);
	XVT_ASSERT_INT_EQ(queued(1023)->last_nack_ms, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_local_copy_dropped"), 0);

	g_front_state.net_runtime_recv_queue_count = 1024;
	XVT_ASSERT_INT_EQ(net_send_sequenced_direct_play_packet(
				  LOCAL_ID, 2, 9, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1024);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_local_copy_dropped", "to"),
			  LOCAL_ID);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_local_copy_dropped", "queued"), 1024);
}

/* A packet outside the sequence scheme goes with sequence 0 and its type
 * alone, or with the group bits for a send to the group, then the body's
 * length, the body and a NOP trailer; types from 60 to 63 have neither length
 * nor trailer. It is recorded in no history and queued nowhere, nothing is
 * sent to the local player, and the call returns 1 without DirectPlay, 1 when
 * Send succeeds and 0 when it fails. A failure other than an invalid player is
 * logged as a warning. */
static void check_control_send(void)
{
	static const struct {
		int to;
		int type;
		int header_bits;
		int size;
	} rows[] = {
		{30, NET_PACKET_KEEPALIVE, 0, 13},
		{GROUP_ID, NET_PACKET_KEEPALIVE, 0x8080, 13},
		{0, NET_PACKET_KEEPALIVE, 0, 13},
		{30, NET_PACKET_SEQUENCE_STATUS, 0, 13},
		{30, NET_PACKET_RESYNC_CHECKSUMS, 0, 10},
		{30, NET_PACKET_RESYNC_CHUNK, 0, 10},
		{30, NET_PACKET_PROBE_REQUEST, 0, 13},
	};
	int packet[3] = {0, 0x11223344, 0x55667788};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		fresh();
		packet[0] = rows[row].type;
		XVT_ASSERT_INT_EQ(net_send_direct_play_packet(rows[row].to,
							      packet,
							      sizeof packet, 0),
				  1);
		XVT_ASSERT_INT_EQ(g_sent_count, 0);
		open_session();
		XVT_ASSERT_INT_EQ(net_send_direct_play_packet(rows[row].to,
							      packet,
							      sizeof packet, 0),
				  1);
		XVT_ASSERT_INT_EQ(g_sent_count, 1);
		XVT_ASSERT_INT_EQ(g_sent[0].from, LOCAL_ID);
		XVT_ASSERT_INT_EQ(g_sent[0].to, rows[row].to);
		XVT_ASSERT_INT_EQ(sent_header(0),
				  rows[row].header_bits | rows[row].type);
		XVT_ASSERT_INT_EQ(g_sent[0].size, rows[row].size);
		if (rows[row].size == 10) {
			XVT_ASSERT_INT_EQ(sent_word(0, 2), packet[1]);
			XVT_ASSERT_INT_EQ(sent_word(0, 6), packet[2]);
		} else {
			XVT_ASSERT_INT_EQ(sent_length(0), 8);
			XVT_ASSERT_INT_EQ(sent_word(0, 4), packet[1]);
			XVT_ASSERT_INT_EQ(sent_word(0, 8), packet[2]);
			XVT_ASSERT_INT_EQ(g_sent[0].bytes[12], NET_PACKET_NOP);
		}
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  0);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_sent_history_write_index, 0);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_control_sent", "to"),
			rows[row].to);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_control_sent", "type"),
			rows[row].type);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_control_sent", "bytes"),
			rows[row].size);
	}
	fresh();
	open_session();
	packet[0] = NET_PACKET_KEEPALIVE;
	XVT_ASSERT_INT_EQ(
		net_send_direct_play_packet(LOCAL_ID, packet, sizeof packet, 0),
		1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	g_send_result = DPERR_INVALIDPLAYER;
	XVT_ASSERT_INT_EQ(
		net_send_direct_play_packet(30, packet, sizeof packet, 0), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_control_send_failed"), 0);
	g_send_result = DPERR_PENDING;
	XVT_ASSERT_INT_EQ(
		net_send_direct_play_packet(30, packet, sizeof packet, 0), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_control_send_failed"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_control_send_failed", "to"),
			  30);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_control_send_failed", "type"),
		NET_PACKET_KEEPALIVE);
}

/* The first packet sent on the broadcast or the group channel carries a NOP
 * trailer, the next the first one's type byte and body; the group's header has
 * both group bits, the broadcast's none; a type from 60 to 63 has no length
 * word and no trailer. Each send also clears whatever the sent history and the
 * receive queue entries it takes held: resend requests, and a resent-copy
 * mark. */
static void check_broadcast_and_group_layout(void)
{
	static const struct {
		int to;
		int bits;
	} rows[] = {{0, 0}, {GROUP_ID, 0x8080}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int to = rows[row].to;
		fresh();
		open_session();
		for (int i = 0; i < 3; ++i) {
			struct net_queued_packet *entry =
				&g_front_state.net_runtime_sent_history[i];
			entry->last_nack_ms = 5;
			entry->nack_retry_count = 3;
			entry = queued(i);
			entry->last_nack_ms = 5;
			entry->nack_retry_count = 3;
			entry->is_resent_copy = 1;
		}
		int first = send_game_packet(to, NET_PACKET_CHAT, 0x1111);
		int second =
			send_game_packet(to, NET_PACKET_REMOTE_INPUT, 0x2222);
		int bare = send_game_packet(to, NET_PACKET_RESYNC_CHECKSUMS,
					    0x3333);
		int third = send_game_packet(to, NET_PACKET_CHAT, 0x4444);
		XVT_ASSERT_INT_EQ(g_sent[first].to, to);
		XVT_ASSERT_INT_EQ(sent_header(first),
				  rows[row].bits | NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(g_sent[first].size, 9);
		XVT_ASSERT_INT_EQ(sent_length(first), 4);
		XVT_ASSERT_INT_EQ(sent_word(first, 4), 0x1111);
		XVT_ASSERT_INT_EQ(g_sent[first].bytes[8], NET_PACKET_NOP);
		XVT_ASSERT_INT_EQ(sent_header(second),
				  rows[row].bits | (1 << 8) |
					  NET_PACKET_REMOTE_INPUT);
		XVT_ASSERT_INT_EQ(g_sent[second].size, 13);
		XVT_ASSERT_INT_EQ(g_sent[second].bytes[8], NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(sent_word(second, 9), 0x1111);
		XVT_ASSERT_INT_EQ(sent_header(bare),
				  rows[row].bits | (2 << 8) |
					  NET_PACKET_RESYNC_CHECKSUMS);
		XVT_ASSERT_INT_EQ(g_sent[bare].size, 6);
		XVT_ASSERT_INT_EQ(sent_word(bare, 2), 0x3333);
		XVT_ASSERT_INT_EQ(sent_header(third),
				  rows[row].bits | (3 << 8) | NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(g_sent[third].size, 13);
		XVT_ASSERT_INT_EQ(g_sent[third].bytes[8],
				  NET_PACKET_RESYNC_CHECKSUMS);
		XVT_ASSERT_INT_EQ(sent_word(third, 9), 0x3333);
		for (int i = 0; i < 3; ++i) {
			XVT_ASSERT_INT_EQ(
				g_front_state.net_runtime_sent_history[i]
					.last_nack_ms,
				0);
			XVT_ASSERT_INT_EQ(
				g_front_state.net_runtime_sent_history[i]
					.nack_retry_count,
				0);
			XVT_ASSERT_INT_EQ(queued(i)->last_nack_ms, 0);
			XVT_ASSERT_INT_EQ(queued(i)->nack_retry_count, 0);
			XVT_ASSERT_INT_EQ(queued(i)->is_resent_copy, 0);
		}
	}
}

/* A packet sent to the local player, or to the group or everyone when the
 * receive queue is full, has its local copy dropped with a warning that names
 * the destination and the count. */
static void check_local_copy_dropped_logged(void)
{
	static const int destinations[] = {LOCAL_ID, GROUP_ID, 0};
	for (int i = 0; i < 3; ++i) {
		int packet[2] = {NET_PACKET_CHAT, 1};
		fresh();
		open_session();
		g_front_state.net_runtime_recv_queue_count = 1024;
		XVT_ASSERT_INT_EQ(net_send_packet_internal(destinations[i],
							   packet,
							   sizeof packet),
				  1);
		XVT_ASSERT_INT_EQ(
			count_lines("network.lobby_local_copy_dropped"), 1);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_local_copy_dropped", "to"),
			destinations[i]);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_local_copy_dropped", "type"),
			NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_local_copy_dropped",
					     "queued"),
				  1024);
	}
	int packet[2] = {NET_PACKET_CHAT, 1};
	fresh();
	open_session();
	g_front_state.net_runtime_recv_queue_count = 1024;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_local_copy_dropped"), 0);
}

/* A send that fails with anything but an invalid player is logged as a warning
 * with the player and type; the local copy dropped for a full queue is logged
 * with the count; each packet sent is logged with its channel, sequence and
 * size. The send-and-flush leaves the back buffer unlocked while it sends and
 * locks it again when it was locked. */
static void check_send_logs_and_back_buffer(void)
{
	int packet[2] = {NET_PACKET_CHAT, 1};
	fresh();
	open_session();
	g_send_result = DPERR_PENDING;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, packet, sizeof packet),
			  0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_send_failed"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_send_failed", "to"), 30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_send_failed", "type"),
			  NET_PACKET_CHAT);
	g_send_result = DPERR_INVALIDPLAYER;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(30, packet, sizeof packet),
			  0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_send_failed"), 1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_packet_sent"), 2);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_sent", "to"), 30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_sent", "channel"),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_sent", "seq"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_sent", "bytes"), 13);

	g_front_state.net_runtime_recv_queue_count = 1024;
	XVT_ASSERT_INT_EQ(net_send_packet_internal(0, packet, sizeof packet),
			  0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_local_copy_dropped", "to"),
			  0);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_local_copy_dropped", "queued"), 1024);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_sent", "channel"),
			  0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_packet_sent", "queued"),
			  1024);

	fresh();
	open_session();
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	XVT_ASSERT_INT_EQ(net_send_packet_and_flush(30, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_send_saw_locked, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	frontend_display_unlock_back_buffer();
	XVT_ASSERT_INT_EQ(net_send_packet_and_flush(30, packet, sizeof packet),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);
	xvt_test_close_display();
}

/* ------------------------------------------------------------------------ */
/* Closing the session, the polls and the hand-over to the flight session. */

/* Closing the session with DirectPlay open destroys the local player, then the
 * group when there is one, closes and releases DirectPlay, with the back buffer
 * unlocked meanwhile, and sets the transport back to IPX. In every case it
 * clears the session's not-lost mark, empties the receive queue, resets the
 * counters and trailers and the 40 peer slots whether in use or not, and
 * returns 1. The closing is logged, with whether the call is for quitting. */
static void check_shutdown_session(void)
{
	for (int quit = 0; quit < 2; ++quit) {
		fresh();
		xvt_network_session_on_close();
		open_session();
		xvt_test_open_display();
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
		g_net_active_transport_type = NET_TRANSPORT_TCPIP;
		xvt_network_session_host_lost();
		g_front_state.net_runtime_recv_queue_read_index = 7;
		g_front_state.net_runtime_recv_queue_write_index = 12;
		g_front_state.net_runtime_recv_queue_count = 5;
		g_front_state.net_runtime_broadcast_seq_counter = 9;
		g_front_state.net_runtime_group_seq_counter = 8;
		g_front_state.net_runtime_broadcast_pending_payload
			.piggyback_empty = 0;
		g_front_state.net_runtime_group_pending_payload
			.piggyback_empty = 0;
		g_front_state.net_reliable_retry_long_timeout_mode = 1;
		net_find_or_create_peer_slot(40);
		net_find_or_create_peer_slot(41);
		net_find_or_create_peer_slot(42);
		for (int i = 0; i < 40; ++i) {
			peer((unsigned int)i)->direct_play_id = 40 + i;
			peer((unsigned int)i)->send_seq = 5;
			peer((unsigned int)i)->recv_seq_channel_a = 6;
			peer((unsigned int)i)->recv_seq_channel_b = 7;
			peer((unsigned int)i)->recv_seq_default = 8;
			peer((unsigned int)i)->last_delivered_seq_channel_a = 9;
			peer((unsigned int)i)->last_delivered_seq_channel_b =
				10;
			peer((unsigned int)i)->last_delivered_seq_default = 11;
			peer((unsigned int)i)->last_piggyback_type =
				NET_PACKET_CHAT;
			peer((unsigned int)i)->piggyback_length = 9;
			peer((unsigned int)i)->last_activity_ms = 1;
			peer((unsigned int)i)->last_heard_ms = 2;
			peer((unsigned int)i)->packet_count = 3;
			peer((unsigned int)i)->packet_drop_count = 4;
			peer((unsigned int)i)->packet_retry_count = 5;
		}
		if (quit) {
			net_shutdown_direct_play_session_for_quit();
		} else {
			net_shutdown_direct_play_session();
		}
		XVT_ASSERT_INT_EQ(g_closing_count, 4);
		static const int order[4][2] = {
			{1, LOCAL_ID}, {2, GROUP_ID}, {3, 0}, {4, 0}};
		for (int i = 0; i < 4; ++i) {
			XVT_ASSERT_INT_EQ(g_closing_calls[i][0], order[i][0]);
			XVT_ASSERT_INT_EQ(g_closing_calls[i][1], order[i][1]);
		}
		XVT_ASSERT_INT_EQ(g_closing_saw_locked, 0);
		XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
		XVT_ASSERT_TRUE(g_front_state.net_direct_play == NULL);
		XVT_ASSERT_INT_EQ(g_net_active_transport_type,
				  NET_TRANSPORT_IPX);
		XVT_ASSERT_INT_EQ(g_front_state.net_group_dplay_id, 0);
		XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_recv_queue_read_index, 0);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_recv_queue_write_index, 0);
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count,
				  0);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_broadcast_seq_counter, 0);
		XVT_ASSERT_INT_EQ(g_front_state.net_runtime_group_seq_counter,
				  0);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_broadcast_pending_payload
				.piggyback_empty,
			1);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_runtime_group_pending_payload
				.piggyback_empty,
			1);
		XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count,
				  0);
		XVT_ASSERT_INT_EQ(
			g_front_state.net_reliable_retry_long_timeout_mode, 0);
		for (int i = 0; i < 40; ++i) {
			XVT_ASSERT_INT_EQ(
				slot_is_cleared(peer((unsigned int)i)), 1);
		}
		XVT_ASSERT_INT_EQ(count_lines("network.lobby_closing"), 1);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "open"),
				  1);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "player"),
				  LOCAL_ID);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "group"),
				  GROUP_ID);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "queued"),
				  5);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "peers"),
				  3);
		XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "quit"),
				  quit);
		XVT_ASSERT_INT_EQ(
			line_value("network.lobby_closing", "handshake"), 1);
		xvt_test_close_display();
	}

	/* No group to destroy: that call is skipped. */
	fresh();
	open_session();
	g_front_state.net_group_dplay_id = 0;
	XVT_ASSERT_INT_EQ(net_shutdown_direct_play_session_ex(0, 1), 1);
	XVT_ASSERT_INT_EQ(g_closing_count, 3);
	XVT_ASSERT_INT_EQ(g_closing_calls[0][0], 1);
	XVT_ASSERT_INT_EQ(g_closing_calls[1][0], 3);
	XVT_ASSERT_INT_EQ(g_closing_calls[2][0], 4);

	/* Without DirectPlay nothing is asked of it, the transport and the
	 * group stay, and the rest is cleared. */
	fresh();
	g_net_active_transport_type = NET_TRANSPORT_TCPIP;
	g_front_state.net_runtime_recv_queue_count = 3;
	g_front_state.net_runtime_broadcast_seq_counter = 4;
	XVT_ASSERT_INT_EQ(net_shutdown_direct_play_session_ex(1, 0), 1);
	XVT_ASSERT_INT_EQ(g_closing_count, 0);
	XVT_ASSERT_INT_EQ(g_net_active_transport_type, NET_TRANSPORT_TCPIP);
	XVT_ASSERT_INT_EQ(g_front_state.net_group_dplay_id, GROUP_ID);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_seq_counter, 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "open"), 0);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "quit"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_closing", "handshake"), 0);
	g_net_active_transport_type = NET_TRANSPORT_IPX;
}

/* Appends a DirectPlay system message to the receive queue as the pump would
 * queue it: from sender 0, its words the type, the kind of player and the
 * player. */
static void push_system(int type, int player)
{
	int index = g_front_state.net_runtime_recv_queue_write_index;
	struct net_queued_packet *entry = queued(index);
	int words[3] = {type, PLAYER_TYPE, player};
	memset(entry, 0, sizeof *entry);
	memcpy(entry->payload, words, sizeof words);
	entry->payload_size = sizeof words;
	g_front_state.net_runtime_recv_queue_write_index = (index + 1) % 1024;
	++g_front_state.net_runtime_recv_queue_count;
}

/* A poll for a player created, as the poll for a packet type, looks for a
 * queued DirectPlay system message that announces a player, which is not a
 * game packet of that number and not another system message; both polls return
 * 1 with more than 512 packets queued and log the backlog, the packet or system
 * message found is logged, and the scan runs on round the end of the ring. Both
 * leave the back buffer locked when it was, however they end. */
static void check_polls(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(net_poll_for_player_created_or_backlog(), 0);
	open_session();
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	g_front_state.net_runtime_recv_queue_read_index = 1022;
	g_front_state.net_runtime_recv_queue_write_index = 1022;
	/* A game packet numbered like the message, and another message. */
	push_packet(30, 1, 0, 0, DPSYS_CREATEPLAYERORGROUP, 1);
	push_system(DPSYS_DESTROYPLAYERORGROUP, 30);
	XVT_ASSERT_INT_EQ(net_poll_for_player_created_or_backlog(), 0);
	XVT_ASSERT_INT_EQ(g_receive_saw_locked, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_poll_found"), 0);
	push_system(DPSYS_CREATEPLAYERORGROUP, 40);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 1);
	XVT_ASSERT_INT_EQ(net_poll_for_player_created_or_backlog(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_poll_found", "type"),
			  DPSYS_CREATEPLAYERORGROUP);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_poll_found", "system"), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 3);

	g_front_state.net_runtime_recv_queue_count = 513;
	XVT_ASSERT_INT_EQ(net_poll_for_player_created_or_backlog(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_backlog", "queued"), 513);
	forget_lines();
	g_front_state.net_runtime_recv_queue_count = 512;
	g_front_state.net_runtime_recv_queue_read_index = 5;
	XVT_ASSERT_INT_EQ(net_poll_for_player_created_or_backlog(), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_backlog"), 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);

	/* The poll for a type: a system message first, the packet round the
	 * end of the ring. */
	xvt_test_close_display();
	fresh();
	open_session();
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	g_front_state.net_runtime_recv_queue_read_index = 1023;
	g_front_state.net_runtime_recv_queue_write_index = 1023;
	push_system(NET_PACKET_CHAT, 30);
	push_packet(30, 1, 0, 0, NET_PACKET_CHAT, 1);
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_CHAT),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_poll_found", "type"),
			  NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_poll_found", "system"), 0);
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_PING),
			  0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	g_front_state.net_runtime_recv_queue_count = 513;
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_PING),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_backlog", "queued"), 513);
	frontend_display_unlock_back_buffer();
	g_front_state.net_runtime_recv_queue_count = 2;
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_CHAT),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);
	xvt_test_close_display();

	/* Both polls pump first: what DirectPlay holds is found. */
	int body = 3;
	fresh();
	open_session();
	inbox_packet(30, LOCAL_ID, ONE_PLAYER_BIT | NET_PACKET_CHAT, &body,
		     sizeof body);
	XVT_ASSERT_INT_EQ(net_poll_for_packet_type_or_backlog(NET_PACKET_CHAT),
			  1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
	fresh();
	open_session();
	int words[3] = {DPSYS_CREATEPLAYERORGROUP, PLAYER_TYPE, 30};
	inbox_bytes(0, LOCAL_ID, words, sizeof words);
	XVT_ASSERT_INT_EQ(net_poll_for_player_created_or_backlog(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 1);
}

/* The next packet for the game comes after each system message ahead of it has
 * been handled, and is handed out with its sender and size; the back buffer is
 * unlocked meanwhile and locked again when it was. With nothing to hand out it
 * returns nothing. A system message behind a packet that waits stays. */
static void check_next_app_packet(void)
{
	fresh();
	xvt_network_session_on_close();
	open_session();
	g_front_state.net_host_player_id = 30;
	roster_player(0, LOCAL_ID, 1);
	list_player(LOCAL_ID, PLAYER_TYPE, "Luke", "L0");
	list_player(30, PLAYER_TYPE, "Wedge", "L3");
	list_player(40, PLAYER_TYPE, "Biggs", "L2");
	xvt_test_open_display();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	push_system(DPSYS_CREATEPLAYERORGROUP, 30);
	push_system(DPSYS_CREATEPLAYERORGROUP, 40);
	push_packet(30, 1, 0, 0, NET_PACKET_CHAT, 7);
	DPID sender = 0;
	uint32_t size = 0;
	const int *packet = net_get_next_app_packet(&sender, &size);
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(sender, 30);
	XVT_ASSERT_INT_EQ(size, 8);
	XVT_ASSERT_INT_EQ(packet[0], NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(packet[1], 7);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_system_handled"), 2);
	XVT_ASSERT_INT_EQ(g_front_state.net_player_count, 3);
	XVT_ASSERT_INT_EQ(g_enum_saw_locked, 0);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 0);

	sender = 77;
	XVT_ASSERT_TRUE(net_get_next_app_packet(&sender, &size) == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 1);
	frontend_display_unlock_back_buffer();
	XVT_ASSERT_TRUE(net_get_next_app_packet(&sender, &size) == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_locked, 0);

	/* A system message only waiting behind a packet that is not yet due. */
	forget_lines();
	push_packet(40, 1, 5, 0, NET_PACKET_CHAT, 1);
	push_system(DPSYS_CREATEPLAYERORGROUP, 50);
	XVT_ASSERT_TRUE(net_get_next_app_packet(&sender, &size) == NULL);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_system_handled"), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	xvt_test_close_display();
}

/* The flight session's side of the hand-over, a copy of what the lobby
 * kept or gives back. */
static struct net_queued_packet g_flight_queue[1024];
static struct net_queued_packet g_flight_history[128];
static struct net_reliable_peer_slot g_flight_slots[40];

/* Fills a queue entry or history entry with values that tell it from others. */
static void mark_entry(struct net_queued_packet *entry, int number)
{
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = 100 + number;
	entry->payload_size = 8;
	entry->last_nack_ms = 10 + number;
	entry->nack_retry_count = (uint8_t)(number % 5);
	entry->packet_class = (uint8_t)(number % 3);
	entry->sequence_byte = (uint8_t)(number % 128);
	entry->is_resent_copy = (uint8_t)(number % 2);
	for (int i = 0; i < 512; ++i) {
		entry->payload[i] = (uint8_t)(number * 5 + i);
	}
}

static const GUID k_app_guid = {
	0x11223344, 0x5566, 0x7788, {1, 2, 3, 4, 5, 6, 7, 8}};
static const GUID k_session_guid = {
	0x99AABBCC, 0xDDEE, 0xFF00, {9, 8, 7, 6, 5, 4, 3, 2}};

/* The hand-over to the flight session copies the interface, both GUIDs, the
 * group and host ids, the local player, the receive queue with its indices and
 * count, only the entries in use and each at its own index, the peer slots in
 * use, both counters with their trailers and the whole sent history with its
 * write index, and returns 1. The hand-over is logged. */
static void check_hand_over_to_flight(void)
{
	void *interface = NULL;
	GUID app = {0};
	GUID session = {0};
	int32_t group = 0;
	int host = 0;
	struct net_player_info local;
	int32_t read = 0;
	int count = 0;
	int write = 0;
	uint32_t slot_count = 0;
	uint32_t broadcast_seq = 0;
	uint32_t group_seq = 0;
	char broadcast_payload[512];
	char group_payload[512];
	int broadcast_length = 0;
	int broadcast_empty = 0;
	int group_length = 0;
	int group_empty = 0;
	int history_write = 0;
	fresh();
	open_session();
	g_front_state.net_app_guid = k_app_guid;
	g_front_state.net_joined_session_guid = k_session_guid;
	g_front_state.net_host_player_id = 55;
	strcpy(g_front_state.net_runtime_local_player.player_name, "Luke");
	g_front_state.net_runtime_recv_queue_read_index = 1022;
	g_front_state.net_runtime_recv_queue_count = 4;
	g_front_state.net_runtime_recv_queue_write_index = 2;
	for (int i = 0; i < 1024; ++i) {
		mark_entry(queued(i), i);
	}
	memset(g_flight_queue, 0xEE, sizeof g_flight_queue);
	g_front_state.net_reliable_peer_slot_count = 3;
	for (int i = 0; i < 40; ++i) {
		peer((unsigned int)i)->direct_play_id = 40 + i;
		peer((unsigned int)i)->packet_count = 7 + i;
	}
	memset(g_flight_slots, 0xEE, sizeof g_flight_slots);
	g_front_state.net_runtime_broadcast_seq_counter = 9;
	g_front_state.net_runtime_group_seq_counter = 8;
	for (int i = 0; i < 512; ++i) {
		g_front_state.net_runtime_broadcast_pending_payload.payload[i] =
			(uint8_t)i;
		g_front_state.net_runtime_group_pending_payload.payload[i] =
			(uint8_t)(3 * i);
	}
	g_front_state.net_runtime_broadcast_pending_payload.payload_length = 20;
	g_front_state.net_runtime_broadcast_pending_payload.piggyback_empty = 0;
	g_front_state.net_runtime_group_pending_payload.payload_length = 30;
	g_front_state.net_runtime_group_pending_payload.piggyback_empty = 1;
	for (int i = 0; i < 128; ++i) {
		mark_entry(&g_front_state.net_runtime_sent_history[i],
			   2000 + i);
	}
	g_front_state.net_runtime_sent_history_write_index = 77;
	memset(g_flight_history, 0xEE, sizeof g_flight_history);
	XVT_ASSERT_INT_EQ(net_session_import_runtime_state(
				  &interface, &app, &session, &group, &host,
				  &local, g_flight_queue, &read, &count, &write,
				  g_flight_slots, &slot_count, &broadcast_seq,
				  broadcast_payload, &broadcast_length,
				  &broadcast_empty, &group_seq, group_payload,
				  &group_length, &group_empty, g_flight_history,
				  &history_write),
			  1);
	XVT_ASSERT_TRUE(interface == &g_fake_direct_play);
	XVT_ASSERT_INT_EQ(memcmp(&app, &k_app_guid, sizeof app), 0);
	XVT_ASSERT_INT_EQ(memcmp(&session, &k_session_guid, sizeof session), 0);
	XVT_ASSERT_INT_EQ(group, GROUP_ID);
	XVT_ASSERT_INT_EQ(host, 55);
	XVT_ASSERT_INT_EQ(local.player_id, LOCAL_ID);
	XVT_ASSERT_INT_EQ(strcmp(local.player_name, "Luke"), 0);
	XVT_ASSERT_INT_EQ(read, 1022);
	XVT_ASSERT_INT_EQ(count, 4);
	XVT_ASSERT_INT_EQ(write, 2);
	static const int in_use[] = {1022, 1023, 0, 1};
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(memcmp(&g_flight_queue[in_use[i]],
					 queued(in_use[i]),
					 sizeof g_flight_queue[0]),
				  0);
	}
	XVT_ASSERT_INT_EQ(g_flight_queue[2].direct_play_id, 0xEEEEEEEE);
	XVT_ASSERT_INT_EQ(g_flight_queue[1021].direct_play_id, 0xEEEEEEEE);
	XVT_ASSERT_INT_EQ(slot_count, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(memcmp(&g_flight_slots[i],
					 peer((unsigned int)i),
					 sizeof g_flight_slots[0]),
				  0);
	}
	XVT_ASSERT_INT_EQ(g_flight_slots[3].direct_play_id, 0xEEEEEEEE);
	XVT_ASSERT_INT_EQ(broadcast_seq, 9);
	XVT_ASSERT_INT_EQ(group_seq, 8);
	XVT_ASSERT_INT_EQ(
		memcmp(broadcast_payload,
		       g_front_state.net_runtime_broadcast_pending_payload
			       .payload,
		       512),
		0);
	XVT_ASSERT_INT_EQ(
		memcmp(group_payload,
		       g_front_state.net_runtime_group_pending_payload.payload,
		       512),
		0);
	XVT_ASSERT_INT_EQ(broadcast_length, 20);
	XVT_ASSERT_INT_EQ(broadcast_empty, 0);
	XVT_ASSERT_INT_EQ(group_length, 30);
	XVT_ASSERT_INT_EQ(group_empty, 1);
	XVT_ASSERT_INT_EQ(memcmp(g_flight_history,
				 g_front_state.net_runtime_sent_history,
				 sizeof g_flight_history),
			  0);
	XVT_ASSERT_INT_EQ(history_write, 77);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handoff", "queued"), 4);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handoff", "peers"), 3);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handoff_state", "read"),
			  1022);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handoff_state", "write"),
			  2);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_handoff_state", "broadcast_seq"), 9);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_handoff_state", "group_seq"), 8);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handoff_state", "history"),
			  77);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handoff_state", "host"),
			  55);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handoff_state", "group"),
			  GROUP_ID);
}

/* The hand-back from the flight session copies the local player, the receive
 * queue with its indices and count, only the entries in use and each at its own
 * index, the peer slots in use with each last_heard_ms set to the current time,
 * both counters with their trailers and the whole sent history, replacing what
 * the lobby kept, and keeps the flight's world-message history and its write
 * index; the interface, GUIDs, group and host are ignored. It returns 1 and
 * logs the hand-back. */
static void check_hand_back_from_flight(void)
{
	struct net_player_info local;
	int32_t read = 1022;
	int count = 4;
	int write = 2;
	int slot_count = 3;
	int broadcast_seq = 9;
	int group_seq = 8;
	char broadcast_payload[512];
	char group_payload[512];
	int broadcast_length = 20;
	int broadcast_empty = 0;
	int group_length = 30;
	int group_empty = 1;
	int history_write = 77;
	int world_write = 33;
	fresh();
	memset(&local, 0, sizeof local);
	local.player_id = 4321;
	strcpy(local.player_name, "Wedge");
	for (int i = 0; i < 1024; ++i) {
		mark_entry(&g_flight_queue[i], i);
		mark_entry(queued(i), 5000 + i);
	}
	for (int i = 0; i < 40; ++i) {
		memset(&g_flight_slots[i], 0, sizeof g_flight_slots[0]);
		g_flight_slots[i].direct_play_id = 40 + i;
		g_flight_slots[i].packet_count = 7 + i;
		g_flight_slots[i].last_heard_ms = 5;
		peer((unsigned int)i)->direct_play_id = 900 + i;
	}
	for (int i = 0; i < 512; ++i) {
		broadcast_payload[i] = (char)i;
		group_payload[i] = (char)(3 * i);
	}
	for (int i = 0; i < 128; ++i) {
		mark_entry(&g_flight_history[i], 2000 + i);
		mark_entry(&g_front_state.net_runtime_sent_history[i],
			   7000 + i);
	}
	xvt_time_advance_host_clock(SECOND_US);
	void *interface = NULL;
	int group = 0;
	int host = 0;
	memset(g_world_history, 0, sizeof g_world_history);
	XVT_ASSERT_INT_EQ(
		net_session_export_runtime_state(
			&interface, &k_app_guid, &k_session_guid, &group, &host,
			&local, g_flight_queue, &read, &count, &write,
			g_flight_slots, &slot_count, &broadcast_seq,
			broadcast_payload, &broadcast_length, &broadcast_empty,
			&group_seq, group_payload, &group_length, &group_empty,
			g_flight_history, &history_write, g_world_history,
			&world_write),
		1);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_local_player.player_id,
			  4321);
	XVT_ASSERT_INT_EQ(
		strcmp(g_front_state.net_runtime_local_player.player_name,
		       "Wedge"),
		0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_read_index,
			  1022);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 4);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_write_index, 2);
	static const int in_use[] = {1022, 1023, 0, 1};
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(memcmp(queued(in_use[i]),
					 &g_flight_queue[in_use[i]],
					 sizeof g_flight_queue[0]),
				  0);
	}
	XVT_ASSERT_INT_EQ(queued(2)->direct_play_id, 5002 + 100);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(peer((unsigned int)i)->direct_play_id,
				  40 + i);
		XVT_ASSERT_INT_EQ(peer((unsigned int)i)->packet_count, 7 + i);
		XVT_ASSERT_INT_EQ(peer((unsigned int)i)->last_heard_ms,
				  GetTickCount());
	}
	XVT_ASSERT_INT_EQ(peer(3)->direct_play_id, 903);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_seq_counter, 9);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_group_seq_counter, 8);
	XVT_ASSERT_INT_EQ(
		memcmp(g_front_state.net_runtime_broadcast_pending_payload
			       .payload,
		       broadcast_payload, 512),
		0);
	XVT_ASSERT_INT_EQ(
		memcmp(g_front_state.net_runtime_group_pending_payload.payload,
		       group_payload, 512),
		0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_pending_payload
				  .payload_length,
			  20);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_broadcast_pending_payload
				  .piggyback_empty,
			  0);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_group_pending_payload.payload_length,
		30);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_runtime_group_pending_payload.piggyback_empty,
		1);
	XVT_ASSERT_INT_EQ(memcmp(g_front_state.net_runtime_sent_history,
				 g_flight_history, sizeof g_flight_history),
			  0);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_sent_history_write_index,
			  77);
	XVT_ASSERT_TRUE(g_front_state.net_flight_sent_world_message_history ==
			g_world_history);
	XVT_ASSERT_INT_EQ(
		g_front_state.net_flight_sent_world_message_write_index, 33);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handback", "queued"), 4);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handback", "peers"), 3);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handback_state", "read"),
			  1022);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handback_state", "write"),
			  2);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_handback_state", "broadcast_seq"), 9);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_handback_state", "group_seq"), 8);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_handback_state", "history"),
			  77);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_handback_state", "world_history"),
		33);
}

/* Marking a player ready logs it, and with DirectPlay open an id that is not
 * in the roster is refused with a warning; clearing a flag logs it. */
static void check_ready_logs(void)
{
	fresh();
	g_front_state.net_players[1].player_id = 30;
	g_front_state.net_player_count = 2;
	net_mark_player_ready_no_lock(30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_roster_ready", "player"),
			  30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_roster_ready", "ready"), 1);
	net_mark_player_ready_no_lock(99);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_roster_ready_refused"), 0);
	open_session();
	net_mark_player_ready_no_lock(99);
	XVT_ASSERT_INT_EQ(count_lines("network.lobby_roster_ready_refused"), 1);
	XVT_ASSERT_INT_EQ(
		line_value("network.lobby_roster_ready_refused", "player"), 99);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 1);
	net_clear_player_ready_flag(30);
	XVT_ASSERT_INT_EQ(line_value("network.lobby_roster_ready", "ready"), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 0);
}

/* With every peer slot taken, a player with none is asked about without
 * adding one: its counts are the link figures entry's alone, and its loss rate
 * counts the entry as the host or any other player does. */
static void check_counts_without_slot(void)
{
	fresh();
	fill_peer_table();
	g_net_player_connection_stats[0].player_id = 999;
	g_net_player_connection_stats[0].packet_count = 10;
	g_net_player_connection_stats[0].packet_drop_count = 1;
	g_net_player_connection_stats[0].packet_retry_count = 1;
	XVT_ASSERT_INT_EQ(net_get_player_packet_count(999), 10);
	XVT_ASSERT_INT_EQ(net_get_player_packet_drop_count(999), 1);
	XVT_ASSERT_INT_EQ(net_get_player_packet_retry_count(999), 1);
	g_front_state.net_is_host = 1;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(999), 3000);
	g_front_state.net_is_host = 0;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(999), 3000);
	XVT_ASSERT_INT_EQ(net_get_player_packet_count(998), 0);
	XVT_ASSERT_INT_EQ(net_get_player_packet_drop_count(998), 0);
	XVT_ASSERT_INT_EQ(net_get_player_packet_retry_count(998), 0);
	g_front_state.net_is_host = 1;
	XVT_ASSERT_INT_EQ(net_get_packet_drop_rate_basis_points(998), 0);
	XVT_ASSERT_INT_EQ(g_front_state.net_reliable_peer_slot_count, 40);
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

/* Known failure resent_copy_first_discarded, issue #112: the dequeue's own
 * comment says a resent copy that arrives in time fills the gap. A resent copy
 * first in the queue, ahead of the sequence expected, is thrown away instead,
 * so the packet behind it asks its sender again for a sequence the copy held.
 * With the copy of 4 first and a packet of 5 behind it, expecting 3, only 3
 * should be asked for. */
static void check_resent_copy_first_kept(void)
{
	fresh();
	open_session();
	unsigned int slot = net_find_or_create_peer_slot(30);
	*delivered_on(peer(slot), 1) = 2;
	push_packet(30, 1, 4, 1, NET_PACKET_CHAT, 1);
	push_packet(30, 1, 5, 0, NET_PACKET_CHAT, 2);
	XVT_ASSERT_TRUE(dequeue() == NULL);
	XVT_ASSERT_INT_EQ(g_front_state.net_runtime_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(nacks_sent(), 1);
}

int main(int argc, char **argv)
{
	fail_after_seconds(30);
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
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
			{"resent_copy_first_discarded",
			 check_resent_copy_first_kept},
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
	check_pump_back_buffer();
	check_pump_system_messages();
	check_pump_misaddressed_and_ping();
	check_pump_keepalive_ack_latency();
	check_pump_keepalive_ack_new_entry();
	check_pump_world_nack();
	check_pump_nack();
	check_pump_keepalive();
	check_pump_resent_copies();
	check_pump_resent_copy_sizes();
	check_pump_resent_resync_copy();
	check_pump_previous_missed();
	check_pump_resync_types_bare();
	check_pump_packet_channels();
	check_pump_long_bodies();
	check_silent_peer_on_host();
	check_silent_peer_without_slot();
	check_silent_host_on_client();
	check_pump_keepalives_and_silent_peers();
	check_dequeue_in_order();
	check_dequeue_sends_keepalives();
	check_dequeue_system_messages();
	check_dequeue_drops_flight_packets();
	check_dequeue_drops_stale_and_slotless();
	check_dequeue_defers_busy_peer();
	check_dequeue_asks_for_missing();
	check_dequeue_gives_up_on_gap();
	check_dequeue_gives_up_across_wrap();
	check_dequeue_long_timeout_mode();
	check_dequeue_gap_filled_by_copies();
	check_dequeue_under_pressure();
	check_system_player_created();
	check_system_player_destroyed_on_host();
	check_system_player_destroyed_on_client();
	check_system_player_renamed();
	check_roster_refresh();
	check_create_player();
	check_sequenced_send_layout();
	check_sequenced_send_to_self();
	check_control_send();
	check_broadcast_and_group_layout();
	check_local_copy_dropped_logged();
	check_send_logs_and_back_buffer();
	check_shutdown_session();
	check_polls();
	check_next_app_packet();
	check_hand_over_to_flight();
	check_hand_back_from_flight();
	check_ready_logs();
	check_counts_without_slot();
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

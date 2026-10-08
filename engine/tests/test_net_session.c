/* Tests for xvt/net/net_session.c, the original flight's network session: its
 * opening, roster and lookups, its sends on the three channels, the receive
 * pump that reads DirectPlay, the receive queue's delivery order with its
 * resend requests, waits and full-queue pass, and the host's answers to
 * DirectPlay's system messages. Each check sets the session it needs in
 * g_net_session and fills the receive queue itself, entry by entry, or through
 * the receive pump. The session is opened once early on as a host flying
 * alone, which is what makes later system messages take the flight's
 * branches; the checks of the other branches run before it. No game data is
 * read.
 *
 * Packets sent with no DirectPlay interface only come back to this player's own
 * queue. Where a function needs DirectPlay, the checks give the session a stand
 * in that keeps every packet sent, hands the receive pump the messages a check
 * put in its inbox, and answers every other call with success; no packet
 * leaves the machine. The game packets in the inbox are built by the
 * session's own senders: a check sends a packet to player 200, then puts those
 * bytes in the inbox as if player 200 had sent them to this player. The checks
 * also read the lines the code logs, kept by a log sink with DEBUG lines let
 * through.
 *
 * Not checked here: lines no input reaches. Nothing sets the pump's count of
 * packets to skip. The receive's full-queue pass always returns the queue's
 * first entry: the first pass looked at that entry first and, having returned
 * nothing, had asked for its gap, so the pass's own stale test, its skip of an
 * entry never asked about and its log of a stalled queue never run. */
#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/compat/dplay.h"
#include "test_assert.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/timing/host_clock.h"

enum {
	LOCAL_DPID = 1,
	PEER_DPID = 200,
	OTHER_DPID = 300,
	GROUP_DPID = 2000,
	SECOND_US = 1000000,
	MS_US = 1000,
	MARK_WORD = 1, /* Payload word that tells test packets apart */
	FAKE_MESSAGES = 32,
	LINE_CAPACITY = 4096,
	LINE_SIZE = 256,
	/* A peer slot's size in 8-byte words, the unit of the value the
	 * system-message handler returns when it frees a slot */
	SLOT_WORDS = sizeof(struct net_reliable_peer_slot) / 8,
};

/* Byte offsets in a packet as sent. A game or control packet: the header
 * word, the body's length, the body. A resent packet: the header word, the
 * channel byte, the body's length, the body. */
enum {
	GAME_LENGTH = 2,
	GAME_BODY = 4,
	RESEND_CLASS = 2,
	RESEND_BODY = 5,
};

/* ------------------------------------------------------------------------ */
/* A DirectPlay stand in. */

struct fake_message {
	DPID from;
	DPID to;
	uint32_t size;
	uint8_t bytes[1024];
};

/* Every send is counted; the last one's destination and bytes are kept, and
 * the last FAKE_MESSAGES sends whole, send n at n % FAKE_MESSAGES. */
static int g_sent_count;
static DPID g_sent_to;
static uint8_t g_sent_bytes[1024];
static struct fake_message g_sent[FAKE_MESSAGES];
static HRESULT g_send_result; /* What Send returns */
static int g_removed_count;
static DPID g_removed_player;
static int g_added_count;
static DPID g_added_players[8];
static int g_caps_calls;
/* The messages Receive hands out, in order, and the next one. */
static struct fake_message g_inbox[FAKE_MESSAGES];
static int g_inbox_count;
static int g_inbox_next;

static HRESULT AERON_DXAPI fake_send(IDirectPlay2A *self, DPID from_id,
				     DPID to_id, uint32_t flags, void *data,
				     uint32_t data_size)
{
	(void)self;
	(void)flags;
	XVT_ASSERT_TRUE(data_size <= sizeof g_sent_bytes);
	struct fake_message *message = &g_sent[g_sent_count % FAKE_MESSAGES];
	message->from = from_id;
	message->to = to_id;
	message->size = data_size;
	memcpy(message->bytes, data, data_size);
	++g_sent_count;
	g_sent_to = to_id;
	memcpy(g_sent_bytes, data, data_size);
	return g_send_result;
}

static HRESULT AERON_DXAPI fake_receive(IDirectPlay2A *self, DPID *from_id,
					DPID *to_id, uint32_t flags, void *data,
					uint32_t *data_size)
{
	(void)self;
	(void)flags;
	if (g_inbox_next >= g_inbox_count) {
		return DPERR_NOMESSAGES;
	}
	const struct fake_message *message = &g_inbox[g_inbox_next++];
	XVT_ASSERT_TRUE(message->size <= *data_size);
	*from_id = message->from;
	*to_id = message->to;
	*data_size = message->size;
	memcpy(data, message->bytes, message->size);
	return 0;
}

static HRESULT AERON_DXAPI fake_get_caps(IDirectPlay2A *self, DPCAPS *caps,
					 uint32_t flags)
{
	(void)self;
	(void)flags;
	++g_caps_calls;
	caps->dwMaxBufferSize = 1024;
	caps->dwMaxPlayers = 8;
	return 0;
}

static HRESULT AERON_DXAPI fake_add_player_to_group(IDirectPlay2A *self,
						    DPID group_id,
						    DPID player_id)
{
	(void)self;
	(void)group_id;
	XVT_ASSERT_TRUE(g_added_count < 8);
	g_added_players[g_added_count++] = player_id;
	return 0;
}

static HRESULT AERON_DXAPI fake_delete_player_from_group(IDirectPlay2A *self,
							 DPID group_id,
							 DPID player_id)
{
	(void)self;
	(void)group_id;
	++g_removed_count;
	g_removed_player = player_id;
	return 0;
}

/* The players the stand in lists, in order. */
static DPID g_listed_ids[10];
static uint32_t g_listed_types[10];
static DPNAME g_listed_names[10];
static int g_listed_count;

static HRESULT AERON_DXAPI fake_enum_players(IDirectPlay2A *self,
					     GUID *instance_guid,
					     DPEnumPlayersCallback2 callback,
					     void *context, uint32_t flags)
{
	(void)self;
	(void)instance_guid;
	(void)flags;
	for (int i = 0; i < g_listed_count; ++i) {
		if (!callback(g_listed_ids[i], g_listed_types[i],
			      &g_listed_names[i], 0, context)) {
			break;
		}
	}
	return 0;
}

static IDirectPlay2AVtbl g_fake_vtbl;
static IDirectPlay2A g_fake_dplay = {&g_fake_vtbl};

static void use_fake_dplay(void)
{
	memset(&g_fake_vtbl, 0, sizeof g_fake_vtbl);
	g_fake_vtbl.Send = fake_send;
	g_fake_vtbl.Receive = fake_receive;
	g_fake_vtbl.GetCaps = fake_get_caps;
	g_fake_vtbl.AddPlayerToGroup = fake_add_player_to_group;
	g_fake_vtbl.DeletePlayerFromGroup = fake_delete_player_from_group;
	g_fake_vtbl.EnumPlayers = fake_enum_players;
	g_net_session.dplay_interface = &g_fake_dplay;
	g_sent_count = 0;
	g_sent_to = 0;
	g_send_result = 0;
	g_removed_count = 0;
	g_removed_player = 0;
	g_added_count = 0;
	g_caps_calls = 0;
	g_listed_count = 0;
	g_inbox_count = 0;
	g_inbox_next = 0;
}

/* Starts counting sends again from 0. */
static void forget_sends(void) { g_sent_count = 0; }

/* Send number index, which the stand in still keeps. */
static const struct fake_message *sent_message(int index)
{
	XVT_ASSERT_TRUE(index >= 0 && index < g_sent_count &&
			g_sent_count - index <= FAKE_MESSAGES);
	return &g_sent[index % FAKE_MESSAGES];
}

/* The 16-bit and 32-bit values at offset in send number index. */
static int sent_short(int index, int offset)
{
	uint16_t value;
	memcpy(&value, sent_message(index)->bytes + offset, sizeof value);
	return value;
}

static int sent_word(int index, int offset)
{
	int value;
	memcpy(&value, sent_message(index)->bytes + offset, sizeof value);
	return value;
}

/* The type, sequence and channel bits (0x80 and 0x8000) of the header of
 * send number index. */
static int sent_type(int index) { return sent_short(index, 0) & 0x7F; }

static int sent_sequence(int index)
{
	return (sent_short(index, 0) >> 8) & 0x7F;
}

static int sent_channel_bits(int index)
{
	return sent_short(index, 0) & 0x8080;
}

/* Puts a message of size bytes in the inbox, from and to the given ids. */
static void inbox_message(DPID from_id, DPID to_id, const void *bytes,
			  uint32_t size)
{
	XVT_ASSERT_TRUE(g_inbox_count < FAKE_MESSAGES);
	XVT_ASSERT_TRUE(size <= sizeof g_inbox[0].bytes);
	struct fake_message *message = &g_inbox[g_inbox_count++];
	message->from = from_id;
	message->to = to_id;
	message->size = size;
	memcpy(message->bytes, bytes, size);
}

/* Puts the last send in the inbox as a packet from PEER_DPID to this
 * player. */
static void deliver_last_send(void)
{
	const struct fake_message *sent = sent_message(g_sent_count - 1);
	inbox_message(PEER_DPID, LOCAL_DPID, sent->bytes, sent->size);
}

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

/* The number of kept lines that start with start: an event name, with any
 * of its first values. */
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

/* The number after " key=" in the last kept line that starts with start;
 * the check fails when there is none. */
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
/* The session and its queue. */

static void empty_queue(void)
{
	g_net_recv_queue_count = 0;
	g_net_recv_queue_read_index = 0;
	g_net_recv_queue_write_index = 0;
}

/* An empty queue whose entries go to index start, start + 1 and on around
 * the ring. */
static void empty_queue_at(int start)
{
	g_net_recv_queue_count = 0;
	g_net_recv_queue_read_index = start;
	g_net_recv_queue_write_index = start;
}

/* A session with no DirectPlay interface whose roster holds this player alone,
 * id 1, active. No peer slot is in use, the queue and both sent histories are
 * empty, internet play is off and the host clock reads one second. The log
 * lines kept so far are dropped. */
static void session_world(int host)
{
	memset(&g_net_session, 0, sizeof g_net_session);
	g_net_session.local_is_host = host;
	g_net_session.host_dplay_id = host ? LOCAL_DPID : PEER_DPID;
	g_net_session.local_player_info.direct_play_id = LOCAL_DPID;
	g_net_session.local_player_info.active_flag = 1;
	strcpy(g_net_session.local_player_info.player_name, "Local");
	g_net_session.players[0] = g_net_session.local_player_info;
	g_net_session.player_count = 1;
	g_net_session.broadcast_piggyback_empty = 1;
	g_net_session.group_piggyback_empty = 1;
	empty_queue();
	memset(g_net_session_sent_history, 0,
	       sizeof g_net_session_sent_history);
	memset(g_net_session_sent_world_message_history, 0,
	       sizeof g_net_session_sent_world_message_history);
	g_net_session_sent_history_write_index = 0;
	g_net_session_sent_world_message_write_index = 0;
	g_game_config.internet_play = 0;
	xvt_time_reset();
	xvt_time_advance_host_clock(SECOND_US);
	forget_lines();
}

/* A client session (the host is PEER_DPID) with the DirectPlay stand in and
 * the group GROUP_DPID. */
static void pump_world(void)
{
	session_world(0);
	use_fake_dplay();
	g_net_session.group_dplay_id = GROUP_DPID;
}

/* Appends an 8-byte packet of the given type to the receive queue, from
 * sender on channel packet_class (0 all players, 1 direct, 2 group) with
 * sequence; mark goes in the payload's second word to tell packets apart. */
static void queue_packet(int sender, int packet_class, int sequence, int resent,
			 int type, int mark)
{
	struct net_queued_packet *entry =
		&g_net_session_recv_queue[g_net_recv_queue_write_index];
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = (DPID)sender;
	entry->packet_class = (uint8_t)packet_class;
	entry->sequence_byte = (uint8_t)sequence;
	entry->is_resent_copy = (uint8_t)resent;
	entry->payload_size = 8;
	memcpy(entry->payload, &type, sizeof type);
	memcpy(entry->payload + 4 * MARK_WORD, &mark, sizeof mark);
	g_net_recv_queue_write_index =
		(g_net_recv_queue_write_index + 1) % 1024;
	++g_net_recv_queue_count;
}

/* The queue entry position places after the read index, and a word of its
 * payload, the type being word 0. */
static struct net_queued_packet *queued(int position)
{
	return &g_net_session_recv_queue[(g_net_recv_queue_read_index +
					  position) %
					 1024];
}

static int queued_word(int position, int word)
{
	int value;
	memcpy(&value, queued(position)->payload + 4 * word, sizeof value);
	return value;
}

/* Receives one packet and returns its mark, or -1 when none is ready. */
static int receive_mark(void)
{
	int sender = -1;
	int size = -1;
	int *packet = net_session_receive_packet(&sender, &size);
	if (packet == NULL) {
		return -1;
	}
	return packet[MARK_WORD];
}

/* Gives PEER_DPID a peer slot whose last delivered sequence on the direct
 * channel is last_delivered. */
static struct net_reliable_peer_slot *peer_slot(int last_delivered)
{
	unsigned int slot = net_reliable_find_or_create_peer_slot(PEER_DPID);
	XVT_ASSERT_INT_EQ(slot, 0);
	g_net_session.reliable_peer_slots[slot].last_delivered_seq_default =
		last_delivered;
	return &g_net_session.reliable_peer_slots[slot];
}

/* The peer slot of id, made when it has none. */
static struct net_reliable_peer_slot *slot_of(int id)
{
	unsigned int slot = net_reliable_find_or_create_peer_slot(id);
	XVT_ASSERT_TRUE(slot < 40);
	return &g_net_session.reliable_peer_slots[slot];
}

/* A slot's last delivered and newest received sequences on the channel
 * packet_class: 0 all players, 2 group, else direct. */
static int *last_delivered(struct net_reliable_peer_slot *slot,
			   int packet_class)
{
	if (packet_class == 0) {
		return &slot->last_delivered_seq_channel_a;
	}
	if (packet_class == 2) {
		return &slot->last_delivered_seq_channel_b;
	}
	return &slot->last_delivered_seq_default;
}

static int *newest_received(struct net_reliable_peer_slot *slot,
			    int packet_class)
{
	if (packet_class == 0) {
		return &slot->recv_seq_channel_a;
	}
	if (packet_class == 2) {
		return &slot->recv_seq_channel_b;
	}
	return &slot->recv_seq_default;
}

/* Sets the next sequence of the channel a packet to id goes on: all players
 * for 0, the group for GROUP_DPID, else id's peer slot. */
static void set_next_sequence(int id, int sequence)
{
	if (id == 0) {
		g_net_session.broadcast_seq_counter = (uint32_t)sequence;
	} else if (id == GROUP_DPID) {
		g_net_session.group_seq_counter = (uint32_t)sequence;
	} else {
		slot_of(id)->send_seq = sequence;
	}
}

static int next_sequence(int id)
{
	if (id == 0) {
		return (int)g_net_session.broadcast_seq_counter;
	}
	if (id == GROUP_DPID) {
		return (int)g_net_session.group_seq_counter;
	}
	return slot_of(id)->send_seq;
}

/* Fills a four-word packet: its type, then three words. */
static void make_packet(unsigned int *packet, int type, int word1, int word2,
			int word3)
{
	packet[0] = (unsigned int)type;
	packet[1] = (unsigned int)word1;
	packet[2] = (unsigned int)word2;
	packet[3] = (unsigned int)word3;
}

/* Sends id a packet of size bytes, 4 to 16: type, then mark. */
static void send_marked(int id, int type, int mark, int size)
{
	unsigned int packet[4];
	make_packet(packet, type, mark, 0, 0);
	XVT_ASSERT_INT_EQ(net_session_send_packet(id, packet, size), 1);
}

/* Puts in the inbox a control packet of size bytes, 4 to 16, from PEER_DPID:
 * type, then the three words. */
static void deliver_control(int type, int word1, int word2, int word3, int size)
{
	unsigned int packet[4];
	make_packet(packet, type, word1, word2, word3);
	XVT_ASSERT_INT_EQ(net_session_send_compact_game_packet(PEER_DPID,
							       packet, size, 1),
			  1);
	deliver_last_send();
}

/* Gives every field of the slot but its id a value no new slot has, and
 * checks that a slot holds those values. */
static void scramble_slot(struct net_reliable_peer_slot *slot)
{
	slot->last_delivered_seq_default = 11;
	slot->recv_seq_default = 12;
	slot->last_delivered_seq_channel_a = 13;
	slot->recv_seq_channel_a = 14;
	slot->last_delivered_seq_channel_b = 15;
	slot->recv_seq_channel_b = 16;
	slot->send_seq = 17;
	slot->last_piggyback_type = NET_PACKET_CHAT;
	slot->piggyback_length = 18;
	slot->last_activity_ms = 19;
	slot->packet_count = 20;
	slot->packet_drop_count = 21;
}

static void assert_slot_scrambled(const struct net_reliable_peer_slot *slot)
{
	XVT_ASSERT_INT_EQ(slot->last_delivered_seq_default, 11);
	XVT_ASSERT_INT_EQ(slot->recv_seq_default, 12);
	XVT_ASSERT_INT_EQ(slot->last_delivered_seq_channel_a, 13);
	XVT_ASSERT_INT_EQ(slot->recv_seq_channel_a, 14);
	XVT_ASSERT_INT_EQ(slot->last_delivered_seq_channel_b, 15);
	XVT_ASSERT_INT_EQ(slot->recv_seq_channel_b, 16);
	XVT_ASSERT_INT_EQ(slot->send_seq, 17);
	XVT_ASSERT_INT_EQ(slot->last_piggyback_type, NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(slot->piggyback_length, 18);
	XVT_ASSERT_INT_EQ(slot->last_activity_ms, 19);
	XVT_ASSERT_INT_EQ(slot->packet_count, 20);
	XVT_ASSERT_INT_EQ(slot->packet_drop_count, 21);
}

/* A free slot: id 0, every sequence 127, send sequence 0, a one-byte NOP
 * trailer, activity time and counts 0. */
static void assert_slot_free(const struct net_reliable_peer_slot *slot)
{
	XVT_ASSERT_INT_EQ(slot->direct_play_id, 0);
	XVT_ASSERT_INT_EQ(slot->last_delivered_seq_default, 127);
	XVT_ASSERT_INT_EQ(slot->recv_seq_default, 127);
	XVT_ASSERT_INT_EQ(slot->last_delivered_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(slot->recv_seq_channel_a, 127);
	XVT_ASSERT_INT_EQ(slot->last_delivered_seq_channel_b, 127);
	XVT_ASSERT_INT_EQ(slot->recv_seq_channel_b, 127);
	XVT_ASSERT_INT_EQ(slot->send_seq, 0);
	XVT_ASSERT_INT_EQ(slot->last_piggyback_type, NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(slot->piggyback_length, 1);
	XVT_ASSERT_INT_EQ(slot->last_activity_ms, 0);
	XVT_ASSERT_INT_EQ(slot->packet_count, 0);
	XVT_ASSERT_INT_EQ(slot->packet_drop_count, 0);
}

/* Has the stand in list the given players, each a player named "Short", long
 * name "Long", and puts the same ids in the pilot record's network players. */
static void list_players(const DPID *ids, int count)
{
	static char short_name[] = "Short";
	static char long_name[] = "Long";
	memset(g_pilot_data.network_players, 0,
	       sizeof g_pilot_data.network_players);
	for (int i = 0; i < count; ++i) {
		g_listed_ids[i] = ids[i];
		g_listed_types[i] = DPPLAYERTYPE_PLAYER;
		g_listed_names[i].lpszShortNameA = short_name;
		g_listed_names[i].lpszLongNameA = long_name;
		g_pilot_data.network_players[i].direct_play_id = ids[i];
	}
	g_listed_count = count;
}

/* ------------------------------------------------------------------------ */
/* Opening the session, and the roster. */

/* A host flying alone with no DirectPlay interface takes the names it is given,
 * id 1, and slot 0 of a one-player roster. */
static void check_solo_init(void)
{
	memset(&g_net_session, 0, sizeof g_net_session);
	g_net_recv_queue_count = 9;
	XVT_ASSERT_INT_EQ(net_session_init_game_session("Formal", "Pilot", 1,
							NULL, NET_TRANSPORT_IPX,
							1, 0, NULL),
			  1);
	XVT_ASSERT_INT_EQ(net_session_is_local_host(), 1);
	XVT_ASSERT_INT_EQ(net_session_get_local_dplay_id(), 1);
	XVT_ASSERT_INT_EQ(net_session_get_player_count(), 1);
	XVT_ASSERT_INT_EQ(g_net_session.players[0].direct_play_id, 1);
	XVT_ASSERT_INT_EQ(g_net_session.players[0].active_flag, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[0].long_name, "Formal"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[0].player_name, "Pilot"),
			  0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 0);
}

/* Puts three players in the roster: this one, 200 (active) and 300 (not). */
static void three_player_roster(void)
{
	g_net_session.players[1].direct_play_id = PEER_DPID;
	g_net_session.players[1].active_flag = 1;
	strcpy(g_net_session.players[1].player_name, "Wedge");
	g_net_session.players[2].direct_play_id = 300;
	g_net_session.players[2].active_flag = 0;
	strcpy(g_net_session.players[2].player_name, "Biggs");
	g_net_session.player_count = 3;
}

static void check_roster_lookups(void)
{
	int count = -1;
	session_world(0);
	three_player_roster();
	XVT_ASSERT_TRUE(net_session_get_player_roster(&count) ==
			g_net_session.players);
	XVT_ASSERT_INT_EQ(count, 3);
	XVT_ASSERT_INT_EQ(net_session_get_player_count(), 3);
	XVT_ASSERT_INT_EQ(net_session_find_player_slot_by_dpid(300), 2);
	XVT_ASSERT_INT_EQ(net_session_find_player_slot_by_dpid(LOCAL_DPID), 0);
	XVT_ASSERT_INT_EQ(net_session_find_player_slot_by_dpid(999), 8);
	XVT_ASSERT_INT_EQ(net_session_get_host_dplay_id(), PEER_DPID);
	XVT_ASSERT_INT_EQ(net_session_get_local_dplay_id(), LOCAL_DPID);
	XVT_ASSERT_INT_EQ(net_session_is_local_host(), 0);
	/* Only entries whose active flag is exactly 1 count. */
	g_net_session.players[3].active_flag = 2;
	XVT_ASSERT_INT_EQ(net_session_count_active_players(), 2);
	XVT_ASSERT_INT_EQ(net_session_get_fixed_payload_size(NET_PACKET_CHAT),
			  0);
	XVT_ASSERT_INT_EQ(net_session_stub_return_true(), 1);
	/* An empty roster still counts this player. */
	g_net_session.player_count = 0;
	XVT_ASSERT_INT_EQ(net_session_get_player_count(), 1);
}

/* A slot's name is found by its DirectPlay id; with one player, it is this
 * player's name whatever the slot; with no match, NULL. */
static void check_player_names(void)
{
	session_world(0);
	three_player_roster();
	XVT_ASSERT_INT_EQ(strcmp(net_session_get_player_name(2), "Biggs"), 0);
	XVT_ASSERT_INT_EQ(strcmp(net_session_get_player_name(1), "Wedge"), 0);
	XVT_ASSERT_TRUE(net_session_get_player_name(5) == NULL);
	g_net_session.player_count = 1;
	XVT_ASSERT_INT_EQ(strcmp(net_session_get_player_name(5), "Local"), 0);
}

/* The listing adds DirectPlay players the pilot record also holds, with
 * their names cut to 15 characters, and skips the rest. */
static void check_enumerate_players(void)
{
	static char long_name[] = "A long name of twenty";
	static char short_name[] = "Short";
	session_world(1);
	use_fake_dplay();
	memset(g_pilot_data.network_players, 0,
	       sizeof g_pilot_data.network_players);
	g_pilot_data.network_players[3].direct_play_id = PEER_DPID;
	g_pilot_data.network_players[5].direct_play_id = 300;
	DPID ids[] = {PEER_DPID, 400, 300, 300};
	uint32_t types[] = {1, 1, 0, 1};
	for (int i = 0; i < 4; ++i) {
		g_listed_ids[i] = ids[i];
		g_listed_types[i] = types[i];
		g_listed_names[i].lpszLongNameA = long_name;
		g_listed_names[i].lpszShortNameA = short_name;
	}
	g_listed_count = 4;
	g_net_session.player_count = 0;
	XVT_ASSERT_INT_EQ(net_session_enumerate_players(), 1);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 2);
	XVT_ASSERT_INT_EQ(g_net_session.players[0].direct_play_id, PEER_DPID);
	XVT_ASSERT_INT_EQ(g_net_session.players[1].direct_play_id, 300);
	XVT_ASSERT_INT_EQ(g_net_session.players[1].active_flag, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[0].player_name, "Short"),
			  0);
	XVT_ASSERT_INT_EQ(strlen(g_net_session.players[0].long_name), 15);
	XVT_ASSERT_INT_EQ(
		strncmp(g_net_session.players[0].long_name, long_name, 15), 0);
	memset(g_pilot_data.network_players, 0,
	       sizeof g_pilot_data.network_players);
}

/* The listing stops once 8 players are in. */
static void check_enumerate_stops_at_eight(void)
{
	static DPNAME name = {0, 0, "Name", "Name"};
	session_world(1);
	g_net_session.player_count = 8;
	XVT_ASSERT_INT_EQ(
		net_session_enum_players_callback(PEER_DPID, 1, &name, 0, NULL),
		0);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 8);
}

/* ------------------------------------------------------------------------ */
/* Sending to this player, and the receive queue. */

/* With no DirectPlay interface, packets sent to all players come back to this
 * player's queue and are received in the order sent. */
static void check_send_comes_back_in_order(void)
{
	unsigned int packet[2];
	session_world(1);
	for (unsigned int mark = 0; mark < 3; ++mark) {
		packet[0] = NET_PACKET_CHAT;
		packet[1] = 10 + mark;
		XVT_ASSERT_INT_EQ(net_session_send_packet(0, packet, 8), 1);
	}
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 3);
	/* Each took the next sequence of the all-players channel. */
	XVT_ASSERT_INT_EQ(g_net_session.broadcast_seq_counter, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(g_net_session_recv_queue[i].packet_class, 0);
		XVT_ASSERT_INT_EQ(g_net_session_recv_queue[i].sequence_byte, i);
	}
	for (int mark = 0; mark < 3; ++mark) {
		int sender = -1;
		int size = -1;
		int *received = net_session_receive_packet(&sender, &size);
		XVT_ASSERT_TRUE(received != NULL);
		XVT_ASSERT_INT_EQ(received[0], NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(received[MARK_WORD], 10 + mark);
		XVT_ASSERT_INT_EQ(sender, LOCAL_DPID);
		XVT_ASSERT_INT_EQ(size, 8);
	}
	XVT_ASSERT_TRUE(receive_mark() == -1);
	/* Under 4 bytes nothing is sent. */
	XVT_ASSERT_INT_EQ(net_session_send_packet(0, packet, 3), 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
}

/* With DirectPlay, a packet to another player goes to DirectPlay only. One to
 * all players or to the session group goes out and also comes back, on the
 * all-players or the group channel; one to this player comes back on the
 * direct channel and is not handed to DirectPlay. */
static void check_send_routes(void)
{
	unsigned int packet[2] = {NET_PACKET_CHAT, 0};
	session_world(1);
	use_fake_dplay();
	g_net_session.group_dplay_id = 50;
	XVT_ASSERT_INT_EQ(net_session_send_packet(PEER_DPID, packet, 8), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(g_sent_to, PEER_DPID);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(net_session_send_packet(0, packet, 8), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[0].packet_class, 0);
	XVT_ASSERT_INT_EQ(net_session_send_packet(50, packet, 8), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 3);
	XVT_ASSERT_INT_EQ(g_sent_to, 50);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[1].packet_class, 2);
	XVT_ASSERT_INT_EQ(net_session_send_packet(LOCAL_DPID, packet, 8), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 3);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 3);
	XVT_ASSERT_INT_EQ(g_net_session_recv_queue[2].packet_class, 1);
	XVT_ASSERT_INT_EQ(g_net_session.broadcast_seq_counter, 1);
	XVT_ASSERT_INT_EQ(g_net_session.group_seq_counter, 1);
}

/* The next packet on a new peer's direct channel is sequence 0; it is
 * delivered with its sender and size, and counted on the peer's slot. */
static void check_receive_next_expected(void)
{
	int sender = -1;
	int size = -1;
	session_world(0);
	queue_packet(PEER_DPID, 1, 0, 0, NET_PACKET_CHAT, 5);
	int *packet = net_session_receive_packet(&sender, &size);
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(packet[MARK_WORD], 5);
	XVT_ASSERT_INT_EQ(sender, PEER_DPID);
	XVT_ASSERT_INT_EQ(size, 8);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(
		g_net_session.reliable_peer_slots[0].last_delivered_seq_default,
		0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slots[0].packet_count, 1);
	XVT_ASSERT_TRUE(net_session_receive_packet(&sender, &size) == NULL);
}

/* Each channel keeps its own sequence: sequence 0 is next on all three. */
static void check_receive_channels(void)
{
	session_world(0);
	queue_packet(PEER_DPID, 0, 0, 0, NET_PACKET_CHAT, 1);
	queue_packet(PEER_DPID, 2, 0, 0, NET_PACKET_CHAT, 2);
	queue_packet(PEER_DPID, 1, 0, 0, NET_PACKET_CHAT, 3);
	XVT_ASSERT_INT_EQ(receive_mark(), 1);
	XVT_ASSERT_INT_EQ(receive_mark(), 2);
	XVT_ASSERT_INT_EQ(receive_mark(), 3);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slots[0]
				  .last_delivered_seq_channel_a,
			  0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slots[0]
				  .last_delivered_seq_channel_b,
			  0);
}

/* Entries 1 to 28 sequence numbers behind the next expected one are dropped,
 * counting around the 128 wrap; 29 behind is kept. */
static void check_receive_drops_stale(void)
{
	session_world(0);
	peer_slot(49);
	queue_packet(PEER_DPID, 1, 22, 0, NET_PACKET_CHAT, 1);
	queue_packet(PEER_DPID, 1, 49, 0, NET_PACKET_CHAT, 2);
	XVT_ASSERT_INT_EQ(receive_mark(), -1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	queue_packet(PEER_DPID, 1, 21, 0, NET_PACKET_CHAT, 3);
	XVT_ASSERT_INT_EQ(receive_mark(), -1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);

	session_world(0);
	peer_slot(5);
	queue_packet(PEER_DPID, 1, 106, 0, NET_PACKET_CHAT, 4);
	XVT_ASSERT_INT_EQ(receive_mark(), -1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	queue_packet(PEER_DPID, 1, 105, 0, NET_PACKET_CHAT, 5);
	XVT_ASSERT_INT_EQ(receive_mark(), -1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
}

/* A packet past a gap waits; the missing one, come back as a resent copy,
 * fills the gap and is delivered first. */
static void check_receive_gap_filled(void)
{
	session_world(0);
	queue_packet(PEER_DPID, 1, 1, 0, NET_PACKET_CHAT, 11);
	XVT_ASSERT_INT_EQ(receive_mark(), -1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
	queue_packet(PEER_DPID, 1, 0, 1, NET_PACKET_CHAT, 10);
	XVT_ASSERT_INT_EQ(receive_mark(), 10);
	XVT_ASSERT_INT_EQ(receive_mark(), 11);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
}

/* A system message is returned from the front of the queue only. */
static void check_receive_system_message(void)
{
	session_world(0);
	queue_packet(PEER_DPID, 1, 1, 0, NET_PACKET_CHAT, 1);
	queue_packet(0, 0, 0, 0, DPSYS_CREATEPLAYERORGROUP, 2);
	XVT_ASSERT_INT_EQ(receive_mark(), -1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);

	session_world(0);
	queue_packet(0, 0, 0, 0, DPSYS_CREATEPLAYERORGROUP, 3);
	int sender = -1;
	int size = -1;
	int *packet = net_session_receive_packet(&sender, &size);
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(packet[MARK_WORD], 3);
	XVT_ASSERT_INT_EQ(sender, 0);
}

/* The game receive hands system messages to the handler and returns the next
 * game packet. */
static void check_receive_game_packet(void)
{
	int sender = -1;
	int size = -1;
	session_world(0);
	queue_packet(0, 0, 0, 0, DPSYS_CREATEPLAYERORGROUP, 1);
	queue_packet(PEER_DPID, 1, 0, 0, NET_PACKET_CHAT, 2);
	int *packet = net_session_receive_game_packet(&sender, &size);
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(packet[MARK_WORD], 2);
	XVT_ASSERT_INT_EQ(sender, PEER_DPID);
	XVT_ASSERT_TRUE(net_session_receive_game_packet(&sender, &size) ==
			NULL);
}

/* With 1,023 entries queued and nothing deliverable, the first entry whose gap
 * was already asked about is delivered past its gap. The first entry, 99,
 * leaves a gap so wide that the peer's other entries are not looked at. */
static void check_receive_full_queue(void)
{
	session_world(0);
	queue_packet(PEER_DPID, 1, 99, 0, NET_PACKET_CHAT, 99);
	for (int i = 1; i < 1023; ++i) {
		queue_packet(PEER_DPID, 1, 100 + i % 20, 0, NET_PACKET_CHAT,
			     1000 + i);
	}
	XVT_ASSERT_INT_EQ(receive_mark(), 99);
	XVT_ASSERT_INT_EQ(
		g_net_session.reliable_peer_slots[0].last_delivered_seq_default,
		99);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1022);
	empty_queue();
}

/* ------------------------------------------------------------------------ */
/* System messages. */

/* A DirectPlay system message: type, player type, player id. */
static void system_message(int *message, int type, int player)
{
	message[0] = type;
	message[1] = DPPLAYERTYPE_PLAYER;
	message[2] = player;
}

/* A host whose roster holds this player, 200 and 300, both active, with peer
 * slots for this player (whose packets to itself come back through one), 200
 * and 300 in that order, and a DirectPlay stand in. */
static void host_with_two_peers(void)
{
	session_world(1);
	use_fake_dplay();
	three_player_roster();
	g_net_session.players[2].active_flag = 1;
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(LOCAL_DPID), 0);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(PEER_DPID), 1);
	XVT_ASSERT_INT_EQ(net_reliable_find_or_create_peer_slot(300), 2);
	g_net_session.reliable_peer_slots[1].packet_count = 4;
	g_net_session.reliable_peer_slots[2].packet_count = 6;
	g_net_session.reliable_peer_slots[2].last_delivered_seq_default = 9;
}

/* A departing player is taken out of the group and marked inactive, the host
 * queues itself a STARTUP_READY, and the last peer slot moves into the
 * departed player's, leaving the old last slot free. */
static void check_host_player_departs(void)
{
	int message[3];
	host_with_two_peers();
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, PEER_DPID);
	net_session_handle_direct_play_system_message(message[0], message);
	XVT_ASSERT_INT_EQ(g_removed_count, 1);
	XVT_ASSERT_INT_EQ(g_removed_player, PEER_DPID);
	XVT_ASSERT_INT_EQ(g_net_session.players[1].active_flag, 0);
	XVT_ASSERT_INT_EQ(g_net_session.players[2].active_flag, 1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
	int type = 0;
	memcpy(&type, g_net_session_recv_queue[0].payload, sizeof type);
	XVT_ASSERT_INT_EQ(type, NET_PACKET_STARTUP_READY);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);
	const struct net_reliable_peer_slot *moved =
		&g_net_session.reliable_peer_slots[1];
	XVT_ASSERT_INT_EQ(moved->direct_play_id, 300);
	XVT_ASSERT_INT_EQ(moved->packet_count, 6);
	XVT_ASSERT_INT_EQ(moved->last_delivered_seq_default, 9);
	const struct net_reliable_peer_slot *freed =
		&g_net_session.reliable_peer_slots[2];
	XVT_ASSERT_INT_EQ(freed->direct_play_id, 0);
	XVT_ASSERT_INT_EQ(freed->last_delivered_seq_default, 127);
	XVT_ASSERT_INT_EQ(freed->packet_count, 0);
}

/* A player who was not active leaves no STARTUP_READY; a client does
 * nothing. */
static void check_departure_without_notice(void)
{
	int message[3];
	host_with_two_peers();
	g_net_session.players[1].active_flag = 0;
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, PEER_DPID);
	net_session_handle_direct_play_system_message(message[0], message);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);

	host_with_two_peers();
	g_net_session.local_is_host = 0;
	net_session_handle_direct_play_system_message(message[0], message);
	XVT_ASSERT_INT_EQ(g_removed_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.players[1].active_flag, 1);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 3);
}

/* A new player gets the sequence status with every peer slot, then the flight
 * session status with the protocol version, both sent to it; a client sends
 * nothing. */
static void check_host_player_joins(void)
{
	int message[3];
	host_with_two_peers();
	system_message(message, DPSYS_CREATEPLAYERORGROUP, 400);
	net_session_handle_direct_play_system_message(message[0], message);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(g_sent_to, 400);
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.packet_type,
			  NET_PACKET_FLIGHT_SESSION_STATUS);
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.payload_dwords[0],
			  mission_get_elapsed_clock_seconds());
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.payload_dwords[1],
			  FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(g_sent_bytes[0] & 0x7f,
			  NET_PACKET_FLIGHT_SESSION_STATUS);

	host_with_two_peers();
	g_net_session.local_is_host = 0;
	net_session_handle_direct_play_system_message(message[0], message);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
}

/* A rename updates the matching roster entry, both names cut to 12
 * characters. */
static void check_player_renamed(void)
{
	struct net_player_name_message message;
	memset(&message, 0, sizeof message);
	message.header.dwType = DPSYS_SETPLAYERORGROUPNAME;
	message.header.dwPlayerType = DPPLAYERTYPE_PLAYER;
	message.header.dpId = 300;
	memcpy(message.names, "ShortNameOf15ch\0LongNameOfFifteen",
	       sizeof "ShortNameOf15ch\0LongNameOfFifteen");
	session_world(0);
	three_player_roster();
	net_session_handle_direct_play_system_message(
		DPSYS_SETPLAYERORGROUPNAME, (const int *)&message);
	XVT_ASSERT_INT_EQ(
		strcmp(g_net_session.players[2].player_name, "ShortNameOf1"),
		0);
	XVT_ASSERT_INT_EQ(
		strcmp(g_net_session.players[2].long_name, "LongNameOfFi"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[1].player_name, "Wedge"),
			  0);
}

/* ------------------------------------------------------------------------ */
/* Keepalives. */

/* A roster player's slot idle for over 3,000 ms gets a KEEPALIVE asking for
 * the sequence after the newest seen on each channel, and the slot's activity
 * time moves to now; one idle for 3,000 ms or less gets none. */
static void check_keepalive(void)
{
	session_world(0);
	use_fake_dplay();
	three_player_roster();
	unsigned int slot = net_reliable_find_or_create_peer_slot(PEER_DPID);
	g_net_session.reliable_peer_slots[slot].recv_seq_channel_a = 4;
	g_net_session.reliable_peer_slots[slot].recv_seq_channel_b = 127;
	g_net_session.reliable_peer_slots[slot].recv_seq_default = 10;
	uint32_t start =
		g_net_session.reliable_peer_slots[slot].last_activity_ms;
	xvt_time_advance_host_clock(3000 * 1000);
	XVT_ASSERT_INT_EQ(net_session_send_reliable_keepalives(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	xvt_time_advance_host_clock(2 * 1000);
	XVT_ASSERT_INT_EQ(net_session_send_reliable_keepalives(), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(g_sent_to, PEER_DPID);
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.packet_type,
			  NET_PACKET_KEEPALIVE);
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.payload_dwords[0], 5);
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.payload_dwords[1], 0);
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.payload_dwords[2], 11);
	XVT_ASSERT_INT_EQ(
		g_net_session.reliable_peer_slots[slot].last_activity_ms,
		start + 3002);
	XVT_ASSERT_INT_EQ(g_sent_bytes[0] & 0x7f, NET_PACKET_KEEPALIVE);
	/* Roster player 300 got a slot too. */
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);
}

/* ------------------------------------------------------------------------ */
/* net_session_handle_direct_play_system_message before any session init:
 * the handshake flag is 0. */

/* Before any session init, a new player is sent the sequence status and an
 * 8-byte FLIGHT_SESSION_STATUS holding 0; then the roster is listed again and
 * every other listed player is added to the group. The arrival of this player
 * itself sends nothing, and a group's changes nothing. */
static void check_join_before_any_session(void)
{
	static const DPID ids[] = {LOCAL_DPID, PEER_DPID, OTHER_DPID};
	int message[3];
	session_world(1);
	use_fake_dplay();
	list_players(ids, 3);
	struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
	peer->last_delivered_seq_channel_a = 3;
	peer->last_delivered_seq_channel_b = 4;
	peer->recv_seq_channel_a = 5;
	peer->recv_seq_channel_b = 6;
	slot_of(OTHER_DPID);
	system_message(message, DPSYS_CREATEPLAYERORGROUP, OTHER_DPID);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  1);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_message(0)->to, OTHER_DPID);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_SEQUENCE_STATUS);
	/* 8 bytes per peer slot, and 12 after the type: 0, the slot count and
	 * the time. */
	XVT_ASSERT_INT_EQ(sent_short(0, GAME_LENGTH), 2 * 8 + 12);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY), 0);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 4), 2);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 8), 1000);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 12), PEER_DPID);
	XVT_ASSERT_INT_EQ(sent_message(0)->bytes[GAME_BODY + 16], 3);
	XVT_ASSERT_INT_EQ(sent_message(0)->bytes[GAME_BODY + 17], 4);
	XVT_ASSERT_INT_EQ(sent_message(0)->bytes[GAME_BODY + 18], 5);
	XVT_ASSERT_INT_EQ(sent_message(0)->bytes[GAME_BODY + 19], 6);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 20), OTHER_DPID);
	XVT_ASSERT_INT_EQ(sent_message(1)->to, OTHER_DPID);
	XVT_ASSERT_INT_EQ(sent_type(1), NET_PACKET_FLIGHT_SESSION_STATUS);
	XVT_ASSERT_INT_EQ(sent_short(1, GAME_LENGTH), 4);
	XVT_ASSERT_INT_EQ(sent_word(1, GAME_BODY), 0);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 3);
	XVT_ASSERT_INT_EQ(g_net_session.players[2].direct_play_id, OTHER_DPID);
	XVT_ASSERT_INT_EQ(g_added_count, 2);
	XVT_ASSERT_INT_EQ(g_added_players[0], PEER_DPID);
	XVT_ASSERT_INT_EQ(g_added_players[1], OTHER_DPID);

	forget_sends();
	g_added_count = 0;
	system_message(message, DPSYS_CREATEPLAYERORGROUP, LOCAL_DPID);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  1);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 3);
	XVT_ASSERT_INT_EQ(g_added_count, 2);

	g_added_count = 0;
	g_net_session.player_count = 1;
	message[1] = 0;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 1);
	XVT_ASSERT_INT_EQ(g_added_count, 0);
	list_players(ids, 0);
}

/* Before any session init, a departure lists the roster again, takes the
 * player out of the group and frees its peer slot: the last slot moves into
 * it and the last is cleared. The value returned is the slots left in 8-byte
 * words of slot; with no slot, the player's id, or the group removal's result
 * when no slot is in use. A group leaving changes nothing. */
static void check_departure_before_any_session(void)
{
	static const DPID ids[] = {LOCAL_DPID, PEER_DPID, OTHER_DPID};
	int message[3];
	session_world(1);
	use_fake_dplay();
	list_players(ids, 3);
	slot_of(PEER_DPID)->packet_count = 4;
	scramble_slot(slot_of(OTHER_DPID));
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, PEER_DPID);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  SLOT_WORDS);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 3);
	XVT_ASSERT_INT_EQ(g_removed_count, 1);
	XVT_ASSERT_INT_EQ(g_removed_player, PEER_DPID);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slots[0].direct_play_id,
			  OTHER_DPID);
	assert_slot_scrambled(&g_net_session.reliable_peer_slots[0]);
	assert_slot_free(&g_net_session.reliable_peer_slots[1]);

	message[2] = 999;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  999);
	XVT_ASSERT_INT_EQ(g_removed_count, 2);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 1);

	session_world(1);
	use_fake_dplay();
	list_players(ids, 3);
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, PEER_DPID);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(g_removed_count, 1);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 3);

	g_net_session.player_count = 1;
	message[1] = 0;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 1);
	XVT_ASSERT_INT_EQ(g_removed_count, 1);
	list_players(ids, 0);
}

/* ------------------------------------------------------------------------ */
/* net_session_init_game_session: the reset, the solo host with an interface,
 * the multiplayer path. */

/* Opening the session clears the world-message history and its write index
 * and the scratch packet's trailing state, takes the transport given, and
 * resets every peer slot past those the frontend hands over to a free slot. */
static void check_init_resets_session(void)
{
	forget_lines();
	memset(&g_front_state, 0, sizeof g_front_state);
	g_front_state.net_reliable_peer_slot_count = 1;
	g_front_state.net_runtime_reliable_peer_slots[0].direct_play_id =
		PEER_DPID;
	memset(&g_net_session, 0x5A, sizeof g_net_session);
	for (int i = 0; i < 256; ++i) {
		g_net_session_sent_world_message_history[i].payload_size = 9;
	}
	g_net_session_sent_world_message_write_index = 77;
	g_net_session_scratch_packet.trailing_state = 5;
	XVT_ASSERT_INT_EQ(
		net_session_init_game_session("Formal", "Pilot", 1, NULL,
					      NET_TRANSPORT_SERIAL, 1, 0, NULL),
		1);
	XVT_ASSERT_INT_EQ(g_net_session.network_type, NET_TRANSPORT_SERIAL);
	XVT_ASSERT_INT_EQ(g_net_session_sent_world_message_write_index, 0);
	for (int i = 0; i < 256; ++i) {
		XVT_ASSERT_INT_EQ(g_net_session_sent_world_message_history[i]
					  .payload_size,
				  0);
	}
	XVT_ASSERT_INT_EQ(g_net_session_scratch_packet.trailing_state, 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 1);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slots[0].direct_play_id,
			  PEER_DPID);
	for (int i = 1; i < 40; ++i) {
		assert_slot_free(&g_net_session.reliable_peer_slots[i]);
	}
	XVT_ASSERT_INT_EQ(g_net_session.reliable_use_fixed_resend_timeouts, 0);
	memset(&g_front_state, 0, sizeof g_front_state);
}

/* A host flying alone with a DirectPlay interface asks it for its caps once,
 * keeps the id the frontend handed over and sends nothing. */
static void check_solo_init_with_interface(void)
{
	forget_lines();
	memset(&g_front_state, 0, sizeof g_front_state);
	use_fake_dplay();
	g_front_state.net_direct_play = &g_fake_dplay;
	g_front_state.net_runtime_local_player.player_id = 7;
	XVT_ASSERT_INT_EQ(net_session_init_game_session("Formal", "Pilot", 1,
							NULL, NET_TRANSPORT_IPX,
							1, 0, NULL),
			  1);
	XVT_ASSERT_INT_EQ(g_caps_calls, 1);
	XVT_ASSERT_TRUE(g_net_session.dplay_interface == &g_fake_dplay);
	XVT_ASSERT_INT_EQ(net_session_get_local_dplay_id(), 7);
	XVT_ASSERT_INT_EQ(net_session_is_local_host(), 1);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 1);
	XVT_ASSERT_INT_EQ(g_net_session.players[0].direct_play_id, 7);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[0].long_name, "Formal"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[0].player_name, "Pilot"),
			  0);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	memset(&g_front_state, 0, sizeof g_front_state);
}

/* A client opening a flight with others asks DirectPlay for its caps, lists
 * the players the pilot record also holds, sends the host a STARTUP_READY
 * then a NOP, and returns the roster exchange's result: pending, or 1 when it
 * joins a flight in progress. A host sends both to itself. */
static void check_multiplayer_init(void)
{
	static const DPID ids[] = {LOCAL_DPID, PEER_DPID, 400};
	forget_lines();
	memset(&g_front_state, 0, sizeof g_front_state);
	use_fake_dplay();
	list_players(ids, 3);
	g_pilot_data.network_players[2].direct_play_id = 0;
	g_front_state.net_direct_play = &g_fake_dplay;
	g_front_state.net_host_player_id = PEER_DPID;
	g_front_state.net_group_dplay_id = GROUP_DPID;
	g_front_state.net_runtime_local_player.player_id = LOCAL_DPID;
	XVT_ASSERT_INT_EQ(net_session_init_game_session("Formal", "Pilot", 0,
							NULL, NET_TRANSPORT_IPX,
							2, 0, NULL),
			  XVT_FLIGHT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(g_caps_calls, 1);
	XVT_ASSERT_INT_EQ(line_value("network.dplay_caps", "buffer"), 1024);
	XVT_ASSERT_INT_EQ(line_value("network.dplay_caps", "players"), 8);
	XVT_ASSERT_INT_EQ(net_session_is_local_host(), 0);
	XVT_ASSERT_INT_EQ(net_session_get_host_dplay_id(), PEER_DPID);
	XVT_ASSERT_INT_EQ(g_net_session.player_count, 2);
	XVT_ASSERT_INT_EQ(g_net_session.players[0].direct_play_id, LOCAL_DPID);
	XVT_ASSERT_INT_EQ(g_net_session.players[1].direct_play_id, PEER_DPID);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_message(0)->to, PEER_DPID);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_STARTUP_READY);
	XVT_ASSERT_INT_EQ(sent_sequence(0), 0);
	XVT_ASSERT_INT_EQ(sent_message(1)->to, PEER_DPID);
	XVT_ASSERT_INT_EQ(sent_type(1), NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(sent_sequence(1), 1);

	XVT_ASSERT_INT_EQ(net_session_init_game_session("Formal", "Pilot", 0,
							NULL, NET_TRANSPORT_IPX,
							2, 1, NULL),
			  1);
	XVT_ASSERT_INT_EQ(g_sent_count, 4);

	forget_sends();
	g_front_state.net_host_player_id = LOCAL_DPID;
	XVT_ASSERT_INT_EQ(net_session_init_game_session("Formal", "Pilot", 1,
							NULL, NET_TRANSPORT_IPX,
							2, 0, NULL),
			  XVT_FLIGHT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued_word(0, 0), NET_PACKET_STARTUP_READY);
	XVT_ASSERT_INT_EQ(queued_word(1, 0), NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(queued(0)->direct_play_id, LOCAL_DPID);
	list_players(ids, 0);
	memset(&g_front_state, 0, sizeof g_front_state);
}

/* ------------------------------------------------------------------------ */
/* net_session_pump_incoming_packets: no interface, a full queue, system
 * messages, the header and the body's length. */

/* With no DirectPlay interface the pump does nothing; with one it sets its
 * state to 0 and reads until DirectPlay has nothing more. */
static void check_pump_needs_interface(void)
{
	session_world(0);
	g_net_session.receive_pump_state = 7;
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_session.receive_pump_state, 7);
	use_fake_dplay();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_session.receive_pump_state, 0);
	XVT_ASSERT_INT_EQ(count_lines("network.receive_drained"), 1);
}

/* With 1,023 entries queued the pump keeps only system messages and the
 * host's packets, in order, and returns without reading DirectPlay; with
 * 1,022 it reads. */
static void check_pump_full_queue(void)
{
	pump_world();
	for (int i = 0; i < 1023; ++i) {
		int sender = i < 3 ? PEER_DPID : (i < 5 ? 0 : OTHER_DPID);
		queue_packet(sender, 1, i % 128, 0, NET_PACKET_CHAT, i);
	}
	send_marked(PEER_DPID, NET_PACKET_CHAT, 77, 8);
	deliver_last_send();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_inbox_next, 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 5);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 5);
	XVT_ASSERT_INT_EQ(queued_word(4, MARK_WORD), 4);
	XVT_ASSERT_INT_EQ(line_value("network.receive_queue_full", "queued"),
			  1023);

	empty_queue();
	for (int i = 0; i < 1022; ++i) {
		queue_packet(PEER_DPID, 1, 1, 0, NET_PACKET_CHAT, i);
	}
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_inbox_next, 1);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1023);
	empty_queue();
}

/* A system message (sender 0) is queued as received, with sender 0 and not
 * marked resent; one over 512 bytes is cut to 512 with a warning. The write
 * index wraps from 1,023 to 0. */
static void check_pump_system_messages(void)
{
	uint8_t bytes[600];
	pump_world();
	for (int i = 0; i < 600; ++i) {
		bytes[i] = (uint8_t)(i * 7);
	}
	int type = DPSYS_CREATEPLAYERORGROUP;
	memcpy(bytes, &type, sizeof type);
	inbox_message(0, LOCAL_DPID, bytes, 600);
	inbox_message(0, LOCAL_DPID, bytes, 512);
	inbox_message(0, LOCAL_DPID, bytes, 12);
	for (int i = 0; i < 3; ++i) {
		g_net_session_recv_queue[(1023 + i) % 1024].direct_play_id = 99;
		g_net_session_recv_queue[(1023 + i) % 1024].is_resent_copy = 1;
	}
	empty_queue_at(1023);
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 3);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 2);
	static const uint32_t sizes[] = {512, 512, 12};
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(queued(i)->direct_play_id, 0);
		XVT_ASSERT_INT_EQ(queued(i)->is_resent_copy, 0);
		XVT_ASSERT_INT_EQ(queued(i)->payload_size, sizes[i]);
		XVT_ASSERT_INT_EQ(memcmp(queued(i)->payload, bytes, sizes[i]),
				  0);
	}
	XVT_ASSERT_INT_EQ(count_lines_holding("network.packet_truncated",
					      "part=\"system\""),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.packet_truncated", "bytes"), 600);
}

/* A packet addressed to another player is dropped with a warning; the same
 * packet to this player is queued. */
static void check_pump_misaddressed(void)
{
	pump_world();
	send_marked(PEER_DPID, NET_PACKET_CHAT, 5, 8);
	deliver_last_send();
	g_inbox[0].to = 999;
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(line_value("network.packet_misaddressed", "to"), 999);
	deliver_last_send();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued_word(0, MARK_WORD), 5);
}

/* A body over 508 bytes is cut to 508 with a warning; one of 508 is kept
 * whole. */
static void check_pump_long_body(void)
{
	unsigned int packet[150];
	pump_world();
	for (int i = 0; i < 150; ++i) {
		packet[i] = 1000u + (unsigned int)i;
	}
	packet[0] = NET_PACKET_CHAT;
	XVT_ASSERT_INT_EQ(
		net_session_send_compact_game_packet(PEER_DPID, packet, 600, 1),
		1);
	deliver_last_send();
	XVT_ASSERT_INT_EQ(
		net_session_send_compact_game_packet(PEER_DPID, packet, 512, 1),
		1);
	deliver_last_send();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_INT_EQ(queued(i)->payload_size, 512);
		XVT_ASSERT_INT_EQ(queued_word(i, 0), NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(
			memcmp(queued(i)->payload + 4, packet + 1, 508), 0);
	}
	XVT_ASSERT_INT_EQ(count_lines_holding("network.packet_truncated",
					      "part=\"body\""),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.packet_truncated", "bytes"), 596);
}

/* ------------------------------------------------------------------------ */
/* net_session_pump_incoming_packets: PING, KEEPALIVE_ACK, WORLD_NACK, NACK
 * and KEEPALIVE. */

/* A PING is answered with a PONG sent back to its sender, and a KEEPALIVE_ACK
 * is ignored; neither is queued. */
static void check_pump_ping_and_keepalive_ack(void)
{
	pump_world();
	deliver_control(NET_PACKET_PING, 0, 0, 0, 4);
	deliver_control(NET_PACKET_KEEPALIVE_ACK, 0, 0, 0, 4);
	forget_sends();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 1);
	XVT_ASSERT_INT_EQ(sent_message(0)->to, PEER_DPID);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_PONG);
	XVT_ASSERT_INT_EQ(count_lines("network.ping_answered"), 1);
	XVT_ASSERT_INT_EQ(count_lines("network.keepalive_ack_ignored"), 1);
}

/* A WORLD_NACK is answered with the world message whose first word, its top
 * bit cleared, is the tick asked for, resent under its own sequence on the
 * all-players channel; with none, a NOP in the sequence asked for. Each
 * counts in the peer's drop count; one under 8 bytes is only logged. */
static void check_pump_world_nack(void)
{
	unsigned int packet[4];
	pump_world();
	make_packet(packet, NET_PACKET_WORLD_MESSAGE, (int)(0x80000054u), 1, 0);
	XVT_ASSERT_INT_EQ(net_session_send_packet(0, packet, 12), 1);
	make_packet(packet, NET_PACKET_WORLD_MESSAGE, (int)(0x80000055u), 2, 0);
	XVT_ASSERT_INT_EQ(net_session_send_packet(0, packet, 12), 1);
	empty_queue();
	deliver_control(NET_PACKET_WORLD_NACK, 0x55, 1, 0, 12);
	deliver_control(NET_PACKET_WORLD_NACK, 5, 9, 0, 12);
	deliver_control(NET_PACKET_WORLD_NACK, 5, 9, 0, 8);
	forget_sends();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_message(0)->to, PEER_DPID);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_WORLD_MESSAGE);
	XVT_ASSERT_INT_EQ(sent_sequence(0), 1);
	XVT_ASSERT_INT_EQ(sent_channel_bits(0), 0x80);
	XVT_ASSERT_INT_EQ(sent_message(0)->bytes[RESEND_CLASS], 0);
	XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY), (int)(0x80000055u));
	XVT_ASSERT_INT_EQ(sent_word(0, RESEND_BODY + 4), 2);
	XVT_ASSERT_INT_EQ(sent_message(1)->to, PEER_DPID);
	XVT_ASSERT_INT_EQ(sent_type(1), NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(sent_sequence(1), 9);
	XVT_ASSERT_INT_EQ(sent_channel_bits(1), 0x80);
	XVT_ASSERT_INT_EQ(sent_message(1)->bytes[RESEND_CLASS], 0);
	XVT_ASSERT_INT_EQ(slot_of(PEER_DPID)->packet_drop_count, 2);
	XVT_ASSERT_INT_EQ(count_lines("network.world_resent"), 1);
	XVT_ASSERT_INT_EQ(count_lines("network.world_resend_missing"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.control_packet_short", "type"),
			  NET_PACKET_WORLD_NACK);
}

/* A NACK is answered from the sent history with the packet of the sequence
 * and channel asked for, resent under that channel; on the direct channel only
 * a packet sent to the asking peer matches. With none, a NOP in that sequence
 * is sent. Each counts in the peer's drop count; one under 8 bytes is only
 * logged. */
static void check_pump_nack(void)
{
	static const struct {
		int packet_class;
		int mark;
	} resends[] = {{1, 2}, {2, 3}, {0, 4}};
	pump_world();
	send_marked(OTHER_DPID, NET_PACKET_CHAT, 1, 8);
	send_marked(PEER_DPID, NET_PACKET_CHAT, 2, 8);
	send_marked(GROUP_DPID, NET_PACKET_CHAT, 3, 8);
	send_marked(0, NET_PACKET_CHAT, 4, 8);
	empty_queue();
	deliver_control(NET_PACKET_NACK, 0, 1, 0, 12);
	deliver_control(NET_PACKET_NACK, 0, 2, 0, 12);
	deliver_control(NET_PACKET_NACK, 0, 0, 0, 12);
	deliver_control(NET_PACKET_NACK, 9, 1, 0, 12);
	deliver_control(NET_PACKET_NACK, 9, 1, 0, 8);
	forget_sends();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 4);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(sent_message(i)->to, PEER_DPID);
		XVT_ASSERT_INT_EQ(sent_type(i), NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(sent_sequence(i), 0);
		XVT_ASSERT_INT_EQ(sent_channel_bits(i), 0x80);
		XVT_ASSERT_INT_EQ(sent_message(i)->bytes[RESEND_CLASS],
				  resends[i].packet_class);
		XVT_ASSERT_INT_EQ(sent_word(i, RESEND_BODY), resends[i].mark);
	}
	XVT_ASSERT_INT_EQ(sent_type(3), NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(sent_sequence(3), 9);
	XVT_ASSERT_INT_EQ(sent_message(3)->bytes[RESEND_CLASS], 1);
	XVT_ASSERT_INT_EQ(slot_of(PEER_DPID)->packet_drop_count, 4);
	XVT_ASSERT_INT_EQ(count_lines("network.packet_resent"), 3);
	XVT_ASSERT_INT_EQ(count_lines("network.packet_resend_missing"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.control_packet_short", "type"),
			  NET_PACKET_NACK);
}

/* A KEEPALIVE is answered with the packet, on each channel, whose sequence
 * the peer asks for, once per channel; on the direct channel only one sent to
 * that peer. A channel whose next sequence is the one asked for needs
 * nothing; a sequence asked for and not in the history is logged. One under
 * 12 bytes is only logged. */
static void check_pump_keepalive(void)
{
	static const struct {
		int packet_class;
		int mark;
	} resends[] = {{1, 2}, {2, 3}, {0, 4}};
	pump_world();
	send_marked(OTHER_DPID, NET_PACKET_CHAT, 1, 8);
	send_marked(PEER_DPID, NET_PACKET_CHAT, 2, 8);
	send_marked(GROUP_DPID, NET_PACKET_CHAT, 3, 8);
	send_marked(0, NET_PACKET_CHAT, 4, 8);
	send_marked(0, NET_PACKET_CHAT, 5, 8);
	empty_queue();
	/* The all-players channel's next sequence is 2, the group's 1, and
	 * the next to 200 alone 1. */
	deliver_control(NET_PACKET_KEEPALIVE, 0, 0, 0, 16);
	deliver_control(NET_PACKET_KEEPALIVE, 2, 1, 1, 16);
	deliver_control(NET_PACKET_KEEPALIVE, 1, 50, 50, 16);
	deliver_control(NET_PACKET_KEEPALIVE, 0, 0, 0, 12);
	forget_sends();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(g_sent_count, 4);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(sent_message(i)->to, PEER_DPID);
		XVT_ASSERT_INT_EQ(sent_type(i), NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(sent_sequence(i), 0);
		XVT_ASSERT_INT_EQ(sent_message(i)->bytes[RESEND_CLASS],
				  resends[i].packet_class);
		XVT_ASSERT_INT_EQ(sent_word(i, RESEND_BODY), resends[i].mark);
	}
	XVT_ASSERT_INT_EQ(sent_sequence(3), 1);
	XVT_ASSERT_INT_EQ(sent_message(3)->bytes[RESEND_CLASS], 0);
	XVT_ASSERT_INT_EQ(sent_word(3, RESEND_BODY), 5);
	XVT_ASSERT_INT_EQ(count_lines("network.keepalive_unanswered"), 1);
	XVT_ASSERT_INT_EQ(line_value("network.keepalive_unanswered", "group"),
			  50);
	XVT_ASSERT_INT_EQ(line_value("network.keepalive_unanswered", "direct"),
			  50);
	XVT_ASSERT_INT_EQ(line_value("network.control_packet_short", "type"),
			  NET_PACKET_KEEPALIVE);
}

/* ------------------------------------------------------------------------ */
/* net_session_pump_incoming_packets: a resent copy, the previous packet
 * missing, and the packet queued. */

/* A resent packet is queued as a resent copy under the channel its marker
 * byte names, any marker but 0 and 2 meaning the direct channel, with its
 * sequence; the channel's newest received sequence moves only when the copy
 * carries the next one. */
static void check_pump_resent_copies(void)
{
	static const struct {
		int marker;
		int packet_class;
	} rows[] = {{0, 0}, {2, 2}, {1, 1}, {5, 1}};
	unsigned int packet[4];
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		pump_world();
		make_packet(packet, NET_PACKET_CHAT, 10, 0, 0);
		net_session_send_sequenced_game_packet(
			PEER_DPID, (uint8_t)rows[row].marker, 0, packet, 8);
		deliver_last_send();
		make_packet(packet, NET_PACKET_CHAT, 11, 0, 0);
		net_session_send_sequenced_game_packet(
			PEER_DPID, (uint8_t)rows[row].marker, 7, packet, 8);
		deliver_last_send();
		for (int i = 0; i < 2; ++i) {
			g_net_session_recv_queue[i].nack_retry_count = 3;
			g_net_session_recv_queue[i].last_nack_ms = 5;
		}
		net_session_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
		for (int i = 0; i < 2; ++i) {
			XVT_ASSERT_INT_EQ(queued(i)->direct_play_id, PEER_DPID);
			XVT_ASSERT_INT_EQ(queued(i)->packet_class,
					  rows[row].packet_class);
			XVT_ASSERT_INT_EQ(queued(i)->is_resent_copy, 1);
			XVT_ASSERT_INT_EQ(queued(i)->nack_retry_count, 0);
			XVT_ASSERT_INT_EQ(queued(i)->last_nack_ms, 0);
			XVT_ASSERT_INT_EQ(queued(i)->payload_size, 8);
			XVT_ASSERT_INT_EQ(queued_word(i, 0), NET_PACKET_CHAT);
			XVT_ASSERT_INT_EQ(queued_word(i, MARK_WORD), 10 + i);
		}
		XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 0);
		XVT_ASSERT_INT_EQ(queued(1)->sequence_byte, 7);
		struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
		for (int packet_class = 0; packet_class < 3; ++packet_class) {
			XVT_ASSERT_INT_EQ(*newest_received(peer, packet_class),
					  packet_class == rows[row].packet_class
						  ? 0
						  : 127);
		}
	}
}

/* A resent copy's body over 508 bytes is cut to 508 with a warning, and one of
 * 508 is kept whole; a copy of a resync type, which has no length, is queued
 * whole. The write index wraps from 1,023 to 0. */
static void check_pump_resent_copy_sizes(void)
{
	unsigned int packet[150];
	pump_world();
	empty_queue_at(1023);
	for (int i = 0; i < 150; ++i) {
		packet[i] = 2000u + (unsigned int)i;
	}
	packet[0] = NET_PACKET_CHAT;
	net_session_send_sequenced_game_packet(PEER_DPID, 1, 3, packet, 600);
	deliver_last_send();
	make_packet(packet, NET_PACKET_RESYNC_CHUNK, 12, 13, 0);
	net_session_send_sequenced_game_packet(PEER_DPID, 1, 4, packet, 12);
	deliver_last_send();
	packet[0] = NET_PACKET_CHAT;
	net_session_send_sequenced_game_packet(PEER_DPID, 1, 5, packet, 512);
	deliver_last_send();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 3);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 2);
	XVT_ASSERT_INT_EQ(queued(0)->payload_size, 512);
	XVT_ASSERT_INT_EQ(queued_word(0, 0), NET_PACKET_CHAT);
	XVT_ASSERT_INT_EQ(queued_word(0, 1), 2001);
	XVT_ASSERT_INT_EQ(queued_word(0, 127), 2127);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.packet_truncated",
					      "part=\"copy\""),
			  1);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.packet_truncated",
					      "part=\"copy\" bytes=596"),
			  1);
	XVT_ASSERT_INT_EQ(queued(1)->payload_size, 12);
	XVT_ASSERT_INT_EQ(queued_word(1, 0), NET_PACKET_RESYNC_CHUNK);
	XVT_ASSERT_INT_EQ(queued_word(1, 1), 12);
	XVT_ASSERT_INT_EQ(queued_word(1, 2), 13);
	XVT_ASSERT_INT_EQ(queued(2)->payload_size, 512);
	XVT_ASSERT_INT_EQ(queued_word(2, 1), 12);
}

/* When a packet's previous sequence on its channel is new, the copy of that
 * packet riding behind it is queued first, under the previous sequence, and
 * the peer's drop count rises except on the group channel. Sequence 0's
 * previous one is 127, and the write index wraps from 1,023 to 0. */
static void check_pump_previous_missing(void)
{
	static const struct {
		int id;
		int packet_class;
		int drops;
		int first_sequence;
	} rows[] = {
		{PEER_DPID, 1, 1, 0},
		{0, 0, 1, 0},
		{GROUP_DPID, 2, 0, 0},
		{PEER_DPID, 1, 1, 127},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int first = rows[row].first_sequence;
		pump_world();
		set_next_sequence(rows[row].id, first);
		send_marked(rows[row].id, NET_PACKET_CHAT, 10, 8);
		send_marked(rows[row].id, NET_PACKET_CHAT, 11, 8);
		deliver_last_send();
		empty_queue_at(1023);
		if (first == 127) {
			slot_of(PEER_DPID)->recv_seq_default = 126;
		}
		for (int i = 0; i < 2; ++i) {
			queued(i)->nack_retry_count = 3;
			queued(i)->last_nack_ms = 5;
			queued(i)->is_resent_copy = 1;
		}
		net_session_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 1);
		for (int i = 0; i < 2; ++i) {
			XVT_ASSERT_INT_EQ(queued(i)->direct_play_id, PEER_DPID);
			XVT_ASSERT_INT_EQ(queued(i)->packet_class,
					  rows[row].packet_class);
			XVT_ASSERT_INT_EQ(queued(i)->is_resent_copy, 0);
			XVT_ASSERT_INT_EQ(queued(i)->nack_retry_count, 0);
			XVT_ASSERT_INT_EQ(queued(i)->last_nack_ms, 0);
			XVT_ASSERT_INT_EQ(queued(i)->payload_size, 8);
			XVT_ASSERT_INT_EQ(queued_word(i, MARK_WORD), 10 + i);
		}
		XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, first);
		XVT_ASSERT_INT_EQ(queued(1)->sequence_byte, (first + 1) % 128);
		XVT_ASSERT_INT_EQ(slot_of(PEER_DPID)->packet_drop_count,
				  rows[row].drops);
		XVT_ASSERT_INT_EQ(
			line_value("network.previous_missing", "recovered"), 1);
	}
}

/* The copy riding behind a packet is cut to 508 bytes of body with a warning.
 * A 513-byte internet-play input, which goes into no 512-byte sent history,
 * leaves a 509-byte body as the group channel's saved copy. */
static void check_pump_previous_copy_cut(void)
{
	unsigned int packet[130];
	pump_world();
	g_game_config.internet_play = 1;
	for (int i = 0; i < 130; ++i) {
		packet[i] = 3000u + (unsigned int)i;
	}
	packet[0] = NET_PACKET_REMOTE_INPUT;
	XVT_ASSERT_INT_EQ(net_session_send_packet(PEER_DPID, packet, 513), 1);
	send_marked(PEER_DPID, NET_PACKET_REMOTE_INPUT, 11, 8);
	deliver_last_send();
	net_session_pump_incoming_packets();
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued(0)->packet_class, 2);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 0);
	XVT_ASSERT_INT_EQ(queued(0)->payload_size, 512);
	XVT_ASSERT_INT_EQ(queued_word(0, 0), NET_PACKET_REMOTE_INPUT);
	XVT_ASSERT_INT_EQ(memcmp(queued(0)->payload + 4, packet + 1, 508), 0);
	XVT_ASSERT_INT_EQ(queued(1)->sequence_byte, 1);
	XVT_ASSERT_INT_EQ(queued_word(1, MARK_WORD), 11);
	XVT_ASSERT_INT_EQ(count_lines_holding("network.packet_truncated",
					      "part=\"trailer\""),
			  1);
	XVT_ASSERT_INT_EQ(line_value("network.packet_truncated", "bytes"), 509);
	XVT_ASSERT_INT_EQ(slot_of(PEER_DPID)->packet_drop_count, 0);
	g_game_config.internet_play = 0;
}

/* A packet is queued with its sender, channel, sequence and body, the write
 * index wrapping from 1,023 to 0. A repeat is queued again marked as a resent
 * copy, except on the group channel, where it is dropped. */
static void check_pump_queues_packets(void)
{
	static const struct {
		int id;
		int packet_class;
		int repeat_kept;
	} rows[] = {{PEER_DPID, 1, 1}, {0, 0, 1}, {GROUP_DPID, 2, 0}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		pump_world();
		send_marked(rows[row].id, NET_PACKET_CHAT, 20, 8);
		deliver_last_send();
		deliver_last_send();
		empty_queue_at(1023);
		g_net_session_recv_queue[1023].is_resent_copy = 1;
		g_net_session_recv_queue[1023].nack_retry_count = 3;
		g_net_session_recv_queue[1023].last_nack_ms = 5;
		g_net_session_recv_queue[0].is_resent_copy = 0;
		net_session_pump_incoming_packets();
		int count = 1 + rows[row].repeat_kept;
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, count);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, count - 1);
		for (int i = 0; i < count; ++i) {
			XVT_ASSERT_INT_EQ(queued(i)->direct_play_id, PEER_DPID);
			XVT_ASSERT_INT_EQ(queued(i)->packet_class,
					  rows[row].packet_class);
			XVT_ASSERT_INT_EQ(queued(i)->sequence_byte, 0);
			XVT_ASSERT_INT_EQ(queued(i)->is_resent_copy, i);
			XVT_ASSERT_INT_EQ(queued(i)->nack_retry_count, 0);
			XVT_ASSERT_INT_EQ(queued(i)->last_nack_ms, 0);
			XVT_ASSERT_INT_EQ(queued(i)->payload_size, 8);
			XVT_ASSERT_INT_EQ(queued_word(i, 0), NET_PACKET_CHAT);
			XVT_ASSERT_INT_EQ(queued_word(i, MARK_WORD), 20);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* net_session_send_packet: too short, the resync types, internet-play input,
 * the three channels and their saved copies. */

/* Outside the resync types (RESYNC_CHECKSUMS to RESYNC_CHUNK) the body gets a
 * 2-byte length and, behind it, the channel's saved copy; the resync types go
 * out bare. The pump reads either kind back whole. */
static void check_send_resync_types_bare(void)
{
	static const struct {
		int type;
		int bare;
	} rows[] = {
		{NET_PACKET_RESYNC_CHECKSUMS - 1, 0},
		{NET_PACKET_RESYNC_CHECKSUMS, 1},
		{NET_PACKET_RESYNC_CHUNK, 1},
		{NET_PACKET_RESYNC_CHUNK + 1, 0},
	};
	unsigned int packet[4];
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		pump_world();
		make_packet(packet, rows[row].type, 30, 31, 0);
		XVT_ASSERT_INT_EQ(
			net_session_send_packet(PEER_DPID, packet, 12), 1);
		XVT_ASSERT_INT_EQ(sent_type(0), rows[row].type);
		if (rows[row].bare) {
			XVT_ASSERT_INT_EQ(sent_message(0)->size, 2 + 8);
			XVT_ASSERT_INT_EQ(sent_word(0, 2), 30);
		} else {
			XVT_ASSERT_INT_EQ(sent_message(0)->size, 2 + 2 + 8 + 1);
			XVT_ASSERT_INT_EQ(sent_short(0, GAME_LENGTH), 8);
			XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY), 30);
			XVT_ASSERT_INT_EQ(sent_message(0)->bytes[12],
					  NET_PACKET_NOP);
		}
		deliver_last_send();
		net_session_pump_incoming_packets();
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
		XVT_ASSERT_INT_EQ(queued(0)->payload_size, 12);
		XVT_ASSERT_INT_EQ(queued_word(0, 0), rows[row].type);
		XVT_ASSERT_INT_EQ(queued_word(0, 1), 30);
		XVT_ASSERT_INT_EQ(queued_word(0, 2), 31);
	}
}

/* Each packet takes the next sequence of its channel, which wraps from 127 to
 * 0, and carries behind its body the type and body of the channel's previous
 * packet, a NOP the first time. The channels: to id 0 all players (no channel
 * bits), to the group bits 0x8080, to one player bit 0x8000 and that peer's
 * own sequence. */
static void check_send_channels_and_saved_copies(void)
{
	static const struct {
		int id;
		int bits;
	} rows[] = {{0, 0}, {GROUP_DPID, 0x8080}, {PEER_DPID, 0x8000}};
	unsigned int packet[4];
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int id = rows[row].id;
		session_world(1);
		use_fake_dplay();
		g_net_session.group_dplay_id = GROUP_DPID;
		set_next_sequence(id, 126);
		make_packet(packet, NET_PACKET_CHAT, 0x11, 0x22, 0);
		XVT_ASSERT_INT_EQ(net_session_send_packet(id, packet, 12), 1);
		make_packet(packet, NET_PACKET_ACK, 0x33, 0, 0);
		XVT_ASSERT_INT_EQ(net_session_send_packet(id, packet, 8), 1);
		make_packet(packet, NET_PACKET_CHAT, 0x44, 0, 0);
		XVT_ASSERT_INT_EQ(net_session_send_packet(id, packet, 8), 1);
		XVT_ASSERT_INT_EQ(next_sequence(id), 1);
		XVT_ASSERT_INT_EQ(g_sent_count, 3);
		static const int types[] = {NET_PACKET_CHAT, NET_PACKET_ACK,
					    NET_PACKET_CHAT};
		static const int sequences[] = {126, 127, 0};
		static const int lengths[] = {8, 4, 4};
		for (int i = 0; i < 3; ++i) {
			XVT_ASSERT_INT_EQ(sent_message(i)->to, id);
			XVT_ASSERT_INT_EQ(sent_type(i), types[i]);
			XVT_ASSERT_INT_EQ(sent_sequence(i), sequences[i]);
			XVT_ASSERT_INT_EQ(sent_channel_bits(i), rows[row].bits);
			XVT_ASSERT_INT_EQ(sent_short(i, GAME_LENGTH),
					  lengths[i]);
		}
		XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY), 0x11);
		XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 4), 0x22);
		XVT_ASSERT_INT_EQ(sent_message(0)->bytes[12], NET_PACKET_NOP);
		XVT_ASSERT_INT_EQ(sent_message(0)->size, 13);
		XVT_ASSERT_INT_EQ(sent_word(1, GAME_BODY), 0x33);
		XVT_ASSERT_INT_EQ(sent_message(1)->bytes[8], NET_PACKET_CHAT);
		XVT_ASSERT_INT_EQ(sent_word(1, 9), 0x11);
		XVT_ASSERT_INT_EQ(sent_word(1, 13), 0x22);
		XVT_ASSERT_INT_EQ(sent_message(1)->size, 17);
		XVT_ASSERT_INT_EQ(sent_word(2, GAME_BODY), 0x44);
		XVT_ASSERT_INT_EQ(sent_message(2)->bytes[8], NET_PACKET_ACK);
		XVT_ASSERT_INT_EQ(sent_word(2, 9), 0x33);
		XVT_ASSERT_INT_EQ(sent_message(2)->size, 13);
	}
}

/* In internet play a REMOTE_INPUT goes on the group channel (bits 0x8080)
 * whatever its destination and is kept in no sent history; sent to all it
 * comes back to this player's queue on the group channel, which becomes this
 * player's newest group sequence. Outside internet play it goes direct. */
static void check_send_internet_input(void)
{
	session_world(1);
	use_fake_dplay();
	g_net_session.group_dplay_id = GROUP_DPID;
	g_game_config.internet_play = 1;
	g_net_session.group_seq_counter = 127;
	send_marked(PEER_DPID, NET_PACKET_REMOTE_INPUT, 40, 8);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_REMOTE_INPUT);
	XVT_ASSERT_INT_EQ(sent_sequence(0), 127);
	XVT_ASSERT_INT_EQ(sent_channel_bits(0), 0x8080);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY), 40);
	XVT_ASSERT_INT_EQ(sent_message(0)->bytes[8], NET_PACKET_NOP);
	XVT_ASSERT_INT_EQ(g_net_session.group_seq_counter, 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	send_marked(0, NET_PACKET_REMOTE_INPUT, 41, 8);
	XVT_ASSERT_INT_EQ(sent_message(1)->to, 0);
	XVT_ASSERT_INT_EQ(sent_sequence(1), 0);
	XVT_ASSERT_INT_EQ(sent_channel_bits(1), 0x8080);
	XVT_ASSERT_INT_EQ(sent_message(1)->bytes[8], NET_PACKET_REMOTE_INPUT);
	XVT_ASSERT_INT_EQ(sent_word(1, 9), 40);
	XVT_ASSERT_INT_EQ(g_net_session.group_seq_counter, 1);
	XVT_ASSERT_INT_EQ(g_net_session.broadcast_seq_counter, 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued(0)->direct_play_id, LOCAL_DPID);
	XVT_ASSERT_INT_EQ(queued(0)->packet_class, 2);
	XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 0);
	XVT_ASSERT_INT_EQ(queued_word(0, MARK_WORD), 41);
	slot_of(LOCAL_DPID)->recv_seq_channel_b = 9;
	send_marked(0, NET_PACKET_REMOTE_INPUT, 42, 8);
	XVT_ASSERT_INT_EQ(slot_of(LOCAL_DPID)->recv_seq_channel_b, 1);
	XVT_ASSERT_INT_EQ(g_net_session_sent_history_write_index, 0);
	XVT_ASSERT_INT_EQ(g_net_session_sent_history[0].payload_size, 0);

	g_game_config.internet_play = 0;
	send_marked(PEER_DPID, NET_PACKET_REMOTE_INPUT, 43, 8);
	XVT_ASSERT_INT_EQ(sent_channel_bits(3), 0x8000);
	XVT_ASSERT_INT_EQ(g_net_session_sent_history_write_index, 1);
}

/* ------------------------------------------------------------------------ */
/* net_session_send_packet: the sent histories, the copy queued for this
 * player, and DirectPlay's result. */

/* A packet to another player goes into the sent history with its
 * destination, size, channel and sequence, and a world message also into the
 * world-message history, always as channel 0; the write indices wrap at 128
 * and 256. A packet to this player itself goes into neither. */
static void check_send_histories(void)
{
	unsigned int world[4];
	session_world(1);
	use_fake_dplay();
	g_net_session.group_dplay_id = GROUP_DPID;
	slot_of(PEER_DPID)->send_seq = 4;
	g_net_session_sent_history_write_index = 127;
	g_net_session_sent_world_message_write_index = 255;
	g_net_session_sent_history[127].nack_retry_count = 5;
	g_net_session_sent_history[127].last_nack_ms = 9;
	g_net_session_sent_world_message_history[255].nack_retry_count = 5;
	g_net_session_sent_world_message_history[255].last_nack_ms = 9;
	make_packet(world, NET_PACKET_WORLD_MESSAGE, (int)(0x80000064u), 50, 0);
	XVT_ASSERT_INT_EQ(net_session_send_packet(PEER_DPID, world, 12), 1);
	const struct net_queued_packet *entries[] = {
		&g_net_session_sent_history[127],
		&g_net_session_sent_world_message_history[255],
	};
	static const int classes[] = {1, 0};
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_INT_EQ(entries[i]->direct_play_id, PEER_DPID);
		XVT_ASSERT_INT_EQ(entries[i]->payload_size, 12);
		XVT_ASSERT_INT_EQ(entries[i]->packet_class, classes[i]);
		XVT_ASSERT_INT_EQ(entries[i]->sequence_byte, 4);
		XVT_ASSERT_INT_EQ(entries[i]->nack_retry_count, 0);
		XVT_ASSERT_INT_EQ(entries[i]->last_nack_ms, 0);
		XVT_ASSERT_INT_EQ(memcmp(entries[i]->payload, world, 12), 0);
	}
	XVT_ASSERT_INT_EQ(g_net_session_sent_history_write_index, 0);
	XVT_ASSERT_INT_EQ(g_net_session_sent_world_message_write_index, 0);

	send_marked(GROUP_DPID, NET_PACKET_CHAT, 51, 8);
	send_marked(0, NET_PACKET_CHAT, 52, 8);
	XVT_ASSERT_INT_EQ(net_session_send_packet(0, world, 12), 1);
	static const int ids[] = {GROUP_DPID, 0, 0};
	static const int history_classes[] = {2, 0, 0};
	static const int sequences[] = {0, 0, 1};
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(g_net_session_sent_history[i].direct_play_id,
				  ids[i]);
		XVT_ASSERT_INT_EQ(g_net_session_sent_history[i].packet_class,
				  history_classes[i]);
		XVT_ASSERT_INT_EQ(g_net_session_sent_history[i].sequence_byte,
				  sequences[i]);
	}
	XVT_ASSERT_INT_EQ(
		g_net_session_sent_world_message_history[0].sequence_byte, 1);
	XVT_ASSERT_INT_EQ(
		g_net_session_sent_world_message_history[0].direct_play_id, 0);
	XVT_ASSERT_INT_EQ(g_net_session_sent_world_message_write_index, 1);
	XVT_ASSERT_INT_EQ(net_session_send_packet(LOCAL_DPID, world, 12), 1);
	XVT_ASSERT_INT_EQ(g_net_session_sent_history_write_index, 3);
	XVT_ASSERT_INT_EQ(g_net_session_sent_world_message_write_index, 1);
}

/* A packet to all players, to the group, to this player, or sent with no
 * DirectPlay interface, is queued for this player on its channel with its
 * sequence, and becomes this player's newest received sequence there. The
 * write index wraps from 1,023 to 0. */
static void check_send_queued_for_self(void)
{
	static const struct {
		int id;
		int packet_class;
		int interface;
	} rows[] = {
		{0, 0, 1},
		{GROUP_DPID, 2, 1},
		{LOCAL_DPID, 1, 1},
		{PEER_DPID, 1, 0},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int id = rows[row].id;
		int packet_class = rows[row].packet_class;
		session_world(1);
		if (rows[row].interface) {
			use_fake_dplay();
		}
		forget_sends();
		g_net_session.group_dplay_id = GROUP_DPID;
		set_next_sequence(id, 5);
		empty_queue_at(1023);
		g_net_session_recv_queue[1023].direct_play_id = 99;
		g_net_session_recv_queue[1023].is_resent_copy = 1;
		g_net_session_recv_queue[1023].nack_retry_count = 3;
		g_net_session_recv_queue[1023].last_nack_ms = 7;
		send_marked(id, NET_PACKET_CHAT, 60, 8);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 0);
		XVT_ASSERT_INT_EQ(queued(0)->direct_play_id, LOCAL_DPID);
		XVT_ASSERT_INT_EQ(queued(0)->packet_class, packet_class);
		XVT_ASSERT_INT_EQ(queued(0)->sequence_byte, 5);
		XVT_ASSERT_INT_EQ(queued(0)->is_resent_copy, 0);
		XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 0);
		XVT_ASSERT_INT_EQ(queued(0)->last_nack_ms, 0);
		XVT_ASSERT_INT_EQ(queued(0)->payload_size, 8);
		XVT_ASSERT_INT_EQ(queued_word(0, MARK_WORD), 60);
		XVT_ASSERT_INT_EQ(
			*newest_received(slot_of(LOCAL_DPID), packet_class), 5);
		XVT_ASSERT_INT_EQ(g_sent_count,
				  rows[row].interface && id != LOCAL_DPID);
	}

	/* With all 40 peer slots taken by others, the packet is queued and no
	 * slot's newest sequence moves. */
	session_world(1);
	use_fake_dplay();
	for (int i = 0; i < 40; ++i) {
		slot_of(1000 + i);
	}
	send_marked(0, NET_PACKET_CHAT, 61, 8);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued(0)->packet_class, 0);
	for (int i = 0; i < 40; ++i) {
		XVT_ASSERT_INT_EQ(
			g_net_session.reliable_peer_slots[i].recv_seq_channel_a,
			127);
	}
}

/* With 1,024 entries queued the queue is trimmed before this player's copy
 * is added: a host keeps only system messages, so its copy is queued; a
 * client keeps the host's packets, and with every entry from the host its
 * copy is dropped with an error. Either way the packet is sent. */
static void check_send_full_queue(void)
{
	for (int host = 1; host >= 0; --host) {
		session_world(host);
		use_fake_dplay();
		for (int i = 0; i < 1024; ++i) {
			g_net_session_recv_queue[i].direct_play_id = PEER_DPID;
			g_net_session_recv_queue[i].payload_size = 8;
		}
		g_net_recv_queue_count = 1024;
		send_marked(0, NET_PACKET_CHAT, 70, 8);
		XVT_ASSERT_INT_EQ(g_sent_count, 1);
		if (host) {
			XVT_ASSERT_INT_EQ(
				line_value("network.receive_queue_purged",
					   "kept"),
				0);
			XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
			XVT_ASSERT_INT_EQ(queued(0)->direct_play_id,
					  LOCAL_DPID);
			XVT_ASSERT_INT_EQ(queued_word(0, MARK_WORD), 70);
			XVT_ASSERT_INT_EQ(
				count_lines("network.own_packet_dropped"), 0);
		} else {
			XVT_ASSERT_INT_EQ(
				line_value("network.receive_queue_purged",
					   "kept"),
				1024);
			XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1024);
			XVT_ASSERT_INT_EQ(
				count_lines("network.own_packet_dropped"), 1);
		}
	}
	empty_queue();
}

/* The send returns 1 when DirectPlay takes the packet; DPERR_BUSY makes it 0
 * with a warning, DPERR_INVALIDPLAYER 0 without one. */
static void check_send_results(void)
{
	unsigned int packet[2] = {NET_PACKET_CHAT, 0};
	session_world(1);
	use_fake_dplay();
	g_send_result = DPERR_BUSY;
	XVT_ASSERT_INT_EQ(net_session_send_packet(PEER_DPID, packet, 8), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.send_failed"), 1);
	g_send_result = DPERR_INVALIDPLAYER;
	XVT_ASSERT_INT_EQ(net_session_send_packet(PEER_DPID, packet, 8), 0);
	XVT_ASSERT_INT_EQ(count_lines("network.send_failed"), 1);
	g_send_result = 0;
	XVT_ASSERT_INT_EQ(net_session_send_packet(PEER_DPID, packet, 8), 1);
	XVT_ASSERT_INT_EQ(g_sent_count, 3);
	XVT_ASSERT_INT_EQ(count_lines("network.send_failed"), 1);
}

/* ------------------------------------------------------------------------ */
/* net_session_handle_direct_play_system_message: a player joins, a player
 * leaves, a rename, other messages. */

/* The new player's sequence status holds 0, the slot count and the time,
 * then per peer slot its id and its delivered and newest sequences on the
 * all-players and group channels: 8 bytes per slot plus 16 with the type.
 * The session status holds the elapsed seconds, the protocol version and 0;
 * the send's result is returned. This player's arrival sends nothing, and a
 * group's returns 0. */
static void check_host_join_sequence_status(void)
{
	int message[3];
	host_with_two_peers();
	struct net_reliable_peer_slot *peer =
		&g_net_session.reliable_peer_slots[1];
	peer->last_delivered_seq_channel_a = 3;
	peer->last_delivered_seq_channel_b = 4;
	peer->recv_seq_channel_a = 5;
	peer->recv_seq_channel_b = 6;
	system_message(message, DPSYS_CREATEPLAYERORGROUP, 400);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  1);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_message(0)->to, 400);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_SEQUENCE_STATUS);
	XVT_ASSERT_INT_EQ(sent_short(0, GAME_LENGTH), 3 * 8 + 12);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY), 0);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 4), 3);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 8), 1000);
	static const int ids[] = {LOCAL_DPID, PEER_DPID, OTHER_DPID};
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY + 12 + 8 * i), ids[i]);
	}
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(sent_message(0)->bytes[GAME_BODY + 24 + i],
				  3 + i);
	}
	XVT_ASSERT_INT_EQ(sent_message(1)->to, 400);
	XVT_ASSERT_INT_EQ(sent_type(1), NET_PACKET_FLIGHT_SESSION_STATUS);
	XVT_ASSERT_INT_EQ(sent_short(1, GAME_LENGTH), 12);
	XVT_ASSERT_INT_EQ(sent_word(1, GAME_BODY),
			  mission_get_elapsed_clock_seconds());
	XVT_ASSERT_INT_EQ(sent_word(1, GAME_BODY + 4),
			  FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(sent_word(1, GAME_BODY + 8), 0);
	XVT_ASSERT_INT_EQ(line_value("network.player_joined_flight", "player"),
			  400);

	forget_sends();
	g_send_result = DPERR_BUSY;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	g_send_result = 0;
	forget_sends();
	message[2] = LOCAL_DPID;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  1);
	message[1] = 0;
	message[2] = 400;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
	g_net_session.local_is_host = 0;
	message[1] = DPPLAYERTYPE_PLAYER;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(g_sent_count, 0);
}

/* A client told that the host left reports it has lost the host and does
 * nothing more; a group of the host's id leaving is not reported. */
static void check_host_departure_reported(void)
{
	int message[3];
	session_world(0);
	use_fake_dplay();
	three_player_roster();
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, PEER_DPID);
	message[1] = 0;
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
	message[1] = DPPLAYERTYPE_PLAYER;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 1);
	XVT_ASSERT_INT_EQ(count_lines("network.host_lost"), 1);
	XVT_ASSERT_INT_EQ(g_removed_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.players[1].active_flag, 1);
}

/* The host frees a departing player's peer slot: the last slot moves into it
 * and the last is cleared, and the value returned is the slots left in
 * 8-byte words of slot. With no slot for the player its id is returned;
 * with no slot in use, the group removal's result, or the roster count when
 * the player is not in the roster. A group leaving changes nothing. */
static void check_host_departure_slots(void)
{
	int message[3];
	host_with_two_peers();
	g_net_session.reliable_peer_slots[2].direct_play_id = OTHER_DPID;
	scramble_slot(&g_net_session.reliable_peer_slots[2]);
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, PEER_DPID);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  2 * SLOT_WORDS);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slots[1].direct_play_id,
			  OTHER_DPID);
	assert_slot_scrambled(&g_net_session.reliable_peer_slots[1]);
	assert_slot_free(&g_net_session.reliable_peer_slots[2]);
	XVT_ASSERT_INT_EQ(line_value("network.player_destroyed", "slot"), 1);

	host_with_two_peers();
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, 999);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  999);
	XVT_ASSERT_INT_EQ(g_removed_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 3);
	message[1] = 0;
	message[2] = PEER_DPID;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(g_removed_count, 0);
	XVT_ASSERT_INT_EQ(g_net_session.players[1].active_flag, 1);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 3);

	session_world(1);
	use_fake_dplay();
	three_player_roster();
	g_net_session.players[1].active_flag = 0;
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, PEER_DPID);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  0);
	XVT_ASSERT_INT_EQ(g_removed_count, 1);
	message[2] = 999;
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  message[0], message),
			  3);
	XVT_ASSERT_INT_EQ(g_removed_count, 1);
}

/* A rename whose names do not both end within the message is refused with a
 * warning and changes nothing; a rename of a player not in the roster
 * returns the roster count, and of a group 0. Any other message returns its
 * type. */
static void check_rename_refused(void)
{
	struct net_player_name_message message;
	memset(&message, 0, sizeof message);
	message.header.dwType = DPSYS_SETPLAYERORGROUPNAME;
	message.header.dwPlayerType = DPPLAYERTYPE_PLAYER;
	message.header.dpId = OTHER_DPID;
	memset(message.names, 'A', sizeof message.names);
	session_world(0);
	three_player_roster();
	XVT_ASSERT_INT_EQ(
		net_session_handle_direct_play_system_message(
			DPSYS_SETPLAYERORGROUPNAME, (const int *)&message),
		0);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[2].player_name, "Biggs"),
			  0);
	XVT_ASSERT_INT_EQ(line_value("network.rename_rejected", "slot"), 2);
	memcpy(message.names, "New\0Name", sizeof "New\0Name");
	message.header.dpId = 999;
	XVT_ASSERT_INT_EQ(
		net_session_handle_direct_play_system_message(
			DPSYS_SETPLAYERORGROUPNAME, (const int *)&message),
		3);
	message.header.dpId = OTHER_DPID;
	message.header.dwPlayerType = 0;
	XVT_ASSERT_INT_EQ(
		net_session_handle_direct_play_system_message(
			DPSYS_SETPLAYERORGROUPNAME, (const int *)&message),
		0);
	XVT_ASSERT_INT_EQ(strcmp(g_net_session.players[2].player_name, "Biggs"),
			  0);
	XVT_ASSERT_INT_EQ(net_session_handle_direct_play_system_message(
				  0x21, (const int *)&message),
			  0x21);
}

/* ------------------------------------------------------------------------ */
/* net_session_receive_packet: system messages, the peer and its channel,
 * stale entries, the queue's end. */

/* The receive steps around the ring's end past each kind of entry it does
 * not return: a system message not first, a resent copy not first, an entry
 * waiting on a gap, a stale one, one past the peer's examined limit and one
 * past its missing limit. The entry after it, at index 0, is 300's next one
 * and is returned. A system message first at index 1,023 is returned and
 * the read index wraps. */
static void check_receive_wraps_at_queue_end(void)
{
	enum { SYSTEM, COPY, GAP, STALE, OVER_EXAMINED, OVER_MISSING, KINDS };
	for (int kind = 0; kind < KINDS; ++kind) {
		session_world(0);
		switch (kind) {
		case SYSTEM:
			empty_queue_at(1022);
			queue_packet(PEER_DPID, 1, 2, 0, NET_PACKET_CHAT, 1);
			queue_packet(0, 0, 0, 0, DPSYS_CREATEPLAYERORGROUP, 2);
			break;
		case COPY:
			empty_queue_at(1022);
			queue_packet(PEER_DPID, 1, 2, 0, NET_PACKET_CHAT, 1);
			queue_packet(PEER_DPID, 1, 9, 1, NET_PACKET_CHAT, 2);
			break;
		case GAP:
			empty_queue_at(1023);
			queue_packet(PEER_DPID, 1, 2, 0, NET_PACKET_CHAT, 1);
			break;
		case STALE:
			empty_queue_at(1023);
			queue_packet(PEER_DPID, 1, 120, 0, NET_PACKET_CHAT, 1);
			break;
		case OVER_EXAMINED:
			empty_queue_at(1022);
			queue_packet(PEER_DPID, 1, 95, 0, NET_PACKET_CHAT, 1);
			queue_packet(PEER_DPID, 0, 0, 0, NET_PACKET_CHAT, 2);
			break;
		default:
			empty_queue_at(1022);
			queue_packet(PEER_DPID, 1, 30, 0, NET_PACKET_CHAT, 1);
			queue_packet(PEER_DPID, 0, 3, 0, NET_PACKET_CHAT, 2);
			break;
		}
		queue_packet(OTHER_DPID, 1, 0, 0, NET_PACKET_CHAT, 3);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_write_index, 1);
		XVT_ASSERT_INT_EQ(receive_mark(), 3);
	}

	session_world(0);
	empty_queue_at(1023);
	queue_packet(0, 0, 0, 0, DPSYS_CREATEPLAYERORGROUP, 4);
	int sender = -1;
	int size = -1;
	int *packet = net_session_receive_packet(&sender, &size);
	XVT_ASSERT_TRUE(packet != NULL);
	XVT_ASSERT_INT_EQ(packet[MARK_WORD], 4);
	XVT_ASSERT_INT_EQ(size, 8);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_read_index, 0);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
}

/* A peer's entries stop being examined once its examined count, one per
 * entry plus the size of each gap, passes 90: behind a gap of 89 the next
 * entry is returned, behind one of 90 it is not. */
static void check_receive_examined_limit(void)
{
	static const struct {
		int gap;
		int returned;
	} rows[] = {{89, 2}, {90, -1}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		session_world(0);
		queue_packet(PEER_DPID, 1, rows[row].gap, 0, NET_PACKET_CHAT,
			     1);
		queue_packet(PEER_DPID, 0, 0, 0, NET_PACKET_CHAT, 2);
		XVT_ASSERT_INT_EQ(receive_mark(), rows[row].returned);
	}
}

/* A peer's entries stop being examined once more than 25 of its missing
 * sequences were not found: behind a gap of 25 the next entry's own gap is
 * asked about, behind one of 26 it is not. */
static void check_receive_missing_limit(void)
{
	static const struct {
		int gap;
		int asked;
	} rows[] = {{25, 1}, {26, 0}};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		session_world(0);
		queue_packet(PEER_DPID, 1, rows[row].gap, 0, NET_PACKET_CHAT,
			     1);
		queue_packet(PEER_DPID, 0, 2, 0, NET_PACKET_CHAT, 2);
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 1);
		XVT_ASSERT_INT_EQ(queued(1)->nack_retry_count, rows[row].asked);
	}
}

/* An entry from a sender with no peer slot, all 40 being taken, is dropped;
 * a stale entry not first is dropped and the entry behind it examined in its
 * place. */
static void check_receive_drops_without_slot(void)
{
	session_world(0);
	for (int i = 0; i < 40; ++i) {
		slot_of(1000 + i);
	}
	queue_packet(PEER_DPID, 1, 0, 0, NET_PACKET_CHAT, 1);
	queue_packet(1000, 1, 0, 0, NET_PACKET_CHAT, 2);
	XVT_ASSERT_INT_EQ(receive_mark(), 2);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
	XVT_ASSERT_INT_EQ(line_value("network.stale_dropped", "peer"), 40);

	session_world(0);
	queue_packet(PEER_DPID, 0, 5, 0, NET_PACKET_CHAT, 1);
	queue_packet(PEER_DPID, 1, 120, 0, NET_PACKET_CHAT, 2);
	queue_packet(PEER_DPID, 1, 0, 0, NET_PACKET_CHAT, 3);
	XVT_ASSERT_INT_EQ(receive_mark(), 3);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
	XVT_ASSERT_INT_EQ(queued_word(0, MARK_WORD), 1);
}

/* ------------------------------------------------------------------------ */
/* net_session_receive_packet: in order, internet-play input, a gap filled
 * from the queue. */

/* In internet play a REMOTE_INPUT is returned as soon as it is met, on any
 * channel, skipping the gap: its sequence becomes the delivered one and the
 * peer's activity time is now, but it is not counted. One in order is not
 * counted either; any other packet in order is. Outside internet play a
 * REMOTE_INPUT past a gap waits. */
static void check_receive_internet_input(void)
{
	for (int packet_class = 0; packet_class < 3; ++packet_class) {
		session_world(0);
		g_game_config.internet_play = 1;
		struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
		*last_delivered(peer, packet_class) = 5;
		g_net_last_delivered_recv_sequence = 99;
		xvt_time_advance_host_clock(500 * MS_US);
		queue_packet(PEER_DPID, packet_class, 9, 0,
			     NET_PACKET_REMOTE_INPUT, 1);
		XVT_ASSERT_INT_EQ(receive_mark(), 1);
		XVT_ASSERT_INT_EQ(*last_delivered(peer, packet_class), 9);
		XVT_ASSERT_INT_EQ(g_net_last_delivered_recv_sequence, 9);
		XVT_ASSERT_INT_EQ(peer->last_activity_ms, 1500);
		XVT_ASSERT_INT_EQ(peer->packet_count, 0);
		XVT_ASSERT_INT_EQ(
			count_lines_holding("network.packet_delivered",
					    "path=\"internet_input\""),
			1);
		queue_packet(PEER_DPID, packet_class, 10, 0,
			     NET_PACKET_REMOTE_INPUT, 2);
		XVT_ASSERT_INT_EQ(receive_mark(), 2);
		XVT_ASSERT_INT_EQ(peer->packet_count, 0);
		g_game_config.internet_play = 0;
		xvt_time_advance_host_clock(500 * MS_US);
		queue_packet(PEER_DPID, packet_class, 11, 0,
			     NET_PACKET_REMOTE_INPUT, 3);
		XVT_ASSERT_INT_EQ(receive_mark(), 3);
		XVT_ASSERT_INT_EQ(peer->packet_count, 1);
		XVT_ASSERT_INT_EQ(peer->last_activity_ms, 2000);
		XVT_ASSERT_INT_EQ(*last_delivered(peer, packet_class), 11);
		XVT_ASSERT_INT_EQ(g_net_last_delivered_recv_sequence, 11);
		queue_packet(PEER_DPID, packet_class, 13, 0,
			     NET_PACKET_REMOTE_INPUT, 4);
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
	}
}

/* On each channel a gap is filled by a resent copy of the expected sequence
 * found in the queue: it is returned, delivered and counted. The waiting
 * entry's resend count and time are cleared when the copy was all it
 * lacked. */
static void check_receive_gap_filled_channels(void)
{
	for (int packet_class = 0; packet_class < 3; ++packet_class) {
		for (int gap = 1; gap <= 2; ++gap) {
			session_world(0);
			g_net_last_delivered_recv_sequence = 99;
			queue_packet(PEER_DPID, packet_class, gap, 0,
				     NET_PACKET_CHAT, 11);
			queued(0)->nack_retry_count = 2;
			queued(0)->last_nack_ms = 5;
			queue_packet(PEER_DPID, packet_class, 0, 1,
				     NET_PACKET_CHAT, 10);
			XVT_ASSERT_INT_EQ(receive_mark(), 10);
			struct net_reliable_peer_slot *peer =
				slot_of(PEER_DPID);
			XVT_ASSERT_INT_EQ(*last_delivered(peer, packet_class),
					  0);
			XVT_ASSERT_INT_EQ(peer->packet_count, 1);
			XVT_ASSERT_INT_EQ(g_net_last_delivered_recv_sequence,
					  0);
			XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
			XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count,
					  gap == 1 ? 0 : 2);
			XVT_ASSERT_INT_EQ(queued(0)->last_nack_ms,
					  gap == 1 ? 0 : 5);
			XVT_ASSERT_INT_EQ(
				count_lines_holding("network.packet_delivered",
						    "path=\"gap_filled\""),
				1);
		}
	}
}

/* A missing sequence whose resent copy is queued is not asked for again,
 * though only the expected one is returned; the copy stays queued. When
 * every sequence between an entry and the one examined before it is queued,
 * the entry's own resend count and time are cleared. */
static void check_receive_copies_not_asked_again(void)
{
	session_world(0);
	use_fake_dplay();
	queue_packet(PEER_DPID, 1, 3, 0, NET_PACKET_CHAT, 3);
	queue_packet(PEER_DPID, 1, 1, 1, NET_PACKET_CHAT, 1);
	XVT_ASSERT_INT_EQ(receive_mark(), -1);
	XVT_ASSERT_INT_EQ(g_sent_count, 2);
	XVT_ASSERT_INT_EQ(sent_type(0), NET_PACKET_NACK);
	XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY), 0);
	XVT_ASSERT_INT_EQ(sent_word(1, GAME_BODY), 2);
	XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 2);
	XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 1);
	XVT_ASSERT_INT_EQ(queued(1)->is_resent_copy, 1);

	for (int copy = 0; copy <= 1; ++copy) {
		session_world(0);
		queue_packet(PEER_DPID, 1, 5, 0, NET_PACKET_CHAT, 5);
		if (copy) {
			queue_packet(PEER_DPID, 1, 6, 1, NET_PACKET_CHAT, 6);
		}
		queue_packet(PEER_DPID, 1, 6 + copy, 0, NET_PACKET_CHAT, 7);
		queued(1 + copy)->nack_retry_count = 3;
		queued(1 + copy)->last_nack_ms = 5;
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 1);
		XVT_ASSERT_INT_EQ(queued(1 + copy)->nack_retry_count, 0);
		XVT_ASSERT_INT_EQ(queued(1 + copy)->last_nack_ms, 0);
	}
}

/* ------------------------------------------------------------------------ */
/* net_session_receive_packet: the first resend request, the waits, giving
 * up. */

/* A gap is asked about with one NACK per missing sequence, holding it and the
 * channel, sent to the entry's sender; a world message on the all-players
 * channel gets a WORLD_NACK instead, holding the missing message's tick, the
 * world message's first word less g_net_update_interval_ticks per sequence
 * between, and the sequence. The entry then counts one request at the time
 * now, and the peer one per missing sequence. */
static void check_receive_first_nack(void)
{
	static const struct {
		int packet_class;
		int type;
		int nack_type;
	} rows[] = {
		{1, NET_PACKET_CHAT, NET_PACKET_NACK},
		{2, NET_PACKET_CHAT, NET_PACKET_NACK},
		{0, NET_PACKET_CHAT, NET_PACKET_NACK},
		{0, NET_PACKET_WORLD_MESSAGE, NET_PACKET_WORLD_NACK},
		{1, NET_PACKET_WORLD_MESSAGE, NET_PACKET_NACK},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int packet_class = rows[row].packet_class;
		session_world(0);
		use_fake_dplay();
		struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
		*last_delivered(peer, packet_class) = 5;
		queue_packet(PEER_DPID, packet_class, 8, 0, rows[row].type,
			     (int)(0x80000000u | 1000u));
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		XVT_ASSERT_INT_EQ(g_sent_count, 2);
		for (int i = 0; i < 2; ++i) {
			XVT_ASSERT_INT_EQ(sent_message(i)->to, PEER_DPID);
			XVT_ASSERT_INT_EQ(sent_type(i), rows[row].nack_type);
			XVT_ASSERT_INT_EQ(sent_short(i, GAME_LENGTH), 8);
			if (rows[row].nack_type == NET_PACKET_WORLD_NACK) {
				XVT_ASSERT_INT_EQ(
					sent_word(i, GAME_BODY),
					1000 - (2 -
						i) * g_net_update_interval_ticks);
				XVT_ASSERT_INT_EQ(sent_word(i, GAME_BODY + 4),
						  6 + i);
			} else {
				XVT_ASSERT_INT_EQ(sent_word(i, GAME_BODY),
						  6 + i);
				XVT_ASSERT_INT_EQ(sent_word(i, GAME_BODY + 4),
						  packet_class);
			}
		}
		XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count, 1);
		XVT_ASSERT_INT_EQ(queued(0)->last_nack_ms, 1000);
		XVT_ASSERT_INT_EQ(peer->packet_retry_count, 2);
	}
}

/* The requests are repeated after a wait that doubles each time, from 2,000
 * ms; once the entry's request count passes 3, or 5 for a world message, the
 * next wait's end gives up the gap and returns the entry, delivered and
 * counted. */
static void check_receive_waits_then_gives_up(void)
{
	static const struct {
		int packet_class;
		int type;
		int limit;
	} rows[] = {
		{1, NET_PACKET_CHAT, 3},
		{0, NET_PACKET_WORLD_MESSAGE, 5},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int packet_class = rows[row].packet_class;
		int limit = rows[row].limit;
		session_world(0);
		use_fake_dplay();
		struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
		*last_delivered(peer, packet_class) = 5;
		queue_packet(PEER_DPID, packet_class, 7, 0, rows[row].type, 77);
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		XVT_ASSERT_INT_EQ(g_sent_count, 1);
		for (int retries = 1; retries <= limit; ++retries) {
			xvt_time_advance_host_clock((1000 << retries) * MS_US);
			XVT_ASSERT_INT_EQ(receive_mark(), -1);
			XVT_ASSERT_INT_EQ(g_sent_count, retries);
			xvt_time_advance_host_clock(MS_US);
			XVT_ASSERT_INT_EQ(receive_mark(), -1);
			XVT_ASSERT_INT_EQ(g_sent_count, retries + 1);
			XVT_ASSERT_INT_EQ(queued(0)->nack_retry_count,
					  retries + 1);
			XVT_ASSERT_INT_EQ(queued(0)->last_nack_ms,
					  (int)timeGetTime());
		}
		xvt_time_advance_host_clock((1000 << (limit + 1)) * MS_US);
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		xvt_time_advance_host_clock(MS_US);
		XVT_ASSERT_INT_EQ(receive_mark(), 77);
		XVT_ASSERT_INT_EQ(g_sent_count, limit + 1);
		XVT_ASSERT_INT_EQ(*last_delivered(peer, packet_class), 7);
		XVT_ASSERT_INT_EQ(g_net_last_delivered_recv_sequence, 7);
		XVT_ASSERT_INT_EQ(peer->packet_count, 1);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 0);
		XVT_ASSERT_INT_EQ(line_value("network.nack_gave_up", "limit"),
				  limit);
		XVT_ASSERT_INT_EQ(
			count_lines_holding("network.packet_delivered",
					    "path=\"gave_up\""),
			1);
	}
}

/* With reliable_use_fixed_resend_timeouts set, a gap is given up at the end
 * of the first wait: 3,000 ms, or 40,000 ms for a world message. */
static void check_receive_fixed_timeouts(void)
{
	static const struct {
		int packet_class;
		int type;
		int wait_ms;
	} rows[] = {
		{1, NET_PACKET_CHAT, 3000},
		{2, NET_PACKET_CHAT, 3000},
		{0, NET_PACKET_WORLD_MESSAGE, 40000},
	};
	for (size_t row = 0; row < sizeof rows / sizeof rows[0]; ++row) {
		int packet_class = rows[row].packet_class;
		session_world(0);
		use_fake_dplay();
		g_net_session.reliable_use_fixed_resend_timeouts = 1;
		struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
		*last_delivered(peer, packet_class) = 5;
		queue_packet(PEER_DPID, packet_class, 7, 0, rows[row].type, 77);
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		xvt_time_advance_host_clock(rows[row].wait_ms * MS_US);
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		xvt_time_advance_host_clock(MS_US);
		XVT_ASSERT_INT_EQ(receive_mark(), 77);
		XVT_ASSERT_INT_EQ(g_sent_count, 1);
		XVT_ASSERT_INT_EQ(*last_delivered(peer, packet_class), 7);
		XVT_ASSERT_INT_EQ(line_value("network.nack_gave_up", "limit"),
				  0);
	}
}

/* Giving up a gap returns the first packet held past it, on any channel: a
 * queued resent copy of a later missing sequence, found across the 127 to 0
 * wrap, before the entry that waited, which then comes next in order. */
static void check_receive_give_up_returns_copy(void)
{
	for (int packet_class = 0; packet_class < 3; ++packet_class) {
		session_world(0);
		use_fake_dplay();
		g_net_session.reliable_use_fixed_resend_timeouts = 1;
		g_net_last_delivered_recv_sequence = 99;
		struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
		*last_delivered(peer, packet_class) = 126;
		queue_packet(PEER_DPID, packet_class, 1, 0, NET_PACKET_CHAT, 1);
		queue_packet(PEER_DPID, packet_class, 0, 1, NET_PACKET_CHAT, 0);
		XVT_ASSERT_INT_EQ(receive_mark(), -1);
		XVT_ASSERT_INT_EQ(g_sent_count, 1);
		XVT_ASSERT_INT_EQ(sent_word(0, GAME_BODY), 127);
		xvt_time_advance_host_clock(3001 * MS_US);
		XVT_ASSERT_INT_EQ(receive_mark(), 0);
		XVT_ASSERT_INT_EQ(*last_delivered(peer, packet_class), 0);
		XVT_ASSERT_INT_EQ(g_net_last_delivered_recv_sequence, 0);
		XVT_ASSERT_INT_EQ(peer->packet_count, 1);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1);
		XVT_ASSERT_INT_EQ(
			count_lines_holding("network.packet_delivered",
					    "path=\"gave_up\""),
			1);
		XVT_ASSERT_INT_EQ(receive_mark(), 1);
		XVT_ASSERT_INT_EQ(peer->packet_count, 2);
	}
}

/* ------------------------------------------------------------------------ */
/* net_session_receive_packet: the full-queue pass. */

/* With 1,023 entries queued and nothing returned, the first entry already
 * asked about is returned on any channel, its gap given up with a warning
 * naming the expected sequence, which wraps from 127 to 0: the entry becomes
 * the delivered sequence and is counted. With 1,022 nothing is returned. */
static void check_receive_full_queue_channels(void)
{
	for (int row = 0; row < 9; ++row) {
		int packet_class = row % 3;
		int delivered = row < 6 ? 5 : 127;
		int entries = row < 3 ? 1022 : 1023;
		int sequence = (delivered + 4) % 128;
		session_world(0);
		g_net_last_delivered_recv_sequence = 99;
		struct net_reliable_peer_slot *peer = slot_of(PEER_DPID);
		*last_delivered(peer, packet_class) = delivered;
		queue_packet(PEER_DPID, packet_class, sequence, 0,
			     NET_PACKET_CHAT, sequence);
		queued(0)->nack_retry_count = 1;
		queued(0)->last_nack_ms = (int)timeGetTime();
		for (int i = 1; i < entries; ++i) {
			queue_packet(OTHER_DPID, 1, 50, 1, NET_PACKET_CHAT,
				     1000 + i);
		}
		if (entries == 1022) {
			XVT_ASSERT_INT_EQ(receive_mark(), -1);
			XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1022);
			continue;
		}
		XVT_ASSERT_INT_EQ(receive_mark(), sequence);
		XVT_ASSERT_INT_EQ(*last_delivered(peer, packet_class),
				  sequence);
		XVT_ASSERT_INT_EQ(g_net_last_delivered_recv_sequence, sequence);
		XVT_ASSERT_INT_EQ(peer->packet_count, 1);
		XVT_ASSERT_INT_EQ(g_net_recv_queue_count, 1022);
		XVT_ASSERT_INT_EQ(line_value("network.queue_full_gap_skipped",
					     "expected"),
				  (delivered + 1) % 128);
	}
	empty_queue();
}

/* ------------------------------------------------------------------------ */
/* Known failures. */

/* Known failure resent_copy_first_discarded, issue #112: net_reliable.h says
 * only resent copies fill a gap when the receiver searches the queue, and the
 * function promises the entry with the next expected sequence. A resent copy
 * of sequence 1 is first in the queue before sequence 0 has come; it is thrown
 * away, so once 0 is delivered, 1 is never delivered. */
static void check_resent_copy_kept_for_gap(void)
{
	session_world(0);
	queue_packet(PEER_DPID, 1, 1, 1, NET_PACKET_CHAT, 21);
	receive_mark();
	queue_packet(PEER_DPID, 1, 0, 0, NET_PACKET_CHAT, 20);
	XVT_ASSERT_INT_EQ(receive_mark(), 20);
	XVT_ASSERT_INT_EQ(receive_mark(), 21);
}

/* Known failure peer_slot_keeps_retry_count, issue #114: net_reliable.h gives
 * packet_retry_count as the gaps in this peer's packets for which a resend
 * was requested. Player 300 holds the last peer slot, with 9 such gaps, and
 * leaves; the next new sender gets that slot and starts with 9 gaps it never
 * had. */
static void check_freed_slot_retry_count(void)
{
	int message[3];
	host_with_two_peers();
	g_net_session.reliable_peer_slots[2].packet_retry_count = 9;
	system_message(message, DPSYS_DESTROYPLAYERORGROUP, 300);
	net_session_handle_direct_play_system_message(message[0], message);
	XVT_ASSERT_INT_EQ(g_net_session.reliable_peer_slot_count, 2);
	unsigned int slot = net_reliable_find_or_create_peer_slot(400);
	XVT_ASSERT_INT_EQ(slot, 2);
	XVT_ASSERT_INT_EQ(
		g_net_session.reliable_peer_slots[slot].packet_retry_count, 0);
}

int main(int argc, char **argv)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	/* Every session init sets the flag that picks the system messages'
	 * branches, and nothing clears it, so the branches for 0 run only
	 * before the first init: these checks come first, also before a
	 * known-failure check. */
	check_join_before_any_session();
	check_departure_before_any_session();
	/* The session is opened once, as a host flying alone; every later
	 * system message then takes the flight's branches. */
	check_solo_init();
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"resent_copy_first_discarded",
			 check_resent_copy_kept_for_gap},
			{"peer_slot_keeps_retry_count",
			 check_freed_slot_retry_count},
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
	check_roster_lookups();
	check_player_names();
	check_enumerate_players();
	check_enumerate_stops_at_eight();
	check_send_comes_back_in_order();
	check_send_routes();
	check_receive_next_expected();
	check_receive_channels();
	check_receive_drops_stale();
	check_receive_gap_filled();
	check_receive_system_message();
	check_receive_game_packet();
	check_receive_full_queue();
	check_host_player_departs();
	check_departure_without_notice();
	check_host_player_joins();
	check_player_renamed();
	check_keepalive();
	check_init_resets_session();
	check_solo_init_with_interface();
	check_multiplayer_init();
	check_pump_needs_interface();
	check_pump_full_queue();
	check_pump_system_messages();
	check_pump_misaddressed();
	check_pump_long_body();
	check_pump_ping_and_keepalive_ack();
	check_pump_world_nack();
	check_pump_nack();
	check_pump_keepalive();
	check_pump_resent_copies();
	check_pump_resent_copy_sizes();
	check_pump_previous_missing();
	check_pump_previous_copy_cut();
	check_pump_queues_packets();
	check_send_resync_types_bare();
	check_send_channels_and_saved_copies();
	check_send_internet_input();
	check_send_histories();
	check_send_queued_for_self();
	check_send_full_queue();
	check_send_results();
	check_host_join_sequence_status();
	check_host_departure_reported();
	check_host_departure_slots();
	check_rename_refused();
	check_receive_wraps_at_queue_end();
	check_receive_examined_limit();
	check_receive_missing_limit();
	check_receive_drops_without_slot();
	check_receive_internet_input();
	check_receive_gap_filled_channels();
	check_receive_copies_not_asked_again();
	check_receive_first_nack();
	check_receive_waits_then_gives_up();
	check_receive_fixed_timeouts();
	check_receive_give_up_returns_copy();
	check_receive_full_queue_channels();
	return 0;
}

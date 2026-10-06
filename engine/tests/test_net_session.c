/* Tests for xvt/net/net_session.c, the original flight's network session: its
 * roster and lookups, the receive queue's delivery order, and the host's
 * answers to DirectPlay's system messages. Each check sets the session it
 * needs in g_net_session and fills the receive queue itself, entry by entry,
 * as the receive pump would. The session is opened once at the start as a host
 * flying alone, which is what makes later system messages take the flight's
 * branches. No game data is read.
 *
 * Packets sent with no DirectPlay interface only come back to this player's own
 * queue. Where a function needs DirectPlay, the checks give the session a stand
 * in that records what is sent and removed and answers every call with
 * success; no packet leaves the machine.
 *
 * Not checked here: the receive pump, which needs DirectPlay's own message
 * format from a peer, and the resend requests and time outs of a gap. */
#include <stdint.h>
#include <string.h>

#include "aeron/compat/dplay.h"
#include "test_assert.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt_runtime/timing/host_clock.h"

enum {
	LOCAL_DPID = 1,
	PEER_DPID = 200,
	SECOND_US = 1000000,
	MARK_WORD = 1, /* Payload word that tells test packets apart */
};

/* ------------------------------------------------------------------------ */
/* A DirectPlay stand in. */

static int g_sent_count;
static DPID g_sent_to;
static uint8_t g_sent_bytes[1024];
static int g_removed_count;
static DPID g_removed_player;

static HRESULT AERON_DXAPI fake_send(IDirectPlay2A *self, DPID from_id,
				     DPID to_id, uint32_t flags, void *data,
				     uint32_t data_size)
{
	(void)self;
	(void)from_id;
	(void)flags;
	++g_sent_count;
	g_sent_to = to_id;
	memcpy(g_sent_bytes, data,
	       data_size < sizeof g_sent_bytes ? data_size
					       : sizeof g_sent_bytes);
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
	g_fake_vtbl.DeletePlayerFromGroup = fake_delete_player_from_group;
	g_fake_vtbl.EnumPlayers = fake_enum_players;
	g_net_session.dplay_interface = &g_fake_dplay;
	g_sent_count = 0;
	g_sent_to = 0;
	g_removed_count = 0;
	g_removed_player = 0;
	g_listed_count = 0;
}

/* ------------------------------------------------------------------------ */
/* The session and its queue. */

static void empty_queue(void)
{
	g_net_recv_queue_count = 0;
	g_net_recv_queue_read_index = 0;
	g_net_recv_queue_write_index = 0;
}

/* A session with no DirectPlay interface whose roster holds this player alone,
 * id 1, active. No peer slot is in use, the queue is empty and the host clock
 * reads one second. */
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
	g_net_session_sent_history_write_index = 0;
	g_net_session_sent_world_message_write_index = 0;
	xvt_time_reset();
	xvt_time_advance_host_clock(SECOND_US);
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
	return 0;
}

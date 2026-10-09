#include "xvt/net/net_session.h"

#include <string.h>

#include "aeron/compat/dplay.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/frontend/config.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/network_session.h"

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

/* Next slot, 0-127, that net_session_send_packet fills in
 * g_net_session_sent_history; it wraps to 0. net_session_init_game_session takes it
 * from the frontend's saved state, and net_session_shutdown hands it back. */
// GLOBAL: XVT 0x52701C
int g_net_session_sent_history_write_index = 0;
/* Next slot, 0-255, that net_session_send_packet fills in
 * g_net_session_sent_world_message_history; it wraps to 0. Zeroed by
 * net_session_init_game_session; net_session_shutdown hands it to the frontend. */
// GLOBAL: XVT 0x527020
int g_net_session_sent_world_message_write_index = 0;
/* The last 128 packets this player sent to others, each with its destination,
 * size, channel class (0 all players, 1 direct, 2 group) and sequence, kept so
 * net_session_pump_incoming_packets can resend one a peer asks for. Internet-play
 * inputs are not kept. Filled by net_session_send_packet;
 * net_session_init_game_session loads the frontend's saved copy and
 * net_session_shutdown hands it back. */
// GLOBAL: XVT 0x5DD9B8
struct net_queued_packet g_net_session_sent_history[128] = {0};
/* The last 256 world messages this player sent, kept so
 * net_session_pump_incoming_packets can resend the one with the tick a WORLD_NACK
 * asks for. Filled by net_session_send_packet; cleared by
 * net_session_init_game_session. */
// GLOBAL: XVT 0x5EE1B8
struct net_queued_packet g_net_session_sent_world_message_history[256] = {0};
/* The flight's network session: DirectPlay interface and ids, the player
 * roster, channel sequences and saved copies, and reliable delivery state per
 * peer. net_session_init_game_session clears it and loads it from the frontend's
 * saved state. 18 functions write it, chiefly net_session_init_game_session,
 * net_session_send_packet, net_session_pump_incoming_packets,
 * net_session_receive_packet, net_session_handle_direct_play_system_message and the
 * NetReliable_ functions. */
// GLOBAL: XVT 0x9994C0
struct net_session_state g_net_session = {0};
/* The buffer session packets are built in just before they are sent: startup
 * and roster packets, peer sequence status and keepalives. 5 functions write
 * it: net_session_init_game_session,
 * net_session_handle_direct_play_system_message,
 * net_session_send_reliable_keepalives, xvt_flight_network_send_roster_record
 * and xvt_flight_network_exchange_roster. */
// GLOBAL: XVT 0x5597A0
struct net_session_scratch_state g_net_session_scratch_packet = {0};
/* Picks the branches net_session_handle_direct_play_system_message takes.
 * net_session_init_game_session sets it to 1 and nothing sets it back to 0, so
 * after the first session of a run the branches for 0 never run. */
// GLOBAL: XVT 0x9EC608
static uint8_t g_net_session_flight_handshake_active = 0;

/* Part of net_session_init_game_session: clears g_net_session, stores
 * network_type in it and gives its two channels and its 40 reliable peer slots
 * their opening values; sets g_net_session_flight_handshake_active, and clears
 * g_net_session_scratch_packet.trailing_state,
 * g_net_session_sent_world_message_history with its write index and
 * g_net_recv_queue_count. */
static void net_session_reset_for_flight(network_transport_type network_type)
{
	memset(&g_net_session, 0, sizeof(g_net_session));
	g_net_session_flight_handshake_active = 1;
	g_net_session_scratch_packet.trailing_state = 0;
	g_net_session.network_type = network_type;
	g_net_session.broadcast_seq_counter = 0;
	g_net_session.broadcast_piggyback_empty = 1;
	g_net_session.broadcast_payload[0] = NET_PACKET_NOP;
	g_net_session.broadcast_payload_length = 1;
	g_net_session.group_seq_counter = 0;
	g_net_session.group_piggyback_empty = 1;
	g_net_session.group_payload[0] = NET_PACKET_NOP;
	g_net_session.group_payload_length = 1;
	g_net_session.reliable_use_fixed_resend_timeouts = 0;
	for (int player_index = 0; player_index < 40; ++player_index) {
		g_net_session.reliable_peer_slots[player_index]
			.last_delivered_seq_default = 127;
		g_net_session.reliable_peer_slots[player_index]
			.last_delivered_seq_channel_a = 127;
		g_net_session.reliable_peer_slots[player_index]
			.last_delivered_seq_channel_b = 127;
		g_net_session.reliable_peer_slots[player_index]
			.recv_seq_default = 127;
		g_net_session.reliable_peer_slots[player_index]
			.recv_seq_channel_a = 127;
		g_net_session.reliable_peer_slots[player_index]
			.recv_seq_channel_b = 127;
		g_net_session.reliable_peer_slots[player_index].send_seq = 0;
		g_net_session.reliable_peer_slots[player_index].direct_play_id =
			0;
		g_net_session.reliable_peer_slots[player_index]
			.last_piggyback_type = NET_PACKET_NOP;
		g_net_session.reliable_peer_slots[player_index]
			.piggyback_length = 1;
		g_net_session.reliable_peer_slots[player_index]
			.last_activity_ms = 0;
		g_net_session.reliable_peer_slots[player_index].packet_count =
			0;
		g_net_session.reliable_peer_slots[player_index]
			.packet_drop_count = 0;
	}
	g_net_session_sent_world_message_write_index = 0;
	memset(g_net_session_sent_world_message_history, 0,
	       sizeof(g_net_session_sent_world_message_history));
	g_net_recv_queue_count = 0;
	g_net_session.reliable_peer_slot_count = 0;
}

/* Part of net_session_init_game_session: loads the state the frontend saved
 * through net_session_import_runtime_state into g_net_session,
 * g_net_session_recv_queue with its indices and count, and
 * g_net_session_sent_history with its write index, and logs what it loaded. */
static void net_session_import_lobby_state(void)
{
	net_session_import_runtime_state(
		(void **)&g_net_session.dplay_interface,
		&g_net_session.app_guid, &g_net_session.instance_guid,
		(int32_t *)&g_net_session.group_dplay_id,
		&g_net_session.host_dplay_id,
		(struct net_player_info *)&g_net_session.local_player_info,
		g_net_session_recv_queue, &g_net_recv_queue_read_index,
		(int *)&g_net_recv_queue_count, &g_net_recv_queue_write_index,
		g_net_session.reliable_peer_slots,
		&g_net_session.reliable_peer_slot_count,
		&g_net_session.broadcast_seq_counter,
		(char *)g_net_session.broadcast_payload,
		&g_net_session.broadcast_payload_length,
		&g_net_session.broadcast_piggyback_empty,
		&g_net_session.group_seq_counter,
		(char *)g_net_session.group_payload,
		&g_net_session.group_payload_length,
		&g_net_session.group_piggyback_empty,
		g_net_session_sent_history,
		&g_net_session_sent_history_write_index);
	XVT_LOG_DEBUG(
		"network.flight_session_imported interface=%d local=%u peers=%u queued=%u read=%d write=%d history=%d broadcast_seq=%u group_seq=%u",
		g_net_session.dplay_interface != NULL,
		(unsigned)g_net_session.local_player_info.direct_play_id,
		g_net_session.reliable_peer_slot_count, g_net_recv_queue_count,
		g_net_recv_queue_read_index, g_net_recv_queue_write_index,
		g_net_session_sent_history_write_index,
		(unsigned)g_net_session.broadcast_seq_counter,
		(unsigned)g_net_session.group_seq_counter);
}

/* Part of net_session_init_game_session for a host flying alone: loads the
 * saved state, takes id 1 without a DirectPlay interface, takes formal_name and
 * pilot_name as its names, becomes roster slot 0 and returns 1. */
static int net_session_open_solo(const char *formal_name,
				 const char *pilot_name, int is_host,
				 int in_progress_launch)
{
	int success = 1;
	DPCAPS direct_play_caps;
	net_session_import_lobby_state();
	if (g_net_session.dplay_interface == NULL) {
		g_net_session.local_player_info.direct_play_id = success;
		g_net_session.local_player_info.active_flag = success;
	} else {
		memset(&direct_play_caps, 0, sizeof(direct_play_caps));
		direct_play_caps.dwSize = sizeof(direct_play_caps);
		g_net_session.dplay_interface->lpVtbl->GetCaps(
			g_net_session.dplay_interface, &direct_play_caps, 0);
	}
	strncpy(g_net_session.local_player_info.long_name, formal_name,
		sizeof(g_net_session.local_player_info.long_name));
	strncpy(g_net_session.local_player_info.player_name, pilot_name,
		sizeof(g_net_session.local_player_info.player_name));
	g_net_session.players[0] = g_net_session.local_player_info;
	g_net_session.local_is_host = is_host;
	g_net_session.player_count = success;
	XVT_LOG_INFO(
		"network.flight_session_open mode=\"solo\" host=%d players=%d in_progress=%d",
		is_host, g_net_session.player_count, in_progress_launch);
	return success;
}

/* Opens the flight's network session from the state the frontend saved
 * (net_session_import_runtime_state): DirectPlay interface, ids, receive queue,
 * reliable peer slots, channel sequences and sent history. It first clears
 * g_net_session and g_net_recv_queue_count, resets the 40 reliable peer slots,
 * g_net_session_sent_world_message_history and its write index, and
 * g_net_session_scratch_packet.trailing_state, and sets
 * g_net_session_flight_handshake_active; the import then fills
 * g_net_session_recv_queue with its indices and count, and
 * g_net_session_sent_history with its write index. A solo host takes formalName
 * and pilot_name as its names, becomes roster slot 0 and returns 1; without a
 * DirectPlay interface its id is 1. Otherwise it lists the DirectPlay players
 * that g_pilot_data.network_players also holds, and sends the host a
 * STARTUP_READY and a NOP. It then returns
 * xvt_flight_network_begin_roster_exchange's result, which is
 * XVT_FLIGHT_NETWORK_PENDING unless a client joins a flight in progress.
 * mp_game_name and connection_address are unused. Does not check for a
 * DirectPlay interface outside the solo path. */
// FUNCTION: XVT 0x46C230
int net_session_init_game_session(const char *formal_name,
				  const char *pilot_name, int is_host,
				  const char *mp_game_name,
				  network_transport_type network_type,
				  int num_human_players, int in_progress_launch,
				  const char *connection_address)
{

	char dial_number[32] = "Dial a New Number.";
	(void)dial_number;
	(void)mp_game_name;
	(void)connection_address;
	XVT_LOG_DEBUG(
		"network.session_init host=%d players=%d in_progress=%d transport=%d",
		is_host, num_human_players, in_progress_launch,
		(int)network_type);
	net_session_reset_for_flight(network_type);
	/* The 1 stored in success here also serves below as the player count,
	 * the host flag, a DirectPlay id and an active flag. */
	int success = 1;

	DPCAPS direct_play_caps;
	if (num_human_players == success && is_host == success) {
		/* The single-player path reuses the persisted DirectPlay snapshot. */
		return net_session_open_solo(formal_name, pilot_name, is_host,
					     in_progress_launch);
	}

	net_session_import_lobby_state();
	memset(&direct_play_caps, 0, sizeof(direct_play_caps));
	direct_play_caps.dwSize = sizeof(direct_play_caps);
	g_net_session.dplay_interface->lpVtbl->GetCaps(
		g_net_session.dplay_interface, &direct_play_caps, 0);
	XVT_LOG_DEBUG(
		"network.dplay_caps buffer=%u queue=%u players=%u timeout=%u",
		(unsigned)direct_play_caps.dwMaxBufferSize,
		(unsigned)direct_play_caps.dwMaxQueueSize,
		(unsigned)direct_play_caps.dwMaxPlayers,
		(unsigned)direct_play_caps.dwTimeout);
	g_net_session.player_count = 0;
	g_net_session.local_is_host = is_host;
	net_session_enumerate_players();
	g_net_session_flight_handshake_active = 1;
	timeGetTime();
	g_net_session_scratch_packet.packet_type = NET_PACKET_STARTUP_READY;
	net_session_send_packet(g_net_session.host_dplay_id,
				(unsigned int *)&g_net_session_scratch_packet,
				4);
	g_net_session_scratch_packet.packet_type = NET_PACKET_NOP;
	net_session_send_packet(g_net_session.host_dplay_id,
				(unsigned int *)&g_net_session_scratch_packet,
				4);

	XVT_LOG_INFO(
		"network.flight_session_open mode=\"multi\" host=%d players=%d in_progress=%d",
		is_host, num_human_players, in_progress_launch);
	return xvt_flight_network_begin_roster_exchange(num_human_players,
							in_progress_launch);
}

/* Hands this session's network state back to the frontend through
 * net_session_export_runtime_state: receive queue, reliable peer slots, channel
 * sequences and saved copies, sent history, and the sent world-message history
 * with its write index. Closes nothing. Returns 1. */
// FUNCTION: XVT 0x46C6A0
int net_session_shutdown(void)
{
	net_session_export_runtime_state(
		(void **)&g_net_session.dplay_interface,
		&g_net_session.app_guid, &g_net_session.instance_guid,
		(int *)&g_net_session.group_dplay_id,
		&g_net_session.host_dplay_id, &g_net_session.local_player_info,
		g_net_session_recv_queue, &g_net_recv_queue_read_index,
		(int *)&g_net_recv_queue_count, &g_net_recv_queue_write_index,
		g_net_session.reliable_peer_slots,
		(int *)&g_net_session.reliable_peer_slot_count,
		(int *)&g_net_session.broadcast_seq_counter,
		g_net_session.broadcast_payload,
		&g_net_session.broadcast_payload_length,
		&g_net_session.broadcast_piggyback_empty,
		(int *)&g_net_session.group_seq_counter,
		g_net_session.group_payload,
		&g_net_session.group_payload_length,
		&g_net_session.group_piggyback_empty,
		g_net_session_sent_history,
		&g_net_session_sent_history_write_index,
		g_net_session_sent_world_message_history,
		&g_net_session_sent_world_message_write_index);
	XVT_LOG_INFO("network.flight_session_end queued=%u peers=%u",
		     g_net_recv_queue_count,
		     g_net_session.reliable_peer_slot_count);
	XVT_LOG_DEBUG(
		"network.flight_session_exported history=%d world_history=%d broadcast_seq=%u group_seq=%u read=%d write=%d",
		g_net_session_sent_history_write_index,
		g_net_session_sent_world_message_write_index,
		(unsigned)g_net_session.broadcast_seq_counter,
		(unsigned)g_net_session.group_seq_counter,
		g_net_recv_queue_read_index, g_net_recv_queue_write_index);
	return 1;
}

/* Has DirectPlay list every player through net_session_enum_players_callback,
 * which adds them to the roster. Returns 1. Does not reset the roster count
 * first or check for a DirectPlay interface. */
// FUNCTION: XVT 0x46C730
int net_session_enumerate_players(void)
{
	g_net_session.dplay_interface->lpVtbl->EnumPlayers(
		g_net_session.dplay_interface, 0,
		net_session_enum_players_callback, 0, 0);
	XVT_LOG_DEBUG("network.players_enumerated players=%d",
		      g_net_session.player_count);
	return 1;
}

/* Adds one DirectPlay player to the roster: skips entries of type 0 (not
 * players) and players missing from g_pilot_data.network_players; stores the long
 * name in long_name and the short name in playerName, cut to 15 characters,
 * with the id and active_flag 1, and counts it in g_net_session.player_count.
 * Returns 0, which stops the listing, once 8 players are in; else 1. */
// FUNCTION: XVT 0x46C750
int AERON_DXAPI net_session_enum_players_callback(DPID dplay_id,
						  uint32_t player_type,
						  const DPNAME *name_info,
						  uint32_t flags, void *context)
{
	(void)flags;
	(void)context;

	if (player_type == 0) {
		XVT_LOG_DEBUG(
			"network.player_skipped player=%u reason=\"not_player\"",
			(unsigned)dplay_id);
		return 1;
	}
	if (g_net_session.player_count >= 8) {
		XVT_LOG_WARN("network.roster_full player=%u",
			     (unsigned)dplay_id);
		return 0;
	}
	if (pilot_data_has_network_player_dpid(dplay_id) != 0) {
		strncpy(g_net_session.players[g_net_session.player_count]
				.long_name,
			name_info->lpszLongNameA, 16);
		strncpy(g_net_session.players[g_net_session.player_count]
				.player_name,
			name_info->lpszShortNameA, 16);
		char *name_end =
			&g_net_session.players[g_net_session.player_count]
				 .long_name[15];
		*name_end = '\0';
		name_end = &g_net_session.players[g_net_session.player_count]
				    .player_name[15];
		*name_end = '\0';
		int *player_value =
			&g_net_session.players[g_net_session.player_count]
				 .direct_play_id;
		*player_value = dplay_id;
		player_value =
			&g_net_session.players[g_net_session.player_count]
				 .active_flag;
		*player_value = 1;
		XVT_LOG_DEBUG(
			"network.player_added slot=%d player=%u name=\"%s\" long_name=\"%s\"",
			g_net_session.player_count, (unsigned)dplay_id,
			g_net_session.players[g_net_session.player_count]
				.player_name,
			g_net_session.players[g_net_session.player_count]
				.long_name);
		g_net_session.player_count++;
	} else {
		XVT_LOG_DEBUG(
			"network.player_skipped player=%u reason=\"not_in_mission\"",
			(unsigned)dplay_id);
	}
	return 1;
}

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

/* Returns the roster, g_net_session.players, and stores its entry count in
 * *outCount. */
// FUNCTION: XVT 0x46DFA0
struct session_player_info *net_session_get_player_roster(int *out_count)
{
	*out_count = g_net_session.player_count;
	return g_net_session.players;
}

/* Returns the roster count, or 1 when it is 0. */
// FUNCTION: XVT 0x46E000
int net_session_get_player_count(void)
{
	if (g_net_session.player_count == 0) {
		return 1;
	}
	return g_net_session.player_count;
}

/* Returns g_net_session.local_is_host, nonzero when this player hosts. */
// FUNCTION: XVT 0x46E040
int net_session_is_local_host(void) { return g_net_session.local_is_host; }

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

enum {
	RELIABLE_SEQUENCE_SENTINEL = 127,
	PEER_SNAPSHOT_METADATA_DWORD_COUNT = 3,
	PEER_SNAPSHOT_STRIDE = 8,
	PEER_PREV_CHANNEL_A_OFFSET = 4,
	PEER_PREV_CHANNEL_B_OFFSET = 5,
	PEER_CHANNEL_A_OFFSET = 6,
	PEER_CHANNEL_B_OFFSET = 7,
	PEER_SLOT_QWORD_COUNT =
		sizeof(struct net_reliable_peer_slot) / sizeof(uint64_t)
};

/* Part of net_session_handle_direct_play_system_message for a new player: sends
 * the player in packet[2] a SEQUENCE_STATUS with every reliable peer slot's id
 * and sequences, built in g_net_session_scratch_packet. */
static void net_session_send_sequence_status(const int *packet)
{
	int peer_index;
	struct net_reliable_peer_slot *peer;
	uint8_t *encoded_peer;
	g_net_session_scratch_packet.payload_dwords[0] = 0;
	g_net_session_scratch_packet.packet_type = NET_PACKET_SEQUENCE_STATUS;
	g_net_session_scratch_packet.payload_dwords[1] =
		(int)g_net_session.reliable_peer_slot_count;
	g_net_session_scratch_packet.payload_dwords[2] = (int)timeGetTime();
	encoded_peer =
		(uint8_t *)&g_net_session_scratch_packet
			.payload_dwords[PEER_SNAPSHOT_METADATA_DWORD_COUNT];
	for (peer_index = 0;
	     peer_index < (int)g_net_session.reliable_peer_slot_count;
	     ++peer_index) {
		peer = &g_net_session.reliable_peer_slots[peer_index];
		memcpy(&encoded_peer[peer_index * PEER_SNAPSHOT_STRIDE],
		       &peer->direct_play_id, sizeof(peer->direct_play_id));
		encoded_peer[peer_index * PEER_SNAPSHOT_STRIDE +
			     PEER_PREV_CHANNEL_A_OFFSET] =
			(uint8_t)peer->last_delivered_seq_channel_a;
		encoded_peer[peer_index * PEER_SNAPSHOT_STRIDE +
			     PEER_PREV_CHANNEL_B_OFFSET] =
			(uint8_t)peer->last_delivered_seq_channel_b;
		encoded_peer[peer_index * PEER_SNAPSHOT_STRIDE +
			     PEER_CHANNEL_A_OFFSET] =
			(uint8_t)peer->recv_seq_channel_a;
		encoded_peer[peer_index * PEER_SNAPSHOT_STRIDE +
			     PEER_CHANNEL_B_OFFSET] =
			(uint8_t)peer->recv_seq_channel_b;
	}
	net_session_send_packet(
		packet[2], (unsigned int *)&g_net_session_scratch_packet,
		8 * (int)g_net_session.reliable_peer_slot_count + 16);
}

/* Part of net_session_handle_direct_play_system_message for
 * DPSYS_CREATEPLAYERORGROUP: only the host acts. It sends the new player in
 * packet[2] its sequence status and session status, and while
 * g_net_session_flight_handshake_active is 0 it lists the roster again and adds
 * every other player to the group. Returns the function's result. */
static int net_session_on_player_created(const int *packet)
{
	int result;
	uint8_t handshake_active;
	int player_index;
	result = net_session_is_local_host();
	if (result == 0) {
		return result;
	}
	handshake_active = g_net_session_flight_handshake_active;
	result = packet[1];
	if (handshake_active != 0) {
		if (result == 1 &&
		    packet[2] !=
			    g_net_session.local_player_info.direct_play_id) {
			net_session_send_sequence_status(packet);
			g_net_session_scratch_packet.packet_type =
				NET_PACKET_FLIGHT_SESSION_STATUS;
			g_net_session_scratch_packet.payload_dwords[0] =
				mission_get_elapsed_clock_seconds();
			g_net_session_scratch_packet.payload_dwords[1] =
				FRONTEND_NET_PROTOCOL_VERSION;
			g_net_session_scratch_packet.payload_dwords[2] = 0;
			XVT_LOG_INFO(
				"network.player_joined_flight player=%u peers=%u seconds=%d",
				(unsigned)packet[2],
				g_net_session.reliable_peer_slot_count,
				g_net_session_scratch_packet.payload_dwords[0]);
			return net_session_send_packet(
				packet[2],
				(unsigned int *)&g_net_session_scratch_packet,
				16);
		}
	} else if (result == 1) {
		if (packet[2] !=
		    g_net_session.local_player_info.direct_play_id) {
			net_session_send_sequence_status(packet);
			g_net_session_scratch_packet.packet_type =
				NET_PACKET_FLIGHT_SESSION_STATUS;
			g_net_session_scratch_packet.payload_dwords[0] = 0;
			net_session_send_packet(
				packet[2],
				(unsigned int *)&g_net_session_scratch_packet,
				8);
		}
		g_net_session.player_count = 0;
		net_session_enumerate_players();
		for (player_index = 0;
		     player_index < g_net_session.player_count;
		     ++player_index) {
			if (g_net_session.players[player_index]
				    .direct_play_id !=
			    g_net_session.local_player_info.direct_play_id) {
				net_session_add_player_to_group(
					&g_net_session.players[player_index]);
			}
		}
	}
	return result;
}

/* Part of net_session_handle_direct_play_system_message for a departed player:
 * gives reliable peer slot g_net_session.reliable_peer_slot_count, the first
 * past the slots in use, its opening values. */
static void net_session_clear_last_peer_slot(void)
{
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.direct_play_id = 0;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.last_delivered_seq_default = RELIABLE_SEQUENCE_SENTINEL;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.last_delivered_seq_channel_a = RELIABLE_SEQUENCE_SENTINEL;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.last_delivered_seq_channel_b = RELIABLE_SEQUENCE_SENTINEL;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.recv_seq_default = RELIABLE_SEQUENCE_SENTINEL;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.recv_seq_channel_a = RELIABLE_SEQUENCE_SENTINEL;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.recv_seq_channel_b = RELIABLE_SEQUENCE_SENTINEL;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.send_seq = 0;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.last_piggyback_type = NET_PACKET_NOP;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.piggyback_length = 1;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.last_activity_ms = 0;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.packet_count = 0;
	g_net_session
		.reliable_peer_slots[g_net_session.reliable_peer_slot_count]
		.packet_drop_count = 0;
}

/* Part of net_session_handle_direct_play_system_message for a departed player:
 * frees reliable peer slot player_index by moving the last slot into its place,
 * and clears the last slot. */
static void net_session_free_peer_slot(int player_index)
{
	--g_net_session.reliable_peer_slot_count;
	memcpy(&g_net_session.reliable_peer_slots[player_index],
	       &g_net_session.reliable_peer_slots
			[g_net_session.reliable_peer_slot_count],
	       sizeof(g_net_session.reliable_peer_slots[player_index]));
	net_session_clear_last_peer_slot();
}

/* Part of net_session_handle_direct_play_system_message for a departed
 * player: finds the player in packet[2] in the roster, queues this player a
 * STARTUP_READY if it was active, removes it from the DirectPlay group and
 * marks it inactive; result gets the roster count, then the removal's
 * result. */
static void net_session_mark_player_departed(int *result, const int *packet)
{
	int player_index;
	*result = g_net_session.player_count;
	for (player_index = 0; player_index < g_net_session.player_count;
	     ++player_index) {
		if (g_net_session.players[player_index].direct_play_id ==
		    packet[2]) {
			if (g_net_session.players[player_index].active_flag !=
			    0) {
				g_net_session_scratch_packet.packet_type =
					NET_PACKET_STARTUP_READY;
				net_session_send_packet(
					g_net_session.local_player_info
						.direct_play_id,
					(unsigned int
						 *)&g_net_session_scratch_packet,
					4);
			}
			*result =
				net_session_remove_player_from_group(packet[2]);
			XVT_LOG_INFO(
				"network.player_destroyed player=%u slot=%d active=%d result=%#x",
				(unsigned)packet[2], player_index,
				g_net_session.players[player_index].active_flag,
				(unsigned)*result);
			g_net_session.players[player_index].active_flag = 0;
			break;
		}
	}
}

/* Part of net_session_handle_direct_play_system_message for a departed player
 * while g_net_session_flight_handshake_active is set: marks the player in
 * packet[2] inactive and frees its reliable peer slot. Returns the function's
 * result. */
static int net_session_on_player_left_in_flight(const int *packet)
{
	int result;
	int player_index;
	result = packet[1];
	if (result == 1) {
		net_session_mark_player_departed(&result, packet);
		/* Below, player_index indexes reliable peer
		 * slots, to drop the departed player's slot. */
		player_index = 0;
		if ((int)g_net_session.reliable_peer_slot_count <=
		    player_index) {
			return result;
		}
		result = packet[2];
		do {
			if (g_net_session.reliable_peer_slots[player_index]
				    .direct_play_id == (DPID)packet[2]) {
				XVT_LOG_DEBUG(
					"network.peer_slot_freed player=%u peer=%d delivered=%d drops=%d gaps=%d left=%u",
					(unsigned)packet[2], player_index,
					g_net_session
						.reliable_peer_slots
							[player_index]
						.packet_count,
					g_net_session
						.reliable_peer_slots
							[player_index]
						.packet_drop_count,
					g_net_session
						.reliable_peer_slots
							[player_index]
						.packet_retry_count,
					g_net_session.reliable_peer_slot_count -
						1);
				net_session_free_peer_slot(player_index);
				return PEER_SLOT_QWORD_COUNT *
				       (int)g_net_session
					       .reliable_peer_slot_count;
			}
			++player_index;
		} while (player_index <
			 (int)g_net_session.reliable_peer_slot_count);
		return result;
	}
	return result;
}

/* Part of net_session_handle_direct_play_system_message for
 * DPSYS_DESTROYPLAYERORGROUP: reports a departed host to
 * xvt_network_session_host_lost; then only the host acts, removing the player
 * in packet[2] from the DirectPlay group and freeing its reliable peer slot.
 * Returns the function's result. */
static int net_session_on_player_destroyed(const int *packet)
{
	int result;
	int player_index;
	if (packet[1] == DPPLAYERTYPE_PLAYER &&
	    packet[2] == g_net_session.host_dplay_id) {
		xvt_network_session_host_lost();
	}
	result = net_session_is_local_host();
	if (result == 0) {
		return result;
	}
	if (g_net_session_flight_handshake_active != 0) {
		return net_session_on_player_left_in_flight(packet);
	}
	result = packet[1];
	if (result != 1) {
		return result;
	}
	g_net_session.player_count = 0;
	net_session_enumerate_players();
	result = net_session_remove_player_from_group(packet[2]);
	/* Below, player_index indexes reliable peer slots, to drop the
	 * departed player's slot. */
	player_index = 0;
	if ((int)g_net_session.reliable_peer_slot_count <= player_index) {
		return result;
	}
	result = packet[2];
	do {
		if (g_net_session.reliable_peer_slots[player_index]
			    .direct_play_id == (DPID)packet[2]) {
			net_session_free_peer_slot(player_index);
			return PEER_SLOT_QWORD_COUNT *
			       (int)g_net_session.reliable_peer_slot_count;
		}
		++player_index;
	} while (player_index < (int)g_net_session.reliable_peer_slot_count);
	return result;
}

/* Part of net_session_handle_direct_play_system_message for
 * DPSYS_SETPLAYERORGROUPNAME: copies the new names into the matching roster
 * entry, cut to 12 characters. Returns the function's result. */
static int net_session_on_player_renamed(const int *packet)
{
	int result;
	int player_index;
	result = ((const struct net_player_name_message *)packet)
			 ->header.dwPlayerType;
	if (result == 1) {
		result = g_net_session.player_count;
		for (player_index = 0;
		     player_index < g_net_session.player_count;
		     ++player_index) {
			if (g_net_session.players[player_index]
				    .direct_play_id ==
			    (int)((const struct net_player_name_message *)
					  packet)
				    ->header.dpId) {
				if (!xvt_network_session_copy_player_names(
					    (const struct
					     net_player_name_message *)packet,
					    g_net_session.players[player_index]
						    .player_name,
					    sizeof(g_net_session
							   .players[player_index]
							   .player_name),
					    g_net_session.players[player_index]
						    .long_name,
					    sizeof(g_net_session
							   .players[player_index]
							   .long_name))) {
					XVT_LOG_WARN(
						"network.rename_rejected player=%u slot=%d",
						(unsigned)g_net_session
							.players[player_index]
							.direct_play_id,
						player_index);
					return 0;
				}
				g_net_session.players[player_index]
					.player_name[12] = '\0';
				g_net_session.players[player_index]
					.long_name[12] = '\0';
				XVT_LOG_DEBUG(
					"network.player_renamed player=%u slot=%d name=\"%s\" long_name=\"%s\"",
					(unsigned)g_net_session
						.players[player_index]
						.direct_play_id,
					player_index,
					g_net_session.players[player_index]
						.player_name,
					g_net_session.players[player_index]
						.long_name);
				return 0;
			}
		}
	}
	return result;
}

/* Acts on a DirectPlay system message, writing g_net_session and
 * g_net_session_scratch_packet; only the host acts, except on a name change. A
 * new player is sent a SEQUENCE_STATUS with every reliable peer slot's id and
 * sequences, then a FLIGHT_SESSION_STATUS with the mission's elapsed seconds
 * and the protocol version (103). A departed player is first reported to
 * xvt_network_session_host_lost when it was the host; the host then queues
 * itself a STARTUP_READY if the player was active, removes it from the
 * DirectPlay group, marks it inactive, and frees its reliable peer slot by
 * moving the last slot into its place. A name change updates the matching
 * roster entry's names, cut to 12 characters, with a bounded copy
 * (xvt_network_session_copy_player_names). When
 * g_net_session_flight_handshake_active is 0, a new player's session status
 * carries 0 instead, and the roster is listed again with every other player
 * added to the group, and a departure lists the roster again; that never
 * happens after a session init. Returns a value no caller uses. */
// FUNCTION: XVT 0x46E080
int net_session_handle_direct_play_system_message(int packet_opcode,
						  const int *packet)
{
	int result = packet_opcode;
	XVT_LOG_DEBUG("network.system_message_handled opcode=%d host=%d",
		      packet_opcode, g_net_session.local_is_host);

	switch (packet_opcode) {
	case DPSYS_CREATEPLAYERORGROUP:
		return net_session_on_player_created(packet);

	case DPSYS_DESTROYPLAYERORGROUP:
		return net_session_on_player_destroyed(packet);

	case DPSYS_SETPLAYERORGROUPNAME:
		return net_session_on_player_renamed(packet);

	default:
		return result;
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
	uint8_t inspected[40];
	uint8_t retry_counts[40];
	uint8_t last_sequences[40][3];
	uint8_t inspection_limits[40];
	struct net_queued_packet *packet;
	unsigned int peer_index;
	int search_sequence;
	int direct_play_id;
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

	if ((int)g_net_recv_queue_count < 1023) {
		return NULL;
	}
	return net_session_take_from_full_queue(out_sender_dpid,
						out_payload_size);
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

/* Returns the roster slot whose DirectPlay id is dpid, or 8 when none is. */
// FUNCTION: XVT 0x46F660
int net_session_find_player_slot_by_dpid(int dpid)
{
	int player_slot;

	for (player_slot = 0; player_slot < 8; ++player_slot) {
		if (g_net_session.players[player_slot].direct_play_id == dpid) {
			return player_slot;
		}
	}
	XVT_LOG_DEBUG("network.player_slot_missing player=%u", (unsigned)dpid);

	return player_slot;
}

/* Returns the host's DirectPlay id, g_net_session.host_dplay_id. */
// FUNCTION: XVT 0x46F700
int net_session_get_host_dplay_id(void) { return g_net_session.host_dplay_id; }

/* Returns this player's DirectPlay id. */
// FUNCTION: XVT 0x46F710
int net_session_get_local_dplay_id(void)
{
	return g_net_session.local_player_info.direct_play_id;
}

/* Returns the short name of the roster entry in player_slot, found by matching
 * DirectPlay ids; in a one-player session, this player's own name whatever the
 * slot. When no entry matches, it returns NULL. */
// FUNCTION: XVT 0x46F720
char *net_session_get_player_name(int player_slot)
{
	if (g_net_session.player_count == 1) {
		return g_net_session.local_player_info.player_name;
	}

	int roster_index = 0;
	if (g_net_session.player_count > roster_index) {
		do {
			if (net_session_find_player_slot_by_dpid(
				    g_net_session.players[roster_index]
					    .direct_play_id) == player_slot) {
				return g_net_session.players[roster_index]
					.player_name;
			}
			++roster_index;
		} while (roster_index < g_net_session.player_count);
	}
	XVT_LOG_DEBUG("network.player_name_missing slot=%d players=%d",
		      player_slot, g_net_session.player_count);
	return NULL;
}

/* Asks DirectPlay to add the player to the session group,
 * g_net_session.group_dplay_id; returns DirectPlay's result code. Does not check
 * for a DirectPlay interface. */
// FUNCTION: XVT 0x46FA10
int net_session_add_player_to_group(
	const struct session_player_info *player_info)
{
	return g_net_session.dplay_interface->lpVtbl->AddPlayerToGroup(
		g_net_session.dplay_interface, g_net_session.group_dplay_id,
		player_info->direct_play_id);
}

/* Returns how many of the 8 roster slots have active_flag 1. */
// FUNCTION: XVT 0x46FA30
int net_session_count_active_players(void)
{
	int active_player_count = 0;

	for (int player_index = 0; player_index < 8; ++player_index) {
		if (g_net_session.players[player_index].active_flag == 1) {
			++active_player_count;
		}
	}
	return active_player_count;
}

/* Asks DirectPlay to remove the player from the session group; returns
 * DirectPlay's result code. Does not check for a DirectPlay interface. */
// FUNCTION: XVT 0x46FA50
int net_session_remove_player_from_group(int player_dplay_id)
{
	return g_net_session.dplay_interface->lpVtbl->DeletePlayerFromGroup(
		g_net_session.dplay_interface, g_net_session.group_dplay_id,
		(DPID)player_dplay_id);
}

/* Returns 1 and does nothing else. */
// FUNCTION: XVT 0x46FAC0
int net_session_stub_return_true(void) { return 1; }

/* Returns 0 for every packet type: no type has a fixed size, so every packet
 * outside the resync types carries a 2-byte length. */
// FUNCTION: XVT 0x46FAD0
int net_session_get_fixed_payload_size(int packet_type)
{
	(void)packet_type;
	return 0;
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

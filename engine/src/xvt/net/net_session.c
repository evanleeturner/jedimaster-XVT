#include "xvt/net/net_session.h"

#include <string.h>

#include "aeron/compat/dplay.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/network_session.h"

/* Next slot, 0-127, that net_session_record_sent_packet fills in
 * g_net_session_sent_history; it wraps to 0. net_session_import_lobby_state
 * takes it from the frontend's saved state, and net_session_shutdown hands it
 * back. */
// GLOBAL: XVT 0x52701C
int g_net_session_sent_history_write_index = 0;
/* Next slot, 0-255, that net_session_record_sent_packet fills in
 * g_net_session_sent_world_message_history; it wraps to 0. Zeroed by
 * net_session_reset_for_flight; net_session_shutdown hands it to the frontend.
 */
// GLOBAL: XVT 0x527020
int g_net_session_sent_world_message_write_index = 0;
/* The last 128 packets this player sent to others, each with its destination,
 * size, channel class (0 all players, 1 direct, 2 group) and sequence, kept so
 * net_session_resend_from_history and net_session_resend_for_keepalive can
 * resend one a peer asks for. Internet-play inputs are not kept. Filled by
 * net_session_record_sent_packet; net_session_import_lobby_state loads the
 * frontend's saved copy and net_session_shutdown hands it back. */
// GLOBAL: XVT 0x5DD9B8
struct net_queued_packet g_net_session_sent_history[128] = {0};
/* The last 256 world messages this player sent, kept so
 * net_session_answer_world_nack can find the one with the tick a WORLD_NACK
 * asks for, which net_session_resend_world_message resends. Filled by
 * net_session_record_sent_packet; cleared by net_session_reset_for_flight. */
// GLOBAL: XVT 0x5EE1B8
struct net_queued_packet g_net_session_sent_world_message_history[256] = {0};
/* The flight's network session: DirectPlay interface and ids, the player
 * roster, channel sequences and saved copies, and reliable delivery state per
 * peer. net_session_reset_for_flight clears it and
 * net_session_import_lobby_state loads it from the frontend's saved state. Many
 * functions write it, chiefly those of net_session_send.c, net_session_pump.c
 * and net_session_receive.c, the system message functions of this file and the
 * net_reliable_ functions. */
// GLOBAL: XVT 0x9994C0
struct net_session_state g_net_session = {0};
/* The buffer session packets are built in just before they are sent: startup
 * and roster packets, peer sequence status and keepalives. 8 functions write
 * it: net_session_reset_for_flight, net_session_init_game_session,
 * net_session_send_sequence_status, net_session_on_player_created,
 * net_session_mark_player_departed, net_session_send_reliable_keepalives,
 * xvt_flight_network_send_roster_record and xvt_flight_network_exchange_roster.
 */
// GLOBAL: XVT 0x5597A0
struct net_session_scratch_state g_net_session_scratch_packet = {0};
/* Picks the branches net_session_on_player_created and
 * net_session_on_player_destroyed take. net_session_reset_for_flight and
 * net_session_init_game_session set it to 1 and nothing sets it back to 0, so
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
 * g_net_session_sent_history with its write index. A solo host takes
 * formal_name and pilot_name as its names, becomes roster slot 0 and returns 1;
 * without a DirectPlay interface its id is 1. Otherwise it lists the DirectPlay
 * players that g_pilot_data.network_players also holds, and sends the host a
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

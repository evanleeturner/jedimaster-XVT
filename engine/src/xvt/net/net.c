#include "xvt/net/net.h"

#include <string.h>

#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net_reliable.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/network_session.h"

/* Transport of the open lobby session. Written by xvt_network_session_update
 * (TCP/IP), and reset to IPX by net_shutdown_direct_play_session_ex when it
 * closes DirectPlay. Nothing reads it. */
// GLOBAL: XVT 0x665020
network_transport_type g_net_active_transport_type = NET_TRANSPORT_IPX;

/* Service provider GUID the game passes to DirectPlay for an IPX game. */
// GLOBAL: XVT 0x5182A0
static const GUID g_net_direct_play_ipx_service_provider_guid = {
	0x685BC400,
	0x9D2C,
	0x11CF,
	{0xA9, 0xCD, 0x00, 0xAA, 0x00, 0x68, 0x86, 0xE3},
};
/* Service provider GUID the game passes to DirectPlay for a modem game. */
// GLOBAL: XVT 0x5182B0
static const GUID g_net_direct_play_modem_service_provider_guid = {
	0x44EAA760,
	0xCB68,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
/* Service provider GUID the game passes to DirectPlay for a TCP/IP game, the
 * only transport the game uses. */
// GLOBAL: XVT 0x5182C0
static const GUID g_net_direct_play_tcp_ip_service_provider_guid = {
	0x36E95EE0,
	0x8577,
	0x11CF,
	{0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82},
};
/* Service provider GUID the game passes to DirectPlay for a serial game. */
// GLOBAL: XVT 0x5182D0
static const GUID g_net_direct_play_serial_service_provider_guid = {
	0x0F1D6860,
	0x88D9,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
/* Interface id that xvt_network_session_factory passes to QueryInterface to get
 * the IDirectPlay2A interface kept in g_front_state.net_direct_play. */
// GLOBAL: XVT 0x518EF0
const GUID IID_IDirectPlay2A = {
	0x9D460580,
	0xA822,
	0x11CF,
	{0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82},
};
/* Copy of the provider GUID that net_get_direct_play_service_provider_guid last
 * looked up; that function returns this copy's address. */
// GLOBAL: XVT 0x665040
static GUID g_net_direct_play_service_provider_guid_scratch = {0};
/* Link figures for up to 40 players, keyed by DirectPlay id. On the host,
 * net_pump_incoming_packets fills them from each player's keepalive acks; on
 * the other players, the Net_SetPlayer* setters store what the host's lobby
 * packets report. Seven writers in all. Cleared by xvt_network_session_factory;
 * the host clears a player's entry when the player leaves
 * (net_handle_direct_play_system_message). */
// GLOBAL: XVT 0x665050
struct net_player_connection_stats g_net_player_connection_stats[40];

/* Closes the lobby session: net_shutdown_direct_play_session_ex(1, 1). */
// FUNCTION: XVT 0x4CD9C0
void net_shutdown_direct_play_session_for_quit(void)
{
	net_shutdown_direct_play_session_ex(1, 1);
}

/* Closes the lobby session: net_shutdown_direct_play_session_ex(0, 1). */
// FUNCTION: XVT 0x4CD9D0
void net_shutdown_direct_play_session(void)
{
	net_shutdown_direct_play_session_ex(0, 1);
}

/* Closes the lobby session. It first calls xvt_network_session_on_close. When
 * DirectPlay is open it sets g_net_active_transport_type to IPX, destroys the
 * local player and the group, and closes and releases DirectPlay. In every case
 * it empties the receive queue and resets the broadcast and group counters and
 * the 40 peer slots. Returns 1. The back buffer is unlocked meanwhile and
 * relocked when it was locked. Both arguments are only logged. */
// FUNCTION: XVT 0x4CD9F0
int net_shutdown_direct_play_session_ex(int suppress_restart,
					int wait_for_handshake_acks)
{
	enum {
		NET_DESTROY_PLAYER_TIMEOUT_MS = 20000,
		NET_RELIABLE_PEER_CAPACITY =
			sizeof(g_front_state.net_runtime_reliable_peer_slots) /
			sizeof(g_front_state
				       .net_runtime_reliable_peer_slots[0]),
		NET_RELIABLE_SEQUENCE_INITIAL = 127
	};

	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	xvt_network_session_on_close();
	XVT_LOG_DEBUG(
		"network.lobby_closing open=%d player=%u group=%u queued=%d peers=%u quit=%d handshake=%d",
		g_front_state.net_direct_play != NULL,
		(unsigned)g_front_state.net_runtime_local_player.player_id,
		(unsigned)g_front_state.net_group_dplay_id,
		g_front_state.net_runtime_recv_queue_count,
		(unsigned)g_front_state.net_reliable_peer_slot_count,
		suppress_restart, wait_for_handshake_acks);
	if (g_front_state.net_direct_play != NULL) {
		(void)suppress_restart;
		(void)wait_for_handshake_acks;
		g_net_active_transport_type = NET_TRANSPORT_IPX;
		g_front_state.net_direct_play->lpVtbl->DestroyPlayer(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id);
		if (g_front_state.net_group_dplay_id != 0) {
			g_front_state.net_direct_play->lpVtbl->DestroyGroup(
				g_front_state.net_direct_play,
				g_front_state.net_group_dplay_id);
			g_front_state.net_group_dplay_id = 0;
		}
		g_front_state.net_direct_play->lpVtbl->Close(
			g_front_state.net_direct_play);
		g_front_state.net_direct_play->lpVtbl->Release(
			g_front_state.net_direct_play);
		g_front_state.net_direct_play = NULL;
	}

	g_front_state.net_runtime_recv_queue_write_index = 0;
	g_front_state.net_runtime_recv_queue_read_index = 0;
	g_front_state.net_runtime_recv_queue_count = 0;
	g_front_state.net_runtime_broadcast_seq_counter = 0;
	g_front_state.net_runtime_broadcast_pending_payload.piggyback_empty = 1;
	g_front_state.net_runtime_group_seq_counter = 0;
	g_front_state.net_runtime_group_pending_payload.piggyback_empty = 1;
	g_front_state.net_reliable_peer_slot_count = 0;
	g_front_state.net_reliable_retry_long_timeout_mode = 0;
	for (int peer_index = 0; peer_index < NET_RELIABLE_PEER_CAPACITY;
	     ++peer_index) {
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_delivered_seq_default =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_a =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_delivered_seq_channel_b =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.recv_seq_default = NET_RELIABLE_SEQUENCE_INITIAL;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.recv_seq_channel_a = NET_RELIABLE_SEQUENCE_INITIAL;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.recv_seq_channel_b = NET_RELIABLE_SEQUENCE_INITIAL;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.send_seq = 0;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.direct_play_id = 0;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_piggyback_type = NET_PACKET_NOP;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.piggyback_length = 1;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_activity_ms = 0;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_heard_ms = 0;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.packet_count = 0;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.packet_drop_count = 0;
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.packet_retry_count = 0;
	}
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 1;
}

/* Rebuilds the lobby roster, g_front_state.net_players, from DirectPlay's
 * players: keeps entry 0 (the local player), clears the rest, sets
 * g_front_state.net_player_count to 1 and enumerates through
 * net_enum_players_callback, then carries each player's ready flag over by id.
 * Returns 0 without DirectPlay, else 1. The back buffer is unlocked
 * meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4CDC20
int net_refresh_player_roster(void)
{
	if (g_front_state.net_direct_play == NULL) {
		XVT_LOG_DEBUG("network.lobby_roster_skipped");
		return 0;
	}
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	struct net_player_info old_players[32];
	memcpy(old_players, g_front_state.net_players, sizeof(old_players));
	memset(&g_front_state.net_players[1], 0,
	       sizeof(g_front_state.net_players) -
		       sizeof(g_front_state.net_players[0]));
	g_front_state.net_player_count = 1;
	g_front_state.net_direct_play->lpVtbl->EnumPlayers(
		g_front_state.net_direct_play, NULL, net_enum_players_callback,
		NULL, 0);
	for (int player_index = 0;
	     player_index < g_front_state.net_player_count; ++player_index) {
		for (int old_player_index = 0; old_player_index < 32;
		     ++old_player_index) {
			if (old_players[old_player_index].player_id ==
			    g_front_state.net_players[player_index].player_id) {
				g_front_state.net_players[player_index]
					.ready_flag =
					old_players[old_player_index]
						.ready_flag;
				break;
			}
		}
	}
	XVT_LOG_DEBUG("network.lobby_roster players=%d",
		      g_front_state.net_player_count);
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 1;
}

/* Player enumeration callback: adds one DirectPlay player to the roster (long
 * name to long_name and short name to playerName, each cut to 15
 * characters, its id, not ready) and raises g_front_state.net_player_count.
 * Skips entries of type 0 and the local player. Returns 0, which stops the
 * enumeration, once 32 players are listed; else 1. */
// FUNCTION: XVT 0x4CDCF0
int AERON_DXAPI net_enum_players_callback(DPID player_id, uint32_t player_type,
					  const DPNAME *name_desc,
					  uint32_t flags, void *context)
{
	(void)flags;
	(void)context;

	if (g_front_state.net_player_count >= 32) {
		XVT_LOG_WARN("network.lobby_roster_full players=%d",
			     g_front_state.net_player_count);
		return 0;
	}
	if (player_type == 0) {
		return 1;
	}
	if (g_front_state.net_players[0].player_id == player_id) {
		return 1;
	}
	strncpy(g_front_state.net_players[g_front_state.net_player_count]
			.long_name,
		name_desc->lpszLongNameA,
		sizeof(g_front_state.net_players[g_front_state.net_player_count]
			       .long_name));
	strncpy(g_front_state.net_players[g_front_state.net_player_count]
			.player_name,
		name_desc->lpszShortNameA,
		sizeof(g_front_state.net_players[g_front_state.net_player_count]
			       .player_name));
	g_front_state.net_players[g_front_state.net_player_count]
		.long_name[15] = '\0';
	g_front_state.net_players[g_front_state.net_player_count]
		.player_name[15] = '\0';
	XVT_LOG_DEBUG(
		"network.lobby_player_listed index=%d player=%u name=\"%s\"",
		g_front_state.net_player_count, (unsigned)player_id,
		g_front_state.net_players[g_front_state.net_player_count]
			.player_name);
	g_front_state.net_players[g_front_state.net_player_count].player_id =
		player_id;
	g_front_state.net_players[g_front_state.net_player_count++].ready_flag =
		0;
	return 1;
}

/* Creates the local DirectPlay player with the given long and short names and
 * returns its id, or 0 on failure. It tries once and returns
 * XVT_NETWORK_PENDING while DirectPlay reports the call pending. */
// FUNCTION: XVT 0x4CDE40
int net_create_direct_play_player(const char *long_player_info,
				  const char *short_player_name)
{
	DPID player = 0;
	DPNAME name = {sizeof(name), 0, (char *)short_player_name,
		       (char *)long_player_info};
	HRESULT result = g_front_state.net_direct_play->lpVtbl->CreatePlayer(
		g_front_state.net_direct_play, &player, &name, NULL, NULL, 0,
		0);
	XVT_LOG_DEBUG("network.lobby_player_created result=%#x player=%u",
		      (unsigned)result, (unsigned)player);
	return result == DPERR_PENDING ? XVT_NETWORK_PENDING
	       : result == 0	       ? (int)player
				       : 0;
}

/* Copies the DirectPlay service provider GUID for a transport into
 * g_net_direct_play_service_provider_guid_scratch and returns its address; returns
 * NULL for an unknown transport. */
// FUNCTION: XVT 0x4CE050
const GUID *
net_get_direct_play_service_provider_guid(network_transport_type network_type)
{
	const GUID *result;

	switch (network_type) {
	case NET_TRANSPORT_IPX:
		g_net_direct_play_service_provider_guid_scratch =
			g_net_direct_play_ipx_service_provider_guid;
		result = &g_net_direct_play_service_provider_guid_scratch;
		break;
	case NET_TRANSPORT_TCPIP:
		g_net_direct_play_service_provider_guid_scratch =
			g_net_direct_play_tcp_ip_service_provider_guid;
		result = &g_net_direct_play_service_provider_guid_scratch;
		break;
	case NET_TRANSPORT_MODEM:
		g_net_direct_play_service_provider_guid_scratch =
			g_net_direct_play_modem_service_provider_guid;
		result = &g_net_direct_play_service_provider_guid_scratch;
		break;
	case NET_TRANSPORT_SERIAL:
		g_net_direct_play_service_provider_guid_scratch =
			g_net_direct_play_serial_service_provider_guid;
		result = &g_net_direct_play_service_provider_guid_scratch;
		break;
	default:
		XVT_LOG_ERROR("network.transport_unknown transport=%d",
			      (int)network_type);
		result = NULL;
		break;
	}
	return result;
}

/* Returns the lobby roster, g_front_state.net_players, with its count in
 * *outCount. */
// FUNCTION: XVT 0x4CFE80
struct net_player_info *net_get_player_roster(int *out_count)
{
	*out_count = g_front_state.net_player_count;
	return g_front_state.net_players;
}

/* Returns g_front_state.net_ready_player_left_this_frame, which
 * net_handle_direct_play_system_message sets on the host when a ready player
 * leaves and the frontend's frame loop clears every frame. */
// FUNCTION: XVT 0x4CFED0
int net_did_ready_player_leave_this_frame(void)
{
	return g_front_state.net_ready_player_left_this_frame;
}

/* Returns g_front_state.net_is_host, nonzero on the lobby session's host. */
// FUNCTION: XVT 0x4CFEE0
int net_is_host(void) { return g_front_state.net_is_host; }

/* Pumps incoming packets, then returns 1 when more than 512 packets are
 * queued or a queued packet from a player has the given type, else 0; 0
 * also without DirectPlay. Nothing is taken from the queue. The back buffer
 * is unlocked meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4CFEF0
int net_poll_for_packet_type_or_backlog(int packet_type)
{
	enum {
		QUEUE_CAPACITY =
			sizeof(g_front_state.net_runtime_recv_queue) /
			sizeof(g_front_state.net_runtime_recv_queue[0]),
		BACKLOG_THRESHOLD = QUEUE_CAPACITY / 2,
	};

	if (g_front_state.net_direct_play == NULL) {
		return 0;
	}
	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	net_pump_incoming_packets();
	if (g_front_state.net_runtime_recv_queue_count > BACKLOG_THRESHOLD) {
		XVT_LOG_WARN("network.lobby_backlog queued=%d",
			     g_front_state.net_runtime_recv_queue_count);
		if (back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		return 1;
	}
	int queue_index = g_front_state.net_runtime_recv_queue_read_index;
	int remaining = g_front_state.net_runtime_recv_queue_count;
	while (remaining > 0) {
		if (g_front_state.net_runtime_recv_queue[queue_index]
				    .direct_play_id != 0 &&
		    *(int *)g_front_state.net_runtime_recv_queue[queue_index]
				    .payload == packet_type) {
			XVT_LOG_DEBUG(
				"network.lobby_poll_found type=%d system=%d",
				packet_type, 0);
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return 1;
		}
		if (++queue_index >= QUEUE_CAPACITY) {
			queue_index = 0;
		}
		--remaining;
	}
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 0;
}

/* As net_poll_for_packet_type_or_backlog, looking instead for a queued DirectPlay
 * system message that announces a new player or group
 * (DPSYS_CREATEPLAYERORGROUP). */
// FUNCTION: XVT 0x4CFFB0
int net_poll_for_player_created_or_backlog(void)
{
	enum {
		QUEUE_CAPACITY =
			sizeof(g_front_state.net_runtime_recv_queue) /
			sizeof(g_front_state.net_runtime_recv_queue[0]),
		BACKLOG_THRESHOLD = QUEUE_CAPACITY / 2,
	};

	if (g_front_state.net_direct_play == NULL) {
		return 0;
	}
	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	net_pump_incoming_packets();
	if (g_front_state.net_runtime_recv_queue_count > BACKLOG_THRESHOLD) {
		XVT_LOG_WARN("network.lobby_backlog queued=%d",
			     g_front_state.net_runtime_recv_queue_count);
		if (back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		return 1;
	}
	int queue_index = g_front_state.net_runtime_recv_queue_read_index;
	int remaining = g_front_state.net_runtime_recv_queue_count;
	while (remaining > 0) {
		if (g_front_state.net_runtime_recv_queue[queue_index]
				    .direct_play_id == 0 &&
		    *(int *)g_front_state.net_runtime_recv_queue[queue_index]
				    .payload == DPSYS_CREATEPLAYERORGROUP) {
			XVT_LOG_DEBUG(
				"network.lobby_poll_found type=%d system=%d",
				(int)DPSYS_CREATEPLAYERORGROUP, 1);
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return 1;
		}
		if (++queue_index >= QUEUE_CAPACITY) {
			queue_index = 0;
		}
		--remaining;
	}
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 0;
}

/* Returns the next game packet from the lobby queue, taken through
 * net_dequeue_incoming_packet, after handing every DirectPlay system message
 * that comes first to net_handle_direct_play_system_message; NULL when none is
 * ready. The packet stays valid until the next dequeue. The back buffer is
 * unlocked meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4D0070
int *net_get_next_app_packet(DPID *out_sender_id, uint32_t *out_packet_size)
{
	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int *packet;
	do {
		packet = net_dequeue_incoming_packet(out_sender_id,
						     out_packet_size);
		if (packet == NULL || *out_sender_id != 0) {
			break;
		}
		net_handle_direct_play_system_message(*packet, packet);
	} while (1);
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return packet;
}

/* Returns g_front_state.net_host_player_id, the host's DirectPlay id; 0 until it
 * is known. */
// FUNCTION: XVT 0x4D11D0
int net_get_host_player_id(void) { return g_front_state.net_host_player_id; }

/* Returns the local player's DirectPlay id, kept in roster entry 0. */
// FUNCTION: XVT 0x4D11E0
int net_get_local_player_id(void)
{
	return g_front_state.net_players[0].player_id;
}

/* Sets the ready flag of the roster player with that id; does nothing when
 * there is none. Unlike net_set_player_ready it leaves the back buffer alone
 * and works without DirectPlay. */
// FUNCTION: XVT 0x4D1240
void net_mark_player_ready_no_lock(int player_id)
{
	int player_index;

	for (player_index = 0; player_index < g_front_state.net_player_count;
	     ++player_index) {
		if (g_front_state.net_players[player_index].player_id ==
		    (DPID)player_id) {
			break;
		}
	}
	if (player_index != g_front_state.net_player_count) {
		g_front_state.net_players[player_index].ready_flag = 1;
		XVT_LOG_DEBUG("network.lobby_roster_ready player=%u ready=1",
			      (unsigned)player_id);
	} else if (g_front_state.net_direct_play != NULL) {
		XVT_LOG_WARN(
			"network.lobby_roster_ready_refused player=%u reason=\"not_in_roster\"",
			(unsigned)player_id);
	}
}

/* Clears the ready flag of the roster entry with that id, searching all 32
 * entries rather than only those in use. */
// FUNCTION: XVT 0x4D1280
void net_clear_player_ready_flag(int player_id)
{
	int player_index;

	for (player_index = 0; player_index < 32; player_index++) {
		if (g_front_state.net_players[player_index].player_id ==
		    (DPID)player_id) {
			break;
		}
	}
	if (player_index != 32) {
		g_front_state.net_players[player_index].ready_flag = 0;
		XVT_LOG_DEBUG("network.lobby_roster_ready player=%u ready=0",
			      (unsigned)player_id);
	}
}

/* Returns the ready flag of the roster player with that id, or 0 when there
 * is none. */
// FUNCTION: XVT 0x4D12B0
int net_is_player_ready(int player_id)
{
	int player_index;

	for (player_index = 0; player_index < g_front_state.net_player_count;
	     ++player_index) {
		if (g_front_state.net_players[player_index].player_id ==
		    (DPID)player_id) {
			break;
		}
	}
	if (player_index == g_front_state.net_player_count) {
		return 0;
	}
	return g_front_state.net_players[player_index].ready_flag;
}

/* Returns the roster entry of the player with that id, or NULL. */
// FUNCTION: XVT 0x4D12F0
struct net_player_info *net_find_player(int player_id)
{
	int player_index;

	for (player_index = 0; player_index < g_front_state.net_player_count;
	     ++player_index) {
		if (g_front_state.net_players[player_index].player_id ==
		    (DPID)player_id) {
			break;
		}
	}
	if (player_index == g_front_state.net_player_count) {
		return 0;
	}
	return &g_front_state.net_players[player_index];
}

/* Sets the ready flag of the roster player with that id and returns 1;
 * returns 0 without DirectPlay or when the id is not in the roster. The back
 * buffer is unlocked meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4D1330
int net_set_player_ready(int player_id)
{
	if (g_front_state.net_direct_play == NULL) {
		XVT_LOG_WARN(
			"network.lobby_roster_ready_refused player=%u reason=\"no_session\"",
			(unsigned)player_id);
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int player_index = 0;
	if (g_front_state.net_player_count > 0) {
		do {
			if (g_front_state.net_players[player_index].player_id ==
			    (DPID)player_id) {
				break;
			}
			++player_index;
		} while (player_index < g_front_state.net_player_count);
	}
	if (player_index == g_front_state.net_player_count) {
		XVT_LOG_WARN(
			"network.lobby_roster_ready_refused player=%u reason=\"not_in_roster\"",
			(unsigned)player_id);
		if (was_back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		return 0;
	}

	g_front_state.net_players[player_index].ready_flag = 1;
	XVT_LOG_DEBUG("network.lobby_roster_ready player=%u ready=1",
		      (unsigned)player_id);
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 1;
}

/* net_clear_player_ready_flag with the back buffer unlocked meanwhile and
 * relocked when it was locked; does nothing without DirectPlay. */
// FUNCTION: XVT 0x4D13B0
void net_clear_player_ready_flag_with_lock_guard(int player_id)
{
	if (g_front_state.net_direct_play != NULL) {
		int was_back_buffer_locked = g_front_state.back_buffer_locked;
		frontend_display_unlock_back_buffer();
		net_clear_player_ready_flag(player_id);
		if (was_back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
	}
}

/* Counts the roster entries whose ready flag is 1, over all 32 entries. */
// FUNCTION: XVT 0x4D1400
int net_count_ready_players(void)
{
	int count = 0;

	for (int player_index = 0; player_index < 32; ++player_index) {
		if (g_front_state.net_players[player_index].ready_flag == 1) {
			++count;
		}
	}
	return count;
}

/* Clears the ready flag of all 32 roster entries. */
// FUNCTION: XVT 0x4D1420
void net_clear_player_ready_flags(void)
{
	for (int player_index = 0; player_index < 32; ++player_index) {
		g_front_state.net_players[player_index].ready_flag = 0;
	}
	XVT_LOG_DEBUG("network.lobby_roster_ready_cleared");
}

/* Copies the lobby's DirectPlay state from g_front_state into the flight
 * session's variables when a flight starts: the interface, application and
 * session GUIDs, group and host ids, local player, receive queue (entries kept
 * at their indices) with its indices and count, peer slots, broadcast and group
 * counters and trailers, and the 128-entry sent history. Returns 1. Only
 * net_session_import_lobby_state calls it. */
// FUNCTION: XVT 0x4D1940
int net_session_import_runtime_state(
	void **dplay_interface_out, GUID *app_guid_out, GUID *session_guid_out,
	int32_t *group_id_out, int *host_player_id_out,
	struct net_player_info *local_player_info_out,
	struct net_queued_packet *recv_queue_out, int32_t *recv_queue_read_out,
	int *recv_queue_count_out, int *recv_queue_write_out,
	struct net_reliable_peer_slot *reliable_peer_slots_out,
	uint32_t *reliable_peer_slot_count_out,
	uint32_t *broadcast_seq_counter_out, char *broadcast_payload_out,
	int *broadcast_payload_length_out, int *broadcast_piggyback_empty_out,
	uint32_t *group_seq_counter_out, char *group_payload_out,
	int *group_payload_length_out, int *group_piggyback_empty_out,
	struct net_queued_packet *sent_history_out,
	int *sent_history_write_index_out)
{
	*dplay_interface_out = g_front_state.net_direct_play;
	*app_guid_out = g_front_state.net_app_guid;
	*session_guid_out = g_front_state.net_joined_session_guid;
	*group_id_out = g_front_state.net_group_dplay_id;
	*host_player_id_out = g_front_state.net_host_player_id;
	*local_player_info_out = g_front_state.net_runtime_local_player;
	*recv_queue_count_out = g_front_state.net_runtime_recv_queue_count;
	*recv_queue_read_out = g_front_state.net_runtime_recv_queue_read_index;
	*recv_queue_write_out =
		g_front_state.net_runtime_recv_queue_write_index;
	int queue_index = g_front_state.net_runtime_recv_queue_read_index;
	int packet_index;
	for (packet_index = 0;
	     packet_index < g_front_state.net_runtime_recv_queue_count;
	     packet_index++) {
		memcpy(&recv_queue_out[queue_index],
		       &g_front_state.net_runtime_recv_queue[queue_index],
		       sizeof(recv_queue_out[queue_index]));
		queue_index++;
		if (queue_index >= 1024) {
			queue_index = 0;
		}
	}

	*reliable_peer_slot_count_out =
		g_front_state.net_reliable_peer_slot_count;
	for (packet_index = 0;
	     packet_index < (int)g_front_state.net_reliable_peer_slot_count;
	     packet_index++) {
		memcpy(&reliable_peer_slots_out[packet_index],
		       &g_front_state
				.net_runtime_reliable_peer_slots[packet_index],
		       sizeof(reliable_peer_slots_out[packet_index]));
	}

	*broadcast_seq_counter_out =
		g_front_state.net_runtime_broadcast_seq_counter;
	memcpy(broadcast_payload_out,
	       g_front_state.net_runtime_broadcast_pending_payload.payload,
	       sizeof(g_front_state.net_runtime_broadcast_pending_payload
			      .payload));
	*broadcast_payload_length_out =
		g_front_state.net_runtime_broadcast_pending_payload
			.payload_length;
	*broadcast_piggyback_empty_out =
		g_front_state.net_runtime_broadcast_pending_payload
			.piggyback_empty;
	*group_seq_counter_out = g_front_state.net_runtime_group_seq_counter;
	memcpy(group_payload_out,
	       g_front_state.net_runtime_group_pending_payload.payload,
	       sizeof(g_front_state.net_runtime_group_pending_payload.payload));
	*group_payload_length_out =
		g_front_state.net_runtime_group_pending_payload.payload_length;
	*group_piggyback_empty_out =
		g_front_state.net_runtime_group_pending_payload.piggyback_empty;

	memset(sent_history_out, 0,
	       sizeof(g_front_state.net_runtime_sent_history));
	for (packet_index = 0; packet_index < 128; packet_index++) {
		memcpy(&sent_history_out[packet_index],
		       &g_front_state.net_runtime_sent_history[packet_index],
		       sizeof(sent_history_out[packet_index]));
	}
	*sent_history_write_index_out =
		g_front_state.net_runtime_sent_history_write_index;
	XVT_LOG_INFO("network.lobby_handoff queued=%d peers=%u",
		     g_front_state.net_runtime_recv_queue_count,
		     (unsigned)g_front_state.net_reliable_peer_slot_count);
	XVT_LOG_DEBUG(
		"network.lobby_handoff_state read=%d write=%d broadcast_seq=%d group_seq=%d history=%d host=%u group=%u",
		g_front_state.net_runtime_recv_queue_read_index,
		g_front_state.net_runtime_recv_queue_write_index,
		g_front_state.net_runtime_broadcast_seq_counter,
		g_front_state.net_runtime_group_seq_counter,
		g_front_state.net_runtime_sent_history_write_index,
		(unsigned)g_front_state.net_host_player_id,
		(unsigned)g_front_state.net_group_dplay_id);
	return 1;
}

/* Copies the flight session's state back into g_front_state when it shuts
 * down (net_session_shutdown): the local player, receive queue with its
 * indices, peer slots (each last_heard_ms set to the current time), broadcast
 * and group counters and trailers, and sent history. It also keeps a pointer
 * to the flight's 256-entry world-message history, and that history's write
 * index, in g_front_state.net_flight_sent_world_message_history and
 * net_flight_sent_world_message_write_index; net_pump_incoming_packets resends from
 * that history on a WORLD_NACK. The interface, GUID, group and host arguments
 * are ignored. Returns 1. */
// FUNCTION: XVT 0x4D1B10
int net_session_export_runtime_state(
	void **dplay_interface, const void *app_guid, const void *session_guid,
	const int *group_id, const int *host_player_id,
	const void *local_player_info,
	const struct net_queued_packet *recv_queue_entries,
	const int *recv_queue_read, const int *recv_queue_count,
	const int *recv_queue_write,
	const struct net_reliable_peer_slot *reliable_peer_slots,
	const int *reliable_peer_slot_count, const int *broadcast_seq_counter,
	const void *broadcast_payload, const int *broadcast_payload_length,
	const int *broadcast_piggyback_empty, const int *group_seq_counter,
	const void *group_payload, const int *group_payload_length,
	const int *group_piggyback_empty,
	const struct net_queued_packet *sent_history,
	const int *sent_history_write_index,
	struct net_queued_packet *sent_world_message_history,
	const int *sent_world_message_write_index)
{
	(void)dplay_interface;
	(void)app_guid;
	(void)session_guid;
	(void)group_id;
	(void)host_player_id;

	memcpy(&g_front_state.net_runtime_local_player, local_player_info,
	       sizeof(g_front_state.net_runtime_local_player));
	g_front_state.net_runtime_recv_queue_count = *recv_queue_count;
	int queue_index = *recv_queue_read;
	g_front_state.net_runtime_recv_queue_read_index = *recv_queue_read;
	g_front_state.net_runtime_recv_queue_write_index = *recv_queue_write;
	int packet_index;
	for (packet_index = 0;
	     packet_index < g_front_state.net_runtime_recv_queue_count;
	     ++packet_index) {
		memcpy(&g_front_state.net_runtime_recv_queue[queue_index],
		       &recv_queue_entries[queue_index],
		       sizeof(g_front_state
				      .net_runtime_recv_queue[queue_index]));
		++queue_index;
		if (queue_index >= 1024) {
			queue_index = 0;
		}
	}

	g_front_state.net_reliable_peer_slot_count = *reliable_peer_slot_count;
	for (int peer_index = 0;
	     peer_index < (int)g_front_state.net_reliable_peer_slot_count;
	     ++peer_index) {
		memcpy(&g_front_state
				.net_runtime_reliable_peer_slots[peer_index],
		       &reliable_peer_slots[peer_index],
		       sizeof(g_front_state.net_runtime_reliable_peer_slots
				      [peer_index]));
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_heard_ms = GetTickCount();
	}

	g_front_state.net_runtime_broadcast_seq_counter =
		*broadcast_seq_counter;
	memcpy(g_front_state.net_runtime_broadcast_pending_payload.payload,
	       broadcast_payload,
	       sizeof(g_front_state.net_runtime_broadcast_pending_payload
			      .payload));
	g_front_state.net_runtime_broadcast_pending_payload.payload_length =
		*broadcast_payload_length;
	g_front_state.net_runtime_broadcast_pending_payload.piggyback_empty =
		*broadcast_piggyback_empty;
	g_front_state.net_runtime_group_seq_counter = *group_seq_counter;
	memcpy(g_front_state.net_runtime_group_pending_payload.payload,
	       group_payload,
	       sizeof(g_front_state.net_runtime_group_pending_payload.payload));
	g_front_state.net_runtime_group_pending_payload.payload_length =
		*group_payload_length;
	g_front_state.net_runtime_group_pending_payload.piggyback_empty =
		*group_piggyback_empty;

	memset(g_front_state.net_runtime_sent_history, 0,
	       sizeof(g_front_state.net_runtime_sent_history));
	for (packet_index = 0; packet_index < 128; ++packet_index) {
		memcpy(&g_front_state.net_runtime_sent_history[packet_index],
		       &sent_history[packet_index],
		       sizeof(g_front_state
				      .net_runtime_sent_history[packet_index]));
	}
	g_front_state.net_runtime_sent_history_write_index =
		*sent_history_write_index;
	g_front_state.net_flight_sent_world_message_history =
		sent_world_message_history;
	g_front_state.net_flight_sent_world_message_write_index =
		*sent_world_message_write_index;
	XVT_LOG_INFO("network.lobby_handback queued=%d peers=%u",
		     g_front_state.net_runtime_recv_queue_count,
		     (unsigned)g_front_state.net_reliable_peer_slot_count);
	XVT_LOG_DEBUG(
		"network.lobby_handback_state read=%d write=%d broadcast_seq=%d group_seq=%d history=%d world_history=%d",
		g_front_state.net_runtime_recv_queue_read_index,
		g_front_state.net_runtime_recv_queue_write_index,
		g_front_state.net_runtime_broadcast_seq_counter,
		g_front_state.net_runtime_group_seq_counter,
		g_front_state.net_runtime_sent_history_write_index,
		g_front_state.net_flight_sent_world_message_write_index);
	return 1;
}

/* Asks DirectPlay to give player playerId the given long and short names.
 * Returns 1 when SetPlayerName succeeds, else 0; it returns XVT_NETWORK_PENDING
 * while the call is pending. Without DirectPlay it returns 0 with the back
 * buffer unlocked and not locked again; otherwise it relocks the back buffer
 * when it was locked. */
// FUNCTION: XVT 0x4D22E0
int net_set_player_name_with_lock_guard(unsigned int player_id,
					const char *long_name,
					const char *short_name)
{
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	if (g_front_state.net_direct_play == NULL) {
		XVT_LOG_WARN(
			"network.lobby_rename_refused player=%u reason=\"no_session\"",
			player_id);
		return 0;
	}

	DPNAME player_name;
	memset(&player_name, 0, sizeof(player_name));
	player_name.lpszShortNameA = (char *)short_name;
	player_name.lpszLongNameA = (char *)long_name;
	player_name.dwSize = sizeof(player_name);
	HRESULT result = g_front_state.net_direct_play->lpVtbl->SetPlayerName(
		g_front_state.net_direct_play, player_id, &player_name, 0);
	XVT_LOG_DEBUG(
		"network.lobby_rename_requested player=%u pilot=\"%s\" long_byte=%u result=%#x",
		player_id, short_name, (unsigned)(unsigned char)long_name[0],
		(unsigned)result);

	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	if (result == DPERR_PENDING) {
		return XVT_NETWORK_PENDING;
	}
	if (result != 0) {
		XVT_LOG_WARN("network.lobby_rename_failed player=%u result=%#x",
			     player_id, (unsigned)result);
	}
	return result == 0;
}

/* net_refresh_player_roster with the back buffer unlocked meanwhile and
 * relocked when it was locked. Returns 0 without DirectPlay, else 1. */
// FUNCTION: XVT 0x4D2370
int net_refresh_player_roster_with_lock_guard(void)
{
	if (g_front_state.net_direct_play == NULL) {
		return 0;
	}
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	g_front_state.net_player_count = 1;
	net_refresh_player_roster();
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 1;
}

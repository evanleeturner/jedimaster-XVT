#include "xvt_runtime/runtime/network_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/util/time.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/network_metadata.h"

enum {
	SESSION_IDLE,
	SESSION_CLOSE,
	SESSION_FACTORY,
	SESSION_PREPARE,
	SESSION_OPEN,
	SESSION_PLAYER,
	SESSION_GROUP,
	SESSION_ROSTER,
	SESSION_REGISTER,
	SESSION_HANDSHAKE,
	SESSION_ADMISSION,
	SESSION_ESTABLISHED,
	SESSION_FAILED
};

static struct {
	int phase;
	int host;
	int online;
	int opened;
	int registered;
	int closing;
	int cancel_join;
	int flight;
	int flight_ready;
	int lost;
	GUID app;
	GUID instance;
	char rating_text[16];
	char player_name[16];
	char name[32];
	uint64_t deadline;
	uint64_t next_retry_us;
	AeronDplayDirectoryError error;
	struct xvt_network_metadata metadata;
	AeronDplayRoomMetadata published;
} g_session;

static char g_origin[AERON_DPLAY_DIRECTORY_URL_CAPACITY];
static const GUID g_application = {
	0x09438c20, 0xe06a, 0x11ce, {0x86, 0x81, 0, 0xaa, 0, 0x6c, 0x5d, 0x57}};

int xvt_network_session_copy_player_names(
	const struct net_player_name_message *message, char *short_name,
	size_t short_capacity, char *long_name, size_t long_capacity)
{
	const char *short_end;
	const char *long_start;
	const char *long_end;
	size_t short_size;
	size_t long_size;
	size_t remaining;
	if (!message || !short_name || !long_name || !short_capacity ||
	    !long_capacity) {
		return 0;
	}
	short_end = memchr(message->names, 0, sizeof(message->names));
	if (!short_end) {
		return 0;
	}
	long_start = short_end + 1;
	remaining =
		sizeof(message->names) - (size_t)(long_start - message->names);
	long_end = memchr(long_start, 0, remaining);
	if (!long_end) {
		return 0;
	}
	short_size = (size_t)(short_end - message->names);
	long_size = (size_t)(long_end - long_start);
	if (short_size >= short_capacity) {
		short_size = short_capacity - 1;
	}
	if (long_size >= long_capacity) {
		long_size = long_capacity - 1;
	}
	memcpy(short_name, message->names, short_size);
	short_name[short_size] = 0;
	memcpy(long_name, long_start, long_size);
	long_name[long_size] = 0;
	return 1;
}

AeronDplayDirectoryError xvt_network_session_configure(void)
{
	const struct xvt_settings *settings = xvt_config_settings();
	const char *origin = settings ? settings->lobby_url : "";
	if (!*origin) {
		return AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED;
	}
	if (!strcmp(origin, g_origin)) {
		return AERON_DPLAY_DIRECTORY_ERROR_NONE;
	}
	if (strlen(origin) >= sizeof(g_origin)) {
		return AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST;
	}
	AeronDplayDirectoryConfig config = {0};
	strcpy(config.lobby_url, origin);
	config.application_id = g_application;
	snprintf(config.game_version, sizeof(config.game_version), "%d",
		 FRONTEND_NET_PROTOCOL_VERSION);
	AeronDplayDirectoryError error = AeronDplayDirectory_Configure(&config);
	if (!error) {
		strcpy(g_origin, origin);
	}
	return error;
}

static const char *xvt_network_session_role(void)
{
	return g_session.host ? "host" : "join";
}

/* Marks the session established and says so; the three ways in are the online host's registration,
 * the offline host's roster, and the client's admission. */
static void xvt_network_session_establish(void)
{
	g_session.phase = SESSION_ESTABLISHED;
	XVT_LOG_INFO("network.session_ready role=\"%s\" online=%d",
		     xvt_network_session_role(), g_session.online);
}

void xvt_network_session_on_close(void)
{
	/* Every shutdown calls this, with or without a session; only a session under way is reported. */
	if (g_session.phase != SESSION_IDLE &&
	    g_session.phase != SESSION_FAILED) {
		XVT_LOG_INFO("network.session_closed phase=%d",
			     g_session.phase);
	}
	xvt_flight_network_clear_cookies();
	if (g_session.registered) {
		AeronDplayDirectory_StopHosting();
	}
	if (g_session.online && !g_session.host) {
		if (g_session.opened) {
			g_session.cancel_join = 1;
		} else {
			AeronDplayDirectory_CancelJoin();
		}
	}
	g_session.registered = 0;
	g_session.flight = g_session.flight_ready = g_session.lost = 0;
	g_session.closing = 1;
	g_session.phase = SESSION_IDLE;
}

void xvt_network_session_leave(void) { net_shutdown_direct_play_session(); }

void xvt_network_session_cancel(void) { xvt_network_session_leave(); }

static int xvt_network_session_fail(AeronDplayDirectoryError error)
{
	XVT_LOG_WARN("network.session_failed phase=%d error=%u",
		     g_session.phase, (unsigned)error);
	xvt_network_session_leave();
	g_session.error = error;
	g_session.phase = SESSION_FAILED;
	return 0;
}

static int xvt_network_session_start(const char *rating_text,
				     const char *player_name, const char *name,
				     int host, int online, const GUID *room)
{
	if (g_session.phase != SESSION_IDLE &&
	    g_session.phase != SESSION_FAILED &&
	    g_session.phase != SESSION_ESTABLISHED) {
		return XVT_NETWORK_PENDING;
	}
	xvt_network_session_leave();
	/* Preserve completion of a preceding close while preparing the next session. */
	int closing = g_session.closing;
	int cancel_join = g_session.cancel_join;
	memset(&g_session, 0, sizeof(g_session));
	g_session.closing = closing;
	g_session.cancel_join = cancel_join;
	g_session.app = g_application;
	g_session.host = host;
	g_session.online = online;
	if (!rating_text || !player_name || !name || (!host && !room)) {
		return xvt_network_session_fail(
			AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	}
	if (strlen(rating_text) >= sizeof(g_session.rating_text) ||
	    strlen(player_name) >= sizeof(g_session.player_name) ||
	    strlen(name) >= sizeof(g_session.name)) {
		return xvt_network_session_fail(
			AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	}
	strcpy(g_session.rating_text, rating_text);
	strcpy(g_session.player_name, player_name);
	if (*name) {
		strcpy(g_session.name, name);
	} else {
		snprintf(g_session.name, sizeof(g_session.name), "%s's Game.",
			 player_name);
	}
	if (room) {
		g_session.instance = *room;
	}
	g_mission_setup_is_host = host;
	if (online) {
		g_frontend_mission_session_mode =
			host ? FRONTEND_MISSION_SESSION_NET_HOST
			     : FRONTEND_MISSION_SESSION_NET_CLIENT;
	}
	g_session.phase = SESSION_CLOSE;
	XVT_LOG_INFO("network.session_begin role=\"%s\" online=%d",
		     xvt_network_session_role(), online);
	XVT_LOG_DEBUG(
		"network.session_names pilot=\"%s\" game=\"%s\" rating=\"%s\"",
		g_session.player_name, g_session.name, g_session.rating_text);
	return XVT_NETWORK_PENDING;
}

int xvt_network_session_begin_host(const char *rating_text,
				   const char *player_name, const char *name,
				   int online)
{
	return xvt_network_session_start(rating_text, player_name, name, 1,
					 online, NULL);
}

int xvt_network_session_begin_join(const char *rating_text,
				   const char *player_name, const GUID *room)
{
	return xvt_network_session_start(rating_text, player_name, "", 0, 1,
					 room);
}

/* Clears the reliable-transport counters, peer slots, send history and connection stats, the player lists
 * and the roster; copies in this session's application GUID, host flag and names; then creates the
 * DirectPlay interface. Returns 0 without a TCP/IP provider or when the interface cannot be made. */
static int xvt_network_session_factory(void)
{
	const GUID *provider =
		net_get_direct_play_service_provider_guid(NET_TRANSPORT_TCPIP);
	IDirectPlay *temporary = NULL;
	HRESULT result;
	if (!provider) {
		return 0;
	}
	g_front_state.net_runtime_sent_history_write_index = 0;
	g_front_state.net_runtime_broadcast_seq_counter =
		g_front_state.net_runtime_group_seq_counter = 0;
	g_front_state.net_runtime_broadcast_pending_payload.piggyback_empty = 1;
	g_front_state.net_runtime_broadcast_pending_payload.payload[0] =
		NET_PACKET_NOP;
	g_front_state.net_runtime_broadcast_pending_payload.payload_length = 1;
	g_front_state.net_runtime_group_pending_payload.piggyback_empty = 1;
	g_front_state.net_runtime_group_pending_payload.payload[0] =
		NET_PACKET_NOP;
	g_front_state.net_runtime_group_pending_payload.payload_length = 1;
	g_front_state.net_reliable_peer_slot_count = 0;
	g_front_state.net_reliable_retry_long_timeout_mode = 0;
	g_front_state.net_group_dplay_id = 0;
	g_front_state.net_host_player_id = 0;
	g_front_state.net_flight_sent_world_message_history = NULL;
	g_front_state.net_flight_sent_world_message_write_index = 0;
	for (int i = 0; i < 40; ++i) {
		struct net_reliable_peer_slot *peer =
			&g_front_state.net_runtime_reliable_peer_slots[i];
		peer->last_delivered_seq_default =
			peer->last_delivered_seq_channel_a =
				peer->last_delivered_seq_channel_b = 127;
		peer->recv_seq_default = peer->recv_seq_channel_a =
			peer->recv_seq_channel_b = 127;
		peer->send_seq = 0;
		peer->direct_play_id = 0;
		peer->last_piggyback_type = NET_PACKET_NOP;
		peer->piggyback_length = 1;
		peer->last_activity_ms = peer->last_heard_ms = 0;
		peer->packet_count = peer->packet_drop_count =
			peer->packet_retry_count = 0;
	}
	memset(g_front_state.net_runtime_sent_history, 0,
	       sizeof(g_front_state.net_runtime_sent_history));
	memset(g_net_player_connection_stats, 0,
	       sizeof(g_net_player_connection_stats));
	/* A new session owns fresh admission state; browsing never resets this roster. */
	memset(g_front_state.net_players, 0, sizeof(g_front_state.net_players));
	memset(&g_front_state.net_runtime_local_player, 0,
	       sizeof(g_front_state.net_runtime_local_player));
	memset(g_mp_roster, 0, sizeof(g_mp_roster));
	g_mission_setup_roster_authoritative = 0;
	g_front_state.net_app_guid = g_session.app;
	g_front_state.net_is_host = g_session.host;
	strcpy(g_front_state.net_players[0].long_name, g_session.rating_text);
	strcpy(g_front_state.net_players[0].player_name, g_session.player_name);
	strcpy(g_front_state.net_session_name, g_session.name);
	result = DirectPlayCreate(provider, &temporary, NULL);
	if (result) {
		return 0;
	}
	result = temporary->lpVtbl->QueryInterface(
		temporary, &IID_IDirectPlay2A,
		(void **)&g_front_state.net_direct_play);
	temporary->lpVtbl->Release(temporary);
	return result == 0;
}

static int xvt_network_session_handshake(void)
{
	DPID sender;
	uint32_t size;
	for (int count = 0; count < 32; ++count) {
		int *packet = net_get_next_app_packet(&sender, &size);
		if (!packet) {
			break;
		}
		if (size < 16 || packet[0] != NET_PACKET_SEQUENCE_STATUS) {
			continue;
		}
		unsigned peers = (unsigned)packet[2];
		struct net_reliable_peer_slot saved = {0};
		if (peers > 40 || size < 16 + 8 * peers) {
			continue;
		}
		g_front_state.net_host_player_id = sender;
		for (unsigned i = 0;
		     i < g_front_state.net_reliable_peer_slot_count; ++i) {
			if (g_front_state.net_runtime_reliable_peer_slots[i]
				    .direct_play_id == sender) {
				saved = g_front_state
						.net_runtime_reliable_peer_slots
							[i];
			}
		}
		g_front_state.net_reliable_peer_slot_count = peers;
		for (unsigned i = 0; i < peers; ++i) {
			struct net_reliable_peer_slot *peer =
				&g_front_state
					 .net_runtime_reliable_peer_slots[i];
			const uint8_t *row =
				(const uint8_t *)packet + 16 + 8 * i;
			memcpy(&peer->direct_play_id, row, 4);
			peer->last_delivered_seq_channel_a = row[4];
			peer->last_delivered_seq_channel_b = row[5];
			peer->recv_seq_channel_a = row[6];
			peer->recv_seq_channel_b = row[7];
			peer->last_delivered_seq_default =
				peer->recv_seq_default = 127;
			peer->send_seq = 0;
			peer->last_piggyback_type = NET_PACKET_NOP;
			peer->piggyback_length = 1;
			peer->last_activity_ms = peer->last_heard_ms =
				GetTickCount();
			if (saved.direct_play_id &&
			    peer->direct_play_id == saved.direct_play_id) {
				peer->last_delivered_seq_default =
					saved.last_delivered_seq_default;
				peer->recv_seq_default = saved.recv_seq_default;
				peer->send_seq = saved.send_seq;
				memcpy(&peer->last_piggyback_type,
				       &saved.last_piggyback_type,
				       saved.piggyback_length);
				peer->piggyback_length = saved.piggyback_length;
				peer->last_activity_ms = saved.last_activity_ms;
				peer->last_heard_ms = saved.last_heard_ms;
			}
		}
		int response[5] = {NET_PACKET_KEEPALIVE_ACK, packet[3], 0, 0,
				   0};
		net_send_direct_play_packet(sender, response, sizeof(response),
					    0);
		int request[6] = {NET_PACKET_JOIN_REQUEST,
				  FRONTEND_NET_PROTOCOL_VERSION,
				  0,
				  0,
				  0,
				  0};
		memcpy(request + 2, g_game_config.password,
		       sizeof(g_game_config.password));
		net_send_packet_and_flush(sender, request, sizeof(request));
		XVT_LOG_DEBUG("network.handshake peers=%u", peers);
		g_session.phase = SESSION_ADMISSION;
		return 1;
	}
	return XVT_NETWORK_PENDING;
}

static int xvt_network_session_register(void)
{
	if (!g_session.registered) {
		xvt_network_metadata_build(&g_session.metadata, 0);
		AeronDplayDirectoryError error =
			AeronDplayDirectory_StartHosting(
				&g_session.instance, &g_session.metadata.room);
		if (error) {
			return xvt_network_session_fail(error);
		}
		g_session.registered = 1;
		g_session.published = g_session.metadata.room;
	}
	AeronDplayDirectoryStatus status;
	AeronDplayDirectory_GetHostStatus(&status);
	if (status.state == AERON_DPLAY_DIRECTORY_FAILED) {
		return xvt_network_session_fail(status.error);
	}
	if (status.state != AERON_DPLAY_DIRECTORY_SUCCEEDED) {
		return XVT_NETWORK_PENDING;
	}
	xvt_network_session_establish();
	return 1;
}

/* Starts the roster with the local player: the host lists itself and moves
 * on to registering (online) or straight to an established session; a
 * joining player moves on to the handshake. */
static void xvt_network_session_roster(void)
{
	g_front_state.net_player_count = 1;
	net_refresh_player_roster();
	g_front_state.net_runtime_recv_queue_read_index =
		g_front_state.net_runtime_recv_queue_write_index = 0;
	g_front_state.net_runtime_recv_queue_count = 0;
	if (g_session.host) {
		g_front_state.net_host_player_id =
			g_front_state.net_runtime_local_player.player_id;
		net_set_player_ready(net_get_local_player_id());
		snprintf(g_mp_roster[0].name, sizeof(g_mp_roster[0].name), "%s",
			 g_session.player_name);
		g_mp_roster[0].player_id = net_get_local_player_id();
		g_mp_roster[0].pilot_rating = g_pilot_data.rating;
		if (g_session.online) {
			g_session.phase = SESSION_REGISTER;
		} else {
			xvt_network_session_establish();
		}
	} else {
		g_session.phase = SESSION_HANDSHAKE;
	}
}

/* One update of the session state machine: each phase does its step (the
 * longer steps are the functions above) and names the next phase, so the
 * whole host and join sequence reads in one switch. */
int xvt_network_session_update(void)
{
	HRESULT result;
	if (g_session.lost && !g_session.flight) {
		return xvt_network_session_fail(
			AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
	}
	switch (g_session.phase) {
	case SESSION_CLOSE: {
		if (g_session.closing || AeronDplay_IsActive()) {
			return XVT_NETWORK_PENDING;
		}
		AeronDplayDirectoryError error =
			g_session.online ? xvt_network_session_configure() : 0;
		if (error == AERON_DPLAY_DIRECTORY_ERROR_BUSY) {
			return XVT_NETWORK_PENDING;
		}
		if (error) {
			return xvt_network_session_fail(error);
		}
		g_session.phase = SESSION_FACTORY;
		break;
	}
	case SESSION_FACTORY:
		if (!xvt_network_session_factory()) {
			return xvt_network_session_fail(
				AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
		}
		if (!g_session.host) {
			AeronDplayDirectoryError error =
				AeronDplayDirectory_BeginJoin(
					&g_session.instance);
			if (error) {
				return xvt_network_session_fail(error);
			}
			AeronDplayJoinStatus join;
			AeronDplayDirectory_GetJoinStatus(&join);
			g_session.deadline = join.deadline_us;
		}
		g_session.phase =
			g_session.host ? SESSION_OPEN : SESSION_PREPARE;
		break;
	case SESSION_PREPARE: {
		AeronDplayJoinStatus join;
		AeronDplayDirectory_GetJoinStatus(&join);
		if (join.preparation.state == AERON_DPLAY_DIRECTORY_FAILED ||
		    join.preparation.state == AERON_DPLAY_DIRECTORY_CANCELLED) {
			return xvt_network_session_fail(join.preparation.error);
		}
		if (join.preparation.state == AERON_DPLAY_DIRECTORY_SUCCEEDED) {
			g_session.phase = SESSION_OPEN;
		}
		break;
	}
	case SESSION_OPEN: {
		DPSESSIONDESC2 desc = {0};
		desc.dwSize = sizeof(desc);
		desc.guidApplication = g_session.app;
		desc.guidInstance = g_session.instance;
		desc.dwMaxPlayers = 16;
		desc.lpszSessionNameA = g_session.name;
		g_session.opened = 1;
		result = g_front_state.net_direct_play->lpVtbl->Open(
			g_front_state.net_direct_play, &desc,
			g_session.host ? DPOPEN_CREATE : DPOPEN_JOIN);
		if (result == DPERR_PENDING || result == DPERR_BUSY) {
			break;
		}
		if (result) {
			return xvt_network_session_fail(
				AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
		}
		g_session.instance = g_front_state.net_joined_session_guid =
			desc.guidInstance;
		g_net_active_transport_type = NET_TRANSPORT_TCPIP;
		g_session.phase = SESSION_PLAYER;
		break;
	}
	case SESSION_PLAYER: {
		int player = net_create_direct_play_player(
			g_session.rating_text, g_session.player_name);
		if (player == XVT_NETWORK_PENDING) {
			break;
		}
		if (!player) {
			return xvt_network_session_fail(
				AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
		}
		g_front_state.net_players[0].player_id = player;
		g_front_state.net_runtime_local_player =
			g_front_state.net_players[0];
		g_session.phase =
			g_session.host ? SESSION_GROUP : SESSION_ROSTER;
		break;
	}
	case SESSION_GROUP:
		result = g_front_state.net_direct_play->lpVtbl->CreateGroup(
			g_front_state.net_direct_play,
			&g_front_state.net_group_dplay_id, NULL, NULL, 0, 0);
		if (result == DPERR_PENDING || result == DPERR_BUSY) {
			break;
		}
		if (result) {
			return xvt_network_session_fail(
				AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
		}
		g_front_state.net_runtime_reliable_peer_slots[0]
			.direct_play_id = g_front_state.net_group_dplay_id;
		g_front_state.net_reliable_peer_slot_count = 1;
		g_session.phase = SESSION_ROSTER;
		break;
	case SESSION_ROSTER:
		xvt_network_session_roster();
		break;
	case SESSION_REGISTER:
		net_pump_incoming_packets();
		return xvt_network_session_register();
	case SESSION_HANDSHAKE:
		return xvt_network_session_handshake();
	case SESSION_ADMISSION:
	case SESSION_ESTABLISHED:
		return 1;
	case SESSION_FAILED:
	case SESSION_IDLE:
		return 0;
	}
	return XVT_NETWORK_PENDING;
}

int xvt_network_session_accept_admission(DPID sender, DPID player)
{
	if (g_session.phase != SESSION_ADMISSION ||
	    sender != g_front_state.net_host_player_id ||
	    player != g_front_state.net_runtime_local_player.player_id ||
	    g_session.lost || Aeron_NowUs() >= g_session.deadline) {
		return 0;
	}
	AeronDplayDirectory_FinishJoin();
	g_session.deadline = 0;
	xvt_network_session_establish();
	return 1;
}

void xvt_network_session_reject(void)
{
	XVT_LOG_WARN("network.join_rejected");
	xvt_network_session_leave();
	AeronDplayDirectory_FinishJoin();
	g_session.cancel_join = 0;
}

void xvt_network_session_host_lost(void)
{
	if (!g_session.lost) {
		XVT_LOG_WARN("network.host_lost flight=%d", g_session.flight);
	}
	g_session.lost = 1;
}

int xvt_network_session_is_lost(void) { return g_session.lost; }

struct xvt_network_session_status xvt_network_session_get_status(void)
{
	struct xvt_network_session_status status = {XVT_NETWORK_SESSION_IDLE,
						    g_session.error};
	if (g_session.phase == SESSION_FAILED) {
		status.state = XVT_NETWORK_SESSION_FAILED;
	} else if (g_session.phase == SESSION_ESTABLISHED) {
		status.state = XVT_NETWORK_SESSION_ESTABLISHED;
	} else if (g_session.phase == SESSION_ADMISSION) {
		status.state = XVT_NETWORK_SESSION_ADMISSION;
	} else if (g_session.phase != SESSION_IDLE || g_session.closing) {
		status.state = XVT_NETWORK_SESSION_PENDING;
	}
	return status;
}

void xvt_network_session_service(void)
{
	if (g_session.closing && !AeronDplay_IsActive()) {
		if (g_session.cancel_join) {
			AeronDplayDirectory_CancelJoin();
		}
		g_session.cancel_join = g_session.closing = 0;
	}
	if (g_session.phase == SESSION_FAILED ||
	    g_session.phase == SESSION_IDLE || g_session.closing) {
		return;
	}
	if (g_session.lost && !g_session.flight) {
		xvt_network_session_fail(
			AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
		return;
	}
	if (g_session.deadline) {
		AeronDplayJoinStatus join;
		AeronDplayDirectory_GetJoinStatus(&join);
		if (Aeron_NowUs() >= g_session.deadline ||
		    join.preparation.state == AERON_DPLAY_DIRECTORY_FAILED) {
			xvt_network_session_fail(
				join.preparation.error
					? join.preparation.error
					: AERON_DPLAY_DIRECTORY_ERROR_TIMEOUT);
			return;
		}
	}
	if (!g_session.host || !g_session.registered ||
	    g_session.phase != SESSION_ESTABLISHED || g_session.lost) {
		return;
	}
	struct xvt_network_metadata current;
	if (g_session.flight) {
		current = g_session.metadata;
		if (g_session.flight_ready) {
			xvt_network_metadata_keep_active_players(&current);
		}
	} else {
		int accepting =
			g_front_state.screen_states[g_front_state
							    .screen_stack_top]
					.update_fn == mission_setup_update &&
			g_front_state.frame_counter > 0;
		xvt_network_metadata_build(&current, accepting);
	}
	if (!current.room.players) {
		return;
	}
	if (memcmp(&current.room, &g_session.published, sizeof(current.room)) &&
	    !AeronDplayDirectory_UpdateHost(&current.room)) {
		g_session.published = current.room;
	}
	g_session.metadata = current;
	AeronDplayDirectoryStatus status;
	AeronDplayDirectory_GetHostStatus(&status);
	uint64_t now = Aeron_NowUs();
	if (status.state == AERON_DPLAY_DIRECTORY_FAILED &&
	    now >= g_session.next_retry_us) {
		XVT_LOG_WARN("network.directory_retry");
		AeronDplayDirectory_StartHosting(&g_session.instance,
						 &g_session.published);
		g_session.next_retry_us = now + 15000000;
	}
}

void xvt_network_session_begin_flight(void)
{
	if (g_session.phase != SESSION_ESTABLISHED) {
		return;
	}
	if (g_session.host && g_session.registered) {
		struct xvt_network_metadata current;
		xvt_network_metadata_build(&current, 0);
		if (current.room.players) {
			g_session.metadata = current;
		}
		g_session.metadata.room.state = AERON_DPLAY_ROOM_FLIGHT;
		g_session.metadata.room.joinable = 0;
		if (!AeronDplayDirectory_UpdateHost(&g_session.metadata.room)) {
			g_session.published = g_session.metadata.room;
		}
	}
	g_session.flight = 1;
	g_session.flight_ready = 0;
}

void xvt_network_session_mark_flight_ready(void) { g_session.flight_ready = 1; }

void xvt_network_session_end_flight(void)
{
	if (!g_session.flight) {
		return;
	}
	g_session.flight = g_session.flight_ready = 0;
	net_refresh_player_roster();
}

void xvt_network_session_shutdown(void)
{
	memset(&g_session, 0, sizeof(g_session));
	g_origin[0] = 0;
}

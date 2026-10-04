#include "xvt/net/net.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/network_session.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt/util/win32.h"
#include "xvt_runtime/log/log_both_builds.h"

#ifndef XVT_MODERN
__declspec(dllimport) int __stdcall
RegOpenKeyExA(uintptr_t key, const char *sub_key, unsigned int options,
	      unsigned int access, void **result);
__declspec(dllimport) int __stdcall
RegQueryValueExA(void *key, const char *value_name, unsigned int *reserved,
		 unsigned int *type, void *data, unsigned int *data_size);
__declspec(dllimport) int __stdcall
RegSetValueExA(void *key, const char *value_name, unsigned int reserved,
	       unsigned int type, const void *data, unsigned int data_size);
__declspec(dllimport) int __stdcall RegCloseKey(void *key);
__declspec(dllimport) void *__stdcall GlobalAlloc(unsigned int flags,
						  size_t size);
__declspec(dllimport) void *__stdcall GlobalLock(void *memory_handle);
__declspec(dllimport) void *__stdcall GlobalHandle(const void *memory);
__declspec(dllimport) int __stdcall GlobalUnlock(void *memory_handle);
__declspec(dllimport) void *__stdcall GlobalFree(void *memory_handle);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int exit_code);
__declspec(dllimport) void *__stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall SetWindowTextA(void *window,
						   const char *text);
__declspec(dllimport) int __stdcall TerminateProcess(void *process,
						     unsigned int exit_code);
HRESULT __stdcall DirectPlayCreate(const GUID *service_provider_guid,
				   IDirectPlay **out_direct_play,
				   void *outer_unknown);
HRESULT __stdcall DirectPlayLobbyCreateA(const GUID *lobby_provider_guid,
					 IDirectPlayLobbyA **out_lobby,
					 void *outer_unknown, void *data,
					 uint32_t data_size);

enum {
	NET_REGISTRY_ALL_ACCESS = 0xF003F,
	NET_REGISTRY_BINARY = 3,
};

/* The EnableAutodial value, up to 5 bytes, that
 * net_disable_auto_dial_registry_setting reads from the Internet Settings
 * registry key and net_restore_auto_dial_registry_setting writes back. */
// GLOBAL: XVT 0x665018
static uint8_t g_net_saved_enable_auto_dial_value[5] = {0};
/* 1 while net_disable_auto_dial_registry_setting has turned autodial off and
 * net_restore_auto_dial_registry_setting has not yet put the saved value back. */
// GLOBAL: XVT 0x665418
static int g_net_auto_dial_registry_changed = 0;
#endif

/* Transport of the open lobby session. Written by net_start_network_session on
 * success, by xvt_network_session_update (TCP/IP) in the modern build, and
 * reset to IPX by net_shutdown_direct_play_session_ex when it closes DirectPlay.
 * Only the original build reads it, to wait for shutdown acks on TCP/IP. */
// GLOBAL: XVT 0x665020
network_transport_type g_net_active_transport_type = NET_TRANSPORT_IPX;

/* A game packet as the lobby sends it through DirectPlay. */
struct net_direct_play_encoded_packet {
	/* Bits 0-6 the packet type, bits 8-14 the sequence. Bits 15 and 7 are
	 * set for the group; bit 15 alone for a sequenced packet to one
	 * player. */
	int16_t packet_type_header;
	/* Body length, present only outside types 60-63 for a type with no
	 * fixed size; otherwise the body starts here. */
	int16_t payload_size;
	/* The body, then, outside types 60-63, a trailer: a NOP byte, or the
	 * type byte and body of the previous packet on the channel. */
	uint8_t payload[1020];
};

#pragma pack(push, 1)

/* A resent packet as the lobby sends it through DirectPlay. */
struct net_direct_play_sequenced_packet {
	/* Type and sequence as in net_direct_play_encoded_packet, with bit 7 set
	 * and bit 15 clear, which marks a resend. */
	int16_t packet_type_header;
	uint8_t packet_class; /* Channel: 0 broadcast, 1 one player, 2 group. */
	/* Body length, present only outside types 60-63 for a type with no
	 * fixed size; otherwise the body starts here. */
	int16_t payload_size;
	/* The body, then, outside types 60-63, a NOP byte. */
	uint8_t payload[1019];
};

#pragma pack(pop)
typedef char xvt_size_net_direct_play_sequenced_packet
	[(sizeof(struct net_direct_play_sequenced_packet) == 1024) ? 1 : -1];

#ifndef XVT_MODERN
/* Fixed instance GUID that net_open_direct_play_session puts in the session
 * description it hands DirectPlay's lobby for a modem or TCP/IP game. */
// GLOBAL: XVT 0x518290
const GUID g_net_lobby_session_instance_guid = {
	0x09438C20,
	0xE01F,
	0x11CF,
	{0x86, 0x81, 0x11, 0xAA, 0x15, 0x3D, 0x4E, 0x58},
};
#endif
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
 * only transport the modern build uses. */
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
/* Interface id that net_start_network_session, net_enumerate_app_sessions and
 * xvt_network_session_factory pass to QueryInterface to get the IDirectPlay2A
 * interface kept in g_front_state.net_direct_play. */
// GLOBAL: XVT 0x518EF0
const GUID IID_IDirectPlay2A = {
	0x9D460580,
	0xA822,
	0x11CF,
	{0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82},
};
#ifndef XVT_MODERN
/* Address type net_open_direct_play_session gives a serial game's COM port
 * settings. */
// GLOBAL: XVT 0x518F00
const GUID g_net_direct_play_com_port_address_type_guid = {
	0xF2F0CE00,
	0xE0AF,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
/* Address type net_open_direct_play_session gives a modem game's address. */
// GLOBAL: XVT 0x518F10
const GUID g_net_direct_play_phone_address_type_guid = {
	0x78EC89A0,
	0xE0AF,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
/* Address type net_open_direct_play_session gives a TCP/IP game's address. */
// GLOBAL: XVT 0x518F20
const GUID g_net_direct_play_inet_address_type_guid = {
	0xC4A54DA0,
	0xE0AF,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
#endif
/* Copy of the provider GUID that net_get_direct_play_service_provider_guid last
 * looked up; that function returns this copy's address. */
// GLOBAL: XVT 0x665040
static GUID g_net_direct_play_service_provider_guid_scratch = {0};
/* Link figures for up to 40 players, keyed by DirectPlay id. On the host,
 * net_pump_incoming_packets fills them from each player's keepalive acks; on
 * the other players, the Net_SetPlayer* setters store what the host's lobby
 * packets report. Eight writers in all. Cleared by net_start_network_session
 * and xvt_network_session_factory; the host clears a player's entry when the
 * player leaves (net_handle_direct_play_system_message). */
// GLOBAL: XVT 0x665050
struct net_player_connection_stats g_net_player_connection_stats[40];
/* Instance GUID of the session net_enum_sessions_match_name_callback last found
 * by name; net_find_session_by_name returns its address. Never cleared. */
// GLOBAL: XVT 0x665028
GUID g_net_matched_session_instance_guid = {0};
/* Sessions net_enumerate_app_sessions_callback has stored so far; set to 0 by
 * net_enumerate_app_sessions before each enumeration. */
// GLOBAL: XVT 0x665410
int g_net_enum_session_count = 0;
/* Room in the caller's session array for the enumeration under way; set by
 * net_enumerate_app_sessions. */
// GLOBAL: XVT 0x665414
int g_net_enum_session_capacity = 0;

#ifndef XVT_MODERN
/* Opens a lobby session. It first resets the lobby's reliable layer in
 * g_front_state (sequence counters, trailers, the 40 peer slots, sent history,
 * host and group ids), clears g_net_player_connection_stats and flips DirectDraw
 * to the GDI surface, then returns 0 at once when a DirectPlay interface
 * already exists. IPX and serial create DirectPlay for the transport's
 * provider, then host (is_host 1) or join, by instance GUID or else by name,
 * a session named session_name, "<name>'s Game." when that is empty, or
 * "Direct serial game." for serial. Modem and TCP/IP go through
 * net_open_direct_play_session with the session name "Dial a New Number."
 * whatever session_name holds. It then creates the local player; a host also
 * creates the group, which takes the first peer slot. It refreshes the
 * roster and empties the receive queue. With wait_for_player_count above 0 it
 * pumps packets until that many players are in, with no time limit, and sets
 * no host id. Otherwise a host takes its own id, and a joiner waits for the
 * host's NET_PACKET_SEQUENCE_STATUS (5 seconds per wait, any other packet
 * starting a new wait), takes the host's id and the peer slot table from it,
 * keeping its own one-player state with the host, and answers with a
 * NET_PACKET_KEEPALIVE_ACK that echoes the host's time stamp. The packet's
 * peer count is not checked against the 40 slots. Returns 1 and sets
 * g_net_active_transport_type on success, or 0 on any failure, after releasing
 * what it opened. Autodial is off for the call and the back buffer unlocked;
 * both are restored on every return. Only the original build calls this. */
// FUNCTION: XVT 0x4CCF90
int net_start_network_session(int app_guid_data1, int app_guid_data2,
			      int app_guid_data3, int app_guid_data4,
			      const char *local_player_info,
			      const char *local_player_name, int is_host,
			      const char *session_name,
			      network_transport_type network_type,
			      int wait_for_player_count, int unused_a11,
			      const char *connection_address,
			      const GUID *join_session_instance_guid)
{
	enum {
		NET_RELIABLE_PEER_CAPACITY =
			sizeof(g_front_state.net_runtime_reliable_peer_slots) /
			sizeof(g_front_state
				       .net_runtime_reliable_peer_slots[0]),
		NET_RELIABLE_SEQUENCE_INITIAL = 127,
		NET_JOIN_TIMEOUT_SECONDS = 5,
		NET_PLAYER_NAME_TERMINATOR_INDEX = 15,
		NET_SESSION_NAME_TERMINATOR_INDEX = 31,
		NET_JOIN_SEQUENCE_RECORD_SIZE = 8,
		NET_JOIN_SEQUENCE_RECORD_OFFSET = 16,
		NET_JOIN_RESPONSE_SIZE = 20,
	};

	struct {
		int words[10];
		uint32_t received_size;
	} packet_workspace;

	(void)unused_a11;

	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	net_disable_auto_dial_registry_setting();
	g_front_state.net_runtime_sent_history_write_index = 0;
	g_front_state.net_runtime_broadcast_seq_counter = 0;
	g_front_state.net_runtime_broadcast_pending_payload.piggyback_empty = 1;
	g_front_state.net_runtime_broadcast_pending_payload.payload[0] =
		NET_PACKET_NOP;
	g_front_state.net_runtime_broadcast_pending_payload.payload_length = 1;
	g_front_state.net_runtime_group_seq_counter = 0;
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
	memset(g_front_state.net_runtime_sent_history, 0,
	       sizeof(g_front_state.net_runtime_sent_history));
	memset(g_net_player_connection_stats, 0,
	       sizeof(g_net_player_connection_stats));
	g_front_state.direct_draw->lpVtbl->FlipToGDISurface(
		g_front_state.direct_draw);

	if (g_front_state.net_direct_play != NULL) {
		if (back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		net_restore_auto_dial_registry_setting();
		return 0;
	}
	network_transport_type selected_network_type = network_type;
	switch (selected_network_type) {
	case NET_TRANSPORT_IPX:
		memcpy(&packet_workspace.words[6],
		       &g_net_direct_play_ipx_service_provider_guid,
		       sizeof(GUID));
		break;
	case NET_TRANSPORT_TCPIP:
		memcpy(&packet_workspace.words[6],
		       &g_net_direct_play_tcp_ip_service_provider_guid,
		       sizeof(GUID));
		break;
	case NET_TRANSPORT_MODEM:
		memcpy(&packet_workspace.words[6],
		       &g_net_direct_play_modem_service_provider_guid,
		       sizeof(GUID));
		break;
	case NET_TRANSPORT_SERIAL:
		memcpy(&packet_workspace.words[6],
		       &g_net_direct_play_serial_service_provider_guid,
		       sizeof(GUID));
		break;
	}

	HRESULT result;
	char game_session_name[32];
	char dial_number[32] = "Dial a New Number.";
	char error_message[256];
	const GUID *service_provider_guid;
	if (selected_network_type != NET_TRANSPORT_MODEM &&
	    selected_network_type != NET_TRANSPORT_TCPIP) {
		service_provider_guid =
			net_get_direct_play_service_provider_guid(
				selected_network_type);
		if (service_provider_guid == NULL) {
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}
		if (DirectPlayCreate(service_provider_guid,
				     &g_front_state.net_temp_direct_play,
				     NULL) != 0) {
			if (error_text_load_line(6, error_message) == 0) {
				frontend_display_show_game_message_box(
					"WARNING:  Connection failure!\n"
					"\n"
					"Make sure your Windows 95 network\n"
					"settings are properly configured\n"
					"for this type of network game.\n"
					"\n"
					"Press Enter to continue.");
			} else {
				frontend_display_show_game_message_box(
					error_message);
			}
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}
		result = g_front_state.net_temp_direct_play->lpVtbl
				 ->QueryInterface(
					 g_front_state.net_temp_direct_play,
					 &IID_IDirectPlay2A,
					 (void **)&g_front_state
						 .net_direct_play);
		g_front_state.net_temp_direct_play->lpVtbl->Release(
			g_front_state.net_temp_direct_play);
		g_front_state.net_temp_direct_play = NULL;
		if (result != 0) {
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}

		strncpy(g_front_state.net_players[0].long_name,
			local_player_info,
			sizeof(g_front_state.net_players[0].long_name));
		g_front_state.net_players[0]
			.long_name[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		strncpy(g_front_state.net_players[0].player_name,
			local_player_name,
			sizeof(g_front_state.net_players[0].player_name));
		g_front_state.net_players[0]
			.player_name[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		if (selected_network_type == NET_TRANSPORT_MODEM) {
			strcpy(game_session_name, dial_number);
		} else if (selected_network_type == NET_TRANSPORT_SERIAL) {
			char direct_serial_name[32] = "Direct serial game.";
			strcpy(game_session_name, direct_serial_name);
		} else if (session_name[0] == '\0') {
			sprintf(game_session_name, "%s's Game.",
				local_player_name);
		} else {
			strcpy(game_session_name, session_name);
		}
		strncpy(g_front_state.net_session_name, game_session_name,
			sizeof(g_front_state.net_session_name));
		g_front_state
			.net_session_name[NET_SESSION_NAME_TERMINATOR_INDEX] =
			'\0';
		g_front_state.net_is_host = is_host;
		g_front_state.net_app_guid = *(const GUID *)&app_guid_data1;
		switch (is_host) {
		case 0:
			result = net_join_direct_play_session(
				game_session_name, join_session_instance_guid);
			break;
		case 1:
			result =
				net_host_direct_play_session(game_session_name);
			break;
		default:
			break;
		}
		if (result == 0) {
			g_front_state.net_direct_play->lpVtbl->Release(
				g_front_state.net_direct_play);
			g_front_state.net_direct_play = NULL;
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}
	} else {
		service_provider_guid =
			net_get_direct_play_service_provider_guid(
				selected_network_type);
		if (service_provider_guid == NULL) {
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}
		if (DirectPlayCreate(service_provider_guid,
				     &g_front_state.net_temp_direct_play,
				     NULL) != 0) {
			if (error_text_load_line(6, error_message) == 0) {
				frontend_display_show_game_message_box(
					"WARNING:  Connection failure!\n"
					"\n"
					"Make sure your Windows 95 network\n"
					"settings are properly configured\n"
					"for this type of network game.\n"
					"\n"
					"Press Enter to continue.");
			} else {
				frontend_display_show_game_message_box(
					error_message);
			}
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}
		g_front_state.net_temp_direct_play->lpVtbl->Release(
			g_front_state.net_temp_direct_play);
		g_front_state.net_temp_direct_play = NULL;
		g_front_state.net_is_host = is_host;
		strncpy(g_front_state.net_players[0].long_name,
			local_player_info,
			sizeof(g_front_state.net_players[0].long_name));
		g_front_state.net_players[0]
			.long_name[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		strncpy(g_front_state.net_players[0].player_name,
			local_player_name,
			sizeof(g_front_state.net_players[0].player_name));
		g_front_state.net_players[0]
			.player_name[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		if (net_open_direct_play_session(
			    *(const GUID *)&app_guid_data1, local_player_info,
			    local_player_name, is_host, dial_number,
			    selected_network_type, connection_address) == 0) {
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}
	}

	DPCAPS direct_play_caps;
	memset(&direct_play_caps, 0, sizeof(direct_play_caps));
	direct_play_caps.dwSize = sizeof(direct_play_caps);
	g_front_state.net_direct_play->lpVtbl->GetCaps(
		g_front_state.net_direct_play, &direct_play_caps, 0);
	DPID direct_play_player = net_create_direct_play_player(
		local_player_info, local_player_name);
	if (direct_play_player == 0) {
		g_front_state.net_direct_play->lpVtbl->Close(
			g_front_state.net_direct_play);
		g_front_state.net_direct_play->lpVtbl->Release(
			g_front_state.net_direct_play);
		g_front_state.net_direct_play = NULL;
		if (g_front_state.net_direct_play_lobby != NULL) {
			g_front_state.net_direct_play_lobby->lpVtbl->Release(
				g_front_state.net_direct_play_lobby);
			g_front_state.net_direct_play_lobby = NULL;
		}
		if (back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		net_restore_auto_dial_registry_setting();
		return 0;
	}
	g_front_state.net_players[0].player_id = direct_play_player;
	g_front_state.net_runtime_local_player = g_front_state.net_players[0];
	if (is_host != 0) {
		if (g_front_state.net_direct_play->lpVtbl->CreateGroup(
			    g_front_state.net_direct_play,
			    &g_front_state.net_group_dplay_id, NULL, NULL, 0,
			    0) != 0) {
			g_front_state.net_direct_play->lpVtbl->DestroyPlayer(
				g_front_state.net_direct_play,
				g_front_state.net_runtime_local_player
					.player_id);
			g_front_state.net_direct_play->lpVtbl->Close(
				g_front_state.net_direct_play);
			g_front_state.net_direct_play->lpVtbl->Release(
				g_front_state.net_direct_play);
			g_front_state.net_direct_play = NULL;
			if (g_front_state.net_direct_play_lobby != NULL) {
				g_front_state.net_direct_play_lobby->lpVtbl
					->Release(
						g_front_state
							.net_direct_play_lobby);
				g_front_state.net_direct_play_lobby = NULL;
			}
			if (back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			net_restore_auto_dial_registry_setting();
			return 0;
		}
		g_front_state.net_runtime_reliable_peer_slots[0]
			.direct_play_id = g_front_state.net_group_dplay_id;
		g_front_state.net_reliable_peer_slot_count = 1;
		sprintf(game_session_name, "My id%u\n",
			g_front_state.net_runtime_local_player.player_id);
		sprintf(game_session_name, "Group id%u\n",
			g_front_state.net_group_dplay_id);
	}
	g_front_state.net_player_count = 1;
	net_refresh_player_roster();
	g_front_state.net_runtime_recv_queue_write_index = 0;
	g_front_state.net_runtime_recv_queue_read_index = 0;
	g_front_state.net_runtime_recv_queue_count = 0;

	if (wait_for_player_count > 0) {
		while (net_get_player_count() < wait_for_player_count) {
			net_pump_incoming_packets();
		}
	}
	if (wait_for_player_count <= 0) {
		if (is_host != 0) {
			g_front_state.net_host_player_id =
				g_front_state.net_runtime_local_player
					.player_id;
		} else {
			DPID sender_id;
			int *received_packet;
			do {
				received_packet = net_wait_for_app_packet(
					&sender_id,
					&packet_workspace.received_size,
					NET_JOIN_TIMEOUT_SECONDS);
				if (received_packet == NULL) {
					g_front_state.net_direct_play->lpVtbl
						->DestroyPlayer(
							g_front_state
								.net_direct_play,
							g_front_state
								.net_runtime_local_player
								.player_id);
					g_front_state.net_direct_play->lpVtbl
						->Close(g_front_state
								.net_direct_play);
					g_front_state.net_direct_play->lpVtbl
						->Release(
							g_front_state
								.net_direct_play);
					g_front_state.net_direct_play = NULL;
					if (g_front_state
						    .net_direct_play_lobby !=
					    NULL) {
						g_front_state
							.net_direct_play_lobby
							->lpVtbl->Release(
								g_front_state
									.net_direct_play_lobby);
						g_front_state
							.net_direct_play_lobby =
							NULL;
					}
					if (back_buffer_locked != 0) {
						g_draw_surface_ptr =
							frontend_display_lock_back_buffer();
					}
					net_restore_auto_dial_registry_setting();
					return 0;
				}
			} while (received_packet[0] !=
				 NET_PACKET_SEQUENCE_STATUS);

			g_front_state.net_host_player_id = sender_id;
			struct net_reliable_peer_slot saved_host_slot;
			memset(&saved_host_slot, 0, sizeof(saved_host_slot));
			uint32_t peer_slot_index;
			for (peer_slot_index = 0;
			     g_front_state.net_reliable_peer_slot_count >
			     peer_slot_index;
			     ++peer_slot_index) {
				if (g_front_state
					    .net_runtime_reliable_peer_slots
						    [peer_slot_index]
					    .direct_play_id ==
				    g_front_state.net_host_player_id) {
					memcpy(&saved_host_slot,
					       &g_front_state
							.net_runtime_reliable_peer_slots
								[peer_slot_index],
					       sizeof(saved_host_slot));
					break;
				}
			}

			g_front_state.net_reliable_peer_slot_count =
				(uint32_t)received_packet[2];
			/* From here result holds the host's time stamp in ms,
			 * echoed back below in the keepalive ack. */
			result = received_packet[3];
			const uint8_t *peer_sequence_records =
				(const uint8_t *)received_packet +
				NET_JOIN_SEQUENCE_RECORD_OFFSET;
			for (peer_slot_index = 0;
			     g_front_state.net_reliable_peer_slot_count >
			     peer_slot_index;
			     ++peer_slot_index) {
				int sequence_record_offset =
					peer_slot_index *
					NET_JOIN_SEQUENCE_RECORD_SIZE;
				memcpy(&g_front_state
						.net_runtime_reliable_peer_slots
							[peer_slot_index]
						.direct_play_id,
				       &peer_sequence_records
					       [sequence_record_offset],
				       sizeof(g_front_state
						      .net_runtime_reliable_peer_slots
							      [peer_slot_index]
						      .direct_play_id));
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.last_delivered_seq_channel_a =
					peer_sequence_records
						[sequence_record_offset + 4];
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.last_delivered_seq_channel_b =
					peer_sequence_records
						[sequence_record_offset + 5];
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.recv_seq_channel_a =
					peer_sequence_records
						[sequence_record_offset + 6];
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.recv_seq_channel_b =
					peer_sequence_records
						[sequence_record_offset + 7];
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.last_delivered_seq_default =
					NET_RELIABLE_SEQUENCE_INITIAL;
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.recv_seq_default =
					NET_RELIABLE_SEQUENCE_INITIAL;
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.send_seq = 0;
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.last_piggyback_type = NET_PACKET_NOP;
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.piggyback_length = 1;
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.last_activity_ms = GetTickCount();
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_slot_index]
					.last_heard_ms = GetTickCount();
			}

			if (saved_host_slot.direct_play_id != 0) {
				for (peer_slot_index = 0;
				     g_front_state
					     .net_reliable_peer_slot_count >
				     peer_slot_index;
				     ++peer_slot_index) {
					if (g_front_state
						    .net_runtime_reliable_peer_slots
							    [peer_slot_index]
						    .direct_play_id ==
					    g_front_state.net_host_player_id) {
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_slot_index]
							.last_delivered_seq_default =
							saved_host_slot
								.last_delivered_seq_default;
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_slot_index]
							.recv_seq_default =
							saved_host_slot
								.recv_seq_default;
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_slot_index]
							.send_seq =
							saved_host_slot
								.send_seq;
						memcpy(&g_front_state
								.net_runtime_reliable_peer_slots
									[peer_slot_index]
								.last_piggyback_type,
						       &saved_host_slot
								.last_piggyback_type,
						       saved_host_slot
							       .piggyback_length);
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_slot_index]
							.piggyback_length =
							saved_host_slot
								.piggyback_length;
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_slot_index]
							.last_activity_ms =
							saved_host_slot
								.last_activity_ms;
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_slot_index]
							.last_heard_ms =
							saved_host_slot
								.last_heard_ms;
					}
				}
			}
			packet_workspace.words[0] = NET_PACKET_KEEPALIVE_ACK;
			memcpy(&packet_workspace.words[1], &result,
			       sizeof(packet_workspace.words[1]));
			memset(&packet_workspace.words[2], 0,
			       NET_JOIN_RESPONSE_SIZE -
				       2 * sizeof(packet_workspace.words[0]));
			net_send_direct_play_packet(
				g_front_state.net_host_player_id,
				packet_workspace.words, NET_JOIN_RESPONSE_SIZE,
				0);
		}
	}

	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	net_restore_auto_dial_registry_setting();
	g_net_active_transport_type = selected_network_type;
	return 1;
}
#endif

/* net_shutdown_direct_play_session_ex(1, 1): in the original build, with the
 * TCP/IP shutdown handshake and no relaunch when it fails. */
// FUNCTION: XVT 0x4CD9C0
void net_shutdown_direct_play_session_for_quit(void)
{
	net_shutdown_direct_play_session_ex(1, 1);
}

/* net_shutdown_direct_play_session_ex(0, 1): in the original build, with the
 * TCP/IP shutdown handshake, relaunching the game when it fails. */
// FUNCTION: XVT 0x4CD9D0
void net_shutdown_direct_play_session(void)
{
	net_shutdown_direct_play_session_ex(0, 1);
}

/* net_shutdown_direct_play_session_ex(0, 0): without the shutdown handshake. Only
 * the original build calls this. */
// FUNCTION: XVT 0x4CD9E0
void net_shutdown_direct_play_session_no_handshake(void)
{
	net_shutdown_direct_play_session_ex(0, 0);
}

/* Closes the lobby session. The modern build first calls
 * xvt_network_session_on_close. When DirectPlay is open: in the original build
 * a TCP/IP session with wait_for_handshake_acks runs
 * net_wait_for_shutdown_handshake_acks, and when that fails saves the persistent
 * state, shuts the display and CD audio and exits the process, first
 * starting "z_xvt__.exe skipintro" unless suppress_restart is set. It then
 * sets g_net_active_transport_type to IPX and destroys the local player; in the
 * original build, when that takes over 20 seconds, it shows a DirectPlay
 * error (unless suppress_restart is set) and terminates the process. It
 * destroys the group, closes and releases DirectPlay. In every case it
 * empties the receive queue and resets the broadcast and group counters and
 * the 40 peer slots. Returns 1. The back buffer is unlocked meanwhile and
 * relocked when it was locked. */
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

#ifndef XVT_MODERN
	uint32_t destroy_player_start_time;
	void *current_process;
#endif

	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
#ifdef XVT_MODERN
	xvt_network_session_on_close();
	XVT_LOG_DEBUG(
		"network.lobby_closing open=%d player=%u group=%u queued=%d peers=%u quit=%d handshake=%d",
		g_front_state.net_direct_play != NULL,
		(unsigned)g_front_state.net_runtime_local_player.player_id,
		(unsigned)g_front_state.net_group_dplay_id,
		g_front_state.net_runtime_recv_queue_count,
		(unsigned)g_front_state.net_reliable_peer_slot_count,
		suppress_restart, wait_for_handshake_acks);
#endif
	if (g_front_state.net_direct_play != NULL) {
#ifndef XVT_MODERN
		if (g_net_active_transport_type == NET_TRANSPORT_TCPIP &&
		    wait_for_handshake_acks != 0) {
			if (net_wait_for_shutdown_handshake_acks() == 0) {
				if (suppress_restart != 0) {
					frontend_save_persistent_state();
					frontend_display_shutdown(0);
					cd_audio_close_device();
					ExitProcess(0);
				} else {
					frontend_save_persistent_state();
					frontend_display_shutdown(0);
					cd_audio_close_device();
					if (g_front_state.hWnd != NULL) {
						SetWindowTextA(
							g_front_state.hWnd,
							"XvT - Exiting");
					}
					win32_create_process_from_command_line(
						"z_xvt__.exe skipintro");
					ExitProcess(0);
				}
			}
		}
#else
		(void)suppress_restart;
		(void)wait_for_handshake_acks;
#endif
		g_net_active_transport_type = NET_TRANSPORT_IPX;
#ifndef XVT_MODERN
		destroy_player_start_time = GetTickCount();
#endif
		g_front_state.net_direct_play->lpVtbl->DestroyPlayer(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id);
#ifndef XVT_MODERN
		if (GetTickCount() - destroy_player_start_time >
		    NET_DESTROY_PLAYER_TIMEOUT_MS) {
			if (suppress_restart == 0) {
				frontend_dialog_show_network_abort_error(
					frontend_string_get(
						FRONTSTR_762_DIRECT_PLAY_ERROR_FAILED_TO_DISCONNECT),
					frontend_string_get(
						FRONTSTR_763_ATTEMPTING_TO_EXIT_TO_WINDOWS),
					frontend_string_get(
						FRONTSTR_764_YOUR_COMPUTER_MAY_STOP_RESPONDING),
					frontend_string_get(
						FRONTSTR_006_EXIT_TO_WINDOWS),
					NULL);
			}
			frontend_sound_shutdown_direct_sound();
			cd_audio_close_device();
			current_process = GetCurrentProcess();
			TerminateProcess(current_process, 0);
		}
#endif
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

#ifndef XVT_MODERN
/* Creates a DirectPlay session for up to 32 players under
 * g_front_state.net_app_guid with the given name. Returns 1 when Open succeeds,
 * else 0. Only the original build calls this. */
// FUNCTION: XVT 0x4CDDC0
int net_host_direct_play_session(const char *session_name)
{
	DPSESSIONDESC2 session_desc;

	memset(&session_desc, 0, sizeof(session_desc));
	session_desc.dwSize = sizeof(session_desc);
	session_desc.dwFlags = 0x40;
	session_desc.guidApplication = g_front_state.net_app_guid;
	session_desc.dwMaxPlayers = 32;
	session_desc.lpszSessionNameA = (char *)session_name;

	return g_front_state.net_direct_play->lpVtbl->Open(
		       g_front_state.net_direct_play, &session_desc, 2) == 0;
}
#endif

/* Creates the local DirectPlay player with the given long and short names
 * and returns its id, or 0 on failure. The original build tries up to 5
 * times; the modern build tries once and returns XVT_NETWORK_PENDING while
 * DirectPlay reports the call pending. */
// FUNCTION: XVT 0x4CDE40
int net_create_direct_play_player(const char *long_player_info,
				  const char *short_player_name)
{
#ifdef XVT_MODERN
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
#else
	int attempts_remaining = 5;
	DPNAME player_name;
	memset(&player_name, 0, sizeof(player_name));
	player_name.lpszShortNameA = (char *)short_player_name;
	player_name.lpszLongNameA = (char *)long_player_info;
	player_name.dwSize = sizeof(player_name);
	DPID player_id;
	do {
		if (g_front_state.net_direct_play->lpVtbl->CreatePlayer(
			    g_front_state.net_direct_play, &player_id,
			    &player_name, NULL, NULL, 0, 0) == 0) {
			break;
		}
		--attempts_remaining;
	} while (attempts_remaining != 0);
	if (attempts_remaining == 0) {
		return 0;
	}
	return player_id;
#endif
}

#ifndef XVT_MODERN
/* Joins the session with session_instance_guid or, when that is NULL, the one
 * net_find_session_by_name finds by name. Stores its GUID in
 * g_front_state.net_joined_session_guid and returns 1; returns 0 when no session
 * is found or Open fails. Only the original build calls this. */
// FUNCTION: XVT 0x4CDEB0
int net_join_direct_play_session(const char *session_name,
				 const GUID *session_instance_guid)
{
	const GUID *resolved_session_guid = session_instance_guid;
	if (resolved_session_guid == 0) {
		resolved_session_guid = net_find_session_by_name(session_name);
		if (resolved_session_guid == 0) {
			return 0;
		}
	}

	DPSESSIONDESC2 session_desc;
	memset(&session_desc, 0, sizeof(session_desc));
	session_desc.dwSize = sizeof(session_desc);
	session_desc.dwFlags = 0x40;
	session_desc.guidInstance = *resolved_session_guid;
	if (g_front_state.net_direct_play->lpVtbl->Open(
		    g_front_state.net_direct_play, &session_desc, 1) != 0) {
		return 0;
	}

	g_front_state.net_joined_session_guid = *resolved_session_guid;
	return 1;
}
#endif

#ifndef XVT_MODERN
/* Enumerates the application's sessions, letting
 * net_enum_sessions_match_name_callback record the one named session_name, and
 * returns &g_net_matched_session_instance_guid when EnumSessions succeeds, else
 * NULL. It does not check that a session matched: with no match it returns
 * whatever GUID the global last held. Only the original build calls this. */
// FUNCTION: XVT 0x4CDF60
const GUID *net_find_session_by_name(const char *session_name)
{
	DPSESSIONDESC2 session_desc;

	memset(&session_desc, 0, sizeof(session_desc));
	session_desc.dwSize = sizeof(session_desc);
	session_desc.guidApplication = g_front_state.net_app_guid;

	return g_front_state.net_direct_play->lpVtbl->EnumSessions(
		       g_front_state.net_direct_play, &session_desc, 0,
		       net_enum_sessions_match_name_callback,
		       (void *)session_name, 1) == 0
		       ? &g_net_matched_session_instance_guid
		       : 0;
}
#endif

#ifndef XVT_MODERN
/* Session enumeration callback: copies the instance GUID of the session
 * named context into g_net_matched_session_instance_guid and returns 0 to stop.
 * Also stops on a timeout or a NULL description; otherwise returns 1 to go
 * on. Only the original build calls this. */
// FUNCTION: XVT 0x4CDFD0
int AERON_DXAPI net_enum_sessions_match_name_callback(
	const DPSESSIONDESC2 *session_desc, uint32_t *timeout_ms,
	uint32_t flags, void *context)
{
	(void)timeout_ms;

	if (session_desc == 0 || (flags & 1) != 0) {
		return 0;
	}
	if (strcmp(session_desc->lpszSessionNameA, (const char *)context) ==
	    0) {
		g_net_matched_session_instance_guid =
			session_desc->guidInstance;
		return 0;
	}

	return 1;
}
#endif

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

/* Reads every waiting DirectPlay message into the lobby receive queue,
 * g_front_state.net_runtime_recv_queue, after sending due keepalives
 * (net_send_sequence_keepalives) and checking for silent peers
 * (net_drop_silent_peers). Stops when DirectPlay has nothing more or 1023
 * entries are queued, and does nothing without DirectPlay. System messages
 * (sender 0) are queued whole, up to 512 bytes; messages for anyone but the
 * local player are dropped. A PING is answered with a PONG. A KEEPALIVE_ACK
 * stamps the sender's last_heard_ms and updates its g_net_player_connection_stats
 * entry: the counts it carries, and a latency sample (the time since the
 * echoed stamp, less 40 ms) when under 750 ms and not over half above the
 * average; a new entry starts with the sample capped at 750. A NACK, or a
 * WORLD_NACK once a flight has ended, resends the packet asked for from the
 * sent history or from the flight's world-message history, or a NOP with
 * that sequence when it is gone, and counts a drop on the sender after its
 * first 20 packets. A KEEPALIVE from the host is answered with a
 * KEEPALIVE_ACK carrying this side's counts; any KEEPALIVE makes it resend,
 * on each channel whose counter has moved on, the packet the sender expects
 * next. Every other packet stamps the sender's last_heard_ms and is queued with
 * its channel and sequence: a resend as a resent copy; otherwise its trailer
 * first, when the previous sequence was not seen (a drop), then the packet
 * itself unless its sequence was already seen. Bodies are cut to 508 bytes.
 * The back buffer is unlocked meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4CE130
void net_pump_incoming_packets(void)
{
	enum {
		QUEUE_CAPACITY = 1024,
		QUEUE_LIMIT = QUEUE_CAPACITY - 1,
		SYSTEM_PACKET_SIZE_LIMIT = 512,
		HISTORY_CAPACITY = 128,
		SENT_WORLD_MESSAGE_HISTORY_CAPACITY = 256,
		PEER_CAPACITY = 40,
		MAX_PAYLOAD_SIZE = 508,
		SEQUENCE_LIMIT = 127,
		SEQUENCE_NOTHING_TO_RESEND = 128,
		LATENCY_SEND_BIAS_MS = 40,
		LATENCY_MAX_MS = 750,
		LATENCY_OUTLIER_PERCENT = 50,
		PACKET_DROP_WARMUP_COUNT = 20,
	};

	struct {
		uint16_t header;
		uint8_t data[1022];
	} wire_packet;

	int back_buffer_locked = g_front_state.back_buffer_locked;

	frontend_display_unlock_back_buffer();
	DPID from_id;
	DPID to_id;
	int packet_words[6];
	if (g_front_state.net_direct_play != NULL) {
		net_send_sequence_keepalives();
		net_drop_silent_peers();
		for (;;) {
			if (g_front_state.net_runtime_recv_queue_count >=
			    QUEUE_LIMIT) {
				XVT_LOG_WARN(
					"network.lobby_receive_queue_full queued=%d",
					g_front_state
						.net_runtime_recv_queue_count);
				if (back_buffer_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
			uint32_t wire_size = sizeof(wire_packet);
			if (g_front_state.net_direct_play->lpVtbl->Receive(
				    g_front_state.net_direct_play, &from_id,
				    &to_id, 1, &wire_packet, &wire_size) != 0) {
				if (back_buffer_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
			if (from_id == 0) {
				XVT_LOG_DEBUG(
					"network.lobby_system_received bytes=%u",
					(unsigned)wire_size);
				if (wire_size > SYSTEM_PACKET_SIZE_LIMIT) {
					wire_size = SYSTEM_PACKET_SIZE_LIMIT;
				}
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.direct_play_id = from_id;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.payload_size = wire_size;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.is_resent_copy = 0;
				memcpy(g_front_state
					       .net_runtime_recv_queue
						       [g_front_state
								.net_runtime_recv_queue_write_index]
					       .payload,
				       &wire_packet.header, wire_size);
				++g_front_state
					  .net_runtime_recv_queue_write_index;
				++g_front_state.net_runtime_recv_queue_count;
				if (g_front_state
					    .net_runtime_recv_queue_write_index >=
				    QUEUE_CAPACITY) {
					g_front_state
						.net_runtime_recv_queue_write_index =
						0;
				}
				continue;
			}
			if (to_id !=
			    g_front_state.net_runtime_local_player.player_id) {
				XVT_LOG_DEBUG(
					"network.lobby_packet_not_local from=%u to=%u",
					(unsigned)from_id, (unsigned)to_id);
				continue;
			}

			int *payload = (int *)wire_packet.data;
			unsigned int packet_type = wire_packet.header & 0x7F;
			int group_channel = (wire_packet.header & 0x80) != 0;
			int broadcast_channel =
				(wire_packet.header & 0x8000) == 0;
			int sequence = (wire_packet.header >> 8) & 0x7F;
			/* Outside the resync types (60-63) every packet carries
			 * a piggyback trailer after its payload, and a type
			 * with no fixed size also starts with a length word;
			 * has_length stands for both. */
			int has_length =
				packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_RESYNC_CHUNK + 1;
			uint32_t payload_size;
			if (has_length) {
				payload_size = (uint32_t)
					net_session_get_fixed_payload_size(
						(int)packet_type);
				if (payload_size == 0) {
					payload_size =
						*(const uint16_t *)
							 wire_packet.data;
					payload = (int *)(wire_packet.data +
							  sizeof(uint16_t));
				}
			}
			if (packet_type == NET_PACKET_PING) {
				packet_words[0] = NET_PACKET_PONG;
				net_send_packet_and_flush(
					(int)from_id, packet_words,
					sizeof(packet_words[0]));
				XVT_LOG_DEBUG(
					"network.lobby_ping_answered from=%u",
					(unsigned)from_id);
				continue;
			}

			unsigned int peer_index;
			if (packet_type == NET_PACKET_KEEPALIVE_ACK) {
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_heard_ms = GetTickCount();
				uint32_t echoed_send_ms = (uint32_t)payload[0];
				/* Until the average is computed below,
				 * average_latency_ms holds the current time
				 * less LATENCY_SEND_BIAS_MS, the clock the
				 * echoed send time is measured against. */
				uint32_t average_latency_ms =
					GetTickCount() - LATENCY_SEND_BIAS_MS;
				uint32_t latency_ms;
				if (echoed_send_ms >= average_latency_ms) {
					latency_ms = 1;
				} else {
					latency_ms = average_latency_ms -
						     echoed_send_ms;
				}

				unsigned int stats_index = 0;
				while (stats_index < PEER_CAPACITY &&
				       g_net_player_connection_stats
						       [stats_index]
							       .player_id !=
					       (int)from_id) {
					++stats_index;
				}
				if (stats_index < PEER_CAPACITY) {
					if (latency_ms < LATENCY_MAX_MS) {
						if (g_net_player_connection_stats
							    [stats_index]
								    .latency_sample_count !=
						    0) {
							uint32_t latency_total_ms =
								g_net_player_connection_stats
									[stats_index]
										.latency_total_ms;
							average_latency_ms =
								latency_total_ms /
								g_net_player_connection_stats
									[stats_index]
										.latency_sample_count;
							if (latency_ms >
							    average_latency_ms) {
								if (100 *
									    (latency_ms -
									     average_latency_ms) /
									    average_latency_ms >
								    LATENCY_OUTLIER_PERCENT) {
									g_net_player_connection_stats
										[stats_index]
											.packet_count = payload
										[1];
									g_net_player_connection_stats
										[stats_index]
											.packet_drop_count =
										payload[2];
									g_net_player_connection_stats
										[stats_index]
											.packet_retry_count =
										payload[3];
								} else {
									g_net_player_connection_stats
										[stats_index]
											.latency_total_ms =
										latency_ms +
										latency_total_ms;
									g_net_player_connection_stats
										[stats_index]
											.packet_count = payload
										[1];
									g_net_player_connection_stats
										[stats_index]
											.packet_drop_count =
										payload[2];
									g_net_player_connection_stats
										[stats_index]
											.packet_retry_count =
										payload[3];
									++g_net_player_connection_stats
										  [stats_index]
											  .latency_sample_count;
								}
							} else {
								g_net_player_connection_stats
									[stats_index]
										.latency_total_ms =
									latency_ms +
									latency_total_ms;
								g_net_player_connection_stats
									[stats_index]
										.packet_count =
									payload[1];
								g_net_player_connection_stats
									[stats_index]
										.packet_drop_count =
									payload[2];
								g_net_player_connection_stats
									[stats_index]
										.packet_retry_count =
									payload[3];
								++g_net_player_connection_stats
									  [stats_index]
										  .latency_sample_count;
							}
						}
					} else {
						g_net_player_connection_stats
							[stats_index]
								.packet_count =
							payload[1];
						g_net_player_connection_stats
							[stats_index]
								.packet_drop_count =
							payload[2];
						g_net_player_connection_stats
							[stats_index]
								.packet_retry_count =
							payload[3];
					}
				}

				if (stats_index >= PEER_CAPACITY) {
					stats_index = 0;
					while (stats_index < PEER_CAPACITY &&
					       g_net_player_connection_stats
							       [stats_index]
								       .player_id !=
						       0) {
						++stats_index;
					}
					if (stats_index < PEER_CAPACITY) {
						if (latency_ms >
						    LATENCY_MAX_MS) {
							latency_ms =
								LATENCY_MAX_MS;
						}
						g_net_player_connection_stats
							[stats_index]
								.player_id =
							(int)from_id;
						g_net_player_connection_stats
							[stats_index]
								.latency_total_ms =
							latency_ms;
						g_net_player_connection_stats
							[stats_index]
								.packet_count =
							payload[1];
						g_net_player_connection_stats
							[stats_index]
								.packet_drop_count =
							payload[2];
						g_net_player_connection_stats
							[stats_index]
								.packet_retry_count =
							payload[3];
						g_net_player_connection_stats
							[stats_index]
								.latency_sample_count =
							1;
					} else {
						XVT_LOG_WARN(
							"network.lobby_link_stats_full player=%u figure=\"all\"",
							(unsigned)from_id);
					}
				}
				XVT_LOG_DEBUG(
					"network.lobby_keepalive_ack from=%u latency=%u entry=%u packets=%d drops=%d retries=%d",
					(unsigned)from_id, (unsigned)latency_ms,
					stats_index, payload[1], payload[2],
					payload[3]);
				continue;
			}

			if (packet_type == NET_PACKET_WORLD_NACK) {
				if (g_front_state
					    .net_flight_sent_world_message_history !=
				    NULL) {
					int missing_world_tick = payload[0];
					int control_value1 = payload[1];
					peer_index =
						net_find_or_create_peer_slot(
							(int)from_id);
					if ((unsigned int)g_front_state
						    .net_runtime_reliable_peer_slots
							    [peer_index]
						    .packet_count >
					    PACKET_DROP_WARMUP_COUNT) {
						++g_front_state
							  .net_runtime_reliable_peer_slots
								  [peer_index]
							  .packet_drop_count;
					}

					int search_index =
						g_front_state
							.net_flight_sent_world_message_write_index;
					unsigned int search_count = 0;
					struct net_queued_packet *queued;
					while (search_count <
					       SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
						queued =
							&g_front_state.net_flight_sent_world_message_history
								 [search_index];
						if (queued->payload_size != 0) {
							if ((*(const uint32_t
								       *)&queued->payload
								      [sizeof(int)] &
							     0x7FFFFFFF) ==
							    (uint32_t)
								    missing_world_tick) {
								break;
							}
						}
						++search_count;
						++search_index;
						if ((unsigned int)
							    search_index >=
						    SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
							search_index = 0;
						}
					}
					if (search_count <
					    SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
						queued =
							&g_front_state.net_flight_sent_world_message_history
								 [search_index];
						net_send_sequenced_direct_play_packet(
							(int)from_id, 0,
							queued->sequence_byte,
							queued->payload,
							queued->payload_size);
					}
					if (search_count >=
					    SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
						packet_words[0] =
							NET_PACKET_NOP;
						net_send_sequenced_direct_play_packet(
							(int)from_id, 0,
							control_value1,
							packet_words,
							sizeof(packet_words
								       [0]));
					}
					XVT_LOG_DEBUG(
						"network.lobby_world_nack from=%u tick=%d found=%d",
						(unsigned)from_id,
						missing_world_tick,
						search_count <
							SENT_WORLD_MESSAGE_HISTORY_CAPACITY);
				} else {
					XVT_LOG_WARN(
						"network.lobby_world_nack_ignored from=%u tick=%d",
						(unsigned)from_id, payload[0]);
				}
				continue;
			}

			if (packet_type == NET_PACKET_NACK) {
				int expected_broadcast_sequence = payload[0];
				int control_value1 = payload[1];
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_heard_ms = GetTickCount();
				if ((unsigned int)g_front_state
					    .net_runtime_reliable_peer_slots
						    [peer_index]
					    .packet_count >
				    PACKET_DROP_WARMUP_COUNT) {
					++g_front_state
						  .net_runtime_reliable_peer_slots
							  [peer_index]
						  .packet_drop_count;
				}

				int search_index =
					g_front_state
						.net_runtime_sent_history_write_index;
				unsigned int search_count = 0;
				while (search_count < HISTORY_CAPACITY) {
					if (g_front_state
						    .net_runtime_sent_history
							    [search_index]
						    .payload_size != 0) {
						if (control_value1 == 0 ||
						    control_value1 == 2) {
							if (g_front_state.net_runtime_sent_history
									    [search_index]
										    .sequence_byte ==
								    expected_broadcast_sequence &&
							    g_front_state.net_runtime_sent_history
									    [search_index]
										    .packet_class ==
								    control_value1) {
								break;
							}
						} else if (
							g_front_state.net_runtime_sent_history
									[search_index]
										.direct_play_id ==
								from_id &&
							g_front_state.net_runtime_sent_history
									[search_index]
										.sequence_byte ==
								expected_broadcast_sequence &&
							g_front_state.net_runtime_sent_history
									[search_index]
										.packet_class ==
								control_value1) {
							break;
						}
					}
					++search_count;
					++search_index;
					if ((unsigned int)search_index >=
					    HISTORY_CAPACITY) {
						search_index = 0;
					}
				}
				if (search_count < HISTORY_CAPACITY) {
					net_send_sequenced_direct_play_packet(
						(int)from_id, control_value1,
						expected_broadcast_sequence,
						g_front_state
							.net_runtime_sent_history
								[search_index]
							.payload,
						g_front_state
							.net_runtime_sent_history
								[search_index]
							.payload_size);
				}
				if (search_count >= HISTORY_CAPACITY) {
					packet_words[0] = NET_PACKET_NOP;
					net_send_sequenced_direct_play_packet(
						(int)from_id, control_value1,
						expected_broadcast_sequence,
						packet_words,
						sizeof(packet_words[0]));
				}
				XVT_LOG_DEBUG(
					"network.lobby_nack from=%u seq=%d channel=%d found=%d",
					(unsigned)from_id,
					expected_broadcast_sequence,
					control_value1,
					search_count < HISTORY_CAPACITY);
				continue;
			}

			if (packet_type == NET_PACKET_KEEPALIVE) {
				int control_value0 = payload[0];
				int expected_group_sequence = payload[1];
				int expected_direct_sequence = payload[2];
				if (g_front_state.net_host_player_id ==
				    from_id) {
					peer_index =
						net_find_or_create_peer_slot(
							(int)from_id);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_heard_ms = GetTickCount();
					packet_words[0] =
						NET_PACKET_KEEPALIVE_ACK;
					packet_words[1] = payload[3];
					packet_words[2] =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.packet_count;
					packet_words[3] =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.packet_drop_count;
					packet_words[4] =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.packet_retry_count;
					net_send_direct_play_packet(
						(int)g_front_state
							.net_host_player_id,
						packet_words,
						5 * sizeof(packet_words[0]), 0);
				}
				if (g_front_state
					    .net_runtime_broadcast_seq_counter ==
				    control_value0) {
					control_value0 =
						SEQUENCE_NOTHING_TO_RESEND;
				}
				if (g_front_state
					    .net_runtime_group_seq_counter ==
				    expected_group_sequence) {
					expected_group_sequence =
						SEQUENCE_NOTHING_TO_RESEND;
				}
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				if (g_front_state.net_reliable_peer_slot_count ==
					    peer_index ||
				    g_front_state.net_runtime_reliable_peer_slots
						    [peer_index]
							    .send_seq ==
					    expected_direct_sequence) {
					expected_direct_sequence =
						SEQUENCE_NOTHING_TO_RESEND;
				}

				unsigned int search_count = HISTORY_CAPACITY;
				int search_index =
					g_front_state
						.net_runtime_sent_history_write_index;
				do {
					if (control_value0 ==
						    SEQUENCE_NOTHING_TO_RESEND &&
					    expected_group_sequence ==
						    SEQUENCE_NOTHING_TO_RESEND &&
					    expected_direct_sequence ==
						    SEQUENCE_NOTHING_TO_RESEND) {
						break;
					}
					if (g_front_state
						    .net_runtime_sent_history
							    [search_index]
						    .payload_size != 0) {
						uint8_t packet_class =
							g_front_state
								.net_runtime_sent_history
									[search_index]
								.packet_class;
						if (packet_class == 0) {
							if (g_front_state
								    .net_runtime_sent_history
									    [search_index]
								    .sequence_byte ==
							    control_value0) {
								net_send_sequenced_direct_play_packet(
									(int)from_id,
									0,
									control_value0,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload_size);
								control_value0 =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						} else if (packet_class == 2) {
							if (g_front_state
								    .net_runtime_sent_history
									    [search_index]
								    .sequence_byte ==
							    expected_group_sequence) {
								net_send_sequenced_direct_play_packet(
									(int)from_id,
									2,
									expected_group_sequence,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload_size);
								expected_group_sequence =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						} else {
							if (g_front_state.net_runtime_sent_history
									    [search_index]
										    .direct_play_id ==
								    from_id &&
							    g_front_state.net_runtime_sent_history
									    [search_index]
										    .sequence_byte ==
								    expected_direct_sequence) {
								net_send_sequenced_direct_play_packet(
									(int)from_id,
									1,
									expected_direct_sequence,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload_size);
								expected_direct_sequence =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						}
					}
					if ((unsigned int)++search_index >=
					    HISTORY_CAPACITY) {
						search_index = 0;
					}
				} while (--search_count != 0);
				XVT_LOG_DEBUG(
					"network.lobby_keepalive from=%u host=%d",
					(unsigned)from_id,
					g_front_state.net_host_player_id ==
						from_id);
				if (control_value0 !=
					    SEQUENCE_NOTHING_TO_RESEND ||
				    expected_group_sequence !=
					    SEQUENCE_NOTHING_TO_RESEND ||
				    expected_direct_sequence !=
					    SEQUENCE_NOTHING_TO_RESEND) {
					XVT_LOG_WARN(
						"network.lobby_resend_unavailable from=%u broadcast=%d group=%d direct=%d",
						(unsigned)from_id,
						control_value0,
						expected_group_sequence,
						expected_direct_sequence);
				}
				/* Keepalives are unsequenced control packets. */
				continue;
			}

			peer_index = net_find_or_create_peer_slot((int)from_id);
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.last_heard_ms = GetTickCount();

			if (broadcast_channel != 0 && group_channel != 0) {
				if (wire_packet.data[0] != 0) {
					group_channel =
						wire_packet.data[0] == 2;
					broadcast_channel = 0;
				} else {
					group_channel = 0;
					broadcast_channel = 1;
				}
				uint8_t *retransmission_payload =
					wire_packet.data + 1;
				if (has_length) {
					payload_size = (uint32_t)
						net_session_get_fixed_payload_size(
							(int)packet_type);
					if (payload_size == 0) {
#ifdef XVT_MODERN
						uint16_t encoded_size;
						memcpy(&encoded_size,
						       wire_packet.data + 1,
						       sizeof(encoded_size));
						payload_size = encoded_size;
#else
						payload_size = *(
							const uint16_t
								*)(wire_packet
									   .data +
								   1);
#endif
						retransmission_payload =
							wire_packet.data + 1 +
							sizeof(uint16_t);
					}
				} else {
					payload_size =
						wire_size -
						sizeof(wire_packet.header);
				}
				if (payload_size > MAX_PAYLOAD_SIZE) {
					payload_size = MAX_PAYLOAD_SIZE;
				}

				*(uint32_t *)g_front_state
					 .net_runtime_recv_queue
						 [g_front_state
							  .net_runtime_recv_queue_write_index]
					 .payload = packet_type;
				memcpy(g_front_state.net_runtime_recv_queue
						       [g_front_state
								.net_runtime_recv_queue_write_index]
							       .payload +
					       sizeof(packet_type),
				       retransmission_payload, payload_size);
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.direct_play_id = from_id;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.payload_size =
					payload_size + sizeof(packet_type);
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.last_nack_ms = 0;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.nack_retry_count = 0;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.is_resent_copy = 1;
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				if (broadcast_channel != 0) {
					g_front_state
						.net_runtime_recv_queue
							[g_front_state
								 .net_runtime_recv_queue_write_index]
						.packet_class = 0;
					if (g_front_state.net_reliable_peer_slot_count >
						    peer_index &&
					    peer_index < PEER_CAPACITY) {
						unsigned int next_sequence =
							(unsigned int)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_a +
							1;
						if (next_sequence >
						    SEQUENCE_LIMIT) {
							next_sequence = 0;
						}
						if (next_sequence ==
						    (unsigned int)sequence) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_a =
								(int)next_sequence;
						}
					}
				} else if (group_channel != 0) {
					g_front_state
						.net_runtime_recv_queue
							[g_front_state
								 .net_runtime_recv_queue_write_index]
						.packet_class = 2;
					if (g_front_state.net_reliable_peer_slot_count >
						    peer_index &&
					    peer_index < PEER_CAPACITY) {
						unsigned int next_sequence =
							(unsigned int)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_b +
							1;
						if (next_sequence >
						    SEQUENCE_LIMIT) {
							next_sequence = 0;
						}
						if (next_sequence ==
						    (unsigned int)sequence) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_b =
								(int)next_sequence;
						}
					}
				} else {
					g_front_state
						.net_runtime_recv_queue
							[g_front_state
								 .net_runtime_recv_queue_write_index]
						.packet_class = 1;
					if (g_front_state.net_reliable_peer_slot_count >
						    peer_index &&
					    peer_index < PEER_CAPACITY) {
						unsigned int next_sequence =
							(unsigned int)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_default +
							1;
						if (next_sequence >
						    SEQUENCE_LIMIT) {
							next_sequence = 0;
						}
						if (next_sequence ==
						    (unsigned int)sequence) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_default =
								(int)next_sequence;
						}
					}
				}
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.sequence_byte = (uint8_t)sequence;
				XVT_LOG_DEBUG(
					"network.lobby_resent_received from=%u type=%u channel=%d seq=%d bytes=%u",
					(unsigned)from_id, packet_type,
					broadcast_channel != 0 ? 0
					: group_channel != 0   ? 2
							       : 1,
					sequence, (unsigned)payload_size);
				++g_front_state
					  .net_runtime_recv_queue_write_index;
				++g_front_state.net_runtime_recv_queue_count;
				if (g_front_state
					    .net_runtime_recv_queue_write_index >=
				    QUEUE_CAPACITY) {
					g_front_state
						.net_runtime_recv_queue_write_index =
						0;
				}
				continue;
			}

			if (has_length) {
				int previous_sequence = sequence == 0
								? SEQUENCE_LIMIT
								: sequence - 1;
				if (net_check_and_record_incoming_sequence(
					    (int)from_id, previous_sequence,
					    broadcast_channel,
					    group_channel) == 0) {
					if ((unsigned int)g_front_state
						    .net_runtime_reliable_peer_slots
							    [peer_index]
						    .packet_count >
					    PACKET_DROP_WARMUP_COUNT) {
						++g_front_state
							  .net_runtime_reliable_peer_slots
								  [peer_index]
							  .packet_drop_count;
					}
					uint8_t *piggyback_payload =
						(uint8_t *)payload +
						payload_size;
					int previous_packet_type =
						*piggyback_payload++;
					XVT_LOG_DEBUG(
						"network.lobby_packet_missed from=%u seq=%d channel=%d trailer=%u",
						(unsigned)from_id,
						previous_sequence,
						broadcast_channel != 0 ? 0
						: group_channel != 0   ? 2
								       : 1,
						(unsigned)previous_packet_type);
					if (previous_packet_type !=
					    NET_PACKET_NOP) {
						unsigned int piggyback_size =
							wire_size -
							(uint16_t)(piggyback_payload -
								   (uint8_t *)&wire_packet
									   .header);
						if (piggyback_size >
						    MAX_PAYLOAD_SIZE) {
							piggyback_size =
								MAX_PAYLOAD_SIZE;
						}
						*(uint32_t *)g_front_state
							 .net_runtime_recv_queue
								 [g_front_state
									  .net_runtime_recv_queue_write_index]
							 .payload =
							previous_packet_type;
						memcpy(g_front_state.net_runtime_recv_queue
								       [g_front_state
										.net_runtime_recv_queue_write_index]
									       .payload +
							       sizeof(int),
						       piggyback_payload,
						       piggyback_size);
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.direct_play_id =
							from_id;
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.payload_size =
							piggyback_size +
							sizeof(int);
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.last_nack_ms = 0;
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.nack_retry_count = 0;
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.is_resent_copy = 0;
						if (broadcast_channel != 0) {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.packet_class =
								0;
						} else if (group_channel != 0) {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.packet_class =
								2;
						} else {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.packet_class =
								1;
						}
						if (sequence == 0) {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.sequence_byte =
								SEQUENCE_LIMIT;
						} else {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.sequence_byte =
								(uint8_t)(sequence -
									  1);
						}
						++g_front_state
							  .net_runtime_recv_queue_write_index;
						++g_front_state
							  .net_runtime_recv_queue_count;
						if (g_front_state
							    .net_runtime_recv_queue_write_index >=
						    QUEUE_CAPACITY) {
							g_front_state
								.net_runtime_recv_queue_write_index =
								0;
						}
					}
				}
			}

			/* peer_index is reused here as a flag: 1 when the
			 * sequence is stale or out of range (skipped). */
			peer_index = net_check_and_record_incoming_sequence(
				(int)from_id, sequence, broadcast_channel,
				group_channel);
			if (peer_index != 0) {
				XVT_LOG_DEBUG(
					"network.lobby_packet_repeat from=%u type=%u seq=%d channel=%d",
					(unsigned)from_id, packet_type,
					sequence,
					broadcast_channel != 0 ? 0
					: group_channel != 0   ? 2
							       : 1);
				continue;
			}
			payload = (int *)wire_packet.data;
			if (has_length) {
				payload_size = (uint32_t)
					net_session_get_fixed_payload_size(
						(int)packet_type);
				if (payload_size == 0) {
					payload_size =
						*(const uint16_t *)
							 wire_packet.data;
					payload = (int *)(wire_packet.data +
							  sizeof(uint16_t));
				}
			} else {
				payload_size =
					wire_size - sizeof(wire_packet.header);
			}
			if (payload_size > MAX_PAYLOAD_SIZE) {
				payload_size = MAX_PAYLOAD_SIZE;
			}
			*(uint32_t *)g_front_state
				 .net_runtime_recv_queue
					 [g_front_state
						  .net_runtime_recv_queue_write_index]
				 .payload = packet_type;
			memcpy(g_front_state.net_runtime_recv_queue
					       [g_front_state
							.net_runtime_recv_queue_write_index]
						       .payload +
				       sizeof(packet_type),
			       payload, payload_size);
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.direct_play_id = from_id;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.payload_size =
				payload_size + sizeof(packet_type);
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.last_nack_ms = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.nack_retry_count = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.is_resent_copy = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.is_resent_copy = peer_index != 0;
			if (broadcast_channel != 0) {
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.packet_class = 0;
			} else if (group_channel != 0) {
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.packet_class = 2;
			} else {
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.packet_class = 1;
			}
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.sequence_byte = (uint8_t)sequence;
			++g_front_state.net_runtime_recv_queue_write_index;
			++g_front_state.net_runtime_recv_queue_count;
			XVT_LOG_DEBUG(
				"network.lobby_packet_received from=%u type=%u seq=%d channel=%d bytes=%u queued=%d",
				(unsigned)from_id, packet_type, sequence,
				broadcast_channel != 0 ? 0
				: group_channel != 0   ? 2
						       : 1,
				(unsigned)payload_size,
				g_front_state.net_runtime_recv_queue_count);
			if (g_front_state.net_runtime_recv_queue_write_index >=
			    QUEUE_CAPACITY) {
				g_front_state
					.net_runtime_recv_queue_write_index = 0;
			}
		}
	}
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
}

/* Sends a packet through net_send_packet_internal, then a NOP to the same
 * player, whose trailer carries a second copy of the packet at once. Returns
 * the first send's result, or 1 without DirectPlay. The back buffer is
 * unlocked meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4CEF70
int net_send_packet_and_flush(int to_player_id, const void *packet,
			      unsigned int packet_size)
{
	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}
	int back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int result =
		net_send_packet_internal(to_player_id, packet, packet_size);
	int flush_packet = NET_PACKET_NOP;
	net_send_packet_internal(to_player_id, &flush_packet,
				 sizeof(flush_packet));
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return result;
}

/* Sends one game packet on the channel its destination picks: id 0 the
 * broadcast channel, the group's id the group channel, any other id the
 * one-player channel, each with its own sequence counter (the one-player
 * counter in the peer's slot). Outside types 60-63, a type with no fixed
 * size gets a length word, and the previous packet sent on the same channel
 * follows as a trailer (a NOP byte after a reset); every packet then becomes
 * the next trailer. A packet not for the local player goes into the
 * 128-entry sent history. One for the local player, for everyone or for the
 * group, or any packet sent without DirectPlay, is also queued locally as
 * received from the local player (when fewer than 1024 are queued), setting
 * the local peer slot's newest received sequence. Returns 1 without
 * DirectPlay, when Send succeeds or when nothing needs sending, else 0. Does
 * not check packet_size against the 512-byte history and queue entries; with
 * the peer table full, the one-player path uses slot 40, one past the end of
 * the table. */
// FUNCTION: XVT 0x4CEFE0
int net_send_packet_internal(int to_player_id, const void *packet,
			     unsigned int packet_size)
{
	unsigned int packet_type = *(const unsigned int *)packet;
	uint8_t packet_type_byte = packet_type & 0x7F;
	uint16_t packet_header = packet_type_byte;
	int append_pending;
	if (packet_type >= NET_PACKET_RESYNC_CHECKSUMS &&
	    packet_type < NET_PACKET_PROBE_REQUEST) {
		append_pending = 0;
	} else {
		append_pending = 1;
	}
	char debug_text[256];
	struct net_direct_play_encoded_packet encoded_packet;
	uint8_t *encoded_payload;
	unsigned int encoded_size;
	if (to_player_id == 0) {
		sprintf(debug_text, "(SB %u) ",
			g_front_state.net_runtime_broadcast_seq_counter);
		packet_header |=
			(g_front_state.net_runtime_broadcast_seq_counter & 0x7F)
			<< 8;
		++g_front_state.net_runtime_broadcast_seq_counter;
		if (g_front_state.net_runtime_broadcast_seq_counter > 127) {
			g_front_state.net_runtime_broadcast_seq_counter = 0;
		}
		encoded_packet.packet_type_header = packet_header;
		encoded_payload = (uint8_t *)&encoded_packet.payload_size;
		encoded_size = 2;
		if (append_pending &&
		    net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_packet.payload_size = packet_size - 4;
			encoded_payload = encoded_packet.payload;
			encoded_size = 4;
		}
		memcpy(encoded_payload, (const uint8_t *)packet + 4,
		       packet_size - 4);
		encoded_payload += packet_size - 4;
		encoded_size += packet_size - 4;
		if (append_pending) {
			if (g_front_state.net_runtime_broadcast_pending_payload
				    .piggyback_empty != 0) {
				*encoded_payload = NET_PACKET_NOP;
				++encoded_size;
				g_front_state
					.net_runtime_broadcast_pending_payload
					.piggyback_empty = 0;
			} else {
				memcpy(encoded_payload,
				       g_front_state
					       .net_runtime_broadcast_pending_payload
					       .payload,
				       g_front_state
					       .net_runtime_broadcast_pending_payload
					       .payload_length);
				encoded_size +=
					g_front_state
						.net_runtime_broadcast_pending_payload
						.payload_length;
			}
		}
		g_front_state.net_runtime_broadcast_pending_payload.payload[0] =
			packet_type_byte;
		memcpy(g_front_state.net_runtime_broadcast_pending_payload
				       .payload +
			       1,
		       (const uint8_t *)packet + 4, packet_size - 4);
		g_front_state.net_runtime_broadcast_pending_payload
			.payload_length = packet_size - 3;
	} else if (g_front_state.net_group_dplay_id == (DPID)to_player_id) {
		sprintf(debug_text, "(SG %u) ",
			g_front_state.net_runtime_broadcast_seq_counter);
		packet_header |=
			(g_front_state.net_runtime_group_seq_counter & 0x7F)
			<< 8;
		++g_front_state.net_runtime_group_seq_counter;
		packet_header |= 0x8080;
		if (g_front_state.net_runtime_group_seq_counter > 127) {
			g_front_state.net_runtime_group_seq_counter = 0;
		}
		encoded_packet.packet_type_header = packet_header;
		encoded_payload = (uint8_t *)&encoded_packet.payload_size;
		encoded_size = 2;
		if (append_pending &&
		    net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_packet.payload_size = packet_size - 4;
			encoded_payload = encoded_packet.payload;
			encoded_size = 4;
		}
		memcpy(encoded_payload, (const uint8_t *)packet + 4,
		       packet_size - 4);
		encoded_payload += packet_size - 4;
		encoded_size += packet_size - 4;
		if (append_pending) {
			if (g_front_state.net_runtime_group_pending_payload
				    .piggyback_empty != 0) {
				*encoded_payload = NET_PACKET_NOP;
				++encoded_size;
				g_front_state.net_runtime_group_pending_payload
					.piggyback_empty = 0;
			} else {
				memcpy(encoded_payload,
				       g_front_state
					       .net_runtime_group_pending_payload
					       .payload,
				       g_front_state
					       .net_runtime_group_pending_payload
					       .payload_length);
				encoded_size +=
					g_front_state
						.net_runtime_group_pending_payload
						.payload_length;
			}
		}
		g_front_state.net_runtime_group_pending_payload.payload[0] =
			packet_type_byte;
		memcpy(g_front_state.net_runtime_group_pending_payload.payload +
			       1,
		       (const uint8_t *)packet + 4, packet_size - 4);
		g_front_state.net_runtime_group_pending_payload.payload_length =
			packet_size - 3;
	} else {
		unsigned int peer_index =
			net_find_or_create_peer_slot(to_player_id);
		if (g_front_state.net_reliable_peer_slot_count > peer_index &&
		    peer_index < 40) {
			sprintf(debug_text, "(SS %u) ",
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.send_seq);
			int send_sequence =
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.send_seq;
			packet_header |= (send_sequence++ & 0x7F) << 8;
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.send_seq = send_sequence;
			if (send_sequence > 127) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.send_seq = 0;
			}
		}
		packet_header |= 0x8000;
		encoded_packet.packet_type_header = packet_header;
		encoded_payload = (uint8_t *)&encoded_packet.payload_size;
		encoded_size = 2;
		if (append_pending &&
		    net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_packet.payload_size = packet_size - 4;
			encoded_payload = encoded_packet.payload;
			encoded_size = 4;
		}
		memcpy(encoded_payload, (const uint8_t *)packet + 4,
		       packet_size - 4);
		encoded_payload += packet_size - 4;
		encoded_size += packet_size - 4;
		if (append_pending) {
			memcpy(encoded_payload,
			       &g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_piggyback_type,
			       g_front_state
				       .net_runtime_reliable_peer_slots
					       [peer_index]
				       .piggyback_length);
			encoded_size += g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.piggyback_length;
		}
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.last_piggyback_type = packet_type_byte;
		memcpy(g_front_state.net_runtime_reliable_peer_slots[peer_index]
			       .piggyback_payload,
		       (const uint8_t *)packet + 4, packet_size - 4);
		g_front_state.net_runtime_reliable_peer_slots[peer_index]
			.piggyback_length = packet_size - 3;
	}

	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)to_player_id) {
		memcpy(g_front_state
			       .net_runtime_sent_history
				       [g_front_state
						.net_runtime_sent_history_write_index]
			       .payload,
		       packet, packet_size);
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.direct_play_id = to_player_id;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.payload_size = packet_size;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.last_nack_ms = 0;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.nack_retry_count = 0;
		if (to_player_id == 0) {
			g_front_state
				.net_runtime_sent_history
					[g_front_state
						 .net_runtime_sent_history_write_index]
				.packet_class = 0;
		} else if (g_front_state.net_group_dplay_id ==
			   (DPID)to_player_id) {
			g_front_state
				.net_runtime_sent_history
					[g_front_state
						 .net_runtime_sent_history_write_index]
				.packet_class = 2;
		} else {
			g_front_state
				.net_runtime_sent_history
					[g_front_state
						 .net_runtime_sent_history_write_index]
				.packet_class = 1;
		}
		packet_header = encoded_packet.packet_type_header;
		g_front_state
			.net_runtime_sent_history
				[g_front_state
					 .net_runtime_sent_history_write_index]
			.sequence_byte = (packet_header & 0x7F00) >> 8;
		++g_front_state.net_runtime_sent_history_write_index;
		if (g_front_state.net_runtime_sent_history_write_index >= 128) {
			g_front_state.net_runtime_sent_history_write_index = 0;
		}
	}
	XVT_LOG_DEBUG(
		"network.lobby_packet_sent to=%u type=%u channel=%d seq=%d bytes=%u queued=%d",
		(unsigned)to_player_id, packet_type,
		to_player_id == 0					 ? 0
		: g_front_state.net_group_dplay_id == (DPID)to_player_id ? 2
									 : 1,
		(encoded_packet.packet_type_header & 0x7F00) >> 8, encoded_size,
		g_front_state.net_runtime_recv_queue_count);

	if ((g_front_state.net_runtime_local_player.player_id ==
		     (DPID)to_player_id ||
	     to_player_id == 0 || g_front_state.net_direct_play == NULL ||
	     g_front_state.net_group_dplay_id == (DPID)to_player_id) &&
	    g_front_state.net_runtime_recv_queue_count < 1024) {
		memcpy(g_front_state
			       .net_runtime_recv_queue
				       [g_front_state
						.net_runtime_recv_queue_write_index]
			       .payload,
		       packet, packet_size);
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.direct_play_id =
			g_front_state.net_runtime_local_player.player_id;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.payload_size = packet_size;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.last_nack_ms = 0;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.nack_retry_count = 0;
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.is_resent_copy = 0;
		unsigned int peer_index = net_find_or_create_peer_slot(
			g_front_state.net_runtime_local_player.player_id);
		if (to_player_id == 0) {
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = 0;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    peer_index < 40) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_channel_a =
					(packet_header & 0x7F00) >> 8;
			}
		} else if (g_front_state.net_group_dplay_id ==
			   (DPID)to_player_id) {
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = 2;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    peer_index < 40) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_channel_b =
					(packet_header & 0x7F00) >> 8;
			}
		} else {
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = 1;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    peer_index < 40) {
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_default =
					(packet_header & 0x7F00) >> 8;
			}
		}
		g_front_state
			.net_runtime_recv_queue
				[g_front_state
					 .net_runtime_recv_queue_write_index]
			.sequence_byte =
			(encoded_packet.packet_type_header & 0x7F00) >> 8;
		++g_front_state.net_runtime_recv_queue_count;
		++g_front_state.net_runtime_recv_queue_write_index;
		if (g_front_state.net_runtime_recv_queue_write_index >= 1024) {
			g_front_state.net_runtime_recv_queue_write_index = 0;
		}
	} else if (g_front_state.net_runtime_local_player.player_id ==
			   (DPID)to_player_id ||
		   to_player_id == 0 || g_front_state.net_direct_play == NULL ||
		   g_front_state.net_group_dplay_id == (DPID)to_player_id) {
		XVT_LOG_WARN(
			"network.lobby_local_copy_dropped to=%u type=%u queued=%d",
			(unsigned)to_player_id, packet_type,
			g_front_state.net_runtime_recv_queue_count);
	}

	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}
	int send_result = 0;
	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)to_player_id) {
		send_result = g_front_state.net_direct_play->lpVtbl->Send(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id,
			to_player_id, 0, &encoded_packet.packet_type_header,
			encoded_size);
	}
	if (send_result != 0) {
		char error_text[80];
		sprintf(error_text, "Send Returned: %-8x\n", send_result);
		XVT_LOG_WARN(
			"network.lobby_send_failed to=%u type=%u result=%#x",
			(unsigned)to_player_id, packet_type,
			(unsigned)send_result);
	}
	return send_result == 0;
}

/* Sends one packet outside the sequence scheme: sequence 0, no sent-history
 * entry, no local copy. A packet to the group carries the group bits; any
 * other carries its type alone, since the one-player mode (delivery_mode 1)
 * is never chosen. Outside types 60-63 a type with no fixed size gets a
 * length word, and every packet a NOP trailer. Nothing is sent to the local
 * player. Returns 1 without DirectPlay, when Send succeeds or when nothing
 * is sent, else 0. The last argument is ignored. */
// FUNCTION: XVT 0x4CF830
int net_send_direct_play_packet(int dest_player_id, const void *packet,
				int packet_size, int unused_send_mode)
{
	(void)unused_send_mode;

	HRESULT send_result = 0;
	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}

	uint32_t packet_type = *(const uint32_t *)packet;
	uint16_t packet_flags = (uint8_t)packet_type & 0x7F;
	int append_terminator = packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_PROBE_REQUEST;
	int delivery_mode = 0;
	if (dest_player_id != 0) {
		delivery_mode =
			g_front_state.net_group_dplay_id == (DPID)dest_player_id
				? 2
				: 0;
	}
	if (delivery_mode == 2) {
		packet_flags |= 0x8080;
	} else if (delivery_mode == 1) {
		packet_flags |= 0x8000;
	}

	struct net_direct_play_encoded_packet encoded_packet;
	encoded_packet.packet_type_header = packet_flags;
	uint8_t *encoded_payload = (uint8_t *)&encoded_packet.payload_size;
	int encoded_header_size = 2;
	if (append_terminator) {
		if (net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_payload = encoded_packet.payload;
			encoded_packet.payload_size = packet_size - 4;
			encoded_header_size = 4;
		}
	}
	memcpy(encoded_payload, (const uint8_t *)packet + 4, packet_size - 4);
	encoded_payload += packet_size - 4;
	int encoded_size = packet_size + encoded_header_size - 4;
	if (append_terminator) {
		++encoded_size;
		*encoded_payload = NET_PACKET_NOP;
	}
	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)dest_player_id) {
		send_result = g_front_state.net_direct_play->lpVtbl->Send(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id,
			dest_player_id, 0, &encoded_packet.packet_type_header,
			encoded_size);
	}
	XVT_LOG_DEBUG("network.lobby_control_sent to=%u type=%u bytes=%d",
		      (unsigned)dest_player_id, (unsigned)packet_type,
		      encoded_size);
	if (send_result != 0) {
		XVT_LOG_WARN(
			"network.lobby_control_send_failed to=%u type=%u result=%#x",
			(unsigned)dest_player_id, (unsigned)packet_type,
			(unsigned)send_result);
	}
	return send_result == 0;
}

/* Resends a packet with a given channel (packet_class) and sequence: the
 * header carries the resend bits and a channel byte follows. Outside types
 * 60-63 a type with no fixed size gets a length word, and every packet a NOP
 * trailer. For the local player it queues the packet locally as a resent
 * copy instead (when fewer than 1024 are queued). Returns 1 without
 * DirectPlay, when Send succeeds, or for the local player; else 0. */
// FUNCTION: XVT 0x4CF980
int net_send_sequenced_direct_play_packet(int dest_player_id, int packet_class,
					  int sequence_id, const void *packet,
					  unsigned int packet_size)
{
	int send_result = 0;
	if (g_front_state.net_direct_play == NULL) {
		return 1;
	}

	unsigned int packet_type = *(const uint32_t *)packet;
	uint8_t packet_type_byte = (uint8_t)packet_type & 0x7F;
	int append_terminator = packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_PROBE_REQUEST;
	char debug_text[256];
	switch (packet_class) {
	case 0:
		sprintf(debug_text, "(RSB %u) ", sequence_id);
		break;
	case 2:
		sprintf(debug_text, "(RSG %u) ", sequence_id);
		break;
	default:
		sprintf(debug_text, "(RSS %u) ", sequence_id);
	}

	struct net_direct_play_sequenced_packet encoded_packet;
	uint8_t *encoded_payload = (uint8_t *)&encoded_packet.payload_size;
	int encoded_header_size = 3;
	encoded_packet.packet_type_header =
		(int16_t)(((((sequence_id & 0x7F) << 8) | packet_type_byte) &
			   0x7F7F) |
			  0x80);
	encoded_packet.packet_class = (uint8_t)packet_class;
	if (append_terminator) {
		if (net_session_get_fixed_payload_size(packet_type) == 0) {
			encoded_payload = encoded_packet.payload;
			encoded_packet.payload_size =
				(int16_t)(packet_size - 4);
			encoded_header_size = 5;
		}
	}

	memcpy(encoded_payload, (const uint8_t *)packet + 4, packet_size - 4);
	encoded_payload += packet_size - 4;
	unsigned int encoded_size = packet_size + encoded_header_size - 4;
	if (append_terminator) {
		++encoded_size;
		*encoded_payload = NET_PACKET_NOP;
	}

	if (g_front_state.net_runtime_local_player.player_id !=
	    (DPID)dest_player_id) {
		send_result = g_front_state.net_direct_play->lpVtbl->Send(
			g_front_state.net_direct_play,
			g_front_state.net_runtime_local_player.player_id,
			dest_player_id, 0, &encoded_packet.packet_type_header,
			encoded_size);
	} else {
		if (g_front_state.net_runtime_recv_queue_count < 1024) {
			memcpy(g_front_state
				       .net_runtime_recv_queue
					       [g_front_state
							.net_runtime_recv_queue_write_index]
				       .payload,
			       packet, packet_size);
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.direct_play_id =
				g_front_state.net_runtime_local_player
					.player_id;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.payload_size = packet_size;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = (uint8_t)packet_class;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.sequence_byte = (uint8_t)sequence_id;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.is_resent_copy = 1;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.last_nack_ms = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.nack_retry_count = 0;
			++g_front_state.net_runtime_recv_queue_count;
			++g_front_state.net_runtime_recv_queue_write_index;
			if (g_front_state.net_runtime_recv_queue_write_index >=
			    1024) {
				g_front_state
					.net_runtime_recv_queue_write_index = 0;
			}
		} else {
			XVT_LOG_WARN(
				"network.lobby_local_copy_dropped to=%u type=%u queued=%d",
				(unsigned)dest_player_id, packet_type,
				g_front_state.net_runtime_recv_queue_count);
		}
	}
	XVT_LOG_DEBUG(
		"network.lobby_packet_resent to=%u type=%u channel=%d seq=%d bytes=%u result=%#x",
		(unsigned)dest_player_id, packet_type, packet_class,
		sequence_id, encoded_size, (unsigned)send_result);
	return send_result == 0;
}

#ifndef XVT_MODERN
/* Lists into out_sessions, sorted by name, up to max_sessions DirectPlay
 * sessions of the application whose GUID the first four arguments make, and
 * returns how many. Without a DirectPlay interface it creates one for
 * network_type; it returns 0 when the provider is unknown, or after a warning
 * box when creation fails, and does not check that QueryInterface succeeded.
 * Afterwards it releases g_front_state.net_direct_play in every case, even one
 * it did not create. Writes g_net_enum_session_capacity and
 * g_net_enum_session_count. The back buffer is unlocked meanwhile and relocked
 * when it was locked. Only the original build calls this. */
// FUNCTION: XVT 0x4CFC20
int net_enumerate_app_sessions(unsigned int app_guid0, unsigned int app_guid1,
			       unsigned int app_guid2, unsigned int app_guid3,
			       struct net_session_enum_entry *out_sessions,
			       int max_sessions,
			       network_transport_type network_type)
{
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	if (g_front_state.net_direct_play == NULL) {
		const GUID *service_provider_guid =
			net_get_direct_play_service_provider_guid(network_type);
		if (service_provider_guid == NULL) {
			if (was_back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return 0;
		}
		if (DirectPlayCreate(service_provider_guid,
				     &g_front_state.net_temp_direct_play,
				     NULL) != 0) {
			char error_message[256];
			if (error_text_load_line(6, error_message) == 0) {
				frontend_display_show_game_message_box(
					"WARNING:  Connection failure!\n"
					"\n"
					"Make sure your Windows 95 network\n"
					"settings are properly configured\n"
					"for this type of network game.\n"
					"\n"
					"Press Enter to continue.");
			} else {
				frontend_display_show_game_message_box(
					error_message);
			}
			if (was_back_buffer_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return 0;
		}
		g_front_state.net_temp_direct_play->lpVtbl->QueryInterface(
			g_front_state.net_temp_direct_play, &IID_IDirectPlay2A,
			(void **)&g_front_state.net_direct_play);
		g_front_state.net_temp_direct_play->lpVtbl->Release(
			g_front_state.net_temp_direct_play);
		g_front_state.net_temp_direct_play = NULL;
	}

	g_net_enum_session_capacity = max_sessions;
	g_net_enum_session_count = 0;
	DPSESSIONDESC2 session_desc;
	memset(&session_desc, 0, sizeof(session_desc));
	session_desc.dwSize = sizeof(session_desc);
	session_desc.guidApplication = *(const GUID *)&app_guid0;
	g_front_state.net_direct_play->lpVtbl->EnumSessions(
		g_front_state.net_direct_play, &session_desc, 0,
		net_enumerate_app_sessions_callback, out_sessions, 1);
	g_front_state.net_direct_play->lpVtbl->Release(
		g_front_state.net_direct_play);
	g_front_state.net_direct_play = NULL;
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	qsort(out_sessions, g_net_enum_session_count, sizeof(*out_sessions),
	      (int (*)(const void *,
		       const void *))net_compare_session_enum_entries_by_name);
	return g_net_enum_session_count;
}
#endif

#ifndef XVT_MODERN
/* Session enumeration callback: while g_net_enum_session_count is below
 * g_net_enum_session_capacity, copies the session's name, cut to 31
 * characters, and instance GUID into the next entry of the array in
 * userData, raises the count and returns 1 to go on. Returns 0 to stop when
 * the array is full, on a timeout or for a NULL description. Only the
 * original build calls this. */
// FUNCTION: XVT 0x4CFDB0
int AERON_DXAPI net_enumerate_app_sessions_callback(
	const DPSESSIONDESC2 *session_desc, uint32_t *timeout_ms,
	uint32_t flags, void *user_data)
{
	struct net_session_enum_entry *out_sessions = user_data;

	(void)timeout_ms;

	if (session_desc == NULL || (flags & 1) != 0) {
		return 0;
	}
	if (g_net_enum_session_capacity > g_net_enum_session_count) {
		strncpy(out_sessions[g_net_enum_session_count].session_name,
			session_desc->lpszSessionNameA,
			sizeof(out_sessions[g_net_enum_session_count]
				       .session_name));
		out_sessions[g_net_enum_session_count].session_name
			[sizeof(out_sessions[g_net_enum_session_count]
					.session_name) -
			 1] = '\0';
		memcpy(&out_sessions[g_net_enum_session_count].session_guid,
		       &session_desc->guidInstance,
		       sizeof(out_sessions[g_net_enum_session_count]
				      .session_guid));
		++g_net_enum_session_count;
		return 1;
	}

	return 0;
}
#endif

#ifndef XVT_MODERN
/* qsort comparison of two session entries by name, as strcmp orders them.
 * Only the original build calls this. */
// FUNCTION: XVT 0x4CFE50
int net_compare_session_enum_entries_by_name(
	const struct net_session_enum_entry *lhs,
	const struct net_session_enum_entry *rhs)
{
	return strcmp(lhs->session_name, rhs->session_name);
}
#endif

/* Returns the lobby roster, g_front_state.net_players, with its count in
 * *outCount. */
// FUNCTION: XVT 0x4CFE80
struct net_player_info *net_get_player_roster(int *out_count)
{
	*out_count = g_front_state.net_player_count;
	return g_front_state.net_players;
}

/* Returns the lobby roster count, or 1 when it is 0. Only the original build
 * calls this. */
// FUNCTION: XVT 0x4CFEA0
int net_get_player_count(void)
{
	if (g_front_state.net_player_count == 0) {
		return 1;
	}
	return g_front_state.net_player_count;
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

/* Acts on a DirectPlay system message from the lobby queue. A player
 * created: the host, for a player other than itself, refreshes the roster
 * and sends the new player a NET_PACKET_SEQUENCE_STATUS with the player
 * count, the peer slot table and its time in ms; a client refreshes the
 * roster. A player destroyed: in the modern build, the host's departure
 * calls xvt_network_session_host_lost. The host clears the leaver's ready flag,
 * setting g_front_state.net_ready_player_left_this_frame when it was set, frees
 * its peer slot by moving the last slot into it, and clears its
 * g_net_player_connection_stats entry; a client whose host left queues a
 * NET_PACKET_HOST_CANCELLED to itself. The roster is then refreshed. A
 * player renamed: its roster entry takes the new short and long names, cut
 * to 12 characters. The original build copies them with strcpy, unbounded,
 * into 16-byte fields; the modern build bounds them
 * (xvt_network_session_copy_player_names) and skips a malformed message. */
// FUNCTION: XVT 0x4D00D0
void net_handle_direct_play_system_message(int packet_type,
					   const void *packet_data)
{
	enum {
		SEQUENCE_STATUS_PACKET_SIZE = 512,
		SEQUENCE_INITIAL_VALUE = 127,
		PLAYER_NAME_TRUNCATION_INDEX = 12,
	};

	const int *packet_words = (const int *)packet_data;
	XVT_LOG_DEBUG(
		"network.lobby_system_handled type=%d kind=%d player=%u host=%d",
		packet_type, packet_words[1], (unsigned)packet_words[2],
		g_front_state.net_is_host);

	switch (packet_type) {
	case DPSYS_CREATEPLAYERORGROUP: {
		if (g_front_state.net_is_host != 0) {
			if (packet_words[1] == DPPLAYERTYPE_PLAYER &&
			    packet_words[2] !=
				    (int)g_front_state.net_host_player_id) {
				struct net_sequence_status_record {
					int player_id;
					uint8_t previous_channel_a;
					uint8_t previous_channel_b;
					uint8_t channel_a;
					uint8_t channel_b;
				};

				struct net_sequence_status_packet {
					int packet_type;
					int player_count;
					int peer_slot_count;
					uint32_t timestamp_ms;
					struct net_sequence_status_record records
						[(SEQUENCE_STATUS_PACKET_SIZE -
						  4 * sizeof(int)) /
						 sizeof(struct
							net_sequence_status_record)];
				};

				g_front_state.net_player_count = 1;
				net_refresh_player_roster();
				struct net_sequence_status_packet status_packet;
				status_packet.player_count =
					g_front_state.net_player_count;
				status_packet.peer_slot_count =
					g_front_state
						.net_reliable_peer_slot_count;
				status_packet.packet_type =
					NET_PACKET_SEQUENCE_STATUS;
				uint32_t now_ms = GetTickCount();
				unsigned int peer_index = 0;
				struct net_sequence_status_record
					*status_records = status_packet.records;
				status_packet.timestamp_ms = now_ms;
				if (g_front_state.net_reliable_peer_slot_count >
				    0) {
					do {
						const struct net_reliable_peer_slot
							*peer = &g_front_state.net_runtime_reliable_peer_slots
									 [peer_index];
						struct net_sequence_status_record
							*record =
								&status_records
									[peer_index];
						record->player_id =
							peer->direct_play_id;
						record->previous_channel_a =
							(uint8_t)peer
								->last_delivered_seq_channel_a;
						record->previous_channel_b =
							(uint8_t)peer
								->last_delivered_seq_channel_b;
						record->channel_a =
							(uint8_t)peer
								->recv_seq_channel_a;
						record->channel_b =
							(uint8_t)peer
								->recv_seq_channel_b;
						++peer_index;
					} while (
						g_front_state
							.net_reliable_peer_slot_count >
						peer_index);
				}
				net_send_packet_and_flush(
					packet_words[2], &status_packet,
					(unsigned int)(sizeof(status_packet
								      .packet_type) +
						       g_front_state.net_reliable_peer_slot_count *
							       sizeof(status_packet
									      .records[0]) +
						       3 * sizeof(int)));
				XVT_LOG_INFO(
					"network.lobby_player_joined player=%u players=%d peers=%u",
					(unsigned)packet_words[2],
					g_front_state.net_player_count,
					(unsigned)g_front_state
						.net_reliable_peer_slot_count);
			}
		} else {
			g_front_state.net_player_count = 1;
			net_refresh_player_roster();
		}
		break;
	}
	case DPSYS_DESTROYPLAYERORGROUP: {
#ifdef XVT_MODERN
		if (packet_words[1] == DPPLAYERTYPE_PLAYER &&
		    (DPID)packet_words[2] == g_front_state.net_host_player_id) {
			xvt_network_session_host_lost();
		}
#endif
		if (g_front_state.net_is_host != 0) {
			if (packet_words[1] == DPPLAYERTYPE_PLAYER) {
				unsigned int player_index;

				for (player_index = 0;
				     player_index < (unsigned int)g_front_state
							    .net_player_count;
				     ++player_index) {
					if (g_front_state
						    .net_players[player_index]
						    .player_id ==
					    (DPID)packet_words[2]) {
						if (g_front_state
							    .net_players
								    [player_index]
							    .ready_flag != 0) {
							g_front_state
								.net_ready_player_left_this_frame =
								1;
						}
						break;
					}
				}
				net_clear_player_ready_flag_with_lock_guard(
					(DPID)packet_words[2]);
				for (unsigned int peer_index = 0;
				     peer_index <
				     g_front_state.net_reliable_peer_slot_count;
				     ++peer_index) {
					if (g_front_state
						    .net_runtime_reliable_peer_slots
							    [peer_index]
						    .direct_play_id ==
					    (DPID)packet_words[2]) {
						--g_front_state
							  .net_reliable_peer_slot_count;
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index] =
							g_front_state.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count];
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.direct_play_id = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.last_delivered_seq_default =
							SEQUENCE_INITIAL_VALUE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.last_delivered_seq_channel_a =
							SEQUENCE_INITIAL_VALUE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.last_delivered_seq_channel_b =
							SEQUENCE_INITIAL_VALUE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.recv_seq_default =
							SEQUENCE_INITIAL_VALUE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.recv_seq_channel_a =
							SEQUENCE_INITIAL_VALUE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.recv_seq_channel_b =
							SEQUENCE_INITIAL_VALUE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.send_seq = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.last_piggyback_type =
							NET_PACKET_NOP;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.piggyback_length = 1;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.last_heard_ms = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.last_activity_ms = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.packet_count = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.packet_drop_count = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[g_front_state
									 .net_reliable_peer_slot_count]
							.packet_retry_count = 0;
						break;
					}
				}
				for (player_index = 0;
				     player_index <
				     (unsigned int)(sizeof(g_net_player_connection_stats) /
						    sizeof(g_net_player_connection_stats
								   [0]));
				     ++player_index) {
					if (g_net_player_connection_stats
						    [player_index]
							    .player_id ==
					    packet_words[2]) {
						g_net_player_connection_stats
							[player_index]
								.player_id = 0;
						g_net_player_connection_stats
							[player_index]
								.latency_total_ms =
							0;
						g_net_player_connection_stats
							[player_index]
								.packet_count =
							0;
						g_net_player_connection_stats
							[player_index]
								.packet_drop_count =
							0;
						g_net_player_connection_stats
							[player_index]
								.packet_retry_count =
							0;
						g_net_player_connection_stats
							[player_index]
								.latency_sample_count =
							0;
						break;
					}
				}
				XVT_LOG_INFO(
					"network.lobby_player_left player=%u ready=%d peers=%u",
					(unsigned)packet_words[2],
					g_front_state
						.net_ready_player_left_this_frame,
					(unsigned)g_front_state
						.net_reliable_peer_slot_count);
			}
		} else if (packet_words[1] == DPPLAYERTYPE_PLAYER &&
			   (DPID)packet_words[2] ==
				   g_front_state.net_host_player_id) {
			const int host_cancelled_packet =
				NET_PACKET_HOST_CANCELLED;
			net_send_packet_and_flush(
				g_front_state.net_runtime_local_player
					.player_id,
				&host_cancelled_packet,
				sizeof(host_cancelled_packet));
			XVT_LOG_DEBUG(
				"network.lobby_host_cancel_queued host=%u",
				(unsigned)packet_words[2]);
		}
		g_front_state.net_player_count = 1;
		net_refresh_player_roster();
		break;
	}
	case DPSYS_SETPLAYERORGROUPNAME:
		if (((const struct net_player_name_message *)packet_data)
			    ->header.dwPlayerType == DPPLAYERTYPE_PLAYER) {
			for (unsigned int player_index = 0;
			     player_index <
			     (unsigned int)g_front_state.net_player_count;
			     ++player_index) {
				if (g_front_state.net_players[player_index]
					    .player_id ==
				    ((const struct net_player_name_message *)
					     packet_data)
					    ->header.dpId) {
#ifdef XVT_MODERN
					if (!xvt_network_session_copy_player_names(
						    (const struct
						     net_player_name_message *)
							    packet_data,
						    g_front_state
							    .net_players
								    [player_index]
							    .player_name,
						    sizeof(g_front_state
								   .net_players
									   [player_index]
								   .player_name),
						    g_front_state
							    .net_players
								    [player_index]
							    .long_name,
						    sizeof(g_front_state
								   .net_players
									   [player_index]
								   .long_name))) {
						XVT_LOG_WARN(
							"network.lobby_rename_rejected player=%u",
							(unsigned)g_front_state
								.net_players
									[player_index]
								.player_id);
						continue;
					}
#else
					strcpy(g_front_state
						       .net_players
							       [player_index]
						       .player_name,
					       ((const struct
						 net_player_name_message *)
							packet_data)
						       ->names);
					strcpy(g_front_state
						       .net_players
							       [player_index]
						       .long_name,
					       &((const struct
						  net_player_name_message *)
							 packet_data)
							->names[strlen(g_front_state
									       .net_players
										       [player_index]
									       .player_name) +
								1]);
#endif
					g_front_state.net_players[player_index].player_name
						[PLAYER_NAME_TRUNCATION_INDEX] =
						'\0';
					g_front_state.net_players[player_index].long_name
						[PLAYER_NAME_TRUNCATION_INDEX] =
						'\0';
					XVT_LOG_DEBUG(
						"network.lobby_player_renamed player=%u name=\"%s\"",
						(unsigned)g_front_state
							.net_players
								[player_index]
							.player_id,
						g_front_state
							.net_players
								[player_index]
							.player_name);
				}
			}
		}
		break;
	default:
		break;
	}
}

/* Hands out the next lobby packet in sequence order, or NULL. It first pumps
 * incoming packets and sends due keepalives. A DirectPlay system message is
 * returned only from the head of the queue. Packets of types below 51
 * (flight types) are dropped, their sequence counted as delivered. A packet
 * that is next on its channel is delivered. One that leaves a gap asks its
 * sender for the missing ones with a NACK, then again each second up to 20
 * more times (in the long-timeout mode it waits 20 seconds and asks no
 * more), and then skips the gap, delivering the first queued packet after
 * it; a resent copy that arrives in time fills the gap. Per call it stops
 * looking at a peer's packets once more than 90 of them, less the size of
 * each gap found, have been seen. Stale packets go when they reach the head
 * of the queue; packets from a sender that gets no peer slot (the table is
 * full) go at once. When 1023 or more stay queued, stale ones are dropped
 * and the first one already asked about is delivered past its gap. It
 * returns g_front_state.net_runtime_recv_scratch_packet.payload, valid until the
 * next call, with the sender and size in the out arguments. Updates the
 * peer slots' delivered sequences, counts and times. */
// FUNCTION: XVT 0x4D0540
void *net_dequeue_incoming_packet(DPID *out_sender_id,
				  uint32_t *out_packet_size)
{
	enum {
		NET_RECV_QUEUE_CAPACITY = 1024,
		NET_RECV_QUEUE_PRESSURE_THRESHOLD = NET_RECV_QUEUE_CAPACITY - 1,
		NET_RELIABLE_PEER_CAPACITY = 40,
		NET_RELIABLE_CHANNEL_COUNT = 3,
		NET_RELIABLE_CHANNEL_A = 0,
		NET_RELIABLE_CHANNEL_DEFAULT = 1,
		NET_RELIABLE_CHANNEL_B = 2,
		NET_RELIABLE_SEQUENCE_LIMIT = 127,
		NET_RELIABLE_SEQUENCE_COUNT = NET_RELIABLE_SEQUENCE_LIMIT + 1,
		NET_FIRST_NON_FLIGHT_PACKET_TYPE = NET_PACKET_WORLD_NACK,
		NET_RELIABLE_REORDER_ALLOWANCE = 90,
		NET_RELIABLE_NEGATIVE_WINDOW = -28,
		NET_RELIABLE_PRESSURE_NEGATIVE_WINDOW = -27,
		NET_RELIABLE_POSITIVE_WINDOW = 100,
		NET_RELIABLE_RETRY_PACKET_WORD_COUNT = 3,
		NET_RELIABLE_RETRY_COUNT_THRESHOLD = 20,
		NET_RELIABLE_SHORT_RETRY_LIMIT = 20,
		NET_RELIABLE_SHORT_RETRY_TIMEOUT_MS = 1000,
		NET_RELIABLE_LONG_RETRY_LIMIT = 0,
		NET_RELIABLE_LONG_RETRY_TIMEOUT_MS = 20000
	};

	net_pump_incoming_packets();
	net_send_sequence_keepalives();
	if (g_front_state.net_runtime_recv_queue_count == 0) {
		return NULL;
	}

	uint8_t processed_packet_counts[NET_RELIABLE_PEER_CAPACITY];
	memset(processed_packet_counts, 0, sizeof(processed_packet_counts));
	uint8_t reorder_allowances[NET_RELIABLE_PEER_CAPACITY];
	memset(reorder_allowances, NET_RELIABLE_REORDER_ALLOWANCE,
	       sizeof(reorder_allowances));
	uint8_t last_seen_sequences[NET_RELIABLE_PEER_CAPACITY]
				   [NET_RELIABLE_CHANNEL_COUNT];
	memset(last_seen_sequences, 0, sizeof(last_seen_sequences));
	int channel_index;
	/* channel_index first walks the peer slots; the test below compares the
	 * slot count it is left at with peer_index to spot a new slot. Only
	 * later does it hold a channel. */
	for (channel_index = 0;
	     channel_index < (int)g_front_state.net_reliable_peer_slot_count;
	     ++channel_index) {
		last_seen_sequences[channel_index][NET_RELIABLE_CHANNEL_A] =
			(uint8_t)g_front_state
				.net_runtime_reliable_peer_slots[channel_index]
				.last_delivered_seq_channel_a;
		last_seen_sequences
			[channel_index][NET_RELIABLE_CHANNEL_DEFAULT] =
				(uint8_t)g_front_state
					.net_runtime_reliable_peer_slots
						[channel_index]
					.last_delivered_seq_default;
		last_seen_sequences[channel_index][NET_RELIABLE_CHANNEL_B] =
			(uint8_t)g_front_state
				.net_runtime_reliable_peer_slots[channel_index]
				.last_delivered_seq_channel_b;
	}

	int scan_index = g_front_state.net_runtime_recv_queue_read_index;
	int remaining_packet_count = g_front_state.net_runtime_recv_queue_count;
	int received_sequence;
	int expected_sequence = 0;
	int use_channel_a;
	int use_channel_b;
	char debug_text[256];
	uint32_t retry_packet[128];
	unsigned int peer_index;
	uint32_t retry_timeout_ms;
	int sequence_delta;
	for (; remaining_packet_count > 0; --remaining_packet_count) {
		if (g_front_state.net_runtime_recv_queue[scan_index]
			    .direct_play_id == 0) {
			if (g_front_state.net_runtime_recv_queue_read_index !=
			    scan_index) {
				if (++scan_index >= NET_RECV_QUEUE_CAPACITY) {
					scan_index = 0;
				}
				continue;
			}
			{
				g_front_state.net_runtime_recv_scratch_packet =
					g_front_state.net_runtime_recv_queue
						[scan_index];
				*out_sender_id = g_front_state
							 .net_runtime_recv_queue
								 [scan_index]
							 .direct_play_id;
				*out_packet_size =
					g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.payload_size;
				--g_front_state.net_runtime_recv_queue_count;
				++g_front_state
					  .net_runtime_recv_queue_read_index;
				if (g_front_state
					    .net_runtime_recv_queue_read_index >=
				    NET_RECV_QUEUE_CAPACITY) {
					g_front_state
						.net_runtime_recv_queue_read_index =
						0;
				}
				return g_front_state
					.net_runtime_recv_scratch_packet
					.payload;
			}
		} else {
			received_sequence =
				g_front_state.net_runtime_recv_queue[scan_index]
					.sequence_byte;
			use_channel_a =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_A;
			use_channel_b =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_B;
			int packet_type =
				*(const int *)g_front_state
					 .net_runtime_recv_queue[scan_index]
					 .payload;
			peer_index = net_find_or_create_peer_slot(
				(int)g_front_state
					.net_runtime_recv_queue[scan_index]
					.direct_play_id);
			if (channel_index == (int)peer_index &&
			    peer_index < NET_RELIABLE_PEER_CAPACITY) {
				last_seen_sequences
					[peer_index][NET_RELIABLE_CHANNEL_A] =
						NET_RELIABLE_SEQUENCE_LIMIT;
				last_seen_sequences
					[peer_index]
					[NET_RELIABLE_CHANNEL_DEFAULT] =
						NET_RELIABLE_SEQUENCE_LIMIT;
				last_seen_sequences
					[peer_index][NET_RELIABLE_CHANNEL_B] =
						NET_RELIABLE_SEQUENCE_LIMIT;
			}

			if (g_front_state.net_reliable_peer_slot_count >
			    peer_index) {
				if ((unsigned int)packet_type <
				    NET_FIRST_NON_FLIGHT_PACKET_TYPE) {
					if (g_front_state
						    .net_runtime_recv_queue
							    [scan_index]
						    .is_resent_copy == 0) {
						if (use_channel_a != 0) {
							last_seen_sequences
								[peer_index]
								[NET_RELIABLE_CHANNEL_A] =
									(uint8_t)
										received_sequence;
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.last_delivered_seq_channel_a =
								received_sequence;
						} else if (use_channel_b != 0) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.last_delivered_seq_channel_b =
								received_sequence;
							last_seen_sequences
								[peer_index]
								[NET_RELIABLE_CHANNEL_B] =
									(uint8_t)
										received_sequence;
						} else {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.last_delivered_seq_default =
								received_sequence;
							last_seen_sequences
								[peer_index]
								[NET_RELIABLE_CHANNEL_DEFAULT] =
									(uint8_t)
										received_sequence;
						}
					}
					XVT_LOG_DEBUG(
						"network.lobby_flight_packet_dropped player=%u channel=%u seq=%d type=%d resent=%u",
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.direct_play_id,
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.packet_class,
						received_sequence, packet_type,
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.is_resent_copy);
					if (net_remove_incoming_packet_at_index(
						    (unsigned int)scan_index) ==
					    0) {
						continue;
					}
					if (++scan_index >=
					    NET_RECV_QUEUE_CAPACITY) {
						scan_index = 0;
					}
					continue;
				}
				if (reorder_allowances[peer_index] <
				    processed_packet_counts[peer_index]) {
					XVT_LOG_DEBUG(
						"network.lobby_peer_deferred player=%u seen=%u allowance=%u",
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.direct_play_id,
						(unsigned)
							processed_packet_counts
								[peer_index],
						(unsigned)reorder_allowances
							[peer_index]);
					if (++scan_index >=
					    NET_RECV_QUEUE_CAPACITY) {
						scan_index = 0;
					}
					continue;
				}
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_heard_ms = GetTickCount();
				if (g_front_state
					    .net_runtime_recv_queue[scan_index]
					    .is_resent_copy == 0) {
					++processed_packet_counts[peer_index];
				}

				if (use_channel_a != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_a +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else if (use_channel_b != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_b +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_default +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				}
			}
			sequence_delta = received_sequence - expected_sequence;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    (sequence_delta < NET_RELIABLE_NEGATIVE_WINDOW ||
			     (sequence_delta >= 0 &&
			      sequence_delta < NET_RELIABLE_POSITIVE_WINDOW))) {
				if (expected_sequence == received_sequence) {
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_activity_ms =
						GetTickCount();
					if (use_channel_a != 0) {
						sprintf(debug_text, "(RB %u) ",
							received_sequence);
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_a =
							received_sequence;
					} else if (use_channel_b != 0) {
						sprintf(debug_text, "(RG %u) ",
							received_sequence);
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_b =
							received_sequence;
					} else {
						sprintf(debug_text, "(RS %u) ",
							received_sequence);
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_default =
							received_sequence;
					}
					++g_front_state
						  .net_runtime_reliable_peer_slots
							  [peer_index]
						  .packet_count;
					g_front_state
						.net_runtime_recv_scratch_packet =
						g_front_state
							.net_runtime_recv_queue
								[scan_index];
					net_remove_incoming_packet_at_index(
						(unsigned int)scan_index);
					*out_sender_id =
						g_front_state
							.net_runtime_recv_scratch_packet
							.direct_play_id;
					*out_packet_size =
						g_front_state
							.net_runtime_recv_scratch_packet
							.payload_size;
					XVT_LOG_DEBUG(
						"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"in_order\"",
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.direct_play_id,
						peer_index,
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.packet_class,
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.sequence_byte,
						packet_type,
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.payload_size,
						g_front_state
							.net_runtime_recv_queue_count);
					return g_front_state
						.net_runtime_recv_scratch_packet
						.payload;
				}
				if (g_front_state
					    .net_runtime_recv_queue[scan_index]
					    .is_resent_copy == 0) {
					if (use_channel_a != 0) {
						channel_index =
							NET_RELIABLE_CHANNEL_A;
					} else if (use_channel_b != 0) {
						channel_index =
							NET_RELIABLE_CHANNEL_B;
					} else {
						channel_index =
							NET_RELIABLE_CHANNEL_DEFAULT;
					}
					int sequence_cursor =
						(int)last_seen_sequences
							[peer_index]
							[channel_index] +
						1;
					last_seen_sequences
						[peer_index]
						[channel_index] = (uint8_t)
							received_sequence;
					if (sequence_cursor >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						sequence_cursor = 0;
					}
					int missing_packet_count =
						received_sequence -
						(int)sequence_cursor;
					if (missing_packet_count < 0) {
						missing_packet_count +=
							NET_RELIABLE_SEQUENCE_COUNT;
					}
					if (reorder_allowances[peer_index] >=
					    missing_packet_count) {
						reorder_allowances
							[peer_index] -=
							(uint8_t)
								missing_packet_count;
					} else {
						reorder_allowances[peer_index] =
							0;
					}
					int sent_retry_request = 0;
					int first_missing_sequence =
						sequence_cursor;
					while (sequence_cursor !=
					       received_sequence) {
						int queued_packet_index =
							net_find_queued_sequenced_packet(
								scan_index,
								(int)sequence_cursor,
								use_channel_a,
								use_channel_b,
								(int)peer_index);
						if (queued_packet_index <
							    NET_RECV_QUEUE_CAPACITY &&
						    queued_packet_index >= 0) {
							if (expected_sequence ==
							    (int)sequence_cursor) {
								g_front_state
									.net_runtime_reliable_peer_slots
										[peer_index]
									.last_activity_ms =
									GetTickCount();
								g_front_state
									.net_runtime_recv_scratch_packet =
									g_front_state
										.net_runtime_recv_queue
											[queued_packet_index];
								if (use_channel_a !=
								    0) {
									sprintf(debug_text,
										"(ROOB %u) ",
										expected_sequence);
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_delivered_seq_channel_a =
										expected_sequence;
								} else if (
									use_channel_b !=
									0) {
									sprintf(debug_text,
										"(ROOG %u) ",
										expected_sequence);
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_delivered_seq_channel_b =
										expected_sequence;
								} else {
									sprintf(debug_text,
										"(ROOS %u) ",
										expected_sequence);
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_delivered_seq_default =
										expected_sequence;
								}
								++g_front_state
									  .net_runtime_reliable_peer_slots
										  [peer_index]
									  .packet_count;
								*out_sender_id =
									g_front_state
										.net_runtime_recv_scratch_packet
										.direct_play_id;
								*out_packet_size =
									g_front_state
										.net_runtime_recv_scratch_packet
										.payload_size;
								if (missing_packet_count <=
								    1) {
									g_front_state
										.net_runtime_recv_queue
											[scan_index]
										.nack_retry_count =
										0;
									g_front_state
										.net_runtime_recv_queue
											[scan_index]
										.last_nack_ms =
										0;
								}
								net_remove_incoming_packet_at_index(
									queued_packet_index);
								XVT_LOG_DEBUG(
									"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"gap_filled\"",
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.direct_play_id,
									peer_index,
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.packet_class,
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.sequence_byte,
									*(const int
										  *)g_front_state
										 .net_runtime_recv_scratch_packet
										 .payload,
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.payload_size,
									g_front_state
										.net_runtime_recv_queue_count);
								return g_front_state
									.net_runtime_recv_scratch_packet
									.payload;
							}
							--missing_packet_count;
						} else if (
							g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.nack_retry_count ==
							0) {
							sprintf(debug_text,
								"(RP %d) ",
								sequence_cursor);
							if ((unsigned int)g_front_state
								    .net_runtime_reliable_peer_slots
									    [peer_index]
								    .packet_count >
							    NET_RELIABLE_RETRY_COUNT_THRESHOLD) {
								++g_front_state
									  .net_runtime_reliable_peer_slots
										  [peer_index]
									  .packet_retry_count;
							}
							retry_packet[1] =
								sequence_cursor;
							retry_packet[0] =
								NET_PACKET_NACK;
							retry_packet[2] =
								use_channel_a
									? NET_RELIABLE_CHANNEL_A
									: (use_channel_b
										   ? NET_RELIABLE_CHANNEL_B
										   : NET_RELIABLE_CHANNEL_DEFAULT);
							net_send_direct_play_packet(
								(int)g_front_state
									.net_runtime_recv_queue
										[scan_index]
									.direct_play_id,
								retry_packet,
								NET_RELIABLE_RETRY_PACKET_WORD_COUNT *
									sizeof(retry_packet
										       [0]),
								1);
							XVT_LOG_DEBUG(
								"network.lobby_nack_sent player=%u channel=%u seq=%d retries=%u gaps=%d",
								(unsigned)g_front_state
									.net_runtime_recv_queue
										[scan_index]
									.direct_play_id,
								(unsigned)retry_packet
									[2],
								sequence_cursor,
								(unsigned)g_front_state
									.net_runtime_recv_queue
										[scan_index]
									.nack_retry_count,
								g_front_state
									.net_runtime_reliable_peer_slots
										[peer_index]
									.packet_retry_count);
							sent_retry_request = 1;
						} else {
							uint32_t now =
								GetTickCount();
							/* queued_packet_index
							 * is reused here as the
							 * NACK retry limit. */
							if (g_front_state
								    .net_reliable_retry_long_timeout_mode ==
							    1) {
								queued_packet_index =
									NET_RELIABLE_LONG_RETRY_LIMIT;
								retry_timeout_ms =
									NET_RELIABLE_LONG_RETRY_TIMEOUT_MS;
							} else {
								queued_packet_index =
									NET_RELIABLE_SHORT_RETRY_LIMIT;
								retry_timeout_ms =
									NET_RELIABLE_SHORT_RETRY_TIMEOUT_MS;
							}
							if (now - (uint32_t)g_front_state
									    .net_runtime_recv_queue
										    [scan_index]
									    .last_nack_ms >
							    retry_timeout_ms) {
								if (g_front_state
									    .net_runtime_recv_queue
										    [scan_index]
									    .nack_retry_count <=
								    (unsigned int)
									    queued_packet_index) {
									sprintf(debug_text,
										"(RP %d) ",
										sequence_cursor);
									retry_packet
										[1] = sequence_cursor;
									retry_packet
										[0] = NET_PACKET_NACK;
									retry_packet[2] =
										use_channel_a
											? NET_RELIABLE_CHANNEL_A
											: (use_channel_b
												   ? NET_RELIABLE_CHANNEL_B
												   : NET_RELIABLE_CHANNEL_DEFAULT);
									net_send_direct_play_packet(
										(int)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.direct_play_id,
										retry_packet,
										NET_RELIABLE_RETRY_PACKET_WORD_COUNT *
											sizeof(retry_packet
												       [0]),
										1);
									XVT_LOG_DEBUG(
										"network.lobby_nack_sent player=%u channel=%u seq=%d retries=%u gaps=%d",
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.direct_play_id,
										(unsigned)retry_packet
											[2],
										sequence_cursor,
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.nack_retry_count,
										g_front_state
											.net_runtime_reliable_peer_slots
												[peer_index]
											.packet_retry_count);
									sent_retry_request =
										1;
								} else {
									XVT_LOG_WARN(
										"network.lobby_nack_gave_up player=%u channel=%u first=%d retries=%u",
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.direct_play_id,
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.packet_class,
										first_missing_sequence,
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.nack_retry_count);
									sequence_cursor =
										first_missing_sequence;
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_activity_ms =
										GetTickCount();
									for (;
									     ;) {
										queued_packet_index = net_find_queued_sequenced_packet(
											scan_index,
											(int)sequence_cursor,
											use_channel_a,
											use_channel_b,
											(int)peer_index);
										if (queued_packet_index >=
											    0 &&
										    queued_packet_index <=
											    NET_RECV_QUEUE_CAPACITY) {
											g_front_state
												.net_runtime_recv_scratch_packet =
												g_front_state
													.net_runtime_recv_queue
														[queued_packet_index];
											net_remove_incoming_packet_at_index(
												queued_packet_index);
											if (use_channel_a !=
											    0) {
												sprintf(debug_text,
													"(ROOB %u) ",
													expected_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_a =
													(int)sequence_cursor;
											} else if (
												use_channel_b !=
												0) {
												sprintf(debug_text,
													"(ROOG %u) ",
													expected_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_b =
													(int)sequence_cursor;
											} else {
												sprintf(debug_text,
													"(ROOS %u) ",
													expected_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_default =
													(int)sequence_cursor;
											}
											++g_front_state
												  .net_runtime_reliable_peer_slots
													  [peer_index]
												  .packet_count;
											*out_sender_id =
												g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id;
											*out_packet_size =
												g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size;
											XVT_LOG_DEBUG(
												"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"past_gap\"",
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id,
												peer_index,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.packet_class,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.sequence_byte,
												*(const int
													  *)g_front_state
													 .net_runtime_recv_scratch_packet
													 .payload,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size,
												g_front_state
													.net_runtime_recv_queue_count);
											return g_front_state
												.net_runtime_recv_scratch_packet
												.payload;
										}
										if (received_sequence ==
										    (int)sequence_cursor) {
											if (use_channel_a !=
											    0) {
												sprintf(debug_text,
													"(ROOB %u) ",
													received_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_a =
													received_sequence;
											} else if (
												use_channel_b !=
												0) {
												sprintf(debug_text,
													"(ROOG %u) ",
													received_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_b =
													received_sequence;
											} else {
												sprintf(debug_text,
													"(ROOS %u) ",
													received_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_default =
													received_sequence;
											}
											++g_front_state
												  .net_runtime_reliable_peer_slots
													  [peer_index]
												  .packet_count;
											g_front_state
												.net_runtime_recv_scratch_packet =
												g_front_state
													.net_runtime_recv_queue
														[scan_index];
											net_remove_incoming_packet_at_index(
												(unsigned int)
													scan_index);
											*out_sender_id =
												g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id;
											*out_packet_size =
												g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size;
											XVT_LOG_DEBUG(
												"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"past_gap\"",
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id,
												peer_index,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.packet_class,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.sequence_byte,
												packet_type,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size,
												g_front_state
													.net_runtime_recv_queue_count);
											return g_front_state
												.net_runtime_recv_scratch_packet
												.payload;
										}
										++sequence_cursor;
										if (sequence_cursor >
										    NET_RELIABLE_SEQUENCE_LIMIT) {
											sequence_cursor =
												0;
										}
									}
								}
							}
						}
						++sequence_cursor;
						if (sequence_cursor >
						    NET_RELIABLE_SEQUENCE_LIMIT) {
							sequence_cursor = 0;
						}
					}

					if (missing_packet_count <= 0) {
						g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.nack_retry_count = 0;
						g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.last_nack_ms = 0;
					} else {
						if (sent_retry_request == 1) {
							++g_front_state
								  .net_runtime_recv_queue
									  [scan_index]
								  .nack_retry_count;
							g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.last_nack_ms =
								(int)GetTickCount();
						}
					}
				} else {
					if (g_front_state
						    .net_runtime_recv_queue_read_index ==
					    scan_index) {
						XVT_LOG_DEBUG(
							"network.lobby_packet_dropped player=%u channel=%u seq=%d expected=%d reason=\"resent_early\"",
							(unsigned)g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.direct_play_id,
							(unsigned)g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.packet_class,
							received_sequence,
							expected_sequence);
						if (net_remove_incoming_packet_at_index(
							    (unsigned int)
								    scan_index) ==
						    0) {
							continue;
						}
					}
				}
			} else {
				XVT_LOG_DEBUG(
					"network.lobby_packet_dropped player=%u channel=%u seq=%d expected=%d reason=\"%s\"",
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.direct_play_id,
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.packet_class,
					received_sequence, expected_sequence,
					g_front_state.net_reliable_peer_slot_count >
							peer_index
						? "stale"
						: "no_peer");
				if (net_remove_incoming_packet_at_index(
					    (unsigned int)scan_index) == 0) {
					continue;
				}
			}
		}

		if (++scan_index >= NET_RECV_QUEUE_CAPACITY) {
			scan_index = 0;
		}
	}

	if (g_front_state.net_runtime_recv_queue_count <
	    NET_RECV_QUEUE_PRESSURE_THRESHOLD) {
		XVT_LOG_DEBUG(
			"network.lobby_receive_waiting queued=%d pass=\"normal\"",
			g_front_state.net_runtime_recv_queue_count);
		return NULL;
	}
	XVT_LOG_WARN("network.lobby_queue_pressure queued=%d",
		     g_front_state.net_runtime_recv_queue_count);
	scan_index = g_front_state.net_runtime_recv_queue_read_index;
	remaining_packet_count = g_front_state.net_runtime_recv_queue_count;
	for (; remaining_packet_count > 0; --remaining_packet_count) {
		if (g_front_state.net_runtime_recv_queue[scan_index]
			    .direct_play_id != 0) {
			received_sequence =
				g_front_state.net_runtime_recv_queue[scan_index]
					.sequence_byte;
			use_channel_a =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_A;
			use_channel_b =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_B;
			peer_index = net_find_or_create_peer_slot(
				(int)g_front_state
					.net_runtime_recv_queue[scan_index]
					.direct_play_id);
			expected_sequence = 0;
			if (peer_index <
				    g_front_state
					    .net_reliable_peer_slot_count &&
			    peer_index < NET_RELIABLE_PEER_CAPACITY) {
				if (use_channel_a != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_a +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else if (use_channel_b != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_b +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_default +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				}
			}
			sequence_delta = received_sequence - expected_sequence;
			if (peer_index >=
				    g_front_state
					    .net_reliable_peer_slot_count ||
			    (sequence_delta >=
				     NET_RELIABLE_PRESSURE_NEGATIVE_WINDOW &&
			     (sequence_delta < 0 ||
			      sequence_delta >=
				      NET_RELIABLE_POSITIVE_WINDOW))) {
				XVT_LOG_DEBUG(
					"network.lobby_packet_dropped player=%u channel=%u seq=%d expected=%d reason=\"%s\"",
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.direct_play_id,
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.packet_class,
					received_sequence, expected_sequence,
					peer_index >= g_front_state
								.net_reliable_peer_slot_count
						? "no_peer"
						: "stale");
				if (net_remove_incoming_packet_at_index(
					    (unsigned int)scan_index) == 0) {
					continue;
				}
			} else if (g_front_state
					   .net_runtime_recv_queue[scan_index]
					   .nack_retry_count != 0) {
				if (use_channel_a != 0) {
					sprintf(debug_text, "(ROOB %u) ",
						received_sequence);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_delivered_seq_channel_a =
						received_sequence;
				} else if (use_channel_b != 0) {
					sprintf(debug_text, "(ROOG %u) ",
						received_sequence);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_delivered_seq_channel_b =
						received_sequence;
				} else {
					sprintf(debug_text, "(ROOS %u) ",
						received_sequence);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_delivered_seq_default =
						received_sequence;
				}
				++g_front_state
					  .net_runtime_reliable_peer_slots
						  [peer_index]
					  .packet_count;
				g_front_state.net_runtime_recv_scratch_packet =
					g_front_state.net_runtime_recv_queue
						[scan_index];
				net_remove_incoming_packet_at_index(
					(unsigned int)scan_index);
				*out_sender_id =
					g_front_state
						.net_runtime_recv_scratch_packet
						.direct_play_id;
				*out_packet_size =
					g_front_state
						.net_runtime_recv_scratch_packet
						.payload_size;
				XVT_LOG_DEBUG(
					"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"queue_full\"",
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.direct_play_id,
					peer_index,
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.packet_class,
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.sequence_byte,
					*(const int *)g_front_state
						 .net_runtime_recv_scratch_packet
						 .payload,
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.payload_size,
					g_front_state
						.net_runtime_recv_queue_count);
				return g_front_state
					.net_runtime_recv_scratch_packet
					.payload;
			}
		}
		if (++scan_index >= NET_RECV_QUEUE_CAPACITY) {
			scan_index = 0;
		}
	}
	XVT_LOG_DEBUG(
		"network.lobby_receive_waiting queued=%d pass=\"pressure\"",
		g_front_state.net_runtime_recv_queue_count);
	return NULL;
}

#ifndef XVT_MODERN
/* Polls net_get_next_app_packet until a game packet arrives and returns it, with
 * its sender and size in the out arguments; returns NULL once timeout_seconds
 * have passed. Only the original build calls this. */
// FUNCTION: XVT 0x4D1130
int *net_wait_for_app_packet(DPID *out_sender_id, uint32_t *out_packet_size,
			     int timeout_seconds)
{
	DPID sender_id;
	uint32_t packet_size;

	uint32_t timeout_ms = (uint32_t)timeout_seconds * 1000;
	uint32_t start_time = GetTickCount();
	int *packet;
	do {
		if (GetTickCount() - start_time > timeout_ms) {
			return NULL;
		}
		packet = net_get_next_app_packet(&sender_id, &packet_size);
	} while (packet == NULL);
	*out_sender_id = sender_id;
	*out_packet_size = packet_size;
	return packet;
}
#endif

/* Returns how many roster players have a DirectPlay id below playerId.
 * Nothing in the engine calls this. */
// FUNCTION: XVT 0x4D11A0
int net_count_players_with_lower_id(DPID player_id)
{
	int lower_player_id_count = 0;
	if (g_front_state.net_player_count > 0) {
		struct net_player_info *player = g_front_state.net_players;
		int remaining_player_count = g_front_state.net_player_count;
		do {
			if (player->player_id < player_id) {
				lower_player_id_count++;
			}
			player++;
			remaining_player_count--;
		} while (remaining_player_count != 0);
	}

	return lower_player_id_count;
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
	} else {
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

#ifndef XVT_MODERN
/* Opens a session through DirectPlay's lobby: creates the lobby object in
 * g_front_state.net_direct_play_lobby, builds an address from connection_address
 * with the transport's address type (net_build_direct_play_address), hands the
 * lobby connection settings for a session of up to 16 players named
 * session_name under g_net_lobby_session_instance_guid, flagged for a host or a
 * joiner by is_host, and connects, which fills g_front_state.net_direct_play.
 * Releases the lobby object on every path. Returns 1 when Connect succeeds,
 * else 0. The address buffer is never freed. For IPX it leaves the address
 * type unset; the serial settings (COM2, 9600 baud) and the IPX arm are
 * never reached, since its one caller passes only modem and TCP/IP. Only the
 * original build calls this. */
// FUNCTION: XVT 0x4D1440
int net_open_direct_play_session(GUID app_guid, const char *local_player_info,
				 const char *local_player_name, int is_host,
				 const char *session_name,
				 network_transport_type network_type,
				 const char *connection_address)
{
	char default_session_name[64] = "modem game";

	(void)default_session_name;
	GUID service_provider_guid;
	DPCOMPORTADDRESS serial_address;
	GUID address_type_guid;
	const GUID *selected_service_provider_guid;
	const char *address;
	switch (network_type) {
	case NET_TRANSPORT_IPX:
		service_provider_guid =
			g_net_direct_play_ipx_service_provider_guid;
		selected_service_provider_guid = &service_provider_guid;
		address = connection_address;
		break;
	case NET_TRANSPORT_TCPIP:
		service_provider_guid =
			g_net_direct_play_tcp_ip_service_provider_guid;
		address_type_guid = g_net_direct_play_inet_address_type_guid;
		selected_service_provider_guid = &service_provider_guid;
		address = connection_address;
		break;
	case NET_TRANSPORT_MODEM:
		service_provider_guid =
			g_net_direct_play_modem_service_provider_guid;
		address_type_guid = g_net_direct_play_phone_address_type_guid;
		selected_service_provider_guid = &service_provider_guid;
		address = connection_address;
		break;
	case NET_TRANSPORT_SERIAL:
		service_provider_guid =
			g_net_direct_play_serial_service_provider_guid;
		serial_address.dwComPort = 2;
		serial_address.dwBaudRate = 9600;
		serial_address.dwFlowControl = 1;
		address_type_guid =
			g_net_direct_play_com_port_address_type_guid;
		selected_service_provider_guid = &service_provider_guid;
		address = (const char *)&serial_address;
		serial_address.dwStopBits = 0;
		serial_address.dwParity = 0;
		break;
	default:
		address = connection_address;
		break;
	}

	if (DirectPlayLobbyCreateA(NULL, &g_front_state.net_direct_play_lobby,
				   NULL, NULL, 0) != 0) {
		return 0;
	}
	void *connection_buffer;
	size_t connection_buffer_size;
	if (net_build_direct_play_address(
		    g_front_state.net_direct_play_lobby,
		    selected_service_provider_guid, &address_type_guid, address,
		    &connection_buffer, &connection_buffer_size) != 0) {
		g_front_state.net_direct_play_lobby->lpVtbl->Release(
			g_front_state.net_direct_play_lobby);
		g_front_state.net_direct_play_lobby = NULL;
		return 0;
	}

	DPSESSIONDESC2 session;
	memset(&session, 0, sizeof(session));
	session.dwSize = sizeof(session);
	session.dwFlags = 0;
	session.guidInstance = g_net_lobby_session_instance_guid;
	session.guidApplication = app_guid;
	session.dwMaxPlayers = 16;
	session.dwCurrentPlayers = 0;
	session.lpszSessionNameA = (char *)session_name;
	session.lpszPasswordA = NULL;
	session.dwReserved1 = 0;
	session.dwReserved2 = 0;
	session.dwUser1 = 0;
	session.dwUser2 = 0;
	session.dwUser3 = 0;
	session.dwUser4 = 0;

	DPNAME player_name;
	memset(&player_name, 0, sizeof(player_name));
	player_name.dwSize = sizeof(player_name);
	player_name.dwFlags = 0;
	player_name.lpszShortNameA = (char *)local_player_name;
	player_name.lpszLongNameA = (char *)local_player_info;

	DPLCONNECTION connection;
	memset(&connection, 0, sizeof(connection));
	connection.dwSize = sizeof(connection);
	connection.dwFlags = 2;
	if (is_host == 0) {
		connection.dwFlags = 1;
	}
	connection.lpSessionDesc = &session;
	connection.lpPlayerName = &player_name;
	connection.guidSP = service_provider_guid;
	connection.lpAddress = connection_buffer;
	connection.dwAddressSize = connection_buffer_size;
	if (g_front_state.net_direct_play_lobby->lpVtbl->SetConnectionSettings(
		    g_front_state.net_direct_play_lobby, 0, 0, &connection) !=
	    0) {
		g_front_state.net_direct_play_lobby->lpVtbl->Release(
			g_front_state.net_direct_play_lobby);
		g_front_state.net_direct_play_lobby = NULL;
		return 0;
	}

	HRESULT connect_result =
		g_front_state.net_direct_play_lobby->lpVtbl->Connect(
			g_front_state.net_direct_play_lobby, 0,
			&g_front_state.net_direct_play, NULL);
	g_front_state.net_direct_play_lobby->lpVtbl->Release(
		g_front_state.net_direct_play_lobby);
	g_front_state.net_direct_play_lobby = NULL;
	return connect_result == 0;
}
#endif

#ifndef XVT_MODERN
/* Builds a DirectPlay address with the lobby's CreateAddress: a first call
 * learns the size (DPERR_BUFFERTOOSMALL), a second fills a buffer from
 * GlobalAlloc, returned with its size through the out arguments; the result
 * is then 0 and the caller owns the buffer. Returns DX_E_INVALIDARG for an
 * all-zero address type, 0x8007000E when the allocation fails, or
 * CreateAddress's error, freeing the buffer. A first call that reports
 * anything but DPERR_BUFFERTOOSMALL is returned as is, out arguments
 * untouched. Only the original build calls this. */
// FUNCTION: XVT 0x4D1840
HRESULT net_build_direct_play_address(IDirectPlayLobbyA *direct_play_lobby,
				      const GUID *service_provider_guid,
				      const GUID *address_type_guid,
				      const char *address,
				      void **out_connection_buffer,
				      size_t *out_connection_buffer_size)
{
	void *connection_buffer = NULL;
	uint32_t address_size = 0;
	if (memcmp(address_type_guid, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
		   sizeof(GUID)) == 0) {
		return DX_E_INVALIDARG;
	}
	const char *address_value = address;
	IDirectPlayLobbyA *lobby = direct_play_lobby;
	HRESULT result = lobby->lpVtbl->CreateAddress(
		lobby, service_provider_guid, address_type_guid, address_value,
		strlen(address_value) + 1, NULL, &address_size);
	if (result == (HRESULT)0x8877001E) {
		connection_buffer = GlobalLock(GlobalAlloc(0x42, address_size));
		if (connection_buffer == NULL) {
			result = (HRESULT)0x8007000E;
		} else {
			result = lobby->lpVtbl->CreateAddress(
				lobby, service_provider_guid, address_type_guid,
				address_value, strlen(address_value) + 1,
				connection_buffer, &address_size);
			if (result >= 0) {
				*out_connection_buffer = connection_buffer;
				*out_connection_buffer_size = address_size;
				return 0;
			}
		}
	}
	if (connection_buffer != NULL) {
		GlobalUnlock(GlobalHandle(connection_buffer));
		GlobalFree(GlobalHandle(connection_buffer));
	}
	return result;
}
#endif

/* Copies the lobby's DirectPlay state from g_front_state into the flight
 * session's variables when a flight starts: the interface, application and
 * session GUIDs, group and host ids, local player, receive queue (entries
 * kept at their indices) with its indices and count, peer slots, broadcast
 * and group counters and trailers, and the 128-entry sent history. Returns
 * 1. Only net_session_init_game_session calls it. */
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

/* Frees every lobby peer slot whose DirectPlay id is neither in the roster
 * nor the group's, moves later slots down into the gaps, and recounts
 * g_front_state.net_reliable_peer_slot_count over all 40 slots. Returns 1. */
// FUNCTION: XVT 0x4D1CA0
int net_compact_reliable_peer_slots_for_roster(void)
{
	int player_count;

	int slot_index = 0;
	struct net_player_info *player_roster =
		net_get_player_roster(&player_count);
	struct net_reliable_peer_slot *slot;
	if ((int)g_front_state.net_reliable_peer_slot_count > 0) {
		slot = g_front_state.net_runtime_reliable_peer_slots;
		do {
			int player_index = 0;
			if (player_count > 0) {
				struct net_player_info *roster_player =
					player_roster;
				do {
					if (roster_player->player_id ==
					    slot->direct_play_id) {
						break;
					}
					++roster_player;
					++player_index;
				} while (player_index < player_count);
			}

			if (player_index >= player_count &&
			    slot->direct_play_id !=
				    g_front_state.net_group_dplay_id) {
				XVT_LOG_DEBUG(
					"network.lobby_peer_freed player=%u peer=%d",
					(unsigned)slot->direct_play_id,
					slot_index);
				slot->direct_play_id = 0;
				slot->last_delivered_seq_default = 127;
				slot->last_delivered_seq_channel_a = 127;
				slot->last_delivered_seq_channel_b = 127;
				slot->recv_seq_default = 127;
				slot->recv_seq_channel_a = 127;
				slot->recv_seq_channel_b = 127;
				slot->send_seq = 0;
				slot->last_piggyback_type = NET_PACKET_NOP;
				slot->piggyback_length = 1;
				slot->last_activity_ms = 0;
				slot->last_heard_ms = 0;
				slot->packet_count = 0;
				slot->packet_drop_count = 0;
				slot->packet_retry_count = 0;
			}
			++slot;
			++slot_index;
		} while ((int)g_front_state.net_reliable_peer_slot_count >
			 slot_index);
	}

	slot_index = 0;
	if ((int)(g_front_state.net_reliable_peer_slot_count - 1) > 0) {
		slot = g_front_state.net_runtime_reliable_peer_slots;
		do {
			if (slot->direct_play_id == 0) {
				int next_slot_index = slot_index + 1;
				if (next_slot_index <
				    (int)g_front_state
					    .net_reliable_peer_slot_count) {
					struct net_reliable_peer_slot *next_slot =
						&g_front_state.net_runtime_reliable_peer_slots
							 [next_slot_index];
					while (next_slot->direct_play_id == 0) {
						++next_slot;
						++next_slot_index;
						if (next_slot_index >=
						    (int)g_front_state
							    .net_reliable_peer_slot_count) {
							break;
						}
					}
					if (next_slot_index <
					    (int)g_front_state
						    .net_reliable_peer_slot_count) {
						memcpy(slot,
						       &g_front_state.net_runtime_reliable_peer_slots
								[next_slot_index],
						       sizeof(*slot));
						XVT_LOG_DEBUG(
							"network.lobby_peer_moved player=%u previous=%d peer=%d",
							(unsigned)slot
								->direct_play_id,
							next_slot_index,
							slot_index);
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.direct_play_id = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.last_delivered_seq_default =
							127;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.last_delivered_seq_channel_a =
							127;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.last_delivered_seq_channel_b =
							127;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.recv_seq_default = 127;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.recv_seq_channel_a =
							127;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.recv_seq_channel_b =
							127;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.send_seq = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.last_piggyback_type =
							NET_PACKET_NOP;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.piggyback_length = 1;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.last_activity_ms = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.last_heard_ms = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.packet_count = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.packet_drop_count = 0;
						g_front_state
							.net_runtime_reliable_peer_slots
								[next_slot_index]
							.packet_retry_count = 0;
					}
				}
			}
			++slot;
			++slot_index;
		} while ((int)(g_front_state.net_reliable_peer_slot_count - 1) >
			 slot_index);
	}

	g_front_state.net_reliable_peer_slot_count = 0;
	for (slot = g_front_state.net_runtime_reliable_peer_slots;
	     slot < &g_front_state.net_runtime_reliable_peer_slots[40];
	     ++slot) {
		if (slot->direct_play_id != 0) {
			++g_front_state.net_reliable_peer_slot_count;
		}
	}
	XVT_LOG_DEBUG("network.lobby_peers_compacted peers=%u",
		      (unsigned)g_front_state.net_reliable_peer_slot_count);
	return 1;
}

/* For each roster player but the local one whose peer slot has had no
 * delivery or keepalive for over 3,000 ms (last_activity_ms), sends a
 * NET_PACKET_KEEPALIVE outside the sequence scheme, carrying the next
 * sequence this side expects from it on the broadcast, group and one-player
 * channels and the current time in ms, and stamps last_activity_ms. Adds a
 * peer slot for any roster player that has none. Returns 1. */
// FUNCTION: XVT 0x4D1EA0
int net_send_sequence_keepalives(void)
{
	int player_count;
	int packet[128];

	unsigned int player_index = 0;
	struct net_player_info *player_roster =
		net_get_player_roster(&player_count);
	if ((unsigned int)player_count > 0) {
		do {
			unsigned int now_ms = GetTickCount();
			if (g_front_state.net_runtime_local_player.player_id !=
			    player_roster[player_index].player_id) {
				unsigned int peer_index =
					net_find_or_create_peer_slot(
						player_roster[player_index]
							.player_id);
				if (g_front_state.net_reliable_peer_slot_count >
					    peer_index &&
				    peer_index < 40) {
					if (now_ms -
						    g_front_state
							    .net_runtime_reliable_peer_slots
								    [peer_index]
							    .last_activity_ms >
					    3000) {
						packet[0] =
							NET_PACKET_KEEPALIVE;
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_activity_ms =
							now_ms;
						unsigned int sequence =
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_a +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[1] = sequence;
						sequence =
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_b +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[2] = sequence;
						sequence =
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_default +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[3] = sequence;
						packet[4] = GetTickCount();
						((int (*)(int, const void *,
							  int, int))
							 net_send_direct_play_packet)(
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.direct_play_id,
							packet, 20, 0);
						XVT_LOG_DEBUG(
							"network.lobby_keepalive_sent player=%u peer=%u broadcast=%d group=%d single=%d",
							(unsigned)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.direct_play_id,
							peer_index, packet[1],
							packet[2], packet[3]);
					}
				}
			}
			++player_index;
		} while (player_index < (unsigned int)player_count);
	}
	return 1;
}

/* Tells whether a lobby packet's sequence was already received from that
 * player on its channel: broadcast when use_channel0 is set, else group when
 * use_channel2 is set, else one-player. Returns 1 when the sequence is not 1
 * to 63 ahead of the newest one received, counting modulo 128 (a duplicate
 * or a stale packet). Otherwise records it as the newest in the player's
 * peer slot and returns 0. Also returns 0, recording nothing, when the call
 * had to add a peer slot or the 40-slot table is full. */
// FUNCTION: XVT 0x4D1FA0
int net_check_and_record_incoming_sequence(int player_id, int sequence_id,
					   int use_channel0, int use_channel2)
{
	unsigned int previous_peer_slot_count =
		g_front_state.net_reliable_peer_slot_count;
	unsigned int peer_index = net_find_or_create_peer_slot(player_id);
	if (previous_peer_slot_count !=
		    g_front_state.net_reliable_peer_slot_count ||
	    peer_index >= 40) {
		XVT_LOG_DEBUG(
			"network.lobby_sequence_unrecorded player=%u seq=%d reason=\"%s\"",
			(unsigned)player_id, sequence_id,
			peer_index >= 40 ? "no_slot" : "new_peer");
		return 0;
	}

	struct net_reliable_peer_slot *peer;
	int previous_sequence;
	if (use_channel0 != 0) {
		peer = &g_front_state
				.net_runtime_reliable_peer_slots[peer_index];
		previous_sequence = peer->recv_seq_channel_a;
	} else if (use_channel2 != 0) {
		peer = &g_front_state
				.net_runtime_reliable_peer_slots[peer_index];
		previous_sequence = peer->recv_seq_channel_b;
	} else {
		peer = &g_front_state
				.net_runtime_reliable_peer_slots[peer_index];
		previous_sequence = peer->recv_seq_default;
	}

	int sequence_delta = sequence_id - previous_sequence;
	if (sequence_delta >= -64 &&
	    (sequence_delta <= 0 || sequence_delta >= 64)) {
		return 1;
	}

	if (use_channel0 != 0) {
		peer->recv_seq_channel_a = sequence_id;
	} else if (use_channel2 != 0) {
		peer->recv_seq_channel_b = sequence_id;
	} else {
		peer->recv_seq_default = sequence_id;
	}
	return 0;
}

/* Searches the lobby receive queue from the oldest entry for a resent copy
 * whose sender holds peer slot peer_slot_index (a sender with no slot counts
 * as the slot count), whose sequence is sequence_id, and whose class is 0
 * when use_channel0 is set, 2 when use_channel2 is set, else neither. Returns
 * its queue index, or -1. The first argument is ignored. */
// FUNCTION: XVT 0x4D2080
int net_find_queued_sequenced_packet(int unused_queue_index, int sequence_id,
				     int use_channel0, int use_channel2,
				     int peer_slot_index)
{
	(void)unused_queue_index;

	int queue_index = g_front_state.net_runtime_recv_queue_read_index;
	for (int remaining = g_front_state.net_runtime_recv_queue_count;
	     remaining != 0; --remaining) {
		if (g_front_state.net_runtime_recv_queue[queue_index]
			    .is_resent_copy != 0) {
			DPID direct_play_id =
				g_front_state
					.net_runtime_recv_queue[queue_index]
					.direct_play_id;
			int sequence_byte =
				g_front_state
					.net_runtime_recv_queue[queue_index]
					.sequence_byte;
			int is_class0 =
				g_front_state
					.net_runtime_recv_queue[queue_index]
					.packet_class == 0;
			int is_class2 =
				g_front_state
					.net_runtime_recv_queue[queue_index]
					.packet_class == 2;
			unsigned int slot = 0;
			if (g_front_state.net_reliable_peer_slot_count > slot) {
				struct net_reliable_peer_slot *peer =
					g_front_state
						.net_runtime_reliable_peer_slots;
				for (;;) {
					if (peer->direct_play_id ==
					    direct_play_id) {
						break;
					}
					++peer;
					++slot;
					if (g_front_state
						    .net_reliable_peer_slot_count <=
					    slot) {
						break;
					}
				}
			}
			if (slot == (unsigned int)peer_slot_index) {
				if (use_channel0 != 0) {
					if (is_class0 &&
					    sequence_byte == sequence_id) {
						return queue_index;
					}
				} else if (use_channel2 != 0) {
					if (is_class2 &&
					    sequence_byte == sequence_id) {
						return queue_index;
					}
				} else if (!is_class0 && !is_class2 &&
					   sequence_byte == sequence_id) {
					return queue_index;
				}
			}
		}
		if ((unsigned int)++queue_index >= 1024) {
			queue_index = 0;
		}
	}
	return -1;
}

/* Takes the entry at queueIndex out of the lobby receive queue and lowers its
 * count. At the read index it advances the read index and returns 1;
 * anywhere else it moves every later entry down one place, steps the write
 * index back and returns 0. Does not check that an entry is queued at
 * queueIndex. */
// FUNCTION: XVT 0x4D2170
int net_remove_incoming_packet_at_index(unsigned int queue_index)
{
	if (g_front_state.net_runtime_recv_queue_read_index ==
	    (int)queue_index) {
		++g_front_state.net_runtime_recv_queue_read_index;
		--g_front_state.net_runtime_recv_queue_count;
		if (g_front_state.net_runtime_recv_queue_read_index >= 1024) {
			g_front_state.net_runtime_recv_queue_read_index = 0;
		}
		return 1;
	}

	{
		unsigned int destination_index = queue_index;
		unsigned int next_index = queue_index + 1;
		if (next_index >= 1024) {
			next_index = 0;
		}
		unsigned int end_index =
			(unsigned int)
				g_front_state.net_runtime_recv_queue_read_index;
		end_index += (unsigned int)
				     g_front_state.net_runtime_recv_queue_count;
		if (end_index >= 1024) {
			end_index -= 1024;
		}
		while (next_index != end_index) {
			memcpy(&g_front_state.net_runtime_recv_queue
					[destination_index],
			       &g_front_state
					.net_runtime_recv_queue[next_index],
			       sizeof(g_front_state.net_runtime_recv_queue
					      [destination_index]));
			++destination_index;
			if (destination_index >= 1024) {
				destination_index = 0;
			}
			++next_index;
			if (next_index >= 1024) {
				next_index = 0;
			}
		}
	}

	--g_front_state.net_runtime_recv_queue_count;
	if (g_front_state.net_runtime_recv_queue_write_index == 0) {
		g_front_state.net_runtime_recv_queue_write_index = 1023;
	} else {
		--g_front_state.net_runtime_recv_queue_write_index;
	}
	XVT_LOG_DEBUG("network.lobby_queue_shifted index=%u queued=%d write=%d",
		      queue_index, g_front_state.net_runtime_recv_queue_count,
		      g_front_state.net_runtime_recv_queue_write_index);
	return 0;
}

/* Returns the player's average latency in ms from its
 * g_net_player_connection_stats entry; 1 when the entry has no samples, 0 when
 * the player has no entry. */
// FUNCTION: XVT 0x4D2250
unsigned int net_get_average_latency_ms(int player_id)
{
	for (int player_index = 0; player_index < 40; player_index++) {
		if (g_net_player_connection_stats[player_index].player_id ==
		    player_id) {
			if (g_net_player_connection_stats[player_index]
				    .latency_sample_count == 0) {
				return 1;
			}
			return g_net_player_connection_stats[player_index]
				       .latency_total_ms /
			       g_net_player_connection_stats[player_index]
				       .latency_sample_count;
		}
	}
	return 0;
}

/* Makes latency_ms the only latency sample of the player's
 * g_net_player_connection_stats entry, claiming the first free entry when the
 * search meets one before the player's. Returns 1, also when all 40 entries
 * belong to other players and nothing changes. */
// FUNCTION: XVT 0x4D2290
int net_set_player_latency_ms(int player_id, int latency_ms)
{
	int player_index = 0;
	while (g_net_player_connection_stats[player_index].player_id !=
		       player_id &&
	       g_net_player_connection_stats[player_index].player_id != 0) {
		player_index++;
		if (player_index >= 40) {
			XVT_LOG_WARN(
				"network.lobby_link_stats_full player=%u figure=\"latency\"",
				(unsigned)player_id);
			return 1;
		}
	}

	{
		struct net_player_connection_stats *stats =
			&g_net_player_connection_stats[player_index];
		stats->player_id = player_id;
		stats->latency_sample_count = 1;
		stats->latency_total_ms = latency_ms;
		XVT_LOG_DEBUG(
			"network.lobby_link_latency_stored player=%u latency=%d entry=%d",
			(unsigned)player_id, latency_ms, player_index);
		return stats->latency_sample_count;
	}
}

/* Asks DirectPlay to give player playerId the given long and short names.
 * Returns 1 when SetPlayerName succeeds, else 0; the modern build returns
 * XVT_NETWORK_PENDING while the call is pending. Without DirectPlay it
 * returns 0 with the back buffer unlocked and not locked again; otherwise
 * it relocks the back buffer when it was locked. */
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
#ifdef XVT_MODERN
	if (result == DPERR_PENDING) {
		return XVT_NETWORK_PENDING;
	}
#endif
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

/* Returns the index of the lobby peer slot for a DirectPlay id. With none, it
 * adds one at the end of g_front_state.net_runtime_reliable_peer_slots, raising
 * g_front_state.net_reliable_peer_slot_count: sequences 127 (so 0 comes next),
 * send sequence 0, a NOP trailer, all counts 0, and last_activity_ms and
 * last_heard_ms set to the current time. Returns 40, one past the table, when
 * the table is full. */
// FUNCTION: XVT 0x4D23B0
unsigned int net_find_or_create_peer_slot(int direct_play_id)
{
	unsigned int slot = 0;
	struct net_reliable_peer_slot *peers =
		g_front_state.net_runtime_reliable_peer_slots;
	if (g_front_state.net_reliable_peer_slot_count > slot) {
		do {
			if (peers[slot].direct_play_id ==
			    (DPID)direct_play_id) {
				return slot;
			}
			++slot;
			if (slot < g_front_state.net_reliable_peer_slot_count) {
				continue;
			}
			break;
		} while (1);
	}

	if (g_front_state.net_reliable_peer_slot_count == slot && slot < 40) {
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.direct_play_id = direct_play_id;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.last_delivered_seq_default = 127;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.last_delivered_seq_channel_a = 127;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.last_delivered_seq_channel_b = 127;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.recv_seq_default = 127;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.recv_seq_channel_a = 127;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.recv_seq_channel_b = 127;
		g_front_state.net_runtime_reliable_peer_slots[slot].send_seq =
			0;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.last_piggyback_type = NET_PACKET_NOP;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.piggyback_length = 1;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.last_activity_ms = GetTickCount();
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.last_heard_ms = GetTickCount();
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.packet_count = 0;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.packet_drop_count = 0;
		g_front_state.net_runtime_reliable_peer_slots[slot]
			.packet_retry_count = 0;
		++g_front_state.net_reliable_peer_slot_count;
		char message[256];
		sprintf(message, "SAdding new net sequence %u\n",
			direct_play_id);
		XVT_LOG_DEBUG(
			"network.lobby_peer_added player=%u peer=%u peers=%u",
			(unsigned)direct_play_id, slot,
			(unsigned)g_front_state.net_reliable_peer_slot_count);
	} else {
		XVT_LOG_WARN("network.lobby_peer_table_full player=%u",
			     (unsigned)direct_play_id);
	}
	return slot;
}

/* Uses net_find_or_create_peer_slot, so asking about an unknown player adds a
 * reliable peer slot for it. */
/* Returns a player's loss rate in hundredths of a percent, at most 10,000:
 * drops plus twice the retries, per 10,000 packets. The host adds its own
 * peer slot counts for the player to the player's g_net_player_connection_stats
 * entry; another player uses the entry alone and returns 0 when there is
 * none. A packet count of 0 counts as 1. */
// FUNCTION: XVT 0x4D24C0
int net_get_packet_drop_rate_basis_points(int player_id)
{
	int result;

	if (g_front_state.net_is_host != 0) {
		unsigned int peer_index =
			net_find_or_create_peer_slot(player_id);
		int packet_count;
		int weighted_drop_count;
		if (g_front_state.net_reliable_peer_slot_count > peer_index &&
		    peer_index < 40) {
			packet_count = g_front_state
					       .net_runtime_reliable_peer_slots
						       [peer_index]
					       .packet_count;
			weighted_drop_count =
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.packet_drop_count +
				2 * g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.packet_retry_count;
		} else {
			packet_count = 0;
			weighted_drop_count = 0;
		}
		for (int player_index = 0; player_index < 40; ++player_index) {
			if (g_net_player_connection_stats[player_index]
				    .player_id == player_id) {
				packet_count += g_net_player_connection_stats
							[player_index]
								.packet_count;
				weighted_drop_count +=
					g_net_player_connection_stats
						[player_index]
							.packet_drop_count +
					2 * g_net_player_connection_stats
							[player_index]
								.packet_retry_count;
			}
		}
		if (packet_count == 0) {
			packet_count = 1;
		}
		result = weighted_drop_count * 10000 / packet_count;
		if (result > 10000) {
			result = 10000;
		}
	} else {
		int player_index = 0;
		while (g_net_player_connection_stats[player_index].player_id !=
		       player_id) {
			++player_index;
			if (player_index >= 40) {
				return 0;
			}
		}
		unsigned int packet_count =
			(unsigned int)
				g_net_player_connection_stats[player_index]
					.packet_count;
		if (packet_count == 0) {
			packet_count = 1;
		}
		result = (g_net_player_connection_stats[player_index]
				  .packet_drop_count +
			  2 * g_net_player_connection_stats[player_index]
					  .packet_retry_count) *
			 10000u / packet_count;
		if (result > 10000) {
			result = 10000;
		}
	}
	return result;
}

/* Deals with peers silent for over 45,000 ms (last_heard_ms). On the host, for
 * each ready roster player other than itself and the group, it sends the
 * player NET_PACKET_PLAYER_KICKED and queues NET_PACKET_PLAYER_LEFT locally
 * as if from that player, on the one-player channel at its next sequence. On
 * a client whose host is silent, it queues NET_PACKET_HOST_CANCELLED as if
 * from the host, on the broadcast channel. Either way it restarts the
 * silence timer, and does nothing more while the receive queue is full.
 * Returns 0 on a client when no peer slot can be had for the host, else 1. */
// FUNCTION: XVT 0x4D25D0
int net_drop_silent_peers(void)
{
	enum {
		PEER_SILENCE_TIMEOUT_MS = 45000,
		RECV_QUEUE_CAPACITY = 1024,
		MAX_SEQUENCE = 127,
	};

	int packet[2];

	if (g_front_state.net_is_host != 0) {
		for (int player_index = 0;
		     player_index < (int)(sizeof(g_front_state.net_players) /
					  sizeof(g_front_state.net_players[0]));
		     ++player_index) {
			if (g_front_state.net_players[player_index].player_id ==
				    0 ||
			    g_front_state.net_players[player_index].player_id ==
				    g_front_state.net_runtime_local_player
					    .player_id ||
			    g_front_state.net_players[player_index].player_id ==
				    g_front_state.net_group_dplay_id ||
			    g_front_state.net_players[player_index]
					    .ready_flag == 0) {
				continue;
			}
			unsigned int peer_index = net_find_or_create_peer_slot(
				g_front_state.net_players[player_index]
					.player_id);
			if (g_front_state.net_reliable_peer_slot_count <=
			    peer_index) {
				continue;
			}
			unsigned int now_ms = GetTickCount();
			if (now_ms - g_front_state
					     .net_runtime_reliable_peer_slots
						     [peer_index]
					     .last_heard_ms <=
			    PEER_SILENCE_TIMEOUT_MS) {
				continue;
			}
			XVT_LOG_WARN(
				"network.lobby_player_silent player=%u ms=%u",
				(unsigned)g_front_state
					.net_players[player_index]
					.player_id,
				(unsigned)(now_ms -
					   g_front_state
						   .net_runtime_reliable_peer_slots
							   [peer_index]
						   .last_heard_ms));
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.last_heard_ms = GetTickCount();
			if (g_front_state.net_runtime_recv_queue_count >=
			    RECV_QUEUE_CAPACITY) {
				XVT_LOG_WARN(
					"network.lobby_drop_deferred player=%u queued=%d kind=\"player_left\"",
					(unsigned)g_front_state
						.net_players[player_index]
						.player_id,
					g_front_state
						.net_runtime_recv_queue_count);
				continue;
			}
			packet[0] = NET_PACKET_PLAYER_KICKED;
			net_send_packet_and_flush(
				g_front_state.net_players[player_index]
					.player_id,
				packet, sizeof(packet[0]));
			*(int *)g_front_state
				 .net_runtime_recv_queue
					 [g_front_state
						  .net_runtime_recv_queue_write_index]
				 .payload = NET_PACKET_PLAYER_LEFT;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.direct_play_id =
				g_front_state.net_players[player_index]
					.player_id;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.payload_size = sizeof(int);
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.last_nack_ms = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.nack_retry_count = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.is_resent_copy = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.packet_class = 1;
			unsigned int sequence =
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_default +
				1;
			if (sequence > MAX_SEQUENCE) {
				sequence = 0;
			}
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.recv_seq_default = sequence;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.sequence_byte = (uint8_t)sequence;
			++g_front_state.net_runtime_recv_queue_count;
			if (++g_front_state
				      .net_runtime_recv_queue_write_index >=
			    RECV_QUEUE_CAPACITY) {
				g_front_state
					.net_runtime_recv_queue_write_index = 0;
			}
			XVT_LOG_DEBUG(
				"network.lobby_departure_queued player=%u seq=%u queued=%d kind=\"player_left\"",
				(unsigned)g_front_state
					.net_players[player_index]
					.player_id,
				sequence,
				g_front_state.net_runtime_recv_queue_count);
		}
	} else {
		unsigned int peer_index = net_find_or_create_peer_slot(
			g_front_state.net_host_player_id);

		if (g_front_state.net_reliable_peer_slot_count <= peer_index) {
			return 0;
		}
		unsigned int now_ms = GetTickCount();
		if (now_ms -
			    g_front_state
				    .net_runtime_reliable_peer_slots[peer_index]
				    .last_heard_ms >
		    PEER_SILENCE_TIMEOUT_MS) {
			XVT_LOG_WARN(
				"network.lobby_host_silent player=%u ms=%u",
				(unsigned)g_front_state.net_host_player_id,
				(unsigned)(now_ms -
					   g_front_state
						   .net_runtime_reliable_peer_slots
							   [peer_index]
						   .last_heard_ms));
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.last_heard_ms = GetTickCount();
			if (g_front_state.net_runtime_recv_queue_count <
			    RECV_QUEUE_CAPACITY) {
				*(int *)g_front_state
					 .net_runtime_recv_queue
						 [g_front_state
							  .net_runtime_recv_queue_write_index]
					 .payload = NET_PACKET_HOST_CANCELLED;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.direct_play_id =
					g_front_state.net_host_player_id;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.payload_size = sizeof(int);
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.last_nack_ms = 0;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.nack_retry_count = 0;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.is_resent_copy = 0;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.packet_class = 0;
				unsigned int sequence =
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.recv_seq_channel_a +
					1;
				if (sequence > MAX_SEQUENCE) {
					sequence = 0;
				}
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.recv_seq_channel_a = sequence;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.sequence_byte = (uint8_t)sequence;
				++g_front_state.net_runtime_recv_queue_count;
				if (++g_front_state
					      .net_runtime_recv_queue_write_index >=
				    RECV_QUEUE_CAPACITY) {
					g_front_state
						.net_runtime_recv_queue_write_index =
						0;
				}
				XVT_LOG_DEBUG(
					"network.lobby_departure_queued player=%u seq=%u queued=%d kind=\"host_cancelled\"",
					(unsigned)g_front_state
						.net_host_player_id,
					sequence,
					g_front_state
						.net_runtime_recv_queue_count);
			} else {
				XVT_LOG_WARN(
					"network.lobby_drop_deferred player=%u queued=%d kind=\"host_cancelled\"",
					(unsigned)g_front_state
						.net_host_player_id,
					g_front_state
						.net_runtime_recv_queue_count);
			}
		}
	}
	return 1;
}

/* Uses net_find_or_create_peer_slot, so asking about an unknown player adds a
 * reliable peer slot for it. */
/* Returns the player's delivered-packet count: its lobby peer slot's plus its
 * g_net_player_connection_stats entry's. */
// FUNCTION: XVT 0x4D28F0
int net_get_player_packet_count(int player_id)
{
	unsigned int peer_index = net_find_or_create_peer_slot(player_id);
	int packet_count;
	if (g_front_state.net_reliable_peer_slot_count > peer_index &&
	    peer_index < 40) {
		packet_count =
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.packet_count;
	} else {
		packet_count = 0;
	}
	for (int player_index = 0; player_index < 40; ++player_index) {
		if (g_net_player_connection_stats[player_index].player_id ==
		    player_id) {
			return packet_count +
			       g_net_player_connection_stats[player_index]
				       .packet_count;
		}
	}
	return packet_count;
}

/* Uses net_find_or_create_peer_slot, so asking about an unknown player adds a
 * reliable peer slot for it. */
/* Returns the player's drop count: its lobby peer slot's plus its
 * g_net_player_connection_stats entry's. */
// FUNCTION: XVT 0x4D2950
int net_get_player_packet_drop_count(int player_id)
{
	unsigned int peer_index = net_find_or_create_peer_slot(player_id);
	int packet_drop_count;
	if (g_front_state.net_reliable_peer_slot_count > peer_index &&
	    peer_index < 40) {
		packet_drop_count =
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.packet_drop_count;
	} else {
		packet_drop_count = 0;
	}
	for (int player_index = 0; player_index < 40; ++player_index) {
		if (g_net_player_connection_stats[player_index].player_id ==
		    player_id) {
			net_find_or_create_peer_slot(player_id);
			return packet_drop_count +
			       g_net_player_connection_stats[player_index]
				       .packet_drop_count;
		}
	}
	return packet_drop_count;
}

/* Uses net_find_or_create_peer_slot, so asking about an unknown player adds a
 * reliable peer slot for it. */
/* Returns the player's retry count: its lobby peer slot's plus its
 * g_net_player_connection_stats entry's. */
// FUNCTION: XVT 0x4D29C0
int net_get_player_packet_retry_count(int player_id)
{
	unsigned int peer_index = net_find_or_create_peer_slot(player_id);
	int packet_retry_count;
	if (g_front_state.net_reliable_peer_slot_count > peer_index &&
	    peer_index < 40) {
		packet_retry_count =
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.packet_retry_count;
	} else {
		packet_retry_count = 0;
	}
	for (int player_index = 0; player_index < 40; ++player_index) {
		if (g_net_player_connection_stats[player_index].player_id ==
		    player_id) {
			return packet_retry_count +
			       g_net_player_connection_stats[player_index]
				       .packet_retry_count;
		}
	}
	return packet_retry_count;
}

/* Stores packet_count in the player's g_net_player_connection_stats entry, or
 * else in the first free entry, which it claims with latency_total_ms 1 and
 * drop and retry counts 0. Returns 1, also when no entry is free. */
// FUNCTION: XVT 0x4D2A20
int net_set_player_packet_count(int player_id, int packet_count)
{
	int player_index = 0;
	do {
		if (g_net_player_connection_stats[player_index].player_id ==
		    player_id) {
			g_net_player_connection_stats[player_index].player_id =
				player_id;
			g_net_player_connection_stats[player_index]
				.packet_count = packet_count;
			break;
		}
		++player_index;
	} while (player_index < 40);

	if (player_index == 40) {
		player_index = 0;
		do {
			if (g_net_player_connection_stats[player_index]
				    .player_id == 0) {
				g_net_player_connection_stats[player_index]
					.player_id = player_id;
				g_net_player_connection_stats[player_index]
					.latency_total_ms = 1;
				g_net_player_connection_stats[player_index]
					.packet_count = packet_count;
				g_net_player_connection_stats[player_index]
					.packet_drop_count = 0;
				g_net_player_connection_stats[player_index]
					.packet_retry_count = 0;
				break;
			}
			++player_index;
			if (player_index >= 40) {
				XVT_LOG_WARN(
					"network.lobby_link_stats_full player=%u figure=\"packets\"",
					(unsigned)player_id);
				return 1;
			}
		} while (1);
	}

	return 1;
}

/* Stores packet_drop_count in the player's g_net_player_connection_stats entry,
 * or else in the first free entry, which it claims with latency_total_ms 1 and
 * packet and retry counts 0. Returns 1, also when no entry is free. */
// FUNCTION: XVT 0x4D2AB0
int net_set_player_packet_drop_count(int player_id, int packet_drop_count)
{
	int player_index = 0;
	do {
		if (g_net_player_connection_stats[player_index].player_id ==
		    player_id) {
			g_net_player_connection_stats[player_index].player_id =
				player_id;
			g_net_player_connection_stats[player_index]
				.packet_drop_count = packet_drop_count;
			break;
		}
		++player_index;
	} while (player_index < 40);

	if (player_index == 40) {
		player_index = 0;
		do {
			if (g_net_player_connection_stats[player_index]
				    .player_id == 0) {
				g_net_player_connection_stats[player_index]
					.player_id = player_id;
				g_net_player_connection_stats[player_index]
					.latency_total_ms = 1;
				g_net_player_connection_stats[player_index]
					.packet_count = 0;
				g_net_player_connection_stats[player_index]
					.packet_drop_count = packet_drop_count;
				g_net_player_connection_stats[player_index]
					.packet_retry_count = 0;
				break;
			}
			++player_index;
			if (player_index >= 40) {
				XVT_LOG_WARN(
					"network.lobby_link_stats_full player=%u figure=\"drops\"",
					(unsigned)player_id);
				return 1;
			}
		} while (1);
	}

	return 1;
}

/* Stores packet_retry_count in the player's g_net_player_connection_stats entry,
 * or else in the first free entry, which it claims with latency_total_ms 1 and
 * packet and drop counts 0. Returns 1, also when no entry is free. */
// FUNCTION: XVT 0x4D2B40
int net_set_player_packet_retry_count(int player_id, int packet_retry_count)
{
	int player_index = 0;
	do {
		if (g_net_player_connection_stats[player_index].player_id ==
		    player_id) {
			g_net_player_connection_stats[player_index].player_id =
				player_id;
			g_net_player_connection_stats[player_index]
				.packet_retry_count = packet_retry_count;
			break;
		}
		++player_index;
	} while (player_index < 40);

	if (player_index == 40) {
		player_index = 0;
		do {
			if (g_net_player_connection_stats[player_index]
				    .player_id == 0) {
				g_net_player_connection_stats[player_index]
					.player_id = player_id;
				g_net_player_connection_stats[player_index]
					.latency_total_ms = 1;
				g_net_player_connection_stats[player_index]
					.packet_count = 0;
				g_net_player_connection_stats[player_index]
					.packet_drop_count = 0;
				g_net_player_connection_stats[player_index]
					.packet_retry_count =
					packet_retry_count;
				break;
			}
			++player_index;
			if (player_index >= 40) {
				XVT_LOG_WARN(
					"network.lobby_link_stats_full player=%u figure=\"retries\"",
					(unsigned)player_id);
				return 1;
			}
		} while (1);
	}

	return 1;
}

/* Turns autodial off: reads EnableAutodial from the Internet Settings
 * registry key into g_net_saved_enable_auto_dial_value and, when its first byte
 * is nonzero, writes 4 zero bytes in its place and sets
 * g_net_auto_dial_registry_changed. Returns 1 when it wrote, else 0. The modern
 * build does nothing and returns 0. Only the original build calls this. */
// FUNCTION: XVT 0x4D2BD0
int net_disable_auto_dial_registry_setting(void)
{
#ifdef XVT_MODERN
	return 0;
#else
	g_net_auto_dial_registry_changed = 0;
	void *registry_key;
	if (RegOpenKeyExA(
		    0x80000001u,
		    "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
		    0, NET_REGISTRY_ALL_ACCESS, &registry_key) != 0) {
		return 0;
	}
	unsigned int data_size = 5;
	if (RegQueryValueExA(registry_key, "EnableAutodial", NULL, NULL,
			     g_net_saved_enable_auto_dial_value,
			     &data_size) != 0) {
		RegCloseKey(registry_key);
		return 0;
	}
	if (g_net_saved_enable_auto_dial_value[0] == 0) {
		RegCloseKey(registry_key);
		return 0;
	}
	unsigned int disabled_value = 0;
	if (RegSetValueExA(registry_key, "EnableAutodial", 0,
			   NET_REGISTRY_BINARY, &disabled_value, 4) != 0) {
		RegCloseKey(registry_key);
		return 0;
	}
	g_net_auto_dial_registry_changed = 1;
	RegCloseKey(registry_key);
	return 1;
#endif
}

/* When g_net_auto_dial_registry_changed is set, writes the first 4 bytes of
 * g_net_saved_enable_auto_dial_value back to EnableAutodial, clears the flag and
 * returns 1; else returns 0. The modern build does nothing and returns 0.
 * Only the original build calls this. */
// FUNCTION: XVT 0x4D2CB0
int net_restore_auto_dial_registry_setting(void)
{
#ifdef XVT_MODERN
	return 0;
#else
	if (g_net_auto_dial_registry_changed == 0) {
		return 0;
	}
	void *registry_key;
	if (RegOpenKeyExA(
		    0x80000001u,
		    "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
		    0, NET_REGISTRY_ALL_ACCESS, &registry_key) != 0) {
		return 0;
	}
	if (RegSetValueExA(registry_key, "EnableAutodial", 0,
			   NET_REGISTRY_BINARY,
			   g_net_saved_enable_auto_dial_value, 4) != 0) {
		RegCloseKey(registry_key);
		return 0;
	}
	g_net_auto_dial_registry_changed = 0;
	RegCloseKey(registry_key);
	return 1;
#endif
}

#ifndef XVT_MODERN
/* Sends a PING to every player, again each second, and counts the distinct
 * players that answer with a PONG. Returns 1 as soon as they and the local
 * player make up the roster count, or 0 after 10 seconds. Any other packet
 * read meanwhile is lost. Only the original build calls this. */
// FUNCTION: XVT 0x4D2D40
int net_wait_for_shutdown_handshake_acks(void)
{
	enum {
		NET_MAX_PLAYERS = 32,
		NET_SHUTDOWN_RESEND_INTERVAL_MS = 1000,
		NET_SHUTDOWN_TIMEOUT_MS = 10000
	};

	DPID acknowledged_player_ids[NET_MAX_PLAYERS];

	memset(acknowledged_player_ids, 0, sizeof(acknowledged_player_ids));
	int shutdown_packet = NET_PACKET_PING;
	net_send_direct_play_packet(0, &shutdown_packet,
				    sizeof(shutdown_packet), 0);
	int acknowledged_player_count = 1;
	uint32_t start_time = GetTickCount();
	uint32_t current_time = start_time;
	uint32_t last_send_time = GetTickCount();
	DPID sender_id;
	uint32_t packet_size;
	unsigned int player_index;
	while (current_time - start_time < NET_SHUTDOWN_TIMEOUT_MS) {
		current_time = GetTickCount();
		if (current_time - last_send_time >
		    NET_SHUTDOWN_RESEND_INTERVAL_MS) {
			last_send_time = current_time;
			net_send_direct_play_packet(0, &shutdown_packet,
						    sizeof(shutdown_packet), 0);
		}
		int *packet = net_get_next_app_packet(&sender_id, &packet_size);
		if (packet != NULL && *packet == NET_PACKET_PONG) {
			for (player_index = 0; player_index < NET_MAX_PLAYERS;
			     ++player_index) {
				if (acknowledged_player_ids[player_index] ==
				    sender_id) {
					break;
				}
			}
			if (player_index == NET_MAX_PLAYERS) {
				++acknowledged_player_count;
				for (player_index = 0;
				     player_index < NET_MAX_PLAYERS;
				     ++player_index) {
					if (acknowledged_player_ids
						    [player_index] == 0) {
						acknowledged_player_ids
							[player_index] =
								sender_id;
						break;
					}
				}
			}
		}
		if (g_front_state.net_player_count <=
		    acknowledged_player_count) {
			return 1;
		}
	}
	return 0;
}
#endif

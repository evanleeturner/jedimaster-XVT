#include "xvt/net/frontend_net_packets.h"

#include <stdint.h>
#include <string.h>

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/frontend_net_setup_packets.h"
#include "xvt/net/net.h"
#include "xvt/net/net_peers.h"
#include "xvt/net/net_send.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/network_session.h"

/* Part of frontend_net_process_network_packets for a FRONTEND_GAME_STARTED
 * or PROBE_RESPONSE: stores payload's mission seconds, version and password
 * flag in the g_frontend_net_probe globals and logs them. */
static void frontend_net_store_game_status(int *payload)
{
	g_frontend_net_probe_mission_elapsed_seconds = payload[0];
	g_frontend_net_probe_version = payload[1];
	g_frontend_net_probe_password_required = payload[2];
	XVT_LOG_DEBUG("network.game_status seconds=%d version=%d password=%d",
		      g_frontend_net_probe_mission_elapsed_seconds,
		      g_frontend_net_probe_version,
		      g_frontend_net_probe_password_required);
}

/* Part of frontend_net_process_network_packets for a PROBE_REQUEST: sends
 * sender_player_id the lobby state and a PROBE_RESPONSE with
 * FRONTEND_NET_PROTOCOL_VERSION and g_game_config.require_password, built
 * in g_frontend_net_packet_scratch. */
static void frontend_net_answer_probe(DPID sender_player_id)
{
	mission_setup_send_lobby_state(sender_player_id);
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_PROBE_RESPONSE;
	*(int *)&g_frontend_net_packet_scratch.payload[0] = 0;
	*(int *)&g_frontend_net_packet_scratch.payload[4] =
		FRONTEND_NET_PROTOCOL_VERSION;
	*(int *)&g_frontend_net_packet_scratch.payload[8] =
		g_game_config.require_password;
	net_send_packet_and_flush(sender_player_id,
				  &g_frontend_net_packet_scratch,
				  4 * sizeof(int));
	XVT_LOG_DEBUG("network.status_query player=%u password=%u",
		      (unsigned)sender_player_id,
		      (unsigned)g_game_config.require_password);
}

/* Part of frontend_net_process_network_packets for a lobby STATE: clears
 * the ready flags, stores the game name, the mission directory and
 * description ids and the players needed from payload, and rebuilds
 * g_mp_roster from the players it lists, marking each known one ready. */
static void frontend_net_on_lobby_state(int *payload)
{
	struct net_player_info *net_player;
	int packet_word_index;
	int roster_index;
	int ready_player_count;

	net_clear_player_ready_flags();
	memcpy(g_pilot_data.multiplayer_game_name, payload,
	       sizeof(g_pilot_data.multiplayer_game_name));
	g_frontend_net_received_mission_directory_id = payload[9];
	g_frontend_net_received_mission_description_id = payload[10];
	g_frontend_net_probe_players_needed = payload[11] - payload[12];
	ready_player_count = payload[12];
	XVT_LOG_DEBUG(
		"network.lobby_state game=\"%.32s\" directory=%d mission=%d players=%d needed=%d",
		g_pilot_data.multiplayer_game_name,
		g_frontend_net_received_mission_directory_id,
		g_frontend_net_received_mission_description_id,
		ready_player_count, g_frontend_net_probe_players_needed);
	packet_word_index = ROSTER_PACKET_FIRST_PLAYER_WORD;
	memset(g_mp_roster, 0, sizeof(g_mp_roster));
	for (roster_index = 0; roster_index < ready_player_count;
	     ++roster_index) {
		if (payload[packet_word_index] == 0) {
			++packet_word_index;
			g_mp_roster[roster_index].player_id = 0;
			g_mp_roster[roster_index].pilot_rating =
				payload[packet_word_index++];
			++packet_word_index;
		} else {
			net_player =
				net_find_player(payload[packet_word_index]);
			if (net_player != NULL) {
				net_mark_player_ready_no_lock(
					payload[packet_word_index]);
				strncpy(g_mp_roster[roster_index].name,
					net_player->player_name,
					PLAYER_NAME_COPY_SIZE);
				g_mp_roster[roster_index].player_id =
					net_player->player_id;
				++packet_word_index;
				g_mp_roster[roster_index].pilot_rating =
					payload[packet_word_index];
				++packet_word_index;
				if (net_is_host() == 0) {
					net_set_player_latency_ms(
						g_mp_roster[roster_index]
							.player_id,
						payload[packet_word_index]);
				}
				++packet_word_index;
			} else {
				memcpy(g_mp_roster[roster_index].name,
				       "No name", sizeof("No name"));
				g_mp_roster[roster_index].player_id =
					payload[packet_word_index];
				++packet_word_index;
				g_mp_roster[roster_index].pilot_rating =
					payload[packet_word_index];
				packet_word_index += 2;
			}
		}
		XVT_LOG_DEBUG(
			"network.roster_entry index=%d player=%u rating=%d latency=%d name=\"%.14s\"",
			roster_index,
			(unsigned)g_mp_roster[roster_index].player_id,
			(int)g_mp_roster[roster_index].pilot_rating,
			payload[packet_word_index - 1],
			g_mp_roster[roster_index].name);
	}
}

/* Part of frontend_net_process_network_packets for a JOIN_REQUEST of the
 * right size on the host: refuses it, sending sender_player_id the reason,
 * or admits the player, telling everyone with PLAYER_ADMITTED, sending the
 * lobby state and setting g_mission_setup_begin_button_lockout_frames. */
static void frontend_net_answer_join_request(DPID sender_player_id,
					     int *payload)
{
	int ready_player_count;

	ready_player_count = net_count_ready_players();
	if (g_mission_setup_roster_authoritative != 0) {
		XVT_LOG_WARN(
			"network.join_request_refused player=%u reason=\"locked\" version=%d",
			(unsigned)sender_player_id, payload[0]);
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_ROSTER_LOCKED;
		net_send_packet_and_flush(sender_player_id,
					  &g_frontend_net_packet_scratch,
					  sizeof(int));
	} else if (ready_player_count >= MAX_PLAYERS) {
		XVT_LOG_WARN(
			"network.join_request_refused player=%u reason=\"full\" version=%d",
			(unsigned)sender_player_id, payload[0]);
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_GAME_FULL;
		net_send_packet_and_flush(sender_player_id,
					  &g_frontend_net_packet_scratch,
					  sizeof(int));
	} else if (payload[0] != FRONTEND_NET_PROTOCOL_VERSION) {
		XVT_LOG_WARN(
			"network.join_request_refused player=%u reason=\"version\" version=%d",
			(unsigned)sender_player_id, payload[0]);
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_VERSION_MISMATCH;
		net_send_packet_and_flush(sender_player_id,
					  &g_frontend_net_packet_scratch,
					  sizeof(int));
	} else if (g_game_config.require_password != 0 &&
		   strncmp(g_game_config.password, (const char *)&payload[1],
			   sizeof(g_game_config.password)) != 0) {
		XVT_LOG_WARN(
			"network.join_request_refused player=%u reason=\"password\" version=%d",
			(unsigned)sender_player_id, payload[0]);
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_PASSWORD_REQUIRED;
		net_send_packet_and_flush(sender_player_id,
					  &g_frontend_net_packet_scratch,
					  sizeof(int));
	} else if (net_set_player_ready(sender_player_id) != 0) {
		*(int *)&g_frontend_net_packet_scratch.payload[0] =
			sender_player_id;
		g_mission_setup_begin_button_lockout_frames =
			BEGIN_BUTTON_LOCKOUT_FRAMES;
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_PLAYER_ADMITTED;
		net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
					  2 * sizeof(int));
		mission_setup_send_lobby_state(0);
		XVT_LOG_INFO("network.player_admitted player=%u players=%d",
			     (unsigned)sender_player_id,
			     ready_player_count + 1);
	} else {
		XVT_LOG_WARN(
			"network.join_request_refused player=%u reason=\"not_listed\" version=%d",
			(unsigned)sender_player_id, payload[0]);
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_GAME_FULL;
		net_send_packet_and_flush(sender_player_id,
					  &g_frontend_net_packet_scratch,
					  sizeof(int));
	}
}

/* Part of frontend_net_process_network_packets for a PLAYER_ADMITTED that
 * passed its checks: plays the new player sound when datapad sound is on
 * and marks player payload[0] ready. */
static void frontend_net_accept_admission(int *payload)
{
	if (g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"newpsound", 1, 0, 255,
			12 * g_game_config.sfx_datapad_volume, 63);
	}
	net_mark_player_ready_no_lock(payload[0]);
	XVT_LOG_DEBUG("network.admission_received player=%u",
		      (unsigned)payload[0]);
}

/* Part of frontend_net_process_network_packets for a PLAYER_LEFT: when
 * sender_player_id is among the first net_count_ready_players() entries of
 * g_mp_roster, clears the ready flags and the player's own and sends the
 * lobby state. Then sends everyone a MOVIE_SYNC of op 2 for the player. */
static void frontend_net_on_player_left(DPID sender_player_id)
{
	int roster_index;
	int ready_player_count;

	ready_player_count = net_count_ready_players();
	roster_index = 0;
	if (ready_player_count > 0) {
		do {
			if ((DPID)g_mp_roster[roster_index].player_id ==
			    sender_player_id) {
				if (g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"exitpsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				memset(g_mp_roster_ready_flags, 0,
				       sizeof(g_mp_roster_ready_flags));
				net_clear_player_ready_flag_with_lock_guard(
					sender_player_id);
				mission_setup_send_lobby_state(0);
				break;
			}
			++roster_index;
		} while (roster_index < ready_player_count);
	}
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_MOVIE_SYNC;
	*(int *)&g_frontend_net_packet_scratch.payload[0] = 2;
	*(int *)&g_frontend_net_packet_scratch.payload[4] = sender_player_id;
	net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
				  3 * sizeof(int));
	XVT_LOG_DEBUG("network.leave_notice player=%u index=%d ready=%d",
		      (unsigned)sender_player_id, roster_index,
		      ready_player_count);
}

/* Part of frontend_net_process_network_packets for a CHAT: adds the line
 * in payload to g_frontend_chat_log_buffer as that function's comment tells
 * it, taking the type word off *packet_size; logs a warning when there is
 * no log. */
static void frontend_net_on_chat(DPID sender_player_id, int *payload,
				 uint32_t *packet_size)
{
	int bytes_to_discard;

	if (g_frontend_chat_log_buffer != NULL) {
		*packet_size -= sizeof(int);
		bytes_to_discard = CHAT_LOG_CONTENT_LIMIT;
		bytes_to_discard -= g_frontend_chat_log_used_bytes;
		bytes_to_discard -= (int)*packet_size;
		if (bytes_to_discard < 0) {
			bytes_to_discard = -bytes_to_discard;
			memmove(g_frontend_chat_log_buffer,
				&g_frontend_chat_log_buffer[bytes_to_discard],
				CHAT_LOG_CAPACITY - bytes_to_discard);
			g_frontend_chat_log_used_bytes -= bytes_to_discard;
		}
		memcpy(&g_frontend_chat_log_buffer
			       [g_frontend_chat_log_used_bytes],
		       payload, *packet_size);
		if ((DPID)net_get_local_player_id() == sender_player_id) {
			g_frontend_chat_log_buffer
				[g_frontend_chat_log_used_bytes] = 5;
		}
		g_frontend_chat_log_used_bytes += *packet_size;
		g_frontend_chat_log_buffer[g_frontend_chat_log_used_bytes++] =
			'\n';
		g_frontend_chat_log_buffer[g_frontend_chat_log_used_bytes] =
			'\0';
		XVT_LOG_DEBUG(
			"network.chat_received player=%u bytes=%u used=%d",
			(unsigned)sender_player_id, (unsigned)*packet_size,
			g_frontend_chat_log_used_bytes);
	} else {
		XVT_LOG_WARN(
			"network.chat_dropped player=%u kind=\"chat_line\"",
			(unsigned)sender_player_id);
	}
}

/* Part of frontend_net_process_network_packets for a PLAYER_UNAVAILABLE:
 * the host clears the ready flags and sender_player_id's own and sends the
 * lobby state. */
static void frontend_net_on_player_unavailable(DPID sender_player_id)
{
	if (net_is_host() != 0) {
		if (g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"exitpsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		memset(g_mp_roster_ready_flags, 0,
		       sizeof(g_mp_roster_ready_flags));
		net_clear_player_ready_flag(sender_player_id);
		mission_setup_send_lobby_state(0);
	}
	XVT_LOG_DEBUG("network.player_unavailable player=%u",
		      (unsigned)sender_player_id);
}

/* Part of frontend_net_process_network_packets for a CHAT_SYNC_REQUEST:
 * sends sender_player_id g_frontend_chat_log_buffer in CHAT_SYNC_CHUNK
 * packets of up to 400 bytes, or one empty chunk, with *packet_size as each
 * chunk's size; logs a warning when there is no log. */
static void frontend_net_on_chat_sync_request(DPID sender_player_id,
					      uint32_t *packet_size)
{
	int chunk_index;
	int chat_offset;
	int remaining_bytes;

	chunk_index = 0;
	if (g_frontend_chat_log_buffer != NULL) {
		remaining_bytes = g_frontend_chat_log_used_bytes;
		if (remaining_bytes == 0) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_CHAT_SYNC_CHUNK;
			*(int *)&g_frontend_net_packet_scratch.payload[0] = 0;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch,
				2 * sizeof(int));
		} else {
			chat_offset = 0;
			do {
				/* packet_size is reused here as the
				 * size of each outgoing chat chunk. */
				*packet_size = CHAT_SYNC_CHUNK_SIZE;
				if (remaining_bytes < (int)*packet_size) {
					*packet_size = remaining_bytes;
				}
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_CHAT_SYNC_CHUNK;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[0] = chunk_index++;
				memcpy(&g_frontend_net_packet_scratch
						.payload[4],
				       &g_frontend_chat_log_buffer[chat_offset],
				       *packet_size);
				remaining_bytes -= *packet_size;
				chat_offset += *packet_size;
				net_send_packet_and_flush(
					sender_player_id,
					&g_frontend_net_packet_scratch,
					*packet_size + 2 * sizeof(int));
			} while (remaining_bytes > 0);
		}
		XVT_LOG_DEBUG(
			"network.chat_sync_sent player=%u bytes=%d chunks=%d",
			(unsigned)sender_player_id,
			g_frontend_chat_log_used_bytes, chunk_index);
	} else {
		XVT_LOG_WARN(
			"network.chat_dropped player=%u kind=\"chat_log_request\"",
			(unsigned)sender_player_id);
	}
}

/* Part of frontend_net_process_network_packets for a CHAT_SYNC_CHUNK:
 * takes the two header words off *packet_size, clears the log for chunk 0,
 * turns every byte from 0 to 6 into 6 and copies the chunk into
 * g_frontend_chat_log_buffer at its place. */
static void frontend_net_on_chat_sync_chunk(int *payload, uint32_t *packet_size)
{
	uint8_t *chat_chunk_bytes;
	int player_index;
	int chunk_index;

	chunk_index = payload[0];
	*packet_size -= 2 * sizeof(int);
	if (g_frontend_chat_log_buffer == NULL) {
		XVT_LOG_ERROR("network.chat_chunk_unheld index=%d",
			      chunk_index);
	}
	if (chunk_index == 0) {
		memset(g_frontend_chat_log_buffer, 0, CHAT_LOG_CAPACITY);
		g_frontend_chat_log_used_bytes = 0;
	}
	chat_chunk_bytes = (uint8_t *)&payload[1];
	/* player_index is reused here as a byte index into the chat chunk. */
	for (player_index = 0; player_index < (int)*packet_size;
	     ++player_index) {
		if (chat_chunk_bytes[player_index] <= 6) {
			chat_chunk_bytes[player_index] = 6;
		}
	}
	memcpy(&g_frontend_chat_log_buffer[CHAT_SYNC_CHUNK_SIZE * chunk_index],
	       chat_chunk_bytes, *packet_size);
	g_frontend_chat_log_used_bytes += *packet_size;
	XVT_LOG_DEBUG("network.chat_chunk index=%d bytes=%u used=%d",
		      chunk_index, (unsigned)*packet_size,
		      g_frontend_chat_log_used_bytes);
}

/* Part of frontend_net_process_network_packets for a READY_ROSTER: clears
 * the ready flags, stores the players needed and rebuilds g_mp_roster from
 * the players payload lists, as for a lobby STATE. */
static void frontend_net_on_ready_roster(int *payload)
{
	struct net_player_info *net_player;
	int packet_word_index;
	int roster_index;
	int ready_player_count;

	net_clear_player_ready_flags();
	g_frontend_net_probe_players_needed = payload[0] - payload[1];
	ready_player_count = payload[1];
	XVT_LOG_DEBUG("network.ready_roster players=%d needed=%d",
		      ready_player_count, g_frontend_net_probe_players_needed);
	packet_word_index = ROSTER_PACKET_COMPACT_FIRST_PLAYER_WORD;
	memset(g_mp_roster, 0, sizeof(g_mp_roster));
	for (roster_index = 0; roster_index < ready_player_count;
	     ++roster_index) {
		if (payload[packet_word_index] == 0) {
			++packet_word_index;
			g_mp_roster[roster_index].player_id = 0;
			g_mp_roster[roster_index].pilot_rating =
				payload[packet_word_index++];
			++packet_word_index;
		} else {
			net_player =
				net_find_player(payload[packet_word_index]);
			if (net_player != NULL) {
				net_mark_player_ready_no_lock(
					payload[packet_word_index]);
				strncpy(g_mp_roster[roster_index].name,
					net_player->player_name,
					PLAYER_NAME_COPY_SIZE);
				g_mp_roster[roster_index].player_id =
					net_player->player_id;
				++packet_word_index;
				g_mp_roster[roster_index].pilot_rating =
					payload[packet_word_index];
				++packet_word_index;
				if (net_is_host() == 0) {
					net_set_player_latency_ms(
						g_mp_roster[roster_index]
							.player_id,
						payload[packet_word_index]);
				}
				++packet_word_index;
			} else {
				memcpy(g_mp_roster[roster_index].name,
				       "No name", sizeof("No name"));
				g_mp_roster[roster_index].player_id =
					payload[packet_word_index];
				++packet_word_index;
				g_mp_roster[roster_index].pilot_rating =
					payload[packet_word_index];
				packet_word_index += 2;
			}
		}
		XVT_LOG_DEBUG(
			"network.roster_entry index=%d player=%u rating=%d latency=%d name=\"%.14s\"",
			roster_index,
			(unsigned)g_mp_roster[roster_index].player_id,
			(int)g_mp_roster[roster_index].pilot_rating,
			payload[packet_word_index - 1],
			g_mp_roster[roster_index].name);
	}
}

/* Part of frontend_net_process_network_packets for a MOVIE_SYNC: by op
 * payload[0], marks sender_player_id (0) or every player (1) waiting in
 * g_movie_multiplayer_sync_players, or removes player payload[1] (2). */
static void frontend_net_on_movie_sync(DPID sender_player_id, int *payload)
{
	int movie_player_index;

	XVT_LOG_DEBUG("network.movie_sync player=%u op=%d target=%u",
		      (unsigned)sender_player_id, payload[0],
		      payload[0] == 2 ? (unsigned)payload[1] : 0u);
	for (movie_player_index = 0; movie_player_index < MAX_PLAYERS;
	     ++movie_player_index) {
		switch (payload[0]) {
		case 0:
			if ((DPID)g_movie_multiplayer_sync_players
				    [movie_player_index]
					    .player_id == sender_player_id) {
				g_movie_multiplayer_sync_players
					[movie_player_index]
						.is_waiting = 1;
				movie_player_index = MAX_PLAYERS;
			}
			break;

		case 1:
			g_movie_multiplayer_sync_players[movie_player_index]
				.is_waiting = 1;
			break;

		case 2:
			if (payload[1] ==
			    g_movie_multiplayer_sync_players[movie_player_index]
				    .player_id) {
				g_movie_multiplayer_sync_players
					[movie_player_index]
						.player_id = 0;
				movie_player_index = MAX_PLAYERS;
			}
			break;
		}
	}
}

/* Reads one frontend packet (net_get_next_app_packet) and acts on it; returns
 * its type, or NET_PACKET_NONE when there is none, the type is unknown, or the
 * packet was refused. First, when a ready player left this frame, it sends
 * everyone the lobby state. Each packet's sender goes in
 * g_frontend_net_packet_sender_player_id. A PROBE_REQUEST gets the lobby state
 * and a PROBE_RESPONSE (version and password flag); a PROBE_RESPONSE or
 * game-started notice fills the g_frontendNetProbe globals. A lobby STATE or
 * READY_ROSTER rebuilds g_mp_roster and the ready flags, a STATE also
 * g_pilot_data.multiplayer_game_name and
 * g_frontend_net_received_mission_directory_id and DescriptionId. A
 * JOIN_REQUEST is refused (roster locked, full, version, password) or admitted,
 * telling all players, resending the lobby state and setting
 * g_mission_setup_begin_button_lockout_frames to 24; one that reaches a client
 * or has the wrong size is dropped. Team and flight assignments, ready flags,
 * game options, loadouts, network statistics, mission starts, choices,
 * countdowns and reservations are stored in g_mission_setup_player_assignments,
 * g_mission_setup_player_flight_group_indices, g_mp_roster_ready_flags, the
 * g_missionSetupSelected globals, g_mp_roster, g_game_config, g_pilot_data and
 * g_frontend_net_packet_arg0 and g_frontend_net_reserving_player_id. A CHAT
 * line is added to g_frontend_chat_log_buffer, dropping the oldest bytes past
 * 1,022, with this player's own lines marked by color code 5; a
 * CHAT_SYNC_REQUEST gets the log in 400-byte chunks, and a received chunk
 * rebuilds it with every byte from 0 to 6 turned into color code 6. A
 * PLAYER_UNAVAILABLE always returns NET_PACKET_NONE. Also handles movie sync
 * (g_movie_multiplayer_sync_players), battle progress (the g_remoteBattle
 * globals), briefing arrivals (g_frontend_briefing_entered_count),
 * RETURN_TO_SETUP (g_frontend_skip_screen_entry_setup) and player departures,
 * relays a SUBMIT_MISSION_CHOICE to all as a MISSION_CHOICE, and writes
 * g_frontend_net_packet_scratch. Does not check team, slot or chunk indices or
 * chat sizes from the packet, or the chat log for NULL before a sync chunk. */
// FUNCTION: XVT 0x4E0490
int frontend_net_process_network_packets(void)
{
	int *packet;
	int *payload;
	uint8_t *payload_bytes;
	uint32_t packet_size;
	DPID sender_player_id;
	int packet_type;
	int roster_index;

	net_get_host_player_id();
	packet = net_get_next_app_packet(&sender_player_id, &packet_size);
	if (net_did_ready_player_leave_this_frame() != 0) {
		mission_setup_send_lobby_state(0);
		XVT_LOG_DEBUG("network.lobby_state_resent");
	}
	if (packet == NULL) {
		return 0;
	}

	packet_type = packet[0];
	g_frontend_net_packet_sender_player_id = sender_player_id;
	payload = &packet[1];
	payload_bytes = (uint8_t *)payload;
	switch (packet_type) {
	case NET_PACKET_FRONTEND_GAME_STARTED:
	case NET_PACKET_PROBE_RESPONSE:
		frontend_net_store_game_status(payload);
		break;
	case NET_PACKET_PROBE_REQUEST:
		frontend_net_answer_probe(sender_player_id);
		break;
	case NET_PACKET_STATE:
		frontend_net_on_lobby_state(payload);
		break;
	case NET_PACKET_JOIN_REQUEST:
		if (!net_is_host() || packet_size != 6 * sizeof(int)) {
			XVT_LOG_WARN(
				"network.join_request_dropped player=%u bytes=%u",
				(unsigned)sender_player_id,
				(unsigned)packet_size);
			packet_type = NET_PACKET_NONE;
			break;
		}
		frontend_net_answer_join_request(sender_player_id, payload);
		break;
	case NET_PACKET_GAME_FULL:
	case NET_PACKET_HOST_CANCELLED:
	case NET_PACKET_PLAYER_KICKED:
	case NET_PACKET_SESSION_CANCELLED:
	case NET_PACKET_VERSION_MISMATCH:
	case NET_PACKET_PASSWORD_REQUIRED:
	case NET_PACKET_ROSTER_LOCKED:
	case NET_PACKET_FLIGHT_ASSIGNMENTS_READY:
		break;
	case NET_PACKET_PLAYER_ADMITTED:
		if (packet_size < 2 * sizeof(int) ||
		    sender_player_id != (DPID)net_get_host_player_id() ||
		    (xvt_network_session_get_status().state ==
			     XVT_NETWORK_SESSION_ADMISSION &&
		     !xvt_network_session_accept_admission(sender_player_id,
							   (DPID)payload[0]))) {
			XVT_LOG_WARN(
				"network.admission_rejected player=%u bytes=%u",
				(unsigned)sender_player_id,
				(unsigned)packet_size);
			packet_type = NET_PACKET_NONE;
			break;
		}
		frontend_net_find_local_roster_entry(&roster_index);
		if (roster_index >= MAX_PLAYERS &&
		    net_get_local_player_id() != payload[0]) {
			XVT_LOG_DEBUG("network.admission_ignored player=%u",
				      (unsigned)payload[0]);
			packet_type = NET_PACKET_NONE;
			break;
		}
		frontend_net_accept_admission(payload);
		break;
	case NET_PACKET_PLAYER_LEFT:
		frontend_net_on_player_left(sender_player_id);
		break;
	case NET_PACKET_CHAT:
		frontend_net_on_chat(sender_player_id, payload, &packet_size);
		break;
	case NET_PACKET_PLAYER_UNAVAILABLE:
		frontend_net_on_player_unavailable(sender_player_id);
		packet_type = NET_PACKET_NONE;
		break;
	case NET_PACKET_CHAT_SYNC_REQUEST:
		frontend_net_on_chat_sync_request(sender_player_id,
						  &packet_size);
		break;
	case NET_PACKET_CHAT_SYNC_CHUNK:
		frontend_net_on_chat_sync_chunk(payload, &packet_size);
		break;
	case NET_PACKET_READY_ROSTER:
		frontend_net_on_ready_roster(payload);
		break;
	case NET_PACKET_MOVIE_SYNC:
		frontend_net_on_movie_sync(sender_player_id, payload);
		break;
	default:
		frontend_net_process_setup_packet(
			&packet_type, sender_player_id, payload, payload_bytes);
		break;
	}
	return packet_type;
}

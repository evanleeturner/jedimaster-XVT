#include "xvt/net/net.h"

#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net_reliable.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/network_session.h"

/* Acts on a DirectPlay system message from the lobby queue. A player created:
 * the host, for a player other than itself, refreshes the roster and sends the
 * new player a NET_PACKET_SEQUENCE_STATUS with the player count, the peer slot
 * table and its time in ms; a client refreshes the roster. A player destroyed:
 * the host's departure calls xvt_network_session_host_lost. The host clears the
 * leaver's ready flag, setting g_front_state.net_ready_player_left_this_frame
 * when it was set, frees its peer slot by moving the last slot into it, and
 * clears its g_net_player_connection_stats entry; a client whose host left
 * queues a NET_PACKET_HOST_CANCELLED to itself. The roster is then refreshed. A
 * player renamed: its roster entry takes the new short and long names, cut to
 * 12 characters. The copy is bounded (xvt_network_session_copy_player_names),
 * and a malformed message is skipped. */
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
		if (packet_words[1] == DPPLAYERTYPE_PLAYER &&
		    (DPID)packet_words[2] == g_front_state.net_host_player_id) {
			xvt_network_session_host_lost();
		}
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

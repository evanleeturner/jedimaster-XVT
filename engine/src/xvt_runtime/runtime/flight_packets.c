#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/resync_task.h"

enum {
	PLAYER_COUNT = XVT_FLIGHT_PLAYERS,
	WORLD_STATE_CHUNK_COUNT = XVT_RESYNC_CHUNKS_PER_BATCH,
	PACKET_CLOCK_PROBE_REPLY_SIZE = 2 * sizeof(int),
	CLOCK_PROBE_BIAS_TICKS = 20,
	CLOCK_PROBE_LIMIT_TICKS = 472
};

/* Echoes a peer's clock probe back to it, then moves the allowed clock lead halfway (at least 1
 * tick) toward the lead the probe asks for, halved for a synchronous game or a small session. */
static void xvt_flight_network_answer_clock_probe(int sender_dpid,
						  const int *packet)
{
	int adjustment;
	int target_lead;

	g_flight_net_scratch_packet.packet_type = NET_PACKET_CLOCK_PROBE_REPLY;
	g_flight_net_scratch_packet.payload_dwords[0] = packet[1];

	xvt_flight_network_send_packet(
		sender_dpid, (unsigned int *)&g_flight_net_scratch_packet,
		PACKET_CLOCK_PROBE_REPLY_SIZE);
	target_lead = packet[2];
	if (g_internet_play_enabled == 0 ||
	    g_flight_net_small_session_player_threshold >
		    g_active_flight_player_count) {
		target_lead >>= 1;
	}
	if (g_flight_net_clock_lead_ticks < target_lead) {
		adjustment = (target_lead - g_flight_net_clock_lead_ticks) >> 1;
		if (adjustment == 0) {
			adjustment = 1;
		}
		g_flight_net_clock_lead_ticks += adjustment;
	} else if (g_flight_net_clock_lead_ticks > target_lead) {
		adjustment = (g_flight_net_clock_lead_ticks - target_lead) >> 1;
		if (adjustment == 0) {
			adjustment = 1;
		}
		g_flight_net_clock_lead_ticks -= adjustment;
	}
}

/* On a client, when a reply carries the probe timestamp last sent, moves the allowed clock lead
 * halfway (at least 1 tick) toward the probe's round trip plus the clock adjustment so far and a
 * 20-tick bias, while that total stays under 472 ticks. A tick is 4 ms. */
static void xvt_flight_network_apply_clock_probe_reply(const int *packet)
{
	if (net_session_is_local_host() == 0 &&
	    packet[1] == g_flight_net_clock_probe_timestamp) {
		int adjustment;
		int target_lead;

		target_lead = g_flight_net_clock_adjust_accum_ticks;
		target_lead += g_input_timestamp;
		target_lead -= packet[1];
		target_lead += CLOCK_PROBE_BIAS_TICKS;

		if (target_lead < CLOCK_PROBE_LIMIT_TICKS) {
			if (g_flight_net_clock_lead_ticks < target_lead) {
				adjustment = (target_lead -
					      g_flight_net_clock_lead_ticks) >>
					     1;
				if (adjustment == 0) {
					adjustment = 1;
				}
				g_flight_net_clock_lead_ticks += adjustment;
			} else if (g_flight_net_clock_lead_ticks >
				   target_lead) {
				adjustment = (g_flight_net_clock_lead_ticks -
					      target_lead) >>
					     1;
				if (adjustment == 0) {
					adjustment = 1;
				}
				g_flight_net_clock_lead_ticks -= adjustment;
			}
		}
	}
}

static int xvt_flight_network_control(int sender_dpid, int *packet)
{
	switch (packet[0]) {
	case NET_PACKET_PLAYER_DISCONNECTED: {
		int player_index = packet[1];

		if (player_index >= 0 && player_index < PLAYER_COUNT) {
			g_player_connected[player_index] = 0;
		}
		return 0;
	}
	case NET_PACKET_WORLD_CHECKSUM:
		if (g_players[net_session_find_player_slot_by_dpid(sender_dpid)]
			    .participation_state) {
			xvt_resync_defer_checksum(sender_dpid, packet);
		}
		return 0;
	case NET_PACKET_SESSION_ABORT:
		g_flight_net_host_abort_received = 1;
		g_flight_mission_state.mission_end_pending = 1;
		g_players[g_local_player].participation_state = 0;
		return 1;
	case NET_PACKET_RESYNC_CHUNK_ACK: {
		unsigned int chunk_index;

		g_flight_net_world_state_ack_received_flag = 1;
		chunk_index = (unsigned int)packet[1];
		if (chunk_index < WORLD_STATE_CHUNK_COUNT) {
			g_flight_net_world_state_chunk_acked[chunk_index] = 1;
		}
		return 0;
	}
	case NET_PACKET_PLAYER_ABORT: {
		int player_index = packet[1];
		if (sender_dpid != net_session_get_host_dplay_id() &&
		    ((unsigned)player_index >= XVT_FLIGHT_PLAYERS ||
		     g_players[player_index].network.direct_play_id !=
			     sender_dpid)) {
			return 0;
		}
		if (xvt_flight_network_player_abort((unsigned)player_index)) {
			return 0;
		}

		if (player_index >= 0 && player_index < PLAYER_COUNT) {
			g_player_abort_flags[player_index] = 1;
		}
		if (player_index != g_local_player) {
			return 0;
		}
		g_flight_mission_state.mission_end_pending = 1;
		g_players[g_local_player].participation_state = 0;
		g_player_abort_flags[g_local_player] = 1;
		flight_net_mark_pilot_network_player_left(g_local_player);
		return 1;
	}
	case NET_PACKET_RESYNC_NOTICE:
		g_flight_net_resync_player_dplay_id = packet[1];
		return 0;
	case NET_PACKET_SERVER_CHECKSUM:
		flight_sync_handle_server_checksum_packet((uint8_t *)packet);
		flight_net_send_clock_probe_to_host();
		return 0;
	case NET_PACKET_ACK:
		if (g_flight_net_pending_ack_count != 0) {
			--g_flight_net_pending_ack_count;
			if (g_flight_net_pending_ack_count == 0) {
				g_flight_net_world_message_turn_timestamp = 0;
				return 1;
			}
		}
		return 0;
	case NET_PACKET_CLOCK_LEAD:
		g_flight_net_clock_lead_ticks = packet[1];
		return 0;
	case NET_PACKET_STILL_LOADING:
		if (net_session_get_host_dplay_id() == sender_dpid) {
			g_flight_net_host_timeout_elapsed_ticks = 0;
		} else {
			int player_index = net_session_find_player_slot_by_dpid(
				sender_dpid);

			if (g_players[player_index].participation_state != 0 &&
			    g_flight_net_peer_silence_ticks[player_index] > 0) {
				g_flight_net_peer_silence_ticks[player_index] =
					0;
			}
		}
		return 0;
	case NET_PACKET_CLOCK_PROBE:
		xvt_flight_network_answer_clock_probe(sender_dpid, packet);
		return 0;
	case NET_PACKET_CLOCK_PROBE_REPLY:
		xvt_flight_network_apply_clock_probe_reply(packet);
		return 0;
	default:
		return 0;
	}
}

void xvt_flight_network_process_packets(void)
{
	if (!xvt_flight_network_cookie() ||
	    !g_players[g_local_player].participation_state) {
		return;
	}
	int current_timestamp =
		g_input_timestamp + (int)time_consume_elapsed_ticks();
	while (xvt_flight_network_take_packet_budget()) {
		int sender, size;
		int *packet = net_session_receive_game_packet(&sender, &size);
		current_timestamp += (int)time_consume_elapsed_ticks();
		if (!packet) {
			if (!net_session_is_local_host() ||
			    !xvt_flight_network_take_world_send_turn(
				    g_input_timestamp)) {
				break;
			}
			xvt_flight_network_send_world();
			current_timestamp += (int)time_consume_elapsed_ticks();
			continue;
		}
		if (!xvt_flight_network_decode_control((const uint8_t *)packet,
						       &size) ||
		    (unsigned)net_session_find_player_slot_by_dpid(sender) >=
			    XVT_FLIGHT_PLAYERS) {
			continue;
		}
		if (xvt_flight_network_receive(sender, (const uint8_t *)packet,
					       size) ||
		    xvt_resync_receive_packet(sender, (const uint8_t *)packet,
					      size)) {
			continue;
		}
		if (xvt_flight_network_control(sender, packet)) {
			return;
		}
	}
	g_input_timestamp =
		current_timestamp + (int)time_consume_elapsed_ticks();
}

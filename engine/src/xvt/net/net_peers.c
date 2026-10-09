#include "xvt/net/net.h"

#include <stdio.h>
#include <string.h>

#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net_reliable.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"

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

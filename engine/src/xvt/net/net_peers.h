#ifndef XVT_NET_NET_PEERS_H
#define XVT_NET_NET_PEERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The lobby's peer slots, the sequence records and queue removal, the silent
 * peers, and the latency and packet figures kept for each player. */

int net_compact_reliable_peer_slots_for_roster(void);
int net_check_and_record_incoming_sequence(int player_id, int sequence_id,
					   int use_channel0, int use_channel2);
int net_find_queued_sequenced_packet(int unused_queue_index, int sequence_id,
				     int use_channel0, int use_channel2,
				     int peer_slot_index);
int net_remove_incoming_packet_at_index(unsigned int queue_index);
unsigned int net_get_average_latency_ms(int player_id);
int net_set_player_latency_ms(int player_id, int latency_ms);
unsigned int net_find_or_create_peer_slot(int direct_play_id);
int net_get_packet_drop_rate_basis_points(int player_id);
int net_drop_silent_peers(void);
int net_get_player_packet_count(int player_id);
int net_get_player_packet_drop_count(int player_id);
int net_get_player_packet_retry_count(int player_id);
int net_set_player_packet_count(int player_id, int packet_count);
int net_set_player_packet_drop_count(int player_id, int packet_drop_count);
int net_set_player_packet_retry_count(int player_id, int packet_retry_count);

#ifdef __cplusplus
}
#endif

#endif

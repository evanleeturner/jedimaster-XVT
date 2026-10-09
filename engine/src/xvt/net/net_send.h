#ifndef XVT_NET_NET_SEND_H
#define XVT_NET_NET_SEND_H

#ifdef __cplusplus
extern "C" {
#endif

/* The lobby's sends: on the channel the destination picks, with a second copy
 * at once, outside the sequence scheme, a resend under its old sequence, and
 * the keepalives. */

int net_send_packet_and_flush(int to_player_id, const void *packet,
			      unsigned int packet_size);
int net_send_packet_internal(int to_player_id, const void *packet,
			     unsigned int packet_size);
int net_send_direct_play_packet(int dest_player_id, const void *packet,
				int packet_size, int unused_send_mode);
int net_send_sequenced_direct_play_packet(int dest_player_id, int packet_class,
					  int sequence_id, const void *packet,
					  unsigned int packet_size);
int net_send_sequence_keepalives(void);

#ifdef __cplusplus
}
#endif

#endif

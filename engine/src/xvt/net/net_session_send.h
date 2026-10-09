#ifndef XVT_NET_NET_SESSION_SEND_H
#define XVT_NET_NET_SESSION_SEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The flight session's sends: to every roster player, to one id on its
 * channel, a resend under its old sequence, a control packet, and the
 * keepalives. */

int net_session_broadcast_packet_to_players(unsigned int *payload,
					    int payload_size);
int net_session_send_packet(int direct_play_id, unsigned int *payload,
			    signed int payload_size);
int net_session_send_sequenced_game_packet(int dest_dplay_id,
					   uint8_t packet_class,
					   uint8_t sequence,
					   const unsigned int *packet,
					   unsigned int packet_size);
int net_session_send_compact_game_packet(int direct_play_id,
					 unsigned int *payload,
					 int payload_size, ...);
int net_session_send_reliable_keepalives(void);

#ifdef __cplusplus
}
#endif

#endif

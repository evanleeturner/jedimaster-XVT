#ifndef XVT_NET_NET_SESSION_RECEIVE_H
#define XVT_NET_NET_SESSION_RECEIVE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The flight session's receive: the next game packet, or the next packet
 * of any kind, in order per peer and channel. */

int *net_session_receive_game_packet(int *out_sender_dpid,
				     int *out_payload_size);
void *net_session_receive_packet(int *out_sender_dpid, int *out_payload_size);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_NET_NET_SESSION_PUMP_H
#define XVT_NET_NET_SESSION_PUMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* The flight session's receive pump, which moves the packets DirectPlay
 * holds for this player into the receive queue. */

void net_session_pump_incoming_packets(void);

#ifdef __cplusplus
}
#endif

#endif

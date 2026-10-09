#ifndef XVT_NET_NET_PUMP_H
#define XVT_NET_NET_PUMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* The lobby's receive pump, which moves the messages DirectPlay holds for the
 * local player into the lobby receive queue. */

void net_pump_incoming_packets(void);

#ifdef __cplusplus
}
#endif

#endif

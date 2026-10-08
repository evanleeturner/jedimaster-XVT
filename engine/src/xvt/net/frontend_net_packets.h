#ifndef XVT_NET_FRONTEND_NET_PACKETS_H
#define XVT_NET_FRONTEND_NET_PACKETS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The frontend's packet handler, which the lobby, mission setup, briefing
 * and debriefing screens call each frame. */

int frontend_net_process_network_packets(void);

#ifdef __cplusplus
}
#endif

#endif

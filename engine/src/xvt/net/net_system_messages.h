#ifndef XVT_NET_NET_SYSTEM_MESSAGES_H
#define XVT_NET_NET_SYSTEM_MESSAGES_H

#ifdef __cplusplus
extern "C" {
#endif

/* The lobby's handling of DirectPlay's system messages. */

void net_handle_direct_play_system_message(int packet_type,
					   const void *packet_data);

#ifdef __cplusplus
}
#endif

#endif

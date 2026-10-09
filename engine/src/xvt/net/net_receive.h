#ifndef XVT_NET_NET_RECEIVE_H
#define XVT_NET_NET_RECEIVE_H

#include <stdint.h>

#include "aeron/compat/dplay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The lobby's dequeue, which hands out the next lobby packet in sequence
 * order. */

void *net_dequeue_incoming_packet(DPID *out_sender_id,
				  uint32_t *out_packet_size);

#ifdef __cplusplus
}
#endif

#endif

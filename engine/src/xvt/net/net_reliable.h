#ifndef XVT_NET_NET_RELIABLE_H
#define XVT_NET_NET_RELIABLE_H

#include <stdint.h>

#include "aeron/compat/dplay.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/* One peer of the reliable layer that the lobby (g_front_state) and the flight
 * session (g_net_session) each keep in a table of 40. A peer has three
 * channels, each with its own sequence numbers, 0 to 127: broadcast (packets
 * sent to every player), group (packets sent to the session's group) and
 * one-player (packets sent to this peer alone). */
struct net_reliable_peer_slot {
	/* The peer's DirectPlay id, or the group's in the host's first lobby
	 * slot; 0 when the slot is free. */
	DPID direct_play_id;
	/* Sequence of the last packet from this peer delivered to the game on
	 * the one-player channel; 127 at start, so 0 comes next. */
	int last_delivered_seq_default;
	/* Newest sequence received from this peer on the one-player channel;
	 * 127 at start. A keepalive asks the peer for the one after it. */
	int recv_seq_default;
	int last_delivered_seq_channel_a; /* Last delivered, broadcast channel. */
	int recv_seq_channel_a; /* Newest received, broadcast channel. */
	int last_delivered_seq_channel_b; /* Last delivered, group channel. */
	int recv_seq_channel_b;		  /* Newest received, group channel. */
	int send_seq; /* Next sequence for a packet sent to this peer alone. */
	/* Type of the last packet sent to this peer alone; with the bytes after
	 * it, the trailer the next such packet carries so the peer can recover
	 * a lost one. NOP at start. */
	uint8_t last_piggyback_type;
	uint8_t piggyback_payload[511]; /* Body of that last packet. */
	/* Trailer bytes, type byte included; 1 at start. */
	int piggyback_length;
	/* Time in ms of the last delivery from this peer or keepalive sent to
	 * it; a keepalive goes out after 3,000 ms without either. */
	uint32_t last_activity_ms;
	/* Counts packets delivered from this peer (in internet play the flight
	 * session leaves remote inputs out on one path). */
	int packet_count;
	/* Counts losses on the link with this peer: its requests to resend, and
	 * its packets recovered from a trailer. */
	int packet_drop_count;
	/* Gaps in this peer's packets for which a resend was requested. */
	int packet_retry_count;
	/* Time in ms the lobby last heard from this peer; the flight session's
	 * code leaves it alone. net_drop_silent_peers acts after 45,000 ms. */
	uint32_t last_heard_ms;
};

#pragma pack(pop)
typedef char xvt_size_net_reliable_peer_slot
	[(sizeof(struct net_reliable_peer_slot) == 568) ? 1 : -1];

#pragma pack(push, 1)

/* One packet in a receive queue or a sent history. */
struct net_queued_packet {
	/* Sender in a receive queue, where 0 marks a DirectPlay system message;
	 * destination in a sent history, where 0 means every player. */
	DPID direct_play_id;
	/* Bytes of payload in use; 0 marks an empty history entry. */
	uint32_t payload_size;
	/* Time in ms a resend was last requested for the gap this packet
	 * revealed; 0 when none was. */
	int last_nack_ms;
	uint8_t nack_retry_count; /* Resend requests sent for that gap. */
	uint8_t packet_class; /* Channel: 0 broadcast, 1 one player, 2 group. */
	uint8_t sequence_byte; /* Sequence on that channel, 0 to 127. */
	/* 1 when the packet came in as a resend; only such entries fill a gap
	 * when the receiver searches the queue. */
	uint8_t is_resent_copy;
	/* A 4-byte type word, then the body; a DirectPlay system message is
	 * kept as received. */
	uint8_t payload[512];
};

#pragma pack(pop)
typedef char xvt_size_net_queued_packet
	[(sizeof(struct net_queued_packet) == 528) ? 1 : -1];

/* Queued rename messages retain both NUL-terminated names after the ABI header.
 * The pointer fields in the copied header are not used by the recovered readers. */
struct net_player_name_message {
	DPMSG_SETPLAYERORGROUPNAME header; /* DirectPlay's rename message. */
	/* The short name, then the long name, each ending in a NUL. */
	char names[sizeof(((struct net_queued_packet *)0)->payload) -
		   sizeof(DPMSG_SETPLAYERORGROUPNAME)];
};

#pragma pack(push, 1)

/* The lobby's trailer store for its broadcast and group channels. */
struct net_piggyback_payload {
	/* Type byte, then the body, of the last packet sent on the channel. */
	uint8_t payload[512];
	int payload_length; /* Bytes of payload in use, type byte included. */
	/* Nonzero after a reset: the next packet on the channel carries a NOP
	 * trailer instead, and clears it. */
	int piggyback_empty;
};

#pragma pack(pop)
typedef char xvt_size_net_piggyback_payload
	[(sizeof(struct net_piggyback_payload) == 520) ? 1 : -1];

extern int g_net_recv_queue_read_index;
extern int g_net_recv_queue_write_index;
extern unsigned int g_net_recv_queue_count;
extern struct net_queued_packet g_net_session_recv_queue[1024];
extern int g_net_last_delivered_recv_sequence;

int net_reliable_check_and_record_recv_sequence(int direct_play_id,
						int sequence, int channel_a,
						int channel_b);
int net_reliable_find_queued_recv_packet(int unused_search_index,
					 int remote_seq, int channel_a,
					 int channel_b, int peer_slot);
int net_reliable_remove_queued_packet(unsigned int queue_index);
unsigned int net_reliable_find_or_create_peer_slot(int direct_play_id);
int net_reliable_get_peer_packet_drop_count_by_dpid(int direct_play_id);
int net_reliable_keep_only_host_received_packets(void);

#ifdef __cplusplus
}
#endif

#endif

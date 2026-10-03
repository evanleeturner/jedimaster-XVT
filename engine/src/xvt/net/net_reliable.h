#ifndef XVT_NET_NET_RELIABLE_H
#define XVT_NET_NET_RELIABLE_H

#include "aeron/compat/dplay.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/* One peer of the reliable layer that the lobby (g_frontState) and the flight
 * session (g_netSession) each keep in a table of 40. A peer has three
 * channels, each with its own sequence numbers, 0 to 127: broadcast (packets
 * sent to every player), group (packets sent to the session's group) and
 * one-player (packets sent to this peer alone). */
struct NetReliablePeerSlot {
	/* The peer's DirectPlay id, or the group's in the host's first lobby
	 * slot; 0 when the slot is free. */
	DPID directPlayId;
	/* Sequence of the last packet from this peer delivered to the game on
	 * the one-player channel; 127 at start, so 0 comes next. */
	int lastDeliveredSeqDefault;
	/* Newest sequence received from this peer on the one-player channel;
	 * 127 at start. A keepalive asks the peer for the one after it. */
	int recvSeqDefault;
	int lastDeliveredSeqChannelA; /* Last delivered, broadcast channel. */
	int recvSeqChannelA;	      /* Newest received, broadcast channel. */
	int lastDeliveredSeqChannelB; /* Last delivered, group channel. */
	int recvSeqChannelB;	      /* Newest received, group channel. */
	int sendSeq; /* Next sequence for a packet sent to this peer alone. */
	/* Type of the last packet sent to this peer alone; with the bytes after
	 * it, the trailer the next such packet carries so the peer can recover
	 * a lost one. NOP at start. */
	uint8_t lastPiggybackType;
	uint8_t piggybackPayload[511]; /* Body of that last packet. */
	/* Trailer bytes, type byte included; 1 at start. */
	int piggybackLength;
	/* Time in ms of the last delivery from this peer or keepalive sent to
	 * it; a keepalive goes out after 3,000 ms without either. */
	uint32_t lastActivityMs;
	/* Counts packets delivered from this peer (in internet play the flight
	 * session leaves remote inputs out on one path). */
	int packetCount;
	/* Counts losses on the link with this peer: its requests to resend, and
	 * its packets recovered from a trailer. */
	int packetDropCount;
	/* Gaps in this peer's packets for which a resend was requested. */
	int packetRetryCount;
	/* Time in ms the lobby last heard from this peer; the flight session's
	 * code leaves it alone. Net_DropSilentPeers acts after 45,000 ms. */
	uint32_t lastHeardMs;
};

#pragma pack(pop)
typedef char xvt_size_NetReliablePeerSlot[(sizeof(NetReliablePeerSlot) == 568)
						  ? 1
						  : -1];

#pragma pack(push, 1)

/* One packet in a receive queue or a sent history. */
struct NetQueuedPacket {
	/* Sender in a receive queue, where 0 marks a DirectPlay system message;
	 * destination in a sent history, where 0 means every player. */
	DPID directPlayId;
	/* Bytes of payload in use; 0 marks an empty history entry. */
	uint32_t payloadSize;
	/* Time in ms a resend was last requested for the gap this packet
	 * revealed; 0 when none was. */
	int lastNackMs;
	uint8_t nackRetryCount; /* Resend requests sent for that gap. */
	uint8_t packetClass;  /* Channel: 0 broadcast, 1 one player, 2 group. */
	uint8_t sequenceByte; /* Sequence on that channel, 0 to 127. */
	/* 1 when the packet came in as a resend; only such entries fill a gap
	 * when the receiver searches the queue. */
	uint8_t isResentCopy;
	/* A 4-byte type word, then the body; a DirectPlay system message is
	 * kept as received. */
	uint8_t payload[512];
};

#pragma pack(pop)
typedef char
	xvt_size_NetQueuedPacket[(sizeof(NetQueuedPacket) == 528) ? 1 : -1];

/* Queued rename messages retain both NUL-terminated names after the ABI header.
 * The pointer fields in the copied header are not used by the recovered readers. */
typedef struct NetPlayerNameMessage {
	DPMSG_SETPLAYERORGROUPNAME header; /* DirectPlay's rename message. */
	/* The short name, then the long name, each ending in a NUL. */
	char names[sizeof(((NetQueuedPacket *)0)->payload) -
		   sizeof(DPMSG_SETPLAYERORGROUPNAME)];
} NetPlayerNameMessage;

#pragma pack(push, 1)

/* The lobby's trailer store for its broadcast and group channels. */
struct NetPiggybackPayload {
	/* Type byte, then the body, of the last packet sent on the channel. */
	uint8_t payload[512];
	int payloadLength; /* Bytes of payload in use, type byte included. */
	/* Nonzero after a reset: the next packet on the channel carries a NOP
	 * trailer instead, and clears it. */
	int piggybackEmpty;
};

#pragma pack(pop)
typedef char xvt_size_NetPiggybackPayload[(sizeof(NetPiggybackPayload) == 520)
						  ? 1
						  : -1];

extern int g_netRecvQueueReadIndex;
extern int g_netRecvQueueWriteIndex;
extern unsigned int g_netRecvQueueCount;
extern NetQueuedPacket g_netSessionRecvQueue[1024];
extern int g_netLastDeliveredRecvSequence;

int NetReliable_GetLastDeliveredRecvSequence(void);
int NetReliable_CheckAndRecordRecvSequence(int directPlayId, int sequence,
					   int channelA, int channelB);
int NetReliable_FindQueuedRecvPacket(int unusedSearchIndex, int remoteSeq,
				     int channelA, int channelB, int peerSlot);
int NetReliable_RemoveQueuedPacket(unsigned int queueIndex);
unsigned int NetReliable_FindOrCreatePeerSlot(int directPlayId);
void NetReliable_ResetRecvQueueState(void);
int NetReliable_GetPeerPacketDropCountByDpid(int directPlayId);
int NetReliable_KeepOnlyHostReceivedPackets(void);

#ifdef __cplusplus
}
#endif

#endif

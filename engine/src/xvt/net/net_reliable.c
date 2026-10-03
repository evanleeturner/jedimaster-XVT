#include "xvt/net/net_reliable.h"

#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include <stddef.h>
#include <string.h>

/* Index of the next free entry in g_netSessionRecvQueue, 0 to 1023. Six
 * writers; chiefly NetSession_PumpIncomingPackets, which queues arrivals,
 * NetReliable_RemoveQueuedPacket, which steps it back one, and
 * NetSession_ImportRuntimeState, which copies in the lobby's index when a
 * flight session starts. */
// GLOBAL: XVT 0x527024
int g_netRecvQueueWriteIndex = 0;
/* Index of the oldest entry in g_netSessionRecvQueue, 0 to 1023. Written by
 * NetSession_ReceivePacket and NetReliable_RemoveQueuedPacket as entries
 * leave, by NetReliable_ResetRecvQueueState, which sets it to the write index,
 * and by NetSession_ImportRuntimeState. */
// GLOBAL: XVT 0x527028
int g_netRecvQueueReadIndex;
/* Entries held in g_netSessionRecvQueue, 0 to 1024. Nine writers; chiefly
 * NetSession_PumpIncomingPackets, which adds arrivals, and
 * NetReliable_RemoveQueuedPacket, which takes them out.
 * NetSession_InitGameSession sets it to 0 before NetSession_ImportRuntimeState
 * copies in the lobby's count; NetReliable_ResetRecvQueueState sets it to 0. */
// GLOBAL: XVT 0x52702C
unsigned int g_netRecvQueueCount;
/* The flight session's receive queue, a ring of 1024 packets: DirectPlay
 * system messages, packets from other players, resent copies and the local
 * player's own packets, held until NetSession_ReceivePacket hands them out in
 * sequence order. Seven writers; chiefly NetSession_PumpIncomingPackets. */
// GLOBAL: XVT 0x5599A8
NetQueuedPacket g_netSessionRecvQueue[1024];
/* Sequence, 0 to 127, of the last packet NetSession_ReceivePacket delivered;
 * only that function writes it. Read only by
 * NetReliable_GetLastDeliveredRecvSequence, which nothing calls. */
// GLOBAL: XVT 0x60F1BC
int g_netLastDeliveredRecvSequence;

/* Returns g_netLastDeliveredRecvSequence. Nothing in the engine calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x46F650
int NetReliable_GetLastDeliveredRecvSequence(void)
{
	return g_netLastDeliveredRecvSequence;
}

/* Tells whether a flight-session packet's sequence was already received from
 * that player on its channel: broadcast when channelA is set, else group when
 * channelB is set, else one-player. Returns 1 when the sequence is not 1 to 63
 * ahead of the newest one received, counting modulo 128 (a duplicate or a
 * stale packet). Otherwise records it as the newest in
 * g_netSession.reliablePeerSlots and returns 0. Also returns 0, recording
 * nothing, when the call had to add a peer slot or the 40-slot table is
 * full. */
// FUNCTION: XVT 0x46FC00
int NetReliable_CheckAndRecordRecvSequence(int directPlayId, int sequence,
					   int channelA, int channelB)
{
	uint32_t savedSlotCount;
	unsigned int slot;
	int recvSequence;
	int delta;

	savedSlotCount = g_netSession.reliablePeerSlotCount;
	slot = NetReliable_FindOrCreatePeerSlot(directPlayId);
	if (savedSlotCount != g_netSession.reliablePeerSlotCount ||
	    slot >= 40) {
		return 0;
	}

	if (channelA) {
		recvSequence =
			g_netSession.reliablePeerSlots[slot].recvSeqChannelA;
	} else if (channelB) {
		recvSequence =
			g_netSession.reliablePeerSlots[slot].recvSeqChannelB;
	} else {
		recvSequence =
			g_netSession.reliablePeerSlots[slot].recvSeqDefault;
	}

	delta = sequence - recvSequence;
	if (delta >= -64 && (delta <= 0 || delta >= 64)) {
		return 1;
	}

	if (channelA) {
		g_netSession.reliablePeerSlots[slot].recvSeqChannelA = sequence;
	} else if (channelB) {
		g_netSession.reliablePeerSlots[slot].recvSeqChannelB = sequence;
	} else {
		g_netSession.reliablePeerSlots[slot].recvSeqDefault = sequence;
	}
	return 0;
}

/* Finds a queued reliable receive packet matching sequence, channel, and peer
 * filters. Returns the ring index or -1. */
/* Searches g_netSessionRecvQueue from the oldest entry and looks only at
 * resent copies. A packet matches when its sender holds slot peerSlot in
 * g_netSession.reliablePeerSlots (a sender with no slot counts as the slot
 * count), its sequence byte equals remoteSeq, and its class is 0 when
 * channelA is set, 2 when channelB is set, else neither. The first argument
 * is ignored. */
// FUNCTION: XVT 0x46FCE0
int NetReliable_FindQueuedRecvPacket(int unusedSearchIndex, int remoteSeq,
				     int channelA, int channelB, int peerSlot)
{
	unsigned int i;
	int index;
	int sequenceByte;
	int isClass0;
	int isClass2;
	unsigned int slot;
	DPID directPlayId;
	NetReliablePeerSlot *peer;

	(void)unusedSearchIndex;

	index = g_netRecvQueueReadIndex;
	for (i = g_netRecvQueueCount; i != 0; --i) {
		if (g_netSessionRecvQueue[index].isResentCopy != 0) {
			directPlayId =
				g_netSessionRecvQueue[index].directPlayId;
			sequenceByte =
				g_netSessionRecvQueue[index].sequenceByte;
			isClass0 =
				g_netSessionRecvQueue[index].packetClass == 0;
			isClass2 =
				g_netSessionRecvQueue[index].packetClass == 2;
			slot = 0;
			if (g_netSession.reliablePeerSlotCount != 0) {
				peer = g_netSession.reliablePeerSlots;
				while (peer->directPlayId != directPlayId) {
					++peer;
					++slot;
					if (slot >=
					    g_netSession
						    .reliablePeerSlotCount) {
						break;
					}
				}
			}
			if (slot == (unsigned int)peerSlot) {
				if (channelA != 0) {
					if (isClass0 &&
					    sequenceByte == remoteSeq) {
						return index;
					}
				} else if (channelB != 0) {
					if (isClass2 &&
					    sequenceByte == remoteSeq) {
						return index;
					}
				} else if (!isClass0 && !isClass2 &&
					   sequenceByte == remoteSeq) {
					return index;
				}
			}
		}
		if ((unsigned int)++index >= 0x400) {
			index = 0;
		}
	}
	return -1;
}

/* Takes the entry at queueIndex out of g_netSessionRecvQueue and lowers
 * g_netRecvQueueCount. At the read index it advances g_netRecvQueueReadIndex
 * and returns 1; anywhere else it moves every later entry down one place,
 * steps g_netRecvQueueWriteIndex back and returns 0. Does not check that an
 * entry is queued at queueIndex. */
// FUNCTION: XVT 0x46FDF0
int NetReliable_RemoveQueuedPacket(unsigned int queueIndex)
{
	if (g_netRecvQueueReadIndex == (int)queueIndex) {
		++g_netRecvQueueReadIndex;
		--g_netRecvQueueCount;
		if (g_netRecvQueueReadIndex >= 1024) {
			g_netRecvQueueReadIndex = 0;
		}
		return 1;
	} else {
		unsigned int srcIndex;
		unsigned int endIndex;

		srcIndex = queueIndex + 1;
		if (srcIndex >= 1024u) {
			srcIndex = 0;
		}
		endIndex = g_netRecvQueueReadIndex + g_netRecvQueueCount;
		if (endIndex >= 1024u) {
			endIndex -= 1024u;
		}

		while (endIndex != srcIndex) {
			memcpy(&g_netSessionRecvQueue[queueIndex],
			       &g_netSessionRecvQueue[srcIndex],
			       sizeof(g_netSessionRecvQueue[queueIndex]));
			++queueIndex;
			if (queueIndex >= 1024u) {
				queueIndex = 0;
			}
			++srcIndex;
			if (srcIndex >= 1024u) {
				srcIndex = 0;
			}
		}

		--g_netRecvQueueCount;
		if (g_netRecvQueueWriteIndex == 0) {
			g_netRecvQueueWriteIndex = 1023;
		} else {
			--g_netRecvQueueWriteIndex;
		}
		return 0;
	}
}

/* Returns the index of the flight session's peer slot for a DirectPlay id. With
 * none, it adds one at the end of g_netSession.reliablePeerSlots, raising
 * g_netSession.reliablePeerSlotCount: sequences 127 (so 0 comes next), send
 * sequence 0, a NOP trailer, packet and drop counts 0, and lastActivityMs set
 * to timeGetTime. Returns 40, one past the table, when the table is full.
 * Unlike Net_FindOrCreatePeerSlot it leaves a new slot's lastHeardMs and
 * packetRetryCount as they were. */
// FUNCTION: XVT 0x46FEE0
unsigned int NetReliable_FindOrCreatePeerSlot(int directPlayId)
{
	unsigned int slot;
	NetReliablePeerSlot *peer;
	unsigned int initializeSlot;
	unsigned int *peerSlotCount;

	slot = 0;
	if (g_netSession.reliablePeerSlotCount != 0) {
		peer = g_netSession.reliablePeerSlots;
		do {
			if (peer->directPlayId == (DPID)directPlayId) {
				return slot;
			}
			peer++;
			slot++;
		} while (slot < g_netSession.reliablePeerSlotCount);
	}

	if (g_netSession.reliablePeerSlotCount == slot && slot < 40) {
		g_netSession.reliablePeerSlots[slot].directPlayId =
			directPlayId;
		initializeSlot = slot;
		g_netSession.reliablePeerSlots[initializeSlot]
			.lastDeliveredSeqDefault = 127;
		g_netSession.reliablePeerSlots[initializeSlot]
			.lastDeliveredSeqChannelA = 127;
		g_netSession.reliablePeerSlots[initializeSlot]
			.lastDeliveredSeqChannelB = 127;
		g_netSession.reliablePeerSlots[initializeSlot].recvSeqDefault =
			127;
		g_netSession.reliablePeerSlots[initializeSlot].recvSeqChannelA =
			127;
		g_netSession.reliablePeerSlots[initializeSlot].recvSeqChannelB =
			127;
		g_netSession.reliablePeerSlots[initializeSlot].sendSeq = 0;
		g_netSession.reliablePeerSlots[initializeSlot]
			.lastPiggybackType = NET_PACKET_NOP;
		g_netSession.reliablePeerSlots[initializeSlot].piggybackLength =
			1;
		g_netSession.reliablePeerSlots[slot].lastActivityMs =
			timeGetTime();
		g_netSession.reliablePeerSlots[slot].packetCount = 0;
		g_netSession.reliablePeerSlots[slot].packetDropCount = 0;
		peerSlotCount = &g_netSession.reliablePeerSlotCount;
		++*peerSlotCount;
	}

	return slot;
}

/* Drops every packet queued for the flight session (read index set to the
 * write index, count 0). For every peer slot it then counts the newest
 * sequences received as delivered and sets lastActivityMs to timeGetTime.
 * Only the original build calls this. */
// FUNCTION: XVT 0x46FFC0
void NetReliable_ResetRecvQueueState(void)
{
	unsigned int slot;

	g_netRecvQueueReadIndex = g_netRecvQueueWriteIndex;
	g_netRecvQueueCount = 0;

	for (slot = 0; slot < g_netSession.reliablePeerSlotCount; ++slot) {
		g_netSession.reliablePeerSlots[slot].lastDeliveredSeqDefault =
			g_netSession.reliablePeerSlots[slot].recvSeqDefault;
		g_netSession.reliablePeerSlots[slot].lastDeliveredSeqChannelA =
			g_netSession.reliablePeerSlots[slot].recvSeqChannelA;
		g_netSession.reliablePeerSlots[slot].lastDeliveredSeqChannelB =
			g_netSession.reliablePeerSlots[slot].recvSeqChannelB;
		g_netSession.reliablePeerSlots[slot].lastActivityMs =
			timeGetTime();
	}
}

/* Returns packetDropCount of the flight session's peer slot for a DirectPlay
 * id. With no such slot the modern build returns -1, and the original build
 * reaches the end with no return statement, so the value is undefined. */
// FUNCTION: XVT 0x470020
int NetReliable_GetPeerPacketDropCountByDpid(int directPlayId)
{
	unsigned int slot;
	NetReliablePeerSlot *peer;

	slot = 0;
	if (g_netSession.reliablePeerSlotCount != 0) {
		peer = g_netSession.reliablePeerSlots;
		do {
			if ((DPID)directPlayId == peer->directPlayId) {
				return g_netSession.reliablePeerSlots[slot]
					.packetDropCount;
			}
			peer++;
			slot++;
		} while (slot < g_netSession.reliablePeerSlotCount);
	}
#ifdef XVT_MODERN
	return -1;
#endif
}

#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma function(memcpy)
#endif
/* Drops every packet in g_netSessionRecvQueue except DirectPlay system
 * messages (sender 0) and packets from g_netSession.hostDplayId, packing the
 * kept ones from the read index on and setting g_netRecvQueueCount and
 * g_netRecvQueueWriteIndex to match. For every peer slot but the host's it
 * then counts the newest sequences received as delivered and sets
 * lastActivityMs to timeGetTime. Returns 1. The flight session's receive and
 * send code calls it when the receive queue is full. */
// FUNCTION: XVT 0x470060
int NetReliable_KeepOnlyHostReceivedPackets(void)
{
	unsigned int dstIndex;
	unsigned int srcIndex;
	unsigned int keptCount;
	unsigned int slot;
	DPID directPlayId;
	NetQueuedPacket *sourcePacket;
	NetReliablePeerSlot *peer;

	dstIndex = (unsigned int)g_netRecvQueueReadIndex;
	srcIndex = (unsigned int)g_netRecvQueueReadIndex;
	keptCount = 0;
	while ((int)g_netRecvQueueCount > 0) {
		sourcePacket = &g_netSessionRecvQueue[srcIndex];
		directPlayId = sourcePacket->directPlayId;
		if (directPlayId == 0 ||
		    (DPID)g_netSession.hostDplayId == directPlayId) {
			memcpy(&g_netSessionRecvQueue[dstIndex], sourcePacket,
			       sizeof(*sourcePacket));
			++dstIndex;
			if (dstIndex >= 1024) {
				dstIndex = 0;
			}
			++keptCount;
		}

		++srcIndex;
		if (srcIndex >= 1024) {
			srcIndex = 0;
		}
		--g_netRecvQueueCount;
	}

	g_netRecvQueueCount = keptCount;
	g_netRecvQueueWriteIndex = (int)dstIndex;
	slot = 0;
	if (g_netSession.reliablePeerSlotCount != 0) {
		peer = g_netSession.reliablePeerSlots;
		do {
			if (peer->directPlayId !=
			    (DPID)g_netSession.hostDplayId) {
				peer->lastDeliveredSeqDefault =
					peer->recvSeqDefault;
				peer->lastDeliveredSeqChannelA =
					peer->recvSeqChannelA;
				peer->lastDeliveredSeqChannelB =
					peer->recvSeqChannelB;
				peer->lastActivityMs = timeGetTime();
			}
			++peer;
			++slot;
		} while (g_netSession.reliablePeerSlotCount > slot);
	}

	return 1;
}
#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma intrinsic(memcpy)
#endif

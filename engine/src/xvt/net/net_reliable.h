#ifndef XVT_NET_NET_RELIABLE_H
#define XVT_NET_NET_RELIABLE_H

#include "aeron/compat/dplay.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct NetReliablePeerSlot {
	DPID directPlayId;
	int prevRecvSeqDefault;
	int recvSeqDefault;
	int prevRecvSeqChannelA;
	int recvSeqChannelA;
	int prevRecvSeqChannelB;
	int recvSeqChannelB;
	int sendSeq;
	uint8_t lastPiggybackType;
	uint8_t piggybackPayload[511];
	int piggybackLength;
	uint32_t lastActivityMs;
	int packetCount;
	int packetDropCount;
	int packetRetryCount;
	uint32_t lastKeepaliveMs;
};

#pragma pack(pop)
typedef char xvt_size_NetReliablePeerSlot[(sizeof(NetReliablePeerSlot) == 568) ? 1 : -1];

#pragma pack(push, 1)

struct NetQueuedPacket {
	DPID directPlayId;
	uint32_t payloadSize;
	int aux;
	uint8_t meta0;
	uint8_t packetClass;
	uint8_t sequenceByte;
	uint8_t queuedFlag;
	uint8_t payload[512];
};

#pragma pack(pop)
typedef char xvt_size_NetQueuedPacket[(sizeof(NetQueuedPacket) == 528) ? 1 : -1];

/* Queued rename messages retain both NUL-terminated names after the ABI header.
 * The pointer fields in the copied header are not used by the recovered readers. */
typedef struct NetPlayerNameMessage {
	DPMSG_SETPLAYERORGROUPNAME header;
	char names[sizeof(((NetQueuedPacket*)0)->payload) - sizeof(DPMSG_SETPLAYERORGROUPNAME)];
} NetPlayerNameMessage;

#pragma pack(push, 1)

struct NetPendingPayload {
	uint8_t payload[512];
	int payloadLength;
	int pendingFlush;
};

#pragma pack(pop)
typedef char xvt_size_NetPendingPayload[(sizeof(NetPendingPayload) == 520) ? 1 : -1];

extern int g_netRecvQueueReadIndex;
extern int g_netRecvQueueWriteIndex;
extern unsigned int g_netRecvQueueCount;
extern NetQueuedPacket g_netSessionRecvQueue[1024];
extern int g_netLastDeliveredRecvSequence;

int NetReliable_GetLastDeliveredRecvSequence(void);
int NetReliable_IsDuplicateRecvSequence(int directPlayId, int sequence, int channelA, int channelB);
int NetReliable_FindQueuedRecvPacket(int unused, int remoteSeq, int wantType0, int wantType2, int peerSlot);
int NetReliable_RemoveQueuedPacket(unsigned int queueIndex);
unsigned int NetReliable_FindOrCreatePeerSlot(int directPlayId);
void NetReliable_ResetRecvQueueState(void);
int NetReliable_GetPeerPacketDropCountByDpid_0(int directPlayId);
int NetReliable_CompactLocalReceiveQueue(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_NET_NET_SESSION_H
#define XVT_NET_NET_SESSION_H

#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct SessionPlayerInfo {
	char sessionName[16];
	char playerName[16];
	int directPlayId;
	int activeFlag;
};

#pragma pack(pop)
typedef char xvt_size_SessionPlayerInfo[(sizeof(SessionPlayerInfo) == 40) ? 1 : -1];

#pragma pack(push, 1)

struct NetSessionScratchPacket {
	int packetType;
	int payloadDwords[127];
};

#pragma pack(pop)
typedef char xvt_size_NetSessionScratchPacket[(sizeof(NetSessionScratchPacket) == 512) ? 1 : -1];

#pragma pack(push, 1)

typedef struct NetSessionScratchState {
	int packetType;
	int payloadDwords[127];
	int trailingState;
} NetSessionScratchState;

#pragma pack(pop)
typedef char xvt_size_NetSessionScratchState[(sizeof(NetSessionScratchState) == 516) ? 1 : -1];

/* DirectPlay flight-session state is reset as one block by the original game. */
#pragma pack(push, 1)

typedef struct NetSessionState {
	uint32_t reservedState0;
	IDirectPlay2A* dplayInterface;
	uint32_t reservedState1;
	NetworkTransportType networkType;
	GUID appGuid;
	GUID instanceGuid;
	int groupDplayId;
	int localPlayerId;
	int hostDplayId;
	int playerCount;
	int receivePumpState;
	uint8_t reservedSessionState[32];
	int reliableUseFixedResendTimeouts;
	SessionPlayerInfo localPlayerInfo;
	SessionPlayerInfo players[8];
	SessionPlayerInfo playerInfoQueue[8];
	uint32_t broadcastSeqCounter;
	uint8_t broadcastPayload[512];
	int broadcastPayloadLength;
	int broadcastPendingFlush;
	uint32_t groupSeqCounter;
	uint8_t groupPayload[512];
	int groupPayloadLength;
	int groupPendingFlush;
	NetReliablePeerSlot reliablePeerSlots[40];
	NetReliablePeerSlot reliablePeerScratch;
	unsigned int reliablePeerSlotCount;
	int playerInfoQueueCount;
	NetQueuedPacket recvScratchPacket;
} NetSessionState;

#pragma pack(pop)
typedef char xvt_size_NetSessionState[(sizeof(NetSessionState) == 25652 + sizeof(void*)) ? 1 : -1];

extern NetSessionState g_netSession;

extern NetSessionScratchState g_netSessionScratchPacket;
extern int g_netSessionRecvHistoryCount;
extern int g_netSessionRecvQueueHighWater;
extern NetQueuedPacket g_netSessionRecvHistory[128];
extern NetQueuedPacket g_netSessionExportRecvQueue[256];

void NetSession_DebugTrace(const char* message);
int NetSession_InitGameSession(const char* sessionName, const char* pilotName, int localId,
							   const char* mpGameName, NetworkTransportType networkType, int numHumanPlayers,
							   int inProgressLaunch, const char* connectionAddress);
int NetSession_Shutdown(void);
int NetSession_EnumeratePlayers(void);
int AERON_DXAPI NetSession_EnumPlayersCallback(DPID dplayId, uint32_t playerType, const DPNAME* nameInfo,
											   uint32_t flags, void* context);
/* Only contiguous retransmissions advance the channel receive watermark. */
static inline void NetSession_AdvanceReceivedSequence(int* receivedSequence, unsigned int sequence) {
	unsigned int nextSequence = (unsigned int)*receivedSequence + 1;
	if (nextSequence > 127)
		nextSequence = 0;
	if (nextSequence == sequence)
		*receivedSequence = nextSequence;
}

void NetSession_PumpIncomingPackets(void);
int NetSession_BroadcastPacketToPlayers(unsigned int* payload, int payloadSize);
int NetSession_SendPacket(int directPlayId, unsigned int* payload, signed int payloadSize);
int NetSession_SendSequencedGamePacket(int destDplayId, uint8_t localSeq, uint8_t remoteSeq,
									   const unsigned int* packet, unsigned int packetSize);
SessionPlayerInfo* NetSession_GetPlayerRoster(int* outCount);
SessionPlayerInfo* NetSession_GetLocalPlayerInfo(void);
int NetSession_SetPlayerRoster(const SessionPlayerInfo* players, int playerCount);
int NetSession_GetPlayerCount(void);
int NetSession_GetLocalPlayerId(void);
int* NetSession_ReceiveGamePacket(int* outSenderDpid, int* outPayloadSize);
int NetSession_HandleHandshakePacket(int packetOpcode, int* packet);
void* NetSession_ReceivePacket(int* outSenderDpid, int* outPayloadSize);
int NetSession_SendCompactGamePacket(int directPlayId, unsigned int* payload, int payloadSize, ...);
int* NetSession_WaitForGamePacket(int* outDpid, int* outAux, int timeoutSeconds);
int NetSession_FindPlayerSlotByDpid(int dpid);
int NetSession_GetPlayerDplayId(int playerIndex);
int NetSession_GetDplayIdByActivePlayerIndex(int activePlayerIndex);
int NetSession_FindActivePlayerIndexByDpid(int dpid);
int NetSession_GetHostDplayId(void);
int NetSession_GetLocalDplayId(void);
char* NetSession_GetPlayerName(int playerSlot);
SessionPlayerInfo* NetSession_PeekQueuedPlayerInfo(void);
void NetSession_DiscardFirstQueuedPlayerInfo(void);
int NetSession_CountLeadingActivePlayers(void);
void NetSession_SetPlayerCount(int playerCount);
void NetSession_AddPlayerToRosterSlot(int playerSlot, const SessionPlayerInfo* playerInfo);
int NetSession_BroadcastPlayerRoster(int toPlayerId);
int NetSession_GetQueuedPlayerInfoCount(void);
void NetSession_QueuePlayerInfo(const SessionPlayerInfo* playerInfo);
int NetSession_AddPlayerToGroup(const SessionPlayerInfo* playerInfo);
int NetSession_CountActivePlayers(void);
int NetSession_RemovePlayerFromGroup(int playerDplayId);
int NetSession_SelectFirstActivePlayerAsHost(void);
int NetSession_UnusedStubReturnTrue(void);
int NetSession_StubReturnTrue(void);
int NetSession_ExitStub(int packetType);
int NetSession_SendReliableKeepalives(void);

#ifdef __cplusplus
}
#endif

#endif

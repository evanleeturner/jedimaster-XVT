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
	/* DirectPlay long name; the formal name in a solo flight */
	char longName[16];
	char playerName[16]; /* Short name; pilotName if solo */
	int directPlayId;    /* DirectPlay player id */
	int activeFlag;	     /* 1 while the player is in the session */
};

#pragma pack(pop)
typedef char
	xvt_size_SessionPlayerInfo[(sizeof(SessionPlayerInfo) == 40) ? 1 : -1];

#pragma pack(push, 1)

struct NetSessionScratchPacket {
	int packetType;		/* NET_PACKET_ value naming the packet */
	int payloadDwords[127]; /* Body; its layout depends on packetType */
};

#pragma pack(pop)
typedef char xvt_size_NetSessionScratchPacket
	[(sizeof(NetSessionScratchPacket) == 512) ? 1 : -1];

#pragma pack(push, 1)

typedef struct NetSessionScratchState {
	int packetType;		/* NET_PACKET_ value naming the packet */
	int payloadDwords[127]; /* Body; its layout depends on packetType */
	/* Zeroed by NetSession_InitGameSession; nothing reads it */
	int trailingState;
} NetSessionScratchState;

#pragma pack(pop)
typedef char xvt_size_NetSessionScratchState
	[(sizeof(NetSessionScratchState) == 516) ? 1 : -1];

/* DirectPlay flight-session state is reset as one block by the original game. */
#pragma pack(push, 1)

typedef struct NetSessionState {
	/* Never named; only NetSession_InitGameSession's clear sets it */
	uint32_t reservedState0;
	/* Taken from the frontend; NULL when there is none or a startup wait
	 * timed out, and then sends are only queued for this player */
	IDirectPlay2A *dplayInterface;
	/* Never named; only NetSession_InitGameSession's clear sets it */
	uint32_t reservedState1;
	/* Transport given to NetSession_InitGameSession; nothing reads it */
	NetworkTransportType networkType;
	/* DirectPlay application GUID from the frontend; nothing reads it */
	GUID appGuid;
	/* GUID of the joined session, from the frontend; nothing reads it */
	GUID instanceGuid;
	/* DirectPlay group of the players; sends to it use the group channel */
	int groupDplayId;
	int localIsHost; /* Nonzero when this player hosts */
	int hostDplayId; /* The host's DirectPlay id */
	int playerCount; /* Entries in players */
	/* Set to 0 by NetSession_PumpIncomingPackets; nothing reads it */
	int receivePumpState;
	/* Never named; only NetSession_InitGameSession's clear sets it */
	uint8_t reservedSessionState[32];
	/* Only ever set to 0. When nonzero, NetSession_ReceivePacket gives up
	 * a gap a fixed time after one NACK: 3,000 ms, or 40,000 ms for world
	 * messages. */
	int reliableUseFixedResendTimeouts;
	/* This player's own entry, from the frontend */
	SessionPlayerInfo localPlayerInfo;
	SessionPlayerInfo players[8]; /* The roster */
	/* Queue of player entries; only functions nothing calls use it */
	SessionPlayerInfo playerInfoQueue[8];
	/* Next sequence, 0-127, for packets to all players */
	uint32_t broadcastSeqCounter;
	/* Type byte and body of the last packet to all players, sent again
	 * behind the next one */
	uint8_t broadcastPayload[512];
	int broadcastPayloadLength; /* Bytes used in broadcastPayload */
	/* While 1, the next packet to all carries a NOP, not a copy */
	int broadcastPiggybackEmpty;
	/* Next sequence, 0-127, for the group channel, which also carries
	 * internet-play inputs */
	uint32_t groupSeqCounter;
	/* Type byte and body of the last group-channel packet, sent again
	 * behind the next one */
	uint8_t groupPayload[512];
	int groupPayloadLength; /* Bytes used in groupPayload */
	/* While 1, the next group packet carries a NOP, not a copy */
	int groupPiggybackEmpty;
	/* Per-peer delivery state: sequences, saved copy, activity, counts */
	NetReliablePeerSlot reliablePeerSlots[40];
	/* Never named in code. It follows reliablePeerSlots, so
	 * NetSession_SendPacket's unchecked use of slot 40, when all 40 slots
	 * are taken, lands here. */
	NetReliablePeerSlot reliablePeerOverflowSlot;
	unsigned int reliablePeerSlotCount; /* reliablePeerSlots in use */
	int playerInfoQueueCount;	    /* Entries in playerInfoQueue */
	/* Copy of the packet NetSession_ReceivePacket last returned; callers
	 * get a pointer into it */
	NetQueuedPacket recvScratchPacket;
} NetSessionState;

#pragma pack(pop)
typedef char xvt_size_NetSessionState
	[(sizeof(NetSessionState) == 25652 + sizeof(void *)) ? 1 : -1];

extern NetSessionState g_netSession;

extern NetSessionScratchState g_netSessionScratchPacket;
extern int g_netSessionSentHistoryWriteIndex;
extern int g_netSessionSentWorldMessageWriteIndex;
extern NetQueuedPacket g_netSessionSentHistory[128];
extern NetQueuedPacket g_netSessionSentWorldMessageHistory[256];

void NetSession_DebugTrace(const char *message);
int NetSession_InitGameSession(const char *formalName, const char *pilotName,
			       int isHost, const char *mpGameName,
			       NetworkTransportType networkType,
			       int numHumanPlayers, int inProgressLaunch,
			       const char *connectionAddress);
int NetSession_Shutdown(void);
int NetSession_EnumeratePlayers(void);
int AERON_DXAPI NetSession_EnumPlayersCallback(DPID dplayId,
					       uint32_t playerType,
					       const DPNAME *nameInfo,
					       uint32_t flags, void *context);
/* Only contiguous retransmissions advance the channel receive watermark. */
static inline void NetSession_AdvanceReceivedSequence(int *receivedSequence,
						      unsigned int sequence)
{
	unsigned int nextSequence = (unsigned int)*receivedSequence + 1;
	if (nextSequence > 127) {
		nextSequence = 0;
	}
	if (nextSequence == sequence) {
		*receivedSequence = nextSequence;
	}
}

void NetSession_PumpIncomingPackets(void);
int NetSession_BroadcastPacketToPlayers(unsigned int *payload, int payloadSize);
int NetSession_SendPacket(int directPlayId, unsigned int *payload,
			  signed int payloadSize);
int NetSession_SendSequencedGamePacket(int destDplayId, uint8_t packetClass,
				       uint8_t sequence,
				       const unsigned int *packet,
				       unsigned int packetSize);
SessionPlayerInfo *NetSession_GetPlayerRoster(int *outCount);
SessionPlayerInfo *NetSession_GetLocalPlayerInfo(void);
int NetSession_SetPlayerRoster(const SessionPlayerInfo *players,
			       int playerCount);
int NetSession_GetPlayerCount(void);
int NetSession_IsLocalHost(void);
int *NetSession_ReceiveGamePacket(int *outSenderDpid, int *outPayloadSize);
int NetSession_HandleDirectPlaySystemMessage(int packetOpcode, int *packet);
void *NetSession_ReceivePacket(int *outSenderDpid, int *outPayloadSize);
int NetSession_SendCompactGamePacket(int directPlayId, unsigned int *payload,
				     int payloadSize, ...);
int *NetSession_WaitForGamePacket(int *outDpid, int *outPayloadSize,
				  int timeoutSeconds);
int NetSession_FindPlayerSlotByDpid(int dpid);
int NetSession_GetPlayerDplayId(int playerIndex);
int NetSession_GetDplayIdByActivePlayerIndex(int activePlayerIndex);
int NetSession_FindActivePlayerIndexByDpid(int dpid);
int NetSession_GetHostDplayId(void);
int NetSession_GetLocalDplayId(void);
char *NetSession_GetPlayerName(int playerSlot);
SessionPlayerInfo *NetSession_PeekQueuedPlayerInfo(void);
void NetSession_DiscardFirstQueuedPlayerInfo(void);
int NetSession_CountLeadingActivePlayers(void);
void NetSession_SetPlayerCount(int playerCount);
void NetSession_AddPlayerToRosterSlot(int playerSlot,
				      const SessionPlayerInfo *playerInfo);
int NetSession_BroadcastPlayerRoster(int toPlayerId);
int NetSession_GetQueuedPlayerInfoCount(void);
void NetSession_QueuePlayerInfo(const SessionPlayerInfo *playerInfo);
int NetSession_AddPlayerToGroup(const SessionPlayerInfo *playerInfo);
int NetSession_CountActivePlayers(void);
int NetSession_RemovePlayerFromGroup(int playerDplayId);
int NetSession_SelectFirstActivePlayerAsHost(void);
int NetSession_UnusedStubReturnTrue(void);
int NetSession_StubReturnTrue(void);
int NetSession_GetFixedPayloadSize(int packetType);
int NetSession_SendReliableKeepalives(void);

#ifdef __cplusplus
}
#endif

#endif

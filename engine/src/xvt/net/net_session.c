#include "xvt/net/net_session.h"
#ifdef XVT_MODERN
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/network_session.h"
#endif

#include "aeron/compat/dplay.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/frontend/config.h"
#include "xvt/net/net_reliable.h"
#include "xvt/util/time.h"

#include <string.h>

#pragma pack(push, 1)

struct NetSessionCompactEncodedPacket {
	/* Type in bits 0-6, sequence in 8-14, channel bits 0x80, 0x8000 */
	int16_t packetTypeHeader;
	/* Body length when the type takes one; else the body starts here */
	int16_t payloadSize;
	uint8_t payload[1020]; /* Body, then the saved copy or a NOP */
};

struct NetSessionSequencedEncodedPacket {
	/* Type, sequence, and bit 0x80 with bit 15 clear: a resend */
	int16_t packetTypeHeader;
	/* Channel class: 0 all players, 1 direct, 2 group */
	uint8_t packetClass;
	/* Body length when the type takes one; else the body starts here */
	int16_t payloadSize;
	uint8_t payload[1019]; /* Body, then a NOP outside the resync types */
};

#pragma pack(pop)
typedef char xvt_size_NetSessionCompactEncodedPacket
	[(sizeof(struct NetSessionCompactEncodedPacket) == 1024) ? 1 : -1];
typedef char xvt_size_NetSessionSequencedEncodedPacket
	[(sizeof(struct NetSessionSequencedEncodedPacket) == 1024) ? 1 : -1];

/* Next slot, 0-127, that NetSession_SendPacket fills in
 * g_netSessionSentHistory; it wraps to 0. NetSession_InitGameSession takes it
 * from the frontend's saved state, and NetSession_Shutdown hands it back. */
// GLOBAL: XVT 0x52701C
int g_netSessionSentHistoryWriteIndex = 0;
/* Next slot, 0-255, that NetSession_SendPacket fills in
 * g_netSessionSentWorldMessageHistory; it wraps to 0. Zeroed by
 * NetSession_InitGameSession; NetSession_Shutdown hands it to the frontend. */
// GLOBAL: XVT 0x527020
int g_netSessionSentWorldMessageWriteIndex = 0;
/* The last 128 packets this player sent to others, each with its destination,
 * size, channel class (0 all players, 1 direct, 2 group) and sequence, kept so
 * NetSession_PumpIncomingPackets can resend one a peer asks for. Internet-play
 * inputs are not kept. Filled by NetSession_SendPacket;
 * NetSession_InitGameSession loads the frontend's saved copy and
 * NetSession_Shutdown hands it back. */
// GLOBAL: XVT 0x5DD9B8
struct NetQueuedPacket g_netSessionSentHistory[128] = {0};
/* The last 256 world messages this player sent, kept so
 * NetSession_PumpIncomingPackets can resend the one with the tick a WORLD_NACK
 * asks for. Filled by NetSession_SendPacket; cleared by
 * NetSession_InitGameSession. */
// GLOBAL: XVT 0x5EE1B8
struct NetQueuedPacket g_netSessionSentWorldMessageHistory[256] = {0};
/* The flight's network session: DirectPlay interface and ids, the player
 * roster, channel sequences and saved copies, and reliable delivery state per
 * peer. NetSession_InitGameSession clears it and loads it from the frontend's
 * saved state. 18 functions write it, chiefly NetSession_InitGameSession,
 * NetSession_SendPacket, NetSession_PumpIncomingPackets,
 * NetSession_ReceivePacket, NetSession_HandleDirectPlaySystemMessage and the
 * NetReliable_ functions. */
// GLOBAL: XVT 0x9994C0
struct NetSessionState g_netSession = {0};
/* The buffer session packets are built in just before they are sent: startup
 * and roster packets, peer sequence status and keepalives. 6 functions write
 * it: NetSession_InitGameSession, NetSession_HandleDirectPlaySystemMessage,
 * NetSession_BroadcastPlayerRoster, NetSession_SendReliableKeepalives,
 * XvtFlightNetwork_SendRosterRecord and XvtFlightNetwork_ExchangeRoster. */
// GLOBAL: XVT 0x5597A0
struct NetSessionScratchState g_netSessionScratchPacket = {0};
/* Picks the branches NetSession_HandleDirectPlaySystemMessage takes.
 * NetSession_InitGameSession sets it to 1 and nothing sets it back to 0, so
 * after the first session of a run the branches for 0 never run. */
// GLOBAL: XVT 0x9EC608
uint8_t g_netSessionFlightHandshakeActive = 0;

/* Does nothing; message is ignored. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x46C220
void NetSession_DebugTrace(const char *message) { (void)message; }

/* Opens the flight's network session from the state the frontend saved
 * (NetSession_ImportRuntimeState): DirectPlay interface, ids, receive queue,
 * reliable peer slots, channel sequences and sent history. It first clears
 * g_netSession and g_netRecvQueueCount, resets the 40 reliable peer slots,
 * g_netSessionSentWorldMessageHistory and its write index, and
 * g_netSessionScratchPacket.trailingState, and sets
 * g_netSessionFlightHandshakeActive; the import then fills
 * g_netSessionRecvQueue with its indices and count, and g_netSessionSentHistory
 * with its write index. A solo host takes formalName and pilotName as its
 * names, becomes roster slot 0 and returns 1; without a DirectPlay interface
 * its id is 1. Otherwise it lists the DirectPlay players that
 * g_pilotData.networkPlayers also holds, and sends the host a STARTUP_READY and
 * a NOP. The modern build then returns XvtFlightNetwork_BeginRosterExchange's
 * result, which is XVT_FLIGHT_NETWORK_PENDING unless a client joins a flight in
 * progress. The original blocks: the host waits for numHumanPlayers
 * STARTUP_READY packets, its own included, then with more than one sends
 * everyone the roster; a client, unless inProgressLaunch, waits for the roster
 * count and entries. A 60-second wait with no packet returns 0, clearing the
 * DirectPlay interface pointer except while roster entries arrive; else 1.
 * mpGameName and connectionAddress are unused. Does not check for a DirectPlay
 * interface outside the solo path, nor the slot number in a roster entry. */
// FUNCTION: XVT 0x46C230
int NetSession_InitGameSession(const char *formalName, const char *pilotName,
			       int isHost, const char *mpGameName,
			       NetworkTransportType networkType,
			       int numHumanPlayers, int inProgressLaunch,
			       const char *connectionAddress)
{
	DPCAPS directPlayCaps;
	char dialNumber[32] = "Dial a New Number.";
	int playerIndex;
	int success;
#ifndef XVT_MODERN
	int outPayloadSize;
	int outDpid;
	int *packet;
	int receivedPlayerCount;
	int rosterIndex;
	int *rosterEntry;
#endif

	(void)dialNumber;
	(void)mpGameName;
	(void)connectionAddress;
	NetSession_DebugTrace("Init network");
	memset(&g_netSession, 0, sizeof(g_netSession));
	g_netSessionFlightHandshakeActive = 1;
	g_netSessionScratchPacket.trailingState = 0;
	g_netSession.networkType = networkType;
	g_netSession.broadcastSeqCounter = 0;
	g_netSession.broadcastPiggybackEmpty = 1;
	g_netSession.broadcastPayload[0] = NET_PACKET_NOP;
	g_netSession.broadcastPayloadLength = 1;
	g_netSession.groupSeqCounter = 0;
	g_netSession.groupPiggybackEmpty = 1;
	g_netSession.groupPayload[0] = NET_PACKET_NOP;
	g_netSession.groupPayloadLength = 1;
	g_netSession.reliableUseFixedResendTimeouts = 0;
	for (playerIndex = 0; playerIndex < 40; ++playerIndex) {
		g_netSession.reliablePeerSlots[playerIndex]
			.lastDeliveredSeqDefault = 127;
		g_netSession.reliablePeerSlots[playerIndex]
			.lastDeliveredSeqChannelA = 127;
		g_netSession.reliablePeerSlots[playerIndex]
			.lastDeliveredSeqChannelB = 127;
		g_netSession.reliablePeerSlots[playerIndex].recvSeqDefault =
			127;
		g_netSession.reliablePeerSlots[playerIndex].recvSeqChannelA =
			127;
		g_netSession.reliablePeerSlots[playerIndex].recvSeqChannelB =
			127;
		g_netSession.reliablePeerSlots[playerIndex].sendSeq = 0;
		g_netSession.reliablePeerSlots[playerIndex].directPlayId = 0;
		g_netSession.reliablePeerSlots[playerIndex].lastPiggybackType =
			NET_PACKET_NOP;
		g_netSession.reliablePeerSlots[playerIndex].piggybackLength = 1;
		g_netSession.reliablePeerSlots[playerIndex].lastActivityMs = 0;
		g_netSession.reliablePeerSlots[playerIndex].packetCount = 0;
		g_netSession.reliablePeerSlots[playerIndex].packetDropCount = 0;
	}
	g_netSessionSentWorldMessageWriteIndex = 0;
	memset(g_netSessionSentWorldMessageHistory, 0,
	       sizeof(g_netSessionSentWorldMessageHistory));
	g_netRecvQueueCount = 0;
	g_netSession.reliablePeerSlotCount = 0;
	/* The 1 stored in success here also serves below as the player count, the host flag, a DirectPlay id and
	 * an active flag. */
	success = 1;

	if (numHumanPlayers == success && isHost == success) {
		/* The single-player path reuses the persisted DirectPlay snapshot. */
		NetSession_ImportRuntimeState(
			(void **)&g_netSession.dplayInterface,
			&g_netSession.appGuid, &g_netSession.instanceGuid,
			(int32_t *)&g_netSession.groupDplayId,
			&g_netSession.hostDplayId,
			(struct NetPlayerInfo *)&g_netSession.localPlayerInfo,
			g_netSessionRecvQueue, &g_netRecvQueueReadIndex,
			(int *)&g_netRecvQueueCount, &g_netRecvQueueWriteIndex,
			g_netSession.reliablePeerSlots,
			&g_netSession.reliablePeerSlotCount,
			&g_netSession.broadcastSeqCounter,
			(char *)g_netSession.broadcastPayload,
			&g_netSession.broadcastPayloadLength,
			&g_netSession.broadcastPiggybackEmpty,
			&g_netSession.groupSeqCounter,
			(char *)g_netSession.groupPayload,
			&g_netSession.groupPayloadLength,
			&g_netSession.groupPiggybackEmpty,
			g_netSessionSentHistory,
			&g_netSessionSentHistoryWriteIndex);
		if (g_netSession.dplayInterface == NULL) {
			g_netSession.localPlayerInfo.directPlayId = success;
			g_netSession.localPlayerInfo.activeFlag = success;
		} else {
			memset(&directPlayCaps, 0, sizeof(directPlayCaps));
			directPlayCaps.dwSize = sizeof(directPlayCaps);
			g_netSession.dplayInterface->lpVtbl->GetCaps(
				g_netSession.dplayInterface, &directPlayCaps,
				0);
		}
		strncpy(g_netSession.localPlayerInfo.longName, formalName,
			sizeof(g_netSession.localPlayerInfo.longName));
		strncpy(g_netSession.localPlayerInfo.playerName, pilotName,
			sizeof(g_netSession.localPlayerInfo.playerName));
		g_netSession.players[0] = g_netSession.localPlayerInfo;
		g_netSession.localIsHost = isHost;
		g_netSession.playerCount = success;
		return success;
	}

	NetSession_ImportRuntimeState(
		(void **)&g_netSession.dplayInterface, &g_netSession.appGuid,
		&g_netSession.instanceGuid,
		(int32_t *)&g_netSession.groupDplayId,
		&g_netSession.hostDplayId,
		(struct NetPlayerInfo *)&g_netSession.localPlayerInfo,
		g_netSessionRecvQueue, &g_netRecvQueueReadIndex,
		(int *)&g_netRecvQueueCount, &g_netRecvQueueWriteIndex,
		g_netSession.reliablePeerSlots,
		&g_netSession.reliablePeerSlotCount,
		&g_netSession.broadcastSeqCounter,
		(char *)g_netSession.broadcastPayload,
		&g_netSession.broadcastPayloadLength,
		&g_netSession.broadcastPiggybackEmpty,
		&g_netSession.groupSeqCounter,
		(char *)g_netSession.groupPayload,
		&g_netSession.groupPayloadLength,
		&g_netSession.groupPiggybackEmpty, g_netSessionSentHistory,
		&g_netSessionSentHistoryWriteIndex);
	memset(&directPlayCaps, 0, sizeof(directPlayCaps));
	directPlayCaps.dwSize = sizeof(directPlayCaps);
	g_netSession.dplayInterface->lpVtbl->GetCaps(
		g_netSession.dplayInterface, &directPlayCaps, 0);
	g_netSession.playerCount = 0;
	g_netSession.localIsHost = isHost;
	NetSession_EnumeratePlayers();
#ifndef XVT_MODERN
	receivedPlayerCount = numHumanPlayers;
#endif
	g_netSessionFlightHandshakeActive = 1;
	timeGetTime();
	g_netSessionScratchPacket.packetType = NET_PACKET_STARTUP_READY;
	NetSession_SendPacket(g_netSession.hostDplayId,
			      (unsigned int *)&g_netSessionScratchPacket, 4);
	g_netSessionScratchPacket.packetType = NET_PACKET_NOP;
	NetSession_SendPacket(g_netSession.hostDplayId,
			      (unsigned int *)&g_netSessionScratchPacket, 4);

#ifdef XVT_MODERN
	return XvtFlightNetwork_BeginRosterExchange(numHumanPlayers,
						    inProgressLaunch);
#else
	if (NetSession_IsLocalHost() != 0 && numHumanPlayers != 0) {
		while (receivedPlayerCount != 0) {
			packet = NetSession_WaitForGamePacket(
				&outDpid, &outPayloadSize, 60);
			if (packet == NULL) {
				g_netSession.dplayInterface = NULL;
				return 0;
			}
			if (*packet == NET_PACKET_STARTUP_READY) {
				--receivedPlayerCount;
			}
		}
	}

	if (NetSession_IsLocalHost() != 0) {
		if (numHumanPlayers > 1) {
			NetSession_BroadcastPlayerRoster(0);
			g_netSessionScratchPacket.packetType = NET_PACKET_NOP;
			NetSession_SendPacket(
				0, (unsigned int *)&g_netSessionScratchPacket,
				4);
		}
	} else if (inProgressLaunch == 0) {
		do {
			packet = NetSession_WaitForGamePacket(
				&outDpid, &outPayloadSize, 60);
			if (packet == NULL) {
				g_netSession.dplayInterface = NULL;
				return 0;
			}
		} while (*packet != NET_PACKET_ROSTER_COUNT);

		g_netSession.playerCount = packet[1];
		receivedPlayerCount = 0;
		while (g_netSession.playerCount > receivedPlayerCount) {
			do {
				packet = NetSession_WaitForGamePacket(
					&outDpid, &outPayloadSize, 60);
				if (packet == NULL) {
					return 0;
				}
			} while (*packet != NET_PACKET_ROSTER_ENTRY);
			rosterEntry = packet + 2;
			rosterIndex = packet[1];
			memcpy(&g_netSession.players[rosterIndex], rosterEntry,
			       sizeof(struct SessionPlayerInfo));
			++receivedPlayerCount;
		}
	}
	return success;
#endif
}

/* Hands this session's network state back to the frontend through
 * NetSession_ExportRuntimeState: receive queue, reliable peer slots, channel
 * sequences and saved copies, sent history, and the sent world-message history
 * with its write index. Closes nothing. Returns 1. */
// FUNCTION: XVT 0x46C6A0
int NetSession_Shutdown(void)
{
	NetSession_ExportRuntimeState(
		(void **)&g_netSession.dplayInterface, &g_netSession.appGuid,
		&g_netSession.instanceGuid, (int *)&g_netSession.groupDplayId,
		&g_netSession.hostDplayId, &g_netSession.localPlayerInfo,
		g_netSessionRecvQueue, &g_netRecvQueueReadIndex,
		(int *)&g_netRecvQueueCount, &g_netRecvQueueWriteIndex,
		g_netSession.reliablePeerSlots,
		(int *)&g_netSession.reliablePeerSlotCount,
		(int *)&g_netSession.broadcastSeqCounter,
		g_netSession.broadcastPayload,
		&g_netSession.broadcastPayloadLength,
		&g_netSession.broadcastPiggybackEmpty,
		(int *)&g_netSession.groupSeqCounter, g_netSession.groupPayload,
		&g_netSession.groupPayloadLength,
		&g_netSession.groupPiggybackEmpty, g_netSessionSentHistory,
		&g_netSessionSentHistoryWriteIndex,
		g_netSessionSentWorldMessageHistory,
		&g_netSessionSentWorldMessageWriteIndex);
	return 1;
}

/* Has DirectPlay list every player through NetSession_EnumPlayersCallback,
 * which adds them to the roster. Returns 1. Does not reset the roster count
 * first or check for a DirectPlay interface. */
// FUNCTION: XVT 0x46C730
int NetSession_EnumeratePlayers(void)
{
	g_netSession.dplayInterface->lpVtbl->EnumPlayers(
		g_netSession.dplayInterface, 0, NetSession_EnumPlayersCallback,
		0, 0);
	return 1;
}

/* Adds one DirectPlay player to the roster: skips entries of type 0 (not
 * players) and players missing from g_pilotData.networkPlayers; stores the long
 * name in longName and the short name in playerName, cut to 15 characters,
 * with the id and activeFlag 1, and counts it in g_netSession.playerCount.
 * Returns 0, which stops the listing, once 8 players are in; else 1. */
// FUNCTION: XVT 0x46C750
int AERON_DXAPI NetSession_EnumPlayersCallback(DPID dplayId,
					       uint32_t playerType,
					       const DPNAME *nameInfo,
					       uint32_t flags, void *context)
{
	char *nameEnd;
	int *playerValue;

	(void)flags;
	(void)context;

	if (playerType == 0) {
		return 1;
	}
	if (g_netSession.playerCount >= 8) {
		return 0;
	}
	if (PilotData_HasNetworkPlayerDpid(dplayId) != 0) {
		strncpy(g_netSession.players[g_netSession.playerCount].longName,
			nameInfo->lpszLongNameA, 16);
		strncpy(g_netSession.players[g_netSession.playerCount]
				.playerName,
			nameInfo->lpszShortNameA, 16);
		nameEnd = &g_netSession.players[g_netSession.playerCount]
				   .longName[15];
		*nameEnd = '\0';
		nameEnd = &g_netSession.players[g_netSession.playerCount]
				   .playerName[15];
		*nameEnd = '\0';
		playerValue = &g_netSession.players[g_netSession.playerCount]
				       .directPlayId;
		*playerValue = dplayId;
		playerValue = &g_netSession.players[g_netSession.playerCount]
				       .activeFlag;
		*playerValue = 1;
		g_netSession.playerCount++;
	}
	return 1;
}

enum {
	RECEIVE_QUEUE_CAPACITY = 1024,
	RECEIVE_QUEUE_LIMIT = RECEIVE_QUEUE_CAPACITY - 1,
	HISTORY_CAPACITY = 128,
	WORLD_HISTORY_CAPACITY = 256,
	MAX_PAYLOAD_SIZE = 508,
	SEQUENCE_MODULUS = 128,
	SEQUENCE_NONE = 128
};

/* Moves every packet DirectPlay holds for this player into the receive queue.
 * Does nothing without a DirectPlay interface; stops when DirectPlay has no
 * more, or when the queue holds 1,023 entries, after calling
 * NetReliable_KeepOnlyHostReceivedPackets. System messages (sender 0) are
 * queued as they come; packets addressed to another player are skipped. It
 * answers a PING with a PONG and ignores a KEEPALIVE_ACK. A peer's WORLD_NACK
 * gets the world message with that tick from
 * g_netSessionSentWorldMessageHistory, a NACK the asked sequence and channel
 * from g_netSessionSentHistory, either one a NOP in that sequence when the
 * packet is gone, and a KEEPALIVE the next packet on each channel that the peer
 * still lacks; NACKs count in the peer's drop count. A resent packet (bit 0x80
 * set, bit 15 clear) is queued under the channel its marker byte names. Any
 * other packet is queued with its channel and sequence, marked as a resent copy
 * when NetReliable_CheckAndRecordRecvSequence finds the sequence not new (a
 * repeat or a stale one), except that such a packet on the group channel is
 * dropped; when NetReliable_CheckAndRecordRecvSequence says the
 * channel's previous sequence is new, the copy of it riding behind the packet
 * is queued too. Writes
 * g_netSession.receivePumpState (0), the peer slots' receive sequences and drop
 * counts, g_netSessionRecvQueue, g_netRecvQueueWriteIndex and
 * g_netRecvQueueCount. */
// FUNCTION: XVT 0x46C830
void NetSession_PumpIncomingPackets(void)
{
	static int receiveSuppressCount;
	DPID fromId;
	DPID toId;
	struct {
		uint16_t header;    /* Type, sequence and channel bits */
		uint8_t data[1022]; /* The rest; a system message fills both */
	} wirePacket;
	unsigned int responsePacket[2];
	uint32_t wireSize;
	uint8_t *packetData;
	uint32_t packetSize;
	unsigned int packetType;
	int sequence;
	int groupChannel;
	int broadcastChannel;
	int hasLength;
	unsigned int peerSlot;
	int duplicate;
	int previousSequence;
	uint8_t *piggyback;
	uint32_t piggybackSize;
	unsigned int piggybackType;
	if (g_netSession.dplayInterface == NULL) {
		return;
	}
	g_netSession.receivePumpState = 0;
	for (;;) {
		if ((int)g_netRecvQueueCount >= RECEIVE_QUEUE_LIMIT) {
			NetSession_DebugTrace("Ran out of receive buffers!!!");
			NetReliable_KeepOnlyHostReceivedPackets();
			return;
		}
		wireSize = sizeof(wirePacket);
		if (g_netSession.dplayInterface->lpVtbl->Receive(
			    g_netSession.dplayInterface, &fromId, &toId, 1,
			    &wirePacket, &wireSize) != 0) {
			return;
		}
		if (fromId == 0) {
			NetSession_DebugTrace("(RSM)");
			if (wireSize >
			    sizeof(g_netSessionRecvQueue[0].payload)) {
				wireSize = sizeof(
					g_netSessionRecvQueue[0].payload);
			}
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.directPlayId = fromId;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.payloadSize = wireSize;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.isResentCopy = 0;
			memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				       .payload,
			       &wirePacket.header, wireSize);
			++g_netRecvQueueWriteIndex;
			++g_netRecvQueueCount;
			if (g_netRecvQueueWriteIndex >=
			    RECEIVE_QUEUE_CAPACITY) {
				g_netRecvQueueWriteIndex = 0;
			}
			continue;
		}
		if (receiveSuppressCount != 0) {
			--receiveSuppressCount;
			continue;
		}
		if (toId != (DPID)g_netSession.localPlayerInfo.directPlayId) {
			continue;
		}
		packetType = wirePacket.header & 0x7F;
		groupChannel = (wirePacket.header & 0x80) != 0;
		broadcastChannel = (wirePacket.header & 0x8000) == 0;
		sequence = (wirePacket.header >> 8) & 0x7F;
		packetData = wirePacket.data;
		packetSize = wireSize - sizeof(wirePacket.header);
		hasLength = packetType < NET_PACKET_RESYNC_CHECKSUMS ||
			    packetType > NET_PACKET_RESYNC_CHUNK;
		if (hasLength && !(broadcastChannel && groupChannel) &&
		    NetSession_GetFixedPayloadSize(packetType) == 0 &&
		    packetSize >= sizeof(uint16_t)) {
			uint16_t encodedSize;
			memcpy(&encodedSize, packetData, sizeof(encodedSize));
			packetData += sizeof(encodedSize);
			packetSize = encodedSize;
		}
		if (packetSize > MAX_PAYLOAD_SIZE) {
			packetSize = MAX_PAYLOAD_SIZE;
		}
		if (packetType == NET_PACKET_PING) {
			responsePacket[0] = NET_PACKET_PONG;
			NetSession_SendPacket(fromId, responsePacket,
					      sizeof(uint32_t));
			continue;
		}
		if (packetType == NET_PACKET_KEEPALIVE_ACK) {
			continue;
		}
		if (packetType == NET_PACKET_WORLD_NACK) {
			if (packetSize >= 2 * sizeof(int)) {
				unsigned int searchCount;
				unsigned int worldCursor;
				int timestamp;
				int requestedSequence;
				memcpy(&timestamp, packetData,
				       sizeof(timestamp));
				memcpy(&requestedSequence,
				       packetData + sizeof(timestamp),
				       sizeof(requestedSequence));
				peerSlot = NetReliable_FindOrCreatePeerSlot(
					fromId);
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 40) {
					++g_netSession
						  .reliablePeerSlots[peerSlot]
						  .packetDropCount;
				}
				worldCursor = (unsigned int)
					g_netSessionSentWorldMessageWriteIndex;
				for (searchCount = 0;
				     searchCount < WORLD_HISTORY_CAPACITY;
				     ++searchCount) {
					if (g_netSessionSentWorldMessageHistory
							    [worldCursor]
								    .payloadSize !=
						    0 &&
					    ((*((uint32_t
							 *)&g_netSessionSentWorldMessageHistory
							[worldCursor]
								.payload[4]) &
					      0x7FFFFFFF) ==
					     (uint32_t)timestamp)) {
						break;
					}
					if (++worldCursor >=
					    WORLD_HISTORY_CAPACITY) {
						worldCursor = 0;
					}
				}
				if (searchCount < WORLD_HISTORY_CAPACITY) {
					NetSession_SendSequencedGamePacket(
						fromId, 0,
						g_netSessionSentWorldMessageHistory
							[worldCursor]
								.sequenceByte,
						(const unsigned int *)
							g_netSessionSentWorldMessageHistory
								[worldCursor]
									.payload,
						g_netSessionSentWorldMessageHistory
							[worldCursor]
								.payloadSize);
				} else {
					responsePacket[0] = NET_PACKET_NOP;
					NetSession_SendSequencedGamePacket(
						fromId, 0,
						(uint8_t)requestedSequence,
						responsePacket,
						sizeof(uint32_t));
				}
			}
			continue;
		}
		if (packetType == NET_PACKET_NACK) {
			if (packetSize >= 2 * sizeof(int)) {
				int requestedSequence;
				int requestedClass;
				unsigned int searchCount;
				unsigned int historyCursor;
				memcpy(&requestedSequence, packetData,
				       sizeof(requestedSequence));
				memcpy(&requestedClass,
				       packetData + sizeof(requestedSequence),
				       sizeof(requestedClass));
				peerSlot = NetReliable_FindOrCreatePeerSlot(
					fromId);
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 40) {
					++g_netSession
						  .reliablePeerSlots[peerSlot]
						  .packetDropCount;
				}
				historyCursor = (unsigned int)
					g_netSessionSentHistoryWriteIndex;
				for (searchCount = 0;
				     searchCount < HISTORY_CAPACITY;
				     ++searchCount) {
					if (g_netSessionSentHistory[historyCursor]
							    .payloadSize != 0 &&
					    g_netSessionSentHistory[historyCursor]
							    .sequenceByte ==
						    requestedSequence &&
					    g_netSessionSentHistory[historyCursor]
							    .packetClass ==
						    requestedClass &&
					    (requestedClass == 0 ||
					     requestedClass == 2 ||
					     g_netSessionSentHistory[historyCursor]
							     .directPlayId ==
						     fromId)) {
						break;
					}
					if (++historyCursor >=
					    HISTORY_CAPACITY) {
						historyCursor = 0;
					}
				}
				if (searchCount < HISTORY_CAPACITY) {
					NetSession_SendSequencedGamePacket(
						fromId, (uint8_t)requestedClass,
						g_netSessionSentHistory
							[historyCursor]
								.sequenceByte,
						(const unsigned int *)
							g_netSessionSentHistory
								[historyCursor]
									.payload,
						g_netSessionSentHistory
							[historyCursor]
								.payloadSize);
				} else {
					responsePacket[0] = NET_PACKET_NOP;
					NetSession_SendSequencedGamePacket(
						fromId, (uint8_t)requestedClass,
						(uint8_t)requestedSequence,
						responsePacket,
						sizeof(uint32_t));
				}
			}
			continue;
		}
		if (packetType == NET_PACKET_KEEPALIVE) {
			if (packetSize >= 3 * sizeof(int)) {
				unsigned int searchCount;
				unsigned int historyCursor;
				uint8_t packetClass;
				int expectedBroadcast;
				int expectedGroup;
				int expectedDirected;
				memcpy(&expectedBroadcast, packetData,
				       sizeof(expectedBroadcast));
				memcpy(&expectedGroup,
				       packetData + sizeof(expectedBroadcast),
				       sizeof(expectedGroup));
				memcpy(&expectedDirected,
				       packetData +
					       2 * sizeof(expectedBroadcast),
				       sizeof(expectedDirected));
				if (expectedBroadcast ==
				    (int)g_netSession.broadcastSeqCounter) {
					expectedBroadcast = SEQUENCE_NONE;
				}
				if (expectedGroup ==
				    (int)g_netSession.groupSeqCounter) {
					expectedGroup = SEQUENCE_NONE;
				}
				peerSlot = NetReliable_FindOrCreatePeerSlot(
					fromId);
				if (peerSlot >=
					    g_netSession
						    .reliablePeerSlotCount ||
				    g_netSession.reliablePeerSlots[peerSlot]
						    .sendSeq ==
					    expectedDirected) {
					expectedDirected = SEQUENCE_NONE;
				}
				historyCursor = (unsigned int)
					g_netSessionSentHistoryWriteIndex;
				for (searchCount = 0;
				     searchCount < HISTORY_CAPACITY &&
				     (expectedBroadcast != SEQUENCE_NONE ||
				      expectedGroup != SEQUENCE_NONE ||
				      expectedDirected != SEQUENCE_NONE);
				     ++searchCount) {
					if (g_netSessionSentHistory
						    [historyCursor]
							    .payloadSize != 0) {
						packetClass =
							g_netSessionSentHistory
								[historyCursor]
									.packetClass;
						if (packetClass == 0) {
							if (g_netSessionSentHistory
								    [historyCursor]
									    .sequenceByte ==
							    expectedBroadcast) {
								NetSession_SendSequencedGamePacket(
									fromId,
									0,
									expectedBroadcast,
									(const unsigned int
										 *)g_netSessionSentHistory
										[historyCursor]
											.payload,
									g_netSessionSentHistory
										[historyCursor]
											.payloadSize);
								expectedBroadcast =
									SEQUENCE_NONE;
							}
						} else if (packetClass == 2) {
							if (g_netSessionSentHistory
								    [historyCursor]
									    .sequenceByte ==
							    expectedGroup) {
								NetSession_SendSequencedGamePacket(
									fromId,
									2,
									expectedGroup,
									(const unsigned int
										 *)g_netSessionSentHistory
										[historyCursor]
											.payload,
									g_netSessionSentHistory
										[historyCursor]
											.payloadSize);
								expectedGroup =
									SEQUENCE_NONE;
							}
						} else if (
							g_netSessionSentHistory[historyCursor]
									.directPlayId ==
								fromId &&
							g_netSessionSentHistory[historyCursor]
									.sequenceByte ==
								expectedDirected) {
							NetSession_SendSequencedGamePacket(
								fromId, 1,
								expectedDirected,
								(const unsigned int
									 *)g_netSessionSentHistory
									[historyCursor]
										.payload,
								g_netSessionSentHistory
									[historyCursor]
										.payloadSize);
							expectedDirected =
								SEQUENCE_NONE;
						}
					}
					if (++historyCursor >=
					    HISTORY_CAPACITY) {
						historyCursor = 0;
					}
				}
			}
			continue;
		}
		peerSlot = NetReliable_FindOrCreatePeerSlot(fromId);
		if (broadcastChannel && groupChannel) {
			uint8_t channelMarker;
			const uint8_t *appPayload;
			uint32_t appPayloadSize;
			channelMarker = packetSize != 0 ? packetData[0] : 0;
			appPayload =
				packetSize != 0 ? packetData + 1 : packetData;
			appPayloadSize = packetSize != 0 ? packetSize - 1 : 0;
			if (hasLength &&
			    NetSession_GetFixedPayloadSize(packetType) == 0 &&
			    appPayloadSize >= sizeof(uint16_t)) {
				uint16_t encodedSize;
				memcpy(&encodedSize, appPayload,
				       sizeof(encodedSize));
				appPayload += sizeof(encodedSize);
				appPayloadSize = encodedSize;
			}
			if (appPayloadSize > MAX_PAYLOAD_SIZE) {
				appPayloadSize = MAX_PAYLOAD_SIZE;
			}
			memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				       .payload,
			       &packetType, sizeof(packetType));
			memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					       .payload +
				       sizeof(packetType),
			       appPayload, appPayloadSize);
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.directPlayId = fromId;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.payloadSize =
				appPayloadSize + sizeof(packetType);
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.lastNackMs = 0;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.nackRetryCount = 0;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.isResentCopy = 1;
			peerSlot = NetReliable_FindOrCreatePeerSlot(fromId);
			if (channelMarker == 0) {
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 0;
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 40) {
					NetSession_AdvanceReceivedSequence(
						&g_netSession
							 .reliablePeerSlots
								 [peerSlot]
							 .recvSeqChannelA,
						sequence);
				}
			} else if (channelMarker == 2) {
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 2;
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 40) {
					NetSession_AdvanceReceivedSequence(
						&g_netSession
							 .reliablePeerSlots
								 [peerSlot]
							 .recvSeqChannelB,
						sequence);
				}
			} else {
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 1;
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 40) {
					NetSession_AdvanceReceivedSequence(
						&g_netSession
							 .reliablePeerSlots
								 [peerSlot]
							 .recvSeqDefault,
						sequence);
				}
			}
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.sequenceByte = (uint8_t)sequence;
			++g_netRecvQueueCount;
			++g_netRecvQueueWriteIndex;
			if (g_netRecvQueueWriteIndex >=
			    RECEIVE_QUEUE_CAPACITY) {
				g_netRecvQueueWriteIndex = 0;
			}
			continue;
		}
		peerSlot = NetReliable_FindOrCreatePeerSlot(fromId);
		if (hasLength) {
			previousSequence = sequence == 0 ? SEQUENCE_MODULUS - 1
							 : sequence - 1;
			duplicate = NetReliable_CheckAndRecordRecvSequence(
				fromId, previousSequence, broadcastChannel,
				groupChannel);
			if (!duplicate) {
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 40 && !groupChannel) {
					++g_netSession
						  .reliablePeerSlots[peerSlot]
						  .packetDropCount;
				}
				piggyback = packetData + packetSize;
				piggybackSize =
					wireSize -
					(unsigned int)(piggyback -
						       (uint8_t *)&wirePacket
							       .header);
				if (piggybackSize > 0) {
					piggybackType = piggyback[0];
					if (piggybackType != NET_PACKET_NOP) {
						if (piggybackSize - 1 >
						    MAX_PAYLOAD_SIZE) {
							piggybackSize =
								MAX_PAYLOAD_SIZE +
								1;
						}
						memcpy(g_netSessionRecvQueue
							       [g_netRecvQueueWriteIndex]
								       .payload,
						       &piggybackType,
						       sizeof(piggybackType));
						memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
								       .payload +
							       sizeof(uint32_t),
						       piggyback + 1,
						       piggybackSize - 1);
						g_netSessionRecvQueue
							[g_netRecvQueueWriteIndex]
								.directPlayId =
							fromId;
						g_netSessionRecvQueue
							[g_netRecvQueueWriteIndex]
								.payloadSize =
							piggybackSize +
							sizeof(uint32_t) - 1;
						g_netSessionRecvQueue
							[g_netRecvQueueWriteIndex]
								.lastNackMs = 0;
						g_netSessionRecvQueue
							[g_netRecvQueueWriteIndex]
								.nackRetryCount =
							0;
						g_netSessionRecvQueue
							[g_netRecvQueueWriteIndex]
								.isResentCopy =
							0;
						if (broadcastChannel) {
							g_netSessionRecvQueue
								[g_netRecvQueueWriteIndex]
									.packetClass =
								0;
						} else {
							g_netSessionRecvQueue
								[g_netRecvQueueWriteIndex]
									.packetClass =
								2;
							if (!groupChannel) {
								g_netSessionRecvQueue
									[g_netRecvQueueWriteIndex]
										.packetClass =
									1;
							}
						}
						g_netSessionRecvQueue
							[g_netRecvQueueWriteIndex]
								.sequenceByte =
							(uint8_t)
								previousSequence;
						++g_netRecvQueueWriteIndex;
						++g_netRecvQueueCount;
						if (g_netRecvQueueWriteIndex >=
						    RECEIVE_QUEUE_CAPACITY) {
							g_netRecvQueueWriteIndex =
								0;
						}
					}
				}
			}
		}
		duplicate = NetReliable_CheckAndRecordRecvSequence(
			fromId, sequence, broadcastChannel, groupChannel);
		if (!groupChannel || !duplicate) {
			memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				       .payload,
			       &packetType, sizeof(packetType));
			memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					       .payload +
				       sizeof(packetType),
			       packetData, packetSize);
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.directPlayId = fromId;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.payloadSize = packetSize + sizeof(packetType);
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.lastNackMs = 0;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.nackRetryCount = 0;
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.isResentCopy = 1;
			if (!duplicate) {
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.isResentCopy = 0;
			}
			if (broadcastChannel) {
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 0;
			} else {
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 2;
				if (!groupChannel) {
					g_netSessionRecvQueue
						[g_netRecvQueueWriteIndex]
							.packetClass = 1;
				}
			}
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.sequenceByte = (uint8_t)sequence;
			++g_netRecvQueueWriteIndex;
			++g_netRecvQueueCount;
			if (g_netRecvQueueWriteIndex >=
			    RECEIVE_QUEUE_CAPACITY) {
				g_netRecvQueueWriteIndex = 0;
			}
		}
	}
}

/* Sends the packet with NetSession_SendPacket to every active roster entry with
 * a nonzero id, this player included. Returns the last send's result when the
 * last roster entry was sent to; otherwise that entry's DirectPlay id, or the
 * roster count when the roster is empty. */
// FUNCTION: XVT 0x46D300
int NetSession_BroadcastPacketToPlayers(unsigned int *payload, int payloadSize)
{
	int result;
	int playerIndex;

	result = g_netSession.playerCount;
	if (result > 0) {
		for (playerIndex = 0; playerIndex < g_netSession.playerCount;
		     ++playerIndex) {
			result = g_netSession.players[playerIndex].directPlayId;
			if (result != 0 &&
			    g_netSession.players[playerIndex].activeFlag != 0) {
				result = NetSession_SendPacket(result, payload,
							       payloadSize);
			}
		}
	}
	return result;
}

/* Sends one game packet whose first word is its type; payloadSize counts its
 * bytes, and below 4 nothing is sent and 0 returned. The packet goes out with a
 * 2-byte header, the type in the low 7 bits and a 7-bit sequence above, on one
 * of three channels: to id 0, all players, on g_netSession.broadcastSeqCounter;
 * to the session group, or any internet-play REMOTE_INPUT, on groupSeqCounter
 * (bits 0x8080); to one player, on that peer's reliable slot sequence (bit
 * 0x8000). Outside the resync types (RESYNC_CHECKSUMS to RESYNC_CHUNK) the body
 * gets a 2-byte length and, behind it, the channel's previous packet (a NOP the
 * first time), from which a receiver can recover a lost one; this packet then
 * becomes the channel's saved copy. Packets to others go into
 * g_netSessionSentHistory, and world messages into
 * g_netSessionSentWorldMessageHistory, for resends; internet-play inputs do
 * not. A packet to all, to the group, to this player, or sent with no
 * DirectPlay interface is also queued for this player to receive, in
 * g_netSessionRecvQueue with g_netRecvQueueWriteIndex and g_netRecvQueueCount.
 * Returns 1 when DirectPlay takes it, when the destination is this player, or
 * with no interface; else 0. The original arm's check before it records its own
 * receive sequence compares a 0-or-1 flag with 40, so it always passes; the
 * modern arm checks the slot. Does not check payloadSize against the 512-byte
 * saved copies and history entries, or the reliable slot index before it uses
 * the slot's saved copy. */
// FUNCTION: XVT 0x46D350
int NetSession_SendPacket(int directPlayId, unsigned int *payload,
			  signed int payloadSize)
{
	unsigned int packetType;
	int appendPending;
	uint16_t packetHeader;
	int sendResult;
	struct NetSessionCompactEncodedPacket encodedPacket;
	uint8_t *encodedPayload;
	int encodedHeaderSize;
	int encodedSize;
	uint8_t packetTypeByte[4];

	sendResult = 0;
	if (payloadSize < 4) {
		return 0;
	}

	packetType = *payload;
	packetHeader = packetTypeByte[0] = packetType & 0x7F;
	if (packetType < NET_PACKET_RESYNC_CHECKSUMS ||
	    packetType >= NET_PACKET_RESYNC_CHUNK + 1) {
		appendPending = 1;
	} else {
		appendPending = 0;
	}

	if (packetType == NET_PACKET_REMOTE_INPUT &&
	    g_gameConfig.internetPlay == 1) {
		packetHeader |= (g_netSession.groupSeqCounter & 0x7F) << 8;
		++g_netSession.groupSeqCounter;
		packetHeader |= 0x8080;
		if ((int)g_netSession.groupSeqCounter > 127) {
			g_netSession.groupSeqCounter = 0;
		}
		encodedPacket.packetTypeHeader = packetHeader;
		encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
		encodedHeaderSize = 2;
		if (appendPending &&
		    NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPacket.payloadSize = payloadSize - 4;
			encodedPayload = encodedPacket.payload;
			encodedHeaderSize = 4;
		}
		memcpy(encodedPayload, payload + 1, payloadSize - 4);
		encodedPayload += payloadSize - 4;
		encodedSize = payloadSize + encodedHeaderSize - 4;
		if (appendPending) {
			if (g_netSession.groupPiggybackEmpty != 0) {
				*encodedPayload = NET_PACKET_NOP;
				g_netSession.groupPiggybackEmpty = 0;
				++encodedSize;
			} else {
				memcpy(encodedPayload,
				       g_netSession.groupPayload,
				       g_netSession.groupPayloadLength);
				encodedSize += g_netSession.groupPayloadLength;
			}
		}
		g_netSession.groupPayload[0] = packetTypeByte[0];
		memcpy(g_netSession.groupPayload + 1, payload + 1,
		       payloadSize - 4);
		g_netSession.groupPayloadLength = payloadSize - 3;
	} else if (directPlayId == 0) {
		packetHeader |= (g_netSession.broadcastSeqCounter & 0x7F) << 8;
		++g_netSession.broadcastSeqCounter;
		if ((int)g_netSession.broadcastSeqCounter > 127) {
			g_netSession.broadcastSeqCounter = 0;
		}
		encodedPacket.packetTypeHeader = packetHeader;
		encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
		encodedHeaderSize = 2;
		if (appendPending &&
		    NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPacket.payloadSize = payloadSize - 4;
			encodedPayload = encodedPacket.payload;
			encodedHeaderSize = 4;
		}
		memcpy(encodedPayload, payload + 1, payloadSize - 4);
		encodedPayload += payloadSize - 4;
		encodedSize = payloadSize + encodedHeaderSize - 4;
		if (appendPending) {
			if (g_netSession.broadcastPiggybackEmpty != 0) {
				*encodedPayload = NET_PACKET_NOP;
				g_netSession.broadcastPiggybackEmpty = 0;
				++encodedSize;
			} else {
				memcpy(encodedPayload,
				       g_netSession.broadcastPayload,
				       g_netSession.broadcastPayloadLength);
				encodedSize +=
					g_netSession.broadcastPayloadLength;
			}
		}
		g_netSession.broadcastPayload[0] = packetTypeByte[0];
		memcpy(g_netSession.broadcastPayload + 1, payload + 1,
		       payloadSize - 4);
		g_netSession.broadcastPayloadLength = payloadSize - 3;
	} else if (directPlayId == g_netSession.groupDplayId) {
		packetHeader |= (g_netSession.groupSeqCounter & 0x7F) << 8;
		++g_netSession.groupSeqCounter;
		packetHeader |= 0x8080;
		if ((int)g_netSession.groupSeqCounter > 127) {
			g_netSession.groupSeqCounter = 0;
		}
		encodedPacket.packetTypeHeader = packetHeader;
		encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
		encodedHeaderSize = 2;
		if (appendPending &&
		    NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPacket.payloadSize = payloadSize - 4;
			encodedPayload = encodedPacket.payload;
			encodedHeaderSize = 4;
		}
		memcpy(encodedPayload, payload + 1, payloadSize - 4);
		encodedPayload += payloadSize - 4;
		encodedSize = payloadSize + encodedHeaderSize - 4;
		if (appendPending) {
			if (g_netSession.groupPiggybackEmpty != 0) {
				*encodedPayload = NET_PACKET_NOP;
				g_netSession.groupPiggybackEmpty = 0;
				++encodedSize;
			} else {
				memcpy(encodedPayload,
				       g_netSession.groupPayload,
				       g_netSession.groupPayloadLength);
				encodedSize += g_netSession.groupPayloadLength;
			}
		}
		g_netSession.groupPayload[0] = packetTypeByte[0];
		memcpy(g_netSession.groupPayload + 1, payload + 1,
		       payloadSize - 4);
		g_netSession.groupPayloadLength = payloadSize - 3;
	} else {
		unsigned int peerSlot =
			NetReliable_FindOrCreatePeerSlot(directPlayId);
		if (g_netSession.reliablePeerSlotCount > peerSlot &&
		    peerSlot < 40) {
			int sendSequence;
			sendSequence = g_netSession.reliablePeerSlots[peerSlot]
					       .sendSeq;
			packetHeader |= (sendSequence++ & 0x7F) << 8;
			g_netSession.reliablePeerSlots[peerSlot].sendSeq =
				sendSequence;
			if (sendSequence > 127) {
				g_netSession.reliablePeerSlots[peerSlot]
					.sendSeq = 0;
			}
		}
		packetHeader |= 0x8000;
		encodedPacket.packetTypeHeader = packetHeader;
		encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
		encodedHeaderSize = 2;
		if (appendPending &&
		    NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPacket.payloadSize = payloadSize - 4;
			encodedPayload = encodedPacket.payload;
			encodedHeaderSize = 4;
		}
		memcpy(encodedPayload, payload + 1, payloadSize - 4);
		encodedPayload += payloadSize - 4;
		encodedSize = payloadSize + encodedHeaderSize - 4;
		if (appendPending) {
			memcpy(encodedPayload,
			       &g_netSession.reliablePeerSlots[peerSlot]
					.lastPiggybackType,
			       g_netSession.reliablePeerSlots[peerSlot]
				       .piggybackLength);
			encodedSize += g_netSession.reliablePeerSlots[peerSlot]
					       .piggybackLength;
		}
		g_netSession.reliablePeerSlots[peerSlot].lastPiggybackType =
			packetTypeByte[0];
		memcpy(g_netSession.reliablePeerSlots[peerSlot]
			       .piggybackPayload,
		       payload + 1, payloadSize - 4);
		g_netSession.reliablePeerSlots[peerSlot].piggybackLength =
			payloadSize - 3;
	}

	if ((packetType != NET_PACKET_REMOTE_INPUT ||
	     g_gameConfig.internetPlay != 1) &&
	    g_netSession.localPlayerInfo.directPlayId != directPlayId) {
		if (packetType == NET_PACKET_WORLD_MESSAGE) {
			struct NetQueuedPacket *queuedPacket;
			memcpy(g_netSessionSentWorldMessageHistory
				       [g_netSessionSentWorldMessageWriteIndex]
					       .payload,
			       payload, payloadSize);
			queuedPacket =
				&g_netSessionSentWorldMessageHistory
					[g_netSessionSentWorldMessageWriteIndex];
			queuedPacket->directPlayId = directPlayId;
			queuedPacket->payloadSize = payloadSize;
			queuedPacket->lastNackMs = 0;
			queuedPacket->nackRetryCount = 0;
			queuedPacket->packetClass = 0;
			queuedPacket->sequenceByte =
				(encodedPacket.packetTypeHeader & 0x7F00) >> 8;
			++g_netSessionSentWorldMessageWriteIndex;
			if (g_netSessionSentWorldMessageWriteIndex >= 256) {
				g_netSessionSentWorldMessageWriteIndex = 0;
			}
		}

		{
			int historyIndex;
			memcpy(g_netSessionSentHistory
				       [g_netSessionSentHistoryWriteIndex]
					       .payload,
			       payload, payloadSize);
			historyIndex = g_netSessionSentHistoryWriteIndex;
			g_netSessionSentHistory[historyIndex].directPlayId =
				directPlayId;
			g_netSessionSentHistory[historyIndex].payloadSize =
				payloadSize;
			g_netSessionSentHistory[historyIndex].lastNackMs = 0;
			g_netSessionSentHistory[historyIndex].nackRetryCount =
				0;
			if (directPlayId == 0) {
				g_netSessionSentHistory[historyIndex]
					.packetClass = 0;
			} else if (directPlayId == g_netSession.groupDplayId) {
				g_netSessionSentHistory[historyIndex]
					.packetClass = 2;
			} else {
				g_netSessionSentHistory[historyIndex]
					.packetClass = 1;
			}
			++g_netSessionSentHistoryWriteIndex;
			g_netSessionSentHistory[historyIndex].sequenceByte =
				(encodedPacket.packetTypeHeader & 0x7F00) >> 8;
		}
		if (g_netSessionSentHistoryWriteIndex >= 128) {
			g_netSessionSentHistoryWriteIndex = 0;
		}
	}

	if (g_netSession.localPlayerInfo.directPlayId == directPlayId ||
	    directPlayId == 0 || g_netSession.dplayInterface == NULL ||
	    directPlayId == g_netSession.groupDplayId) {
		if ((int)g_netRecvQueueCount >= 1024) {
			NetReliable_KeepOnlyHostReceivedPackets();
		}
		if ((int)g_netRecvQueueCount < 1024) {
			unsigned int queueIndex;
			unsigned int peerSlot;
			int peerSlotAvailable;
			memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				       .payload,
			       payload, payloadSize);
			queueIndex = (unsigned int)g_netRecvQueueWriteIndex;
			g_netSessionRecvQueue[queueIndex].directPlayId =
				(DPID)g_netSession.localPlayerInfo.directPlayId;
			g_netSessionRecvQueue[queueIndex].payloadSize =
				payloadSize;
			g_netSessionRecvQueue[queueIndex].lastNackMs = 0;
			g_netSessionRecvQueue[queueIndex].nackRetryCount = 0;
			g_netSessionRecvQueue[queueIndex].isResentCopy = 0;
			peerSlot = NetReliable_FindOrCreatePeerSlot(
				g_netSession.localPlayerInfo.directPlayId);
			if (packetType == NET_PACKET_REMOTE_INPUT &&
			    g_gameConfig.internetPlay == 1) {
				peerSlotAvailable =
					peerSlot <
					g_netSession.reliablePeerSlotCount;
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 2;
#ifdef XVT_MODERN
				if (peerSlotAvailable && peerSlot < 40) {
#else
				if (peerSlotAvailable < 40) {
#endif
					packetHeader &= 0x7F00;
					packetHeader >>= 8;
					g_netSession.reliablePeerSlots[peerSlot]
						.recvSeqChannelB = packetHeader;
				}
			} else if (directPlayId == 0) {
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 0;
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 40) {
					packetHeader &= 0x7F00;
					packetHeader >>= 8;
					g_netSession.reliablePeerSlots[peerSlot]
						.recvSeqChannelA = packetHeader;
				}
			} else if (directPlayId == g_netSession.groupDplayId) {
				peerSlotAvailable =
					peerSlot <
					g_netSession.reliablePeerSlotCount;
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 2;
#ifdef XVT_MODERN
				if (peerSlotAvailable && peerSlot < 40) {
#else
				if (peerSlotAvailable < 40) {
#endif
					packetHeader &= 0x7F00;
					packetHeader >>= 8;
					g_netSession.reliablePeerSlots[peerSlot]
						.recvSeqChannelB = packetHeader;
				}
			} else {
				peerSlotAvailable =
					peerSlot <
					g_netSession.reliablePeerSlotCount;
				g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
					.packetClass = 1;
#ifdef XVT_MODERN
				if (peerSlotAvailable && peerSlot < 40) {
#else
				if (peerSlotAvailable < 40) {
#endif
					packetHeader &= 0x7F00;
					packetHeader >>= 8;
					g_netSession.reliablePeerSlots[peerSlot]
						.recvSeqDefault = packetHeader;
				}
			}
			g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				.sequenceByte =
				(encodedPacket.packetTypeHeader & 0x7F00) >> 8;
			++g_netRecvQueueCount;
			++g_netRecvQueueWriteIndex;
			if (g_netRecvQueueWriteIndex >= 1024) {
				g_netRecvQueueWriteIndex = 0;
			}
		}
	}

	if (g_netSession.dplayInterface == NULL) {
		return 1;
	}
	if (g_netSession.localPlayerInfo.directPlayId != directPlayId) {
		sendResult = g_netSession.dplayInterface->lpVtbl->Send(
			g_netSession.dplayInterface,
			(DPID)g_netSession.localPlayerInfo.directPlayId,
			(DPID)directPlayId, 0, &encodedPacket.packetTypeHeader,
			encodedSize);
	}
	return sendResult == 0;
}

/* Resends a packet from history to one player under its original channel class
 * and sequence: the header carries the type, the sequence and bit 0x80 with bit
 * 15 clear, which marks a resend, then the class byte; outside the resync types
 * the body gets a 2-byte length and a trailing NOP instead of a saved copy.
 * Sent to this player itself, it is queued locally as a resent copy when the
 * queue has room, in g_netSessionRecvQueue with g_netRecvQueueWriteIndex and
 * g_netRecvQueueCount. Returns 1 when DirectPlay takes it, when it was for this
 * player, or with no DirectPlay interface; else 0. Does not check that
 * packetSize is at least 4. */
// FUNCTION: XVT 0x46DD80
int NetSession_SendSequencedGamePacket(int destDplayId, uint8_t packetClass,
				       uint8_t sequence,
				       const unsigned int *packet,
				       unsigned int packetSize)
{
	int appendTerminator;
	HRESULT sendResult;
	struct NetSessionSequencedEncodedPacket encodedPacket;
	unsigned int packetType;
	uint16_t packetFlags;
	uint8_t *encodedPayload;
	int encodedSize;
	unsigned int packetDataSize;
	unsigned int queueIndex;
	unsigned int queueCount;
	int fixedPayloadSize;

	sendResult = 0;
	if (g_netSession.dplayInterface == NULL) {
		return 1;
	}

	packetType = *packet;
	packetFlags = (uint8_t)packetType & 0x7F;
	appendTerminator = packetType < NET_PACKET_RESYNC_CHECKSUMS ||
			   packetType >= NET_PACKET_RESYNC_CHUNK + 1;
	packetFlags |= (uint16_t)(sequence & 0x7F) << 8;
	packetFlags |= 0x80;
	packetFlags &= 0x7FFF;
	encodedPacket.packetTypeHeader = (int16_t)packetFlags;
	encodedPacket.packetClass = packetClass;
	encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
	encodedSize = 3;
	if (appendTerminator) {
		fixedPayloadSize =
			NetSession_GetFixedPayloadSize((int)packetType);
		packetDataSize = packetSize;
		if (fixedPayloadSize == 0) {
			encodedPayload = encodedPacket.payload;
			encodedPacket.payloadSize =
				(int16_t)(packetDataSize - 4);
			encodedSize = 5;
		}
	} else {
		packetDataSize = packetSize;
	}

	memcpy(encodedPayload, packet + 1, packetDataSize - 4);
	encodedPayload += packetDataSize - 4;
	encodedSize += (int)packetDataSize - 4;
	if (appendTerminator) {
		*encodedPayload = NET_PACKET_NOP;
		++encodedSize;
	}

	if (g_netSession.localPlayerInfo.directPlayId != destDplayId) {
		sendResult = g_netSession.dplayInterface->lpVtbl->Send(
			g_netSession.dplayInterface,
			(DPID)g_netSession.localPlayerInfo.directPlayId,
			(DPID)destDplayId, 0, &encodedPacket.packetTypeHeader,
			(uint32_t)encodedSize);
	} else {
		if ((int)g_netRecvQueueCount >= 1024) {
			NetReliable_KeepOnlyHostReceivedPackets();
		}
		if ((int)g_netRecvQueueCount < 1024) {
			memcpy(g_netSessionRecvQueue[g_netRecvQueueWriteIndex]
				       .payload,
			       packet, packetDataSize);
			queueIndex = (unsigned int)g_netRecvQueueWriteIndex;
			g_netSessionRecvQueue[queueIndex].directPlayId =
				(DPID)g_netSession.localPlayerInfo.directPlayId;
			g_netSessionRecvQueue[queueIndex].payloadSize =
				packetDataSize;
			queueCount = g_netRecvQueueCount;
			g_netSessionRecvQueue[queueIndex].packetClass =
				packetClass;
			++queueCount;
			g_netSessionRecvQueue[queueIndex].sequenceByte =
				sequence;
			g_netRecvQueueCount = queueCount;
			g_netSessionRecvQueue[queueIndex].isResentCopy = 1;
			g_netSessionRecvQueue[queueIndex].lastNackMs = 0;
			g_netSessionRecvQueue[queueIndex].nackRetryCount = 0;
			++g_netRecvQueueWriteIndex;
			if (g_netRecvQueueWriteIndex >= 1024) {
				g_netRecvQueueWriteIndex = 0;
			}
		}
	}

	return sendResult == 0;
}

/* Returns the roster, g_netSession.players, and stores its entry count in
 * *outCount. */
// FUNCTION: XVT 0x46DFA0
struct SessionPlayerInfo *NetSession_GetPlayerRoster(int *outCount)
{
	*outCount = g_netSession.playerCount;
	return g_netSession.players;
}

/* Returns this player's own entry, g_netSession.localPlayerInfo. Nothing calls
 * this. */
// FUNCTION: XVT 0x46DFC0
struct SessionPlayerInfo *NetSession_GetLocalPlayerInfo(void)
{
	return &g_netSession.localPlayerInfo;
}

/* Copies playerCount entries into the roster and sets the roster count; returns
 * 1. Does not check playerCount against the 8 slots. Nothing calls this. */
// FUNCTION: XVT 0x46DFD0
int NetSession_SetPlayerRoster(const struct SessionPlayerInfo *players,
			       int playerCount)
{
	memcpy(g_netSession.players, players,
	       (size_t)playerCount * sizeof(*players));
	g_netSession.playerCount = playerCount;
	return 1;
}

/* Returns the roster count, or 1 when it is 0. */
// FUNCTION: XVT 0x46E000
int NetSession_GetPlayerCount(void)
{
	if (g_netSession.playerCount == 0) {
		return 1;
	}
	return g_netSession.playerCount;
}

/* Returns g_netSession.localIsHost, nonzero when this player hosts. */
// FUNCTION: XVT 0x46E040
int NetSession_IsLocalHost(void) { return g_netSession.localIsHost; }

/* Returns the next game packet from NetSession_ReceivePacket, with its sender
 * and size, or NULL when none is ready. DirectPlay system messages (sender 0)
 * met on the way go to NetSession_HandleDirectPlaySystemMessage and are not
 * returned. The packet stays valid until the next receive. */
// FUNCTION: XVT 0x46E050
int *NetSession_ReceiveGamePacket(int *outSenderDpid, int *outPayloadSize)
{
	int *packet;

	for (;;) {
		packet = (int *)NetSession_ReceivePacket(outSenderDpid,
							 outPayloadSize);
		if (packet == NULL || *outSenderDpid != 0) {
			return packet;
		}
		NetSession_HandleDirectPlaySystemMessage(*packet, packet);
	}
}

/* Acts on a DirectPlay system message, writing g_netSession and
 * g_netSessionScratchPacket; only the host acts, except on a name change. A new
 * player is sent a SEQUENCE_STATUS with every reliable peer slot's id and
 * sequences, then a FLIGHT_SESSION_STATUS with the mission's elapsed seconds
 * and the protocol version (103 in the modern build, 101 in the original). A
 * departed player is first reported to XvtNetworkSession_HostLost in the modern
 * build when it was the host; the host then queues itself a STARTUP_READY if
 * the player was active, removes it from the DirectPlay group, marks it
 * inactive, and frees its reliable peer slot by moving the last slot into its
 * place. A name change updates the matching roster entry's names, cut to 12
 * characters; the original arm copies them with strcpy, unbounded. When
 * g_netSessionFlightHandshakeActive is 0, a new player's session status carries
 * 0 instead, and the roster is listed again with every other player added to
 * the group, and a departure lists the roster again; that never happens after a
 * session init. Returns a value no caller uses. */
// FUNCTION: XVT 0x46E080
int NetSession_HandleDirectPlaySystemMessage(int packetOpcode, int *packet)
{
	enum {
		RELIABLE_SEQUENCE_SENTINEL = 127,
		PEER_SNAPSHOT_METADATA_DWORD_COUNT = 3,
		PEER_SNAPSHOT_STRIDE = 8,
		PEER_PREV_CHANNEL_A_OFFSET = 4,
		PEER_PREV_CHANNEL_B_OFFSET = 5,
		PEER_CHANNEL_A_OFFSET = 6,
		PEER_CHANNEL_B_OFFSET = 7,
		PEER_SLOT_QWORD_COUNT =
			sizeof(struct NetReliablePeerSlot) / sizeof(uint64_t)
	};

	int result = packetOpcode;
	int playerIndex;
	int peerIndex;
	uint8_t handshakeActive;
	struct NetReliablePeerSlot *peer;
	uint8_t *encodedPeer;

	switch (packetOpcode) {
	case DPSYS_CREATEPLAYERORGROUP:
		result = NetSession_IsLocalHost();
		if (result == 0) {
			return result;
		}
		handshakeActive = g_netSessionFlightHandshakeActive;
		result = packet[1];
		if (handshakeActive != 0) {
			if (result == 1 &&
			    packet[2] !=
				    g_netSession.localPlayerInfo.directPlayId) {
				g_netSessionScratchPacket.payloadDwords[0] = 0;
				g_netSessionScratchPacket.packetType =
					NET_PACKET_SEQUENCE_STATUS;
				g_netSessionScratchPacket.payloadDwords[1] =
					(int)g_netSession.reliablePeerSlotCount;
				g_netSessionScratchPacket.payloadDwords[2] =
					(int)timeGetTime();
				encodedPeer =
					(uint8_t *)&g_netSessionScratchPacket.payloadDwords
						[PEER_SNAPSHOT_METADATA_DWORD_COUNT];
				for (peerIndex = 0;
				     peerIndex <
				     (int)g_netSession.reliablePeerSlotCount;
				     ++peerIndex) {
					peer = &g_netSession.reliablePeerSlots
							[peerIndex];
					memcpy(&encodedPeer
						       [peerIndex *
							PEER_SNAPSHOT_STRIDE],
					       &peer->directPlayId,
					       sizeof(peer->directPlayId));
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_PREV_CHANNEL_A_OFFSET] =
						(uint8_t)peer
							->lastDeliveredSeqChannelA;
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_PREV_CHANNEL_B_OFFSET] =
						(uint8_t)peer
							->lastDeliveredSeqChannelB;
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_CHANNEL_A_OFFSET] =
						(uint8_t)peer->recvSeqChannelA;
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_CHANNEL_B_OFFSET] =
						(uint8_t)peer->recvSeqChannelB;
				}
				NetSession_SendPacket(
					packet[2],
					(unsigned int
						 *)&g_netSessionScratchPacket,
					8 * (int)g_netSession.reliablePeerSlotCount +
						16);
				g_netSessionScratchPacket.packetType =
					NET_PACKET_FLIGHT_SESSION_STATUS;
				g_netSessionScratchPacket.payloadDwords[0] =
					Mission_GetElapsedClockSeconds();
				g_netSessionScratchPacket.payloadDwords[1] =
#ifdef XVT_MODERN
					FRONTEND_NET_PROTOCOL_VERSION;
#else
					101;
#endif
				g_netSessionScratchPacket.payloadDwords[2] = 0;
				return NetSession_SendPacket(
					packet[2],
					(unsigned int
						 *)&g_netSessionScratchPacket,
					16);
			}
		} else if (result == 1) {
			if (packet[2] !=
			    g_netSession.localPlayerInfo.directPlayId) {
				g_netSessionScratchPacket.payloadDwords[0] = 0;
				g_netSessionScratchPacket.packetType =
					NET_PACKET_SEQUENCE_STATUS;
				g_netSessionScratchPacket.payloadDwords[1] =
					(int)g_netSession.reliablePeerSlotCount;
				g_netSessionScratchPacket.payloadDwords[2] =
					(int)timeGetTime();
				encodedPeer =
					(uint8_t *)&g_netSessionScratchPacket.payloadDwords
						[PEER_SNAPSHOT_METADATA_DWORD_COUNT];
				for (peerIndex = 0;
				     peerIndex <
				     (int)g_netSession.reliablePeerSlotCount;
				     ++peerIndex) {
					peer = &g_netSession.reliablePeerSlots
							[peerIndex];
					memcpy(&encodedPeer
						       [peerIndex *
							PEER_SNAPSHOT_STRIDE],
					       &peer->directPlayId,
					       sizeof(peer->directPlayId));
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_PREV_CHANNEL_A_OFFSET] =
						(uint8_t)peer
							->lastDeliveredSeqChannelA;
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_PREV_CHANNEL_B_OFFSET] =
						(uint8_t)peer
							->lastDeliveredSeqChannelB;
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_CHANNEL_A_OFFSET] =
						(uint8_t)peer->recvSeqChannelA;
					encodedPeer[peerIndex *
							    PEER_SNAPSHOT_STRIDE +
						    PEER_CHANNEL_B_OFFSET] =
						(uint8_t)peer->recvSeqChannelB;
				}
				NetSession_SendPacket(
					packet[2],
					(unsigned int
						 *)&g_netSessionScratchPacket,
					8 * (int)g_netSession.reliablePeerSlotCount +
						16);
				g_netSessionScratchPacket.packetType =
					NET_PACKET_FLIGHT_SESSION_STATUS;
				g_netSessionScratchPacket.payloadDwords[0] = 0;
				NetSession_SendPacket(
					packet[2],
					(unsigned int
						 *)&g_netSessionScratchPacket,
					8);
			}
			g_netSession.playerCount = 0;
			NetSession_EnumeratePlayers();
			for (playerIndex = 0;
			     playerIndex < g_netSession.playerCount;
			     ++playerIndex) {
				if (g_netSession.players[playerIndex]
					    .directPlayId !=
				    g_netSession.localPlayerInfo.directPlayId) {
					NetSession_AddPlayerToGroup(
						&g_netSession
							 .players[playerIndex]);
				}
			}
		}
		return result;

	case DPSYS_DESTROYPLAYERORGROUP:
#ifdef XVT_MODERN
		if (packet[1] == DPPLAYERTYPE_PLAYER &&
		    packet[2] == g_netSession.hostDplayId) {
			XvtNetworkSession_HostLost();
		}
#endif
		result = NetSession_IsLocalHost();
		if (result == 0) {
			return result;
		}
		if (g_netSessionFlightHandshakeActive != 0) {
			result = packet[1];
			if (result == 1) {
				result = g_netSession.playerCount;
				for (playerIndex = 0;
				     playerIndex < g_netSession.playerCount;
				     ++playerIndex) {
					if (g_netSession.players[playerIndex]
						    .directPlayId ==
					    packet[2]) {
						if (g_netSession
							    .players[playerIndex]
							    .activeFlag != 0) {
							g_netSessionScratchPacket
								.packetType =
								NET_PACKET_STARTUP_READY;
							NetSession_SendPacket(
								g_netSession
									.localPlayerInfo
									.directPlayId,
								(unsigned int
									 *)&g_netSessionScratchPacket,
								4);
						}
						result =
							NetSession_RemovePlayerFromGroup(
								packet[2]);
						g_netSession
							.players[playerIndex]
							.activeFlag = 0;
						break;
					}
				}
				/* Below, playerIndex indexes reliable peer slots, to drop the departed player's slot. */
				playerIndex = 0;
				if ((int)g_netSession.reliablePeerSlotCount <=
				    playerIndex) {
					return result;
				}
				result = packet[2];
				do {
					if (g_netSession
						    .reliablePeerSlots
							    [playerIndex]
						    .directPlayId ==
					    (DPID)packet[2]) {
						--g_netSession
							  .reliablePeerSlotCount;
						memcpy(&g_netSession.reliablePeerSlots
								[playerIndex],
						       &g_netSession.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount],
						       sizeof(g_netSession.reliablePeerSlots
								      [playerIndex]));
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.directPlayId = 0;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.lastDeliveredSeqDefault =
							RELIABLE_SEQUENCE_SENTINEL;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.lastDeliveredSeqChannelA =
							RELIABLE_SEQUENCE_SENTINEL;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.lastDeliveredSeqChannelB =
							RELIABLE_SEQUENCE_SENTINEL;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.recvSeqDefault =
							RELIABLE_SEQUENCE_SENTINEL;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.recvSeqChannelA =
							RELIABLE_SEQUENCE_SENTINEL;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.recvSeqChannelB =
							RELIABLE_SEQUENCE_SENTINEL;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.sendSeq = 0;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.lastPiggybackType =
							NET_PACKET_NOP;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.piggybackLength = 1;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.lastActivityMs = 0;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.packetCount = 0;
						g_netSession
							.reliablePeerSlots
								[g_netSession
									 .reliablePeerSlotCount]
							.packetDropCount = 0;
						return PEER_SLOT_QWORD_COUNT *
						       (int)g_netSession
							       .reliablePeerSlotCount;
					}
					++playerIndex;
				} while (playerIndex <
					 (int)g_netSession
						 .reliablePeerSlotCount);
				return result;
			}
			return result;
		}
		result = packet[1];
		if (result != 1) {
			return result;
		}
		g_netSession.playerCount = 0;
		NetSession_EnumeratePlayers();
		result = NetSession_RemovePlayerFromGroup(packet[2]);
		/* Below, playerIndex indexes reliable peer slots, to drop the departed player's slot. */
		playerIndex = 0;
		if ((int)g_netSession.reliablePeerSlotCount <= playerIndex) {
			return result;
		}
		result = packet[2];
		do {
			if (g_netSession.reliablePeerSlots[playerIndex]
				    .directPlayId == (DPID)packet[2]) {
				--g_netSession.reliablePeerSlotCount;
				memcpy(&g_netSession
						.reliablePeerSlots[playerIndex],
				       &g_netSession.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount],
				       sizeof(g_netSession.reliablePeerSlots
						      [playerIndex]));
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.directPlayId = 0;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.lastDeliveredSeqDefault =
					RELIABLE_SEQUENCE_SENTINEL;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.lastDeliveredSeqChannelA =
					RELIABLE_SEQUENCE_SENTINEL;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.lastDeliveredSeqChannelB =
					RELIABLE_SEQUENCE_SENTINEL;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.recvSeqDefault =
					RELIABLE_SEQUENCE_SENTINEL;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.recvSeqChannelA =
					RELIABLE_SEQUENCE_SENTINEL;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.recvSeqChannelB =
					RELIABLE_SEQUENCE_SENTINEL;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.sendSeq = 0;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.lastPiggybackType = NET_PACKET_NOP;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.piggybackLength = 1;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.lastActivityMs = 0;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.packetCount = 0;
				g_netSession
					.reliablePeerSlots
						[g_netSession
							 .reliablePeerSlotCount]
					.packetDropCount = 0;
				return PEER_SLOT_QWORD_COUNT *
				       (int)g_netSession.reliablePeerSlotCount;
			}
			++playerIndex;
		} while (playerIndex < (int)g_netSession.reliablePeerSlotCount);
		return result;

	case DPSYS_SETPLAYERORGROUPNAME:
		result = ((const struct NetPlayerNameMessage *)packet)
				 ->header.dwPlayerType;
		if (result == 1) {
			result = g_netSession.playerCount;
			for (playerIndex = 0;
			     playerIndex < g_netSession.playerCount;
			     ++playerIndex) {
				if (g_netSession.players[playerIndex]
					    .directPlayId ==
				    (int)((const struct NetPlayerNameMessage *)
						  packet)
					    ->header.dpId) {
#ifdef XVT_MODERN
					if (!XvtNetworkSession_CopyPlayerNames(
						    (const struct
						     NetPlayerNameMessage *)
							    packet,
						    g_netSession
							    .players[playerIndex]
							    .playerName,
						    sizeof(g_netSession
								   .players[playerIndex]
								   .playerName),
						    g_netSession
							    .players[playerIndex]
							    .longName,
						    sizeof(g_netSession
								   .players[playerIndex]
								   .longName))) {
						return 0;
					}
#else
					strcpy(g_netSession.players[playerIndex]
						       .playerName,
					       ((const struct
						 NetPlayerNameMessage *)packet)
						       ->names);
					strcpy(g_netSession.players[playerIndex]
						       .longName,
					       &((const struct
						  NetPlayerNameMessage *)packet)
							->names[strlen(g_netSession
									       .players[playerIndex]
									       .playerName) +
								1]);
#endif
					g_netSession.players[playerIndex]
						.playerName[12] = '\0';
					g_netSession.players[playerIndex]
						.longName[12] = '\0';
					return 0;
				}
			}
		}
		return result;

	default:
		return result;
	}
}

/* Returns the next packet to hand to the game, in order per peer and channel,
 * or NULL; *outSenderDpid and *outPayloadSize describe it, and the pointer,
 * into g_netSession.recvScratchPacket, is valid until the next call. It first
 * runs NetSession_PumpIncomingPackets and NetSession_SendReliableKeepalives. A
 * system message is returned only from the front of the queue. Entries from a
 * peer with no reliable slot (all 40 taken), or 1 to 28 sequence numbers behind
 * the next expected one (counting around the 128 wrap), are dropped. The entry
 * with the next expected sequence on its channel is returned, and an
 * internet-play REMOTE_INPUT is returned as soon as it is met, skipping any
 * gap. On a gap it looks in the queue for the missing sequences and returns the
 * expected one when found; otherwise it sends a NACK per missing sequence, or
 * for a world message a WORLD_NACK holding the missing message's tick estimated
 * from g_netUpdateIntervalTicks, and repeats them after a wait that doubles
 * each time, from 2,000 ms; once its NACK count passes 3 (5 for world messages)
 * it gives up the gap and returns the first packet it holds past it. Per call,
 * a peer's entries stop being examined once its examined count, which also
 * grows by the size of each gap, passes 90, or its count of missing sequences
 * not found passes 25. When the queue holds 1,023
 * entries or more and nothing was returned, it returns the first entry already
 * NACKed, giving up its gap. Writes the peer slots' delivered sequences,
 * activity times and counts, g_netLastDeliveredRecvSequence,
 * g_netSession.recvScratchPacket, and the receive queue: g_netRecvQueueCount,
 * g_netRecvQueueReadIndex and its entries, also through
 * NetReliable_RemoveQueuedPacket. */
// FUNCTION: XVT 0x46E780
void *NetSession_ReceivePacket(int *outSenderDpid, int *outPayloadSize)
{
	int sequence;
	int expectedSequence;
	int queueIndex;

	struct {
		int wantChannelA; /* Entry is on the all-players channel */
		int wantChannelB; /* Entry is on the group channel */
		unsigned int remainingQueueEntries; /* Entries left to scan */
	} channels;

	int nextSequence;
	int sequenceDistance;
	int queuedIndex;
	int payloadType;
	int missingTickOffset;
	uint8_t *payload;
	int sentRetry;
	int unusedSearchIndex;
	int remoteSequence;
	uint8_t inspected[40];
	uint8_t retryCounts[40];
	uint8_t lastSequences[40][3];
	uint8_t inspectionLimits[40];
	struct NetSessionScratchPacket retryPacket;
	struct NetQueuedPacket *packet;
	unsigned int oldPeerCount;
	unsigned int peerIndex;
	int peerSlotIndex;
	int peerSlotsRemaining;
	int delta;
	uint32_t now;
	unsigned int timeout;
	unsigned int retryLimit;
	int searchSequence;
	int directPlayId;

	extern int g_netUpdateIntervalTicks;

	NetSession_PumpIncomingPackets();
	NetSession_SendReliableKeepalives();
	if (g_netRecvQueueCount == 0) {
		return NULL;
	}

	memset(retryCounts, 0, sizeof(retryCounts));
	memset(inspected, 0, sizeof(inspected));
	memset(inspectionLimits, 90, sizeof(inspectionLimits));
	memset(lastSequences, 0, sizeof(lastSequences));
	peerSlotsRemaining = g_netSession.reliablePeerSlotCount;
	if ((int)g_netSession.reliablePeerSlotCount > 0) {
		peerSlotIndex = 0;
		do {
			lastSequences[peerSlotIndex][0] =
				(uint8_t)g_netSession
					.reliablePeerSlots[peerSlotIndex]
					.lastDeliveredSeqChannelA;
			lastSequences[peerSlotIndex][1] =
				(uint8_t)g_netSession
					.reliablePeerSlots[peerSlotIndex]
					.lastDeliveredSeqDefault;
			lastSequences[peerSlotIndex][2] =
				(uint8_t)g_netSession
					.reliablePeerSlots[peerSlotIndex]
					.lastDeliveredSeqChannelB;
			++peerSlotIndex;
			--peerSlotsRemaining;
		} while (peerSlotsRemaining != 0);
	}

	queueIndex = g_netRecvQueueReadIndex;
	channels.remainingQueueEntries = g_netRecvQueueCount;
	while ((int)channels.remainingQueueEntries > 0) {
		directPlayId = g_netSessionRecvQueue[queueIndex].directPlayId;
		packet = &g_netSessionRecvQueue[queueIndex];
		if (directPlayId == 0) {
			if (queueIndex == g_netRecvQueueReadIndex) {
				memcpy(&g_netSession.recvScratchPacket,
				       &g_netSessionRecvQueue[queueIndex],
				       sizeof(g_netSession.recvScratchPacket));
				*outSenderDpid =
					g_netSessionRecvQueue[queueIndex]
						.directPlayId;
				*outPayloadSize =
					g_netSessionRecvQueue[queueIndex]
						.payloadSize;
				--g_netRecvQueueCount;
				++g_netRecvQueueReadIndex;
				if (g_netRecvQueueReadIndex >= 1024) {
					g_netRecvQueueReadIndex = 0;
				}
				return g_netSession.recvScratchPacket.payload;
			}
			++queueIndex;
			if (queueIndex >= 1024) {
				queueIndex = 0;
			}
			--channels.remainingQueueEntries;
			continue;
		}
		sequence = g_netSessionRecvQueue[queueIndex].sequenceByte;
		oldPeerCount = g_netSession.reliablePeerSlotCount;
		channels.wantChannelA =
			g_netSessionRecvQueue[queueIndex].packetClass == 0;
		channels.wantChannelB =
			g_netSessionRecvQueue[queueIndex].packetClass == 2;
		peerIndex = NetReliable_FindOrCreatePeerSlot(directPlayId);
		if (oldPeerCount != g_netSession.reliablePeerSlotCount &&
		    peerIndex < 40) {
			lastSequences[peerIndex][0] = 127;
			lastSequences[peerIndex][1] = 127;
			lastSequences[peerIndex][2] = 127;
		}
		if (peerIndex < g_netSession.reliablePeerSlotCount) {
			if (inspectionLimits[peerIndex] <
			    inspected[peerIndex]) {
				++queueIndex;
				if (queueIndex >= 1024) {
					queueIndex = 0;
				}
				--channels.remainingQueueEntries;
				continue;
			}
			if (g_netSessionRecvQueue[queueIndex].isResentCopy ==
			    0) {
				++inspected[peerIndex];
			}

			if (channels.wantChannelA) {
				expectedSequence =
					g_netSession
						.reliablePeerSlots[peerIndex]
						.lastDeliveredSeqChannelA +
					1;
				if (expectedSequence > 127) {
					expectedSequence = 0;
				}
				nextSequence =
					(unsigned int)
						lastSequences[peerIndex][0] +
					1;
				if (nextSequence > 127) {
					nextSequence = 0;
				}
			} else if (channels.wantChannelB) {
				expectedSequence =
					g_netSession
						.reliablePeerSlots[peerIndex]
						.lastDeliveredSeqChannelB +
					1;
				if (expectedSequence > 127) {
					expectedSequence = 0;
				}
				nextSequence =
					(unsigned int)
						lastSequences[peerIndex][2] +
					1;
				if (nextSequence > 127) {
					nextSequence = 0;
				}
			} else {
				expectedSequence =
					g_netSession
						.reliablePeerSlots[peerIndex]
						.lastDeliveredSeqDefault +
					1;
				if (expectedSequence > 127) {
					expectedSequence = 0;
				}
				nextSequence =
					(unsigned int)
						lastSequences[peerIndex][1] +
					1;
				if (nextSequence > 127) {
					nextSequence = 0;
				}
			}
		}
#ifdef XVT_MODERN
		else {
			expectedSequence = 0;
			nextSequence = 0;
		}
#endif

		payload = g_netSessionRecvQueue[queueIndex].payload;
#ifdef XVT_MODERN
		memcpy(&payloadType, payload, sizeof(payloadType));
#else
		payloadType = *(int *)payload;
#endif
		delta = sequence - expectedSequence;
		if (peerIndex >= g_netSession.reliablePeerSlotCount ||
		    (delta >= -28 && (delta < 0 || delta >= 100))) {
			if (NetReliable_RemoveQueuedPacket(queueIndex) != 0) {
				++queueIndex;
				if (queueIndex >= 1024) {
					queueIndex = 0;
				}
			}
			--channels.remainingQueueEntries;
			continue;
		}

		if (expectedSequence == sequence &&
		    nextSequence == expectedSequence) {
			g_netSession.reliablePeerSlots[peerIndex]
				.lastActivityMs = timeGetTime();
			if (channels.wantChannelA) {
				g_netSession.reliablePeerSlots[peerIndex]
					.lastDeliveredSeqChannelA = sequence;
			} else if (channels.wantChannelB) {
				g_netSession.reliablePeerSlots[peerIndex]
					.lastDeliveredSeqChannelB = sequence;
			} else {
				g_netSession.reliablePeerSlots[peerIndex]
					.lastDeliveredSeqDefault = sequence;
			}
			g_netLastDeliveredRecvSequence = sequence;
			if (payloadType != NET_PACKET_REMOTE_INPUT ||
			    g_gameConfig.internetPlay != 1) {
				++g_netSession.reliablePeerSlots[peerIndex]
					  .packetCount;
			}
			memcpy(&g_netSession.recvScratchPacket,
			       &g_netSessionRecvQueue[queueIndex],
			       sizeof(g_netSession.recvScratchPacket));
			NetReliable_RemoveQueuedPacket(queueIndex);
			*outSenderDpid =
				g_netSession.recvScratchPacket.directPlayId;
			*outPayloadSize =
				g_netSession.recvScratchPacket.payloadSize;
			return g_netSession.recvScratchPacket.payload;
		}

		if (payloadType == NET_PACKET_REMOTE_INPUT &&
		    g_gameConfig.internetPlay == 1) {
			g_netSession.reliablePeerSlots[peerIndex]
				.lastActivityMs = timeGetTime();
			if (channels.wantChannelA) {
				g_netSession.reliablePeerSlots[peerIndex]
					.lastDeliveredSeqChannelA = sequence;
			} else if (channels.wantChannelB) {
				g_netSession.reliablePeerSlots[peerIndex]
					.lastDeliveredSeqChannelB = sequence;
			} else {
				g_netSession.reliablePeerSlots[peerIndex]
					.lastDeliveredSeqDefault = sequence;
			}
			g_netLastDeliveredRecvSequence = sequence;
			memcpy(&g_netSession.recvScratchPacket,
			       &g_netSessionRecvQueue[queueIndex],
			       sizeof(g_netSession.recvScratchPacket));
			NetReliable_RemoveQueuedPacket(queueIndex);
			*outSenderDpid =
				g_netSession.recvScratchPacket.directPlayId;
			*outPayloadSize =
				g_netSession.recvScratchPacket.payloadSize;
			return g_netSession.recvScratchPacket.payload;
		}

		if (g_netSessionRecvQueue[queueIndex].isResentCopy == 0) {
			int selectedChannel =
				channels.wantChannelA
					? 0
					: (channels.wantChannelB ? 2 : 1);
			remoteSequence =
				(unsigned int)lastSequences[peerIndex]
							   [selectedChannel] +
				1;
			lastSequences[peerIndex][selectedChannel] =
				(uint8_t)sequence;
			if (remoteSequence > 127) {
				remoteSequence = 0;
			}
			sequenceDistance = sequence - remoteSequence;
			if (sequenceDistance < 0) {
				sequenceDistance += 128;
			}
			searchSequence = remoteSequence;
			missingTickOffset = sequenceDistance;
			missingTickOffset *= g_netUpdateIntervalTicks;
			sentRetry = 0;
			if (inspected[peerIndex] < 127) {
				inspected[peerIndex] =
					(uint8_t)(inspected[peerIndex] +
						  sequenceDistance);
			}
			if (retryCounts[peerIndex] > 25) {
				++queueIndex;
				if (queueIndex >= 1024) {
					queueIndex = 0;
				}
				--channels.remainingQueueEntries;
				continue;
			}

			while (sequence != remoteSequence) {
				unusedSearchIndex = queueIndex;
				queuedIndex = NetReliable_FindQueuedRecvPacket(
					unusedSearchIndex, remoteSequence,
					channels.wantChannelA,
					channels.wantChannelB, (int)peerIndex);
				if (queuedIndex < 1024 && queuedIndex >= 0) {
					if (expectedSequence ==
					    remoteSequence) {
						g_netSession
							.reliablePeerSlots
								[peerIndex]
							.lastActivityMs =
							timeGetTime();
						memcpy(&g_netSession
								.recvScratchPacket,
						       &g_netSessionRecvQueue
							       [queuedIndex],
						       sizeof(g_netSession
								      .recvScratchPacket));
						NetReliable_RemoveQueuedPacket(
							queuedIndex);
						if (channels.wantChannelA) {
							g_netSession
								.reliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqChannelA =
								expectedSequence;
						} else if (
							channels.wantChannelB) {
							g_netSession
								.reliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqChannelB =
								expectedSequence;
						} else {
							g_netSession
								.reliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqDefault =
								expectedSequence;
						}
						g_netLastDeliveredRecvSequence =
							expectedSequence;
						++g_netSession
							  .reliablePeerSlots
								  [peerIndex]
							  .packetCount;
						*outSenderDpid =
							g_netSession
								.recvScratchPacket
								.directPlayId;
						*outPayloadSize =
							g_netSession
								.recvScratchPacket
								.payloadSize;
						if (sequenceDistance <= 1) {
							g_netSessionRecvQueue
								[queueIndex]
									.nackRetryCount =
								0;
							g_netSessionRecvQueue
								[queueIndex]
									.lastNackMs =
								0;
						}
						return g_netSession
							.recvScratchPacket
							.payload;
					}
					--sequenceDistance;
				} else {
					++retryCounts[peerIndex];
					if (g_netSessionRecvQueue[queueIndex]
						    .nackRetryCount == 0) {
						int retryChannel =
							channels.wantChannelA
								? 0
								: (channels.wantChannelB
									   ? 2
									   : 1);
						if (payloadType ==
							    NET_PACKET_WORLD_MESSAGE
#ifdef XVT_MODERN
						    && channels.wantChannelA
#endif
						) {
							retryPacket.packetType =
								NET_PACKET_WORLD_NACK;
#ifdef XVT_MODERN
							memcpy(&retryPacket.payloadDwords
									[0],
							       payload + 4,
							       sizeof(retryPacket
									      .payloadDwords
										      [0]));
							retryPacket
								.payloadDwords
									[0] =
								(retryPacket.payloadDwords
									 [0] &
								 0x7fffffff) -
								missingTickOffset;
#else
							retryPacket
								.payloadDwords
									[0] =
								(*(int *)(payload +
									  4) &
								 0x7fffffff) -
								missingTickOffset;
#endif
							retryPacket
								.payloadDwords
									[1] =
								remoteSequence;
							NetSession_SendCompactGamePacket(
								packet->directPlayId,
								(unsigned int
									 *)&retryPacket,
								12, 1);
						} else {
							int retryDirectPlayId =
								packet->directPlayId;
							retryPacket
								.payloadDwords
									[0] =
								remoteSequence;
							retryPacket.packetType =
								NET_PACKET_NACK;
							retryPacket
								.payloadDwords
									[1] =
								retryChannel;
							NetSession_SendCompactGamePacket(
								retryDirectPlayId,
								(unsigned int
									 *)&retryPacket,
								12, 1);
						}
						++g_netSession
							  .reliablePeerSlots
								  [peerIndex]
							  .packetRetryCount;
						sentRetry = 1;
					} else {
						int timeoutPayloadType;
						now = timeGetTime();
						timeoutPayloadType =
							payloadType;
						if (g_netSession
							    .reliableUseFixedResendTimeouts !=
						    0) {
							retryLimit = 0;
							timeout =
								timeoutPayloadType ==
										2
									? 40000
									: 3000;
						} else {
							retryLimit =
								timeoutPayloadType ==
										2
									? 5
									: 3;
							timeout =
								1000
								<< g_netSessionRecvQueue
									   [queueIndex]
										   .nackRetryCount;
						}
						if (now - (uint32_t)g_netSessionRecvQueue
								    [queueIndex]
									    .lastNackMs >
						    timeout) {
							NetSession_DebugTrace(
								"(RTO) ");
							if (g_netSessionRecvQueue
								    [queueIndex]
									    .nackRetryCount >
							    retryLimit) {
								NetSession_DebugTrace(
									"(TMR) ");
								g_netSession
									.reliablePeerSlots
										[peerIndex]
									.lastActivityMs =
									timeGetTime();
								remoteSequence =
									searchSequence;
								for (;;) {
									queuedIndex = NetReliable_FindQueuedRecvPacket(
										unusedSearchIndex,
										remoteSequence,
										channels.wantChannelA,
										channels.wantChannelB,
										(int)peerIndex);
									if (queuedIndex >=
										    0 &&
									    queuedIndex <=
										    1024) {
										break;
									}
									if (sequence ==
									    remoteSequence) {
										if (channels.wantChannelA) {
											g_netSession
												.reliablePeerSlots
													[peerIndex]
												.lastDeliveredSeqChannelA =
												sequence;
										} else if (
											channels.wantChannelB) {
											g_netSession
												.reliablePeerSlots
													[peerIndex]
												.lastDeliveredSeqChannelB =
												sequence;
										} else {
											g_netSession
												.reliablePeerSlots
													[peerIndex]
												.lastDeliveredSeqDefault =
												sequence;
										}
										g_netLastDeliveredRecvSequence =
											sequence;
										++g_netSession
											  .reliablePeerSlots
												  [peerIndex]
											  .packetCount;
										memcpy(&g_netSession
												.recvScratchPacket,
										       &g_netSessionRecvQueue
											       [queueIndex],
										       sizeof(g_netSession
												      .recvScratchPacket));
										NetReliable_RemoveQueuedPacket(
											(unsigned int)
												unusedSearchIndex);
										*outSenderDpid =
											g_netSession
												.recvScratchPacket
												.directPlayId;
										*outPayloadSize =
											g_netSession
												.recvScratchPacket
												.payloadSize;
										return g_netSession
											.recvScratchPacket
											.payload;
									}
									++remoteSequence;
									if (remoteSequence >
									    127) {
										remoteSequence =
											0;
									}
								}
								memcpy(&g_netSession
										.recvScratchPacket,
								       &g_netSessionRecvQueue
									       [queuedIndex],
								       sizeof(g_netSession
										      .recvScratchPacket));
								NetReliable_RemoveQueuedPacket(
									queuedIndex);
								if (channels.wantChannelA) {
									g_netSession
										.reliablePeerSlots
											[peerIndex]
										.lastDeliveredSeqChannelA =
										remoteSequence;
								} else if (
									channels.wantChannelB) {
									g_netSession
										.reliablePeerSlots
											[peerIndex]
										.lastDeliveredSeqChannelB =
										remoteSequence;
								} else {
									g_netSession
										.reliablePeerSlots
											[peerIndex]
										.lastDeliveredSeqDefault =
										remoteSequence;
								}
								g_netLastDeliveredRecvSequence =
									remoteSequence;
								++g_netSession
									  .reliablePeerSlots
										  [peerIndex]
									  .packetCount;
								*outSenderDpid =
									g_netSession
										.recvScratchPacket
										.directPlayId;
								*outPayloadSize =
									g_netSession
										.recvScratchPacket
										.payloadSize;
								return g_netSession
									.recvScratchPacket
									.payload;
							}
							{
								int retryChannel =
									channels.wantChannelA
										? 0
										: (channels.wantChannelB
											   ? 2
											   : 1);
								if (payloadType ==
									    NET_PACKET_WORLD_MESSAGE
#ifdef XVT_MODERN
								    &&
								    channels.wantChannelA
#endif
								) {
									retryPacket
										.packetType =
										NET_PACKET_WORLD_NACK;
#ifdef XVT_MODERN
									memcpy(&retryPacket
											.payloadDwords
												[0],
									       payload +
										       4,
									       sizeof(retryPacket
											      .payloadDwords
												      [0]));
									retryPacket
										.payloadDwords
											[0] =
										(retryPacket
											 .payloadDwords
												 [0] &
										 0x7fffffff) -
										missingTickOffset;
#else
									retryPacket
										.payloadDwords
											[0] =
										(*(int *)(payload +
											  4) &
										 0x7fffffff) -
										missingTickOffset;
#endif
									retryPacket
										.payloadDwords
											[1] =
										remoteSequence;
									NetSession_SendCompactGamePacket(
										packet->directPlayId,
										(unsigned int
											 *)&retryPacket,
										12,
										1);
								} else {
									int retryDirectPlayId =
										packet->directPlayId;
									retryPacket
										.payloadDwords
											[0] =
										remoteSequence;
									retryPacket
										.packetType =
										NET_PACKET_NACK;
									retryPacket
										.payloadDwords
											[1] =
										retryChannel;
									NetSession_SendCompactGamePacket(
										retryDirectPlayId,
										(unsigned int
											 *)&retryPacket,
										12,
										1);
								}
								sentRetry = 1;
							}
						}
					}
				}
				missingTickOffset -= g_netUpdateIntervalTicks;
				++remoteSequence;
				if (remoteSequence > 127) {
					remoteSequence = 0;
				}
			}

			if (sequenceDistance <= 0) {
				g_netSessionRecvQueue[queueIndex]
					.nackRetryCount = 0;
				g_netSessionRecvQueue[queueIndex].lastNackMs =
					0;
			} else if (sentRetry == 1) {
				++g_netSessionRecvQueue[queueIndex]
					  .nackRetryCount;
				g_netSessionRecvQueue[queueIndex].lastNackMs =
					timeGetTime();
			}
		} else {
			if (g_netRecvQueueReadIndex == queueIndex) {
				if (NetReliable_RemoveQueuedPacket(
					    queueIndex) == 0) {
					--channels.remainingQueueEntries;
					continue;
				}
			}
			++queueIndex;
			if (queueIndex >= 1024) {
				queueIndex = 0;
			}
			--channels.remainingQueueEntries;
			continue;
		}
		++queueIndex;
		if (queueIndex >= 1024) {
			queueIndex = 0;
		}
		--channels.remainingQueueEntries;
	}

	if ((int)g_netRecvQueueCount < 1023) {
		return NULL;
	}
	{
		unsigned int fullQueueIndex = g_netRecvQueueReadIndex;
		int fullRemaining = g_netRecvQueueCount;
		while (fullRemaining > 0) {
			if (g_netSessionRecvQueue[fullQueueIndex]
				    .directPlayId != 0) {
				sequence = g_netSessionRecvQueue[fullQueueIndex]
						   .sequenceByte;
				channels.wantChannelA =
					g_netSessionRecvQueue[fullQueueIndex]
						.packetClass == 0;
				channels.wantChannelB =
					g_netSessionRecvQueue[fullQueueIndex]
						.packetClass == 2;
				peerIndex = NetReliable_FindOrCreatePeerSlot(
					g_netSessionRecvQueue[fullQueueIndex]
						.directPlayId);
				if (peerIndex <
					    g_netSession
						    .reliablePeerSlotCount &&
				    peerIndex < 40) {
					if (channels.wantChannelA) {
						expectedSequence =
							g_netSession
								.reliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqChannelA +
							1;
						if (expectedSequence > 127) {
							expectedSequence = 0;
						}
					} else if (channels.wantChannelB) {
						expectedSequence =
							g_netSession
								.reliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqChannelB +
							1;
						if (expectedSequence > 127) {
							expectedSequence = 0;
						}
					} else {
						expectedSequence =
							g_netSession
								.reliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqDefault +
							1;
						if (expectedSequence > 127) {
							expectedSequence = 0;
						}
					}
				}
#ifdef XVT_MODERN
				else {
					expectedSequence = 0;
				}
#endif
				delta = sequence - expectedSequence;
				if (peerIndex >=
					    g_netSession
						    .reliablePeerSlotCount ||
				    (delta >= -27 &&
				     (delta < 0 || delta >= 100))) {
					if (NetReliable_RemoveQueuedPacket(
						    fullQueueIndex) == 0) {
						--fullRemaining;
						continue;
					}
				} else if (g_netSessionRecvQueue[fullQueueIndex]
						   .nackRetryCount == 0) {
					++fullQueueIndex;
					if ((int)fullQueueIndex >= 1024) {
						fullQueueIndex = 0;
					}
					--fullRemaining;
					continue;
				} else {
					NetSession_DebugTrace("(NMR) ");
					if (channels.wantChannelA) {
						g_netSession
							.reliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelA =
							sequence;
					} else if (channels.wantChannelB) {
						g_netSession
							.reliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelB =
							sequence;
					} else {
						g_netSession
							.reliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqDefault =
							sequence;
					}
					g_netLastDeliveredRecvSequence =
						sequence;
					++g_netSession
						  .reliablePeerSlots[peerIndex]
						  .packetCount;
					memcpy(&g_netSession.recvScratchPacket,
					       &g_netSessionRecvQueue
						       [fullQueueIndex],
					       sizeof(g_netSession
							      .recvScratchPacket));
					NetReliable_RemoveQueuedPacket(
						fullQueueIndex);
					*outSenderDpid =
						g_netSession.recvScratchPacket
							.directPlayId;
					*outPayloadSize =
						g_netSession.recvScratchPacket
							.payloadSize;
					return g_netSession.recvScratchPacket
						.payload;
				}
			}
			++fullQueueIndex;
			if ((int)fullQueueIndex >= 1024) {
				fullQueueIndex = 0;
			}
			--fullRemaining;
		}
	}
	return NULL;
}

/* Sends a control packet (NACK, WORLD_NACK, KEEPALIVE) with sequence 0 and no
 * saved copy behind it: the header holds the type, with bits 0x8080 for the
 * session group or an internet-play REMOTE_INPUT; outside the resync types the
 * body gets a 2-byte length and a trailing NOP. Nothing is sent to this player
 * itself. Returns 1 when DirectPlay takes it, when the destination is this
 * player, or with no DirectPlay interface; else 0. The fourth argument, 0 or 1
 * at every call, is never read, and the deliveryMode 1 branch cannot be taken.
 * Does not check payloadSize. */
// FUNCTION: XVT 0x46F470
int NetSession_SendCompactGamePacket(int directPlayId, unsigned int *payload,
				     int payloadSize, ...)
{
	int appendTerminator;
	HRESULT sendResult;
	struct NetSessionCompactEncodedPacket encodedPacket;
	unsigned int packetType;
	uint16_t packetFlags;
	int deliveryMode;
	uint8_t *encodedPayload;
	int encodedSize;
	int packetDataSize;
	int fixedPayloadSize;

	sendResult = 0;
	if (g_netSession.dplayInterface == NULL) {
		return 1;
	}
	packetType = *payload;
	packetFlags = (uint8_t)packetType & 0x7F;
	appendTerminator = packetType < NET_PACKET_RESYNC_CHECKSUMS ||
			   packetType >= NET_PACKET_RESYNC_CHUNK + 1;
	if (packetType == NET_PACKET_REMOTE_INPUT &&
	    g_gameConfig.internetPlay == 1) {
		deliveryMode = 2;
	} else {
		if (directPlayId == 0) {
			deliveryMode = 0;
		} else if (directPlayId == g_netSession.groupDplayId) {
			deliveryMode = 2;
		} else {
			deliveryMode = 0;
		}
	}
	if (deliveryMode == 2) {
		packetFlags |= 0x8080;
	} else if (deliveryMode == 1) {
		packetFlags |= 0x8000;
	}
	encodedPacket.packetTypeHeader = (int16_t)packetFlags;
	encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
	encodedSize = 2;
	if (appendTerminator) {
		fixedPayloadSize =
			NetSession_GetFixedPayloadSize((int)packetType);
		packetDataSize = payloadSize;
		if (fixedPayloadSize == 0) {
			encodedPayload = encodedPacket.payload;
			encodedPacket.payloadSize =
				(int16_t)(packetDataSize - 4);
			encodedSize = 4;
		}
	} else {
		packetDataSize = payloadSize;
	}
	memcpy(encodedPayload, payload + 1, (size_t)(packetDataSize - 4));
	encodedPayload += packetDataSize - 4;
	encodedSize += packetDataSize - 4;
	if (appendTerminator) {
		++encodedSize;
		*encodedPayload = NET_PACKET_NOP;
	}
	if (directPlayId != g_netSession.localPlayerInfo.directPlayId) {
		sendResult = g_netSession.dplayInterface->lpVtbl->Send(
			g_netSession.dplayInterface,
			(DPID)g_netSession.localPlayerInfo.directPlayId,
			(DPID)directPlayId, 0, &encodedPacket.packetTypeHeader,
			(uint32_t)encodedSize);
	}

	return sendResult == 0;
}

#ifndef XVT_MODERN
/* Polls NetSession_ReceiveGamePacket until a packet arrives or timeoutSeconds
 * pass, without sleeping between polls; returns the packet with its sender and
 * size, or NULL on timeout. Only the original build calls this. */
// FUNCTION: XVT 0x46F5E0
int *NetSession_WaitForGamePacket(int *outDpid, int *outPayloadSize,
				  int timeoutSeconds)
{
	uint32_t startTime;
	uint32_t timeoutMilliseconds;
	int senderDpid;
	int payloadSize;
	int copiedPayloadSize;
	int *packet;

	timeoutMilliseconds = (uint32_t)(timeoutSeconds * 1000);
	startTime = timeGetTime();
	while (timeGetTime() - startTime <= timeoutMilliseconds) {
		packet =
			NetSession_ReceiveGamePacket(&senderDpid, &payloadSize);
		if (packet != NULL) {
			copiedPayloadSize = payloadSize;
			*outDpid = senderDpid;
			*outPayloadSize = copiedPayloadSize;
			return packet;
		}
	}
	return NULL;
}
#endif

/* Returns the roster slot whose DirectPlay id is dpid, or 8 when none is. */
// FUNCTION: XVT 0x46F660
int NetSession_FindPlayerSlotByDpid(int dpid)
{
	int playerSlot;

	for (playerSlot = 0; playerSlot < 8; ++playerSlot) {
		if (g_netSession.players[playerSlot].directPlayId == dpid) {
			return playerSlot;
		}
	}

	return playerSlot;
}

/* Returns the DirectPlay id in roster slot playerIndex; does not check the
 * index. Nothing calls this. */
// FUNCTION: XVT 0x46F680
int NetSession_GetPlayerDplayId(int playerIndex)
{
	return g_netSession.players[playerIndex].directPlayId;
}

/* Returns the DirectPlay id of the active roster entry numbered
 * activePlayerIndex, counting active entries from 0, or 0 when there are not
 * that many. Nothing calls this. */
// FUNCTION: XVT 0x46F690
int NetSession_GetDplayIdByActivePlayerIndex(int activePlayerIndex)
{
	int activeIndex;
	unsigned int playerSlot;

	activeIndex = 0;
	for (playerSlot = 0; playerSlot < 8; playerSlot++) {
		if (g_netSession.players[playerSlot].activeFlag != 0) {
			if (activeIndex == activePlayerIndex) {
				break;
			}
			activeIndex++;
		}
	}

	if (playerSlot < 8) {
		return g_netSession.players[playerSlot].directPlayId;
	}
	return 0;
}

/* Returns how many active roster entries come before the active entry whose id
 * is dpid; when none is, the count of all active entries. Nothing calls
 * this. */
// FUNCTION: XVT 0x46F6D0
int NetSession_FindActivePlayerIndexByDpid(int dpid)
{
	int activePlayerIndex;
	int playerSlot;

	activePlayerIndex = 0;
	for (playerSlot = 0; playerSlot < 8; playerSlot++) {
		if (g_netSession.players[playerSlot].activeFlag != 0) {
			if (g_netSession.players[playerSlot].directPlayId ==
			    dpid) {
				break;
			}
			activePlayerIndex++;
		}
	}

	return activePlayerIndex;
}

/* Returns the host's DirectPlay id, g_netSession.hostDplayId. */
// FUNCTION: XVT 0x46F700
int NetSession_GetHostDplayId(void) { return g_netSession.hostDplayId; }

/* Returns this player's DirectPlay id. */
// FUNCTION: XVT 0x46F710
int NetSession_GetLocalDplayId(void)
{
	return g_netSession.localPlayerInfo.directPlayId;
}

/* Returns the short name of the roster entry in playerSlot, found by matching
 * DirectPlay ids; in a one-player session, this player's own name whatever the
 * slot. When no entry matches, the modern build returns NULL and the original
 * reaches the end with no return statement. */
// FUNCTION: XVT 0x46F720
char *NetSession_GetPlayerName(int playerSlot)
{
	int rosterIndex;

	if (g_netSession.playerCount == 1) {
		return g_netSession.localPlayerInfo.playerName;
	}

	rosterIndex = 0;
	if (g_netSession.playerCount > rosterIndex) {
		do {
			if (NetSession_FindPlayerSlotByDpid(
				    g_netSession.players[rosterIndex]
					    .directPlayId) == playerSlot) {
				return g_netSession.players[rosterIndex]
					.playerName;
			}
			++rosterIndex;
		} while (rosterIndex < g_netSession.playerCount);
	}
#ifdef XVT_MODERN
	return NULL;
#endif
}

/* Returns the first queued player entry, or NULL when the queue is empty.
 * Nothing calls this. */
// FUNCTION: XVT 0x46F780
struct SessionPlayerInfo *NetSession_PeekQueuedPlayerInfo(void)
{
	return g_netSession.playerInfoQueueCount > 0
		       ? g_netSession.playerInfoQueue
		       : 0;
}

#ifndef XVT_MODERN
#pragma function(memcpy)
#endif
/* Takes 1 off g_netSession.playerInfoQueueCount and copies the queue up over
 * its first entry, but copies only count minus 1 bytes, not entries, so the
 * queue is not moved up. Nothing calls this. */
// FUNCTION: XVT 0x46F7A0
void NetSession_DiscardFirstQueuedPlayerInfo(void)
{
	if (g_netSession.playerInfoQueueCount > 0) {
		memcpy(g_netSession.playerInfoQueue,
		       &g_netSession.playerInfoQueue[1],
		       (size_t)(g_netSession.playerInfoQueueCount - 1));
		g_netSession.playerInfoQueueCount--;
	}
}
#ifndef XVT_MODERN
#pragma intrinsic(memcpy)
#endif

/* Returns how many roster slots from slot 0 are active before the first
 * inactive one. Nothing calls this. */
// FUNCTION: XVT 0x46F7D0
int NetSession_CountLeadingActivePlayers(void)
{
	int playerCount;

	for (playerCount = 0; playerCount < 8; ++playerCount) {
		if (g_netSession.players[playerCount].activeFlag == 0) {
			break;
		}
	}
	return playerCount;
}

/* Sets the roster count. Nothing calls this. */
// FUNCTION: XVT 0x46F7F0
void NetSession_SetPlayerCount(int playerCount)
{
	g_netSession.playerCount = playerCount;
}

/* Copies playerInfo into roster slot playerSlot, marks it active and adds 1 to
 * the roster count, even when the slot was in use. Does not check playerSlot.
 * Nothing calls this. */
// FUNCTION: XVT 0x46F800
void NetSession_AddPlayerToRosterSlot(
	int playerSlot, const struct SessionPlayerInfo *playerInfo)
{
	struct SessionPlayerInfo *rosterSlot;

	g_netSession.players[playerSlot] = *playerInfo;
	rosterSlot = &g_netSession.players[playerSlot];
	rosterSlot->activeFlag = 1;
	g_netSession.playerCount++;
}

#ifndef XVT_MODERN
/* Sends toPlayerId (0 for all players) a ROSTER_COUNT, then one ROSTER_ENTRY
 * per roster entry with its index. When sending to all, it waits after each
 * send until a packet of that type arrives, which its own queued copy
 * satisfies, and returns 0 when a 60 s wait sees none; else returns 1. Writes
 * g_netSessionScratchPacket. Only the original build calls this. */
// FUNCTION: XVT 0x46F840
int NetSession_BroadcastPlayerRoster(int toPlayerId)
{
	int outDpid;
	int outPayloadSize;
	int *packet;
	int playerIndex;

	g_netSessionScratchPacket.packetType = NET_PACKET_ROSTER_COUNT;
	g_netSessionScratchPacket.payloadDwords[0] = g_netSession.playerCount;
	NetSession_SendPacket(toPlayerId,
			      (unsigned int *)&g_netSessionScratchPacket, 8);

	if (toPlayerId == 0) {
		do {
			packet = NetSession_WaitForGamePacket(
				&outDpid, &outPayloadSize, 60);
			if (packet == NULL) {
				return 0;
			}
		} while (*packet != NET_PACKET_ROSTER_COUNT);
	}

	for (playerIndex = 0; playerIndex < g_netSession.playerCount;
	     ++playerIndex) {
		g_netSessionScratchPacket.packetType = NET_PACKET_ROSTER_ENTRY;
		g_netSessionScratchPacket.payloadDwords[0] = playerIndex;
		memcpy(&g_netSessionScratchPacket.payloadDwords[1],
		       &g_netSession.players[playerIndex],
		       sizeof(struct SessionPlayerInfo));
		NetSession_SendPacket(
			toPlayerId, (unsigned int *)&g_netSessionScratchPacket,
			48);
		if (toPlayerId == 0) {
			do {
				packet = NetSession_WaitForGamePacket(
					&outDpid, &outPayloadSize, 60);
				if (packet == NULL) {
					return 0;
				}
			} while (*packet != NET_PACKET_ROSTER_ENTRY);
		}
	}
	return 1;
}
#endif

/* Returns g_netSession.playerInfoQueueCount. Nothing calls this. */
// FUNCTION: XVT 0x46F9D0
int NetSession_GetQueuedPlayerInfoCount(void)
{
	return g_netSession.playerInfoQueueCount;
}

/* Appends a copy of playerInfo to g_netSession.playerInfoQueue. Does not check
 * that the 8-entry queue has room. Nothing calls this. */
// FUNCTION: XVT 0x46F9E0
void NetSession_QueuePlayerInfo(const struct SessionPlayerInfo *playerInfo)
{
	g_netSession.playerInfoQueue[g_netSession.playerInfoQueueCount] =
		*playerInfo;
	g_netSession.playerInfoQueueCount++;
}

/* Asks DirectPlay to add the player to the session group,
 * g_netSession.groupDplayId; returns DirectPlay's result code. Does not check
 * for a DirectPlay interface. */
// FUNCTION: XVT 0x46FA10
int NetSession_AddPlayerToGroup(const struct SessionPlayerInfo *playerInfo)
{
	return g_netSession.dplayInterface->lpVtbl->AddPlayerToGroup(
		g_netSession.dplayInterface, g_netSession.groupDplayId,
		playerInfo->directPlayId);
}

/* Returns how many of the 8 roster slots have activeFlag 1. */
// FUNCTION: XVT 0x46FA30
int NetSession_CountActivePlayers(void)
{
	int activePlayerCount = 0;
	int playerIndex;

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		if (g_netSession.players[playerIndex].activeFlag == 1) {
			++activePlayerCount;
		}
	}
	return activePlayerCount;
}

/* Asks DirectPlay to remove the player from the session group; returns
 * DirectPlay's result code. Does not check for a DirectPlay interface. */
// FUNCTION: XVT 0x46FA50
int NetSession_RemovePlayerFromGroup(int playerDplayId)
{
	return g_netSession.dplayInterface->lpVtbl->DeletePlayerFromGroup(
		g_netSession.dplayInterface, g_netSession.groupDplayId,
		(DPID)playerDplayId);
}

/* Makes the first active roster entry the host in g_netSession.hostDplayId and
 * returns 1, or returns 0 when none is active. Does not change
 * g_netSession.localIsHost. Nothing calls this. */
// FUNCTION: XVT 0x46FA70
int NetSession_SelectFirstActivePlayerAsHost(void)
{
	int playerIndex;

	playerIndex = 0;
	while (g_netSession.players[playerIndex].activeFlag == 0) {
		playerIndex++;
		if (playerIndex >= 8) {
			return 0;
		}
	}
	g_netSession.hostDplayId =
		g_netSession.players[playerIndex].directPlayId;
	return 1;
}

/* Returns 1 and does nothing else. Nothing calls this. */
// FUNCTION: XVT 0x46FAB0
int NetSession_UnusedStubReturnTrue(void) { return 1; }

/* Returns 1 and does nothing else. */
// FUNCTION: XVT 0x46FAC0
int NetSession_StubReturnTrue(void) { return 1; }

/* Returns 0 for every packet type: no type has a fixed size, so every packet
 * outside the resync types carries a 2-byte length. */
// FUNCTION: XVT 0x46FAD0
int NetSession_GetFixedPayloadSize(int packetType)
{
	(void)packetType;
	return 0;
}

/* Sends each other roster player a KEEPALIVE when its reliable peer slot has
 * been idle over 3,000 ms, holding the sequence after the newest this player
 * has seen from it on each channel (all players, group, direct) and the time;
 * the peer then resends the packet with that sequence on each channel, if it
 * has sent one. Only peers in the first 8 of the 40 slots get one. Creates a
 * slot for any roster player that has none, and writes
 * g_netSessionScratchPacket and the slot's activity time. Returns 1. */
// FUNCTION: XVT 0x46FAE0
int NetSession_SendReliableKeepalives(void)
{
	struct SessionPlayerInfo *player;
	struct SessionPlayerInfo *playerRoster;
	unsigned int playerIndex;
	unsigned int peerSlot;
	unsigned int reliablePeerIndex;
	uint32_t currentTime;
	unsigned int nextSequence;
	int playerCount;

	playerIndex = 0;
	playerRoster = NetSession_GetPlayerRoster(&playerCount);
	if (playerCount != 0) {
		player = playerRoster;
		do {
			currentTime = timeGetTime();
			if (g_netSession.localPlayerInfo.directPlayId !=
			    player->directPlayId) {
				peerSlot = NetReliable_FindOrCreatePeerSlot(
					player->directPlayId);
				if (peerSlot < g_netSession
						       .reliablePeerSlotCount &&
				    peerSlot < 8) {
					reliablePeerIndex = peerSlot;
					if (currentTime -
						    g_netSession
							    .reliablePeerSlots
								    [peerSlot]
							    .lastActivityMs >
					    3000) {
						g_netSession
							.reliablePeerSlots
								[peerSlot]
							.lastActivityMs =
							currentTime;
						NetSession_DebugTrace(
							"(Sending RRA) ");
						g_netSessionScratchPacket
							.packetType =
							NET_PACKET_KEEPALIVE;
						nextSequence =
							g_netSession
								.reliablePeerSlots
									[reliablePeerIndex]
								.recvSeqChannelA +
							1;
						if (nextSequence > 127) {
							nextSequence = 0;
						}
						g_netSessionScratchPacket
							.payloadDwords[0] =
							(int)nextSequence;
						nextSequence =
							g_netSession
								.reliablePeerSlots
									[reliablePeerIndex]
								.recvSeqChannelB +
							1;
						if (nextSequence > 127) {
							nextSequence = 0;
						}
						g_netSessionScratchPacket
							.payloadDwords[1] =
							(int)nextSequence;
						nextSequence =
							g_netSession
								.reliablePeerSlots
									[reliablePeerIndex]
								.recvSeqDefault +
							1;
						if (nextSequence > 127) {
							nextSequence = 0;
						}
						g_netSessionScratchPacket
							.payloadDwords[2] =
							(int)nextSequence;
						g_netSessionScratchPacket
							.payloadDwords[3] =
							(int)timeGetTime();
						NetSession_SendCompactGamePacket(
							g_netSession
								.reliablePeerSlots
									[reliablePeerIndex]
								.directPlayId,
							(unsigned int
								 *)&g_netSessionScratchPacket,
							20, 0);
					}
				}
			}
			++player;
			++playerIndex;
		} while ((unsigned int)playerCount > playerIndex);
	}
	return 1;
}

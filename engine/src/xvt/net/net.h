#ifndef XVT_NET_NET_H
#define XVT_NET_NET_H

#include "aeron/compat/dplay.h"
#include "aeron/compat/win_types.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct NetPlayerInfo {
	char playerName[16];
	char sessionName[16];
	DPID playerId;
	int readyFlag;
};

#pragma pack(pop)
typedef char xvt_size_NetPlayerInfo[(sizeof(NetPlayerInfo) == 40) ? 1 : -1];

struct NetPlayerConnectionStats {
	int playerId;
	uint32_t latencyTotalMs;
	int packetCount;
	int packetDropCount;
	int packetRetryCount;
	uint32_t latencySampleCount;
};

typedef enum NetworkTransportType {
	NET_TRANSPORT_IPX = 0x0,
	NET_TRANSPORT_TCPIP = 0x1,
	NET_TRANSPORT_MODEM = 0x2,
	NET_TRANSPORT_SERIAL = 0x3,
} NetworkTransportType;

/* Game opcodes carried inside DirectPlay messages. System notifications use DPSYS_*.
 * Phase-specific names distinguish overlapping frontend and flight payloads. */
enum NetPacketType {
	NET_PACKET_NONE = 0, /* No application event; not transmitted. */
	/* Shared reliability and handshake. */
	NET_PACKET_WORLD_NACK = 51,
	NET_PACKET_PONG = 52,
	NET_PACKET_PING = 53,
	NET_PACKET_KEEPALIVE_ACK = 54,
	NET_PACKET_KEEPALIVE = 55,
	NET_PACKET_NACK = 56,
	NET_PACKET_NOP = 57,
	NET_PACKET_SEQUENCE_STATUS = 59,

	/* Flight. */
	NET_PACKET_REMOTE_INPUT = 1,
	NET_PACKET_WORLD_MESSAGE = 2,
	NET_PACKET_PLAYER_DISCONNECTED = 3,
	NET_PACKET_WORLD_CHECKSUM = 4,
	NET_PACKET_SESSION_ABORT = 8,
	NET_PACKET_STARTUP_READY = 13,
	NET_PACKET_ROSTER_COUNT = 14,
	NET_PACKET_ROSTER_ENTRY = 15,
	NET_PACKET_RESYNC_CHUNK_ACK = 16,
	NET_PACKET_PLAYER_OPTIONS = 17,
	NET_PACKET_PLAYER_OPTIONS_ROSTER = 18,
	NET_PACKET_FLIGHT_MISSION_START = 19,
	NET_PACKET_MISSION_LOADING_READY = 20,
	NET_PACKET_PLAYER_ABORT = 21,
	NET_PACKET_INPUT_BATCH = 22,
	NET_PACKET_RESYNC_NOTICE = 23,
	NET_PACKET_SERVER_CHECKSUM = 24,
	NET_PACKET_ACK = 27,
	NET_PACKET_CLOCK_LEAD = 28,
	NET_PACKET_STILL_LOADING = 29,
	NET_PACKET_CLOCK_PROBE = 30,
	NET_PACKET_CLOCK_PROBE_REPLY = 31,
	NET_PACKET_PLAYER_TAUNTS = 32,
	NET_PACKET_FLIGHT_SESSION_STATUS = 58,
	NET_PACKET_RESYNC_CHECKSUMS = 60,
	NET_PACKET_RESYNC_REQUEST = 61,
	NET_PACKET_RESYNC_APPLY = 62,
	NET_PACKET_RESYNC_CHUNK = 63,

	/* Frontend. */
	NET_PACKET_FRONTEND_GAME_STARTED = 58,
	NET_PACKET_PROBE_REQUEST = 64,
	NET_PACKET_STATE = 65,
	NET_PACKET_JOIN_REQUEST = 66,
	NET_PACKET_GAME_FULL = 68,
	NET_PACKET_PLAYER_ADMITTED = 69,
	NET_PACKET_HOST_CANCELLED = 70,
	NET_PACKET_PLAYER_LEFT = 71,
	NET_PACKET_TEAM_ASSIGNMENTS_READY = 72,
	NET_PACKET_FRONTEND_MISSION_START = 73,
	NET_PACKET_FRONTEND_OPCODE_74 = 74, /* Probed by the legacy dialog-dismiss path. */
	NET_PACKET_NEXT_TOURNAMENT_MISSION = 75,
	NET_PACKET_NEXT_BATTLE_MISSION = 76,
	NET_PACKET_CHAT = 77,
	NET_PACKET_TEAM_ASSIGNMENT = 79,
	NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY = 80,
	NET_PACKET_PLAYER_READY = 81,
	NET_PACKET_PLAYER_UNREADY = 82,
	NET_PACKET_RETURN_TO_SETUP = 83,
	NET_PACKET_CLEAR_TEAM_ASSIGNMENTS = 84,
	NET_PACKET_TEAM_ASSIGNMENTS = 85,
	NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS = 86,
	NET_PACKET_FLIGHT_ASSIGNMENTS = 87,
	NET_PACKET_MISSION_CHOICE = 88,
	NET_PACKET_GAME_OPTIONS = 89,
	NET_PACKET_REPLAY_MISSION = 90,
	NET_PACKET_PLAYER_KICKED = 91,
	NET_PACKET_SESSION_CANCELLED = 92,
	NET_PACKET_VERSION_MISMATCH = 94,
	NET_PACKET_PASSWORD_REQUIRED = 95,
	NET_PACKET_ROSTER_LOCKED = 96,
	NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS = 97,
	NET_PACKET_CRAFT_LOADOUT = 98,
	NET_PACKET_BRIEFING_ENTERED = 99,
	NET_PACKET_BRIEFING_COUNTDOWN = 100,
	NET_PACKET_ASSIGNMENT_COUNTDOWN = 101,
	NET_PACKET_RETURN_TO_MISSION_SELECTION = 102,
	NET_PACKET_RELEASE_TEAM_RESERVATION = 103,
	NET_PACKET_TEAM_RESERVATION = 104,
	NET_PACKET_PLAYER_UNAVAILABLE = 105,
	NET_PACKET_RELEASE_FLIGHT_RESERVATION = 106,
	NET_PACKET_FLIGHT_RESERVATION = 107,
	NET_PACKET_CHAT_SYNC_REQUEST = 108,
	NET_PACKET_CHAT_SYNC_CHUNK = 109,
	NET_PACKET_FLIGHT_ASSIGNMENT = 110,
	NET_PACKET_LOBBY_SELECTION = 111,
	NET_PACKET_FLIGHT_ASSIGNMENTS_READY = 112,
	NET_PACKET_LOADOUT_ROSTER = 113,
	NET_PACKET_PROBE_RESPONSE = 114,
	NET_PACKET_READY_ROSTER = 116,
	NET_PACKET_PILOT_RATING = 117,
	NET_PACKET_REPLAY_CURRENT_MISSION = 118,
	NET_PACKET_BATTLE_CONTINUATION = 119,
	NET_PACKET_BATTLE_PROGRESS = 120,
	NET_PACKET_NEXT_CAMPAIGN_MISSION = 121,
	NET_PACKET_REPLAY_CAMPAIGN_MISSION = 122,
	NET_PACKET_CAMPAIGN_CONTINUATION = 123,
	NET_PACKET_MOVIE_SYNC = 124,
	NET_PACKET_SUBMIT_MISSION_CHOICE = 125,
};

/* GUID layout with the final eight bytes grouped for session key arithmetic. */
typedef struct NetSessionGuid {
	unsigned int data1;
	unsigned short data2;
	unsigned short data3;
	unsigned int data4[2];
} NetSessionGuid;

typedef char xvt_size_NetSessionGuid[(sizeof(NetSessionGuid) == 16) ? 1 : -1];

struct NetSessionEnumEntry {
	char sessionName[32];
	NetSessionGuid sessionGuid;
};

extern NetPlayerConnectionStats g_netPlayerConnectionStats[40];
extern GUID g_netMatchedSessionInstanceGuid;
extern const GUID IID_IDirectPlay2A;
extern NetworkTransportType g_netActiveTransportType;
#ifndef XVT_MODERN
extern const GUID g_netLobbySessionInstanceGuid;
extern const GUID g_netDirectPlayComPortAddressTypeGuid;
extern const GUID g_netDirectPlayPhoneAddressTypeGuid;
extern const GUID g_netDirectPlayInetAddressTypeGuid;
#endif

void Net_ShutdownDirectPlaySessionForQuit(void);
void Net_ShutdownDirectPlaySession(void);
void Net_ShutdownDirectPlaySessionNoJoinAbort(void);
int Net_ShutdownDirectPlaySessionEx(int suppressRestart, int allowJoinAbortExit);
int Net_RefreshPlayerRoster(void);
int AERON_DXAPI Net_EnumPlayersCallback(DPID playerId, uint32_t playerType, const DPNAME* nameDesc,
										uint32_t flags, void* context);
int Net_CreateDirectPlayPlayer(const char* longPlayerInfo, const char* shortPlayerName);
const GUID* Net_GetDirectPlayServiceProviderGuid(NetworkTransportType networkType);
void Net_PumpIncomingPackets(void);
int Net_SendPacketAndFlush(int toPlayerId, const void* packet, unsigned int packetSize);
int Net_SendPacketInternal(int toPlayerId, const void* packet, unsigned int packetSize);
int Net_SendDirectPlayPacket(int destPlayerId, const void* packet, int packetSize, int unusedSendMode);
int Net_SendSequencedDirectPlayPacket(int destPlayerId, int sequenceMode, int sequenceId, const void* packet,
									  unsigned int packetSize);
NetPlayerInfo* Net_GetPlayerRoster(int* outCount);
int Net_GetPlayerCount(void);
int Net_DidReadyPlayerLeaveThisFrame(void);
int Net_IsHost(void);
int Net_HasQueuedPacketTypeOrBacklog(int packetType);
int Net_HasQueuedJoinRequestOrBacklog(void);
int* Net_GetNextAppPacket(DPID* outSenderId, uint32_t* outPacketSize);
void Net_HandleFrontendRosterPacket(int packetType, const void* packetData);
void* Net_DequeueIncomingPacket(DPID* outSenderId, uint32_t* outPacketSize);
int* Net_WaitForAppPacket(DPID* outSenderId, uint32_t* outPacketSize, int timeoutSeconds);
int sub_4D11A0(DPID playerId);
int Net_GetHostPlayerId(void);
int Net_GetLocalPlayerId(void);
void Net_MarkPlayerReadyNoLock(int playerId);
void Net_ClearPlayerReadyFlag(int playerId);
int Net_IsPlayerReady(int playerId);
NetPlayerInfo* Net_FindPlayer(int playerId);
int Net_SetPlayerReady(int playerId);
void Net_ClearPlayerReadyFlagWithLockGuard(int playerId);
int Net_CountReadyPlayers(void);
void Net_ClearPlayerReadyFlags(void);
int NetSession_ImportRuntimeState(void** dplayInterfaceOut, GUID* appGuidOut, GUID* sessionGuidOut,
								  int32_t* groupIdOut, int* hostPlayerIdOut, NetPlayerInfo* sessionNameOut,
								  NetQueuedPacket* directPlaySlotsOut, int32_t* recvQueueReadOut,
								  int* recvQueueCountOut, int* recvQueueWriteOut,
								  NetReliablePeerSlot* reliablePeerSlotsOut, uint32_t* netSlotCountOut,
								  uint32_t* smallStateOut, char* stateBytesAOut, int* stateDwordAOut,
								  int* stateDwordBOut, uint32_t* stateDwordCOut, char* stateBytesBOut,
								  int* stateDwordDOut, int* stateDwordEOut, NetQueuedPacket* recvHistoryOut,
								  int* recvHistoryCountOut);
int NetSession_ExportRuntimeState(void** dplayInterface, const void* appGuid, const void* sessionGuid,
								  int* groupId, int* hostPlayerId, const void* sessionName,
								  const NetQueuedPacket* directPlaySlots, int* recvQueueRead,
								  int* recvQueueCount, int* recvQueueWrite,
								  const NetReliablePeerSlot* reliablePeerSlots, int* netSlotCount,
								  int* smallState, const void* stateBytesA, int* stateDwordA,
								  int* stateDwordB, int* stateDwordC, const void* stateBytesB,
								  int* stateDwordD, int* stateDwordE, const NetQueuedPacket* recvHistory,
								  int* recvHistoryCount, NetQueuedPacket* recvQueue, int* recvQueueHighWater);
int NetSession_CompactReliablePeerSlotsForRoster(void);
int Net_SendSequenceKeepalives(void);
int Net_CheckAndRecordIncomingSequence(int playerId, int sequenceId, int useChannel0, int useChannel2);
int Net_FindQueuedSequencedPacket(int unusedQueueIndex, int sequenceId, int useChannel0, int useChannel2,
								  int sequenceEntryIndex);
int Net_RemoveIncomingPacketAtIndex(unsigned int queueIndex);
unsigned int Net_GetAverageLatencyMs(int playerId);
int Net_SetPlayerLatencyMs(int playerId, int latencyMs);
int Net_SetPlayerNameWithLockGuard(unsigned int playerId, const char* longName, const char* shortName);
int Net_ResetRosterToLocalPlayerWithLockGuard(void);
unsigned int Net_AddSequence(int directPlayId);
int Net_GetPacketDropRateBasisPoints(int playerId);
int Net_UpdateKeepaliveSequences(void);
int Net_GetPlayerPacketCount(int playerId);
int Net_GetPlayerPacketDropCount(int playerId);
int Net_GetPlayerPacketRetryCount(int playerId);
int Net_SetPlayerPacketCount(int playerId, int packetCount);
int Net_SetPlayerPacketDropCount(int playerId, int packetDropCount);
int Net_SetPlayerPacketRetryCount(int playerId, int packetRetryCount);
int Net_DisableAutoDialRegistrySetting(void);
int Net_RestoreAutoDialRegistrySetting(void);
int Net_WaitForShutdownHandshakeAcks(void);

#ifndef XVT_MODERN
int Net_StartNetworkSession(int appGuidData1, int appGuidData2, int appGuidData3, int appGuidData4,
							const char* localPlayerInfo, const char* localPlayerName, int isHost,
							const char* sessionName, NetworkTransportType networkType, int waitForPlayerCount,
							int unusedA11, const char* connectionAddress,
							const GUID* joinSessionInstanceGuid);
int Net_HostDirectPlaySession(const char* sessionName);
int Net_JoinDirectPlaySession(const char* sessionName, const GUID* sessionInstanceGuid);
const GUID* Net_FindSessionByName(const char* sessionName);
int AERON_DXAPI Net_EnumSessionsMatchNameCallback(const DPSESSIONDESC2* sessionDesc, uint32_t* timeoutMs,
												  uint32_t flags, void* context);
int Net_EnumerateAppSessions(unsigned int appGuid0, unsigned int appGuid1, unsigned int appGuid2,
							 unsigned int appGuid3, NetSessionEnumEntry* outSessions, int maxSessions,
							 NetworkTransportType networkType);
int AERON_DXAPI Net_EnumerateAppSessionsCallback(const DPSESSIONDESC2* sessionDesc, uint32_t* timeoutMs,
												 uint32_t flags, void* userData);
int Net_CompareSessionEnumEntriesByName(const NetSessionEnumEntry* lhs, const NetSessionEnumEntry* rhs);
int Net_OpenDirectPlaySession(GUID appGuid, const char* localPlayerInfo, const char* localPlayerName,
							  int isHost, const char* sessionName, NetworkTransportType networkType,
							  const char* connectionAddress);
HRESULT Net_BuildDirectPlayAddress(IDirectPlayLobbyA* directPlayLobby, const GUID* serviceProviderGuid,
								   const GUID* addressTypeGuid, const char* address,
								   void** outConnectionBuffer, size_t* outConnectionBufferSize);
#endif

#ifdef __cplusplus
}
#endif

#endif

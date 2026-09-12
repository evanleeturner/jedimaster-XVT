#ifndef XVT_NET_FLIGHT_NET_H
#define XVT_NET_FLIGHT_NET_H

#include "xvt/net/net.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flightNetNextClientInputSendTimestamp;
extern int g_flightNetSmallSessionPlayerThreshold;
extern int g_flightNetLastSentWorldMessageTimestamp;
extern int g_flightNetWorldChecksumResetAccumMs;
extern int g_flightNetSentWorldMessageCount;
extern int g_flightNetReceivedWorldMessageCount;
extern int dtMs;
extern int g_serverTickTime;
extern int g_flightNetClockAdjustAccumTicks;
extern int g_flightNetClockProbeTimestamp;
extern int g_flightNetHostTimeoutElapsedMs;

#pragma pack(push, 1)

struct FlightNetScratchPacket {
	int packetType;
	int payloadDwords[127];
};

#pragma pack(pop)
typedef char xvt_size_FlightNetScratchPacket[(sizeof(FlightNetScratchPacket) == 512) ? 1 : -1];

#pragma pack(push, 1)

struct FlightNetInputDeltaBatchPacket {
	int packetType;
	uint8_t frameCount;
	uint8_t streamBytes[507];
};

#pragma pack(pop)
typedef char
	xvt_size_FlightNetInputDeltaBatchPacket[(sizeof(FlightNetInputDeltaBatchPacket) == 512) ? 1 : -1];

#pragma pack(push, 1)

struct FlightNetWorldStateChunkPacket {
	int packetType;
	int baseChecksum;
	int chunkIndex;
	uint8_t payload[500];
};

#pragma pack(pop)
typedef char
	xvt_size_FlightNetWorldStateChunkPacket[(sizeof(FlightNetWorldStateChunkPacket) == 512) ? 1 : -1];

#pragma pack(push, 1)

typedef struct FlightNetWorldStateChunkRecordHeader {
	int worldOffset;
	int dataSize;
} FlightNetWorldStateChunkRecordHeader;

#pragma pack(pop)
typedef char xvt_size_FlightNetWorldStateChunkRecordHeader[(sizeof(FlightNetWorldStateChunkRecordHeader) == 8)
															   ? 1
															   : -1];

extern int g_playerAbortFlags[8];
extern int g_inputTimestamp;
extern int g_flightNetHostAbortReceived;
extern int g_flightNetWorldChecksumPeerStatus[8];
extern int g_playerConnected[8];
extern FlightInputFrameRecord g_currentInputFrame;
extern FlightNetScratchPacket g_flightNetScratchPacket;
extern int g_flightNetLastInputDeltaCodeByPlayer[8];
extern int g_flightNetPeerSilenceTicks[8];
extern int g_lastFrameTime;
extern int g_lastKeyframeTime;
extern int g_flightNetLastInputBatchSendTime;
extern FlightNetInputDeltaBatchPacket g_flightNetInputDeltaBatchPacket;
extern int g_flightNetInputDeltaBatchLen;
extern int g_flightNetInputBatchIntervalTicks;
extern int g_flightNetWorldStateAckReceivedFlag;
extern int g_flightNetWorldStateChunkAcked[16];
extern int g_flightNetLocalResyncChecksums[126];
extern int g_flightNetRemoteResyncChecksums[126];
extern FlightNetWorldStateChunkPacket g_flightNetWorldStateChunkPackets[16];
extern int g_flightNetRemoteResyncChecksumsReceivedFlag;
extern int g_flightNetResyncPlayerDplayId;
extern int g_flightNetRecoveryUiActive;
extern int g_flightNetPendingAckCount;

char* FlightNet_GetStatusPlayerName(void);
int FlightNet_SyncPlayerOptionsAndTaunts(void);
int FlightNet_WaitForMissionStart(void);
int FlightNet_SendClockProbeToHost(void);
int FlightNet_BroadcastStillLoadingPulse(void);
int FlightNet_BroadcastLocalPlayerLeft(void);
int FlightNet_SendStillLoadingPulse(void);
int FlightNet_BroadcastPlayerDisconnected(int playerSlot);
int FlightNet_BroadcastPlayerAbort(int playerSlot);
int FlightNet_FindPilotNetworkPlayerIndex(int playerIdx);
void FlightNet_MarkPilotNetworkPlayerLeft(int playerSlot);
void FlightNet_ProcessIncomingPackets(void);
int32_t FlightNet_SampleAndSendInput(void);
void FlightNet_InitMissionStartAckState(void);
int FlightNet_ShouldSendClientWorldMessage(int inputTimestamp);
void FlightNet_SendClientWorldMessage(int inputTimestamp);
int FlightNet_SendWorldChecksumToLocalPlayer(const int* worldChecksum, const int* peerChecksum,
											 int checksumDwordCount);
int FlightNet_BroadcastWorldChecksum(const int* worldChecksum, const int* peerChecksum,
									 int checksumDwordCount);
void FlightNet_SendWorldStateResyncApplyRequest(int directPlayId, int worldStateSize);
int FlightNet_SendWorldStateResyncToPlayer(int directPlayId, uint8_t* worldState, int worldStateSize);
int FlightNet_WaitForWorldStateChunkAcks(int directPlayId, int chunkCount);
void FlightNet_HandleWorldStateResyncPacket(const int* packet);

#ifdef __cplusplus
}
#endif

#endif

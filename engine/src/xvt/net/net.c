#include "xvt/net/net.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/network_session.h"
#endif
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt/util/win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XVT_MODERN
__declspec(dllimport) int __stdcall
RegOpenKeyExA(uintptr_t key, const char *subKey, unsigned int options,
	      unsigned int access, void **result);
__declspec(dllimport) int __stdcall
RegQueryValueExA(void *key, const char *valueName, unsigned int *reserved,
		 unsigned int *type, void *data, unsigned int *dataSize);
__declspec(dllimport) int __stdcall
RegSetValueExA(void *key, const char *valueName, unsigned int reserved,
	       unsigned int type, const void *data, unsigned int dataSize);
__declspec(dllimport) int __stdcall RegCloseKey(void *key);
__declspec(dllimport) void *__stdcall GlobalAlloc(unsigned int flags,
						  size_t size);
__declspec(dllimport) void *__stdcall GlobalLock(void *memoryHandle);
__declspec(dllimport) void *__stdcall GlobalHandle(const void *memory);
__declspec(dllimport) int __stdcall GlobalUnlock(void *memoryHandle);
__declspec(dllimport) void *__stdcall GlobalFree(void *memoryHandle);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int exitCode);
__declspec(dllimport) void *__stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall SetWindowTextA(void *window,
						   const char *text);
__declspec(dllimport) int __stdcall TerminateProcess(void *process,
						     unsigned int exitCode);
HRESULT __stdcall DirectPlayCreate(const GUID *serviceProviderGuid,
				   IDirectPlay **outDirectPlay,
				   void *outerUnknown);
HRESULT __stdcall DirectPlayLobbyCreateA(const GUID *lobbyProviderGuid,
					 IDirectPlayLobbyA **outLobby,
					 void *outerUnknown, void *data,
					 uint32_t dataSize);

enum {
	NET_REGISTRY_ALL_ACCESS = 0xF003F,
	NET_REGISTRY_BINARY = 3,
};

// GLOBAL: XVT 0x665018
static uint8_t g_netSavedEnableAutoDialValue[5] = {0};
// GLOBAL: XVT 0x665418
static int g_netAutoDialRegistryChanged = 0;
#endif

// GLOBAL: XVT 0x665020
NetworkTransportType g_netActiveTransportType = NET_TRANSPORT_IPX;

struct NetDirectPlayEncodedPacket {
	int16_t packetTypeHeader;
	int16_t payloadSize;
	uint8_t payload[1020];
};

#pragma pack(push, 1)

typedef struct NetDirectPlaySequencedPacket {
	int16_t packetTypeHeader;
	uint8_t packetClass;
	int16_t payloadSize;
	uint8_t payload[1019];
} NetDirectPlaySequencedPacket;

#pragma pack(pop)
typedef char xvt_size_NetDirectPlaySequencedPacket
	[(sizeof(NetDirectPlaySequencedPacket) == 1024) ? 1 : -1];

#ifndef XVT_MODERN
// GLOBAL: XVT 0x518290
const GUID g_netLobbySessionInstanceGuid = {
	0x09438C20,
	0xE01F,
	0x11CF,
	{0x86, 0x81, 0x11, 0xAA, 0x15, 0x3D, 0x4E, 0x58},
};
#endif
// GLOBAL: XVT 0x5182A0
const GUID g_netDirectPlayIpxServiceProviderGuid = {
	0x685BC400,
	0x9D2C,
	0x11CF,
	{0xA9, 0xCD, 0x00, 0xAA, 0x00, 0x68, 0x86, 0xE3},
};
// GLOBAL: XVT 0x5182B0
const GUID g_netDirectPlayModemServiceProviderGuid = {
	0x44EAA760,
	0xCB68,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
// GLOBAL: XVT 0x5182C0
const GUID g_netDirectPlayTcpIpServiceProviderGuid = {
	0x36E95EE0,
	0x8577,
	0x11CF,
	{0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82},
};
// GLOBAL: XVT 0x5182D0
const GUID g_netDirectPlaySerialServiceProviderGuid = {
	0x0F1D6860,
	0x88D9,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
// GLOBAL: XVT 0x518EF0
const GUID IID_IDirectPlay2A = {
	0x9D460580,
	0xA822,
	0x11CF,
	{0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82},
};
#ifndef XVT_MODERN
// GLOBAL: XVT 0x518F00
const GUID g_netDirectPlayComPortAddressTypeGuid = {
	0xF2F0CE00,
	0xE0AF,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
// GLOBAL: XVT 0x518F10
const GUID g_netDirectPlayPhoneAddressTypeGuid = {
	0x78EC89A0,
	0xE0AF,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
// GLOBAL: XVT 0x518F20
const GUID g_netDirectPlayInetAddressTypeGuid = {
	0xC4A54DA0,
	0xE0AF,
	0x11CF,
	{0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E},
};
#endif
// GLOBAL: XVT 0x665040
GUID g_netDirectPlayServiceProviderGuidScratch = {0};
// GLOBAL: XVT 0x665050
NetPlayerConnectionStats g_netPlayerConnectionStats[40];
// GLOBAL: XVT 0x665028
GUID g_netMatchedSessionInstanceGuid = {0};
// GLOBAL: XVT 0x665410
int g_netEnumSessionCount = 0;
// GLOBAL: XVT 0x665414
int g_netEnumSessionCapacity = 0;

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CCF90
int Net_StartNetworkSession(
	int appGuidData1, int appGuidData2, int appGuidData3, int appGuidData4,
	const char *localPlayerInfo, const char *localPlayerName, int isHost,
	const char *sessionName, NetworkTransportType networkType,
	int waitForPlayerCount, int unusedA11, const char *connectionAddress,
	const GUID *joinSessionInstanceGuid)
{
	enum {
		NET_RELIABLE_PEER_CAPACITY =
			sizeof(g_frontState.netRuntimeReliablePeerSlots) /
			sizeof(g_frontState.netRuntimeReliablePeerSlots[0]),
		NET_RELIABLE_SEQUENCE_INITIAL = 127,
		NET_JOIN_TIMEOUT_SECONDS = 5,
		NET_PLAYER_NAME_TERMINATOR_INDEX = 15,
		NET_SESSION_NAME_TERMINATOR_INDEX = 31,
		NET_JOIN_SEQUENCE_RECORD_SIZE = 8,
		NET_JOIN_SEQUENCE_RECORD_OFFSET = 16,
		NET_JOIN_RESPONSE_SIZE = 20,
	};

	DPID senderId;
	HRESULT result;
	int backBufferLocked;
	char gameSessionName[32];

	struct {
		int words[10];
		uint32_t receivedSize;
	} packetWorkspace;

	char dialNumber[32] = "Dial a New Number.";
	char directSerialName[32] = "Direct serial game.";
	DPCAPS directPlayCaps;
	NetReliablePeerSlot savedHostSlot;
	char errorMessage[256];
	int peerIndex;
	NetworkTransportType selectedNetworkType;
	const GUID *serviceProviderGuid;
	DPID directPlayPlayer;
	int *receivedPacket;
	const uint8_t *peerSequenceRecords;
	uint32_t peerSlotIndex;
	int sequenceRecordOffset;

	(void)unusedA11;

	backBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	Net_DisableAutoDialRegistrySetting();
	g_frontState.netRuntimeSentHistoryWriteIndex = 0;
	g_frontState.netRuntimeBroadcastSeqCounter = 0;
	g_frontState.netRuntimeBroadcastPendingPayload.piggybackEmpty = 1;
	g_frontState.netRuntimeBroadcastPendingPayload.payload[0] =
		NET_PACKET_NOP;
	g_frontState.netRuntimeBroadcastPendingPayload.payloadLength = 1;
	g_frontState.netRuntimeGroupSeqCounter = 0;
	g_frontState.netRuntimeGroupPendingPayload.piggybackEmpty = 1;
	g_frontState.netRuntimeGroupPendingPayload.payload[0] = NET_PACKET_NOP;
	g_frontState.netRuntimeGroupPendingPayload.payloadLength = 1;
	g_frontState.netReliablePeerSlotCount = 0;
	g_frontState.netReliableRetryLongTimeoutMode = 0;
	g_frontState.netGroupDplayId = 0;
	g_frontState.netHostPlayerId = 0;
	g_frontState.netExportRecvQueuePtr = NULL;
	g_frontState.netExportRecvQueueHighWater = 0;
	for (peerIndex = 0; peerIndex < NET_RELIABLE_PEER_CAPACITY;
	     ++peerIndex) {
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastDeliveredSeqDefault =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastDeliveredSeqChannelA =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastDeliveredSeqChannelB =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.recvSeqDefault = NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.recvSeqChannelA = NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.recvSeqChannelB = NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex].sendSeq = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.directPlayId = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastPiggybackType = NET_PACKET_NOP;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.piggybackLength = 1;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastActivityMs = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastHeardMs = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.packetCount = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.packetDropCount = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.packetRetryCount = 0;
	}
	memset(g_frontState.netRuntimeSentHistory, 0,
	       sizeof(g_frontState.netRuntimeSentHistory));
	memset(g_netPlayerConnectionStats, 0,
	       sizeof(g_netPlayerConnectionStats));
	g_frontState.directDraw->lpVtbl->FlipToGDISurface(
		g_frontState.directDraw);

	if (g_frontState.netDirectPlay != NULL) {
		if (backBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		Net_RestoreAutoDialRegistrySetting();
		return 0;
	}
	selectedNetworkType = networkType;
	switch (selectedNetworkType) {
	case NET_TRANSPORT_IPX:
		memcpy(&packetWorkspace.words[6],
		       &g_netDirectPlayIpxServiceProviderGuid, sizeof(GUID));
		break;
	case NET_TRANSPORT_TCPIP:
		memcpy(&packetWorkspace.words[6],
		       &g_netDirectPlayTcpIpServiceProviderGuid, sizeof(GUID));
		break;
	case NET_TRANSPORT_MODEM:
		memcpy(&packetWorkspace.words[6],
		       &g_netDirectPlayModemServiceProviderGuid, sizeof(GUID));
		break;
	case NET_TRANSPORT_SERIAL:
		memcpy(&packetWorkspace.words[6],
		       &g_netDirectPlaySerialServiceProviderGuid, sizeof(GUID));
		break;
	}

	if (selectedNetworkType != NET_TRANSPORT_MODEM &&
	    selectedNetworkType != NET_TRANSPORT_TCPIP) {
		serviceProviderGuid = Net_GetDirectPlayServiceProviderGuid(
			selectedNetworkType);
		if (serviceProviderGuid == NULL) {
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}
		if (DirectPlayCreate(serviceProviderGuid,
				     &g_frontState.netTempDirectPlay,
				     NULL) != 0) {
			if (ErrorText_LoadLine(6, errorMessage) == 0) {
				FrontendDisplay_ShowGameMessageBox(
					"WARNING:  Connection failure!\n\nMake sure your Windows 95 network\nsettings are "
					"properly configured\nfor this type of network game.\n\nPress Enter to continue.");
			} else {
				FrontendDisplay_ShowGameMessageBox(
					errorMessage);
			}
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}
		result = g_frontState.netTempDirectPlay->lpVtbl->QueryInterface(
			g_frontState.netTempDirectPlay, &IID_IDirectPlay2A,
			(void **)&g_frontState.netDirectPlay);
		g_frontState.netTempDirectPlay->lpVtbl->Release(
			g_frontState.netTempDirectPlay);
		g_frontState.netTempDirectPlay = NULL;
		if (result != 0) {
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}

		strncpy(g_frontState.netPlayers[0].playerInfo, localPlayerInfo,
			sizeof(g_frontState.netPlayers[0].playerInfo));
		g_frontState.netPlayers[0]
			.playerInfo[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		strncpy(g_frontState.netPlayers[0].playerName, localPlayerName,
			sizeof(g_frontState.netPlayers[0].playerName));
		g_frontState.netPlayers[0]
			.playerName[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		if (selectedNetworkType == NET_TRANSPORT_MODEM) {
			strcpy(gameSessionName, dialNumber);
		} else if (selectedNetworkType == NET_TRANSPORT_SERIAL) {
			strcpy(gameSessionName, directSerialName);
		} else if (sessionName[0] == '\0') {
			sprintf(gameSessionName, "%s's Game.", localPlayerName);
		} else {
			strcpy(gameSessionName, sessionName);
		}
		strncpy(g_frontState.netSessionName, gameSessionName,
			sizeof(g_frontState.netSessionName));
		g_frontState.netSessionName[NET_SESSION_NAME_TERMINATOR_INDEX] =
			'\0';
		g_frontState.netIsHost = isHost;
		g_frontState.netAppGuid = *(const GUID *)&appGuidData1;
		switch (isHost) {
		case 0:
			result = Net_JoinDirectPlaySession(
				gameSessionName, joinSessionInstanceGuid);
			break;
		case 1:
			result = Net_HostDirectPlaySession(gameSessionName);
			break;
		default:
			break;
		}
		if (result == 0) {
			g_frontState.netDirectPlay->lpVtbl->Release(
				g_frontState.netDirectPlay);
			g_frontState.netDirectPlay = NULL;
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}
	} else {
		serviceProviderGuid = Net_GetDirectPlayServiceProviderGuid(
			selectedNetworkType);
		if (serviceProviderGuid == NULL) {
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}
		if (DirectPlayCreate(serviceProviderGuid,
				     &g_frontState.netTempDirectPlay,
				     NULL) != 0) {
			if (ErrorText_LoadLine(6, errorMessage) == 0) {
				FrontendDisplay_ShowGameMessageBox(
					"WARNING:  Connection failure!\n\nMake sure your Windows 95 network\nsettings are "
					"properly configured\nfor this type of network game.\n\nPress Enter to continue.");
			} else {
				FrontendDisplay_ShowGameMessageBox(
					errorMessage);
			}
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}
		g_frontState.netTempDirectPlay->lpVtbl->Release(
			g_frontState.netTempDirectPlay);
		g_frontState.netTempDirectPlay = NULL;
		g_frontState.netIsHost = isHost;
		strncpy(g_frontState.netPlayers[0].playerInfo, localPlayerInfo,
			sizeof(g_frontState.netPlayers[0].playerInfo));
		g_frontState.netPlayers[0]
			.playerInfo[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		strncpy(g_frontState.netPlayers[0].playerName, localPlayerName,
			sizeof(g_frontState.netPlayers[0].playerName));
		g_frontState.netPlayers[0]
			.playerName[NET_PLAYER_NAME_TERMINATOR_INDEX] = '\0';
		if (Net_OpenDirectPlaySession(
			    *(const GUID *)&appGuidData1, localPlayerInfo,
			    localPlayerName, isHost, dialNumber,
			    selectedNetworkType, connectionAddress) == 0) {
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}
	}

	memset(&directPlayCaps, 0, sizeof(directPlayCaps));
	directPlayCaps.dwSize = sizeof(directPlayCaps);
	g_frontState.netDirectPlay->lpVtbl->GetCaps(g_frontState.netDirectPlay,
						    &directPlayCaps, 0);
	directPlayPlayer =
		Net_CreateDirectPlayPlayer(localPlayerInfo, localPlayerName);
	if (directPlayPlayer == 0) {
		g_frontState.netDirectPlay->lpVtbl->Close(
			g_frontState.netDirectPlay);
		g_frontState.netDirectPlay->lpVtbl->Release(
			g_frontState.netDirectPlay);
		g_frontState.netDirectPlay = NULL;
		if (g_frontState.netDirectPlayLobby != NULL) {
			g_frontState.netDirectPlayLobby->lpVtbl->Release(
				g_frontState.netDirectPlayLobby);
			g_frontState.netDirectPlayLobby = NULL;
		}
		if (backBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		Net_RestoreAutoDialRegistrySetting();
		return 0;
	}
	g_frontState.netPlayers[0].playerId = directPlayPlayer;
	g_frontState.netRuntimeLocalPlayer = g_frontState.netPlayers[0];
	if (isHost != 0) {
		if (g_frontState.netDirectPlay->lpVtbl->CreateGroup(
			    g_frontState.netDirectPlay,
			    &g_frontState.netGroupDplayId, NULL, NULL, 0,
			    0) != 0) {
			g_frontState.netDirectPlay->lpVtbl->DestroyPlayer(
				g_frontState.netDirectPlay,
				g_frontState.netRuntimeLocalPlayer.playerId);
			g_frontState.netDirectPlay->lpVtbl->Close(
				g_frontState.netDirectPlay);
			g_frontState.netDirectPlay->lpVtbl->Release(
				g_frontState.netDirectPlay);
			g_frontState.netDirectPlay = NULL;
			if (g_frontState.netDirectPlayLobby != NULL) {
				g_frontState.netDirectPlayLobby->lpVtbl
					->Release(g_frontState
							  .netDirectPlayLobby);
				g_frontState.netDirectPlayLobby = NULL;
			}
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			Net_RestoreAutoDialRegistrySetting();
			return 0;
		}
		g_frontState.netRuntimeReliablePeerSlots[0].directPlayId =
			g_frontState.netGroupDplayId;
		g_frontState.netReliablePeerSlotCount = 1;
		sprintf(gameSessionName, "My id%u\n",
			g_frontState.netRuntimeLocalPlayer.playerId);
		sprintf(gameSessionName, "Group id%u\n",
			g_frontState.netGroupDplayId);
	}
	g_frontState.netPlayerCount = 1;
	Net_RefreshPlayerRoster();
	g_frontState.netRuntimeRecvQueueWriteIndex = 0;
	g_frontState.netRuntimeRecvQueueReadIndex = 0;
	g_frontState.netRuntimeRecvQueueCount = 0;

	if (waitForPlayerCount > 0) {
		while (Net_GetPlayerCount() < waitForPlayerCount) {
			Net_PumpIncomingPackets();
		}
	}
	if (waitForPlayerCount <= 0) {
		if (isHost != 0) {
			g_frontState.netHostPlayerId =
				g_frontState.netRuntimeLocalPlayer.playerId;
		} else {
			do {
				receivedPacket = Net_WaitForAppPacket(
					&senderId,
					&packetWorkspace.receivedSize,
					NET_JOIN_TIMEOUT_SECONDS);
				if (receivedPacket == NULL) {
					g_frontState.netDirectPlay->lpVtbl
						->DestroyPlayer(
							g_frontState
								.netDirectPlay,
							g_frontState
								.netRuntimeLocalPlayer
								.playerId);
					g_frontState.netDirectPlay->lpVtbl
						->Close(g_frontState
								.netDirectPlay);
					g_frontState.netDirectPlay->lpVtbl
						->Release(
							g_frontState
								.netDirectPlay);
					g_frontState.netDirectPlay = NULL;
					if (g_frontState.netDirectPlayLobby !=
					    NULL) {
						g_frontState.netDirectPlayLobby
							->lpVtbl->Release(
								g_frontState
									.netDirectPlayLobby);
						g_frontState
							.netDirectPlayLobby =
							NULL;
					}
					if (backBufferLocked != 0) {
						g_drawSurfacePtr =
							FrontendDisplay_LockBackBuffer();
					}
					Net_RestoreAutoDialRegistrySetting();
					return 0;
				}
			} while (receivedPacket[0] !=
				 NET_PACKET_SEQUENCE_STATUS);

			g_frontState.netHostPlayerId = senderId;
			memset(&savedHostSlot, 0, sizeof(savedHostSlot));
			for (peerSlotIndex = 0;
			     g_frontState.netReliablePeerSlotCount >
			     peerSlotIndex;
			     ++peerSlotIndex) {
				if (g_frontState
					    .netRuntimeReliablePeerSlots
						    [peerSlotIndex]
					    .directPlayId ==
				    g_frontState.netHostPlayerId) {
					memcpy(&savedHostSlot,
					       &g_frontState
							.netRuntimeReliablePeerSlots
								[peerSlotIndex],
					       sizeof(savedHostSlot));
					break;
				}
			}

			g_frontState.netReliablePeerSlotCount =
				(uint32_t)receivedPacket[2];
			/* From here result holds the host's time stamp in ms, echoed back below in the keepalive ack. */
			result = receivedPacket[3];
			peerSequenceRecords = (const uint8_t *)receivedPacket +
					      NET_JOIN_SEQUENCE_RECORD_OFFSET;
			for (peerSlotIndex = 0;
			     g_frontState.netReliablePeerSlotCount >
			     peerSlotIndex;
			     ++peerSlotIndex) {
				sequenceRecordOffset =
					peerSlotIndex *
					NET_JOIN_SEQUENCE_RECORD_SIZE;
				memcpy(&g_frontState
						.netRuntimeReliablePeerSlots
							[peerSlotIndex]
						.directPlayId,
				       &peerSequenceRecords
					       [sequenceRecordOffset],
				       sizeof(g_frontState
						      .netRuntimeReliablePeerSlots
							      [peerSlotIndex]
						      .directPlayId));
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.lastDeliveredSeqChannelA =
					peerSequenceRecords
						[sequenceRecordOffset + 4];
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.lastDeliveredSeqChannelB =
					peerSequenceRecords
						[sequenceRecordOffset + 5];
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.recvSeqChannelA = peerSequenceRecords
					[sequenceRecordOffset + 6];
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.recvSeqChannelB = peerSequenceRecords
					[sequenceRecordOffset + 7];
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.lastDeliveredSeqDefault =
					NET_RELIABLE_SEQUENCE_INITIAL;
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.recvSeqDefault =
					NET_RELIABLE_SEQUENCE_INITIAL;
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.sendSeq = 0;
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.lastPiggybackType = NET_PACKET_NOP;
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.piggybackLength = 1;
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.lastActivityMs = GetTickCount();
				g_frontState
					.netRuntimeReliablePeerSlots
						[peerSlotIndex]
					.lastHeardMs = GetTickCount();
			}

			if (savedHostSlot.directPlayId != 0) {
				for (peerSlotIndex = 0;
				     g_frontState.netReliablePeerSlotCount >
				     peerSlotIndex;
				     ++peerSlotIndex) {
					if (g_frontState
						    .netRuntimeReliablePeerSlots
							    [peerSlotIndex]
						    .directPlayId ==
					    g_frontState.netHostPlayerId) {
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerSlotIndex]
							.lastDeliveredSeqDefault =
							savedHostSlot
								.lastDeliveredSeqDefault;
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerSlotIndex]
							.recvSeqDefault =
							savedHostSlot
								.recvSeqDefault;
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerSlotIndex]
							.sendSeq =
							savedHostSlot.sendSeq;
						memcpy(&g_frontState
								.netRuntimeReliablePeerSlots
									[peerSlotIndex]
								.lastPiggybackType,
						       &savedHostSlot
								.lastPiggybackType,
						       savedHostSlot
							       .piggybackLength);
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerSlotIndex]
							.piggybackLength =
							savedHostSlot
								.piggybackLength;
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerSlotIndex]
							.lastActivityMs =
							savedHostSlot
								.lastActivityMs;
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerSlotIndex]
							.lastHeardMs =
							savedHostSlot
								.lastHeardMs;
					}
				}
			}
			packetWorkspace.words[0] = NET_PACKET_KEEPALIVE_ACK;
			memcpy(&packetWorkspace.words[1], &result,
			       sizeof(packetWorkspace.words[1]));
			memset(&packetWorkspace.words[2], 0,
			       NET_JOIN_RESPONSE_SIZE -
				       2 * sizeof(packetWorkspace.words[0]));
			Net_SendDirectPlayPacket(g_frontState.netHostPlayerId,
						 packetWorkspace.words,
						 NET_JOIN_RESPONSE_SIZE, 0);
		}
	}

	if (backBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	Net_RestoreAutoDialRegistrySetting();
	g_netActiveTransportType = selectedNetworkType;
	return 1;
}
#endif

// FUNCTION: XVT 0x4CD9C0
void Net_ShutdownDirectPlaySessionForQuit(void)
{
	Net_ShutdownDirectPlaySessionEx(1, 1);
}

// FUNCTION: XVT 0x4CD9D0
void Net_ShutdownDirectPlaySession(void)
{
	Net_ShutdownDirectPlaySessionEx(0, 1);
}

// FUNCTION: XVT 0x4CD9E0
void Net_ShutdownDirectPlaySessionNoHandshake(void)
{
	Net_ShutdownDirectPlaySessionEx(0, 0);
}

// FUNCTION: XVT 0x4CD9F0
int Net_ShutdownDirectPlaySessionEx(int suppressRestart,
				    int waitForHandshakeAcks)
{
	enum {
		NET_DESTROY_PLAYER_TIMEOUT_MS = 20000,
		NET_RELIABLE_PEER_CAPACITY =
			sizeof(g_frontState.netRuntimeReliablePeerSlots) /
			sizeof(g_frontState.netRuntimeReliablePeerSlots[0]),
		NET_RELIABLE_SEQUENCE_INITIAL = 127
	};

	int backBufferLocked;
	int peerIndex;
#ifndef XVT_MODERN
	uint32_t destroyPlayerStartTime;
	void *currentProcess;
#endif

	backBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
#ifdef XVT_MODERN
	XvtNetworkSession_OnClose();
#endif
	if (g_frontState.netDirectPlay != NULL) {
#ifndef XVT_MODERN
		if (g_netActiveTransportType == NET_TRANSPORT_TCPIP &&
		    waitForHandshakeAcks != 0) {
			if (Net_WaitForShutdownHandshakeAcks() == 0) {
				if (suppressRestart != 0) {
					Frontend_SavePersistentState();
					FrontendDisplay_Shutdown(0);
					CDAudio_CloseDevice();
					ExitProcess(0);
				} else {
					Frontend_SavePersistentState();
					FrontendDisplay_Shutdown(0);
					CDAudio_CloseDevice();
					if (g_frontState.hWnd != NULL) {
						SetWindowTextA(
							g_frontState.hWnd,
							"XvT - Exiting");
					}
					Win32_CreateProcessFromCommandLine(
						"z_xvt__.exe skipintro");
					ExitProcess(0);
				}
			}
		}
#else
		(void)suppressRestart;
		(void)waitForHandshakeAcks;
#endif
		g_netActiveTransportType = NET_TRANSPORT_IPX;
#ifndef XVT_MODERN
		destroyPlayerStartTime = GetTickCount();
#endif
		g_frontState.netDirectPlay->lpVtbl->DestroyPlayer(
			g_frontState.netDirectPlay,
			g_frontState.netRuntimeLocalPlayer.playerId);
#ifndef XVT_MODERN
		if (GetTickCount() - destroyPlayerStartTime >
		    NET_DESTROY_PLAYER_TIMEOUT_MS) {
			if (suppressRestart == 0) {
				FrontendDialog_ShowNetworkAbortError(
					FrontendString_Get(
						FRONTSTR_762_DIRECT_PLAY_ERROR_FAILED_TO_DISCONNECT),
					FrontendString_Get(
						FRONTSTR_763_ATTEMPTING_TO_EXIT_TO_WINDOWS),
					FrontendString_Get(
						FRONTSTR_764_YOUR_COMPUTER_MAY_STOP_RESPONDING),
					FrontendString_Get(
						FRONTSTR_006_EXIT_TO_WINDOWS),
					NULL);
			}
			FrontendSound_ShutdownDirectSound();
			CDAudio_CloseDevice();
			currentProcess = GetCurrentProcess();
			TerminateProcess(currentProcess, 0);
		}
#endif
		if (g_frontState.netGroupDplayId != 0) {
			g_frontState.netDirectPlay->lpVtbl->DestroyGroup(
				g_frontState.netDirectPlay,
				g_frontState.netGroupDplayId);
			g_frontState.netGroupDplayId = 0;
		}
		g_frontState.netDirectPlay->lpVtbl->Close(
			g_frontState.netDirectPlay);
		g_frontState.netDirectPlay->lpVtbl->Release(
			g_frontState.netDirectPlay);
		g_frontState.netDirectPlay = NULL;
	}

	g_frontState.netRuntimeRecvQueueWriteIndex = 0;
	g_frontState.netRuntimeRecvQueueReadIndex = 0;
	g_frontState.netRuntimeRecvQueueCount = 0;
	g_frontState.netRuntimeBroadcastSeqCounter = 0;
	g_frontState.netRuntimeBroadcastPendingPayload.piggybackEmpty = 1;
	g_frontState.netRuntimeGroupSeqCounter = 0;
	g_frontState.netRuntimeGroupPendingPayload.piggybackEmpty = 1;
	g_frontState.netReliablePeerSlotCount = 0;
	g_frontState.netReliableRetryLongTimeoutMode = 0;
	for (peerIndex = 0; peerIndex < NET_RELIABLE_PEER_CAPACITY;
	     ++peerIndex) {
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastDeliveredSeqDefault =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastDeliveredSeqChannelA =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastDeliveredSeqChannelB =
			NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.recvSeqDefault = NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.recvSeqChannelA = NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.recvSeqChannelB = NET_RELIABLE_SEQUENCE_INITIAL;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex].sendSeq = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.directPlayId = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastPiggybackType = NET_PACKET_NOP;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.piggybackLength = 1;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastActivityMs = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastHeardMs = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.packetCount = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.packetDropCount = 0;
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.packetRetryCount = 0;
	}
	if (backBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 1;
}

// FUNCTION: XVT 0x4CDC20
int Net_RefreshPlayerRoster(void)
{
	int wasBackBufferLocked;
	int playerIndex;
	int oldPlayerIndex;
	NetPlayerInfo oldPlayers[32];

	if (g_frontState.netDirectPlay == NULL) {
		return 0;
	}
	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	memcpy(oldPlayers, g_frontState.netPlayers, sizeof(oldPlayers));
	memset(&g_frontState.netPlayers[1], 0,
	       sizeof(g_frontState.netPlayers) -
		       sizeof(g_frontState.netPlayers[0]));
	g_frontState.netPlayerCount = 1;
	g_frontState.netDirectPlay->lpVtbl->EnumPlayers(
		g_frontState.netDirectPlay, NULL, Net_EnumPlayersCallback, NULL,
		0);
	for (playerIndex = 0; playerIndex < g_frontState.netPlayerCount;
	     ++playerIndex) {
		for (oldPlayerIndex = 0; oldPlayerIndex < 32;
		     ++oldPlayerIndex) {
			if (oldPlayers[oldPlayerIndex].playerId ==
			    g_frontState.netPlayers[playerIndex].playerId) {
				g_frontState.netPlayers[playerIndex].readyFlag =
					oldPlayers[oldPlayerIndex].readyFlag;
				break;
			}
		}
	}
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 1;
}

// FUNCTION: XVT 0x4CDCF0
int AERON_DXAPI Net_EnumPlayersCallback(DPID playerId, uint32_t playerType,
					const DPNAME *nameDesc, uint32_t flags,
					void *context)
{
	(void)flags;
	(void)context;

	if (g_frontState.netPlayerCount >= 32) {
		return 0;
	}
	if (playerType == 0) {
		return 1;
	}
	if (g_frontState.netPlayers[0].playerId == playerId) {
		return 1;
	}
	strncpy(g_frontState.netPlayers[g_frontState.netPlayerCount].playerInfo,
		nameDesc->lpszLongNameA,
		sizeof(g_frontState.netPlayers[g_frontState.netPlayerCount]
			       .playerInfo));
	strncpy(g_frontState.netPlayers[g_frontState.netPlayerCount].playerName,
		nameDesc->lpszShortNameA,
		sizeof(g_frontState.netPlayers[g_frontState.netPlayerCount]
			       .playerName));
	g_frontState.netPlayers[g_frontState.netPlayerCount].playerInfo[15] =
		'\0';
	g_frontState.netPlayers[g_frontState.netPlayerCount].playerName[15] =
		'\0';
	g_frontState.netPlayers[g_frontState.netPlayerCount].playerId =
		playerId;
	g_frontState.netPlayers[g_frontState.netPlayerCount++].readyFlag = 0;
	return 1;
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CDDC0
int Net_HostDirectPlaySession(const char *sessionName)
{
	DPSESSIONDESC2 sessionDesc;

	memset(&sessionDesc, 0, sizeof(sessionDesc));
	sessionDesc.dwSize = sizeof(sessionDesc);
	sessionDesc.dwFlags = 0x40;
	sessionDesc.guidApplication = g_frontState.netAppGuid;
	sessionDesc.dwMaxPlayers = 32;
	sessionDesc.lpszSessionNameA = (char *)sessionName;

	return g_frontState.netDirectPlay->lpVtbl->Open(
		       g_frontState.netDirectPlay, &sessionDesc, 2) == 0;
}
#endif

// FUNCTION: XVT 0x4CDE40
int Net_CreateDirectPlayPlayer(const char *longPlayerInfo,
			       const char *shortPlayerName)
{
#ifdef XVT_MODERN
	DPID player = 0;
	DPNAME name = {sizeof(name), 0, (char *)shortPlayerName,
		       (char *)longPlayerInfo};
	HRESULT result = g_frontState.netDirectPlay->lpVtbl->CreatePlayer(
		g_frontState.netDirectPlay, &player, &name, NULL, NULL, 0, 0);
	return result == DPERR_PENDING ? XVT_NETWORK_PENDING
	       : result == 0	       ? (int)player
				       : 0;
#else
	int attemptsRemaining;
	DPID playerId;
	DPNAME playerName;

	attemptsRemaining = 5;
	memset(&playerName, 0, sizeof(playerName));
	playerName.lpszShortNameA = (char *)shortPlayerName;
	playerName.lpszLongNameA = (char *)longPlayerInfo;
	playerName.dwSize = sizeof(playerName);
	do {
		if (g_frontState.netDirectPlay->lpVtbl->CreatePlayer(
			    g_frontState.netDirectPlay, &playerId, &playerName,
			    NULL, NULL, 0, 0) == 0) {
			break;
		}
		--attemptsRemaining;
	} while (attemptsRemaining != 0);
	if (attemptsRemaining == 0) {
		return 0;
	}
	return playerId;
#endif
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CDEB0
int Net_JoinDirectPlaySession(const char *sessionName,
			      const GUID *sessionInstanceGuid)
{
	const GUID *resolvedSessionGuid;
	DPSESSIONDESC2 sessionDesc;

	resolvedSessionGuid = sessionInstanceGuid;
	if (resolvedSessionGuid == 0) {
		resolvedSessionGuid = Net_FindSessionByName(sessionName);
		if (resolvedSessionGuid == 0) {
			return 0;
		}
	}

	memset(&sessionDesc, 0, sizeof(sessionDesc));
	sessionDesc.dwSize = sizeof(sessionDesc);
	sessionDesc.dwFlags = 0x40;
	sessionDesc.guidInstance = *resolvedSessionGuid;
	if (g_frontState.netDirectPlay->lpVtbl->Open(g_frontState.netDirectPlay,
						     &sessionDesc, 1) != 0) {
		return 0;
	}

	g_frontState.netJoinedSessionGuid = *resolvedSessionGuid;
	return 1;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CDF60
const GUID *Net_FindSessionByName(const char *sessionName)
{
	DPSESSIONDESC2 sessionDesc;

	memset(&sessionDesc, 0, sizeof(sessionDesc));
	sessionDesc.dwSize = sizeof(sessionDesc);
	sessionDesc.guidApplication = g_frontState.netAppGuid;

	return g_frontState.netDirectPlay->lpVtbl->EnumSessions(
		       g_frontState.netDirectPlay, &sessionDesc, 0,
		       Net_EnumSessionsMatchNameCallback, (void *)sessionName,
		       1) == 0
		       ? &g_netMatchedSessionInstanceGuid
		       : 0;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CDFD0
int AERON_DXAPI Net_EnumSessionsMatchNameCallback(
	const DPSESSIONDESC2 *sessionDesc, uint32_t *timeoutMs, uint32_t flags,
	void *context)
{
	(void)timeoutMs;

	if (sessionDesc == 0 || (flags & 1) != 0) {
		return 0;
	}
	if (strcmp(sessionDesc->lpszSessionNameA, (const char *)context) == 0) {
		g_netMatchedSessionInstanceGuid = sessionDesc->guidInstance;
		return 0;
	}

	return 1;
}
#endif

// FUNCTION: XVT 0x4CE050
const GUID *
Net_GetDirectPlayServiceProviderGuid(NetworkTransportType networkType)
{
	const GUID *result;

	switch (networkType) {
	case NET_TRANSPORT_IPX:
		g_netDirectPlayServiceProviderGuidScratch =
			g_netDirectPlayIpxServiceProviderGuid;
		result = &g_netDirectPlayServiceProviderGuidScratch;
		break;
	case NET_TRANSPORT_TCPIP:
		g_netDirectPlayServiceProviderGuidScratch =
			g_netDirectPlayTcpIpServiceProviderGuid;
		result = &g_netDirectPlayServiceProviderGuidScratch;
		break;
	case NET_TRANSPORT_MODEM:
		g_netDirectPlayServiceProviderGuidScratch =
			g_netDirectPlayModemServiceProviderGuid;
		result = &g_netDirectPlayServiceProviderGuidScratch;
		break;
	case NET_TRANSPORT_SERIAL:
		g_netDirectPlayServiceProviderGuidScratch =
			g_netDirectPlaySerialServiceProviderGuid;
		result = &g_netDirectPlayServiceProviderGuidScratch;
		break;
	default:
		result = NULL;
		break;
	}
	return result;
}

// FUNCTION: XVT 0x4CE130
void Net_PumpIncomingPackets(void)
{
	enum {
		QUEUE_CAPACITY = 1024,
		QUEUE_LIMIT = QUEUE_CAPACITY - 1,
		SYSTEM_PACKET_SIZE_LIMIT = 512,
		HISTORY_CAPACITY = 128,
		EXPORT_QUEUE_CAPACITY = 256,
		PEER_CAPACITY = 40,
		MAX_PAYLOAD_SIZE = 508,
		SEQUENCE_LIMIT = 127,
		SEQUENCE_NOTHING_TO_RESEND = 128,
		LATENCY_SEND_BIAS_MS = 40,
		LATENCY_MAX_MS = 750,
		LATENCY_OUTLIER_PERCENT = 50,
		PACKET_DROP_WARMUP_COUNT = 20,
	};

	struct {
		uint16_t header;
		uint8_t data[1022];
	} wirePacket;

	DPID fromId;
	DPID toId;
	uint32_t wireSize;
	int backBufferLocked;
	int packetWords[6];

	backBufferLocked = g_frontState.backBufferLocked;

	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.netDirectPlay != NULL) {
		Net_SendSequenceKeepalives();
		Net_DropSilentPeers();
		for (;;) {
			int *payload;
			uint32_t payloadSize;
			unsigned int packetType;
			int broadcastChannel;
			int groupChannel;
			int sequence;
			int hasLength;
			unsigned int peerIndex;

			if (g_frontState.netRuntimeRecvQueueCount >=
			    QUEUE_LIMIT) {
				if (backBufferLocked != 0) {
					g_drawSurfacePtr =
						FrontendDisplay_LockBackBuffer();
				}
				return;
			}
			wireSize = sizeof(wirePacket);
			if (g_frontState.netDirectPlay->lpVtbl->Receive(
				    g_frontState.netDirectPlay, &fromId, &toId,
				    1, &wirePacket, &wireSize) != 0) {
				if (backBufferLocked != 0) {
					g_drawSurfacePtr =
						FrontendDisplay_LockBackBuffer();
				}
				return;
			}
			if (fromId == 0) {
				if (wireSize > SYSTEM_PACKET_SIZE_LIMIT) {
					wireSize = SYSTEM_PACKET_SIZE_LIMIT;
				}
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.directPlayId = fromId;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.payloadSize = wireSize;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.isResentCopy = 0;
				memcpy(g_frontState
					       .netRuntimeRecvQueue
						       [g_frontState
								.netRuntimeRecvQueueWriteIndex]
					       .payload,
				       &wirePacket.header, wireSize);
				++g_frontState.netRuntimeRecvQueueWriteIndex;
				++g_frontState.netRuntimeRecvQueueCount;
				if (g_frontState
					    .netRuntimeRecvQueueWriteIndex >=
				    QUEUE_CAPACITY) {
					g_frontState
						.netRuntimeRecvQueueWriteIndex =
						0;
				}
				continue;
			}
			if (toId !=
			    g_frontState.netRuntimeLocalPlayer.playerId) {
				continue;
			}

			payload = (int *)wirePacket.data;
			packetType = wirePacket.header & 0x7F;
			groupChannel = (wirePacket.header & 0x80) != 0;
			broadcastChannel = (wirePacket.header & 0x8000) == 0;
			sequence = (wirePacket.header >> 8) & 0x7F;
			/* Outside the resync types (60-63) every packet carries a piggyback trailer after its payload,
			 * and a type with no fixed size also starts with a length word; hasLength stands for both. */
			hasLength = packetType < NET_PACKET_RESYNC_CHECKSUMS ||
				    packetType >= NET_PACKET_RESYNC_CHUNK + 1;
			if (hasLength) {
				payloadSize = (uint32_t)
					NetSession_GetFixedPayloadSize(
						(int)packetType);
				if (payloadSize == 0) {
					payloadSize = *(const uint16_t *)
							       wirePacket.data;
					payload = (int *)(wirePacket.data +
							  sizeof(uint16_t));
				}
			}
			if (packetType == NET_PACKET_PING) {
				packetWords[0] = NET_PACKET_PONG;
				Net_SendPacketAndFlush((int)fromId, packetWords,
						       sizeof(packetWords[0]));
				continue;
			}

			if (packetType == NET_PACKET_KEEPALIVE_ACK) {
				uint32_t latencyMs;
				uint32_t echoedSendMs;
				uint32_t averageLatencyMs;
				unsigned int statsIndex;

				peerIndex =
					Net_FindOrCreatePeerSlot((int)fromId);
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.lastHeardMs = GetTickCount();
				echoedSendMs = (uint32_t)payload[0];
				/* Until the average is computed below, averageLatencyMs holds the current time less
				 * LATENCY_SEND_BIAS_MS, the clock the echoed send time is measured against. */
				averageLatencyMs =
					GetTickCount() - LATENCY_SEND_BIAS_MS;
				if (echoedSendMs >= averageLatencyMs) {
					latencyMs = 1;
				} else {
					latencyMs =
						averageLatencyMs - echoedSendMs;
				}

				statsIndex = 0;
				while (statsIndex < PEER_CAPACITY &&
				       g_netPlayerConnectionStats[statsIndex]
						       .playerId !=
					       (int)fromId) {
					++statsIndex;
				}
				if (statsIndex < PEER_CAPACITY) {
					if (latencyMs < LATENCY_MAX_MS) {
						if (g_netPlayerConnectionStats
							    [statsIndex]
								    .latencySampleCount !=
						    0) {
							uint32_t latencyTotalMs =
								g_netPlayerConnectionStats
									[statsIndex]
										.latencyTotalMs;
							averageLatencyMs =
								latencyTotalMs /
								g_netPlayerConnectionStats
									[statsIndex]
										.latencySampleCount;
							if (latencyMs >
							    averageLatencyMs) {
								if (100 *
									    (latencyMs -
									     averageLatencyMs) /
									    averageLatencyMs >
								    LATENCY_OUTLIER_PERCENT) {
									g_netPlayerConnectionStats
										[statsIndex]
											.packetCount = payload
										[1];
									g_netPlayerConnectionStats
										[statsIndex]
											.packetDropCount = payload
										[2];
									g_netPlayerConnectionStats
										[statsIndex]
											.packetRetryCount =
										payload[3];
								} else {
									g_netPlayerConnectionStats
										[statsIndex]
											.latencyTotalMs =
										latencyMs +
										latencyTotalMs;
									g_netPlayerConnectionStats
										[statsIndex]
											.packetCount = payload
										[1];
									g_netPlayerConnectionStats
										[statsIndex]
											.packetDropCount = payload
										[2];
									g_netPlayerConnectionStats
										[statsIndex]
											.packetRetryCount =
										payload[3];
									++g_netPlayerConnectionStats
										  [statsIndex]
											  .latencySampleCount;
								}
							} else {
								g_netPlayerConnectionStats
									[statsIndex]
										.latencyTotalMs =
									latencyMs +
									latencyTotalMs;
								g_netPlayerConnectionStats
									[statsIndex]
										.packetCount =
									payload[1];
								g_netPlayerConnectionStats
									[statsIndex]
										.packetDropCount =
									payload[2];
								g_netPlayerConnectionStats
									[statsIndex]
										.packetRetryCount =
									payload[3];
								++g_netPlayerConnectionStats
									  [statsIndex]
										  .latencySampleCount;
							}
						}
					} else {
						g_netPlayerConnectionStats
							[statsIndex]
								.packetCount =
							payload[1];
						g_netPlayerConnectionStats
							[statsIndex]
								.packetDropCount =
							payload[2];
						g_netPlayerConnectionStats
							[statsIndex]
								.packetRetryCount =
							payload[3];
					}
				}

				if (statsIndex >= PEER_CAPACITY) {
					statsIndex = 0;
					while (statsIndex < PEER_CAPACITY &&
					       g_netPlayerConnectionStats
							       [statsIndex]
								       .playerId !=
						       0) {
						++statsIndex;
					}
					if (statsIndex < PEER_CAPACITY) {
						if (latencyMs >
						    LATENCY_MAX_MS) {
							latencyMs =
								LATENCY_MAX_MS;
						}
						g_netPlayerConnectionStats
							[statsIndex]
								.playerId =
							(int)fromId;
						g_netPlayerConnectionStats
							[statsIndex]
								.latencyTotalMs =
							latencyMs;
						g_netPlayerConnectionStats
							[statsIndex]
								.packetCount =
							payload[1];
						g_netPlayerConnectionStats
							[statsIndex]
								.packetDropCount =
							payload[2];
						g_netPlayerConnectionStats
							[statsIndex]
								.packetRetryCount =
							payload[3];
						g_netPlayerConnectionStats
							[statsIndex]
								.latencySampleCount =
							1;
					}
				}
				continue;
			}

			if (packetType == NET_PACKET_WORLD_NACK) {
				NetQueuedPacket *queued;
				int missingWorldTick;
				int controlValue1;
				int searchIndex;
				unsigned int searchCount;

				if (g_frontState.netExportRecvQueuePtr !=
				    NULL) {
					missingWorldTick = payload[0];
					controlValue1 = payload[1];
					peerIndex = Net_FindOrCreatePeerSlot(
						(int)fromId);
					if ((unsigned int)g_frontState
						    .netRuntimeReliablePeerSlots
							    [peerIndex]
						    .packetCount >
					    PACKET_DROP_WARMUP_COUNT) {
						++g_frontState
							  .netRuntimeReliablePeerSlots
								  [peerIndex]
							  .packetDropCount;
					}

					searchIndex =
						g_frontState
							.netExportRecvQueueHighWater;
					searchCount = 0;
					while (searchCount <
					       EXPORT_QUEUE_CAPACITY) {
						queued =
							&g_frontState.netExportRecvQueuePtr
								 [searchIndex];
						if (queued->payloadSize != 0) {
							if ((*(const uint32_t
								       *)&queued->payload
								      [sizeof(int)] &
							     0x7FFFFFFF) ==
							    (uint32_t)
								    missingWorldTick) {
								break;
							}
						}
						++searchCount;
						++searchIndex;
						if ((unsigned int)searchIndex >=
						    EXPORT_QUEUE_CAPACITY) {
							searchIndex = 0;
						}
					}
					if (searchCount <
					    EXPORT_QUEUE_CAPACITY) {
						queued =
							&g_frontState.netExportRecvQueuePtr
								 [searchIndex];
						Net_SendSequencedDirectPlayPacket(
							(int)fromId, 0,
							queued->sequenceByte,
							queued->payload,
							queued->payloadSize);
					}
					if (searchCount >=
					    EXPORT_QUEUE_CAPACITY) {
						packetWords[0] = NET_PACKET_NOP;
						Net_SendSequencedDirectPlayPacket(
							(int)fromId, 0,
							controlValue1,
							packetWords,
							sizeof(packetWords[0]));
					}
				}
				continue;
			}

			if (packetType == NET_PACKET_NACK) {
				int expectedBroadcastSequence;
				int controlValue1;
				int searchIndex;
				unsigned int searchCount;

				expectedBroadcastSequence = payload[0];
				controlValue1 = payload[1];
				peerIndex =
					Net_FindOrCreatePeerSlot((int)fromId);
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.lastHeardMs = GetTickCount();
				if ((unsigned int)g_frontState
					    .netRuntimeReliablePeerSlots
						    [peerIndex]
					    .packetCount >
				    PACKET_DROP_WARMUP_COUNT) {
					++g_frontState
						  .netRuntimeReliablePeerSlots
							  [peerIndex]
						  .packetDropCount;
				}

				searchIndex =
					g_frontState
						.netRuntimeSentHistoryWriteIndex;
				searchCount = 0;
				while (searchCount < HISTORY_CAPACITY) {
					if (g_frontState
						    .netRuntimeSentHistory
							    [searchIndex]
						    .payloadSize != 0) {
						if (controlValue1 == 0 ||
						    controlValue1 == 2) {
							if (g_frontState.netRuntimeSentHistory
									    [searchIndex]
										    .sequenceByte ==
								    expectedBroadcastSequence &&
							    g_frontState.netRuntimeSentHistory
									    [searchIndex]
										    .packetClass ==
								    controlValue1) {
								break;
							}
						} else if (
							g_frontState.netRuntimeSentHistory
									[searchIndex]
										.directPlayId ==
								fromId &&
							g_frontState.netRuntimeSentHistory
									[searchIndex]
										.sequenceByte ==
								expectedBroadcastSequence &&
							g_frontState.netRuntimeSentHistory
									[searchIndex]
										.packetClass ==
								controlValue1) {
							break;
						}
					}
					++searchCount;
					++searchIndex;
					if ((unsigned int)searchIndex >=
					    HISTORY_CAPACITY) {
						searchIndex = 0;
					}
				}
				if (searchCount < HISTORY_CAPACITY) {
					Net_SendSequencedDirectPlayPacket(
						(int)fromId, controlValue1,
						expectedBroadcastSequence,
						g_frontState
							.netRuntimeSentHistory
								[searchIndex]
							.payload,
						g_frontState
							.netRuntimeSentHistory
								[searchIndex]
							.payloadSize);
				}
				if (searchCount >= HISTORY_CAPACITY) {
					packetWords[0] = NET_PACKET_NOP;
					Net_SendSequencedDirectPlayPacket(
						(int)fromId, controlValue1,
						expectedBroadcastSequence,
						packetWords,
						sizeof(packetWords[0]));
				}
				continue;
			}

			if (packetType == NET_PACKET_KEEPALIVE) {
				int controlValue0;
				int expectedGroupSequence;
				int expectedDirectSequence;
				int searchIndex;
				unsigned int searchCount;

				controlValue0 = payload[0];
				expectedGroupSequence = payload[1];
				expectedDirectSequence = payload[2];
				if (g_frontState.netHostPlayerId == fromId) {
					peerIndex = Net_FindOrCreatePeerSlot(
						(int)fromId);
					g_frontState
						.netRuntimeReliablePeerSlots
							[peerIndex]
						.lastHeardMs = GetTickCount();
					packetWords[0] =
						NET_PACKET_KEEPALIVE_ACK;
					packetWords[1] = payload[3];
					packetWords[2] =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.packetCount;
					packetWords[3] =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.packetDropCount;
					packetWords[4] =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.packetRetryCount;
					Net_SendDirectPlayPacket(
						(int)g_frontState
							.netHostPlayerId,
						packetWords,
						5 * sizeof(packetWords[0]), 0);
				}
				if (g_frontState
					    .netRuntimeBroadcastSeqCounter ==
				    controlValue0) {
					controlValue0 =
						SEQUENCE_NOTHING_TO_RESEND;
				}
				if (g_frontState.netRuntimeGroupSeqCounter ==
				    expectedGroupSequence) {
					expectedGroupSequence =
						SEQUENCE_NOTHING_TO_RESEND;
				}
				peerIndex =
					Net_FindOrCreatePeerSlot((int)fromId);
				if (g_frontState.netReliablePeerSlotCount ==
					    peerIndex ||
				    g_frontState.netRuntimeReliablePeerSlots
						    [peerIndex]
							    .sendSeq ==
					    expectedDirectSequence) {
					expectedDirectSequence =
						SEQUENCE_NOTHING_TO_RESEND;
				}

				searchCount = HISTORY_CAPACITY;
				searchIndex =
					g_frontState
						.netRuntimeSentHistoryWriteIndex;
				do {
					if (controlValue0 ==
						    SEQUENCE_NOTHING_TO_RESEND &&
					    expectedGroupSequence ==
						    SEQUENCE_NOTHING_TO_RESEND &&
					    expectedDirectSequence ==
						    SEQUENCE_NOTHING_TO_RESEND) {
						break;
					}
					if (g_frontState
						    .netRuntimeSentHistory
							    [searchIndex]
						    .payloadSize != 0) {
						uint8_t packetClass =
							g_frontState
								.netRuntimeSentHistory
									[searchIndex]
								.packetClass;
						if (packetClass == 0) {
							if (g_frontState
								    .netRuntimeSentHistory
									    [searchIndex]
								    .sequenceByte ==
							    controlValue0) {
								Net_SendSequencedDirectPlayPacket(
									(int)fromId,
									0,
									controlValue0,
									g_frontState
										.netRuntimeSentHistory
											[searchIndex]
										.payload,
									g_frontState
										.netRuntimeSentHistory
											[searchIndex]
										.payloadSize);
								controlValue0 =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						} else if (packetClass == 2) {
							if (g_frontState
								    .netRuntimeSentHistory
									    [searchIndex]
								    .sequenceByte ==
							    expectedGroupSequence) {
								Net_SendSequencedDirectPlayPacket(
									(int)fromId,
									2,
									expectedGroupSequence,
									g_frontState
										.netRuntimeSentHistory
											[searchIndex]
										.payload,
									g_frontState
										.netRuntimeSentHistory
											[searchIndex]
										.payloadSize);
								expectedGroupSequence =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						} else {
							if (g_frontState.netRuntimeSentHistory
									    [searchIndex]
										    .directPlayId ==
								    fromId &&
							    g_frontState.netRuntimeSentHistory
									    [searchIndex]
										    .sequenceByte ==
								    expectedDirectSequence) {
								Net_SendSequencedDirectPlayPacket(
									(int)fromId,
									1,
									expectedDirectSequence,
									g_frontState
										.netRuntimeSentHistory
											[searchIndex]
										.payload,
									g_frontState
										.netRuntimeSentHistory
											[searchIndex]
										.payloadSize);
								expectedDirectSequence =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						}
					}
					if ((unsigned int)++searchIndex >=
					    HISTORY_CAPACITY) {
						searchIndex = 0;
					}
				} while (--searchCount != 0);
				/* Keepalives are unsequenced control packets. */
				continue;
			}

			peerIndex = Net_FindOrCreatePeerSlot((int)fromId);
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.lastHeardMs = GetTickCount();

			if (broadcastChannel != 0 && groupChannel != 0) {
				uint8_t *retransmissionPayload;
				if (wirePacket.data[0] != 0) {
					groupChannel = wirePacket.data[0] == 2;
					broadcastChannel = 0;
				} else {
					groupChannel = 0;
					broadcastChannel = 1;
				}
				retransmissionPayload = wirePacket.data + 1;
				if (hasLength) {
					payloadSize = (uint32_t)
						NetSession_GetFixedPayloadSize(
							(int)packetType);
					if (payloadSize == 0) {
#ifdef XVT_MODERN
						uint16_t encodedSize;
						memcpy(&encodedSize,
						       wirePacket.data + 1,
						       sizeof(encodedSize));
						payloadSize = encodedSize;
#else
						payloadSize = *(
							const uint16_t
								*)(wirePacket
									   .data +
								   1);
#endif
						retransmissionPayload =
							wirePacket.data + 1 +
							sizeof(uint16_t);
					}
				} else {
					payloadSize = wireSize -
						      sizeof(wirePacket.header);
				}
				if (payloadSize > MAX_PAYLOAD_SIZE) {
					payloadSize = MAX_PAYLOAD_SIZE;
				}

				*(uint32_t *)g_frontState
					 .netRuntimeRecvQueue
						 [g_frontState
							  .netRuntimeRecvQueueWriteIndex]
					 .payload = packetType;
				memcpy(g_frontState.netRuntimeRecvQueue
						       [g_frontState
								.netRuntimeRecvQueueWriteIndex]
							       .payload +
					       sizeof(packetType),
				       retransmissionPayload, payloadSize);
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.directPlayId = fromId;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.payloadSize =
					payloadSize + sizeof(packetType);
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.lastNackMs = 0;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.nackRetryCount = 0;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.isResentCopy = 1;
				peerIndex =
					Net_FindOrCreatePeerSlot((int)fromId);
				if (broadcastChannel != 0) {
					g_frontState
						.netRuntimeRecvQueue
							[g_frontState
								 .netRuntimeRecvQueueWriteIndex]
						.packetClass = 0;
					if (g_frontState.netReliablePeerSlotCount >
						    peerIndex &&
					    peerIndex < PEER_CAPACITY) {
						unsigned int nextSequence =
							(unsigned int)g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqChannelA +
							1;
						if (nextSequence >
						    SEQUENCE_LIMIT) {
							nextSequence = 0;
						}
						if (nextSequence ==
						    (unsigned int)sequence) {
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqChannelA =
								(int)nextSequence;
						}
					}
				} else if (groupChannel != 0) {
					g_frontState
						.netRuntimeRecvQueue
							[g_frontState
								 .netRuntimeRecvQueueWriteIndex]
						.packetClass = 2;
					if (g_frontState.netReliablePeerSlotCount >
						    peerIndex &&
					    peerIndex < PEER_CAPACITY) {
						unsigned int nextSequence =
							(unsigned int)g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqChannelB +
							1;
						if (nextSequence >
						    SEQUENCE_LIMIT) {
							nextSequence = 0;
						}
						if (nextSequence ==
						    (unsigned int)sequence) {
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqChannelB =
								(int)nextSequence;
						}
					}
				} else {
					g_frontState
						.netRuntimeRecvQueue
							[g_frontState
								 .netRuntimeRecvQueueWriteIndex]
						.packetClass = 1;
					if (g_frontState.netReliablePeerSlotCount >
						    peerIndex &&
					    peerIndex < PEER_CAPACITY) {
						unsigned int nextSequence =
							(unsigned int)g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqDefault +
							1;
						if (nextSequence >
						    SEQUENCE_LIMIT) {
							nextSequence = 0;
						}
						if (nextSequence ==
						    (unsigned int)sequence) {
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqDefault =
								(int)nextSequence;
						}
					}
				}
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.sequenceByte = (uint8_t)sequence;
				++g_frontState.netRuntimeRecvQueueWriteIndex;
				++g_frontState.netRuntimeRecvQueueCount;
				if (g_frontState
					    .netRuntimeRecvQueueWriteIndex >=
				    QUEUE_CAPACITY) {
					g_frontState
						.netRuntimeRecvQueueWriteIndex =
						0;
				}
				continue;
			}

			if (hasLength) {
				uint8_t *piggybackPayload;
				unsigned int piggybackSize;
				int previousSequence;
				int previousPacketType;

				previousSequence = sequence == 0
							   ? SEQUENCE_LIMIT
							   : sequence - 1;
				if (Net_CheckAndRecordIncomingSequence(
					    (int)fromId, previousSequence,
					    broadcastChannel,
					    groupChannel) == 0) {
					if ((unsigned int)g_frontState
						    .netRuntimeReliablePeerSlots
							    [peerIndex]
						    .packetCount >
					    PACKET_DROP_WARMUP_COUNT) {
						++g_frontState
							  .netRuntimeReliablePeerSlots
								  [peerIndex]
							  .packetDropCount;
					}
					piggybackPayload = (uint8_t *)payload +
							   payloadSize;
					previousPacketType =
						*piggybackPayload++;
					if (previousPacketType !=
					    NET_PACKET_NOP) {
						piggybackSize =
							wireSize -
							(uint16_t)(piggybackPayload -
								   (uint8_t *)&wirePacket
									   .header);
						if (piggybackSize >
						    MAX_PAYLOAD_SIZE) {
							piggybackSize =
								MAX_PAYLOAD_SIZE;
						}
						*(uint32_t *)g_frontState
							 .netRuntimeRecvQueue
								 [g_frontState
									  .netRuntimeRecvQueueWriteIndex]
							 .payload =
							previousPacketType;
						memcpy(g_frontState.netRuntimeRecvQueue
								       [g_frontState
										.netRuntimeRecvQueueWriteIndex]
									       .payload +
							       sizeof(int),
						       piggybackPayload,
						       piggybackSize);
						g_frontState
							.netRuntimeRecvQueue
								[g_frontState
									 .netRuntimeRecvQueueWriteIndex]
							.directPlayId = fromId;
						g_frontState
							.netRuntimeRecvQueue
								[g_frontState
									 .netRuntimeRecvQueueWriteIndex]
							.payloadSize =
							piggybackSize +
							sizeof(int);
						g_frontState
							.netRuntimeRecvQueue
								[g_frontState
									 .netRuntimeRecvQueueWriteIndex]
							.lastNackMs = 0;
						g_frontState
							.netRuntimeRecvQueue
								[g_frontState
									 .netRuntimeRecvQueueWriteIndex]
							.nackRetryCount = 0;
						g_frontState
							.netRuntimeRecvQueue
								[g_frontState
									 .netRuntimeRecvQueueWriteIndex]
							.isResentCopy = 0;
						if (broadcastChannel != 0) {
							g_frontState
								.netRuntimeRecvQueue
									[g_frontState
										 .netRuntimeRecvQueueWriteIndex]
								.packetClass =
								0;
						} else if (groupChannel != 0) {
							g_frontState
								.netRuntimeRecvQueue
									[g_frontState
										 .netRuntimeRecvQueueWriteIndex]
								.packetClass =
								2;
						} else {
							g_frontState
								.netRuntimeRecvQueue
									[g_frontState
										 .netRuntimeRecvQueueWriteIndex]
								.packetClass =
								1;
						}
						if (sequence == 0) {
							g_frontState
								.netRuntimeRecvQueue
									[g_frontState
										 .netRuntimeRecvQueueWriteIndex]
								.sequenceByte =
								SEQUENCE_LIMIT;
						} else {
							g_frontState
								.netRuntimeRecvQueue
									[g_frontState
										 .netRuntimeRecvQueueWriteIndex]
								.sequenceByte =
								(uint8_t)(sequence -
									  1);
						}
						++g_frontState
							  .netRuntimeRecvQueueWriteIndex;
						++g_frontState
							  .netRuntimeRecvQueueCount;
						if (g_frontState
							    .netRuntimeRecvQueueWriteIndex >=
						    QUEUE_CAPACITY) {
							g_frontState
								.netRuntimeRecvQueueWriteIndex =
								0;
						}
					}
				}
			}

			/* peerIndex is reused here as a flag: 1 when the sequence is stale or out of range (skipped). */
			peerIndex = Net_CheckAndRecordIncomingSequence(
				(int)fromId, sequence, broadcastChannel,
				groupChannel);
			if (peerIndex != 0) {
				continue;
			}
			payload = (int *)wirePacket.data;
			if (hasLength) {
				payloadSize = (uint32_t)
					NetSession_GetFixedPayloadSize(
						(int)packetType);
				if (payloadSize == 0) {
					payloadSize = *(const uint16_t *)
							       wirePacket.data;
					payload = (int *)(wirePacket.data +
							  sizeof(uint16_t));
				}
			} else {
				payloadSize =
					wireSize - sizeof(wirePacket.header);
			}
			if (payloadSize > MAX_PAYLOAD_SIZE) {
				payloadSize = MAX_PAYLOAD_SIZE;
			}
			*(uint32_t *)g_frontState
				 .netRuntimeRecvQueue
					 [g_frontState
						  .netRuntimeRecvQueueWriteIndex]
				 .payload = packetType;
			memcpy(g_frontState.netRuntimeRecvQueue
					       [g_frontState
							.netRuntimeRecvQueueWriteIndex]
						       .payload +
				       sizeof(packetType),
			       payload, payloadSize);
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.directPlayId = fromId;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.payloadSize = payloadSize + sizeof(packetType);
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.lastNackMs = 0;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.nackRetryCount = 0;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.isResentCopy = 0;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.isResentCopy = peerIndex != 0;
			if (broadcastChannel != 0) {
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.packetClass = 0;
			} else if (groupChannel != 0) {
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.packetClass = 2;
			} else {
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.packetClass = 1;
			}
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.sequenceByte = (uint8_t)sequence;
			++g_frontState.netRuntimeRecvQueueWriteIndex;
			++g_frontState.netRuntimeRecvQueueCount;
			if (g_frontState.netRuntimeRecvQueueWriteIndex >=
			    QUEUE_CAPACITY) {
				g_frontState.netRuntimeRecvQueueWriteIndex = 0;
			}
		}
	}
	if (backBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
}

// FUNCTION: XVT 0x4CEF70
int Net_SendPacketAndFlush(int toPlayerId, const void *packet,
			   unsigned int packetSize)
{
	int backBufferLocked;
	int flushPacket;
	int result;

	if (g_frontState.netDirectPlay == NULL) {
		return 1;
	}
	backBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	result = Net_SendPacketInternal(toPlayerId, packet, packetSize);
	flushPacket = NET_PACKET_NOP;
	Net_SendPacketInternal(toPlayerId, &flushPacket, sizeof(flushPacket));
	if (backBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return result;
}

// FUNCTION: XVT 0x4CEFE0
int Net_SendPacketInternal(int toPlayerId, const void *packet,
			   unsigned int packetSize)
{
	unsigned int packetType;
	int appendPending;
	uint16_t packetHeader;
	int sendResult = 0;
	char debugText[256];
	struct NetDirectPlayEncodedPacket encodedPacket;
	uint8_t *encodedPayload;
	unsigned int encodedSize;
	uint8_t packetTypeByte;

	packetType = *(const unsigned int *)packet;
	packetTypeByte = packetType & 0x7F;
	packetHeader = packetTypeByte;
	if (packetType >= NET_PACKET_RESYNC_CHECKSUMS &&
	    packetType < NET_PACKET_PROBE_REQUEST) {
		appendPending = 0;
	} else {
		appendPending = 1;
	}
	if (toPlayerId == 0) {
		sprintf(debugText, "(SB %u) ",
			g_frontState.netRuntimeBroadcastSeqCounter);
		packetHeader |=
			(g_frontState.netRuntimeBroadcastSeqCounter & 0x7F)
			<< 8;
		++g_frontState.netRuntimeBroadcastSeqCounter;
		if (g_frontState.netRuntimeBroadcastSeqCounter > 127) {
			g_frontState.netRuntimeBroadcastSeqCounter = 0;
		}
		encodedPacket.packetTypeHeader = packetHeader;
		encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
		encodedSize = 2;
		if (appendPending &&
		    NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPacket.payloadSize = packetSize - 4;
			encodedPayload = encodedPacket.payload;
			encodedSize = 4;
		}
		memcpy(encodedPayload, (const uint8_t *)packet + 4,
		       packetSize - 4);
		encodedPayload += packetSize - 4;
		encodedSize += packetSize - 4;
		if (appendPending) {
			if (g_frontState.netRuntimeBroadcastPendingPayload
				    .piggybackEmpty != 0) {
				*encodedPayload = NET_PACKET_NOP;
				++encodedSize;
				g_frontState.netRuntimeBroadcastPendingPayload
					.piggybackEmpty = 0;
			} else {
				memcpy(encodedPayload,
				       g_frontState
					       .netRuntimeBroadcastPendingPayload
					       .payload,
				       g_frontState
					       .netRuntimeBroadcastPendingPayload
					       .payloadLength);
				encodedSize +=
					g_frontState
						.netRuntimeBroadcastPendingPayload
						.payloadLength;
			}
		}
		g_frontState.netRuntimeBroadcastPendingPayload.payload[0] =
			packetTypeByte;
		memcpy(g_frontState.netRuntimeBroadcastPendingPayload.payload +
			       1,
		       (const uint8_t *)packet + 4, packetSize - 4);
		g_frontState.netRuntimeBroadcastPendingPayload.payloadLength =
			packetSize - 3;
	} else if (g_frontState.netGroupDplayId == (DPID)toPlayerId) {
		sprintf(debugText, "(SG %u) ",
			g_frontState.netRuntimeBroadcastSeqCounter);
		packetHeader |= (g_frontState.netRuntimeGroupSeqCounter & 0x7F)
				<< 8;
		++g_frontState.netRuntimeGroupSeqCounter;
		packetHeader |= 0x8080;
		if (g_frontState.netRuntimeGroupSeqCounter > 127) {
			g_frontState.netRuntimeGroupSeqCounter = 0;
		}
		encodedPacket.packetTypeHeader = packetHeader;
		encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
		encodedSize = 2;
		if (appendPending &&
		    NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPacket.payloadSize = packetSize - 4;
			encodedPayload = encodedPacket.payload;
			encodedSize = 4;
		}
		memcpy(encodedPayload, (const uint8_t *)packet + 4,
		       packetSize - 4);
		encodedPayload += packetSize - 4;
		encodedSize += packetSize - 4;
		if (appendPending) {
			if (g_frontState.netRuntimeGroupPendingPayload
				    .piggybackEmpty != 0) {
				*encodedPayload = NET_PACKET_NOP;
				++encodedSize;
				g_frontState.netRuntimeGroupPendingPayload
					.piggybackEmpty = 0;
			} else {
				memcpy(encodedPayload,
				       g_frontState
					       .netRuntimeGroupPendingPayload
					       .payload,
				       g_frontState
					       .netRuntimeGroupPendingPayload
					       .payloadLength);
				encodedSize +=
					g_frontState
						.netRuntimeGroupPendingPayload
						.payloadLength;
			}
		}
		g_frontState.netRuntimeGroupPendingPayload.payload[0] =
			packetTypeByte;
		memcpy(g_frontState.netRuntimeGroupPendingPayload.payload + 1,
		       (const uint8_t *)packet + 4, packetSize - 4);
		g_frontState.netRuntimeGroupPendingPayload.payloadLength =
			packetSize - 3;
	} else {
		unsigned int peerIndex = Net_FindOrCreatePeerSlot(toPlayerId);
		if (g_frontState.netReliablePeerSlotCount > peerIndex &&
		    peerIndex < 40) {
			int sendSequence;
			sprintf(debugText, "(SS %u) ",
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.sendSeq);
			sendSequence =
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.sendSeq;
			packetHeader |= (sendSequence++ & 0x7F) << 8;
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.sendSeq = sendSequence;
			if (sendSequence > 127) {
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.sendSeq = 0;
			}
		}
		packetHeader |= 0x8000;
		encodedPacket.packetTypeHeader = packetHeader;
		encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
		encodedSize = 2;
		if (appendPending &&
		    NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPacket.payloadSize = packetSize - 4;
			encodedPayload = encodedPacket.payload;
			encodedSize = 4;
		}
		memcpy(encodedPayload, (const uint8_t *)packet + 4,
		       packetSize - 4);
		encodedPayload += packetSize - 4;
		encodedSize += packetSize - 4;
		if (appendPending) {
			memcpy(encodedPayload,
			       &g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.lastPiggybackType,
			       g_frontState
				       .netRuntimeReliablePeerSlots[peerIndex]
				       .piggybackLength);
			encodedSize +=
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.piggybackLength;
		}
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastPiggybackType = packetTypeByte;
		memcpy(g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			       .piggybackPayload,
		       (const uint8_t *)packet + 4, packetSize - 4);
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.piggybackLength = packetSize - 3;
	}

	if (g_frontState.netRuntimeLocalPlayer.playerId != (DPID)toPlayerId) {
		memcpy(g_frontState
			       .netRuntimeSentHistory
				       [g_frontState
						.netRuntimeSentHistoryWriteIndex]
			       .payload,
		       packet, packetSize);
		g_frontState
			.netRuntimeSentHistory
				[g_frontState.netRuntimeSentHistoryWriteIndex]
			.directPlayId = toPlayerId;
		g_frontState
			.netRuntimeSentHistory
				[g_frontState.netRuntimeSentHistoryWriteIndex]
			.payloadSize = packetSize;
		g_frontState
			.netRuntimeSentHistory
				[g_frontState.netRuntimeSentHistoryWriteIndex]
			.lastNackMs = 0;
		g_frontState
			.netRuntimeSentHistory
				[g_frontState.netRuntimeSentHistoryWriteIndex]
			.nackRetryCount = 0;
		if (toPlayerId == 0) {
			g_frontState
				.netRuntimeSentHistory
					[g_frontState
						 .netRuntimeSentHistoryWriteIndex]
				.packetClass = 0;
		} else if (g_frontState.netGroupDplayId == (DPID)toPlayerId) {
			g_frontState
				.netRuntimeSentHistory
					[g_frontState
						 .netRuntimeSentHistoryWriteIndex]
				.packetClass = 2;
		} else {
			g_frontState
				.netRuntimeSentHistory
					[g_frontState
						 .netRuntimeSentHistoryWriteIndex]
				.packetClass = 1;
		}
		packetHeader = encodedPacket.packetTypeHeader;
		g_frontState
			.netRuntimeSentHistory
				[g_frontState.netRuntimeSentHistoryWriteIndex]
			.sequenceByte = (packetHeader & 0x7F00) >> 8;
		++g_frontState.netRuntimeSentHistoryWriteIndex;
		if (g_frontState.netRuntimeSentHistoryWriteIndex >= 128) {
			g_frontState.netRuntimeSentHistoryWriteIndex = 0;
		}
	}

	if ((g_frontState.netRuntimeLocalPlayer.playerId == (DPID)toPlayerId ||
	     toPlayerId == 0 || g_frontState.netDirectPlay == NULL ||
	     g_frontState.netGroupDplayId == (DPID)toPlayerId) &&
	    g_frontState.netRuntimeRecvQueueCount < 1024) {
		unsigned int peerIndex;
		memcpy(g_frontState
			       .netRuntimeRecvQueue
				       [g_frontState
						.netRuntimeRecvQueueWriteIndex]
			       .payload,
		       packet, packetSize);
		g_frontState
			.netRuntimeRecvQueue
				[g_frontState.netRuntimeRecvQueueWriteIndex]
			.directPlayId =
			g_frontState.netRuntimeLocalPlayer.playerId;
		g_frontState
			.netRuntimeRecvQueue
				[g_frontState.netRuntimeRecvQueueWriteIndex]
			.payloadSize = packetSize;
		g_frontState
			.netRuntimeRecvQueue
				[g_frontState.netRuntimeRecvQueueWriteIndex]
			.lastNackMs = 0;
		g_frontState
			.netRuntimeRecvQueue
				[g_frontState.netRuntimeRecvQueueWriteIndex]
			.nackRetryCount = 0;
		g_frontState
			.netRuntimeRecvQueue
				[g_frontState.netRuntimeRecvQueueWriteIndex]
			.isResentCopy = 0;
		peerIndex = Net_FindOrCreatePeerSlot(
			g_frontState.netRuntimeLocalPlayer.playerId);
		if (toPlayerId == 0) {
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.packetClass = 0;
			if (g_frontState.netReliablePeerSlotCount > peerIndex &&
			    peerIndex < 40) {
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.recvSeqChannelA =
					(packetHeader & 0x7F00) >> 8;
			}
		} else if (g_frontState.netGroupDplayId == (DPID)toPlayerId) {
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.packetClass = 2;
			if (g_frontState.netReliablePeerSlotCount > peerIndex &&
			    peerIndex < 40) {
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.recvSeqChannelB =
					(packetHeader & 0x7F00) >> 8;
			}
		} else {
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.packetClass = 1;
			if (g_frontState.netReliablePeerSlotCount > peerIndex &&
			    peerIndex < 40) {
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.recvSeqDefault =
					(packetHeader & 0x7F00) >> 8;
			}
		}
		g_frontState
			.netRuntimeRecvQueue
				[g_frontState.netRuntimeRecvQueueWriteIndex]
			.sequenceByte =
			(encodedPacket.packetTypeHeader & 0x7F00) >> 8;
		++g_frontState.netRuntimeRecvQueueCount;
		++g_frontState.netRuntimeRecvQueueWriteIndex;
		if (g_frontState.netRuntimeRecvQueueWriteIndex >= 1024) {
			g_frontState.netRuntimeRecvQueueWriteIndex = 0;
		}
	}

	if (g_frontState.netDirectPlay == NULL) {
		return 1;
	}
	if (g_frontState.netRuntimeLocalPlayer.playerId != (DPID)toPlayerId) {
		sendResult = g_frontState.netDirectPlay->lpVtbl->Send(
			g_frontState.netDirectPlay,
			g_frontState.netRuntimeLocalPlayer.playerId, toPlayerId,
			0, &encodedPacket.packetTypeHeader, encodedSize);
	}
	if (sendResult != 0) {
		char errorText[80];
		sprintf(errorText, "Send Returned: %-8x\n", sendResult);
	}
	return sendResult == 0;
}

// FUNCTION: XVT 0x4CF830
int Net_SendDirectPlayPacket(int destPlayerId, const void *packet,
			     int packetSize, int unusedSendMode)
{
	int appendTerminator;
	HRESULT sendResult;
	struct NetDirectPlayEncodedPacket encodedPacket;
	uint32_t packetType;
	uint16_t packetFlags;
	int deliveryMode;
	uint8_t *encodedPayload;
	int encodedHeaderSize;
	int encodedSize;

	(void)unusedSendMode;

	sendResult = 0;
	if (g_frontState.netDirectPlay == NULL) {
		return 1;
	}

	packetType = *(const uint32_t *)packet;
	packetFlags = (uint8_t)packetType & 0x7F;
	appendTerminator = packetType < NET_PACKET_RESYNC_CHECKSUMS ||
			   packetType >= NET_PACKET_PROBE_REQUEST;
	deliveryMode = 0;
	if (destPlayerId != 0) {
		deliveryMode =
			g_frontState.netGroupDplayId == (DPID)destPlayerId ? 2
									   : 0;
	}
	if (deliveryMode == 2) {
		packetFlags |= 0x8080;
	} else if (deliveryMode == 1) {
		packetFlags |= 0x8000;
	}

	encodedPacket.packetTypeHeader = packetFlags;
	encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
	encodedHeaderSize = 2;
	if (appendTerminator) {
		if (NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPayload = encodedPacket.payload;
			encodedPacket.payloadSize = packetSize - 4;
			encodedHeaderSize = 4;
		}
	}
	memcpy(encodedPayload, (const uint8_t *)packet + 4, packetSize - 4);
	encodedPayload += packetSize - 4;
	encodedSize = packetSize + encodedHeaderSize - 4;
	if (appendTerminator) {
		++encodedSize;
		*encodedPayload = NET_PACKET_NOP;
	}
	if (g_frontState.netRuntimeLocalPlayer.playerId != (DPID)destPlayerId) {
		sendResult = g_frontState.netDirectPlay->lpVtbl->Send(
			g_frontState.netDirectPlay,
			g_frontState.netRuntimeLocalPlayer.playerId,
			destPlayerId, 0, &encodedPacket.packetTypeHeader,
			encodedSize);
	}
	return sendResult == 0;
}

// FUNCTION: XVT 0x4CF980
int Net_SendSequencedDirectPlayPacket(int destPlayerId, int packetClass,
				      int sequenceId, const void *packet,
				      unsigned int packetSize)
{
	unsigned int packetType;
	uint8_t packetTypeByte;
	int appendTerminator;
	int sendResult;
	char debugText[256];
	NetDirectPlaySequencedPacket encodedPacket;
	uint8_t *encodedPayload;
	int encodedHeaderSize;
	unsigned int encodedSize;

	sendResult = 0;
	if (g_frontState.netDirectPlay == NULL) {
		return 1;
	}

	packetType = *(const uint32_t *)packet;
	packetTypeByte = (uint8_t)packetType & 0x7F;
	appendTerminator = packetType < NET_PACKET_RESYNC_CHECKSUMS ||
			   packetType >= NET_PACKET_PROBE_REQUEST;
	switch (packetClass) {
	case 0:
		sprintf(debugText, "(RSB %u) ", sequenceId);
		break;
	case 2:
		sprintf(debugText, "(RSG %u) ", sequenceId);
		break;
	default:
		sprintf(debugText, "(RSS %u) ", sequenceId);
	}

	encodedPayload = (uint8_t *)&encodedPacket.payloadSize;
	encodedHeaderSize = 3;
	encodedPacket.packetTypeHeader =
		(int16_t)(((((sequenceId & 0x7F) << 8) | packetTypeByte) &
			   0x7F7F) |
			  0x80);
	encodedPacket.packetClass = (uint8_t)packetClass;
	if (appendTerminator) {
		if (NetSession_GetFixedPayloadSize(packetType) == 0) {
			encodedPayload = encodedPacket.payload;
			encodedPacket.payloadSize = (int16_t)(packetSize - 4);
			encodedHeaderSize = 5;
		}
	}

	memcpy(encodedPayload, (const uint8_t *)packet + 4, packetSize - 4);
	encodedPayload += packetSize - 4;
	encodedSize = packetSize + encodedHeaderSize - 4;
	if (appendTerminator) {
		++encodedSize;
		*encodedPayload = NET_PACKET_NOP;
	}

	if (g_frontState.netRuntimeLocalPlayer.playerId != (DPID)destPlayerId) {
		sendResult = g_frontState.netDirectPlay->lpVtbl->Send(
			g_frontState.netDirectPlay,
			g_frontState.netRuntimeLocalPlayer.playerId,
			destPlayerId, 0, &encodedPacket.packetTypeHeader,
			encodedSize);
	} else {
		if (g_frontState.netRuntimeRecvQueueCount < 1024) {
			memcpy(g_frontState
				       .netRuntimeRecvQueue
					       [g_frontState
							.netRuntimeRecvQueueWriteIndex]
				       .payload,
			       packet, packetSize);
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.directPlayId =
				g_frontState.netRuntimeLocalPlayer.playerId;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.payloadSize = packetSize;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.packetClass = (uint8_t)packetClass;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.sequenceByte = (uint8_t)sequenceId;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.isResentCopy = 1;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.lastNackMs = 0;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.nackRetryCount = 0;
			++g_frontState.netRuntimeRecvQueueCount;
			++g_frontState.netRuntimeRecvQueueWriteIndex;
			if (g_frontState.netRuntimeRecvQueueWriteIndex >=
			    1024) {
				g_frontState.netRuntimeRecvQueueWriteIndex = 0;
			}
		}
	}
	return sendResult == 0;
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CFC20
int Net_EnumerateAppSessions(unsigned int appGuid0, unsigned int appGuid1,
			     unsigned int appGuid2, unsigned int appGuid3,
			     NetSessionEnumEntry *outSessions, int maxSessions,
			     NetworkTransportType networkType)
{
	int wasBackBufferLocked;
	const GUID *serviceProviderGuid;
	char errorMessage[256];
	DPSESSIONDESC2 sessionDesc;

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.netDirectPlay == NULL) {
		serviceProviderGuid =
			Net_GetDirectPlayServiceProviderGuid(networkType);
		if (serviceProviderGuid == NULL) {
			if (wasBackBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			return 0;
		}
		if (DirectPlayCreate(serviceProviderGuid,
				     &g_frontState.netTempDirectPlay,
				     NULL) != 0) {
			if (ErrorText_LoadLine(6, errorMessage) == 0) {
				FrontendDisplay_ShowGameMessageBox(
					"WARNING:  Connection failure!\n\nMake sure your Windows 95 network\nsettings are "
					"properly "
					"configured\nfor this type of network game.\n\nPress Enter to continue.");
			} else {
				FrontendDisplay_ShowGameMessageBox(
					errorMessage);
			}
			if (wasBackBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			return 0;
		}
		g_frontState.netTempDirectPlay->lpVtbl->QueryInterface(
			g_frontState.netTempDirectPlay, &IID_IDirectPlay2A,
			(void **)&g_frontState.netDirectPlay);
		g_frontState.netTempDirectPlay->lpVtbl->Release(
			g_frontState.netTempDirectPlay);
		g_frontState.netTempDirectPlay = NULL;
	}

	g_netEnumSessionCapacity = maxSessions;
	g_netEnumSessionCount = 0;
	memset(&sessionDesc, 0, sizeof(sessionDesc));
	sessionDesc.dwSize = sizeof(sessionDesc);
	sessionDesc.guidApplication = *(const GUID *)&appGuid0;
	g_frontState.netDirectPlay->lpVtbl->EnumSessions(
		g_frontState.netDirectPlay, &sessionDesc, 0,
		Net_EnumerateAppSessionsCallback, outSessions, 1);
	g_frontState.netDirectPlay->lpVtbl->Release(g_frontState.netDirectPlay);
	g_frontState.netDirectPlay = NULL;
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	qsort(outSessions, g_netEnumSessionCount, sizeof(*outSessions),
	      (int (*)(const void *,
		       const void *))Net_CompareSessionEnumEntriesByName);
	return g_netEnumSessionCount;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CFDB0
int AERON_DXAPI Net_EnumerateAppSessionsCallback(
	const DPSESSIONDESC2 *sessionDesc, uint32_t *timeoutMs, uint32_t flags,
	void *userData)
{
	NetSessionEnumEntry *outSessions = userData;

	(void)timeoutMs;

	if (sessionDesc == NULL || (flags & 1) != 0) {
		return 0;
	}
	if (g_netEnumSessionCapacity > g_netEnumSessionCount) {
		strncpy(outSessions[g_netEnumSessionCount].sessionName,
			sessionDesc->lpszSessionNameA,
			sizeof(outSessions[g_netEnumSessionCount].sessionName));
		outSessions[g_netEnumSessionCount]
			.sessionName[sizeof(outSessions[g_netEnumSessionCount]
						    .sessionName) -
				     1] = '\0';
		memcpy(&outSessions[g_netEnumSessionCount].sessionGuid,
		       &sessionDesc->guidInstance,
		       sizeof(outSessions[g_netEnumSessionCount].sessionGuid));
		++g_netEnumSessionCount;
		return 1;
	}

	return 0;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4CFE50
int Net_CompareSessionEnumEntriesByName(const NetSessionEnumEntry *lhs,
					const NetSessionEnumEntry *rhs)
{
	return strcmp(lhs->sessionName, rhs->sessionName);
}
#endif

// FUNCTION: XVT 0x4CFE80
NetPlayerInfo *Net_GetPlayerRoster(int *outCount)
{
	*outCount = g_frontState.netPlayerCount;
	return g_frontState.netPlayers;
}

// FUNCTION: XVT 0x4CFEA0
int Net_GetPlayerCount(void)
{
	if (g_frontState.netPlayerCount == 0) {
		return 1;
	}
	return g_frontState.netPlayerCount;
}

// FUNCTION: XVT 0x4CFED0
int Net_DidReadyPlayerLeaveThisFrame(void)
{
	return g_frontState.netReadyPlayerLeftThisFrame;
}

// FUNCTION: XVT 0x4CFEE0
int Net_IsHost(void) { return g_frontState.netIsHost; }

// FUNCTION: XVT 0x4CFEF0
int Net_PollForPacketTypeOrBacklog(int packetType)
{
	enum {
		QUEUE_CAPACITY = sizeof(g_frontState.netRuntimeRecvQueue) /
				 sizeof(g_frontState.netRuntimeRecvQueue[0]),
		BACKLOG_THRESHOLD = QUEUE_CAPACITY / 2,
	};

	int backBufferLocked;
	int queueIndex;
	int remaining;

	if (g_frontState.netDirectPlay == NULL) {
		return 0;
	}
	backBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	Net_PumpIncomingPackets();
	if (g_frontState.netRuntimeRecvQueueCount > BACKLOG_THRESHOLD) {
		if (backBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		return 1;
	}
	queueIndex = g_frontState.netRuntimeRecvQueueReadIndex;
	remaining = g_frontState.netRuntimeRecvQueueCount;
	while (remaining > 0) {
		if (g_frontState.netRuntimeRecvQueue[queueIndex].directPlayId !=
			    0 &&
		    *(int *)g_frontState.netRuntimeRecvQueue[queueIndex]
				    .payload == packetType) {
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			return 1;
		}
		if (++queueIndex >= QUEUE_CAPACITY) {
			queueIndex = 0;
		}
		--remaining;
	}
	if (backBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 0;
}

// FUNCTION: XVT 0x4CFFB0
int Net_PollForPlayerCreatedOrBacklog(void)
{
	enum {
		QUEUE_CAPACITY = sizeof(g_frontState.netRuntimeRecvQueue) /
				 sizeof(g_frontState.netRuntimeRecvQueue[0]),
		BACKLOG_THRESHOLD = QUEUE_CAPACITY / 2,
	};

	int backBufferLocked;
	int queueIndex;
	int remaining;

	if (g_frontState.netDirectPlay == NULL) {
		return 0;
	}
	backBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	Net_PumpIncomingPackets();
	if (g_frontState.netRuntimeRecvQueueCount > BACKLOG_THRESHOLD) {
		if (backBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		return 1;
	}
	queueIndex = g_frontState.netRuntimeRecvQueueReadIndex;
	remaining = g_frontState.netRuntimeRecvQueueCount;
	while (remaining > 0) {
		if (g_frontState.netRuntimeRecvQueue[queueIndex].directPlayId ==
			    0 &&
		    *(int *)g_frontState.netRuntimeRecvQueue[queueIndex]
				    .payload == DPSYS_CREATEPLAYERORGROUP) {
			if (backBufferLocked != 0) {
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
			}
			return 1;
		}
		if (++queueIndex >= QUEUE_CAPACITY) {
			queueIndex = 0;
		}
		--remaining;
	}
	if (backBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 0;
}

// FUNCTION: XVT 0x4D0070
int *Net_GetNextAppPacket(DPID *outSenderId, uint32_t *outPacketSize)
{
	int backBufferLocked;
	int *packet;

	backBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	do {
		packet = Net_DequeueIncomingPacket(outSenderId, outPacketSize);
		if (packet == NULL || *outSenderId != 0) {
			break;
		}
		Net_HandleDirectPlaySystemMessage(*packet, packet);
	} while (1);
	if (backBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return packet;
}

// FUNCTION: XVT 0x4D00D0
void Net_HandleDirectPlaySystemMessage(int packetType, const void *packetData)
{
	enum {
		SEQUENCE_STATUS_PACKET_SIZE = 512,
		SEQUENCE_INITIAL_VALUE = 127,
		PLAYER_NAME_TRUNCATION_INDEX = 12,
	};

	const int *packetWords = (const int *)packetData;

	switch (packetType) {
	case DPSYS_CREATEPLAYERORGROUP: {
		if (g_frontState.netIsHost != 0) {
			if (packetWords[1] == DPPLAYERTYPE_PLAYER &&
			    packetWords[2] !=
				    (int)g_frontState.netHostPlayerId) {
				typedef struct NetSequenceStatusRecord {
					int playerId;
					uint8_t previousChannelA;
					uint8_t previousChannelB;
					uint8_t channelA;
					uint8_t channelB;
				} NetSequenceStatusRecord;

				typedef struct NetSequenceStatusPacket {
					int packetType;
					int playerCount;
					int peerSlotCount;
					uint32_t timestampMs;
					NetSequenceStatusRecord records
						[(SEQUENCE_STATUS_PACKET_SIZE -
						  4 * sizeof(int)) /
						 sizeof(NetSequenceStatusRecord)];
				} NetSequenceStatusPacket;

				NetSequenceStatusRecord *statusRecords;
				NetSequenceStatusPacket statusPacket;
				unsigned int peerIndex;
				uint32_t nowMs;

				g_frontState.netPlayerCount = 1;
				Net_RefreshPlayerRoster();
				statusPacket.playerCount =
					g_frontState.netPlayerCount;
				statusPacket.peerSlotCount =
					g_frontState.netReliablePeerSlotCount;
				statusPacket.packetType =
					NET_PACKET_SEQUENCE_STATUS;
				nowMs = GetTickCount();
				peerIndex = 0;
				statusRecords = statusPacket.records;
				statusPacket.timestampMs = nowMs;
				if (g_frontState.netReliablePeerSlotCount > 0) {
					do {
						const NetReliablePeerSlot *peer =
							&g_frontState.netRuntimeReliablePeerSlots
								 [peerIndex];
						NetSequenceStatusRecord *record =
							&statusRecords
								[peerIndex];
						record->playerId =
							peer->directPlayId;
						record->previousChannelA =
							(uint8_t)peer
								->lastDeliveredSeqChannelA;
						record->previousChannelB =
							(uint8_t)peer
								->lastDeliveredSeqChannelB;
						record->channelA =
							(uint8_t)peer
								->recvSeqChannelA;
						record->channelB =
							(uint8_t)peer
								->recvSeqChannelB;
						++peerIndex;
					} while (
						g_frontState
							.netReliablePeerSlotCount >
						peerIndex);
				}
				Net_SendPacketAndFlush(
					packetWords[2], &statusPacket,
					(unsigned int)(sizeof(statusPacket
								      .packetType) +
						       g_frontState.netReliablePeerSlotCount *
							       sizeof(statusPacket
									      .records[0]) +
						       3 * sizeof(int)));
			}
		} else {
			g_frontState.netPlayerCount = 1;
			Net_RefreshPlayerRoster();
		}
		break;
	}
	case DPSYS_DESTROYPLAYERORGROUP: {
#ifdef XVT_MODERN
		if (packetWords[1] == DPPLAYERTYPE_PLAYER &&
		    (DPID)packetWords[2] == g_frontState.netHostPlayerId) {
			XvtNetworkSession_HostLost();
		}
#endif
		if (g_frontState.netIsHost != 0) {
			if (packetWords[1] == DPPLAYERTYPE_PLAYER) {
				unsigned int playerIndex;
				unsigned int peerIndex;

				for (playerIndex = 0;
				     playerIndex <
				     (unsigned int)g_frontState.netPlayerCount;
				     ++playerIndex) {
					if (g_frontState.netPlayers[playerIndex]
						    .playerId ==
					    (DPID)packetWords[2]) {
						if (g_frontState
							    .netPlayers
								    [playerIndex]
							    .readyFlag != 0) {
							g_frontState
								.netReadyPlayerLeftThisFrame =
								1;
						}
						break;
					}
				}
				Net_ClearPlayerReadyFlagWithLockGuard(
					(DPID)packetWords[2]);
				for (peerIndex = 0;
				     peerIndex <
				     g_frontState.netReliablePeerSlotCount;
				     ++peerIndex) {
					if (g_frontState
						    .netRuntimeReliablePeerSlots
							    [peerIndex]
						    .directPlayId ==
					    (DPID)packetWords[2]) {
						--g_frontState
							  .netReliablePeerSlotCount;
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex] =
							g_frontState.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount];
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.directPlayId = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.lastDeliveredSeqDefault =
							SEQUENCE_INITIAL_VALUE;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.lastDeliveredSeqChannelA =
							SEQUENCE_INITIAL_VALUE;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.lastDeliveredSeqChannelB =
							SEQUENCE_INITIAL_VALUE;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.recvSeqDefault =
							SEQUENCE_INITIAL_VALUE;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.recvSeqChannelA =
							SEQUENCE_INITIAL_VALUE;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.recvSeqChannelB =
							SEQUENCE_INITIAL_VALUE;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.sendSeq = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.lastPiggybackType =
							NET_PACKET_NOP;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.piggybackLength = 1;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.lastHeardMs = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.lastActivityMs = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.packetCount = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.packetDropCount = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[g_frontState
									 .netReliablePeerSlotCount]
							.packetRetryCount = 0;
						break;
					}
				}
				for (playerIndex = 0;
				     playerIndex <
				     (unsigned int)(sizeof(g_netPlayerConnectionStats) /
						    sizeof(g_netPlayerConnectionStats
								   [0]));
				     ++playerIndex) {
					if (g_netPlayerConnectionStats
						    [playerIndex]
							    .playerId ==
					    packetWords[2]) {
						g_netPlayerConnectionStats
							[playerIndex]
								.playerId = 0;
						g_netPlayerConnectionStats
							[playerIndex]
								.latencyTotalMs =
							0;
						g_netPlayerConnectionStats
							[playerIndex]
								.packetCount =
							0;
						g_netPlayerConnectionStats
							[playerIndex]
								.packetDropCount =
							0;
						g_netPlayerConnectionStats
							[playerIndex]
								.packetRetryCount =
							0;
						g_netPlayerConnectionStats
							[playerIndex]
								.latencySampleCount =
							0;
						break;
					}
				}
			}
		} else if (packetWords[1] == DPPLAYERTYPE_PLAYER &&
			   (DPID)packetWords[2] ==
				   g_frontState.netHostPlayerId) {
			const int hostCancelledPacket =
				NET_PACKET_HOST_CANCELLED;
			Net_SendPacketAndFlush(
				g_frontState.netRuntimeLocalPlayer.playerId,
				&hostCancelledPacket,
				sizeof(hostCancelledPacket));
		}
		g_frontState.netPlayerCount = 1;
		Net_RefreshPlayerRoster();
		break;
	}
	case DPSYS_SETPLAYERORGROUPNAME:
		if (((const NetPlayerNameMessage *)packetData)
			    ->header.dwPlayerType == DPPLAYERTYPE_PLAYER) {
			unsigned int playerIndex;
			for (playerIndex = 0;
			     playerIndex <
			     (unsigned int)g_frontState.netPlayerCount;
			     ++playerIndex) {
				if (g_frontState.netPlayers[playerIndex]
					    .playerId ==
				    ((const NetPlayerNameMessage *)packetData)
					    ->header.dpId) {
#ifdef XVT_MODERN
					if (!XvtNetworkSession_CopyPlayerNames(
						    (const NetPlayerNameMessage
							     *)packetData,
						    g_frontState
							    .netPlayers
								    [playerIndex]
							    .playerName,
						    sizeof(g_frontState
								   .netPlayers
									   [playerIndex]
								   .playerName),
						    g_frontState
							    .netPlayers
								    [playerIndex]
							    .playerInfo,
						    sizeof(g_frontState
								   .netPlayers
									   [playerIndex]
								   .playerInfo))) {
						continue;
					}
#else
					strcpy(g_frontState
						       .netPlayers[playerIndex]
						       .playerName,
					       ((const NetPlayerNameMessage *)
							packetData)
						       ->names);
					strcpy(g_frontState
						       .netPlayers[playerIndex]
						       .playerInfo,
					       &((const NetPlayerNameMessage *)
							 packetData)
							->names[strlen(g_frontState
									       .netPlayers
										       [playerIndex]
									       .playerName) +
								1]);
#endif
					g_frontState.netPlayers[playerIndex].playerName
						[PLAYER_NAME_TRUNCATION_INDEX] =
						'\0';
					g_frontState.netPlayers[playerIndex].playerInfo
						[PLAYER_NAME_TRUNCATION_INDEX] =
						'\0';
				}
			}
		}
		break;
	default:
		break;
	}
}

// FUNCTION: XVT 0x4D0540
void *Net_DequeueIncomingPacket(DPID *outSenderId, uint32_t *outPacketSize)
{
	enum {
		NET_RECV_QUEUE_CAPACITY = 1024,
		NET_RECV_QUEUE_PRESSURE_THRESHOLD = NET_RECV_QUEUE_CAPACITY - 1,
		NET_RELIABLE_PEER_CAPACITY = 40,
		NET_RELIABLE_CHANNEL_COUNT = 3,
		NET_RELIABLE_CHANNEL_A = 0,
		NET_RELIABLE_CHANNEL_DEFAULT = 1,
		NET_RELIABLE_CHANNEL_B = 2,
		NET_RELIABLE_SEQUENCE_LIMIT = 127,
		NET_RELIABLE_SEQUENCE_COUNT = NET_RELIABLE_SEQUENCE_LIMIT + 1,
		NET_FIRST_NON_FLIGHT_PACKET_TYPE = NET_PACKET_WORLD_NACK,
		NET_RELIABLE_REORDER_ALLOWANCE = 90,
		NET_RELIABLE_NEGATIVE_WINDOW = -28,
		NET_RELIABLE_PRESSURE_NEGATIVE_WINDOW = -27,
		NET_RELIABLE_POSITIVE_WINDOW = 100,
		NET_RELIABLE_RETRY_PACKET_WORD_COUNT = 3,
		NET_RELIABLE_RETRY_COUNT_THRESHOLD = 20,
		NET_RELIABLE_SHORT_RETRY_LIMIT = 20,
		NET_RELIABLE_SHORT_RETRY_TIMEOUT_MS = 1000,
		NET_RELIABLE_LONG_RETRY_LIMIT = 0,
		NET_RELIABLE_LONG_RETRY_TIMEOUT_MS = 20000
	};

	int receivedSequence;
	int expectedSequence = 0;

	int queuedPacketIndex;
	int useChannelA;
	int useChannelB;
	int remainingPacketCount;
	int missingPacketCount;
	int channelIndex;
	int sentRetryRequest;
	int firstMissingSequence;
	uint8_t lastSeenSequences[NET_RELIABLE_PEER_CAPACITY]
				 [NET_RELIABLE_CHANNEL_COUNT];
	uint8_t processedPacketCounts[NET_RELIABLE_PEER_CAPACITY];
	uint8_t reorderAllowances[NET_RELIABLE_PEER_CAPACITY];
	char debugText[256];
	uint32_t retryPacket[128];

	unsigned int peerIndex;
	int sequenceCursor;
	uint32_t retryTimeoutMs;
	uint32_t now;
	int scanIndex;
	int packetType;
	int sequenceDelta;

	Net_PumpIncomingPackets();
	Net_SendSequenceKeepalives();
	if (g_frontState.netRuntimeRecvQueueCount == 0) {
		return NULL;
	}

	memset(processedPacketCounts, 0, sizeof(processedPacketCounts));
	memset(reorderAllowances, NET_RELIABLE_REORDER_ALLOWANCE,
	       sizeof(reorderAllowances));
	memset(lastSeenSequences, 0, sizeof(lastSeenSequences));
	/* channelIndex first walks the peer slots; the test below compares the slot count it is left at with
	 * peerIndex to spot a new slot. Only later does it hold a channel. */
	for (channelIndex = 0;
	     channelIndex < (int)g_frontState.netReliablePeerSlotCount;
	     ++channelIndex) {
		lastSeenSequences[channelIndex][NET_RELIABLE_CHANNEL_A] =
			(uint8_t)g_frontState
				.netRuntimeReliablePeerSlots[channelIndex]
				.lastDeliveredSeqChannelA;
		lastSeenSequences[channelIndex][NET_RELIABLE_CHANNEL_DEFAULT] =
			(uint8_t)g_frontState
				.netRuntimeReliablePeerSlots[channelIndex]
				.lastDeliveredSeqDefault;
		lastSeenSequences[channelIndex][NET_RELIABLE_CHANNEL_B] =
			(uint8_t)g_frontState
				.netRuntimeReliablePeerSlots[channelIndex]
				.lastDeliveredSeqChannelB;
	}

	scanIndex = g_frontState.netRuntimeRecvQueueReadIndex;
	remainingPacketCount = g_frontState.netRuntimeRecvQueueCount;
	for (; remainingPacketCount > 0; --remainingPacketCount) {
		if (g_frontState.netRuntimeRecvQueue[scanIndex].directPlayId ==
		    0) {
			if (g_frontState.netRuntimeRecvQueueReadIndex !=
			    scanIndex) {
				if (++scanIndex >= NET_RECV_QUEUE_CAPACITY) {
					scanIndex = 0;
				}
				continue;
			}
			{
				g_frontState.netRuntimeRecvScratchPacket =
					g_frontState
						.netRuntimeRecvQueue[scanIndex];
				*outSenderId =
					g_frontState
						.netRuntimeRecvQueue[scanIndex]
						.directPlayId;
				*outPacketSize =
					g_frontState
						.netRuntimeRecvQueue[scanIndex]
						.payloadSize;
				--g_frontState.netRuntimeRecvQueueCount;
				++g_frontState.netRuntimeRecvQueueReadIndex;
				if (g_frontState.netRuntimeRecvQueueReadIndex >=
				    NET_RECV_QUEUE_CAPACITY) {
					g_frontState
						.netRuntimeRecvQueueReadIndex =
						0;
				}
				return g_frontState.netRuntimeRecvScratchPacket
					.payload;
			}
		} else {
			receivedSequence =
				g_frontState.netRuntimeRecvQueue[scanIndex]
					.sequenceByte;
			useChannelA =
				g_frontState.netRuntimeRecvQueue[scanIndex]
					.packetClass == NET_RELIABLE_CHANNEL_A;
			useChannelB =
				g_frontState.netRuntimeRecvQueue[scanIndex]
					.packetClass == NET_RELIABLE_CHANNEL_B;
			packetType = *(const int *)g_frontState
					      .netRuntimeRecvQueue[scanIndex]
					      .payload;
			peerIndex = Net_FindOrCreatePeerSlot(
				(int)g_frontState.netRuntimeRecvQueue[scanIndex]
					.directPlayId);
			if (channelIndex == (int)peerIndex &&
			    peerIndex < NET_RELIABLE_PEER_CAPACITY) {
				lastSeenSequences
					[peerIndex][NET_RELIABLE_CHANNEL_A] =
						NET_RELIABLE_SEQUENCE_LIMIT;
				lastSeenSequences
					[peerIndex]
					[NET_RELIABLE_CHANNEL_DEFAULT] =
						NET_RELIABLE_SEQUENCE_LIMIT;
				lastSeenSequences
					[peerIndex][NET_RELIABLE_CHANNEL_B] =
						NET_RELIABLE_SEQUENCE_LIMIT;
			}

			if (g_frontState.netReliablePeerSlotCount > peerIndex) {
				if ((unsigned int)packetType <
				    NET_FIRST_NON_FLIGHT_PACKET_TYPE) {
					if (g_frontState
						    .netRuntimeRecvQueue
							    [scanIndex]
						    .isResentCopy == 0) {
						if (useChannelA != 0) {
							lastSeenSequences
								[peerIndex]
								[NET_RELIABLE_CHANNEL_A] =
									(uint8_t)
										receivedSequence;
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqChannelA =
								receivedSequence;
						} else if (useChannelB != 0) {
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqChannelB =
								receivedSequence;
							lastSeenSequences
								[peerIndex]
								[NET_RELIABLE_CHANNEL_B] =
									(uint8_t)
										receivedSequence;
						} else {
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.lastDeliveredSeqDefault =
								receivedSequence;
							lastSeenSequences
								[peerIndex]
								[NET_RELIABLE_CHANNEL_DEFAULT] =
									(uint8_t)
										receivedSequence;
						}
					}
					if (Net_RemoveIncomingPacketAtIndex(
						    (unsigned int)scanIndex) ==
					    0) {
						continue;
					}
					if (++scanIndex >=
					    NET_RECV_QUEUE_CAPACITY) {
						scanIndex = 0;
					}
					continue;
				}
				if (reorderAllowances[peerIndex] <
				    processedPacketCounts[peerIndex]) {
					if (++scanIndex >=
					    NET_RECV_QUEUE_CAPACITY) {
						scanIndex = 0;
					}
					continue;
				}
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.lastHeardMs = GetTickCount();
				if (g_frontState.netRuntimeRecvQueue[scanIndex]
					    .isResentCopy == 0) {
					++processedPacketCounts[peerIndex];
				}

				if (useChannelA != 0) {
					expectedSequence =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelA +
						1;
					if (expectedSequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expectedSequence = 0;
					}
				} else if (useChannelB != 0) {
					expectedSequence =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelB +
						1;
					if (expectedSequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expectedSequence = 0;
					}
				} else {
					expectedSequence =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqDefault +
						1;
					if (expectedSequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expectedSequence = 0;
					}
				}
			}
			sequenceDelta = receivedSequence - expectedSequence;
			if (g_frontState.netReliablePeerSlotCount > peerIndex &&
			    (sequenceDelta < NET_RELIABLE_NEGATIVE_WINDOW ||
			     (sequenceDelta >= 0 &&
			      sequenceDelta < NET_RELIABLE_POSITIVE_WINDOW))) {
				if (expectedSequence == receivedSequence) {
					g_frontState
						.netRuntimeReliablePeerSlots
							[peerIndex]
						.lastActivityMs =
						GetTickCount();
					if (useChannelA != 0) {
						sprintf(debugText, "(RB %u) ",
							receivedSequence);
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelA =
							receivedSequence;
					} else if (useChannelB != 0) {
						sprintf(debugText, "(RG %u) ",
							receivedSequence);
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelB =
							receivedSequence;
					} else {
						sprintf(debugText, "(RS %u) ",
							receivedSequence);
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqDefault =
							receivedSequence;
					}
					++g_frontState
						  .netRuntimeReliablePeerSlots
							  [peerIndex]
						  .packetCount;
					g_frontState
						.netRuntimeRecvScratchPacket =
						g_frontState.netRuntimeRecvQueue
							[scanIndex];
					Net_RemoveIncomingPacketAtIndex(
						(unsigned int)scanIndex);
					*outSenderId =
						g_frontState
							.netRuntimeRecvScratchPacket
							.directPlayId;
					*outPacketSize =
						g_frontState
							.netRuntimeRecvScratchPacket
							.payloadSize;
					return g_frontState
						.netRuntimeRecvScratchPacket
						.payload;
				}
				if (g_frontState.netRuntimeRecvQueue[scanIndex]
					    .isResentCopy == 0) {
					if (useChannelA != 0) {
						channelIndex =
							NET_RELIABLE_CHANNEL_A;
					} else if (useChannelB != 0) {
						channelIndex =
							NET_RELIABLE_CHANNEL_B;
					} else {
						channelIndex =
							NET_RELIABLE_CHANNEL_DEFAULT;
					}
					sequenceCursor =
						(int)lastSeenSequences
							[peerIndex]
							[channelIndex] +
						1;
					lastSeenSequences
						[peerIndex]
						[channelIndex] = (uint8_t)
							receivedSequence;
					if (sequenceCursor >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						sequenceCursor = 0;
					}
					missingPacketCount =
						receivedSequence -
						(int)sequenceCursor;
					if (missingPacketCount < 0) {
						missingPacketCount +=
							NET_RELIABLE_SEQUENCE_COUNT;
					}
					if (reorderAllowances[peerIndex] >=
					    missingPacketCount) {
						reorderAllowances[peerIndex] -=
							(uint8_t)
								missingPacketCount;
					} else {
						reorderAllowances[peerIndex] =
							0;
					}
					sentRetryRequest = 0;
					firstMissingSequence = sequenceCursor;
					while (sequenceCursor !=
					       receivedSequence) {
						queuedPacketIndex =
							Net_FindQueuedSequencedPacket(
								scanIndex,
								(int)sequenceCursor,
								useChannelA,
								useChannelB,
								(int)peerIndex);
						if (queuedPacketIndex <
							    NET_RECV_QUEUE_CAPACITY &&
						    queuedPacketIndex >= 0) {
							if (expectedSequence ==
							    (int)sequenceCursor) {
								g_frontState
									.netRuntimeReliablePeerSlots
										[peerIndex]
									.lastActivityMs =
									GetTickCount();
								g_frontState
									.netRuntimeRecvScratchPacket =
									g_frontState
										.netRuntimeRecvQueue
											[queuedPacketIndex];
								if (useChannelA !=
								    0) {
									sprintf(debugText,
										"(ROOB %u) ",
										expectedSequence);
									g_frontState
										.netRuntimeReliablePeerSlots
											[peerIndex]
										.lastDeliveredSeqChannelA =
										expectedSequence;
								} else if (
									useChannelB !=
									0) {
									sprintf(debugText,
										"(ROOG %u) ",
										expectedSequence);
									g_frontState
										.netRuntimeReliablePeerSlots
											[peerIndex]
										.lastDeliveredSeqChannelB =
										expectedSequence;
								} else {
									sprintf(debugText,
										"(ROOS %u) ",
										expectedSequence);
									g_frontState
										.netRuntimeReliablePeerSlots
											[peerIndex]
										.lastDeliveredSeqDefault =
										expectedSequence;
								}
								++g_frontState
									  .netRuntimeReliablePeerSlots
										  [peerIndex]
									  .packetCount;
								*outSenderId =
									g_frontState
										.netRuntimeRecvScratchPacket
										.directPlayId;
								*outPacketSize =
									g_frontState
										.netRuntimeRecvScratchPacket
										.payloadSize;
								if (missingPacketCount <=
								    1) {
									g_frontState
										.netRuntimeRecvQueue
											[scanIndex]
										.nackRetryCount =
										0;
									g_frontState
										.netRuntimeRecvQueue
											[scanIndex]
										.lastNackMs =
										0;
								}
								Net_RemoveIncomingPacketAtIndex(
									queuedPacketIndex);
								return g_frontState
									.netRuntimeRecvScratchPacket
									.payload;
							}
							--missingPacketCount;
						} else if (
							g_frontState
								.netRuntimeRecvQueue
									[scanIndex]
								.nackRetryCount ==
							0) {
							sprintf(debugText,
								"(RP %d) ",
								sequenceCursor);
							if ((unsigned int)g_frontState
								    .netRuntimeReliablePeerSlots
									    [peerIndex]
								    .packetCount >
							    NET_RELIABLE_RETRY_COUNT_THRESHOLD) {
								++g_frontState
									  .netRuntimeReliablePeerSlots
										  [peerIndex]
									  .packetRetryCount;
							}
							retryPacket[1] =
								sequenceCursor;
							retryPacket[0] =
								NET_PACKET_NACK;
							retryPacket[2] =
								useChannelA
									? NET_RELIABLE_CHANNEL_A
									: (useChannelB
										   ? NET_RELIABLE_CHANNEL_B
										   : NET_RELIABLE_CHANNEL_DEFAULT);
							Net_SendDirectPlayPacket(
								(int)g_frontState
									.netRuntimeRecvQueue
										[scanIndex]
									.directPlayId,
								retryPacket,
								NET_RELIABLE_RETRY_PACKET_WORD_COUNT *
									sizeof(retryPacket
										       [0]),
								1);
							sentRetryRequest = 1;
						} else {
							now = GetTickCount();
							/* queuedPacketIndex is reused here as the NACK retry limit. */
							if (g_frontState
								    .netReliableRetryLongTimeoutMode ==
							    1) {
								queuedPacketIndex =
									NET_RELIABLE_LONG_RETRY_LIMIT;
								retryTimeoutMs =
									NET_RELIABLE_LONG_RETRY_TIMEOUT_MS;
							} else {
								queuedPacketIndex =
									NET_RELIABLE_SHORT_RETRY_LIMIT;
								retryTimeoutMs =
									NET_RELIABLE_SHORT_RETRY_TIMEOUT_MS;
							}
							if (now - (uint32_t)g_frontState
									    .netRuntimeRecvQueue
										    [scanIndex]
									    .lastNackMs >
							    retryTimeoutMs) {
								if (g_frontState
									    .netRuntimeRecvQueue
										    [scanIndex]
									    .nackRetryCount <=
								    (unsigned int)
									    queuedPacketIndex) {
									sprintf(debugText,
										"(RP %d) ",
										sequenceCursor);
									retryPacket[1] =
										sequenceCursor;
									retryPacket[0] =
										NET_PACKET_NACK;
									retryPacket[2] =
										useChannelA
											? NET_RELIABLE_CHANNEL_A
											: (useChannelB
												   ? NET_RELIABLE_CHANNEL_B
												   : NET_RELIABLE_CHANNEL_DEFAULT);
									Net_SendDirectPlayPacket(
										(int)g_frontState
											.netRuntimeRecvQueue
												[scanIndex]
											.directPlayId,
										retryPacket,
										NET_RELIABLE_RETRY_PACKET_WORD_COUNT *
											sizeof(retryPacket
												       [0]),
										1);
									sentRetryRequest =
										1;
								} else {
									sequenceCursor =
										firstMissingSequence;
									g_frontState
										.netRuntimeReliablePeerSlots
											[peerIndex]
										.lastActivityMs =
										GetTickCount();
									for (;
									     ;) {
										queuedPacketIndex = Net_FindQueuedSequencedPacket(
											scanIndex,
											(int)sequenceCursor,
											useChannelA,
											useChannelB,
											(int)peerIndex);
										if (queuedPacketIndex >=
											    0 &&
										    queuedPacketIndex <=
											    NET_RECV_QUEUE_CAPACITY) {
											g_frontState
												.netRuntimeRecvScratchPacket =
												g_frontState
													.netRuntimeRecvQueue
														[queuedPacketIndex];
											Net_RemoveIncomingPacketAtIndex(
												queuedPacketIndex);
											if (useChannelA !=
											    0) {
												sprintf(debugText,
													"(ROOB %u) ",
													expectedSequence);
												g_frontState
													.netRuntimeReliablePeerSlots
														[peerIndex]
													.lastDeliveredSeqChannelA =
													(int)sequenceCursor;
											} else if (
												useChannelB !=
												0) {
												sprintf(debugText,
													"(ROOG %u) ",
													expectedSequence);
												g_frontState
													.netRuntimeReliablePeerSlots
														[peerIndex]
													.lastDeliveredSeqChannelB =
													(int)sequenceCursor;
											} else {
												sprintf(debugText,
													"(ROOS %u) ",
													expectedSequence);
												g_frontState
													.netRuntimeReliablePeerSlots
														[peerIndex]
													.lastDeliveredSeqDefault =
													(int)sequenceCursor;
											}
											++g_frontState
												  .netRuntimeReliablePeerSlots
													  [peerIndex]
												  .packetCount;
											*outSenderId =
												g_frontState
													.netRuntimeRecvScratchPacket
													.directPlayId;
											*outPacketSize =
												g_frontState
													.netRuntimeRecvScratchPacket
													.payloadSize;
											return g_frontState
												.netRuntimeRecvScratchPacket
												.payload;
										}
										if (receivedSequence ==
										    (int)sequenceCursor) {
											if (useChannelA !=
											    0) {
												sprintf(debugText,
													"(ROOB %u) ",
													receivedSequence);
												g_frontState
													.netRuntimeReliablePeerSlots
														[peerIndex]
													.lastDeliveredSeqChannelA =
													receivedSequence;
											} else if (
												useChannelB !=
												0) {
												sprintf(debugText,
													"(ROOG %u) ",
													receivedSequence);
												g_frontState
													.netRuntimeReliablePeerSlots
														[peerIndex]
													.lastDeliveredSeqChannelB =
													receivedSequence;
											} else {
												sprintf(debugText,
													"(ROOS %u) ",
													receivedSequence);
												g_frontState
													.netRuntimeReliablePeerSlots
														[peerIndex]
													.lastDeliveredSeqDefault =
													receivedSequence;
											}
											++g_frontState
												  .netRuntimeReliablePeerSlots
													  [peerIndex]
												  .packetCount;
											g_frontState
												.netRuntimeRecvScratchPacket =
												g_frontState
													.netRuntimeRecvQueue
														[scanIndex];
											Net_RemoveIncomingPacketAtIndex(
												(unsigned int)
													scanIndex);
											*outSenderId =
												g_frontState
													.netRuntimeRecvScratchPacket
													.directPlayId;
											*outPacketSize =
												g_frontState
													.netRuntimeRecvScratchPacket
													.payloadSize;
											return g_frontState
												.netRuntimeRecvScratchPacket
												.payload;
										}
										++sequenceCursor;
										if (sequenceCursor >
										    NET_RELIABLE_SEQUENCE_LIMIT) {
											sequenceCursor =
												0;
										}
									}
								}
							}
						}
						++sequenceCursor;
						if (sequenceCursor >
						    NET_RELIABLE_SEQUENCE_LIMIT) {
							sequenceCursor = 0;
						}
					}

					if (missingPacketCount <= 0) {
						g_frontState
							.netRuntimeRecvQueue
								[scanIndex]
							.nackRetryCount = 0;
						g_frontState
							.netRuntimeRecvQueue
								[scanIndex]
							.lastNackMs = 0;
					} else {
						if (sentRetryRequest == 1) {
							++g_frontState
								  .netRuntimeRecvQueue
									  [scanIndex]
								  .nackRetryCount;
							g_frontState
								.netRuntimeRecvQueue
									[scanIndex]
								.lastNackMs = (int)
								GetTickCount();
						}
					}
				} else {
					if (g_frontState
						    .netRuntimeRecvQueueReadIndex ==
					    scanIndex) {
						if (Net_RemoveIncomingPacketAtIndex(
							    (unsigned int)
								    scanIndex) ==
						    0) {
							continue;
						}
					}
				}
			} else {
				if (Net_RemoveIncomingPacketAtIndex(
					    (unsigned int)scanIndex) == 0) {
					continue;
				}
			}
		}

		if (++scanIndex >= NET_RECV_QUEUE_CAPACITY) {
			scanIndex = 0;
		}
	}

	if (g_frontState.netRuntimeRecvQueueCount <
	    NET_RECV_QUEUE_PRESSURE_THRESHOLD) {
		return NULL;
	}
	scanIndex = g_frontState.netRuntimeRecvQueueReadIndex;
	remainingPacketCount = g_frontState.netRuntimeRecvQueueCount;
	for (; remainingPacketCount > 0; --remainingPacketCount) {
		if (g_frontState.netRuntimeRecvQueue[scanIndex].directPlayId !=
		    0) {
			receivedSequence =
				g_frontState.netRuntimeRecvQueue[scanIndex]
					.sequenceByte;
			useChannelA =
				g_frontState.netRuntimeRecvQueue[scanIndex]
					.packetClass == NET_RELIABLE_CHANNEL_A;
			useChannelB =
				g_frontState.netRuntimeRecvQueue[scanIndex]
					.packetClass == NET_RELIABLE_CHANNEL_B;
			peerIndex = Net_FindOrCreatePeerSlot(
				(int)g_frontState.netRuntimeRecvQueue[scanIndex]
					.directPlayId);
			expectedSequence = 0;
			if (peerIndex < g_frontState.netReliablePeerSlotCount &&
			    peerIndex < NET_RELIABLE_PEER_CAPACITY) {
				if (useChannelA != 0) {
					expectedSequence =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelA +
						1;
					if (expectedSequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expectedSequence = 0;
					}
				} else if (useChannelB != 0) {
					expectedSequence =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqChannelB +
						1;
					if (expectedSequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expectedSequence = 0;
					}
				} else {
					expectedSequence =
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastDeliveredSeqDefault +
						1;
					if (expectedSequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expectedSequence = 0;
					}
				}
			}
			sequenceDelta = receivedSequence - expectedSequence;
			if (peerIndex >=
				    g_frontState.netReliablePeerSlotCount ||
			    (sequenceDelta >=
				     NET_RELIABLE_PRESSURE_NEGATIVE_WINDOW &&
			     (sequenceDelta < 0 ||
			      sequenceDelta >= NET_RELIABLE_POSITIVE_WINDOW))) {
				if (Net_RemoveIncomingPacketAtIndex(
					    (unsigned int)scanIndex) == 0) {
					continue;
				}
			} else if (g_frontState.netRuntimeRecvQueue[scanIndex]
					   .nackRetryCount != 0) {
				if (useChannelA != 0) {
					sprintf(debugText, "(ROOB %u) ",
						receivedSequence);
					g_frontState
						.netRuntimeReliablePeerSlots
							[peerIndex]
						.lastDeliveredSeqChannelA =
						receivedSequence;
				} else if (useChannelB != 0) {
					sprintf(debugText, "(ROOG %u) ",
						receivedSequence);
					g_frontState
						.netRuntimeReliablePeerSlots
							[peerIndex]
						.lastDeliveredSeqChannelB =
						receivedSequence;
				} else {
					sprintf(debugText, "(ROOS %u) ",
						receivedSequence);
					g_frontState
						.netRuntimeReliablePeerSlots
							[peerIndex]
						.lastDeliveredSeqDefault =
						receivedSequence;
				}
				++g_frontState
					  .netRuntimeReliablePeerSlots
						  [peerIndex]
					  .packetCount;
				g_frontState.netRuntimeRecvScratchPacket =
					g_frontState
						.netRuntimeRecvQueue[scanIndex];
				Net_RemoveIncomingPacketAtIndex(
					(unsigned int)scanIndex);
				*outSenderId =
					g_frontState.netRuntimeRecvScratchPacket
						.directPlayId;
				*outPacketSize =
					g_frontState.netRuntimeRecvScratchPacket
						.payloadSize;
				return g_frontState.netRuntimeRecvScratchPacket
					.payload;
			}
		}
		if (++scanIndex >= NET_RECV_QUEUE_CAPACITY) {
			scanIndex = 0;
		}
	}
	return NULL;
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D1130
int *Net_WaitForAppPacket(DPID *outSenderId, uint32_t *outPacketSize,
			  int timeoutSeconds)
{
	DPID senderId;
	uint32_t packetSize;
	uint32_t startTime;
	uint32_t timeoutMs;
	int *packet;

	timeoutMs = (uint32_t)timeoutSeconds * 1000;
	startTime = GetTickCount();
	do {
		if (GetTickCount() - startTime > timeoutMs) {
			return NULL;
		}
		packet = Net_GetNextAppPacket(&senderId, &packetSize);
	} while (packet == NULL);
	*outSenderId = senderId;
	*outPacketSize = packetSize;
	return packet;
}
#endif

// FUNCTION: XVT 0x4D11A0
int Net_CountPlayersWithLowerId(DPID playerId)
{
	int lowerPlayerIdCount;
	int remainingPlayerCount;
	NetPlayerInfo *player;

	lowerPlayerIdCount = 0;
	if (g_frontState.netPlayerCount > 0) {
		player = g_frontState.netPlayers;
		remainingPlayerCount = g_frontState.netPlayerCount;
		do {
			if (player->playerId < playerId) {
				lowerPlayerIdCount++;
			}
			player++;
			remainingPlayerCount--;
		} while (remainingPlayerCount != 0);
	}

	return lowerPlayerIdCount;
}

// FUNCTION: XVT 0x4D11D0
int Net_GetHostPlayerId(void) { return g_frontState.netHostPlayerId; }

// FUNCTION: XVT 0x4D11E0
int Net_GetLocalPlayerId(void) { return g_frontState.netPlayers[0].playerId; }

// FUNCTION: XVT 0x4D1240
void Net_MarkPlayerReadyNoLock(int playerId)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < g_frontState.netPlayerCount;
	     ++playerIndex) {
		if (g_frontState.netPlayers[playerIndex].playerId ==
		    (DPID)playerId) {
			break;
		}
	}
	if (playerIndex != g_frontState.netPlayerCount) {
		g_frontState.netPlayers[playerIndex].readyFlag = 1;
	}
}

// FUNCTION: XVT 0x4D1280
void Net_ClearPlayerReadyFlag(int playerId)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < 32; playerIndex++) {
		if (g_frontState.netPlayers[playerIndex].playerId ==
		    (DPID)playerId) {
			break;
		}
	}
	if (playerIndex != 32) {
		g_frontState.netPlayers[playerIndex].readyFlag = 0;
	}
}

// FUNCTION: XVT 0x4D12B0
int Net_IsPlayerReady(int playerId)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < g_frontState.netPlayerCount;
	     ++playerIndex) {
		if (g_frontState.netPlayers[playerIndex].playerId ==
		    (DPID)playerId) {
			break;
		}
	}
	if (playerIndex == g_frontState.netPlayerCount) {
		return 0;
	}
	return g_frontState.netPlayers[playerIndex].readyFlag;
}

// FUNCTION: XVT 0x4D12F0
NetPlayerInfo *Net_FindPlayer(int playerId)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < g_frontState.netPlayerCount;
	     ++playerIndex) {
		if (g_frontState.netPlayers[playerIndex].playerId ==
		    (DPID)playerId) {
			break;
		}
	}
	if (playerIndex == g_frontState.netPlayerCount) {
		return 0;
	}
	return &g_frontState.netPlayers[playerIndex];
}

// FUNCTION: XVT 0x4D1330
int Net_SetPlayerReady(int playerId)
{
	int wasBackBufferLocked;
	int playerIndex;

	if (g_frontState.netDirectPlay == NULL) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	playerIndex = 0;
	if (g_frontState.netPlayerCount > 0) {
		do {
			if (g_frontState.netPlayers[playerIndex].playerId ==
			    (DPID)playerId) {
				break;
			}
			++playerIndex;
		} while (playerIndex < g_frontState.netPlayerCount);
	}
	if (playerIndex == g_frontState.netPlayerCount) {
		if (wasBackBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		return 0;
	}

	g_frontState.netPlayers[playerIndex].readyFlag = 1;
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 1;
}

// FUNCTION: XVT 0x4D13B0
void Net_ClearPlayerReadyFlagWithLockGuard(int playerId)
{
	int wasBackBufferLocked;

	if (g_frontState.netDirectPlay != NULL) {
		wasBackBufferLocked = g_frontState.backBufferLocked;
		FrontendDisplay_UnlockBackBuffer();
		Net_ClearPlayerReadyFlag(playerId);
		if (wasBackBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
	}
}

// FUNCTION: XVT 0x4D1400
int Net_CountReadyPlayers(void)
{
	int count = 0;
	int playerIndex;

	for (playerIndex = 0; playerIndex < 32; ++playerIndex) {
		if (g_frontState.netPlayers[playerIndex].readyFlag == 1) {
			++count;
		}
	}
	return count;
}

// FUNCTION: XVT 0x4D1420
void Net_ClearPlayerReadyFlags(void)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < 32; ++playerIndex) {
		g_frontState.netPlayers[playerIndex].readyFlag = 0;
	}
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D1440
int Net_OpenDirectPlaySession(GUID appGuid, const char *localPlayerInfo,
			      const char *localPlayerName, int isHost,
			      const char *sessionName,
			      NetworkTransportType networkType,
			      const char *connectionAddress)
{
	GUID serviceProviderGuid;
	void *connectionBuffer;
	size_t connectionBufferSize;
	DPNAME playerName;
	DPCOMPORTADDRESS serialAddress;
	GUID addressTypeGuid;
	DPLCONNECTION connection;
	DPSESSIONDESC2 session;
	char defaultSessionName[64] = "modem game";
	const GUID *selectedServiceProviderGuid;
	const char *address;
	HRESULT connectResult;

	(void)defaultSessionName;
	switch (networkType) {
	case NET_TRANSPORT_IPX:
		serviceProviderGuid = g_netDirectPlayIpxServiceProviderGuid;
		selectedServiceProviderGuid = &serviceProviderGuid;
		address = connectionAddress;
		break;
	case NET_TRANSPORT_TCPIP:
		serviceProviderGuid = g_netDirectPlayTcpIpServiceProviderGuid;
		addressTypeGuid = g_netDirectPlayInetAddressTypeGuid;
		selectedServiceProviderGuid = &serviceProviderGuid;
		address = connectionAddress;
		break;
	case NET_TRANSPORT_MODEM:
		serviceProviderGuid = g_netDirectPlayModemServiceProviderGuid;
		addressTypeGuid = g_netDirectPlayPhoneAddressTypeGuid;
		selectedServiceProviderGuid = &serviceProviderGuid;
		address = connectionAddress;
		break;
	case NET_TRANSPORT_SERIAL:
		serviceProviderGuid = g_netDirectPlaySerialServiceProviderGuid;
		serialAddress.dwComPort = 2;
		serialAddress.dwBaudRate = 9600;
		serialAddress.dwFlowControl = 1;
		addressTypeGuid = g_netDirectPlayComPortAddressTypeGuid;
		selectedServiceProviderGuid = &serviceProviderGuid;
		address = (const char *)&serialAddress;
		serialAddress.dwStopBits = 0;
		serialAddress.dwParity = 0;
		break;
	default:
		address = connectionAddress;
		break;
	}

	if (DirectPlayLobbyCreateA(NULL, &g_frontState.netDirectPlayLobby, NULL,
				   NULL, 0) != 0) {
		return 0;
	}
	if (Net_BuildDirectPlayAddress(
		    g_frontState.netDirectPlayLobby,
		    selectedServiceProviderGuid, &addressTypeGuid, address,
		    &connectionBuffer, &connectionBufferSize) != 0) {
		g_frontState.netDirectPlayLobby->lpVtbl->Release(
			g_frontState.netDirectPlayLobby);
		g_frontState.netDirectPlayLobby = NULL;
		return 0;
	}

	memset(&session, 0, sizeof(session));
	session.dwSize = sizeof(session);
	session.dwFlags = 0;
	session.guidInstance = g_netLobbySessionInstanceGuid;
	session.guidApplication = appGuid;
	session.dwMaxPlayers = 16;
	session.dwCurrentPlayers = 0;
	session.lpszSessionNameA = (char *)sessionName;
	session.lpszPasswordA = NULL;
	session.dwReserved1 = 0;
	session.dwReserved2 = 0;
	session.dwUser1 = 0;
	session.dwUser2 = 0;
	session.dwUser3 = 0;
	session.dwUser4 = 0;

	memset(&playerName, 0, sizeof(playerName));
	playerName.dwSize = sizeof(playerName);
	playerName.dwFlags = 0;
	playerName.lpszShortNameA = (char *)localPlayerName;
	playerName.lpszLongNameA = (char *)localPlayerInfo;

	memset(&connection, 0, sizeof(connection));
	connection.dwSize = sizeof(connection);
	connection.dwFlags = 2;
	if (isHost == 0) {
		connection.dwFlags = 1;
	}
	connection.lpSessionDesc = &session;
	connection.lpPlayerName = &playerName;
	connection.guidSP = serviceProviderGuid;
	connection.lpAddress = connectionBuffer;
	connection.dwAddressSize = connectionBufferSize;
	if (g_frontState.netDirectPlayLobby->lpVtbl->SetConnectionSettings(
		    g_frontState.netDirectPlayLobby, 0, 0, &connection) != 0) {
		g_frontState.netDirectPlayLobby->lpVtbl->Release(
			g_frontState.netDirectPlayLobby);
		g_frontState.netDirectPlayLobby = NULL;
		return 0;
	}

	connectResult = g_frontState.netDirectPlayLobby->lpVtbl->Connect(
		g_frontState.netDirectPlayLobby, 0, &g_frontState.netDirectPlay,
		NULL);
	g_frontState.netDirectPlayLobby->lpVtbl->Release(
		g_frontState.netDirectPlayLobby);
	g_frontState.netDirectPlayLobby = NULL;
	return connectResult == 0;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D1840
HRESULT Net_BuildDirectPlayAddress(IDirectPlayLobbyA *directPlayLobby,
				   const GUID *serviceProviderGuid,
				   const GUID *addressTypeGuid,
				   const char *address,
				   void **outConnectionBuffer,
				   size_t *outConnectionBufferSize)
{
	void *connectionBuffer;
	HRESULT result;
	uint32_t addressSize;
	const char *addressValue;
	IDirectPlayLobbyA *lobby;

	connectionBuffer = NULL;
	addressSize = 0;
	if (memcmp(addressTypeGuid, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
		   sizeof(GUID)) == 0) {
		return DX_E_INVALIDARG;
	}
	addressValue = address;
	lobby = directPlayLobby;
	result = lobby->lpVtbl->CreateAddress(
		lobby, serviceProviderGuid, addressTypeGuid, addressValue,
		strlen(addressValue) + 1, NULL, &addressSize);
	if (result == (HRESULT)0x8877001E) {
		connectionBuffer = GlobalLock(GlobalAlloc(0x42, addressSize));
		if (connectionBuffer == NULL) {
			result = (HRESULT)0x8007000E;
		} else {
			result = lobby->lpVtbl->CreateAddress(
				lobby, serviceProviderGuid, addressTypeGuid,
				addressValue, strlen(addressValue) + 1,
				connectionBuffer, &addressSize);
			if (result >= 0) {
				*outConnectionBuffer = connectionBuffer;
				*outConnectionBufferSize = addressSize;
				return 0;
			}
		}
	}
	if (connectionBuffer != NULL) {
		GlobalUnlock(GlobalHandle(connectionBuffer));
		GlobalFree(GlobalHandle(connectionBuffer));
	}
	return result;
}
#endif

// FUNCTION: XVT 0x4D1940
int NetSession_ImportRuntimeState(
	void **dplayInterfaceOut, GUID *appGuidOut, GUID *sessionGuidOut,
	int32_t *groupIdOut, int *hostPlayerIdOut,
	NetPlayerInfo *localPlayerInfoOut, NetQueuedPacket *recvQueueOut,
	int32_t *recvQueueReadOut, int *recvQueueCountOut,
	int *recvQueueWriteOut, NetReliablePeerSlot *reliablePeerSlotsOut,
	uint32_t *reliablePeerSlotCountOut, uint32_t *broadcastSeqCounterOut,
	char *broadcastPayloadOut, int *broadcastPayloadLengthOut,
	int *broadcastPiggybackEmptyOut, uint32_t *groupSeqCounterOut,
	char *groupPayloadOut, int *groupPayloadLengthOut,
	int *groupPiggybackEmptyOut, NetQueuedPacket *sentHistoryOut,
	int *sentHistoryWriteIndexOut)
{
	int packetIndex;
	int queueIndex;

	*dplayInterfaceOut = g_frontState.netDirectPlay;
	*appGuidOut = g_frontState.netAppGuid;
	*sessionGuidOut = g_frontState.netJoinedSessionGuid;
	*groupIdOut = g_frontState.netGroupDplayId;
	*hostPlayerIdOut = g_frontState.netHostPlayerId;
	*localPlayerInfoOut = g_frontState.netRuntimeLocalPlayer;
	*recvQueueCountOut = g_frontState.netRuntimeRecvQueueCount;
	*recvQueueReadOut = g_frontState.netRuntimeRecvQueueReadIndex;
	*recvQueueWriteOut = g_frontState.netRuntimeRecvQueueWriteIndex;
	queueIndex = g_frontState.netRuntimeRecvQueueReadIndex;
	for (packetIndex = 0;
	     packetIndex < g_frontState.netRuntimeRecvQueueCount;
	     packetIndex++) {
		memcpy(&recvQueueOut[queueIndex],
		       &g_frontState.netRuntimeRecvQueue[queueIndex],
		       sizeof(recvQueueOut[queueIndex]));
		queueIndex++;
		if (queueIndex >= 1024) {
			queueIndex = 0;
		}
	}

	*reliablePeerSlotCountOut = g_frontState.netReliablePeerSlotCount;
	for (packetIndex = 0;
	     packetIndex < (int)g_frontState.netReliablePeerSlotCount;
	     packetIndex++) {
		memcpy(&reliablePeerSlotsOut[packetIndex],
		       &g_frontState.netRuntimeReliablePeerSlots[packetIndex],
		       sizeof(reliablePeerSlotsOut[packetIndex]));
	}

	*broadcastSeqCounterOut = g_frontState.netRuntimeBroadcastSeqCounter;
	memcpy(broadcastPayloadOut,
	       g_frontState.netRuntimeBroadcastPendingPayload.payload,
	       sizeof(g_frontState.netRuntimeBroadcastPendingPayload.payload));
	*broadcastPayloadLengthOut =
		g_frontState.netRuntimeBroadcastPendingPayload.payloadLength;
	*broadcastPiggybackEmptyOut =
		g_frontState.netRuntimeBroadcastPendingPayload.piggybackEmpty;
	*groupSeqCounterOut = g_frontState.netRuntimeGroupSeqCounter;
	memcpy(groupPayloadOut,
	       g_frontState.netRuntimeGroupPendingPayload.payload,
	       sizeof(g_frontState.netRuntimeGroupPendingPayload.payload));
	*groupPayloadLengthOut =
		g_frontState.netRuntimeGroupPendingPayload.payloadLength;
	*groupPiggybackEmptyOut =
		g_frontState.netRuntimeGroupPendingPayload.piggybackEmpty;

	memset(sentHistoryOut, 0, sizeof(g_frontState.netRuntimeSentHistory));
	for (packetIndex = 0; packetIndex < 128; packetIndex++) {
		memcpy(&sentHistoryOut[packetIndex],
		       &g_frontState.netRuntimeSentHistory[packetIndex],
		       sizeof(sentHistoryOut[packetIndex]));
	}
	*sentHistoryWriteIndexOut =
		g_frontState.netRuntimeSentHistoryWriteIndex;
	return 1;
}

// FUNCTION: XVT 0x4D1B10
int NetSession_ExportRuntimeState(
	void **dplayInterface, const void *appGuid, const void *sessionGuid,
	int *groupId, int *hostPlayerId, const void *localPlayerInfo,
	const NetQueuedPacket *recvQueueEntries, int *recvQueueRead,
	int *recvQueueCount, int *recvQueueWrite,
	const NetReliablePeerSlot *reliablePeerSlots,
	int *reliablePeerSlotCount, int *broadcastSeqCounter,
	const void *broadcastPayload, int *broadcastPayloadLength,
	int *broadcastPiggybackEmpty, int *groupSeqCounter,
	const void *groupPayload, int *groupPayloadLength,
	int *groupPiggybackEmpty, const NetQueuedPacket *sentHistory,
	int *sentHistoryWriteIndex, NetQueuedPacket *sentWorldMessageHistory,
	int *sentWorldMessageWriteIndex)
{
	int queueIndex;
	int packetIndex;
	int peerIndex;

	(void)dplayInterface;
	(void)appGuid;
	(void)sessionGuid;
	(void)groupId;
	(void)hostPlayerId;

	memcpy(&g_frontState.netRuntimeLocalPlayer, localPlayerInfo,
	       sizeof(g_frontState.netRuntimeLocalPlayer));
	g_frontState.netRuntimeRecvQueueCount = *recvQueueCount;
	queueIndex = *recvQueueRead;
	g_frontState.netRuntimeRecvQueueReadIndex = *recvQueueRead;
	g_frontState.netRuntimeRecvQueueWriteIndex = *recvQueueWrite;
	for (packetIndex = 0;
	     packetIndex < g_frontState.netRuntimeRecvQueueCount;
	     ++packetIndex) {
		memcpy(&g_frontState.netRuntimeRecvQueue[queueIndex],
		       &recvQueueEntries[queueIndex],
		       sizeof(g_frontState.netRuntimeRecvQueue[queueIndex]));
		++queueIndex;
		if (queueIndex >= 1024) {
			queueIndex = 0;
		}
	}

	g_frontState.netReliablePeerSlotCount = *reliablePeerSlotCount;
	for (peerIndex = 0;
	     peerIndex < (int)g_frontState.netReliablePeerSlotCount;
	     ++peerIndex) {
		memcpy(&g_frontState.netRuntimeReliablePeerSlots[peerIndex],
		       &reliablePeerSlots[peerIndex],
		       sizeof(g_frontState
				      .netRuntimeReliablePeerSlots[peerIndex]));
		g_frontState.netRuntimeReliablePeerSlots[peerIndex]
			.lastHeardMs = GetTickCount();
	}

	g_frontState.netRuntimeBroadcastSeqCounter = *broadcastSeqCounter;
	memcpy(g_frontState.netRuntimeBroadcastPendingPayload.payload,
	       broadcastPayload,
	       sizeof(g_frontState.netRuntimeBroadcastPendingPayload.payload));
	g_frontState.netRuntimeBroadcastPendingPayload.payloadLength =
		*broadcastPayloadLength;
	g_frontState.netRuntimeBroadcastPendingPayload.piggybackEmpty =
		*broadcastPiggybackEmpty;
	g_frontState.netRuntimeGroupSeqCounter = *groupSeqCounter;
	memcpy(g_frontState.netRuntimeGroupPendingPayload.payload, groupPayload,
	       sizeof(g_frontState.netRuntimeGroupPendingPayload.payload));
	g_frontState.netRuntimeGroupPendingPayload.payloadLength =
		*groupPayloadLength;
	g_frontState.netRuntimeGroupPendingPayload.piggybackEmpty =
		*groupPiggybackEmpty;

	memset(g_frontState.netRuntimeSentHistory, 0,
	       sizeof(g_frontState.netRuntimeSentHistory));
	for (packetIndex = 0; packetIndex < 128; ++packetIndex) {
		memcpy(&g_frontState.netRuntimeSentHistory[packetIndex],
		       &sentHistory[packetIndex],
		       sizeof(g_frontState.netRuntimeSentHistory[packetIndex]));
	}
	g_frontState.netRuntimeSentHistoryWriteIndex = *sentHistoryWriteIndex;
	g_frontState.netExportRecvQueuePtr = sentWorldMessageHistory;
	g_frontState.netExportRecvQueueHighWater = *sentWorldMessageWriteIndex;
	return 1;
}

// FUNCTION: XVT 0x4D1CA0
int Net_CompactReliablePeerSlotsForRoster(void)
{
	int playerCount;
	NetPlayerInfo *playerRoster;
	int slotIndex;
	NetReliablePeerSlot *slot;
	int playerIndex;
	NetPlayerInfo *rosterPlayer;
	int nextSlotIndex;
	NetReliablePeerSlot *nextSlot;

	slotIndex = 0;
	playerRoster = Net_GetPlayerRoster(&playerCount);
	if ((int)g_frontState.netReliablePeerSlotCount > 0) {
		slot = g_frontState.netRuntimeReliablePeerSlots;
		do {
			playerIndex = 0;
			if (playerCount > 0) {
				rosterPlayer = playerRoster;
				do {
					if (rosterPlayer->playerId ==
					    slot->directPlayId) {
						break;
					}
					++rosterPlayer;
					++playerIndex;
				} while (playerIndex < playerCount);
			}

			if (playerIndex >= playerCount &&
			    slot->directPlayId !=
				    g_frontState.netGroupDplayId) {
				slot->directPlayId = 0;
				slot->lastDeliveredSeqDefault = 127;
				slot->lastDeliveredSeqChannelA = 127;
				slot->lastDeliveredSeqChannelB = 127;
				slot->recvSeqDefault = 127;
				slot->recvSeqChannelA = 127;
				slot->recvSeqChannelB = 127;
				slot->sendSeq = 0;
				slot->lastPiggybackType = NET_PACKET_NOP;
				slot->piggybackLength = 1;
				slot->lastActivityMs = 0;
				slot->lastHeardMs = 0;
				slot->packetCount = 0;
				slot->packetDropCount = 0;
				slot->packetRetryCount = 0;
			}
			++slot;
			++slotIndex;
		} while ((int)g_frontState.netReliablePeerSlotCount >
			 slotIndex);
	}

	slotIndex = 0;
	if ((int)(g_frontState.netReliablePeerSlotCount - 1) > 0) {
		slot = g_frontState.netRuntimeReliablePeerSlots;
		do {
			if (slot->directPlayId == 0) {
				nextSlotIndex = slotIndex + 1;
				if (nextSlotIndex <
				    (int)g_frontState
					    .netReliablePeerSlotCount) {
					nextSlot =
						&g_frontState
							 .netRuntimeReliablePeerSlots
								 [nextSlotIndex];
					while (nextSlot->directPlayId == 0) {
						++nextSlot;
						++nextSlotIndex;
						if (nextSlotIndex >=
						    (int)g_frontState
							    .netReliablePeerSlotCount) {
							break;
						}
					}
					if (nextSlotIndex <
					    (int)g_frontState
						    .netReliablePeerSlotCount) {
						memcpy(slot,
						       &g_frontState.netRuntimeReliablePeerSlots
								[nextSlotIndex],
						       sizeof(*slot));
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.directPlayId = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.lastDeliveredSeqDefault =
							127;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.lastDeliveredSeqChannelA =
							127;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.lastDeliveredSeqChannelB =
							127;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.recvSeqDefault = 127;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.recvSeqChannelA = 127;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.recvSeqChannelB = 127;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.sendSeq = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.lastPiggybackType =
							NET_PACKET_NOP;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.piggybackLength = 1;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.lastActivityMs = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.lastHeardMs = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.packetCount = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.packetDropCount = 0;
						g_frontState
							.netRuntimeReliablePeerSlots
								[nextSlotIndex]
							.packetRetryCount = 0;
					}
				}
			}
			++slot;
			++slotIndex;
		} while ((int)(g_frontState.netReliablePeerSlotCount - 1) >
			 slotIndex);
	}

	g_frontState.netReliablePeerSlotCount = 0;
	for (slot = g_frontState.netRuntimeReliablePeerSlots;
	     slot < &g_frontState.netRuntimeReliablePeerSlots[40]; ++slot) {
		if (slot->directPlayId != 0) {
			++g_frontState.netReliablePeerSlotCount;
		}
	}
	return 1;
}

// FUNCTION: XVT 0x4D1EA0
int Net_SendSequenceKeepalives(void)
{
	int playerCount;
	int packet[128];
	NetPlayerInfo *playerRoster;
	unsigned int playerIndex;

	playerIndex = 0;
	playerRoster = Net_GetPlayerRoster(&playerCount);
	if ((unsigned int)playerCount > 0) {
		do {
			unsigned int nowMs;

			nowMs = GetTickCount();
			if (g_frontState.netRuntimeLocalPlayer.playerId !=
			    playerRoster[playerIndex].playerId) {
				unsigned int peerIndex;

				peerIndex = Net_FindOrCreatePeerSlot(
					playerRoster[playerIndex].playerId);
				if (g_frontState.netReliablePeerSlotCount >
					    peerIndex &&
				    peerIndex < 40) {
					unsigned int sequence;
					if (nowMs - g_frontState
							    .netRuntimeReliablePeerSlots
								    [peerIndex]
							    .lastActivityMs >
					    3000) {
						packet[0] =
							NET_PACKET_KEEPALIVE;
						g_frontState
							.netRuntimeReliablePeerSlots
								[peerIndex]
							.lastActivityMs = nowMs;
						sequence =
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqChannelA +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[1] = sequence;
						sequence =
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqChannelB +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[2] = sequence;
						sequence =
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.recvSeqDefault +
							1;
						if (sequence > 127) {
							sequence = 0;
						}
						packet[3] = sequence;
						packet[4] = GetTickCount();
						((int (*)(int, const void *,
							  int, int))
							 Net_SendDirectPlayPacket)(
							g_frontState
								.netRuntimeReliablePeerSlots
									[peerIndex]
								.directPlayId,
							packet, 20, 0);
					}
				}
			}
			++playerIndex;
		} while (playerIndex < (unsigned int)playerCount);
	}
	return 1;
}

// FUNCTION: XVT 0x4D1FA0
int Net_CheckAndRecordIncomingSequence(int playerId, int sequenceId,
				       int useChannel0, int useChannel2)
{
	unsigned int previousPeerSlotCount;
	unsigned int peerIndex;
	NetReliablePeerSlot *peer;
	int previousSequence;
	int sequenceDelta;

	previousPeerSlotCount = g_frontState.netReliablePeerSlotCount;
	peerIndex = Net_FindOrCreatePeerSlot(playerId);
	if (previousPeerSlotCount != g_frontState.netReliablePeerSlotCount ||
	    peerIndex >= 40) {
		return 0;
	}

	if (useChannel0 != 0) {
		peer = &g_frontState.netRuntimeReliablePeerSlots[peerIndex];
		previousSequence = peer->recvSeqChannelA;
	} else if (useChannel2 != 0) {
		peer = &g_frontState.netRuntimeReliablePeerSlots[peerIndex];
		previousSequence = peer->recvSeqChannelB;
	} else {
		peer = &g_frontState.netRuntimeReliablePeerSlots[peerIndex];
		previousSequence = peer->recvSeqDefault;
	}

	sequenceDelta = sequenceId - previousSequence;
	if (sequenceDelta >= -64 &&
	    (sequenceDelta <= 0 || sequenceDelta >= 64)) {
		return 1;
	}

	if (useChannel0 != 0) {
		peer->recvSeqChannelA = sequenceId;
	} else if (useChannel2 != 0) {
		peer->recvSeqChannelB = sequenceId;
	} else {
		peer->recvSeqDefault = sequenceId;
	}
	return 0;
}

// FUNCTION: XVT 0x4D2080
int Net_FindQueuedSequencedPacket(int unusedQueueIndex, int sequenceId,
				  int useChannel0, int useChannel2,
				  int peerSlotIndex)
{
	int remaining;
	int queueIndex;
	int sequenceByte;
	int isClass0;
	int isClass2;
	unsigned int slot;
	DPID directPlayId;
	NetReliablePeerSlot *peer;

	(void)unusedQueueIndex;

	queueIndex = g_frontState.netRuntimeRecvQueueReadIndex;
	for (remaining = g_frontState.netRuntimeRecvQueueCount; remaining != 0;
	     --remaining) {
		if (g_frontState.netRuntimeRecvQueue[queueIndex].isResentCopy !=
		    0) {
			directPlayId =
				g_frontState.netRuntimeRecvQueue[queueIndex]
					.directPlayId;
			sequenceByte =
				g_frontState.netRuntimeRecvQueue[queueIndex]
					.sequenceByte;
			isClass0 = g_frontState.netRuntimeRecvQueue[queueIndex]
					   .packetClass == 0;
			isClass2 = g_frontState.netRuntimeRecvQueue[queueIndex]
					   .packetClass == 2;
			slot = 0;
			if (g_frontState.netReliablePeerSlotCount > slot) {
				peer = g_frontState.netRuntimeReliablePeerSlots;
				for (;;) {
					if (peer->directPlayId ==
					    directPlayId) {
						break;
					}
					++peer;
					++slot;
					if (g_frontState
						    .netReliablePeerSlotCount <=
					    slot) {
						break;
					}
				}
			}
			if (slot == (unsigned int)peerSlotIndex) {
				if (useChannel0 != 0) {
					if (isClass0 &&
					    sequenceByte == sequenceId) {
						return queueIndex;
					}
				} else if (useChannel2 != 0) {
					if (isClass2 &&
					    sequenceByte == sequenceId) {
						return queueIndex;
					}
				} else if (!isClass0 && !isClass2 &&
					   sequenceByte == sequenceId) {
					return queueIndex;
				}
			}
		}
		if ((unsigned int)++queueIndex >= 1024) {
			queueIndex = 0;
		}
	}
	return -1;
}

// FUNCTION: XVT 0x4D2170
int Net_RemoveIncomingPacketAtIndex(unsigned int queueIndex)
{
	if (g_frontState.netRuntimeRecvQueueReadIndex == (int)queueIndex) {
		++g_frontState.netRuntimeRecvQueueReadIndex;
		--g_frontState.netRuntimeRecvQueueCount;
		if (g_frontState.netRuntimeRecvQueueReadIndex >= 1024) {
			g_frontState.netRuntimeRecvQueueReadIndex = 0;
		}
		return 1;
	}

	{
		unsigned int destinationIndex;
		unsigned int nextIndex;
		unsigned int endIndex;

		destinationIndex = queueIndex;
		nextIndex = queueIndex + 1;
		if (nextIndex >= 1024) {
			nextIndex = 0;
		}
		endIndex =
			(unsigned int)g_frontState.netRuntimeRecvQueueReadIndex;
		endIndex += (unsigned int)g_frontState.netRuntimeRecvQueueCount;
		if (endIndex >= 1024) {
			endIndex -= 1024;
		}
		while (nextIndex != endIndex) {
			memcpy(&g_frontState
					.netRuntimeRecvQueue[destinationIndex],
			       &g_frontState.netRuntimeRecvQueue[nextIndex],
			       sizeof(g_frontState.netRuntimeRecvQueue
					      [destinationIndex]));
			++destinationIndex;
			if (destinationIndex >= 1024) {
				destinationIndex = 0;
			}
			++nextIndex;
			if (nextIndex >= 1024) {
				nextIndex = 0;
			}
		}
	}

	--g_frontState.netRuntimeRecvQueueCount;
	if (g_frontState.netRuntimeRecvQueueWriteIndex == 0) {
		g_frontState.netRuntimeRecvQueueWriteIndex = 1023;
	} else {
		--g_frontState.netRuntimeRecvQueueWriteIndex;
	}
	return 0;
}

// FUNCTION: XVT 0x4D2250
unsigned int Net_GetAverageLatencyMs(int playerId)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < 40; playerIndex++) {
		if (g_netPlayerConnectionStats[playerIndex].playerId ==
		    playerId) {
			if (g_netPlayerConnectionStats[playerIndex]
				    .latencySampleCount == 0) {
				return 1;
			}
			return g_netPlayerConnectionStats[playerIndex]
				       .latencyTotalMs /
			       g_netPlayerConnectionStats[playerIndex]
				       .latencySampleCount;
		}
	}
	return 0;
}

// FUNCTION: XVT 0x4D2290
int Net_SetPlayerLatencyMs(int playerId, int latencyMs)
{
	int playerIndex;

	playerIndex = 0;
	while (g_netPlayerConnectionStats[playerIndex].playerId != playerId &&
	       g_netPlayerConnectionStats[playerIndex].playerId != 0) {
		playerIndex++;
		if (playerIndex >= 40) {
			return 1;
		}
	}

	{
		NetPlayerConnectionStats *stats =
			&g_netPlayerConnectionStats[playerIndex];
		stats->playerId = playerId;
		stats->latencySampleCount = 1;
		stats->latencyTotalMs = latencyMs;
		return stats->latencySampleCount;
	}
}

// FUNCTION: XVT 0x4D22E0
int Net_SetPlayerNameWithLockGuard(unsigned int playerId, const char *longName,
				   const char *shortName)
{
	int wasBackBufferLocked;
	DPNAME playerName;
	HRESULT result;

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.netDirectPlay == NULL) {
		return 0;
	}

	memset(&playerName, 0, sizeof(playerName));
	playerName.lpszShortNameA = (char *)shortName;
	playerName.lpszLongNameA = (char *)longName;
	playerName.dwSize = sizeof(playerName);
	result = g_frontState.netDirectPlay->lpVtbl->SetPlayerName(
		g_frontState.netDirectPlay, playerId, &playerName, 0);

	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
#ifdef XVT_MODERN
	if (result == DPERR_PENDING) {
		return XVT_NETWORK_PENDING;
	}
#endif
	return result == 0;
}

// FUNCTION: XVT 0x4D2370
int Net_RefreshPlayerRosterWithLockGuard(void)
{
	int wasBackBufferLocked;

	if (g_frontState.netDirectPlay == NULL) {
		return 0;
	}
	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	g_frontState.netPlayerCount = 1;
	Net_RefreshPlayerRoster();
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 1;
}

// FUNCTION: XVT 0x4D23B0
unsigned int Net_FindOrCreatePeerSlot(int directPlayId)
{
	unsigned int slot;
	NetReliablePeerSlot *peers;
	char message[256];

	slot = 0;
	peers = g_frontState.netRuntimeReliablePeerSlots;
	if (g_frontState.netReliablePeerSlotCount > slot) {
		do {
			if (peers[slot].directPlayId == (DPID)directPlayId) {
				return slot;
			}
			++slot;
			if (slot < g_frontState.netReliablePeerSlotCount) {
				continue;
			}
			break;
		} while (1);
	}

	if (g_frontState.netReliablePeerSlotCount == slot && slot < 40) {
		g_frontState.netRuntimeReliablePeerSlots[slot].directPlayId =
			directPlayId;
		g_frontState.netRuntimeReliablePeerSlots[slot]
			.lastDeliveredSeqDefault = 127;
		g_frontState.netRuntimeReliablePeerSlots[slot]
			.lastDeliveredSeqChannelA = 127;
		g_frontState.netRuntimeReliablePeerSlots[slot]
			.lastDeliveredSeqChannelB = 127;
		g_frontState.netRuntimeReliablePeerSlots[slot].recvSeqDefault =
			127;
		g_frontState.netRuntimeReliablePeerSlots[slot].recvSeqChannelA =
			127;
		g_frontState.netRuntimeReliablePeerSlots[slot].recvSeqChannelB =
			127;
		g_frontState.netRuntimeReliablePeerSlots[slot].sendSeq = 0;
		g_frontState.netRuntimeReliablePeerSlots[slot]
			.lastPiggybackType = NET_PACKET_NOP;
		g_frontState.netRuntimeReliablePeerSlots[slot].piggybackLength =
			1;
		g_frontState.netRuntimeReliablePeerSlots[slot].lastActivityMs =
			GetTickCount();
		g_frontState.netRuntimeReliablePeerSlots[slot].lastHeardMs =
			GetTickCount();
		g_frontState.netRuntimeReliablePeerSlots[slot].packetCount = 0;
		g_frontState.netRuntimeReliablePeerSlots[slot].packetDropCount =
			0;
		g_frontState.netRuntimeReliablePeerSlots[slot]
			.packetRetryCount = 0;
		++g_frontState.netReliablePeerSlotCount;
		sprintf(message, "SAdding new net sequence %u\n", directPlayId);
	}
	return slot;
}

/* Uses Net_FindOrCreatePeerSlot, so asking about an unknown player adds a reliable peer slot for it. */
// FUNCTION: XVT 0x4D24C0
int Net_GetPacketDropRateBasisPoints(int playerId)
{
	int result;

	if (g_frontState.netIsHost != 0) {
		unsigned int peerIndex;
		int packetCount;
		int weightedDropCount;
		int playerIndex;

		peerIndex = Net_FindOrCreatePeerSlot(playerId);
		if (g_frontState.netReliablePeerSlotCount > peerIndex &&
		    peerIndex < 40) {
			packetCount =
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.packetCount;
			weightedDropCount =
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.packetDropCount +
				2 * g_frontState.netRuntimeReliablePeerSlots
						[peerIndex]
							.packetRetryCount;
		} else {
			packetCount = 0;
			weightedDropCount = 0;
		}
		for (playerIndex = 0; playerIndex < 40; ++playerIndex) {
			if (g_netPlayerConnectionStats[playerIndex].playerId ==
			    playerId) {
				packetCount +=
					g_netPlayerConnectionStats[playerIndex]
						.packetCount;
				weightedDropCount +=
					g_netPlayerConnectionStats[playerIndex]
						.packetDropCount +
					2 * g_netPlayerConnectionStats
							[playerIndex]
								.packetRetryCount;
			}
		}
		if (packetCount == 0) {
			packetCount = 1;
		}
		result = weightedDropCount * 10000 / packetCount;
		if (result > 10000) {
			result = 10000;
		}
	} else {
		int playerIndex;
		unsigned int packetCount;

		playerIndex = 0;
		while (g_netPlayerConnectionStats[playerIndex].playerId !=
		       playerId) {
			++playerIndex;
			if (playerIndex >= 40) {
				return 0;
			}
		}
		packetCount =
			(unsigned int)g_netPlayerConnectionStats[playerIndex]
				.packetCount;
		if (packetCount == 0) {
			packetCount = 1;
		}
		result = (g_netPlayerConnectionStats[playerIndex]
				  .packetDropCount +
			  2 * g_netPlayerConnectionStats[playerIndex]
					  .packetRetryCount) *
			 10000u / packetCount;
		if (result > 10000) {
			result = 10000;
		}
	}
	return result;
}

// FUNCTION: XVT 0x4D25D0
int Net_DropSilentPeers(void)
{
	enum {
		PEER_SILENCE_TIMEOUT_MS = 45000,
		RECV_QUEUE_CAPACITY = 1024,
		MAX_SEQUENCE = 127,
	};

	int packet[2];
	int playerIndex;

	if (g_frontState.netIsHost != 0) {
		for (playerIndex = 0;
		     playerIndex < (int)(sizeof(g_frontState.netPlayers) /
					 sizeof(g_frontState.netPlayers[0]));
		     ++playerIndex) {
			unsigned int peerIndex;
			unsigned int sequence;
			unsigned int nowMs;

			if (g_frontState.netPlayers[playerIndex].playerId ==
				    0 ||
			    g_frontState.netPlayers[playerIndex].playerId ==
				    g_frontState.netRuntimeLocalPlayer
					    .playerId ||
			    g_frontState.netPlayers[playerIndex].playerId ==
				    g_frontState.netGroupDplayId ||
			    g_frontState.netPlayers[playerIndex].readyFlag ==
				    0) {
				continue;
			}
			peerIndex = Net_FindOrCreatePeerSlot(
				g_frontState.netPlayers[playerIndex].playerId);
			if (g_frontState.netReliablePeerSlotCount <=
			    peerIndex) {
				continue;
			}
			nowMs = GetTickCount();
			if (nowMs - g_frontState
					    .netRuntimeReliablePeerSlots
						    [peerIndex]
					    .lastHeardMs <=
			    PEER_SILENCE_TIMEOUT_MS) {
				continue;
			}
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.lastHeardMs = GetTickCount();
			if (g_frontState.netRuntimeRecvQueueCount >=
			    RECV_QUEUE_CAPACITY) {
				continue;
			}
			packet[0] = NET_PACKET_PLAYER_KICKED;
			Net_SendPacketAndFlush(
				g_frontState.netPlayers[playerIndex].playerId,
				packet, sizeof(packet[0]));
			*(int *)g_frontState
				 .netRuntimeRecvQueue
					 [g_frontState
						  .netRuntimeRecvQueueWriteIndex]
				 .payload = NET_PACKET_PLAYER_LEFT;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.directPlayId =
				g_frontState.netPlayers[playerIndex].playerId;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.payloadSize = sizeof(int);
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.lastNackMs = 0;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.nackRetryCount = 0;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.isResentCopy = 0;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.packetClass = 1;
			sequence =
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.recvSeqDefault +
				1;
			if (sequence > MAX_SEQUENCE) {
				sequence = 0;
			}
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.recvSeqDefault = sequence;
			g_frontState
				.netRuntimeRecvQueue
					[g_frontState
						 .netRuntimeRecvQueueWriteIndex]
				.sequenceByte = (uint8_t)sequence;
			++g_frontState.netRuntimeRecvQueueCount;
			if (++g_frontState.netRuntimeRecvQueueWriteIndex >=
			    RECV_QUEUE_CAPACITY) {
				g_frontState.netRuntimeRecvQueueWriteIndex = 0;
			}
		}
	} else {
		unsigned int peerIndex =
			Net_FindOrCreatePeerSlot(g_frontState.netHostPlayerId);
		unsigned int sequence;
		unsigned int nowMs;

		if (g_frontState.netReliablePeerSlotCount <= peerIndex) {
			return 0;
		}
		nowMs = GetTickCount();
		if (nowMs - g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				    .lastHeardMs >
		    PEER_SILENCE_TIMEOUT_MS) {
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.lastHeardMs = GetTickCount();
			if (g_frontState.netRuntimeRecvQueueCount <
			    RECV_QUEUE_CAPACITY) {
				*(int *)g_frontState
					 .netRuntimeRecvQueue
						 [g_frontState
							  .netRuntimeRecvQueueWriteIndex]
					 .payload = NET_PACKET_HOST_CANCELLED;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.directPlayId =
					g_frontState.netHostPlayerId;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.payloadSize = sizeof(int);
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.lastNackMs = 0;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.nackRetryCount = 0;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.isResentCopy = 0;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.packetClass = 0;
				sequence = g_frontState
						   .netRuntimeReliablePeerSlots
							   [peerIndex]
						   .recvSeqChannelA +
					   1;
				if (sequence > MAX_SEQUENCE) {
					sequence = 0;
				}
				g_frontState
					.netRuntimeReliablePeerSlots[peerIndex]
					.recvSeqChannelA = sequence;
				g_frontState
					.netRuntimeRecvQueue
						[g_frontState
							 .netRuntimeRecvQueueWriteIndex]
					.sequenceByte = (uint8_t)sequence;
				++g_frontState.netRuntimeRecvQueueCount;
				if (++g_frontState
					      .netRuntimeRecvQueueWriteIndex >=
				    RECV_QUEUE_CAPACITY) {
					g_frontState
						.netRuntimeRecvQueueWriteIndex =
						0;
				}
			}
		}
	}
	return 1;
}

/* Uses Net_FindOrCreatePeerSlot, so asking about an unknown player adds a reliable peer slot for it. */
// FUNCTION: XVT 0x4D28F0
int Net_GetPlayerPacketCount(int playerId)
{
	unsigned int peerIndex;
	int packetCount;
	int playerIndex;

	peerIndex = Net_FindOrCreatePeerSlot(playerId);
	if (g_frontState.netReliablePeerSlotCount > peerIndex &&
	    peerIndex < 40) {
		packetCount =
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.packetCount;
	} else {
		packetCount = 0;
	}
	for (playerIndex = 0; playerIndex < 40; ++playerIndex) {
		if (g_netPlayerConnectionStats[playerIndex].playerId ==
		    playerId) {
			return packetCount +
			       g_netPlayerConnectionStats[playerIndex]
				       .packetCount;
		}
	}
	return packetCount;
}

/* Uses Net_FindOrCreatePeerSlot, so asking about an unknown player adds a reliable peer slot for it. */
// FUNCTION: XVT 0x4D2950
int Net_GetPlayerPacketDropCount(int playerId)
{
	unsigned int peerIndex;
	int packetDropCount;
	int playerIndex;

	peerIndex = Net_FindOrCreatePeerSlot(playerId);
	if (g_frontState.netReliablePeerSlotCount > peerIndex &&
	    peerIndex < 40) {
		packetDropCount =
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.packetDropCount;
	} else {
		packetDropCount = 0;
	}
	for (playerIndex = 0; playerIndex < 40; ++playerIndex) {
		if (g_netPlayerConnectionStats[playerIndex].playerId ==
		    playerId) {
			Net_FindOrCreatePeerSlot(playerId);
			return packetDropCount +
			       g_netPlayerConnectionStats[playerIndex]
				       .packetDropCount;
		}
	}
	return packetDropCount;
}

/* Uses Net_FindOrCreatePeerSlot, so asking about an unknown player adds a reliable peer slot for it. */
// FUNCTION: XVT 0x4D29C0
int Net_GetPlayerPacketRetryCount(int playerId)
{
	unsigned int peerIndex;
	int packetRetryCount;
	int playerIndex;

	peerIndex = Net_FindOrCreatePeerSlot(playerId);
	if (g_frontState.netReliablePeerSlotCount > peerIndex &&
	    peerIndex < 40) {
		packetRetryCount =
			g_frontState.netRuntimeReliablePeerSlots[peerIndex]
				.packetRetryCount;
	} else {
		packetRetryCount = 0;
	}
	for (playerIndex = 0; playerIndex < 40; ++playerIndex) {
		if (g_netPlayerConnectionStats[playerIndex].playerId ==
		    playerId) {
			return packetRetryCount +
			       g_netPlayerConnectionStats[playerIndex]
				       .packetRetryCount;
		}
	}
	return packetRetryCount;
}

// FUNCTION: XVT 0x4D2A20
int Net_SetPlayerPacketCount(int playerId, int packetCount)
{
	int playerIndex;

	playerIndex = 0;
	do {
		if (g_netPlayerConnectionStats[playerIndex].playerId ==
		    playerId) {
			g_netPlayerConnectionStats[playerIndex].playerId =
				playerId;
			g_netPlayerConnectionStats[playerIndex].packetCount =
				packetCount;
			break;
		}
		++playerIndex;
	} while (playerIndex < 40);

	if (playerIndex == 40) {
		playerIndex = 0;
		do {
			if (g_netPlayerConnectionStats[playerIndex].playerId ==
			    0) {
				g_netPlayerConnectionStats[playerIndex]
					.playerId = playerId;
				g_netPlayerConnectionStats[playerIndex]
					.latencyTotalMs = 1;
				g_netPlayerConnectionStats[playerIndex]
					.packetCount = packetCount;
				g_netPlayerConnectionStats[playerIndex]
					.packetDropCount = 0;
				g_netPlayerConnectionStats[playerIndex]
					.packetRetryCount = 0;
				break;
			}
			++playerIndex;
			if (playerIndex >= 40) {
				return 1;
			}
		} while (1);
	}

	return 1;
}

// FUNCTION: XVT 0x4D2AB0
int Net_SetPlayerPacketDropCount(int playerId, int packetDropCount)
{
	int playerIndex;

	playerIndex = 0;
	do {
		if (g_netPlayerConnectionStats[playerIndex].playerId ==
		    playerId) {
			g_netPlayerConnectionStats[playerIndex].playerId =
				playerId;
			g_netPlayerConnectionStats[playerIndex]
				.packetDropCount = packetDropCount;
			break;
		}
		++playerIndex;
	} while (playerIndex < 40);

	if (playerIndex == 40) {
		playerIndex = 0;
		do {
			if (g_netPlayerConnectionStats[playerIndex].playerId ==
			    0) {
				g_netPlayerConnectionStats[playerIndex]
					.playerId = playerId;
				g_netPlayerConnectionStats[playerIndex]
					.latencyTotalMs = 1;
				g_netPlayerConnectionStats[playerIndex]
					.packetCount = 0;
				g_netPlayerConnectionStats[playerIndex]
					.packetDropCount = packetDropCount;
				g_netPlayerConnectionStats[playerIndex]
					.packetRetryCount = 0;
				break;
			}
			++playerIndex;
			if (playerIndex >= 40) {
				return 1;
			}
		} while (1);
	}

	return 1;
}

// FUNCTION: XVT 0x4D2B40
int Net_SetPlayerPacketRetryCount(int playerId, int packetRetryCount)
{
	int playerIndex;

	playerIndex = 0;
	do {
		if (g_netPlayerConnectionStats[playerIndex].playerId ==
		    playerId) {
			g_netPlayerConnectionStats[playerIndex].playerId =
				playerId;
			g_netPlayerConnectionStats[playerIndex]
				.packetRetryCount = packetRetryCount;
			break;
		}
		++playerIndex;
	} while (playerIndex < 40);

	if (playerIndex == 40) {
		playerIndex = 0;
		do {
			if (g_netPlayerConnectionStats[playerIndex].playerId ==
			    0) {
				g_netPlayerConnectionStats[playerIndex]
					.playerId = playerId;
				g_netPlayerConnectionStats[playerIndex]
					.latencyTotalMs = 1;
				g_netPlayerConnectionStats[playerIndex]
					.packetCount = 0;
				g_netPlayerConnectionStats[playerIndex]
					.packetDropCount = 0;
				g_netPlayerConnectionStats[playerIndex]
					.packetRetryCount = packetRetryCount;
				break;
			}
			++playerIndex;
			if (playerIndex >= 40) {
				return 1;
			}
		} while (1);
	}

	return 1;
}

// FUNCTION: XVT 0x4D2BD0
int Net_DisableAutoDialRegistrySetting(void)
{
#ifdef XVT_MODERN
	return 0;
#else
	void *registryKey;
	unsigned int dataSize;
	unsigned int disabledValue;

	g_netAutoDialRegistryChanged = 0;
	if (RegOpenKeyExA(
		    0x80000001u,
		    "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
		    0, NET_REGISTRY_ALL_ACCESS, &registryKey) != 0) {
		return 0;
	}
	dataSize = 5;
	if (RegQueryValueExA(registryKey, "EnableAutodial", NULL, NULL,
			     g_netSavedEnableAutoDialValue, &dataSize) != 0) {
		RegCloseKey(registryKey);
		return 0;
	}
	if (g_netSavedEnableAutoDialValue[0] == 0) {
		RegCloseKey(registryKey);
		return 0;
	}
	disabledValue = 0;
	if (RegSetValueExA(registryKey, "EnableAutodial", 0,
			   NET_REGISTRY_BINARY, &disabledValue, 4) != 0) {
		RegCloseKey(registryKey);
		return 0;
	}
	g_netAutoDialRegistryChanged = 1;
	RegCloseKey(registryKey);
	return 1;
#endif
}

// FUNCTION: XVT 0x4D2CB0
int Net_RestoreAutoDialRegistrySetting(void)
{
#ifdef XVT_MODERN
	return 0;
#else
	void *registryKey;

	if (g_netAutoDialRegistryChanged == 0) {
		return 0;
	}
	if (RegOpenKeyExA(
		    0x80000001u,
		    "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
		    0, NET_REGISTRY_ALL_ACCESS, &registryKey) != 0) {
		return 0;
	}
	if (RegSetValueExA(registryKey, "EnableAutodial", 0,
			   NET_REGISTRY_BINARY, g_netSavedEnableAutoDialValue,
			   4) != 0) {
		RegCloseKey(registryKey);
		return 0;
	}
	g_netAutoDialRegistryChanged = 0;
	RegCloseKey(registryKey);
	return 1;
#endif
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D2D40
int Net_WaitForShutdownHandshakeAcks(void)
{
	enum {
		NET_MAX_PLAYERS = 32,
		NET_SHUTDOWN_RESEND_INTERVAL_MS = 1000,
		NET_SHUTDOWN_TIMEOUT_MS = 10000
	};

	int shutdownPacket;
	DPID senderId;
	DPID acknowledgedPlayerIds[NET_MAX_PLAYERS];
	uint32_t packetSize;
	uint32_t currentTime;
	uint32_t startTime;
	uint32_t lastSendTime;
	int acknowledgedPlayerCount;
	unsigned int playerIndex;
	int *packet;

	memset(acknowledgedPlayerIds, 0, sizeof(acknowledgedPlayerIds));
	shutdownPacket = NET_PACKET_PING;
	Net_SendDirectPlayPacket(0, &shutdownPacket, sizeof(shutdownPacket), 0);
	acknowledgedPlayerCount = 1;
	startTime = GetTickCount();
	currentTime = startTime;
	lastSendTime = GetTickCount();
	while (currentTime - startTime < NET_SHUTDOWN_TIMEOUT_MS) {
		currentTime = GetTickCount();
		if (currentTime - lastSendTime >
		    NET_SHUTDOWN_RESEND_INTERVAL_MS) {
			lastSendTime = currentTime;
			Net_SendDirectPlayPacket(0, &shutdownPacket,
						 sizeof(shutdownPacket), 0);
		}
		packet = Net_GetNextAppPacket(&senderId, &packetSize);
		if (packet != NULL && *packet == NET_PACKET_PONG) {
			for (playerIndex = 0; playerIndex < NET_MAX_PLAYERS;
			     ++playerIndex) {
				if (acknowledgedPlayerIds[playerIndex] ==
				    senderId) {
					break;
				}
			}
			if (playerIndex == NET_MAX_PLAYERS) {
				++acknowledgedPlayerCount;
				for (playerIndex = 0;
				     playerIndex < NET_MAX_PLAYERS;
				     ++playerIndex) {
					if (acknowledgedPlayerIds
						    [playerIndex] == 0) {
						acknowledgedPlayerIds
							[playerIndex] =
								senderId;
						break;
					}
				}
			}
		}
		if (g_frontState.netPlayerCount <= acknowledgedPlayerCount) {
			return 1;
		}
	}
	return 0;
}
#endif

#ifndef XVT_NET_FRONTEND_NET_H
#define XVT_NET_FRONTEND_NET_H

#include "xvt/net/net.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	FRONTEND_NET_PROTOCOL_VERSION =
#ifdef XVT_MODERN
		103
#else
		101
#endif
};

#pragma pack(push, 1)

struct FrontendNetPacketScratch {
	int packetType;	      /* NET_PACKET_ value naming the packet */
	uint8_t payload[508]; /* Body; its layout depends on packetType */
};

#pragma pack(pop)
typedef char xvt_size_FrontendNetPacketScratch
	[(sizeof(FrontendNetPacketScratch) == 512) ? 1 : -1];

struct FrontendNetSessionEntry {
	char gameName[32]; /* Session name from DirectPlay */
	/* DirectPlay session GUID; a key of 0 marks an empty entry */
	NetSessionGuid sessionGuid;
	/* Players needed: 9 until probed, 0 if full or no answer came */
	unsigned int playersNeeded;
	/* GetTickCount ms of the last probe; 0 before any */
	unsigned int lastQueryMs;
	/* Host's protocol version: this build's until probed, 0 if no answer */
	unsigned int version;
	uint8_t passwordRequired; /* Nonzero when the game needs a password */
	uint8_t gameInFlight;	  /* 1 when the probe found the game flying */
};

extern int g_frontendNetSessionCount;
extern int g_frontendNetReceivedMissionDescriptionId;
extern int g_frontendNetReceivedMissionDirectoryId;
extern const unsigned int g_frontendNetXvtDirectPlayAppGuid[4];
extern FrontendNetPacketScratch g_frontendNetPacketScratch;
extern FrontendNetSessionEntry g_frontendNetSessionList[32];
extern int g_frontendNetSessionListScrollOffset;
extern int g_frontendNetSelectedSessionIdx;
extern int g_frontendNetPacketSenderPlayerId;
extern int g_frontendNetPacketArg0;
extern int g_frontendNetReservingPlayerId;
extern int g_hostGameStartPending;
extern int g_frontendQuickStartLaunchFlag;
extern int g_frontendNetProbeVersion;
extern int g_frontendNetProbePlayersNeeded;
extern int g_frontendNetProbePasswordRequired;
extern int g_frontendNetProbeMissionElapsedSeconds;
extern int g_frontendBriefingEnteredCount;
extern char g_frontendChatInputBuffer[100];
extern char *g_frontendChatLogBuffer;
extern int g_frontendChatLogUsedBytes;
extern int g_frontendChatTeamOnly;
extern int g_frontendChatScrollOffset;
extern char g_frontendNetSelectedGameName[32];

int FrontendNet_DrawJoinGameList(int frameCounter);
int FrontendNet_JoinGameScreen(int frameCounter);
int FrontendNet_AwaitJoinAdmissionScreen(int frameCounter);
int FrontendNet_DrawJoinGameMissionBriefing(void);
int FrontendNet_DrawJoinGamePlayerRoster(void);
int FrontendNet_UpdateAndDrawChatPanel(int frameCounter);
int FrontendNet_DrawJoinGameSidebarsAndQueryAll(void);
int FrontendNet_HostGameExit(int frameCounter);
int FrontendNet_HostGameScreen(int frameCounter);
int FrontendNet_ProcessNetworkPackets(void);

#ifndef XVT_MODERN
int FrontendNet_ConnectToSelectedGameScreen(int frameCounter);
int FrontendNet_MakeSessionGuidKey(NetSessionGuid guid);
int FrontendNet_RefreshSessionList(void);
int FrontendNet_CompareSessionListEntries(const FrontendNetSessionEntry *lhs,
					  const FrontendNetSessionEntry *rhs);
int FrontendNet_SortSessions(void);
int FrontendNet_ProbeAllSessions(void);
int FrontendNet_ProbeSessionByIndex(int sessionIdx);
#endif

#ifdef __cplusplus
}
#endif

#endif

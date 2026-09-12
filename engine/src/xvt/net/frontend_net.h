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
	int packetType;
	uint8_t payload[508];
};

#pragma pack(pop)
typedef char xvt_size_FrontendNetPacketScratch[(sizeof(FrontendNetPacketScratch) == 512) ? 1 : -1];

struct FrontendNetSessionEntry {
	char gameName[32];
	NetSessionGuid sessionGuid;
	unsigned int playersNeeded;
	unsigned int lastQueryTick;
	unsigned int version;
	uint8_t passwordRequired;
	uint8_t queryState;
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
extern int g_frontendNetPacketArg1;
extern int g_hostGameStartPending;
extern int g_frontendQuickStartLaunchFlag;
extern int g_frontendNetProbeVersion;
extern int g_frontendNetProbePlayersNeeded;
extern int g_frontendNetProbePasswordRequired;
extern int g_frontendNetProbeResponseType;
extern int g_frontendMissionOpcode99Count;
extern char g_frontendChatInputBuffer[100];
extern char* g_frontendChatLogBuffer;
extern int g_frontendChatLogUsedBytes;
extern int g_frontendChatTeamOnly;
extern int g_frontendChatScrollOffset;
extern char g_frontendNetSelectedGameName[32];

int FrontendNet_DrawJoinGameList(int resetScroll);
int FrontendNet_JoinGameScreen(int firstFrame);
int FrontendNet_AccessAllianceNetworkScreen(int firstFrame);
int FrontendNet_DrawJoinGameMissionBriefing(void);
int FrontendNet_DrawJoinGamePlayerRoster(void);
int FrontendNet_UpdateAndDrawPanel(int frameCounter);
int FrontendNet_DrawJoinGameSidebarsAndQueryAll(void);
int FrontendNet_HostGameExit(int frameCounter);
int FrontendNet_HostGameScreen(int firstFrame);
int FrontendNet_ProcessNetworkPackets(void);

#ifndef XVT_MODERN
int FrontendNet_ConnectToSelectedGameScreen(int firstFrame);
int FrontendNet_MakeSessionGuidKey(NetSessionGuid guid);
int FrontendNet_RefreshSessionList(void);
int FrontendNet_CompareSessionListEntries(const FrontendNetSessionEntry* lhs,
										  const FrontendNetSessionEntry* rhs);
int FrontendNet_SortSessions(void);
int FrontendNet_ProbeAllSessions(void);
int FrontendNet_ProbeSessionByIndex(int sessionIdx);
#endif

#ifdef __cplusplus
}
#endif

#endif

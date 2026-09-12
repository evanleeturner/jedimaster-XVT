#include "xvt_runtime/runtime/movie_sync.h"

#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

#include <stdio.h>

void XvtMovieSync_Begin(void) {
	unsigned int count = Net_CountReadyPlayers();
	unsigned int index;
	for (index = 0; index < 8; ++index) {
		g_movieMultiplayerSyncPlayers[index].playerId = index < count ? g_mpRoster[index].playerId : 0;
		g_movieMultiplayerSyncPlayers[index].isWaiting = 0;
	}
	g_moviePlaybackCompletionState = 0;
	g_movieMultiplayerSyncDeadlineTick = 0;
}

void XvtMovieSync_Wait(void) {
	int packet[2] = { NET_PACKET_MOVIE_SYNC, 0 };
	int index;
	if (g_moviePlaybackCompletionState)
		return;
	g_moviePlaybackCompletionState = 1;
	for (index = 0; index < 8; ++index)
		if (g_movieMultiplayerSyncPlayers[index].playerId == Net_GetLocalPlayerId())
			g_movieMultiplayerSyncPlayers[index].isWaiting = 1;
	Net_SendPacketAndFlush(0, packet, sizeof(packet));
	g_movieMultiplayerSyncDeadlineTick = GetTickCount() + (Net_IsHost() ? 5000 : 20000);
}

int XvtMovieSync_Tick(void) {
	int index;
	int waiting = 0;
	FrontendNet_ProcessNetworkPackets();
	for (index = 0; index < 8; ++index)
		if (g_movieMultiplayerSyncPlayers[index].playerId && !g_movieMultiplayerSyncPlayers[index].isWaiting)
			++waiting;
	if (g_moviePlaybackCompletionState == 1 &&
		(int32_t)(GetTickCount() - g_movieMultiplayerSyncDeadlineTick) > 0)
		g_moviePlaybackCompletionState = 2;
	return waiting == 0;
}

void XvtMovieSync_Draw(int top_margin, int bottom_margin) {
	int index;
	int roster;
	int count = Net_CountReadyPlayers();
	RECT rect;
	char text[128];
	if (!g_moviePlaybackCompletionState)
		return;
	for (index = 0; index < 8; ++index) {
		if (!g_movieMultiplayerSyncPlayers[index].playerId)
			continue;
		text[0] = 0;
		for (roster = 0; roster < count && roster < 8; ++roster) {
			if (g_mpRoster[roster].playerId == g_movieMultiplayerSyncPlayers[index].playerId) {
				snprintf(text, sizeof(text), "%s%s", g_mpRoster[roster].name,
						 FrontendString_Get(g_movieMultiplayerSyncPlayers[index].isWaiting
												? FRONTSTR_805_WAITING
												: FRONTSTR_804_WATCHING));
				break;
			}
		}
		rect = (RECT) { 32 + 144 * (index & 3), top_margin / 2 * (index >> 2), 32 + 144 * ((index & 3) + 1),
						top_margin / 2 * ((index >> 2) + 1) };
		FrontendText_DrawCentered(12, text, &rect, 0xffff);
	}
	if (g_moviePlaybackCompletionState == 2 && bottom_margin > 0) {
		rect = (RECT) { 0, 480 - bottom_margin, 639, 479 };
		FrontendDraw_Rect(&rect, 0, 0, 0, -1);
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(Net_IsHost() ? FRONTSTR_807_STILL_WAITING_FOR_OTHERS_HIT_C_TO_CONTINUE_THE_GAME
											: FRONTSTR_806_STILL_WAITING_FOR_OTHERS_HIT_E_TO_EXIT_THE_GAME),
			&rect, 0xffff);
	}
}

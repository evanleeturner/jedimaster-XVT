#include "xvt_runtime/runtime/network_dialogs.h"

#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_session.h"

#include <string.h>

int XvtNetworkDialogs_Resume(int result, int action) {
	(void)result;
	RECT screen = { 0, 0, 640, 480 };
	switch ((XvtNetworkDialogAction)action) {
		case XVT_NETWORK_ACCESS_REJECTED:
		case XVT_NETWORK_ACCESS_PASSWORD:
			if (action == XVT_NETWORK_ACCESS_PASSWORD)
				FrontendScreen_QueuePush(Config_OptionsDatapadUpdate, &screen);
			XvtNetworkDialogs_Return(0);
			break;
	}
	return 0;
}

int XvtNetworkDialogs_Connecting(void) {
	RECT message = { 0, 0, 640, 480 }, cancel = { 85, 447, 176, 471 };
	FrontImage_DrawSpriteOpaque("background", 0, 0);
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_645_CONNECTING), &message, 0xffff);
	FrontendCursor_Show();
	return FrontendButton_DrawSpriteHitTest(&cancel, "leaveup", "leavedown",
											FrontendString_Get(FRONTSTR_019_CANCEL), 12, 0, 8,
											"buttonsound") != 0;
}

void XvtNetworkDialogs_Return(int host) {
	g_skipFrontendEntryMovie = 1;
	g_frontendMissionSessionMode =
		host ? FRONTEND_MISSION_SESSION_NET_HOST : FRONTEND_MISSION_SESSION_NET_CLIENT;
	g_frontendNetSelectedSessionIdx = -1;
	g_frontendNetProbeResponseType = 0;
	g_frontendNetReceivedMissionDescriptionId = -1;
	memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
	if (g_briefingText)
		memset(g_briefingText, 0, 4096);
	FrontendScreen_SetCallbacks(host ? FrontendNet_HostGameScreen : FrontendNet_JoinGameScreen,
								host ? FrontendNet_HostGameExit : FrontendMissionList_FreeScreenResources);
}

static int XvtNetworkDialogs_AfterFailure(int result, int host) {
	(void)result;
	XvtNetworkDialogs_Return(host);
	return 0;
}

void XvtNetworkDialogs_Failed(AeronDplayDirectoryError error, int host) {
	const char* message;
	XvtNetworkSession_Reset();
	switch (error) {
		case AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED:
			message = "The multiplayer directory is not configured.";
			break;
		case AERON_DPLAY_DIRECTORY_ERROR_INCOMPATIBLE:
			message = "This game uses an incompatible version.";
			break;
		case AERON_DPLAY_DIRECTORY_ERROR_FULL:
			message = "This game is full.";
			break;
		case AERON_DPLAY_DIRECTORY_ERROR_NOT_JOINABLE:
			message = "This game is not accepting players.";
			break;
		case AERON_DPLAY_DIRECTORY_ERROR_CLOSED:
		case AERON_DPLAY_DIRECTORY_ERROR_NOT_FOUND:
			message = "This connection has expired. Please try again.";
			break;
		case AERON_DPLAY_DIRECTORY_ERROR_TIMEOUT:
			message = "The connection attempt timed out.";
			break;
		case AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST:
			message = "The selected room or multiplayer configuration is invalid.";
			break;
		case AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED:
			message = "The connection to the game was lost or could not be established.";
			break;
		default:
			message = "The multiplayer directory is unavailable. Please try again.";
			break;
	}
	int result = XvtDialog_Confirm(message, "", "", FrontendString_Get(FRONTSTR_523_OKAY), NULL, 0);
	if (result == XVT_DIALOG_PENDING)
		XvtDialog_ContinueWith(XvtNetworkDialogs_AfterFailure, host);
	else
		XvtNetworkDialogs_Return(host);
}

int XvtNetworkDialogs_AdmissionFailed(void) {
	XvtNetworkSessionStatus status = XvtNetworkSession_GetStatus();
	if (status.state != XVT_NETWORK_SESSION_FAILED)
		return 0;
	XvtNetworkDialogs_Failed(status.error, 0);
	return 1;
}

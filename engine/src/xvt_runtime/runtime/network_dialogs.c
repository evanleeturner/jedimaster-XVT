#include "xvt_runtime/runtime/network_dialogs.h"

#include <string.h>

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

int xvt_network_dialogs_resume(int result, int action)
{
	(void)result;
	struct RECT screen = {0, 0, 640, 480};
	switch ((xvt_network_dialog_action)action) {
	case XVT_NETWORK_ACCESS_REJECTED:
	case XVT_NETWORK_ACCESS_PASSWORD:
		if (action == XVT_NETWORK_ACCESS_PASSWORD) {
			frontend_screen_queue_push(
				config_options_datapad_update, &screen);
		}
		xvt_network_dialogs_return(0);
		break;
	}
	return 0;
}

int xvt_network_dialogs_connecting(void)
{
	struct RECT message = {0, 0, 640, 480}, cancel = {85, 447, 176, 471};
	front_image_draw_sprite_opaque("background", 0, 0);
	frontend_text_draw_centered(
		15, frontend_string_get(FRONTSTR_645_CONNECTING), &message,
		0xffff);
	frontend_cursor_show();
	return frontend_button_handle_sprite_button(
		       &cancel, "leaveup", "leavedown",
		       frontend_string_get(FRONTSTR_019_CANCEL), 12, 0, 8,
		       "buttonsound") != 0;
}

void xvt_network_dialogs_return(int host)
{
	g_frontend_skip_screen_entry_setup = 1;
	g_frontend_mission_session_mode =
		host ? FRONTEND_MISSION_SESSION_NET_HOST
		     : FRONTEND_MISSION_SESSION_NET_CLIENT;
	g_frontend_net_selected_session_idx = -1;
	g_frontend_net_probe_mission_elapsed_seconds = 0;
	g_frontend_net_received_mission_description_id = -1;
	memset(g_frontend_net_selected_game_name, 0,
	       sizeof(g_frontend_net_selected_game_name));
	if (g_mission_text) {
		memset(g_mission_text, 0, 4096);
	}
	frontend_screen_set_callbacks(
		host ? frontend_net_host_game_screen
		     : frontend_net_join_game_screen,
		host ? frontend_net_host_game_exit
		     : frontend_mission_list_free_screen_resources);
}

static int xvt_network_dialogs_after_failure(int result, int host)
{
	(void)result;
	xvt_network_dialogs_return(host);
	return 0;
}

void xvt_network_dialogs_show_failure(AeronDplayDirectoryError error, int host)
{
	const char *message;
	xvt_network_session_leave();
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
		message =
			"The selected room or multiplayer configuration is invalid.";
		break;
	case AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED:
		message =
			"The connection to the game was lost or could not be established.";
		break;
	default:
		message =
			"The multiplayer directory is unavailable. Please try again.";
		break;
	}
	int result = xvt_dialog_confirm(message, "", "",
					frontend_string_get(FRONTSTR_523_OKAY),
					NULL, 0);
	if (result == XVT_DIALOG_PENDING) {
		xvt_dialog_continue_with(xvt_network_dialogs_after_failure,
					 host);
	} else {
		xvt_network_dialogs_return(host);
	}
}

int xvt_network_dialogs_report_admission_failure(void)
{
	struct xvt_network_session_status status =
		xvt_network_session_get_status();
	if (status.state != XVT_NETWORK_SESSION_FAILED) {
		return 0;
	}
	xvt_network_dialogs_show_failure(status.error, 0);
	return 1;
}

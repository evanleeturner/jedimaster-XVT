#include "xvt_runtime/runtime/network_task.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/runtime/network_dialogs.h"
#include "xvt_runtime/runtime/network_metadata.h"
#include "xvt_runtime/runtime/network_session.h"

enum { BROWSER_REFRESH_US = 10000000, BROWSER_VISIBLE_ROWS = 6 };

static struct {
	int active, action;
} g_network;

static struct {
	AeronDplayDirectorySnapshot snapshot;
	GUID selected;
	int selected_index, scroll, pending;
	uint64_t refresh_at, updated_at;
	AeronDplayDirectoryError error;
	struct xvt_network_preview preview;
} g_browser = {.selected_index = -1};

/* Read mission setup's list/description formats into browser-owned storage. */
static int xvt_network_task_find_mission_file_and_title(
	const AeronDplayDirectoryMission *mission, char *path, size_t capacity)
{
	char list[256], line[256], filename[256], title[256];
	if (!mission->present || mission->directory >= 6) {
		return 0;
	}
	snprintf(list, sizeof(list), "%s/mission.lst",
		 g_mission_directory_names[mission->directory]);
	xvt_file *file = file_open(list, "r");
	if (!file) {
		return 0;
	}
	int found = 0;
	while (FILE_GETS(line, sizeof(line), file)) {
		if (!line[0] || line[0] == '[' || line[0] == '\n' ||
		    line[0] == '\r' || (line[0] == '/' && line[1] == '/')) {
			continue;
		}
		char *end;
		long id = strtol(line, &end, 10);
		if (!FILE_GETS(filename, sizeof(filename), file) ||
		    !FILE_GETS(title, sizeof(title), file)) {
			break;
		}
		if (end == line || id != mission->id) {
			continue;
		}
		filename[strcspn(filename, "\r\n")] = 0;
		for (char *c = filename; *c; ++c) {
			*c = (char)tolower((unsigned char)*c);
		}
		title[strcspn(title, "\r\n")] = 0;
		char *name = filename;
		if (name[0] == '*') {
			name = strlen(name) >= 2 ? strchr(name + 2, ' ') : NULL;
			if (name) {
				name = strchr(name + 1, ' ');
			}
			if (name) {
				++name;
			}
		} else if (name[0] == '&') {
			name = strlen(name) >= 2 ? name + 2 : NULL;
		}
		if (!name || !*name) {
			break;
		}
		int length = snprintf(
			path, capacity, "%s/%s",
			g_mission_directory_names[mission->directory], name);
		if (length < 0 || (size_t)length >= capacity) {
			break;
		}
		snprintf(g_browser.preview.title,
			 sizeof(g_browser.preview.title), "%s", title);
		found = 1;
		break;
	}
	file_close(file);
	return found;
}

static void xvt_network_task_load_preview(void)
{
	memset(&g_browser.preview, 0, sizeof(g_browser.preview));
	const AeronDplayDirectoryRoom *room = xvt_network_task_selected_room();
	if (!room) {
		return;
	}
	char path[512];
	strcpy(g_browser.preview.text, "Description unavailable");
	if (!xvt_network_task_find_mission_file_and_title(
		    &room->metadata.mission, path, sizeof(path))) {
		return;
	}
	xvt_file *file = file_open(path, "rb");
	if (!file) {
		return;
	}
	char text[4096] = {0};
	unsigned directory = room->metadata.mission.directory;
	if (directory == MISSION_DIRECTORY_TOURNAMENTS ||
	    directory == MISSION_DIRECTORY_BATTLES ||
	    directory == MISSION_DIRECTORY_CAMPAIGNS) {
		char line[256], *end;
		if (FILE_GETS(line, sizeof(line), file)) {
			long lines_to_skip = strtol(line, &end, 10);
			if (end != line && lines_to_skip >= 0 &&
			    lines_to_skip <= 65536) {
				while (lines_to_skip > 0 &&
				       FILE_GETS(line, sizeof(line), file)) {
					--lines_to_skip;
				}
				size_t used = 0;
				while (!lines_to_skip &&
				       used + 1 < sizeof(text) &&
				       FILE_GETS(line, sizeof(line), file)) {
					for (size_t i = 0;
					     line[i] && used + 1 < sizeof(text);
					     ++i) {
						if ((uint8_t)line[i] >= 32 ||
						    line[i] == '\n') {
							text[used++] = line[i];
						}
					}
				}
			}
		}
	} else {
		uint16_t version = 0;
		file_read_word(file, &version);
		int size = version == 12		      ? 1024
			   : (version == 13 || version == 14) ? 4096
							      : 0;
		if (size && file_get_size(file) >= size + 2 &&
		    !file_seek(file, -size, SEEK_END)) {
			if (!file_read_bytes(file, text, (size_t)size)) {
				text[0] = 0;
			}
			text[size - 1] = 0;
		}
	}
	if (text[0] && !FILE_HAS_ERROR(file)) {
		memcpy(g_browser.preview.text, text, sizeof(text));
	}
	file_close(file);
}

const AeronDplayDirectorySnapshot *xvt_network_task_snapshot(void)
{
	return &g_browser.snapshot;
}

const AeronDplayDirectoryRoom *xvt_network_task_selected_room(void)
{
	int i = g_browser.selected_index;
	return i >= 0 && (unsigned)i < g_browser.snapshot.room_count
		       ? &g_browser.snapshot.rooms[i]
		       : NULL;
}

int xvt_network_task_selected_index(void) { return g_browser.selected_index; }

int *xvt_network_task_scroll_offset(void) { return &g_browser.scroll; }

struct xvt_network_preview *xvt_network_task_preview(void)
{
	return &g_browser.preview;
}

AeronDplayDirectoryError xvt_network_task_browser_error(void)
{
	return g_browser.error;
}

unsigned xvt_network_task_snapshot_age(void)
{
	uint64_t seconds =
		g_browser.updated_at
			? (Aeron_NowUs() - g_browser.updated_at) / 1000000
			: 0;
	return seconds > 359999 ? 359999 : (unsigned)seconds;
}

int xvt_network_task_compatible(const AeronDplayDirectoryRoom *room)
{
	char version[AERON_DPLAY_DIRECTORY_VERSION_CAPACITY];
	snprintf(version, sizeof(version), "%d", FRONTEND_NET_PROTOCOL_VERSION);
	return room && room->protocol == AERON_DPLAY_DIRECTORY_PROTOCOL &&
	       !strcmp(room->game_version, version);
}

int xvt_network_task_can_join(void)
{
	const AeronDplayDirectoryRoom *room = xvt_network_task_selected_room();
	return !g_network.active && !g_browser.error &&
	       g_browser.snapshot.available &&
	       xvt_network_task_compatible(room) && room->metadata.joinable &&
	       room->metadata.players < room->metadata.max_players;
}

void xvt_network_task_toggle_selection(int index)
{
	if (index == g_browser.selected_index || index < 0 ||
	    (unsigned)index >= g_browser.snapshot.room_count) {
		index = -1;
	}
	g_browser.selected_index = index;
	memset(&g_browser.selected, 0, sizeof(g_browser.selected));
	if (index >= 0) {
		g_browser.selected = g_browser.snapshot.rooms[index].room_id;
	}
	xvt_network_task_load_preview();
}

void xvt_network_task_refresh(void)
{
	g_browser.refresh_at = Aeron_NowUs() + BROWSER_REFRESH_US;
	AeronDplayDirectoryError error = xvt_network_session_configure();
	if (!error) {
		error = AeronDplayDirectory_Refresh();
	}
	if (error) {
		g_browser.error = error;
		g_browser.pending = 0;
		return;
	}
	g_browser.pending = 1;
}

void xvt_network_task_open_browser(void)
{
	xvt_network_session_leave();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_CLIENT;
	xvt_network_task_refresh();
}

int xvt_network_task_browser_visible(void)
{
	for (int i = 0; i <= g_front_state.screen_stack_top; ++i) {
		if (g_front_state.screen_states[i].update_fn ==
		    frontend_net_join_game_screen) {
			return !g_network.active;
		}
	}
	return 0;
}

void xvt_network_task_service_browser(void)
{
	if (!xvt_network_task_browser_visible()) {
		return;
	}
	uint64_t now = Aeron_NowUs();
	if (g_browser.pending) {
		AeronDplayDirectoryMission previous = {0};
		const AeronDplayDirectoryRoom *room =
			xvt_network_task_selected_room();
		if (room) {
			previous = room->metadata.mission;
		}
		AeronDplayDirectory_GetSnapshot(&g_browser.snapshot);
		if (g_browser.snapshot.refresh.state ==
		    AERON_DPLAY_DIRECTORY_SUCCEEDED) {
			int old_index = g_browser.selected_index;
			g_browser.pending = 0;
			g_browser.error = AERON_DPLAY_DIRECTORY_ERROR_NONE;
			g_browser.updated_at = now;
			g_browser.selected_index = -1;
			for (unsigned i = 0; i < g_browser.snapshot.room_count;
			     ++i) {
				if (!memcmp(&g_browser.selected,
					    &g_browser.snapshot.rooms[i]
						     .room_id,
					    sizeof(GUID))) {
					g_browser.selected_index = (int)i;
				}
			}
			if (g_browser.selected_index < 0) {
				memset(&g_browser.selected, 0,
				       sizeof(g_browser.selected));
			}
			int maximum = (int)g_browser.snapshot.room_count -
				      BROWSER_VISIBLE_ROWS;
			if (maximum < 0) {
				maximum = 0;
			}
			if (g_browser.scroll > maximum) {
				g_browser.scroll = maximum;
			}
			if (g_browser.selected_index >= 0 &&
			    old_index != g_browser.selected_index) {
				if (g_browser.selected_index <
				    g_browser.scroll) {
					g_browser.scroll =
						g_browser.selected_index;
				} else if (g_browser.selected_index >=
					   g_browser.scroll +
						   BROWSER_VISIBLE_ROWS) {
					g_browser.scroll =
						g_browser.selected_index -
						BROWSER_VISIBLE_ROWS + 1;
				}
			}
			room = xvt_network_task_selected_room();
			if (!room || memcmp(&previous, &room->metadata.mission,
					    sizeof(previous))) {
				xvt_network_task_load_preview();
			}
		} else if (g_browser.snapshot.refresh.state ==
			   AERON_DPLAY_DIRECTORY_FAILED) {
			g_browser.pending = 0;
			g_browser.error = g_browser.snapshot.refresh.error;
		}
	}
	if (now >= g_browser.refresh_at) {
		xvt_network_task_refresh();
	}
}

void xvt_network_task_begin(int action)
{
	if (g_network.active) {
		return;
	}
	int host =
		action == XVT_NETWORK_HOST || action == XVT_NETWORK_AUTO_HOST;
	const AeronDplayDirectoryRoom *room = xvt_network_task_selected_room();
	if (!host && !xvt_network_task_can_join()) {
		return;
	}
	char rating_text[2] = {(char)(g_pilot_data.rating + 1), 0};
	g_network.action = action;
	g_network.active = 1;
	if (host) {
		xvt_network_session_begin_host(
			rating_text, g_pilot_data.name,
			g_pilot_data.multiplayer_game_name,
			g_frontend_mission_session_mode !=
				FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	} else {
		xvt_network_metadata_from_utf8(
			g_pilot_data.multiplayer_game_name,
			sizeof(g_pilot_data.multiplayer_game_name),
			room->metadata.name);
		xvt_network_session_begin_join(rating_text, g_pilot_data.name,
					       &room->room_id);
	}
}

static void xvt_network_task_restore_cursor(void)
{
	frontend_display_unlock_back_buffer();
	frontend_cursor_hide_os_cursor();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
}

static void xvt_network_task_finish_session(int result)
{
	g_network.active = 0;
	xvt_network_task_restore_cursor();
	/* Failure dialogs must capture the restored parent rendering state. */
	if (g_network.action != XVT_NETWORK_AUTO_HOST) {
		frontend_display_enable_offscreen_restore();
	}
	if (!result) {
		xvt_network_dialogs_show_failure(
			xvt_network_session_get_status().error,
			g_network.action != XVT_NETWORK_CONNECT);
	} else if (g_network.action == XVT_NETWORK_CONNECT) {
		frontend_screen_set_callbacks(
			frontend_net_await_join_admission_screen, NULL);
	} else {
		if (g_network.action == XVT_NETWORK_HOST) {
			frontend_display_clear_offscreen_surface();
		}
		frontend_screen_set_callbacks(mission_setup_update,
					      mission_setup_exit);
	}
	frontend_cursor_show();
}

int xvt_network_task_resume(int *result)
{
	if (!g_network.active) {
		struct xvt_network_session_status status =
			xvt_network_session_get_status();
		if (status.state == XVT_NETWORK_SESSION_FAILED) {
			*result = 0;
			xvt_network_dialogs_show_failure(status.error, 0);
			return 1;
		}
		return 0;
	}
	*result = 0;
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	if ((input && input->has_focus && !xvt_input_is_captured() &&
	     input->key_pressed[AERON_KEY_ESCAPE]) ||
	    xvt_network_dialogs_connecting()) {
		xvt_network_task_cancel();
		return 1;
	}
	int status = xvt_network_session_update();
	if (status != XVT_NETWORK_PENDING) {
		xvt_network_task_finish_session(status);
	}
	return 1;
}

int xvt_network_task_is_active(void) { return g_network.active; }

void xvt_network_task_cancel(void)
{
	xvt_network_session_cancel();
	xvt_network_task_restore_cursor();
	frontend_display_enable_offscreen_restore();
	xvt_network_dialogs_return(g_network.action == XVT_NETWORK_HOST ||
				   g_network.action == XVT_NETWORK_AUTO_HOST);
	memset(&g_network, 0, sizeof(g_network));
}

void xvt_network_task_shutdown(void)
{
	xvt_network_session_leave();
	memset(&g_network, 0, sizeof(g_network));
	memset(&g_browser, 0, sizeof(g_browser));
	g_browser.selected_index = -1;
}

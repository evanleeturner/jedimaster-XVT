#include "xvt_runtime/runtime/network_task.h"
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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
	XvtNetworkPreview preview;
} g_browser = { .selected_index = -1 };

/* Read mission setup's list/description formats into browser-owned storage. */
static int XvtNetworkTask_MissionFile(const AeronDplayDirectoryMission* mission, char* path,
									  size_t capacity) {
	char list[256], line[256], filename[256], title[256];
	if (!mission->present || mission->directory >= 6)
		return 0;
	snprintf(list, sizeof(list), "%s/mission.lst", g_campaignDirNames[mission->directory]);
	XvtFile* file = File_Open(list, "r");
	if (!file)
		return 0;
	int found = 0;
	while (File_Gets(line, sizeof(line), file)) {
		if (!line[0] || line[0] == '[' || line[0] == '\n' || line[0] == '\r' ||
			(line[0] == '/' && line[1] == '/'))
			continue;
		char* end;
		long id = strtol(line, &end, 10);
		if (!File_Gets(filename, sizeof(filename), file) || !File_Gets(title, sizeof(title), file))
			break;
		if (end == line || id != mission->id)
			continue;
		filename[strcspn(filename, "\r\n")] = 0;
		for (char* c = filename; *c; ++c)
			*c = (char)tolower((unsigned char)*c);
		title[strcspn(title, "\r\n")] = 0;
		char* name = filename;
		if (name[0] == '*') {
			name = strlen(name) >= 2 ? strchr(name + 2, ' ') : NULL;
			if (name)
				name = strchr(name + 1, ' ');
			if (name)
				++name;
		} else if (name[0] == '&')
			name = strlen(name) >= 2 ? name + 2 : NULL;
		if (!name || !*name)
			break;
		int length = snprintf(path, capacity, "%s/%s", g_campaignDirNames[mission->directory], name);
		if (length < 0 || (size_t)length >= capacity)
			break;
		snprintf(g_browser.preview.title, sizeof(g_browser.preview.title), "%s", title);
		found = 1;
		break;
	}
	File_Close(file);
	return found;
}

static void XvtNetworkTask_LoadPreview(void) {
	memset(&g_browser.preview, 0, sizeof(g_browser.preview));
	const AeronDplayDirectoryRoom* room = XvtNetworkTask_SelectedRoom();
	if (!room)
		return;
	char path[512];
	strcpy(g_browser.preview.text, "Description unavailable");
	if (!XvtNetworkTask_MissionFile(&room->metadata.mission, path, sizeof(path)))
		return;
	XvtFile* file = File_Open(path, "rb");
	if (!file)
		return;
	char text[4096] = { 0 };
	unsigned directory = room->metadata.mission.directory;
	if (directory == MISSION_DIRECTORY_TOURNAMENTS || directory == MISSION_DIRECTORY_BATTLES ||
		directory == MISSION_DIRECTORY_CAMPAIGNS) {
		char line[256], *end;
		if (File_Gets(line, sizeof(line), file)) {
			long skipped = strtol(line, &end, 10);
			if (end != line && skipped >= 0 && skipped <= 65536) {
				while (skipped > 0 && File_Gets(line, sizeof(line), file))
					--skipped;
				size_t used = 0;
				while (!skipped && used + 1 < sizeof(text) && File_Gets(line, sizeof(line), file)) {
					for (size_t i = 0; line[i] && used + 1 < sizeof(text); ++i)
						if ((uint8_t)line[i] >= 32 || line[i] == '\n')
							text[used++] = line[i];
				}
			}
		}
	} else {
		uint16_t version = 0;
		File_ReadWord(file, &version);
		int size = version == 12 ? 1024 : (version == 13 || version == 14) ? 4096 : 0;
		if (size && File_GetSize(file) >= size + 2 && !File_Seek(file, -size, SEEK_END)) {
			if (!File_ReadCount(file, text, (size_t)size))
				text[0] = 0;
			text[size - 1] = 0;
		}
	}
	if (text[0] && !File_HasError(file))
		memcpy(g_browser.preview.text, text, sizeof(text));
	File_Close(file);
}

const AeronDplayDirectorySnapshot* XvtNetworkTask_Snapshot(void) { return &g_browser.snapshot; }

const AeronDplayDirectoryRoom* XvtNetworkTask_SelectedRoom(void) {
	int i = g_browser.selected_index;
	return i >= 0 && (unsigned)i < g_browser.snapshot.room_count ? &g_browser.snapshot.rooms[i] : NULL;
}

int XvtNetworkTask_SelectedIndex(void) { return g_browser.selected_index; }

int* XvtNetworkTask_ScrollOffset(void) { return &g_browser.scroll; }

XvtNetworkPreview* XvtNetworkTask_Preview(void) { return &g_browser.preview; }

AeronDplayDirectoryError XvtNetworkTask_BrowserError(void) { return g_browser.error; }

unsigned XvtNetworkTask_SnapshotAge(void) {
	uint64_t seconds = g_browser.updated_at ? (Aeron_NowUs() - g_browser.updated_at) / 1000000 : 0;
	return seconds > 359999 ? 359999 : (unsigned)seconds;
}

int XvtNetworkTask_Compatible(const AeronDplayDirectoryRoom* room) {
	char version[AERON_DPLAY_DIRECTORY_VERSION_CAPACITY];
	snprintf(version, sizeof(version), "%d", FRONTEND_NET_PROTOCOL_VERSION);
	return room && room->protocol == AERON_DPLAY_DIRECTORY_PROTOCOL && !strcmp(room->game_version, version);
}

int XvtNetworkTask_CanJoin(void) {
	const AeronDplayDirectoryRoom* room = XvtNetworkTask_SelectedRoom();
	return !g_network.active && !g_browser.error && g_browser.snapshot.available &&
		   XvtNetworkTask_Compatible(room) && room->metadata.joinable &&
		   room->metadata.players < room->metadata.max_players;
}

void XvtNetworkTask_Select(int index) {
	if (index == g_browser.selected_index || index < 0 || (unsigned)index >= g_browser.snapshot.room_count)
		index = -1;
	g_browser.selected_index = index;
	memset(&g_browser.selected, 0, sizeof(g_browser.selected));
	if (index >= 0)
		g_browser.selected = g_browser.snapshot.rooms[index].room_id;
	XvtNetworkTask_LoadPreview();
}

void XvtNetworkTask_Refresh(void) {
	g_browser.refresh_at = Aeron_NowUs() + BROWSER_REFRESH_US;
	AeronDplayDirectoryError error = XvtNetworkSession_Configure();
	if (!error)
		error = AeronDplayDirectory_Refresh();
	if (error) {
		g_browser.error = error;
		g_browser.pending = 0;
		return;
	}
	g_browser.pending = 1;
}

void XvtNetworkTask_OpenBrowser(void) {
	XvtNetworkSession_Leave();
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
	XvtNetworkTask_Refresh();
}

int XvtNetworkTask_BrowserVisible(void) {
	for (int i = 0; i <= g_frontState.screenStackTop; ++i)
		if (g_frontState.screenStates[i].updateFn == FrontendNet_JoinGameScreen)
			return !g_network.active;
	return 0;
}

void XvtNetworkTask_ServiceBrowser(void) {
	if (!XvtNetworkTask_BrowserVisible())
		return;
	uint64_t now = Aeron_NowUs();
	if (g_browser.pending) {
		AeronDplayDirectoryMission previous = { 0 };
		const AeronDplayDirectoryRoom* room = XvtNetworkTask_SelectedRoom();
		if (room)
			previous = room->metadata.mission;
		AeronDplayDirectory_GetSnapshot(&g_browser.snapshot);
		if (g_browser.snapshot.refresh.state == AERON_DPLAY_DIRECTORY_SUCCEEDED) {
			int old_index = g_browser.selected_index;
			g_browser.pending = 0;
			g_browser.error = AERON_DPLAY_DIRECTORY_ERROR_NONE;
			g_browser.updated_at = now;
			g_browser.selected_index = -1;
			for (unsigned i = 0; i < g_browser.snapshot.room_count; ++i)
				if (!memcmp(&g_browser.selected, &g_browser.snapshot.rooms[i].room_id, sizeof(GUID)))
					g_browser.selected_index = (int)i;
			if (g_browser.selected_index < 0)
				memset(&g_browser.selected, 0, sizeof(g_browser.selected));
			int maximum = (int)g_browser.snapshot.room_count - BROWSER_VISIBLE_ROWS;
			if (maximum < 0)
				maximum = 0;
			if (g_browser.scroll > maximum)
				g_browser.scroll = maximum;
			if (g_browser.selected_index >= 0 && old_index != g_browser.selected_index) {
				if (g_browser.selected_index < g_browser.scroll)
					g_browser.scroll = g_browser.selected_index;
				else if (g_browser.selected_index >= g_browser.scroll + BROWSER_VISIBLE_ROWS)
					g_browser.scroll = g_browser.selected_index - BROWSER_VISIBLE_ROWS + 1;
			}
			room = XvtNetworkTask_SelectedRoom();
			if (!room || memcmp(&previous, &room->metadata.mission, sizeof(previous)))
				XvtNetworkTask_LoadPreview();
		} else if (g_browser.snapshot.refresh.state == AERON_DPLAY_DIRECTORY_FAILED) {
			g_browser.pending = 0;
			g_browser.error = g_browser.snapshot.refresh.error;
		}
	}
	if (now >= g_browser.refresh_at)
		XvtNetworkTask_Refresh();
}

void XvtNetworkTask_Begin(int action) {
	if (g_network.active)
		return;
	int host = action == XVT_NETWORK_HOST || action == XVT_NETWORK_AUTO_HOST;
	const AeronDplayDirectoryRoom* room = XvtNetworkTask_SelectedRoom();
	if (!host && !XvtNetworkTask_CanJoin())
		return;
	char info[2] = { (char)(g_pilotData.rating + 1), 0 };
	g_network.action = action;
	g_network.active = 1;
	if (host)
		XvtNetworkSession_BeginHost(info, g_pilotData.name, g_pilotData.multiplayerGameName,
									g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	else {
		XvtNetworkMetadata_FromUtf8(g_pilotData.multiplayerGameName, sizeof(g_pilotData.multiplayerGameName),
									room->metadata.name);
		XvtNetworkSession_BeginJoin(info, g_pilotData.name, &room->room_id);
	}
}

static void XvtNetworkTask_RestoreCursor(void) {
	FrontendDisplay_UnlockBackBuffer();
	FrontendCursor_HideOsCursor();
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
}

static void XvtNetworkTask_FinishSession(int result) {
	g_network.active = 0;
	XvtNetworkTask_RestoreCursor();
	/* Failure dialogs must capture the restored parent rendering state. */
	if (g_network.action != XVT_NETWORK_AUTO_HOST)
		FrontendDisplay_EnableOffscreenRestore();
	if (!result)
		XvtNetworkDialogs_Failed(XvtNetworkSession_GetStatus().error,
								 g_network.action != XVT_NETWORK_CONNECT);
	else if (g_network.action == XVT_NETWORK_CONNECT)
		FrontendScreen_SetCallbacks(FrontendNet_AccessAllianceNetworkScreen, NULL);
	else {
		if (g_network.action == XVT_NETWORK_HOST)
			FrontendDisplay_ClearOffscreenSurface();
		FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
	}
	FrontendCursor_Show();
}

int XvtNetworkTask_Resume(int* result) {
	if (!g_network.active) {
		XvtNetworkSessionStatus status = XvtNetworkSession_GetStatus();
		if (status.state == XVT_NETWORK_SESSION_FAILED) {
			*result = 0;
			XvtNetworkDialogs_Failed(status.error, 0);
			return 1;
		}
		return 0;
	}
	*result = 0;
	const AeronInputSnapshot* input = Aeron_InputSnapshot();
	if ((input && input->has_focus && !XvtInput_IsCaptured() && input->key_pressed[AERON_KEY_ESCAPE]) ||
		XvtNetworkDialogs_Connecting()) {
		XvtNetworkTask_Cancel();
		return 1;
	}
	int status = XvtNetworkSession_Tick();
	if (status != XVT_NETWORK_PENDING)
		XvtNetworkTask_FinishSession(status);
	return 1;
}

int XvtNetworkTask_IsActive(void) { return g_network.active; }

void XvtNetworkTask_Cancel(void) {
	XvtNetworkSession_Cancel();
	XvtNetworkTask_RestoreCursor();
	FrontendDisplay_EnableOffscreenRestore();
	XvtNetworkDialogs_Return(g_network.action == XVT_NETWORK_HOST ||
							 g_network.action == XVT_NETWORK_AUTO_HOST);
	memset(&g_network, 0, sizeof(g_network));
}

void XvtNetworkTask_Shutdown(void) {
	XvtNetworkSession_Reset();
	memset(&g_network, 0, sizeof(g_network));
	memset(&g_browser, 0, sizeof(g_browser));
	g_browser.selected_index = -1;
}

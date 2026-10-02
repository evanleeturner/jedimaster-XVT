#ifndef XVT_RUNTIME_NETWORK_TASK_H
#define XVT_RUNTIME_NETWORK_TASK_H
#include "aeron/compat/dplay_directory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The frontend side of multiplayer: the room browser over the directory (snapshot, selection kept
 * by room id across refreshes, scroll offset, mission preview), and one host or join attempt at a
 * time, run through the network session while the frontend screen is suspended. */

enum { XVT_NETWORK_HOST, XVT_NETWORK_AUTO_HOST, XVT_NETWORK_CONNECT };

typedef struct XvtNetworkPreview {
	char title[256], text[4096];
	int scroll;
} XvtNetworkPreview;

/* Starts a host (either host action) or a join of the selected room (CONNECT), sending the
 * pilot's rating and name; a join first copies the room name into the pilot's game name. Ignored
 * while an attempt is active, and for CONNECT unless CanJoin. */
void XvtNetworkTask_Begin(int action);
/* 1 while a host or join attempt runs. */
int XvtNetworkTask_IsActive(void);
/* Leaves any network session, enters client mode and starts a refresh. */
void XvtNetworkTask_OpenBrowser(void);
/* Asks the directory for a new snapshot and schedules the next automatic refresh 10 seconds out;
 * a configuration or request error is recorded as the browser error. */
void XvtNetworkTask_Refresh(void);
/* Per host frame while the browser is visible: copies the directory snapshot while a refresh is
 * pending; on success clears the error, re-finds the selected room by id (dropping the selection
 * when it is gone), keeps the scroll in range and a moved selection visible, and reloads the
 * preview when the selected mission changed or nothing is selected; on failure records the error.
 * Refreshes every 10 seconds. */
void XvtNetworkTask_ServiceBrowser(void);
/* 1 while the join screen is anywhere on the screen stack and no attempt runs. */
int XvtNetworkTask_BrowserVisible(void);
/* The latest directory snapshot. */
const AeronDplayDirectorySnapshot* XvtNetworkTask_Snapshot(void);
/* The selected room in the snapshot, or NULL. */
const AeronDplayDirectoryRoom* XvtNetworkTask_SelectedRoom(void);
/* The selected room's index, or -1. */
int XvtNetworkTask_SelectedIndex(void);
/* The browser's list scroll offset, for the list draw to read and write. */
int* XvtNetworkTask_ScrollOffset(void);
/* The selected room's mission title and description, read from the mission files; the text is
 * "Description unavailable" when they cannot be read, and both are empty with no selection. */
XvtNetworkPreview* XvtNetworkTask_Preview(void);
/* Seconds since the last successful refresh, 0 before one, at most 359999. */
unsigned XvtNetworkTask_SnapshotAge(void);
/* The last refresh error, cleared by the next successful refresh. */
AeronDplayDirectoryError XvtNetworkTask_BrowserError(void);
/* 1 when room uses this directory protocol and its game version is FRONTEND_NET_PROTOCOL_VERSION
 * in decimal. */
int XvtNetworkTask_Compatible(const AeronDplayDirectoryRoom* room);
/* 1 when no attempt runs, the browser has no error, the snapshot is available and the selected
 * room is compatible, joinable and not full. */
int XvtNetworkTask_CanJoin(void);
/* Selects room index and loads its preview; the selected index again, or one out of range, clears
 * the selection. */
void XvtNetworkTask_ToggleSelection(int index);
/* Called under the frontend draw lock in place of the suspended screen update. */
/* With no attempt: when the network session has failed, shows the failure dialog, sets result to
 * 0 and returns 1; otherwise returns 0. During an attempt, sets result to 0 and returns 1: Escape,
 * or the cancel button of the connecting screen drawn otherwise, cancels; otherwise the session is
 * ticked, and when it finishes, a failure shows the failure dialog, a join opens the alliance
 * network screen, and a host opens mission setup. */
int XvtNetworkTask_Resume(int* result);
/* Cancels the attempt and returns to the host or join screen by the attempt's action; call only
 * during an attempt, since an idle task reads as a host. */
void XvtNetworkTask_Cancel(void);
/* Resets the network session and forgets the attempt and the browser state. */
void XvtNetworkTask_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif

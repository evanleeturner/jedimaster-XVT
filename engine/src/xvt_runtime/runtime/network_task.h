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

struct xvt_network_preview {
	char title[256];
	char text[4096];
	int scroll;
};

/* Starts a host (either host action) or a join of the selected room (CONNECT), sending the
 * pilot's rating and name; a join first copies the room name into the pilot's game name. Ignored
 * while an attempt is active, and for CONNECT unless CanJoin. */
void xvt_network_task_begin(int action);
/* 1 while a host or join attempt runs. */
int xvt_network_task_is_active(void);
/* Leaves any network session, enters client mode and starts a refresh. */
void xvt_network_task_open_browser(void);
/* Asks the directory for a new snapshot and schedules the next automatic refresh 10 seconds out;
 * a configuration or request error is recorded as the browser error. */
void xvt_network_task_refresh(void);
/* Per host frame while the browser is visible: copies the directory snapshot while a refresh is
 * pending; on success clears the error, re-finds the selected room by id (dropping the selection
 * when it is gone), keeps the scroll in range and a moved selection visible, and reloads the
 * preview when the selected mission changed or nothing is selected; on failure records the error.
 * Refreshes every 10 seconds. */
void xvt_network_task_service_browser(void);
/* 1 while the join screen is anywhere on the screen stack and no attempt runs. */
int xvt_network_task_browser_visible(void);
/* The latest directory snapshot. */
const AeronDplayDirectorySnapshot *xvt_network_task_snapshot(void);
/* The selected room in the snapshot, or NULL. */
const AeronDplayDirectoryRoom *xvt_network_task_selected_room(void);
/* The selected room's index, or -1. */
int xvt_network_task_selected_index(void);
/* The browser's list scroll offset, for the list draw to read and write. */
int *xvt_network_task_scroll_offset(void);
/* The selected room's mission title and description, read from the mission files; the text is
 * "Description unavailable" when they cannot be read, and both are empty with no selection. */
struct xvt_network_preview *xvt_network_task_preview(void);
/* Seconds since the last successful refresh, 0 before one, at most 359999. */
unsigned xvt_network_task_snapshot_age(void);
/* The last refresh error, cleared by the next successful refresh. */
AeronDplayDirectoryError xvt_network_task_browser_error(void);
/* 1 when room uses this directory protocol and its game version is FRONTEND_NET_PROTOCOL_VERSION
 * in decimal. */
int xvt_network_task_compatible(const AeronDplayDirectoryRoom *room);
/* 1 when no attempt runs, the browser has no error, the snapshot is available and the selected
 * room is compatible, joinable and not full. */
int xvt_network_task_can_join(void);
/* Selects room index and loads its preview; the selected index again, or one out of range, clears
 * the selection. */
void xvt_network_task_toggle_selection(int index);
/* Called under the frontend draw lock in place of the suspended screen update. */
/* With no attempt: when the network session has failed, shows the failure
 * dialog, sets result to 0 and returns 1; otherwise returns 0. During an
 * attempt, sets result to 0 and returns 1: Escape, or the cancel button of the
 * connecting screen drawn otherwise, cancels; otherwise the session is ticked,
 * and when it finishes, a failure shows the failure dialog, a join opens the
 * await-admission screen, and a host opens mission setup. */
int xvt_network_task_resume(int *result);
/* Cancels the attempt and returns to the host or join screen by the attempt's action; call only
 * during an attempt, since an idle task reads as a host. */
void xvt_network_task_cancel(void);
/* Leaves the network session and forgets the attempt and the browser state. */
void xvt_network_task_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif

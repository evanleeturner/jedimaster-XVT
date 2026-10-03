#ifndef XVT_RUNTIME_LAUNCH_TASK_H
#define XVT_RUNTIME_LAUNCH_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/* The hand-off from the frontend to a flight: Queue builds the flight command, a music fade runs,
 * the port takes the pending command and starts the flight task, and Complete brings the frontend
 * back. Phases: idle, fade, pending, running. */

/* The modern body of FrontendFlight_LaunchSession. Writes the config, saves the pilot, checks the
 * installation and the selected mission, builds the flight command into
 * g_frontendFlightCommandLine, hides the cursor, starts a music fade when datapad music plays, and
 * returns 0 with the fade phase entered. Returns 0 and does nothing while a launch is active, and
 * 1 when saving the pilot fails. A missing installation or mission, or a command over its length,
 * ends the program through XvtStorage_Fatal. */
int XvtLaunchTask_Queue(void);
/* Moves fade to pending once the fade ends. Escape during fade or pending cancels the launch
 * through Complete(0). */
void XvtLaunchTask_Update(void);
/* 1 in any phase but idle. */
int XvtLaunchTask_IsActive(void);
/* 1 in the pending phase. */
int XvtLaunchTask_HasPendingLaunch(void);
/* From pending only: releases the frontend display surfaces for flight, enters the running phase
 * and returns the flight command; otherwise returns NULL. */
const char *XvtLaunchTask_BeginPendingLaunch(void);
/* Ignored when idle. Returns to idle and cancels the fade. After a running flight, restores the
 * frontend surfaces (a failure ends the program), reloads the sound list, writes the config,
 * refreshes the rating name, saves the pilot and reinitializes CD audio. With datapad music on,
 * plays track 7 at the configured volume. Then opens the debrief when succeeded, or shows the
 * cursor, shuts down the DirectPlay session and opens the concourse. The new screen starts at
 * frame 0, and no exit callback runs for the switch. */
void XvtLaunchTask_Complete(int succeeded);
/* Returns to idle and clears the command without restoring anything. */
void XvtLaunchTask_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RUNTIME_FRONTEND_TASK_H
#define XVT_RUNTIME_FRONTEND_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The frontend's frame loop on the host clock: paces screen frames, runs the top screen's update
 * and exit callbacks, and holds a frame while a dialog, campaign prefix or network task is
 * suspended in it. */

/* Clears the frontend state and its latches, allocates the sound and image tables, loads the
 * config, creates the main window and installs the first screen: the concourse when skip_intro is
 * set, otherwise the opening movie and credits. Returns 1, or 0 when an allocation or the window
 * fails; the task counts as initialized either way, so Shutdown still cleans up. */
int xvt_frontend_task_init(int skip_intro);
/* Runs at most one frame per frame interval, and nothing once ShouldQuit is set. The first frame
 * after Init finishes startup: resource loading with skip_intro, bootstrap mode otherwise. While a
 * launch is active only the launch task updates; while a dialog is active only the dialog updates;
 * otherwise runs run_frame, where a result of 1 or 2 sets ShouldQuit. Then presents the frame
 * unless a movie, dialog result, continuation, campaign prefix or network task is holding the last
 * presented one, or the credits screen is fading out. */
void xvt_frontend_task_update(void);
/* Runs one frame of the top screen; returns 0 when it has no update callback. A
 * waiting network task or dialog continuation is resumed in place of the
 * update. While a campaign prefix is pending or the network task is active, or
 * when the update opened a dialog, returns 0 with the frame counter unchanged,
 * so the screen repeats that frame. Otherwise runs the exit callback captured
 * before the update when the callbacks changed or the result is 1, pushes any
 * pending screen, draws the visible cursor, advances the frame counter and
 * returns the update's result. A back buffer that cannot be locked ends the
 * program. */
int xvt_frontend_task_run_frame(void);
/* Per host frame outside flight: polls both joysticks at most every 100 ms, pumps network packets
 * unless the network task is active, and updates the CD task. */
void xvt_frontend_task_service_frame_systems(void);
/* 1 once a frame returned 1 or 2 through Update. */
int xvt_frontend_task_should_quit(void);
/* Microseconds until the next frame or CD task event, whichever is sooner; 0 when overdue. */
uint64_t xvt_frontend_task_next_wake_delay_us(void);
/* After Init only: stops the campaign, network, movie, CD, launch and dialog tasks, saves the
 * pilot and writes the config when the UI strings were loaded (a failed pilot save ends the
 * program), exits the concourse, then frees the frontend's lists, memory handles, sound buffers,
 * display and buffers. */
void xvt_frontend_task_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RUNTIME_PORT_H
#define XVT_RUNTIME_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game runtime as seen by the host application: one Update per host frame drives, in order, the
 * input, the host clock, the CD task, the movie, flight or frontend task, a finished or pending
 * flight launch, the network session and the render snapshot. */

/* Main-thread contract. Aeron and render snapshot storage must outlive the port. */
/* Requires a 640x480 Aeron logical size; otherwise logs, sets exit code 1 and returns 0. Resets the
 * clock, presentation and input and starts the frontend; a frontend that fails to start sets exit
 * code 1, shuts the port down and returns 0. Returns 1, at once when already initialized. */
int xvt_port_init(void);
/* Before Init only: start at the concourse instead of the opening movie. */
void xvt_port_set_skip_intro(int skip_intro);
/* 1 between a successful Init and Shutdown. */
int xvt_port_is_initialized(void);
/* Runs one host frame. Does nothing once ServiceQuit returns 1; while quitting, only updates DirectPlay
 * and services the network session. Runs a paused frame instead when the settings menu is open,
 * or the window lacks focus and no movie or campaign wait continues without it, unless the network
 * requires progress. The first frame after Init, a pause, or a flight's start or end does not
 * advance the host clock by delta_us. */
void xvt_port_update(int32_t delta_us);
/* Marks the port paused (pausing audio on the first such frame), discards input, keeps a movie
 * paused, presents and commits the snapshot; the next Update resumes without advancing the clock. */
void xvt_port_paused_frame(void);
/* Records whether the settings menu is open; an open menu pauses Update unless the network requires
 * progress. */
void xvt_port_set_settings_open(int open);
/* Latches a request to open the settings menu. */
void xvt_port_request_settings(void);
/* Returns and clears the settings request latch. */
int xvt_port_consume_settings_request(void);
/* 1 while DirectPlay, the network task or the game browser is active, or a multiplayer flight
 * runs; Update then keeps running without focus or with settings open. */
int xvt_port_network_requires_progress(void);
/* 1 when not initialized or on a fatal error. The first call after Aeron or the frontend asks to
 * quit starts quitting: it shuts down the network task and the DirectPlay session. Once quitting,
 * returns 1 when DirectPlay is no longer active. */
int xvt_port_service_quit(void);
/* 1 after a fatal error, otherwise the exit code set by Init (0 or 1). */
int xvt_port_get_exit_code(void);
/* Relative to Aeron_BeginFrame, UINT64_MAX when no task has a deadline. */
/* While quitting, DirectPlay's delay; while paused, UINT64_MAX; otherwise the soonest of the active
 * task (movie, flight or frontend), the CD task and DirectPlay. */
uint64_t xvt_port_next_wake_delay_us(void);
/* Lifts the classic rendering suppression; after Init, shuts down the movie, flight and frontend
 * tasks, DirectPlay, the network session, input, the Aeron compatibility layers and presentation,
 * restores the mouse, cursor and audio, commits an empty-scene snapshot and resets the clock. */
void xvt_port_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif

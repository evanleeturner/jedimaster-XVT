#ifndef XVT_RUNTIME_MOVIE_TASK_H
#define XVT_RUNTIME_MOVIE_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One Smacker movie at a time, played without blocking: centered at native size in the classic
 * frame, with subtitles from movies/<name>.txt, and in network sessions synchronized with the
 * other players. Results: 0 finished or skipped, 2 not opened or a playback error, 3 larger than
 * 640x480, 5 aborted by the multiplayer skip. */

enum { XVT_MOVIE_PENDING = -1 };

/* Opens movies/<name>.smk and starts it, saving the frontend's window mode, offscreen restore and
 * back-buffer lock. synchronize takes effect only in a network session, where a missing movie
 * falls back to Flyby1a. Returns -1 once playing; returns 2 without playing for an empty or
 * over-long name, a movie that cannot be found or opened, or while a movie is active, its result
 * untaken, or its player not yet reaped. */
int xvt_movie_task_begin(const char *name, int synchronize);
/* Advances the active movie one host frame and draws it with its subtitles. A key or click skips
 * it, or in a network session goes to the multiplayer skip handler. Completes when playback ends,
 * fails or the frame proves larger than 640x480, or in a network session when every player has
 * finished or the skip aborted it. */
void xvt_movie_task_update(void);
/* Pauses the active movie and redraws its current frame. */
void xvt_movie_task_paused_frame(void);
/* Once the movie has completed, closes its player and subtitles and restores the saved window
 * mode, offscreen restore and back-buffer lock; call on the host frame after completion. */
void xvt_movie_task_reap_finished(void);
/* 1 while a movie plays, including a network session's wait for the other players. */
int xvt_movie_task_is_active(void);
/* 1 while a synchronized movie plays. */
int xvt_movie_task_continues_without_focus(void);
/* Stores the completed movie's result and returns 1, once it has been reaped; otherwise 0. */
int xvt_movie_task_take_result(int *result);
/* Stops the active movie's playback; it counts as finished. A synchronized movie then waits for
 * the other players. */
void xvt_movie_task_stop(void);
/* Cancels the last subtitle layer submission. */
void xvt_movie_task_suppress_classic_subtitles(void);
/* The player's next frame delay while a movie plays, otherwise 10 ms. */
uint64_t xvt_movie_task_next_wake_delay_us(void);
/* Closes any movie and forgets its state and result, without restoring the saved frontend state. */
void xvt_movie_task_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RUNTIME_FRONTEND_MOVIES_H
#define XVT_RUNTIME_FRONTEND_MOVIES_H

#ifdef __cplusplus
extern "C" {
#endif

/* The pilot record's movie viewer: starts a movie without blocking and restores the pilot record
 * screen once the movie task reports a result. */

/* Plays name through movie_play without multiplayer sync. Returns 1 when the movie is pending, and
 * ResumeViewer then waits for it; otherwise 0. movie_play first hands back any
 * result not yet taken from the movie task; then name is not started and this returns 0. */
int xvt_frontend_movies_play_viewer(const char *name);
/* Returns 0 when no viewer movie is pending, and 1 while it still plays. When its result arrives,
 * discards it, clears and presents the display, redraws the pilot record background, requests CD
 * audio resume and returns 0. */
int xvt_frontend_movies_resume_viewer(void);
/* Forgets a pending viewer movie without stopping it or taking its result. */
void xvt_frontend_movies_reset(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RUNTIME_CUTSCENE_TASK_H
#define XVT_RUNTIME_CUTSCENE_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/* The modern body of cutscene_play_for_current_mission_phase: plays the cutscenes that match the
 * current mission and phase one movie at a time, returning to the caller while each plays. */

/* Starts a run only in the training-exercise mission directory with a mission sequence active and
 * a cutscene table loaded; otherwise returns 0. The call that starts a run records phase; later
 * calls continue the run and ignore phase. Plays, in table order, each entry whose mission index,
 * phase and description id match the pilot's current mission, suspending CD audio for the movie
 * and restoring the frontend display and requesting CD audio resume after it. Call again while
 * it returns -1. Returns 1 once every match has played or none matched; returns 0 when a movie's
 * result is nonzero, skipping the remaining matches. Every return other than -1 leaves no run
 * active. */
/* Returns -1 while a movie is pending, otherwise the recovered cutscene result. */
int xvt_cutscene_task_play(int phase);
/* Ends any run without stopping a pending movie or touching display or audio; the next Play starts
 * over. */
void xvt_cutscene_task_reset(void);

#ifdef __cplusplus
}
#endif

#endif

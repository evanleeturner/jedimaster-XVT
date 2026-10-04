#ifndef XVT_RUNTIME_DIALOG_TASK_H
#define XVT_RUNTIME_DIALOG_TASK_H

#include "xvt/frontend/frontend_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One modal frontend dialog at a time, run without blocking. Begin records the
 * parent screen's state, Update runs the dialog as a pushed screen, and ending
 * it restores the parent. The result is held until taken, by the caller or by a
 * registered continuation. */

enum { XVT_DIALOG_PENDING = -1 };

typedef int (*xvt_dialog_continuation)(int result, int context);

/* Registers continuation to run with the dialog's result and context in place of the parent's
 * next frame once the dialog ends. Returns 0, so a screen can return it directly. */
int xvt_dialog_continue_with(xvt_dialog_continuation continuation, int context);
/* When a dialog has ended and a continuation is registered, takes the result, runs the
 * continuation once, stores its return in frame_result and returns 1; otherwise returns 0. */
int xvt_dialog_resume_continuation(int *frame_result);

/* Opens a dialog drawn by update in rect (the whole screen when NULL), saving the parent's frame
 * counter, pending callback change, offscreen restore, cursor and overlay text. Does nothing
 * while a dialog is active or a result is untaken. Returns -1. */
int xvt_dialog_begin(frontend_screen_update_fn update, const struct RECT *rect);
/* Runs the active dialog for one frame, pushing its screen first (a failed push ends the
 * program). Escape after the first frame ends it with result 0; an update returning 1 ends it
 * with the dialog's result, or for the pilot-name prompt, 1 when a name was typed. Ending
 * restores the parent's saved state, flushes the keyboard and clears the click latches. */
void xvt_dialog_update(void);
/* 1 while a dialog is open. */
int xvt_dialog_is_active(void);
/* 1 while the pilot-name prompt is open. */
int xvt_dialog_is_text_prompt(void);
/* 1 from the end of a dialog until its result is taken. */
int xvt_dialog_has_result(void);
/* Stores the untaken result and returns 1, or returns 0 when there is none. */
int xvt_dialog_take_result(int *result);
/* Call every frame until it returns something other than -1. Returns any untaken result first,
 * whichever dialog produced it; returns -1 while a dialog is open; otherwise copies the three
 * lines and two button labels (NULL as empty), plays the warning sound when enabled, and opens a
 * confirm dialog, or the network abort dialog when network is set, returning -1. */
int xvt_dialog_confirm(const char *line1, const char *line2, const char *line3,
		       const char *okay_label, const char *cancel_label,
		       int network);
/* Like Confirm, for the pilot-name prompt: returns any untaken result first, whichever dialog
 * produced it, copying the first 12 characters of the first dialog text line into name and
 * terminating it (name needs 13 bytes). For this prompt that line is the typed name, empty when
 * escaped; after a confirm dialog it is the confirm's first line. Returns -1 until then. */
int xvt_dialog_pilot_name(char *name);
/* Ends an open dialog, restoring its parent, and forgets any result and continuation. */
void xvt_dialog_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif

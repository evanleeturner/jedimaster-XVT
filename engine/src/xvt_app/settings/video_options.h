#ifndef XVT_APP_VIDEO_OPTIONS_H
#define XVT_APP_VIDEO_OPTIONS_H
#include "xvt_runtime/config/video_config.h"
/* The video settings as the menu edits them: the requested record, the accepted one (applied to the
 * renderer), the persisted one (written to the user overrides) and the shipped defaults. A request is
 * applied at the next ApplyPending through the configured apply function and written to the config by
 * Flush, on close or exit. One static state. */
typedef bool (*xvt_video_apply_fn)(const struct xvt_video_settings *previous,
				   const struct xvt_video_settings *requested,
				   char *error, size_t capacity);
/* Reads the shipped defaults and the current settings as the defaults and the requested record, each
 * with MSAA forced to 1 sample when its temporal upscaling is on; makes the accepted and persisted
 * records the requested one; keeps apply; clears the pending and restore flags; notes whether the window
 * is fullscreen. */
void xvt_video_options_configure(xvt_video_apply_fn apply);
/* Copies the requested record, the one the page shows. */
void xvt_video_options_get(struct xvt_video_settings *out);
/* Validates options (false with error otherwise), makes them the requested record, marks a change
 * pending when they differ from the accepted record, and clears the restore flag. */
bool xvt_video_options_request(const struct xvt_video_settings *options,
			       char *error, size_t capacity);
/* Requests the shipped defaults, pending when they differ from the accepted record, and sets the restore
 * flag, so the next Flush writes even when nothing differs from the persisted record. */
void xvt_video_options_restore_defaults(void);
/* Once per frame. A fullscreen change observed on the window while nothing is pending is adopted into the
 * requested and accepted records. With a change pending: clears it and calls apply with the accepted and
 * requested records; on failure, or without an apply function, the request reverts to the accepted
 * record, the restore flag clears and false is returned with apply's error (error is left untouched
 * without an apply function); on success the requested record becomes accepted and the fullscreen state
 * is read again. true when nothing was pending. */
bool xvt_video_options_apply_pending(char *error, size_t capacity);
/* Writes the accepted record, or the requested one when exiting, to the user overrides: nothing when it
 * equals the persisted record and no restore is flagged; the video overrides are removed when it equals
 * the defaults, else set. On success it becomes the persisted record and the restore flag clears. */
bool xvt_video_options_flush(bool exiting, char *error, size_t capacity);
#endif

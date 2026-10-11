#ifndef XVT_RUNTIME_LAUNCH_RECORD_H
#define XVT_RUNTIME_LAUNCH_RECORD_H

#ifdef __cplusplus
extern "C" {
#endif

/* A diagnostic: the state a battle starts from, written to files so two ways
 * of starting the same battle can be compared field by field. State: the
 * path given at start, empty until set. */

/* Remembers path (copied, at most 511 characters) as the recording to write at
 * the next battle start. An empty or NULL path switches recording off. */
void xvt_launch_record_set_path(const char *path);
/* 1 when a recording path is set. */
int xvt_launch_record_is_enabled(void);
/* Writes the recording when a path is set, and does nothing otherwise. Call it
 * where the battle starts, with the flight command line. Two files result: the
 * pilot record bytes as they are, in "<path>.pilot", and "<path>" holding one
 * line "name size hex" for each value the battle reads from the menus: the
 * launch line, the session mode, the host flag, the human player count, the
 * local player index, every member of the game settings, of each roster entry
 * and of each player's flight state, and the team assignment tables. Returns 1
 * when both files were written, else 0 with an error logged. */
int xvt_launch_record_write(const char *command);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_RUNTIME_LAUNCH_DOOR_H
#define XVT_RUNTIME_LAUNCH_DOOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The door --launch opens: starts one single-player battle from a JSON
 * description without the menus, and ends the program with a result file.
 * It does what the menus do, by the same game functions, in this order: the
 * frontend starts with the intro skipped and the description's pilot as the
 * last pilot; on the first frame of the concourse it picks the side and
 * presses Fly Solo; on the first frame of the mission setup screen it picks
 * the mission, sets the settings and presses Quick Start; the screens after
 * that run as they do for a player. When the battle ends it saves the pilot,
 * writes the result file and asks the program to quit. State: the description,
 * the result path and the phase. */

/* Reads and checks the description in the file at path. On success keeps it
 * and returns 1. Otherwise writes a result file that says why (beside the
 * description as "<path>.result.json" when it names none) and returns 0; the
 * caller ends the program with XVT_LAUNCH_EXIT_REFUSED. A description that
 * names an AI program starts it first (ai_program.h), telling it run_id, the
 * engine's log level and ai_log_file ("" for standard error only), so its log
 * names the engine's run; a program that cannot be started is a refusal. */
int xvt_launch_door_open(const char *path, uint32_t run_id,
			 const char *ai_log_file);
/* 1 from a successful open until the result is written. */
int xvt_launch_door_is_active(void);
/* Advances the door by one step after a frontend frame; does nothing when
 * closed, or while the flight runs. Past the time allowed to reach the
 * battle it ends the program with XVT_LAUNCH_EXIT_NOT_REACHED. */
void xvt_launch_door_step(void);
/* Called when the flight has ended, flown 1 when it ran. Saves nothing: the
 * launch task has saved the pilot. Writes the result file from the pilot
 * record and asks the program to quit with the matching exit code. */
void xvt_launch_door_finish(int flown);

#ifdef __cplusplus
}
#endif

#endif

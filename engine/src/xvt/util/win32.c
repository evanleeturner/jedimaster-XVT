#include "xvt/util/win32.h"

#include <string.h>

/* In the original build, starts the program commandLine names, with default
 * startup settings, and returns CreateProcessA's result: nonzero when the
 * process started. Does not wait for it, and never closes the handles of the
 * new process or its thread. The modern build starts nothing and returns 0.
 * Only the original build calls this: net_shutdown_direct_play_session_ex starts
 * "z_xvt__.exe skipintro" just before the game exits. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC570
int win32_create_process_from_command_line(char *command_line)
{
	(void)command_line;
	return 0;
}

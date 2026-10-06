#include "xvt/util/win32.h"

#include <string.h>

/* Starts nothing and returns 0. Nothing calls it; the 1997 game started the
 * program commandLine names here. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC570
int win32_create_process_from_command_line(char *command_line)
{
	(void)command_line;
	return 0;
}

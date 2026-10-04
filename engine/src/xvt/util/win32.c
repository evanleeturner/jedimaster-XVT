#include "xvt/util/win32.h"

#include <string.h>

#ifndef XVT_MODERN
struct win32_startup_info {
	uint32_t cb;		 /* Bytes in this structure; set to its size. */
	char *reserved;		 /* Reserved; set to NULL. */
	char *desktop;		 /* Desktop to start on; set to NULL. */
	char *title;		 /* Console window title; left NULL. */
	uint32_t x;		 /* Window x; left 0. */
	uint32_t y;		 /* Window y; left 0. */
	uint32_t x_size;	 /* Window width; left 0. */
	uint32_t y_size;	 /* Window height; left 0. */
	uint32_t x_count_chars;	 /* Console columns; left 0. */
	uint32_t y_count_chars;	 /* Console rows; left 0. */
	uint32_t fill_attribute; /* Console colors; left 0. */
	uint32_t flags; /* Which fields above count; set to 0, so none do. */
	uint16_t show_window;	 /* Show state; left 0. */
	uint16_t reserved2_size; /* Reserved; set to 0. */
	uint8_t *reserved2;	 /* Reserved; set to NULL. */
	void *standard_input;	 /* Left NULL. */
	void *standard_output;	 /* Left NULL. */
	void *standard_error;	 /* Left NULL. */
};

struct win32_process_information {
	void *process; /* New process's handle; nothing reads or closes it. */
	/* Its first thread's handle; nothing reads or closes it. */
	void *thread;
	uint32_t process_id; /* New process's id; nothing reads it. */
	uint32_t thread_id;  /* Its first thread's id; nothing reads it. */
};

__declspec(dllimport) int __stdcall
CreateProcessA(const char *application_name, char *command_line,
	       void *process_attributes, void *thread_attributes,
	       int inherit_handles, uint32_t creation_flags, void *environment,
	       const char *current_directory,
	       struct win32_startup_info *startup_info,
	       struct win32_process_information *process_information);
#endif

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
#ifdef XVT_MODERN
	(void)command_line;
	return 0;
#else
	struct win32_startup_info startup_info;

	memset(&startup_info, 0, sizeof(startup_info));
	startup_info.cb = sizeof(startup_info);
	startup_info.reserved2_size = 0;
	startup_info.reserved = NULL;
	startup_info.reserved2 = NULL;
	startup_info.desktop = NULL;
	startup_info.flags = 0;

	struct win32_process_information process_information;
	return CreateProcessA(NULL, command_line, NULL, NULL, 0, 0, NULL, NULL,
			      &startup_info, &process_information);
#endif
}

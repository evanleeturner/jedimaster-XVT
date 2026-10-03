#include "xvt/util/win32.h"

#include <string.h>

#ifndef XVT_MODERN
struct Win32StartupInfo {
	uint32_t cb;		/* Bytes in this structure; set to its size. */
	char *reserved;		/* Reserved; set to NULL. */
	char *desktop;		/* Desktop to start on; set to NULL. */
	char *title;		/* Console window title; left NULL. */
	uint32_t x;		/* Window x; left 0. */
	uint32_t y;		/* Window y; left 0. */
	uint32_t xSize;		/* Window width; left 0. */
	uint32_t ySize;		/* Window height; left 0. */
	uint32_t xCountChars;	/* Console columns; left 0. */
	uint32_t yCountChars;	/* Console rows; left 0. */
	uint32_t fillAttribute; /* Console colors; left 0. */
	uint32_t flags; /* Which fields above count; set to 0, so none do. */
	uint16_t showWindow;	/* Show state; left 0. */
	uint16_t reserved2Size; /* Reserved; set to 0. */
	uint8_t *reserved2;	/* Reserved; set to NULL. */
	void *standardInput;	/* Left NULL. */
	void *standardOutput;	/* Left NULL. */
	void *standardError;	/* Left NULL. */
};

struct Win32ProcessInformation {
	void *process; /* New process's handle; nothing reads or closes it. */
	/* Its first thread's handle; nothing reads or closes it. */
	void *thread;
	uint32_t processId; /* New process's id; nothing reads it. */
	uint32_t threadId;  /* Its first thread's id; nothing reads it. */
};

__declspec(dllimport) int __stdcall
CreateProcessA(const char *applicationName, char *commandLine,
	       void *processAttributes, void *threadAttributes,
	       int inheritHandles, uint32_t creationFlags, void *environment,
	       const char *currentDirectory,
	       struct Win32StartupInfo *startupInfo,
	       struct Win32ProcessInformation *processInformation);
#endif

/* In the original build, starts the program commandLine names, with default
 * startup settings, and returns CreateProcessA's result: nonzero when the
 * process started. Does not wait for it, and never closes the handles of the
 * new process or its thread. The modern build starts nothing and returns 0.
 * Only the original build calls this: Net_ShutdownDirectPlaySessionEx starts
 * "z_xvt__.exe skipintro" just before the game exits. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC570
int Win32_CreateProcessFromCommandLine(char *commandLine)
{
#ifdef XVT_MODERN
	(void)commandLine;
	return 0;
#else
	struct Win32StartupInfo startupInfo;
	struct Win32ProcessInformation processInformation;

	memset(&startupInfo, 0, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.reserved2Size = 0;
	startupInfo.reserved = NULL;
	startupInfo.reserved2 = NULL;
	startupInfo.desktop = NULL;
	startupInfo.flags = 0;

	return CreateProcessA(NULL, commandLine, NULL, NULL, 0, 0, NULL, NULL,
			      &startupInfo, &processInformation);
#endif
}

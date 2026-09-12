#include "xvt/util/win32.h"

#include <string.h>

#ifndef XVT_MODERN
struct Win32StartupInfo {
	uint32_t cb;
	char* reserved;
	char* desktop;
	char* title;
	uint32_t x;
	uint32_t y;
	uint32_t xSize;
	uint32_t ySize;
	uint32_t xCountChars;
	uint32_t yCountChars;
	uint32_t fillAttribute;
	uint32_t flags;
	uint16_t showWindow;
	uint16_t reserved2Size;
	uint8_t* reserved2;
	void* standardInput;
	void* standardOutput;
	void* standardError;
};

struct Win32ProcessInformation {
	void* process;
	void* thread;
	uint32_t processId;
	uint32_t threadId;
};

__declspec(dllimport) int __stdcall CreateProcessA(const char* applicationName, char* commandLine,
												   void* processAttributes, void* threadAttributes,
												   int inheritHandles, uint32_t creationFlags,
												   void* environment, const char* currentDirectory,
												   struct Win32StartupInfo* startupInfo,
												   struct Win32ProcessInformation* processInformation);
#endif

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC570
int Win32_CreateProcessFromCommandLine(char* commandLine) {
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

	return CreateProcessA(NULL, commandLine, NULL, NULL, 0, 0, NULL, NULL, &startupInfo, &processInformation);
#endif
}

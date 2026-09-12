#ifndef XVT_UTIL_DEBUG_CONSOLE_H
#define XVT_UTIL_DEBUG_CONSOLE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_debugConsoleInitialized;
#ifndef XVT_MODERN
extern uint8_t g_debugConsoleTextBuffer[80 * 25 * 2];
#endif

void DebugPrintf(const char* format, ...);
void DebugConsole_SetInitialized(int initialized);
void DebugConsole_SetCursorPosition(int column, int row);
int DebugConsole_WriteText(const char* text);
void DebugConsole_WriteTextInScrollRegion(int topRow, int bottomRow, const char* text);
void DebugConsole_ToggleFileDump(void);

#ifdef __cplusplus
}
#endif

#endif

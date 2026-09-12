#include "xvt/util/debug_console.h"

#include "xvt/assets/file.h"

#include <stdio.h>
#include <string.h>

// GLOBAL: XVT 0x5235E4
static int g_debugConsoleFileDumpGate0 = 0;
// GLOBAL: XVT 0x5235E8
static int g_debugConsoleFileDumpGate1 = 0;
// GLOBAL: XVT 0x5235EC
static int g_debugConsoleFileDumpGate2 = 1;
// GLOBAL: XVT 0x528100
int g_debugConsoleInitialized;
// GLOBAL: XVT 0x528104
int g_debugConsoleCursorColumn;
// GLOBAL: XVT 0x528108
int g_debugConsoleCursorRow;
// GLOBAL: XVT 0x52810C
int g_debugConsoleScrollTopRow = 0;
// GLOBAL: XVT 0x528110
int g_debugConsoleScrollBottomRow = 24;
// GLOBAL: XVT 0x528114
int g_debugConsoleFileDumpEnabled;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079E0
void DebugPrintf(const char* format, ...) { (void)format; }

// FUNCTION: XVT 0x4ACBF0
void DebugConsole_SetInitialized(int initialized) {
	g_debugConsoleFileDumpEnabled = 0;
	g_debugConsoleInitialized = initialized;
}

// FUNCTION: XVT 0x4ACC10
void DebugConsole_SetCursorPosition(int column, int row) {
	g_debugConsoleCursorColumn = column;
	g_debugConsoleCursorRow = row;
}

#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma function(memcpy)
#endif
// FUNCTION: XVT 0x4ACC30
int DebugConsole_WriteText(const char* text) {
	uint8_t* textBuffer;
	const char* textCursor;
	int sourceLength;
	int remainingLength;
	int charIndex;
	int zero;
	XvtFile* stream;
	uint8_t* textCell;

#ifdef XVT_MODERN
	static uint8_t modernTextBuffer[80 * 25 * 2];
	textBuffer = modernTextBuffer;
#else
	textBuffer = g_debugConsoleTextBuffer;
#endif
	if (g_debugConsoleInitialized == 0) {
		int initializeCount;
		uint8_t* initializeCell;

		initializeCell = textBuffer;
		initializeCount = 2000;
		do {
			*initializeCell++ = ' ';
			*initializeCell++ = 7;
			--initializeCount;
		} while (initializeCount != 0);
		g_debugConsoleInitialized = 1;
	}

	if (g_debugConsoleFileDumpEnabled != 0) {
#ifdef XVT_MODERN
		stream = File_Open("mpDump.txt", "a");
		textCursor = text;
		if (stream != NULL) {
			File_WriteCount(stream, text, strlen(text));
			File_Close(stream);
		}
#else
		stream = File_RawOpen("mpDump.txt", "a");
		textCursor = text;
		if (stream != NULL) {
			File_Printf(stream, "%s", text);
			File_RawClose(stream);
		}
#endif
	} else {
		textCursor = text;
	}

	if (*textCursor == '\n') {
		++textCursor;
		g_debugConsoleCursorColumn = 0;
		++g_debugConsoleCursorRow;
	}

	zero = 0;
	for (;;) {
		remainingLength = strlen(textCursor);
		sourceLength = remainingLength;
		if (g_debugConsoleCursorRow > g_debugConsoleScrollBottomRow) {
#ifdef XVT_MODERN
			memmove(&textBuffer[160 * g_debugConsoleScrollTopRow],
					&textBuffer[160 * g_debugConsoleScrollTopRow + 160],
					(size_t)(160 * (g_debugConsoleScrollBottomRow - g_debugConsoleScrollTopRow)));
#else
			memcpy(&textBuffer[160 * g_debugConsoleScrollTopRow],
				   &textBuffer[160 * g_debugConsoleScrollTopRow + 160],
				   (size_t)(160 * (g_debugConsoleScrollBottomRow - g_debugConsoleScrollTopRow)));
#endif
			{
				int clearCount;
				uint8_t* clearCell;

				clearCell = &textBuffer[160 * g_debugConsoleScrollBottomRow];
				clearCount = 80;
				do {
					*clearCell = ' ';
					++clearCell;
					++clearCell;
					--clearCount;
				} while (clearCount != 0);
			}
			g_debugConsoleCursorRow = g_debugConsoleScrollBottomRow;
		}

		if (remainingLength + g_debugConsoleCursorColumn > 80)
			remainingLength = 80 - g_debugConsoleCursorColumn;

		charIndex = zero;
		textCell = &textBuffer[2 * (g_debugConsoleCursorColumn + 80 * g_debugConsoleCursorRow)];
		if (remainingLength > 0) {
			for (;;) {
				if (textCursor[charIndex] == '\n') {
					textCursor += charIndex + 1;
					sourceLength -= charIndex + 1;
					remainingLength = zero;
					g_debugConsoleCursorColumn = zero;
					++g_debugConsoleCursorRow;
					break;
				}
				textCell[charIndex * 2] = (uint8_t)textCursor[charIndex];
				++charIndex;
				if (remainingLength > charIndex)
					continue;
				break;
			}
		}

		g_debugConsoleCursorColumn += remainingLength;
		if (remainingLength == sourceLength)
			return g_debugConsoleCursorColumn;
		textCursor += remainingLength;
		g_debugConsoleCursorColumn = zero;
		++g_debugConsoleCursorRow;
	}
}
#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma intrinsic(memcpy)
#endif

// FUNCTION: XVT 0x4ACDD0
void DebugConsole_WriteTextInScrollRegion(int topRow, int bottomRow, const char* text) {
	g_debugConsoleScrollBottomRow = bottomRow;
	g_debugConsoleScrollTopRow = topRow;
	g_debugConsoleCursorRow = bottomRow;
	if (g_debugConsoleCursorColumn == 0)
		g_debugConsoleCursorRow = bottomRow + 1;
	DebugConsole_WriteText(text);
	g_debugConsoleScrollTopRow = 0;
	g_debugConsoleScrollBottomRow = 24;
}

// FUNCTION: XVT 0x4ACE20
void DebugConsole_ToggleFileDump(void) {
	if (g_debugConsoleFileDumpEnabled != 0) {
		g_debugConsoleFileDumpEnabled = 0;
		return;
	}

	if (g_debugConsoleFileDumpGate0 != 0 || g_debugConsoleFileDumpGate1 != 0 ||
		g_debugConsoleFileDumpGate2 != 0) {
		++g_debugConsoleFileDumpEnabled;
	}
}

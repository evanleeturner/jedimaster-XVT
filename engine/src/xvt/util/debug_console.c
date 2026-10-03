#include "xvt/util/debug_console.h"

#include "xvt/assets/file.h"

#include <stdio.h>
#include <string.h>

/* The first of three switches DebugConsole_ToggleFileDump checks: it turns the
 * file dump on only when one of them is nonzero. Starts at 0, and nothing
 * writes it. */
// GLOBAL: XVT 0x5235E4
static int g_debugConsoleFileDumpGate0 = 0;
/* The second of those three switches. Starts at 0, and nothing writes it. */
// GLOBAL: XVT 0x5235E8
static int g_debugConsoleFileDumpGate1 = 0;
/* The third of those three switches. Starts at 1 and nothing writes it, so
 * DebugConsole_ToggleFileDump may always turn the dump on. */
// GLOBAL: XVT 0x5235EC
static int g_debugConsoleFileDumpGate2 = 1;
/* 1 once DebugConsole_WriteText has filled its text buffer with blanks; while
 * it is 0, the next call does that first. DebugConsole_SetInitialized, which
 * nothing calls, is its only other writer. */
// GLOBAL: XVT 0x528100
int g_debugConsoleInitialized;
/* Column, 0 to 80, where DebugConsole_WriteText writes next. Written by
 * DebugConsole_WriteText and by DebugConsole_SetCursorPosition, which nothing
 * calls. */
// GLOBAL: XVT 0x528104
int g_debugConsoleCursorColumn;
/* Row where DebugConsole_WriteText writes next. When it is past
 * g_debugConsoleScrollBottomRow, the next write scrolls the region up one row
 * and writes on its bottom row. Written by DebugConsole_WriteText,
 * DebugConsole_WriteTextInScrollRegion and DebugConsole_SetCursorPosition; only
 * the first of these is ever called. */
// GLOBAL: XVT 0x528108
int g_debugConsoleCursorRow;
/* Top row of the region DebugConsole_WriteText scrolls: 0, except while
 * DebugConsole_WriteTextInScrollRegion runs, which nothing calls. */
// GLOBAL: XVT 0x52810C
int g_debugConsoleScrollTopRow = 0;
/* Bottom row of the region DebugConsole_WriteText scrolls: 24, the last row,
 * except while DebugConsole_WriteTextInScrollRegion runs, which nothing
 * calls. */
// GLOBAL: XVT 0x528110
int g_debugConsoleScrollBottomRow = 24;
/* Nonzero while DebugConsole_WriteText also appends its text to mpDump.txt.
 * Only DebugConsole_ToggleFileDump and DebugConsole_SetInitialized write it,
 * and nothing calls either, so it stays 0. */
// GLOBAL: XVT 0x528114
int g_debugConsoleFileDumpEnabled;

/* Does nothing in either build: its calls print nothing, and it reads none of
 * its arguments. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079E0
void DebugPrintf(const char *format, ...) { (void)format; }

/* Also turns off the mpDump.txt file dump. */
/* Sets g_debugConsoleInitialized; 0 makes the next DebugConsole_WriteText blank
 * its buffer. Nothing calls this. */
// FUNCTION: XVT 0x4ACBF0
void DebugConsole_SetInitialized(int initialized)
{
	g_debugConsoleFileDumpEnabled = 0;
	g_debugConsoleInitialized = initialized;
}

/* Sets g_debugConsoleCursorColumn and g_debugConsoleCursorRow without checking
 * them against the 80 by 25 buffer. Nothing calls this. */
// FUNCTION: XVT 0x4ACC10
void DebugConsole_SetCursorPosition(int column, int row)
{
	g_debugConsoleCursorColumn = column;
	g_debugConsoleCursorRow = row;
}

#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma function(memcpy)
#endif
/* Writes text into an 80-column, 25-row buffer of character and color byte
 * pairs at g_debugConsoleCursorColumn and g_debugConsoleCursorRow, writing the
 * character bytes only, and wraps at column 80. Before writing a row, when the
 * cursor row is past g_debugConsoleScrollBottomRow, it moves the rows from
 * g_debugConsoleScrollTopRow + 1 to the bottom row up one, blanks the
 * characters of the bottom row and puts the cursor row there. A newline at the
 * start or end of text moves to column 0 of the next row; any other newline
 * moves down two rows. While g_debugConsoleInitialized is 0 it first fills the
 * buffer with blanks of color 7 and sets it to 1; while
 * g_debugConsoleFileDumpEnabled is set it first appends text to mpDump.txt.
 * Returns the cursor column after the text. The buffer is
 * g_debugConsoleTextBuffer in the original build and a static array in the
 * modern one; nothing in the engine reads either back. Its one caller,
 * RenderTexture_FindOrAllocateCacheEntry, writes a warning when the texture
 * cache is full. */
// FUNCTION: XVT 0x4ACC30
int DebugConsole_WriteText(const char *text)
{
	uint8_t *textBuffer;
	const char *textCursor;
	int sourceLength;
	int remainingLength;
	int charIndex;
	int zero;
	XvtFile *stream;
	uint8_t *textCell;

#ifdef XVT_MODERN
	static uint8_t modernTextBuffer[80 * 25 * 2];
	textBuffer = modernTextBuffer;
#else
	textBuffer = g_debugConsoleTextBuffer;
#endif
	if (g_debugConsoleInitialized == 0) {
		int initializeCount;
		uint8_t *initializeCell;

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
			File_WriteBytes(stream, text, strlen(text));
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
				&textBuffer[160 * g_debugConsoleScrollTopRow +
					    160],
				(size_t)(160 * (g_debugConsoleScrollBottomRow -
						g_debugConsoleScrollTopRow)));
#else
			memcpy(&textBuffer[160 * g_debugConsoleScrollTopRow],
			       &textBuffer[160 * g_debugConsoleScrollTopRow +
					   160],
			       (size_t)(160 * (g_debugConsoleScrollBottomRow -
					       g_debugConsoleScrollTopRow)));
#endif
			{
				int clearCount;
				uint8_t *clearCell;

				clearCell =
					&textBuffer
						[160 *
						 g_debugConsoleScrollBottomRow];
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

		if (remainingLength + g_debugConsoleCursorColumn > 80) {
			remainingLength = 80 - g_debugConsoleCursorColumn;
		}

		charIndex = zero;
		textCell = &textBuffer[2 * (g_debugConsoleCursorColumn +
					    80 * g_debugConsoleCursorRow)];
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
				textCell[charIndex * 2] =
					(uint8_t)textCursor[charIndex];
				++charIndex;
				if (remainingLength > charIndex) {
					continue;
				}
				break;
			}
		}

		g_debugConsoleCursorColumn += remainingLength;
		if (remainingLength == sourceLength) {
			return g_debugConsoleCursorColumn;
		}
		textCursor += remainingLength;
		g_debugConsoleCursorColumn = zero;
		++g_debugConsoleCursorRow;
	}
}
#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma intrinsic(memcpy)
#endif

/* Writes text with DebugConsole_WriteText in a scroll region of rows topRow to
 * bottomRow, starting on bottomRow; when the cursor column is 0 it starts one
 * row below instead, so the write first scrolls the region up. Then sets the
 * region back to rows 0 to 24 and leaves the cursor where the write left it.
 * Nothing calls this. */
// FUNCTION: XVT 0x4ACDD0
void DebugConsole_WriteTextInScrollRegion(int topRow, int bottomRow,
					  const char *text)
{
	g_debugConsoleScrollBottomRow = bottomRow;
	g_debugConsoleScrollTopRow = topRow;
	g_debugConsoleCursorRow = bottomRow;
	if (g_debugConsoleCursorColumn == 0) {
		g_debugConsoleCursorRow = bottomRow + 1;
	}
	DebugConsole_WriteText(text);
	g_debugConsoleScrollTopRow = 0;
	g_debugConsoleScrollBottomRow = 24;
}

/* Sets g_debugConsoleFileDumpEnabled to 0 when it is nonzero. Otherwise adds 1
 * to it when one of the three gates is nonzero; the third starts at 1 and never
 * changes, so it always does. Nothing calls this. */
// FUNCTION: XVT 0x4ACE20
void DebugConsole_ToggleFileDump(void)
{
	if (g_debugConsoleFileDumpEnabled != 0) {
		g_debugConsoleFileDumpEnabled = 0;
		return;
	}

	if (g_debugConsoleFileDumpGate0 != 0 ||
	    g_debugConsoleFileDumpGate1 != 0 ||
	    g_debugConsoleFileDumpGate2 != 0) {
		++g_debugConsoleFileDumpEnabled;
	}
}

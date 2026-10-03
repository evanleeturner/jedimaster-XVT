#include "xvt/frontend/credits.h"
#include "xvt/assets/file.h"

#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per text buffer (0 and 1), the X position of the logo drawn with that
 * buffer's page. Credits_ParseNextPage sets it from the page header's seventh
 * number; Credits_UpdateScreen sets both to 0 on its first frame. */
// GLOBAL: XVT 0x6697A8
int g_creditsLogoX[2] = {0};
/* Per text buffer, the logo X last drawn into the offscreen background. Only
 * Credits_UpdateScreen writes it: it sets both to 0 on its first frame and
 * copies g_creditsLogoX in whenever one of the six logo values changed, which
 * redraws the background. */
// GLOBAL: XVT 0x6697A0
int g_creditsPrevLogoX[2] = {0};
/* Per text buffer, the Y position of the logo drawn with that buffer's page;
 * the page header's eighth number. Written like g_creditsLogoX. */
// GLOBAL: XVT 0x6697B0
int g_creditsLogoY[2] = {0};
/* 1 when the page last read from credits.txt ended at a line starting with
 * '*', so another page follows; 0 when the file ended. Written by
 * Credits_ParseNextPage through the pointer Credits_UpdateScreen passes, and
 * set to 0 by Credits_UpdateScreen on its first frame. */
// GLOBAL: XVT 0x6697B8
int g_creditsHasMorePages = 0;
/* Pages shown before the current one: 0 on the first page. Only
 * Credits_UpdateScreen writes it: 0 on its first frame, raised by one at each
 * new page. It picks the hidden photo that Shift, Alt and F12 held together
 * draw. */
// GLOBAL: XVT 0x6697BC
int g_creditsPageIndex = 0;
/* Per text buffer, the logo Y last drawn into the offscreen background.
 * Written like g_creditsPrevLogoX. */
// GLOBAL: XVT 0x6697C0
int g_creditsPrevLogoY[2] = {0};
/* Color given to each credits line as it is read: the low 16 bits of
 * FrontendDisplay_PackRGB for the last line starting with '~'. Set by
 * Credits_ParseTextLine; Credits_UpdateScreen sets 0xFFFF on its first
 * frame. */
// GLOBAL: XVT 0x6697C8
uint16_t g_creditsCurrentTextColor = 0;
/* credits.txt, open while the credits screen runs. Credits_UpdateScreen opens
 * it on its first frame, closing any copy still open; it is NULL when that
 * open fails. FrontendBootstrap_ExitCreditsAndLoadFrontend closes it and sets
 * NULL, and so does XvtFrontendTask_Shutdown in the modern build. */
// GLOBAL: XVT 0x6697CC
XvtFile *g_frontendCreditsFile = NULL;
/* Per text buffer, the logo drawn with that buffer's page: 1 the "totallylogo"
 * image, 2 the "leclogo" image, any other value none. The page header's sixth
 * number. Written like g_creditsLogoX. */
// GLOBAL: XVT 0x6697D0
int g_creditsLogoId[2] = {0};
/* Per text buffer, the logo id last drawn into the offscreen background.
 * Written like g_creditsPrevLogoX. */
// GLOBAL: XVT 0x6697D8
int g_creditsPrevLogoId[2] = {0};
/* Frontend frame count at which the current page's time is up. The first page
 * stores its duration here directly, since the screen's frames count from 0;
 * Credits_UpdateScreen adds each later page's duration, the page header's fifth
 * number, when it reads that page. */
// GLOBAL: XVT 0x6697E0
int g_creditsPageEndFrame = 0;
/* Frames the newest page's text takes to fade in: the page header's fourth
 * number, passed to FrontendText_StartTextFadeIn. The first page ignores it
 * and fades over 200 frames. Written by Credits_ParseNextPage through the
 * pointer Credits_UpdateScreen passes. */
// GLOBAL: XVT 0x6697E4
int g_creditsTextFadeFrames = 0;
/* Per text buffer, the Y position of the page's first text line; later lines
 * sit 19 pixels apart. The page header's second number, set by
 * Credits_ParseNextPage. Credits_UpdateScreen sets only entry 1 to 0 on its
 * first frame. */
// GLOBAL: XVT 0x6697E8
int g_creditsTextY[2] = {0};
/* Per text buffer, the X position of the page's text lines; the page header's
 * first number, set by Credits_ParseNextPage. Credits_UpdateScreen sets only
 * entry 0 to 0 on its first frame. */
// GLOBAL: XVT 0x6697F0
int g_creditsTextX[2] = {0};
/* Text buffer, 0 or 1, the newest page was read into: the page header's third
 * number minus 1, with any result above 1 made 1. Written by
 * Credits_ParseNextPage through the pointer Credits_UpdateScreen passes. That
 * buffer's text fades in; the other buffer's is drawn without the fade. */
// GLOBAL: XVT 0x6697F8
unsigned int g_creditsBufferIdx = 0;
/* Two pages of credits text, up to 32 lines of 255 characters each, one page
 * per buffer. Credits_ParseNextPage empties a buffer before it fills it, and
 * Credits_ParseTextLine copies each line in without the color prefix. */
// GLOBAL: XVT 0x669800
char g_creditsTextLines[2][32][256] = {0};
/* Color of each line in g_creditsTextLines, set by Credits_ParseTextLine from
 * g_creditsCurrentTextColor. */
// GLOBAL: XVT 0x66D800
uint16_t g_creditsTextColors[2][32] = {0};
/* 1 once the credits are ending: after the last page's time is up, or on a
 * mouse click, Esc, Enter or Space. Only Credits_UpdateScreen writes it,
 * setting 0 on its first frame; its next frame after the 1 switches to the
 * concourse, in the modern build once the CD fade is over.
 * XvtFrontendTask_Update also reads it, to keep the last frame shown. */
// GLOBAL: XVT 0x52D0C8
int g_creditsExitPending = 0;

/* Besides loading the credits images, font and sound list, this sets the display options for the screen,
 * hides the cursor and starts CD track 4 playing on a loop. */
/* Also loads font size 15, turns on refilling the back buffer from the
 * offscreen surface after each present, sets the CD aux volume to 0x8000 and
 * plays track 4 from its start. Returns 0 on every path and checks no load or
 * play result. Its one caller is FrontendBootstrap_ExitIntroAndLoadCredits. */
// FUNCTION: XVT 0x4FB4F0
int Credits_LoadScreenResources(void)
{
	FrontendDisplay_DisableEscapeClose();
	FrontendDisplay_SetSurfaceClearColor(0);
	FrontendCursor_Hide();
	FrontendDisplay_DisableClearAfterPresent();
	FrontendDisplay_EnableOffscreenRestore();
	FrontendText_LoadFont(15);
	FrontendSound_LoadList("sfx\\sfx.lst");
	FrontImage_RegisterResourceDefault("frontres\\credits.bmp",
					   "background");
	FrontImage_RegisterResource("frontres\\leclogo.bmp", "leclogo", 0, 0);
	FrontImage_RegisterResource("frontres\\totallyg.bmp", "totallylogo", 0,
				    0);
	FrontImage_RegisterResourceDefault("frontres\\comp01.bmp", "comp01");
	FrontImage_RegisterResourceDefault("frontres\\test.bmp", "testers");
	FrontImage_RegisterResourceDefault("frontres\\jbrs.bmp", "lakota");
	FrontImage_RegisterResourceDefault("frontres\\arts.bmp", "artists");
	CDAudio_EnableLoopCurrentTrack();
	CDAudio_Initialize();
	CDAudio_SetAuxVolume(0x8000u);
	CDAudio_PlayTrackFromTime(4, 0, 0);
	return 0;
}

/* Reads the next page of credits.txt into one of the two text buffers. A page
 * is a header line of ten unsigned numbers (text X, text Y, buffer 1 or 2 with
 * any other number counting as 2, fade frames, duration frames, logo id, logo
 * X, logo Y and two that are read and ignored), then up to 32 text lines; lines
 * starting with "//" are skipped and do not count. The header sets that
 * buffer's g_creditsTextX, g_creditsTextY, g_creditsLogoId, g_creditsLogoX and
 * g_creditsLogoY, and the buffer's 32 lines are emptied first. Each text line
 * goes through Credits_ParseTextLine, without its newline. A line starting with
 * '*' ends the page and sets *outHasMorePages to 1; after 32 lines it skips
 * ahead to that line or the end of the file. Returns 0 with *outHasMorePages at
 * 0 when g_frontendCreditsFile is NULL, else 1, also at the end of the file,
 * where *outHasMorePages stays 0. Does not check how many header numbers were
 * read, and lowers *outBufferIdx by one even when none were. */
// FUNCTION: XVT 0x4FBAF0
int Credits_ParseNextPage(unsigned int *outBufferIdx, int *outHasMorePages,
			  int *outPageDurationFrames, int *outTextFadeFrames)
{
	unsigned int logoId;
	unsigned int logoX;
	unsigned int logoY;
	unsigned int textX;
	unsigned int textY;
	unsigned int unusedA;
	unsigned int unusedB;
	char line[256];
	int lineIndex;
	int clearIndex;
	size_t lineLength;

	*outHasMorePages = 0;
	if (g_frontendCreditsFile == NULL) {
		return 0;
	}
	File_Scanf(g_frontendCreditsFile, "%u %u %u %u %u %u %u %u %u %u\n",
		   &textX, &textY, outBufferIdx, outTextFadeFrames,
		   outPageDurationFrames, &logoId, &logoX, &logoY, &unusedA,
		   &unusedB);
	(*outBufferIdx)--;
	if (*outBufferIdx > 1) {
		*outBufferIdx = 1;
	}
	g_creditsLogoId[*outBufferIdx] = logoId;
	g_creditsLogoX[*outBufferIdx] = logoX;
	g_creditsLogoY[*outBufferIdx] = logoY;
	g_creditsTextX[*outBufferIdx] = textX;
	g_creditsTextY[*outBufferIdx] = textY;
	for (clearIndex = 0; clearIndex < 32; clearIndex++) {
		g_creditsTextLines[*outBufferIdx][clearIndex][0] = '\0';
	}

	lineIndex = 0;
	do {
		if (File_Gets(line, sizeof(line), g_frontendCreditsFile) ==
		    NULL) {
			return 1;
		}
		if (line[0] != '/' || line[1] != '/') {
			lineLength = strlen(line);
			if (line[lineLength - 1] == '\n') {
				line[lineLength - 1] = '\0';
			}
			if (line[0] == '*') {
				*outHasMorePages = 1;
				return 1;
			}
			Credits_ParseTextLine(line, *outBufferIdx, lineIndex);
			lineIndex++;
		}
	} while (lineIndex < 32);

	while (File_Gets(line, sizeof(line), g_frontendCreditsFile) != NULL) {
		if (line[0] == '*') {
			*outHasMorePages = 1;
			return 1;
		}
	}
	return 1;
}

/* Stores one credits line in g_creditsTextLines[bufferIdx][lineIdx] and its
 * color in g_creditsTextColors. A line starting with '~' first sets
 * g_creditsCurrentTextColor from three decimal numbers after it, red, green
 * and blue, each ended by a space, through FrontendDisplay_PackRGB; the text
 * after them is stored. Other lines are stored whole, in the color still in
 * g_creditsCurrentTextColor. Returns 1. Checks neither index nor the length
 * of the text it copies. */
// FUNCTION: XVT 0x4FBCD0
int Credits_ParseTextLine(const char *line, unsigned int bufferIdx,
			  unsigned int lineIdx)
{
	const char *text;
	int remaining;
	char token[256];

	text = line;
	remaining = strlen(line);
	if (*text == '~') {
		int tokenLength;
		int charIndex;
		int red;
		int green;

		++text;
		tokenLength = 0;
		if (remaining > 0) {
			for (charIndex = 0; remaining > charIndex;
			     ++charIndex) {
				char c;

				c = *text;
				if (c == ' ' || c == '\0') {
					++text;
					--remaining;
					token[tokenLength] = '\0';
					break;
				}
				token[tokenLength++] = c;
				++text;
				--remaining;
			}
		}
		red = atoi(token);

		tokenLength = 0;
		if (remaining > 0) {
			for (charIndex = 0; remaining > charIndex;
			     ++charIndex) {
				char c;

				c = *text;
				if (c == ' ' || c == '\0') {
					++text;
					--remaining;
					token[tokenLength] = '\0';
					break;
				}
				token[tokenLength++] = c;
				++text;
				--remaining;
			}
		}
		green = atoi(token);

		tokenLength = 0;
		if (remaining > 0) {
			for (charIndex = 0; remaining > charIndex;
			     ++charIndex) {
				char c;

				c = *text;
				if (c == ' ' || c == '\0') {
					token[tokenLength] = '\0';
					++text;
					break;
				}
				token[tokenLength++] = c;
				++text;
				--remaining;
			}
		}
		g_creditsCurrentTextColor =
			FrontendDisplay_PackRGB(red, green, atoi(token));
	}
	g_creditsTextColors[bufferIdx][lineIdx] = g_creditsCurrentTextColor;
	strcpy(g_creditsTextLines[bufferIdx][lineIdx], text);
	return 1;
}

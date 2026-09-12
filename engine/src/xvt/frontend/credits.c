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

// GLOBAL: XVT 0x6697A8
int g_creditsLogoX[2] = { 0 };
// GLOBAL: XVT 0x6697A0
int g_creditsPrevLogoX[2] = { 0 };
// GLOBAL: XVT 0x6697B0
int g_creditsLogoY[2] = { 0 };
// GLOBAL: XVT 0x6697B8
int g_creditsHasMorePages = 0;
// GLOBAL: XVT 0x6697BC
int g_creditsPageIndex = 0;
// GLOBAL: XVT 0x6697C0
int g_creditsPrevLogoY[2] = { 0 };
// GLOBAL: XVT 0x6697C8
uint16_t g_creditsCurrentTextColor = 0;
// GLOBAL: XVT 0x6697CC
XvtFile* g_frontendCreditsFile = NULL;
// GLOBAL: XVT 0x6697D0
int g_creditsLogoId[2] = { 0 };
// GLOBAL: XVT 0x6697D8
int g_creditsPrevLogoId[2] = { 0 };
// GLOBAL: XVT 0x6697E0
int g_creditsPageEndFrame = 0;
// GLOBAL: XVT 0x6697E4
int g_creditsGlyphScratchFrames = 0;
// GLOBAL: XVT 0x6697E8
int g_creditsTextY[2] = { 0 };
// GLOBAL: XVT 0x6697F0
int g_creditsTextX[2] = { 0 };
// GLOBAL: XVT 0x6697F8
unsigned int g_creditsBufferIdx = 0;
// GLOBAL: XVT 0x669800
char g_creditsTextLines[2][32][256] = { 0 };
// GLOBAL: XVT 0x66D800
uint16_t g_creditsTextColors[2][32] = { 0 };
// GLOBAL: XVT 0x52D0C8
int g_creditsExitPending = 0;

// FUNCTION: XVT 0x4FB4F0
int Credits_LoadScreenResources(void) {
	FrontendDisplay_DisableEscapeClose();
	FrontendDisplay_SetSurfaceClearColor(0);
	FrontendCursor_Hide();
	FrontendDisplay_ClearPresentFrameReady();
	FrontendDisplay_EnableOffscreenRestore();
	FrontendText_LoadFont(15);
	FrontendSound_LoadList("sfx\\sfx.lst");
	FrontImage_RegisterResourceDefault("frontres\\credits.bmp", "background");
	FrontImage_RegisterResource("frontres\\leclogo.bmp", "leclogo", 0, 0);
	FrontImage_RegisterResource("frontres\\totallyg.bmp", "totallylogo", 0, 0);
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

// FUNCTION: XVT 0x4FBAF0
int Credits_ParseNextPage(unsigned int* outBufferIdx, int* outHasMorePages, int* outPageDurationFrames,
						  int* outGlyphScratchFrames) {
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
	File_Scanf(g_frontendCreditsFile, "%u %u %u %u %u %u %u %u %u %u\n", &textX, &textY, outBufferIdx,
			   outGlyphScratchFrames, outPageDurationFrames, &logoId, &logoX, &logoY, &unusedA, &unusedB);
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
		if (File_Gets(line, sizeof(line), g_frontendCreditsFile) == NULL) {
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

// FUNCTION: XVT 0x4FBCD0
int Credits_ParseTextLine(const char* line, unsigned int bufferIdx, unsigned int lineIdx) {
	const char* text;
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
			for (charIndex = 0; remaining > charIndex; ++charIndex) {
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
			for (charIndex = 0; remaining > charIndex; ++charIndex) {
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
			for (charIndex = 0; remaining > charIndex; ++charIndex) {
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
		g_creditsCurrentTextColor = FrontendDisplay_PackRGB(red, green, atoi(token));
	}
	g_creditsTextColors[bufferIdx][lineIdx] = g_creditsCurrentTextColor;
	strcpy(g_creditsTextLines[bufferIdx][lineIdx], text);
	return 1;
}

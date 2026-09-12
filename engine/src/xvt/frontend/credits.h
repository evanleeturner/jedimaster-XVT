#ifndef XVT_FRONTEND_CREDITS_H
#define XVT_FRONTEND_CREDITS_H

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t g_creditsCurrentTextColor;
extern XvtFile* g_frontendCreditsFile;
extern int g_creditsLogoX[2];
extern int g_creditsPrevLogoX[2];
extern int g_creditsLogoY[2];
extern int g_creditsPrevLogoY[2];
extern int g_creditsLogoId[2];
extern int g_creditsPrevLogoId[2];
extern int g_creditsHasMorePages;
extern int g_creditsPageIndex;
extern int g_creditsPageEndFrame;
extern int g_creditsGlyphScratchFrames;
extern int g_creditsTextY[2];
extern int g_creditsTextX[2];
extern unsigned int g_creditsBufferIdx;
extern char g_creditsTextLines[2][32][256];
extern uint16_t g_creditsTextColors[2][32];
extern int g_creditsExitPending;

int Credits_LoadScreenResources(void);
int Credits_ParseNextPage(unsigned int* outBufferIdx, int* outHasMorePages, int* outPageDurationFrames,
						  int* outGlyphScratchFrames);
int Credits_ParseTextLine(const char* line, unsigned int bufferIdx, unsigned int lineIdx);

#ifdef __cplusplus
}
#endif

#endif

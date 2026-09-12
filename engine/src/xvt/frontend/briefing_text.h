#ifndef XVT_FRONTEND_BRIEFING_TEXT_H
#define XVT_FRONTEND_BRIEFING_TEXT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char* g_briefingMapLabelTexts[32];
extern char* g_briefingTextBlocks[32];
extern char* g_briefingTextBlockPaddingBuffers[20];
extern char* g_briefingText;

int16_t BriefingText_FreeAllocatedBuffersExit(void);
void BriefingText_FreeAllocatedBuffers(void);

#ifdef __cplusplus
}
#endif

#endif

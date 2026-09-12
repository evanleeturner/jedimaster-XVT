#include "xvt/frontend/briefing_text.h"
#include <stdlib.h>

// GLOBAL: XVT 0x669582
char* g_briefingMapLabelTexts[32] = { 0 };
// GLOBAL: XVT 0x669602
char* g_briefingTextBlocks[32] = { 0 };
// GLOBAL: XVT 0x669682
char* g_briefingTextBlockPaddingBuffers[20] = { 0 };
// GLOBAL: XVT 0xAA6118
char* g_briefingText = NULL;

// FUNCTION: XVT 0x4F68B0
int16_t BriefingText_FreeAllocatedBuffersExit(void) {
	BriefingText_FreeAllocatedBuffers();
	return 1;
}

// FUNCTION: XVT 0x4F6B10
void BriefingText_FreeAllocatedBuffers(void) {
	int16_t index;

	for (index = 0; index < 32; ++index) {
		if (g_briefingMapLabelTexts[index] != NULL) {
			free(g_briefingMapLabelTexts[index]);
		}
	}

	for (index = 0; index < 32; ++index) {
		if (g_briefingTextBlocks[index] != NULL) {
			free(g_briefingTextBlocks[index]);
		}
	}

	for (index = 0; index < 20; ++index) {
		if (g_briefingTextBlockPaddingBuffers[index] != NULL) {
			free(g_briefingTextBlockPaddingBuffers[index]);
		}
	}
}

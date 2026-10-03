#include "xvt/frontend/briefing_text.h"
#include <stdlib.h>

/* The active briefing's 32 map label strings: 40-byte heap buffers that
 * FrontendMission_InitForBriefing allocates and
 * FrontendMission_LoadCurrentWithBriefing fills from the mission file. Label
 * entries of the briefing script pick one by index. */
// GLOBAL: XVT 0x669582
char *g_briefingMapLabelTexts[32] = {0};
/* The active briefing's 32 text blocks: 320-byte heap buffers, allocated and
 * filled like g_briefingMapLabelTexts. The block text slot 1 names is drawn
 * under the briefing map. */
// GLOBAL: XVT 0x669602
char *g_briefingTextBlocks[32] = {0};
/* Twenty 1024-byte heap buffers that FrontendMission_InitForBriefing
 * allocates and BriefingText_FreeAllocatedBuffers frees; nothing else uses
 * them. */
// GLOBAL: XVT 0x669682
char *g_briefingUnusedBuffers[20] = {0};
/* 4096-byte heap buffer of the text shown with a mission or a network game:
 * the mission's description from MissionSetup_LoadMissionDescText, or the
 * text MissionDebrief_ReadOutcomeText writes; NULL while none is held. Many
 * functions allocate, fill, clear and free it, chiefly in mission_setup.c,
 * mission_debrief.c and frontend_net.c. */
// GLOBAL: XVT 0xAA6118
char *g_missionText = NULL;

/* Frees the briefing's text buffers through BriefingText_FreeAllocatedBuffers
 * and returns 1. Its one caller is MissionSetup_FreeScreenResources. */
// FUNCTION: XVT 0x4F68B0
int16_t BriefingText_FreeAllocatedBuffersExit(void)
{
	BriefingText_FreeAllocatedBuffers();
	return 1;
}

/* Frees every non-NULL buffer in g_briefingMapLabelTexts,
 * g_briefingTextBlocks and g_briefingUnusedBuffers and leaves the pointers as
 * they were, so a second call before FrontendMission_InitForBriefing
 * allocates again frees them twice. */
// FUNCTION: XVT 0x4F6B10
void BriefingText_FreeAllocatedBuffers(void)
{
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
		if (g_briefingUnusedBuffers[index] != NULL) {
			free(g_briefingUnusedBuffers[index]);
		}
	}
}

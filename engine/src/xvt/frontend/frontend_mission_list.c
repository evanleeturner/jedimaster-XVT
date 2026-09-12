#include "xvt/frontend/frontend_mission_list.h"

#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/mission_setup.h"

#include <stdlib.h>

// FUNCTION: XVT 0x4D7370
int FrontendMissionList_FreeScreenResources(int frameCounter) {
	(void)frameCounter;

	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_briefingText != NULL) {
		free(g_briefingText);
		g_briefingText = NULL;
	}
	Frontend_ResetScrollableControls();
	FrontImage_FreeResourceByName("background");
	return 0;
}

// FUNCTION: XVT 0x4F1C60
int FrontendMissionList_FreeScreenResourcesAndClearInputGate(void) {
	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_briefingText != NULL) {
		free(g_briefingText);
		g_briefingText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	Frontend_ResetScrollableControls();
	FrontendMouse_ClearInputGate();
	return 0;
}

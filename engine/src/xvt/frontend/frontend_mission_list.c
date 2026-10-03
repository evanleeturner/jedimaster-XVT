#include "xvt/frontend/frontend_mission_list.h"

#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/mission_setup.h"

#include <stdlib.h>

/* Exit function of the mission list screens: frees g_missionList and
 * g_missionText and sets them NULL, forgets the scrollable controls and frees
 * the "background" image. Returns 0; ignores frameCounter. */
// FUNCTION: XVT 0x4D7370
int FrontendMissionList_FreeScreenResources(int frameCounter)
{
	(void)frameCounter;

	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_missionText != NULL) {
		free(g_missionText);
		g_missionText = NULL;
	}
	Frontend_ResetScrollableControls();
	FrontImage_FreeResourceByName("background");
	return 0;
}

/* FrontendMissionList_FreeScreenResources, with the image freed before the
 * controls are forgotten, that also clears the mouse input gate. Returns 0.
 * The original build passes it as an exit function through a cast, so it
 * ignores the frame counter it is given. */
// FUNCTION: XVT 0x4F1C60
int FrontendMissionList_FreeScreenResourcesAndClearInputGate(void)
{
	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_missionText != NULL) {
		free(g_missionText);
		g_missionText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	Frontend_ResetScrollableControls();
	FrontendMouse_ClearInputGate();
	return 0;
}

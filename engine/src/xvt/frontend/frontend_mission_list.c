#include "xvt/frontend/frontend_mission_list.h"

#include <stdlib.h>

#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/mission_setup.h"

/* Exit function of the mission list screens: frees g_mission_list and
 * g_mission_text and sets them NULL, forgets the scrollable controls and frees
 * the "background" image. Returns 0; ignores frame_counter. */
// FUNCTION: XVT 0x4D7370
int frontend_mission_list_free_screen_resources(int frame_counter)
{
	(void)frame_counter;

	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	frontend_reset_scrollable_controls();
	front_image_free_resource_by_name("background");
	return 0;
}

/* frontend_mission_list_free_screen_resources, with the image freed before the
 * controls are forgotten, that also clears the mouse input gate. Returns 0. It
 * is passed as an exit function through a cast, so it ignores the frame counter
 * it is given. */
// FUNCTION: XVT 0x4F1C60
int frontend_mission_list_free_screen_resources_and_clear_input_gate(void)
{
	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	front_image_free_resource_by_name("background");
	frontend_reset_scrollable_controls();
	frontend_mouse_clear_input_gate();
	return 0;
}

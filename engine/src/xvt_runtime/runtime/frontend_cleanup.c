#include "xvt_runtime/runtime/frontend_cleanup.h"

#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"

int xvt_frontend_cleanup_mission_resources(int frame)
{
	(void)frame;
	return frontend_mission_list_free_screen_resources_and_clear_input_gate();
}

int xvt_frontend_cleanup_current_mission(int frame)
{
	(void)frame;
	return mission_setup_exit_current_mission();
}

int xvt_frontend_cleanup_next_mission(int frame)
{
	(void)frame;
	return mission_setup_exit_next_mission();
}

int xvt_frontend_cleanup_battle_choice(int frame)
{
	(void)frame;
	return mission_setup_battle_choice_exit();
}

/* Checks the screen exit callbacks (xvt_runtime/runtime/frontend_cleanup.h)
 * against the promise in their header: each ignores the frame it is given and
 * returns what the original function it calls returns. Those functions free the
 * mission list, the briefing text and the screen's background image, none of
 * which is loaded here, so calling them twice is harmless. Every case starts
 * from a cleared frontend.
 *
 * Not checked here: what the original functions themselves do, which is the
 * recovered game's code. */
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt_runtime/runtime/frontend_cleanup.h"

typedef int (*exit_callback)(int frame);
typedef int (*original)(void);

static const int g_frames[] = {0, 1, -1, 1000, 0x7fffffff};

static void check_same_result(exit_callback callback, original original)
{
	for (unsigned i = 0; i < sizeof g_frames / sizeof g_frames[0]; ++i) {
		memset(&g_front_state, 0, sizeof g_front_state);
		int expected = original();
		memset(&g_front_state, 0, sizeof g_front_state);
		XVT_ASSERT_INT_EQ(callback(g_frames[i]), expected);
	}
}

int main(void)
{
	check_same_result(
		xvt_frontend_cleanup_mission_resources,
		frontend_mission_list_free_screen_resources_and_clear_input_gate);
	check_same_result(xvt_frontend_cleanup_current_mission,
			  mission_setup_exit_current_mission);
	check_same_result(xvt_frontend_cleanup_next_mission,
			  mission_setup_exit_next_mission);
	check_same_result(xvt_frontend_cleanup_battle_choice,
			  mission_setup_battle_choice_exit);
	return 0;
}

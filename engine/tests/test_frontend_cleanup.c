/* Checks the screen exit callbacks (xvt_runtime/runtime/frontend_cleanup.h) against the promise in their
 * header: each ignores the frame it is given and returns what the original function it calls returns.
 * Those functions free the mission list, the briefing text and the screen's background image, none of
 * which is loaded here, so calling them twice is harmless. Every case starts from a cleared frontend.
 *
 * Not checked here: what the original functions themselves do, which is the recovered game's code. */
#include "test_assert.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt_runtime/runtime/frontend_cleanup.h"

#include <string.h>

typedef int (*ExitCallback)(int frame);
typedef int (*Original)(void);

static const int g_frames[] = { 0, 1, -1, 1000, 0x7fffffff };

static void CheckSameResult(ExitCallback callback, Original original) {
	for (unsigned i = 0; i < sizeof g_frames / sizeof g_frames[0]; ++i) {
		memset(&g_frontState, 0, sizeof g_frontState);
		int expected = original();
		memset(&g_frontState, 0, sizeof g_frontState);
		XVT_ASSERT_INT_EQ(callback(g_frames[i]), expected);
	}
}

int main(void) {
	CheckSameResult(XvtFrontendCleanup_MissionResources,
					FrontendMissionList_FreeScreenResourcesAndClearInputGate);
	CheckSameResult(XvtFrontendCleanup_CurrentMission, MissionSetup_ExitCurrentMission);
	CheckSameResult(XvtFrontendCleanup_NextMission, MissionSetup_ExitNextMission);
	CheckSameResult(XvtFrontendCleanup_BattleChoice, MissionSetup_BattleChoice_Exit);
	return 0;
}

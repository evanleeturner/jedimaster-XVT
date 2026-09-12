#include "xvt_runtime/runtime/frontend_cleanup.h"

#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"

int XvtFrontendCleanup_MissionResources(int frame) {
	(void)frame;
	return FrontendMissionList_FreeScreenResourcesAndClearInputGate();
}

int XvtFrontendCleanup_CurrentMission(int frame) {
	(void)frame;
	return MissionSetup_ExitCurrentMission();
}

int XvtFrontendCleanup_NextMission(int frame) {
	(void)frame;
	return MissionSetup_ExitNextMission();
}

int XvtFrontendCleanup_BattleChoice(int frame) {
	(void)frame;
	return MissionSetup_BattleChoice_Exit();
}

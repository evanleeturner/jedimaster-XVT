#ifndef XVT_FRONTEND_MISSION_BRIEFING_H
#define XVT_FRONTEND_MISSION_BRIEFING_H

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MissionBriefingCraftScreenFaction {
	MISSION_BRIEFING_CRAFT_SCREEN_REBEL = 0x0,
	MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL = 0x1,
} MissionBriefingCraftScreenFaction;

typedef enum MissionBriefingLaunchCountdownState {
	MISSION_BRIEFING_COUNTDOWN_IDLE = 0x0,
	MISSION_BRIEFING_COUNTDOWN_ACTIVE = 0x1,
	MISSION_BRIEFING_COUNTDOWN_EXPIRED = 0x2,
} MissionBriefingLaunchCountdownState;

extern int g_missionBriefingCraftSelectionActive;
extern MissionBriefingCraftScreenFaction g_missionBriefingCraftScreenFaction;

int MissionBriefing_CraftSelectionExit(int frameCounter);
int MissionBriefing_CraftSelectionUpdate(int frameCounter);
int MissionBriefing_BroadcastRosterAndAssignments(void);
int16_t MissionBriefing_HandleMapMouseInput(struct RECT *viewportRect,
					    struct RECT *clipRect,
					    int16_t suppressInput, int leftDown,
					    int rightDown, int16_t mouseX,
					    int16_t mouseY);
int16_t MissionBriefing_DrawMapViewport(struct RECT *viewportRect,
					struct RECT *clipRect,
					int16_t highlightPhase);
int MissionBriefing_AreAllNetworkPlayersReady(void);

#ifdef __cplusplus
}
#endif

#endif

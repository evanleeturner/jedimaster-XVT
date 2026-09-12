#ifndef XVT_FLIGHT_HUD_MFD_H
#define XVT_FLIGHT_HUD_MFD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum MfdPageId {
	MFD_PAGE_SCOREBOARD = 0,
	MFD_PAGE_GOALS = 1,
	MFD_PAGE_MESSAGE_LOG = 2,
	MFD_PAGE_DAMAGE = 3,
	MFD_PAGE_FLIGHT_GROUPS = 4,
	MFD_PAGE_FRIENDLY_CRAFT = 5,
	MFD_PAGE_COMMAND = 6,
	MFD_PAGE_UNKNOWN_7 = 7,
	MFD_PAGE_COUNT = 8,
	MFD_PAGE_NONE = UINT16_MAX,
};

enum MfdPageState {
	MFD_PAGE_STATE_CLOSING = -2,
	MFD_PAGE_STATE_REOPENED = -1, ///< Open state produced when a display rebuild cancels a pending close.
	MFD_PAGE_STATE_CLOSED = 0,
	MFD_PAGE_STATE_OPEN = 1,
};

extern uint16_t g_mfdActivePage;
extern uint16_t g_mfdSecondaryPage;
extern int16_t g_mfdPageStates[MFD_PAGE_COUNT];
extern uint16_t g_mfdSavedActivePage;
extern uint16_t g_mfdSavedSecondaryPage;
extern int16_t g_savedMfdPageStates[MFD_PAGE_COUNT];
extern uint16_t g_mfdCommandCameraStateCache;
extern int16_t g_mfdMissionScoreboardFirstVisibleRow;
extern int16_t g_mfdMissionScoreboardLastPlayerCount;
extern int16_t g_mfdMissionScoreboardLastWidth;
extern uint16_t g_mfdCraftListCachedRowCount;
extern int16_t g_mfdCraftListTopRowByMode[2];
extern const char g_mfdCraftListStatusLetters[11];
extern const char* g_strMapRoomText[20];
extern const char* g_mfdDeveloperCreditsLines[47];

int16_t Mfd_DrawMissionGoalsPage(void);
void Mfd_DrawMissionScoreboardPage(void);
void Mfd_DrawCraftListPage(uint16_t showFlightGroupsPage);
void Mfd_BuildScratchCraftListName(uint16_t objectIdx);
int16_t Mfd_GetFlightGroupGoalStatusStringId(uint16_t objectIndex);
void Mfd_DrawCommandMenuPage(void);
void Mfd_TogglePage(uint16_t page);
int16_t Mfd_FindSecondaryOpenPage(void);
int16_t Mfd_DrawMessageLogPage(void);
int Mfd_GetMessageLogRecordIndex(int displayOffset);

#ifdef __cplusplus
}
#endif

#endif

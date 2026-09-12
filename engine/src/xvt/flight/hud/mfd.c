#include "xvt/flight/hud/mfd.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#endif

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/math/math2.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt/util/memory.h"
#include <stdio.h>
#include <string.h>

// GLOBAL: XVT 0x521548
uint16_t g_mfdActivePage = MFD_PAGE_NONE;
// GLOBAL: XVT 0x52154C
uint16_t g_mfdSecondaryPage = MFD_PAGE_NONE;
// GLOBAL: XVT 0x521554
uint16_t g_mfdSavedActivePage = MFD_PAGE_NONE;
// GLOBAL: XVT 0x521558
uint16_t g_mfdSavedSecondaryPage = MFD_PAGE_NONE;
// GLOBAL: XVT 0xA08330
int16_t g_savedMfdPageStates[MFD_PAGE_COUNT] = { MFD_PAGE_STATE_CLOSED };
// GLOBAL: XVT 0xA08BA0
int16_t g_mfdPageStates[MFD_PAGE_COUNT] = { MFD_PAGE_STATE_CLOSED };
// GLOBAL: XVT 0x622BBC
uint16_t g_mfdMessageLogScrollOffset = 0;
// GLOBAL: XVT 0x622BB0
uint16_t g_mfdMessageLogRedraw = 0;
// GLOBAL: XVT 0x622BB8
unsigned int g_mfdMessageLogLastDrawTotalCount = 0;
// GLOBAL: XVT 0x5569F0
uint16_t g_mfdCommandCameraStateCache = 0;
// GLOBAL: XVT 0x5569E0
int16_t g_mfdMissionScoreboardFirstVisibleRow = 0;
// GLOBAL: XVT 0x5569E4
int16_t g_mfdMissionScoreboardLastPlayerCount = 0;
// GLOBAL: XVT 0x5569E8
int16_t g_mfdMissionScoreboardLastWidth = 0;
// GLOBAL: XVT 0x5569D8
uint16_t g_mfdCraftListCachedRowCount = 0;
// GLOBAL: XVT 0x5569EC
int16_t g_mfdCraftListTopRowByMode[2] = { 0 };
// GLOBAL: XVT 0x9A7A10
int g_mfdGoalsLineCounts[8] = { 0 };
// GLOBAL: XVT 0x5507F8
uint8_t g_mfdGoalsCachedSecondaryStatus = 0;
// GLOBAL: XVT 0x5507FC
int g_mfdGoalsCurrentTotalLines = 0;
// GLOBAL: XVT 0x550800
int g_mfdGoalsCachedVisibleGoalCount = 0;
// GLOBAL: XVT 0x550804
uint16_t g_mfdGoalsRedrawNeeded = 0;
// GLOBAL: XVT 0x550808
int g_mfdGoalsCachedLineCounts[8] = { 0 };
// GLOBAL: XVT 0x550828
uint16_t g_mfdGoalsUnusedState = 0;
// GLOBAL: XVT 0x550830
uint8_t g_mfdGoalsCachedPrimaryStatus = 0;
// GLOBAL: XVT 0x550838
int g_mfdGoalsCurrentScrollTop = 0;
// GLOBAL: XVT 0x51BE38
uint16_t g_mfdGoalsDisplayStateBySectionType[4][3] = {
	{ 2, 1, 1 },
	{ 4, 0, 0 },
	{ 0, 4, 0 },
	{ 1, 0, 1 },
};
// GLOBAL: XVT 0x51BE50
uint16_t g_mfdGoalsCountAltStateBySectionType[4][3] = {
	{ 2, 1, 1 },
	{ 4, 0, 0 },
	{ 0, 4, 0 },
	{ 4, 0, 1 },
};
// GLOBAL: XVT 0x51BE68
uint16_t g_mfdGoalsConditionMaskBySectionType[4][3] = {
	{ 2, 1, 1 },
	{ 6, 0, 0 },
	{ 0, 6, 0 },
	{ 1, 0, 1 },
};
// GLOBAL: XVT 0x523F80
const char g_mfdCraftListStatusLetters[11] = "CJNRFCJNRF";
// GLOBAL: XVT 0xA0A790
const char* g_strMapRoomText[20] = { 0 };
// GLOBAL: XVT 0x523F90
const char* g_mfdDeveloperCreditsLines[47] = {
	"Special Thanks to:",
	"  Wendy, Frank and Stephanie Post;",
	"  Eric & Krisitin Johnston; Mick & Sarah Foley;",
	"  Jake Hoelter; Grace Hoppin; Hernan Espinoza;",
	"  Johnathan, Jennifer, Jade & Jordan Huggins;",
	"  The San Francisco School of Circus Arts;",
	"  Sarah Steben; Sam Payne; Sandra Feusi;",
	"  Sandrine Deplanque and especially Mr. Lu Yi!",
	"        - Brad",
	0,
	"  David Owen @ ILMphoto; Ben Scott Pye;",
	"  Contesa; the DarkLight guys; Duff;",
	"  Francis Dunnery; the Scott clan;",
	"  Sarah Munday; Adeus Ayrton;",
	"        - Mark S.",
	"  I'd like to thank Debby McLeod",
	"        - Jim",
	" ",
	"  To Billy B.",
	"        - James",
	" ",
	"  I'd like to thank my wonderful girlfriend Eva",
	"        -  Albert",
	"  My father who gave me the sky",
	"  My mother who gave me the song",
	"  My wife Laurie who gave me the strength",
	"  My son Nicholas who gave me a future",
	"  And Jim McLeod and Rick Stienbach",
	"  who put a pencil in my hand and told me",
	"  to use it.",
	"        - Bucky",
	"  Mom, Dad, Joe & Jen; I love you guys!",
	"  Greg Fox; for a lifetime of friendship.",
	"  The best guys: Bill, Chris, Eddie, Jeff,",
	"  Jim, Mark, Mike, Peter, Rick, Thad, and",
	"      the CRS Liberty Crew!",
	"  Saturn Padua a best friend and roommate.",
	"  I couldn't have done it without your support!",
	"        - Bill",
	"  I wish to thank my wife Maria for all her",
	"  patience, love and support during the creation",
	"  of this game. A newlywed wife shouldn't have to",
	"  return from her honeymoon to see her husband",
	"  disappear into 'crunch mode' for five months!",
	"  I'd also like to thank Larry for giving me this",
	"  great job ... now can I have a month off?",
	"         -  David",
};

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x413C80
int16_t Mfd_DrawMissionGoalsPage(void) {
	enum {
		SECTION_COUNT = 4,
		GOAL_TYPE_MAX = 2,
		FLIGHT_GROUP_GOAL_COUNT = 8,
		TRIGGER_PAIR_COUNT = 2,
		TRIGGER_COUNT_PER_PAIR = 2,
		TEAM_NONE = 10,
		MISSION_GOALS_REFRESH_TICKS = 472,
		SCREEN_WIDTH = 320,
		SCREEN_HEIGHT = 200,
		COLOR_MAP_BACKGROUND = 0x34,
		COLOR_NORMAL_TEXT = 0x43,
		COLOR_ACTIVE_PAGE = 0x46,
		COLOR_FAILURE = 0x4A,
		COLOR_UNRESOLVED = 0x4E,
		COLOR_SUCCESS = 0x52,
		COCKPIT_OVERLAY_STR_BONUS = 21,
		COCKPIT_OVERLAY_STR_PENALTY = 22,
	};

	int playerIff;
	int totalVisibleGoals;
	int scrollTop;
	int lineIndex;
	int16_t sectionIdx;
	uint16_t cursorY;
	uint16_t goalType;
	int16_t bottom;
	int16_t top;
	int16_t left;
	int16_t right;
	int16_t lineStep;
	int16_t pairIdx;
	int16_t lastVisibleLine;
	int16_t triggerIdx;
	uint16_t goalIdx;
	int16_t flightGroupIdx;
	int16_t statusTitle;
	unsigned int statusColor;
	char text[80];

	if (g_players[g_localPlayer].mapCameraState != 0) {
		left = g_mfdMapBlitSourceX + 2;
		right = g_mfdMapBlitSourceX + g_mfdMapBlitWidth - 2;
		top = g_mfdMapBlitSourceY + 2;
		bottom = g_mfdMapBlitSourceY + g_mfdMapBlitHeight - 2;
	} else {
		left = g_mfdGoalsBlitSourceX + 2;
		right = g_mfdGoalsBlitSourceX + g_mfdGoalsBlitWidth - 2;
		top = g_mfdGoalsBlitSourceY + 2;
		bottom = g_mfdGoalsBlitSourceY + g_mfdGoalsBlitHeight - 2;
	}

#ifdef XVT_MODERN
	XvtCockpitPages_SetOrigin(MFD_PAGE_GOALS, left - 2, top - 2);
#endif
	totalVisibleGoals = 0;
	playerIff = (uint16_t)g_players[g_localPlayer].playerIff;
	for (sectionIdx = 0; sectionIdx < SECTION_COUNT; ++sectionIdx) {
		g_mfdGoalsLineCounts[sectionIdx] = 0;
		for (goalType = 0; goalType <= GOAL_TYPE_MAX; ++goalType) {
			uint16_t globalState = g_flightMissionState.runtime.teamGlobalGoalState[playerIff][goalType];
			uint16_t displayState;

			if (globalState != 0 &&
				(globalState == g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] ||
				 globalState == g_mfdGoalsCountAltStateBySectionType[sectionIdx][goalType])) {
				for (pairIdx = 0; pairIdx < TRIGGER_PAIR_COUNT; ++pairIdx) {
					int teamOrVariable = TEAM_NONE;

					for (triggerIdx = 0; triggerIdx < TRIGGER_COUNT_PER_PAIR; ++triggerIdx) {
						uint16_t condition = g_missionGlobalGoals[playerIff][goalType]
												 .triggerPairs[pairIdx]
												 .triggers[triggerIdx]
												 .condition;
						uint16_t variableType = g_missionGlobalGoals[playerIff][goalType]
													.triggerPairs[pairIdx]
													.triggers[triggerIdx]
													.variableType;
						uint16_t variable = g_missionGlobalGoals[playerIff][goalType]
												.triggerPairs[pairIdx]
												.triggers[triggerIdx]
												.variable;
						uint16_t amount = (uint8_t)g_missionGlobalGoals[playerIff][goalType]
											  .triggerPairs[pairIdx]
											  .triggers[triggerIdx]
											  .amount;

						if (condition == MISSION_COND_NO_CONDITION || condition == MISSION_COND_NEVER_FALSE ||
							condition == MISSION_COND_ALWAYS_TRUE) {
							continue;
						}
						if (g_missionGlobalGoals[playerIff][goalType]
									.triggerPairs[pairIdx]
									.triggers[triggerIdx + 1]
									.condition == MISSION_COND_NO_CONDITION &&
							g_missionGlobalGoals[playerIff][goalType]
									.triggerPairs[pairIdx]
									.triggers[triggerIdx + 1]
									.variableType == GOAL_TARGET_TEAM) {
							teamOrVariable = g_missionGlobalGoals[playerIff][goalType]
												 .triggerPairs[pairIdx]
												 .triggers[triggerIdx + 1]
												 .variable;
						}
						if (((uint16_t)Mission_EvaluateCondition(condition, variableType, variable, amount, 0,
																 teamOrVariable) &
							 g_mfdGoalsConditionMaskBySectionType[sectionIdx][goalType]) != 0) {
							++g_mfdGoalsLineCounts[sectionIdx];
							if (goalType == 2)
								++g_mfdGoalsLineCounts[sectionIdx];
						}
					}
				}
			}

			displayState = g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType];
			for (goalIdx = 0; goalIdx < FLIGHT_GROUP_GOAL_COUNT; ++goalIdx) {
				for (flightGroupIdx = 0; flightGroupIdx < g_missionHeader.numFlightGroups; ++flightGroupIdx) {
					if (g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].enabledTeams[playerIff] ==
							0 ||
						g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].type != goalType ||
						g_missionFgStats[flightGroupIdx].goalState[8 * playerIff + goalIdx] != displayState) {
						continue;
					}
					if (goalType == 2) {
						if (sectionIdx == GOAL_TITLE_STR_FAILED_OBJECTIVES &&
							250 * g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].points < 0) {
							g_mfdGoalsLineCounts[sectionIdx] += 2;
						} else if (sectionIdx == GOAL_TITLE_STR_COMPLETED_OBJECTIVES &&
								   250 * g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].points >=
									   0) {
							g_mfdGoalsLineCounts[sectionIdx] += 2;
						}
					} else {
						++g_mfdGoalsLineCounts[sectionIdx];
					}
				}
			}
			if (g_mfdGoalsLineCounts[sectionIdx] != 0)
				++g_mfdGoalsLineCounts[sectionIdx];
			totalVisibleGoals += g_mfdGoalsLineCounts[sectionIdx];
		}
	}

	FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight,
							 g_flight16bppBytesPerPixel * g_screenWidth);
	if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + HUD_MFD_GOALS_ELEMENT] !=
		(uint16_t)g_mfdPageStates[MFD_PAGE_GOALS]) {
		if (g_players[g_localPlayer].mapCameraState != 0) {
			FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
		} else {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		}
		FlightText_SetClipRect(left - 2, top - 2, right + 2, bottom + 2);
		g_flightFillClipRectFn();
		if (g_mfdPageStates[MFD_PAGE_GOALS] == MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			XvtCockpitPages_Clear(MFD_PAGE_GOALS);
#endif
			if (g_mfdActivePage == MFD_PAGE_GOALS) {
				g_mfdActivePage = g_mfdSecondaryPage;
				g_mfdSecondaryPage = Mfd_FindSecondaryOpenPage();
			}
			FlightSw_SetRenderTarget(NULL, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
			return 0;
		}
	}

	FlightText_SetClipRect(left, top, right, bottom);
	FlightText_SetWordWrap(1);
	FlightText_SetClearLineBackground(1);
	FlightText_SetFontTier(0);
	if (g_players[g_localPlayer].mapCameraState != 0) {
		FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
	} else {
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
	}
	lineStep = g_flightFontLineHeight + 2;
	if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + HUD_MFD_GOALS_ELEMENT] !=
		(uint16_t)g_mfdPageStates[MFD_PAGE_GOALS]) {
		scrollTop = 0;
		g_mfdGoalsUnusedState = 0;
		g_mfdGoalsCachedVisibleGoalCount = 0;
		g_mfdGoalsCurrentTotalLines = 0;
		g_playerFlightTransientTimers[g_localPlayer].missionGoalsRefreshTimer = MISSION_GOALS_REFRESH_TICKS;
		g_mfdGoalsRedrawNeeded = 1;
		g_mfdGoalsCachedPrimaryStatus = -1;
		g_mfdGoalsCachedSecondaryStatus = -1;
	} else {
		scrollTop = g_mfdGoalsCurrentScrollTop;
		if (g_mfdActivePage == MFD_PAGE_GOALS) {
			switch (g_currentActionKey) {
				case FLIGHT_KEY_MFD_SCROLL_UP:
					if (scrollTop > 0) {
						g_mfdGoalsRedrawNeeded = 1;
						--scrollTop;
					}
					break;
				case FLIGHT_KEY_MFD_SCROLL_DOWN:
					if ((top - bottom) / lineStep + g_mfdGoalsCurrentTotalLines >= scrollTop) {
						g_mfdGoalsRedrawNeeded = 1;
						++scrollTop;
					}
					break;
			}
		}
		if (totalVisibleGoals != g_mfdGoalsCachedVisibleGoalCount) {
			g_mfdGoalsRedrawNeeded = 1;
			g_mfdGoalsCachedVisibleGoalCount = totalVisibleGoals;
		}
		for (sectionIdx = 0; sectionIdx < 8; ++sectionIdx) {
			if (g_mfdGoalsCachedLineCounts[sectionIdx] != g_mfdGoalsLineCounts[sectionIdx]) {
				g_mfdGoalsRedrawNeeded = 1;
				break;
			}
		}
		if (g_flightMissionState.runtime.teamGoalStatus[(uint16_t)g_players[g_localPlayer].playerIff][0] !=
				g_mfdGoalsCachedPrimaryStatus ||
			g_flightMissionState.runtime.teamGoalStatus[(uint16_t)g_players[g_localPlayer].playerIff][1] !=
				g_mfdGoalsCachedSecondaryStatus) {
			g_mfdGoalsRedrawNeeded = 1;
		}
	}

	if ((int16_t)g_playerFlightTransientTimers[g_localPlayer].missionGoalsRefreshTimer <= 0) {
		g_mfdGoalsRedrawNeeded = 1;
		g_playerFlightTransientTimers[g_localPlayer].missionGoalsRefreshTimer = MISSION_GOALS_REFRESH_TICKS;
	}
	g_mfdGoalsCurrentScrollTop = scrollTop;
	lastVisibleLine = scrollTop + (bottom - top) / lineStep - 1;
	cursorY = top;

	if (g_mfdGoalsRedrawNeeded != 0) {
#ifdef XVT_MODERN
		XvtCockpitPages_Clear(MFD_PAGE_GOALS);
		XvtCockpitPages_BeginSection(MFD_PAGE_GOALS, XVT_COCKPIT_PAGE_BODY);
#endif
		lineIndex = 0;
		FlightText_SetClipRect(left - 2, top - 2, right + 2, bottom + 2);
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBackground(MFD_PAGE_GOALS);
#endif
		g_flightFillClipRectFn();
		FlightText_SetClipRect(left, top, right, bottom);
		statusTitle = GOAL_TITLE_STR_MISSION_OUTCOME;
		FlightText_SetScratch(g_strGoalTitles[statusTitle]);
		switch (
			g_flightMissionState.runtime.teamGoalStatus[(uint16_t)g_players[g_localPlayer].playerIff][0]) {
			case 0:
				switch (g_flightMissionState.runtime
							.teamGoalStatus[(uint16_t)g_players[g_localPlayer].playerIff][1]) {
					case 0:
					case 2:
						statusTitle = GOAL_TITLE_STR_UNRESOLVED;
						statusColor = COLOR_UNRESOLVED;
						break;
					case 1:
						statusTitle = GOAL_TITLE_STR_LOSS;
						statusColor = COLOR_FAILURE;
						break;
				}
				break;
			case 1:
				switch (g_flightMissionState.runtime
							.teamGoalStatus[(uint16_t)g_players[g_localPlayer].playerIff][1]) {
					case 0:
					case 2:
						statusTitle = GOAL_TITLE_STR_VICTORY;
						statusColor = COLOR_SUCCESS;
						break;
					case 1:
						statusTitle = GOAL_TITLE_STR_DRAW;
						statusColor = COLOR_ACTIVE_PAGE;
						break;
				}
				break;
			case 2:
				statusTitle = GOAL_TITLE_STR_LOSS;
				statusColor = COLOR_FAILURE;
				break;
		}
		FlightText_AppendScratchChar(' ');
		FlightText_AppendScratchString(g_strGoalTitles[statusTitle]);
		FlightText_SetCursor(left, cursorY);
		FlightText_SetColor(statusColor);
		FlightText_DrawStringCentered(g_flightTextScratchBuffer);
		cursorY += lineStep;

		for (sectionIdx = 0; sectionIdx < SECTION_COUNT; ++sectionIdx) {
			uint8_t titlePending;

			FlightText_SetColor(g_goalTitleColorByIndex[sectionIdx]);
			titlePending = 1;
			for (goalType = 0; goalType <= GOAL_TYPE_MAX; ++goalType) {
				uint16_t globalState;
				uint16_t displayState;
				uint8_t bonusPrefixWidth;

				if (g_mfdGoalsLineCounts[sectionIdx] == 0) {
					continue;
				}
				bonusPrefixWidth = 0;
				globalState = g_flightMissionState.runtime.teamGlobalGoalState[playerIff][goalType];
				if (globalState != 0 &&
					(globalState == g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] ||
					 globalState == g_mfdGoalsCountAltStateBySectionType[sectionIdx][goalType])) {
					int16_t triggerOrdinal = 0;

					for (pairIdx = 0; pairIdx < TRIGGER_PAIR_COUNT; ++pairIdx) {
						int teamOrVariable = TEAM_NONE;

						for (triggerIdx = 0; triggerIdx < TRIGGER_COUNT_PER_PAIR;
							 ++triggerIdx, ++triggerOrdinal) {
							uint16_t condition = g_missionGlobalGoals[playerIff][goalType]
													 .triggerPairs[pairIdx]
													 .triggers[triggerIdx]
													 .condition;
							uint16_t variableType = g_missionGlobalGoals[playerIff][goalType]
														.triggerPairs[pairIdx]
														.triggers[triggerIdx]
														.variableType;
							uint16_t variable = g_missionGlobalGoals[playerIff][goalType]
													.triggerPairs[pairIdx]
													.triggers[triggerIdx]
													.variable;
							uint16_t amount = (uint8_t)g_missionGlobalGoals[playerIff][goalType]
												  .triggerPairs[pairIdx]
												  .triggers[triggerIdx]
												  .amount;
							uint16_t goalStatus;
							uint16_t drawnHeight;

							if (condition == MISSION_COND_NO_CONDITION ||
								condition == MISSION_COND_NEVER_FALSE ||
								condition == MISSION_COND_ALWAYS_TRUE) {
								continue;
							}
							if (g_missionGlobalGoals[playerIff][goalType]
										.triggerPairs[pairIdx]
										.triggers[triggerIdx + 1]
										.condition == MISSION_COND_NO_CONDITION &&
								g_missionGlobalGoals[playerIff][goalType]
										.triggerPairs[pairIdx]
										.triggers[triggerIdx + 1]
										.variableType == GOAL_TARGET_TEAM) {
								teamOrVariable = g_missionGlobalGoals[playerIff][goalType]
													 .triggerPairs[pairIdx]
													 .triggers[triggerIdx + 1]
													 .variable;
							}
							if (((uint16_t)Mission_EvaluateCondition(condition, variableType, variable,
																	 amount, 0, teamOrVariable) &
								 g_mfdGoalsConditionMaskBySectionType[sectionIdx][goalType]) == 0) {
								continue;
							}

							if (goalType == 2 && bonusPrefixWidth == 0) {
								int points = 250 * g_missionGlobalGoals[playerIff][goalType].rawPoints;
								if ((sectionIdx == GOAL_TITLE_STR_FAILED_OBJECTIVES && points >= 0) ||
									(sectionIdx == GOAL_TITLE_STR_COMPLETED_OBJECTIVES && points < 0)) {
									pairIdx = TRIGGER_PAIR_COUNT;
									break;
								}
								if (titlePending != 0) {
									if (lineIndex >= g_mfdGoalsCurrentScrollTop &&
										lineIndex < lastVisibleLine) {
										FlightText_SetCursor(left, cursorY);
										FlightText_DrawStringCentered(g_strGoalTitles[sectionIdx]);
										cursorY += lineStep;
									}
									titlePending = 0;
									++lineIndex;
								}
								FlightText_SetCursor(left, cursorY);
								sprintf(text, "(%s %ld) ",
										g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_BONUS + (points < 0)],
										(long)points);
								FlightText_SetColor(points < 0 ? COLOR_FAILURE : COLOR_SUCCESS);
								FlightText_DrawString(text);
								FlightText_SetColor(g_goalTitleColorByIndex[sectionIdx]);
								bonusPrefixWidth = FlightText_MeasureStringWidth(text);
							}

							if (titlePending != 0) {
								if (lineIndex >= g_mfdGoalsCurrentScrollTop && lineIndex < lastVisibleLine) {
									FlightText_SetCursor(left, cursorY);
									FlightText_DrawStringCentered(g_strGoalTitles[sectionIdx]);
									cursorY += lineStep;
								}
								titlePending = 0;
								++lineIndex;
							}
							drawnHeight = 0;
							if (lineIndex >= g_mfdGoalsCurrentScrollTop && lineIndex <= lastVisibleLine) {
								const char* overrideText = NULL;

								if (g_globalGoalOverrideStringHandles
										[playerIff][goalType][triggerOrdinal]
										[g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] & 3] !=
									0) {
									overrideText = (const char*)Memory_LockHandle(
										g_globalGoalOverrideStringHandles
											[playerIff][goalType][triggerOrdinal]
											[g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] & 3]);
								}
								FlightText_SetCursor(left, cursorY);
								goalStatus =
									sectionIdx == GOAL_TITLE_STR_CONDITIONS_TO_PREVENT && goalType == 1
										? 5
										: g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType];
								if (goalType == 2)
									g_flightCursorX += bonusPrefixWidth;
								if (overrideText != NULL) {
									FlightText_DrawString(overrideText);
									if (g_flightMissionState.runtime
												.globalGoalTriggerCounts[1][playerIff][goalType]
																		[triggerOrdinal] <= 1 ||
										condition == MISSION_COND_BOARDED ||
										condition == MISSION_COND_DOCKED ||
										condition == MISSION_COND_COMPLETED_MISSION ||
										condition == MISSION_COND_NOT_BOARDED ||
										condition == MISSION_COND_FAILED_MISSION ||
										condition == MISSION_COND_NOT_DOCKED) {
										g_flightDrawCharFn('\n');
										drawnHeight = FlightText_GetWrapHeightForString(overrideText);
										cursorY += drawnHeight + g_flightFontLineHeight + 2;
									} else {
										sprintf(text, " (%ld%%)",
												(long)(100 *
													   g_flightMissionState.runtime
														   .globalGoalTriggerCounts[0][playerIff][goalType]
																				   [triggerOrdinal] /
													   g_flightMissionState.runtime
														   .globalGoalTriggerCounts[1][playerIff][goalType]
																				   [triggerOrdinal]));
										if (sectionIdx == GOAL_TITLE_STR_FAILED_OBJECTIVES ||
											sectionIdx == GOAL_TITLE_STR_CONDITIONS_TO_PREVENT) {
											FlightText_SetColor(COLOR_FAILURE);
										} else if (sectionIdx == GOAL_TITLE_STR_COMPLETED_OBJECTIVES ||
												   sectionIdx == GOAL_TITLE_STR_OBJECTIVES_TO_ACCOMPLISH) {
											FlightText_SetColor(COLOR_SUCCESS);
										} else {
											FlightText_SetColor(COLOR_NORMAL_TEXT);
										}
										FlightText_SetScratch(overrideText);
										FlightText_AppendScratchString(text);
										FlightText_DrawString(text);
										FlightText_SetColor(g_goalTitleColorByIndex[sectionIdx]);
										g_flightDrawCharFn('\n');
										drawnHeight =
											FlightText_GetWrapHeightForString(g_flightTextScratchBuffer);
										cursorY += drawnHeight + g_flightFontLineHeight + 2;
									}
								} else {
									int percentComplete;

									if (g_flightMissionState.runtime
												.globalGoalTriggerCounts[1][playerIff][goalType]
																		[triggerOrdinal] <= 1 ||
										condition == MISSION_COND_BOARDED ||
										condition == MISSION_COND_DOCKED ||
										condition == MISSION_COND_COMPLETED_MISSION ||
										condition == MISSION_COND_NOT_BOARDED ||
										condition == MISSION_COND_FAILED_MISSION ||
										condition == MISSION_COND_NOT_DOCKED) {
										percentComplete = -1;
									} else {
										percentComplete = 100 *
														  g_flightMissionState.runtime
															  .globalGoalTriggerCounts[0][playerIff][goalType]
																					  [triggerOrdinal] /
														  g_flightMissionState.runtime
															  .globalGoalTriggerCounts[1][playerIff][goalType]
																					  [triggerOrdinal];
									}
									drawnHeight = goals_outputgoal(variable, condition, variableType,
																   goalStatus, amount, 0, overrideText,
																   percentComplete, sectionIdx);
									cursorY += drawnHeight;
									if (drawnHeight <= g_flightFontLineHeight + 2)
										drawnHeight = 0;
								}
								if (overrideText != NULL) {
									Memory_UnlockHandle(
										g_globalGoalOverrideStringHandles
											[playerIff][goalType][triggerOrdinal]
											[g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] & 3]);
								}
							}
							if (drawnHeight != 0) {
								lineIndex += 2;
							} else {
								++lineIndex;
							}
						}
					}
				}

				displayState = g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType];
				for (goalIdx = 0; goalIdx < FLIGHT_GROUP_GOAL_COUNT; ++goalIdx) {
					for (flightGroupIdx = 0; flightGroupIdx < g_missionHeader.numFlightGroups;
						 ++flightGroupIdx) {
						uint16_t drawnHeight;
						uint16_t goalStatus;
						uint16_t eventCondition;
						uint16_t amountOp;
						uint16_t timeLimit;

						if (g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].enabledTeams[playerIff] ==
								0 ||
							g_missionFgStats[flightGroupIdx].arrivalEnabled == 0 ||
							g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].type != goalType ||
							g_missionFgStats[flightGroupIdx].goalState[8 * playerIff + goalIdx] !=
								displayState) {
							continue;
						}
						eventCondition =
							g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].eventCondition;
						if (eventCondition == MISSION_COND_NEVER_FALSE ||
							eventCondition == MISSION_COND_ALWAYS_TRUE) {
							continue;
						}
						if (goalType == 2) {
							int points = 250 * g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].points;

							if ((sectionIdx == GOAL_TITLE_STR_FAILED_OBJECTIVES && points >= 0) ||
								(sectionIdx == GOAL_TITLE_STR_COMPLETED_OBJECTIVES && points < 0)) {
								continue;
							}
						}
						if (titlePending != 0) {
							if (lineIndex >= g_mfdGoalsCurrentScrollTop && lineIndex < lastVisibleLine) {
								FlightText_SetCursor(left, cursorY);
								FlightText_DrawStringCentered(g_strGoalTitles[sectionIdx]);
								cursorY += lineStep;
							}
							titlePending = 0;
							++lineIndex;
						}
						drawnHeight = 0;
						amountOp = (uint8_t)g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].amount;
						timeLimit = g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].timeLimit5s;
						if (lineIndex >= g_mfdGoalsCurrentScrollTop && lineIndex < lastVisibleLine) {
							const char* overrideText = NULL;

							if (g_missionFgOverrideStringHandles
									[flightGroupIdx][goalIdx]
									[g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] & 3] != 0) {
								overrideText = (const char*)Memory_LockHandle(
									g_missionFgOverrideStringHandles
										[flightGroupIdx][goalIdx]
										[g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] & 3]);
							}
							FlightText_SetCursor(left, cursorY);
							goalStatus = sectionIdx == GOAL_TITLE_STR_CONDITIONS_TO_PREVENT && goalType == 1
											 ? 5
											 : g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType];
							if (goalType == 2) {
								int points =
									250 * g_missionFlightGroups[flightGroupIdx].fg.goals[goalIdx].points;

								sprintf(text, "(%s %ld) ",
										g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_BONUS + (points < 0)],
										(long)points);
								FlightText_SetColor(points < 0 ? COLOR_FAILURE : COLOR_SUCCESS);
								FlightText_DrawString(text);
								FlightText_SetColor(g_goalTitleColorByIndex[sectionIdx]);
							}
							if (overrideText != NULL) {
								FlightText_DrawString(overrideText);
								g_flightDrawCharFn('\n');
								drawnHeight = FlightText_GetWrapHeightForString(overrideText);
								cursorY += drawnHeight + g_flightFontLineHeight + 2;
							} else {
								drawnHeight = goals_outputgoal(flightGroupIdx, eventCondition,
															   GOAL_TARGET_FLIGHT_GROUP, goalStatus, amountOp,
															   timeLimit, overrideText, -1, sectionIdx);
								cursorY += drawnHeight;
								if (drawnHeight <= g_flightFontLineHeight + 2)
									drawnHeight = 0;
							}
							if (overrideText != NULL) {
								Memory_UnlockHandle(
									g_missionFgOverrideStringHandles
										[flightGroupIdx][goalIdx]
										[g_mfdGoalsDisplayStateBySectionType[sectionIdx][goalType] & 3]);
							}
						}
						if (drawnHeight != 0) {
							lineIndex += 2;
						} else {
							++lineIndex;
						}
					}
				}
			}
		}

#ifdef XVT_MODERN
		XvtCockpitPages_EndSection();
#endif
		g_mfdGoalsCurrentTotalLines = lineIndex;
		for (sectionIdx = 0; sectionIdx < 8; ++sectionIdx)
			g_mfdGoalsCachedLineCounts[sectionIdx] = g_mfdGoalsLineCounts[sectionIdx];
		g_mfdGoalsCachedPrimaryStatus =
			g_flightMissionState.runtime.teamGoalStatus[(uint16_t)g_players[g_localPlayer].playerIff][0];
		g_mfdGoalsCachedSecondaryStatus =
			g_flightMissionState.runtime.teamGoalStatus[(uint16_t)g_players[g_localPlayer].playerIff][1];
	}

	if (g_players[g_localPlayer].mapCameraState == 0) {
		bottom = g_mfdGoalsBlitSourceY + lineStep * (g_mfdGoalsCurrentTotalLines + 1);
		if (bottom > g_mfdGoalsBlitSourceY + g_mfdGoalsBlitHeight - 2)
			bottom = g_mfdGoalsBlitSourceY + g_mfdGoalsBlitHeight - 2;
	}
	if (g_players[g_localPlayer].mapCameraState == 0 && g_mfdActivePage == MFD_PAGE_NONE) {
		g_mfdActivePage = MFD_PAGE_GOALS;
	}
	if (g_mfdActivePage == MFD_PAGE_GOALS) {
		FlightText_SetBackgroundColor(COLOR_ACTIVE_PAGE);
		FlightText_SetClipRect(left - 2, top - 2, right + 2, bottom + 2);
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBorder(MFD_PAGE_GOALS);
#endif
		g_flightFillRectClippedFn(left - 2, top - 2, right + 2, bottom + 2, 1);
	} else if (g_mfdSecondaryPage == MFD_PAGE_GOALS) {
		if (g_players[g_localPlayer].mapCameraState != 0) {
			FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
		} else {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		}
		FlightText_SetClipRect(left - 2, top - 2, right + 2, bottom + 2);
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBorder(MFD_PAGE_GOALS);
#endif
		g_flightFillRectClippedFn(left - 2, top - 2, right + 2, bottom + 2, 1);
	}
#ifdef XVT_MODERN
	XvtCockpitPages_RecordScroll(MFD_PAGE_GOALS, g_mfdGoalsCurrentScrollTop, g_mfdGoalsCurrentTotalLines, -1);
#endif
	g_mfdGoalsRedrawNeeded = 0;
	FlightText_SetWordWrap(0);
	FlightSw_SetRenderTarget(NULL, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
	return 0;
}

// FUNCTION: XVT 0x44BC90
void Mfd_DrawMissionScoreboardPage(void) {
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		DEFAULT_SCREEN_WIDTH = 320,
		DEFAULT_SCREEN_HEIGHT = 200,
		MAX_LOW_RES_PLAYER_ROWS = 7,
		SCORE_DIGIT_COLUMNS = 6,
		TEAM_KILL_STAT_FULL = 0,
		TEAM_KILL_STAT_SHARED = 1,
		COLOR_MFD_BACKGROUND = 0x2C,
		COLOR_MAP_BACKGROUND = 0x34,
		COLOR_NORMAL_TEXT = 0x43,
		COLOR_ACTIVE_PAGE = 0x46,
		COLOR_LOCAL_ENTRY = 0x4E,
		COLOR_WINNING_ENTRY = 0x52,
	};

	int teamCount;

	struct ScoreboardScratch {
		int participatingTeamCount;
		int order[TEAM_COUNT];
		int playerFgCountByTeam[TEAM_COUNT];
		int ownedFgCountByTeam[TEAM_COUNT];
		char text[80];
		int teamMarkers[TEAM_COUNT];
	} scratch;

	int16_t paneWidth;
	uint16_t spaceWidth;
	int16_t scoreColumnX;
	uint16_t lineStep;
	int16_t left;
	int16_t top;
	int16_t right;
	int16_t bottomY;
	int16_t rowY;
	int16_t lastVisibleExclusive;
	int16_t row;
	int entryCount;
	int16_t playerIdx;
	int16_t needsRedraw;
	int16_t sharedKillCount;

	FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight,
							 g_screenWidth * g_flight16bppBytesPerPixel);
	FlightText_SetFontTier(0);
	paneWidth = 0;
	needsRedraw = 0;
	for (playerIdx = 0; playerIdx < PLAYER_COUNT; ++playerIdx) {
		if (g_players[playerIdx].connectedFlag != 0) {
			int16_t nameWidth;

			FlightText_SetScratch(NetSession_GetPlayerName(playerIdx));
			nameWidth = (int16_t)FlightText_MeasureStringWidth(g_flightTextScratchBuffer);
			if (paneWidth < nameWidth) {
				paneWidth = nameWidth;
			}
		}
	}
	if (g_missionHeader.missionType == MISSION_TYPE_QUICK_START && g_pilotData.missionSequenceActive == 1) {
		paneWidth = (int16_t)(paneWidth + FlightText_MeasureStringWidth("(0)"));
	}
	{
		int16_t headerWidth;

		headerWidth =
			(int16_t)FlightText_MeasureStringWidth(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_PLAYER]);
		if (paneWidth < headerWidth) {
			paneWidth = headerWidth;
		}
	}
	FlightText_SetScratch(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_SCORE]);
	FlightText_AppendScratchString("          ");
	FlightText_AppendScratchString(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_KILLS]);
	spaceWidth = FlightText_MeasureStringWidth("  ");
	scoreColumnX = (int16_t)(paneWidth + spaceWidth);
	paneWidth = (int16_t)(paneWidth + FlightText_MeasureStringWidth(g_flightTextScratchBuffer));
	if (g_missionHeader.missionType == MISSION_TYPE_QUICK_START) {
		int fgIdx;
		int team;

		memset(scratch.teamMarkers, 0, sizeof(scratch.teamMarkers));
		memset(scratch.ownedFgCountByTeam, 0, sizeof(scratch.ownedFgCountByTeam));
		memset(scratch.playerFgCountByTeam, 0, sizeof(scratch.playerFgCountByTeam));
		for (fgIdx = 0; fgIdx < (int16_t)g_missionHeader.numFlightGroups; ++fgIdx) {
			if (g_missionFlightGroups[fgIdx].fg.playerNumber != 0 &&
				g_missionFgStats[fgIdx].hasArrived != 0) {
				++scratch.playerFgCountByTeam[g_missionFlightGroups[fgIdx].fg.team];
			}
			if (g_missionFlightGroups[fgIdx].playerOwnerIdx != -1 &&
				g_missionFgStats[fgIdx].hasArrived != 0) {
				++scratch.ownedFgCountByTeam[g_missionFlightGroups[fgIdx].fg.team];
			}
		}
		teamCount = 0;
		for (team = 0; team < TEAM_COUNT; ++team) {
			if (scratch.playerFgCountByTeam[team] != 0) {
				scratch.order[teamCount++] = team;
			}
		}
#ifdef XVT_MODERN
		if (teamCount > 0)
#endif
		{
			for (row = (int16_t)teamCount; row < TEAM_COUNT; ++row) {
				scratch.order[row] = scratch.order[teamCount - 1];
			}
		}
		entryCount = teamCount;
		if (g_pilotData.missionSequenceActive == 1) {
			scratch.participatingTeamCount = g_pilotData.meleeTournamentSequenceState.participatingTeamCount;
			for (row = 0; row < scratch.participatingTeamCount; ++row) {
				int placement;
				int16_t otherTeam;
				int score;

				placement = 0;
				score = g_flightMissionState.runtime.teamScores[TEAM_SCORE_MISSION][row] +
						g_flightMissionState.runtime.teamScores[TEAM_SCORE_BONUS_TENTHS][row] +
						g_pilotData.meleeTournamentSequenceState.teamStandings[row].totalScore;
				for (otherTeam = 0; otherTeam < scratch.participatingTeamCount; ++otherTeam) {
					if (otherTeam != row &&
						g_flightMissionState.runtime.teamScores[TEAM_SCORE_MISSION][otherTeam] +
								g_flightMissionState.runtime.teamScores[TEAM_SCORE_BONUS_TENTHS][otherTeam] +
								g_pilotData.meleeTournamentSequenceState.teamStandings[otherTeam].totalScore >
							score) {
						++placement;
					}
				}
				scratch.teamMarkers[row] = placement + 1;
			}
		}
	} else {
		entryCount = g_activeFlightPlayerCount;
	}
	if (entryCount == 0) {
		entryCount = 1;
	}

	if (g_players[g_localPlayer].mapCameraState != 0) {
		left = (int16_t)(g_mfdMapBlitSourceX + 2);
		top = (int16_t)(g_mfdMapBlitSourceY + 2);
		right = (int16_t)(g_mfdMapBlitSourceX + g_mfdMapBlitWidth - 2);
		bottomY = (int16_t)(g_mfdMapBlitSourceY + g_mfdMapBlitHeight - 2);
	} else {
		top = (int16_t)(g_mfdMissionScoreboardBlitSourceY + 2);
		++entryCount;
		left = (int16_t)(g_mfdMissionScoreboardBlitSourceX + 2);
		right = (int16_t)(left + paneWidth);
		bottomY = (int16_t)((int16_t)entryCount * (g_flightFontLineHeight + 1) + top + 2);
		if (g_mfdMissionScoreboardLastWidth != right ||
			g_mfdMissionScoreboardLastPlayerCount != g_activeFlightPlayerCount) {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			if (g_mfdMissionScoreboardLastPlayerCount != 0) {
				bottomY =
					(int16_t)((g_mfdMissionScoreboardLastPlayerCount + 1) * (g_flightFontLineHeight + 1) +
							  top + 2);
			}
			if (g_mfdMissionScoreboardLastWidth != 0) {
				right = (int16_t)g_mfdMissionScoreboardLastWidth;
			}
			FlightText_SetClipRect((int16_t)(left - 2), (int16_t)(top - 2), (int16_t)(right + 2),
								   (int16_t)(bottomY + 2));
			g_flightFillClipRectFn();
			right = (int16_t)(left + paneWidth);
			bottomY = (int16_t)((int16_t)entryCount * (g_flightFontLineHeight + 1) + top + 2);
			needsRedraw = 1;
		}
	}

#ifdef XVT_MODERN
	XvtCockpitPages_SetOrigin(MFD_PAGE_SCOREBOARD, left - 2, top - 2);
#endif
	rowY = top;
	lineStep = (uint16_t)(g_flightFontLineHeight + 1);
	if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + HUD_MFD_SCOREBOARD_ELEMENT] !=
			g_mfdPageStates[MFD_PAGE_SCOREBOARD] ||
		needsRedraw != 0) {
		if (g_players[g_localPlayer].mapCameraState != 0) {
			FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
		} else if (g_mfdPageStates[MFD_PAGE_SCOREBOARD] == MFD_PAGE_STATE_CLOSING ||
				   g_players[g_localPlayer].viewState.hudStateLive != HUD_VIEW_FORWARD) {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		} else {
			FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
		}
		FlightText_SetClipRect((int16_t)(left - 2), (int16_t)(top - 2), (int16_t)(right + 2),
							   (int16_t)(bottomY + 2));
#ifdef XVT_MODERN
		XvtCockpitPages_Clear(MFD_PAGE_SCOREBOARD);
		XvtCockpitPages_RecordBackground(MFD_PAGE_SCOREBOARD);
#endif
		g_flightFillClipRectFn();
		if (g_mfdPageStates[MFD_PAGE_SCOREBOARD] == MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			XvtCockpitPages_Clear(MFD_PAGE_SCOREBOARD);
#endif
			if (g_mfdActivePage == MFD_PAGE_SCOREBOARD) {
				g_mfdActivePage = g_mfdSecondaryPage;
				g_mfdSecondaryPage = Mfd_FindSecondaryOpenPage();
			}
			g_mfdMissionScoreboardLastWidth = 0;
			g_mfdMissionScoreboardLastPlayerCount = 0;
			FlightSw_SetRenderTarget(NULL, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, 0);
			return;
		}
		if (g_players[g_localPlayer].mapCameraState != 0) {
			FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
		} else if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
			FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
		} else {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		}
#ifdef XVT_MODERN
		XvtCockpitPages_BeginSection(MFD_PAGE_SCOREBOARD, XVT_COCKPIT_PAGE_HEADER);
#endif
		FlightText_SetColor(COLOR_ACTIVE_PAGE);
		FlightText_SetCursor(left, top);
		if (g_missionHeader.missionType == MISSION_TYPE_QUICK_START &&
			scratch.playerFgCountByTeam[(uint16_t)g_players[g_localPlayer].playerIff] > 1) {
			FlightText_DrawString(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_TEAM]);
		} else {
			FlightText_DrawString(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_PLAYER]);
		}
		FlightText_SetScratch(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_SCORE]);
		FlightText_AppendScratchString("     ");
		FlightText_AppendScratchString(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_KILLS]);
		FlightText_DrawStringRightAligned(g_flightTextScratchBuffer);
#ifdef XVT_MODERN
		XvtCockpitPages_EndSection();
#endif
		g_mfdMissionScoreboardFirstVisibleRow = 0;
		g_mfdMissionScoreboardLastWidth = right;
		g_mfdMissionScoreboardLastPlayerCount = g_activeFlightPlayerCount;
	}

	rowY = (int16_t)(rowY + lineStep + 1);
	if (g_players[g_localPlayer].mapCameraState == 0 && g_mfdActivePage == MFD_PAGE_NONE) {
		g_mfdActivePage = MFD_PAGE_SCOREBOARD;
	}
	if (g_mfdActivePage == MFD_PAGE_SCOREBOARD) {
		FlightText_SetBackgroundColor(COLOR_ACTIVE_PAGE);
		FlightText_SetClipRect((int16_t)(left - 2), (int16_t)(top - 2), (int16_t)(right + 2),
							   (int16_t)(bottomY + 2));
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBorder(MFD_PAGE_SCOREBOARD);
#endif
		g_flightFillRectClippedFn((uint16_t)(left - 2), (uint16_t)(top - 2), (uint16_t)(right + 2),
								  (uint16_t)(bottomY + 2), 1);
	} else {
		if (g_mfdSecondaryPage == MFD_PAGE_SCOREBOARD) {
			if (g_players[g_localPlayer].mapCameraState != 0) {
				FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
			} else if (g_players[g_localPlayer].viewState.hudStateLive != HUD_VIEW_FORWARD) {
				FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			} else {
				FlightText_SetBackgroundColor(COLOR_LOCAL_ENTRY);
			}
			FlightText_SetClipRect((int16_t)(left - 2), (int16_t)(top - 2), (int16_t)(right + 2),
								   (int16_t)(bottomY + 2));
#ifdef XVT_MODERN
			XvtCockpitPages_RecordBorder(MFD_PAGE_SCOREBOARD);
#endif
			g_flightFillRectClippedFn((uint16_t)(left - 2), (uint16_t)(top - 2), (uint16_t)(right + 2),
									  (uint16_t)(bottomY + 2), 1);
		} else {
			if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
				FlightText_SetBackgroundColor(COLOR_LOCAL_ENTRY);
				FlightText_SetClipRect((int16_t)(left - 2), (int16_t)(top - 2), (int16_t)(right + 2),
									   (int16_t)(bottomY + 2));
#ifdef XVT_MODERN
				XvtCockpitPages_RecordBorder(MFD_PAGE_SCOREBOARD);
#endif
				g_flightFillRectClippedFn((uint16_t)(left - 2), (uint16_t)(top - 2), (uint16_t)(right + 2),
										  (uint16_t)(bottomY + 2), 1);
			}
		}
	}

	FlightText_SetClipRect(left, rowY, right, bottomY);
	if (g_players[g_localPlayer].mapCameraState != 0) {
		FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
	} else if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
		FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
	} else {
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
	}
	FlightText_SetColor(
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + HUD_MFD_SCOREBOARD_ELEMENT].colorIndex);
	if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240 && g_players[g_localPlayer].mapCameraState != 0 &&
		g_mfdActivePage == MFD_PAGE_SCOREBOARD) {
		switch (g_currentActionKey) {
			case FLIGHT_KEY_MFD_SCROLL_UP:
				if (g_mfdMissionScoreboardFirstVisibleRow > 0) {
					--g_mfdMissionScoreboardFirstVisibleRow;
				}
				break;
			case FLIGHT_KEY_MFD_SCROLL_DOWN:
				if (g_missionHeader.missionType == MISSION_TYPE_QUICK_START) {
					if ((top - bottomY) / lineStep + teamCount >= g_mfdMissionScoreboardFirstVisibleRow) {
						++g_mfdMissionScoreboardFirstVisibleRow;
					}
				} else if (g_activeFlightPlayerCount > MAX_LOW_RES_PLAYER_ROWS &&
						   g_mfdMissionScoreboardLastPlayerCount + (top - bottomY) / lineStep >=
							   g_mfdMissionScoreboardFirstVisibleRow) {
					++g_mfdMissionScoreboardFirstVisibleRow;
				}
				break;
		}
	}
	lastVisibleExclusive = (int16_t)(g_mfdMissionScoreboardFirstVisibleRow + (bottomY - top) / lineStep - 1);
#ifdef XVT_MODERN
	XvtCockpitPages_RecordScroll(MFD_PAGE_SCOREBOARD, g_mfdMissionScoreboardFirstVisibleRow,
								 g_players[g_localPlayer].mapCameraState != 0 ? entryCount : entryCount - 1,
								 -1);
	XvtCockpitPages_BeginSection(MFD_PAGE_SCOREBOARD, XVT_COCKPIT_PAGE_BODY);
#endif

	if (g_missionHeader.missionType == MISSION_TYPE_QUICK_START) {
		if (teamCount > 1) {
			int16_t swapped;

			do {
				swapped = 0;
				for (row = 0; row < teamCount - 1; ++row) {
					int team;
					int nextTeam;

					team = scratch.order[row];
					if (team < TEAM_COUNT) {
						nextTeam = scratch.order[row + 1];
						if (g_flightMissionState.runtime.teamScores[TEAM_SCORE_BONUS_TENTHS][nextTeam] +
								g_flightMissionState.runtime.teamScores[TEAM_SCORE_MISSION][nextTeam] >
							g_flightMissionState.runtime.teamScores[TEAM_SCORE_BONUS_TENTHS][team] +
								g_flightMissionState.runtime.teamScores[TEAM_SCORE_MISSION][team]) {
							int16_t previousTeam;

							previousTeam = scratch.order[row];
							scratch.order[row] = scratch.order[row + 1];
							scratch.order[row + 1] = previousTeam;
							swapped = 1;
							break;
						}
					}
				}
			} while (swapped != 0);
		}
		for (row = 0; row < teamCount; ++row) {
			int team;

			if (row >= g_mfdMissionScoreboardFirstVisibleRow && row < lastVisibleExclusive &&
				scratch.order[row] < TEAM_COUNT) {
				int marker;

				team = scratch.order[row];
				if (scratch.playerFgCountByTeam[team] == 1 && scratch.ownedFgCountByTeam[team] == 1) {
					int fgIdx;

					for (fgIdx = 0; fgIdx < (int16_t)g_missionHeader.numFlightGroups; ++fgIdx) {
						if (g_missionFlightGroups[fgIdx].playerOwnerIdx != -1 &&
							g_missionFlightGroups[fgIdx].fg.team == team) {
							FlightText_SetScratch(
								NetSession_GetPlayerName(g_missionFlightGroups[fgIdx].playerOwnerIdx));
							break;
						}
					}
					if (fgIdx == (int16_t)g_missionHeader.numFlightGroups) {
						FlightText_SetScratch(g_missionTeams[team].name);
					}
				} else {
					FlightText_SetScratch(g_missionTeams[team].name);
				}
				marker = scratch.teamMarkers[team];
				if (marker != 0 && (marker <= 3 || team == (uint16_t)g_players[g_localPlayer].playerIff)) {
					FlightText_AppendScratchChar(' ');
					FlightText_AppendScratchChar('(');
					FlightText_AppendScratchChar((char)(marker + '0'));
					FlightText_AppendScratchChar(')');
				}
				FlightText_SetClipRect(left, rowY, right, (int16_t)(rowY + lineStep));
				g_flightFillClipRectFn();
#ifdef XVT_MODERN
				XvtCockpitPages_RecordRow((uint32_t)team,
										  team == (uint16_t)g_players[g_localPlayer].playerIff);
#endif
				if (team == (uint16_t)g_players[g_localPlayer].playerIff) {
					FlightText_SetColor(COLOR_LOCAL_ENTRY);
				} else if (marker == 1) {
					FlightText_SetColor(COLOR_WINNING_ENTRY);
				} else {
					FlightText_SetColor(COLOR_NORMAL_TEXT);
				}
				FlightText_SetCursor(left, (uint16_t)rowY);
				FlightText_DrawString(g_flightTextScratchBuffer);
				g_flightDrawCharFn('\n');
				FlightText_FormatScratchInt(
					g_flightMissionState.runtime.teamScores[TEAM_SCORE_BONUS_TENTHS][team] +
					g_flightMissionState.runtime.teamScores[TEAM_SCORE_MISSION][team]);
				strcpy(scratch.text, g_flightTextScratchBuffer);
				strcat(scratch.text, "    ");
				sharedKillCount =
					(int16_t)g_flightMissionState.runtime.teamKillStats[TEAM_KILL_STAT_SHARED][team];
				FlightText_FormatScratchInt(
					(int16_t)g_flightMissionState.runtime.teamKillStats[TEAM_KILL_STAT_FULL][team]);
				strcat(scratch.text, g_flightTextScratchBuffer);
				strcat(scratch.text, "(");
				FlightText_FormatScratchInt(sharedKillCount);
				strcat(scratch.text, g_flightTextScratchBuffer);
				strcat(scratch.text, ")");
				FlightText_SetCursor(left, (uint16_t)rowY);
				FlightText_DrawStringRightAligned(scratch.text);
				rowY = (int16_t)(rowY + lineStep);
			}
		}
	} else {
		int16_t connectedCount;

		connectedCount = 0;
		for (playerIdx = 0; playerIdx < PLAYER_COUNT; ++playerIdx) {
			if (g_players[playerIdx].connectedFlag == 1 || g_players[playerIdx].connectedFlag == 2) {
				scratch.order[connectedCount++] = playerIdx;
			}
		}
#ifdef XVT_MODERN
		if (connectedCount > 0)
#endif
		{
			for (row = connectedCount; row < PLAYER_COUNT; ++row) {
				scratch.order[row] = scratch.order[connectedCount - 1];
			}
		}
		if (connectedCount > 1) {
			int16_t swapped;

			do {
				swapped = 0;
				for (row = 0; row < connectedCount - 1; ++row) {
					int player;
					int nextPlayer;

					player = scratch.order[row];
					if (player < PLAYER_COUNT) {
						nextPlayer = scratch.order[row + 1];
						if (g_players[nextPlayer].missionStats.missionScore +
								g_flightMissionState.runtime
									.teamScores[TEAM_SCORE_BONUS_TENTHS]
											   [(uint16_t)g_players[nextPlayer].playerIff] >
							g_players[player].missionStats.missionScore +
								g_flightMissionState.runtime
									.teamScores[TEAM_SCORE_BONUS_TENTHS]
											   [(uint16_t)g_players[player].playerIff]) {
							int16_t previousPlayer;

							previousPlayer = scratch.order[row];
							scratch.order[row] = scratch.order[row + 1];
							scratch.order[row + 1] = previousPlayer;
							swapped = 1;
							break;
						}
					}
				}
			} while (swapped != 0);
		}
		for (row = 0; row < connectedCount; ++row) {
			int player;

			if (row >= g_mfdMissionScoreboardFirstVisibleRow && row < lastVisibleExclusive &&
				scratch.order[row] < PLAYER_COUNT) {
				int16_t digits;
				int16_t xOffset;
				int16_t fullKillCount;
				int16_t fgIdx;
				int16_t rowBottomY;

				player = scratch.order[row];
				FlightText_SetScratch(NetSession_GetPlayerName(player));
				rowBottomY = (int16_t)(rowY + lineStep);
				FlightText_SetClipRect(left, rowY, right, rowBottomY);
				g_flightFillClipRectFn();
#ifdef XVT_MODERN
				XvtCockpitPages_RecordRow((uint32_t)player, player == g_localPlayer);
#endif
				if (player == g_localPlayer) {
					FlightText_SetColor(COLOR_LOCAL_ENTRY);
				} else {
					FlightText_SetColor(COLOR_NORMAL_TEXT);
				}
				FlightText_SetCursor(left, (uint16_t)rowY);
				FlightText_DrawString(g_flightTextScratchBuffer);
				g_flightDrawCharFn('\n');
				digits = (int16_t)FlightText_FormatScratchInt(
					g_players[player].missionStats.missionScore +
					g_flightMissionState.runtime
						.teamScores[TEAM_SCORE_BONUS_TENTHS][(uint16_t)g_players[player].playerIff]);
				xOffset = 0;
				if (digits < SCORE_DIGIT_COLUMNS) {
					xOffset = (int16_t)(spaceWidth * (SCORE_DIGIT_COLUMNS - digits));
				}
				FlightText_SetCursor(left + xOffset + scoreColumnX, (uint16_t)rowY);
				strcpy(scratch.text, g_flightTextScratchBuffer);
				strcat(scratch.text, "    ");
				fullKillCount = 0;
				sharedKillCount = 0;
				for (fgIdx = 0; fgIdx < (int16_t)g_missionHeader.numFlightGroups; ++fgIdx) {
					fullKillCount =
						(int16_t)(fullKillCount +
								  g_players[player].perMissionKills.killsFullOnFlightGroup[fgIdx]);
					sharedKillCount =
						(int16_t)(sharedKillCount +
								  g_players[player].perMissionKills.killsSharedOnFlightGroup[fgIdx]);
				}
				FlightText_FormatScratchInt(fullKillCount);
				strcat(scratch.text, g_flightTextScratchBuffer);
				strcat(scratch.text, "(");
				FlightText_FormatScratchInt(sharedKillCount);
				strcat(scratch.text, g_flightTextScratchBuffer);
				strcat(scratch.text, ")");
				FlightText_SetCursor(left, (uint16_t)rowY);
				FlightText_DrawStringRightAligned(scratch.text);
				rowY = rowBottomY;
			}
		}
	}
#ifdef XVT_MODERN
	XvtCockpitPages_EndSection();
#endif
	FlightSw_SetRenderTarget(NULL, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, 0);
}

// FUNCTION: XVT 0x44CE40
void Mfd_DrawCraftListPage(uint16_t showFlightGroupsPage) {
	enum {
		MAX_FLIGHT_GROUPS = 48,
		DEFAULT_SCREEN_WIDTH = 320,
		DEFAULT_SCREEN_HEIGHT = 200,
		TARGET_NONE_AI = 255,
		DISPLAY_NAME_FLAGS = 7,
		REFRESH_TICKS = 472,
		COLOR_MFD_BACKGROUND = 0x2C,
		COLOR_MAP_BACKGROUND = 0x34,
		COLOR_NORMAL_TEXT = 0x43,
		COLOR_HEALTHY = 0x46,
		COLOR_DAMAGED = 0x4A,
		COLOR_MEDIUM = 0x4E,
		HEALTH_CRITICAL_PERCENT = 20,
		HEALTH_GOOD_PERCENT = 50,
		PERCENTAGE_SCALE = 655,
		HEALTH_DIGIT_WIDTH = 3,
		CRAFT_NAME_COLUMN_WIDTH = 11,
		ORDERS_COLUMN_OFFSET = 17,
		STATUS_COLUMN_OFFSET = 15,
	};

	uint16_t craftRows[MAX_FLIGHT_GROUPS];
	int localPlayer;
	uint16_t playerIff;
	uint16_t pageIndex;
	uint16_t layoutIndex;
	uint16_t rowCount;
	int rowTotal;
	int flightGroupIdx;
	int playerObjectIdx;
	int row;
	int otherRow;
	uint16_t lineStep;
	int16_t paneLeft;
	uint16_t paneTop;
	int16_t paneRight;
	int16_t paneBottom;
	uint16_t rowY;
	int halfCharacterWidth;
	uint16_t needsRedraw;
	uint16_t lastVisibleRow;
	uint16_t lastTeam;
	int16_t* pageState;

	FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight,
							 g_screenWidth * g_flight16bppBytesPerPixel);
	FlightText_SetFontTier(0);
	localPlayer = g_localPlayer;
	craftRows[0] = g_players[localPlayer].boundFlightGroupIdx;
	playerIff = (uint16_t)g_players[localPlayer].playerIff;
	pageIndex = (uint16_t)(MFD_PAGE_FRIENDLY_CRAFT - showFlightGroupsPage);
	layoutIndex = (uint16_t)(HUD_MFD_CRAFT_LIST_ELEMENT + showFlightGroupsPage);
	playerObjectIdx = g_players[localPlayer].objectIndex;
	do {
		if (playerObjectIdx != -1 &&
			g_objectTable[playerObjectIdx].mobj->pCraft->systemHealth[DAMAGE_SYSTEM_04_TARGETING_COMPUTER] == 0) {
			pageState = &g_mfdPageStates[pageIndex];
			if (*pageState != MFD_PAGE_STATE_CLOSING) {
				g_msgArgTable[0] = IFMSG_096_TARGETING_COMPUTER;
				g_msgArgTable[1] = IFMSG_087_DAMAGED_AND_INOPERATIVE;
				*pageState = MFD_PAGE_STATE_CLOSING;
				msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG, g_localPlayer);
				break;
			}
		}

		{
			uint16_t friendlyFlightGroups[MAX_FLIGHT_GROUPS];
			uint16_t flightGroupCount;

			friendlyFlightGroups[0] = craftRows[0];
			flightGroupCount = 1;
		if (showFlightGroupsPage == 1) {
			int objectIdx;

			rowCount = 0;
			for (objectIdx = g_activeRegionObjectSlotStart; objectIdx < g_activeRegionCraftObjectSlotEnd;
				 ++objectIdx) {
				CraftData* craft;
				uint16_t team;

				if (g_objectTable[objectIdx].objectType != 0 && g_objectTable[objectIdx].mobj != NULL && g_objectTable[objectIdx].mobj->pCraft != NULL) {
					team = g_objectTable[objectIdx].mobj->team;
					if (g_objectTable[objectIdx].mobj->team != playerIff && g_missionTeams[playerIff].allies[team] == 0) {
						craft = g_objectTable[objectIdx].mobj->pCraft;
						if (craft->objectKind != CRAFT_OBJECT_KIND_BREAKING_UP &&
							craft->objectKind != CRAFT_OBJECT_KIND_EXPLODING) {
							craftRows[rowCount++] = (uint16_t)(objectIdx | (team << 8));
						}
					}
				}
			}
		} else {
			for (flightGroupIdx = 0; flightGroupIdx < g_missionHeader.numFlightGroups; ++flightGroupIdx) {
				int team;
				int isHostile;

				team = g_missionFlightGroups[flightGroupIdx].fg.team;
				isHostile = team == playerIff ? 0 : g_missionTeams[playerIff].allies[team] < 1;
				if (!isHostile && flightGroupIdx != craftRows[0]) {
					friendlyFlightGroups[flightGroupCount++] = (uint16_t)flightGroupIdx;
				}
			}
			rowCount = 0;
			for (flightGroupIdx = 0; flightGroupIdx < flightGroupCount; ++flightGroupIdx) {
				int friendlyFlightGroup;
				int objectIdx;

				friendlyFlightGroup = friendlyFlightGroups[flightGroupIdx];
				for (objectIdx = g_activeRegionObjectSlotStart; objectIdx < g_activeRegionCraftObjectSlotEnd;
					 ++objectIdx) {
					MobileObject* mobileObject;
					CraftData* craft;

					if (g_objectTable[objectIdx].flightGroupIdx == friendlyFlightGroup && g_objectTable[objectIdx].objectType != 0) {
						mobileObject = g_objectTable[objectIdx].mobj;
						if (mobileObject != NULL) {
							craft = mobileObject->pCraft;
							if (craft != NULL && craft->objectKind != CRAFT_OBJECT_KIND_BREAKING_UP &&
								craft->objectKind != CRAFT_OBJECT_KIND_EXPLODING) {
								craftRows[rowCount++] =
									(uint16_t)(objectIdx | ((uint16_t)mobileObject->team << 8));
							}
						}
					}
				}
			}
		}

		}
		rowTotal = rowCount;
		row = 0;
		do {
			for (otherRow = row + 1; otherRow < rowCount; ++otherRow) {
				int currentTeam;
				int otherTeam;
				int currentRow;
				uint16_t savedRow;

				currentRow = craftRows[row];
				currentTeam = currentRow & ~0xFF;
				otherTeam = (uint16_t)(craftRows[otherRow] & 0xFF00);
				if (otherTeam < currentTeam) {
					savedRow = currentRow;
					craftRows[row] = craftRows[otherRow];
					craftRows[otherRow] = savedRow;
					otherRow = row + 1;
				}
			}
			++row;
		} while (row < rowCount);
		if (rowCount == 0) {
			pageState = &g_mfdPageStates[pageIndex];
			if (*pageState != MFD_PAGE_STATE_CLOSING) {
				if (*pageState != MFD_PAGE_STATE_CLOSED) {
					*pageState = MFD_PAGE_STATE_CLOSING;
				} else {
					*pageState = MFD_PAGE_STATE_CLOSED;
				}
				break;
			}
		}

		lineStep = (uint16_t)(g_flightFontLineHeight + 2);
		if (showFlightGroupsPage == 1 && g_players[localPlayer].mapCameraState != 0) {
			paneLeft = (int16_t)(g_mfdMapBlitSourceX + 2);
			paneTop = (int16_t)(g_mfdMapBlitSourceY + 2);
			paneRight = (int16_t)(g_mfdMapBlitSourceX + g_mfdMapBlitWidth - 2);
			paneBottom = (int16_t)(g_mfdMapBlitHeight + g_mfdMapBlitSourceY - 2);
		} else {
			paneLeft = (int16_t)(g_mfdCraftListBlitSourceX + 2);
			paneTop = (int16_t)(g_mfdCraftListBlitSourceY + 2);
			paneRight = (int16_t)(g_mfdCraftListBlitSourceX + g_mfdCraftListBlitWidth - 2);
			if (g_mfdPageStates[pageIndex] == MFD_PAGE_STATE_CLOSING ||
				g_players[localPlayer].mapCameraState != 0) {
				paneBottom = (int16_t)(g_mfdCraftListBlitHeight + g_mfdCraftListBlitSourceY - 2);
			} else {
				paneBottom = (int16_t)(g_mfdCraftListBlitSourceY + lineStep * (rowCount + 1));
				if (paneBottom > g_mfdCraftListBlitHeight + g_mfdCraftListBlitSourceY - 2 ||
					paneBottom <= g_mfdCraftListBlitSourceY) {
					paneBottom = (int16_t)(g_mfdCraftListBlitHeight + g_mfdCraftListBlitSourceY - 2);
				}
			}
		}
		if (g_players[localPlayer].mapCameraState == 0 && g_mfdActivePage == MFD_PAGE_NONE) {
			g_mfdActivePage = (int16_t)pageIndex;
		}

#ifdef XVT_MODERN
		XvtCockpitPages_SetOrigin(pageIndex, paneLeft - 2, paneTop - 2);
#endif
		pageState = &g_mfdPageStates[pageIndex];
		if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + layoutIndex] != *pageState) {
			int16_t clearBottom;

			if (g_players[localPlayer].mapCameraState != 0) {
				FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
			} else if (*pageState == MFD_PAGE_STATE_CLOSING ||
					   g_players[localPlayer].viewState.hudStateLive != HUD_VIEW_FORWARD) {
				FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			} else {
				FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
			}
			if (g_players[g_localPlayer].mapCameraState != 0) {
				if (showFlightGroupsPage == 1) {
					clearBottom = (int16_t)(g_mfdMapBlitHeight + g_mfdMapBlitSourceY - 2);
				} else {
					clearBottom = (int16_t)(g_mfdCraftListBlitHeight + g_mfdCraftListBlitSourceY - 2);
				}
			} else {
				clearBottom = paneBottom;
			}
			FlightText_SetClipRect((int16_t)(paneLeft - 2), (int16_t)(paneTop - 2), (int16_t)(paneRight + 2),
								   (int16_t)(clearBottom + 2));
			g_flightFillClipRectFn();
			FlightText_SetClipRect((int16_t)(paneLeft - 2), (int16_t)(paneTop - 2), (int16_t)(paneRight + 2),
								   (int16_t)(paneBottom + 2));
			if (*pageState == MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
				XvtCockpitPages_Clear(pageIndex);
#endif
				if (g_mfdActivePage == pageIndex) {
					g_mfdActivePage = g_mfdSecondaryPage;
					g_mfdSecondaryPage = Mfd_FindSecondaryOpenPage();
				}
				FlightSw_SetRenderTarget(NULL, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, 0);
				return;
			}
#ifdef XVT_MODERN
			XvtCockpitPages_Clear(pageIndex);
			XvtCockpitPages_RecordBackground(pageIndex);
			XvtCockpitPages_BeginSection(pageIndex, XVT_COCKPIT_PAGE_HEADER);
#endif
			FlightText_SetClearLineBackground(1);
			FlightText_SetColor(COLOR_HEALTHY);
			FlightText_SetFontTier(0);
			FlightText_SetClipRect(paneLeft, paneTop, paneRight, paneBottom);
			if (g_players[g_localPlayer].mapCameraState != 0) {
				FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
			} else if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
				FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
			} else {
				FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			}
			halfCharacterWidth = g_flightFontHalfHeight;
			FlightText_SetCursor(paneLeft, paneTop);
			FlightText_DrawString(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_CRAFT]);
			{
				int16_t healthHeaderX;

				healthHeaderX = halfCharacterWidth;
				healthHeaderX = (int16_t)(healthHeaderX * CRAFT_NAME_COLUMN_WIDTH + paneLeft);
				FlightText_SetScratch(g_strCmdThreatDisplayText[1]);
				FlightText_AppendScratchString("/");
				FlightText_AppendScratchString(g_strCmdThreatDisplayText[2]);
				FlightText_SetCursor(healthHeaderX, paneTop);
			}
			FlightText_DrawString(g_flightTextScratchBuffer);
			{
				int16_t targetColumnX;

				targetColumnX = (int16_t)(g_flightCursorX + halfCharacterWidth);
				if (showFlightGroupsPage == 1) {
					FlightText_SetCursor(targetColumnX, paneTop);
					FlightText_DrawString(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_TARGET]);
					if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240) {
						int16_t ordersColumnX;

						ordersColumnX = halfCharacterWidth;
						ordersColumnX = (int16_t)(ordersColumnX * ORDERS_COLUMN_OFFSET + targetColumnX);
						FlightText_SetCursor(ordersColumnX, paneTop);
						FlightText_DrawString(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_ORDERS]);
					}
				} else if (g_players[g_localPlayer].mapCameraState == 0 ||
						   g_flightResolutionMode != FLIGHT_RESOLUTION_320X240) {
					FlightText_SetCursor(targetColumnX, paneTop);
					FlightText_DrawStringRightAligned(g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_TARGET]);
				}
			}
#ifdef XVT_MODERN
			XvtCockpitPages_EndSection();
#endif
			needsRedraw = 1;
			rowY = (int16_t)(paneTop + lineStep);
			g_mfdCraftListTopRowByMode[showFlightGroupsPage] = 0;
			g_playerFlightTransientTimers[g_localPlayer].mfdCraftListRefreshTimer = REFRESH_TICKS;
			FlightText_SetClipRect(paneLeft, rowY, paneRight, paneBottom);
			g_mfdCraftListCachedRowCount = rowCount;
		} else {
			rowY = (int16_t)(paneTop + lineStep);
			halfCharacterWidth = g_flightFontHalfHeight;
			FlightText_SetClearLineBackground(1);
			FlightText_SetColor(COLOR_HEALTHY);
			FlightText_SetFontTier(0);
			FlightText_SetClipRect(paneLeft, paneTop, paneRight, paneBottom);
			if (g_players[g_localPlayer].mapCameraState != 0) {
				FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
			} else if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
				FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
			} else {
				FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			}
			if (rowCount != g_mfdCraftListCachedRowCount) {
				int16_t clearBottom;

				if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
					FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
				}
				if (showFlightGroupsPage == 1 && g_players[g_localPlayer].mapCameraState != 0) {
					clearBottom = (int16_t)(g_mfdMapBlitHeight + g_mfdMapBlitSourceY - 2);
				} else {
					clearBottom = (int16_t)(g_mfdCraftListBlitHeight + g_mfdCraftListBlitSourceY - 2);
				}
				FlightText_SetClipRect((int16_t)(paneLeft - 2), rowY, (int16_t)(paneRight + 2),
									   (int16_t)(clearBottom + 2));
				g_flightFillClipRectFn();
				if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
					int16_t newBottom;

					FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
					newBottom = (int16_t)(rowY + lineStep * rowCount);
					if (newBottom > g_mfdCraftListBlitHeight + g_mfdCraftListBlitSourceY - 2) {
						newBottom = paneBottom;
					}
					FlightText_SetClipRect((int16_t)(paneLeft - 2), rowY, (int16_t)(paneRight + 2),
										   (int16_t)(newBottom + 2));
					g_flightFillClipRectFn();
				}
				needsRedraw = 1;
				g_mfdCraftListCachedRowCount = rowCount;
			} else {
				needsRedraw = 0;
			}
		}

		if (g_mfdActivePage == pageIndex) {
			int16_t borderTop;
			FlightText_SetBackgroundColor(COLOR_HEALTHY);
			borderTop = (int16_t)(paneTop - 2);
			FlightText_SetClipRect((int16_t)(paneLeft - 2), borderTop, (int16_t)(paneRight + 2),
								   (int16_t)(paneBottom + 2));
#ifdef XVT_MODERN
			XvtCockpitPages_RecordBorder(pageIndex);
#endif
			g_flightFillRectClippedFn((uint16_t)(paneLeft - 2), (uint16_t)borderTop,
									  (uint16_t)(paneRight + 2), (uint16_t)(paneBottom + 2), 1);
		} else if (g_mfdSecondaryPage == pageIndex) {
			int16_t borderTop;
			if (g_players[g_localPlayer].mapCameraState != 0) {
				FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
			} else if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
				FlightText_SetBackgroundColor(COLOR_MEDIUM);
			} else {
				FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			}
			borderTop = (int16_t)(paneTop - 2);
			FlightText_SetClipRect((int16_t)(paneLeft - 2), borderTop, (int16_t)(paneRight + 2),
								   (int16_t)(paneBottom + 2));
#ifdef XVT_MODERN
			XvtCockpitPages_RecordBorder(pageIndex);
#endif
			g_flightFillRectClippedFn((uint16_t)(paneLeft - 2), (uint16_t)borderTop,
									  (uint16_t)(paneRight + 2), (uint16_t)(paneBottom + 2), 1);
		} else if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
			int16_t borderTop;

			FlightText_SetBackgroundColor(COLOR_MEDIUM);
			borderTop = (int16_t)(paneTop - 2);
			FlightText_SetClipRect((int16_t)(paneLeft - 2), borderTop, (int16_t)(paneRight + 2),
								   (int16_t)(paneBottom + 2));
#ifdef XVT_MODERN
			XvtCockpitPages_RecordBorder(pageIndex);
#endif
			g_flightFillRectClippedFn((uint16_t)(paneLeft - 2), (uint16_t)borderTop,
									  (uint16_t)(paneRight + 2), (uint16_t)(paneBottom + 2), 1);
		}
		if (g_players[g_localPlayer].mapCameraState != 0) {
			FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
		} else if (g_players[g_localPlayer].viewState.hudStateLive == HUD_VIEW_FORWARD) {
			FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
		} else {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		}

		if (g_mfdActivePage == pageIndex) {
			unsigned int actionKey;

			actionKey = g_currentActionKey;
			switch (actionKey) {
				case FLIGHT_KEY_MFD_SCROLL_UP:
					if (g_mfdCraftListTopRowByMode[showFlightGroupsPage] != 0) {
						--g_mfdCraftListTopRowByMode[showFlightGroupsPage];
						needsRedraw = 1;
					}
					break;
				case FLIGHT_KEY_MFD_SCROLL_DOWN:
					if (rowCount > 1 && rowCount + (rowY - paneBottom) / lineStep >=
											(uint16_t)g_mfdCraftListTopRowByMode[showFlightGroupsPage]) {
						++g_mfdCraftListTopRowByMode[showFlightGroupsPage];
						needsRedraw = 1;
					}
					break;
			}
		}
		if ((int16_t)g_playerFlightTransientTimers[g_localPlayer].mfdCraftListRefreshTimer <= 0) {
			needsRedraw = 1;
			g_playerFlightTransientTimers[g_localPlayer].mfdCraftListRefreshTimer = REFRESH_TICKS;
		}
		lastVisibleRow =
			(int16_t)(g_mfdCraftListTopRowByMode[showFlightGroupsPage] + (paneBottom - rowY) / lineStep);
		if (rowCount < (uint16_t)lastVisibleRow) {
			if (g_mfdCraftListTopRowByMode[showFlightGroupsPage] != 0) {
				--g_mfdCraftListTopRowByMode[showFlightGroupsPage];
			}
			lastVisibleRow =
				(int16_t)(g_mfdCraftListTopRowByMode[showFlightGroupsPage] + (paneBottom - rowY) / lineStep);
		}

#ifdef XVT_MODERN
		XvtCockpitPages_RecordScroll(pageIndex, g_mfdCraftListTopRowByMode[showFlightGroupsPage], rowCount,
									 -1);
#endif
		lastTeam = UINT16_MAX;
		if (needsRedraw != 0) {
			const uint16_t* craftRow;

#ifdef XVT_MODERN
			XvtCockpitPages_BeginSection(pageIndex, XVT_COCKPIT_PAGE_BODY);
#endif
			FlightText_SetClipRect(paneLeft, rowY, paneRight, paneBottom);
			g_flightFillClipRectFn();
			row = 0;
			if (rowCount != 0) {
				craftRow = craftRows;
				do {
					uint16_t packedRow;
					uint16_t team;
					uint16_t listedObjectIdx;
					const char* statusColor;

					FlightText_SetColor(COLOR_NORMAL_TEXT);
					if (row < (uint16_t)g_mfdCraftListTopRowByMode[showFlightGroupsPage] ||
						row > (uint16_t)lastVisibleRow) {
						continue;
					}
					packedRow = *craftRow;
					listedObjectIdx = (uint16_t)(packedRow & 0xFF);
					team = (uint16_t)(packedRow >> 8);
					statusColor = &g_mfdCraftListStatusLetters[team];
					FlightText_SetColor((uint8_t)*statusColor);
					if (lastTeam != team &&
						row == (uint16_t)g_mfdCraftListTopRowByMode[showFlightGroupsPage]) {
						lastTeam = team;
						if (g_players[g_localPlayer].mapCameraState != 0) {
							FlightSw_SetRenderTarget(NULL, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, 0);
							FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
							FlightText_SetBackgroundColor(COLOR_MAP_BACKGROUND);
							FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight,
													 g_screenWidth * g_flight16bppBytesPerPixel);
						}
						FlightText_SetClipRect(paneLeft, rowY, paneRight, paneBottom);
					}
					Mfd_BuildScratchCraftListName(listedObjectIdx);
					FlightText_SetCursor(paneLeft, rowY);
					g_flightTextScratchBuffer[9] = 0;
					FlightText_DrawString(g_flightTextScratchBuffer);
					{
						int16_t healthColumnX;
						int16_t statusColumnBaseX;
						ObjectRecord* object;
						MobileObject* mobileObject;

						healthColumnX = (int16_t)(paneLeft + CRAFT_NAME_COLUMN_WIDTH * halfCharacterWidth);
						statusColumnBaseX = healthColumnX;
						object = &g_objectTable[listedObjectIdx];
						mobileObject = object->mobj;
						if (mobileObject != NULL) {
							CraftData* craft;
							int hullPercent;

							craft = mobileObject->pCraft;
							if (craft != NULL && craft->objectKind != CRAFT_OBJECT_KIND_BREAKING_UP &&
								craft->objectKind != CRAFT_OBJECT_KIND_EXPLODING) {
								int shieldAverage;
								unsigned int maxShield;
								int shieldPercent;
								int percentage;

								shieldAverage = (craft->shieldEnergy[0] + craft->shieldEnergy[1]) / 2;
								maxShield = (unsigned int)Craft_GetObjectMaxShield(listedObjectIdx);
								if (shieldAverage != 0 && maxShield != 0) {
									percentage =
										(uint16_t)MATH2_percentage((unsigned int)shieldAverage, maxShield);
									shieldPercent = 2 * (percentage / PERCENTAGE_SCALE);
								} else {
									shieldPercent = 0;
								}
								if (shieldPercent <= HEALTH_CRITICAL_PERCENT) {
									FlightText_SetColor(COLOR_DAMAGED);
								} else if (shieldPercent <= HEALTH_GOOD_PERCENT) {
									FlightText_SetColor(COLOR_MEDIUM);
								} else {
									FlightText_SetColor(COLOR_HEALTHY);
								}
								FlightText_SetCursor(healthColumnX + halfCharacterWidth, rowY);
								FlightText_DrawDecimalNumber(shieldPercent, HEALTH_DIGIT_WIDTH, 1);
								FlightText_SetColor((uint8_t)*statusColor);
								g_flightDrawCharFn('/');
							}
							hullPercent = 0;
							if (craft->objectKind != CRAFT_OBJECT_KIND_BREAKING_UP &&
								craft->objectKind != CRAFT_OBJECT_KIND_EXPLODING) {
								if (craft->hullDamage > craft->hullMax) {
									hullPercent = 1;
								} else {
									hullPercent = (uint16_t)MATH2_percentage(
													  craft->hullMax - craft->hullDamage, craft->hullMax);
								hullPercent = (uint16_t)hullPercent / PERCENTAGE_SCALE;
									if (hullPercent == 0) {
										hullPercent = 100;
									}
								}
							}
							if (hullPercent != 0) {
								int digitCount;

								digitCount = (uint16_t)FlightText_FormatScratchInt(hullPercent);
								FlightText_SetCursor(g_flightCursorX + (HEALTH_DIGIT_WIDTH - digitCount) *
																		   halfCharacterWidth,
													 rowY);
								if (hullPercent <= HEALTH_CRITICAL_PERCENT) {
									FlightText_SetColor(COLOR_DAMAGED);
								} else if (hullPercent <= HEALTH_GOOD_PERCENT) {
									FlightText_SetColor(COLOR_MEDIUM);
								} else {
									FlightText_SetColor(COLOR_HEALTHY);
								}
								FlightText_DrawString(g_flightTextScratchBuffer);
							}
							FlightText_SetColor((uint8_t)*statusColor);
							{
								int16_t targetColumnX;

								targetColumnX = (int16_t)(g_flightCursorX + 2 * halfCharacterWidth);
								statusColumnBaseX = targetColumnX;
								if (g_players[g_localPlayer].mapCameraState == 0 ||
									showFlightGroupsPage != 0 ||
									g_flightResolutionMode != FLIGHT_RESOLUTION_320X240) {
									FlightText_SetCursor(targetColumnX, rowY);
									if (g_objectTable[listedObjectIdx].playerOwnerIdx != -1) {
										uint16_t targetObjectIdx;

										targetObjectIdx =
											g_players[g_objectTable[listedObjectIdx].playerOwnerIdx]
												.currentTargetObjectIdx;
										if (targetObjectIdx != UINT16_MAX &&
											targetObjectIdx < g_activeRegionCraftObjectSlotEnd &&
											g_objectTable[targetObjectIdx].mobj != NULL) {
											Hud_AppendObjectDisplayName((uint16_t)targetObjectIdx,
																		DISPLAY_NAME_FLAGS);
										} else {
											FlightText_SetScratch(g_strMeshComponentNames[32]);
										}
									} else {
										uint16_t targetObjectIdx;

										targetObjectIdx = (uint16_t)craft->aiController.targetObjIdx;
										if (targetObjectIdx != TARGET_NONE_AI &&
											targetObjectIdx != UINT16_MAX && targetObjectIdx < 0x8000) {
											Hud_AppendObjectDisplayName((uint16_t)targetObjectIdx,
																		DISPLAY_NAME_FLAGS);
										} else {
											FlightText_SetScratch(g_strMeshComponentNames[32]);
										}
									}
									if (showFlightGroupsPage != 0 ||
										g_flightResolutionMode == FLIGHT_RESOLUTION_320X240 ||
										g_players[g_localPlayer].mapCameraState != 0) {
										FlightText_DrawString(g_flightTextScratchBuffer);
									} else {
										FlightText_DrawStringRightAligned(g_flightTextScratchBuffer);
									}
								}
							}
						}
						if (showFlightGroupsPage != 0 &&
							g_flightResolutionMode != FLIGHT_RESOLUTION_320X240) {
							int16_t statusColumnX;
							unsigned int statusStringId;

							statusColumnX = halfCharacterWidth;
							statusColumnX =
								(int16_t)(statusColumnX * STATUS_COLUMN_OFFSET + statusColumnBaseX);
							FlightText_SetCursor(statusColumnX, rowY);
							statusStringId = (uint16_t)Mfd_GetFlightGroupGoalStatusStringId(listedObjectIdx);
							if (statusStringId != FG_GOAL_STATUS_STR_NONE) {
								FlightText_DrawString(g_strCockpitOverlayText[statusStringId]);
							} else {
								FlightText_DrawString(g_strMeshComponentNames[32]);
							}
						}
					}
					rowY = (uint16_t)(rowY + lineStep);
				} while (++craftRow, ++row < rowTotal);
			}
		}
#ifdef XVT_MODERN
		XvtCockpitPages_EndSection();
#endif
	} while (0);
	FlightSw_SetRenderTarget(NULL, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, 0);
}

// FUNCTION: XVT 0x44E0A0
void Mfd_BuildScratchCraftListName(uint16_t objectIdx) {
	ObjectRecord* object;
	MobileObject* mobileObject;
	CraftData* craft;
	int flightGroupIdx;
	uint16_t craftNumber;
	uint16_t tensDigit;
	uint16_t onesDigit;

	g_flightTextScratchBuffer[0] = '\0';
	object = &g_objectTable[objectIdx];
	mobileObject = object->mobj;
	if (mobileObject != 0 && mobileObject->state == 0) {
		craft = mobileObject->pCraft;
		flightGroupIdx = object->flightGroupIdx;
		FlightText_AppendScratchString(g_missionFlightGroups[flightGroupIdx].fg.name);
		craftNumber = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(flightGroupIdx, craft);
		if (craftNumber != 0) {
			FlightText_AppendScratchChar(' ');
			if (craftNumber >= 10) {
				tensDigit = craftNumber / 10;
				onesDigit = craftNumber % 10;
				FlightText_AppendScratchChar((char)(tensDigit + '0'));
				FlightText_AppendScratchChar((char)(onesDigit + '0'));
			} else {
				FlightText_AppendScratchChar((char)(craftNumber + '0'));
			}
		}
	}
}

// FUNCTION: XVT 0x44E170
int16_t Mfd_GetFlightGroupGoalStatusStringId(uint16_t objectIndex) {
	unsigned int goalIndex;
	FlightGroupGoal* goal;
	int16_t* playerIff;
	unsigned int globalTriggerIndex;
	MissionTriggerPair* triggerPair;
	MissionTrigger* trigger;
	int eventCondition;
	unsigned int flightGroupIdx;
	int inspectActive;
	int disableActive;
	int captureActive;
	int boardActive;
	CraftData* craft;
	int destroyActive;
	int specialCargoOnly;
	int attackActive;

	if (g_projectileObjectSlotStart <= objectIndex && g_projectileObjectSlotEnd > objectIndex) {
		return FG_GOAL_STATUS_STR_NONE;
	}
	if (g_activeRegionCraftObjectSlotEnd > objectIndex) {
		craft = g_objectTable[objectIndex].mobj->pCraft;
	} else {
		craft = NULL;
	}

	inspectActive = 0;
	destroyActive = 0;
	disableActive = 0;
	attackActive = 0;
	captureActive = 0;
	boardActive = 0;
	flightGroupIdx = g_objectTable[objectIndex].flightGroupIdx;
	specialCargoOnly = 0;
	playerIff = &g_players[g_localPlayer].playerIff;

	for (goalIndex = 0; goalIndex < 8; ++goalIndex) {
		goal = &g_missionFlightGroups[flightGroupIdx].fg.goals[goalIndex];
		if (goal->enabledTeams[(uint16_t)*playerIff] != 0 && goal->type == 0 &&
			g_missionFgStats[flightGroupIdx].goalState[8 * (uint16_t)*playerIff + goalIndex] == 4) {
			if (goal->amount == GOAL_AMT_ALL_SPECIAL_CARGO) {
				if (g_missionFgStats[flightGroupIdx].specialCargoOutcome[FLIGHT_GROUP_OUTCOME_INSPECTED] ==
					0) {
					inspectActive = 1;
				}
				specialCargoOnly = 1;
			}
			eventCondition = goal->eventCondition;
			if (eventCondition == 2) {
				destroyActive = 1;
			} else if (eventCondition == 3) {
				attackActive = 1;
			} else if (eventCondition == 8) {
				disableActive = 1;
			} else if (eventCondition == 5) {
				inspectActive = 1;
			} else if (eventCondition == 4) {
				captureActive = 1;
			} else if (eventCondition == 6) {
				boardActive = 1;
			}
		}
	}

	for (globalTriggerIndex = 0; globalTriggerIndex < 4; ++globalTriggerIndex) {
		if (globalTriggerIndex < 2) {
			triggerPair = &g_missionGlobalGoals[(uint16_t)*playerIff][0].triggerPairs[0];
		} else {
			triggerPair = &g_missionGlobalGoals[(uint16_t)*playerIff][0].triggerPairs[1];
		}
		if ((globalTriggerIndex & 1) == 0) {
			trigger = &triggerPair->triggers[0];
		} else {
			trigger = &triggerPair->triggers[1];
		}
		eventCondition = trigger->condition;
		if (eventCondition != 10 && Mission_FlightGroupMatchesTriggerVariable(
										flightGroupIdx, trigger->variableType, trigger->variable)) {
			if (eventCondition == 2) {
				destroyActive = 1;
			} else if (eventCondition == 3) {
				attackActive = 1;
			} else if (eventCondition == 8) {
				disableActive = 1;
			} else if (eventCondition == 5) {
				inspectActive = 1;
			} else if (eventCondition == 4) {
				captureActive = 1;
			} else if (eventCondition == 6) {
				boardActive = 1;
			}
		}
	}

	if (g_activeRegionCraftObjectSlotEnd > objectIndex) {
		if (inspectActive != 0 && craft->iffVisibility[(uint16_t)*playerIff] != 0) {
			inspectActive = 0;
		}
		if (captureActive != 0 || boardActive != 0) {
			if (g_objectTable[objectIndex].mobj->speed != 0) {
				disableActive = 1;
			}
			if (specialCargoOnly != 0 &&
				g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft != craft->waveNumber) {
				captureActive = 0;
				boardActive = 0;
			}
		}
		if (disableActive != 0 && specialCargoOnly != 0 &&
			g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft != craft->waveNumber) {
			disableActive = 0;
		}
		if (destroyActive != 0 && specialCargoOnly != 0 &&
			g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft != craft->waveNumber) {
			destroyActive = 0;
		}
	}

	if (inspectActive != 0) {
		return FG_GOAL_STATUS_STR_INSPECT;
	}
	if (disableActive != 0) {
		if (captureActive != 0) {
			return FG_GOAL_STATUS_STR_CAPTURE;
		}
		return boardActive == 0 ? FG_GOAL_STATUS_STR_DISABLE : FG_GOAL_STATUS_STR_BOARD;
	}
	if (captureActive != 0) {
		return FG_GOAL_STATUS_STR_CAPTURE;
	}
	if (boardActive != 0) {
		return FG_GOAL_STATUS_STR_BOARD;
	}
	if (destroyActive != 0) {
		return FG_GOAL_STATUS_STR_DESTROY;
	}
	return attackActive == 0 ? FG_GOAL_STATUS_STR_NONE : FG_GOAL_STATUS_STR_ATTACK;
}

// FUNCTION: XVT 0x44E5A0
void Mfd_DrawCommandMenuPage(void) {
	enum {
		MFD_BACKGROUND_COLOR = 0x34,
		MFD_ACTIVE_BACKGROUND_COLOR = 0x46,
		MFD_TEXT_COLOR = 0x43,
		MFD_HIGHLIGHT_COLOR = 0x4A,
		MFD_CREDIT_COLOR = 0x4E,
		MFD_DEFAULT_WIDTH = 320,
		MFD_DEFAULT_HEIGHT = 200
	};

	int16_t left;
	int16_t borderTop;
	int16_t bottom;
	int16_t top;
	int16_t right;
	int16_t column;
	int16_t lineStep;
	int16_t textMode;
	int firstCredit;
	int lastCredit;
	int localPlayer;
	int row;
	int rowStart;
	uint16_t activePage;
	int pitchBytes;

	pitchBytes = g_flight16bppBytesPerPixel;
	pitchBytes *= g_screenWidth;
	FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight, pitchBytes);
	FlightText_SetFontTier(0);
	left = (int16_t)(g_mfdMapBlitSourceX + 2);
	right = (int16_t)(g_mfdMapBlitWidth + g_mfdMapBlitSourceX - 2);
	top = (int16_t)(g_mfdMapBlitSourceY + 2);
	bottom = (int16_t)(g_mfdMapBlitSourceY + g_mfdMapBlitHeight - 2);
#ifdef XVT_MODERN
	XvtCockpitPages_SetOrigin(MFD_PAGE_COMMAND, left - 2, top - 2);
#endif
	activePage = g_mfdActivePage;
	if (activePage == MFD_PAGE_NONE) {
		activePage = MFD_PAGE_COMMAND;
	}
	g_mfdActivePage = activePage;
	do {
		if (activePage == MFD_PAGE_COMMAND) {
			FlightText_SetBackgroundColor(MFD_ACTIVE_BACKGROUND_COLOR);
		} else {
			if (g_mfdSecondaryPage != MFD_PAGE_COMMAND) {
				break;
			}
			FlightText_SetBackgroundColor(MFD_BACKGROUND_COLOR);
		}
		borderTop = (int16_t)(top - 2);
		FlightText_SetClipRect(left - 2, borderTop, right + 2, bottom + 2);
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBorder(MFD_PAGE_COMMAND);
#endif
		g_flightFillRectClippedFn((uint16_t)(left - 2), (uint16_t)borderTop, (uint16_t)(right + 2),
								  (uint16_t)(bottom + 2), 1);
	} while (0);

	if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + HUD_MFD_MAP_OR_COMMAND_ELEMENT] !=
		g_mfdPageStates[MFD_PAGE_COMMAND]) {
		FlightText_SetBackgroundColor(MFD_BACKGROUND_COLOR);
		FlightText_SetClipRect(left - 2, top - 2, right + 2, bottom + 2);
#ifdef XVT_MODERN
		XvtCockpitPages_Clear(MFD_PAGE_COMMAND);
		XvtCockpitPages_RecordBackground(MFD_PAGE_COMMAND);
#endif
		g_flightFillClipRectFn();
		if (g_mfdPageStates[MFD_PAGE_COMMAND] == MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			XvtCockpitPages_Clear(MFD_PAGE_COMMAND);
#endif
			if (g_mfdActivePage == MFD_PAGE_COMMAND) {
				g_mfdActivePage = g_mfdSecondaryPage;
				g_mfdSecondaryPage = Mfd_FindSecondaryOpenPage();
			}
			FlightSw_SetRenderTarget(NULL, MFD_DEFAULT_WIDTH, MFD_DEFAULT_HEIGHT, 0);
			return;
		}
		FlightText_SetClearLineBackground(1);
		FlightText_SetColor(MFD_TEXT_COLOR);
		FlightText_SetFontTier(0);
		FlightText_SetClipRect(left, top, right, bottom);
		lineStep = (int16_t)(g_flightFontLineHeight + 2);
		FlightText_SetClipRect(left, top, right, bottom);
		localPlayer = g_localPlayer;
		g_mfdCommandCameraStateCache = g_players[localPlayer].mapCameraState;
		textMode = 1;
	} else {
		lineStep = (int16_t)(g_flightFontLineHeight + 2);
		FlightText_SetClearLineBackground(1);
		FlightText_SetColor(MFD_TEXT_COLOR);
		FlightText_SetFontTier(0);
		FlightText_SetClipRect(left, top, right, bottom);
		FlightText_SetBackgroundColor(MFD_BACKGROUND_COLOR);
		localPlayer = g_localPlayer;
		if (g_mfdCommandCameraStateCache != g_players[localPlayer].mapCameraState) {
			g_mfdCommandCameraStateCache = g_players[localPlayer].mapCameraState;
			textMode = 1;
		} else {
			textMode = 0;
		}
	}

	if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480 && g_players[localPlayer].mapCameraState != 0 &&
		g_mfdActivePage == MFD_PAGE_COMMAND) {
		switch (g_currentActionKey) {
			case 0xA8:
				textMode = 3;
				firstCredit = 21;
				lastCredit = 29;
				break;
			case 0xA9:
				textMode = 3;
				firstCredit = 0;
				lastCredit = 5;
				break;
			case 0xAA:
				textMode = 2;
				firstCredit = 0;
				lastCredit = 9;
				break;
			case 0xAB:
				textMode = 3;
				firstCredit = 5;
				lastCredit = 13;
				break;
			case 0xAC:
				textMode = 3;
				firstCredit = 29;
				lastCredit = 37;
				break;
			case 0xAD:
				textMode = 3;
				firstCredit = 13;
				lastCredit = 21;
				break;
			default:
				break;
		}
	}

#ifdef XVT_MODERN
	if (textMode != 0) {
		XvtCockpitPages_RecordMode(MFD_PAGE_COMMAND, textMode);
		XvtCockpitPages_BeginSection(MFD_PAGE_COMMAND, XVT_COCKPIT_PAGE_BODY);
	}
#endif
	if (textMode == 1) {
#ifdef XVT_MODERN
		XvtCockpitPages_RecordScroll(MFD_PAGE_COMMAND, 0, 9, -1);
#endif
		row = 0;
		FlightText_SetWordWrap(0);
		g_flightFillClipRectFn();
		while (row < 9) {
			rowStart = row;
			FlightText_SetCursor(left, (uint16_t)top);
			FlightText_SetScratch(g_strMapRoomText[rowStart]);
			if (rowStart > 0 && rowStart < 5) {
				FlightText_AppendScratchString(g_strMapRoomText[rowStart + 1]);
				FlightText_DrawString(g_strMapRoomText[rowStart]);
				FlightText_SetColor(MFD_HIGHLIGHT_COLOR);
				FlightText_DrawString(g_strMapRoomText[rowStart + 1]);
				FlightText_SetColor(MFD_TEXT_COLOR);
			} else {
				FlightText_DrawString(g_strMapRoomText[rowStart]);
			}
			if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
				int16_t width;
				width = (int16_t)(FlightText_MeasureStringWidth(g_flightTextScratchBuffer) + 1);
				if (width < 16) {
					width = 16;
				}
				column = (int16_t)(left + width);
			} else {
				column = (int16_t)(left + FlightText_MeasureStringWidth(g_strMapRoomText[8]) + 5);
			}
			FlightText_SetCursor(column, (uint16_t)top);
			if (rowStart > 0 && rowStart < 5) {
				FlightText_DrawString(g_strMapRoomText[rowStart + 9]);
				++row;
				FlightText_SetColor(MFD_HIGHLIGHT_COLOR);
				FlightText_DrawString(g_strMapRoomText[rowStart + 10]);
				FlightText_SetColor(MFD_TEXT_COLOR);
			} else {
				FlightText_DrawString(g_strMapRoomText[rowStart + 9]);
			}
			top = (int16_t)(top + lineStep);
			++row;
		}
	} else if (textMode == 2) {
		int creditIndex;

#ifdef XVT_MODERN
		XvtCockpitPages_RecordScroll(MFD_PAGE_COMMAND, firstCredit, lastCredit - firstCredit, -1);
#endif
		g_flightFillClipRectFn();
		FlightText_SetColor(MFD_CREDIT_COLOR);
		for (creditIndex = firstCredit; creditIndex < lastCredit; ++creditIndex) {
			FlightText_SetCursor(left, (uint16_t)top);
			FlightText_DrawString(g_mfdDeveloperCreditsLines[creditIndex]);
			top = (int16_t)(top + lineStep);
		}
	} else if (textMode == 3) {
		int creditIndex;

#ifdef XVT_MODERN
		XvtCockpitPages_RecordScroll(MFD_PAGE_COMMAND, firstCredit, 1 + lastCredit - firstCredit, -1);
#endif
		g_flightFillClipRectFn();
		FlightText_SetColor(MFD_CREDIT_COLOR);
		for (creditIndex = 0; creditIndex < 1; ++creditIndex) {
			FlightText_SetCursor(left, (uint16_t)top);
			FlightText_DrawString(g_mfdDeveloperCreditsLines[creditIndex]);
			top = (int16_t)(top + lineStep);
		}
		for (creditIndex = firstCredit; creditIndex < lastCredit; ++creditIndex) {
			FlightText_SetCursor(left, (uint16_t)top);
			FlightText_DrawString(g_mfdDeveloperCreditsLines[creditIndex + 10]);
			top = (int16_t)(top + lineStep);
		}
	}

#ifdef XVT_MODERN
	XvtCockpitPages_EndSection();
#endif
	FlightSw_SetRenderTarget(NULL, MFD_DEFAULT_WIDTH, MFD_DEFAULT_HEIGHT, 0);
}

// FUNCTION: XVT 0x4803F0
void Mfd_TogglePage(uint16_t page) {
	int16_t* pageState;
	int16_t previousSecondaryPage;
	uint16_t otherPage;

	if (g_players[g_localPlayer].mapCameraState == 0) {
		pageState = &g_mfdPageStates[page];
		if (*pageState != MFD_PAGE_STATE_CLOSED) {
			*pageState = MFD_PAGE_STATE_CLOSING;
		} else {
			int16_t previousActivePage;

			previousActivePage = g_mfdActivePage;
			*pageState = MFD_PAGE_STATE_OPEN;
			previousSecondaryPage = g_mfdSecondaryPage;
			g_mfdSecondaryPage = previousActivePage;
			g_mfdActivePage = page;
		}
		if (g_mfdPageStates[MFD_PAGE_FRIENDLY_CRAFT] != MFD_PAGE_STATE_CLOSED &&
			g_mfdPageStates[MFD_PAGE_FLIGHT_GROUPS] != MFD_PAGE_STATE_CLOSED) {
			if (page == MFD_PAGE_FRIENDLY_CRAFT) {
				g_mfdPageStates[MFD_PAGE_FLIGHT_GROUPS] = MFD_PAGE_STATE_CLOSING;
			} else {
				g_mfdPageStates[MFD_PAGE_FRIENDLY_CRAFT] = MFD_PAGE_STATE_CLOSING;
			}
			if (previousSecondaryPage != MFD_PAGE_FRIENDLY_CRAFT &&
				previousSecondaryPage != MFD_PAGE_FLIGHT_GROUPS) {
				g_mfdSecondaryPage = previousSecondaryPage;
			}
		}
		return;
	}

	if (page != MFD_PAGE_MESSAGE_LOG) {
		for (otherPage = MFD_PAGE_SCOREBOARD; otherPage < MFD_PAGE_COUNT; ++otherPage) {
			if (page != otherPage && otherPage != MFD_PAGE_FRIENDLY_CRAFT &&
				otherPage != MFD_PAGE_MESSAGE_LOG && g_mfdPageStates[otherPage] != MFD_PAGE_STATE_CLOSED) {
				g_mfdPageStates[otherPage] = MFD_PAGE_STATE_CLOSING;
			}
		}
	}
	pageState = &g_mfdPageStates[page];
	if (*pageState != MFD_PAGE_STATE_CLOSED) {
		*pageState = MFD_PAGE_STATE_CLOSING;
		return;
	}
	*pageState = MFD_PAGE_STATE_OPEN;
	g_mfdSecondaryPage = g_mfdActivePage;
	g_mfdActivePage = page;
}

// FUNCTION: XVT 0x480530
int16_t Mfd_FindSecondaryOpenPage(void) {
	uint16_t activePage;
	uint16_t page;

	activePage = g_mfdActivePage;
	for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT; page++) {
		if (g_mfdPageStates[page] != MFD_PAGE_STATE_CLOSED && activePage != page) {
			return (int16_t)page;
		}
	}
	return -1;
}

// FUNCTION: XVT 0x49EC40
int16_t Mfd_DrawMessageLogPage(void) {
	int16_t left;
	int16_t top;
	int16_t right;
	int16_t bottom;
	int16_t lineHeight;
	int16_t cursorY;
	int16_t displayOffset;
	int16_t visibleLines;
	int16_t maxDisplayOffset;
	int16_t logRecordCount;
	unsigned int pitch;
	int recordIndex;
	uint8_t prefixCode;
	uint8_t ch;
	char* text;

#ifdef XVT_MODERN
	ch = 0;
	cursorY = 0;
#endif

	left = (int16_t)(g_readyMessagePaneLeft + 2);
	top = (int16_t)(g_readyMessagePaneTop + 2);
	right = (int16_t)(g_readyMessagePaneRight - 2);
	bottom = (int16_t)(g_readyMessagePaneBottom - 2);
	pitch = g_flight16bppBytesPerPixel * g_screenWidth;
	FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight, (int)pitch);
	if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + HUD_MFD_MESSAGE_LOG_ELEMENT] !=
		g_mfdPageStates[MFD_PAGE_MESSAGE_LOG]) {
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		FlightText_SetClipRect(g_readyMessagePaneLeft, g_readyMessagePaneTop, g_readyMessagePaneRight,
							   g_readyMessagePaneBottom);
#ifdef XVT_MODERN
		XvtCockpitMessages_Clear(XVT_COCKPIT_MESSAGE_READY);
#endif
		g_flightFillClipRectFn();
		if (g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] == MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			XvtCockpitPages_Clear(MFD_PAGE_MESSAGE_LOG);
#endif
			if (g_mfdActivePage == MFD_PAGE_MESSAGE_LOG) {
				g_mfdActivePage = g_mfdSecondaryPage;
				g_mfdSecondaryPage = Mfd_FindSecondaryOpenPage();
			}
			FlightSw_SetRenderTarget(NULL, 320, 200, 0);
			return cursorY;
		}
	}

	FlightText_SetClipRect(left, top, right, bottom);
	FlightText_SetWordWrap(1);
	FlightText_SetClearLineBackground(1);
	FlightText_SetFontTier(0);
#ifdef XVT_MODERN
	XvtCockpitPages_SetOrigin(MFD_PAGE_MESSAGE_LOG,
							  g_readyMessagePaneLeft -
								  (g_flightPlayerCount >= 1 ? 2 * g_flightFontHalfHeight : 0),
							  g_readyMessagePaneTop);
#endif
	FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
	g_flightTextShadowEnabled = 1;
	lineHeight = (int16_t)(g_flightFontLineHeight + 2);
	FlightText_SetColor(0x43);
	logRecordCount = (int16_t)g_messageLogWriteIndex;
	g_messageLogRecords = (HudInFlightMessageRecord*)Memory_LockHandle(g_messageLogHandle);
	Memory_UnlockHandle(g_messageLogHandle);
	cursorY = top;

	if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + HUD_MFD_MESSAGE_LOG_ELEMENT] !=
		g_mfdPageStates[MFD_PAGE_MESSAGE_LOG]) {
		g_mfdMessageLogScrollOffset = 0;
		g_mfdMessageLogLastDrawTotalCount = 0;
		g_mfdMessageLogRedraw = 1;
		g_flightFillClipRectFn();
	} else {
		if (g_mfdActivePage == MFD_PAGE_MESSAGE_LOG) {
			switch (g_currentActionKey) {
				case 0xA6:
					if (g_mfdMessageLogScrollOffset > 0) {
						--g_mfdMessageLogScrollOffset;
						g_mfdMessageLogRedraw = 1;
					}
					break;
				case 0xA7:
					maxDisplayOffset = g_messageLogWrapped != 0 ? 300 : logRecordCount;
					if ((top - bottom) / lineHeight + maxDisplayOffset >= g_mfdMessageLogScrollOffset) {
						++g_mfdMessageLogScrollOffset;
						g_mfdMessageLogRedraw = 1;
					}
					break;
				case 0xAC:
					if (g_mfdMessageLogScrollOffset > 4) {
						g_mfdMessageLogScrollOffset -= 4;
						g_mfdMessageLogRedraw = 1;
					}
					break;
				case 0xAD:
					maxDisplayOffset = g_messageLogWrapped != 0 ? 300 : logRecordCount;
					if (maxDisplayOffset + (top - bottom) / lineHeight - 4 >= g_mfdMessageLogScrollOffset) {
						g_mfdMessageLogScrollOffset += 4;
						g_mfdMessageLogRedraw = 1;
					}
					break;
				default:
					break;
			}
		}
		if (g_messageLogTotalCount != g_mfdMessageLogLastDrawTotalCount) {
			g_mfdMessageLogRedraw = 1;
		}
	}

	visibleLines = (int16_t)((bottom - top) / lineHeight + g_mfdMessageLogScrollOffset);
	if (g_mfdMessageLogRedraw != 0) {
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBackground(MFD_PAGE_MESSAGE_LOG);
		XvtCockpitPages_BeginSection(MFD_PAGE_MESSAGE_LOG, XVT_COCKPIT_PAGE_BODY);
#endif
		g_flightFillClipRectFn();
		for (displayOffset = 0; displayOffset <= visibleLines; ++displayOffset) {
			if (displayOffset >= g_mfdMessageLogScrollOffset && displayOffset < 300) {
				FlightText_SetCursor(left, cursorY);
				recordIndex = Mfd_GetMessageLogRecordIndex(displayOffset);
				if (recordIndex >= 0) {
					text = g_messageLogRecords[recordIndex].text;
					prefixCode = (uint8_t)text[0];
					if (prefixCode < 9) {
						FlightText_SetColor(g_messageTextPrefixColorCodes[prefixCode]);
						++text;
						if (prefixCode == 1) {
							if ((uint8_t)text[0] >= '0' && (uint8_t)text[0] <= '3') {
								FlightText_SetColor(g_messageTextPrefixColorCodes[(uint8_t)text[0] - '(']);
								++text;
							}
						} else if (prefixCode == 2) {
							FlightText_SetColor(
								g_messageSenderIffColorCodes[g_messageLogRecords[recordIndex].senderIff]);
						}
					} else {
						FlightText_SetColor(0x42);
					}
					while (*text != '\0') {
						if (*text == '[') {
							if (g_flightTextColorIndex == 0xD4)
								--g_flightTextColorIndex;
							else
								++g_flightTextColorIndex;
						} else if (*text == ']') {
							if (g_flightTextColorIndex == 0xD3)
								++g_flightTextColorIndex;
							else
								--g_flightTextColorIndex;
						} else {
							g_flightDrawCharFn((uint8_t)*text);
						}
						ch = (uint8_t)*text;
						++text;
					}
					if (ch != '?' && ch != '!' && ch != ':' && ch != ' ') {
						g_flightDrawCharFn('.');
					}
					FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
					g_flightDrawCharFn('\n');
					FlightText_SetColor(0x42);
					FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
					if (g_messageLogRecords[recordIndex].clockHour != 0) {
						FlightText_SetCursor(right - FlightText_MeasureStringWidth("00:00:00 "), cursorY);
						FlightText_DrawDecimalNumber(g_messageLogRecords[recordIndex].clockHour, 2, 1);
						g_flightDrawCharFn(':');
						FlightText_DrawDecimalNumber(g_messageLogRecords[recordIndex].clockMinute, 2, 2);
					} else {
						FlightText_SetCursor(right - FlightText_MeasureStringWidth("00:00 "), cursorY);
						FlightText_DrawDecimalNumber(g_messageLogRecords[recordIndex].clockMinute, 2, 1);
					}
					g_flightDrawCharFn(':');
					FlightText_DrawDecimalNumber(g_messageLogRecords[recordIndex].clockTick, 2, 2);
					g_flightDrawCharFn(' ');
					cursorY = (int16_t)(cursorY + lineHeight);
				}
			}
		}
	}

#ifdef XVT_MODERN
	XvtCockpitPages_EndSection();
	XvtCockpitPages_RecordScroll(MFD_PAGE_MESSAGE_LOG, g_mfdMessageLogScrollOffset,
								 g_messageLogWrapped ? 300 : logRecordCount, -1);
#endif
	FlightText_SetWordWrap(0);
	g_mfdMessageLogRedraw = 0;
	if (g_players[g_localPlayer].mapCameraState == 0 && g_mfdActivePage == MFD_PAGE_NONE) {
		g_mfdActivePage = MFD_PAGE_MESSAGE_LOG;
	}
	if (g_mfdActivePage == MFD_PAGE_MESSAGE_LOG) {
		FlightText_SetBackgroundColor(0x46);
		FlightText_SetClipRect(left - 2, top - 2, right + 2, bottom + 2);
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBorder(MFD_PAGE_MESSAGE_LOG);
#endif
		g_flightFillRectClippedFn(left - 2, top - 2, right + 2, bottom + 2, 1);
	} else if (g_mfdSecondaryPage == MFD_PAGE_MESSAGE_LOG) {
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		FlightText_SetClipRect(left - 2, top - 2, right + 2, bottom + 2);
#ifdef XVT_MODERN
		XvtCockpitPages_RecordBorder(MFD_PAGE_MESSAGE_LOG);
#endif
		g_flightFillRectClippedFn(left - 2, top - 2, right + 2, bottom + 2, 1);
	}
	FlightSw_SetRenderTarget(NULL, 320, 200, 0);
	g_mfdMessageLogLastDrawTotalCount = g_messageLogTotalCount;
	return visibleLines;
}

// FUNCTION: XVT 0x49F330
int Mfd_GetMessageLogRecordIndex(int displayOffset) {
	int recordIndex;

	if (g_messageLogTotalCount <= 300) {
		return g_messageLogTotalCount - displayOffset - 1;
	}
	recordIndex = g_messageLogWriteIndex - displayOffset - 1;
	if (recordIndex < 0) {
		recordIndex += 300;
	}
	return recordIndex;
}

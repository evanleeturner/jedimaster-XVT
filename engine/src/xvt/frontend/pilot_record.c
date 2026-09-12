#include "xvt/frontend/pilot_record.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/frontend_movies.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/movie.h"
#include "xvt/input/keyboard.h"

#include <stdlib.h>
#include <string.h>
#ifdef XVT_MODERN
#include <strings.h>
#endif

enum {
	CAMPAIGN_AWARD_FLAG_COUNT = 16,
	CAMPAIGN_AWARD_VISIBLE_FLAG_COUNT = 15,
	CUTSCENE_VIEWER_ROW_HEIGHT = 15,
	CUTSCENE_VIEWER_VISIBLE_ROW_COUNT = 21,
	PILOT_LIST_VISIBLE_COUNT = 13,
	PILOT_LIST_PAGE_STEP = 5,
	PILOT_LIST_SCROLLBAR_CONTROL_ID = 4,
	PILOT_NAME_COMPARE_LENGTH = 12,
	PILOT_NAME_MAX_CHARS = 13,
	PILOT_UI_SOUND_PRIORITY = 255,
	PILOT_UI_SOUND_VOLUME_SCALE = 12,
	PILOT_UI_SOUND_CENTER_PAN = 63,
	PILOT_DELETE_VIRTUAL_KEY = 0x2E,
	MISSION_ACHIEVEMENT_ROW_HEIGHT = 15,
	MISSION_ACHIEVEMENT_VISIBLE_ROWS = 21,
	MISSION_ACHIEVEMENT_TEXT_X = 125,
	MISSION_ACHIEVEMENT_TEXT_RIGHT = 303,
	MISSION_ACHIEVEMENT_SCORE_X = 323,
	MISSION_ACHIEVEMENT_DETAIL_X = 373,
	MISSION_ACHIEVEMENT_HEADER_X = 88,
	MISSION_ACHIEVEMENT_FIRST_Y = 111,
	MISSION_ACHIEVEMENT_FONT_SIZE = 12,
	MISSION_ACHIEVEMENT_TITLE_FONT_SIZE = 15,
	MISSION_ACHIEVEMENT_SCROLL_PAGE_STEP = 5,
	MISSION_ACHIEVEMENT_SCROLL_CONTROL_ID = 3,
	MISSION_ACHIEVEMENT_AWARD_RIGHT = 125,
	MISSION_ACHIEVEMENT_AWARD_HEIGHT = 14,
	MISSION_ACHIEVEMENT_TEXT_COLOR = 0xFFFF,
	MISSION_ACHIEVEMENT_TITLE_LEFT = 84,
	MISSION_ACHIEVEMENT_TITLE_TOP = 90,
	MISSION_ACHIEVEMENT_TITLE_RIGHT = 404,
	MISSION_ACHIEVEMENT_TITLE_BOTTOM = 106,
	MISSION_ACHIEVEMENT_SCROLL_LEFT = 425,
	MISSION_ACHIEVEMENT_SCROLL_TOP = 107,
	MISSION_ACHIEVEMENT_SCROLL_RIGHT = 434,
	MISSION_ACHIEVEMENT_SCROLL_BOTTOM = 433,
	MISSION_ACHIEVEMENT_RATING_TEXT_CODE = 6,
	MISSION_ACHIEVEMENT_NAME_TEXT_CODE = 4,
	MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG = 4,
	MISSION_ACHIEVEMENT_PLACEMENT_STRING_OFFSET = 317,
	MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET = 381,
	MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET = 659,
};

// GLOBAL: XVT 0xB6A2B0
int g_pilotRecordPage = 0;
// GLOBAL: XVT 0xB6A2E0
PilotData g_pilotData;
// GLOBAL: XVT 0xB69CC8
unsigned int g_campaignAwardSpriteCount = 0;
// GLOBAL: XVT 0xB69CD8
CampaignAwardSpriteEntry* g_campaignAwardSprites = NULL;
// GLOBAL: XVT 0x664EC8
int g_campaignSingleplayerAwardCount = 0;
// GLOBAL: XVT 0x664ED0
int g_campaignMedalScrollOffset = 0;
// GLOBAL: XVT 0x664EE8
int g_campaignSingleplayerAwardFlags[CAMPAIGN_AWARD_FLAG_COUNT] = { 0 };
// GLOBAL: XVT 0x664F28
int g_campaignMultiplayerAwardCount = 0;
// GLOBAL: XVT 0x664F60
int g_campaignMultiplayerAwardFlags[CAMPAIGN_AWARD_FLAG_COUNT] = { 0 };
// GLOBAL: XVT 0x664FBC
int g_campaignMedalEntryCount = 0;
// GLOBAL: XVT 0x664EAC
int g_cutsceneViewerScrollRow = 0;
// GLOBAL: XVT 0x664EA0
int g_pilotSpTournamentHistoryCount = -1;
// GLOBAL: XVT 0x664EA4
int g_pilotMpCombatHistoryCount = -1;
// GLOBAL: XVT 0x664EA8
int g_pilotMpTournamentHistoryCount = -1;
// GLOBAL: XVT 0x664EB0
int g_pilotMpTrainingHistoryCount = -1;
// GLOBAL: XVT 0x664ECC
int g_pilotMpMeleeHistoryCount = -1;
// GLOBAL: XVT 0x664FB8
int g_cutsceneViewerTotalRows = 0;
// GLOBAL: XVT 0x664FA8
char g_pilotRecordNameInput[14] = { 0 };
// GLOBAL: XVT 0x52B0E0
int g_pilotListScrollOffset = 0;
// GLOBAL: XVT 0x664EB8
int g_pilotStatsAssists[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x664ED8
int g_pilotStatsPlayerKills[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x664F30
int g_pilotStatsLossesToNonPlayers[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x664F3C
int g_pilotAchievementsScrollOffset = -1;
// GLOBAL: XVT 0x664F44
int g_pilotSpTrainingHistoryCount = -1;
// GLOBAL: XVT 0x664F48
int g_pilotSpMeleeHistoryCount = -1;
// GLOBAL: XVT 0x664F4C
int g_pilotSpCombatHistoryCount = -1;
// GLOBAL: XVT 0x664F50
int g_pilotMpBattleHistoryCount = -1;
// GLOBAL: XVT 0x664F40
int g_pilotStatsHasCraftKillsByType = -1;
// GLOBAL: XVT 0x664F54
int g_pilotStatsRowHasData = -1;
// GLOBAL: XVT 0x664F58
int g_pilotStatsHasLossesToPlayersByRank = -1;
// GLOBAL: XVT 0x664F5C
int g_pilotStatisticsScrollOffset = -1;
// GLOBAL: XVT 0x664FC0
int g_pilotStatsNonPlayerKillsShared[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x664FD0
int g_pilotStatsTotalKillsShared[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x664FE0
int g_pilotStatsNonPlayerKills[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x664FEC
int g_pilotStatsHasPlayerKillsByRating = -1;
// GLOBAL: XVT 0x664FF0
int g_pilotStatsPlayerKillsShared[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x665000
int g_pilotRecordPageRowCount = -1;
// GLOBAL: XVT 0x664FA0
int g_pilotSpCampaignHistoryRowCount = -1;
// GLOBAL: XVT 0x664FFC
int g_pilotSpBattleHistoryCount = -1;
// GLOBAL: XVT 0x665008
int g_pilotStatsLossesToPlayers[3] = { -1, -1, -1 };
// GLOBAL: XVT 0x665014
int g_pilotMpCampaignHistoryRowCount = -1;
// GLOBAL: XVT 0x52B018
POINT g_pilotRatingIconPos[25] = { { 364, 397 }, { 364, 381 }, { 364, 365 }, { 364, 349 }, { 197, 397 },
								   { 197, 381 }, { 197, 365 }, { 197, 349 }, { 364, 322 }, { 364, 306 },
								   { 364, 290 }, { 364, 274 }, { 197, 322 }, { 197, 306 }, { 197, 290 },
								   { 197, 274 }, { 364, 247 }, { 364, 231 }, { 364, 215 }, { 364, 199 },
								   { 197, 247 }, { 197, 231 }, { 197, 215 }, { 197, 199 }, { 187, 123 } };

// FUNCTION: XVT 0x4BEA50
int PilotRecord_UpdatePilotSelectionPanel(int frameCounter) {
	RECT rect;
	FrontendFileListNode* node;
	int selectedIndex;
	int pilotIndex;
	int accepted;

#ifdef XVT_MODERN
	if (XvtFrontendAction_Pending(XVT_ACTION_PILOT) == 2) {
		if (!XvtDialog_TakeResult(&accepted))
			return 1;
		XvtFrontendAction_Finish(XVT_ACTION_PILOT);
		if (accepted && !Pilot_DeleteCurrent())
			XvtStorage_Fatal("Cannot delete the selected pilot", 1);
		g_pilotListScrollOffset = 0;
		PilotRecord_RebuildPilotList(&selectedIndex);
		PilotRecord_RedrawBackground();
		g_concourseRedrawRequested = 1;
		return 1;
	}
	if (XvtFrontendAction_Pending(XVT_ACTION_PILOT) == 1) {
		selectedIndex = FrontendDialog_PromptForPilotName(g_pilotRecordNameInput);
		if (selectedIndex == XVT_DIALOG_PENDING)
			return 1;
		XvtFrontendAction_Finish(XVT_ACTION_PILOT);
	} else {
#endif
		FrontendDraw_RectAssign(&rect, 451, 90, 605, 106);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_463_PILOT_ROSTER), &rect, 0xFFFF);
		if (frameCounter == 0) {
			Keyboard_FlushCharBuffer();
			memset(g_pilotRecordNameInput, 0, sizeof(g_pilotRecordNameInput));
			if (g_pilotFileList != NULL && g_pilotFileList->count > PILOT_LIST_VISIBLE_COUNT) {
				PilotRecord_RebuildPilotList(&g_pilotListScrollOffset);
			} else {
				PilotRecord_RebuildPilotList(&selectedIndex);
			}
		}

		if (g_pilotFileList != NULL && g_pilotFileList->count > PILOT_LIST_VISIBLE_COUNT) {
			FrontendDraw_RectAssign(&rect, 596, 117, 605, 320);
			g_pilotListScrollOffset =
				FrontendScrollbar_Draw(&rect, g_pilotListScrollOffset, g_pilotFileList->count, 0,
									   PILOT_LIST_PAGE_STEP, g_colorNavy, PILOT_LIST_SCROLLBAR_CONTROL_ID);
		}

		FrontendDraw_RectAssign(&rect, 461, 117, 595, 320);
		selectedIndex = PilotRecord_DrawPilotList(&rect, g_pilotListScrollOffset);
		if (selectedIndex != 0) {
#ifdef XVT_MODERN
			accepted = strncasecmp(g_pilotData.name, g_pilotListDisplayNames[selectedIndex - 1],
								   PILOT_NAME_COMPARE_LENGTH);
#else
		accepted = _strnicmp(g_pilotData.name, g_pilotListDisplayNames[selectedIndex - 1],
							 PILOT_NAME_COMPARE_LENGTH);
#endif
			if (accepted != 0) {
				node = g_pilotFileList->head;
				pilotIndex = selectedIndex - 1;
				for (; pilotIndex > 0; --pilotIndex) {
					node = node->next;
				}
				if (Pilot_LoadFromPath(node->path) != 0) {
					if (g_gameConfig.sfxDatapadEnabled != 0) {
						FrontendSound_PlayUISound("logonsound", 1, 0, PILOT_UI_SOUND_PRIORITY,
												  PILOT_UI_SOUND_VOLUME_SCALE * g_gameConfig.sfxDatapadVolume,
												  PILOT_UI_SOUND_CENTER_PAN);
					}
					g_pilotData.team = g_pilotData.factionStatistics[g_pilotData.currentFactionId].team;
					g_pilotData.missionDirectoryId =
						g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionDirectoryId;
					memcpy(g_pilotData.missionDescriptionIds,
						   g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionDescriptionIds,
						   sizeof(g_pilotData.missionDescriptionIds));
					g_pilotData.missionSequenceActive =
						g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionSequenceActive;
					g_pilotData.missionSequenceDescriptionId =
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.missionSequenceDescriptionId;
				}
			}
			memset(g_pilotRecordNameInput, 0, sizeof(g_pilotRecordNameInput));
			PilotRecord_RebuildPilotList(&selectedIndex);
			PilotRecord_RedrawBackground();
			g_concourseRedrawRequested = 1;
		}

#ifdef XVT_MODERN
		if (!g_pilotFileList) {
			XvtStorage_Fatal("Cannot enumerate saved pilots", 1);
			return 1;
		}
#endif
		if (g_pilotFileList->count == 0) {
#ifdef XVT_MODERN
			if (frameCounter == 0) {
				XvtFrontendAction_Trigger(XVT_ACTION_PILOT, 1, 1);
				FrontendDialog_PromptForPilotName(g_pilotRecordNameInput);
				return 1;
			}
			FrontendDraw_RectAssign(&rect, 461, 321, 595, 340);
			selectedIndex = FrontendText_DrawEditableField(&rect, g_pilotRecordNameInput,
														   PILOT_NAME_MAX_CHARS, 0, 12, "\\*$~|:<>?/\t\"");
#else
		selectedIndex = FrontendDialog_PromptForPilotName(g_pilotRecordNameInput);
#endif
		} else {
			FrontendDraw_RectAssign(&rect, 461, 321, 595, 340);
			selectedIndex = FrontendText_DrawEditableField(&rect, g_pilotRecordNameInput,
														   PILOT_NAME_MAX_CHARS, 0, 12, "\\*$~|:<>?/\t\"");
		}

#ifdef XVT_MODERN
	}
#endif
	if (selectedIndex != 0 && g_pilotRecordNameInput[0] != '\0') {
		selectedIndex = 0;
		g_concourseRedrawRequested = 1;
		if (g_pilotFileList != NULL) {
			pilotIndex = 0;
			node = g_pilotFileList->head;
			if (g_pilotListDisplayNames != NULL && g_pilotFileList->count > 0) {
				for (; pilotIndex < g_pilotFileList->count; ++pilotIndex) {
#ifdef XVT_MODERN
					accepted = strcasecmp(g_pilotListDisplayNames[pilotIndex], g_pilotRecordNameInput);
#else
					accepted = _strcmpi(g_pilotListDisplayNames[pilotIndex], g_pilotRecordNameInput);
#endif
					if (accepted == 0) {
#ifdef XVT_MODERN
						selectedIndex = 1;
#endif
						if (Pilot_LoadFromPath(node->path) != 0) {
							if (g_gameConfig.sfxDatapadEnabled != 0) {
								FrontendSound_PlayUISound("logonsound", 1, 0, PILOT_UI_SOUND_PRIORITY,
														  PILOT_UI_SOUND_VOLUME_SCALE *
															  g_gameConfig.sfxDatapadVolume,
														  PILOT_UI_SOUND_CENTER_PAN);
							}
							g_pilotData.team =
								g_pilotData.factionStatistics[g_pilotData.currentFactionId].team;
							g_pilotData.missionDirectoryId =
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
									.missionDirectoryId;
							memcpy(g_pilotData.missionDescriptionIds,
								   g_pilotData.factionStatistics[g_pilotData.currentFactionId]
									   .missionDescriptionIds,
								   sizeof(g_pilotData.missionDescriptionIds));
							g_pilotData.missionSequenceActive =
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
									.missionSequenceActive;
							g_pilotData.missionSequenceDescriptionId =
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
									.missionSequenceDescriptionId;
						}
						break;
					}
					node = node->next;
				}
			}
		}
		if (selectedIndex == 0) {
			if (g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound("logonsound", 1, 0, PILOT_UI_SOUND_PRIORITY,
										  PILOT_UI_SOUND_VOLUME_SCALE * g_gameConfig.sfxDatapadVolume,
										  PILOT_UI_SOUND_CENTER_PAN);
			}
#ifdef XVT_MODERN
			if (!Pilot_CreateNew(g_pilotRecordNameInput)) {
				XvtStorage_Fatal("Cannot save the new pilot", 1);
				return 1;
			}
#else
			Pilot_CreateNew(g_pilotRecordNameInput);
#endif
			if (g_pilotFileList != NULL && g_pilotFileList->count > PILOT_LIST_VISIBLE_COUNT) {
				PilotRecord_RebuildPilotList(&g_pilotListScrollOffset);
			} else {
				PilotRecord_RebuildPilotList(&selectedIndex);
			}
		}
		memset(g_pilotRecordNameInput, 0, sizeof(g_pilotRecordNameInput));
		PilotRecord_RedrawBackground();
	}

	FrontendDraw_RectAssign(&rect, 451, 358, 605, 378);
	selectedIndex = FrontendButton_HandleTextButton(&rect, FrontendString_Get(FRONTSTR_464_DELETE_PILOT), 15,
													0xFFFF, 20, "buttonsound");
	if ((selectedIndex != 0 || Keyboard_IsKeyDown(PILOT_DELETE_VIRTUAL_KEY)) && g_pilotData.name[0] != '\0') {
#ifdef XVT_MODERN
		XvtFrontendAction_Trigger(XVT_ACTION_PILOT, 2, 1);
		FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_527_ARE_YOU_SURE_YOU_WANT),
										 FrontendString_Get(FRONTSTR_528_TO_DELETE_THIS_PILOT), NULL,
										 FrontendString_Get(FRONTSTR_529_YES),
										 FrontendString_Get(FRONTSTR_530_NO));
		return 1;
#else
		if (FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_527_ARE_YOU_SURE_YOU_WANT),
											 FrontendString_Get(FRONTSTR_528_TO_DELETE_THIS_PILOT), NULL,
											 FrontendString_Get(FRONTSTR_529_YES),
											 FrontendString_Get(FRONTSTR_530_NO)) != 0) {
			Pilot_DeleteCurrent();
		}
		g_pilotListScrollOffset = 0;
#endif
	}

	return 1;
}

// FUNCTION: XVT 0x4BEF80
int PilotRecord_DrawPilotList(const RECT* bounds, int firstVisibleIndex) {
	RECT rect;
	RECT previousClipRect;
	int mouseX;
	int mouseY;
	int selectedIndex;
	int firstIndex;
	int pilotIndex;
	int displayNameIndex;
	int count;

	if (g_pilotFileList == NULL)
		return 0;

	FrontendDraw_RectCopy(&rect, bounds);
	FrontendCursor_GetPos(&mouseX, &mouseY);
	rect.bottom = rect.top + 14;
	count = g_pilotFileList->count;
	selectedIndex = 0;
	if (g_pilotListDisplayNames != NULL) {
		firstIndex = firstVisibleIndex;
		pilotIndex = firstIndex;
		if (pilotIndex < firstIndex + 13) {
			displayNameIndex = firstIndex;
			do {
				if (pilotIndex >= count)
					break;
				if (FrontendDraw_PointInRect(&rect, mouseX, mouseY)) {
					FrontendDraw_FillRectTranslucent(&rect, 0, 0, g_colorGreen);
					if (FrontendMouse_GetLeftClick() || FrontendMouse_GetRightClick())
						selectedIndex = pilotIndex + 1;
				}
				FrontendDisplay_GetScreenClipRect(&previousClipRect);
				FrontendDisplay_SetScreenClipRect640x480(&rect);
#ifdef XVT_MODERN
				if (strcasecmp(g_pilotListDisplayNames[displayNameIndex], g_pilotData.name) == 0) {
					FrontendText_DrawAlignedInRect(12, g_pilotListDisplayNames[displayNameIndex], &rect, 0, 1,
												   g_colorLightBlue);
				} else {
					FrontendText_DrawAlignedInRect(12, g_pilotListDisplayNames[displayNameIndex], &rect, 0, 1,
												   0xFFFF);
				}
#else
				if (_strcmpi(g_pilotListDisplayNames[displayNameIndex], g_pilotData.name) == 0) {
					FrontendText_DrawAlignedInRect(12, g_pilotListDisplayNames[displayNameIndex], &rect, 0, 1,
												   g_colorLightBlue);
				} else {
					FrontendText_DrawAlignedInRect(12, g_pilotListDisplayNames[displayNameIndex], &rect, 0, 1,
												   0xFFFF);
				}
#endif
				++pilotIndex;
				++displayNameIndex;
				FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
				FrontendDraw_RectOffsetXY(&rect, 0, 15);
			} while (pilotIndex < firstIndex + 13);
		}
	}

	return selectedIndex;
}

// FUNCTION: XVT 0x4BF100
int PilotRecord_RebuildPilotList(int* selectedIndex) {
	FrontendFileListNode* node;
	int displayOffset;
	int pilotIndex;

	if (g_pilotListDisplayNames != NULL) {
		free(g_pilotListDisplayNames);
		g_pilotListDisplayNames = NULL;
	}
	if (g_pilotFileList != NULL) {
		FrontendFileList_Free(g_pilotFileList);
		g_pilotFileList = NULL;
	}
	File_ChangeToBaseGameInstallPath();
	g_pilotFileList = FrontendFileList_BuildSorted("*.plt");
	File_ChangeToInstallPath();
	if (g_pilotFileList == NULL) {
		return 0;
	}

	node = g_pilotFileList->head;
	if (g_pilotFileList->count > 0) {
		g_pilotListDisplayNames =
			(char (*)[14])malloc(sizeof(*g_pilotListDisplayNames) * g_pilotFileList->count);
#ifdef XVT_MODERN
		if (!g_pilotListDisplayNames)
			return 0;
#endif
		memset(g_pilotListDisplayNames, 0, sizeof(*g_pilotListDisplayNames) * g_pilotFileList->count);
	} else {
		g_pilotListDisplayNames = NULL;
	}
	if (g_pilotListDisplayNames != NULL) {
		pilotIndex = 0;
		if (node != NULL) {
			displayOffset = 0;
			do {
				XvtFile* stream = File_Open(node->path, "rb");

				if (stream != NULL) {
					File_ReadCount(stream, (char*)g_pilotListDisplayNames + displayOffset, 12);
					((char*)g_pilotListDisplayNames)[displayOffset + 12] = '\0';
#ifdef XVT_MODERN
					if (strcasecmp((char*)g_pilotListDisplayNames + displayOffset, g_pilotData.name) == 0) {
#else
					if (_strcmpi((char*)g_pilotListDisplayNames + displayOffset, g_pilotData.name) == 0) {
#endif
						*selectedIndex = pilotIndex;
					}
					File_Close(stream);
					displayOffset += 14;
					++pilotIndex;
				}
				node = node->next;
			} while (node != NULL);
		}
		return 1;
	}

	return 0;
}

// FUNCTION: XVT 0x4C0D70
int PilotRecord_DrawPilotStatisticsPage(void) {
	enum {
		MISSION_TYPE_COUNT = 3,
		CRAFT_TYPE_COUNT = 100,
		PLAYER_RATING_COUNT = 25,
		AI_RATING_COUNT = 6,
		VISIBLE_ROW_COUNT = 21,
		ROW_HEIGHT = 15,
		TEXT_X = 88,
		EXERCISE_X = 218,
		MELEE_X = 288,
		COMBAT_X = 358,
		FIRST_ROW_Y = 111,
		TEXT_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TEXT_COLOR_WHITE = 0xFFFF,
		SCROLLBAR_PAGE_STEP = 5,
		SCROLLBAR_CONTROL_ID = 3,
		PILOT_RATING_STRING_BASE = 122,
		CRAFT_NAME_STRING_BASE = 21,
	};

	RECT rect;
	int missionType;
	int craftType;
	int rating;
	int row;
	int y;
	int value;
	int sharedValue;
	int exerciseMissionCount;
	int meleeMissionCount;
	int combatMissionCount;
	float sharedAverage;
	unsigned int shotsFired;

	FrontendDraw_RectAssign(&rect, 84, 90, 434, 106);
	if (g_pilotData.name[0] == '\0') {
		sprintf(g_frontendScratchBuffer, "%s", FrontendString_Get(FRONTSTR_007_PILOT_STATISTICS));
	} else {
		sprintf(g_frontendScratchBuffer, "%s: %c%s %c%s", FrontendString_Get(FRONTSTR_007_PILOT_STATISTICS),
				6, g_pilotData.ratingName, 4, g_pilotData.name);
	}
	FrontendText_DrawCentered(TITLE_FONT_SIZE, g_frontendScratchBuffer, &rect, TEXT_COLOR_WHITE);
	if (g_pilotData.name[0] == '\0') {
		return 0;
	}

	if (g_concourseRedrawRequested != 0) {
		g_pilotStatisticsScrollOffset = 0;
		g_concourseRedrawRequested = 0;
		g_pilotRecordPageRowCount = 25;
		if ((unsigned int)g_pilotData.rating < PILOT_RATING_JEDI_MASTER) {
			g_pilotRecordPageRowCount = 26;
		}

		g_pilotStatsLossesToNonPlayers[0] = 0;
		g_pilotStatsLossesToPlayers[0] = 0;
		g_pilotStatsLossesToNonPlayers[1] = 0;
		g_pilotStatsLossesToPlayers[1] = 0;
		g_pilotStatsLossesToNonPlayers[2] = 0;
		g_pilotStatsLossesToPlayers[2] = 0;
		g_pilotStatsNonPlayerKillsShared[0] = 0;
		g_pilotStatsNonPlayerKillsShared[1] = 0;
		g_pilotStatsNonPlayerKills[0] = 0;
		g_pilotStatsNonPlayerKillsShared[2] = 0;
		g_pilotStatsNonPlayerKills[1] = 0;
		g_pilotStatsNonPlayerKills[2] = 0;
		g_pilotStatsPlayerKillsShared[0] = 0;
		g_pilotStatsPlayerKills[0] = 0;
		g_pilotStatsPlayerKillsShared[1] = 0;
		g_pilotStatsPlayerKills[1] = 0;
		g_pilotStatsPlayerKillsShared[2] = 0;
		g_pilotStatsPlayerKills[2] = 0;
		g_pilotStatsTotalKillsShared[0] = 0;
		g_pilotStatsTotalKillsShared[1] = 0;
		g_pilotStatsTotalKillsShared[2] = 0;
		g_pilotStatsAssists[0] = 0;
		g_pilotStatsAssists[1] = 0;
		g_pilotStatsAssists[2] = 0;

		g_pilotStatsHasCraftKillsByType = 0;
		for (craftType = 0; craftType < CRAFT_TYPE_COUNT; ++craftType) {
			g_pilotStatsRowHasData = 0;
			for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
				if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsPerCraftPerMT[missionType][craftType] != 0 ||
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsSharedPerCraftPerMT[missionType][craftType] != 0) {
					g_pilotStatsRowHasData = 1;
					g_pilotStatsHasCraftKillsByType = 1;
				}
			}
			if (g_pilotStatsRowHasData != 0) {
				++g_pilotRecordPageRowCount;
			}
		}
		if (g_pilotStatsHasCraftKillsByType != 0) {
			g_pilotRecordPageRowCount += 2;
		}

		g_pilotStatsHasPlayerKillsByRating = 0;
		for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
			g_pilotStatsRowHasData = 0;
			for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
				if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsFullOnPlayerRatingPerMT[missionType][rating] != 0 ||
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsSharedOnPlayerRatingPerMT[missionType][rating] != 0) {
					g_pilotStatsRowHasData = 1;
					g_pilotStatsHasPlayerKillsByRating = 1;
				}
			}
			if (g_pilotStatsRowHasData != 0) {
				++g_pilotRecordPageRowCount;
			}
		}
		if (g_pilotStatsHasPlayerKillsByRating != 0) {
			g_pilotRecordPageRowCount += 2;
		}

		g_pilotStatsHasLossesToPlayersByRank = 0;
		for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
			g_pilotStatsRowHasData = 0;
			for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
				if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killedByPlayerRatingPerMT[missionType][rating] != 0) {
					g_pilotStatsRowHasData = 1;
					g_pilotStatsHasLossesToPlayersByRank = 1;
				}
			}
			if (g_pilotStatsRowHasData != 0) {
				++g_pilotRecordPageRowCount;
			}
		}
		if (g_pilotStatsHasLossesToPlayersByRank != 0) {
			g_pilotRecordPageRowCount += 2;
		}

		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (craftType = 0; craftType < CRAFT_TYPE_COUNT; ++craftType) {
				g_pilotStatsAssists[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsAssistsPerCraftPerMT[missionType][craftType];
				g_pilotStatsTotalKillsShared[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsSharedPerCraftPerMT[missionType][craftType];
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
				g_pilotStatsPlayerKills[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsFullOnPlayerRatingPerMT[missionType][rating];
				g_pilotStatsPlayerKillsShared[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsSharedOnPlayerRatingPerMT[missionType][rating];
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				g_pilotStatsNonPlayerKills[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsFullOnAIRatingPerMT[missionType][rating];
				g_pilotStatsNonPlayerKillsShared[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsSharedOnAIRatingPerMT[missionType][rating];
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
				g_pilotStatsLossesToPlayers[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killedByPlayerRatingPerMT[missionType][rating];
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				g_pilotStatsLossesToNonPlayers[missionType] +=
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killedByAIRatingPerMT[missionType][rating];
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 425, 107, 434, 433);
	if (g_pilotRecordPageRowCount > VISIBLE_ROW_COUNT) {
		g_pilotStatisticsScrollOffset =
			FrontendScrollbar_Draw(&rect, g_pilotStatisticsScrollOffset, g_pilotRecordPageRowCount, 0,
								   SCROLLBAR_PAGE_STEP, g_colorNavy, SCROLLBAR_CONTROL_ID);
	}
	row = 0;
	y = FIRST_ROW_Y;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		sprintf(g_frontendScratchBuffer, "%c%s: %c%d", 4, FrontendString_Get(FRONTSTR_011_TOTAL_SCORE), 1,
				g_pilotData.totalScore);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4, FrontendString_Get(FRONTSTR_348_PILOT_RATING), 6,
				FrontendString_Get((FrontendStringId)(g_pilotData.rating + PILOT_RATING_STRING_BASE)));
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if ((unsigned int)g_pilotData.rating < PILOT_RATING_JEDI_MASTER) {
		if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
			sprintf(g_frontendScratchBuffer, "%c%s %c%d%%", 4,
					FrontendString_Get(FRONTSTR_349_ADVANCEMENT_TOWARD_NEXT_PROMOTION), 1,
					g_pilotData.nextPromotionPercent);
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, y, TEXT_COLOR_WHITE);
			y += ROW_HEIGHT;
		}
		++row;
	}
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_350_SUMMARY_OF_KILLS), TEXT_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_189_EXERCISE), EXERCISE_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_013_MELEE), MELEE_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_014_COMBAT), COMBAT_X, y,
						  g_colorLightBlue);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_297_TOTAL_KILLS), TEXT_X, y,
						  g_colorLightBlue);
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalKillsPerMT[0] != 0 ||
			g_pilotStatsTotalKillsShared[0] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)",
					g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalKillsPerMT[0],
					g_pilotStatsTotalKillsShared[0]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalKillsPerMT[1] != 0 ||
			g_pilotStatsTotalKillsShared[1] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)",
					g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalKillsPerMT[1],
					g_pilotStatsTotalKillsShared[1]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalKillsPerMT[2] != 0 ||
			g_pilotStatsTotalKillsShared[2] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)",
					g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalKillsPerMT[2],
					g_pilotStatsTotalKillsShared[2]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_351_PLAYER_KILLS), TEXT_X, y,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "----");
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsPlayerKills[1] != 0 || g_pilotStatsPlayerKillsShared[1] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)", g_pilotStatsPlayerKills[1],
					g_pilotStatsPlayerKillsShared[1]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsPlayerKills[2] != 0 || g_pilotStatsPlayerKillsShared[2] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)", g_pilotStatsPlayerKills[2],
					g_pilotStatsPlayerKillsShared[2]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_352_NON_PLAYER_KILLS), TEXT_X, y,
						  g_colorLightBlue);
		if (g_pilotStatsNonPlayerKills[0] != 0 || g_pilotStatsNonPlayerKillsShared[0] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)", g_pilotStatsNonPlayerKills[0],
					g_pilotStatsNonPlayerKillsShared[0]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsNonPlayerKills[1] != 0 || g_pilotStatsNonPlayerKillsShared[1] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)", g_pilotStatsNonPlayerKills[1],
					g_pilotStatsNonPlayerKillsShared[1]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsNonPlayerKills[2] != 0 || g_pilotStatsNonPlayerKillsShared[2] != 0) {
			sprintf(g_frontendScratchBuffer, "%d (%d)", g_pilotStatsNonPlayerKills[2],
					g_pilotStatsNonPlayerKillsShared[2]);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_353_ASSISTS), TEXT_X, y,
						  g_colorLightBlue);
		if (g_pilotStatsAssists[0] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsAssists[0]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsAssists[1] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsAssists[1]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsAssists[2] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsAssists[2]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_354_HIDDEN_CARGO_FOUND), TEXT_X, y,
						  g_colorLightBlue);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.numSpecialInspectedPerMT[0];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.numSpecialInspectedPerMT[1];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.numSpecialInspectedPerMT[2];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_355_LASER_ACCURACY), TEXT_X, y,
						  g_colorLightBlue);
		shotsFired = (unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						 .stats.energyFiredPerMT[0];
		if (shotsFired == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d%%",
					100 *
						g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.energyHitsPerMT[0] /
						shotsFired);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		shotsFired = (unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						 .stats.energyFiredPerMT[1];
		if (shotsFired == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d%%",
					100 *
						g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.energyHitsPerMT[1] /
						shotsFired);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		shotsFired = (unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						 .stats.energyFiredPerMT[2];
		if (shotsFired == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d%%",
					100 *
						g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.energyHitsPerMT[2] /
						shotsFired);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_356_WARHEAD_ACCURACY), TEXT_X, y,
						  g_colorLightBlue);
		shotsFired = (unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						 .stats.warheadsFiredPerMT[0];
		if (shotsFired == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(
				g_frontendScratchBuffer, "%d%%",
				100 * g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.warheadsHitsPerMT[0] /
					shotsFired);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		shotsFired = (unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						 .stats.warheadsFiredPerMT[1];
		if (shotsFired == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(
				g_frontendScratchBuffer, "%d%%",
				100 * g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.warheadsHitsPerMT[1] /
					shotsFired);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		shotsFired = (unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						 .stats.warheadsFiredPerMT[2];
		if (shotsFired == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(
				g_frontendScratchBuffer, "%d%%",
				100 * g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.warheadsHitsPerMT[2] /
					shotsFired);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (g_pilotStatsHasPlayerKillsByRating != 0) {
		if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
			y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_357_PLAYER_KILLS_BY_RANK), TEXT_X,
							  y, g_colorLightBlue);
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_013_MELEE), MELEE_X, y,
							  g_colorLightBlue);
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_014_COMBAT), COMBAT_X, y,
							  g_colorLightBlue);
			y += ROW_HEIGHT;
		}
		++row;
	}
	for (rating = PLAYER_RATING_COUNT - 1; rating >= 0; --rating) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.stats.killsFullOnPlayerRatingPerMT[1][rating] != 0 ||
			g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.stats.killsSharedOnPlayerRatingPerMT[1][rating] != 0 ||
			g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.stats.killsFullOnPlayerRatingPerMT[2][rating] != 0 ||
			g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.stats.killsSharedOnPlayerRatingPerMT[2][rating] != 0) {
			if (row >= g_pilotStatisticsScrollOffset &&
				row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
				sprintf(g_frontendScratchBuffer, "%c%s", 6,
						FrontendString_Get((FrontendStringId)(rating + PILOT_RATING_STRING_BASE)));
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, y, g_colorLightBlue);
				if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsFullOnPlayerRatingPerMT[1][rating] == 0 &&
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsSharedOnPlayerRatingPerMT[1][rating] == 0) {
					sprintf(g_frontendScratchBuffer, "----");
				} else {
					sprintf(g_frontendScratchBuffer, "%d (%d)",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.stats.killsFullOnPlayerRatingPerMT[1][rating],
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.stats.killsSharedOnPlayerRatingPerMT[1][rating]);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
				if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsFullOnPlayerRatingPerMT[2][rating] == 0 &&
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsSharedOnPlayerRatingPerMT[2][rating] == 0) {
					sprintf(g_frontendScratchBuffer, "----");
				} else {
					sprintf(g_frontendScratchBuffer, "%d (%d)",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.stats.killsFullOnPlayerRatingPerMT[2][rating],
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.stats.killsSharedOnPlayerRatingPerMT[2][rating]);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
				y += ROW_HEIGHT;
			}
			++row;
		}
	}

	if (g_pilotStatsHasCraftKillsByType != 0) {
		if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
			y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_358_CRAFT_KILLS_BY_TYPE), TEXT_X, y,
							  g_colorLightBlue);
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_189_EXERCISE), EXERCISE_X, y,
							  g_colorLightBlue);
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_013_MELEE), MELEE_X, y,
							  g_colorLightBlue);
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_014_COMBAT), COMBAT_X, y,
							  g_colorLightBlue);
			y += ROW_HEIGHT;
		}
		++row;
	}
	for (craftType = 0; craftType < CRAFT_TYPE_COUNT; ++craftType) {
		g_pilotStatsRowHasData = 0;
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsPerCraftPerMT[missionType][craftType] != 0 ||
				g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.killsSharedPerCraftPerMT[missionType][craftType] != 0) {
				g_pilotStatsRowHasData = 1;
			}
		}
		if (g_pilotStatsRowHasData != 0) {
			if (row >= g_pilotStatisticsScrollOffset &&
				row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
				FrontendText_Draw(TEXT_FONT_SIZE,
								  FrontendString_Get((FrontendStringId)(craftType + CRAFT_NAME_STRING_BASE)),
								  TEXT_X, y, g_colorRed);
				sharedValue = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .stats.killsSharedPerCraftPerMT[0][craftType];
				value = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsPerCraftPerMT[0][craftType];
				if (value == 0 && sharedValue == 0) {
					sprintf(g_frontendScratchBuffer, "----");
				} else {
					sprintf(g_frontendScratchBuffer, "%d (%d)", value, sharedValue);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
				sharedValue = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .stats.killsSharedPerCraftPerMT[1][craftType];
				value = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsPerCraftPerMT[1][craftType];
				if (value == 0 && sharedValue == 0) {
					sprintf(g_frontendScratchBuffer, "----");
				} else {
					sprintf(g_frontendScratchBuffer, "%d (%d)", value, sharedValue);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
				sharedValue = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .stats.killsSharedPerCraftPerMT[2][craftType];
				value = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killsPerCraftPerMT[2][craftType];
				if (value == 0 && sharedValue == 0) {
					sprintf(g_frontendScratchBuffer, "----");
				} else {
					sprintf(g_frontendScratchBuffer, "%d (%d)", value, sharedValue);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
				y += ROW_HEIGHT;
			}
			++row;
		}
	}

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_359_AVGS_PER_MISSION), TEXT_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_189_EXERCISE), EXERCISE_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_013_MELEE), MELEE_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_014_COMBAT), COMBAT_X, y,
						  g_colorLightBlue);
		y += ROW_HEIGHT;
	}
	++row;
	exerciseMissionCount =
		g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.standaloneMissionsPlayedPerMT[0] +
		g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.sequenceMissionsPlayedPerMT[0];
	if (exerciseMissionCount == 0) {
		exerciseMissionCount = 1;
	}
	meleeMissionCount =
		g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.standaloneMissionsPlayedPerMT[1] +
		g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.sequenceMissionsPlayedPerMT[1];
	if (meleeMissionCount == 0) {
		meleeMissionCount = 1;
	}
	combatMissionCount =
		g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.standaloneMissionsPlayedPerMT[2] +
		g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.sequenceMissionsPlayedPerMT[2];
	if (combatMissionCount == 0) {
		combatMissionCount = 1;
	}

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_297_TOTAL_KILLS), TEXT_X, y,
						  g_colorLightBlue);
		sharedAverage = (double)g_pilotStatsTotalKillsShared[0] / (double)exerciseMissionCount;
		if ((double)(unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.totalKillsPerMT[0] /
					(double)exerciseMissionCount !=
				0.0 ||
			sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)(unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.totalKillsPerMT[0] /
						(double)exerciseMissionCount,
					(double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		sharedAverage = (double)g_pilotStatsTotalKillsShared[1] / (double)meleeMissionCount;
		if ((double)(unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.totalKillsPerMT[1] /
					(double)meleeMissionCount !=
				0.0 ||
			sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)(unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.totalKillsPerMT[1] /
						(double)meleeMissionCount,
					(double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		sharedAverage = (double)g_pilotStatsTotalKillsShared[2] / (double)combatMissionCount;
		if ((double)(unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.stats.totalKillsPerMT[2] /
					(double)combatMissionCount !=
				0.0 ||
			sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)(unsigned int)g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.totalKillsPerMT[2] /
						(double)combatMissionCount,
					(double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_351_PLAYER_KILLS), TEXT_X, y,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "----");
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		sharedAverage = (double)g_pilotStatsPlayerKillsShared[1] / (double)meleeMissionCount;
		if ((double)g_pilotStatsPlayerKills[1] / (double)meleeMissionCount != 0.0 || sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)g_pilotStatsPlayerKills[1] / (double)meleeMissionCount, (double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		sharedAverage = (double)g_pilotStatsPlayerKillsShared[2] / (double)combatMissionCount;
		if ((double)g_pilotStatsPlayerKills[2] / (double)combatMissionCount != 0.0 || sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)g_pilotStatsPlayerKills[2] / (double)combatMissionCount, (double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_352_NON_PLAYER_KILLS), TEXT_X, y,
						  g_colorLightBlue);
		sharedAverage = (double)g_pilotStatsNonPlayerKillsShared[0] / (double)exerciseMissionCount;
		if ((double)g_pilotStatsNonPlayerKills[0] / (double)exerciseMissionCount != 0.0 ||
			sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)g_pilotStatsNonPlayerKills[0] / (double)exerciseMissionCount,
					(double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		sharedAverage = (double)g_pilotStatsNonPlayerKillsShared[1] / (double)meleeMissionCount;
		if ((double)g_pilotStatsNonPlayerKills[1] / (double)meleeMissionCount != 0.0 ||
			sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)g_pilotStatsNonPlayerKills[1] / (double)meleeMissionCount, (double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		sharedAverage = (double)g_pilotStatsNonPlayerKillsShared[2] / (double)combatMissionCount;
		if ((double)g_pilotStatsNonPlayerKills[2] / (double)combatMissionCount != 0.0 ||
			sharedAverage != 0.0f) {
			sprintf(g_frontendScratchBuffer, "%.1f (%.1f)",
					(double)g_pilotStatsNonPlayerKills[2] / (double)combatMissionCount,
					(double)sharedAverage);
		} else {
			sprintf(g_frontendScratchBuffer, "----");
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_353_ASSISTS), TEXT_X, y,
						  g_colorLightBlue);
		if ((double)g_pilotStatsAssists[0] / (double)exerciseMissionCount == 0.0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%.1f",
					(double)g_pilotStatsAssists[0] / (double)exerciseMissionCount);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		if ((double)g_pilotStatsAssists[1] / (double)meleeMissionCount == 0.0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%.1f",
					(double)g_pilotStatsAssists[1] / (double)meleeMissionCount);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		if ((double)g_pilotStatsAssists[2] / (double)combatMissionCount == 0.0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%.1f",
					(double)g_pilotStatsAssists[2] / (double)combatMissionCount);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_360_TOTAL_LOSSES), TEXT_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_189_EXERCISE), EXERCISE_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_013_MELEE), MELEE_X, y,
						  g_colorLightBlue);
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_014_COMBAT), COMBAT_X, y,
						  g_colorLightBlue);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_361_TOTAL_CRAFT_LOSSES), TEXT_X, y,
						  g_colorLightBlue);
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalCraftLossesPerMT[0] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(
				g_frontendScratchBuffer, "%d",
				g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalCraftLossesPerMT[0]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalCraftLossesPerMT[1];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.totalCraftLossesPerMT[2];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_362_TO_PLAYER_PILOTS), TEXT_X, y,
						  g_colorLightBlue);
		if (g_pilotStatsLossesToPlayers[0] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsLossesToPlayers[0]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsLossesToPlayers[1] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsLossesToPlayers[1]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsLossesToPlayers[2] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsLossesToPlayers[2]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_363_TO_NON_PLAYER_PILOTS), TEXT_X, y,
						  g_colorLightBlue);
		if (g_pilotStatsLossesToNonPlayers[0] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsLossesToNonPlayers[0]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsLossesToNonPlayers[1] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsLossesToNonPlayers[1]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilotStatsLossesToNonPlayers[2] == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", g_pilotStatsLossesToNonPlayers[2]);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_364_TO_STARSHIPS), TEXT_X, y,
						  g_colorLightBlue);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByStarshipsPerMT[0];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByStarshipsPerMT[1];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByStarshipsPerMT[2];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_365_TO_MINES), TEXT_X, y,
						  g_colorLightBlue);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByMinesPerMT[0];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByMinesPerMT[1];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByMinesPerMT[2];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_366_FROM_COLLISIONS), TEXT_X, y,
						  g_colorLightBlue);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByCollisionsPerMT[0];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByCollisionsPerMT[1];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilotData.factionStatistics[g_pilotData.currentFactionId].stats.lossesByCollisionsPerMT[2];
		if (value == 0) {
			sprintf(g_frontendScratchBuffer, "----");
		} else {
			sprintf(g_frontendScratchBuffer, "%d", value);
		}
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (g_pilotStatsHasLossesToPlayersByRank != 0) {
		if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
			y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_pilotStatisticsScrollOffset && row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_534_LOSSES_TO_PLAYERS_BY_RANK),
							  TEXT_X, y, g_colorLightBlue);
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_013_MELEE), MELEE_X, y,
							  g_colorLightBlue);
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_014_COMBAT), COMBAT_X, y,
							  g_colorLightBlue);
			y += ROW_HEIGHT;
		}
		++row;
	}
	for (rating = PLAYER_RATING_COUNT - 1; rating >= 0; --rating) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.stats.killedByPlayerRatingPerMT[1][rating] != 0 ||
			g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.stats.killedByPlayerRatingPerMT[2][rating] != 0) {
			if (row >= g_pilotStatisticsScrollOffset &&
				row - g_pilotStatisticsScrollOffset < VISIBLE_ROW_COUNT) {
				sprintf(g_frontendScratchBuffer, "%c%s", 6,
						FrontendString_Get((FrontendStringId)(rating + PILOT_RATING_STRING_BASE)));
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, y, g_colorLightBlue);
				value = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killedByPlayerRatingPerMT[1][rating];
				if (value == 0) {
					sprintf(g_frontendScratchBuffer, "----");
				} else {
					sprintf(g_frontendScratchBuffer, "%d", value);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, MELEE_X, y, TEXT_COLOR_WHITE);
				value = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.stats.killedByPlayerRatingPerMT[2][rating];
				if (value == 0) {
					sprintf(g_frontendScratchBuffer, "----");
				} else {
					sprintf(g_frontendScratchBuffer, "%d", value);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, COMBAT_X, y, TEXT_COLOR_WHITE);
				y += ROW_HEIGHT;
			}
			++row;
		}
	}
	return 1;
}

// FUNCTION: XVT 0x4C3590
int PilotRecord_DrawMissionAchievementsPage(void) {
	RECT rect;
	RECT previousClipRect;
	const char* awardSpriteFormat;
	int listIndex;
	int childListIndex;
	int awardId;
	int row;
	int y;
	int hasPrevious;
	int drawFlags;

	FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TITLE_LEFT, MISSION_ACHIEVEMENT_TITLE_TOP,
							MISSION_ACHIEVEMENT_TITLE_RIGHT, MISSION_ACHIEVEMENT_TITLE_BOTTOM);
	if (g_pilotData.name[0] == '\0') {
		sprintf(g_frontendScratchBuffer, "%s", FrontendString_Get(FRONTSTR_010_MISSION_ACHIEVEMENTS));
	} else {
		sprintf(g_frontendScratchBuffer, "%s: %c%s %c%s",
				FrontendString_Get(FRONTSTR_010_MISSION_ACHIEVEMENTS), MISSION_ACHIEVEMENT_RATING_TEXT_CODE,
				g_pilotData.ratingName, MISSION_ACHIEVEMENT_NAME_TEXT_CODE, g_pilotData.name);
	}
	FrontendText_DrawCentered(MISSION_ACHIEVEMENT_TITLE_FONT_SIZE, g_frontendScratchBuffer, &rect,
							  MISSION_ACHIEVEMENT_TEXT_COLOR);
	if (g_pilotData.name[0] == '\0') {
		return 0;
	}

	if (g_concourseRedrawRequested != 0) {
		if (g_pilotRecordSingleplayerTrainingMissionList != NULL) {
			free(g_pilotRecordSingleplayerTrainingMissionList);
			g_pilotRecordSingleplayerTrainingMissionList = NULL;
		}
		if (g_pilotRecordMultiplayerTrainingMissionList != NULL) {
			free(g_pilotRecordMultiplayerTrainingMissionList);
			g_pilotRecordMultiplayerTrainingMissionList = NULL;
		}
		if (g_pilotRecordMeleeMissionList != NULL) {
			free(g_pilotRecordMeleeMissionList);
			g_pilotRecordMeleeMissionList = NULL;
		}
		if (g_pilotRecordTournamentMissionList != NULL) {
			free(g_pilotRecordTournamentMissionList);
			g_pilotRecordTournamentMissionList = NULL;
		}
		if (g_pilotRecordSingleplayerCombatMissionList != NULL) {
			free(g_pilotRecordSingleplayerCombatMissionList);
			g_pilotRecordSingleplayerCombatMissionList = NULL;
		}
		if (g_pilotRecordMultiplayerCombatMissionList != NULL) {
			free(g_pilotRecordMultiplayerCombatMissionList);
			g_pilotRecordMultiplayerCombatMissionList = NULL;
		}
		if (g_battleMissionList != NULL) {
			free(g_battleMissionList);
			g_battleMissionList = NULL;
		}
		if (g_pilotRecordSingleplayerCampaignMissionList != NULL) {
			free(g_pilotRecordSingleplayerCampaignMissionList);
			g_pilotRecordSingleplayerCampaignMissionList = NULL;
		}
		if (g_pilotRecordMultiplayerCampaignMissionList != NULL) {
			free(g_pilotRecordMultiplayerCampaignMissionList);
			g_pilotRecordMultiplayerCampaignMissionList = NULL;
		}
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_pilotRecordSingleplayerTrainingMissionList = g_missionList;
		g_pilotRecordSingleplayerTrainingMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerTrainingMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordSingleplayerTrainingMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordSingleplayerTrainingMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordSingleplayerTrainingMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		g_pilotRecordMultiplayerTrainingMissionList = g_missionList;
		g_pilotRecordMultiplayerTrainingMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerTrainingMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordMultiplayerTrainingMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordMultiplayerTrainingMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordMultiplayerTrainingMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_MELEES);
		g_pilotRecordMeleeMissionList = g_missionList;
		g_pilotRecordMeleeMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordMeleeMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordMeleeMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordMeleeMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordMeleeMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_TOURNAMENTS);
		g_pilotRecordTournamentMissionList = g_missionList;
		g_pilotRecordTournamentMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordTournamentMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordTournamentMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordTournamentMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordTournamentMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
		g_pilotRecordSingleplayerCombatMissionList = g_missionList;
		g_pilotRecordSingleplayerCombatMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCombatMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordSingleplayerCombatMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordSingleplayerCombatMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordSingleplayerCombatMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		g_pilotRecordMultiplayerCombatMissionList = g_missionList;
		g_pilotRecordMultiplayerCombatMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCombatMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordMultiplayerCombatMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordMultiplayerCombatMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordMultiplayerCombatMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_BATTLES);
		g_battleMissionList = g_missionList;
		g_battleMissionListCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_battleMissionListCount; ++listIndex) {
			awardId = (int)strlen(g_battleMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_battleMissionList[listIndex].description[awardId] == '(') {
					g_battleMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_CAMPAIGNS);
		g_pilotRecordSingleplayerCampaignMissionList = g_missionList;
		g_pilotRecordSingleplayerCampaignMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCampaignMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordSingleplayerCampaignMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordSingleplayerCampaignMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordSingleplayerCampaignMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_CAMPAIGNS);
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		g_pilotRecordMultiplayerCampaignMissionList = g_missionList;
		g_pilotRecordMultiplayerCampaignMissionCount = g_missionCount;
		g_missionList = NULL;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCampaignMissionCount; ++listIndex) {
			awardId = (int)strlen(g_pilotRecordMultiplayerCampaignMissionList[listIndex].description) - 1;
			for (; awardId > 0; --awardId) {
				if (g_pilotRecordMultiplayerCampaignMissionList[listIndex].description[awardId] == '(') {
					g_pilotRecordMultiplayerCampaignMissionList[listIndex].description[awardId] = '\0';
					break;
				}
			}
		}
		g_pilotAchievementsScrollOffset = 0;
		g_concourseRedrawRequested = 0;
		g_pilotRecordPageRowCount = 0;
		g_pilotSpCampaignHistoryRowCount = 0;
		g_pilotSpBattleHistoryCount = 0;
		g_pilotSpTournamentHistoryCount = 0;
		g_pilotSpTrainingHistoryCount = 0;
		g_pilotSpCombatHistoryCount = 0;
		g_pilotSpMeleeHistoryCount = 0;
		g_pilotMpCampaignHistoryRowCount = 0;
		g_pilotMpBattleHistoryCount = 0;
		g_pilotMpTournamentHistoryCount = 0;
		g_pilotMpTrainingHistoryCount = 0;
		g_pilotMpCombatHistoryCount = 0;
		g_pilotMpMeleeHistoryCount = 0;

		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spTrainingMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				++g_pilotSpTrainingHistoryCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpTrainingMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				++g_pilotMpTrainingHistoryCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordMeleeMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				++g_pilotSpMeleeHistoryCount;
			}
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				++g_pilotMpMeleeHistoryCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCombatMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCombatMissions[g_pilotRecordSingleplayerCombatMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				++g_pilotSpCombatHistoryCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCombatMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCombatMissions[g_pilotRecordMultiplayerCombatMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				++g_pilotMpCombatHistoryCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordTournamentMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				++g_pilotSpTournamentHistoryCount;
			}
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				++g_pilotMpTournamentHistoryCount;
			}
		}
		for (listIndex = 0; listIndex < g_battleMissionListCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spBattles[g_battleMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				++g_pilotSpBattleHistoryCount;
			}
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpBattles[g_battleMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				++g_pilotMpBattleHistoryCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCampaignMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				++g_pilotSpCampaignHistoryRowCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCampaignMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex].missionIdx -
										1]
					.numberTimesFlown != 0) {
				++g_pilotSpCampaignHistoryRowCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCampaignMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCampaigns[g_pilotRecordMultiplayerCampaignMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				++g_pilotMpCampaignHistoryRowCount;
			}
		}
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCampaignMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex].missionIdx - 1]
					.numberTimesFlown != 0) {
				++g_pilotMpCampaignHistoryRowCount;
			}
		}
		row = g_pilotSpBattleHistoryCount;
		row += g_pilotSpTournamentHistoryCount;
		row += g_pilotMpCampaignHistoryRowCount;
		row += g_pilotSpTrainingHistoryCount;
		row += g_pilotMpBattleHistoryCount;
		row += g_pilotSpCombatHistoryCount;
		row += g_pilotSpMeleeHistoryCount;
		row += g_pilotMpTournamentHistoryCount;
		row += g_pilotMpTrainingHistoryCount;
		row += g_pilotMpCombatHistoryCount;
		row += g_pilotMpMeleeHistoryCount;
		row += g_pilotSpCampaignHistoryRowCount;
		g_pilotRecordPageRowCount = row;
		hasPrevious = 0;
		if (g_pilotSpTrainingHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			hasPrevious = 1;
		}
		if (g_pilotSpMeleeHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotSpCombatHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotSpTournamentHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotSpBattleHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotSpCampaignHistoryRowCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotMpTrainingHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotMpMeleeHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotMpCombatHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotMpTournamentHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
			hasPrevious = 1;
		}
		if (g_pilotMpBattleHistoryCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 0) {
				hasPrevious = 1;
				++g_pilotRecordPageRowCount;
			}
		}
		if (g_pilotMpCampaignHistoryRowCount != 0) {
			g_pilotRecordPageRowCount += 2;
			if (hasPrevious == 1) {
				++g_pilotRecordPageRowCount;
			}
		}
	}

	FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_SCROLL_LEFT, MISSION_ACHIEVEMENT_SCROLL_TOP,
							MISSION_ACHIEVEMENT_SCROLL_RIGHT, MISSION_ACHIEVEMENT_SCROLL_BOTTOM);
	if (g_pilotRecordPageRowCount > MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
		g_pilotAchievementsScrollOffset = FrontendScrollbar_Draw(
			&rect, g_pilotAchievementsScrollOffset, g_pilotRecordPageRowCount, 0,
			MISSION_ACHIEVEMENT_SCROLL_PAGE_STEP, g_colorNavy, MISSION_ACHIEVEMENT_SCROLL_CONTROL_ID);
	}

	row = 0;
	y = MISSION_ACHIEVEMENT_FIRST_Y;
	hasPrevious = 0;
	if (g_pilotSpTrainingHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_451_SINGLE_PLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
							  FrontendString_Get(FRONTSTR_453_TRAINING_HISTORY), MISSION_ACHIEVEMENT_HEADER_X,
							  y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_461_TIME),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spTrainingMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordSingleplayerTrainingMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "....",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spTrainingMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex]
														.missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spTrainingMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex]
													.missionIdx]
							.bestTime == 0) {
						strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						Frontend_FormatSecondsToClockString(
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spTrainingMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex]
														.missionIdx]
								.bestTime);
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpMeleeHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_451_SINGLE_PLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_454_MELEE_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_462_FINISH),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMeleeMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags = FrontendText_Draw(
						MISSION_ACHIEVEMENT_FONT_SIZE, g_pilotRecordMeleeMissionList[listIndex].description,
						MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "....",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
							.bestPlacement != 0) {
						sprintf(
							g_frontendScratchBuffer, "%s",
							FrontendString_Get(
								(FrontendStringId)(g_pilotData.factionStatistics[g_pilotData.currentFactionId]
													   .spMeleeMissions
														   [g_pilotRecordMeleeMissionList[listIndex]
																.missionIdx]
													   .bestPlacement +
												   317)));
					} else {
						sprintf(g_frontendScratchBuffer, "---");
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpTournamentHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_451_SINGLE_PLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
							  FrontendString_Get(FRONTSTR_456_TOURNAMENT_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_462_FINISH),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordTournamentMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordTournamentMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
							.bestPlacement != 0) {
						sprintf(
							g_frontendScratchBuffer, "%s",
							FrontendString_Get(
								(FrontendStringId)(g_pilotData.factionStatistics[g_pilotData.currentFactionId]
													   .spTournaments
														   [g_pilotRecordTournamentMissionList[listIndex]
																.missionIdx]
													   .bestPlacement +
												   317)));
					} else {
						sprintf(g_frontendScratchBuffer, "---");
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpCombatHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_451_SINGLE_PLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_455_COMBAT_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_461_TIME),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCombatMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCombatMissions[g_pilotRecordSingleplayerCombatMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordSingleplayerCombatMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spCombatMissions[g_pilotRecordSingleplayerCombatMissionList[listIndex]
													  .missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spCombatMissions[g_pilotRecordSingleplayerCombatMissionList[listIndex]
												  .missionIdx]
							.bestTime == 0) {
						strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						Frontend_FormatSecondsToClockString(
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spCombatMissions[g_pilotRecordSingleplayerCombatMissionList[listIndex]
													  .missionIdx]
								.bestTime);
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpBattleHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_451_SINGLE_PLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_457_BATTLE_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_718_MARGIN),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_battleMissionListCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spBattles[g_battleMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags = FrontendText_Draw(
						MISSION_ACHIEVEMENT_FONT_SIZE, g_battleMissionList[listIndex].description,
						MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spBattles[g_battleMissionList[listIndex].missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spBattles[g_battleMissionList[listIndex].missionIdx]
							.victoryCount != 0) {
						sprintf(g_frontendScratchBuffer, "%d %s",
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
									.spBattles[g_battleMissionList[listIndex].missionIdx]
									.bestVictoryMargin,
								FrontendString_Get(FRONTSTR_719_VICTS));
					} else {
						sprintf(g_frontendScratchBuffer, "---");
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpCampaignHistoryRowCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_451_SINGLE_PLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
							  FrontendString_Get(FRONTSTR_458_CAMPAIGN_HISTORY), MISSION_ACHIEVEMENT_HEADER_X,
							  y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_787_PROGRESS),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCampaignMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordSingleplayerCampaignMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(
						g_frontendScratchBuffer, "%d",
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[listIndex].missionIdx]
							.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[listIndex].missionIdx]
							.isFinished != 0) {
						strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_789_FINISHED));
					} else {
						sprintf(g_frontendScratchBuffer, "%s %d", FrontendString_Get(FRONTSTR_788_MIS),
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[listIndex]
														 .missionIdx]
										.nextMissionIndex +
									1);
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (childListIndex = 0; childListIndex < g_pilotRecordSingleplayerTrainingMissionCount;
					 ++childListIndex) {
					awardId = g_pilotRecordSingleplayerTrainingMissionList[childListIndex].missionIdx;
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spCampaignMissions[awardId - 1]
								.numberTimesFlown != 0 &&
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spCampaignMissions[awardId - 1]
								.campaignId ==
							g_pilotRecordSingleplayerCampaignMissionList[listIndex].missionIdx) {
						if (g_pilotAchievementsScrollOffset <= row &&
							row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							FrontendDisplay_GetScreenClipRect(&previousClipRect);
							FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
													MISSION_ACHIEVEMENT_TEXT_RIGHT,
													y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
							FrontendDisplay_SetScreenClipRect640x480(&rect);
							drawFlags = FrontendText_Draw(
								MISSION_ACHIEVEMENT_FONT_SIZE,
								g_pilotRecordSingleplayerTrainingMissionList[childListIndex].description,
								MISSION_ACHIEVEMENT_TEXT_X, y, g_colorGray);
							FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
							if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
								FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
												  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, g_colorGray);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}
	if (g_pilotMpTrainingHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_452_MULTIPLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
							  FrontendString_Get(FRONTSTR_453_TRAINING_HISTORY), MISSION_ACHIEVEMENT_HEADER_X,
							  y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_461_TIME),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpTrainingMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordMultiplayerTrainingMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "....",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpTrainingMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex]
														.missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpTrainingMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex]
													.missionIdx]
							.bestTime == 0) {
						strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						Frontend_FormatSecondsToClockString(
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpTrainingMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex]
														.missionIdx]
								.bestTime);
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpMeleeHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_452_MULTIPLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_454_MELEE_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_462_FINISH),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMeleeMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags = FrontendText_Draw(
						MISSION_ACHIEVEMENT_FONT_SIZE, g_pilotRecordMeleeMissionList[listIndex].description,
						MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "....",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
							.bestPlacement != 0) {
						sprintf(
							g_frontendScratchBuffer, "%s",
							FrontendString_Get(
								(FrontendStringId)(g_pilotData.factionStatistics[g_pilotData.currentFactionId]
													   .mpMeleeMissions
														   [g_pilotRecordMeleeMissionList[listIndex]
																.missionIdx]
													   .bestPlacement +
												   317)));
					} else {
						sprintf(g_frontendScratchBuffer, "---");
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpTournamentHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_452_MULTIPLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
							  FrontendString_Get(FRONTSTR_456_TOURNAMENT_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_462_FINISH),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordTournamentMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordTournamentMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
							.bestPlacement != 0) {
						sprintf(
							g_frontendScratchBuffer, "%s",
							FrontendString_Get(
								(FrontendStringId)(g_pilotData.factionStatistics[g_pilotData.currentFactionId]
													   .mpTournaments
														   [g_pilotRecordTournamentMissionList[listIndex]
																.missionIdx]
													   .bestPlacement +
												   317)));
					} else {
						sprintf(g_frontendScratchBuffer, "---");
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpCombatHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_452_MULTIPLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_455_COMBAT_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_461_TIME),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCombatMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCombatMissions[g_pilotRecordMultiplayerCombatMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordMultiplayerCombatMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(
						g_frontendScratchBuffer, "%d",
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpCombatMissions[g_pilotRecordMultiplayerCombatMissionList[listIndex].missionIdx]
							.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpCombatMissions[g_pilotRecordMultiplayerCombatMissionList[listIndex].missionIdx]
							.bestTime == 0) {
						strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						Frontend_FormatSecondsToClockString(
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpCombatMissions[g_pilotRecordMultiplayerCombatMissionList[listIndex]
													  .missionIdx]
								.bestTime);
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpBattleHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_452_MULTIPLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_457_BATTLE_HISTORY),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_718_MARGIN),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_battleMissionListCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpBattles[g_battleMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags = FrontendText_Draw(
						MISSION_ACHIEVEMENT_FONT_SIZE, g_battleMissionList[listIndex].description,
						MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpBattles[g_battleMissionList[listIndex].missionIdx]
								.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpBattles[g_battleMissionList[listIndex].missionIdx]
							.victoryCount != 0) {
						sprintf(g_frontendScratchBuffer, "%d %s",
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
									.mpBattles[g_battleMissionList[listIndex].missionIdx]
									.bestVictoryMargin,
								FrontendString_Get(FRONTSTR_719_VICTS));
					} else {
						sprintf(g_frontendScratchBuffer, "---");
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpCampaignHistoryRowCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_452_MULTIPLAYER),
							  MISSION_ACHIEVEMENT_HEADER_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_459_BEST),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
							  FrontendString_Get(FRONTSTR_458_CAMPAIGN_HISTORY), MISSION_ACHIEVEMENT_HEADER_X,
							  y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_460_SCORE),
							  MISSION_ACHIEVEMENT_SCORE_X, y, g_colorLightBlue);
			FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, FrontendString_Get(FRONTSTR_787_PROGRESS),
							  MISSION_ACHIEVEMENT_DETAIL_X, y, g_colorLightBlue);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCampaignMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCampaigns[g_pilotRecordMultiplayerCampaignMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					FrontendDisplay_GetScreenClipRect(&previousClipRect);
					FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
											MISSION_ACHIEVEMENT_TEXT_RIGHT,
											y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					drawFlags =
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE,
										  g_pilotRecordMultiplayerCampaignMissionList[listIndex].description,
										  MISSION_ACHIEVEMENT_TEXT_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
					if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
						FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
										  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(
						g_frontendScratchBuffer, "%d",
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpCampaigns[g_pilotRecordMultiplayerCampaignMissionList[listIndex].missionIdx]
							.bestScore);
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_SCORE_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpCampaigns[g_pilotRecordMultiplayerCampaignMissionList[listIndex].missionIdx]
							.isFinished != 0) {
						strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_789_FINISHED));
					} else {
						sprintf(g_frontendScratchBuffer, "%s %d", FrontendString_Get(FRONTSTR_788_MIS),
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.mpCampaigns[g_pilotRecordMultiplayerCampaignMissionList[listIndex]
														 .missionIdx]
										.nextMissionIndex +
									1);
					}
					FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, g_frontendScratchBuffer,
									  MISSION_ACHIEVEMENT_DETAIL_X, y, MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (childListIndex = 0; childListIndex < g_pilotRecordMultiplayerTrainingMissionCount;
					 ++childListIndex) {
					awardId = g_pilotRecordMultiplayerTrainingMissionList[childListIndex].missionIdx;
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpCampaignMissions[awardId - 1]
								.numberTimesFlown != 0 &&
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpCampaignMissions[awardId - 1]
								.campaignId ==
							g_pilotRecordMultiplayerCampaignMissionList[listIndex].missionIdx) {
						if (g_pilotAchievementsScrollOffset <= row &&
							row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							FrontendDisplay_GetScreenClipRect(&previousClipRect);
							FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_TEXT_X, y,
													MISSION_ACHIEVEMENT_TEXT_RIGHT,
													y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
							FrontendDisplay_SetScreenClipRect640x480(&rect);
							drawFlags = FrontendText_Draw(
								MISSION_ACHIEVEMENT_FONT_SIZE,
								g_pilotRecordMultiplayerTrainingMissionList[childListIndex].description,
								MISSION_ACHIEVEMENT_TEXT_X, y, g_colorGray);
							FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
							if ((drawFlags & MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) != 0) {
								FrontendText_Draw(MISSION_ACHIEVEMENT_FONT_SIZE, "...",
												  MISSION_ACHIEVEMENT_TEXT_RIGHT, y, g_colorGray);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}

	row = 0;
	y = MISSION_ACHIEVEMENT_FIRST_Y;
	hasPrevious = 0;
	if (g_pilotSpTrainingHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spTrainingMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .spTrainingMissions[g_pilotRecordSingleplayerTrainingMissionList[listIndex]
														  .missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilotData.currentFactionId != 0) {
							awardSpriteFormat = "citlvl%d";
						} else {
							awardSpriteFormat = "rcitlvl%d";
						}
						sprintf(g_frontendScratchBuffer, awardSpriteFormat, awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpMeleeHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMeleeMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .spMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontendScratchBuffer, "medlvl%d", awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpTournamentHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordTournamentMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .spTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontendScratchBuffer, "medlvl%d", awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpCombatHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCombatMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCombatMissions[g_pilotRecordSingleplayerCombatMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .spCombatMissions[g_pilotRecordSingleplayerCombatMissionList[listIndex]
														.missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilotData.currentFactionId != 0) {
							awardSpriteFormat = "citlvl%d";
						} else {
							awardSpriteFormat = "rcitlvl%d";
						}
						sprintf(g_frontendScratchBuffer, awardSpriteFormat, awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpBattleHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_battleMissionListCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spBattles[g_battleMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .spBattles[g_battleMissionList[listIndex].missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontendScratchBuffer, "medlvl%d", awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotSpCampaignHistoryRowCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordSingleplayerCampaignMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (childListIndex = 0; childListIndex < g_pilotRecordSingleplayerTrainingMissionCount;
					 ++childListIndex) {
					awardId = g_pilotRecordSingleplayerTrainingMissionList[childListIndex].missionIdx;
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spCampaignMissions[awardId - 1]
								.numberTimesFlown != 0 &&
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spCampaignMissions[awardId - 1]
								.campaignId ==
							g_pilotRecordSingleplayerCampaignMissionList[listIndex].missionIdx) {
						if (g_pilotAchievementsScrollOffset <= row &&
							row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										  .spCampaignMissions[awardId - 1]
										  .awardId;
							if (awardId != 0) {
								FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
														MISSION_ACHIEVEMENT_AWARD_RIGHT,
														y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
								if (g_pilotData.currentFactionId != 0) {
									awardSpriteFormat = "citlvl%d";
								} else {
									awardSpriteFormat = "rcitlvl%d";
								}
								sprintf(g_frontendScratchBuffer, awardSpriteFormat, awardId);
								FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X,
													  y);
								FrontendButton_DrawSpriteAndTooltip(
									&rect, NULL, FrontendString_Get((FrontendStringId)(awardId + 659)),
									MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}
	if (g_pilotMpTrainingHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerTrainingMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpTrainingMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .mpTrainingMissions[g_pilotRecordMultiplayerTrainingMissionList[listIndex]
														  .missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilotData.currentFactionId != 0) {
							awardSpriteFormat = "citlvl%d";
						} else {
							awardSpriteFormat = "rcitlvl%d";
						}
						sprintf(g_frontendScratchBuffer, awardSpriteFormat, awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpMeleeHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMeleeMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .mpMeleeMissions[g_pilotRecordMeleeMissionList[listIndex].missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontendScratchBuffer, "medlvl%d", awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpTournamentHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordTournamentMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .mpTournaments[g_pilotRecordTournamentMissionList[listIndex].missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontendScratchBuffer, "medlvl%d", awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpCombatHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCombatMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCombatMissions[g_pilotRecordMultiplayerCombatMissionList[listIndex].missionIdx]
					.numberTimesFlown != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId =
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpCombatMissions[g_pilotRecordMultiplayerCombatMissionList[listIndex].missionIdx]
							.awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilotData.currentFactionId != 0) {
							awardSpriteFormat = "citlvl%d";
						} else {
							awardSpriteFormat = "rcitlvl%d";
						}
						sprintf(g_frontendScratchBuffer, awardSpriteFormat, awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpBattleHistoryCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_battleMissionListCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpBattles[g_battleMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								  .mpBattles[g_battleMissionList[listIndex].missionIdx]
								  .awardId;
					if (awardId != 0) {
						FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
												MISSION_ACHIEVEMENT_AWARD_RIGHT,
												y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontendScratchBuffer, "medlvl%d", awardId);
						FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X, y);
						FrontendButton_DrawSpriteAndTooltip(
							&rect, NULL,
							FrontendString_Get(
								(FrontendStringId)(awardId + MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilotMpCampaignHistoryRowCount != 0) {
		if (hasPrevious != 0) {
			if (g_pilotAchievementsScrollOffset <= row &&
				row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		hasPrevious = 1;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilotAchievementsScrollOffset <= row &&
			row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (listIndex = 0; listIndex < g_pilotRecordMultiplayerCampaignMissionCount; ++listIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCampaigns[g_pilotRecordMultiplayerCampaignMissionList[listIndex].missionIdx]
					.attemptCount != 0) {
				if (g_pilotAchievementsScrollOffset <= row &&
					row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (childListIndex = 0; childListIndex < g_pilotRecordMultiplayerTrainingMissionCount;
					 ++childListIndex) {
					awardId = g_pilotRecordMultiplayerTrainingMissionList[childListIndex].missionIdx;
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpCampaignMissions[awardId - 1]
								.numberTimesFlown != 0 &&
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpCampaignMissions[awardId - 1]
								.campaignId ==
							g_pilotRecordMultiplayerCampaignMissionList[listIndex].missionIdx) {
						if (g_pilotAchievementsScrollOffset <= row &&
							row - g_pilotAchievementsScrollOffset < MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							awardId = g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										  .mpCampaignMissions[awardId - 1]
										  .awardId;
							if (awardId != 0) {
								FrontendDraw_RectAssign(&rect, MISSION_ACHIEVEMENT_HEADER_X, y,
														MISSION_ACHIEVEMENT_AWARD_RIGHT,
														y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
								if (g_pilotData.currentFactionId != 0) {
									awardSpriteFormat = "citlvl%d";
								} else {
									awardSpriteFormat = "rcitlvl%d";
								}
								sprintf(g_frontendScratchBuffer, awardSpriteFormat, awardId);
								FrontImage_DrawSprite(g_frontendScratchBuffer, MISSION_ACHIEVEMENT_HEADER_X,
													  y);
								FrontendButton_DrawSpriteAndTooltip(
									&rect, NULL, FrontendString_Get((FrontendStringId)(awardId + 659)),
									MISSION_ACHIEVEMENT_FONT_SIZE, MISSION_ACHIEVEMENT_TEXT_COLOR);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}
	return 1;
}

// FUNCTION: XVT 0x4C7CC0
int PilotRecord_DrawCutsceneViewerPage(void) {
	RECT rect;
	RECT textRect;
	RECT previousClipRect;
	MissionListEntry* campaignEntry;
	int campaignIndex;
	unsigned int cutsceneIndex;
	unsigned int searchIndex;
	int mouseX;
	int mouseY;
	int hasCampaignCutscene;
	int selectedCutscene;
	int y;
	int thumbnailX;
	int thumbnailHeight;

	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&textRect, 84, 90, 404, 106);
	if (g_pilotData.name[0] == '\0') {
		sprintf(g_frontendScratchBuffer, "%s", FrontendString_Get(FRONTSTR_796_VIEW_CUTSCENES));
	} else {
		sprintf(g_frontendScratchBuffer, "%s: %c%s %c%s", FrontendString_Get(FRONTSTR_796_VIEW_CUTSCENES), 6,
				g_pilotData.ratingName, 4, g_pilotData.name);
	}
	FrontendText_DrawCentered(15, g_frontendScratchBuffer, &textRect, 0xFFFF);
	if (g_pilotData.name[0] == '\0')
		return 0;
	if (g_cutsceneTable == NULL)
		return 0;

	if (g_concourseRedrawRequested != 0) {
		if (g_pilotRecordSingleplayerCampaignMissionList != NULL) {
			free(g_pilotRecordSingleplayerCampaignMissionList);
			g_pilotRecordSingleplayerCampaignMissionList = NULL;
		}
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_CAMPAIGNS);
		g_pilotRecordSingleplayerCampaignMissionList = g_missionList;
		g_pilotRecordSingleplayerCampaignMissionCount = g_missionCount;
		g_missionList = NULL;
		for (campaignIndex = 0; campaignIndex < g_pilotRecordSingleplayerCampaignMissionCount;
			 ++campaignIndex) {
			searchIndex = (unsigned int)strlen(
							  g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].description) +
						  1;
			searchIndex -= 2;
			if (searchIndex != 0) {
				campaignEntry = &g_pilotRecordSingleplayerCampaignMissionList[campaignIndex];
				do {
					if (campaignEntry->description[searchIndex] == '(') {
						campaignEntry->description[searchIndex] = '\0';
						break;
					}
					--searchIndex;
				} while (searchIndex != 0);
			}
		}

		g_cutsceneViewerScrollRow = 0;
		g_cutsceneViewerTotalRows = 0;
		for (campaignIndex = 0; campaignIndex < g_pilotRecordSingleplayerCampaignMissionCount;
			 ++campaignIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].missionIdx]
						.attemptCount == 0 &&
				g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.mpCampaigns[g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].missionIdx]
						.attemptCount == 0) {
				continue;
			}
			hasCampaignCutscene = 0;
			for (cutsceneIndex = 0; cutsceneIndex < (unsigned int)g_cutsceneCount; ++cutsceneIndex) {
				if (g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].missionIdx !=
					g_cutsceneTable[cutsceneIndex].missionIdx)
					continue;
				if (g_cutsceneTable[cutsceneIndex].playAfterDebriefing == 0) {
					if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.spCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId - 1]
								.numberTimesFlown == 0 &&
						g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.mpCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId - 1]
								.numberTimesFlown == 0) {
						continue;
					}
				} else if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								   .spCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId -
													   1]
								   .isCompleted == 0 &&
						   g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								   .mpCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId -
													   1]
								   .isCompleted == 0) {
					continue;
				}
				if (hasCampaignCutscene == 0) {
					hasCampaignCutscene = 1;
					g_cutsceneViewerTotalRows += 2;
				}
				++g_cutsceneViewerTotalRows;
				FrontImage_GetResourceRect(g_cutsceneTable[cutsceneIndex].thumbnailSprite, &rect);
				g_cutsceneViewerTotalRows +=
					(unsigned int)(rect.bottom - rect.top + CUTSCENE_VIEWER_ROW_HEIGHT + 3) /
					CUTSCENE_VIEWER_ROW_HEIGHT;
			}
		}
		g_concourseRedrawRequested = 0;
	}

	FrontendDraw_RectAssign(&textRect, 425, 107, 434, 433);
	if ((unsigned int)g_cutsceneViewerTotalRows > CUTSCENE_VIEWER_VISIBLE_ROW_COUNT) {
		g_cutsceneViewerScrollRow =
			FrontendScrollbar_Draw(&textRect, g_cutsceneViewerScrollRow, g_cutsceneViewerTotalRows, 0, 5,
								   (unsigned int)g_colorNavy, 11);
	}
	FrontendDisplay_GetScreenClipRect(&previousClipRect);
	FrontendDraw_RectAssign(&textRect, 88, 111, 424, 433);
	FrontendDisplay_SetScreenClipRect640x480(&textRect);
	selectedCutscene = 0;
	y = 111 - CUTSCENE_VIEWER_ROW_HEIGHT * g_cutsceneViewerScrollRow;
	FrontendDraw_RectAssign(&textRect, 88, y, 424, y + CUTSCENE_VIEWER_ROW_HEIGHT - 1);
	for (campaignIndex = 0; campaignIndex < g_pilotRecordSingleplayerCampaignMissionCount; ++campaignIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCampaigns[g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].missionIdx]
					.attemptCount == 0 &&
			g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.mpCampaigns[g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].missionIdx]
					.attemptCount == 0) {
			continue;
		}
		hasCampaignCutscene = 0;
		for (cutsceneIndex = 0; cutsceneIndex < (unsigned int)g_cutsceneCount; ++cutsceneIndex) {
			if (g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].missionIdx !=
				g_cutsceneTable[cutsceneIndex].missionIdx)
				continue;
			if (g_cutsceneTable[cutsceneIndex].playAfterDebriefing == 0) {
				if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId - 1]
							.numberTimesFlown == 0 &&
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId - 1]
							.numberTimesFlown == 0) {
					continue;
				}
			} else if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							   .spCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId - 1]
							   .isCompleted == 0 &&
					   g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							   .mpCampaignMissions[g_cutsceneTable[cutsceneIndex].missionDescriptionId - 1]
							   .isCompleted == 0) {
				continue;
			}
			if (hasCampaignCutscene == 0) {
				hasCampaignCutscene = 1;
				FrontendText_DrawCentered(
					12, g_pilotRecordSingleplayerCampaignMissionList[campaignIndex].description, &textRect,
					g_colorLightBlue);
				y += 30;
				FrontendDraw_RectOffsetXY(&textRect, 0, 30);
			}
			FrontendText_DrawCentered(12, g_cutsceneTable[cutsceneIndex].description, &textRect, 0xFFFF);
			y += CUTSCENE_VIEWER_ROW_HEIGHT;
			FrontendDraw_RectOffsetXY(&textRect, 0, CUTSCENE_VIEWER_ROW_HEIGHT);
			FrontImage_GetResourceRect(g_cutsceneTable[cutsceneIndex].thumbnailSprite, &rect);
			thumbnailX = 256 - ((unsigned int)(rect.right - rect.left + 1) >> 1);
			thumbnailHeight = CUTSCENE_VIEWER_ROW_HEIGHT *
							  ((unsigned int)(rect.bottom - rect.top + CUTSCENE_VIEWER_ROW_HEIGHT + 3) /
							   CUTSCENE_VIEWER_ROW_HEIGHT);
			FrontImage_DrawSpriteOpaque(g_cutsceneTable[cutsceneIndex].thumbnailSprite, thumbnailX, y + 3);
			FrontendDraw_RectOffsetXY(&rect, thumbnailX, y + 3);
			if (FrontendDraw_PointInRect(&rect, mouseX, mouseY)) {
				FrontendDraw_RectOutline(&rect, 0, 0, g_colorGreen);
				if (FrontendButton_DrawSpriteHitTest(&rect, "", "", NULL, 15, 0xFFFF, 36, "jewelsound") !=
					0) {
					selectedCutscene = (int)cutsceneIndex + 1;
				}
			}
			y += thumbnailHeight;
			FrontendDraw_RectOffsetXY(&textRect, 0, thumbnailHeight);
			if (y > 433)
				break;
		}
		if (y > 433)
			break;
	}
	FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
	if (selectedCutscene != 0) {
		CDAudio_SuspendPlayback();
		FrontendDisplay_DisableOffscreenRestore();
		FrontendDisplay_UnlockBackBuffer();
		FrontendDisplay_ClearBackBuffer();
		FrontendDisplay_PresentFrame();
		FrontendDisplay_ClearBackBuffer();
#ifdef XVT_MODERN
		if (XvtFrontendMovies_PlayViewer(g_cutsceneTable[selectedCutscene - 1].movieName))
			return 1;
#else
		Movie_Play(g_cutsceneTable[selectedCutscene - 1].movieName, 0);
#endif
		FrontendDisplay_ClearBackBuffer();
		FrontendDisplay_PresentFrame();
		FrontendDisplay_ClearBackBuffer();
		FrontendDisplay_EnableOffscreenRestore();
		PilotRecord_RedrawBackground();
		CDAudio_RequestResumePlayback();
	}
	return 1;
}

// FUNCTION: XVT 0x4C83A0
int PilotRecord_DrawCampaignMedalsPage(void) {
	RECT rect;
	RECT awardRect;
	MissionListEntry* campaignEntry;
	int* campaignId;
	unsigned int missionIndex;
	unsigned int trainingMissionIndex;
	unsigned int searchIndex;
	unsigned int awardSpriteIndex;
	unsigned int awardIndex;
	unsigned int remainingCampaignCount;
	int campaignMissionId;
	int eligibleCampaignIndex;
	int awardX;
	int awardY;

	FrontendDraw_RectAssign(&rect, 84, 90, 404, 106);
	if (g_pilotData.name[0] == '\0') {
		sprintf(g_frontendScratchBuffer, "%s", FrontendString_Get(FRONTSTR_826_CAMPAIGN_MEDALS));
	} else {
		sprintf(g_frontendScratchBuffer, "%s: %c%s %c%s", FrontendString_Get(FRONTSTR_826_CAMPAIGN_MEDALS), 6,
				g_pilotData.ratingName, 4, g_pilotData.name);
	}
	FrontendText_DrawCentered(15, g_frontendScratchBuffer, &rect, 0xFFFF);
	if (g_pilotData.name[0] == '\0')
		return 0;
	if (g_campaignAwardSprites == NULL)
		return 0;

	if (g_concourseRedrawRequested != 0) {
		if (g_pilotRecordSingleplayerCampaignMissionList != NULL) {
			free(g_pilotRecordSingleplayerCampaignMissionList);
			g_pilotRecordSingleplayerCampaignMissionList = NULL;
		}
		if (g_pilotRecordSingleplayerTrainingMissionList != NULL) {
			free(g_pilotRecordSingleplayerTrainingMissionList);
			g_pilotRecordSingleplayerTrainingMissionList = NULL;
		}
		if (g_pilotRecordMultiplayerTrainingMissionList != NULL) {
			free(g_pilotRecordMultiplayerTrainingMissionList);
			g_pilotRecordMultiplayerTrainingMissionList = NULL;
		}

		MissionSetup_LoadMissionList(MISSION_DIRECTORY_CAMPAIGNS);
		g_pilotRecordSingleplayerCampaignMissionList = g_missionList;
		g_pilotRecordSingleplayerCampaignMissionCount = g_missionCount;
		g_missionList = NULL;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_pilotRecordSingleplayerTrainingMissionList = g_missionList;
		g_pilotRecordSingleplayerTrainingMissionCount = g_missionCount;
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
		g_missionList = NULL;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		g_pilotRecordMultiplayerTrainingMissionList = g_missionList;
		g_pilotRecordMultiplayerTrainingMissionCount = g_missionCount;
		g_missionList = NULL;

		memset(g_campaignSingleplayerAwardFlags, 0, sizeof(g_campaignSingleplayerAwardFlags));
		memset(g_campaignMultiplayerAwardFlags, 0, sizeof(g_campaignMultiplayerAwardFlags));
		missionIndex = 0;
		if (missionIndex < g_pilotRecordSingleplayerCampaignMissionCount) {
			do {
				searchIndex = (unsigned int)strlen(
								  g_pilotRecordSingleplayerCampaignMissionList[missionIndex].description) +
							  1;
				searchIndex -= 2;
				if (searchIndex != 0) {
					campaignEntry = &g_pilotRecordSingleplayerCampaignMissionList[missionIndex];
					do {
						if (campaignEntry->description[searchIndex] == '(') {
							campaignEntry->description[searchIndex] = '\0';
							break;
						}
						--searchIndex;
					} while (searchIndex != 0);
				}
				++missionIndex;
			} while (missionIndex < g_pilotRecordSingleplayerCampaignMissionCount);
		}

		g_campaignMedalScrollOffset = 0;
		g_campaignMedalEntryCount = 0;
		remainingCampaignCount = g_pilotRecordSingleplayerCampaignMissionCount;
		missionIndex = 0;
		if (remainingCampaignCount) {
			do {
				campaignId = &g_pilotRecordSingleplayerCampaignMissionList[missionIndex].missionIdx;
				if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.spCampaigns[*campaignId]
							.attemptCount != 0 ||
					g_pilotData.factionStatistics[g_pilotData.currentFactionId]
							.mpCampaigns[*campaignId]
							.attemptCount != 0) {
					for (awardSpriteIndex = 0; awardSpriteIndex < g_campaignAwardSpriteCount;
						 ++awardSpriteIndex) {
						if (g_campaignAwardSprites[awardSpriteIndex].campaignId == *campaignId)
							break;
					}
					if (awardSpriteIndex != g_campaignAwardSpriteCount) {
						awardIndex = 0;
						g_campaignSingleplayerAwardCount = 0;
						for (trainingMissionIndex = 0;
							 trainingMissionIndex < g_pilotRecordSingleplayerTrainingMissionCount;
							 ++trainingMissionIndex) {
							campaignMissionId =
								g_pilotRecordSingleplayerTrainingMissionList[trainingMissionIndex].missionIdx;
							if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.spCampaignMissions[campaignMissionId - 1]
										.campaignId == *campaignId &&
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.spCampaignMissions[campaignMissionId - 1]
										.numberTimesFlown != 0) {
								if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.spCampaignMissions[campaignMissionId - 1]
										.awardEligible != 0) {
									g_campaignSingleplayerAwardFlags[awardIndex] = 1;
									++g_campaignSingleplayerAwardCount;
								}
								++awardIndex;
								if (awardIndex >= sizeof(g_campaignSingleplayerAwardFlags) /
													  sizeof(g_campaignSingleplayerAwardFlags[0]))
									break;
							}
						}

						awardIndex = 0;
						g_campaignMultiplayerAwardCount = 0;
						for (trainingMissionIndex = 0;
							 trainingMissionIndex < g_pilotRecordMultiplayerTrainingMissionCount;
							 ++trainingMissionIndex) {
							campaignMissionId =
								g_pilotRecordMultiplayerTrainingMissionList[trainingMissionIndex].missionIdx;
							if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.mpCampaignMissions[campaignMissionId - 1]
										.campaignId == *campaignId &&
								g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.mpCampaignMissions[campaignMissionId - 1]
										.numberTimesFlown != 0) {
								if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
										.mpCampaignMissions[campaignMissionId - 1]
										.awardEligible != 0) {
									g_campaignMultiplayerAwardFlags[awardIndex] = 1;
									++g_campaignMultiplayerAwardCount;
								}
								++awardIndex;
								if (awardIndex >= sizeof(g_campaignMultiplayerAwardFlags) /
													  sizeof(g_campaignMultiplayerAwardFlags[0]))
									break;
							}
						}
						++g_campaignMedalEntryCount;
					}
				}
				++missionIndex;
			} while (--remainingCampaignCount != 0);
		}
		g_concourseRedrawRequested = 0;
	}

	FrontendDraw_RectAssign(&rect, 425, 107, 434, 433);
	if ((unsigned int)g_campaignMedalEntryCount > 1) {
		g_campaignMedalScrollOffset =
			FrontendScrollbar_Draw(&rect, g_campaignMedalScrollOffset, g_campaignMedalEntryCount, 0, 5,
								   (unsigned int)g_colorNavy, 12);
	}

	eligibleCampaignIndex = 0;
	missionIndex = 0;
	if (g_pilotRecordSingleplayerCampaignMissionCount != 0) {
		do {
			campaignId = &g_pilotRecordSingleplayerCampaignMissionList[missionIndex].missionIdx;
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.spCampaigns[*campaignId]
						.attemptCount != 0 ||
				g_pilotData.factionStatistics[g_pilotData.currentFactionId]
						.mpCampaigns[*campaignId]
						.attemptCount != 0) {
				for (awardSpriteIndex = 0; awardSpriteIndex < g_campaignAwardSpriteCount;
					 ++awardSpriteIndex) {
					if (g_campaignAwardSprites[awardSpriteIndex].campaignId == *campaignId)
						break;
				}
				if (awardSpriteIndex != g_campaignAwardSpriteCount) {
					if (eligibleCampaignIndex == g_campaignMedalScrollOffset) {
						FrontendDraw_RectAssign(&rect, 88, 111, 424, 125);
						FrontendText_DrawCentered(
							12, g_pilotRecordSingleplayerCampaignMissionList[missionIndex].description, &rect,
							g_colorLightBlue);
						FrontImage_GetResourceRect(
							g_campaignAwardSprites[awardSpriteIndex].mainAwardSpriteName, &awardRect);
						awardX = 256 - ((awardRect.right - awardRect.left + 1) >> 1);
						awardY = 279 - ((awardRect.bottom - awardRect.top + 1) >> 1);
						FrontImage_DrawSprite(g_campaignAwardSprites[awardSpriteIndex].mainAwardSpriteName,
											  awardX, awardY);
						if (g_campaignSingleplayerAwardCount != 0) {
							for (awardIndex = 0; awardIndex < CAMPAIGN_AWARD_VISIBLE_FLAG_COUNT;
								 ++awardIndex) {
								if (g_campaignSingleplayerAwardFlags[awardIndex] != 0) {
									FrontImage_DrawSprite(
										g_campaignAwardSprites[awardSpriteIndex]
											.singleplayerMissionAwardSpriteNames[awardIndex],
										awardX, awardY);
								}
							}
						}
						if (g_campaignMultiplayerAwardCount != 0) {
							for (awardIndex = 0; awardIndex < CAMPAIGN_AWARD_VISIBLE_FLAG_COUNT;
								 ++awardIndex) {
								if (g_campaignMultiplayerAwardFlags[awardIndex] != 0) {
									FrontImage_DrawSprite(g_campaignAwardSprites[awardSpriteIndex]
															  .multiplayerMissionAwardSpriteNames[awardIndex],
														  awardX, awardY);
								}
							}
						}
						break;
					}
					++eligibleCampaignIndex;
				}
			}
			++missionIndex;
		} while (missionIndex < g_pilotRecordSingleplayerCampaignMissionCount);
	}
	return 1;
}

// FUNCTION: XVT 0x4C8990
int PilotRecord_DrawPilotAwardsPage(void) {
	RECT rect;
	int y;
	int awardIndex;
	int factionId;

	FrontendDraw_RectAssign(&rect, 84, 90, 404, 106);
	if (g_pilotData.name[0] == '\0') {
		sprintf(g_frontendScratchBuffer, "%s", FrontendString_Get(FRONTSTR_009_PILOT_AWARDS));
	} else {
		sprintf(g_frontendScratchBuffer, "%s: %c%s %c%s", FrontendString_Get(FRONTSTR_009_PILOT_AWARDS), 6,
				g_pilotData.ratingName, 4, g_pilotData.name);
	}
	FrontendText_DrawCentered(15, g_frontendScratchBuffer, &rect, 0xFFFF);
	if (g_pilotData.name[0] == '\0') {
		return 0;
	}
	FrontendDraw_RectAssign(&rect, 268, 118, 424, 132);
	FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_379_TOURNAMENT_TROPHY), &rect, 0xFFFF);
	y = 133;
	FrontendDraw_RectAssign(&rect, 353, 133, 387, 146);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].tournamentTrophies[awardIndex] != 0) {
			sprintf(g_frontendScratchBuffer, "medlvl%d", (int)awardIndex + 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 353, y);
			sprintf(
				g_frontendScratchBuffer, "%d",
				g_pilotData.factionStatistics[g_pilotData.currentFactionId].tournamentTrophies[awardIndex]);
			FrontendText_Draw(10, g_frontendScratchBuffer, 403, y + 1, 0xFFFF);
		}
		y += 22;
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}

	FrontendDraw_RectAssign(&rect, 97, 118, 253, 132);
	FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_378_MELEE_PLAQUE), &rect, 0xFFFF);
	y = 133;
	FrontendDraw_RectAssign(&rect, 182, 133, 216, 146);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].meleePlaques[awardIndex] != 0) {
			sprintf(g_frontendScratchBuffer, "medlvl%d", (int)awardIndex + 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 182, y);
			sprintf(g_frontendScratchBuffer, "%d",
					g_pilotData.factionStatistics[g_pilotData.currentFactionId].meleePlaques[awardIndex]);
			FrontendText_Draw(10, g_frontendScratchBuffer, 232, y + 1, 0xFFFF);
		}
		y += 22;
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}

	FrontendDraw_RectAssign(&rect, 268, 269, 424, 283);
	FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_381_BATTLE_MEDALLION), &rect, 0xFFFF);
	y = 284;
	FrontendDraw_RectAssign(&rect, 353, 284, 387, 297);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].battleMedallions[awardIndex] != 0) {
			if (g_pilotData.currentFactionId == 0)
				factionId = g_pilotData.currentFactionId;
			sprintf(g_frontendScratchBuffer, "medlvl%d", (int)awardIndex + 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 353, y);
			factionId = g_pilotData.currentFactionId;
			sprintf(g_frontendScratchBuffer, "%d",
					g_pilotData.factionStatistics[factionId].battleMedallions[awardIndex]);
			FrontendText_Draw(10, g_frontendScratchBuffer, 403, y + 1, 0xFFFF);
		}
		y += 22;
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}

	FrontendDraw_RectAssign(&rect, 97, 269, 253, 283);
	FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_380_MISSION_EVALUATION), &rect, 0xFFFF);
	y = 284;
	FrontendDraw_RectAssign(&rect, 182, 284, 216, 297);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionEvaluations[awardIndex] != 0) {
			if (g_pilotData.currentFactionId == 0)
				sprintf(g_frontendScratchBuffer, "rcitlvl%d", (int)awardIndex + 1);
			else
				sprintf(g_frontendScratchBuffer, "citlvl%d", (int)awardIndex + 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 182, y);
			sprintf(
				g_frontendScratchBuffer, "%d",
				g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionEvaluations[awardIndex]);
			FrontendText_Draw(10, g_frontendScratchBuffer, 232, y + 1, 0xFFFF);
		}
		y += 22;
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}

	FrontendDraw_RectAssign(&rect, 353, 133, 387, 146);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].tournamentTrophies[awardIndex] != 0) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, NULL, FrontendString_Get((FrontendStringId)(FRONTSTR_382_GOLD + awardIndex)), 12,
				0xFFFF);
		}
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}
	FrontendDraw_RectAssign(&rect, 182, 133, 216, 146);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].meleePlaques[awardIndex] != 0) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, NULL, FrontendString_Get((FrontendStringId)(FRONTSTR_382_GOLD + awardIndex)), 12,
				0xFFFF);
		}
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}
	FrontendDraw_RectAssign(&rect, 353, 284, 387, 297);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		factionId = g_pilotData.currentFactionId;
		if (g_pilotData.factionStatistics[factionId].battleMedallions[awardIndex] != 0) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, NULL, FrontendString_Get((FrontendStringId)(FRONTSTR_382_GOLD + awardIndex)), 12,
				0xFFFF);
		}
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}
	FrontendDraw_RectAssign(&rect, 182, 284, 216, 297);
	for (awardIndex = 0; awardIndex < 6; ++awardIndex) {
		factionId = g_pilotData.currentFactionId;
		if (g_pilotData.factionStatistics[factionId].missionEvaluations[awardIndex] != 0) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, NULL,
				FrontendString_Get((FrontendStringId)(FRONTSTR_660_TOP_PERFORMANCE + awardIndex)), 12,
				0xFFFF);
		}
		FrontendDraw_RectOffsetXY(&rect, 0, 22);
	}
	return 1;
}

// FUNCTION: XVT 0x4C90C0
int PilotRecord_DrawPilotRatingPage(void) {
	RECT rect;
	int ratingIndex;
	int mouseX;
	int mouseY;

	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&rect, 84, 90, 404, 106);
	if (g_pilotData.name[0] == '\0')
		sprintf(g_frontendScratchBuffer, "%s", FrontendString_Get(FRONTSTR_008_PILOT_RATING));
	else
		sprintf(g_frontendScratchBuffer, "%s: %c%s %c%s", FrontendString_Get(FRONTSTR_008_PILOT_RATING), 6,
				g_pilotData.ratingName, 4, g_pilotData.name);
	FrontendText_DrawCentered(15, g_frontendScratchBuffer, &rect, 0xFFFF);
	if (g_pilotData.name[0] == '\0')
		return 0;
	if ((unsigned)g_pilotData.rating >= PILOT_RATING_FLIGHT_CADET) {
		FrontImage_DrawSprite("cadet", 270, 349);
		FrontendDraw_RectAssign(&rect, 270, 334, 415, 348);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_393_CADET), &rect, 0xFFFF);
	}
	if ((unsigned)g_pilotData.rating >= PILOT_RATING_OFFICER_4TH_CLASS) {
		FrontImage_DrawSprite("officer", 103, 349);
		FrontendDraw_RectAssign(&rect, 103, 334, 248, 348);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_394_OFFICER), &rect, 0xFFFF);
	}
	if ((unsigned)g_pilotData.rating >= PILOT_RATING_VETERAN_4TH_GRADE) {
		FrontImage_DrawSprite("veteran", 270, 274);
		FrontendDraw_RectAssign(&rect, 270, 259, 415, 273);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_395_VETERAN), &rect, 0xFFFF);
	}
	if ((unsigned)g_pilotData.rating >= PILOT_RATING_ACE_4TH_LEVEL) {
		FrontImage_DrawSprite("ace", 103, 274);
		FrontendDraw_RectAssign(&rect, 103, 259, 248, 273);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_396_ACE), &rect, 0xFFFF);
	}
	if ((unsigned)g_pilotData.rating >= PILOT_RATING_TOP_ACE_4TH_ORDER) {
		FrontImage_DrawSprite("topace", 270, 199);
		FrontendDraw_RectAssign(&rect, 270, 184, 415, 198);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_397_TOP_ACE), &rect, 0xFFFF);
	}
	if ((unsigned)g_pilotData.rating >= PILOT_RATING_JEDI_4TH_DEGREE) {
		FrontImage_DrawSprite("master", 103, 199);
		FrontendDraw_RectAssign(&rect, 103, 184, 248, 199);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_398_JEDI), &rect, 0xFFFF);
	}
	if ((unsigned)g_pilotData.rating >= PILOT_RATING_JEDI_MASTER) {
		FrontendDraw_RectAssign(&rect, 187, 107, 323, 121);
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_399_JEDI_MASTER), &rect, 0xFFFF);
	}
	if (g_pilotData.totalMissionsPlayedCountPerRating[0] != 0) {
		FrontImage_DrawSprite("rank0", g_pilotRatingIconPos[0].x, g_pilotRatingIconPos[0].y);
		FrontendDraw_RectAssign(&rect, g_pilotRatingIconPos[0].x, g_pilotRatingIconPos[0].y,
								g_pilotRatingIconPos[0].x + 51, g_pilotRatingIconPos[0].y + 11);
		if (FrontendDraw_PointInRect(&rect, mouseX, mouseY)) {
			sprintf(g_frontendScratchBuffer, "%s %s %d", FrontendString_Get(FRONTSTR_122_TARGET_DRONE),
					FrontendString_Get(FRONTSTR_644_ACHIEVED_ON_MISSION),
					g_pilotData.totalMissionsPlayedCountPerRating[0]);
			FrontendButton_DrawSpriteAndTooltip(&rect, NULL, g_frontendScratchBuffer, 12, 0xFFFF);
		}
	}
	if (g_pilotData.totalMissionsPlayedCountPerRating[1] != 0) {
		FrontImage_DrawSprite("rank1", g_pilotRatingIconPos[1].x, g_pilotRatingIconPos[1].y);
		FrontendDraw_RectAssign(&rect, g_pilotRatingIconPos[1].x, g_pilotRatingIconPos[1].y,
								g_pilotRatingIconPos[1].x + 51, g_pilotRatingIconPos[1].y + 11);
		if (FrontendDraw_PointInRect(&rect, mouseX, mouseY)) {
			sprintf(g_frontendScratchBuffer, "%s %s %d", FrontendString_Get(FRONTSTR_122_TARGET_DRONE + 1),
					FrontendString_Get(FRONTSTR_644_ACHIEVED_ON_MISSION),
					g_pilotData.totalMissionsPlayedCountPerRating[1]);
			FrontendButton_DrawSpriteAndTooltip(&rect, NULL, g_frontendScratchBuffer, 12, 0xFFFF);
		}
	}
	for (ratingIndex = PILOT_RATING_TRAINEE; (unsigned)ratingIndex <= (unsigned)g_pilotData.rating;
		 ++ratingIndex) {
		POINT* icon = &g_pilotRatingIconPos[ratingIndex];
		sprintf(g_frontendScratchBuffer, "rank%d", ratingIndex);
		FrontImage_DrawSprite(g_frontendScratchBuffer, icon->x, icon->y);
		if ((unsigned)ratingIndex >= 24)
			FrontendDraw_RectAssign(&rect, icon->x, icon->y, icon->x + 138, icon->y + 59);
		else
			FrontendDraw_RectAssign(&rect, icon->x, icon->y, icon->x + 51, icon->y + 11);
		if (FrontendDraw_PointInRect(&rect, mouseX, mouseY)) {
			sprintf(g_frontendScratchBuffer, "%s %s %d",
					FrontendString_Get(FRONTSTR_122_TARGET_DRONE + ratingIndex),
					FrontendString_Get(FRONTSTR_644_ACHIEVED_ON_MISSION),
					g_pilotData.totalMissionsPlayedCountPerRating[ratingIndex]);
			FrontendButton_DrawSpriteAndTooltip(&rect, NULL, g_frontendScratchBuffer, 12, 0xFFFF);
		}
	}
	return 1;
}

// FUNCTION: XVT 0x4C9660
int PilotRecord_UpdateNavigationControls(void) {
	RECT rect;
	FrontendNavigationSlotState slotStates[8] = {
		FRONTEND_NAVIGATION_SLOT_ACTIVE, FRONTEND_NAVIGATION_SLOT_ACTIVE, FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE, FRONTEND_NAVIGATION_SLOT_ACTIVE, FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE, FRONTEND_NAVIGATION_SLOT_ACTIVE,
	};

	if (g_pilotRecordPage == 5)
		slotStates[7] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	else
		slotStates[g_pilotRecordPage] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	slotStates[5 + g_pilotData.currentFactionId] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	FrontendButton_DrawEightSlotNavigationState(slotStates);

	FrontendDraw_RectAssign(&rect, 22, 334, 42, 358);
	if (g_pilotData.currentFactionId == 1) {
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg7d", FrontendString_Get(FRONTSTR_643_IMPERIAL_PILOT),
											12, 0);
	} else if (FrontendButton_DrawSpriteHitTest(&rect, "reg7u", "reg7u",
												FrontendString_Get(FRONTSTR_643_IMPERIAL_PILOT), 12, 0, 17,
												"jewelsound")) {
		g_concourseRedrawRequested = 1;
		g_pilotData.currentFactionId = 1;
		g_pilotData.team = g_pilotData.factionStatistics[1].team;
		g_pilotData.missionDirectoryId = g_pilotData.factionStatistics[1].missionDirectoryId;
		memcpy(g_pilotData.missionDescriptionIds, g_pilotData.factionStatistics[1].missionDescriptionIds,
			   sizeof(g_pilotData.missionDescriptionIds));
		g_pilotData.missionSequenceActive = g_pilotData.factionStatistics[1].missionSequenceActive;
		g_pilotData.missionSequenceDescriptionId =
			g_pilotData.factionStatistics[1].missionSequenceDescriptionId;
		PilotRecord_RedrawBackground();
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (g_pilotData.currentFactionId != 0) {
		if (FrontendButton_DrawSpriteHitTest(&rect, "reg6u", "reg6u",
											 FrontendString_Get(FRONTSTR_642_REBEL_PILOT), 12, 0, 16,
											 "jewelsound")) {
			g_concourseRedrawRequested = 1;
			g_pilotData.currentFactionId = 0;
			g_pilotData.team = g_pilotData.factionStatistics[0].team;
			g_pilotData.missionDirectoryId = g_pilotData.factionStatistics[0].missionDirectoryId;
			memcpy(g_pilotData.missionDescriptionIds, g_pilotData.factionStatistics[0].missionDescriptionIds,
				   sizeof(g_pilotData.missionDescriptionIds));
			g_pilotData.missionSequenceActive = g_pilotData.factionStatistics[0].missionSequenceActive;
			g_pilotData.missionSequenceDescriptionId =
				g_pilotData.factionStatistics[0].missionSequenceDescriptionId;
			PilotRecord_RedrawBackground();
		}
	} else {
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg6d", FrontendString_Get(FRONTSTR_642_REBEL_PILOT), 12,
											0);
	}

	FrontendDraw_RectAssign(&rect, 22, 254, 42, 278);
	if (g_pilotRecordPage == 5)
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg8d", FrontendString_Get(FRONTSTR_796_VIEW_CUTSCENES),
											12, 0);
	else if (FrontendButton_DrawSpriteHitTest(&rect, "reg8u", "reg8u",
											  FrontendString_Get(FRONTSTR_796_VIEW_CUTSCENES), 12, 0, 18,
											  "jewelsound")) {
		g_concourseRedrawRequested = 1;
		g_pilotRecordPage = 5;
		PilotRecord_RedrawBackground();
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (g_pilotRecordPage == 4)
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg5d", FrontendString_Get(FRONTSTR_826_CAMPAIGN_MEDALS),
											12, 0);
	else if (FrontendButton_DrawSpriteHitTest(&rect, "reg5u", "reg5u",
											  FrontendString_Get(FRONTSTR_826_CAMPAIGN_MEDALS), 12, 0, 15,
											  "jewelsound")) {
		g_concourseRedrawRequested = 1;
		g_pilotRecordPage = 4;
		PilotRecord_RedrawBackground();
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (g_pilotRecordPage == 3)
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg4d",
											FrontendString_Get(FRONTSTR_010_MISSION_ACHIEVEMENTS), 12, 0);
	else if (FrontendButton_DrawSpriteHitTest(&rect, "reg4u", "reg4u",
											  FrontendString_Get(FRONTSTR_010_MISSION_ACHIEVEMENTS), 12, 0,
											  14, "jewelsound")) {
		g_concourseRedrawRequested = 1;
		g_pilotRecordPage = 3;
		PilotRecord_RedrawBackground();
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (g_pilotRecordPage == 2)
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg3d", FrontendString_Get(FRONTSTR_008_PILOT_RATING), 12,
											0);
	else if (FrontendButton_DrawSpriteHitTest(&rect, "reg3u", "reg3u",
											  FrontendString_Get(FRONTSTR_008_PILOT_RATING), 12, 0, 13,
											  "jewelsound")) {
		g_concourseRedrawRequested = 1;
		g_pilotRecordPage = 2;
		PilotRecord_RedrawBackground();
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (g_pilotRecordPage == 1)
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg2d", FrontendString_Get(FRONTSTR_009_PILOT_AWARDS), 12,
											0);
	else if (FrontendButton_DrawSpriteHitTest(&rect, "reg2u", "reg2u",
											  FrontendString_Get(FRONTSTR_009_PILOT_AWARDS), 12, 0, 12,
											  "jewelsound")) {
		g_concourseRedrawRequested = 1;
		g_pilotRecordPage = 1;
		PilotRecord_RedrawBackground();
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (g_pilotRecordPage != 0) {
		if (FrontendButton_DrawSpriteHitTest(&rect, "reg1u", "reg1u",
											 FrontendString_Get(FRONTSTR_007_PILOT_STATISTICS), 12, 0, 11,
											 "jewelsound")) {
			g_concourseRedrawRequested = 1;
			g_pilotRecordPage = 0;
			PilotRecord_RedrawBackground();
		}
	} else {
		FrontendButton_DrawSpriteAndTooltip(&rect, "reg1d", FrontendString_Get(FRONTSTR_007_PILOT_STATISTICS),
											12, 0);
	}
	return 1;
}

// FUNCTION: XVT 0x4CAF60
int PilotRecord_RedrawBackground(void) {
	FrontendDisplay_ClearOffscreenSurface();
	FrontendDisplay_LockOffscreenSurface();
	if (g_pilotData.currentFactionId == 0)
		FrontImage_DrawSpriteOpaque("background0", 0, 0);
	else
		FrontImage_DrawSpriteOpaque("background1", 0, 0);
	FrontImage_DrawSprite("frame", 0, 0);
	if (g_hostCdAvailable != 0)
		FrontImage_DrawSprite("allactive", 0, 0);
	else
		FrontImage_DrawSprite("clientactive", 0, 0);
	FrontImage_DrawSpriteTranslucent("nameoverlay", 0, 0);

	switch (g_pilotRecordPage) {
		case 2:
			FrontImage_DrawSpriteTranslucent("promotionoverlay", 0, 0);
			break;
		case 1:
			FrontImage_DrawSpriteTranslucent("awardoverlay", 0, 0);
			if (g_pilotData.currentFactionId == 0) {
				FrontImage_DrawSprite("rebplaque", 0, 0);
				FrontImage_DrawSprite("rebmedal", 0, 0);
				FrontImage_DrawSprite("rebtrophy", 0, 0);
				FrontImage_DrawSprite("rebcitation", 0, 0);
			} else {
				FrontImage_DrawSprite("impplaque", 0, 0);
				FrontImage_DrawSprite("impmedal", 0, 0);
				FrontImage_DrawSprite("imptrophy", 0, 0);
				FrontImage_DrawSprite("impcitation", 0, 0);
			}
			break;
		case 0:
		case 3:
		case 4:
			FrontImage_DrawSpriteTranslucent("regoverlay", 0, 0);
			break;
		default:
			break;
	}

	FrontendDisplay_UnlockOffscreenSurface(1);
	return 1;
}

// FUNCTION: XVT 0x4CC060
int PilotRecord_LoadCampaignAwardSpriteTable(const char* fileName) {
	XvtFile* stream;
	char* line;
	unsigned int recordCountOrIndex;
	unsigned int multiplayerSpriteBytes;
#ifdef XVT_MODERN
	unsigned int recordCapacity;
#endif

	if (g_campaignAwardSprites != NULL) {
		free(g_campaignAwardSprites);
		g_campaignAwardSprites = NULL;
	}
	stream = File_Open(fileName, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(g_frontendScratchBuffer, 255, stream) == NULL) {
		File_Close(stream);
		return 0;
	}
	recordCountOrIndex = (unsigned int)atoi(g_frontendScratchBuffer);
#ifdef XVT_MODERN
	recordCapacity = recordCountOrIndex;
#endif
	g_campaignAwardSprites =
		(CampaignAwardSpriteEntry*)malloc(sizeof(*g_campaignAwardSprites) * recordCountOrIndex);
	if (g_campaignAwardSprites == NULL) {
#ifdef XVT_MODERN
		File_Close(stream);
#endif
		return 0;
	}
	memset(g_campaignAwardSprites, 0, sizeof(*g_campaignAwardSprites) * recordCountOrIndex);
	g_campaignAwardSpriteCount = 0;

#ifdef XVT_MODERN
	while (g_campaignAwardSpriteCount < recordCapacity) {
#else
	while (1) {
		if (recordCountOrIndex <= g_campaignAwardSpriteCount) {
			break;
		}
#endif
		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
			g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		g_campaignAwardSprites[g_campaignAwardSpriteCount].campaignId = atoi(g_frontendScratchBuffer);

		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				g_campaignAwardSprites[g_campaignAwardSpriteCount].campaignId = 0;
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
			g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		memcpy(g_campaignAwardSprites[g_campaignAwardSpriteCount].mainAwardSpriteName,
			   g_frontendScratchBuffer,
			   sizeof(g_campaignAwardSprites[g_campaignAwardSpriteCount].mainAwardSpriteName));
		g_campaignAwardSprites[g_campaignAwardSpriteCount].mainAwardSpriteName[31] = '\0';

		multiplayerSpriteBytes = 0;
		while (multiplayerSpriteBytes < 15 * 32) {
			do {
				line = File_Gets(g_frontendScratchBuffer, 255, stream);
				if (line == NULL) {
					g_campaignAwardSprites[g_campaignAwardSpriteCount].campaignId = 0;
					File_Close(stream);
					return 1;
				}
			} while (line[0] == '/' && line[1] == '/');

			if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
				g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
			}
			memcpy(
				(char*)g_campaignAwardSprites[g_campaignAwardSpriteCount].multiplayerMissionAwardSpriteNames +
					multiplayerSpriteBytes,
				g_frontendScratchBuffer,
				sizeof(g_campaignAwardSprites[g_campaignAwardSpriteCount]
						   .multiplayerMissionAwardSpriteNames[0]));
			*((char*)g_campaignAwardSprites[g_campaignAwardSpriteCount].multiplayerMissionAwardSpriteNames +
			  multiplayerSpriteBytes + 31) = '\0';
			multiplayerSpriteBytes += 32;
		}

		recordCountOrIndex = 0;
		multiplayerSpriteBytes = 0;
		while (multiplayerSpriteBytes < 15 * 32) {
			do {
				line = File_Gets(g_frontendScratchBuffer, 255, stream);
				if (line == NULL) {
					g_campaignAwardSprites[g_campaignAwardSpriteCount].campaignId = 0;
					File_Close(stream);
					return 1;
				}
			} while (line[0] == '/' && line[1] == '/');

			if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
				g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
			}
			memcpy((char*)g_campaignAwardSprites[g_campaignAwardSpriteCount]
						   .singleplayerMissionAwardSpriteNames +
					   multiplayerSpriteBytes,
				   g_frontendScratchBuffer,
				   sizeof(g_campaignAwardSprites[g_campaignAwardSpriteCount]
							  .singleplayerMissionAwardSpriteNames[0]));
			*((char*)g_campaignAwardSprites[g_campaignAwardSpriteCount].singleplayerMissionAwardSpriteNames +
			  multiplayerSpriteBytes + 31) = '\0';
			multiplayerSpriteBytes += 32;
			++recordCountOrIndex;
		}
		++g_campaignAwardSpriteCount;
	}

	File_Close(stream);
	return 1;
}

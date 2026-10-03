#include "xvt/frontend/frontend.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/assets/model_preview.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/frontend/tech_library.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	FRONTEND_CHAT_LOG_CAPACITY = 1024,
	FRONTEND_CURSOR_BYTES_PER_PIXEL = 2,
	FRONTEND_DATAPAD_MUSIC_TRACK = 7,
	FRONTEND_MUSIC_VOLUME_STEPS = 9,
	AUX_VOLUME_MAX = 65535,
};

/* Controls in g_scrollableControlIds, 0 to 32. Written by
 * Frontend_RegisterScrollableControl, Frontend_UnregisterScrollableControl,
 * Frontend_ResetScrollableControls and FrontendScrollbar_RestoreState. */
// GLOBAL: XVT 0x52C004
int g_scrollableControlCount;
/* Copy of g_scrollableControlCount that FrontendScrollbar_SaveState keeps for
 * FrontendScrollbar_RestoreState. */
// GLOBAL: XVT 0x52C008
int g_scrollableControlCountSaved;
/* Ids of the scrollbars that Tab moves the keyboard focus through, in focus
 * order: entry 0 has the focus and takes Page Up, Page Down and the up and down
 * arrows. */
// GLOBAL: XVT 0x665470
int g_scrollableControlIds[32];
/* Copy of g_scrollableControlIds that FrontendScrollbar_SaveState keeps for
 * FrontendScrollbar_RestoreState. */
// GLOBAL: XVT 0x6654F0
int g_scrollableControlIdsSaved[32];
/* Background color of editable text fields, FrontendDisplay_PackRGB(64, 128,
 * 64); set by Frontend_LoadResources and read by the config screens. */
// GLOBAL: XVT 0xB69CC4
int g_editableFieldBackgroundColor = 0;
/* Gray, FrontendDisplay_PackRGB(96, 96, 96), set by Frontend_LoadResources;
 * mission lists draw unavailable entries in it. */
// GLOBAL: XVT 0xB69CCC
int g_colorGray = 0;
/* Navy, FrontendDisplay_PackRGB(0, 0, 128), set by Frontend_LoadResources;
 * scrollbars are drawn in it. */
// GLOBAL: XVT 0xBB2810
int g_colorNavy = 0;
/* Shared 256-byte text buffer of the frontend: most screens format a line here
 * just before drawing it, and file readers use it for the line being read. */
// GLOBAL: XVT 0xB69D20
char g_frontendScratchBuffer[256] = {0};
/* Pale cyan, FrontendDisplay_PackRGB(196, 252, 252), set by
 * Frontend_LoadResources; read by the button and scrollbar drawing. */
// GLOBAL: XVT 0xB69E24
int g_colorPaleCyan = 0;
/* Green, FrontendDisplay_PackRGB(0, 255, 0), set by Frontend_LoadResources. */
// GLOBAL: XVT 0xB6A2A0
int g_colorGreen = 0;
/* Blue, FrontendDisplay_PackRGB(0, 0, 255), set by Frontend_LoadResources. */
// GLOBAL: XVT 0xB6A2A4
int g_colorBlue = 0;
/* Red, FrontendDisplay_PackRGB(255, 0, 0), set by Frontend_LoadResources. */
// GLOBAL: XVT 0xB69E38
int g_colorRed = 0;
/* Yellow, FrontendDisplay_PackRGB(255, 255, 0), set by Frontend_LoadResources;
 * the most used text color. */
// GLOBAL: XVT 0xB6A2C0
int g_colorYellow = 0;
/* Green, FrontendDisplay_PackRGB(0, 255, 0), the same as g_colorGreen; set by
 * Frontend_LoadResources and read by the network game lists. */
// GLOBAL: XVT 0xB6A2D8
int g_colorGreen2 = 0;
/* First line shown of the mission text in its scrolling box; the box's
 * scrollbar sets it, and screens set 0 when they load a new text. Many
 * functions write it, chiefly in mission_setup.c. */
// GLOBAL: XVT 0x52C208
int g_frontendFirstVisibleLine = 0;
/* 1 when the host CD's first training mission is found, else 0; only
 * Frontend_CheckHostCdPresent writes it. Hosting a game needs it. */
// GLOBAL: XVT 0xB6A250
int g_hostCdAvailable = 0;
/* Teal, FrontendDisplay_PackRGB(48, 111, 123), set by Frontend_LoadResources;
 * read by the button and scrollbar drawing. */
// GLOBAL: XVT 0xB6A264
int g_colorTeal = 0;
/* Copy of g_colorRed, set by Frontend_LoadResources; nothing reads it. */
// GLOBAL: XVT 0xB69CF0
int g_colorRed2 = 0;
/* Copy of g_colorNavy, set by Frontend_LoadResources; nothing reads it. */
// GLOBAL: XVT 0xB69CF4
int g_colorNavy2 = 0;
/* Copy of g_colorBlue, set by Frontend_LoadResources; nothing reads it. */
// GLOBAL: XVT 0xB69CF8
int g_colorBlue2 = 0;
/* Copy of g_colorYellow, set by Frontend_LoadResources; nothing reads it. */
// GLOBAL: XVT 0xB69CFC
int g_colorYellow2 = 0;
/* Violet, FrontendDisplay_PackRGB(128, 0, 255), set by Frontend_LoadResources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D00
int g_colorViolet = 0;
/* Spring green, FrontendDisplay_PackRGB(0, 255, 128), set by
 * Frontend_LoadResources; nothing reads it. */
// GLOBAL: XVT 0xB69D04
int g_colorSpringGreen = 0;
/* Copy of g_editableFieldBackgroundColor, set by Frontend_LoadResources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D08
int g_colorMutedGreen2 = 0;
/* Cyan, FrontendDisplay_PackRGB(0, 255, 255), set by Frontend_LoadResources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D0C
int g_colorCyan = 0;
/* Azure, FrontendDisplay_PackRGB(0, 128, 255), set by Frontend_LoadResources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D10
int g_colorAzure = 0;
/* Orange, FrontendDisplay_PackRGB(255, 128, 0), set by Frontend_LoadResources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D14
int g_colorOrange = 0;
/* Twelve shades of yellow for pulsing text, from FrontendDisplay_PackRGB(128,
 * 128, 0) up to (255, 255, 0) at entry 6 and back down to (149, 149, 0); set by
 * Frontend_LoadResources. */
// GLOBAL: XVT 0xB6A270
int g_pulseColorRamp[12] = {0};
/* 1 when the pilot record pages must reload their mission lists and totals
 * before drawing; the concourse and pilot changes set it, and the pages set 0
 * once rebuilt. Many functions write it, chiefly in pilot_record.c. */
// GLOBAL: XVT 0x664F2C
int g_pilotRecordPagesNeedRebuild = 0;
/* 1 when the CD music could not be started and the concourse has not yet said
 * so. Frontend_LoadResources sets it when CDAudio_Initialize fails, the
 * concourse's CD retry loop updates it, and the concourse sets 0 once it has
 * shown the warning. */
// GLOBAL: XVT 0x664EC4
int g_cdAudioWarningPending = 0;
/* Passed to File_CheckGameCdPresent, whose original build checks the CD's movie
 * files only while this is 0. Frontend_LoadResources sets 0 and the concourse's
 * first frame sets 1 after its CD checks; while it is set, the original
 * concourse also skips its movie CD check. */
// GLOBAL: XVT 0xBB2818
int g_skipMovieChecks = 0;

/* Loads the frontend: the mode start function when the intro is skipped, and
 * the last step of the credits otherwise. Sets g_gameMainSkipIntroRelaunchGate
 * to 1 and g_skipMovieChecks to 0, checks for the host CD, sets the display
 * options (Esc does not quit, clear color 0, cursor shown, no clearing after a
 * present, the back buffer refilled from the offscreen surface), loads fonts
 * 15, 12 and 10, the five image lists, the sound list and the cursor image,
 * clears g_pilotData, and loads the string table, the cutscene table and the
 * campaign award images. Allocates and zeroes the 1024-byte chat log, sets the
 * frontend colors, loads the config, takes a pilot name from the command line,
 * loads the last pilot, and starts the CD music: track 7 at the music volume
 * with datapad music on, else stops the track. Returns 0, which lets the
 * frontend start; checks no load result. */
// FUNCTION: XVT 0x4BDB90
int Frontend_LoadResources(void)
{
	RECT cursorRect;

	g_gameMainSkipIntroRelaunchGate = 1;
	g_skipMovieChecks = 0;
	Frontend_CheckHostCdPresent();
	FrontendDisplay_DisableEscapeClose();
	FrontendDisplay_SetSurfaceClearColor(0);
	FrontendCursor_Show();
	FrontendDisplay_DisableClearAfterPresent();
	FrontendDisplay_EnableOffscreenRestore();
	FrontendText_LoadFont(15);
	FrontendText_LoadFont(12);
	FrontendText_LoadFont(10);
	FrontImage_LoadResourceList("frontres\\top.lst");
	FrontImage_LoadResourceList("frontres\\side.lst");
	FrontImage_LoadResourceList("frontres\\awards.lst");
	FrontImage_LoadResourceList("frontres\\promo.lst");
	FrontImage_LoadResourceList("frontres\\icons.lst");
	FrontendSound_LoadList("sfx\\sfx.lst");
	FrontImage_GetResourceRect("cursor", &cursorRect);
	if (g_cursorSaveBuffer != NULL) {
		free(g_cursorSaveBuffer);
		g_cursorSaveBuffer = NULL;
	}
	g_cursorSaveBuffer =
		malloc(FRONTEND_CURSOR_BYTES_PER_PIXEL *
		       (cursorRect.bottom + 1) * (cursorRect.right + 1));
	FrontendCursor_SetImageFromResourceName("cursor", g_cursorSaveBuffer);
#ifdef XVT_MODERN
	sprintf(g_frontendScratchBuffer, "%p\n", (void *)&g_pilotData);
#else
	sprintf(g_frontendScratchBuffer, "%x\n", &g_pilotData);
#endif
	memset(&g_pilotData, 0, sizeof(g_pilotData));
	FrontendString_LoadTable("fronttxt.txt");
	Cutscene_LoadTable("movies\\cutscene.lst");
	PilotRecord_LoadCampaignAwardSpriteTable("frontres\\campawds.lst");
	g_frontendChatLogBuffer = malloc(FRONTEND_CHAT_LOG_CAPACITY);
	g_frontendChatLogUsedBytes = 0;
	if (g_frontendChatLogBuffer != NULL) {
		memset(g_frontendChatLogBuffer, 0, FRONTEND_CHAT_LOG_CAPACITY);
	}

	g_colorGreen2 = FrontendDisplay_PackRGB(0, 255, 0);
	g_colorNavy = FrontendDisplay_PackRGB(0, 0, 128);
	g_editableFieldBackgroundColor = FrontendDisplay_PackRGB(64, 128, 64);
	g_colorGreen = FrontendDisplay_PackRGB(0, 255, 0);
	g_colorRed = FrontendDisplay_PackRGB(255, 0, 0);
	g_colorBlue = FrontendDisplay_PackRGB(0, 0, 255);
	g_colorYellow = FrontendDisplay_PackRGB(255, 255, 0);
	g_colorGray = FrontendDisplay_PackRGB(96, 96, 96);
	g_colorPaleCyan = FrontendDisplay_PackRGB(196, 252, 252);
	g_colorTeal = FrontendDisplay_PackRGB(48, 111, 123);
	g_colorRed2 = g_colorRed;
	g_colorNavy2 = g_colorNavy;
	g_colorBlue2 = g_colorBlue;
	g_colorYellow2 = g_colorYellow;
	g_colorViolet = FrontendDisplay_PackRGB(128, 0, 255);
	g_colorSpringGreen = FrontendDisplay_PackRGB(0, 255, 128);
	g_colorMutedGreen2 = g_editableFieldBackgroundColor;
	g_colorCyan = FrontendDisplay_PackRGB(0, 255, 255);
	g_colorAzure = FrontendDisplay_PackRGB(0, 128, 255);
	g_colorOrange = FrontendDisplay_PackRGB(255, 128, 0);
	g_pulseColorRamp[0] = FrontendDisplay_PackRGB(128, 128, 0);
	g_pulseColorRamp[1] = FrontendDisplay_PackRGB(149, 149, 0);
	g_pulseColorRamp[2] = FrontendDisplay_PackRGB(170, 170, 0);
	g_pulseColorRamp[3] = FrontendDisplay_PackRGB(192, 192, 0);
	g_pulseColorRamp[4] = FrontendDisplay_PackRGB(213, 213, 0);
	g_pulseColorRamp[5] = FrontendDisplay_PackRGB(234, 234, 0);
	g_pulseColorRamp[6] = FrontendDisplay_PackRGB(255, 255, 0);
	g_pulseColorRamp[7] = FrontendDisplay_PackRGB(234, 234, 0);
	g_pulseColorRamp[8] = FrontendDisplay_PackRGB(213, 213, 0);
	g_pulseColorRamp[9] = FrontendDisplay_PackRGB(192, 192, 0);
	g_pulseColorRamp[10] = FrontendDisplay_PackRGB(170, 170, 0);
	g_pulseColorRamp[11] = FrontendDisplay_PackRGB(149, 149, 0);

	Config_Load();
	Pilot_ParseCommandLine(g_cmdLine);
	Pilot_FindAndLoadByName(g_gameConfig.lastPilotName);
	g_cdAudioWarningPending = CDAudio_Initialize() == 0;
	CDAudio_EnableLoopCurrentTrack();
	if (g_gameConfig.datapadMusicEnabled != 0) {
		CDAudio_SetAuxVolume(AUX_VOLUME_MAX * g_gameConfig.musicVolume /
				     FRONTEND_MUSIC_VOLUME_STEPS);
		CDAudio_PlayTrackFromTime(FRONTEND_DATAPAD_MUSIC_TRACK, 0, 0);
	} else {
		CDAudio_StopCurrentTrack();
	}
	return 0;
}

/* Handles the buttons along the top of the frontend screens and the help
 * toggle, and returns 1 only when the player quits the game. screenContext is 0
 * for the concourse and the join and host screens, 1 for the mission setup
 * screens (mission, teams, flights, battle choice and craft selection), 2 for
 * the config screen, 3 for the craft database and 4 for the debriefing; most
 * buttons show only in some contexts. Exit, or Esc, asks first (differently
 * in a network game, during a mission sequence, or plainly), then tells the
 * other players when needed,
 * writes the config and shuts DirectPlay down. Config opens the config screen,
 * or closes it from the config context, then sending the game options to the
 * players when hosting. Join, Host (with the host CD only), Fly solo and Pilots
 * ask first while a game session is in progress, need a selected pilot (except
 * Pilots), set g_frontendMissionSessionMode and switch to the join, host,
 * mission setup or concourse screen; leaving mission setup this way tells the
 * other players and shuts the session down. The craft database button pushes
 * the craft database, or closes it from that context. Returns 0 otherwise; the
 * modern build also returns 0 while one of its dialogs is up. */
// FUNCTION: XVT 0x4BF9B0
int Frontend_HandleCommonScreenControls(int screenContext)
{
	enum {
		SCREEN_CONTEXT_MISSION = 1,
		SCREEN_CONTEXT_CONFIG = 2,
		SCREEN_CONTEXT_TECH_LIBRARY = 3,
		SCREEN_CONTEXT_DEBRIEF = 4,
		NEVER_STORED_SESSION_MODE = 5,
		BUTTON_FONT_SIZE = 12,
		UI_SOUND_PRIORITY = 255,
		UI_SOUND_PAN_CENTER = 63,
		EXIT_HELD_SLOT = 6,
		CONFIG_HELD_SLOT = 5,
		HOST_HELD_SLOT = 4,
		JOIN_HELD_SLOT = 3,
		SOLO_HELD_SLOT = 2,
		CRAFT_HELD_SLOT = 1,
		PILOT_HELD_SLOT = 0,
		CONFIG_PACKET_SIZE = 19 * sizeof(int),
	};

	int actionTriggered;
	int transitionNeedsSessionShutdown;
	int mouseX;
	int mouseY;
	RECT rect;
	RECT screenRect;
	const char *tooltipText;

	transitionNeedsSessionShutdown = 0;
	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&rect, 610, 445, 634, 469);
	if (g_gameConfig.helpOn != 0) {
		FrontImage_DrawSprite("helpdown", 610, 445);
		actionTriggered = FrontendButton_HandleSpriteButton(
			&rect, NULL, NULL,
			FrontendString_Get(FRONTSTR_704_HELP_TEXT_OFF),
			BUTTON_FONT_SIZE, 0, 35, "buttonsound");
	} else {
		FrontImage_DrawSprite("helpup", 610, 445);
		actionTriggered = FrontendButton_HandleSpriteButton(
			&rect, NULL, NULL,
			FrontendString_Get(FRONTSTR_703_HELP_TEXT_ON),
			BUTTON_FONT_SIZE, 0, 35, "buttonsound");
	}
	if (actionTriggered != 0) {
		g_gameConfig.helpOn ^= 1;
	}
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}

	FrontendDraw_RectAssign(&rect, 586, 4, 633, 71);
	FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_568_EXIT));
	if (screenContext < SCREEN_CONTEXT_CONFIG ||
	    screenContext > SCREEN_CONTEXT_TECH_LIBRARY) {
		if (screenContext != SCREEN_CONTEXT_DEBRIEF) {
			actionTriggered = FrontendButton_HandleSpriteButton(
				&rect, NULL, "exitdown",
				FrontendString_Get(
					FRONTSTR_006_EXIT_TO_WINDOWS),
				BUTTON_FONT_SIZE, 0, EXIT_HELD_SLOT,
				"buttonsound");
		} else {
			FrontImage_DrawSprite("configup", 0, 0);
			actionTriggered = FrontendButton_HandleSpriteButton(
				&rect, "exitup", "exitdown",
				FrontendString_Get(
					FRONTSTR_006_EXIT_TO_WINDOWS),
				BUTTON_FONT_SIZE, 0, EXIT_HELD_SLOT,
				"buttonsound");
		}
		if (Keyboard_PeekChar() == 27) {
			actionTriggered = 1;
			Keyboard_DiscardChar();
		}
#ifdef XVT_MODERN
		actionTriggered = XvtFrontendAction_Trigger(
			XVT_ACTION_OWNER_COMMON, 1, actionTriggered);
#endif
		if (actionTriggered != 0) {
			if (g_frontendGameSessionInProgress != 0) {
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (Net_IsHost() != 0) {
						actionTriggered = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
							FrontendString_Get(
								FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
							FrontendString_Get(
								FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
							FrontendString_Get(
								FRONTSTR_523_OKAY),
							FrontendString_Get(
								FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
						if (XvtDialog_IsActive()) {
							return 0;
						}
#endif
					} else {
						actionTriggered = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
							FrontendString_Get(
								FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
							FrontendString_Get(
								FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
							FrontendString_Get(
								FRONTSTR_523_OKAY),
							FrontendString_Get(
								FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
						if (XvtDialog_IsActive()) {
							return 0;
						}
#endif
					}
				} else if (g_pilotData.missionSequenceActive ==
					   1) {
					if (g_pilotData.missionDirectoryId ==
					    MISSION_DIRECTORY_MELEES) {
						actionTriggered = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
							FrontendString_Get(
								FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
							FrontendString_Get(
								FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
							FrontendString_Get(
								FRONTSTR_523_OKAY),
							FrontendString_Get(
								FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
						if (XvtDialog_IsActive()) {
							return 0;
						}
#endif
					} else if (
						g_pilotData
							.missionDirectoryId ==
						MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
						actionTriggered = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
							FrontendString_Get(
								FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
							FrontendString_Get(
								FRONTSTR_683_TERMINATE_THIS_BATTLE),
							FrontendString_Get(
								FRONTSTR_523_OKAY),
							FrontendString_Get(
								FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
						if (XvtDialog_IsActive()) {
							return 0;
						}
#endif
					} else {
						actionTriggered = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
							FrontendString_Get(
								FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
							FrontendString_Get(
								FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
							FrontendString_Get(
								FRONTSTR_523_OKAY),
							FrontendString_Get(
								FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
						if (XvtDialog_IsActive()) {
							return 0;
						}
#endif
					}
				} else {
					actionTriggered = FrontendDialog_ShowConfirmDialog(
						FrontendString_Get(
							FRONTSTR_634_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
						FrontendString_Get(
							FRONTSTR_635_EMPTY_TRANSLATION_PLACEHOLDER),
						FrontendString_Get(
							FRONTSTR_636_EMPTY_TRANSLATION_PLACEHOLDER),
						FrontendString_Get(
							FRONTSTR_523_OKAY),
						FrontendString_Get(
							FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
					if (XvtDialog_IsActive()) {
						return 0;
					}
#endif
				}
			} else {
				actionTriggered = FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(
						FRONTSTR_634_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
					FrontendString_Get(
						FRONTSTR_635_EMPTY_TRANSLATION_PLACEHOLDER),
					FrontendString_Get(
						FRONTSTR_636_EMPTY_TRANSLATION_PLACEHOLDER),
					FrontendString_Get(FRONTSTR_523_OKAY),
					FrontendString_Get(
						FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
				if (XvtDialog_IsActive()) {
					return 0;
				}
#endif
			}
			if (actionTriggered != 0) {
				switch (screenContext) {
				default:
					Config_Write();
					Net_ShutdownDirectPlaySessionForQuit();
					FrontendButton_DisableOverlayText();
					return 1;
				case SCREEN_CONTEXT_MISSION:
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (Net_IsHost() != 0) {
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_HOST_CANCELLED;
							Net_SendPacketAndFlush(
								0,
								&g_frontendNetPacketScratch,
								sizeof(int));
						} else {
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_PLAYER_LEFT;
							Net_SendPacketAndFlush(
								Net_GetHostPlayerId(),
								&g_frontendNetPacketScratch,
								sizeof(int));
						}
					}
					Config_Write();
					Net_ShutdownDirectPlaySessionForQuit();
					FrontendButton_DisableOverlayText();
					return 1;
				case SCREEN_CONTEXT_DEBRIEF:
					if (g_pilotData.missionSequenceActive ==
					    0) {
						if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (Net_IsHost() != 0) {
								g_frontendNetPacketScratch
									.packetType =
									NET_PACKET_SESSION_CANCELLED;
								Net_SendPacketAndFlush(
									0,
									&g_frontendNetPacketScratch,
									sizeof(int));
							} else {
								g_frontendNetPacketScratch
									.packetType =
									NET_PACKET_PLAYER_LEFT;
								Net_SendPacketAndFlush(
									Net_GetHostPlayerId(),
									&g_frontendNetPacketScratch,
									sizeof(int));
							}
						}
					} else if (
						Net_IsHost() == 0 &&
						g_frontendMissionSessionMode !=
							FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_PLAYER_LEFT;
						Net_SendPacketAndFlush(
							Net_GetHostPlayerId(),
							&g_frontendNetPacketScratch,
							sizeof(int));
					} else if (
						g_frontendMissionSessionMode !=
						FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_HOST_CANCELLED;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							sizeof(int));
					}
					Config_Write();
					Net_ShutdownDirectPlaySessionForQuit();
					FrontendButton_DisableOverlayText();
					return 1;
				}
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 503, 4, 581, 56);
	FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_567_CONFIG));
	if (g_frontendMissionSessionMode == NEVER_STORED_SESSION_MODE ||
	    screenContext == SCREEN_CONTEXT_CONFIG) {
		FrontendButton_UsePressedOverlayStyle();
		FrontendButton_DrawSpriteAndTooltip(
			&rect, "configdown",
			FrontendString_Get(FRONTSTR_570_EXIT_CONFIGURATION),
			BUTTON_FONT_SIZE, 0);
		if (FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0 &&
		    (FrontendMouse_GetLeftClick() != 0 ||
		     FrontendMouse_GetRightClick() != 0)) {
			if (g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound(
					"buttonsound", 1, 0, UI_SOUND_PRIORITY,
					12 * g_gameConfig.sfxDatapadVolume,
					UI_SOUND_PAN_CENTER);
			}
			Config_Write();
			g_activeTextFieldId = 0;
			Keyboard_FlushCharBuffer();
			FrontendScreen_PopState();
			FrontendMouse_ClearInputGate();
			FrontImage_FreeResourceByName("backconfig");
			FrontendText_StopTextFade();
			FrontendScrollbar_RestoreState();
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_NET_HOST) {
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_GAME_OPTIONS;
				*(int *)&g_frontendNetPacketScratch.payload[0] =
					g_gameConfig.difficulty;
				*(int *)&g_frontendNetPacketScratch.payload[4] =
					g_gameConfig.collisions;
				*(int *)&g_frontendNetPacketScratch.payload[8] =
					g_gameConfig.craftJumping;
				*(int *)&g_frontendNetPacketScratch
					 .payload[12] =
					g_gameConfig.randomSetup;
				*(int *)&g_frontendNetPacketScratch
					 .payload[16] =
					g_gameConfig.battleLengthIndex;
				*(int *)&g_frontendNetPacketScratch
					 .payload[20] =
					g_gameConfig.requirePassword;
				*(int *)&g_frontendNetPacketScratch
					 .payload[24] =
					g_gameConfig.inProgressJoin;
				*(int *)&g_frontendNetPacketScratch
					 .payload[28] =
					g_gameConfig.craftSelection;
				*(int *)&g_frontendNetPacketScratch
					 .payload[32] =
					g_gameConfig.locatePlayers;
				*(int *)&g_frontendNetPacketScratch
					 .payload[36] = g_gameConfig.craftWaves;
				*(int *)&g_frontendNetPacketScratch
					 .payload[40] =
					g_gameConfig.missionTimeLimit;
				*(int *)&g_frontendNetPacketScratch
					 .payload[44] =
					g_gameConfig.lastTeamTimeLimitMinutes;
				*(int *)&g_frontendNetPacketScratch
					 .payload[48] = rand();
				*(int *)&g_frontendNetPacketScratch
					 .payload[52] =
					g_gameConfig.internetPlay;
				*(int *)&g_frontendNetPacketScratch
					 .payload[56] =
					g_gameConfig.aiOpponents;
				*(int *)&g_frontendNetPacketScratch
					 .payload[60] =
					g_gameConfig.serverUpdateRate;
				*(int *)&g_frontendNetPacketScratch
					 .payload[64] =
					(uint8_t)g_gameConfig.combatBalance;
				*(int *)&g_frontendNetPacketScratch
					 .payload[68] =
					(uint8_t)g_gameConfig
						.continueBattleOrCampaign;
				Net_SendPacketAndFlush(
					0, &g_frontendNetPacketScratch,
					CONFIG_PACKET_SIZE);
			}
		}
	} else if (screenContext < SCREEN_CONTEXT_TECH_LIBRARY ||
		   screenContext > SCREEN_CONTEXT_TECH_LIBRARY) {
		if (FrontendButton_HandleSpriteButton(
			    &rect, NULL, "configdown",
			    FrontendString_Get(FRONTSTR_005_CONFIGURATION),
			    BUTTON_FONT_SIZE, 0, CONFIG_HELD_SLOT,
			    "buttonsound") != 0 &&
		    (screenContext >= 0 &&
		     (screenContext <= SCREEN_CONTEXT_MISSION ||
		      screenContext == SCREEN_CONTEXT_DEBRIEF))) {
			FrontendDraw_RectAssign(&screenRect, 0, 0, 640, 480);
			FrontendScreen_QueuePush(Config_OptionsDatapadUpdate,
						 &screenRect);
		}
	}

	if (screenContext < SCREEN_CONTEXT_CONFIG) {
		FrontendDraw_RectAssign(&rect, 390, 4, 496, 43);
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_003_JOIN_GAME));
		switch (g_gameConfig.networkType) {
		case NET_TRANSPORT_IPX:
			if (g_gameConfig.internetPlay != 0) {
				tooltipText = FrontendString_Get(
					FRONTSTR_727_JOIN_INTERNET_IPX_GAME);
			} else {
				tooltipText = FrontendString_Get(
					FRONTSTR_728_JOIN_LOCAL_IPX_GAME);
			}
			break;
		case NET_TRANSPORT_TCPIP:
			if (g_gameConfig.internetPlay != 0) {
				tooltipText = FrontendString_Get(
					FRONTSTR_729_JOIN_INTERNET_TCP_IP_GAME);
			} else {
				tooltipText = FrontendString_Get(
					FRONTSTR_730_JOIN_LOCAL_TCP_IP_GAME);
			}
			break;
		case NET_TRANSPORT_MODEM:
			tooltipText = FrontendString_Get(
				FRONTSTR_731_JOIN_DIRECT_MODEM_GAME);
			break;
		case NET_TRANSPORT_SERIAL:
			tooltipText = FrontendString_Get(
				FRONTSTR_732_JOIN_DIRECT_SERIAL_GAME);
			break;
		default:
			tooltipText =
				FrontendString_Get(FRONTSTR_003_JOIN_GAME);
			break;
		}
		strcpy(g_frontendScratchBuffer, tooltipText);
		if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_NET_CLIENT) {
			FrontendButton_UsePressedOverlayStyle();
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "joinindown", g_frontendScratchBuffer,
				BUTTON_FONT_SIZE, 0);
		} else {
			actionTriggered = FrontendButton_HandleSpriteButton(
				&rect, NULL, "joinindown",
				g_frontendScratchBuffer, BUTTON_FONT_SIZE, 0,
				JOIN_HELD_SLOT, "buttonsound");
#ifdef XVT_MODERN
			actionTriggered = XvtFrontendAction_Trigger(
				XVT_ACTION_OWNER_COMMON, 2, actionTriggered);
#endif
			if (actionTriggered != 0) {
				if (g_frontendGameSessionInProgress != 0) {
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (Net_IsHost() != 0) {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						} else {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
								FrontendString_Get(
									FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(
									FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						}
					} else if (
						g_pilotData
							.missionSequenceActive ==
						1) {
						if (g_pilotData
							    .missionDirectoryId ==
						    MISSION_DIRECTORY_MELEES) {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
								FrontendString_Get(
									FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
								FrontendString_Get(
									FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						} else if (
							g_pilotData
								.missionDirectoryId ==
							MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
								FrontendString_Get(
									FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
								FrontendString_Get(
									FRONTSTR_683_TERMINATE_THIS_BATTLE),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						} else {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
								FrontendString_Get(
									FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
								FrontendString_Get(
									FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						}
					}
				}
				if (actionTriggered != 0) {
					if (g_pilotData.name[0] != '\0') {
						g_missionSetupIsHost = 0;
						g_missionSetupRosterAuthoritative =
							0;
						if (screenContext !=
						    SCREEN_CONTEXT_MISSION) {
							Net_ShutdownDirectPlaySession();
							g_frontendMissionSessionMode =
								FRONTEND_MISSION_SESSION_NET_CLIENT;
							FrontendScreen_SetCallbacks(
								FrontendNet_JoinGameScreen,
								FrontendMissionList_FreeScreenResources);
						} else {
							transitionNeedsSessionShutdown =
								1;
							g_frontendMissionSessionMode =
								FRONTEND_MISSION_SESSION_NET_CLIENT;
							FrontendScreen_SetCallbacks(
								FrontendNet_JoinGameScreen,
								FrontendMissionList_FreeScreenResources);
						}
					} else {
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_524_YOU_MUST_SELECT_A_PILOT_FROM),
							FrontendString_Get(
								FRONTSTR_525_THE_PILOT_ROSTER_OR_CREATE),
							FrontendString_Get(
								FRONTSTR_526_A_NEW_ONE_BEFORE_CONTINUING),
							NULL, NULL);
#ifdef XVT_MODERN
						if (XvtDialog_IsActive()) {
							return 0;
						}
#endif
					}
				}
			}
		}

		if (g_hostCdAvailable != 0) {
			FrontendDraw_RectAssign(&rect, 257, 4, 389, 42);
			FrontendButton_SetOverlayText(
				FrontendString_Get(FRONTSTR_004_HOST_GAME));
			switch (g_gameConfig.networkType) {
			case NET_TRANSPORT_IPX:
				if (g_gameConfig.internetPlay != 0) {
					tooltipText = FrontendString_Get(
						FRONTSTR_721_HOST_INTERNET_IPX_GAME);
				} else {
					tooltipText = FrontendString_Get(
						FRONTSTR_722_HOST_LOCAL_IPX_GAME);
				}
				break;
			case NET_TRANSPORT_TCPIP:
				if (g_gameConfig.internetPlay != 0) {
					tooltipText = FrontendString_Get(
						FRONTSTR_723_HOST_INTERNET_TCP_IP_GAME);
				} else {
					tooltipText = FrontendString_Get(
						FRONTSTR_724_HOST_LOCAL_TCP_IP_GAME);
				}
				break;
			case NET_TRANSPORT_MODEM:
				tooltipText = FrontendString_Get(
					FRONTSTR_725_HOST_DIRECT_MODEM_GAME);
				break;
			case NET_TRANSPORT_SERIAL:
				tooltipText = FrontendString_Get(
					FRONTSTR_726_HOST_DIRECT_SERIAL_GAME);
				break;
			default:
				tooltipText = FrontendString_Get(
					FRONTSTR_004_HOST_GAME);
				break;
			}
			strcpy(g_frontendScratchBuffer, tooltipText);
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_NET_HOST) {
				FrontendButton_UsePressedOverlayStyle();
				FrontendButton_DrawSpriteAndTooltip(
					&rect, "creategamedown",
					g_frontendScratchBuffer,
					BUTTON_FONT_SIZE, 0);
			} else {
				actionTriggered =
					FrontendButton_HandleSpriteButton(
						&rect, NULL, "creategamedown",
						g_frontendScratchBuffer,
						BUTTON_FONT_SIZE, 0,
						HOST_HELD_SLOT, "buttonsound");
#ifdef XVT_MODERN
				actionTriggered = XvtFrontendAction_Trigger(
					XVT_ACTION_OWNER_COMMON, 3,
					actionTriggered);
#endif
				if (actionTriggered != 0) {
					if (g_frontendGameSessionInProgress !=
					    0) {
						if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (Net_IsHost() != 0) {
								actionTriggered = FrontendDialog_ShowConfirmDialog(
									FrontendString_Get(
										FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
									FrontendString_Get(
										FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
									FrontendString_Get(
										FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
									FrontendString_Get(
										FRONTSTR_523_OKAY),
									FrontendString_Get(
										FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
								if (XvtDialog_IsActive()) {
									return 0;
								}
#endif
							} else {
								actionTriggered = FrontendDialog_ShowConfirmDialog(
									FrontendString_Get(
										FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
									FrontendString_Get(
										FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
									FrontendString_Get(
										FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
									FrontendString_Get(
										FRONTSTR_523_OKAY),
									FrontendString_Get(
										FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
								if (XvtDialog_IsActive()) {
									return 0;
								}
#endif
							}
						} else if (
							g_pilotData
								.missionSequenceActive ==
							1) {
							if (g_pilotData
								    .missionDirectoryId ==
							    MISSION_DIRECTORY_MELEES) {
								actionTriggered = FrontendDialog_ShowConfirmDialog(
									FrontendString_Get(
										FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
									FrontendString_Get(
										FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
									FrontendString_Get(
										FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
									FrontendString_Get(
										FRONTSTR_523_OKAY),
									FrontendString_Get(
										FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
								if (XvtDialog_IsActive()) {
									return 0;
								}
#endif
							} else if (
								g_pilotData
									.missionDirectoryId ==
								MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
								actionTriggered = FrontendDialog_ShowConfirmDialog(
									FrontendString_Get(
										FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
									FrontendString_Get(
										FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
									FrontendString_Get(
										FRONTSTR_683_TERMINATE_THIS_BATTLE),
									FrontendString_Get(
										FRONTSTR_523_OKAY),
									FrontendString_Get(
										FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
								if (XvtDialog_IsActive()) {
									return 0;
								}
#endif
							} else {
								actionTriggered = FrontendDialog_ShowConfirmDialog(
									FrontendString_Get(
										FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
									FrontendString_Get(
										FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
									FrontendString_Get(
										FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
									FrontendString_Get(
										FRONTSTR_523_OKAY),
									FrontendString_Get(
										FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
								if (XvtDialog_IsActive()) {
									return 0;
								}
#endif
							}
						}
					}
					if (actionTriggered != 0) {
						if (g_pilotData.name[0] !=
						    '\0') {
							if (screenContext !=
							    SCREEN_CONTEXT_MISSION) {
								Net_ShutdownDirectPlaySession();
								g_frontendMissionSessionMode =
									FRONTEND_MISSION_SESSION_NET_HOST;
								FrontendScreen_SetCallbacks(
									FrontendNet_HostGameScreen,
									FrontendNet_HostGameExit);
							} else {
								transitionNeedsSessionShutdown =
									1;
								g_frontendMissionSessionMode =
									FRONTEND_MISSION_SESSION_NET_HOST;
								FrontendScreen_SetCallbacks(
									FrontendNet_HostGameScreen,
									FrontendNet_HostGameExit);
							}
						} else {
							FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_524_YOU_MUST_SELECT_A_PILOT_FROM),
								FrontendString_Get(
									FRONTSTR_525_THE_PILOT_ROSTER_OR_CREATE),
								FrontendString_Get(
									FRONTSTR_526_A_NEW_ONE_BEFORE_CONTINUING),
								NULL, NULL);
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						}
					}
				}
			}

			FrontendDraw_RectAssign(&rect, 153, 4, 252, 43);
			FrontendButton_SetOverlayText(
				FrontendString_Get(FRONTSTR_002_FLY_SOLO));
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontendButton_UsePressedOverlayStyle();
				FrontendButton_DrawSpriteAndTooltip(
					&rect, "flysolodown",
					FrontendString_Get(
						FRONTSTR_002_FLY_SOLO),
					BUTTON_FONT_SIZE, 0);
			} else {
				actionTriggered =
					FrontendButton_HandleSpriteButton(
						&rect, "NULL", "flysolodown",
						FrontendString_Get(
							FRONTSTR_002_FLY_SOLO),
						BUTTON_FONT_SIZE, 0,
						SOLO_HELD_SLOT, "buttonsound");
#ifdef XVT_MODERN
				actionTriggered = XvtFrontendAction_Trigger(
					XVT_ACTION_OWNER_COMMON, 4,
					actionTriggered);
#endif
				if (actionTriggered != 0) {
					if (g_frontendGameSessionInProgress !=
						    0 &&
					    g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (Net_IsHost() != 0) {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						} else {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
								FrontendString_Get(
									FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(
									FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						}
					}
					if (actionTriggered != 0) {
						if (g_pilotData.name[0] !=
						    '\0') {
							g_missionSetupIsHost =
								0;
							g_missionSetupRosterAuthoritative =
								0;
							if (screenContext !=
							    SCREEN_CONTEXT_MISSION) {
								Net_ShutdownDirectPlaySession();
								memset(g_mpRoster,
								       0,
								       sizeof(g_mpRoster));
								g_frontendMissionSessionMode =
									FRONTEND_MISSION_SESSION_SINGLEPLAYER;
								FrontendScreen_SetCallbacks(
									MissionSetup_Update,
									MissionSetup_Exit);
							} else {
								transitionNeedsSessionShutdown =
									1;
								g_frontendMissionSessionMode =
									FRONTEND_MISSION_SESSION_SINGLEPLAYER;
								FrontendScreen_SetCallbacks(
									MissionSetup_Update,
									MissionSetup_Exit);
							}
						} else {
							FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_524_YOU_MUST_SELECT_A_PILOT_FROM),
								FrontendString_Get(
									FRONTSTR_525_THE_PILOT_ROSTER_OR_CREATE),
								FrontendString_Get(
									FRONTSTR_526_A_NEW_ONE_BEFORE_CONTINUING),
								NULL, NULL);
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						}
					}
				}
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 63, 4, 147, 57);
	FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_566_CRAFT));
	if (screenContext == SCREEN_CONTEXT_TECH_LIBRARY) {
		FrontendButton_UsePressedOverlayStyle();
		FrontendButton_DrawSpriteAndTooltip(
			&rect, "reviewcraftdown",
			FrontendString_Get(FRONTSTR_571_EXIT_CRAFT_DATABASE),
			BUTTON_FONT_SIZE, 0);
		if (FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0 &&
		    (FrontendMouse_GetLeftClick() != 0 ||
		     FrontendMouse_GetRightClick() != 0)) {
			if (g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound(
					"buttonsound", 1, 0, UI_SOUND_PRIORITY,
					12 * g_gameConfig.sfxDatapadVolume,
					UI_SOUND_PAN_CENTER);
			}
			g_activeTextFieldId = 0;
			Keyboard_FlushCharBuffer();
			FrontendScreen_PopState();
			FrontendMouse_ClearInputGate();
			FrontImage_FreeResourceByName("backreview");
			if (g_missionBriefingCraftSelectionActive == 0) {
				if (g_shipList != NULL) {
					free(g_shipList);
					g_shipList = NULL;
				}
			} else {
				ModelPreview_RestoreState();
			}
			if (g_techLibrarySpecTextTable != NULL) {
				free(g_techLibrarySpecTextTable);
				g_techLibrarySpecTextTable = NULL;
			}
			FrontendText_StopTextFade();
		}
	} else if (screenContext < SCREEN_CONTEXT_CONFIG &&
		   FrontendButton_HandleSpriteButton(
			   &rect, NULL, "reviewcraftdown",
			   FrontendString_Get(FRONTSTR_001_CRAFT_DATABASE),
			   BUTTON_FONT_SIZE, 0, CRAFT_HELD_SLOT,
			   "buttonsound") != 0) {
		FrontendDraw_RectAssign(&screenRect, 0, 0, 640, 480);
		FrontendScreen_QueuePush(TechLibrary_Update, &screenRect);
	}

	if (screenContext < SCREEN_CONTEXT_CONFIG) {
		FrontendDraw_RectAssign(&rect, 9, 4, 62, 71);
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_565_PILOTS));
		if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_NONE) {
			FrontendButton_UsePressedOverlayStyle();
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "pilotregdown",
				FrontendString_Get(FRONTSTR_000_PILOT_RECORDS),
				BUTTON_FONT_SIZE, 0);
		} else {
			actionTriggered = FrontendButton_HandleSpriteButton(
				&rect, NULL, "pilotregdown",
				FrontendString_Get(FRONTSTR_000_PILOT_RECORDS),
				BUTTON_FONT_SIZE, 0, PILOT_HELD_SLOT,
				"buttonsound");
#ifdef XVT_MODERN
			actionTriggered = XvtFrontendAction_Trigger(
				XVT_ACTION_OWNER_COMMON, 5, actionTriggered);
#endif
			if (actionTriggered != 0) {
				if (g_frontendGameSessionInProgress != 0) {
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (Net_IsHost() != 0) {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						} else {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
								FrontendString_Get(
									FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(
									FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						}
					} else if (
						g_pilotData
							.missionSequenceActive ==
						1) {
						if (g_pilotData
							    .missionDirectoryId ==
						    MISSION_DIRECTORY_MELEES) {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
								FrontendString_Get(
									FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
								FrontendString_Get(
									FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						} else if (
							g_pilotData
								.missionDirectoryId ==
							MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
								FrontendString_Get(
									FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
								FrontendString_Get(
									FRONTSTR_683_TERMINATE_THIS_BATTLE),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						} else {
							actionTriggered = FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(
									FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
								FrontendString_Get(
									FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
								FrontendString_Get(
									FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
								FrontendString_Get(
									FRONTSTR_523_OKAY),
								FrontendString_Get(
									FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
							if (XvtDialog_IsActive()) {
								return 0;
							}
#endif
						}
					}
				}
				if (actionTriggered != 0) {
					g_missionSetupIsHost = 0;
					g_missionSetupRosterAuthoritative = 0;
					if (screenContext !=
					    SCREEN_CONTEXT_MISSION) {
						Net_ShutdownDirectPlaySession();
						g_frontendMissionSessionMode =
							FRONTEND_MISSION_SESSION_NONE;
						FrontendScreen_SetCallbacks(
							Concourse_Update,
							Concourse_Exit);
					} else {
						g_frontendMissionSessionMode =
							FRONTEND_MISSION_SESSION_NONE;
						transitionNeedsSessionShutdown =
							1;
						FrontendScreen_SetCallbacks(
							Concourse_Update,
							Concourse_Exit);
					}
				}
			}
		}

		if (screenContext == SCREEN_CONTEXT_MISSION &&
		    transitionNeedsSessionShutdown != 0) {
			if (Net_IsHost() != 0) {
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_HOST_CANCELLED;
				Net_SendPacketAndFlush(
					0, &g_frontendNetPacketScratch,
					sizeof(int));
			} else {
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_PLAYER_LEFT;
				Net_SendPacketAndFlush(
					Net_GetHostPlayerId(),
					&g_frontendNetPacketScratch,
					sizeof(int));
			}
			Net_ShutdownDirectPlaySession();
			memset(g_mpRoster, 0, sizeof(g_mpRoster));
		}
	}

	FrontendButton_DisableOverlayText();
#ifdef XVT_MODERN
	XvtFrontendAction_Finish(XVT_ACTION_OWNER_COMMON);
#endif
	return 0;
}

/* Writes seconds as "MM:SS", or as "HH:MM:SS" from one hour up, each part at
 * least two digits, into g_frontendScratchBuffer, and returns sprintf's
 * result. */
// FUNCTION: XVT 0x4C9BF0
int Frontend_FormatSecondsToClockString(unsigned int seconds)
{
	unsigned int hours;
	unsigned int minutes;
	unsigned int secondsRemainder;

	secondsRemainder = seconds % 60u;
	minutes = seconds / 60u % 60u;
	hours = seconds / 3600u;
	if (hours == 0) {
		return sprintf(g_frontendScratchBuffer, "%02d:%02d", minutes,
			       secondsRemainder);
	}
	return sprintf(g_frontendScratchBuffer, "%02d:%02d:%02d", hours,
		       minutes, secondsRemainder);
}

/* Copies line lineIndex (0 for the first) of xvterr.txt into outText, turning
 * each two-character \n into a newline, and returns 1. Returns 0 when the file
 * does not open or that line is missing. A negative lineIndex returns 0 in the
 * modern build; the original build then reads a pointer from the unset line
 * buffer. It always copies the first 255 bytes of the line buffer, newline and
 * whatever follows the line included, adding no terminator of its own. The
 * original build reads each line with a 512-byte limit into a 256-byte
 * buffer. */
// FUNCTION: XVT 0x4C9E30
int ErrorText_LoadLine(int lineIndex, char *outText)
{
	XvtFile *stream;
	int linesRemaining;
	char *line;
	int i;
	char value;
#ifdef XVT_MODERN
	char buffer[512];
#else
	char buffer[256];
#endif

#ifdef XVT_MODERN
	stream = File_Open("xvterr.txt", "r");
#else
	stream = File_RawOpen("xvterr.txt", "r");
#endif
	if (stream == 0) {
		return 0;
	}
	if (lineIndex >= 0) {
		linesRemaining = lineIndex + 1;
		do {
			line = File_Gets(buffer, 512, stream);
			--linesRemaining;
		} while (linesRemaining != 0);
	} else {
#ifdef XVT_MODERN
		line = 0;
#else
		line = *(char **)buffer;
#endif
	}
#ifdef XVT_MODERN
	File_Close(stream);
#else
	File_RawClose(stream);
#endif
	if (line == 0) {
		return 0;
	}
	for (i = 0; i < 255; ++i) {
		value = buffer[i];
		if (value == '\\' && buffer[i + 1] == 'n') {
			*outText++ = '\n';
			++i;
		} else {
			*outText++ = value;
		}
	}
	return 1;
}

/* Sets g_hostCdAvailable to whether the host CD's first training mission is
 * there, and returns it. The modern build looks for train/1ta01bf.tie among the
 * assets. The original build opens the drive letter followed by
 * \train\1TA01BF.TIE, with no colon after the letter; with no CD drive it
 * returns 0 and leaves g_hostCdAvailable as it was. */
// FUNCTION: XVT 0x4C9EE0
int Frontend_CheckHostCdPresent(void)
{
#ifdef XVT_MODERN
	char path[XVT_PATH_CAPACITY];
	g_hostCdAvailable = XvtStorage_ResolveAsset("train/1ta01bf.tie", path,
						    sizeof(path)) == 1;
	return g_hostCdAvailable;
#else

	char fileName[128] = "c\\train\\1TA01BF.TIE\0";
	char cdDriveLetter;
	XvtFile *stream;
	int result;

	cdDriveLetter = File_GetCdDriveLetter();
	if (cdDriveLetter == 0) {
		return 0;
	}
	fileName[0] = cdDriveLetter;
	stream = File_RawOpen(fileName, "rb");
	if (stream != 0) {
		File_RawClose(stream);
		result = 1;
	} else {
		result = 0;
	}
	g_hostCdAvailable = result;
	return result;

#endif
}

/* Saves the pilot with Pilot_Save(0) and writes the config; returns 1. The
 * network code calls it. */
// FUNCTION: XVT 0x4C9F60
int Frontend_SavePersistentState(void)
{
	Pilot_Save(0);
	Config_Write();
	return 1;
}

/* Returns 1 when controlId is entry 0 of g_scrollableControlIds, the focused
 * control, else 0. Does not check that any control is registered. */
// FUNCTION: XVT 0x4D9B80
int Frontend_IsScrollableControlFocused(int controlId)
{
	controlId -= g_scrollableControlIds[0];
	return !controlId;
}

/* Adds controlId at the end of g_scrollableControlIds unless it is there
 * already. Returns 1, or 0 when 32 are registered. */
// FUNCTION: XVT 0x4D9BA0
int Frontend_RegisterScrollableControl(int controlId)
{
	int count;
	unsigned int index;

	count = g_scrollableControlCount;
	if ((unsigned int)count >= 32u) {
		return 0;
	}

	index = 0;
	while (index < (unsigned int)count) {
		if (g_scrollableControlIds[index] == controlId) {
			return 1;
		}
		++index;
	}

	g_scrollableControlIds[count] = controlId;
	g_scrollableControlCount = count + 1;
	return 1;
}

#ifndef XVT_MODERN
#pragma function(memcpy)
#endif
/* Removes the first entry equal to controlId from g_scrollableControlIds,
 * moving later entries down one, and returns 1; returns 0 when it is not there.
 * The original build moves them with memcpy over overlapping memory; the modern
 * build uses memmove. */
// FUNCTION: XVT 0x4D9BF0
int Frontend_UnregisterScrollableControl(int controlId)
{
	int count;
	unsigned int index;

	index = 0;
	if (index != (unsigned int)g_scrollableControlCount) {
		count = g_scrollableControlCount;
		do {
			if (g_scrollableControlIds[index] == controlId) {
#ifdef XVT_MODERN
				memmove(&g_scrollableControlIds[index],
					&g_scrollableControlIds[index + 1],
					(size_t)(count - index - 1) *
						sizeof(g_scrollableControlIds
							       [0]));
#else
				memcpy(&g_scrollableControlIds[index],
				       &g_scrollableControlIds[index + 1],
				       (size_t)(count - index - 1) *
					       sizeof(g_scrollableControlIds
							      [0]));
#endif
				--g_scrollableControlCount;
				return 1;
			}
			++index;
		} while (index < (unsigned int)count);
	}
	return 0;
}

/* Moves keyboard focus to the next scrollbar: entry 0 of g_scrollableControlIds
 * goes to the end and the rest move down one. Returns 0 when none is
 * registered, else 1. The original build moves them with memcpy over
 * overlapping memory. */
// FUNCTION: XVT 0x4D9C50
int Frontend_CycleScrollableFocus(void)
{
	int firstControlId;

	if (g_scrollableControlCount == 0) {
		return 0;
	}

	firstControlId = g_scrollableControlIds[0];
#ifdef XVT_MODERN
	/* The source and destination overlap, so modern builds require memmove. */
	memmove(g_scrollableControlIds, &g_scrollableControlIds[1],
		(size_t)(g_scrollableControlCount - 1) *
			sizeof(g_scrollableControlIds[0]));
#else
	memcpy(g_scrollableControlIds, &g_scrollableControlIds[1],
	       (size_t)(g_scrollableControlCount - 1) *
		       sizeof(g_scrollableControlIds[0]));
#endif
	g_scrollableControlIds[g_scrollableControlCount - 1] = firstControlId;
	return 1;
}
#ifndef XVT_MODERN
#pragma intrinsic(memcpy)
#endif

/* Forgets every registered scrollbar: sets g_scrollableControlCount to 0.
 * Returns 1. */
// FUNCTION: XVT 0x4D9CA0
int Frontend_ResetScrollableControls(void)
{
	g_scrollableControlCount = 0;
	return 1;
}

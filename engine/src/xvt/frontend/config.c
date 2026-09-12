#include "xvt/frontend/config.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/port.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/config/config.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/credits.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0xBB2820
JoystickEntry g_joystickEntries[128] = { 0 };
// GLOBAL: XVT 0xBB72A0
int g_joystickEntryCount = 0;
// GLOBAL: XVT 0x664E88
int g_configJoystickActionScrollOffset = 0;
// GLOBAL: XVT 0x664E8C
int g_configJoystickButtonScrollOffset = 0;
// GLOBAL: XVT 0x664E90
int g_configSelectedJoystickButtonIndex = 0;
// GLOBAL: XVT 0x664E94
int g_configSelectedJoystickActionIndex = 0;

// GLOBAL: XVT 0xBB72B0
GameConfig g_gameConfig = { 0 };

// GLOBAL: XVT 0x664E98
int g_configDrawStaticControlBackground = 0;
// GLOBAL: XVT 0xB69CE0
int g_unusedFrontendConcourseHostLatch = 0;
// GLOBAL: XVT 0x664E9C
int g_pendingMenuScreen = 0;

// GLOBAL: XVT 0x52A4C8
char* g_configKeywords[] = { "lastpilot",
							 "backdrop1",
							 "stardensity1",
							 "debris1",
							 "locallights1",
							 "specular1",
							 "diffuse1",
							 "dither1",
							 "textureres1",
							 "mipmap1",
							 "lod1",
							 "screenres1",
							 "windowsize1",
							 "bpp1",
							 "brightness1",
							 "backdrop2",
							 "stardensity2",
							 "debris2",
							 "locallights2",
							 "specular2",
							 "diffuse2",
							 "dither2",
							 "textureres2",
							 "mipmap2",
							 "lod2",
							 "screenres2",
							 "windowsize2",
							 "bpp2",
							 "brightness2",
							 "networktype",
							 "phonenumber",
							 "ipaddress",
							 "sfx_exterior",
							 "sfx_interior",
							 "sfx_engine",
							 "sfx_datapad",
							 "voice_pilot",
							 "voice_tactical_officer",
							 "voice_commander",
							 "voice_special",
							 "music",
							 "sfx_datapad_volume",
							 "sfx_exterior_volume",
							 "sfx_interior_volume",
							 "sfx_engine_volume",
							 "voice_volume",
							 "music_volume",
							 "joybutton1",
							 "joybutton2",
							 "joybutton3",
							 "joybutton4",
							 "joybutton5",
							 "joybutton6",
							 "joybutton7",
							 "joybutton8",
							 "joybutton9",
							 "joybutton10",
							 "joybutton11",
							 "joybutton12",
							 "joybutton13",
							 "joybutton14",
							 "joybutton15",
							 "joybutton16",
							 "joybutton17",
							 "joybutton18",
							 "joybutton19",
							 "joybutton20",
							 "joybutton21",
							 "joybutton22",
							 "joybutton23",
							 "joybutton24",
							 "joybutton25",
							 "joybutton26",
							 "joybutton27",
							 "joybutton28",
							 "joybutton29",
							 "joybutton30",
							 "joybutton31",
							 "joybutton32",
							 "difficulty",
							 "collisions",
							 "craft_jumping",
							 "random_setup",
							 "handicapping",
							 "require_password",
							 "in_progress_join",
							 "craft_selection",
							 "locate_players",
							 "craft_waves",
							 "mission_time_limit",
							 "last_time_limit",
							 "random_seed",
							 "password",
							 "async_flag",
							 "ai_opponents",
							 "help_on",
							 "datapad_music",
							 "server_update_rate",
							 "combat_balance",
							 "taunt1",
							 "taunt2",
							 "taunt3",
							 "taunt4",
							 "use_3d_hardware1",
							 "bilinear1",
							 "use_3d_hardware2",
							 "bilinear2",
							 "" };

// FUNCTION: XVT 0x4B7F30
int Config_OptionsDatapadUpdate(int frameState) {
	enum {
		CONFIG_SCREEN_NETWORK = 0,
		CONFIG_SCREEN_SINGLEPLAYER_VIDEO = 1,
		CONFIG_SCREEN_MULTIPLAYER_VIDEO = 2,
		CONFIG_SCREEN_SOUND = 3,
		CONFIG_SCREEN_JOYSTICK = 4,
		CONFIG_SCREEN_TAUNTS = 5,
		CONFIG_SCREEN_CONTEXT = 2,
		CONFIG_PACKET_SIZE = 19 * sizeof(int),
		PILOT_BANNER_ANIMATION_FRAMES = 32,
	};

	int joystickEntryIndex;
	int animationFrame;
	int dismissRequested;
	RECT rect;

	if (frameState == 0) {
		FrontendCursor_SetPos(32, 127);
		FrontendScrollbar_SaveState();
		Frontend_ResetScrollableControls();
		g_configJoystickActionScrollOffset = 0;
		g_configJoystickButtonScrollOffset = 0;
		g_pendingMenuScreen = CONFIG_SCREEN_NETWORK;
		g_configSelectedJoystickButtonIndex = 0;
		g_configDrawStaticControlBackground = 1;
		Keyboard_FlushCharBuffer();
		Config_LoadJoystickActionDictionary();
		for (joystickEntryIndex = 0; joystickEntryIndex < g_joystickEntryCount; ++joystickEntryIndex) {
			if (g_joystickEntries[joystickEntryIndex].actionCode ==
				g_gameConfig.joyButtons[g_configSelectedJoystickButtonIndex]) {
				g_configSelectedJoystickActionIndex = joystickEntryIndex;
			}
		}
		FrontImage_RegisterResourceDefault("frontres\\configb.bmp", "backconfig");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backconfig", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDraw_RectAssign(&rect, 84, 107, 604, 433);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);
	}

	switch (g_pendingMenuScreen) {
		case CONFIG_SCREEN_NETWORK:
			Config_NetworkOptionsScreen();
			if (g_configDrawStaticControlBackground != 0) {
				FrontendDisplay_LockOffscreenSurface();
				FrontendDisplay_UnlockOffscreenSurface(1);
			}
			break;
		case CONFIG_SCREEN_SINGLEPLAYER_VIDEO:
			Config_DrawVideoOptionRows();
			if (g_configDrawStaticControlBackground != 0) {
				FrontendDisplay_LockOffscreenSurface();
				FrontendDisplay_UnlockOffscreenSurface(1);
			}
			break;
		case CONFIG_SCREEN_MULTIPLAYER_VIDEO:
			Config_DrawVideoOptionRows();
			if (g_configDrawStaticControlBackground != 0) {
				FrontendDisplay_LockOffscreenSurface();
				FrontendDisplay_UnlockOffscreenSurface(1);
			}
			break;
		case CONFIG_SCREEN_SOUND:
			Config_SoundOptionsScreen();
			if (g_configDrawStaticControlBackground != 0) {
				FrontendDisplay_LockOffscreenSurface();
				FrontendDisplay_UnlockOffscreenSurface(1);
			}
			break;
		case CONFIG_SCREEN_JOYSTICK:
			Config_JoystickRemapScreen();
			break;
		case CONFIG_SCREEN_TAUNTS:
			Config_DrawCustomTauntsPage();
			if (g_configDrawStaticControlBackground != 0) {
				FrontendDisplay_LockOffscreenSurface();
				FrontendDisplay_UnlockOffscreenSurface(1);
			}
			break;
	}

	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6, g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorLightBlue);
		animationFrame = frameState % PILOT_BANNER_ANIMATION_FRAMES;
		animationFrame >>= 1;
		sprintf(g_frontendScratchBuffer, "rebtiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 204, 453);
		sprintf(g_frontendScratchBuffer, "imptiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
	}

	g_configDrawStaticControlBackground = 0;
	Config_UpdateNavigationAndRestoreDefaults();
#ifdef XVT_MODERN
	if (XvtDialog_IsActive())
		return 0;
#endif
	if (Frontend_HandleCommonScreenControls(CONFIG_SCREEN_CONTEXT) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (XvtDialog_IsActive())
		return 0;
#endif

	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}
	FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_206_DONE));
	dismissRequested = FrontendButton_DrawSpriteHitTest(
		&rect, "leaveup", "leavedown", FrontendString_Get(FRONTSTR_206_DONE), 12, 0, 8, "buttonsound");
	FrontendButton_DisableOverlayText();
	dismissRequested |= FrontendDialog_HasNetworkDismissPacket();
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NET_HOST) {
		dismissRequested |= Net_HasQueuedJoinRequestOrBacklog();
	}
	if (dismissRequested != 0) {
		Config_Write();
		g_activeTextFieldId = 0;
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
		FrontendMouse_ClearInputGate();
		FrontImage_FreeResourceByName("backconfig");
		FrontendText_ResetGlyphScratch();
		FrontendScrollbar_RestoreState();
		if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NET_HOST) {
			g_frontendNetPacketScratch.packetType = NET_PACKET_GAME_OPTIONS;
			*(int*)&g_frontendNetPacketScratch.payload[0] = (uint8_t)g_gameConfig.difficulty;
			*(int*)&g_frontendNetPacketScratch.payload[4] = g_gameConfig.collisions;
			*(int*)&g_frontendNetPacketScratch.payload[8] = g_gameConfig.craftJumping;
			*(int*)&g_frontendNetPacketScratch.payload[12] = g_gameConfig.randomSetup;
			*(int*)&g_frontendNetPacketScratch.payload[16] = (uint8_t)g_gameConfig.battleLengthIndex;
			*(int*)&g_frontendNetPacketScratch.payload[20] = g_gameConfig.requirePassword;
			*(int*)&g_frontendNetPacketScratch.payload[24] = g_gameConfig.inProgressJoin;
			*(int*)&g_frontendNetPacketScratch.payload[28] = (uint8_t)g_gameConfig.craftSelection;
			*(int*)&g_frontendNetPacketScratch.payload[32] = g_gameConfig.locatePlayers;
			*(int*)&g_frontendNetPacketScratch.payload[36] = (uint8_t)g_gameConfig.craftWaves;
			*(int*)&g_frontendNetPacketScratch.payload[40] = g_gameConfig.missionTimeLimit;
			*(int*)&g_frontendNetPacketScratch.payload[44] = g_gameConfig.lastTeamTimeLimitMinutes;
			*(int*)&g_frontendNetPacketScratch.payload[48] = rand();
			*(int*)&g_frontendNetPacketScratch.payload[52] = g_gameConfig.asyncFlag;
			*(int*)&g_frontendNetPacketScratch.payload[56] = g_gameConfig.aiOpponents;
			*(int*)&g_frontendNetPacketScratch.payload[60] = g_gameConfig.serverUpdateRate;
			*(int*)&g_frontendNetPacketScratch.payload[64] = (uint8_t)g_gameConfig.combatBalance;
			*(int*)&g_frontendNetPacketScratch.payload[68] = (uint8_t)g_gameConfig.continueBattleOrCampaign;
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, CONFIG_PACKET_SIZE);
		}
	}
	return 0;
}

// FUNCTION: XVT 0x4B83A0
void Config_DrawVideoOptionRows(void) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 84, 90, 604, 106);
	FrontendText_DrawCentered(15,
							  g_pendingMenuScreen == 1
								  ? FrontendString_Get(FRONTSTR_219_SINGLE_PLAYER_FLIGHT_ENGINE_OPTIONS)
								  : FrontendString_Get(FRONTSTR_641_MULTIPLAYER_FLIGHT_ENGINE_OPTIONS),
							  &rect, 0xFFFF);

	Config_DrawScreenResolutionOptionRow(g_pendingMenuScreen - 1);
	Config_DrawWindowSizeOptionRow(g_pendingMenuScreen - 1);
	Config_DrawBitsPerPixelOptionRow(g_pendingMenuScreen - 1);
	Config_DrawBrightnessOptionRow(g_pendingMenuScreen - 1);
	Config_DrawDebrisOptionRow(g_pendingMenuScreen - 1);
	Config_DrawBackdropOptionRow(g_pendingMenuScreen - 1);
	Config_DrawStarDensityOptionRow(g_pendingMenuScreen - 1);
	Config_DrawLevelOfDetailOptionRow(g_pendingMenuScreen - 1);
	Config_DrawTextureResolutionOptionRow(g_pendingMenuScreen - 1);
	Config_DrawDitherOptionRow(g_pendingMenuScreen - 1);
	Config_DrawMipmapOptionRow(g_pendingMenuScreen - 1);
	Config_DrawLocalLightsOptionRow(g_pendingMenuScreen - 1);
	Config_DrawSpecularOptionRow(g_pendingMenuScreen - 1);
	Config_DrawDiffuseLightingOptionRow(g_pendingMenuScreen - 1);
	Config_DrawUse3dHardwareOptionRow(g_pendingMenuScreen - 1);
	Config_DrawBilinearOptionRow(g_pendingMenuScreen - 1);
}

// FUNCTION: XVT 0x4B84E0
void Config_DrawScreenResolutionOptionRow(int configIndex) {
	RECT rect;
	int previousScreenResolution;

	FrontendDraw_RectAssign(&rect, 88, 111, 332, 125);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_220_SCREEN_RESOLUTION), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	previousScreenResolution = g_gameConfig.screenRes[configIndex];
	Config_DrawThreeChoiceOption(&g_gameConfig.screenRes[configIndex], &rect, FRONTSTR_221_320_X_240);
	if (g_gameConfig.screenRes[configIndex] != previousScreenResolution) {
		g_gameConfig.windowSize[configIndex] = g_gameConfig.screenRes[configIndex];
	}
}

// FUNCTION: XVT 0x4B8570
void Config_DrawWindowSizeOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 88, 160, 332, 174);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_224_WINDOW_SIZE), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawThreeChoiceOption(&g_gameConfig.windowSize[configIndex], &rect, FRONTSTR_225_320_X_240);
	if (g_gameConfig.windowSize[configIndex] > g_gameConfig.screenRes[configIndex]) {
		g_gameConfig.windowSize[configIndex] = g_gameConfig.screenRes[configIndex];
	}
}

// FUNCTION: XVT 0x4B8600
void Config_DrawBitsPerPixelOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 88, 209, 332, 223);
	if (g_gameConfig.use3dHardware[configIndex] != 0) {
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_232_NUMBER_OF_COLORS), &rect, 0, 1,
									   g_colorGray);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		Config_DrawOptionCycleReadOnly(&g_gameConfig.bpp[configIndex], &rect, FRONTSTR_233_256);
	} else {
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_232_NUMBER_OF_COLORS), &rect, 0, 1,
									   0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		Config_DrawOptionCycle(&g_gameConfig.bpp[configIndex], &rect, FRONTSTR_233_256);
	}
}

// FUNCTION: XVT 0x4B86E0
void Config_DrawBrightnessOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 88, 243, 332, 257);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_255_BRIGHTNESS), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionSliderImpl(&g_gameConfig.brightness[configIndex], &rect, 8, FRONTSTR_256_DIM, 1);
}

// FUNCTION: XVT 0x4B8760
void Config_DrawDebrisOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 88, 292, 332, 306);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_238_SPACE_DEBRIS), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.debris[configIndex], &rect, FRONTSTR_236_OFF);
}

// FUNCTION: XVT 0x4B87E0
void Config_DrawBackdropOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 88, 326, 332, 340);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_235_BACKDROP), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.backdrop[configIndex], &rect, FRONTSTR_236_OFF);
}

// FUNCTION: XVT 0x4B8860
void Config_DrawStarDensityOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 88, 360, 332, 374);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_228_STARFIELD_DENSITY), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawThreeChoiceOption(&g_gameConfig.starDensity[configIndex], &rect, FRONTSTR_229_LOW);
}

// FUNCTION: XVT 0x4B88E0
void Config_DrawLevelOfDetailOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 111, 600, 125);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_251_USE_LOW_DETAIL_MODELS), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionSliderImpl(&g_gameConfig.lod[configIndex], &rect, 20, FRONTSTR_252_NEAR, 1);
}

// FUNCTION: XVT 0x4B8960
void Config_DrawTextureResolutionOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 155, 600, 169);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_243_TEXTURE_RESOLUTION), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawThreeChoiceOption(&g_gameConfig.textureRes[configIndex], &rect, FRONTSTR_244_LOW);
}

// FUNCTION: XVT 0x4B89E0
void Config_DrawDitherOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 199, 600, 213);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_242_DITHERING), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.dither[configIndex], &rect, FRONTSTR_236_OFF);
}

// FUNCTION: XVT 0x4B8A60
void Config_DrawMipmapOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 228, 600, 242);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_247_MIP_MAPPING), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionSliderImpl(&g_gameConfig.mipmap[configIndex], &rect, 20, FRONTSTR_248_BLURRY, 1);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_250_OPTIMAL), &rect, g_colorLightBlue);
}

// FUNCTION: XVT 0x4B8B20
void Config_DrawLocalLightsOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 272, 600, 286);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_239_LOCAL_LIGHT_SOURCE), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.localLights[configIndex], &rect, FRONTSTR_236_OFF);
}

// FUNCTION: XVT 0x4B8BA0
void Config_DrawSpecularOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 301, 600, 315);
	if (g_gameConfig.use3dHardware[configIndex] != 0) {
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_240_SPECULAR_HIGHLIGHTS), &rect, 0, 1,
									   g_colorGray);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		Config_DrawOptionCycleDisabled(&g_gameConfig.specular[configIndex], &rect, FRONTSTR_236_OFF);
	} else {
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_240_SPECULAR_HIGHLIGHTS), &rect, 0, 1,
									   0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		Config_DrawOptionCycle(&g_gameConfig.specular[configIndex], &rect, FRONTSTR_236_OFF);
	}
}

// FUNCTION: XVT 0x4B8C80
void Config_DrawDiffuseLightingOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 330, 600, 344);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_241_DIFFUSE_LIGHTING), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.diffuse[configIndex], &rect, FRONTSTR_236_OFF);
}

// FUNCTION: XVT 0x4B8D00
void Config_DrawUse3dHardwareOptionRow(int configIndex) {
	RECT rect;
	int previousUse3dHardware;

	FrontendDraw_RectAssign(&rect, 356, 359, 600, 373);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_802_3D_HARDWARE), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	previousUse3dHardware = g_gameConfig.use3dHardware[configIndex];
	Config_DrawOptionCycle(&g_gameConfig.use3dHardware[configIndex], &rect, FRONTSTR_236_OFF);
	if (g_gameConfig.use3dHardware[configIndex] != previousUse3dHardware &&
		g_gameConfig.use3dHardware[configIndex] != 0) {
		g_gameConfig.bpp[configIndex] = 1;
	}
}

// FUNCTION: XVT 0x4B8DA0
void Config_DrawBilinearOptionRow(int configIndex) {
	RECT rect;

	FrontendDraw_RectAssign(&rect, 356, 388, 600, 402);
	if (g_gameConfig.use3dHardware[configIndex] != 0) {
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_803_BILINEAR_FILTERING), &rect, 0, 1,
									   0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		Config_DrawOptionCycle(&g_gameConfig.bilinear[configIndex], &rect, FRONTSTR_236_OFF);
	} else {
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_803_BILINEAR_FILTERING), &rect, 0, 1,
									   g_colorGray);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		Config_DrawOptionCycleDisabled(&g_gameConfig.bilinear[configIndex], &rect, FRONTSTR_236_OFF);
	}
}

// FUNCTION: XVT 0x4B8E80
void Config_DrawOptionCycleDisabled(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId) {
	Config_DrawOptionCycleImpl(value, rect, valueBaseStrId, 1, 0);
}

// FUNCTION: XVT 0x4B8EA0
void Config_DrawOptionCycle(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId) {
	Config_DrawOptionCycleImpl(value, rect, valueBaseStrId, 0, 0);
}

// FUNCTION: XVT 0x4B8EC0
void Config_DrawOptionCycleReadOnly(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId) {
	Config_DrawOptionCycleImpl(value, rect, valueBaseStrId, 1, 1);
}

// FUNCTION: XVT 0x4B8EE0
void Config_DrawOptionCycleImpl(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId,
								int translucentSelection, int disableInput) {
	RECT optionRect;
	RECT spriteRect;
	int cursorX;
	int cursorY;
	int optionIndex;
	int labelColor;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontImage_GetResourceRect("offslot", &spriteRect);
	optionIndex = 0;
	FrontendDraw_RectCopy(&optionRect, rect);
	do {
		optionRect.right = optionRect.left + spriteRect.right - spriteRect.left + 1;
		if (g_configDrawStaticControlBackground != 0) {
			FrontendDisplay_LockOffscreenSurface();
			if (optionIndex == 0) {
				FrontImage_DrawSpriteTranslucent("offslot", optionRect.left, optionRect.top);
			} else {
				FrontImage_DrawSpriteTranslucent("onslot", optionRect.left, optionRect.top);
			}
			FrontendDisplay_UnlockOffscreenSurface(0);
			if (optionIndex == 0) {
				FrontImage_DrawSpriteTranslucent("offslot", optionRect.left, optionRect.top);
			} else {
				FrontImage_DrawSpriteTranslucent("onslot", optionRect.left, optionRect.top);
			}
		}

		if (disableInput == 0 && FrontendDraw_PointInRect(&optionRect, cursorX, cursorY) &&
			(FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
			if (*value != optionIndex && g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			}
			*value = (uint8_t)optionIndex;
		}
		if (*value == optionIndex) {
			if (translucentSelection != 0) {
				FrontImage_DrawSpriteTranslucent("3conbtn", optionRect.left, optionRect.top);
			} else {
				FrontImage_DrawSprite("3conbtn", optionRect.left, optionRect.top);
			}
		}
		labelColor = g_colorLightBlue;
		++optionIndex;
		optionRect.left = optionRect.right + 5;
		optionRect.right = optionRect.left + 100;
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(valueBaseStrId + optionIndex - 1), &optionRect,
									   0, 1, labelColor);
		optionRect.left = rect->left + 130;
	} while (optionIndex < 2);
}

// FUNCTION: XVT 0x4B9090
void Config_DrawThreeChoiceOption(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId) {
	RECT optionRect;
	RECT spriteRect;
	int cursorX;
	int cursorY;
	int buttonWidth;
	int right;

	if (g_configDrawStaticControlBackground != 0) {
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("3conbar", rect->left, rect->top);
		FrontendDisplay_UnlockOffscreenSurface(0);
		FrontImage_DrawSpriteTranslucent("3conbar", rect->left, rect->top);
	}

	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontImage_GetResourceRect("3conbtn", &spriteRect);
	buttonWidth = spriteRect.right - spriteRect.left + 1;
	FrontImage_GetResourceRect("3conbar", &spriteRect);
	FrontendDraw_RectOffsetXY(&spriteRect, rect->left, rect->top);
	FrontendDraw_RectCopy(&optionRect, rect);

	optionRect.right = optionRect.left + buttonWidth;
	if (*value == 0) {
		FrontImage_DrawSprite("3conbtn", optionRect.left, optionRect.top);
	} else if ((FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0) &&
			   FrontendDraw_PointInRect(&optionRect, cursorX, cursorY)) {
		if (*value != 0 && g_gameConfig.sfxDatapadEnabled != 0) {
			FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
		}
		*value = 0;
	}
	FrontendDraw_RectOffsetXY(&optionRect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(valueBaseStrId), &optionRect, 0, 1,
								   g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&optionRect, 0, -15);

	optionRect.left = spriteRect.left + ((spriteRect.right - buttonWidth - spriteRect.left) >> 1) + 1;
	optionRect.right = optionRect.left + buttonWidth;
	if (*value == 1) {
		FrontImage_DrawSprite("3conbtn", optionRect.left, optionRect.top);
	} else if ((FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0) &&
			   FrontendDraw_PointInRect(&optionRect, cursorX, cursorY)) {
		if (*value != 1 && g_gameConfig.sfxDatapadEnabled != 0) {
			FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
		}
		*value = 1;
	}
	FrontendDraw_RectOffsetXY(&optionRect, 0, 15);
	FrontendText_DrawCentered(12, FrontendString_Get(valueBaseStrId + 1), &optionRect, g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&optionRect, 0, -15);

	optionRect.right = spriteRect.right;
	optionRect.left = spriteRect.right - buttonWidth + 1;
	if (*value == 2) {
		FrontImage_DrawSprite("3conbtn", optionRect.left, optionRect.top);
	} else if ((FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0) &&
			   FrontendDraw_PointInRect(&optionRect, cursorX, cursorY)) {
		if (*value != 2 && g_gameConfig.sfxDatapadEnabled != 0) {
			FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
		}
		*value = 2;
	}
	FrontendDraw_RectOffsetXY(&optionRect, 0, 15);
	right = spriteRect.right;
	optionRect.left = right - FrontendText_MeasureWidth(FrontendString_Get(valueBaseStrId + 2), 12) + 1;
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(valueBaseStrId + 2), &optionRect, 0, 1,
								   g_colorLightBlue);
}

// FUNCTION: XVT 0x4B93F0
void Config_DrawOptionSliderImpl(uint8_t* value, RECT* rect, int valueCount, FrontendStringId rangeLabelId,
								 int playSoundOnChange) {
	RECT optionRect;
	int cursorY;
	int cursorX;
	float stepSize;
	RECT handleRect;
	double stepSizeAsDouble;
	int sliderWidth;
	int selectedValue;
	int halfStepWidth;
	int optionIndex;
	int integerStepWidth;

	sliderWidth = rect->right - rect->left + 1;
	selectedValue = *value;
	stepSize = (float)((double)sliderWidth / (double)(valueCount - 1));
	if (g_configDrawStaticControlBackground != 0) {
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("conbar", rect->left, rect->top + 5);
		FrontendDisplay_UnlockOffscreenSurface(0);
		FrontImage_DrawSpriteTranslucent("conbar", rect->left, rect->top + 5);
	}

	FrontendDraw_RectCopy(&optionRect, rect);
	FrontendDraw_RectOffsetXY(&optionRect, 0, 15);
	optionRect.right = optionRect.left + sliderWidth / 2 - 1;
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(rangeLabelId), &optionRect, 0, 1, g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&optionRect, sliderWidth / 2, 0);
	optionRect.left = optionRect.right - FrontendText_MeasureWidth(FrontendString_Get(rangeLabelId + 1), 12);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(rangeLabelId + 1), &optionRect, 1, 1,
								   g_colorLightBlue);

	FrontendDraw_RectOffsetXY(rect, 2, 0);
	FrontImage_GetResourceRect("conhandle", &handleRect);
	FrontendDraw_RectOffsetXY(&handleRect, (handleRect.left - handleRect.right - 1) >> 1, 0);
	FrontendDraw_RectOffsetXY(&handleRect, (int)(selectedValue * stepSize), 0);
	FrontImage_DrawSprite("conhandle", handleRect.left + rect->left, handleRect.top + rect->top);

	if (FrontendMouse_GetLeftDown() == 0 && FrontendMouse_GetRightDown() == 0) {
		FrontendDraw_RectOffsetXY(rect, -2, 0);
		return;
	}

	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontendDraw_RectCopy(&optionRect, rect);
	stepSizeAsDouble = stepSize;
	halfStepWidth = (int)(stepSize * 0.5);
	optionRect.left = optionRect.right - halfStepWidth;
	if (FrontendDraw_PointInRect(&optionRect, cursorX, cursorY)) {
		if (playSoundOnChange != 0 && *value != valueCount - 1 && g_gameConfig.sfxDatapadEnabled != 0) {
			FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
		}
		*value = (uint8_t)(valueCount - 1);
		FrontendDraw_RectOffsetXY(rect, -2, 0);
		return;
	}

	FrontendDraw_RectCopy(&optionRect, rect);
	optionRect.right = optionRect.left + halfStepWidth;
	if (FrontendDraw_PointInRect(&optionRect, cursorX, cursorY)) {
		if (playSoundOnChange != 0 && *value != 0 && g_gameConfig.sfxDatapadEnabled != 0) {
			FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
		}
		*value = 0;
		FrontendDraw_RectOffsetXY(rect, -2, 0);
		return;
	}

	optionIndex = 0;
	optionRect.left = optionRect.right;
	optionRect.right += (int)stepSizeAsDouble;
	integerStepWidth = (int)stepSizeAsDouble;
	if (valueCount - 2 <= 0) {
		FrontendDraw_RectOffsetXY(rect, -2, 0);
		return;
	}
	while (!FrontendDraw_PointInRect(&optionRect, cursorX, cursorY)) {
		++optionIndex;
		FrontendDraw_RectOffsetXY(&optionRect, integerStepWidth, 0);
		if (optionIndex >= valueCount - 2) {
			FrontendDraw_RectOffsetXY(rect, -2, 0);
			return;
		}
	}
	if (playSoundOnChange != 0 && *value != optionIndex + 1 && g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
	}
	*value = (uint8_t)(optionIndex + 1);
	FrontendDraw_RectOffsetXY(rect, -2, 0);
}

// FUNCTION: XVT 0x4B97E0
void Config_Load(void) {
	int convertedJoyButtons[20];
	int buttonCount;
	int configIndex;
#ifndef XVT_MODERN
	XvtFile* stream;
	int loadedLegacyConfig;
	char* value;
	int characterIndex;
	int keywordIndex;
	int matchedKeywordIndex;
#endif

	memset(convertedJoyButtons, 0, sizeof(convertedJoyButtons));
	memset(&g_gameConfig, 0, sizeof(g_gameConfig));
	for (configIndex = 0; configIndex < 2; ++configIndex) {
		g_gameConfig.backdrop[configIndex] = 1;
		g_gameConfig.starDensity[configIndex] = 2;
		g_gameConfig.debris[configIndex] = 1;
		g_gameConfig.localLights[configIndex] = 1;
		g_gameConfig.specular[configIndex] = 1;
		g_gameConfig.diffuse[configIndex] = 1;
		g_gameConfig.dither[configIndex] = 1;
		g_gameConfig.textureRes[configIndex] = 2;
		g_gameConfig.mipmap[configIndex] = 10;
		g_gameConfig.lod[configIndex] = 10;
		g_gameConfig.screenRes[configIndex] = 2;
		g_gameConfig.windowSize[configIndex] = 2;
		g_gameConfig.bpp[configIndex] = 0;
		g_gameConfig.brightness[configIndex] = 2;
		g_gameConfig.use3dHardware[configIndex] = FrontendDisplay_IsSecondaryDirectDrawActive();
		g_gameConfig.bilinear[configIndex] = 1;
	}
	g_gameConfig.networkType = 0;
	g_gameConfig.sfxExteriorEnabled = 1;
	g_gameConfig.sfxInteriorEnabled = 1;
	g_gameConfig.sfxEngineEnabled = 1;
	g_gameConfig.sfxDatapadEnabled = 1;
	g_gameConfig.voicePilotEnabled = 2;
	g_gameConfig.voiceTacticalOfficerEnabled = 2;
	g_gameConfig.voiceCommanderEnabled = 1;
	g_gameConfig.voiceSpecialEnabled = 1;
	g_gameConfig.musicEnabled = 1;
	g_gameConfig.datapadMusicEnabled = 1;
	g_gameConfig.sfxDatapadVolume = 9;
	g_gameConfig.sfxExteriorVolume = 9;
	g_gameConfig.sfxInteriorVolume = 9;
	g_gameConfig.sfxEngineVolume = 9;
	g_gameConfig.voiceVolume = 9;
	g_gameConfig.difficulty = GAME_DIFFICULTY_MEDIUM;
	g_gameConfig.collisions = 1;
	g_gameConfig.craftJumping = 1;
	g_gameConfig.randomSetup = 0;
	g_gameConfig.battleLengthIndex = BATTLE_LENGTH_THREE_WINS;
	g_gameConfig.requirePassword = 0;
	g_gameConfig.inProgressJoin = 0;
	g_gameConfig.craftSelection = CRAFT_SELECTION_ON;
	g_gameConfig.locatePlayers = 1;
	g_gameConfig.craftWaves = CRAFT_WAVES_DEFAULT;
	g_gameConfig.lastTeamTimeLimitMinutes = 1;
	g_gameConfig.randomSeed = 0;
	g_gameConfig.asyncFlag = 0;
	g_gameConfig.aiOpponents = 0;
	g_gameConfig.helpOn = 1;
	g_gameConfig.combatBalance = COMBAT_BALANCE_AUTOBALANCE;
	g_gameConfig.continueBattleOrCampaign = SEQUENCE_CONTINUE;
	g_gameConfig.musicVolume = 5;
	g_gameConfig.missionTimeLimit = UINT8_MAX;
	g_gameConfig.serverUpdateRate = 8;

	buttonCount = Joystick_GetButtonCount(0);
	for (configIndex = 0; configIndex < buttonCount && configIndex < 16; ++configIndex) {
		switch (configIndex) {
			case 0:
				g_gameConfig.joyButtons[configIndex] = (uint8_t)-100;
				break;
			case 1:
				g_gameConfig.joyButtons[configIndex] = (uint8_t)-99;
				break;
			case 2:
				g_gameConfig.joyButtons[configIndex] = 114;
				break;
			case 3:
				g_gameConfig.joyButtons[configIndex] = 46;
				break;
			case 4:
				g_gameConfig.joyButtons[configIndex] = 101;
				break;
			case 5:
				g_gameConfig.joyButtons[configIndex] = 105;
				break;
			case 6:
				g_gameConfig.joyButtons[configIndex] = 91;
				break;
			case 7:
				g_gameConfig.joyButtons[configIndex] = 8;
				break;
			case 8:
				g_gameConfig.joyButtons[configIndex] = 13;
				break;
			case 9:
				g_gameConfig.joyButtons[configIndex] = 93;
				break;
		}
	}

	strcpy(g_gameConfig.taunts[0], FrontendString_Get(FRONTSTR_790_STAY_ON_TARGET));
	strcpy(g_gameConfig.taunts[1], FrontendString_Get(FRONTSTR_791_I_CAN_T_SHAKE_HIM));
	strcpy(g_gameConfig.taunts[2], FrontendString_Get(FRONTSTR_792_HE_S_HISTORY));
	strcpy(g_gameConfig.taunts[3], FrontendString_Get(FRONTSTR_793_WOOHOO));

#ifdef XVT_MODERN
	{
		char error[512];
		if (!XvtConfig_Apply(&g_gameConfig, error, sizeof(error)))
			XvtStorage_Fatal(error, 1);
	}
	return;
#else
	stream = File_Open("config2.cfg", "r");
	loadedLegacyConfig = 0;
	if (stream == NULL) {
		File_ChangeToBaseGameInstallPath();
		stream = File_Open("config.cfg", "r");
		File_ChangeToInstallPath();
		loadedLegacyConfig = 1;
	}
	if (stream == NULL) {
		return;
	}

	for (;;) {
		if (File_Gets(g_frontendScratchBuffer, 256, stream) == NULL) {
			break;
		}
		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
			g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}

		value = NULL;
		characterIndex = 0;
		if ((int)strlen(g_frontendScratchBuffer) > 0) {
			do {
				if (g_frontendScratchBuffer[characterIndex] == ' ') {
					g_frontendScratchBuffer[characterIndex] = '\0';
					value = &g_frontendScratchBuffer[characterIndex + 1];
					break;
				}
				++characterIndex;
			} while ((int)strlen(g_frontendScratchBuffer) > characterIndex);
		}
		if (value == NULL) {
			continue;
		}

		keywordIndex = 0;
		matchedKeywordIndex = -1;
		while (*g_configKeywords[keywordIndex] != '\0') {
			if (strcmp(g_configKeywords[keywordIndex], g_frontendScratchBuffer) == 0) {
				matchedKeywordIndex = keywordIndex;
				break;
			}
			++keywordIndex;
		}
		if (matchedKeywordIndex == -1) {
			continue;
		}

		switch (matchedKeywordIndex) {
			case 0:
				memcpy(g_gameConfig.lastPilotName, value, sizeof(g_gameConfig.lastPilotName));
				break;
			case 1:
				g_gameConfig.backdrop[0] = (uint8_t)atoi(value);
				break;
			case 2:
				g_gameConfig.starDensity[0] = (uint8_t)atoi(value);
				break;
			case 3:
				g_gameConfig.debris[0] = (uint8_t)atoi(value);
				break;
			case 4:
				g_gameConfig.localLights[0] = (uint8_t)atoi(value);
				break;
			case 5:
				g_gameConfig.specular[0] = (uint8_t)atoi(value);
				break;
			case 6:
				g_gameConfig.diffuse[0] = (uint8_t)atoi(value);
				break;
			case 7:
				g_gameConfig.dither[0] = (uint8_t)atoi(value);
				break;
			case 8:
				g_gameConfig.textureRes[0] = (uint8_t)atoi(value);
				break;
			case 9:
				g_gameConfig.mipmap[0] = (uint8_t)atoi(value);
				break;
			case 10:
				g_gameConfig.lod[0] = (uint8_t)atoi(value);
				break;
			case 11:
				g_gameConfig.screenRes[0] = (uint8_t)atoi(value);
				break;
			case 12:
				g_gameConfig.windowSize[0] = (uint8_t)atoi(value);
				break;
			case 13:
				g_gameConfig.bpp[0] = (uint8_t)atoi(value);
				break;
			case 14:
				g_gameConfig.brightness[0] = (uint8_t)atoi(value);
				break;
			case 15:
				g_gameConfig.backdrop[1] = (uint8_t)atoi(value);
				break;
			case 16:
				g_gameConfig.starDensity[1] = (uint8_t)atoi(value);
				break;
			case 17:
				g_gameConfig.debris[1] = (uint8_t)atoi(value);
				break;
			case 18:
				g_gameConfig.localLights[1] = (uint8_t)atoi(value);
				break;
			case 19:
				g_gameConfig.specular[1] = (uint8_t)atoi(value);
				break;
			case 20:
				g_gameConfig.diffuse[1] = (uint8_t)atoi(value);
				break;
			case 21:
				g_gameConfig.dither[1] = (uint8_t)atoi(value);
				break;
			case 22:
				g_gameConfig.textureRes[1] = (uint8_t)atoi(value);
				break;
			case 23:
				g_gameConfig.mipmap[1] = (uint8_t)atoi(value);
				break;
			case 24:
				g_gameConfig.lod[1] = (uint8_t)atoi(value);
				break;
			case 25:
				g_gameConfig.screenRes[1] = (uint8_t)atoi(value);
				break;
			case 26:
				g_gameConfig.windowSize[1] = (uint8_t)atoi(value);
				break;
			case 27:
				g_gameConfig.bpp[1] = (uint8_t)atoi(value);
				break;
			case 28:
				g_gameConfig.brightness[1] = (uint8_t)atoi(value);
				break;
			case 29:
				g_gameConfig.networkType = (uint8_t)atoi(value);
				break;
			case 30:
				memcpy(g_gameConfig.phoneNumber, value, sizeof(g_gameConfig.phoneNumber));
				break;
			case 31:
				memcpy(g_gameConfig.ipAddress, value, sizeof(g_gameConfig.ipAddress));
				break;
			case 32:
				g_gameConfig.sfxExteriorEnabled = (uint8_t)atoi(value);
				break;
			case 33:
				g_gameConfig.sfxInteriorEnabled = (uint8_t)atoi(value);
				break;
			case 34:
				g_gameConfig.sfxEngineEnabled = (uint8_t)atoi(value);
				break;
			case 35:
				g_gameConfig.sfxDatapadEnabled = (uint8_t)atoi(value);
				break;
			case 36:
				g_gameConfig.voicePilotEnabled = (uint8_t)atoi(value);
				break;
			case 37:
				g_gameConfig.voiceTacticalOfficerEnabled = (uint8_t)atoi(value);
				break;
			case 38:
				g_gameConfig.voiceCommanderEnabled = (uint8_t)atoi(value);
				break;
			case 39:
				g_gameConfig.voiceSpecialEnabled = (uint8_t)atoi(value);
				break;
			case 40:
				g_gameConfig.musicEnabled = (uint8_t)atoi(value);
				break;
			case 41:
				g_gameConfig.sfxDatapadVolume = (uint8_t)atoi(value);
				break;
			case 42:
				g_gameConfig.sfxExteriorVolume = (uint8_t)atoi(value);
				break;
			case 43:
				g_gameConfig.sfxInteriorVolume = (uint8_t)atoi(value);
				break;
			case 44:
				g_gameConfig.sfxEngineVolume = (uint8_t)atoi(value);
				break;
			case 45:
				g_gameConfig.voiceVolume = (uint8_t)atoi(value);
				break;
			case 46:
				g_gameConfig.musicVolume = (uint8_t)atoi(value);
				break;
			case 47:
			case 48:
			case 49:
			case 50:
			case 51:
			case 52:
			case 53:
			case 54:
			case 55:
			case 56:
			case 57:
			case 58:
			case 59:
			case 60:
			case 61:
			case 62:
			case 63:
			case 64:
			case 65:
			case 66:
				g_gameConfig.joyButtons[matchedKeywordIndex - 47] = (uint8_t)atoi(value);
				if (loadedLegacyConfig != 0) {
					convertedJoyButtons[matchedKeywordIndex - 47] = 1;
				}
				break;
			case 79:
				g_gameConfig.difficulty = (GameDifficulty)atoi(value);
				break;
			case 80:
				g_gameConfig.collisions = (uint8_t)atoi(value);
				break;
			case 81:
				g_gameConfig.craftJumping = (uint8_t)atoi(value);
				break;
			case 82:
				g_gameConfig.randomSetup = (uint8_t)atoi(value);
				break;
			case 83:
				g_gameConfig.battleLengthIndex = (BattleLength)atoi(value);
				break;
			case 84:
				g_gameConfig.requirePassword = (uint8_t)atoi(value);
				break;
			case 85:
				g_gameConfig.inProgressJoin = (uint8_t)atoi(value);
				break;
			case 86:
				g_gameConfig.craftSelection = (CraftSelectionMode)atoi(value);
				break;
			case 87:
				g_gameConfig.locatePlayers = (uint8_t)atoi(value);
				break;
			case 88:
				g_gameConfig.craftWaves = (CraftWaveMode)atoi(value);
				break;
			case 89:
				g_gameConfig.missionTimeLimit = (uint8_t)atoi(value);
				break;
			case 90:
				g_gameConfig.lastTeamTimeLimitMinutes = (uint8_t)atoi(value);
				break;
			case 91:
				g_gameConfig.randomSeed = (unsigned int)atoi(value);
				break;
			case 92:
				memcpy(g_gameConfig.password, value, sizeof(g_gameConfig.password));
				break;
			case 93:
				g_gameConfig.asyncFlag = (uint8_t)atoi(value);
				break;
			case 94:
				g_gameConfig.aiOpponents = (uint8_t)atoi(value);
				break;
			case 95:
				g_gameConfig.helpOn = (uint8_t)atoi(value);
				break;
			case 96:
				g_gameConfig.datapadMusicEnabled = (uint8_t)atoi(value);
				break;
			case 97:
				g_gameConfig.serverUpdateRate = (uint8_t)atoi(value);
				break;
			case 98:
				g_gameConfig.combatBalance = (CombatBalanceMode)atoi(value);
				break;
			case 99:
			case 100:
			case 101:
			case 102:
				memcpy(g_gameConfig.taunts[matchedKeywordIndex - 99], value, sizeof(g_gameConfig.taunts[0]));
				break;
			case 103:
				g_gameConfig.use3dHardware[0] = (uint8_t)atoi(value);
				break;
			case 104:
				g_gameConfig.bilinear[0] = (uint8_t)atoi(value);
				break;
			case 105:
				g_gameConfig.use3dHardware[1] = (uint8_t)atoi(value);
				break;
			case 106:
				g_gameConfig.bilinear[1] = (uint8_t)atoi(value);
				break;
		}
	}

	File_Close(stream);
	if (loadedLegacyConfig != 0) {
		for (configIndex = 0; configIndex < 20; ++configIndex) {
			if (convertedJoyButtons[configIndex] != 0 && g_gameConfig.joyButtons[configIndex] >= 124 &&
				g_gameConfig.joyButtons[configIndex] <= 229) {
				g_gameConfig.joyButtons[configIndex] += 4;
			}
		}
	}
#endif
}

// FUNCTION: XVT 0x4BA3C0
void Config_Write(void) {
#ifdef XVT_MODERN
	char error[512];
	snprintf(g_gameConfig.lastPilotName, sizeof(g_gameConfig.lastPilotName), "%s", g_pilotData.name);
	if (!XvtConfig_Write(&g_gameConfig, error, sizeof(error)))
		XvtStorage_Fatal(error, 1);
#else

	XvtFile* stream;
	int configIndex;
	int optionNumber;

	strncpy(g_gameConfig.lastPilotName, g_pilotData.name, sizeof(g_gameConfig.lastPilotName) - 1);
	g_gameConfig.lastPilotName[sizeof(g_gameConfig.lastPilotName) - 1] = '\0';
	stream = File_Open("config2.cfg", "w");
	if (stream == NULL) {
		return;
	}

	File_Printf(stream, "lastpilot %s\n", g_pilotData.name);
	for (configIndex = 0;
		 configIndex < (int)(sizeof(g_gameConfig.backdrop) / sizeof(g_gameConfig.backdrop[0]));
		 ++configIndex) {
		optionNumber = configIndex + 1;
		File_Printf(stream, "backdrop%d %d\n", optionNumber, g_gameConfig.backdrop[configIndex]);
		File_Printf(stream, "stardensity%d %d\n", optionNumber, g_gameConfig.starDensity[configIndex]);
		File_Printf(stream, "debris%d %d\n", optionNumber, g_gameConfig.debris[configIndex]);
		File_Printf(stream, "locallights%d %d\n", optionNumber, g_gameConfig.localLights[configIndex]);
		File_Printf(stream, "specular%d %d\n", optionNumber, g_gameConfig.specular[configIndex]);
		File_Printf(stream, "diffuse%d %d\n", optionNumber, g_gameConfig.diffuse[configIndex]);
		File_Printf(stream, "dither%d %d\n", optionNumber, g_gameConfig.dither[configIndex]);
		File_Printf(stream, "textureres%d %d\n", optionNumber, g_gameConfig.textureRes[configIndex]);
		File_Printf(stream, "mipmap%d %d\n", optionNumber, g_gameConfig.mipmap[configIndex]);
		File_Printf(stream, "lod%d %d\n", optionNumber, g_gameConfig.lod[configIndex]);
		File_Printf(stream, "screenres%d %d\n", optionNumber, g_gameConfig.screenRes[configIndex]);
		File_Printf(stream, "windowsize%d %d\n", optionNumber, g_gameConfig.windowSize[configIndex]);
		File_Printf(stream, "bpp%d %d\n", optionNumber, g_gameConfig.bpp[configIndex]);
		File_Printf(stream, "brightness%d %d\n", optionNumber, g_gameConfig.brightness[configIndex]);
		File_Printf(stream, "use_3d_hardware%d %d\n", optionNumber, g_gameConfig.use3dHardware[configIndex]);
		File_Printf(stream, "bilinear%d %d\n", optionNumber, g_gameConfig.bilinear[configIndex]);
	}

	File_Printf(stream, "networktype %d\n", g_gameConfig.networkType);
	File_Printf(stream, "phonenumber %s\n", g_gameConfig.phoneNumber);
	File_Printf(stream, "ipaddress %s\n", g_gameConfig.ipAddress);
	File_Printf(stream, "server_update_rate %d\n", g_gameConfig.serverUpdateRate);
	File_Printf(stream, "sfx_exterior %d\n", g_gameConfig.sfxExteriorEnabled);
	File_Printf(stream, "sfx_interior %d\n", g_gameConfig.sfxInteriorEnabled);
	File_Printf(stream, "sfx_engine %d\n", g_gameConfig.sfxEngineEnabled);
	File_Printf(stream, "sfx_datapad %d\n", g_gameConfig.sfxDatapadEnabled);
	File_Printf(stream, "voice_pilot %d\n", g_gameConfig.voicePilotEnabled);
	File_Printf(stream, "voice_tactical_officer %d\n", g_gameConfig.voiceTacticalOfficerEnabled);
	File_Printf(stream, "voice_commander %d\n", g_gameConfig.voiceCommanderEnabled);
	File_Printf(stream, "voice_special %d\n", g_gameConfig.voiceSpecialEnabled);
	File_Printf(stream, "music %d\n", g_gameConfig.musicEnabled);
	File_Printf(stream, "sfx_datapad_volume %d\n", g_gameConfig.sfxDatapadVolume);
	File_Printf(stream, "sfx_exterior_volume %d\n", g_gameConfig.sfxExteriorVolume);
	File_Printf(stream, "sfx_interior_volume %d\n", g_gameConfig.sfxInteriorVolume);
	File_Printf(stream, "sfx_engine_volume %d\n", g_gameConfig.sfxEngineVolume);
	File_Printf(stream, "voice_volume %d\n", g_gameConfig.voiceVolume);
	File_Printf(stream, "music_volume %d\n", g_gameConfig.musicVolume);
	File_Printf(stream, "datapad_music %d\n", g_gameConfig.datapadMusicEnabled);
	File_Printf(stream, "joybutton1 %d\n", g_gameConfig.joyButtons[0]);
	File_Printf(stream, "joybutton2 %d\n", g_gameConfig.joyButtons[1]);
	File_Printf(stream, "joybutton3 %d\n", g_gameConfig.joyButtons[2]);
	File_Printf(stream, "joybutton4 %d\n", g_gameConfig.joyButtons[3]);
	File_Printf(stream, "joybutton5 %d\n", g_gameConfig.joyButtons[4]);
	File_Printf(stream, "joybutton6 %d\n", g_gameConfig.joyButtons[5]);
	File_Printf(stream, "joybutton7 %d\n", g_gameConfig.joyButtons[6]);
	File_Printf(stream, "joybutton8 %d\n", g_gameConfig.joyButtons[7]);
	File_Printf(stream, "joybutton9 %d\n", g_gameConfig.joyButtons[8]);
	File_Printf(stream, "joybutton10 %d\n", g_gameConfig.joyButtons[9]);
	File_Printf(stream, "joybutton11 %d\n", g_gameConfig.joyButtons[10]);
	File_Printf(stream, "joybutton12 %d\n", g_gameConfig.joyButtons[11]);
	File_Printf(stream, "joybutton13 %d\n", g_gameConfig.joyButtons[12]);
	File_Printf(stream, "joybutton14 %d\n", g_gameConfig.joyButtons[13]);
	File_Printf(stream, "joybutton15 %d\n", g_gameConfig.joyButtons[14]);
	File_Printf(stream, "joybutton16 %d\n", g_gameConfig.joyButtons[15]);
	File_Printf(stream, "joybutton17 %d\n", g_gameConfig.joyButtons[16]);
	File_Printf(stream, "joybutton18 %d\n", g_gameConfig.joyButtons[17]);
	File_Printf(stream, "joybutton19 %d\n", g_gameConfig.joyButtons[18]);
	File_Printf(stream, "joybutton20 %d\n", g_gameConfig.joyButtons[19]);
	File_Printf(stream, "difficulty %d\n", g_gameConfig.difficulty);
	File_Printf(stream, "collisions %d\n", g_gameConfig.collisions);
	File_Printf(stream, "craft_jumping %d\n", g_gameConfig.craftJumping);
	File_Printf(stream, "random_setup %d\n", g_gameConfig.randomSetup);
	File_Printf(stream, "handicapping %d\n", g_gameConfig.battleLengthIndex);
	File_Printf(stream, "require_password %d\n", g_gameConfig.requirePassword);
	File_Printf(stream, "in_progress_join %d\n", g_gameConfig.inProgressJoin);
	File_Printf(stream, "craft_selection %d\n", g_gameConfig.craftSelection);
	File_Printf(stream, "locate_players %d\n", g_gameConfig.locatePlayers);
	File_Printf(stream, "craft_waves %d\n", g_gameConfig.craftWaves);
	File_Printf(stream, "mission_time_limit %d\n", g_gameConfig.missionTimeLimit);
	File_Printf(stream, "last_time_limit %d\n", g_gameConfig.lastTeamTimeLimitMinutes);
	File_Printf(stream, "random_seed %d\n", g_gameConfig.randomSeed);
	File_Printf(stream, "password %s\n", g_gameConfig.password);
	File_Printf(stream, "async_flag %d\n", g_gameConfig.asyncFlag);
	File_Printf(stream, "ai_opponents %d\n", g_gameConfig.aiOpponents);
	File_Printf(stream, "help_on %d\n", g_gameConfig.helpOn);
	File_Printf(stream, "combat_balance %d\n", g_gameConfig.combatBalance);
	File_Printf(stream, "taunt1 %s\n", g_gameConfig.taunts[0]);
	File_Printf(stream, "taunt2 %s\n", g_gameConfig.taunts[1]);
	File_Printf(stream, "taunt3 %s\n", g_gameConfig.taunts[2]);
	File_Printf(stream, "taunt4 %s\n", g_gameConfig.taunts[3]);

	File_Close(stream);

#endif
}

// FUNCTION: XVT 0x4BAAF0
int Config_UpdateNavigationAndRestoreDefaults(void) {
	enum {
		CONFIG_PAGE_NETWORK = 0,
		CONFIG_PAGE_SINGLEPLAYER_VIDEO = 1,
		CONFIG_PAGE_MULTIPLAYER_VIDEO = 2,
		CONFIG_PAGE_SOUND = 3,
		CONFIG_PAGE_JOYSTICK = 4,
		CONFIG_PAGE_TAUNTS = 5,
		CONFIG_NAVIGATION_SLOT_COUNT = 8,
		CONFIG_NAVIGATION_PAGE_SLOT_COUNT = 5,
		CONFIG_NAVIGATION_RESTORE_SLOT = 5,
		CONFIG_NAVIGATION_UNUSED_SLOT = 6,
		CONFIG_NAVIGATION_TAUNTS_SLOT = 7,
		CONFIG_BUTTON_LEFT = 22,
		CONFIG_BUTTON_RIGHT = 42,
		CONFIG_RESTORE_BUTTON_TOP = 306,
		CONFIG_RESTORE_BUTTON_BOTTOM = 330,
		CONFIG_TAUNTS_BUTTON_TOP = 254,
		CONFIG_TAUNTS_BUTTON_BOTTOM = 278,
		CONFIG_JOYSTICK_BUTTON_TOP = 226,
		CONFIG_JOYSTICK_BUTTON_BOTTOM = 250,
		CONFIG_BUTTON_VERTICAL_STEP = 28,
		CONFIG_CONTENT_LEFT = 84,
		CONFIG_CONTENT_TOP = 107,
		CONFIG_CONTENT_RIGHT = 604,
		CONFIG_CONTENT_BOTTOM = 433,
		CONFIG_JOYSTICK_SHADE_LEFT = 330,
		CONFIG_JOYSTICK_SHADE_TOP = 111,
		CONFIG_JOYSTICK_SHADE_RIGHT = 590,
		CONFIG_JOYSTICK_SHADE_BOTTOM = 415,
		CONFIG_JOYSTICK_SHADE_BLUE = 0x40,
		CONFIG_BUTTON_FONT_SIZE = 12,
		CONFIG_RESTORE_HOVER_SLOT = 16,
		CONFIG_TAUNTS_HOVER_SLOT = 18,
		CONFIG_JOYSTICK_HOVER_SLOT = 15,
		CONFIG_SOUND_HOVER_SLOT = 14,
		CONFIG_MULTIPLAYER_VIDEO_HOVER_SLOT = 13,
		CONFIG_SINGLEPLAYER_VIDEO_HOVER_SLOT = 12,
		CONFIG_NETWORK_HOVER_SLOT = 11,
		CONFIG_DEFAULT_SERVER_UPDATE_RATE = 8,
		CONFIG_DEFAULT_DISABLED = 0,
		CONFIG_DEFAULT_ENABLED = 1,
		CONFIG_DEFAULT_THREE_CHOICE_MIDDLE = 1,
		CONFIG_DEFAULT_THREE_CHOICE_HIGH = 2,
		CONFIG_DEFAULT_VIDEO_DENSITY = 2,
		CONFIG_DEFAULT_VIDEO_QUALITY = 10,
		CONFIG_DEFAULT_SOUND_VOLUME = 9,
		CONFIG_DEFAULT_MUSIC_VOLUME = 5,
		CONFIG_DEFAULT_JOYSTICK_BUTTON_LIMIT = 16,
		CONFIG_DEFAULT_DATAPAD_MUSIC_VOLUME = 0x8E38,
		CONFIG_DEFAULT_DATAPAD_MUSIC_TRACK = 7
	};

	struct {
		RECT rect;
		FrontendNavigationSlotState navigationSlotStates[CONFIG_NAVIGATION_SLOT_COUNT];
		int cursorY;
		int cursorX;
	} ui;

	int joystickButtonCount;
	int joystickButtonIndex;
	int joystickActionIndex;
	int pendingMenuScreen;
	int previousDatapadMusicEnabled;
	uint8_t selectedJoystickActionCode;

	ui.navigationSlotStates[0] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigationSlotStates[1] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigationSlotStates[2] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigationSlotStates[3] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigationSlotStates[4] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	pendingMenuScreen = g_pendingMenuScreen;
	if (pendingMenuScreen != CONFIG_PAGE_NETWORK ||
		(g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_NET_HOST &&
		 g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_NET_CLIENT)) {
		ui.navigationSlotStates[CONFIG_NAVIGATION_RESTORE_SLOT] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	} else {
		ui.navigationSlotStates[CONFIG_NAVIGATION_RESTORE_SLOT] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	}
	ui.navigationSlotStates[CONFIG_NAVIGATION_UNUSED_SLOT] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	ui.navigationSlotStates[CONFIG_NAVIGATION_TAUNTS_SLOT] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	if (pendingMenuScreen == CONFIG_PAGE_TAUNTS) {
		ui.navigationSlotStates[CONFIG_NAVIGATION_TAUNTS_SLOT] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else {
		ui.navigationSlotStates[pendingMenuScreen] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	}

	FrontendDraw_RectAssign(&ui.rect, CONFIG_BUTTON_LEFT, CONFIG_RESTORE_BUTTON_TOP, CONFIG_BUTTON_RIGHT,
							CONFIG_RESTORE_BUTTON_BOTTOM);
	FrontendCursor_GetPos(&ui.cursorX, &ui.cursorY);
	if (ui.navigationSlotStates[CONFIG_NAVIGATION_RESTORE_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
		FrontendDraw_PointInRect(&ui.rect, ui.cursorX, ui.cursorY) &&
		(FrontendMouse_GetLeftDown() != 0 || FrontendMouse_GetRightDown() != 0 ||
		 FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
		ui.navigationSlotStates[CONFIG_NAVIGATION_RESTORE_SLOT] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	FrontendButton_DrawEightSlotNavigationState(ui.navigationSlotStates);

	if (ui.navigationSlotStates[CONFIG_NAVIGATION_RESTORE_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		FrontendDraw_RectAssign(&ui.rect, CONFIG_BUTTON_LEFT, CONFIG_RESTORE_BUTTON_TOP, CONFIG_BUTTON_RIGHT,
								CONFIG_RESTORE_BUTTON_BOTTOM);
#ifdef XVT_MODERN
		if (XvtFrontendAction_Trigger(
				XVT_ACTION_CONFIG, 1,
				FrontendButton_DrawSpriteHitTest(
					&ui.rect, "config6u", "config6d", FrontendString_Get(FRONTSTR_420_RESTORE_DEFAULTS),
					CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_RESTORE_HOVER_SLOT, "jewelsound"))) {
			int result = FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(FRONTSTR_672_RESTORING_DEFAULTS_WILL_ERASE_ANY_CHANGES),
				FrontendString_Get(FRONTSTR_673_YOU_HAVE_MADE_TO_THESE_SETTINGS),
				FrontendString_Get(FRONTSTR_674_ARE_YOU_SURE_YOU_WANT_TO_DO_THIS),
				FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
			if (result == XVT_DIALOG_PENDING)
				return 0;
			XvtFrontendAction_Finish(XVT_ACTION_CONFIG);
			if (result) {
#else
		if (FrontendButton_DrawSpriteHitTest(
				&ui.rect, "config6u", "config6d", FrontendString_Get(FRONTSTR_420_RESTORE_DEFAULTS),
				CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_RESTORE_HOVER_SLOT, "jewelsound") != 0 &&
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(FRONTSTR_672_RESTORING_DEFAULTS_WILL_ERASE_ANY_CHANGES),
				FrontendString_Get(FRONTSTR_673_YOU_HAVE_MADE_TO_THESE_SETTINGS),
				FrontendString_Get(FRONTSTR_674_ARE_YOU_SURE_YOU_WANT_TO_DO_THIS),
				FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
#endif
				switch (g_pendingMenuScreen) {
					case CONFIG_PAGE_NETWORK:
						g_gameConfig.serverUpdateRate = CONFIG_DEFAULT_SERVER_UPDATE_RATE;
#ifdef XVT_MODERN
						g_gameConfig.networkType = NET_TRANSPORT_TCPIP;
						g_gameConfig.asyncFlag = CONFIG_DEFAULT_ENABLED;
#else
					g_gameConfig.networkType = CONFIG_DEFAULT_DISABLED;
					g_gameConfig.asyncFlag = CONFIG_DEFAULT_DISABLED;
#endif
						break;
					case CONFIG_PAGE_SINGLEPLAYER_VIDEO:
						g_gameConfig.backdrop[0] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.starDensity[0] = CONFIG_DEFAULT_VIDEO_DENSITY;
						g_gameConfig.debris[0] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.localLights[0] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.specular[0] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.diffuse[0] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.dither[0] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.textureRes[0] = CONFIG_DEFAULT_VIDEO_DENSITY;
						g_gameConfig.mipmap[0] = CONFIG_DEFAULT_VIDEO_QUALITY;
						g_gameConfig.lod[0] = CONFIG_DEFAULT_VIDEO_QUALITY;
						g_gameConfig.screenRes[0] = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.windowSize[0] = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.brightness[0] = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.bpp[0] = CONFIG_DEFAULT_DISABLED;
						g_gameConfig.use3dHardware[0] = FrontendDisplay_IsSecondaryDirectDrawActive();
						g_gameConfig.bilinear[0] = CONFIG_DEFAULT_ENABLED;
						break;
					case CONFIG_PAGE_MULTIPLAYER_VIDEO:
						g_gameConfig.backdrop[1] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.starDensity[1] = CONFIG_DEFAULT_VIDEO_DENSITY;
						g_gameConfig.debris[1] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.localLights[1] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.specular[1] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.diffuse[1] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.dither[1] = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.textureRes[1] = CONFIG_DEFAULT_VIDEO_DENSITY;
						g_gameConfig.mipmap[1] = CONFIG_DEFAULT_VIDEO_QUALITY;
						g_gameConfig.lod[1] = CONFIG_DEFAULT_VIDEO_QUALITY;
						g_gameConfig.screenRes[1] = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.windowSize[1] = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.bpp[1] = CONFIG_DEFAULT_DISABLED;
						g_gameConfig.brightness[1] = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.use3dHardware[1] = CONFIG_DEFAULT_DISABLED;
						g_gameConfig.bilinear[1] = CONFIG_DEFAULT_ENABLED;
						break;
					case CONFIG_PAGE_SOUND:
						previousDatapadMusicEnabled = g_gameConfig.datapadMusicEnabled;
						g_gameConfig.sfxExteriorEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.sfxInteriorEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.sfxEngineEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.sfxDatapadEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.voicePilotEnabled = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.voiceTacticalOfficerEnabled = CONFIG_DEFAULT_THREE_CHOICE_HIGH;
						g_gameConfig.voiceCommanderEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.voiceSpecialEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.musicEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.datapadMusicEnabled = CONFIG_DEFAULT_ENABLED;
						g_gameConfig.sfxDatapadVolume = CONFIG_DEFAULT_SOUND_VOLUME;
						g_gameConfig.sfxExteriorVolume = CONFIG_DEFAULT_SOUND_VOLUME;
						g_gameConfig.sfxInteriorVolume = CONFIG_DEFAULT_SOUND_VOLUME;
						g_gameConfig.sfxEngineVolume = CONFIG_DEFAULT_SOUND_VOLUME;
						g_gameConfig.voiceVolume = CONFIG_DEFAULT_SOUND_VOLUME;
						g_gameConfig.musicVolume = CONFIG_DEFAULT_MUSIC_VOLUME;
						if (previousDatapadMusicEnabled != CONFIG_DEFAULT_ENABLED) {
							CDAudio_SetAuxVolume(CONFIG_DEFAULT_DATAPAD_MUSIC_VOLUME);
							CDAudio_PlayTrackFromTime(CONFIG_DEFAULT_DATAPAD_MUSIC_TRACK, 0, 0);
						}
						break;
					case CONFIG_PAGE_JOYSTICK:
						memset(g_gameConfig.joyButtons, 0, sizeof(g_gameConfig.joyButtons));
						joystickButtonCount = Joystick_GetButtonCount(0);
						for (joystickButtonIndex = 0;
							 joystickButtonIndex < joystickButtonCount &&
							 joystickButtonIndex < CONFIG_DEFAULT_JOYSTICK_BUTTON_LIMIT;
							 ++joystickButtonIndex) {
							switch (joystickButtonIndex) {
								case 0:
									g_gameConfig.joyButtons[joystickButtonIndex] = (uint8_t)-100;
									break;
								case 1:
									g_gameConfig.joyButtons[joystickButtonIndex] = (uint8_t)-99;
									break;
								case 2:
									g_gameConfig.joyButtons[joystickButtonIndex] = 114;
									break;
								case 3:
									g_gameConfig.joyButtons[joystickButtonIndex] = 46;
									break;
								case 4:
									g_gameConfig.joyButtons[joystickButtonIndex] = 101;
									break;
								case 5:
									g_gameConfig.joyButtons[joystickButtonIndex] = 105;
									break;
								case 6:
									g_gameConfig.joyButtons[joystickButtonIndex] = 91;
									break;
								case 7:
									g_gameConfig.joyButtons[joystickButtonIndex] = 8;
									break;
								case 8:
									g_gameConfig.joyButtons[joystickButtonIndex] = 13;
									break;
								case 9:
									g_gameConfig.joyButtons[joystickButtonIndex] = 93;
									break;
							}
						}
						joystickActionIndex = 0;
						if (g_joystickEntryCount > 0) {
							selectedJoystickActionCode =
								g_gameConfig.joyButtons[g_configSelectedJoystickButtonIndex];
							do {
								if (g_joystickEntries[joystickActionIndex].actionCode ==
									selectedJoystickActionCode) {
									g_configSelectedJoystickActionIndex = joystickActionIndex;
								}
								++joystickActionIndex;
							} while (joystickActionIndex < g_joystickEntryCount);
						}
						break;
					case CONFIG_PAGE_TAUNTS:
						strcpy(g_gameConfig.taunts[0], FrontendString_Get(FRONTSTR_790_STAY_ON_TARGET));
						strcpy(g_gameConfig.taunts[1], FrontendString_Get(FRONTSTR_791_I_CAN_T_SHAKE_HIM));
						strcpy(g_gameConfig.taunts[2], FrontendString_Get(FRONTSTR_792_HE_S_HISTORY));
						strcpy(g_gameConfig.taunts[3], FrontendString_Get(FRONTSTR_793_WOOHOO));
						break;
				}
			}
#ifdef XVT_MODERN
		}
#endif
	}

	FrontendDraw_RectAssign(&ui.rect, CONFIG_BUTTON_LEFT, CONFIG_TAUNTS_BUTTON_TOP, CONFIG_BUTTON_RIGHT,
							CONFIG_TAUNTS_BUTTON_BOTTOM);
	if (g_pendingMenuScreen == CONFIG_PAGE_TAUNTS) {
		FrontendButton_DrawSpriteAndTooltip(
			&ui.rect, "config8d", FrontendString_Get(FRONTSTR_794_CUSTOM_TAUNTS), CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (FrontendButton_DrawSpriteHitTest(
				   &ui.rect, "config8u", "config8u", FrontendString_Get(FRONTSTR_794_CUSTOM_TAUNTS),
				   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_TAUNTS_HOVER_SLOT, "jewelsound") != 0) {
		g_configDrawStaticControlBackground = 1;
		g_pendingMenuScreen = CONFIG_PAGE_TAUNTS;
		g_activeTextFieldId = 0;
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backconfig", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDraw_RectAssign(&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP, CONFIG_CONTENT_RIGHT,
								CONFIG_CONTENT_BOTTOM);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
	}

	FrontendDraw_RectAssign(&ui.rect, CONFIG_BUTTON_LEFT, CONFIG_JOYSTICK_BUTTON_TOP, CONFIG_BUTTON_RIGHT,
							CONFIG_JOYSTICK_BUTTON_BOTTOM);
#ifdef XVT_MODERN
	if (FrontendButton_DrawSpriteHitTest(&ui.rect, "config5u", "config5u", "OpenXvT Settings",
										 CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_JOYSTICK_HOVER_SLOT,
										 "jewelsound"))
		XvtPort_RequestSettings();
#else
	if (g_pendingMenuScreen == CONFIG_PAGE_JOYSTICK) {
		FrontendButton_DrawSpriteAndTooltip(&ui.rect, "config5d",
											FrontendString_Get(FRONTSTR_415_JOYSTICK_OPTIONS),
											CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (FrontendButton_DrawSpriteHitTest(
				   &ui.rect, "config5u", "config5u", FrontendString_Get(FRONTSTR_415_JOYSTICK_OPTIONS),
				   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_JOYSTICK_HOVER_SLOT, "jewelsound") != 0) {
		g_configDrawStaticControlBackground = 1;
		g_pendingMenuScreen = CONFIG_PAGE_JOYSTICK;
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backconfig", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDraw_RectAssign(&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP, CONFIG_CONTENT_RIGHT,
								CONFIG_CONTENT_BOTTOM);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDraw_RectAssign(&ui.rect, CONFIG_JOYSTICK_SHADE_LEFT, CONFIG_JOYSTICK_SHADE_TOP,
								CONFIG_JOYSTICK_SHADE_RIGHT, CONFIG_JOYSTICK_SHADE_BOTTOM);
		FrontendDraw_FillRectTranslucent(&ui.rect, 0, 0,
										 FrontendDisplay_PackRGB(0, 0, CONFIG_JOYSTICK_SHADE_BLUE));
		FrontendDisplay_UnlockOffscreenSurface(1);
	}
#endif

	FrontendDraw_RectOffsetXY(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_pendingMenuScreen == CONFIG_PAGE_SOUND) {
		FrontendButton_DrawSpriteAndTooltip(
			&ui.rect, "config4d", FrontendString_Get(FRONTSTR_310_SOUND_OPTIONS), CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (FrontendButton_DrawSpriteHitTest(
				   &ui.rect, "config4u", "config4u", FrontendString_Get(FRONTSTR_310_SOUND_OPTIONS),
				   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_SOUND_HOVER_SLOT, "jewelsound") != 0) {
		g_configDrawStaticControlBackground = 1;
		g_pendingMenuScreen = CONFIG_PAGE_SOUND;
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backconfig", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDraw_RectAssign(&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP, CONFIG_CONTENT_RIGHT,
								CONFIG_CONTENT_BOTTOM);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
	}

	FrontendDraw_RectOffsetXY(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_pendingMenuScreen == CONFIG_PAGE_MULTIPLAYER_VIDEO) {
		FrontendButton_DrawSpriteAndTooltip(
			&ui.rect, "config3d", FrontendString_Get(FRONTSTR_641_MULTIPLAYER_FLIGHT_ENGINE_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (FrontendButton_DrawSpriteHitTest(
				   &ui.rect, "config3u", "config3u",
				   FrontendString_Get(FRONTSTR_641_MULTIPLAYER_FLIGHT_ENGINE_OPTIONS),
				   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_MULTIPLAYER_VIDEO_HOVER_SLOT, "jewelsound") != 0) {
		g_configDrawStaticControlBackground = 1;
		g_pendingMenuScreen = CONFIG_PAGE_MULTIPLAYER_VIDEO;
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backconfig", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDraw_RectAssign(&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP, CONFIG_CONTENT_RIGHT,
								CONFIG_CONTENT_BOTTOM);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
	}

	FrontendDraw_RectOffsetXY(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_pendingMenuScreen == CONFIG_PAGE_SINGLEPLAYER_VIDEO) {
		FrontendButton_DrawSpriteAndTooltip(
			&ui.rect, "config2d", FrontendString_Get(FRONTSTR_219_SINGLE_PLAYER_FLIGHT_ENGINE_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (FrontendButton_DrawSpriteHitTest(
				   &ui.rect, "config2u", "config2u",
				   FrontendString_Get(FRONTSTR_219_SINGLE_PLAYER_FLIGHT_ENGINE_OPTIONS),
				   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_SINGLEPLAYER_VIDEO_HOVER_SLOT, "jewelsound") != 0) {
		g_configDrawStaticControlBackground = 1;
		g_pendingMenuScreen = CONFIG_PAGE_SINGLEPLAYER_VIDEO;
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backconfig", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDraw_RectAssign(&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP, CONFIG_CONTENT_RIGHT,
								CONFIG_CONTENT_BOTTOM);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
	}

	FrontendDraw_RectOffsetXY(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_pendingMenuScreen == CONFIG_PAGE_NETWORK) {
		FrontendButton_DrawSpriteAndTooltip(&ui.rect, "config1d",
											FrontendString_Get(FRONTSTR_304_MULTIPLAYER_CONNECTION_OPTIONS),
											CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (FrontendButton_DrawSpriteHitTest(
				   &ui.rect, "config1u", "config1u",
				   FrontendString_Get(FRONTSTR_304_MULTIPLAYER_CONNECTION_OPTIONS), CONFIG_BUTTON_FONT_SIZE,
				   0, CONFIG_NETWORK_HOVER_SLOT, "jewelsound") != 0) {
		Keyboard_FlushCharBuffer();
		g_pendingMenuScreen = CONFIG_PAGE_NETWORK;
		g_activeTextFieldId = 0;
		g_configDrawStaticControlBackground = 1;
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backconfig", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDraw_RectAssign(&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP, CONFIG_CONTENT_RIGHT,
								CONFIG_CONTENT_BOTTOM);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
	}

	return 1;
}

// FUNCTION: XVT 0x4BB680
void Config_NetworkOptionsScreen(void) {
	enum {
		TITLE_LEFT = 84,
		TITLE_TOP = 90,
		TITLE_RIGHT = 604,
		TITLE_BOTTOM = 106,
		LABEL_LEFT = 88,
		LABEL_TOP = 111,
		LABEL_RIGHT = 332,
		LABEL_BOTTOM = 125,
		FONT_LABEL = 12,
		FONT_TITLE = 15,
		FIELD_WIDTH = 200
	};

	RECT rect;
	RECT sourceRect;
	int labelWidth;
#ifndef XVT_MODERN
	int cursorX;
	int cursorY;
	int optionWidth;
	int buttonWidth;
#endif
	uint8_t selectedOption;

	FrontendDraw_RectAssign(&sourceRect, TITLE_LEFT, TITLE_TOP, TITLE_RIGHT, TITLE_BOTTOM);
	FrontendText_DrawCentered(FONT_TITLE, FrontendString_Get(FRONTSTR_304_MULTIPLAYER_CONNECTION_OPTIONS),
							  &sourceRect, 0xFFFF);
#ifdef XVT_MODERN
	FrontendDraw_RectAssign(&sourceRect, LABEL_LEFT, LABEL_TOP, LABEL_RIGHT, LABEL_BOTTOM);
#else
	labelWidth = FrontendText_MeasureWidth(FrontendString_Get(FRONTSTR_308_IP_ADDRESS_NAME), FONT_LABEL);
	optionWidth = FrontendText_MeasureWidth(FrontendString_Get(FRONTSTR_309_PHONE_NUMBER), FONT_LABEL);
	if (optionWidth < labelWidth)
		optionWidth = labelWidth;
	FrontImage_GetResourceRect("offslot", &rect);
	buttonWidth = rect.right - rect.left + 1;
	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontendDraw_RectAssign(&sourceRect, LABEL_LEFT, LABEL_TOP, LABEL_RIGHT, LABEL_BOTTOM);
	if (g_unusedFrontendConcourseHostLatch == 0 &&
		g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER)
		FrontendText_DrawAlignedInRect(
			FONT_LABEL,
			FrontendString_Get(
				FRONTSTR_692_CONNECTION_TYPE_YOU_CANNOT_CHANGE_THESE_OPTIONS_WHILE_HOSTING_OR_JOINING_A_NETWORK_GAME),
			&sourceRect, 0, 1, 0xFFFF);
	else
		FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_477_SELECT_CONNECTION_TYPE),
									   &sourceRect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&sourceRect, 0, 20);
	sourceRect.right = sourceRect.left + 127;
	FrontendDraw_RectCopy(&rect, &sourceRect);
	rect.right = rect.left + buttonWidth;
	if (g_configDrawStaticControlBackground != 0 &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
		FrontendDisplay_UnlockOffscreenSurface(0);
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
	}
	if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
		(FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
		if (g_gameConfig.networkType != NET_TRANSPORT_IPX) {
			if (g_gameConfig.sfxDatapadEnabled != 0)
				FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			if (g_gameConfig.networkType != NET_TRANSPORT_IPX)
				g_gameConfig.asyncFlag = 0;
		}
		g_gameConfig.networkType = NET_TRANSPORT_IPX;
	}
	if (g_gameConfig.networkType == NET_TRANSPORT_IPX)
		FrontImage_DrawSprite("3conbtn", rect.left, rect.top);
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_305_IPX), &rect, 0, 1,
								   g_colorLightBlue);

	FrontendDraw_RectOffsetXY(&sourceRect, 0, 25);
	sourceRect.right = sourceRect.left + 127;
	FrontendDraw_RectCopy(&rect, &sourceRect);
	rect.right = rect.left + buttonWidth;
	if (g_configDrawStaticControlBackground != 0 &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
		FrontendDisplay_UnlockOffscreenSurface(0);
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
	}
	if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
		(FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
		if (g_gameConfig.networkType != NET_TRANSPORT_TCPIP) {
			if (g_gameConfig.sfxDatapadEnabled != 0)
				FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			if (g_gameConfig.networkType != NET_TRANSPORT_TCPIP)
				g_gameConfig.asyncFlag = 1;
		}
		g_gameConfig.networkType = NET_TRANSPORT_TCPIP;
	}
	if (g_gameConfig.networkType == NET_TRANSPORT_TCPIP)
		FrontImage_DrawSprite("3conbtn", rect.left, rect.top);
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_306_TCP_IP), &rect, 0, 1,
								   g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&rect, 100, 0);
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_308_IP_ADDRESS_NAME), &rect, 0, 1,
								   g_colorLightBlue);
	rect.left += optionWidth + 15;
	rect.right = rect.left + FIELD_WIDTH;
	FrontendDraw_RectInsetXY(&rect, 0, -2);
	FrontendDraw_FillRectTranslucent(&rect, 0, 0, g_editableFieldBackgroundColor);
	if (FrontendText_DrawEditableField(&rect, g_gameConfig.ipAddress, 64, 0, FONT_LABEL, NULL) != 0)
		g_activeTextFieldId = 1;

	FrontendDraw_RectOffsetXY(&sourceRect, 0, 25);
	sourceRect.right = sourceRect.left + 127;
	FrontendDraw_RectCopy(&rect, &sourceRect);
	rect.right = rect.left + buttonWidth;
	if (g_configDrawStaticControlBackground != 0 &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
		FrontendDisplay_UnlockOffscreenSurface(0);
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
	}
	if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
		(FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
		if (g_gameConfig.networkType != NET_TRANSPORT_MODEM) {
			if (g_gameConfig.sfxDatapadEnabled != 0)
				FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			if (g_gameConfig.networkType != NET_TRANSPORT_MODEM)
				g_gameConfig.asyncFlag = 0;
		}
		g_gameConfig.networkType = NET_TRANSPORT_MODEM;
	}
	if (g_gameConfig.networkType == NET_TRANSPORT_MODEM)
		FrontImage_DrawSprite("3conbtn", rect.left, rect.top);
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_307_DIRECT_MODEM), &rect, 0, 1,
								   g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&rect, 100, 0);
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_309_PHONE_NUMBER), &rect, 0, 1,
								   g_colorLightBlue);
	rect.left += optionWidth + 15;
	rect.right = rect.left + FIELD_WIDTH;
	FrontendDraw_RectInsetXY(&rect, 0, -2);
	FrontendDraw_FillRectTranslucent(&rect, 0, 0, g_editableFieldBackgroundColor);
	if (FrontendText_DrawEditableField(&rect, g_gameConfig.phoneNumber, 64, 1, FONT_LABEL, NULL) != 0)
		g_activeTextFieldId = 2;

	FrontendDraw_RectOffsetXY(&sourceRect, 0, 25);
	sourceRect.right = sourceRect.left + 127;
	FrontendDraw_RectCopy(&rect, &sourceRect);
	buttonWidth += rect.left;
	rect.right = buttonWidth;
	if (g_configDrawStaticControlBackground != 0 &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
		FrontendDisplay_UnlockOffscreenSurface(0);
		FrontImage_DrawSpriteTranslucent("offslot", rect.left, rect.top);
	}
	if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
		(g_unusedFrontendConcourseHostLatch != 0 ||
		 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
		(FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
		if (g_gameConfig.networkType != NET_TRANSPORT_SERIAL) {
			if (g_gameConfig.sfxDatapadEnabled != 0)
				FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			if (g_gameConfig.networkType != NET_TRANSPORT_SERIAL)
				g_gameConfig.asyncFlag = 0;
		}
		g_gameConfig.networkType = NET_TRANSPORT_SERIAL;
	}
	if (g_gameConfig.networkType == NET_TRANSPORT_SERIAL)
		FrontImage_DrawSprite("3conbtn", rect.left, rect.top);
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_449_DIRECT_SERIAL), &rect, 0, 1,
								   g_colorLightBlue);

#endif
	FrontendDraw_RectOffsetXY(&sourceRect, 0, 35);
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_478_GENERAL_CONNECTION_OPTIONS),
								   &sourceRect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&sourceRect, 0, 20);
	FrontendDraw_RectCopy(&rect, &sourceRect);
	FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_482_GAME_SESSION_PASSWORD), &rect,
								   0, 1, g_colorLightBlue);
	labelWidth =
		FrontendText_MeasureWidth(FrontendString_Get(FRONTSTR_482_GAME_SESSION_PASSWORD), FONT_LABEL);
	rect.left += labelWidth + 15;
	rect.right = rect.left + FIELD_WIDTH;
	FrontendDraw_RectInsetXY(&rect, 0, -2);
	FrontendDraw_FillRectTranslucent(&rect, 0, 0, g_editableFieldBackgroundColor);
	if (FrontendText_DrawEditableField(&rect, g_gameConfig.password, 16, 2, FONT_LABEL, NULL) != 0)
		g_activeTextFieldId = 0;

	FrontendDraw_RectOffsetXY(&sourceRect, 0, 35);
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NET_CLIENT) {
		FrontendText_DrawAlignedInRect(
			FONT_LABEL,
			FrontendString_Get(FRONTSTR_747_HOST_OPTIONS_YOU_ARE_A_CLIENT_YOU_CANNOT_CHANGE_THESE_SETTINGS),
			&sourceRect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 20);
		FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_479_PLAYING_OVER_THE_INTERNET),
									   &sourceRect, 0, 1, g_colorLightBlue);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 17);
		Config_DrawNetworkOptionCycleDisabled(&g_gameConfig.asyncFlag, &sourceRect, FRONTSTR_480_NO);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 20);
		FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_743_HOST_SERVER_UPDATE_RATE),
									   &sourceRect, 0, 1, g_colorLightBlue);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 17);
		selectedOption = (uint8_t)((g_gameConfig.serverUpdateRate >> 1) - 2);
		Config_DrawThreeOptionBar(&selectedOption, &sourceRect, FRONTSTR_744_LOW);
	} else {
		FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_742_HOST_SERVER_OPTIONS),
									   &sourceRect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 20);
		FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_479_PLAYING_OVER_THE_INTERNET),
									   &sourceRect, 0, 1, g_colorLightBlue);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 17);
		Config_DrawOptionCycle(&g_gameConfig.asyncFlag, &sourceRect, FRONTSTR_480_NO);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 20);
		FrontendText_DrawAlignedInRect(FONT_LABEL, FrontendString_Get(FRONTSTR_743_HOST_SERVER_UPDATE_RATE),
									   &sourceRect, 0, 1, g_colorLightBlue);
		FrontendDraw_RectOffsetXY(&sourceRect, 0, 17);
		selectedOption = (uint8_t)((g_gameConfig.serverUpdateRate >> 1) - 2);
		Config_DrawThreeChoiceOption(&selectedOption, &sourceRect, FRONTSTR_744_LOW);
		g_gameConfig.serverUpdateRate = (uint8_t)(2 * selectedOption + 4);
	}
}

// FUNCTION: XVT 0x4BC1C0
void Config_DrawNetworkOptionCycleDisabled(const uint8_t* value, const RECT* rect,
										   FrontendStringId valueBaseStrId) {
	int optionIndex;
	RECT slotRect;
	RECT spriteRect;
	int cursorX;
	int cursorY;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontImage_GetResourceRect("offslot", &spriteRect);
	FrontendDraw_RectCopy(&slotRect, rect);
	for (optionIndex = 0; optionIndex < 2; ++optionIndex) {
		slotRect.right = slotRect.left + spriteRect.right - spriteRect.left + 1;
		if (g_configDrawStaticControlBackground != 0) {
			FrontendDisplay_LockOffscreenSurface();
			if (optionIndex == 0)
				FrontImage_DrawSpriteTranslucent("offslot", slotRect.left, slotRect.top);
			else
				FrontImage_DrawSpriteTranslucent("onslot", slotRect.left, slotRect.top);
			FrontendDisplay_UnlockOffscreenSurface(0);
			if (optionIndex == 0)
				FrontImage_DrawSpriteTranslucent("offslot", slotRect.left, slotRect.top);
			else
				FrontImage_DrawSpriteTranslucent("onslot", slotRect.left, slotRect.top);
		}

		if (*value == optionIndex)
			FrontImage_DrawSprite("3conbtn", slotRect.left, slotRect.top);
		slotRect.left = slotRect.right + 5;
		slotRect.right = slotRect.left + 100;
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(valueBaseStrId + optionIndex), &slotRect, 0,
									   1, g_colorLightBlue);
		slotRect.left = rect->left + 130;
	}
}

// FUNCTION: XVT 0x4BC2F0
void Config_DrawThreeOptionBar(const uint8_t* selectedOption, const RECT* barRect,
							   FrontendStringId firstOptionStringId) {
	RECT optionRect;
	RECT spriteRect;
	int cursorX;
	int cursorY;
	int buttonWidth;
	int right;

	if (g_configDrawStaticControlBackground != 0) {
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("3conbar", barRect->left, barRect->top);
		FrontendDisplay_UnlockOffscreenSurface(0);
		FrontImage_DrawSpriteTranslucent("3conbar", barRect->left, barRect->top);
	}

	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontImage_GetResourceRect("3conbtn", &spriteRect);
	buttonWidth = spriteRect.right - spriteRect.left + 1;
	FrontImage_GetResourceRect("3conbar", &spriteRect);
	FrontendDraw_RectOffsetXY(&spriteRect, barRect->left, barRect->top);
	FrontendDraw_RectCopy(&optionRect, barRect);

	optionRect.right = optionRect.left + buttonWidth;
	if (*selectedOption == 0)
		FrontImage_DrawSprite("3conbtn", optionRect.left, optionRect.top);
	FrontendDraw_RectOffsetXY(&optionRect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(firstOptionStringId), &optionRect, 0, 1,
								   g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&optionRect, 0, -15);

	optionRect.left = spriteRect.left + ((spriteRect.right - buttonWidth - spriteRect.left) >> 1) + 1;
	optionRect.right = optionRect.left + buttonWidth;
	if (*selectedOption == 1)
		FrontImage_DrawSprite("3conbtn", optionRect.left, optionRect.top);
	FrontendDraw_RectOffsetXY(&optionRect, 0, 15);
	FrontendText_DrawCentered(12, FrontendString_Get(firstOptionStringId + 1), &optionRect, g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&optionRect, 0, -15);

	optionRect.right = spriteRect.right;
	optionRect.left = spriteRect.right - buttonWidth + 1;
	if (*selectedOption == 2)
		FrontImage_DrawSprite("3conbtn", optionRect.left, optionRect.top);
	FrontendDraw_RectOffsetXY(&optionRect, 0, 15);
	right = spriteRect.right;
	optionRect.left = right - FrontendText_MeasureWidth(FrontendString_Get(firstOptionStringId + 2), 12) + 1;
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(firstOptionStringId + 2), &optionRect, 0, 1,
								   g_colorLightBlue);
}

// FUNCTION: XVT 0x4BC520
void Config_SoundOptionsScreen(void) {
	RECT rect;
	int previousVolume;
	int previousDatapadMusic;

	FrontendDraw_RectAssign(&rect, 84, 90, 604, 106);
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_310_SOUND_OPTIONS), &rect, 0xFFFF);

	FrontendDraw_RectAssign(&rect, 88, 111, 332, 125);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_408_DATAPAD_SFX), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.sfxDatapadEnabled, &rect, FRONTSTR_236_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	previousVolume = g_gameConfig.sfxDatapadVolume;
	Config_DrawOptionSliderImpl(&g_gameConfig.sfxDatapadVolume, &rect, 10, FRONTSTR_403_VOLUME_LOW, 0);
	if (g_gameConfig.sfxDatapadVolume != previousVolume && g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
	}
	FrontendDraw_RectOffsetXY(&rect, 0, 55);

	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_405_FLIGHT_ENGINE_EXTERIOR_SFX), &rect, 0,
								   1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.sfxExteriorEnabled, &rect, FRONTSTR_236_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	previousVolume = g_gameConfig.sfxExteriorVolume;
	Config_DrawOptionSliderImpl(&g_gameConfig.sfxExteriorVolume, &rect, 10, FRONTSTR_403_VOLUME_LOW, 0);
	if (g_gameConfig.sfxExteriorVolume != previousVolume && g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxExteriorVolume, 63);
	}
	FrontendDraw_RectOffsetXY(&rect, 0, 55);

	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_406_COCKPIT_INTERIOR_SFX), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.sfxInteriorEnabled, &rect, FRONTSTR_236_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	previousVolume = g_gameConfig.sfxInteriorVolume;
	Config_DrawOptionSliderImpl(&g_gameConfig.sfxInteriorVolume, &rect, 10, FRONTSTR_403_VOLUME_LOW, 0);
	if (g_gameConfig.sfxInteriorVolume != previousVolume && g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxInteriorVolume, 63);
	}
	FrontendDraw_RectOffsetXY(&rect, 0, 55);

	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_407_ENGINE_SOUND), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.sfxEngineEnabled, &rect, FRONTSTR_236_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	previousVolume = g_gameConfig.sfxEngineVolume;
	Config_DrawOptionSliderImpl(&g_gameConfig.sfxEngineVolume, &rect, 10, FRONTSTR_403_VOLUME_LOW, 0);
	if (g_gameConfig.sfxEngineVolume != previousVolume && g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxEngineVolume, 63);
	}

	FrontendDraw_RectAssign(&rect, 356, 111, 600, 125);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_409_PILOT_MESSAGES), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawThreeChoiceOption(&g_gameConfig.voicePilotEnabled, &rect, FRONTSTR_400_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 35);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_410_TACTICAL_OFFICER_MESSAGES), &rect, 0,
								   1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawThreeChoiceOption(&g_gameConfig.voiceTacticalOfficerEnabled, &rect, FRONTSTR_400_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 35);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_411_COMMANDER_MESSAGES), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.voiceCommanderEnabled, &rect, FRONTSTR_236_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_412_SPECIAL_MISSION_MESSAGES), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.voiceSpecialEnabled, &rect, FRONTSTR_236_OFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 25);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_414_VOICE_VOLUME), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	previousVolume = g_gameConfig.voiceVolume;
	Config_DrawOptionSliderImpl(&g_gameConfig.voiceVolume, &rect, 10, FRONTSTR_403_VOLUME_LOW, 0);
	if (g_gameConfig.voiceVolume != previousVolume && g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.voiceVolume, 63);
	}

	FrontendDraw_RectOffsetXY(&rect, 0, 35);
	previousDatapadMusic = g_gameConfig.datapadMusicEnabled;
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_735_DATAPAD_MUSIC), &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.datapadMusicEnabled, &rect, FRONTSTR_236_OFF);
	if (g_gameConfig.datapadMusicEnabled != previousDatapadMusic) {
		if (g_gameConfig.datapadMusicEnabled != 0) {
			CDAudio_SetAuxVolume(0xFFFF * g_gameConfig.musicVolume / 9);
			CDAudio_PlayTrackFromTime(7, 0, 0);
		} else {
			CDAudio_StopCurrentTrack();
		}
	}
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_413_FLIGHT_ENGINE_MUSIC), &rect, 0, 1,
								   0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionCycle(&g_gameConfig.musicEnabled, &rect, FRONTSTR_236_OFF);
	previousVolume = g_gameConfig.musicVolume;
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	Config_DrawOptionSliderImpl(&g_gameConfig.musicVolume, &rect, 10, FRONTSTR_403_VOLUME_LOW, 1);
	if (g_gameConfig.musicVolume != previousVolume && g_gameConfig.datapadMusicEnabled != 0) {
		CDAudio_SetAuxVolume(0xFFFF * g_gameConfig.musicVolume / 9);
	}
}

// FUNCTION: XVT 0x4BCC10
void Config_JoystickRemapScreen(void) {
	RECT rect;
	int pressedButton;
	int povDirection;
	int cursorX;
	int cursorY;
	int buttonCount;
	int maximumExclusive;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontendDraw_RectAssign(&rect, 84, 90, 604, 106);
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_415_JOYSTICK_OPTIONS), &rect, 0xFFFF);

	FrontendDraw_RectAssign(&rect, 88, 111, 322, 125);
	{
		int selectedButton = g_configSelectedJoystickButtonIndex;
		if (selectedButton >= 16) {
			sprintf(g_frontendScratchBuffer, "%c%s %s %c%s", 4, FrontendString_Get(FRONTSTR_646_JOYSTICK_POV),
					FrontendString_Get(FRONTSTR_647_UP + selectedButton - 16), 1,
					FrontendString_Get(FRONTSTR_417_CURRENTLY_MAPPED_TO));
		} else {
			sprintf(g_frontendScratchBuffer, "%c%s %d %c%s", 4,
					FrontendString_Get(FRONTSTR_416_JOYSTICK_BUTTON), selectedButton + 1, 1,
					FrontendString_Get(FRONTSTR_417_CURRENTLY_MAPPED_TO));
		}
	}
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	if (g_joystickEntryCount != 0) {
		int selectedAction = g_configSelectedJoystickActionIndex;
		if (selectedAction != 0)
			sprintf(g_frontendScratchBuffer, "%c%s: %c%s", 2, g_joystickEntries[selectedAction].name, 1,
					g_joystickEntries[selectedAction].description);
		else
			sprintf(g_frontendScratchBuffer, "%c%s", 2, g_joystickEntries[selectedAction].name);
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
	}

	FrontendDraw_RectOffsetXY(&rect, 0, 30);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_653_SELECT_A_BUTTON_BY_PICKING_FROM),
								   &rect, 0, 1, g_colorGreen);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_654_THE_LIST_BELOW_OR_PRESSING_ONE_OF),
								   &rect, 0, 1, g_colorGreen);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_655_YOUR_JOYSTICK_BUTTONS_THEN_PICK),
								   &rect, 0, 1, g_colorGreen);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_656_FROM_THE_LIST_OF_AVAILABLE_KEYS),
								   &rect, 0, 1, g_colorGreen);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_657_TO_REMAP), &rect, 0, 1, g_colorGreen);

	{
		povDirection = Joystick_GetPovDirection(0);
		pressedButton = Joystick_GetFirstPressedButton(0);
		if ((pressedButton != -1 && pressedButton < 16) || povDirection != 0) {
			if (pressedButton != -1) {
				int scrollOffset;
				if (g_configSelectedJoystickButtonIndex != pressedButton &&
					g_gameConfig.sfxDatapadEnabled != 0)
					FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume,
											  63);
				scrollOffset = g_configJoystickButtonScrollOffset;
				g_configSelectedJoystickButtonIndex = pressedButton;
				if (pressedButton < scrollOffset || pressedButton - scrollOffset >= 10)
					g_configJoystickButtonScrollOffset = pressedButton;
			} else if (povDirection != 0) {
				if (povDirection - g_configSelectedJoystickButtonIndex != -15 &&
					g_gameConfig.sfxDatapadEnabled != 0)
					FrontendSound_PlayUISound("configsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume,
											  63);
				g_configSelectedJoystickButtonIndex = povDirection + 15;
				{
					int povListIndex = povDirection + Joystick_GetButtonCount(0) - 1;
					int scrollOffset = g_configJoystickButtonScrollOffset;
					if (povListIndex < scrollOffset || povListIndex - scrollOffset >= 10)
						g_configJoystickButtonScrollOffset = povListIndex;
				}
			}
			{
				int entryIndex = 0;
				int entryCount = g_joystickEntryCount;
				if (entryCount > entryIndex) {
					JoystickEntry* entry = g_joystickEntries;
					uint8_t actionCode = g_gameConfig.joyButtons[g_configSelectedJoystickButtonIndex];
					do {
						if (entry->actionCode == actionCode)
							g_configSelectedJoystickActionIndex = entryIndex;
						++entry;
						++entryIndex;
					} while (entryIndex < entryCount);
				}
			}
		}
	}

	buttonCount = Joystick_GetButtonCount(0);
	maximumExclusive = buttonCount;
	if (Joystick_HasPov(0) != 0)
		maximumExclusive += 4;

	FrontendDraw_RectAssign(&rect, 88, 246, 322, 260);
	FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_652_REMAPPABLE_BUTTONS), &rect,
							  g_colorLightBlue);
	if (maximumExclusive > 10) {
		FrontendDraw_RectAssign(&rect, 313, 261, 322, 415);
		g_configJoystickButtonScrollOffset = FrontendScrollbar_Draw(
			&rect, g_configJoystickButtonScrollOffset, maximumExclusive, 0, 5, (unsigned int)g_colorNavy, 1);
		FrontendDraw_RectAssign(&rect, 88, 261, 312, 415);
	} else {
		FrontendDraw_RectAssign(&rect, 88, 261, 322, 415);
	}
	FrontendDraw_Rect(&rect, 0, 0, 0xFFFF, 0);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	FrontendDraw_Rect(&rect, 0, 0, 0, 0);
	rect.bottom = rect.top + 14;
	FrontendDraw_RectInsetXY(&rect, 2, 0);

	{
		int visibleButtonIndex;
		int entryCount;
		for (visibleButtonIndex = g_configJoystickButtonScrollOffset;
			 visibleButtonIndex < g_configJoystickButtonScrollOffset + 10; ++visibleButtonIndex) {
			int actionIndex;
			uint16_t color;
			entryCount = g_joystickEntryCount;
			if (visibleButtonIndex >= maximumExclusive)
				break;
			actionIndex = 0;
			if (entryCount > 0) {
				JoystickEntry* entries = g_joystickEntries;
				for (; actionIndex < entryCount; ++actionIndex) {
					if (visibleButtonIndex < buttonCount) {
						if (g_gameConfig.joyButtons[visibleButtonIndex] == entries[actionIndex].actionCode)
							break;
					} else if (g_gameConfig.joyButtons[visibleButtonIndex - buttonCount + 16] ==
							   entries[actionIndex].actionCode) {
						break;
					}
				}
			}
			if (actionIndex < entryCount) {
				if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) != 0) {
					FrontendDraw_RectOutline(&rect, 0, 0, g_colorGreen);
					if (FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0) {
						if (visibleButtonIndex < buttonCount) {
							if (g_configSelectedJoystickButtonIndex != visibleButtonIndex &&
								g_gameConfig.sfxDatapadEnabled != 0)
								FrontendSound_PlayUISound("configsound", 1, 0, 255,
														  12 * g_gameConfig.sfxDatapadVolume, 63);
							g_configSelectedJoystickButtonIndex = visibleButtonIndex;
						} else {
							if (visibleButtonIndex + buttonCount - g_configSelectedJoystickButtonIndex !=
									16 &&
								g_gameConfig.sfxDatapadEnabled != 0)
								FrontendSound_PlayUISound("configsound", 1, 0, 255,
														  12 * g_gameConfig.sfxDatapadVolume, 63);
							g_configSelectedJoystickButtonIndex = visibleButtonIndex - buttonCount + 16;
						}
						g_configSelectedJoystickActionIndex = actionIndex;
					}
				}
				if (visibleButtonIndex < buttonCount) {
					color = 0xFFFF;
					if (g_configSelectedJoystickButtonIndex == visibleButtonIndex)
						color = (uint16_t)g_colorLightBlue;
					sprintf(g_frontendScratchBuffer, "%s %d: %c%s",
							FrontendString_Get(FRONTSTR_416_JOYSTICK_BUTTON), visibleButtonIndex + 1, 2,
							g_joystickEntries[actionIndex].name);
					FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, color);
				} else {
					color = 0xFFFF;
					if (visibleButtonIndex - buttonCount - g_configSelectedJoystickButtonIndex == -16)
						color = (uint16_t)g_colorLightBlue;
					sprintf(g_frontendScratchBuffer, "%s %s: %c%s",
							FrontendString_Get(FRONTSTR_646_JOYSTICK_POV),
							FrontendString_Get(FRONTSTR_647_UP + visibleButtonIndex - buttonCount), 2,
							g_joystickEntries[actionIndex].name);
					FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, color);
				}
			}
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
		}
	}

	FrontendDraw_RectAssign(&rect, 330, 111, 590, 125);
	FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_651_AVAILABLE_KEYS), &rect, g_colorLightBlue);
	FrontendDraw_RectAssign(&rect, 591, 126, 600, 415);
	g_configJoystickActionScrollOffset = FrontendScrollbar_Draw(
		&rect, g_configJoystickActionScrollOffset, g_joystickEntryCount, 0, 5, (unsigned int)g_colorNavy, 2);
	FrontendDraw_RectAssign(&rect, 330, 126, 590, 415);
	FrontendDraw_Rect(&rect, 0, 0, 0xFFFF, 0);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	FrontendDraw_Rect(&rect, 0, 0, 0, 0);
	rect.bottom = rect.top + 14;
	FrontendDraw_RectInsetXY(&rect, 2, 0);
	{
		int actionIndex;
		for (actionIndex = g_configJoystickActionScrollOffset;
			 actionIndex < g_configJoystickActionScrollOffset + 19 && actionIndex < g_joystickEntryCount;
			 ++actionIndex) {
			JoystickEntry* entry = &g_joystickEntries[actionIndex];
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) != 0) {
				FrontendDraw_RectOutline(&rect, 0, 0, g_colorGreen);
				if (FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0) {
					if (actionIndex != g_configSelectedJoystickActionIndex &&
						g_gameConfig.sfxDatapadEnabled != 0)
						FrontendSound_PlayUISound("settingsound", 1, 0, 255,
												  12 * g_gameConfig.sfxDatapadVolume, 63);
					{
						uint8_t selectedActionCode = entry->actionCode;
						g_configSelectedJoystickActionIndex = actionIndex;
						g_gameConfig.joyButtons[g_configSelectedJoystickButtonIndex] = selectedActionCode;
					}
				}
			}
			sprintf(g_frontendScratchBuffer, "%c%s: %c%s", 2, entry->name, 1, entry->description);
			{
				int textColor = g_colorLightBlue;
				if (actionIndex != g_configSelectedJoystickActionIndex)
					textColor = 0xFFFF;
				FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, textColor);
			}
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
		}
	}

	{
		JoystickEntry* entry;
		int entryIndex = 0;
		uint8_t actionCode = Config_ReadJoystickActionPickerKey();
		int entryCount = g_joystickEntryCount;
		if (entryIndex < entryCount) {
			entry = g_joystickEntries;
			{
				int selectedButton = g_configSelectedJoystickButtonIndex;
				do {
					if (actionCode != 0 && entry->actionCode == actionCode) {
						if (entryIndex != g_configSelectedJoystickActionIndex &&
							g_gameConfig.sfxDatapadEnabled != 0) {
							FrontendSound_PlayUISound("settingsound", 1, 0, 255,
													  12 * g_gameConfig.sfxDatapadVolume, 63);
							entryCount = g_joystickEntryCount;
							selectedButton = g_configSelectedJoystickButtonIndex;
						}
						{
							uint8_t selectedActionCode = entry->actionCode;
							g_configJoystickActionScrollOffset = entryIndex;
							g_configSelectedJoystickActionIndex = entryIndex;
							g_gameConfig.joyButtons[selectedButton] = selectedActionCode;
						}
					}
					++entry;
					++entryIndex;
				} while (entryIndex < entryCount);
			}
		}
	}
}

// FUNCTION: XVT 0x4BD5F0
int Config_LoadJoystickActionDictionary(void) {
	XvtFile* stream;
	char* cursor;
	int tokenLength;
	char token[256];
	char currentCharacter;
	int entryIndex;

	stream = File_Open("joystick.txt", "r");
	if (stream == NULL) {
		return 0;
	}
	g_joystickEntryCount = 0;
	for (;;) {
		cursor = File_Gets(g_frontendScratchBuffer, 128, stream);
		if (cursor == NULL) {
			break;
		}
		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
			g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}

		tokenLength = 0;
		while (*cursor != ' ') {
			currentCharacter = *cursor;
			if (currentCharacter == '\0' || tokenLength >= 256) {
				break;
			}
			token[tokenLength++] = *cursor++;
		}
		token[tokenLength] = '\0';
		++cursor;
		g_joystickEntries[g_joystickEntryCount].actionCode = atoi(token);

		tokenLength = 0;
		while (*cursor != ' ') {
			currentCharacter = *cursor;
			if (currentCharacter == '\0' || tokenLength >= 256) {
				break;
			}
			token[tokenLength++] = *cursor++;
		}
		token[tokenLength] = '\0';
		entryIndex = g_joystickEntryCount;
		strcpy(g_joystickEntries[g_joystickEntryCount].name, token);
		strcpy(g_joystickEntries[entryIndex].description, cursor + 1);
		++g_joystickEntryCount;
	}
	File_Close(stream);
	return 1;
}

// FUNCTION: XVT 0x4BD770
uint8_t Config_ReadJoystickActionPickerKey(void) {
	uint8_t keyCode;
	int isKeyDown;
	int keyIndex;

	keyCode = Keyboard_DequeueChar();
	if (keyCode != 0)
		return keyCode;

	if (Keyboard_IsKeyDown(0x12)) {
		for (keyIndex = 0; keyIndex < 26; ++keyIndex) {
			if (Keyboard_IsKeyDown(keyIndex + 65))
				return keyIndex + 0x80;
		}
		for (keyIndex = 0; keyIndex < 10; ++keyIndex) {
			isKeyDown = Keyboard_IsKeyDown(keyIndex + 48);
			if (isKeyDown)
				return keyIndex - 102;
		}
		return isKeyDown;
	}

	if (Keyboard_IsKeyDown(0x10)) {
		for (keyIndex = 0; keyIndex < 12; ++keyIndex) {
			isKeyDown = Keyboard_IsKeyDown(keyIndex + 112);
			if (isKeyDown)
				return keyIndex - 49;
		}
		return isKeyDown;
	}

	for (keyIndex = 0; keyIndex < 12; ++keyIndex) {
		if (Keyboard_IsKeyDown(keyIndex + 112))
			return keyIndex - 61;
	}
	if (Keyboard_IsKeyDown(0x25))
		return -92;
	if (Keyboard_IsKeyDown(0x27))
		return -91;
	if (Keyboard_IsKeyDown(0x26))
		return -90;
	if (Keyboard_IsKeyDown(0x28))
		return -89;
	if (Keyboard_IsKeyDown(0x2D))
		return -88;
	if (Keyboard_IsKeyDown(0x2E))
		return -87;
	if (Keyboard_IsKeyDown(0x24))
		return -86;
	if (Keyboard_IsKeyDown(0x23))
		return -85;
	if (Keyboard_IsKeyDown(0x21))
		return -84;
	if (Keyboard_IsKeyDown(0x22))
		return -83;
	if (Keyboard_IsKeyDown(0x2C))
		return -82;
	if (Keyboard_IsKeyDown(0x91))
		return -81;
	if (Keyboard_IsKeyDown(0x14))
		return -79;
	if (Keyboard_IsKeyDown(0x60))
		return -78;
	if (Keyboard_IsKeyDown(0x61))
		return -77;
	if (Keyboard_IsKeyDown(0x62))
		return -76;
	if (Keyboard_IsKeyDown(0x63))
		return -75;
	if (Keyboard_IsKeyDown(0x64))
		return -74;
	if (Keyboard_IsKeyDown(0x65))
		return -73;
	if (Keyboard_IsKeyDown(0x66))
		return -72;
	if (Keyboard_IsKeyDown(0x67))
		return -71;
	if (Keyboard_IsKeyDown(0x68))
		return -70;
	if (Keyboard_IsKeyDown(0x69))
		return -69;
	if (Keyboard_IsKeyDown(0x90))
		return -68;
	if (Keyboard_IsKeyDown(0x6A))
		return -66;
	if (Keyboard_IsKeyDown(0x6B))
		return -64;
	if (Keyboard_IsKeyDown(0x6D))
		return -65;
	if (Keyboard_IsKeyDown(0x6E))
		return -62;
	isKeyDown = Keyboard_IsKeyDown(0x6F);
	if (isKeyDown != 0)
		keyCode = -67;
	else
		keyCode = isKeyDown;
	return keyCode;
}

// FUNCTION: XVT 0x4BDA30
void Config_DrawCustomTauntsPage(void) {
	RECT rect;
	int widestLabel;
	int labelIndex;
	int labelWidth;
	int textY;
	int fieldIndex;
	char (*taunt)[70];

	FrontendDraw_RectAssign(&rect, 84, 90, 604, 106);
	widestLabel = 0;
	labelIndex = 0;
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_794_CUSTOM_TAUNTS), &rect, 0xFFFF);
	do {
		++labelIndex;
		sprintf(g_frontendScratchBuffer, "%s%d", FrontendString_Get(FRONTSTR_795_TAUNT), labelIndex);
		labelWidth = FrontendText_MeasureWidth(g_frontendScratchBuffer, 12);
		if (labelWidth > widestLabel)
			widestLabel = labelWidth;
	} while (labelIndex < 4);

	textY = 126;
	fieldIndex = 0;
	taunt = g_gameConfig.taunts;
	FrontendDraw_RectAssign(&rect, widestLabel + 93, 126, 600, 146);
	do {
		labelIndex = fieldIndex + 1;
		sprintf(g_frontendScratchBuffer, "%s%d", FrontendString_Get(FRONTSTR_795_TAUNT), labelIndex);
		FrontendText_Draw(12, g_frontendScratchBuffer, 88, textY, 0xFFFF);
		FrontendDraw_FillRectTranslucent(&rect, 0, 0, g_editableFieldBackgroundColor);
		if (FrontendText_DrawEditableField(&rect, *taunt, 46, fieldIndex, 12, NULL)) {
			g_activeTextFieldId = labelIndex;
			if (labelIndex >= 4)
				g_activeTextFieldId = 0;
		}
		textY += 35;
		++taunt;
		fieldIndex = labelIndex;
		FrontendDraw_RectOffsetXY(&rect, 0, 35);
	} while (taunt < g_gameConfig.taunts + 4);
}

// FUNCTION: XVT 0x4FB670
int Config_CreditsScreen(int frameState) {
	int outPageDurationFrames;
	int lineIndex;
	int key;
	int logoId;

	if (frameState == 0) {
		Keyboard_FlushCharBuffer();
		if (g_frontendCreditsFile != NULL) {
			File_Close(g_frontendCreditsFile);
		}
		g_creditsCurrentTextColor = (uint16_t)-1;
		g_creditsExitPending = 0;
		g_creditsPageIndex = 0;
		g_creditsTextX[0] = 0;
		g_creditsTextY[1] = 0;
		g_creditsLogoId[0] = 0;
		g_creditsLogoY[0] = 0;
		g_creditsLogoX[0] = 0;
		g_creditsLogoId[1] = 0;
		g_creditsLogoY[1] = 0;
		g_creditsLogoX[1] = 0;
		g_frontendCreditsFile = File_Open("credits.txt", "rt");
		g_creditsHasMorePages = 0;
		g_creditsPrevLogoId[0] = 0;
		g_creditsPrevLogoY[0] = 0;
		g_creditsPrevLogoX[0] = 0;
		g_creditsPrevLogoId[1] = 0;
		g_creditsPrevLogoY[1] = 0;
		g_creditsPrevLogoX[1] = 0;
		if (Credits_ParseNextPage(&g_creditsBufferIdx, &g_creditsHasMorePages, &g_creditsPageEndFrame,
								  &g_creditsGlyphScratchFrames) == 0) {
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
			return 0;
		}
		FrontendText_ResetGlyphScratchBuffer(200);
	}
	if (g_creditsExitPending != 0) {
#ifdef XVT_MODERN
		if (XvtCdTask_IsFading())
			return 0;
#endif
		FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
		return 0;
	}

	if (Keyboard_IsKeyDown(0x10) && Keyboard_IsKeyDown(0x12) && Keyboard_IsKeyDown(0x7B)) {
		switch (g_creditsPageIndex) {
			case 0:
				FrontImage_DrawSprite("comp01", 160, 157);
				break;
			case 1:
				FrontImage_DrawSprite("artists", 160, 163);
				break;
			case 2:
				FrontImage_DrawSprite("testers", 160, 152);
				break;
			case 4:
				FrontImage_DrawSprite("lakota", 160, 143);
				break;
			default:
				break;
		}
	}

	if (g_creditsPrevLogoId[0] != g_creditsLogoId[0] || g_creditsPrevLogoX[0] != g_creditsLogoX[0] ||
		g_creditsPrevLogoY[0] != g_creditsLogoY[0] || g_creditsPrevLogoId[1] != g_creditsLogoId[1] ||
		g_creditsPrevLogoX[1] != g_creditsLogoX[1] || g_creditsPrevLogoY[1] != g_creditsLogoY[1]) {
		g_creditsPrevLogoId[0] = g_creditsLogoId[0];
		g_creditsPrevLogoX[0] = g_creditsLogoX[0];
		g_creditsPrevLogoY[0] = g_creditsLogoY[0];
		g_creditsPrevLogoId[1] = g_creditsLogoId[1];
		g_creditsPrevLogoX[1] = g_creditsLogoX[1];
		g_creditsPrevLogoY[1] = g_creditsLogoY[1];
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		logoId = g_creditsLogoId[0];
		switch (logoId) {
			case 1:
				FrontImage_DrawSpriteTranslucent("totallylogo", g_creditsLogoX[0], g_creditsLogoY[0]);
				break;
			case 2:
				FrontImage_DrawSpriteTranslucent("leclogo", g_creditsLogoX[0], g_creditsLogoY[0]);
				break;
			default:
				break;
		}
		logoId = g_creditsLogoId[1];
		switch (logoId) {
			case 1:
				FrontImage_DrawSpriteTranslucent("totallylogo", g_creditsLogoX[1], g_creditsLogoY[1]);
				break;
			case 2:
				FrontImage_DrawSpriteTranslucent("leclogo", g_creditsLogoX[1], g_creditsLogoY[1]);
				break;
			default:
				break;
		}
		FrontendDisplay_UnlockOffscreenSurface(1);
	}

	if ((g_creditsBufferIdx & 1) != 0) {
		FrontendText_PushGlyphScratchTtl();
	}
	for (lineIndex = 0; lineIndex < 32; ++lineIndex) {
		FrontendText_Draw(15, g_creditsTextLines[0][lineIndex], g_creditsTextX[0],
						  g_creditsTextY[0] + 19 * lineIndex, g_creditsTextColors[0][lineIndex]);
	}
	if ((g_creditsBufferIdx & 1) != 0) {
		FrontendText_PopGlyphScratchTtl();
	} else {
		FrontendText_PushGlyphScratchTtl();
	}
	for (lineIndex = 0; lineIndex < 32; ++lineIndex) {
		FrontendText_Draw(15, g_creditsTextLines[1][lineIndex], g_creditsTextX[1],
						  g_creditsTextY[1] + 19 * lineIndex, g_creditsTextColors[1][lineIndex]);
	}
	FrontendText_PopGlyphScratchTtl();

	if (g_creditsHasMorePages != 0) {
		if (g_creditsPageEndFrame <= frameState) {
			Credits_ParseNextPage(&g_creditsBufferIdx, &g_creditsHasMorePages, &outPageDurationFrames,
								  &g_creditsGlyphScratchFrames);
			g_creditsPageEndFrame += outPageDurationFrames;
			FrontendText_ResetGlyphScratchBuffer(g_creditsGlyphScratchFrames);
			++g_creditsPageIndex;
			if (g_creditsHasMorePages == 0) {
				CDAudio_DisableLoopCurrentTrack();
			}
		}
	} else if (g_creditsPageEndFrame <= frameState) {
		g_creditsExitPending = 1;
		FrontendDisplay_ClearOffscreenSurface();
		CDAudio_FadeAuxVolume(0x8000, 0x1000, 2000);
	}

	key = (uint8_t)Keyboard_DequeueChar();
	if (FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0 || key == 27 || key == 13 ||
		key == 32) {
		g_creditsExitPending = 1;
		FrontendDisplay_ClearOffscreenSurface();
		FrontendDisplay_ClearBackBuffer();
		CDAudio_FadeAuxVolume(0x8000, 0x1000, 2000);
	}
	return 0;
}

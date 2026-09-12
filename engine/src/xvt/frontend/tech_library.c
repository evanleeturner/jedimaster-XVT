#include "xvt/frontend/tech_library.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/assets/model_preview.h"
#include "xvt/flight/craft.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/net.h"
#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0xAA6110
TechLibrarySpecText* g_techLibrarySpecTextTable = NULL;

// GLOBAL: XVT 0x665D38
CraftTechStats g_techLibraryCraftStats = { 0 };
// GLOBAL: XVT 0x5182EC
const float g_techLibraryRotationStepDegrees = 5.0f;
// GLOBAL: XVT 0x518300
const double g_techLibraryRotationFullTurnDegrees = 360.0;
// GLOBAL: XVT 0x665D28
int g_techLibrarySelectedShipListIdx = 0;
// GLOBAL: XVT 0x665D2C
int g_techLibraryLightX = 0;
// GLOBAL: XVT 0x665D30
float g_techLibraryPreviewAngleD = 0.0f;
// GLOBAL: XVT 0x665D64
float g_techLibraryPreviewYawDeg = 0.0f;
// GLOBAL: XVT 0x665D68
int g_techLibraryLightY = 0;
// GLOBAL: XVT 0x665D6C
int g_techLibraryLightZ = 0;
// GLOBAL: XVT 0x665D70
float g_techLibraryPreviewRollDeg = 0.0f;
// GLOBAL: XVT 0x665D74
float g_techLibraryPreviewPitchDeg = 0.0f;

// FUNCTION: XVT 0x4E96B0
int TechLibrary_Update(int frameCounter) {
	enum {
		TECH_LIBRARY_SCREEN_CONTEXT = 3,
		TECH_LIBRARY_VIEWPORT_LEFT = 280,
		TECH_LIBRARY_VIEWPORT_TOP = 107,
		TECH_LIBRARY_VIEWPORT_RIGHT = 606,
		TECH_LIBRARY_VIEWPORT_BOTTOM = 433,
		TECH_LIBRARY_TITLE_LEFT = 84,
		TECH_LIBRARY_TITLE_TOP = 90,
		TECH_LIBRARY_TITLE_RIGHT = 604,
		TECH_LIBRARY_TITLE_BOTTOM = 106,
		TECH_LIBRARY_DONE_LEFT = 85,
		TECH_LIBRARY_DONE_TOP = 447,
		TECH_LIBRARY_DONE_RIGHT = 176,
		TECH_LIBRARY_DONE_BOTTOM = 471,
		PILOT_BANNER_LEFT = 200,
		PILOT_BANNER_TOP = 452,
		PILOT_BANNER_RIGHT = 436,
		PILOT_BANNER_BOTTOM = 464,
		PILOT_BANNER_REBEL_X = 204,
		PILOT_BANNER_IMPERIAL_X = 420,
		PILOT_BANNER_ICON_Y = 453,
		PILOT_BANNER_ANIMATION_FRAMES = 32,
		DEFAULT_CURSOR_X = 32,
		DEFAULT_CURSOR_Y = 319,
		DEFAULT_PREVIEW_PITCH_DEGREES = 110,
		DEFAULT_PREVIEW_YAW_DEGREES = 225,
		SPECIAL_CRAFT_WORLD_Y = 100,
		BUTTON_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		DONE_BUTTON_HOVER_SLOT = 8,
	};

	RECT rect;
	int animationFrame;
	int buttonPressed;
	int selectedCraftType;

	if (frameCounter == 0) {
		FrontendCursor_SetPos(DEFAULT_CURSOR_X, DEFAULT_CURSOR_Y);
		g_techLibrarySelectedShipListIdx = 0;
		g_techLibraryPreviewRollDeg = 0.0f;
		g_techLibraryLightX = 1;
		g_techLibraryLightY = 1;
		g_techLibraryLightZ = 1;
		g_techLibraryPreviewAngleD = 0.0f;
		g_techLibraryPreviewPitchDeg = (float)DEFAULT_PREVIEW_PITCH_DEGREES;
		g_techLibraryPreviewYawDeg = (float)DEFAULT_PREVIEW_YAW_DEGREES;
		ShipList_Load();
		TechLibrary_LoadSpecTextTable();
		if (g_missionBriefingActive != 0) {
			ModelPreview_SaveState();
		}
		memset(&g_techLibraryCraftStats, 0, sizeof(g_techLibraryCraftStats));
		g_techLibraryCraftStats.craftType = g_shipList[g_techLibrarySelectedShipListIdx].typeId;
		BuildCraftTechStats(&g_techLibraryCraftStats);
		ModelPreview_LoadModel(g_shipList[g_techLibrarySelectedShipListIdx].modelFileName);
		ModelPreview_SetWhiteDirectionalLight(g_techLibraryLightX, g_techLibraryLightY, g_techLibraryLightZ);
		selectedCraftType = g_shipList[g_techLibrarySelectedShipListIdx].typeId;
		if (selectedCraftType == CRAFT_SPECIES_TIE_INTERCEPTOR ||
			selectedCraftType == CRAFT_SPECIES_TIE_BOMBER) {
			ModelPreview_SetObjectWorldPosition(0, SPECIAL_CRAFT_WORLD_Y, 0);
		} else {
			ModelPreview_SetObjectWorldPosition(0, 0, 0);
		}
		ModelPreview_SetNodeSwitchIndex(0);
		FrontImage_RegisterResourceDefault("frontres\\review.bmp", "backreview");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backreview", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);
	}

	FrontendDraw_RectAssign(&rect, TECH_LIBRARY_VIEWPORT_LEFT, TECH_LIBRARY_VIEWPORT_TOP,
							TECH_LIBRARY_VIEWPORT_RIGHT, TECH_LIBRARY_VIEWPORT_BOTTOM);
	ModelPreview_SetObjectEulerDegrees(g_techLibraryPreviewPitchDeg, g_techLibraryPreviewYawDeg,
									   g_techLibraryPreviewRollDeg);
	ModelPreview_SetObjectAngleDDegrees(g_techLibraryPreviewAngleD);
	ModelPreview_RenderViewport(rect.left, rect.top, rect.right - rect.left + 1, rect.bottom - rect.top + 1,
								NULL);
	FrontendDraw_RectAssign(&rect, TECH_LIBRARY_TITLE_LEFT, TECH_LIBRARY_TITLE_TOP, TECH_LIBRARY_TITLE_RIGHT,
							TECH_LIBRARY_TITLE_BOTTOM);
	FrontendText_DrawCentered(TITLE_FONT_SIZE, FrontendString_Get(FRONTSTR_001_CRAFT_DATABASE), &rect,
							  0xFFFF);
	TechLibrary_DrawCraftSpecPanel();

	FrontendDraw_RectAssign(&rect, PILOT_BANNER_LEFT, PILOT_BANNER_TOP, PILOT_BANNER_RIGHT,
							PILOT_BANNER_BOTTOM);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6, g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(BUTTON_FONT_SIZE, g_frontendScratchBuffer, &rect, g_colorLightBlue);
		animationFrame = frameCounter % PILOT_BANNER_ANIMATION_FRAMES;
		animationFrame >>= 1;
		sprintf(g_frontendScratchBuffer, "rebtiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer, PILOT_BANNER_REBEL_X, PILOT_BANNER_ICON_Y);
		sprintf(g_frontendScratchBuffer, "imptiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer, PILOT_BANNER_IMPERIAL_X, PILOT_BANNER_ICON_Y);
	}

	if (Frontend_HandleCommonScreenControls(TECH_LIBRARY_SCREEN_CONTEXT) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (XvtDialog_IsActive())
		return 0;
#endif
	TechLibrary_UpdateModelControls();
	FrontendDraw_RectAssign(&rect, TECH_LIBRARY_DONE_LEFT, TECH_LIBRARY_DONE_TOP, TECH_LIBRARY_DONE_RIGHT,
							TECH_LIBRARY_DONE_BOTTOM);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
		FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_206_DONE));
	}
	buttonPressed =
		FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown", FrontendString_Get(FRONTSTR_206_DONE),
										 BUTTON_FONT_SIZE, 0, DONE_BUTTON_HOVER_SLOT, "buttonsound");
	FrontendButton_DisableOverlayText();
	buttonPressed |= FrontendDialog_HasNetworkDismissPacket();
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NET_HOST) {
		buttonPressed |= Net_HasQueuedJoinRequestOrBacklog();
	}
	if (buttonPressed != 0) {
		FrontImage_FreeResourceByName("backreview");
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
		if (g_missionBriefingActive == 0) {
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
		FrontendText_ResetGlyphScratch();
	}
	return 0;
}

// FUNCTION: XVT 0x4E9AF0
int TechLibrary_UpdateModelControls(void) {
	enum {
		NAVIGATION_SLOT_COUNT = 8,
		TOP_BUTTON_COUNT = 3,
		BOTTOM_BUTTON_FIRST = 5,
		BOTTOM_BUTTON_END = 7,
		BUTTON_SPACING = 28,
		BUTTON_FONT_SIZE = 12,
		LIGHTING_HOVER_SLOT = 13,
		ROTATE_X_HOVER_SLOT = 12,
		ROTATE_Y_HOVER_SLOT = 11,
		PREVIOUS_CRAFT_HOVER_SLOT = 17,
		NEXT_CRAFT_HOVER_SLOT = 16,
		SPECIAL_CRAFT_WORLD_Y = 100,
	};

	int mouseY;
	int mouseX;
	RECT rect;
	FrontendNavigationSlotState slotStates[NAVIGATION_SLOT_COUNT] = {
		FRONTEND_NAVIGATION_SLOT_ACTIVE,   FRONTEND_NAVIGATION_SLOT_ACTIVE,   FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_INACTIVE, FRONTEND_NAVIGATION_SLOT_INACTIVE, FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,   FRONTEND_NAVIGATION_SLOT_INACTIVE,
	};
	int slotIndex;
	int selectedCraftType;

	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&rect, 22, 114, 42, 138);
	FrontendCursor_GetPos(&mouseX, &mouseY);
	for (slotIndex = 0; slotIndex < TOP_BUTTON_COUNT; ++slotIndex) {
		if (slotStates[slotIndex] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
			FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0 &&
			(FrontendMouse_GetLeftDown() != 0 || FrontendMouse_GetRightDown() != 0 ||
			 FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
			slotStates[slotIndex] = FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		FrontendDraw_RectOffsetXY(&rect, 0, BUTTON_SPACING);
	}

	FrontendDraw_RectAssign(&rect, 22, 306, 42, 330);
	for (slotIndex = BOTTOM_BUTTON_FIRST; slotIndex < BOTTOM_BUTTON_END; ++slotIndex) {
		if (slotStates[slotIndex] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
			FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0 &&
			(FrontendMouse_GetLeftDown() != 0 || FrontendMouse_GetRightDown() != 0 ||
			 FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
			slotStates[slotIndex] = FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		FrontendDraw_RectOffsetXY(&rect, 0, BUTTON_SPACING);
	}
	FrontendButton_DrawEightSlotNavigationState(slotStates);

	FrontendDraw_RectAssign(&rect, 22, 170, 42, 194);
	if (FrontendButton_DrawSpriteHitTest(&rect, "review3u", "review3d",
										 FrontendString_Get(FRONTSTR_294_CHANGE_LIGHTING), BUTTON_FONT_SIZE,
										 0, LIGHTING_HOVER_SLOT, "jewelsound") != 0) {
		--g_techLibraryLightX;
		if (g_techLibraryLightX == -2) {
			--g_techLibraryLightY;
			g_techLibraryLightX = 1;
			if (g_techLibraryLightY == -2) {
				g_techLibraryLightY = 1;
				--g_techLibraryLightZ;
				if (g_techLibraryLightZ == -2) {
					g_techLibraryLightZ = 1;
				}
			}
		}
		ModelPreview_SetWhiteDirectionalLight(g_techLibraryLightX, g_techLibraryLightY, g_techLibraryLightZ);
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	FrontendButton_DrawSpriteHitTest(&rect, "review2u", "review2d", FrontendString_Get(FRONTSTR_292_ROTATE_X),
									 BUTTON_FONT_SIZE, 0, ROTATE_X_HOVER_SLOT, "jewelsound");
	if (FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0) {
		if (FrontendMouse_GetLeftDown() != 0) {
			g_techLibraryPreviewYawDeg -= g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewYawDeg <= 0.0) {
				g_techLibraryPreviewYawDeg = 360.0f;
			}
		} else if (FrontendMouse_GetRightDown() != 0) {
			g_techLibraryPreviewYawDeg += g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewYawDeg >= g_techLibraryRotationFullTurnDegrees) {
				g_techLibraryPreviewYawDeg = 0.0f;
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	FrontendButton_DrawSpriteHitTest(&rect, "review1u", "review1d", FrontendString_Get(FRONTSTR_293_ROTATE_Y),
									 BUTTON_FONT_SIZE, 0, ROTATE_Y_HOVER_SLOT, "jewelsound");
	if (FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0) {
		if (FrontendMouse_GetLeftDown() != 0) {
			g_techLibraryPreviewPitchDeg -= g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewPitchDeg <= 0.0) {
				g_techLibraryPreviewPitchDeg = 360.0f;
			}
		} else if (FrontendMouse_GetRightDown() != 0) {
			g_techLibraryPreviewPitchDeg += g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewPitchDeg >= g_techLibraryRotationFullTurnDegrees) {
				g_techLibraryPreviewPitchDeg = 0.0f;
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 22, 334, 42, 358);
	if (FrontendButton_DrawSpriteHitTest(&rect, "review7u", "review7d",
										 FrontendString_Get(FRONTSTR_296_PREVIOUS_CRAFT), BUTTON_FONT_SIZE, 0,
										 PREVIOUS_CRAFT_HOVER_SLOT, "jewelsound") != 0) {
		--g_techLibrarySelectedShipListIdx;
		if (g_techLibrarySelectedShipListIdx < 0) {
			g_techLibrarySelectedShipListIdx = g_shipCount - 1;
		}
		memset(&g_techLibraryCraftStats, 0, sizeof(g_techLibraryCraftStats));
		g_techLibraryCraftStats.craftType = g_shipList[g_techLibrarySelectedShipListIdx].typeId;
		BuildCraftTechStats(&g_techLibraryCraftStats);
		ModelPreview_LoadModel(g_shipList[g_techLibrarySelectedShipListIdx].modelFileName);
		selectedCraftType = g_shipList[g_techLibrarySelectedShipListIdx].typeId;
		if (selectedCraftType == CRAFT_SPECIES_TIE_INTERCEPTOR ||
			selectedCraftType == CRAFT_SPECIES_TIE_BOMBER) {
			ModelPreview_SetObjectWorldPosition(0, SPECIAL_CRAFT_WORLD_Y, 0);
		} else {
			ModelPreview_SetObjectWorldPosition(0, 0, 0);
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (FrontendButton_DrawSpriteHitTest(&rect, "review6u", "review6d",
										 FrontendString_Get(FRONTSTR_295_NEXT_CRAFT), BUTTON_FONT_SIZE, 0,
										 NEXT_CRAFT_HOVER_SLOT, "jewelsound") != 0) {
		++g_techLibrarySelectedShipListIdx;
		if (g_shipCount <= g_techLibrarySelectedShipListIdx) {
			g_techLibrarySelectedShipListIdx = 0;
		}
		memset(&g_techLibraryCraftStats, 0, sizeof(g_techLibraryCraftStats));
		g_techLibraryCraftStats.craftType = g_shipList[g_techLibrarySelectedShipListIdx].typeId;
		BuildCraftTechStats(&g_techLibraryCraftStats);
		ModelPreview_LoadModel(g_shipList[g_techLibrarySelectedShipListIdx].modelFileName);
		selectedCraftType = g_shipList[g_techLibrarySelectedShipListIdx].typeId;
		if (selectedCraftType != CRAFT_SPECIES_TIE_INTERCEPTOR &&
			selectedCraftType != CRAFT_SPECIES_TIE_BOMBER) {
			ModelPreview_SetObjectWorldPosition(0, 0, 0);
			return 1;
		}
		ModelPreview_SetObjectWorldPosition(0, SPECIAL_CRAFT_WORLD_Y, 0);
	}
	return 1;
}

// FUNCTION: XVT 0x4EA090
int TechLibrary_DrawCraftSpecPanel(void) {
	RECT rect;
	RECT descriptionRect;
	int craftSpecIndex;
	int textLines;
	const char* label;

	FrontendDraw_RectAssign(&rect, 88, 111, 332, 125);
	craftSpecIndex = g_techLibraryCraftStats.craftType - 1;
	if (craftSpecIndex < 0) {
		craftSpecIndex = 0;
	}
	sprintf(g_frontendScratchBuffer, "%s", g_techLibrarySpecTextTable[craftSpecIndex].designation);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 30);
	label =
		FrontendString_Get((FrontendStringId)(FRONTSTR_496_STARFIGHTER + g_techLibraryCraftStats.genusId));
	sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4, FrontendString_Get(FRONTSTR_483_DESIGNATION), 1, label);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4, FrontendString_Get(FRONTSTR_484_MANUFACTURER), 1,
			g_techLibrarySpecTextTable[craftSpecIndex].manufacturer);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4, FrontendString_Get(FRONTSTR_485_IN_USE_BY), 1,
			g_techLibrarySpecTextTable[craftSpecIndex].inUseBy);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_486_SPECIAL_CHARACTERISTICS), &rect, 0, 1,
								   g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendDraw_RectCopy(&descriptionRect, &rect);
	descriptionRect.bottom = descriptionRect.top + 120;
	descriptionRect.left += 20;
	textLines = FrontendText_DrawWrapped(12, g_techLibrarySpecTextTable[craftSpecIndex].description,
										 &descriptionRect, 0xFFFF, 3, 0);
	FrontendDraw_RectOffsetXY(&rect, 0, 5 * (3 * textLines + 6));

	if (g_techLibraryCraftStats.genusId == CRAFT_GENUS_STARFIGHTER) {
		sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4, FrontendString_Get(FRONTSTR_487_SPEED), 1,
				g_techLibraryCraftStats.speedRating, FrontendString_Get(FRONTSTR_513_MGLT));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4, FrontendString_Get(FRONTSTR_488_ACCELERATION), 1,
				g_techLibraryCraftStats.accelerationRating, FrontendString_Get(FRONTSTR_514_MGLT_SECOND));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4,
				FrontendString_Get(FRONTSTR_489_MANUEVERABILITY_RATING), 1,
				g_techLibraryCraftStats.maneuverRating, FrontendString_Get(FRONTSTR_714_DPF));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c", 4, FrontendString_Get(FRONTSTR_490_LASERS), 1);
		if (g_techLibraryCraftStats.laserCount != 0) {
			strcat(g_frontendScratchBuffer,
				   FrontendString_Get((FrontendStringId)(g_techLibraryCraftStats.laserCount + 517)));
			strcat(g_frontendScratchBuffer, " ");
			strcat(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_516_LASERS));
			if (g_techLibraryCraftStats.ionCount != 0) {
				strcat(g_frontendScratchBuffer, " ");
				strcat(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_515_AND));
				strcat(g_frontendScratchBuffer, " ");
			}
		}
		if (g_techLibraryCraftStats.ionCount != 0) {
			strcat(g_frontendScratchBuffer,
				   FrontendString_Get((FrontendStringId)(g_techLibraryCraftStats.ionCount + 517)));
			strcat(g_frontendScratchBuffer, " ");
			strcat(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_517_ION_CANNONS));
		}
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%d", 4,
				FrontendString_Get(FRONTSTR_491_STD_COMBAT_WARHEAD_LOAD), 1,
				g_techLibraryCraftStats.warheadRating);
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
	} else if (g_techLibraryCraftStats.genusId != CRAFT_GENUS_MINE &&
			   g_techLibraryCraftStats.genusId != CRAFT_GENUS_SATELLITE) {
		const double displayedSize = (double)ModelPreview_GetDisplayedSizeMeters();
		const double sizeValue = displayedSize >= 1000.0 ? displayedSize * 0.001 : displayedSize;
		const FrontendStringId sizeUnit = displayedSize >= 1000.0 ? FRONTSTR_522_KM : FRONTSTR_712_METERS;

		sprintf(g_frontendScratchBuffer, "%c%s %c%.1f %s", 4, FrontendString_Get(FRONTSTR_492_SIZE), 1,
				sizeValue, FrontendString_Get(sizeUnit));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4, FrontendString_Get(FRONTSTR_493_CREW), 1,
				g_techLibrarySpecTextTable[craftSpecIndex].crew);
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
	}

	sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4, FrontendString_Get(FRONTSTR_494_SHIELD_RATING), 1,
			g_techLibraryCraftStats.shieldRating, FrontendString_Get(FRONTSTR_715_SBD));
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4, FrontendString_Get(FRONTSTR_495_HULL_RATING), 1,
			g_techLibraryCraftStats.hullRating, FrontendString_Get(FRONTSTR_713_RU));
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	return 1;
}

// FUNCTION: XVT 0x4EA8C0
int TechLibrary_LoadSpecTextTable(void) {
	XvtFile* stream;
	int fieldIndex;
	int entryIndex;
	size_t length;

	stream = File_Open("specdesc.txt", "r");
	if (stream == NULL)
		return 0;

	if (g_techLibrarySpecTextTable != NULL) {
		free(g_techLibrarySpecTextTable);
		g_techLibrarySpecTextTable = NULL;
	}

	g_techLibrarySpecTextTable = (TechLibrarySpecText*)malloc(sizeof(TechLibrarySpecText) * 93u);
#ifndef XVT_MODERN
	memset(g_techLibrarySpecTextTable, 0, sizeof(TechLibrarySpecText) * 93u);
#endif
	if (g_techLibrarySpecTextTable == NULL) {
		File_Close(stream);
		return 0;
	}

#ifdef XVT_MODERN
	memset(g_techLibrarySpecTextTable, 0, sizeof(TechLibrarySpecText) * 93u);
#endif
	for (entryIndex = 0; entryIndex < 93; ++entryIndex) {
		fieldIndex = 0;
		while (fieldIndex < 5) {
			do {
#ifdef XVT_MODERN
				if (File_Gets(g_frontendScratchBuffer, sizeof(g_frontendScratchBuffer), stream) == NULL) {
#else
				if (File_Gets(g_frontendScratchBuffer, 1024, stream) == NULL) {
#endif
					File_Close(stream);
					return 1;
				}
				g_frontendScratchBuffer[255] = '\0';
			} while (g_frontendScratchBuffer[0] == '/' && g_frontendScratchBuffer[1] == '/');

			length = strlen(g_frontendScratchBuffer);
			if (g_frontendScratchBuffer[length - 1] == '\n')
				g_frontendScratchBuffer[length - 1] = '\0';

			switch (fieldIndex) {
				case 0:
					strncpy(g_techLibrarySpecTextTable[entryIndex].designation, g_frontendScratchBuffer, 64u);
					break;
				case 1:
					strncpy(g_techLibrarySpecTextTable[entryIndex].manufacturer, g_frontendScratchBuffer,
							64u);
					break;
				case 2:
					strncpy(g_techLibrarySpecTextTable[entryIndex].inUseBy, g_frontendScratchBuffer, 64u);
					break;
				case 3:
					strncpy(g_techLibrarySpecTextTable[entryIndex].description, g_frontendScratchBuffer,
							256u);
					break;
				case 4:
					strncpy(g_techLibrarySpecTextTable[entryIndex].crew, g_frontendScratchBuffer, 64u);
					break;
			}
			++fieldIndex;
		}
	}

#ifdef XVT_MODERN
	return File_Close(stream);
#else
	File_Close(stream);
#endif
}

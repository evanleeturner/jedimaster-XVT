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

/* Heap table of 93 craft descriptions from specdesc.txt, by CraftSpecies
 * value minus 1. TechLibrary_LoadSpecTextTable loads it on the craft
 * database's first frame; closing the database, by its Done button or its
 * own button among the common screen controls, frees it and sets NULL, as
 * does XvtFrontendTask_Shutdown in the modern build. */
// GLOBAL: XVT 0xAA6110
struct TechLibrarySpecText *g_techLibrarySpecTextTable = NULL;

/* Ratings of the craft shown, from BuildCraftTechStats; zeroed and rebuilt on
 * the craft database's first frame and at each change of craft. */
// GLOBAL: XVT 0x665D38
struct CraftTechStats g_techLibraryCraftStats = {0};
/* Degrees the model turns each frame a rotate button is held: 5. */
// GLOBAL: XVT 0x5182EC
const float g_techLibraryRotationStepDegrees = 5.0f;
/* A full turn, 360 degrees: a rising angle that reaches it wraps to 0. */
// GLOBAL: XVT 0x518300
const double g_techLibraryRotationFullTurnDegrees = 360.0;
/* Index in g_shipList of the craft shown: 0 on the first frame, stepped with
 * wraparound by the Previous craft and Next craft buttons. */
// GLOBAL: XVT 0x665D28
int g_techLibrarySelectedShipListIdx = 0;
/* Light direction x for the model preview, -1, 0 or 1. Set to 1 on the first
 * frame; each press of Change lighting lowers x, and past -1 sets it back to
 * 1 and lowers y the same way, then z. */
// GLOBAL: XVT 0x665D2C
int g_techLibraryLightX = 0;
/* Up-axis angle of the model preview in degrees; set to 0 on the first frame
 * and never changed. */
// GLOBAL: XVT 0x665D30
float g_techLibraryPreviewAngleD = 0.0f;
/* Model yaw in degrees, 225 on the first frame. Holding a mouse button on
 * Rotate X lowers it (left) or raises it (right) by 5 a frame; a value at or
 * under 0 becomes 360, one at or over 360 becomes 0. */
// GLOBAL: XVT 0x665D64
float g_techLibraryPreviewYawDeg = 0.0f;
/* Light direction y, stepped with g_techLibraryLightX. */
// GLOBAL: XVT 0x665D68
int g_techLibraryLightY = 0;
/* Light direction z, stepped with g_techLibraryLightX. */
// GLOBAL: XVT 0x665D6C
int g_techLibraryLightZ = 0;
/* Model roll in degrees; set to 0 on the first frame and never changed. */
// GLOBAL: XVT 0x665D70
float g_techLibraryPreviewRollDeg = 0.0f;
/* Model pitch in degrees, 110 on the first frame; Rotate Y changes it the way
 * Rotate X changes g_techLibraryPreviewYawDeg. */
// GLOBAL: XVT 0x665D74
float g_techLibraryPreviewPitchDeg = 0.0f;

/* Update function of the craft database, a screen the common screen controls
 * push. On frame 0 it sets the cursor, selection, light and angles to their
 * starting values, loads the ship list and the spec text table, saves the
 * model preview's state while a briefing is active, builds the first craft's
 * ratings and loads its model, raised to world y 100 for a TIE Interceptor or
 * TIE Bomber, and draws frontres\review.bmp with its frame and overlays into
 * the offscreen surface. Every frame it renders the model at the current
 * angles in (280, 107) to (606, 433), draws the title, the spec panel and
 * the pilot banner, and returns 1 when Frontend_HandleCommonScreenControls(3)
 * returns 1. Then it handles the model controls and the Done button. Done, a
 * network dismiss packet, or as network host a nonzero
 * Net_PollForPlayerCreatedOrBacklog closes the screen: it frees the
 * background and spec table, pops the screen, and frees g_shipList, or with
 * a briefing active restores the preview state instead. Returns 0 on every
 * other path; the modern build stops there while a dialog is up. */
// FUNCTION: XVT 0x4E96B0
int TechLibrary_Update(int frameCounter)
{
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
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
		DEFAULT_CURSOR_X = 32,
		DEFAULT_CURSOR_Y = 319,
		DEFAULT_PREVIEW_PITCH_DEGREES = 110,
		DEFAULT_PREVIEW_YAW_DEGREES = 225,
		SPECIAL_CRAFT_WORLD_Y = 100,
		BUTTON_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		DONE_BUTTON_HELD_SLOT = 8,
	};

	struct RECT rect;
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
		g_techLibraryPreviewPitchDeg =
			(float)DEFAULT_PREVIEW_PITCH_DEGREES;
		g_techLibraryPreviewYawDeg = (float)DEFAULT_PREVIEW_YAW_DEGREES;
		ShipList_Load();
		TechLibrary_LoadSpecTextTable();
		if (g_missionBriefingCraftSelectionActive != 0) {
			ModelPreview_SaveState();
		}
		memset(&g_techLibraryCraftStats, 0,
		       sizeof(g_techLibraryCraftStats));
		g_techLibraryCraftStats.craftType =
			g_shipList[g_techLibrarySelectedShipListIdx].craftType;
		BuildCraftTechStats(&g_techLibraryCraftStats);
		ModelPreview_LoadModel(
			g_shipList[g_techLibrarySelectedShipListIdx]
				.modelFileName);
		ModelPreview_SetLightDirection(g_techLibraryLightX,
					       g_techLibraryLightY,
					       g_techLibraryLightZ);
		selectedCraftType =
			g_shipList[g_techLibrarySelectedShipListIdx].craftType;
		if (selectedCraftType == CRAFT_SPECIES_TIE_INTERCEPTOR ||
		    selectedCraftType == CRAFT_SPECIES_TIE_BOMBER) {
			ModelPreview_SetObjectWorldPosition(
				0, SPECIAL_CRAFT_WORLD_Y, 0);
		} else {
			ModelPreview_SetObjectWorldPosition(0, 0, 0);
		}
		ModelPreview_SetNodeSwitchIndex(0);
		FrontImage_RegisterResourceDefault("frontres\\review.bmp",
						   "backreview");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backreview", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontImage_DrawSpriteTranslucent("configoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_StartTextFadeIn(20);
	}

	FrontendDraw_RectAssign(
		&rect, TECH_LIBRARY_VIEWPORT_LEFT, TECH_LIBRARY_VIEWPORT_TOP,
		TECH_LIBRARY_VIEWPORT_RIGHT, TECH_LIBRARY_VIEWPORT_BOTTOM);
	ModelPreview_SetObjectEulerDegrees(g_techLibraryPreviewPitchDeg,
					   g_techLibraryPreviewYawDeg,
					   g_techLibraryPreviewRollDeg);
	ModelPreview_SetObjectUpAxisAngleDegrees(g_techLibraryPreviewAngleD);
	ModelPreview_RenderViewport(rect.left, rect.top,
				    rect.right - rect.left + 1,
				    rect.bottom - rect.top + 1, NULL);
	FrontendDraw_RectAssign(
		&rect, TECH_LIBRARY_TITLE_LEFT, TECH_LIBRARY_TITLE_TOP,
		TECH_LIBRARY_TITLE_RIGHT, TECH_LIBRARY_TITLE_BOTTOM);
	FrontendText_DrawCentered(
		TITLE_FONT_SIZE,
		FrontendString_Get(FRONTSTR_001_CRAFT_DATABASE), &rect, 0xFFFF);
	TechLibrary_DrawCraftSpecPanel();

	FrontendDraw_RectAssign(&rect, PILOT_BANNER_LEFT, PILOT_BANNER_TOP,
				PILOT_BANNER_RIGHT, PILOT_BANNER_BOTTOM);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
			g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(BUTTON_FONT_SIZE,
					  g_frontendScratchBuffer, &rect,
					  g_colorYellow);
		animationFrame =
			frameCounter % PILOT_BANNER_ANIMATION_PERIOD_FRAMES;
		animationFrame >>= 1;
		sprintf(g_frontendScratchBuffer, "rebtiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer,
				      PILOT_BANNER_REBEL_X,
				      PILOT_BANNER_ICON_Y);
		sprintf(g_frontendScratchBuffer, "imptiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer,
				      PILOT_BANNER_IMPERIAL_X,
				      PILOT_BANNER_ICON_Y);
	}

	if (Frontend_HandleCommonScreenControls(TECH_LIBRARY_SCREEN_CONTEXT) ==
	    1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (XvtDialog_IsActive()) {
		return 0;
	}
#endif
	TechLibrary_UpdateModelControls();
	FrontendDraw_RectAssign(&rect, TECH_LIBRARY_DONE_LEFT,
				TECH_LIBRARY_DONE_TOP, TECH_LIBRARY_DONE_RIGHT,
				TECH_LIBRARY_DONE_BOTTOM);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_206_DONE));
	}
	buttonPressed = FrontendButton_HandleSpriteButton(
		&rect, "leaveup", "leavedown",
		FrontendString_Get(FRONTSTR_206_DONE), BUTTON_FONT_SIZE, 0,
		DONE_BUTTON_HELD_SLOT, "buttonsound");
	FrontendButton_DisableOverlayText();
	buttonPressed |= FrontendDialog_HasNetworkDismissPacket();
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NET_HOST) {
		buttonPressed |= Net_PollForPlayerCreatedOrBacklog();
	}
	if (buttonPressed != 0) {
		FrontImage_FreeResourceByName("backreview");
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
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
	return 0;
}

/* Handles the craft database's left-hand buttons. Marks navigation slots 0
 * to 2 and 5 to 6 as selected while the mouse is on them with a button down
 * or clicked, and draws the eight slots. Change lighting steps the light
 * direction (g_techLibraryLightX); holding a mouse button on Rotate X or
 * Rotate Y turns the model's yaw or pitch; Previous craft and Next craft
 * step g_techLibrarySelectedShipListIdx with wraparound, rebuild
 * g_techLibraryCraftStats and load the model, raised to world y 100 for a TIE
 * Interceptor or TIE Bomber. Returns 1. The light steps reach (0, 0, 0), a
 * direction with no length, which ModelPreview_SetLightDirection divides by. */
// FUNCTION: XVT 0x4E9AF0
int TechLibrary_UpdateModelControls(void)
{
	enum {
		NAVIGATION_SLOT_COUNT = 8,
		TOP_BUTTON_COUNT = 3,
		BOTTOM_BUTTON_FIRST = 5,
		BOTTOM_BUTTON_END = 7,
		BUTTON_SPACING = 28,
		BUTTON_FONT_SIZE = 12,
		LIGHTING_HELD_SLOT = 13,
		ROTATE_X_HELD_SLOT = 12,
		ROTATE_Y_HELD_SLOT = 11,
		PREVIOUS_CRAFT_HELD_SLOT = 17,
		NEXT_CRAFT_HELD_SLOT = 16,
		SPECIAL_CRAFT_WORLD_Y = 100,
	};

	int mouseY;
	int mouseX;
	struct RECT rect;
	FrontendNavigationSlotState slotStates[NAVIGATION_SLOT_COUNT] = {
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_INACTIVE,
		FRONTEND_NAVIGATION_SLOT_INACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_INACTIVE,
	};
	int slotIndex;
	int selectedCraftType;

	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&rect, 22, 114, 42, 138);
	FrontendCursor_GetPos(&mouseX, &mouseY);
	for (slotIndex = 0; slotIndex < TOP_BUTTON_COUNT; ++slotIndex) {
		if (slotStates[slotIndex] !=
			    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
		    FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0 &&
		    (FrontendMouse_GetLeftDown() != 0 ||
		     FrontendMouse_GetRightDown() != 0 ||
		     FrontendMouse_GetLeftClick() != 0 ||
		     FrontendMouse_GetRightClick() != 0)) {
			slotStates[slotIndex] =
				FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		FrontendDraw_RectOffsetXY(&rect, 0, BUTTON_SPACING);
	}

	FrontendDraw_RectAssign(&rect, 22, 306, 42, 330);
	for (slotIndex = BOTTOM_BUTTON_FIRST; slotIndex < BOTTOM_BUTTON_END;
	     ++slotIndex) {
		if (slotStates[slotIndex] !=
			    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
		    FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0 &&
		    (FrontendMouse_GetLeftDown() != 0 ||
		     FrontendMouse_GetRightDown() != 0 ||
		     FrontendMouse_GetLeftClick() != 0 ||
		     FrontendMouse_GetRightClick() != 0)) {
			slotStates[slotIndex] =
				FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		FrontendDraw_RectOffsetXY(&rect, 0, BUTTON_SPACING);
	}
	FrontendButton_DrawEightSlotNavigationState(slotStates);

	FrontendDraw_RectAssign(&rect, 22, 170, 42, 194);
	if (FrontendButton_HandleSpriteButton(
		    &rect, "review3u", "review3d",
		    FrontendString_Get(FRONTSTR_294_CHANGE_LIGHTING),
		    BUTTON_FONT_SIZE, 0, LIGHTING_HELD_SLOT,
		    "jewelsound") != 0) {
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
		ModelPreview_SetLightDirection(g_techLibraryLightX,
					       g_techLibraryLightY,
					       g_techLibraryLightZ);
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	FrontendButton_HandleSpriteButton(
		&rect, "review2u", "review2d",
		FrontendString_Get(FRONTSTR_292_ROTATE_X), BUTTON_FONT_SIZE, 0,
		ROTATE_X_HELD_SLOT, "jewelsound");
	if (FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0) {
		if (FrontendMouse_GetLeftDown() != 0) {
			g_techLibraryPreviewYawDeg -=
				g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewYawDeg <= 0.0) {
				g_techLibraryPreviewYawDeg = 360.0f;
			}
		} else if (FrontendMouse_GetRightDown() != 0) {
			g_techLibraryPreviewYawDeg +=
				g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewYawDeg >=
			    g_techLibraryRotationFullTurnDegrees) {
				g_techLibraryPreviewYawDeg = 0.0f;
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	FrontendButton_HandleSpriteButton(
		&rect, "review1u", "review1d",
		FrontendString_Get(FRONTSTR_293_ROTATE_Y), BUTTON_FONT_SIZE, 0,
		ROTATE_Y_HELD_SLOT, "jewelsound");
	if (FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0) {
		if (FrontendMouse_GetLeftDown() != 0) {
			g_techLibraryPreviewPitchDeg -=
				g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewPitchDeg <= 0.0) {
				g_techLibraryPreviewPitchDeg = 360.0f;
			}
		} else if (FrontendMouse_GetRightDown() != 0) {
			g_techLibraryPreviewPitchDeg +=
				g_techLibraryRotationStepDegrees;
			if (g_techLibraryPreviewPitchDeg >=
			    g_techLibraryRotationFullTurnDegrees) {
				g_techLibraryPreviewPitchDeg = 0.0f;
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 22, 334, 42, 358);
	if (FrontendButton_HandleSpriteButton(
		    &rect, "review7u", "review7d",
		    FrontendString_Get(FRONTSTR_296_PREVIOUS_CRAFT),
		    BUTTON_FONT_SIZE, 0, PREVIOUS_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		--g_techLibrarySelectedShipListIdx;
		if (g_techLibrarySelectedShipListIdx < 0) {
			g_techLibrarySelectedShipListIdx = g_shipCount - 1;
		}
		memset(&g_techLibraryCraftStats, 0,
		       sizeof(g_techLibraryCraftStats));
		g_techLibraryCraftStats.craftType =
			g_shipList[g_techLibrarySelectedShipListIdx].craftType;
		BuildCraftTechStats(&g_techLibraryCraftStats);
		ModelPreview_LoadModel(
			g_shipList[g_techLibrarySelectedShipListIdx]
				.modelFileName);
		selectedCraftType =
			g_shipList[g_techLibrarySelectedShipListIdx].craftType;
		if (selectedCraftType == CRAFT_SPECIES_TIE_INTERCEPTOR ||
		    selectedCraftType == CRAFT_SPECIES_TIE_BOMBER) {
			ModelPreview_SetObjectWorldPosition(
				0, SPECIAL_CRAFT_WORLD_Y, 0);
		} else {
			ModelPreview_SetObjectWorldPosition(0, 0, 0);
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (FrontendButton_HandleSpriteButton(
		    &rect, "review6u", "review6d",
		    FrontendString_Get(FRONTSTR_295_NEXT_CRAFT),
		    BUTTON_FONT_SIZE, 0, NEXT_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		++g_techLibrarySelectedShipListIdx;
		if (g_shipCount <= g_techLibrarySelectedShipListIdx) {
			g_techLibrarySelectedShipListIdx = 0;
		}
		memset(&g_techLibraryCraftStats, 0,
		       sizeof(g_techLibraryCraftStats));
		g_techLibraryCraftStats.craftType =
			g_shipList[g_techLibrarySelectedShipListIdx].craftType;
		BuildCraftTechStats(&g_techLibraryCraftStats);
		ModelPreview_LoadModel(
			g_shipList[g_techLibrarySelectedShipListIdx]
				.modelFileName);
		selectedCraftType =
			g_shipList[g_techLibrarySelectedShipListIdx].craftType;
		if (selectedCraftType != CRAFT_SPECIES_TIE_INTERCEPTOR &&
		    selectedCraftType != CRAFT_SPECIES_TIE_BOMBER) {
			ModelPreview_SetObjectWorldPosition(0, 0, 0);
			return 1;
		}
		ModelPreview_SetObjectWorldPosition(0, SPECIAL_CRAFT_WORLD_Y,
						    0);
	}
	return 1;
}

/* Draws the spec panel for the craft in g_techLibraryCraftStats, from its entry
 * in g_techLibrarySpecTextTable (craft type minus 1, at least 0), in font 12:
 * the name at (88, 111), then from 30 pixels lower, in rows 15 apart, the
 * designation by genus, manufacturer, users, and the description under a yellow
 * heading. Then, for a starfighter, speed, acceleration, maneuverability, guns
 * and warhead load; for any genus but mine and satellite, the model's size in
 * meters, or kilometers at 1000 meters and over, and the crew; and for all,
 * shield and hull ratings. Returns 1. Checks neither that the table is loaded
 * nor that the entry is under 93. */
// FUNCTION: XVT 0x4EA090
int TechLibrary_DrawCraftSpecPanel(void)
{
	struct RECT rect;
	struct RECT descriptionRect;
	int craftSpecIndex;
	int textLines;
	const char *label;

	FrontendDraw_RectAssign(&rect, 88, 111, 332, 125);
	craftSpecIndex = g_techLibraryCraftStats.craftType - 1;
	if (craftSpecIndex < 0) {
		craftSpecIndex = 0;
	}
	sprintf(g_frontendScratchBuffer, "%s",
		g_techLibrarySpecTextTable[craftSpecIndex].craftName);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1,
				       0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 30);
	label = FrontendString_Get(
		(FrontendStringId)(FRONTSTR_496_STARFIGHTER +
				   g_techLibraryCraftStats.genusId));
	sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4,
		FrontendString_Get(FRONTSTR_483_DESIGNATION), 1, label);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1,
				       0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4,
		FrontendString_Get(FRONTSTR_484_MANUFACTURER), 1,
		g_techLibrarySpecTextTable[craftSpecIndex].manufacturer);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1,
				       0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4,
		FrontendString_Get(FRONTSTR_485_IN_USE_BY), 1,
		g_techLibrarySpecTextTable[craftSpecIndex].inUseBy);
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1,
				       0xFFFF);

	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendText_DrawAlignedInRect(
		12, FrontendString_Get(FRONTSTR_486_SPECIAL_CHARACTERISTICS),
		&rect, 0, 1, g_colorYellow);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	FrontendDraw_RectCopy(&descriptionRect, &rect);
	descriptionRect.bottom = descriptionRect.top + 120;
	descriptionRect.left += 20;
	textLines = FrontendText_DrawWrapped(
		12, g_techLibrarySpecTextTable[craftSpecIndex].description,
		&descriptionRect, 0xFFFF, 3, 0);
	FrontendDraw_RectOffsetXY(&rect, 0, 5 * (3 * textLines + 6));

	if (g_techLibraryCraftStats.genusId == CRAFT_GENUS_STARFIGHTER) {
		sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4,
			FrontendString_Get(FRONTSTR_487_SPEED), 1,
			g_techLibraryCraftStats.speedRating,
			FrontendString_Get(FRONTSTR_513_MGLT));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4,
			FrontendString_Get(FRONTSTR_488_ACCELERATION), 1,
			g_techLibraryCraftStats.accelerationRating,
			FrontendString_Get(FRONTSTR_514_MGLT_SECOND));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4,
			FrontendString_Get(FRONTSTR_489_MANUEVERABILITY_RATING),
			1, g_techLibraryCraftStats.maneuverRating,
			FrontendString_Get(FRONTSTR_714_DPF));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c", 4,
			FrontendString_Get(FRONTSTR_490_LASERS), 1);
		if (g_techLibraryCraftStats.laserCount != 0) {
			strcat(g_frontendScratchBuffer,
			       FrontendString_Get((
				       FrontendStringId)(g_techLibraryCraftStats
								 .laserCount +
							 517)));
			strcat(g_frontendScratchBuffer, " ");
			strcat(g_frontendScratchBuffer,
			       FrontendString_Get(FRONTSTR_516_LASERS));
			if (g_techLibraryCraftStats.ionCount != 0) {
				strcat(g_frontendScratchBuffer, " ");
				strcat(g_frontendScratchBuffer,
				       FrontendString_Get(FRONTSTR_515_AND));
				strcat(g_frontendScratchBuffer, " ");
			}
		}
		if (g_techLibraryCraftStats.ionCount != 0) {
			strcat(g_frontendScratchBuffer,
			       FrontendString_Get((
				       FrontendStringId)(g_techLibraryCraftStats
								 .ionCount +
							 517)));
			strcat(g_frontendScratchBuffer, " ");
			strcat(g_frontendScratchBuffer,
			       FrontendString_Get(FRONTSTR_517_ION_CANNONS));
		}
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%d", 4,
			FrontendString_Get(
				FRONTSTR_491_STD_COMBAT_WARHEAD_LOAD),
			1, g_techLibraryCraftStats.warheadRating);
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
	} else if (g_techLibraryCraftStats.genusId != CRAFT_GENUS_MINE &&
		   g_techLibraryCraftStats.genusId != CRAFT_GENUS_SATELLITE) {
		const double displayedSize =
			(double)ModelPreview_GetDisplayedSizeMeters();
		const double sizeValue = displayedSize >= 1000.0
						 ? displayedSize * 0.001
						 : displayedSize;
		const FrontendStringId sizeUnit = displayedSize >= 1000.0
							  ? FRONTSTR_522_KM
							  : FRONTSTR_712_METERS;

		sprintf(g_frontendScratchBuffer, "%c%s %c%.1f %s", 4,
			FrontendString_Get(FRONTSTR_492_SIZE), 1, sizeValue,
			FrontendString_Get(sizeUnit));
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 4,
			FrontendString_Get(FRONTSTR_493_CREW), 1,
			g_techLibrarySpecTextTable[craftSpecIndex].crew);
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
	}

	sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4,
		FrontendString_Get(FRONTSTR_494_SHIELD_RATING), 1,
		g_techLibraryCraftStats.shieldRating,
		FrontendString_Get(FRONTSTR_715_SBD));
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1,
				       0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	sprintf(g_frontendScratchBuffer, "%c%s %c%d %s", 4,
		FrontendString_Get(FRONTSTR_495_HULL_RATING), 1,
		g_techLibraryCraftStats.hullRating,
		FrontendString_Get(FRONTSTR_713_RU));
	FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1,
				       0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 15);
	return 1;
}

/* Loads specdesc.txt into a new 93-entry g_techLibrarySpecTextTable, freeing
 * the old one. Each entry is five lines, skipping lines that start with "//":
 * name, manufacturer, users, description and crew, each cut to 255
 * characters, without its newline, and copied with strncpy, which leaves no
 * terminator when a line fills its field. Returns 0, keeping the old table,
 * when the file does not open, and 0 when the allocation fails; 1 when the
 * file ends before 93 entries. After all 93, the modern build returns what
 * File_Close returns and the original build returns no value. The original
 * build zeroes the table before checking the allocation, and reads each line
 * with a 1024-byte limit into the 256-byte g_frontendScratchBuffer. */
// FUNCTION: XVT 0x4EA8C0
int TechLibrary_LoadSpecTextTable(void)
{
	XvtFile *stream;
	int fieldIndex;
	int entryIndex;
	size_t length;

	stream = File_Open("specdesc.txt", "r");
	if (stream == NULL) {
		return 0;
	}

	if (g_techLibrarySpecTextTable != NULL) {
		free(g_techLibrarySpecTextTable);
		g_techLibrarySpecTextTable = NULL;
	}

	g_techLibrarySpecTextTable = (struct TechLibrarySpecText *)malloc(
		sizeof(struct TechLibrarySpecText) * 93u);
#ifndef XVT_MODERN
	memset(g_techLibrarySpecTextTable, 0,
	       sizeof(struct TechLibrarySpecText) * 93u);
#endif
	if (g_techLibrarySpecTextTable == NULL) {
		File_Close(stream);
		return 0;
	}

#ifdef XVT_MODERN
	memset(g_techLibrarySpecTextTable, 0,
	       sizeof(struct TechLibrarySpecText) * 93u);
#endif
	for (entryIndex = 0; entryIndex < 93; ++entryIndex) {
		fieldIndex = 0;
		while (fieldIndex < 5) {
			do {
#ifdef XVT_MODERN
				if (File_Gets(g_frontendScratchBuffer,
					      sizeof(g_frontendScratchBuffer),
					      stream) == NULL) {
#else
				if (File_Gets(g_frontendScratchBuffer, 1024,
					      stream) == NULL) {
#endif
					File_Close(stream);
					return 1;
				}
				g_frontendScratchBuffer[255] = '\0';
			} while (g_frontendScratchBuffer[0] == '/' &&
				 g_frontendScratchBuffer[1] == '/');

			length = strlen(g_frontendScratchBuffer);
			if (g_frontendScratchBuffer[length - 1] == '\n') {
				g_frontendScratchBuffer[length - 1] = '\0';
			}

			switch (fieldIndex) {
			case 0:
				strncpy(g_techLibrarySpecTextTable[entryIndex]
						.craftName,
					g_frontendScratchBuffer, 64u);
				break;
			case 1:
				strncpy(g_techLibrarySpecTextTable[entryIndex]
						.manufacturer,
					g_frontendScratchBuffer, 64u);
				break;
			case 2:
				strncpy(g_techLibrarySpecTextTable[entryIndex]
						.inUseBy,
					g_frontendScratchBuffer, 64u);
				break;
			case 3:
				strncpy(g_techLibrarySpecTextTable[entryIndex]
						.description,
					g_frontendScratchBuffer, 256u);
				break;
			case 4:
				strncpy(g_techLibrarySpecTextTable[entryIndex]
						.crew,
					g_frontendScratchBuffer, 64u);
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

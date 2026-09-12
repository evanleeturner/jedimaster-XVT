#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0x6691E8
int16_t g_briefingSelectedMissionPoint14FlightGroupIdx = 0;

// GLOBAL: XVT 0x669204
BriefingMapS16Pair g_briefingMapCenter = { 0, 0 };
// GLOBAL: XVT 0x669208
BriefingMapS16Pair g_briefingMapTargetCenter = { 0, 0 };
// GLOBAL: XVT 0x66920C
BriefingMapS16Pair g_briefingMapScale = { 0, 0 };
// GLOBAL: XVT 0x669210
BriefingMapS16Pair g_briefingMapTargetScale = { 0, 0 };
// GLOBAL: XVT 0x669214
int g_briefingTeamIndex = 0;
// GLOBAL: XVT 0x52C908
int g_mapIconByCraftType[106] = {
	0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  0,  0,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24, 25, 26, 27, 0,  28, 29, 30, 31, 64, 32, 33, 33, 34, 35, 36, 37,
	38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 65, 48, 49, 50, 51, 52, 53, 53, 53, 53, 53, 53,
	63, 61, 62, 54, 55, 55, 55, 55, 55, 56, 57, 58, 66, 60, 59, 59, 59, 60, 60, 58, 61, 61,
	0,  0,  67, 68, 69, 0,  0,  0,  0,  0,  0,  0,  61, 61, 61, 61, 61, 61,
};
// GLOBAL: XVT 0x52CAB0
RECT g_mapIconRects[70] = {
	{ 6, 9, 13, 20 },      { 25, 8, 32, 20 },     { 44, 10, 50, 19 },     { 61, 10, 71, 19 },
	{ 82, 10, 89, 18 },    { 101, 10, 108, 18 },  { 118, 10, 128, 19 },   { 139, 9, 145, 19 },
	{ 157, 10, 166, 19 },  { 175, 9, 185, 18 },   { 196, 8, 202, 20 },    { 215, 9, 221, 19 },
	{ 234, 10, 241, 19 },  { 251, 10, 261, 18 },  { 270, 10, 280, 18 },   { 289, 9, 299, 19 },
	{ 6, 29, 12, 42 },     { 25, 30, 31, 42 },    { 45, 31, 50, 41 },     { 63, 30, 70, 42 },
	{ 82, 30, 89, 42 },    { 103, 34, 106, 39 },  { 121, 33, 125, 40 },   { 140, 31, 145, 41 },
	{ 158, 32, 166, 40 },  { 179, 32, 182, 40 },  { 195, 32, 203, 40 },   { 216, 33, 221, 40 },
	{ 233, 29, 242, 43 },  { 252, 30, 260, 41 },  { 271, 30, 279, 42 },   { 290, 29, 298, 44 },
	{ 5, 53, 14, 63 },     { 24, 53, 32, 64 },    { 44, 51, 50, 65 },     { 63, 51, 69, 65 },
	{ 83, 50, 87, 66 },    { 100, 50, 108, 66 },  { 120, 49, 126, 66 },   { 140, 51, 145, 66 },
	{ 158, 50, 164, 66 },  { 177, 50, 183, 66 },  { 196, 50, 203, 66 },   { 214, 49, 222, 68 },
	{ 234, 50, 240, 66 },  { 253, 51, 259, 65 },  { 271, 50, 279, 66 },   { 289, 48, 299, 68 },
	{ 5, 77, 14, 83 },     { 26, 77, 31, 83 },    { 43, 78, 51, 83 },     { 63, 76, 69, 84 },
	{ 83, 76, 88, 84 },    { 98, 74, 111, 87 },   { 115, 73, 131, 87 },   { 138, 99, 146, 104 },
	{ 157, 98, 165, 105 }, { 176, 98, 184, 105 }, { 197, 101, 202, 103 }, { 216, 100, 221, 105 },
	{ 234, 97, 240, 106 }, { 253, 77, 259, 83 },  { 272, 77, 278, 83 },   { 288, 72, 301, 88 },
	{ 46, 95, 50, 108 },   { 24, 90, 32, 110 },   { 65, 97, 68, 105 },    { 79, 95, 93, 108 },
	{ 98, 95, 111, 108 },  { 120, 94, 128, 108 },
};
// GLOBAL: XVT 0x669220
RECT g_briefingMapSourceRect = { 0, 0, 0, 0 };
// GLOBAL: XVT 0x6696D6
int16_t g_briefingMapCenterDirty = 0;
// GLOBAL: XVT 0x6696D8
int16_t g_briefingMapScaleDirty = 0;
// GLOBAL: XVT 0x6696E4
int16_t g_briefingMapFgMarkerActive[8] = { 0 };
// GLOBAL: XVT 0x6696F4
int16_t g_briefingMapFgMarkerIconIdx[8] = { 0 };
// GLOBAL: XVT 0x669704
int16_t g_briefingMapFgMarkerAge[8] = { 0 };
// GLOBAL: XVT 0x669714
int16_t g_briefingMapFgMarkersChanged = 0;
// GLOBAL: XVT 0x669716
int16_t g_briefingMapLabelActive[8] = { 0 };
// GLOBAL: XVT 0x669726
int16_t g_briefingMapLabelTextIdx[8] = { 0 };
// GLOBAL: XVT 0x669736
int16_t g_briefingMapLabelX[8] = { 0 };
// GLOBAL: XVT 0x669746
int16_t g_briefingMapLabelY[8] = { 0 };
// GLOBAL: XVT 0x669756
int16_t g_briefingMapLabelAge[8] = { 0 };
// GLOBAL: XVT 0x669766
int16_t g_briefingMapLabelStyle[8] = { 0 };
// GLOBAL: XVT 0x669776
int16_t g_briefingMapLabelsChanged = 0;

// FUNCTION: XVT 0x4F7A30
int16_t BriefingMap_SelectNearestMissionPoint14FlightGroup(RECT* viewportRect, int16_t mouseX,
														   int16_t mouseY) {
	int distanceX;
	int distanceY;
	int16_t selectedFlightGroupIdx = 0;
	int16_t projectedY;
	int16_t projectedX;
	int16_t bestDistance;
	int16_t flightGroupIdx;

	bestDistance = 999;
	flightGroupIdx = 0;
	for (; (int16_t)g_frontendMission.flightGroupCount > flightGroupIdx; flightGroupIdx++) {
		int16_t mapX = g_frontendMission.flightGroups[flightGroupIdx].missionPointX[14];
		int16_t mapY = g_frontendMission.flightGroups[flightGroupIdx].missionPointY[14];
		if (g_frontendMission.flightGroups[flightGroupIdx].missionPointEnabled[14] != 0) {
			BriefingMap_ProjectPointToViewport(
				viewportRect, mapX, mapY, &projectedX, &projectedY);
			distanceX = abs(mouseX - projectedX);
			if (distanceX < bestDistance) {
				distanceY = abs(mouseY - projectedY);
				if (distanceY < bestDistance) {
					bestDistance = (int16_t)distanceY;
					if (distanceY <= distanceX) {
						bestDistance = (int16_t)distanceX;
					}
					selectedFlightGroupIdx = flightGroupIdx;
				}
			}
		}
	}

	if (bestDistance != 999) {
		g_briefingSelectedMissionPoint14FlightGroupIdx = selectedFlightGroupIdx;
		return 1;
	}
	return 0;
}

// FUNCTION: XVT 0x4F7B10
void BriefingMap_ProjectPointToViewport(const RECT* viewportRect, int16_t mapX, int16_t mapY, int16_t* outX,
										int16_t* outY) {
	int projectedX;
	int projectedY;

	projectedX = g_briefingMapScale.x * (mapX - g_briefingMapCenter.x) / 256;
	*outX = (int16_t)projectedX;
	*outX = (int16_t)(projectedX + viewportRect->left + ((viewportRect->right - viewportRect->left) >> 1));
	projectedY = g_briefingMapScale.y * (mapY - g_briefingMapCenter.y) / 256;
	*outY = (int16_t)projectedY;
	*outY = (int16_t)(projectedY + viewportRect->top + ((viewportRect->bottom - viewportRect->top) >> 1));
}

// FUNCTION: XVT 0x4F7C20
int16_t BriefingMap_StepS16TowardTarget(int16_t current, int16_t target, int16_t step) {
	if (target < current) {
		current = (int16_t)(current - step);
		if (target > current) {
			current = target;
		}
	}
	if (target > current) {
		current = (int16_t)(current + step);
		if (target < current) {
			current = target;
		}
	}
	return current;
}

// FUNCTION: XVT 0x4F7C50
void BriefingMap_AnimateViewState(void) {
	int16_t maximumDifference;
	int16_t axisDifference;
	int16_t scaleStep;
	int16_t scaleDivisor;
	int16_t centerDifference;
	int16_t index;

	maximumDifference = abs(g_briefingMapScale.x - g_briefingMapTargetScale.x);
	axisDifference = abs(g_briefingMapScale.y - g_briefingMapTargetScale.y);
	if (maximumDifference < axisDifference) {
		maximumDifference = axisDifference;
	}
	scaleStep = 2;
	if (maximumDifference >= 12) {
		scaleStep = 8;
	}
	if (g_briefingMapScale.x < 10) {
		scaleStep = 1;
	}
	g_briefingMapScale.x =
		BriefingMap_StepS16TowardTarget(g_briefingMapScale.x, g_briefingMapTargetScale.x, scaleStep);
	g_briefingMapScale.y =
		BriefingMap_StepS16TowardTarget(g_briefingMapScale.y, g_briefingMapTargetScale.y, scaleStep);

	if (g_briefingMapScale.x != 0) {
		scaleDivisor = 256 / g_briefingMapScale.x + 1;
	} else {
		scaleDivisor = 1;
	}
	centerDifference = abs(g_briefingMapCenter.x - g_briefingMapTargetCenter.x);
	axisDifference = abs(g_briefingMapCenter.y - g_briefingMapTargetCenter.y);
	if (centerDifference < axisDifference) {
		centerDifference = axisDifference;
	}
	axisDifference = centerDifference / (int16_t)scaleDivisor;
	scaleDivisor *= 2;
	if (axisDifference >= 16) {
		scaleDivisor *= 2;
	}
	g_briefingMapCenter.x =
		BriefingMap_StepS16TowardTarget(g_briefingMapCenter.x, g_briefingMapTargetCenter.x, scaleDivisor);
	g_briefingMapCenter.y =
		BriefingMap_StepS16TowardTarget(g_briefingMapCenter.y, g_briefingMapTargetCenter.y, scaleDivisor);

	index = 0;
	do {
		if (g_briefingMapFgMarkerActive[index] != 0) {
			++g_briefingMapFgMarkerAge[index];
		}
		++index;
	} while (index < 8);
	for (index = 0; index < 8; ++index) {
		if (g_briefingMapLabelActive[index] != 0) {
			++g_briefingMapLabelAge[index];
		}
	}
}

// FUNCTION: XVT 0x4F7DD0
void BriefingMap_UpdateScriptPlaybackAfterAnimation(void) {
	if (g_briefingScript.currentTime < g_briefingScript.durationTicks) {
		BriefingScript_AdvanceFrame(0);
	} else {
		g_briefingLastNarratedTextBlockIdx = 0;
		g_briefingTextPageNumber = 0;
		BriefingScript_ResetState();
	}
}

// FUNCTION: XVT 0x4F7E00
int16_t BriefingMap_MouseInputStub(RECT* viewportRect, RECT* clipRect, int leftDown, int rightDown,
								   int16_t mouseX, int16_t mouseY) {
	RECT dst;

	(void)viewportRect;
	(void)clipRect;
	(void)leftDown;
	(void)rightDown;

	FrontendDraw_RectCopy(&dst, &g_briefingMapSourceRect);
	BriefingMap_SelectNearestMissionPoint14FlightGroup(&dst, mouseX, mouseY);
	return 1;
}

// FUNCTION: XVT 0x4F7E40
int16_t BriefingMap_DrawViewportAndSelection(RECT* viewportRect, RECT* clipRect, int16_t highlightPhase) {
	RECT titleRect;
	RECT narrationRect;
	RECT mapViewportRect;
	RECT clippedRect;

	(void)highlightPhase;

	FrontendDraw_RectCopy(&titleRect, &g_briefingMapSourceRect);
	FrontendDraw_RectOffsetXY(&titleRect, viewportRect->left, viewportRect->top);
	titleRect.bottom = titleRect.top + 12;

	FrontendDraw_RectCopy(&narrationRect, &g_briefingMapSourceRect);
	FrontendDraw_RectOffsetXY(&narrationRect, viewportRect->left, viewportRect->top);
	narrationRect.top = narrationRect.bottom - 27;
	if (g_briefingTextSlotActive[1] != 0) {
		FrontendText_DrawFormattedWrappedText(
			&narrationRect, (const uint8_t*)g_briefingTextBlocks[g_briefingTextSlotBlockIdx[1]], 0);
		if (g_briefingLastNarratedTextBlockIdx != g_briefingTextSlotBlockIdx[1]) {
			++g_briefingTextPageNumber;
			g_briefingLastNarratedTextBlockIdx = g_briefingTextSlotBlockIdx[1];
		}
	}

	FrontendDraw_RectCopy(&mapViewportRect, &g_briefingMapSourceRect);
	FrontendDraw_RectOffsetXY(&mapViewportRect, viewportRect->left, viewportRect->top);
	mapViewportRect.bottom -= 28;
	FrontendDraw_RectCopy(&clippedRect, clipRect);
	FrontendDisplay_SetScreenClipRect640x480(&mapViewportRect);
	FrontendDraw_RectClipToBounds(&clippedRect);
	FrontendDisplay_SetScreenClipRect640x480(&clippedRect);
	BriefingMap_DrawGrid(&mapViewportRect, &clippedRect);
	BriefingMap_DrawOverlays(&mapViewportRect, &clippedRect);

	sprintf(g_frontendScratchBuffer, "%s %d", FrontendString_Get(FRONTSTR_640_PAGE),
			g_briefingTextPageNumber);
	FrontendText_Draw(10, g_frontendScratchBuffer, mapViewportRect.right - 60, mapViewportRect.bottom - 14,
					  0xFFFF);
	return 1;
}

// FUNCTION: XVT 0x4F7FD0
void BriefingMap_DrawGrid(const RECT* viewportRect, const RECT* clipRect) {
	int16_t drawX;
	int16_t drawY;
	int16_t xPhase;
	int16_t yPhase;
	int16_t drawAllMinorLines;
	int16_t minorColor;
	int16_t majorColor;
	int16_t xGridIndex;
	int16_t yGridIndex;
	int16_t gridStartX;
	int16_t gridStartY;
	int centerRemainder;
	RECT dst;

	(void)clipRect;

	majorColor = (int16_t)FrontendDisplay_PackRGB(0x96, 0, 0);
	minorColor = (int16_t)FrontendDisplay_PackRGB(0x50, 0, 0);
	FrontendDraw_RectCopy(&dst, viewportRect);

	xGridIndex = g_briefingMapCenter.x / 256;
	if (g_briefingMapCenter.x > 0 && (uint8_t)g_briefingMapCenter.x != 0)
		++xGridIndex;
	centerRemainder = -g_briefingMapCenter.x;
	centerRemainder &= 0xFF;
	gridStartX =
		(int16_t)(dst.left + ((dst.right - dst.left) >> 1) + ((g_briefingMapScale.x * centerRemainder) >> 8));
	while (gridStartX > dst.left) {
		gridStartX = (int16_t)(gridStartX - g_briefingMapScale.x);
		--xGridIndex;
	}

	yGridIndex = g_briefingMapCenter.y / 256;
	if (g_briefingMapCenter.y > 0 && (uint8_t)g_briefingMapCenter.y != 0)
		++yGridIndex;
	centerRemainder = -g_briefingMapCenter.y;
	centerRemainder &= 0xFF;
	gridStartY =
		(int16_t)(dst.top + ((dst.bottom - dst.top) >> 1) + ((g_briefingMapScale.y * centerRemainder) >> 8));
	while (gridStartY > dst.top) {
		gridStartY = (int16_t)(gridStartY - g_briefingMapScale.y);
		--yGridIndex;
	}

	drawX = gridStartX;
	drawY = gridStartY;
	xPhase = xGridIndex;
	yPhase = yGridIndex;
	if (g_briefingMapScale.x >= 16) {
		drawAllMinorLines = g_briefingMapScale.x >= 32;
		if (gridStartX < dst.right) {
			do {
				if ((xPhase & 3) != 0 && ((xPhase & 3) == 2 || drawAllMinorLines))
					FrontendDraw_VerticalLineClipped(dst.top, dst.bottom, drawX, minorColor);
				drawX = (int16_t)(drawX + g_briefingMapScale.x);
				++xPhase;
			} while (drawX < dst.right);
		}
		if (gridStartY < dst.bottom) {
			do {
				if ((yPhase & 3) != 0 && ((yPhase & 3) == 2 || drawAllMinorLines))
					FrontendDraw_HorizontalLineClipped(dst.left, dst.right, drawY, minorColor);
				drawY = (int16_t)(drawY + g_briefingMapScale.y);
				++yPhase;
			} while (drawY < dst.bottom);
		}
		drawX = gridStartX;
		drawY = gridStartY;
		xPhase = xGridIndex;
		yPhase = yGridIndex;
	}

	if (gridStartX < dst.right) {
		do {
			if ((xPhase & 3) == 0)
				FrontendDraw_VerticalLineClipped(dst.top, dst.bottom, drawX, majorColor);
			drawX = (int16_t)(drawX + g_briefingMapScale.x);
			++xPhase;
		} while (drawX < dst.right);
	}
	if (gridStartY < dst.bottom) {
		do {
			if ((yPhase & 3) == 0)
				FrontendDraw_HorizontalLineClipped(dst.left, dst.right, drawY, majorColor);
			drawY = (int16_t)(drawY + g_briefingMapScale.y);
			++yPhase;
		} while (drawY < dst.bottom);
	}
}

// FUNCTION: XVT 0x4F82A0
void BriefingMap_DrawOverlays(RECT* viewportRect, RECT* clipRect) {
	int16_t projectedX;
	int16_t projectedY;
	int16_t index;
	int playerIconNumber;
	RECT mapRect;
	char text[40];

	FrontendDraw_RectCopy(&mapRect, viewportRect);
	index = 0;
	do {
		if (g_briefingMapFgMarkerActive[index] != 0) {
			int16_t briefingIconIndex;

			briefingIconIndex = g_briefingMapFgMarkerIconIdx[index];
			BriefingMap_ProjectPointToViewport(
				&mapRect, g_frontendMission.flightGroups[briefingIconIndex].missionPointX[14],
				g_frontendMission.flightGroups[briefingIconIndex].missionPointY[14], &projectedX,
				&projectedY);
			BriefingMap_DrawCraftIconHighlight(viewportRect, clipRect, briefingIconIndex,
											   g_briefingMapFgMarkerAge[index]);
		}
		++index;
	} while (index < 8);

	for (index = 0; index < 8; ++index) {
		if (g_briefingMapLabelActive[index] != 0) {
			int16_t characterIndex;
			int16_t textIndex;

			textIndex = g_briefingMapLabelTextIdx[index];
			BriefingMap_ProjectPointToViewport(&mapRect, g_briefingMapLabelX[index],
											   g_briefingMapLabelY[index], &projectedX, &projectedY);
			strcpy(text, g_briefingMapLabelTexts[textIndex]);
			for (characterIndex = 0; text[characterIndex] != '\0'; ++characterIndex) {
				if (text[characterIndex] == '[') {
					text[characterIndex] = 2;
				}
				if (text[characterIndex] == ']') {
					text[characterIndex] = 1;
				}
			}
			BriefingMap_DrawRevealedLabelIfActive(text, 1, projectedX, projectedY,
												  g_briefingMapLabelAge[index],
												  g_briefingMapLabelStyle[index]);
		}
	}

	playerIconNumber = 1;
	for (index = 0; index < (int16_t)g_frontendMission.flightGroupCount; ++index) {
		int16_t craftType;
		int16_t mapX;
		int16_t mapY;
		int missionPointIndex;

		craftType = g_frontendMission.flightGroups[index].craftType;
		missionPointIndex = g_briefingTeamIndex + 14;
		mapX = g_frontendMission.flightGroups[index].missionPointX[missionPointIndex];
		mapY = g_frontendMission.flightGroups[index].missionPointY[missionPointIndex];
		if (g_frontendMission.flightGroups[index].missionPointEnabled[missionPointIndex] != 0) {
			int16_t iconColorIndex;

#ifdef XVT_MODERN
			iconColorIndex = 0;
#endif
			switch (g_frontendMission.flightGroups[index].iff) {
				case 0:
					iconColorIndex = 0;
					break;
				case 1:
					iconColorIndex = 1;
					break;
				case 2:
					iconColorIndex = 2;
					break;
				case 3:
					iconColorIndex = 3;
					break;
				case 4:
					iconColorIndex = 1;
					break;
				case 5:
					iconColorIndex = 4;
					break;
				default:
					break;
			}

			if (craftType >= 0) {
				int iconHeight;
				int iconIndex;
				int iconWidth;
				RECT* iconRect;

				iconIndex = g_mapIconByCraftType[craftType];
				iconRect = &g_mapIconRects[iconIndex];
				iconWidth = iconRect->right - iconRect->left + 1;
				iconHeight = iconRect->bottom - iconRect->top + 1;
				BriefingMap_ProjectPointToViewport(&mapRect, mapX, mapY, &projectedX, &projectedY);
				projectedX = (int16_t)(projectedX - (iconWidth >> 1));
				projectedY = (int16_t)(projectedY - (iconHeight >> 1));
				sprintf(g_frontendScratchBuffer, "mapicon%d", iconColorIndex);
				FrontImage_DrawSpriteRectTransparent(g_frontendScratchBuffer, iconRect, projectedX,
													 projectedY);

				if (g_frontendMission.flightGroups[index].playerNumber != 0 &&
					g_frontendMission.flightGroups[index].team == g_pilotData.team) {
					projectedX = (int16_t)(projectedX + iconWidth);
					projectedY = (int16_t)(projectedY + iconHeight);
					sprintf(g_frontendScratchBuffer, "%u", playerIconNumber);
					FrontendText_Draw(10, g_frontendScratchBuffer, projectedX, projectedY, 0xFFFF);
					++playerIconNumber;
				}
			}
		}
	}
}

// FUNCTION: XVT 0x4F8910
void BriefingMap_DrawRevealedLabelIfActive(const char* text, int16_t colorRampGroup, int16_t x, int16_t y,
										   int16_t revealCount, int16_t shadeGroup) {
	if (revealCount >= 0)
		BriefingMap_DrawRevealedLabel(text, colorRampGroup, x, y, (int16_t)(2 * revealCount), shadeGroup);
}

// FUNCTION: XVT 0x4F8950
void BriefingMap_DrawRevealedLabel(const char* text, int16_t colorRampGroup, int16_t x, int16_t y,
								   int16_t revealCount, int16_t shadeGroup) {
	RECT rect;
	char visibleText[64];
	int16_t shadeBase;
	int16_t textLength;
	int16_t visibleCount;
	int16_t revealPhase;
	int16_t shadeIndex;
	int16_t textWidth;
	int shadeColor;
	int visibleIndex;
	int16_t finalShadeIndex;

	(void)colorRampGroup;

	if (revealCount < 0)
		return;

	textLength = (int16_t)strlen(text);
	strcpy(visibleText, text);
	shadeBase = (int16_t)(8 * shadeGroup);
	if (textLength + 2 > revealCount) {
		if (textLength >= revealCount) {
			visibleText[revealCount] = '\0';
			visibleCount = revealCount;
		} else {
			visibleCount = textLength;
		}
		revealPhase = revealCount;
		if (revealPhase > 3)
			revealPhase = 3;
		shadeIndex = (int16_t)(shadeBase + 2 * (3 - revealPhase));
		textWidth = (int16_t)FrontendText_MeasureWidth(visibleText, 10);
		while (shadeIndex <= shadeBase + 6 && visibleCount > 0) {
			shadeColor = g_textShadeRamps[0][shadeIndex++];
			visibleIndex = visibleCount--;
			visibleText[visibleIndex] = '\0';
			FrontendText_Draw(10, visibleText, x, y, shadeColor);
		}
		FrontendDraw_RectAssign(&rect, x + textWidth + 2, y, x + textWidth + 8, y + 6);
		if (textLength > revealCount)
			FrontendDraw_Rect(&rect, 0, 0, g_textShadeRamps[0][shadeBase + 7], 1);
	} else {
		strcpy(visibleText, text);
		if (textLength + 5 > revealCount) {
			finalShadeIndex = (int16_t)(textLength - revealCount + shadeBase + 9);
		} else {
			finalShadeIndex = (int16_t)(shadeBase + 4);
		}
		FrontendText_Draw(10, visibleText, x, y, g_textShadeRamps[0][finalShadeIndex]);
	}
}

// FUNCTION: XVT 0x4F8B30
void BriefingMap_DrawCraftIconHighlight(RECT* viewportRect, RECT* clipRect, int briefingIconIndex,
										int highlightPhase) {
	int iconIndex;
	int16_t craftType;
	int16_t colorBase;
	int16_t screenX;
	int16_t screenY;
	int16_t mapX;
	int16_t mapY;
	int mapIconIndex;
	int iconWidth;
	int iconHeight;
	RECT rect;

	(void)clipRect;

	iconIndex = (int16_t)briefingIconIndex;
	craftType = g_frontendMission.flightGroups[iconIndex].craftType;
	mapX = g_frontendMission.flightGroups[iconIndex].missionPointX[g_briefingTeamIndex + 14];
	mapY = g_frontendMission.flightGroups[iconIndex].missionPointY[g_briefingTeamIndex + 14];
	switch (g_frontendMission.flightGroups[iconIndex].iff) {
		case 0:
			colorBase = 0;
			break;
		case 1:
		case 4:
			colorBase = 8;
			break;
		case 2:
			colorBase = 24;
			break;
		case 3:
			colorBase = 16;
			break;
		case 5:
			colorBase = 32;
			break;
		default:
			colorBase = 0;
			break;
	}

	if (craftType < 0)
		return;

	BriefingMap_ProjectPointToViewport(viewportRect, mapX, mapY, &screenX, &screenY);
	mapIconIndex = g_mapIconByCraftType[craftType];
	iconWidth = g_mapIconRects[mapIconIndex].right - g_mapIconRects[mapIconIndex].left + 1;
	iconHeight = g_mapIconRects[mapIconIndex].bottom - g_mapIconRects[mapIconIndex].top + 1;
	screenX = (int16_t)(screenX - (iconWidth >> 1));
	screenY = (int16_t)(screenY - (iconHeight >> 1));
	FrontendDraw_RectAssign(&rect, screenX, screenY, screenX + iconWidth - 1, screenY + iconHeight - 1);

	if ((int16_t)highlightPhase < 12) {
		int16_t count;
		int16_t shadeIndex;
		int16_t offset;

		if ((int16_t)highlightPhase < 4) {
			shadeIndex = (int16_t)(colorBase - 2 * highlightPhase + 7);
			offset = 16;
			count = (int16_t)(highlightPhase + 1);
		} else if ((int16_t)highlightPhase < 8) {
			shadeIndex = (int16_t)(colorBase + 1);
			offset = (int16_t)(2 * (11 - highlightPhase));
			count = 4;
		} else {
			shadeIndex = (int16_t)(colorBase + 1);
			offset = (int16_t)(2 * (11 - highlightPhase));
			count = (int16_t)(12 - highlightPhase);
		}

		if ((int16_t)highlightPhase >= 8) {
			int inset;

			inset = 11 - (int16_t)highlightPhase;
			FrontendDraw_RectInsetXY(&rect, inset, inset);
			FrontendDraw_Rect(&rect, 0, 0, g_textShadeRamps[0][colorBase + 2], 1);
			FrontendDraw_RectOutline(&rect, 0, 0,
									 g_textShadeRamps[0][colorBase + (int16_t)highlightPhase - 6]);
		}

		if (count > 0) {
			int16_t repeatCount;

			repeatCount = count;
			do {
				int* tintColor;
				int tintIndex;
				int drawOffset;

				tintIndex = shadeIndex;
				drawOffset = (int16_t)offset;
				shadeIndex = (int16_t)(shadeIndex + 2);
				tintColor = &g_textShadeRamps[0][tintIndex];
				FrontImage_DrawSpriteRectTinted("greyicon", &g_mapIconRects[mapIconIndex],
												screenX - drawOffset, screenY - drawOffset, *tintColor);
				FrontImage_DrawSpriteRectTinted("greyicon", &g_mapIconRects[mapIconIndex],
												screenX + drawOffset, screenY - drawOffset, *tintColor);
				FrontImage_DrawSpriteRectTinted("greyicon", &g_mapIconRects[mapIconIndex],
												screenX - drawOffset, screenY + drawOffset, *tintColor);
				FrontImage_DrawSpriteRectTinted("greyicon", &g_mapIconRects[mapIconIndex],
												screenX + drawOffset, screenY + drawOffset, *tintColor);
				offset = (int16_t)(offset - 2);
			} while (--repeatCount != 0);
		}
	} else {
		FrontendDraw_RectInsetXY(&rect, -2, -2);
		FrontendDraw_Rect(&rect, 0, 0, g_textShadeRamps[0][colorBase + 2], 1);
		FrontendDraw_RectOutline(&rect, 0, 0, g_textShadeRamps[0][colorBase + 6]);
	}
}

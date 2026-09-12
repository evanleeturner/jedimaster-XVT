#ifndef XVT_FRONTEND_BRIEFING_MAP_H
#define XVT_FRONTEND_BRIEFING_MAP_H

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct BriefingMapS16Pair {
	int16_t x;
	int16_t y;
};

extern int16_t g_briefingSelectedMissionPoint14FlightGroupIdx;
extern BriefingMapS16Pair g_briefingMapCenter;
extern BriefingMapS16Pair g_briefingMapTargetCenter;
extern BriefingMapS16Pair g_briefingMapScale;
extern BriefingMapS16Pair g_briefingMapTargetScale;
extern int16_t g_briefingMapCenterDirty;
extern int16_t g_briefingMapScaleDirty;
extern int g_briefingTeamIndex;
extern RECT g_briefingMapSourceRect;
extern int g_mapIconByCraftType[106];
extern RECT g_mapIconRects[70];
extern int16_t g_briefingMapFgMarkerActive[8];
extern int16_t g_briefingMapFgMarkerIconIdx[8];
extern int16_t g_briefingMapFgMarkerAge[8];
extern int16_t g_briefingMapFgMarkersChanged;
extern int16_t g_briefingMapLabelActive[8];
extern int16_t g_briefingMapLabelTextIdx[8];
extern int16_t g_briefingMapLabelX[8];
extern int16_t g_briefingMapLabelY[8];
extern int16_t g_briefingMapLabelAge[8];
extern int16_t g_briefingMapLabelStyle[8];
extern int16_t g_briefingMapLabelsChanged;

int16_t BriefingMap_SelectNearestMissionPoint14FlightGroup(RECT* viewportRect, int16_t mouseX,
														   int16_t mouseY);
void BriefingMap_ProjectPointToViewport(const RECT* viewportRect, int16_t mapX, int16_t mapY, int16_t* outX,
										int16_t* outY);
int16_t BriefingMap_StepS16TowardTarget(int16_t current, int16_t target, int16_t step);
void BriefingMap_AnimateViewState(void);
void BriefingMap_UpdateScriptPlaybackAfterAnimation(void);
int16_t BriefingMap_MouseInputStub(RECT* viewportRect, RECT* clipRect, int leftDown, int rightDown,
								   int16_t mouseX, int16_t mouseY);
int16_t BriefingMap_DrawViewportAndSelection(RECT* viewportRect, RECT* clipRect, int16_t highlightPhase);
void BriefingMap_DrawGrid(const RECT* viewportRect, const RECT* clipRect);
void BriefingMap_DrawOverlays(RECT* viewportRect, RECT* clipRect);
void BriefingMap_DrawRevealedLabelIfActive(const char* text, int16_t colorRampGroup, int16_t x, int16_t y,
										   int16_t revealCount, int16_t shadeGroup);
void BriefingMap_DrawRevealedLabel(const char* text, int16_t colorRampGroup, int16_t x, int16_t y,
								   int16_t revealCount, int16_t shadeGroup);
void BriefingMap_DrawCraftIconHighlight(RECT* viewportRect, RECT* clipRect, int briefingIconIndex,
										int highlightPhase);

#ifdef __cplusplus
}
#endif

#endif

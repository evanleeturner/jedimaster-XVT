#include "xvt/flight/flight_loading.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_net.h"
#include "xvt/render/renderer.h"
#include "xvt/util/time.h"

// GLOBAL: XVT 0x5236A8
uint32_t g_flightLoadingProgressStep;
// GLOBAL: XVT 0x5236AC
uint32_t g_flightLoadingProgressLastDrawTick;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x449110
void FlightLoading_ResetProgressState(void) {
#ifdef XVT_MODERN
	XvtCockpitMessages_ClearProgress();
#endif
	g_flightLoadingProgressStep = 0;
	g_flightLoadingProgressLastDrawTick = timeGetTime();
}

// FUNCTION: XVT 0x449130
void FlightLoading_PulseAndDrawProgressScreen(void) {
	/* Advance the progress pulse and redraw the loading bar when due. */
	uint32_t now;
	uint32_t stepPhase;
	int16_t savedCursorX;
	int16_t savedCursorY;
	int16_t savedClipLeft;
	int16_t savedClipTop;
	int16_t savedClipRight;
	int16_t savedClipBottom;
	int16_t savedWordWrap;
	int16_t savedReservedState;
	uint8_t savedTextColor;
	int16_t savedClearLineBackground;
	uint8_t savedBackgroundColor;
	uint8_t savedShadowColor;
	uint8_t savedShadowEnabled;
	int lockCount;
	int unlockCount;
	unsigned int barLeft;
	unsigned int barTop;
	uint8_t lineHeight;
	uint32_t barStep;
	unsigned int barWidth;

	now = timeGetTime();
	stepPhase = g_flightLoadingProgressStep & 0x3fu;
	if (stepPhase != 63u && (int32_t)(now - g_flightLoadingProgressLastDrawTick) < 200) {
		++g_flightLoadingProgressStep;
		return;
	}

#ifdef XVT_MODERN
	XvtRenderCapture_BeginOverlay();
#endif
	g_flightLoadingProgressLastDrawTick = now;
	if (stepPhase == 63u)
		FlightNet_BroadcastStillLoadingPulse();

	savedCursorX = g_flightCursorX;
	savedCursorY = g_flightCursorY;
	savedClipLeft = g_flightClipLeft;
	savedClipTop = g_flightClipTop;
	savedClipRight = g_flightClipRight;
	savedClipBottom = g_flightClipBottom;
	savedWordWrap = g_flightWordWrapEnabled;
	savedReservedState = g_flightTextReservedState91079E;
	savedTextColor = g_flightTextColorIndex;
	savedClearLineBackground = g_flightClearLineBgEnabled;
	savedBackgroundColor = g_flightTextBgColor;
	savedShadowColor = g_flightTextShadowColor;
	savedShadowEnabled = g_flightTextShadowEnabled;

	lockCount = FlightSurface_GetLockCount();
	unlockCount = lockCount;
	while (unlockCount > 0) {
		FlightSurface_Unlock();
		--unlockCount;
	}
	FlightSurface_Lock();

	barLeft = g_screenWidth >> 2;
	barTop = g_screenHeight >> 1;
	lineHeight = g_flightFontLineHeight;
	barStep = (g_flightLoadingProgressStep & 0x7fu) + 1u;
	++g_flightLoadingProgressStep;
	barWidth = (g_screenWidth * barStep) >> 8;

	FlightText_SetClipRect((int16_t)barLeft - 2, (int16_t)barTop - 2, (int16_t)(g_screenWidth - barLeft + 2),
						   (int16_t)(barTop + lineHeight + 2));
	g_flightTextBgColor = (uint8_t)(g_flightLoadingProgressStep / 128u + 48u);
	g_flightFillClipRectFn();
	FlightText_SetClipRect((int16_t)barLeft - 1, (int16_t)barTop - 1, (int16_t)(g_screenWidth - barLeft + 1),
						   (int16_t)(barTop + lineHeight + 1));
	g_flightTextBgColor = 0;
	g_flightFillClipRectFn();
	FlightText_SetClipRect((int16_t)barLeft, (int16_t)barTop, (int16_t)(barLeft + barWidth),
						   (int16_t)(barTop + lineHeight));
	g_flightTextBgColor = (uint8_t)(g_flightLoadingProgressStep / 128u + 48u);
	g_flightFillClipRectFn();

#ifdef XVT_MODERN
	XvtCockpitMessages_RecordProgress(barStep, barLeft, barTop, g_screenWidth - 2 * barLeft, lineHeight,
									  barWidth);
#endif
	FlightSurface_Unlock();
	FlightDisplay_BlitRenderSurface();
	FlightDisplay_Flip();
	while (lockCount > 0) {
		FlightSurface_Lock();
		--lockCount;
	}

	g_flightCursorX = savedCursorX;
	g_flightCursorY = savedCursorY;
	g_flightClipLeft = savedClipLeft;
	g_flightClipTop = savedClipTop;
	g_flightClipRight = savedClipRight;
	g_flightClipBottom = savedClipBottom;
	g_flightWordWrapEnabled = savedWordWrap;
	g_flightTextReservedState91079E = savedReservedState;
	g_flightTextColorIndex = savedTextColor;
	g_flightClearLineBgEnabled = savedClearLineBackground;
	g_flightTextBgColor = savedBackgroundColor;
	g_flightTextShadowColor = savedShadowColor;
	g_flightTextShadowEnabled = savedShadowEnabled;

#ifdef XVT_MODERN
	XvtRenderCapture_EndOverlay();
#endif
}

// FUNCTION: XVT 0x4493C0
void FlightLoading_DrawProgressToCompletion(void) {
	while ((g_flightLoadingProgressStep & 0x7fu) != 0x7fu)
		FlightLoading_PulseAndDrawProgressScreen();
	FlightLoading_PulseAndDrawProgressScreen();
}

// FUNCTION: XVT 0x4493E0
int PilotData_HasNetworkPlayerDpid(int dpid) {
	int playerIndex;

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		if (g_pilotData.networkPlayers[playerIndex].directPlayId != 0 &&
			g_pilotData.networkPlayers[playerIndex].directPlayId == dpid) {
			return 1;
		}
	}
	return 0;
}

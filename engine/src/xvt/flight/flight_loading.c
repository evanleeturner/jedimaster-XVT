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

/* Calls made to FlightLoading_PulseAndDrawProgressScreen since the last
 * reset; its low 7 bits are the bar's fill. Three functions write it:
 * FlightLoading_ResetProgressState sets 0 at flight start,
 * FlightLoading_PulseAndDrawProgressScreen adds 1 per call, and, in the
 * modern build, XvtFlightTask_Update sets its low 7 bits to fill the bar. */
// GLOBAL: XVT 0x5236A8
uint32_t g_flightLoadingProgressStep;
/* timeGetTime, in ms, when the loading bar was last drawn. Written only by
 * FlightLoading_ResetProgressState and
 * FlightLoading_PulseAndDrawProgressScreen. */
// GLOBAL: XVT 0x5236AC
uint32_t g_flightLoadingProgressLastDrawMs;

/* Sets g_flightLoadingProgressStep to 0 and
 * g_flightLoadingProgressLastDrawMs to now; the modern build also clears its
 * record of the bar (XvtCockpitMessages_ClearProgress). */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x449110
void FlightLoading_ResetProgressState(void)
{
#ifdef XVT_MODERN
	XvtCockpitMessages_ClearProgress();
#endif
	g_flightLoadingProgressStep = 0;
	g_flightLoadingProgressLastDrawMs = timeGetTime();
}

/* Called between loading steps: adds 1 to g_flightLoadingProgressStep and,
 * when 200 ms have passed since the last draw or the step's low 6 bits were
 * 63, draws the loading bar and shows it. On the 63 case it first sends the
 * other players a still-loading packet (FlightNet_BroadcastStillLoadingPulse).
 * The bar sits at mid-height from a quarter to three quarters of the screen
 * width, filled in 128 steps that start again from empty, its color index
 * 48 plus the step divided by 128. To draw, it unlocks the surface fully,
 * locks it once, then blits and flips (FlightDisplay_BlitRenderSurface,
 * FlightDisplay_Flip) and locks it back to the count it found. The text
 * cursor, clip rectangle and text colors are saved and put back. The modern
 * build also marks the drawing as an overlay and records the bar for its own
 * renderer. */
// FUNCTION: XVT 0x449130
void FlightLoading_PulseAndDrawProgressScreen(void)
{
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
	if (stepPhase != 63u &&
	    (int32_t)(now - g_flightLoadingProgressLastDrawMs) < 200) {
		++g_flightLoadingProgressStep;
		return;
	}

#ifdef XVT_MODERN
	XvtRenderCapture_BeginOverlay();
#endif
	g_flightLoadingProgressLastDrawMs = now;
	if (stepPhase == 63u) {
		FlightNet_BroadcastStillLoadingPulse();
	}

	savedCursorX = g_flightCursorX;
	savedCursorY = g_flightCursorY;
	savedClipLeft = g_flightClipLeft;
	savedClipTop = g_flightClipTop;
	savedClipRight = g_flightClipRight;
	savedClipBottom = g_flightClipBottom;
	savedWordWrap = g_flightWordWrapEnabled;
	savedReservedState = g_flightTextReservedState;
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

	FlightText_SetClipRect((int16_t)barLeft - 2, (int16_t)barTop - 2,
			       (int16_t)(g_screenWidth - barLeft + 2),
			       (int16_t)(barTop + lineHeight + 2));
	g_flightTextBgColor =
		(uint8_t)(g_flightLoadingProgressStep / 128u + 48u);
	g_flightFillClipRectFn();
	FlightText_SetClipRect((int16_t)barLeft - 1, (int16_t)barTop - 1,
			       (int16_t)(g_screenWidth - barLeft + 1),
			       (int16_t)(barTop + lineHeight + 1));
	g_flightTextBgColor = 0;
	g_flightFillClipRectFn();
	FlightText_SetClipRect((int16_t)barLeft, (int16_t)barTop,
			       (int16_t)(barLeft + barWidth),
			       (int16_t)(barTop + lineHeight));
	g_flightTextBgColor =
		(uint8_t)(g_flightLoadingProgressStep / 128u + 48u);
	g_flightFillClipRectFn();

#ifdef XVT_MODERN
	XvtCockpitMessages_RecordProgress(barStep, barLeft, barTop,
					  g_screenWidth - 2 * barLeft,
					  lineHeight, barWidth);
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
	g_flightTextReservedState = savedReservedState;
	g_flightTextColorIndex = savedTextColor;
	g_flightClearLineBgEnabled = savedClearLineBackground;
	g_flightTextBgColor = savedBackgroundColor;
	g_flightTextShadowColor = savedShadowColor;
	g_flightTextShadowEnabled = savedShadowEnabled;

#ifdef XVT_MODERN
	XvtRenderCapture_EndOverlay();
#endif
}

/* Pulses the loading bar until its low 7 bits reach 127, then once more,
 * which always draws it full. Only the original build calls this; the modern
 * build sets the bits itself and pulses once. */
// FUNCTION: XVT 0x4493C0
void FlightLoading_DrawProgressToCompletion(void)
{
	while ((g_flightLoadingProgressStep & 0x7fu) != 0x7fu) {
		FlightLoading_PulseAndDrawProgressScreen();
	}
	FlightLoading_PulseAndDrawProgressScreen();
}

/* Returns 1 when dpid is nonzero and matches the DirectPlay id of one of the
 * 8 entries of g_pilotData.networkPlayers, else 0. */
// FUNCTION: XVT 0x4493E0
int PilotData_HasNetworkPlayerDpid(int dpid)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		if (g_pilotData.networkPlayers[playerIndex].directPlayId != 0 &&
		    g_pilotData.networkPlayers[playerIndex].directPlayId ==
			    dpid) {
			return 1;
		}
	}
	return 0;
}

#ifndef XVT_REMASTER_HUD_INSTRUMENTS_H
#define XVT_REMASTER_HUD_INSTRUMENTS_H
#include "xvt_remaster/hud_draw.h"
#include "xvt_remaster/render_math.h"
void XvtHudInstruments_DrawCovers(const XvtHudDraw* draw);
void XvtHudInstruments_DrawWidgets(const XvtHudDraw* draw);
void XvtHudInstruments_DrawRadar(const XvtHudDraw* draw);
void XvtHudInstruments_DrawWorldMarkers(const XvtHudDraw* draw, const XvtSnapTargetBox* markers,
										unsigned count, const XvtRenderView* view);
void XvtHudInstruments_DrawCrtMarker(const XvtHudDraw* draw);
void XvtHudInstruments_DrawMouseStick(const XvtHudDraw* draw, const XvtRenderView* view);
#endif

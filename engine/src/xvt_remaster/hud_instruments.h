#ifndef XVT_REMASTER_HUD_INSTRUMENTS_H
#define XVT_REMASTER_HUD_INSTRUMENTS_H
#include "xvt_remaster/hud_draw.h"
#include "xvt_remaster/render_math.h"
/* The cockpit's artwork instruments and drawn markers: covers, weapon, shield, hull, beam and power
 * widgets, the radar, the target boxes projected from world positions, the CRT component marker and
 * the mouse stick. */

/* Draws, before the CRT, each visible feature cover (state 13 as the damaged part), the unavailable
 * shield and beam indicators, and the target panel cover (the alternate part when the cover binding is
 * element 108 of its set). */
void XvtHudInstruments_DrawCovers(const XvtHudDraw* draw);
/* Draws, before the CRT, the weapon widgets (ten charge segments per visible slot with a visible charge,
 * only in the cockpit instrument set, instrument_base 0; the selection, ready and lock parts; the target
 * lock; and the launchers, only in the cockpit instrument set), the shield gauges (primary and overcharge,
 * unless in text mode), hull, beam enabled, the nine beam segments, the four power gauges (segments
 * stepped by step_y), shield distribution, S-foils, countermeasure selection, critical warning, and the
 * threat and armament indicators. */
void XvtHudInstruments_DrawWidgets(const XvtHudDraw* draw);
/* Draws, before the CRT, each visible radar side's blips as single pixels (up to two rows by the
 * coverage bits) in their palette colors, and the marker as ten pixels around its position in palette
 * color 206. */
void XvtHudInstruments_DrawRadar(const XvtHudDraw* draw);
/* Draws the cockpit-layer markers as four corner brackets each, into the before list: a marker is
 * projected through view (skipped behind the camera), its box sized from its extent at view's focal
 * length over depth, between 4 classic pixels for a layout 320 wide (else 8) and three quarters of the
 * source width, plus four, in its palette color. Nothing without a view. */
void XvtHudInstruments_DrawWorldMarkers(const XvtHudDraw* draw, const XvtSnapTargetBox* markers,
										unsigned count, const XvtRenderView* view);
/* Draws a 4 x 4 frame in palette color 206 at the CRT preview's component marker, projected through a
 * view built from the preview's camera at its destination size, cut to the destination, after the CRT;
 * nothing without a valid preview and marker, a buildable view, or a marker in front of the camera. */
void XvtHudInstruments_DrawCrtMarker(const XvtHudDraw* draw);
/* Draws a 4-pixel square frame in palette color 63 at the mouse stick: view's projection center moved
 * by the stick's x and y (of 127) over a sixth of the viewport height, into the after list; nothing
 * without a view or with the stick hidden. */
void XvtHudInstruments_DrawMouseStick(const XvtHudDraw* draw, const XvtRenderView* view);
#endif

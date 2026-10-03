#ifndef XVT_REMASTER_HUD_PANES_H
#define XVT_REMASTER_HUD_PANES_H
#include "xvt_remaster/hud_draw.h"
/* The cockpit's text panes: the readouts by phase, the MFD pages, the message panes, and the loading and
 * alert overlays. Each returns 0 when a field or glyph fails or a page's content runs past its store. */

/* Draws the fields and numbers of phase: the fields before the clock separator (network ping and lag
 * only before the CRT), the readout numbers (speed, throttle, clock, countermeasures, target hull,
 * shields, systems, distances and order times, the proving-grounds counters and signed score, each
 * weapon slot's charge, and before the CRT each launcher's count), then the remaining fields, so
 * separators land after the numeric clears. */
int XvtHudPanes_DrawReadouts(const XvtHudDraw *draw, unsigned phase);
/* Draws every visible MFD page but the message log, after the CRT: its background and border inside its
 * placement, each row's background, its glyphs; then the launcher counts of that phase. */
int XvtHudPanes_DrawPages(const XvtHudDraw *draw);
/* Draws the message log page, each visible message pane's glyphs at its placement, and after the ready
 * pane the network ping and lag fields that belong after the CRT. */
int XvtHudPanes_DrawMessages(const XvtHudDraw *draw);
/* Draws, in the alert phase, the loading text and bar (a foreground border two pixels out, a background
 * border one out, the filled width in foreground) when visible, and the active alert: its border, five
 * row backgrounds, and its visible lines of glyphs. */
int XvtHudPanes_DrawOverlays(const XvtHudDraw *draw);
#endif

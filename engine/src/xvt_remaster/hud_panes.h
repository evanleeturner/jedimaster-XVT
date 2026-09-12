#ifndef XVT_REMASTER_HUD_PANES_H
#define XVT_REMASTER_HUD_PANES_H
#include "xvt_remaster/hud_draw.h"
int XvtHudPanes_DrawReadouts(const XvtHudDraw* draw, unsigned phase);
int XvtHudPanes_DrawPages(const XvtHudDraw* draw);
int XvtHudPanes_DrawMessages(const XvtHudDraw* draw);
int XvtHudPanes_DrawOverlays(const XvtHudDraw* draw);
#endif

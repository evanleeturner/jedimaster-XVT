#ifndef XVT_REMASTER_HUD_TEXT_H
#define XVT_REMASTER_HUD_TEXT_H
#include "xvt_remaster/hud_draw.h"
int XvtHudText_DrawField(const XvtHudDraw* draw, const XvtCockpitTextField* field);
int XvtHudText_DrawNumber(const XvtHudDraw* draw, const XvtCockpitNumber* number, int score);
#endif

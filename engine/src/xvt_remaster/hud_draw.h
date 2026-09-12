#ifndef XVT_REMASTER_HUD_DRAW_H
#define XVT_REMASTER_HUD_DRAW_H
#include "aeron/scene/draw_list2d.h"
#include "xvt_remaster/hud_assets.h"

typedef struct XvtHudDraw {
	AeronDrawList2D *before, *after;
	const XvtCockpitState* state;
	const XvtHudLayout* layout;
	const XvtHudAssetSet* assets;
	float scale, offset_x, offset_y;
	int width, height;
} XvtHudDraw;

AeronDrawList2D* XvtHudDraw_SelectList(const XvtHudDraw* draw, unsigned phase);
int XvtHudDraw_Clip(const XvtHudDraw* draw, XvtSnapRect rect, AeronRectI* out);
void XvtHudDraw_Fill(const XvtHudDraw* draw, unsigned phase, XvtSnapRect rect, uint32_t color);
void XvtHudDraw_TextFill(const XvtHudDraw* draw, const XvtFontAtlas* font, unsigned phase, XvtSnapRect rect,
						 uint32_t color);
void XvtHudDraw_Frame(const XvtHudDraw* draw, unsigned phase, XvtSnapRect rect, uint32_t color);
void XvtHudDraw_FrameClipped(const XvtHudDraw* draw, unsigned phase, XvtSnapRect rect, XvtSnapRect clip,
							 uint32_t color);
void XvtHudDraw_Part(const XvtHudDraw* draw, XvtHudSpriteRole role, unsigned state, int offset_x,
					 int offset_y, unsigned phase);
void XvtHudDraw_Base(const XvtHudDraw* draw);
int XvtHudDraw_Glyph(const XvtHudDraw* draw, const XvtCockpitGlyph* glyph, int origin_x, int origin_y,
					 XvtSnapRect pane_clip, unsigned phase);
#endif

#ifndef XVT_REMASTER_HUD_DRAW_H
#define XVT_REMASTER_HUD_DRAW_H
#include "aeron/scene/draw_list2d.h"
#include "xvt_remaster/hud_assets.h"

/* Primitives for the HUD draw lists in classic cockpit coordinates: fills, frames, atlas parts and glyphs,
 * each scaled and offset by the draw context into the target, appended to the before-CRT or after-CRT
 * list by phase. */

/* before, after: the lists for phases up to XVT_COCKPIT_BEFORE_CRT and for the later ones. state: the
 * cockpit state drawn. layout: the compiled layout. assets: the selected artwork. scale, offset_x,
 * offset_y: the fit of the layout's source size into width x height. */
typedef struct XvtHudDraw {
	AeronDrawList2D *before, *after;
	const XvtCockpitState* state;
	const XvtHudLayout* layout;
	const XvtHudAssetSet* assets;
	float scale, offset_x, offset_y;
	int width, height;
} XvtHudDraw;

/* The before list for phases up to XVT_COCKPIT_BEFORE_CRT, else the after list. */
AeronDrawList2D* XvtHudDraw_SelectList(const XvtHudDraw* draw, unsigned phase);
/* Maps rect into the target, rounded outward to whole pixels and cut to the target. Returns 0 with out
 * untouched for a rect without positive size; otherwise writes out and returns 1 only when it has
 * area. */
int XvtHudDraw_Clip(const XvtHudDraw* draw, XvtSnapRect rect, AeronRectI* out);
/* Adds a premultiplied fill of rect in ARGB color to the phase's list; nothing for a transparent color
 * (alpha 0) or a rect that clips to nothing. */
void XvtHudDraw_Fill(const XvtHudDraw* draw, unsigned phase, XvtSnapRect rect, uint32_t color);
/* Fill drawn as a sprite of font's opaque texel, so it batches with the font's glyphs. */
void XvtHudDraw_TextFill(const XvtHudDraw* draw, const XvtFontAtlas* font, unsigned phase, XvtSnapRect rect,
						 uint32_t color);
/* FrameClipped with the layout's whole source size as the clip. */
void XvtHudDraw_Outline(const XvtHudDraw* draw, unsigned phase, XvtSnapRect rect, uint32_t color);
/* A one-pixel outline of rect, each edge cut to clip before it is filled; nothing for a rect without
 * positive size. */
void XvtHudDraw_OutlineClipped(const XvtHudDraw* draw, unsigned phase, XvtSnapRect rect, XvtSnapRect clip,
							   uint32_t color);
/* Draws sprite role's part number state at the binding's position plus the offsets (mirrored, and
 * anchored at its right edge, when the binding is), a monochrome part tinted with the palette color of
 * its color index, clipped to the source size; nothing when state is past the binding's part count. */
void XvtHudDraw_Part(const XvtHudDraw* draw, XvtHudSpriteRole role, unsigned state, int offset_x,
					 int offset_y, unsigned phase);
/* Draws the base artwork's covered rectangles, mirrored when the layout is, before the CRT; nothing
 * without a base atlas. */
void XvtHudDraw_Base(const XvtHudDraw* draw);
/* Draws one cockpit glyph at its position plus the origin: the background over its bounds (its clip
 * moved by the origin, cut to pane_clip and to its advance, plus one with a shadow, by its height),
 * the shadow one classic pixel down and right when enabled, then the foreground, each cut to those
 * bounds. Returns 0 when the glyph's font is not loaded or its character is outside the font; else 1,
 * also when the bounds clip to nothing. */
int XvtHudDraw_Glyph(const XvtHudDraw* draw, const XvtCockpitGlyph* glyph, int origin_x, int origin_y,
					 XvtSnapRect pane_clip, unsigned phase);
#endif

#ifndef XVT_REMASTER_HUD_DRAW_H
#define XVT_REMASTER_HUD_DRAW_H
#include "aeron/scene/draw_list2d.h"
#include "xvt_remaster/hud_assets.h"

/* Primitives for the HUD draw lists in classic cockpit coordinates: fills,
 * outlines, atlas parts and glyphs, each scaled and offset by the draw context
 * into the target, appended to the before-CRT or after-CRT list by phase. */

/* before, after: the lists for phases up to XVT_COCKPIT_BEFORE_CRT and for the
 * later ones. state: the cockpit state drawn. layout: the compiled layout.
 * assets: the selected artwork. scale, offset_x, offset_y: the fit of the
 * layout's source size into width x height. */
struct xvt_hud_draw {
	AeronDrawList2D *before;
	AeronDrawList2D *after;
	const struct xvt_cockpit_state *state;
	const struct xvt_hud_layout *layout;
	const struct xvt_hud_asset_set *assets;
	float scale;
	float offset_x;
	float offset_y;
	int width;
	int height;
};

/* The before list for phases up to XVT_COCKPIT_BEFORE_CRT, else the after list. */
AeronDrawList2D *xvt_hud_draw_select_list(const struct xvt_hud_draw *draw,
					  unsigned phase);
/* Maps rect into the target, rounded outward to whole pixels and cut to the
 * target. Returns 0 with out untouched for a rect without positive size;
 * otherwise writes out and returns 1 only when it has area. */
int xvt_hud_draw_clip(const struct xvt_hud_draw *draw,
		      struct xvt_snap_rect rect, AeronRectI *out);
/* Adds a premultiplied fill of rect in ARGB color to the phase's list; nothing
 * for a transparent color (alpha 0) or a rect that clips to nothing. */
void xvt_hud_draw_fill(const struct xvt_hud_draw *draw, unsigned phase,
		       struct xvt_snap_rect rect, uint32_t color);
/* Fill drawn as a sprite of font's opaque texel, so it batches with the font's glyphs. */
void xvt_hud_draw_text_fill(const struct xvt_hud_draw *draw,
			    const struct xvt_font_atlas *font, unsigned phase,
			    struct xvt_snap_rect rect, uint32_t color);
/* OutlineClipped with the layout's whole source size as the clip. */
void xvt_hud_draw_outline(const struct xvt_hud_draw *draw, unsigned phase,
			  struct xvt_snap_rect rect, uint32_t color);
/* A one-pixel outline of rect, each edge cut to clip before it is filled;
 * nothing for a rect without positive size. */
void xvt_hud_draw_outline_clipped(const struct xvt_hud_draw *draw,
				  unsigned phase, struct xvt_snap_rect rect,
				  struct xvt_snap_rect clip, uint32_t color);
/* Draws sprite role's part number state at the binding's position plus the
 * offsets (mirrored, and anchored at its right edge, when the binding is), a
 * monochrome part tinted with the palette color of its color index, clipped to
 * the source size; nothing when state is past the binding's part count. */
void xvt_hud_draw_part(const struct xvt_hud_draw *draw,
		       xvt_hud_sprite_role role, unsigned state, int offset_x,
		       int offset_y, unsigned phase);
/* Draws the base artwork's covered rectangles, mirrored when the layout is, before the CRT; nothing
 * without a base atlas. */
void xvt_hud_draw_base(const struct xvt_hud_draw *draw);
/* Draws one cockpit glyph at its position plus the origin: the background over
 * its bounds (its clip moved by the origin, cut to pane_clip and to its
 * advance, plus one with a shadow, by its height), the shadow one classic pixel
 * down and right when enabled, then the foreground, each cut to those bounds.
 * Returns 0 when the glyph's font is not loaded or its character is outside the
 * font; else 1, also when the bounds clip to nothing. */
int xvt_hud_draw_glyph(const struct xvt_hud_draw *draw,
		       const struct xvt_cockpit_glyph *glyph, int origin_x,
		       int origin_y, struct xvt_snap_rect pane_clip,
		       unsigned phase);
#endif

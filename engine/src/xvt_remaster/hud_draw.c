#include "xvt_remaster/hud_draw.h"
#include "xvt_remaster/ui_draw.h"
#include <math.h>

AeronDrawList2D *xvt_hud_draw_select_list(const struct xvt_hud_draw *draw,
					  unsigned phase)
{
	return phase <= XVT_COCKPIT_BEFORE_CRT ? draw->before : draw->after;
}

static struct xvt_snap_rect intersect_rects(struct xvt_snap_rect a,
					    struct xvt_snap_rect b)
{
	int right =
		a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
	int bottom = a.y + a.height < b.y + b.height ? a.y + a.height
						     : b.y + b.height;
	a.x = a.x > b.x ? a.x : b.x;
	a.y = a.y > b.y ? a.y : b.y;
	a.width = right > a.x ? right - a.x : 0;
	a.height = bottom > a.y ? bottom - a.y : 0;
	return a;
}

int xvt_hud_draw_clip(const struct xvt_hud_draw *draw,
		      struct xvt_snap_rect rect, AeronRectI *out)
{
	if (rect.width <= 0 || rect.height <= 0) {
		return 0;
	}
	int left = (int)floorf(rect.x * draw->scale + draw->offset_x);
	int top = (int)floorf(rect.y * draw->scale + draw->offset_y);
	int right = (int)ceilf((rect.x + rect.width) * draw->scale +
			       draw->offset_x);
	int bottom = (int)ceilf((rect.y + rect.height) * draw->scale +
				draw->offset_y);
	left = left > 0 ? left : 0;
	top = top > 0 ? top : 0;
	right = right < draw->width ? right : draw->width;
	bottom = bottom < draw->height ? bottom : draw->height;
	*out = (AeronRectI){left, top, right - left, bottom - top};
	return right > left && bottom > top;
}

void xvt_hud_draw_fill(const struct xvt_hud_draw *draw, unsigned phase,
		       struct xvt_snap_rect rect, uint32_t color)
{
	AeronRectI clip;
	if (!(color >> 24) || !xvt_hud_draw_clip(draw, rect, &clip)) {
		return;
	}
	float rgba[4];
	xvt_ui_color(color, rgba);
	AeronDrawList_AddFill(
		xvt_hud_draw_select_list(draw, phase),
		rect.x * draw->scale + draw->offset_x,
		rect.y * draw->scale + draw->offset_y, rect.width * draw->scale,
		rect.height * draw->scale, rgba, AERON_BLIT2D_BLEND_PMA, NULL);
}

void xvt_hud_draw_text_fill(const struct xvt_hud_draw *draw,
			    const struct xvt_font_atlas *font, unsigned phase,
			    struct xvt_snap_rect rect, uint32_t color)
{
	AeronRectI clip;
	if (!(color >> 24) || !xvt_hud_draw_clip(draw, rect, &clip)) {
		return;
	}
	AeronDrawList2DSprite sprite = {
		.texture = font->atlas.texture,
		.src_u0 = font->white_uv[0],
		.src_v0 = font->white_uv[1],
		.src_u1 = font->white_uv[0],
		.src_v1 = font->white_uv[1],
		.dst_x = rect.x * draw->scale + draw->offset_x,
		.dst_y = rect.y * draw->scale + draw->offset_y,
		.dst_w = rect.width * draw->scale,
		.dst_h = rect.height * draw->scale,
		.blend = AERON_BLIT2D_BLEND_PMA,
		.filter = AERON_BLIT2D_FILTER_LINEAR};
	xvt_ui_color(color, sprite.tint);
	AeronDrawList_AddSprite(xvt_hud_draw_select_list(draw, phase), &sprite);
}

void xvt_hud_draw_outline(const struct xvt_hud_draw *draw, unsigned phase,
			  struct xvt_snap_rect rect, uint32_t color)
{
	xvt_hud_draw_outline_clipped(
		draw, phase, rect,
		(struct xvt_snap_rect){0, 0, draw->layout->source_width,
				       draw->layout->source_height},
		color);
}

void xvt_hud_draw_outline_clipped(const struct xvt_hud_draw *draw,
				  unsigned phase, struct xvt_snap_rect rect,
				  struct xvt_snap_rect clip, uint32_t color)
{
	if (rect.width <= 0 || rect.height <= 0) {
		return;
	}
	struct xvt_snap_rect edges[] = {
		{rect.x, rect.y, rect.width, 1},
		{rect.x, rect.y + rect.height - 1, rect.width, 1},
		{rect.x, rect.y, 1, rect.height},
		{rect.x + rect.width - 1, rect.y, 1, rect.height}};
	for (unsigned edge = 0; edge < 4; ++edge) {
		xvt_hud_draw_fill(draw, phase,
				  intersect_rects(edges[edge], clip), color);
	}
}

static void append_atlas_sprite(const struct xvt_hud_draw *draw,
				const AeronRuntimeAtlas *atlas, unsigned frame,
				float x, float y, float sx, float sy,
				float width, float height, int mirrored,
				int monochrome, uint32_t color, unsigned phase)
{
	const AeronSpriteRect *source = &atlas->layout.frames[frame];
	const AeronRuntimeAtlasPage *page =
		&atlas->pages[atlas->layout.pages[frame]];
	AeronDrawList2DSprite sprite = {
		.texture = page->texture,
		.src_u0 = (source->x + sx) / page->width,
		.src_v0 = (source->y + sy) / page->height,
		.src_u1 = (source->x + sx + width) / page->width,
		.src_v1 = (source->y + sy + height) / page->height,
		.dst_x = x * draw->scale + draw->offset_x,
		.dst_y = y * draw->scale + draw->offset_y,
		.dst_w = width * draw->scale,
		.dst_h = height * draw->scale,
		.tint = {1, 1, 1, 1},
		.blend = AERON_BLIT2D_BLEND_PMA,
		.filter = AERON_BLIT2D_FILTER_LINEAR};
	if (mirrored) {
		float swap = sprite.src_u0;
		sprite.src_u0 = sprite.src_u1;
		sprite.src_u1 = swap;
	}
	if (monochrome) {
		sprite.tint[0] = sprite.tint[1] = sprite.tint[2] = 0;
		xvt_ui_color(color, sprite.bias);
		sprite.bias[3] = 0;
	}
	if (!xvt_hud_draw_clip(
		    draw,
		    (struct xvt_snap_rect){0, 0, draw->layout->source_width,
					   draw->layout->source_height},
		    &sprite.scissor)) {
		return;
	}
	AeronDrawList_AddSprite(xvt_hud_draw_select_list(draw, phase), &sprite);
}

void xvt_hud_draw_part(const struct xvt_hud_draw *draw,
		       xvt_hud_sprite_role role, unsigned state, int offset_x,
		       int offset_y, unsigned phase)
{
	const struct xvt_hud_sprite_binding *binding =
		&draw->layout->sprites[role];
	if (state >= binding->part_count) {
		return;
	}
	const struct xvt_hud_prepared_part *part =
		&draw->assets->bindings[binding->first_part + state];
	const AeronRuntimeAtlas *atlas = &draw->assets->parts;
	float width = atlas->layout.classic_w[part->atlas_frame],
	      height = atlas->layout.classic_h[part->atlas_frame];
	append_atlas_sprite(draw, atlas, part->atlas_frame,
			    binding->x + offset_x -
				    (binding->mirrored ? width - 1 : 0),
			    binding->y + offset_y, 0, 0, width, height,
			    binding->mirrored, part->monochrome,
			    draw->state->palette_argb[part->color], phase);
}

void xvt_hud_draw_base(const struct xvt_hud_draw *draw)
{
	const AeronRuntimeAtlas *atlas = &draw->assets->base;
	if (!atlas->layout.frame_count) {
		return;
	}
	for (unsigned index = 0; index < draw->assets->base_coverage.count;
	     ++index) {
		const AeronImageCoverageRect *rect =
			&draw->assets->base_coverage.rects[index];
		int x = draw->layout->mirrored ? draw->layout->source_width -
							 rect->x - rect->width
					       : rect->x;
		append_atlas_sprite(draw, atlas, 0, x, rect->y, rect->x,
				    rect->y, rect->width, rect->height,
				    draw->layout->mirrored, 0, 0,
				    XVT_COCKPIT_BEFORE_CRT);
	}
}

static void append_glyph_plane(const struct xvt_hud_draw *draw,
			       const struct xvt_font_atlas *font,
			       unsigned glyph_index, float x, float y,
			       uint32_t color, const AeronRectI *clip,
			       unsigned phase)
{
	if (!(color >> 24)) {
		return;
	}
	const AeronFontGlyph *glyph = &font->atlas.glyphs[glyph_index];
	const struct xvt_font_glyph *metrics = &font->glyphs[glyph_index];
	AeronDrawList2DSprite sprite = {
		.texture = font->atlas.texture,
		.src_u0 = (float)glyph->atlas_x / font->atlas.atlas_w,
		.src_v0 = (float)glyph->atlas_y / font->atlas.atlas_h,
		.src_u1 = (float)(glyph->atlas_x + glyph->atlas_w) /
			  font->atlas.atlas_w,
		.src_v1 = (float)(glyph->atlas_y + glyph->atlas_h) /
			  font->atlas.atlas_h,
		.dst_x = x * draw->scale + draw->offset_x,
		.dst_y = y * draw->scale + draw->offset_y,
		.dst_w = metrics->width * draw->scale,
		.dst_h = metrics->height * draw->scale,
		.blend = AERON_BLIT2D_BLEND_PMA,
		.filter = AERON_BLIT2D_FILTER_LINEAR};
	/* Preserve the integer scissor boundaries in geometry so adjacent glyphs
	 * can share a batch without changing their background/shadow order. */
	float left = fmaxf(sprite.dst_x, clip->x);
	float top = fmaxf(sprite.dst_y, clip->y);
	float right = fminf(sprite.dst_x + sprite.dst_w, clip->x + clip->width);
	float bottom =
		fminf(sprite.dst_y + sprite.dst_h, clip->y + clip->height);
	if (right <= left || bottom <= top) {
		return;
	}
	float du = (sprite.src_u1 - sprite.src_u0) / sprite.dst_w;
	float dv = (sprite.src_v1 - sprite.src_v0) / sprite.dst_h;
	sprite.src_u1 = sprite.src_u0 + (right - sprite.dst_x) * du;
	sprite.src_v1 = sprite.src_v0 + (bottom - sprite.dst_y) * dv;
	sprite.src_u0 += (left - sprite.dst_x) * du;
	sprite.src_v0 += (top - sprite.dst_y) * dv;
	sprite.dst_x = left;
	sprite.dst_y = top;
	sprite.dst_w = right - left;
	sprite.dst_h = bottom - top;
	xvt_ui_color(color, sprite.tint);
	AeronDrawList_AddSprite(xvt_hud_draw_select_list(draw, phase), &sprite);
}

int xvt_hud_draw_glyph(const struct xvt_hud_draw *draw,
		       const struct xvt_cockpit_glyph *glyph, int origin_x,
		       int origin_y, struct xvt_snap_rect pane_clip,
		       unsigned phase)
{
	const struct xvt_font_atlas *font =
		xvt_hud_assets_find_font(glyph->font_asset_id);
	if (!font || glyph->character < font->atlas.first_char ||
	    glyph->character >=
		    font->atlas.first_char + font->atlas.num_chars) {
		return 0;
	}
	unsigned glyph_index = glyph->character - font->atlas.first_char;
	int x = glyph->x + origin_x, y = glyph->y + origin_y;
	struct xvt_snap_rect bounds = glyph->clip;
	bounds.x += origin_x;
	bounds.y += origin_y;
	bounds = intersect_rects(bounds, pane_clip);
	bounds = intersect_rects(
		bounds, (struct xvt_snap_rect){
				x, y, glyph->advance + !!glyph->shadow_enabled,
				glyph->height});
	AeronRectI clip;
	if (!xvt_hud_draw_clip(draw, bounds, &clip)) {
		return 1;
	}
	xvt_hud_draw_text_fill(draw, font, phase, bounds,
			       glyph->background_argb);
	if (glyph->shadow_enabled) {
		append_glyph_plane(draw, font, glyph_index, x + 1, y + 1,
				   glyph->shadow_argb, &clip, phase);
	}
	append_glyph_plane(draw, font, glyph_index, x, y,
			   glyph->foreground_argb, &clip, phase);
	return 1;
}

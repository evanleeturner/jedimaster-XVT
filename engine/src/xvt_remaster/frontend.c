#include "xvt_remaster/frontend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xvt_remaster/preview.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/input_bridge.h"
#include "xvt_runtime/runtime/presentation.h"

enum { TARGETS = XVT_TARGET_FRONT_SAVED_FIRST + XVT_TARGET_FRONT_SAVED_COUNT };

static AeronRenderTarget *g_targets[TARGETS];
static AeronDrawList2D *g_list;
static int g_width;
static int g_height;
static int g_presented;
static int g_movie_presented;
static float g_scale;
static int g_release_presented;
static uint64_t g_replayed_snapshot_serial = UINT64_MAX;

static struct xvt_snap_rect g_saved_bounds[TARGETS];
static AeronRenderTarget *g_cursor_target;
static struct xvt_snap_sprite g_cursor;
static int g_cursor_visible;

static int is_saved_target(unsigned id)
{
	return id >= XVT_TARGET_FRONT_SAVED_FIRST && id < TARGETS;
}

static int is_frontend_target(unsigned id)
{
	return id <= XVT_TARGET_FRONT_BACKUP ||
	       id == XVT_TARGET_FRONT_PRESENTED ||
	       id == XVT_TARGET_FRONT_MOVIE || is_saved_target(id);
}

static struct xvt_snap_rect scale_rect(struct xvt_snap_rect rect, float scale)
{
	int left = (int)roundf(rect.x * scale);
	int top = (int)roundf(rect.y * scale);
	return (struct xvt_snap_rect){
		left, top, (int)roundf((rect.x + rect.width) * scale) - left,
		(int)roundf((rect.y + rect.height) * scale) - top};
}

static AeronRenderTarget *create_target(unsigned id, int width, int height)
{
	const char *name;
	char saved_name[48];
	switch (id) {
	case XVT_TARGET_FRONT_BACK:
		name = "xvt.frontend.back";
		break;
	case XVT_TARGET_FRONT_OFFSCREEN:
		name = "xvt.frontend.offscreen";
		break;
	case XVT_TARGET_FRONT_BACKUP:
		name = "xvt.frontend.backup";
		break;
	case XVT_TARGET_FRONT_PRESENTED:
		name = "xvt.frontend.presented";
		break;
	case XVT_TARGET_FRONT_MOVIE:
		name = "xvt.frontend.movie";
		break;
	default:
		snprintf(saved_name, sizeof saved_name, "xvt.frontend.saved.%u",
			 id - XVT_TARGET_FRONT_SAVED_FIRST);
		name = saved_name;
		break;
	}
	return Aeron_CreateRenderTarget(&(AeronRenderTargetDesc){
		.width = width,
		.height = height,
		.format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
		.debug_name = name});
}

static int clear_target(AeronCommandBuffer *cmd, unsigned id, uint32_t color)
{
	float rgba[4];
	xvt_ui_color(color, rgba);
	AeronTexture *texture = Aeron_RenderTargetGetTexture(g_targets[id]);
	AeronDrawList_Begin(
		g_list, g_targets[id], Aeron_TextureGetWidth(texture),
		Aeron_TextureGetHeight(texture), AERON_DRAWLIST2D_CLEAR, rgba);
	AeronDrawList_Render(g_list, cmd);
	return 1;
}

static int prepare_target(AeronCommandBuffer *cmd, unsigned id)
{
	if (!is_frontend_target(id)) {
		return 0;
	}
	if (g_targets[id]) {
		return 1;
	}
	struct xvt_snap_rect bounds =
		is_saved_target(id)
			? scale_rect(g_saved_bounds[id], g_scale)
			: (struct xvt_snap_rect){0, 0, g_width, g_height};
	if (bounds.width <= 0 || bounds.height <= 0) {
		return 0;
	}
	g_targets[id] = create_target(id, bounds.width, bounds.height);
	return g_targets[id] &&
	       (is_saved_target(id) ||
		clear_target(cmd, id,
			     id == XVT_TARGET_FRONT_MOVIE ? 0 : 0xff000000u));
}

static int resize_targets(AeronCommandBuffer *cmd, int width, int height,
			  float scale)
{
	AeronRenderTarget *replacements[TARGETS] = {0};
	for (unsigned id = 0; id < TARGETS; ++id) {
		if (!g_targets[id]) {
			continue;
		}
		struct xvt_snap_rect bounds =
			is_saved_target(id)
				? scale_rect(g_saved_bounds[id], scale)
				: (struct xvt_snap_rect){0, 0, width, height};
		replacements[id] =
			create_target(id, bounds.width, bounds.height);
		if (!replacements[id]) {
			goto failed;
		}
		AeronTexture *source =
			Aeron_RenderTargetGetTexture(g_targets[id]);
		int source_width = Aeron_TextureGetWidth(source);
		int source_height = Aeron_TextureGetHeight(source);
		if (!xvt_ui_copy_frontend(
			    cmd, replacements[id], source,
			    &(struct xvt_snap_rect){0, 0, source_width,
						    source_height},
			    &(struct xvt_snap_rect){0, 0, bounds.width,
						    bounds.height},
			    source_width, source_height)) {
			goto failed;
		}
	}
	for (unsigned id = 0; id < TARGETS; ++id) {
		Aeron_DestroyRenderTarget(g_targets[id]);
		g_targets[id] = replacements[id];
	}
	return 1;
failed:
	for (unsigned id = 0; id < TARGETS; ++id) {
		Aeron_DestroyRenderTarget(replacements[id]);
	}
	return 0;
}

static int prepare_targets(AeronCommandBuffer *cmd, int width, int height)
{
	if (!g_list) {
		g_list = AeronDrawList_Create(65536);
	}
	if (!g_list) {
		return 0;
	}
	float scale = fminf(width / 640.0f, height / 480.0f);
	int w = (int)ceilf(640 * scale);
	int h = (int)ceilf(480 * scale);
	if ((w != g_width || h != g_height) &&
	    !resize_targets(cmd, w, h, scale)) {
		return 0;
	}
	g_scale = scale;
	g_width = w;
	g_height = h;
	return prepare_target(cmd, XVT_TARGET_FRONT_BACK) &&
	       prepare_target(cmd, XVT_TARGET_FRONT_PRESENTED);
}

static int copy_region(AeronCommandBuffer *cmd, unsigned src, unsigned dst,
		       struct xvt_snap_rect from, struct xvt_snap_rect to)
{
	/* Saved slots use local texture coordinates; captured rectangles stay in screen space. */
	if (is_saved_target(src) && !g_targets[src]) {
		return 0;
	}
	if (is_saved_target(dst)) {
		Aeron_DestroyRenderTarget(g_targets[dst]);
		g_targets[dst] = NULL;
		g_saved_bounds[dst] = to;
	}
	if (!prepare_target(cmd, src) || !prepare_target(cmd, dst)) {
		return 0;
	}
	from = scale_rect(from, g_scale);
	to = scale_rect(to, g_scale);
	if (is_saved_target(src)) {
		struct xvt_snap_rect origin =
			scale_rect(g_saved_bounds[src], g_scale);
		from.x -= origin.x;
		from.y -= origin.y;
	}
	if (is_saved_target(dst)) {
		struct xvt_snap_rect origin =
			scale_rect(g_saved_bounds[dst], g_scale);
		to.x -= origin.x;
		to.y -= origin.y;
	}
	AeronTexture *source = Aeron_RenderTargetGetTexture(g_targets[src]);
	int ok = xvt_ui_copy_frontend(cmd, g_targets[dst], source, &from, &to,
				      Aeron_TextureGetWidth(source),
				      Aeron_TextureGetHeight(source));
	if (ok && is_saved_target(src)) {
		Aeron_DestroyRenderTarget(g_targets[src]);
		g_targets[src] = NULL;
		memset(&g_saved_bounds[src], 0, sizeof g_saved_bounds[src]);
	}
	return ok;
}

static int draw_sprite(const struct xvt_snap_sprite *b, float scale)
{
	const AeronRuntimeAtlas *a = xvt_remaster_assets_find_frontend_image(b);
	if (!a) {
		return 0;
	}
	for (int i = 0; i < a->layout.frame_count; ++i) {
		if ((unsigned)a->layout.ids[i] == b->frame) {
			const AeronRuntimeAtlasPage *p =
				&a->pages[a->layout.pages[i]];
			const AeronSpriteRect *r = &a->layout.frames[i];
			float sx = r->w / a->layout.classic_w[i];
			float sy = r->h / a->layout.classic_h[i];
			AeronDrawList2DSprite d = {
				.texture = p->texture,
				.src_u0 = (r->x + b->source.x * sx) / p->width,
				.src_v0 = (r->y + b->source.y * sy) / p->height,
				.src_u1 =
					(r->x +
					 (b->source.x + b->source.width) * sx) /
					p->width,
				.src_v1 = (r->y +
					   (b->source.y + b->source.height) *
						   sy) /
					  p->height,
				.dst_x = b->destination.x * scale,
				.dst_y = b->destination.y * scale,
				.dst_w = b->destination.width * scale,
				.dst_h = b->destination.height * scale,
				.blend = AERON_BLIT2D_BLEND_PMA,
				.filter = AERON_BLIT2D_FILTER_NEAREST};
			struct xvt_snap_rect clip =
				scale_rect(b->draw.clip, scale);
			d.scissor = (AeronRectI){clip.x, clip.y, clip.width,
						 clip.height};
			xvt_ui_color(b->kind == XVT_SPRITE_FRONT_TRANSLUCENT
					     ? 0x80ffffffu
					     : 0xffffffffu,
				     d.tint);
			AeronDrawList_AddSprite(g_list, &d);
			return 1;
		}
	}
	return 0;
}

static void draw_preview(unsigned i)
{
	const struct xvt_preview_output *p = xvt_remaster_preview_output(i);
	if (!p) {
		return;
	}
	struct xvt_snap_rect r = scale_rect(p->destination, g_scale);
	struct xvt_snap_rect clip = scale_rect(p->draw.clip, g_scale);
	AeronDrawList2DSprite d = {
		.texture = p->texture,
		.src_u1 = 1,
		.src_v1 = 1,
		.dst_x = r.x,
		.dst_y = r.y,
		.dst_w = r.width,
		.dst_h = r.height,
		.tint = {1, 1, 1, 1},
		.blend = AERON_BLIT2D_BLEND_PMA,
		.filter = AERON_BLIT2D_FILTER_LINEAR,
		.scissor = {clip.x, clip.y, clip.width, clip.height}};
	AeronDrawList_AddSprite(g_list, &d);
}

static int render_cursor(AeronCommandBuffer *cmd,
			 const struct xvt_snap_sprite *cursor)
{
	g_cursor_visible = 0;
	if (!cursor) {
		return 1;
	}
	int width = cursor->destination.width;
	int height = cursor->destination.height;
	if (width <= 0 || height <= 0) {
		return 0;
	}
	AeronTexture *texture =
		g_cursor_target ? Aeron_RenderTargetGetTexture(g_cursor_target)
				: NULL;
	if (!texture || Aeron_TextureGetWidth(texture) != width ||
	    Aeron_TextureGetHeight(texture) != height) {
		Aeron_DestroyRenderTarget(g_cursor_target);
		g_cursor_target =
			Aeron_CreateRenderTarget(&(AeronRenderTargetDesc){
				.width = width,
				.height = height,
				.format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
				.debug_name = "xvt.frontend.cursor"});
		if (!g_cursor_target) {
			return 0;
		}
	}
	/* Own the pixels with the held presentation, independently of source asset lifetime. */
	struct xvt_snap_sprite local = *cursor;
	local.draw.clip = (struct xvt_snap_rect){0, 0, width, height};
	local.destination = local.draw.clip;
	const float clear[4] = {0, 0, 0, 0};
	AeronDrawList_Begin(g_list, g_cursor_target, width, height,
			    AERON_DRAWLIST2D_CLEAR, clear);
	if (!draw_sprite(&local, 1)) {
		return 0;
	}
	AeronDrawList_Render(g_list, cmd);
	g_cursor = *cursor;
	g_cursor_visible = 1;
	return 1;
}

static int apply_surface_event(AeronCommandBuffer *cmd,
			       const struct xvt_snap_surface_event *e,
			       const struct xvt_snap_sprite *cursor)
{
	if (!is_frontend_target(e->target)) {
		return 1;
	}
	if (e->kind == XVT_SURFACE_PRESENT) {
		if (e->target == XVT_TARGET_FRONT_MOVIE) {
			g_movie_presented = 1;
			return 1;
		}
		AeronDrawList_Begin(
			g_list, g_targets[XVT_TARGET_FRONT_PRESENTED], g_width,
			g_height, AERON_DRAWLIST2D_CLEAR, NULL);
		AeronDrawList2DSprite background = {
			.texture = Aeron_RenderTargetGetTexture(
				g_targets[XVT_TARGET_FRONT_BACK]),
			.src_u1 = 1,
			.src_v1 = 1,
			.dst_w = g_width,
			.dst_h = g_height,
			.tint = {1, 1, 1, 1},
			.blend = AERON_BLIT2D_BLEND_NONE,
			.filter = AERON_BLIT2D_FILTER_NEAREST};
		AeronDrawList_AddSprite(g_list, &background);
		AeronDrawList_Render(g_list, cmd);
		if (!render_cursor(cmd, cursor)) {
			return 0;
		}
		g_presented = 1;
		g_release_presented = 0;
	} else if (e->kind == XVT_SURFACE_RESET) {
		/* Recreated classic surfaces repaint through subsequent events. The held
		 * presentation and independent screen-stack copies survive the reset. */
		for (unsigned i = 0; i < TARGETS; ++i) {
			if (g_targets[i] && i != XVT_TARGET_FRONT_PRESENTED &&
			    !is_saved_target(i) &&
			    !clear_target(cmd, i,
					  i == XVT_TARGET_FRONT_MOVIE
						  ? 0
						  : 0xff000000u)) {
				return 0;
			}
		}
	} else if (!prepare_target(cmd, e->target) ||
		   !clear_target(cmd, e->target, e->color_argb)) {
		return 0;
	}
	return 1;
}

int xvt_frontend_assets_need_preparation(
	const struct xvt_render_snapshot *snapshot)
{
	for (unsigned i = 0; i < snapshot->sprite_count; ++i) {
		const struct xvt_snap_sprite *sprite = &snapshot->sprites[i];
		if (sprite->draw.scope != XVT_SCOPE_FRONTEND) {
			continue;
		}
		if (!xvt_remaster_assets_find_frontend_image(sprite)) {
			return 1;
		}
	}
	return 0;
}

int xvt_frontend_prepare_assets(AeronCommandBuffer *cmd,
				const struct xvt_render_snapshot *snapshot)
{
	for (unsigned i = 0; i < snapshot->sprite_count; ++i) {
		const struct xvt_snap_sprite *sprite = &snapshot->sprites[i];
		if (sprite->draw.scope != XVT_SCOPE_FRONTEND) {
			continue;
		}
		if (!xvt_remaster_assets_prepare_frontend_image(cmd, sprite)) {
			Aeron_CommandBufferSetFailure(
				cmd, "frontend image preparation");
			return 0;
		}
	}
	return 1;
}

int xvt_frontend_needs_replay(const struct xvt_render_snapshot *s, int w, int h)
{
	if (g_list && s->presented_target != XVT_TARGET_FLIGHT_MAIN &&
	    ((int)ceilf(640 * fminf(w / 640.0f, h / 480.0f)) != g_width ||
	     (int)ceilf(480 * fminf(w / 640.0f, h / 480.0f)) != g_height)) {
		return 1;
	}
	if (s->snapshot_serial == g_replayed_snapshot_serial) {
		return 0;
	}
	for (unsigned i = 0; i < s->sprite_count; ++i) {
		if (s->sprites[i].draw.scope == XVT_SCOPE_FRONTEND) {
			return 1;
		}
	}
	for (unsigned i = 0; i < s->glyph_count; ++i) {
		if (s->glyphs[i].draw.scope == XVT_SCOPE_FRONTEND) {
			return 1;
		}
	}
	for (unsigned i = 0; i < s->paint_count; ++i) {
		if (s->paint[i].draw.scope == XVT_SCOPE_FRONTEND) {
			return 1;
		}
	}
	for (unsigned i = 0; i < s->copy_count; ++i) {
		if (s->copies[i].draw.scope == XVT_SCOPE_FRONTEND) {
			return 1;
		}
	}
	for (unsigned i = 0; i < s->surface_event_count; ++i) {
		if (is_frontend_target(s->surface_events[i].target)) {
			return 1;
		}
	}
	return s->preview_count != 0;
}

int xvt_frontend_replay(AeronCommandBuffer *cmd,
			const struct xvt_render_snapshot *s, int width,
			int height)
{
	if (!prepare_targets(cmd, width, height)) {
		return 0;
	}
	if (g_replayed_snapshot_serial == s->snapshot_serial) {
		return 1;
	}
	unsigned indices[6] = {0};
	const struct xvt_snap_sprite *cursor = NULL;
	int active = -1;
	const unsigned counts[6] = {s->sprite_count,	    s->glyph_count,
				    s->paint_count,	    s->copy_count,
				    s->surface_event_count, s->preview_count};
	for (;;) {
		uint32_t order = UINT32_MAX;
		unsigned stream = 6;
		const uint32_t orders[6] = {
			indices[0] < counts[0]
				? s->sprites[indices[0]].draw.z_order
				: UINT32_MAX,
			indices[1] < counts[1]
				? s->glyphs[indices[1]].draw.z_order
				: UINT32_MAX,
			indices[2] < counts[2]
				? s->paint[indices[2]].draw.z_order
				: UINT32_MAX,
			indices[3] < counts[3]
				? s->copies[indices[3]].draw.z_order
				: UINT32_MAX,
			indices[4] < counts[4]
				? s->surface_events[indices[4]].z_order
				: UINT32_MAX,
			indices[5] < counts[5]
				? s->previews[indices[5]].draw.z_order
				: UINT32_MAX};
		for (unsigned j = 0; j < 6; ++j) {
			if (orders[j] < order) {
				order = orders[j];
				stream = j;
			}
		}
		if (stream == 6) {
			break;
		}
		unsigned i = indices[stream]++;
		if (stream == 3 || stream == 4) {
			if (active >= 0) {
				AeronDrawList_Render(g_list, cmd);
			}
			active = -1;
			if (stream == 4) {
				if (!apply_surface_event(cmd,
							 &s->surface_events[i],
							 cursor)) {
					return 0;
				}
				if (s->surface_events[i].kind ==
				    XVT_SURFACE_PRESENT) {
					cursor = NULL;
				}
			} else {
				const struct xvt_snap_copy_rect *c =
					&s->copies[i];
				if (c->draw.scope == XVT_SCOPE_FRONTEND &&
				    !copy_region(cmd, c->source_target,
						 c->draw.target, c->source,
						 c->destination)) {
					return 0;
				}
			}
			continue;
		}
		const struct xvt_snap_draw_header *h =
			stream == 0   ? &s->sprites[i].draw
			: stream == 1 ? &s->glyphs[i].draw
			: stream == 2 ? &s->paint[i].draw
				      : &s->previews[i].draw;
		if (stream == 0 && h->target == XVT_TARGET_FRONT_CURSOR) {
			cursor = &s->sprites[i];
			continue;
		}
		if (h->scope != XVT_SCOPE_FRONTEND ||
		    !is_frontend_target(h->target)) {
			continue;
		}
		if (active != h->target) {
			if (active >= 0) {
				AeronDrawList_Render(g_list, cmd);
			}
			if (!prepare_target(cmd, h->target)) {
				return 0;
			}
			active = h->target;
			AeronDrawList_Begin(g_list, g_targets[active], g_width,
					    g_height, AERON_DRAWLIST2D_LOAD,
					    NULL);
		}
		if (stream == 0) {
			if (!draw_sprite(&s->sprites[i], g_scale)) {
				return 0;
			}
		} else if (stream == 1) {
			xvt_ui_glyph(g_list, &s->glyphs[i], g_scale, 0, 0);
		} else if (stream == 2) {
			xvt_ui_paint(g_list, &s->paint[i], g_scale);
		} else {
			draw_preview(i);
		}
	}
	if (active >= 0) {
		AeronDrawList_Render(g_list, cmd);
	}
	g_replayed_snapshot_serial = s->snapshot_serial;
	return 1;
}

AeronTexture *xvt_frontend_output(void)
{
	return g_presented ? Aeron_RenderTargetGetTexture(
				     g_targets[XVT_TARGET_FRONT_PRESENTED])
			   : NULL;
}

AeronTexture *xvt_frontend_movie_overlay(void)
{
	return g_movie_presented ? Aeron_RenderTargetGetTexture(
					   g_targets[XVT_TARGET_FRONT_MOVIE])
				 : NULL;
}

void xvt_frontend_present_cursor(float opacity)
{
	if (!g_presented || !g_cursor_visible || !g_cursor_target ||
	    opacity <= 0 || xvt_input_is_captured() || Aeron_DebugUiVisible() ||
	    Aeron_RelativeMouseMode()) {
		return;
	}
	int x = g_cursor.destination.x;
	int y = g_cursor.destination.y;
	/* Align with the baked classic cursor during the renderer crossfade. */
	if (opacity >= 1) {
		xvt_input_frontend_cursor_position(&x, &y);
	}
	AeronRectI classic = xvt_presentation_classic_rect();
	struct xvt_snap_rect clip = g_cursor.draw.clip;
	int left = clip.x > 0 ? clip.x : 0;
	int top = clip.y > 0 ? clip.y : 0;
	int right = clip.x + clip.width < 640 ? clip.x + clip.width : 640;
	int bottom = clip.y + clip.height < 480 ? clip.y + clip.height : 480;
	if (right <= left || bottom <= top) {
		return;
	}
	AeronTextureLayerDesc layer = {
		.texture = Aeron_RenderTargetGetTexture(g_cursor_target),
		.logical_rect = {classic.x + x, classic.y + y,
				 g_cursor.destination.width,
				 g_cursor.destination.height},
		.blend_mode = AERON_LAYER_BLEND_PREMULTIPLIED,
		.color_space = AERON_COLOR_SPACE_SRGB,
		.tint_enabled = 1,
		.tint_rgba = {opacity, opacity, opacity, opacity},
		.scissor = {classic.x + left, classic.y + top, right - left,
			    bottom - top}};
	if (!Aeron_SubmitTextureLayer(&layer)) {
		Aeron_RequestFatalRendererError("frontend cursor presentation");
	}
}

void xvt_frontend_shutdown(void)
{
	for (unsigned i = 0; i < TARGETS; ++i) {
		Aeron_DestroyRenderTarget(g_targets[i]);
		g_targets[i] = NULL;
	}
	AeronDrawList_Destroy(g_list);
	g_list = NULL;
	g_replayed_snapshot_serial = UINT64_MAX;
	g_width = 0;
	g_height = 0;
	g_presented = 0;
	g_movie_presented = 0;
	g_release_presented = 0;
	Aeron_DestroyRenderTarget(g_cursor_target);
	g_cursor_target = NULL;
	g_cursor_visible = 0;
	memset(g_saved_bounds, 0, sizeof g_saved_bounds);
}

void xvt_frontend_release_for_flight(void)
{
	for (unsigned id = 0; id < TARGETS; ++id) {
		/* Saved screen-stack images outlive the classic surfaces and must survive a later pop. */
		if (id == XVT_TARGET_FRONT_PRESENTED || is_saved_target(id)) {
			continue;
		}
		Aeron_DestroyRenderTarget(g_targets[id]);
		g_targets[id] = NULL;
	}
	AeronDrawList_Destroy(g_list);
	g_list = NULL;
	g_movie_presented = 0;
	g_release_presented = 1;
}

void xvt_frontend_release_presented(void)
{
	if (!g_release_presented) {
		return;
	}
	Aeron_DestroyRenderTarget(g_targets[XVT_TARGET_FRONT_PRESENTED]);
	g_targets[XVT_TARGET_FRONT_PRESENTED] = NULL;
	g_presented = 0;
	g_release_presented = 0;
	Aeron_DestroyRenderTarget(g_cursor_target);
	g_cursor_target = NULL;
	g_cursor_visible = 0;
}

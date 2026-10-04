#include "xvt_runtime/snapshot/render_frontend.h"

#include <stdlib.h>
#include <string.h>

#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"

static unsigned g_target;
static unsigned g_suppress;
static uint64_t g_serial;
static uint64_t g_generation;
static xvt_scene_kind g_presented_scene;
static unsigned g_presented_target;
static int g_text_entry;
static int g_cursor_drawing;
static int g_cursor_visible;
static int g_surfaces_released;
static struct xvt_snap_sprite g_cursor_sprite;

struct frontend_glyph_identity {
	uintptr_t pixels_address;
	uint16_t character;
	uint16_t width;
	uint16_t height;
};

static struct frontend_glyph_identity g_font_glyphs[10][256];
static uint64_t g_font_asset_ids[10];

static int compare_glyph_identity(const void *left, const void *right)
{
	const struct frontend_glyph_identity *a = left;
	const struct frontend_glyph_identity *b = right;
	if (a->pixels_address != b->pixels_address) {
		return a->pixels_address < b->pixels_address ? -1 : 1;
	}
	return (int)a->character - b->character;
}

void xvt_render_frontend_font_loaded(const struct bitmap_font *font)
{
	for (unsigned slot = 0; slot < 10; ++slot) {
		if (font != &g_front_state.font_slots[slot]) {
			continue;
		}
		g_font_asset_ids[slot] = xvt_render_assets_image_id(font);
		for (unsigned ch = 0; ch < 256; ++ch) {
			g_font_glyphs[slot][ch] =
				(struct frontend_glyph_identity){
					(uintptr_t)(font->p_glyph_bits +
						    font->glyph_bit_offset[ch]),
					ch, font->glyph_width[ch],
					font->glyph_height[ch]};
		}
		qsort(g_font_glyphs[slot], 256, sizeof g_font_glyphs[slot][0],
		      compare_glyph_identity);
		return;
	}
}

static int find_font_glyph(unsigned slot, const struct image_resource *glyph)
{
	const struct frontend_glyph_identity *entries = g_font_glyphs[slot];
	uintptr_t pixels_address = (uintptr_t)glyph->pixels;
	unsigned first = 0;
	unsigned end = 256;
	while (first < end) {
		unsigned middle = first + (end - first) / 2;
		if (entries[middle].pixels_address < pixels_address) {
			first = middle + 1;
		} else {
			end = middle;
		}
	}
	for (; first < 256 && entries[first].pixels_address == pixels_address;
	     ++first) {
		if (entries[first].width == glyph->width &&
		    entries[first].height == glyph->height) {
			return entries[first].character;
		}
	}
	return -1;
}

void xvt_render_frontend_begin_draw(void) { g_text_entry = 0; }

void xvt_render_frontend_text_entry(int field)
{
	if (field == g_active_text_field_id) {
		g_text_entry = 1;
	}
}

void xvt_render_frontend_init(void)
{
	g_target = XVT_TARGET_FRONT_BACK;
	g_suppress = 0;
	g_serial = 0;
	g_generation = 0;
	g_presented_scene = XVT_SCENE_NONE;
	g_presented_target = XVT_TARGET_FRONT_BACK;
	g_text_entry = 0;
	g_cursor_drawing = 0;
	g_cursor_visible = 0;
	g_surfaces_released = 0;
	memset(g_font_glyphs, 0, sizeof g_font_glyphs);
	memset(g_font_asset_ids, 0, sizeof g_font_asset_ids);
}

unsigned xvt_render_frontend_target(void) { return g_target; }

void xvt_render_frontend_select(unsigned target)
{
	if (!g_suppress) {
		g_target = target;
	}
}

void xvt_render_frontend_suppress(int begin)
{
	if (begin) {
		++g_suppress;
	} else if (g_suppress) {
		--g_suppress;
	}
}

static struct xvt_render_snapshot *writer(void)
{
	return g_suppress ? NULL : xvt_render_snapshot_writer();
}

static uint32_t render_frontend_color(unsigned c)
{
	if (g_front_state.display_bpp == 8) {
		const struct frontend_palette_entry *p =
			&g_front_state.display_palette[c & 255];
		return 0xff000000u | ((unsigned)p->red << 16) |
		       ((unsigned)p->green << 8) | p->blue;
	}
	unsigned b = c & 31;
	unsigned r;
	unsigned g;
	if (g_front_state.pixel_format555) {
		r = (c >> 10) & 31;
		g = (c >> 5) & 31;
		g = (g << 3) | (g >> 2);
	} else {
		r = (c >> 11) & 31;
		g = (c >> 5) & 63;
		g = (g << 2) | (g >> 4);
	}
	return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (g << 8) |
	       (b << 3) | (b >> 2);
}

static struct xvt_snap_draw_header header(void)
{
	return (struct xvt_snap_draw_header){
		xvt_render_snapshot_next_order(),
		g_target,
		XVT_SCOPE_FRONTEND,
		{g_front_state.clip_min_x, g_front_state.clip_min_y,
		 g_front_state.clip_max_x - g_front_state.clip_min_x + 1,
		 g_front_state.clip_max_y - g_front_state.clip_min_y + 1}};
}

static unsigned glyph_color(unsigned color)
{
	unsigned cached = g_front_state.text_fade_color_cache[color & 65535];
	if (cached) {
		return cached;
	}
	unsigned total = g_front_state.text_fade_frame_count;
	if (!total) {
		return color;
	}
	unsigned fade = total - g_front_state.text_fade_frames_left;
	unsigned shift = g_front_state.pixel_format555 ? 10 : 11;
	unsigned green = g_front_state.pixel_format555 ? 31 : 63;
	return (((((color >> shift) & 31) * fade / total) & 31) << shift) |
	       (((((color >> 5) & green) * fade / total) & green) << 5) |
	       (((color & 31) * fade / total) & 31);
}

void xvt_render_frontend_image(const struct image_resource *image, int sx,
			       int sy, int x, int y, int width, int height,
			       unsigned kind, unsigned tint)
{
	struct xvt_render_snapshot *s = writer();
	if (!s || !image || width <= 0 || height <= 0) {
		return;
	}
	uint64_t id = xvt_render_assets_image_id(image);
	if (!id || s->sprite_count == XVT_SNAP_SPRITES) {
		++s->dropped_records;
		return;
	}
	struct xvt_snap_sprite *b = g_cursor_drawing
					    ? &g_cursor_sprite
					    : &s->sprites[s->sprite_count++];
	memset(b, 0, sizeof *b);
	b->draw = header();
	b->asset_id = id;
	b->kind = kind;
	b->source = (struct xvt_snap_rect){sx, sy, width, height};
	b->destination = (struct xvt_snap_rect){x, y, width, height};
	b->tint_color = kind == XVT_SPRITE_FRONT_TINTED ? tint : 0;
	if (g_cursor_drawing) {
		b->draw.target = XVT_TARGET_FRONT_CURSOR;
		/* Keep the whole cursor; presentation clips it at the live pointer position. */
		b->source = (struct xvt_snap_rect){0, 0, image->width,
						   image->height};
		b->destination = (struct xvt_snap_rect){
			x - sx, y - sy, image->width, image->height};
		g_cursor_visible = 1;
	}
}

void xvt_render_frontend_glyph(const struct image_resource *glyph, int x, int y,
			       unsigned color, int remap)
{
	struct xvt_render_snapshot *s = writer();
	if (!s || !glyph) {
		return;
	}
	/* All string layouts construct a view into a live ABP font. Resolve it here
	 * after wrapping/clipping, including direct glyph calls, without nested emits. */
	for (unsigned f = 0; f < 10; ++f) {
		const struct bitmap_font *font = &g_front_state.font_slots[f];
		if (!font->in_use) {
			continue;
		}
		int ch = find_font_glyph(f, glyph);
		if (ch < 0) {
			continue;
		}
		if (s->glyph_count == XVT_SNAP_GLYPHS) {
			++s->dropped_records;
			return;
		}
		if (remap && g_front_state.display_bpp == 16 &&
		    g_front_state.text_fade_frames_left) {
			color = glyph_color(color);
		}
		struct xvt_snap_glyph *g = &s->glyphs[s->glyph_count++];
		memset(g, 0, sizeof *g);
		g->draw = header();
		g->font_asset_id = g_font_asset_ids[f];
		g->character = ch;
		g->x = x;
		g->y = y;
		g->advance = font->char_spacing + glyph->width;
		g->foreground_argb = render_frontend_color(color);
		return;
	}
	++s->dropped_records;
}

void xvt_render_frontend_paint(unsigned kind, int x0, int y0, int x1, int y1,
			       unsigned color)
{
	struct xvt_render_snapshot *s = writer();
	if (!s) {
		return;
	}
	if (s->paint_count == XVT_SNAP_PAINTS) {
		++s->dropped_records;
		return;
	}
	struct xvt_snap_paint *p = &s->paint[s->paint_count++];
	*p = (struct xvt_snap_paint){
		header(), kind, render_frontend_color(color), x0, y0, x1, y1};
	if (kind == XVT_PAINT_TRANSLUCENT && g_front_state.display_bpp == 16) {
		p->color_argb = (p->color_argb & 0xffffffu) | 0x80000000u;
	}
}

static void copy_rect(unsigned source, unsigned target,
		      struct xvt_snap_rect rect)
{
	struct xvt_render_snapshot *s = writer();
	if (!s) {
		return;
	}
	if (s->copy_count == XVT_SNAP_COPIES) {
		++s->dropped_records;
		return;
	}
	struct xvt_snap_copy_rect *c = &s->copies[s->copy_count++];
	*c = (struct xvt_snap_copy_rect){.draw = header(),
					 .source_target = (uint16_t)source,
					 .source = rect,
					 .destination = rect};
	c->draw.target = target;
}

void xvt_render_frontend_copy(unsigned source, unsigned target)
{
	copy_rect(source, target, (struct xvt_snap_rect){0, 0, 640, 480});
	if (!g_suppress && target == XVT_TARGET_FRONT_BACK) {
		g_cursor_visible = 0;
	}
}

void xvt_render_frontend_screen(int slot, int restore)
{
	if ((unsigned)slot >= XVT_TARGET_FRONT_SAVED_COUNT) {
		return;
	}
	const struct RECT *r = &g_front_state.screen_states[slot].saved_rect;
	unsigned target = g_front_state.offscreen_restore_enabled
				  ? XVT_TARGET_FRONT_OFFSCREEN
				  : XVT_TARGET_FRONT_BACK;
	unsigned saved = XVT_TARGET_FRONT_SAVED_FIRST + slot;
	copy_rect(restore ? saved : target, restore ? target : saved,
		  (struct xvt_snap_rect){r->left, r->top,
					 r->right - r->left + 1,
					 r->bottom - r->top + 1});
}

static void event(unsigned kind, unsigned target, uint32_t color)
{
	struct xvt_render_snapshot *s = writer();
	if (!s) {
		return;
	}
	if (s->surface_event_count == XVT_SNAP_SURFACE_EVENTS) {
		++s->dropped_records;
		return;
	}
	s->surface_events[s->surface_event_count++] =
		(struct xvt_snap_surface_event){
			xvt_render_snapshot_next_order(),
			kind,
			target,
			0,
			{0, 0, 640, 480},
			color,
			0};
}

void xvt_render_frontend_clear(unsigned target, unsigned color)
{
	event(XVT_SURFACE_CLEAR, target, render_frontend_color(color));
	if (!g_suppress && target == XVT_TARGET_FRONT_BACK) {
		g_cursor_visible = 0;
	}
}

void xvt_render_frontend_presented_scene(xvt_scene_kind kind)
{
	++g_serial;
	g_presented_scene = kind;
	g_presented_target = kind == XVT_SCENE_FLIGHT  ? XVT_TARGET_FLIGHT_MAIN
			     : kind == XVT_SCENE_MOVIE ? XVT_TARGET_FRONT_MOVIE
						       : XVT_TARGET_FRONT_BACK;
}

void xvt_render_frontend_flight_ui_scene(xvt_scene_kind kind)
{
	xvt_render_frontend_presented_scene(kind);
	g_presented_target = XVT_TARGET_FLIGHT_MAIN;
}

void xvt_render_frontend_present(void)
{
	struct xvt_render_snapshot *s = writer();
	if (!s) {
		return;
	}
	/* Cursor drawing belongs to this presentation, not the persistent background. */
	s->cursor.visible = 0;
	if (g_cursor_visible) {
		if (s->sprite_count == XVT_SNAP_SPRITES) {
			++s->dropped_records;
			return;
		}
		struct xvt_snap_sprite cursor = g_cursor_sprite;
		cursor.draw.z_order = xvt_render_snapshot_next_order();
		s->sprites[s->sprite_count++] = cursor;
		s->cursor = (struct xvt_snap_cursor){
			cursor.asset_id,	   cursor.destination.x,
			cursor.destination.y,	   cursor.destination.width,
			cursor.destination.height, 1};
	}
	event(XVT_SURFACE_PRESENT, XVT_TARGET_FRONT_BACK, 0);
	xvt_render_frontend_presented_scene(
		xvt_dialog_is_active()	       ? XVT_SCENE_FRONTEND_MODAL
		: xvt_flight_task_is_loading() ? XVT_SCENE_LOADING
					       : XVT_SCENE_FRONTEND);
}

void xvt_render_frontend_reset(void)
{
	xvt_presentation_require_classic();
	g_surfaces_released = 0;
	++g_generation;
	g_cursor_visible = 0;
	g_cursor_drawing = 0;
	event(XVT_SURFACE_RESET, XVT_TARGET_FRONT_BACK, 0xff000000u);
	g_target = XVT_TARGET_FRONT_BACK;
}

void xvt_render_frontend_release_surfaces(void) { g_surfaces_released = 1; }

void xvt_render_frontend_cursor(int restore)
{
	if (!writer()) {
		return;
	}
	g_cursor_visible = 0;
	if (restore) {
		return;
	}
	g_cursor_drawing = 1;
	if (g_front_state.cursor_sprite_name[0]) {
		/* Capture even when the classic blitter rejects a fully clipped cursor. */
		int index = front_image_find_resource_by_name(
			g_front_state.cursor_sprite_name);
		if (index >= 0) {
			const struct image_resource *image =
				g_front_state.resource_table[index].image;
			if (image) {
				xvt_render_frontend_image(
					image, 0, 0, g_front_state.mouse_x,
					g_front_state.mouse_y, image->width,
					image->height, XVT_SPRITE_FRONT_KEYED,
					0);
			}
		}
		return;
	}
	memset(&g_cursor_sprite, 0, sizeof g_cursor_sprite);
	g_cursor_sprite.draw = header();
	g_cursor_sprite.draw.target = XVT_TARGET_FRONT_CURSOR;
	g_cursor_sprite.asset_id =
		xvt_render_assets_image_id(xvt_render_assets_default_cursor());
	g_cursor_sprite.kind = XVT_SPRITE_FRONT_KEYED;
	g_cursor_sprite.source = (struct xvt_snap_rect){
		0, 0, g_front_state.cursor_width, g_front_state.cursor_height};
	g_cursor_sprite.destination = (struct xvt_snap_rect){
		g_front_state.mouse_x, g_front_state.mouse_y,
		g_front_state.cursor_width, g_front_state.cursor_height};
	g_cursor_visible = 1;
}

void xvt_render_frontend_end_cursor(void) { g_cursor_drawing = 0; }

void xvt_render_frontend_movie(int begin)
{
	if (begin) {
		g_target = XVT_TARGET_FRONT_MOVIE;
		event(XVT_SURFACE_CLEAR, g_target, 0);
	} else {
		event(XVT_SURFACE_PRESENT, XVT_TARGET_FRONT_MOVIE, 0);
		xvt_render_frontend_presented_scene(XVT_SCENE_MOVIE);
		g_target = XVT_TARGET_FRONT_BACK;
	}
}

void xvt_render_frontend_commit(struct xvt_render_snapshot *s)
{
	s->presentation_serial = g_serial;
	s->presented_scene = g_presented_scene;
	s->frontend_generation = g_generation;
	s->frontend_surfaces_released = g_surfaces_released;
	s->presented_target = g_presented_target;
	s->text_entry_active = (s->scene_kind == XVT_SCENE_FRONTEND ||
				s->scene_kind == XVT_SCENE_FRONTEND_MODAL) &&
			       g_text_entry;
}

/* Checks the frontend draw capture (xvt_runtime/snapshot/render_frontend.h)
 * against the promises in its header. The render snapshot is started and its
 * ticks opened as the game does, and the recovered frontend's state
 * (g_front_state: pixel format, palette, clip, fonts, saved screens, cursor) is
 * set here; no game data is read. Test images and fonts are registered with the
 * asset registry under the built-in cursor kind, which needs no file.
 *
 * Not checked: the faded glyph color, since the header does not say what a
 * running fade is, and the modal dialog and loading scenes of Present, which
 * need a dialog or a flight load in progress. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

static struct image_resource g_image;
static struct image_resource g_cursor_image;
static uint8_t g_pixels[64];
static uint8_t g_glyph_bits[256 * 4];
static struct front_image_resource_record g_prepared_resource_generation[1];

/* A fresh snapshot with its first tick open, a 16-bit 565 frontend clipped to
 * (5, 6)-(600, 400), and one registered 32 x 16 image. */
static void fresh(void)
{
	xvt_render_snapshot_shutdown();
	memset(&g_front_state, 0, sizeof g_front_state);
	g_front_state.display_bpp = 16;
	g_front_state.clip_min_x = 5;
	g_front_state.clip_min_y = 6;
	g_front_state.clip_max_x = 600;
	g_front_state.clip_max_y = 400;
	g_active_text_field_id = -1;
	xvt_render_snapshot_init();
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() != NULL);
	g_image = (struct image_resource){
		.width = 32, .height = 16, .pixels = g_pixels};
	XVT_ASSERT_TRUE(xvt_render_assets_register_image(
				&g_image, 0, "", XVT_IMAGE_BUILTIN_CURSOR, 0, 1,
				0, 0, 0) != 0);
}

static struct xvt_render_snapshot *writer(void)
{
	return xvt_render_snapshot_writer();
}

/* Commits the open tick and opens the next; returns the snapshot just committed. */
static const struct xvt_render_snapshot *next_tick(void)
{
	xvt_render_snapshot_commit(0, 1, 0);
	const struct xvt_render_snapshot *committed =
		xvt_render_snapshot_current();
	xvt_render_snapshot_begin_frame();
	return committed;
}

static uint32_t paint_color(unsigned color)
{
	xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, color);
	return writer()->paint[writer()->paint_count - 1].color_argb;
}

static void check_color_conversion(void)
{
	fresh();
	g_front_state.display_bpp = 8;
	g_front_state.display_palette[7] =
		(struct frontend_palette_entry){0x12, 0x34, 0x56, 0};
	XVT_ASSERT_INT_EQ(paint_color(7), 0xFF123456u);

	/* 16 bits, 565: each full channel is 255, an empty one 0. */
	g_front_state.display_bpp = 16;
	g_front_state.pixel_format555 = 0;
	XVT_ASSERT_INT_EQ(paint_color(0xF800), 0xFFFF0000u);
	XVT_ASSERT_INT_EQ(paint_color(0x07E0), 0xFF00FF00u);
	XVT_ASSERT_INT_EQ(paint_color(0x001F), 0xFF0000FFu);
	XVT_ASSERT_INT_EQ(paint_color(0x0000), 0xFF000000u);
	XVT_ASSERT_INT_EQ(paint_color(0xFFFF), 0xFFFFFFFFu);

	/* 16 bits, 555. */
	g_front_state.pixel_format555 = 1;
	XVT_ASSERT_INT_EQ(paint_color(0x7C00), 0xFFFF0000u);
	XVT_ASSERT_INT_EQ(paint_color(0x03E0), 0xFF00FF00u);
	XVT_ASSERT_INT_EQ(paint_color(0x001F), 0xFF0000FFu);
	XVT_ASSERT_INT_EQ(paint_color(0x7FFF), 0xFFFFFFFFu);
}

static void check_paint_record(void)
{
	fresh();
	xvt_render_frontend_select(XVT_TARGET_FRONT_OFFSCREEN);
	xvt_render_frontend_paint(XVT_PAINT_LINE, 1, 2, 3, 4, 0xFFFF);
	xvt_render_frontend_paint(XVT_PAINT_FRAME, 9, 8, 7, 6, 0);
	XVT_ASSERT_INT_EQ(writer()->paint_count, 2);
	const struct xvt_snap_paint *first = &writer()->paint[0];
	const struct xvt_snap_paint *second = &writer()->paint[1];
	XVT_ASSERT_INT_EQ(first->kind, XVT_PAINT_LINE);
	XVT_ASSERT_INT_EQ(first->x0, 1);
	XVT_ASSERT_INT_EQ(first->y0, 2);
	XVT_ASSERT_INT_EQ(first->x1, 3);
	XVT_ASSERT_INT_EQ(first->y1, 4);
	XVT_ASSERT_INT_EQ(first->draw.target, XVT_TARGET_FRONT_OFFSCREEN);
	XVT_ASSERT_INT_EQ(first->draw.clip.x, 5);
	XVT_ASSERT_INT_EQ(first->draw.clip.y, 6);
	XVT_ASSERT_INT_EQ(second->kind, XVT_PAINT_FRAME);
	/* Draw records are ordered. */
	XVT_ASSERT_TRUE(second->draw.z_order > first->draw.z_order);

	/* Translucent paint gets half alpha at 16 bits per pixel only. */
	xvt_render_frontend_paint(XVT_PAINT_TRANSLUCENT, 0, 0, 1, 1, 0xF800);
	uint32_t alpha = writer()->paint[2].color_argb >> 24;
	XVT_ASSERT_TRUE(alpha == 0x7F || alpha == 0x80);
	XVT_ASSERT_INT_EQ(writer()->paint[2].color_argb & 0xFFFFFFu, 0xFF0000u);
	g_front_state.display_bpp = 8;
	xvt_render_frontend_paint(XVT_PAINT_TRANSLUCENT, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(writer()->paint[3].color_argb >> 24, 0xFF);
}

static void check_target_and_suppression(void)
{
	fresh();
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(xvt_render_frontend_target(),
			  XVT_TARGET_FRONT_BACKUP);

	/* Two levels need two ends; while suppressed, Select and every draw call do nothing. */
	xvt_render_frontend_suppress(1);
	xvt_render_frontend_suppress(1);
	xvt_render_frontend_suppress(0);
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(xvt_render_frontend_target(),
			  XVT_TARGET_FRONT_BACKUP);
	xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 4, 4,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACK);
	xvt_render_frontend_clear(XVT_TARGET_FRONT_BACK, 0);
	xvt_render_frontend_screen(0, 0);
	XVT_ASSERT_INT_EQ(writer()->paint_count, 0);
	XVT_ASSERT_INT_EQ(writer()->sprite_count, 0);
	XVT_ASSERT_INT_EQ(writer()->copy_count, 0);
	XVT_ASSERT_INT_EQ(writer()->surface_event_count, 0);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 0);

	xvt_render_frontend_suppress(0);
	xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(writer()->paint_count, 1);

	/* It never drops below zero: an extra end leaves one begin enough to suppress again. */
	xvt_render_frontend_suppress(0);
	xvt_render_frontend_suppress(1);
	xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(writer()->paint_count, 1);
	xvt_render_frontend_suppress(0);
}

/* Without an open tick no draw call records anything. */
static void check_no_tick(void)
{
	fresh();
	xvt_render_snapshot_commit(0, 1, 0);
	const struct xvt_render_snapshot *current =
		xvt_render_snapshot_current();
	xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 4, 4,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACK);
	xvt_render_frontend_clear(XVT_TARGET_FRONT_BACK, 0);
	XVT_ASSERT_INT_EQ(current->paint_count, 0);
	XVT_ASSERT_INT_EQ(current->sprite_count, 0);
	XVT_ASSERT_INT_EQ(current->copy_count, 0);
	XVT_ASSERT_INT_EQ(current->surface_event_count, 0);
	XVT_ASSERT_INT_EQ(current->dropped_records, 0);
}

/* A full list counts a dropped record. */
static void check_full_lists(void)
{
	fresh();
	for (unsigned i = 0; i < XVT_SNAP_PAINTS; ++i) {
		xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	}
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 0);
	xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(writer()->paint_count, XVT_SNAP_PAINTS);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 1);

	for (unsigned i = 0; i < XVT_SNAP_COPIES; ++i) {
		xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
					 XVT_TARGET_FRONT_BACKUP);
	}
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 1);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(writer()->copy_count, XVT_SNAP_COPIES);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 2);

	for (unsigned i = 0; i < XVT_SNAP_SURFACE_EVENTS; ++i) {
		xvt_render_frontend_clear(XVT_TARGET_FRONT_BACKUP, 0);
	}
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 2);
	xvt_render_frontend_clear(XVT_TARGET_FRONT_BACKUP, 0);
	XVT_ASSERT_INT_EQ(writer()->surface_event_count,
			  XVT_SNAP_SURFACE_EVENTS);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 3);

	for (unsigned i = 0; i < XVT_SNAP_SPRITES; ++i) {
		xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 4, 4,
					  XVT_SPRITE_FRONT_OPAQUE, 0);
	}
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 3);
	xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 4, 4,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(writer()->sprite_count, XVT_SNAP_SPRITES);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 4);
}

static void check_image(void)
{
	fresh();
	uint64_t id = xvt_render_assets_image_id(&g_image);
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACKUP);
	xvt_render_frontend_image(&g_image, 3, 4, 100, 200, 10, 12,
				  XVT_SPRITE_FRONT_TINTED, 0x11223344u);
	XVT_ASSERT_INT_EQ(writer()->sprite_count, 1);
	const struct xvt_snap_sprite *sprite = &writer()->sprites[0];
	XVT_ASSERT_INT_EQ(sprite->asset_id, id);
	XVT_ASSERT_INT_EQ(sprite->kind, XVT_SPRITE_FRONT_TINTED);
	XVT_ASSERT_INT_EQ(sprite->tint_color, 0x11223344u);
	XVT_ASSERT_INT_EQ(sprite->source.x, 3);
	XVT_ASSERT_INT_EQ(sprite->source.y, 4);
	XVT_ASSERT_INT_EQ(sprite->source.width, 10);
	XVT_ASSERT_INT_EQ(sprite->source.height, 12);
	XVT_ASSERT_INT_EQ(sprite->destination.x, 100);
	XVT_ASSERT_INT_EQ(sprite->destination.y, 200);
	XVT_ASSERT_INT_EQ(sprite->destination.width, 10);
	XVT_ASSERT_INT_EQ(sprite->destination.height, 12);
	XVT_ASSERT_INT_EQ(sprite->draw.target, XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(sprite->draw.clip.x, 5);
	XVT_ASSERT_INT_EQ(sprite->draw.clip.y, 6);

	/* A NULL or empty image is ignored; one with no registered asset counts as dropped. */
	xvt_render_frontend_image(NULL, 0, 0, 0, 0, 4, 4,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 0, 4,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 4, 0,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(writer()->sprite_count, 1);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 0);
	struct image_resource unknown = {
		.width = 8, .height = 8, .pixels = g_pixels};
	xvt_render_frontend_image(&unknown, 0, 0, 0, 0, 4, 4,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(writer()->sprite_count, 1);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 1);
}

/* Copy records a full 640 x 480 copy between targets; Clear records a clear of a target. */
static void check_copy_and_clear(void)
{
	fresh();
	g_front_state.pixel_format555 = 0;
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(writer()->copy_count, 1);
	const struct xvt_snap_copy_rect *copy = &writer()->copies[0];
	XVT_ASSERT_INT_EQ(copy->source_target, XVT_TARGET_FRONT_OFFSCREEN);
	XVT_ASSERT_INT_EQ(copy->draw.target, XVT_TARGET_FRONT_BACKUP);
	const struct xvt_snap_rect *rects[2] = {&copy->source,
						&copy->destination};
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_INT_EQ(rects[i]->x, 0);
		XVT_ASSERT_INT_EQ(rects[i]->y, 0);
		XVT_ASSERT_INT_EQ(rects[i]->width, 640);
		XVT_ASSERT_INT_EQ(rects[i]->height, 480);
	}

	xvt_render_frontend_clear(XVT_TARGET_FRONT_OFFSCREEN, 0x07E0);
	XVT_ASSERT_INT_EQ(writer()->surface_event_count, 1);
	const struct xvt_snap_surface_event *clear =
		&writer()->surface_events[0];
	XVT_ASSERT_INT_EQ(clear->kind, XVT_SURFACE_CLEAR);
	XVT_ASSERT_INT_EQ(clear->target, XVT_TARGET_FRONT_OFFSCREEN);
	XVT_ASSERT_INT_EQ(clear->color_argb, 0xFF00FF00u);
	XVT_ASSERT_TRUE(clear->z_order > copy->draw.z_order);
}

/* Returns 1 when the last Present recorded a cursor sprite: the snapshot shows a visible cursor. */
static int present_shows_cursor(void)
{
	xvt_render_frontend_present();
	return writer()->cursor.visible != 0;
}

static void check_default_cursor(void)
{
	fresh();
	g_front_state.mouse_x = 120;
	g_front_state.mouse_y = 90;
	g_front_state.cursor_width = 12;
	g_front_state.cursor_height = 20;
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_frontend_present();
	struct xvt_render_snapshot *s = writer();
	XVT_ASSERT_INT_EQ(s->cursor.visible, 1);
	XVT_ASSERT_INT_EQ(
		s->cursor.asset_id,
		xvt_render_assets_image_id(xvt_render_assets_default_cursor()));
	XVT_ASSERT_TRUE(s->cursor.asset_id != 0);
	XVT_ASSERT_INT_EQ(s->cursor.x, 120);
	XVT_ASSERT_INT_EQ(s->cursor.y, 90);
	/* The cursor sprite, then a present event. */
	XVT_ASSERT_INT_EQ(s->sprite_count, 1);
	XVT_ASSERT_INT_EQ(s->sprites[0].asset_id, s->cursor.asset_id);
	XVT_ASSERT_INT_EQ(s->surface_event_count, 1);
	XVT_ASSERT_INT_EQ(s->surface_events[0].kind, XVT_SURFACE_PRESENT);
	XVT_ASSERT_TRUE(s->surface_events[0].z_order >
			s->sprites[0].draw.z_order);

	/* With restore the cursor is hidden. */
	xvt_render_frontend_cursor(1);
	XVT_ASSERT_TRUE(!present_shows_cursor());
}

static void check_named_cursor(void)
{
	fresh();
	g_cursor_image = (struct image_resource){
		.width = 24, .height = 30, .pixels = g_pixels};
	uint64_t id = xvt_render_assets_register_image(&g_cursor_image, 0, "",
						       XVT_IMAGE_BUILTIN_CURSOR,
						       0, 1, 0, 0, 0);
	XVT_ASSERT_TRUE(id != 0);
	memset(g_prepared_resource_generation, 0,
	       sizeof g_prepared_resource_generation);
	strcpy(g_prepared_resource_generation[0].name, "pointer");
	g_prepared_resource_generation[0].image = &g_cursor_image;
	g_front_state.resource_table = g_prepared_resource_generation;
	g_front_state.resource_count = 1;
	strcpy(g_front_state.cursor_sprite_name, "pointer");
	g_front_state.mouse_x = 300;
	g_front_state.mouse_y = 200;
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_frontend_present();
	XVT_ASSERT_INT_EQ(writer()->cursor.visible, 1);
	XVT_ASSERT_INT_EQ(writer()->cursor.asset_id, id);
	XVT_ASSERT_INT_EQ(writer()->cursor.x, 300);
	XVT_ASSERT_INT_EQ(writer()->cursor.y, 200);
}

/* While the cursor is being drawn, an image goes whole to the cursor sprite,
 * not to the sprite list. */
static void check_image_while_drawing_cursor(void)
{
	fresh();
	g_front_state.mouse_x = 50;
	g_front_state.mouse_y = 60;
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_image(&g_image, 2, 3, 50, 60, 5, 5,
				  XVT_SPRITE_FRONT_KEYED, 0);
	xvt_render_frontend_end_cursor();
	XVT_ASSERT_INT_EQ(writer()->sprite_count, 0);
	xvt_render_frontend_present();
	XVT_ASSERT_INT_EQ(writer()->sprite_count, 1);
	const struct xvt_snap_sprite *cursor = &writer()->sprites[0];
	XVT_ASSERT_INT_EQ(cursor->asset_id,
			  xvt_render_assets_image_id(&g_image));
	XVT_ASSERT_INT_EQ(cursor->source.width, g_image.width);
	XVT_ASSERT_INT_EQ(cursor->source.height, g_image.height);
	XVT_ASSERT_INT_EQ(writer()->cursor.visible, 1);

	/* After EndCursor images go to the sprite list again. */
	xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 4, 4,
				  XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(writer()->sprite_count, 2);
}

/* Copy or Clear aimed at the back buffer hides the cursor, unless capture is
 * suppressed; aimed elsewhere, it does not. */
static void check_back_buffer_hides_cursor(void)
{
	fresh();
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACKUP);
	xvt_render_frontend_clear(XVT_TARGET_FRONT_OFFSCREEN, 0);
	XVT_ASSERT_TRUE(present_shows_cursor());
	xvt_render_frontend_suppress(1);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACK);
	xvt_render_frontend_clear(XVT_TARGET_FRONT_BACK, 0);
	xvt_render_frontend_suppress(0);
	XVT_ASSERT_TRUE(present_shows_cursor());
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_TRUE(!present_shows_cursor());

	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_frontend_clear(XVT_TARGET_FRONT_BACK, 0);
	XVT_ASSERT_TRUE(!present_shows_cursor());
}

/* Cursor does nothing without an open tick or while suppressed. */
static void check_cursor_refusals(void)
{
	fresh();
	xvt_render_frontend_suppress(1);
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_frontend_suppress(0);
	XVT_ASSERT_TRUE(!present_shows_cursor());

	xvt_render_snapshot_commit(0, 1, 0);
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_TRUE(!present_shows_cursor());
}

/* With the sprite list full and the cursor visible, Present counts a dropped
 * record and records neither the cursor nor the present event. */
static void check_present_with_sprites_full(void)
{
	fresh();
	for (unsigned i = 0; i < XVT_SNAP_SPRITES; ++i) {
		xvt_render_frontend_image(&g_image, 0, 0, 0, 0, 4, 4,
					  XVT_SPRITE_FRONT_OPAQUE, 0);
	}
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_frontend_present();
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 1);
	XVT_ASSERT_INT_EQ(writer()->surface_event_count, 0);
	XVT_ASSERT_INT_EQ(writer()->cursor.visible, 0);
	XVT_ASSERT_INT_EQ(next_tick()->presented_scene, XVT_SCENE_NONE);
}

/* Present records the presented scene; with no dialog and no flight loading, the frontend. */
static void check_present_scene(void)
{
	fresh();
	uint64_t serial = next_tick()->presentation_serial;
	xvt_render_frontend_present();
	const struct xvt_render_snapshot *committed = next_tick();
	XVT_ASSERT_INT_EQ(committed->presented_scene, XVT_SCENE_FRONTEND);
	XVT_ASSERT_INT_EQ(committed->presented_target, XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_TRUE(committed->presentation_serial > serial);
	XVT_ASSERT_INT_EQ(committed->surface_event_count, 1);
	XVT_ASSERT_INT_EQ(committed->surface_events[0].kind,
			  XVT_SURFACE_PRESENT);
}

static void check_presented_scene(void)
{
	fresh();

	static const struct {
		xvt_scene_kind kind;
		unsigned target;
	} k_scenes[] = {
		{XVT_SCENE_FLIGHT, XVT_TARGET_FLIGHT_MAIN},
		{XVT_SCENE_MOVIE, XVT_TARGET_FRONT_MOVIE},
		{XVT_SCENE_FRONTEND, XVT_TARGET_FRONT_BACK},
		{XVT_SCENE_LOADING, XVT_TARGET_FRONT_BACK},
	};

	uint64_t serial = next_tick()->presentation_serial;
	for (size_t i = 0; i < sizeof k_scenes / sizeof k_scenes[0]; ++i) {
		xvt_render_frontend_presented_scene(k_scenes[i].kind);
		const struct xvt_render_snapshot *committed = next_tick();
		XVT_ASSERT_INT_EQ(committed->presented_scene, k_scenes[i].kind);
		XVT_ASSERT_INT_EQ(committed->presented_target,
				  k_scenes[i].target);
		XVT_ASSERT_TRUE(committed->presentation_serial > serial);
		serial = committed->presentation_serial;
	}
	xvt_render_frontend_flight_ui_scene(XVT_SCENE_LOADING);
	const struct xvt_render_snapshot *committed = next_tick();
	XVT_ASSERT_INT_EQ(committed->presented_scene, XVT_SCENE_LOADING);
	XVT_ASSERT_INT_EQ(committed->presented_target, XVT_TARGET_FLIGHT_MAIN);
	XVT_ASSERT_TRUE(committed->presentation_serial > serial);
}

static void check_reset_and_release(void)
{
	fresh();
	xvt_render_frontend_release_surfaces();
	const struct xvt_render_snapshot *committed = next_tick();
	uint64_t generation = committed->frontend_generation;
	XVT_ASSERT_INT_EQ(committed->frontend_surfaces_released, 1);
	XVT_ASSERT_INT_EQ(next_tick()->frontend_surfaces_released, 1);

	xvt_render_frontend_select(XVT_TARGET_FRONT_OFFSCREEN);
	xvt_render_frontend_cursor(0);
	xvt_render_frontend_end_cursor();
	xvt_render_frontend_reset();
	XVT_ASSERT_INT_EQ(xvt_render_frontend_target(), XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(writer()->surface_event_count, 1);
	XVT_ASSERT_INT_EQ(writer()->surface_events[0].kind, XVT_SURFACE_RESET);
	XVT_ASSERT_INT_EQ(writer()->surface_events[0].color_argb, 0xFF000000u);
	XVT_ASSERT_TRUE(!present_shows_cursor());
	committed = next_tick();
	XVT_ASSERT_INT_EQ(committed->frontend_generation, generation + 1);
	XVT_ASSERT_INT_EQ(committed->frontend_surfaces_released, 0);
}

static void check_screen(void)
{
	fresh();
	g_front_state.screen_states[3].saved_rect =
		(struct RECT){10, 20, 109, 69};
	xvt_render_frontend_screen(3, 0);
	xvt_render_frontend_screen(3, 1);
	g_front_state.offscreen_restore_enabled = 1;
	xvt_render_frontend_screen(3, 1);
	XVT_ASSERT_INT_EQ(writer()->copy_count, 3);
	const struct xvt_snap_copy_rect *save = &writer()->copies[0];
	const struct xvt_snap_copy_rect *restore = &writer()->copies[1];
	const struct xvt_snap_copy_rect *offscreen = &writer()->copies[2];
	XVT_ASSERT_INT_EQ(save->source_target, XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(save->draw.target, XVT_TARGET_FRONT_SAVED_FIRST + 3);
	XVT_ASSERT_INT_EQ(restore->source_target,
			  XVT_TARGET_FRONT_SAVED_FIRST + 3);
	XVT_ASSERT_INT_EQ(restore->draw.target, XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(offscreen->source_target,
			  XVT_TARGET_FRONT_SAVED_FIRST + 3);
	XVT_ASSERT_INT_EQ(offscreen->draw.target, XVT_TARGET_FRONT_OFFSCREEN);
	for (int i = 0; i < 3; ++i) {
		const struct xvt_snap_copy_rect *copy = &writer()->copies[i];
		XVT_ASSERT_INT_EQ(copy->source.x, 10);
		XVT_ASSERT_INT_EQ(copy->source.y, 20);
		XVT_ASSERT_INT_EQ(memcmp(&copy->source, &copy->destination,
					 sizeof copy->source),
				  0);
	}

	/* Other slots are ignored. */
	xvt_render_frontend_screen(-1, 0);
	xvt_render_frontend_screen(XVT_TARGET_FRONT_SAVED_COUNT, 1);
	XVT_ASSERT_INT_EQ(writer()->copy_count, 3);
}

static void check_movie(void)
{
	fresh();
	xvt_render_frontend_movie(1);
	XVT_ASSERT_INT_EQ(xvt_render_frontend_target(), XVT_TARGET_FRONT_MOVIE);
	XVT_ASSERT_INT_EQ(writer()->surface_event_count, 1);
	XVT_ASSERT_INT_EQ(writer()->surface_events[0].kind, XVT_SURFACE_CLEAR);
	XVT_ASSERT_INT_EQ(writer()->surface_events[0].target,
			  XVT_TARGET_FRONT_MOVIE);
	xvt_render_frontend_movie(0);
	XVT_ASSERT_INT_EQ(xvt_render_frontend_target(), XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(writer()->surface_event_count, 2);
	XVT_ASSERT_INT_EQ(writer()->surface_events[1].kind,
			  XVT_SURFACE_PRESENT);
	XVT_ASSERT_INT_EQ(writer()->surface_events[1].target,
			  XVT_TARGET_FRONT_MOVIE);
	XVT_ASSERT_INT_EQ(next_tick()->presented_scene, XVT_SCENE_MOVIE);
}

/* The text-entry mark reaches the snapshot only in a frontend scene. */
static void check_text_entry(void)
{
	fresh();
	xvt_render_snapshot_set_scene_kind(XVT_SCENE_FRONTEND);
	g_active_text_field_id = 4;
	xvt_render_frontend_begin_draw();
	xvt_render_frontend_text_entry(3);
	XVT_ASSERT_INT_EQ(next_tick()->text_entry_active, 0);
	xvt_render_frontend_text_entry(4);
	XVT_ASSERT_INT_EQ(next_tick()->text_entry_active, 1);
	XVT_ASSERT_INT_EQ(next_tick()->text_entry_active, 1);
	xvt_render_frontend_begin_draw();
	XVT_ASSERT_INT_EQ(next_tick()->text_entry_active, 0);

	xvt_render_frontend_text_entry(4);
	xvt_render_snapshot_set_scene_kind(XVT_SCENE_FLIGHT);
	XVT_ASSERT_INT_EQ(next_tick()->text_entry_active, 0);
}

/* Slot 2 holds a font whose glyph c is 4 bytes into the bits per character, 3 to 7 pixels wide. */
static struct bitmap_font *load_test_font(void)
{
	struct bitmap_font *font = &g_front_state.font_slots[2];
	font->p_glyph_bits = g_glyph_bits;
	for (unsigned c = 0; c < 256; ++c) {
		font->glyph_bit_offset[c] = c * 4;
		font->glyph_width[c] = (uint8_t)(3 + c % 5);
		font->glyph_height[c] = 9;
	}
	font->in_use = 1;
	font->char_spacing = 1;
	return font;
}

static struct image_resource glyph_image(const struct bitmap_font *font,
					 unsigned c)
{
	return (struct image_resource){.width = font->glyph_width[c],
				       .height = font->glyph_height[c],
				       .pixels = font->p_glyph_bits +
						 font->glyph_bit_offset[c]};
}

static void check_glyph(void)
{
	fresh();
	g_front_state.pixel_format555 = 0;
	struct bitmap_font *font = load_test_font();
	uint64_t id = xvt_render_assets_register_image(
		font, 0, "", XVT_IMAGE_BUILTIN_CURSOR, 0, 1, 10, 0, 0);
	XVT_ASSERT_TRUE(id != 0);
	xvt_render_frontend_font_loaded(font);
	/* The font's id as registered when it was loaded, not a later one. */
	XVT_ASSERT_TRUE(xvt_render_assets_register_image(
				font, 0, "", XVT_IMAGE_BUILTIN_CURSOR, 0, 1, 12,
				0, 0) != id);

	struct image_resource a = glyph_image(font, 'A');
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACKUP);
	xvt_render_frontend_glyph(&a, 40, 50, 0xF800, 0);
	XVT_ASSERT_INT_EQ(writer()->glyph_count, 1);
	const struct xvt_snap_glyph *glyph = &writer()->glyphs[0];
	XVT_ASSERT_INT_EQ(glyph->character, 'A');
	XVT_ASSERT_INT_EQ(glyph->x, 40);
	XVT_ASSERT_INT_EQ(glyph->y, 50);
	XVT_ASSERT_INT_EQ(glyph->font_asset_id, id);
	XVT_ASSERT_INT_EQ(glyph->foreground_argb, 0xFFFF0000u);
	XVT_ASSERT_INT_EQ(glyph->draw.target, XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 0);

	/* A glyph found in no slot counts as dropped: wrong pixels, wrong size,
	 * or a slot not in use. */
	struct image_resource stray = {
		.width = 3, .height = 9, .pixels = g_pixels};
	xvt_render_frontend_glyph(&stray, 0, 0, 0, 0);
	struct image_resource resized = glyph_image(font, 'B');
	resized.height = 8;
	xvt_render_frontend_glyph(&resized, 0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 2);
	font->in_use = 0;
	xvt_render_frontend_glyph(&a, 0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 3);
	XVT_ASSERT_INT_EQ(writer()->glyph_count, 1);
}

/* A font that is not one of the frontend's font slots is not indexed. */
static void check_font_outside_slots(void)
{
	fresh();
	static struct bitmap_font outside;
	memset(&outside, 0, sizeof outside);
	outside.p_glyph_bits = g_glyph_bits;
	outside.glyph_width['Q'] = 4;
	outside.glyph_height['Q'] = 9;
	outside.glyph_bit_offset['Q'] = 'Q' * 4;
	outside.in_use = 1;
	g_front_state.font_slots[0].in_use = 1;
	xvt_render_frontend_font_loaded(&outside);
	struct image_resource q = glyph_image(&outside, 'Q');
	xvt_render_frontend_glyph(&q, 0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(writer()->glyph_count, 0);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 1);
}

/* Init lifts suppression and starts the presentation serial over. */
static void check_init_resets(void)
{
	fresh();
	xvt_render_frontend_presented_scene(XVT_SCENE_FRONTEND);
	xvt_render_frontend_presented_scene(XVT_SCENE_FRONTEND);
	uint64_t serial = next_tick()->presentation_serial;
	xvt_render_frontend_suppress(1);
	xvt_render_frontend_init();
	xvt_render_frontend_paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(writer()->paint_count, 1);
	XVT_ASSERT_TRUE(next_tick()->presentation_serial < serial);
}

int main(void)
{
	check_color_conversion();
	check_paint_record();
	check_target_and_suppression();
	check_no_tick();
	check_full_lists();
	check_image();
	check_copy_and_clear();
	check_default_cursor();
	check_named_cursor();
	check_image_while_drawing_cursor();
	check_back_buffer_hides_cursor();
	check_cursor_refusals();
	check_present_with_sprites_full();
	check_present_scene();
	check_presented_scene();
	check_reset_and_release();
	check_screen();
	check_movie();
	check_text_entry();
	check_glyph();
	check_font_outside_slots();
	check_init_resets();
	xvt_render_snapshot_shutdown();
	return 0;
}

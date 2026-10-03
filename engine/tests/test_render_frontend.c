/* Checks the frontend draw capture (xvt_runtime/snapshot/render_frontend.h) against the promises in its
 * header. The render snapshot is started and its ticks opened as the game does, and the recovered
 * frontend's state (g_frontState: pixel format, palette, clip, fonts, saved screens, cursor) is set here;
 * no game data is read. Test images and fonts are registered with the asset registry under the built-in
 * cursor kind, which needs no file.
 *
 * Not checked: the faded glyph color, since the header does not say what a running fade is, and the modal
 * dialog and loading scenes of Present, which need a dialog or a flight load in progress. */
#include "test_assert.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

#include <stdint.h>
#include <string.h>

static ImageResource g_image;
static ImageResource g_cursorImage;
static uint8_t g_pixels[64];
static uint8_t g_glyphBits[256 * 4];
static FrontImageResourceRecord g_preparedResourceGeneration[1];

/* A fresh snapshot with its first tick open, a 16-bit 565 frontend clipped to (5, 6)-(600, 400), and one
 * registered 32 x 16 image. */
static void Fresh(void)
{
	XvtRenderSnapshot_Shutdown();
	memset(&g_frontState, 0, sizeof g_frontState);
	g_frontState.displayBpp = 16;
	g_frontState.clipMinX = 5;
	g_frontState.clipMinY = 6;
	g_frontState.clipMaxX = 600;
	g_frontState.clipMaxY = 400;
	g_activeTextFieldId = -1;
	XvtRenderSnapshot_Init();
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() != NULL);
	g_image =
		(ImageResource){.width = 32, .height = 16, .pixels = g_pixels};
	XVT_ASSERT_TRUE(XvtRenderAssets_RegisterImage(&g_image, 0, "",
						      XVT_IMAGE_BUILTIN_CURSOR,
						      0, 1, 0, 0, 0) != 0);
}

static XvtRenderSnapshot *Writer(void) { return XvtRenderSnapshot_Writer(); }

/* Commits the open tick and opens the next; returns the snapshot just committed. */
static const XvtRenderSnapshot *NextTick(void)
{
	XvtRenderSnapshot_Commit(0, 1, 0);
	const XvtRenderSnapshot *committed = XvtRenderSnapshot_Current();
	XvtRenderSnapshot_BeginFrame();
	return committed;
}

static uint32_t PaintColor(unsigned color)
{
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, color);
	return Writer()->paint[Writer()->paint_count - 1].color_argb;
}

static void CheckColorConversion(void)
{
	Fresh();
	g_frontState.displayBpp = 8;
	g_frontState.displayPalette[7] =
		(FrontendPaletteEntry){0x12, 0x34, 0x56, 0};
	XVT_ASSERT_INT_EQ(PaintColor(7), 0xFF123456u);

	/* 16 bits, 565: each full channel is 255, an empty one 0. */
	g_frontState.displayBpp = 16;
	g_frontState.pixelFormat555 = 0;
	XVT_ASSERT_INT_EQ(PaintColor(0xF800), 0xFFFF0000u);
	XVT_ASSERT_INT_EQ(PaintColor(0x07E0), 0xFF00FF00u);
	XVT_ASSERT_INT_EQ(PaintColor(0x001F), 0xFF0000FFu);
	XVT_ASSERT_INT_EQ(PaintColor(0x0000), 0xFF000000u);
	XVT_ASSERT_INT_EQ(PaintColor(0xFFFF), 0xFFFFFFFFu);

	/* 16 bits, 555. */
	g_frontState.pixelFormat555 = 1;
	XVT_ASSERT_INT_EQ(PaintColor(0x7C00), 0xFFFF0000u);
	XVT_ASSERT_INT_EQ(PaintColor(0x03E0), 0xFF00FF00u);
	XVT_ASSERT_INT_EQ(PaintColor(0x001F), 0xFF0000FFu);
	XVT_ASSERT_INT_EQ(PaintColor(0x7FFF), 0xFFFFFFFFu);
}

static void CheckPaintRecord(void)
{
	Fresh();
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_OFFSCREEN);
	XvtRenderFrontend_Paint(XVT_PAINT_LINE, 1, 2, 3, 4, 0xFFFF);
	XvtRenderFrontend_Paint(XVT_PAINT_FRAME, 9, 8, 7, 6, 0);
	XVT_ASSERT_INT_EQ(Writer()->paint_count, 2);
	const XvtSnapPaint *first = &Writer()->paint[0];
	const XvtSnapPaint *second = &Writer()->paint[1];
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
	XvtRenderFrontend_Paint(XVT_PAINT_TRANSLUCENT, 0, 0, 1, 1, 0xF800);
	uint32_t alpha = Writer()->paint[2].color_argb >> 24;
	XVT_ASSERT_TRUE(alpha == 0x7F || alpha == 0x80);
	XVT_ASSERT_INT_EQ(Writer()->paint[2].color_argb & 0xFFFFFFu, 0xFF0000u);
	g_frontState.displayBpp = 8;
	XvtRenderFrontend_Paint(XVT_PAINT_TRANSLUCENT, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(Writer()->paint[3].color_argb >> 24, 0xFF);
}

static void CheckTargetAndSuppression(void)
{
	Fresh();
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(XvtRenderFrontend_Target(), XVT_TARGET_FRONT_BACKUP);

	/* Two levels need two ends; while suppressed, Select and every draw call do nothing. */
	XvtRenderFrontend_Suppress(1);
	XvtRenderFrontend_Suppress(1);
	XvtRenderFrontend_Suppress(0);
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(XvtRenderFrontend_Target(), XVT_TARGET_FRONT_BACKUP);
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 4, 4,
				XVT_SPRITE_FRONT_OPAQUE, 0);
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
			       XVT_TARGET_FRONT_BACK);
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_BACK, 0);
	XvtRenderFrontend_Screen(0, 0);
	XVT_ASSERT_INT_EQ(Writer()->paint_count, 0);
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, 0);
	XVT_ASSERT_INT_EQ(Writer()->copy_count, 0);
	XVT_ASSERT_INT_EQ(Writer()->surface_event_count, 0);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 0);

	XvtRenderFrontend_Suppress(0);
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(Writer()->paint_count, 1);

	/* It never drops below zero: an extra end leaves one begin enough to suppress again. */
	XvtRenderFrontend_Suppress(0);
	XvtRenderFrontend_Suppress(1);
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(Writer()->paint_count, 1);
	XvtRenderFrontend_Suppress(0);
}

/* Without an open tick no draw call records anything. */
static void CheckNoTick(void)
{
	Fresh();
	XvtRenderSnapshot_Commit(0, 1, 0);
	const XvtRenderSnapshot *current = XvtRenderSnapshot_Current();
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 4, 4,
				XVT_SPRITE_FRONT_OPAQUE, 0);
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
			       XVT_TARGET_FRONT_BACK);
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_BACK, 0);
	XVT_ASSERT_INT_EQ(current->paint_count, 0);
	XVT_ASSERT_INT_EQ(current->sprite_count, 0);
	XVT_ASSERT_INT_EQ(current->copy_count, 0);
	XVT_ASSERT_INT_EQ(current->surface_event_count, 0);
	XVT_ASSERT_INT_EQ(current->dropped_records, 0);
}

/* A full list counts a dropped record. */
static void CheckFullLists(void)
{
	Fresh();
	for (unsigned i = 0; i < XVT_SNAP_PAINTS; ++i) {
		XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	}
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 0);
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(Writer()->paint_count, XVT_SNAP_PAINTS);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 1);

	for (unsigned i = 0; i < XVT_SNAP_COPIES; ++i) {
		XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
				       XVT_TARGET_FRONT_BACKUP);
	}
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 1);
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
			       XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(Writer()->copy_count, XVT_SNAP_COPIES);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 2);

	for (unsigned i = 0; i < XVT_SNAP_SURFACE_EVENTS; ++i) {
		XvtRenderFrontend_Clear(XVT_TARGET_FRONT_BACKUP, 0);
	}
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 2);
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_BACKUP, 0);
	XVT_ASSERT_INT_EQ(Writer()->surface_event_count,
			  XVT_SNAP_SURFACE_EVENTS);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 3);

	for (unsigned i = 0; i < XVT_SNAP_SPRITES; ++i) {
		XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 4, 4,
					XVT_SPRITE_FRONT_OPAQUE, 0);
	}
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 3);
	XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 4, 4,
				XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, XVT_SNAP_SPRITES);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 4);
}

static void CheckImage(void)
{
	Fresh();
	uint64_t id = XvtRenderAssets_ImageId(&g_image);
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACKUP);
	XvtRenderFrontend_Image(&g_image, 3, 4, 100, 200, 10, 12,
				XVT_SPRITE_FRONT_TINTED, 0x11223344u);
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, 1);
	const XvtSnapSprite *sprite = &Writer()->sprites[0];
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
	XvtRenderFrontend_Image(NULL, 0, 0, 0, 0, 4, 4, XVT_SPRITE_FRONT_OPAQUE,
				0);
	XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 0, 4,
				XVT_SPRITE_FRONT_OPAQUE, 0);
	XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 4, 0,
				XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, 1);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 0);
	ImageResource unknown = {.width = 8, .height = 8, .pixels = g_pixels};
	XvtRenderFrontend_Image(&unknown, 0, 0, 0, 0, 4, 4,
				XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, 1);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 1);
}

/* Copy records a full 640 x 480 copy between targets; Clear records a clear of a target. */
static void CheckCopyAndClear(void)
{
	Fresh();
	g_frontState.pixelFormat555 = 0;
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
			       XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(Writer()->copy_count, 1);
	const XvtSnapCopyRect *copy = &Writer()->copies[0];
	XVT_ASSERT_INT_EQ(copy->source_target, XVT_TARGET_FRONT_OFFSCREEN);
	XVT_ASSERT_INT_EQ(copy->draw.target, XVT_TARGET_FRONT_BACKUP);
	const XvtSnapRect *rects[2] = {&copy->source, &copy->destination};
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_INT_EQ(rects[i]->x, 0);
		XVT_ASSERT_INT_EQ(rects[i]->y, 0);
		XVT_ASSERT_INT_EQ(rects[i]->width, 640);
		XVT_ASSERT_INT_EQ(rects[i]->height, 480);
	}

	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_OFFSCREEN, 0x07E0);
	XVT_ASSERT_INT_EQ(Writer()->surface_event_count, 1);
	const XvtSnapSurfaceEvent *clear = &Writer()->surface_events[0];
	XVT_ASSERT_INT_EQ(clear->kind, XVT_SURFACE_CLEAR);
	XVT_ASSERT_INT_EQ(clear->target, XVT_TARGET_FRONT_OFFSCREEN);
	XVT_ASSERT_INT_EQ(clear->color_argb, 0xFF00FF00u);
	XVT_ASSERT_TRUE(clear->z_order > copy->draw.z_order);
}

/* Returns 1 when the last Present recorded a cursor sprite: the snapshot shows a visible cursor. */
static int PresentShowsCursor(void)
{
	XvtRenderFrontend_Present();
	return Writer()->cursor.visible != 0;
}

static void CheckDefaultCursor(void)
{
	Fresh();
	g_frontState.mouseX = 120;
	g_frontState.mouseY = 90;
	g_frontState.cursorWidth = 12;
	g_frontState.cursorHeight = 20;
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderFrontend_Present();
	XvtRenderSnapshot *s = Writer();
	XVT_ASSERT_INT_EQ(s->cursor.visible, 1);
	XVT_ASSERT_INT_EQ(
		s->cursor.asset_id,
		XvtRenderAssets_ImageId(XvtRenderAssets_DefaultCursor()));
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
	XvtRenderFrontend_Cursor(1);
	XVT_ASSERT_TRUE(!PresentShowsCursor());
}

static void CheckNamedCursor(void)
{
	Fresh();
	g_cursorImage =
		(ImageResource){.width = 24, .height = 30, .pixels = g_pixels};
	uint64_t id = XvtRenderAssets_RegisterImage(
		&g_cursorImage, 0, "", XVT_IMAGE_BUILTIN_CURSOR, 0, 1, 0, 0, 0);
	XVT_ASSERT_TRUE(id != 0);
	memset(g_preparedResourceGeneration, 0,
	       sizeof g_preparedResourceGeneration);
	strcpy(g_preparedResourceGeneration[0].name, "pointer");
	g_preparedResourceGeneration[0].image = &g_cursorImage;
	g_frontState.resourceTable = g_preparedResourceGeneration;
	g_frontState.resourceCount = 1;
	strcpy(g_frontState.cursorSpriteName, "pointer");
	g_frontState.mouseX = 300;
	g_frontState.mouseY = 200;
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderFrontend_Present();
	XVT_ASSERT_INT_EQ(Writer()->cursor.visible, 1);
	XVT_ASSERT_INT_EQ(Writer()->cursor.asset_id, id);
	XVT_ASSERT_INT_EQ(Writer()->cursor.x, 300);
	XVT_ASSERT_INT_EQ(Writer()->cursor.y, 200);
}

/* While the cursor is being drawn, an image goes whole to the cursor sprite, not to the sprite list. */
static void CheckImageWhileDrawingCursor(void)
{
	Fresh();
	g_frontState.mouseX = 50;
	g_frontState.mouseY = 60;
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_Image(&g_image, 2, 3, 50, 60, 5, 5,
				XVT_SPRITE_FRONT_KEYED, 0);
	XvtRenderFrontend_EndCursor();
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, 0);
	XvtRenderFrontend_Present();
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, 1);
	const XvtSnapSprite *cursor = &Writer()->sprites[0];
	XVT_ASSERT_INT_EQ(cursor->asset_id, XvtRenderAssets_ImageId(&g_image));
	XVT_ASSERT_INT_EQ(cursor->source.width, g_image.width);
	XVT_ASSERT_INT_EQ(cursor->source.height, g_image.height);
	XVT_ASSERT_INT_EQ(Writer()->cursor.visible, 1);

	/* After EndCursor images go to the sprite list again. */
	XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 4, 4,
				XVT_SPRITE_FRONT_OPAQUE, 0);
	XVT_ASSERT_INT_EQ(Writer()->sprite_count, 2);
}

/* Copy or Clear aimed at the back buffer hides the cursor, unless capture is suppressed; aimed elsewhere,
 * it does not. */
static void CheckBackBufferHidesCursor(void)
{
	Fresh();
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
			       XVT_TARGET_FRONT_BACKUP);
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_OFFSCREEN, 0);
	XVT_ASSERT_TRUE(PresentShowsCursor());
	XvtRenderFrontend_Suppress(1);
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
			       XVT_TARGET_FRONT_BACK);
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_BACK, 0);
	XvtRenderFrontend_Suppress(0);
	XVT_ASSERT_TRUE(PresentShowsCursor());
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN,
			       XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_TRUE(!PresentShowsCursor());

	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_BACK, 0);
	XVT_ASSERT_TRUE(!PresentShowsCursor());
}

/* Cursor does nothing without an open tick or while suppressed. */
static void CheckCursorRefusals(void)
{
	Fresh();
	XvtRenderFrontend_Suppress(1);
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderFrontend_Suppress(0);
	XVT_ASSERT_TRUE(!PresentShowsCursor());

	XvtRenderSnapshot_Commit(0, 1, 0);
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_TRUE(!PresentShowsCursor());
}

/* With the sprite list full and the cursor visible, Present counts a dropped record and records neither
 * the cursor nor the present event. */
static void CheckPresentWithSpritesFull(void)
{
	Fresh();
	for (unsigned i = 0; i < XVT_SNAP_SPRITES; ++i) {
		XvtRenderFrontend_Image(&g_image, 0, 0, 0, 0, 4, 4,
					XVT_SPRITE_FRONT_OPAQUE, 0);
	}
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderFrontend_Present();
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 1);
	XVT_ASSERT_INT_EQ(Writer()->surface_event_count, 0);
	XVT_ASSERT_INT_EQ(Writer()->cursor.visible, 0);
	XVT_ASSERT_INT_EQ(NextTick()->presented_scene, XVT_SCENE_NONE);
}

/* Present records the presented scene; with no dialog and no flight loading, the frontend. */
static void CheckPresentScene(void)
{
	Fresh();
	uint64_t serial = NextTick()->presentation_serial;
	XvtRenderFrontend_Present();
	const XvtRenderSnapshot *committed = NextTick();
	XVT_ASSERT_INT_EQ(committed->presented_scene, XVT_SCENE_FRONTEND);
	XVT_ASSERT_INT_EQ(committed->presented_target, XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_TRUE(committed->presentation_serial > serial);
	XVT_ASSERT_INT_EQ(committed->surface_event_count, 1);
	XVT_ASSERT_INT_EQ(committed->surface_events[0].kind,
			  XVT_SURFACE_PRESENT);
}

static void CheckPresentedScene(void)
{
	Fresh();

	static const struct {
		XvtSceneKind kind;
		unsigned target;
	} kScenes[] = {
		{XVT_SCENE_FLIGHT, XVT_TARGET_FLIGHT_MAIN},
		{XVT_SCENE_MOVIE, XVT_TARGET_FRONT_MOVIE},
		{XVT_SCENE_FRONTEND, XVT_TARGET_FRONT_BACK},
		{XVT_SCENE_LOADING, XVT_TARGET_FRONT_BACK},
	};

	uint64_t serial = NextTick()->presentation_serial;
	for (size_t i = 0; i < sizeof kScenes / sizeof kScenes[0]; ++i) {
		XvtRenderFrontend_PresentedScene(kScenes[i].kind);
		const XvtRenderSnapshot *committed = NextTick();
		XVT_ASSERT_INT_EQ(committed->presented_scene, kScenes[i].kind);
		XVT_ASSERT_INT_EQ(committed->presented_target,
				  kScenes[i].target);
		XVT_ASSERT_TRUE(committed->presentation_serial > serial);
		serial = committed->presentation_serial;
	}
	XvtRenderFrontend_FlightUiScene(XVT_SCENE_LOADING);
	const XvtRenderSnapshot *committed = NextTick();
	XVT_ASSERT_INT_EQ(committed->presented_scene, XVT_SCENE_LOADING);
	XVT_ASSERT_INT_EQ(committed->presented_target, XVT_TARGET_FLIGHT_MAIN);
	XVT_ASSERT_TRUE(committed->presentation_serial > serial);
}

static void CheckResetAndRelease(void)
{
	Fresh();
	XvtRenderFrontend_ReleaseSurfaces();
	const XvtRenderSnapshot *committed = NextTick();
	uint64_t generation = committed->frontend_generation;
	XVT_ASSERT_INT_EQ(committed->frontend_surfaces_released, 1);
	XVT_ASSERT_INT_EQ(NextTick()->frontend_surfaces_released, 1);

	XvtRenderFrontend_Select(XVT_TARGET_FRONT_OFFSCREEN);
	XvtRenderFrontend_Cursor(0);
	XvtRenderFrontend_EndCursor();
	XvtRenderFrontend_Reset();
	XVT_ASSERT_INT_EQ(XvtRenderFrontend_Target(), XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(Writer()->surface_event_count, 1);
	XVT_ASSERT_INT_EQ(Writer()->surface_events[0].kind, XVT_SURFACE_RESET);
	XVT_ASSERT_INT_EQ(Writer()->surface_events[0].color_argb, 0xFF000000u);
	XVT_ASSERT_TRUE(!PresentShowsCursor());
	committed = NextTick();
	XVT_ASSERT_INT_EQ(committed->frontend_generation, generation + 1);
	XVT_ASSERT_INT_EQ(committed->frontend_surfaces_released, 0);
}

static void CheckScreen(void)
{
	Fresh();
	g_frontState.screenStates[3].savedRect = (RECT){10, 20, 109, 69};
	XvtRenderFrontend_Screen(3, 0);
	XvtRenderFrontend_Screen(3, 1);
	g_frontState.offscreenRestoreEnabled = 1;
	XvtRenderFrontend_Screen(3, 1);
	XVT_ASSERT_INT_EQ(Writer()->copy_count, 3);
	const XvtSnapCopyRect *save = &Writer()->copies[0];
	const XvtSnapCopyRect *restore = &Writer()->copies[1];
	const XvtSnapCopyRect *offscreen = &Writer()->copies[2];
	XVT_ASSERT_INT_EQ(save->source_target, XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(save->draw.target, XVT_TARGET_FRONT_SAVED_FIRST + 3);
	XVT_ASSERT_INT_EQ(restore->source_target,
			  XVT_TARGET_FRONT_SAVED_FIRST + 3);
	XVT_ASSERT_INT_EQ(restore->draw.target, XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(offscreen->source_target,
			  XVT_TARGET_FRONT_SAVED_FIRST + 3);
	XVT_ASSERT_INT_EQ(offscreen->draw.target, XVT_TARGET_FRONT_OFFSCREEN);
	for (int i = 0; i < 3; ++i) {
		const XvtSnapCopyRect *copy = &Writer()->copies[i];
		XVT_ASSERT_INT_EQ(copy->source.x, 10);
		XVT_ASSERT_INT_EQ(copy->source.y, 20);
		XVT_ASSERT_INT_EQ(memcmp(&copy->source, &copy->destination,
					 sizeof copy->source),
				  0);
	}

	/* Other slots are ignored. */
	XvtRenderFrontend_Screen(-1, 0);
	XvtRenderFrontend_Screen(XVT_TARGET_FRONT_SAVED_COUNT, 1);
	XVT_ASSERT_INT_EQ(Writer()->copy_count, 3);
}

static void CheckMovie(void)
{
	Fresh();
	XvtRenderFrontend_Movie(1);
	XVT_ASSERT_INT_EQ(XvtRenderFrontend_Target(), XVT_TARGET_FRONT_MOVIE);
	XVT_ASSERT_INT_EQ(Writer()->surface_event_count, 1);
	XVT_ASSERT_INT_EQ(Writer()->surface_events[0].kind, XVT_SURFACE_CLEAR);
	XVT_ASSERT_INT_EQ(Writer()->surface_events[0].target,
			  XVT_TARGET_FRONT_MOVIE);
	XvtRenderFrontend_Movie(0);
	XVT_ASSERT_INT_EQ(XvtRenderFrontend_Target(), XVT_TARGET_FRONT_BACK);
	XVT_ASSERT_INT_EQ(Writer()->surface_event_count, 2);
	XVT_ASSERT_INT_EQ(Writer()->surface_events[1].kind,
			  XVT_SURFACE_PRESENT);
	XVT_ASSERT_INT_EQ(Writer()->surface_events[1].target,
			  XVT_TARGET_FRONT_MOVIE);
	XVT_ASSERT_INT_EQ(NextTick()->presented_scene, XVT_SCENE_MOVIE);
}

/* The text-entry mark reaches the snapshot only in a frontend scene. */
static void CheckTextEntry(void)
{
	Fresh();
	XvtRenderSnapshot_SetSceneKind(XVT_SCENE_FRONTEND);
	g_activeTextFieldId = 4;
	XvtRenderFrontend_BeginDraw();
	XvtRenderFrontend_TextEntry(3);
	XVT_ASSERT_INT_EQ(NextTick()->text_entry_active, 0);
	XvtRenderFrontend_TextEntry(4);
	XVT_ASSERT_INT_EQ(NextTick()->text_entry_active, 1);
	XVT_ASSERT_INT_EQ(NextTick()->text_entry_active, 1);
	XvtRenderFrontend_BeginDraw();
	XVT_ASSERT_INT_EQ(NextTick()->text_entry_active, 0);

	XvtRenderFrontend_TextEntry(4);
	XvtRenderSnapshot_SetSceneKind(XVT_SCENE_FLIGHT);
	XVT_ASSERT_INT_EQ(NextTick()->text_entry_active, 0);
}

/* Slot 2 holds a font whose glyph c is 4 bytes into the bits per character, 3 to 7 pixels wide. */
static BitmapFont *LoadTestFont(void)
{
	BitmapFont *font = &g_frontState.fontSlots[2];
	font->pGlyphBits = g_glyphBits;
	for (unsigned c = 0; c < 256; ++c) {
		font->glyphBitOffset[c] = c * 4;
		font->glyphWidth[c] = (uint8_t)(3 + c % 5);
		font->glyphHeight[c] = 9;
	}
	font->inUse = 1;
	font->charSpacing = 1;
	return font;
}

static ImageResource GlyphImage(const BitmapFont *font, unsigned c)
{
	return (ImageResource){.width = font->glyphWidth[c],
			       .height = font->glyphHeight[c],
			       .pixels = font->pGlyphBits +
					 font->glyphBitOffset[c]};
}

static void CheckGlyph(void)
{
	Fresh();
	g_frontState.pixelFormat555 = 0;
	BitmapFont *font = LoadTestFont();
	uint64_t id = XvtRenderAssets_RegisterImage(
		font, 0, "", XVT_IMAGE_BUILTIN_CURSOR, 0, 1, 10, 0, 0);
	XVT_ASSERT_TRUE(id != 0);
	XvtRenderFrontend_FontLoaded(font);
	/* The font's id as registered when it was loaded, not a later one. */
	XVT_ASSERT_TRUE(XvtRenderAssets_RegisterImage(font, 0, "",
						      XVT_IMAGE_BUILTIN_CURSOR,
						      0, 1, 12, 0, 0) != id);

	ImageResource a = GlyphImage(font, 'A');
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACKUP);
	XvtRenderFrontend_Glyph(&a, 40, 50, 0xF800, 0);
	XVT_ASSERT_INT_EQ(Writer()->glyph_count, 1);
	const XvtSnapGlyph *glyph = &Writer()->glyphs[0];
	XVT_ASSERT_INT_EQ(glyph->character, 'A');
	XVT_ASSERT_INT_EQ(glyph->x, 40);
	XVT_ASSERT_INT_EQ(glyph->y, 50);
	XVT_ASSERT_INT_EQ(glyph->font_asset_id, id);
	XVT_ASSERT_INT_EQ(glyph->foreground_argb, 0xFFFF0000u);
	XVT_ASSERT_INT_EQ(glyph->draw.target, XVT_TARGET_FRONT_BACKUP);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 0);

	/* A glyph found in no slot counts as dropped: wrong pixels, wrong size, or a slot not in use. */
	ImageResource stray = {.width = 3, .height = 9, .pixels = g_pixels};
	XvtRenderFrontend_Glyph(&stray, 0, 0, 0, 0);
	ImageResource resized = GlyphImage(font, 'B');
	resized.height = 8;
	XvtRenderFrontend_Glyph(&resized, 0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 2);
	font->inUse = 0;
	XvtRenderFrontend_Glyph(&a, 0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 3);
	XVT_ASSERT_INT_EQ(Writer()->glyph_count, 1);
}

/* A font that is not one of the frontend's font slots is not indexed. */
static void CheckFontOutsideSlots(void)
{
	Fresh();
	static BitmapFont outside;
	memset(&outside, 0, sizeof outside);
	outside.pGlyphBits = g_glyphBits;
	outside.glyphWidth['Q'] = 4;
	outside.glyphHeight['Q'] = 9;
	outside.glyphBitOffset['Q'] = 'Q' * 4;
	outside.inUse = 1;
	g_frontState.fontSlots[0].inUse = 1;
	XvtRenderFrontend_FontLoaded(&outside);
	ImageResource q = GlyphImage(&outside, 'Q');
	XvtRenderFrontend_Glyph(&q, 0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(Writer()->glyph_count, 0);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 1);
}

/* Init lifts suppression and starts the presentation serial over. */
static void CheckInitResets(void)
{
	Fresh();
	XvtRenderFrontend_PresentedScene(XVT_SCENE_FRONTEND);
	XvtRenderFrontend_PresentedScene(XVT_SCENE_FRONTEND);
	uint64_t serial = NextTick()->presentation_serial;
	XvtRenderFrontend_Suppress(1);
	XvtRenderFrontend_Init();
	XvtRenderFrontend_Paint(XVT_PAINT_FILL, 0, 0, 1, 1, 0);
	XVT_ASSERT_INT_EQ(Writer()->paint_count, 1);
	XVT_ASSERT_TRUE(NextTick()->presentation_serial < serial);
}

int main(void)
{
	CheckColorConversion();
	CheckPaintRecord();
	CheckTargetAndSuppression();
	CheckNoTick();
	CheckFullLists();
	CheckImage();
	CheckCopyAndClear();
	CheckDefaultCursor();
	CheckNamedCursor();
	CheckImageWhileDrawingCursor();
	CheckBackBufferHidesCursor();
	CheckCursorRefusals();
	CheckPresentWithSpritesFull();
	CheckPresentScene();
	CheckPresentedScene();
	CheckResetAndRelease();
	CheckScreen();
	CheckMovie();
	CheckTextEntry();
	CheckGlyph();
	CheckFontOutsideSlots();
	CheckInitResets();
	XvtRenderSnapshot_Shutdown();
	return 0;
}

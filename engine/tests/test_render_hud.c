/* Checks the HUD target boxes, the draw scope and the palette colors (xvt_runtime/snapshot/render_hud.h)
 * against the promises in its header. The render snapshot (render_snapshot.h) is started and its ticks
 * opened as the game does, so target boxes have a writer to count drops in. The recovered game's palette,
 * object table and world position globals are set here; no game data is read. */
#include "test_assert.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/render/flight_palette.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static ObjectRecord g_testObjects[3];
static XvtRenderSnapshot* g_out;

/* Two main slots and one static slot, slot 1 empty; a fresh HUD in the cockpit scope; an open tick. */
static void FreshTick(void) {
	memset(g_testObjects, 0, sizeof g_testObjects);
	g_testObjects[0].objectType = 1;
	g_testObjects[0].objectSignature = 0x0A0A;
	g_testObjects[2].objectType = 2;
	g_testObjects[2].objectSignature = 0x0C0C;
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = 2;
	g_regionStaticObjectSlotCount = 1;
	g_worldLocX = 0;
	worldlocy = 0;
	worldlocz = 0;
	XvtRenderSnapshot_Init();
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() != NULL);
	XvtRenderHud_Reset();
}

/* Closes the tick, so no writer is open. */
static void EndTick(void) {
	XvtRenderSnapshot_Commit(0, 1, 0);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() == NULL);
}

/* The number of boxes Publish hands out now. */
static unsigned PublishedCount(void) {
	g_out->target_box_count = 0xFFFF;
	XvtRenderHud_Publish(g_out);
	return g_out->target_box_count;
}

static void CheckScope(void) {
	FreshTick();
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_COCKPIT);
	XvtRenderDraw_Scope(XVT_SCOPE_MAP);
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_MAP);
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_MAP);
	XvtRenderDraw_Scope(XVT_SCOPE_WORLD);
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_WORLD);

	XvtRenderHud_Reset();
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_COCKPIT);
	XvtRenderDraw_Scope(XVT_SCOPE_CRT);
	XvtRenderHud_BeginFrame();
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_COCKPIT);
	EndTick();
}

static void CheckTargetBoxRecord(void) {
	FreshTick();
	g_worldLocX = 1000;
	worldlocy = -2000;
	worldlocz = 3000;
	XvtRenderHud_TargetBox(2, 7, 4096, 33);
	XVT_ASSERT_INT_EQ(PublishedCount(), 1);
	const XvtSnapTargetBox* box = &g_out->target_boxes[0];
	XVT_ASSERT_INT_EQ(box->object.slot, 2);
	XVT_ASSERT_INT_EQ(box->component, 7);
	XVT_ASSERT_INT_EQ(box->extent, 4096);
	XVT_ASSERT_INT_EQ(box->color_index, 33);
	XVT_ASSERT_INT_EQ(box->world_pos[0], 1000);
	XVT_ASSERT_INT_EQ(box->world_pos[1], -2000);
	XVT_ASSERT_INT_EQ(box->world_pos[2], 3000);

	/* Slot 1 is below the slot total but empty: the slot is not checked, so it is kept. */
	XvtRenderHud_TargetBox(1, 0, 1, 1);
	/* Slot 3 is the slot total: not kept. */
	XvtRenderHud_TargetBox(3, 0, 1, 1);
	XVT_ASSERT_INT_EQ(PublishedCount(), 3);
	XVT_ASSERT_INT_EQ(g_out->target_boxes[1].object.slot, 1);
	XVT_ASSERT_TRUE(g_out->target_boxes[2].object.slot != 3);
	EndTick();
}

static void CheckTargetBoxRefusals(void) {
	FreshTick();
	XvtRenderDraw_Scope(XVT_SCOPE_MAP);
	XvtRenderHud_TargetBox(0, 0, 1, 1);
	XvtRenderDraw_Scope(XVT_SCOPE_CRT);
	XvtRenderHud_TargetBox(0, 0, 1, 1);
	XVT_ASSERT_INT_EQ(PublishedCount(), 0);
	XvtRenderDraw_Scope(XVT_SCOPE_WORLD);
	XvtRenderHud_TargetBox(0, 0, 1, 1);
	XVT_ASSERT_INT_EQ(PublishedCount(), 1);

	/* Without an open tick nothing is recorded. */
	EndTick();
	XvtRenderHud_Reset();
	XvtRenderHud_TargetBox(0, 0, 1, 1);
	XVT_ASSERT_INT_EQ(PublishedCount(), 0);
}

static void CheckTargetBoxesFull(void) {
	FreshTick();
	XvtRenderSnapshot* writer = XvtRenderSnapshot_Writer();
	uint32_t dropped = writer->dropped_records;
	for (unsigned i = 0; i < XVT_SNAP_TARGET_BOXES; ++i)
		XvtRenderHud_TargetBox(0, i, (int)i, 1);
	XVT_ASSERT_INT_EQ(writer->dropped_records, dropped);
	XvtRenderHud_TargetBox(0, 9999, 9999, 1);
	XVT_ASSERT_INT_EQ(writer->dropped_records, dropped + 1);
	XVT_ASSERT_INT_EQ(PublishedCount(), XVT_SNAP_TARGET_BOXES);
	for (unsigned i = 0; i < XVT_SNAP_TARGET_BOXES; ++i)
		XVT_ASSERT_INT_EQ(g_out->target_boxes[i].component, i);
	EndTick();
}

static void CheckResetClearsBoxes(void) {
	FreshTick();
	XvtRenderHud_TargetBox(0, 0, 1, 1);
	XvtRenderHud_Reset();
	XVT_ASSERT_INT_EQ(PublishedCount(), 0);
	XvtRenderHud_TargetBox(0, 0, 1, 1);
	XvtRenderHud_TargetBox(2, 0, 1, 1);
	XvtRenderHud_BeginFrame();
	XVT_ASSERT_INT_EQ(PublishedCount(), 0);
	EndTick();
}

static uint32_t Channel(uint32_t argb, int shift) { return (argb >> shift) & 0xFFu; }

static void CheckColor(void) {
	memset(g_swPalette, 0, sizeof g_swPalette);
	g_swPalette[5] = (RgbTriplet) { 63, 0, 32 };
	uint32_t color = XvtRenderDraw_Color(5);
	XVT_ASSERT_INT_EQ(Channel(color, 24), 0xFF);
	XVT_ASSERT_INT_EQ(Channel(color, 16), 0xFF);
	XVT_ASSERT_INT_EQ(Channel(color, 8), 0x00);

	/* Widened to 8 bits: every 6-bit level keeps its value in the top six bits, full scale is 255, and
	 * each channel is read from its own palette byte. */
	for (unsigned level = 0; level < 64; ++level) {
		g_swPalette[9] = (RgbTriplet) { (uint8_t)level, (uint8_t)(63 - level), (uint8_t)level };
		uint32_t argb = XvtRenderDraw_Color(9);
		XVT_ASSERT_INT_EQ(Channel(argb, 24), 0xFF);
		XVT_ASSERT_INT_EQ(Channel(argb, 16) >> 2, level);
		XVT_ASSERT_INT_EQ(Channel(argb, 8) >> 2, 63 - level);
		XVT_ASSERT_INT_EQ(Channel(argb, 0) >> 2, level);
	}
	g_swPalette[9] = (RgbTriplet) { 63, 63, 63 };
	XVT_ASSERT_INT_EQ(XvtRenderDraw_Color(9), 0xFFFFFFFFu);
	g_swPalette[9] = (RgbTriplet) { 0, 0, 0 };
	XVT_ASSERT_INT_EQ(XvtRenderDraw_Color(9), 0xFF000000u);

	/* The index is taken & 255. */
	XVT_ASSERT_INT_EQ(XvtRenderDraw_Color(256 + 5), color);
	XVT_ASSERT_INT_EQ(XvtRenderDraw_Color(0x1205), color);

	/* The classic brightness and the 16-bit palette built for the pixel format do not apply. */
	int brightness = g_flightBrightnessScaleQ8;
	int bytes_per_pixel = g_flightBytesPerPixel;
	g_flightBrightnessScaleQ8 = 17;
	g_flightBytesPerPixel = 4;
	memset(g_flightTextPalette, 0x5A, sizeof g_flightTextPalette);
	XVT_ASSERT_INT_EQ(XvtRenderDraw_Color(5), color);
	g_flightBrightnessScaleQ8 = brightness;
	g_flightBytesPerPixel = bytes_per_pixel;
}

int main(void) {
	g_out = calloc(1, sizeof *g_out);
	XVT_ASSERT_TRUE(g_out != NULL);
	CheckScope();
	CheckTargetBoxRecord();
	CheckTargetBoxRefusals();
	CheckTargetBoxesFull();
	CheckResetClearsBoxes();
	CheckColor();
	XvtRenderSnapshot_Shutdown();
	free(g_out);
	return 0;
}

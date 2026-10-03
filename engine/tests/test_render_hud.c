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

static struct object_record g_test_objects[3];
static struct xvt_render_snapshot *g_out;

/* Two main slots and one static slot, slot 1 empty; a fresh HUD in the cockpit scope; an open tick. */
static void fresh_tick(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	g_test_objects[0].object_type = 1;
	g_test_objects[0].object_signature = 0x0A0A;
	g_test_objects[2].object_type = 2;
	g_test_objects[2].object_signature = 0x0C0C;
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = 2;
	g_region_static_object_slot_count = 1;
	g_world_loc_x = 0;
	g_world_loc_y = 0;
	g_world_loc_z = 0;
	xvt_render_snapshot_init();
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() != NULL);
	xvt_render_hud_reset();
}

/* Closes the tick, so no writer is open. */
static void end_tick(void)
{
	xvt_render_snapshot_commit(0, 1, 0);
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() == NULL);
}

/* The number of boxes Publish hands out now. */
static unsigned published_count(void)
{
	g_out->target_box_count = 0xFFFF;
	xvt_render_hud_publish(g_out);
	return g_out->target_box_count;
}

static void check_scope(void)
{
	fresh_tick();
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_COCKPIT);
	xvt_render_draw_scope(XVT_SCOPE_MAP);
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_MAP);
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_MAP);
	xvt_render_draw_scope(XVT_SCOPE_WORLD);
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_WORLD);

	xvt_render_hud_reset();
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_COCKPIT);
	xvt_render_draw_scope(XVT_SCOPE_CRT);
	xvt_render_hud_begin_frame();
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_COCKPIT);
	end_tick();
}

static void check_target_box_record(void)
{
	fresh_tick();
	g_world_loc_x = 1000;
	g_world_loc_y = -2000;
	g_world_loc_z = 3000;
	xvt_render_hud_target_box(2, 7, 4096, 33);
	XVT_ASSERT_INT_EQ(published_count(), 1);
	const struct xvt_snap_target_box *box = &g_out->target_boxes[0];
	XVT_ASSERT_INT_EQ(box->object.slot, 2);
	XVT_ASSERT_INT_EQ(box->component, 7);
	XVT_ASSERT_INT_EQ(box->extent, 4096);
	XVT_ASSERT_INT_EQ(box->color_index, 33);
	XVT_ASSERT_INT_EQ(box->world_pos[0], 1000);
	XVT_ASSERT_INT_EQ(box->world_pos[1], -2000);
	XVT_ASSERT_INT_EQ(box->world_pos[2], 3000);

	/* Slot 1 is below the slot total but empty: the slot is not checked, so it is kept. */
	xvt_render_hud_target_box(1, 0, 1, 1);
	/* Slot 3 is the slot total: not kept. */
	xvt_render_hud_target_box(3, 0, 1, 1);
	XVT_ASSERT_INT_EQ(published_count(), 3);
	XVT_ASSERT_INT_EQ(g_out->target_boxes[1].object.slot, 1);
	XVT_ASSERT_TRUE(g_out->target_boxes[2].object.slot != 3);
	end_tick();
}

static void check_target_box_refusals(void)
{
	fresh_tick();
	xvt_render_draw_scope(XVT_SCOPE_MAP);
	xvt_render_hud_target_box(0, 0, 1, 1);
	xvt_render_draw_scope(XVT_SCOPE_CRT);
	xvt_render_hud_target_box(0, 0, 1, 1);
	XVT_ASSERT_INT_EQ(published_count(), 0);
	xvt_render_draw_scope(XVT_SCOPE_WORLD);
	xvt_render_hud_target_box(0, 0, 1, 1);
	XVT_ASSERT_INT_EQ(published_count(), 1);

	/* Without an open tick nothing is recorded. */
	end_tick();
	xvt_render_hud_reset();
	xvt_render_hud_target_box(0, 0, 1, 1);
	XVT_ASSERT_INT_EQ(published_count(), 0);
}

static void check_target_boxes_full(void)
{
	fresh_tick();
	struct xvt_render_snapshot *writer = xvt_render_snapshot_writer();
	uint32_t dropped = writer->dropped_records;
	for (unsigned i = 0; i < XVT_SNAP_TARGET_BOXES; ++i) {
		xvt_render_hud_target_box(0, i, (int)i, 1);
	}
	XVT_ASSERT_INT_EQ(writer->dropped_records, dropped);
	xvt_render_hud_target_box(0, 9999, 9999, 1);
	XVT_ASSERT_INT_EQ(writer->dropped_records, dropped + 1);
	XVT_ASSERT_INT_EQ(published_count(), XVT_SNAP_TARGET_BOXES);
	for (unsigned i = 0; i < XVT_SNAP_TARGET_BOXES; ++i) {
		XVT_ASSERT_INT_EQ(g_out->target_boxes[i].component, i);
	}
	end_tick();
}

static void check_reset_clears_boxes(void)
{
	fresh_tick();
	xvt_render_hud_target_box(0, 0, 1, 1);
	xvt_render_hud_reset();
	XVT_ASSERT_INT_EQ(published_count(), 0);
	xvt_render_hud_target_box(0, 0, 1, 1);
	xvt_render_hud_target_box(2, 0, 1, 1);
	xvt_render_hud_begin_frame();
	XVT_ASSERT_INT_EQ(published_count(), 0);
	end_tick();
}

static uint32_t channel(uint32_t argb, int shift)
{
	return (argb >> shift) & 0xFFu;
}

static void check_color(void)
{
	memset(g_sw_palette, 0, sizeof g_sw_palette);
	g_sw_palette[5] = (struct rgb_triplet){63, 0, 32};
	uint32_t color = xvt_render_draw_color(5);
	XVT_ASSERT_INT_EQ(channel(color, 24), 0xFF);
	XVT_ASSERT_INT_EQ(channel(color, 16), 0xFF);
	XVT_ASSERT_INT_EQ(channel(color, 8), 0x00);

	/* Widened to 8 bits: every 6-bit level keeps its value in the top six bits, full scale is 255, and
	 * each channel is read from its own palette byte. */
	for (unsigned level = 0; level < 64; ++level) {
		g_sw_palette[9] = (struct rgb_triplet){
			(uint8_t)level, (uint8_t)(63 - level), (uint8_t)level};
		uint32_t argb = xvt_render_draw_color(9);
		XVT_ASSERT_INT_EQ(channel(argb, 24), 0xFF);
		XVT_ASSERT_INT_EQ(channel(argb, 16) >> 2, level);
		XVT_ASSERT_INT_EQ(channel(argb, 8) >> 2, 63 - level);
		XVT_ASSERT_INT_EQ(channel(argb, 0) >> 2, level);
	}
	g_sw_palette[9] = (struct rgb_triplet){63, 63, 63};
	XVT_ASSERT_INT_EQ(xvt_render_draw_color(9), 0xFFFFFFFFu);
	g_sw_palette[9] = (struct rgb_triplet){0, 0, 0};
	XVT_ASSERT_INT_EQ(xvt_render_draw_color(9), 0xFF000000u);

	/* The index is taken & 255. */
	XVT_ASSERT_INT_EQ(xvt_render_draw_color(256 + 5), color);
	XVT_ASSERT_INT_EQ(xvt_render_draw_color(0x1205), color);

	/* The classic brightness and the 16-bit palette built for the pixel format do not apply. */
	int brightness = g_flight_brightness_scale_q8;
	int bytes_per_pixel = g_flight_bytes_per_pixel;
	g_flight_brightness_scale_q8 = 17;
	g_flight_bytes_per_pixel = 4;
	memset(g_flight_palette16_bpp, 0x5A, sizeof g_flight_palette16_bpp);
	XVT_ASSERT_INT_EQ(xvt_render_draw_color(5), color);
	g_flight_brightness_scale_q8 = brightness;
	g_flight_bytes_per_pixel = bytes_per_pixel;
}

int main(void)
{
	g_out = calloc(1, sizeof *g_out);
	XVT_ASSERT_TRUE(g_out != NULL);
	check_scope();
	check_target_box_record();
	check_target_box_refusals();
	check_target_boxes_full();
	check_reset_clears_boxes();
	check_color();
	xvt_render_snapshot_shutdown();
	free(g_out);
	return 0;
}

/* Checks the render snapshot's slots and ticks (xvt_runtime/snapshot/render_snapshot.h, with Writer and
 * NextOrder from render_capture.h) against the promises in their headers. The capture, frontend and asset
 * modules it drives are the real ones; capture stays inactive except where a check starts a mission. Each
 * check starts from a shut-down snapshot that it initializes again. No game data is read. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

static void fresh(void)
{
	xvt_render_snapshot_shutdown();
	xvt_render_snapshot_init();
}

/* One whole host frame: open, then commit at game time time. */
static const struct xvt_render_snapshot *run_frame(int32_t time)
{
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(time, 1, 0);
	return xvt_render_snapshot_current();
}

static int all_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	for (size_t i = 0; i < size; ++i) {
		if (bytes[i]) {
			return 0;
		}
	}
	return 1;
}

/* Before Init the views are NULL, no tick opens and Commit publishes nothing. */
static void check_before_init(void)
{
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == NULL);
	XVT_ASSERT_TRUE(xvt_render_snapshot_previous() == NULL);
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() == NULL);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 0);
	xvt_render_snapshot_commit(1, 1, 0);
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == NULL);
}

/* The views are NULL until their first publication; the writer is open only inside a tick. */
static void check_first_publication(void)
{
	fresh();
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == NULL);
	XVT_ASSERT_TRUE(xvt_render_snapshot_previous() == NULL);
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() == NULL);
	xvt_render_snapshot_begin_frame();
	struct xvt_render_snapshot *writer = xvt_render_snapshot_writer();
	XVT_ASSERT_TRUE(writer != NULL);
	xvt_render_snapshot_commit(10, 1, 0);
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() == NULL);
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == writer);
	XVT_ASSERT_TRUE(xvt_render_snapshot_previous() == NULL);
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(20, 1, 0);
	XVT_ASSERT_TRUE(xvt_render_snapshot_previous() == writer);
}

/* A commit publishes the writer as current, the old current becomes previous, and the next writer is the
 * slot that is neither. */
static void check_rotation(void)
{
	fresh();
	const struct xvt_render_snapshot *seen[3] = {NULL, NULL, NULL};
	const struct xvt_render_snapshot *current = NULL;
	for (int tick = 0; tick < 7; ++tick) {
		xvt_render_snapshot_begin_frame();
		struct xvt_render_snapshot *writer =
			xvt_render_snapshot_writer();
		XVT_ASSERT_TRUE(writer != NULL);
		XVT_ASSERT_TRUE(writer != xvt_render_snapshot_current());
		XVT_ASSERT_TRUE(writer != xvt_render_snapshot_previous());
		xvt_render_snapshot_commit(tick, 1, 0);
		XVT_ASSERT_TRUE(xvt_render_snapshot_current() == writer);
		XVT_ASSERT_TRUE(xvt_render_snapshot_previous() == current);
		current = writer;
		if (tick < 3) {
			seen[tick] = writer;
		}
	}
	/* Three slots in all. */
	XVT_ASSERT_TRUE(seen[0] != seen[1] && seen[1] != seen[2] &&
			seen[0] != seen[2]);
}

/* Commit stamps game time, host time, focus and pause, and advances the tick index that the next
 * BeginFrame stamps. */
static void check_commit_stamps(void)
{
	fresh();
	const struct xvt_render_snapshot *first = run_frame(1234);
	uint64_t index = first->snapshot_serial;
	uint64_t host = first->capture_host_us;
	XVT_ASSERT_INT_EQ(first->game_time_ticks, 1234);
	XVT_ASSERT_TRUE(first->focused != 0);
	XVT_ASSERT_INT_EQ(first->paused, 0);

	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(-5, 0, 7);
	const struct xvt_render_snapshot *second =
		xvt_render_snapshot_current();
	XVT_ASSERT_INT_EQ(second->game_time_ticks, -5);
	XVT_ASSERT_INT_EQ(second->focused, 0);
	XVT_ASSERT_TRUE(second->paused != 0);
	XVT_ASSERT_INT_EQ(second->snapshot_serial, index + 1);
	XVT_ASSERT_TRUE(second->capture_host_us >= host);
	XVT_ASSERT_INT_EQ(run_frame(0)->snapshot_serial, index + 2);
}

/* Commit runs the asset export into the writer: the built-in cursor that Init registers is listed. */
static void check_commit_exports_assets(void)
{
	fresh();
	const struct xvt_render_snapshot *snapshot = run_frame(0);
	int cursor = 0;
	for (uint32_t i = 0; i < snapshot->image_asset_count; ++i) {
		cursor |= snapshot->image_assets[i].kind ==
			  XVT_IMAGE_BUILTIN_CURSOR;
	}
	XVT_ASSERT_TRUE(cursor);
}

/* BeginFrame zeroes the record counts and the flight, camera, map, hyperspace and cursor flags of the slot
 * it opens; other fields keep what the slot last held. Capture is active here so that Commit leaves the
 * flight view the tick wrote. */
static void check_begin_frame_clears_counts(void)
{
	fresh();
	xvt_render_capture_begin_mission();
	xvt_render_snapshot_begin_frame();
	struct xvt_render_snapshot *slot = xvt_render_snapshot_writer();
	slot->dropped_records = 3;
	slot->flight_valid = 1;
	slot->camera.valid = 1;
	slot->map.active = 1;
	slot->map.object_count = 2;
	slot->map.label_bytes = 9;
	slot->hyperspace.valid = 1;
	slot->hyperspace.count = 4;
	slot->cursor.visible = 1;
	slot->object_count = 5;
	slot->target_box_count = 6;
	slot->sprite_count = 7;
	slot->glyph_count = 8;
	slot->paint_count = 9;
	slot->copy_count = 10;
	slot->surface_event_count = 11;
	slot->preview_count = 2;
	slot->sky.star_grid_divisor = 4321;
	xvt_render_snapshot_commit(0, 1, 0);
	XVT_ASSERT_TRUE(slot->image_asset_count > 0);

	/* Two more ticks, and the slot is the writer again. */
	run_frame(1);
	run_frame(2);
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() == slot);
	XVT_ASSERT_INT_EQ(slot->dropped_records, 0);
	XVT_ASSERT_INT_EQ(slot->flight_valid, 0);
	XVT_ASSERT_INT_EQ(slot->camera.valid, 0);
	XVT_ASSERT_INT_EQ(slot->map.active, 0);
	XVT_ASSERT_INT_EQ(slot->map.object_count, 0);
	XVT_ASSERT_INT_EQ(slot->map.label_bytes, 0);
	XVT_ASSERT_INT_EQ(slot->hyperspace.valid, 0);
	XVT_ASSERT_INT_EQ(slot->hyperspace.count, 0);
	XVT_ASSERT_INT_EQ(slot->cursor.visible, 0);
	XVT_ASSERT_INT_EQ(slot->object_count, 0);
	XVT_ASSERT_INT_EQ(slot->target_box_count, 0);
	XVT_ASSERT_INT_EQ(slot->sprite_count, 0);
	XVT_ASSERT_INT_EQ(slot->glyph_count, 0);
	XVT_ASSERT_INT_EQ(slot->paint_count, 0);
	XVT_ASSERT_INT_EQ(slot->copy_count, 0);
	XVT_ASSERT_INT_EQ(slot->surface_event_count, 0);
	XVT_ASSERT_INT_EQ(slot->preview_count, 0);
	XVT_ASSERT_INT_EQ(slot->opt_asset_count, 0);
	XVT_ASSERT_INT_EQ(slot->texture_asset_count, 0);
	XVT_ASSERT_INT_EQ(slot->image_asset_count, 0);
	XVT_ASSERT_INT_EQ(slot->sky.star_grid_divisor, 4321);
	xvt_render_snapshot_commit(3, 1, 0);
	xvt_render_capture_end_mission();
}

/* BeginFrame does nothing while a frame is open: what the frame wrote stays, and the draw order goes on. */
static void check_begin_frame_idempotent(void)
{
	fresh();
	xvt_render_snapshot_begin_frame();
	struct xvt_render_snapshot *writer = xvt_render_snapshot_writer();
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 0);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 1);
	writer->sprite_count = 3;
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() == writer);
	XVT_ASSERT_INT_EQ(writer->sprite_count, 3);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 2);
	xvt_render_snapshot_commit(0, 1, 0);
}

/* NextOrder counts from 0 in each open tick and returns 0 when none is open. */
static void check_next_order(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 0);
	xvt_render_snapshot_begin_frame();
	for (uint32_t i = 0; i < 5; ++i) {
		XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), i);
	}
	xvt_render_snapshot_commit(0, 1, 0);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 0);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 0);
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 0);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_next_order(), 1);
	xvt_render_snapshot_commit(0, 1, 0);
}

/* The scene kind applies to the open tick and to every later one until it is changed. */
static void check_scene_kind(void)
{
	fresh();
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_writer()->scene_kind,
			  XVT_SCENE_NONE);
	xvt_render_snapshot_set_scene_kind(XVT_SCENE_FRONTEND);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_writer()->scene_kind,
			  XVT_SCENE_FRONTEND);
	xvt_render_snapshot_commit(0, 1, 0);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_current()->scene_kind,
			  XVT_SCENE_FRONTEND);
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_writer()->scene_kind,
			  XVT_SCENE_FRONTEND);
	xvt_render_snapshot_commit(0, 1, 0);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_current()->scene_kind,
			  XVT_SCENE_FRONTEND);

	/* Set between ticks, it applies from the next one. */
	xvt_render_snapshot_set_scene_kind(XVT_SCENE_MOVIE);
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_current()->scene_kind,
			  XVT_SCENE_FRONTEND);
	XVT_ASSERT_INT_EQ(run_frame(0)->scene_kind, XVT_SCENE_MOVIE);
}

/* A second Init before Shutdown does nothing: the published views stay as they were. */
static void check_second_init(void)
{
	fresh();
	const struct xvt_render_snapshot *first = run_frame(77);
	xvt_render_snapshot_init();
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == first);
	XVT_ASSERT_INT_EQ(first->game_time_ticks, 77);
}

/* Shutdown makes the views NULL without clearing the slots, and stops ticks until the next Init, which
 * clears every slot. */
static void check_shutdown(void)
{
	fresh();
	const struct xvt_render_snapshot *slots[3];
	for (int i = 0; i < 3; ++i) {
		slots[i] = run_frame(100 + i);
	}
	xvt_render_snapshot_set_scene_kind(XVT_SCENE_FLIGHT);

	xvt_render_snapshot_shutdown();
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == NULL);
	XVT_ASSERT_TRUE(xvt_render_snapshot_previous() == NULL);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(slots[i]->game_time_ticks, 100 + i);
	}

	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_TRUE(xvt_render_snapshot_writer() == NULL);
	xvt_render_snapshot_commit(5, 1, 0);
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == NULL);

	xvt_render_snapshot_init();
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_TRUE(all_zero(slots[i], sizeof *slots[i]));
	}
	/* Shutdown reset the scene kind. */
	xvt_render_snapshot_begin_frame();
	XVT_ASSERT_INT_EQ(xvt_render_snapshot_writer()->scene_kind,
			  XVT_SCENE_NONE);
	xvt_render_snapshot_commit(0, 1, 0);
}

int main(void)
{
	check_before_init();
	check_first_publication();
	check_rotation();
	check_commit_stamps();
	check_commit_exports_assets();
	check_begin_frame_clears_counts();
	check_begin_frame_idempotent();
	check_next_order();
	check_scene_kind();
	check_second_init();
	check_shutdown();
	xvt_render_snapshot_shutdown();
	return 0;
}

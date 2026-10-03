/* Checks the render snapshot's slots and ticks (xvt_runtime/snapshot/render_snapshot.h, with Writer and
 * NextOrder from render_capture.h) against the promises in their headers. The capture, frontend and asset
 * modules it drives are the real ones; capture stays inactive except where a check starts a mission. Each
 * check starts from a shut-down snapshot that it initializes again. No game data is read. */
#include "test_assert.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void Fresh(void)
{
	XvtRenderSnapshot_Shutdown();
	XvtRenderSnapshot_Init();
}

/* One whole host frame: open, then commit at game time time. */
static const struct XvtRenderSnapshot *RunFrame(int32_t time)
{
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(time, 1, 0);
	return XvtRenderSnapshot_Current();
}

static int AllZero(const void *data, size_t size)
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
static void CheckBeforeInit(void)
{
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == NULL);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Previous() == NULL);
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() == NULL);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 0);
	XvtRenderSnapshot_Commit(1, 1, 0);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == NULL);
}

/* The views are NULL until their first publication; the writer is open only inside a tick. */
static void CheckFirstPublication(void)
{
	Fresh();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == NULL);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Previous() == NULL);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() == NULL);
	XvtRenderSnapshot_BeginFrame();
	struct XvtRenderSnapshot *writer = XvtRenderSnapshot_Writer();
	XVT_ASSERT_TRUE(writer != NULL);
	XvtRenderSnapshot_Commit(10, 1, 0);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() == NULL);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == writer);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Previous() == NULL);
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(20, 1, 0);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Previous() == writer);
}

/* A commit publishes the writer as current, the old current becomes previous, and the next writer is the
 * slot that is neither. */
static void CheckRotation(void)
{
	Fresh();
	const struct XvtRenderSnapshot *seen[3] = {NULL, NULL, NULL};
	const struct XvtRenderSnapshot *current = NULL;
	for (int tick = 0; tick < 7; ++tick) {
		XvtRenderSnapshot_BeginFrame();
		struct XvtRenderSnapshot *writer = XvtRenderSnapshot_Writer();
		XVT_ASSERT_TRUE(writer != NULL);
		XVT_ASSERT_TRUE(writer != XvtRenderSnapshot_Current());
		XVT_ASSERT_TRUE(writer != XvtRenderSnapshot_Previous());
		XvtRenderSnapshot_Commit(tick, 1, 0);
		XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == writer);
		XVT_ASSERT_TRUE(XvtRenderSnapshot_Previous() == current);
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
static void CheckCommitStamps(void)
{
	Fresh();
	const struct XvtRenderSnapshot *first = RunFrame(1234);
	uint64_t index = first->snapshot_serial;
	uint64_t host = first->capture_host_us;
	XVT_ASSERT_INT_EQ(first->game_time_ticks, 1234);
	XVT_ASSERT_TRUE(first->focused != 0);
	XVT_ASSERT_INT_EQ(first->paused, 0);

	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(-5, 0, 7);
	const struct XvtRenderSnapshot *second = XvtRenderSnapshot_Current();
	XVT_ASSERT_INT_EQ(second->game_time_ticks, -5);
	XVT_ASSERT_INT_EQ(second->focused, 0);
	XVT_ASSERT_TRUE(second->paused != 0);
	XVT_ASSERT_INT_EQ(second->snapshot_serial, index + 1);
	XVT_ASSERT_TRUE(second->capture_host_us >= host);
	XVT_ASSERT_INT_EQ(RunFrame(0)->snapshot_serial, index + 2);
}

/* Commit runs the asset export into the writer: the built-in cursor that Init registers is listed. */
static void CheckCommitExportsAssets(void)
{
	Fresh();
	const struct XvtRenderSnapshot *snapshot = RunFrame(0);
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
static void CheckBeginFrameClearsCounts(void)
{
	Fresh();
	XvtRenderCapture_BeginMission();
	XvtRenderSnapshot_BeginFrame();
	struct XvtRenderSnapshot *slot = XvtRenderSnapshot_Writer();
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
	XvtRenderSnapshot_Commit(0, 1, 0);
	XVT_ASSERT_TRUE(slot->image_asset_count > 0);

	/* Two more ticks, and the slot is the writer again. */
	RunFrame(1);
	RunFrame(2);
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() == slot);
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
	XvtRenderSnapshot_Commit(3, 1, 0);
	XvtRenderCapture_EndMission();
}

/* BeginFrame does nothing while a frame is open: what the frame wrote stays, and the draw order goes on. */
static void CheckBeginFrameIdempotent(void)
{
	Fresh();
	XvtRenderSnapshot_BeginFrame();
	struct XvtRenderSnapshot *writer = XvtRenderSnapshot_Writer();
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 0);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 1);
	writer->sprite_count = 3;
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() == writer);
	XVT_ASSERT_INT_EQ(writer->sprite_count, 3);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 2);
	XvtRenderSnapshot_Commit(0, 1, 0);
}

/* NextOrder counts from 0 in each open tick and returns 0 when none is open. */
static void CheckNextOrder(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 0);
	XvtRenderSnapshot_BeginFrame();
	for (uint32_t i = 0; i < 5; ++i) {
		XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), i);
	}
	XvtRenderSnapshot_Commit(0, 1, 0);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 0);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 0);
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 0);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_NextOrder(), 1);
	XvtRenderSnapshot_Commit(0, 1, 0);
}

/* The scene kind applies to the open tick and to every later one until it is changed. */
static void CheckSceneKind(void)
{
	Fresh();
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_Writer()->scene_kind,
			  XVT_SCENE_NONE);
	XvtRenderSnapshot_SetSceneKind(XVT_SCENE_FRONTEND);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_Writer()->scene_kind,
			  XVT_SCENE_FRONTEND);
	XvtRenderSnapshot_Commit(0, 1, 0);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_Current()->scene_kind,
			  XVT_SCENE_FRONTEND);
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_Writer()->scene_kind,
			  XVT_SCENE_FRONTEND);
	XvtRenderSnapshot_Commit(0, 1, 0);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_Current()->scene_kind,
			  XVT_SCENE_FRONTEND);

	/* Set between ticks, it applies from the next one. */
	XvtRenderSnapshot_SetSceneKind(XVT_SCENE_MOVIE);
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_Current()->scene_kind,
			  XVT_SCENE_FRONTEND);
	XVT_ASSERT_INT_EQ(RunFrame(0)->scene_kind, XVT_SCENE_MOVIE);
}

/* A second Init before Shutdown does nothing: the published views stay as they were. */
static void CheckSecondInit(void)
{
	Fresh();
	const struct XvtRenderSnapshot *first = RunFrame(77);
	XvtRenderSnapshot_Init();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == first);
	XVT_ASSERT_INT_EQ(first->game_time_ticks, 77);
}

/* Shutdown makes the views NULL without clearing the slots, and stops ticks until the next Init, which
 * clears every slot. */
static void CheckShutdown(void)
{
	Fresh();
	const struct XvtRenderSnapshot *slots[3];
	for (int i = 0; i < 3; ++i) {
		slots[i] = RunFrame(100 + i);
	}
	XvtRenderSnapshot_SetSceneKind(XVT_SCENE_FLIGHT);

	XvtRenderSnapshot_Shutdown();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == NULL);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Previous() == NULL);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(slots[i]->game_time_ticks, 100 + i);
	}

	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Writer() == NULL);
	XvtRenderSnapshot_Commit(5, 1, 0);
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == NULL);

	XvtRenderSnapshot_Init();
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_TRUE(AllZero(slots[i], sizeof *slots[i]));
	}
	/* Shutdown reset the scene kind. */
	XvtRenderSnapshot_BeginFrame();
	XVT_ASSERT_INT_EQ(XvtRenderSnapshot_Writer()->scene_kind,
			  XVT_SCENE_NONE);
	XvtRenderSnapshot_Commit(0, 1, 0);
}

int main(void)
{
	CheckBeforeInit();
	CheckFirstPublication();
	CheckRotation();
	CheckCommitStamps();
	CheckCommitExportsAssets();
	CheckBeginFrameClearsCounts();
	CheckBeginFrameIdempotent();
	CheckNextOrder();
	CheckSceneKind();
	CheckSecondInit();
	CheckShutdown();
	XvtRenderSnapshot_Shutdown();
	return 0;
}

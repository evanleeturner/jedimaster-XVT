/* Checks the flight-view capture (xvt_runtime/snapshot/render_capture.h)
 * against the promises in its header. The render snapshot runs as the game runs
 * it (its Init starts the capture, each frame's BeginFrame and Commit call into
 * it), and the recovered game's object table, memory handle, local player,
 * viewport and game time are set here; no game data is read. Each check starts
 * from four object slots (three main, one static; slot 1 empty), a 320x200
 * viewport on a 640x480 screen, and game time 100.
 *
 * Not checked: Crt and CrtMarker, whose preview reaches a snapshot only through
 * the cockpit's sealed composition, which needs a captured cockpit and a
 * refreshed instrument state; and a preview made valid by a registered model,
 * which needs a model file resolved through storage. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/renderer.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#include "xvt_runtime/timing/flight_timing.h"

enum { TABLE_HANDLE = 5 };

static struct object_record g_test_objects[4];

static void use_objects(struct object_record *objects, size_t count,
			int main_end, int static_count)
{
	g_object_table = objects;
	g_object_table_handle = TABLE_HANDLE;
	g_handle_tables.ptr_table[TABLE_HANDLE - 1] = objects;
	g_handle_tables.size_table[TABLE_HANDLE - 1] =
		count * sizeof(struct object_record);
	g_region_main_object_slot_end = main_end;
	g_region_static_object_slot_count = static_count;
}

/* A fresh snapshot and capture, capture inactive, timing native, no tick open. */
static void fresh(void)
{
	xvt_render_snapshot_shutdown();
	xvt_flight_timing_end_session();
	memset(g_test_objects, 0, sizeof g_test_objects);
	g_test_objects[0].object_type = 1;
	g_test_objects[0].object_signature = 0x0A00;
	g_test_objects[2].object_type = 2;
	g_test_objects[2].object_signature = 0x0A02;
	g_test_objects[3].object_type = 3;
	g_test_objects[3].object_signature = 0x0A03;
	use_objects(g_test_objects, 4, 3, 1);
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	memset(g_players, 0, sizeof g_players);
	g_local_player = 0;
	g_players[0].current_target_object_idx = -1;
	g_flight_vp_x = 0;
	g_flight_vp_y = 0;
	g_flight_vp_width = 320;
	g_flight_vp_height = 200;
	g_screen_width = 640;
	g_screen_height = 480;
	g_game_time = 100;
	xvt_render_snapshot_init();
}

/* Fresh, with capture active and a tick open. */
static void fresh_mission(void)
{
	fresh();
	xvt_render_capture_begin_mission();
	xvt_render_snapshot_begin_frame();
}

static struct xvt_render_snapshot *writer(void)
{
	return xvt_render_snapshot_writer();
}

/* Commits the open tick; returns the snapshot just committed. */
static const struct xvt_render_snapshot *commit(void)
{
	xvt_render_snapshot_commit(g_game_time, 1, 0);
	return xvt_render_snapshot_current();
}

/* Commits the open tick and opens the next; returns the snapshot just committed. */
static const struct xvt_render_snapshot *next_tick(void)
{
	const struct xvt_render_snapshot *committed = commit();
	xvt_render_snapshot_begin_frame();
	return committed;
}

/* One classic frame whose flip succeeds. */
static void publish_view(void)
{
	xvt_render_capture_capture_view();
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(1);
}

static int has_object(const struct xvt_render_snapshot *s, unsigned slot,
		      uint16_t signature)
{
	for (uint32_t i = 0; i < s->object_count; ++i) {
		if (s->objects[i].id.slot == slot &&
		    s->objects[i].id.signature == signature) {
			return 1;
		}
	}
	return 0;
}

/* After Init there is no last view, and while capture is inactive no flight view is published. */
static void check_inactive(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), -1);
	xvt_render_snapshot_begin_frame();
	publish_view();
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 0);
	const struct xvt_render_snapshot *s = commit();
	XVT_ASSERT_INT_EQ(s->flight_valid, 0);
	XVT_ASSERT_INT_EQ(s->camera.valid, 0);
	XVT_ASSERT_INT_EQ(s->object_count, 0);
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), -1);
}

/* A sealed view presented by a successful flip reaches the writer: each
 * occupied slot, a valid camera, and a raised flight frame serial; its game
 * time becomes the last view tick. */
static void check_published_view(void)
{
	fresh_mission();
	publish_view();
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 1);
	const struct xvt_render_snapshot *s = next_tick();
	XVT_ASSERT_INT_EQ(s->flight_valid, 1);
	XVT_ASSERT_INT_EQ(s->camera.valid, 1);
	XVT_ASSERT_INT_EQ(s->object_count, 3);
	XVT_ASSERT_TRUE(has_object(s, 0, 0x0A00));
	XVT_ASSERT_TRUE(has_object(s, 2, 0x0A02));
	XVT_ASSERT_TRUE(has_object(s, 3, 0x0A03));
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), 100);
	uint64_t serial = s->flight_frame_serial;

	g_game_time = 104;
	publish_view();
	s = next_tick();
	XVT_ASSERT_TRUE(s->flight_frame_serial > serial);
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), 104);
}

/* Slots past the object table's capacity are not captured. */
static void check_table_capacity(void)
{
	fresh_mission();
	g_handle_tables.size_table[TABLE_HANDLE - 1] =
		3 * sizeof(struct object_record);
	publish_view();
	XVT_ASSERT_INT_EQ(writer()->object_count, 2);
	XVT_ASSERT_TRUE(!has_object(writer(), 3, 0x0A03));
}

/* Returns 1 when one classic frame publishes a flight view into the writer. */
static int publishes(void)
{
	writer()->flight_valid = 0;
	publish_view();
	return writer()->flight_valid;
}

/* The view is left invalid when the table is missing or stale, a slot range is
 * negative, the local player index is out of range, or a viewport or screen
 * size is 0. */
static void check_refused_views(void)
{
	fresh_mission();
	XVT_ASSERT_TRUE(publishes());
	g_object_table = NULL;
	XVT_ASSERT_TRUE(!publishes());
	use_objects(g_test_objects, 4, 3, 1);
	g_handle_tables.ptr_table[TABLE_HANDLE - 1] = &g_test_objects[1];
	XVT_ASSERT_TRUE(!publishes());
	use_objects(g_test_objects, 4, 3, 1);
	g_object_table_handle = 0;
	XVT_ASSERT_TRUE(!publishes());
	use_objects(g_test_objects, 4, -1, 1);
	XVT_ASSERT_TRUE(!publishes());
	use_objects(g_test_objects, 4, 3, -1);
	XVT_ASSERT_TRUE(!publishes());
	use_objects(g_test_objects, 4, 3, 1);
	g_local_player = 8;
	XVT_ASSERT_TRUE(!publishes());
	g_local_player = -1;
	XVT_ASSERT_TRUE(!publishes());
	g_local_player = 0;
	XVT_ASSERT_TRUE(publishes());

	uint16_t *sizes16[2] = {&g_flight_vp_width, &g_flight_vp_height};
	for (int i = 0; i < 2; ++i) {
		uint16_t kept = *sizes16[i];
		*sizes16[i] = 0;
		XVT_ASSERT_TRUE(!publishes());
		*sizes16[i] = kept;
	}
	unsigned *sizes[2] = {&g_screen_width, &g_screen_height};
	for (int i = 0; i < 2; ++i) {
		unsigned kept = *sizes[i];
		*sizes[i] = 0;
		XVT_ASSERT_TRUE(!publishes());
		*sizes[i] = kept;
	}
	XVT_ASSERT_TRUE(publishes());
}

/* capture_view sets the draw scope to world, or to map when the local player's map is open. */
static void check_draw_scope(void)
{
	fresh_mission();
	xvt_render_draw_scope(XVT_SCOPE_COCKPIT);
	xvt_render_capture_capture_view();
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_WORLD);
	g_players[0].map_camera_state = 1;
	xvt_render_capture_capture_view();
	XVT_ASSERT_INT_EQ(xvt_render_draw_scope_current(), XVT_SCOPE_MAP);
}

/* Objects past XVT_SNAP_OBJECTS and model types with an unsupported frame
 * sequence or palette count as dropped records in the writer when the view is
 * presented. */
static void check_dropped_records(void)
{
	fresh_mission();
	publish_view();
	uint32_t baseline = writer()->dropped_records;
	next_tick();

	size_t count = XVT_SNAP_OBJECTS + 36;
	struct object_record *objects = calloc(count, sizeof *objects);
	XVT_ASSERT_TRUE(objects != NULL);
	for (size_t i = 0; i < count; ++i) {
		objects[i].object_type = 1;
		objects[i].object_signature = (uint16_t)i;
	}
	use_objects(objects, count, (int)count, 0);
	publish_view();
	XVT_ASSERT_INT_EQ(writer()->object_count, XVT_SNAP_OBJECTS);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, baseline + 36);
	next_tick();

	/* Two model types with neither a frame sequence nor a palette get ones
	 * the capture does not know. */
	unsigned plain[2];
	unsigned found = 0;
	for (unsigned t = 0; t < XVT_SNAP_TYPES && found < 2; ++t) {
		if (!g_object_type_table[t].texture_frame_sequence &&
		    !g_object_type_table[t].palette) {
			plain[found++] = t;
		}
	}
	XVT_ASSERT_INT_EQ(found, 2);
	static int16_t sequence[3] = {1, 2, 3};
	static uint8_t palette[16];
	g_object_type_table[plain[0]].texture_frame_sequence = sequence;
	g_object_type_table[plain[1]].palette = palette;
	use_objects(g_test_objects, 4, 3, 1);
	publish_view();
	XVT_ASSERT_INT_EQ(writer()->dropped_records, baseline + 2);
	g_object_type_table[plain[0]].texture_frame_sequence = NULL;
	g_object_type_table[plain[1]].palette = NULL;
	free(objects);
	use_objects(g_test_objects, 4, 3, 1);
}

/* A failed flip publishes nothing and leaves the sealed view for a later successful one; a later
 * successful flip in the same tick replaces the view, a failed one does not. */
static void check_flips(void)
{
	fresh_mission();
	xvt_render_capture_capture_view();
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(0);
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 0);
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 1);
	XVT_ASSERT_TRUE(has_object(writer(), 2, 0x0A02));

	g_test_objects[2].object_signature = 0x0B02;
	xvt_render_capture_capture_view();
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(0);
	XVT_ASSERT_TRUE(has_object(writer(), 2, 0x0A02));
	xvt_render_capture_capture_view();
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(1);
	XVT_ASSERT_TRUE(has_object(writer(), 2, 0x0B02));
	XVT_ASSERT_TRUE(!has_object(writer(), 2, 0x0A02));
}

/* EndPresentation discards the pending view, sealed or not, and so does BeginFrame. */
static void check_discarded_views(void)
{
	fresh_mission();
	xvt_render_capture_capture_view();
	xvt_render_capture_seal_view();
	xvt_render_capture_end_presentation();
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 0);

	xvt_render_capture_capture_view();
	xvt_render_capture_seal_view();
	xvt_render_capture_begin_frame();
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 0);
}

/* A presented view carries the HUD's target boxes into the writer;
 * BeginClassicFrame begins a new HUD frame, which clears them. */
static void check_hud_boxes(void)
{
	fresh_mission();
	xvt_render_capture_capture_view();
	xvt_render_hud_target_box(0, 1, 100, 5);
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(writer()->target_box_count, 1);

	xvt_render_capture_begin_classic_frame();
	publish_view();
	XVT_ASSERT_INT_EQ(writer()->target_box_count, 0);
}

/* Hyperspace copies the streaks into the pending view, and nothing when there are more than
 * XVT_SNAP_STREAKS. */
static void check_hyperspace(void)
{
	fresh_mission();
	xvt_render_capture_capture_view();
	const int x[3] = {1, 2, 3};
	const int y[3] = {-4, -5, -6};
	const int z[3] = {70, 80, 90};
	const int width[3] = {9, 8, 7};
	const int roll[3] = {100, 200, 300};
	xvt_render_capture_hyperspace(3, x, y, z, width, roll);
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(1);
	const struct xvt_snap_hyperspace *h = &writer()->hyperspace;
	XVT_ASSERT_INT_EQ(h->count, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(h->streaks[i].offset[0], x[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].offset[1], y[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].offset[2], z[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].half_width, width[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].roll, roll[i]);
	}

	xvt_render_capture_capture_view();
	xvt_render_capture_hyperspace(XVT_SNAP_STREAKS + 1, x, y, z, width,
				      roll);
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(writer()->hyperspace.count, 0);
}

/* With no view published in a tick, Commit carries the previous snapshot's view
 * forward when it has the same mission and world generation; otherwise the
 * snapshot has no flight view. */
static void check_carry_forward(void)
{
	fresh_mission();
	xvt_render_capture_capture_view();
	xvt_render_hud_target_box(2, 0, 50, 1);
	xvt_render_capture_seal_view();
	xvt_render_capture_presented(1);
	const struct xvt_render_snapshot *published = next_tick();
	uint64_t mission = published->mission_generation;
	uint64_t world = published->world_generation;

	const struct xvt_render_snapshot *carried = next_tick();
	XVT_ASSERT_INT_EQ(carried->flight_valid, 1);
	XVT_ASSERT_INT_EQ(carried->object_count, 3);
	XVT_ASSERT_TRUE(has_object(carried, 2, 0x0A02));
	XVT_ASSERT_INT_EQ(carried->target_box_count, 1);
	XVT_ASSERT_INT_EQ(carried->mission_generation, mission);
	XVT_ASSERT_INT_EQ(carried->world_generation, world);

	xvt_render_capture_world_changed();
	const struct xvt_render_snapshot *after = next_tick();
	XVT_ASSERT_INT_EQ(after->flight_valid, 0);
	XVT_ASSERT_TRUE(after->world_generation > world);

	/* A new mission, then capture ends: nothing is carried. */
	publish_view();
	next_tick();
	xvt_render_capture_end_mission();
	const struct xvt_render_snapshot *ended = next_tick();
	XVT_ASSERT_INT_EQ(ended->flight_valid, 0);
	XVT_ASSERT_INT_EQ(ended->camera.valid, 0);
}

/* The mission generation rises on BeginMission; the world generation on every
 * world change and on a view whose game time went backward. WorldChanged
 * forgets the last view time and clears the writer's flight and camera
 * validity. */
static void check_generations(void)
{
	fresh_mission();
	const struct xvt_render_snapshot *s = next_tick();
	uint64_t mission = s->mission_generation;
	uint64_t world = s->world_generation;

	xvt_render_capture_begin_mission();
	s = next_tick();
	XVT_ASSERT_TRUE(s->mission_generation > mission);
	XVT_ASSERT_TRUE(s->world_generation > world);
	mission = s->mission_generation;
	world = s->world_generation;

	publish_view();
	s = next_tick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);
	g_game_time = 50;
	publish_view();
	s = next_tick();
	XVT_ASSERT_TRUE(s->world_generation > world);
	XVT_ASSERT_INT_EQ(s->mission_generation, mission);
	world = s->world_generation;

	publish_view();
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 1);
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), 50);
	xvt_render_capture_world_changed();
	XVT_ASSERT_INT_EQ(writer()->flight_valid, 0);
	XVT_ASSERT_INT_EQ(writer()->camera.valid, 0);
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), -1);
	s = next_tick();
	XVT_ASSERT_TRUE(s->world_generation > world);

	world = s->world_generation;
	xvt_render_capture_end_mission();
	s = next_tick();
	XVT_ASSERT_TRUE(s->world_generation > world);
}

/* Network profile: a slot's pose that changed at the authoritative tick flags a correction, which
 * CompleteNetworkWorld applies as a world change. */
static void check_network_correction(void)
{
	fresh_mission();
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_NETWORK_125);
	g_game_time = 10;
	xvt_render_capture_complete_network_world();
	publish_view();
	const struct xvt_render_snapshot *s = next_tick();
	uint64_t world = s->world_generation;

	/* Nothing moved: no correction. */
	xvt_render_capture_check_network_correction();
	xvt_render_capture_complete_network_world();
	s = next_tick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);

	/* A move seen at another game time is not compared. */
	g_test_objects[2].world_x += 5;
	g_game_time = 11;
	xvt_render_capture_check_network_correction();
	g_game_time = 10;
	xvt_render_capture_complete_network_world();
	s = next_tick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);

	/* The move is seen at the authoritative tick, which the presented view at time 10 set. */
	g_test_objects[2].world_x -= 5;
	xvt_render_capture_complete_network_world();
	publish_view();
	next_tick();
	g_test_objects[2].yaw = 0x1234;
	xvt_render_capture_check_network_correction();
	xvt_render_capture_complete_network_world();
	s = next_tick();
	XVT_ASSERT_TRUE(s->world_generation > world);
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), -1);
	xvt_flight_timing_end_session();
}

/* Outside the network profile neither call changes the world. */
static void check_network_only(void)
{
	fresh_mission();
	g_game_time = 10;
	xvt_render_capture_complete_network_world();
	publish_view();
	const struct xvt_render_snapshot *s = next_tick();
	uint64_t world = s->world_generation;
	g_test_objects[2].world_x += 5;
	xvt_render_capture_check_network_correction();
	xvt_render_capture_complete_network_world();
	s = next_tick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);
	XVT_ASSERT_INT_EQ(xvt_render_capture_last_view_tick(), 10);
}

/* A local transient slot is left out of the comparison. */
static void check_network_skips_local_slots(void)
{
	fresh_mission();
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_NETWORK_125);
	g_local_transient_slot_start = 1;
	g_local_debris_slot_end = 2;
	g_test_objects[1].object_type = 4;
	g_test_objects[1].object_signature = 0x0A01;
	g_game_time = 10;
	xvt_render_capture_complete_network_world();
	publish_view();
	uint64_t world = next_tick()->world_generation;
	g_test_objects[1].world_z += 1000;
	xvt_render_capture_check_network_correction();
	xvt_render_capture_complete_network_world();
	XVT_ASSERT_INT_EQ(next_tick()->world_generation, world);
	xvt_flight_timing_end_session();
}

/* Init does not reset the network pose history: a pose recorded before it still
 * flags a correction. */
static void check_init_keeps_pose_history(void)
{
	fresh_mission();
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_NETWORK_125);
	g_game_time = 10;
	xvt_render_capture_complete_network_world();
	publish_view();
	next_tick();
	xvt_render_capture_reset();
	uint64_t world = next_tick()->world_generation;
	g_test_objects[0].roll = 0x0400;
	xvt_render_capture_check_network_correction();
	xvt_render_capture_complete_network_world();
	XVT_ASSERT_TRUE(next_tick()->world_generation > world);
	xvt_flight_timing_end_session();
}

/* Inside an overlay, a successful flip without a sealed view presents the
 * flight scene; outside one it does nothing. End never drops below zero. */
static void check_overlay(void)
{
	fresh_mission();
	uint64_t serial = next_tick()->presentation_serial;
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(next_tick()->presentation_serial, serial);

	xvt_render_capture_begin_overlay();
	xvt_render_capture_begin_overlay();
	xvt_render_capture_end_overlay();
	xvt_render_capture_presented(1);
	const struct xvt_render_snapshot *s = next_tick();
	XVT_ASSERT_TRUE(s->presentation_serial > serial);
	XVT_ASSERT_INT_EQ(s->presented_scene, XVT_SCENE_FLIGHT);
	serial = s->presentation_serial;

	/* A failed flip does nothing, inside the overlay too. */
	xvt_render_capture_presented(0);
	XVT_ASSERT_INT_EQ(next_tick()->presentation_serial, serial);

	xvt_render_capture_end_overlay();
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(next_tick()->presentation_serial, serial);

	/* Ends past zero leave it at zero: one Begin and one End later the flip
	 * is outside again. */
	xvt_render_capture_end_overlay();
	xvt_render_capture_end_overlay();
	xvt_render_capture_begin_overlay();
	xvt_render_capture_presented(1);
	s = next_tick();
	XVT_ASSERT_TRUE(s->presentation_serial > serial);
	serial = s->presentation_serial;
	xvt_render_capture_end_overlay();
	xvt_render_capture_presented(1);
	XVT_ASSERT_INT_EQ(next_tick()->presentation_serial, serial);
}

/* FrontendPreview appends straight to the writer with a 640x480 camera; it is
 * valid only when its handle has an asset id. An empty rectangle or a bad local
 * player index adds nothing; a full list counts a dropped record. */
static void check_frontend_preview(void)
{
	fresh();
	xvt_render_snapshot_begin_frame();
	const float position[3] = {1.5f, -2.5f, 300.0f};
	const float orientation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	xvt_render_capture_frontend_preview(9, position, orientation, 2.5f, 3,
					    10, 20, 100, 80);
	XVT_ASSERT_INT_EQ(writer()->preview_count, 1);
	const struct xvt_snap_preview *p = &writer()->previews[0];
	XVT_ASSERT_INT_EQ(p->valid, 0);
	XVT_ASSERT_INT_EQ(p->camera.screen_width, 640);
	XVT_ASSERT_INT_EQ(p->camera.screen_height, 480);
	XVT_ASSERT_INT_EQ(p->destination.x, 10);
	XVT_ASSERT_INT_EQ(p->destination.y, 20);
	XVT_ASSERT_INT_EQ(p->destination.width, 100);
	XVT_ASSERT_INT_EQ(p->destination.height, 80);
	XVT_ASSERT_INT_EQ(p->node_switch, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_CLOSE(p->view_pos[i], position[i], 0,
				 "a float copied as it is");
	}
	for (int i = 0; i < 9; ++i) {
		XVT_ASSERT_CLOSE(p->view_orient[i], orientation[i], 0,
				 "a float copied as it is");
	}
	XVT_ASSERT_CLOSE(p->model_scale, 2.5, 0, "a float copied as it is");

	xvt_render_capture_frontend_preview(9, position, orientation, 1, 0, 0,
					    0, 0, 80);
	xvt_render_capture_frontend_preview(9, position, orientation, 1, 0, 0,
					    0, 100, -1);
	g_local_player = 8;
	xvt_render_capture_frontend_preview(9, position, orientation, 1, 0, 0,
					    0, 100, 80);
	g_local_player = 0;
	XVT_ASSERT_INT_EQ(writer()->preview_count, 1);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 0);

	for (int i = 1; i < XVT_SNAP_PREVIEWS; ++i) {
		xvt_render_capture_frontend_preview(9, position, orientation, 1,
						    0, 0, 0, 100, 80);
	}
	XVT_ASSERT_INT_EQ(writer()->preview_count, XVT_SNAP_PREVIEWS);
	xvt_render_capture_frontend_preview(9, position, orientation, 1, 0, 0,
					    0, 100, 80);
	XVT_ASSERT_INT_EQ(writer()->preview_count, XVT_SNAP_PREVIEWS);
	XVT_ASSERT_INT_EQ(writer()->dropped_records, 1);

	/* Without an open tick nothing is added. */
	const struct xvt_render_snapshot *s = commit();
	xvt_render_capture_frontend_preview(9, position, orientation, 1, 0, 0,
					    0, 100, 80);
	XVT_ASSERT_INT_EQ(s->preview_count, XVT_SNAP_PREVIEWS);
	XVT_ASSERT_INT_EQ(s->dropped_records, 1);
}

int main(void)
{
	check_inactive();
	check_published_view();
	check_table_capacity();
	check_refused_views();
	check_draw_scope();
	check_dropped_records();
	check_flips();
	check_discarded_views();
	check_hud_boxes();
	check_hyperspace();
	check_carry_forward();
	check_generations();
	check_network_correction();
	check_network_only();
	check_network_skips_local_slots();
	check_init_keeps_pose_history();
	check_overlay();
	check_frontend_preview();
	xvt_render_snapshot_shutdown();
	return 0;
}

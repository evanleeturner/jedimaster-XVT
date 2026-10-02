/* Checks the flight-view capture (xvt_runtime/snapshot/render_capture.h) against the promises in its
 * header. The render snapshot runs as the game runs it (its Init starts the capture, each frame's BeginFrame
 * and Commit call into it), and the recovered game's object table, memory handle, local player, viewport
 * and game time are set here; no game data is read. Each check starts from four object slots (three main,
 * one static; slot 1 empty), a 320x200 viewport on a 640x480 screen, and game time 100.
 *
 * Not checked: Crt and CrtMarker, whose preview reaches a snapshot only through the cockpit's sealed
 * composition, which needs a captured cockpit and a refreshed instrument state; and a preview made valid
 * by a registered model, which needs a model file resolved through storage. */
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { TABLE_HANDLE = 5 };

static ObjectRecord g_testObjects[4];

static void UseObjects(ObjectRecord* objects, size_t count, int main_end, int static_count) {
	g_objectTable = objects;
	g_objectTableHandle = TABLE_HANDLE;
	g_handleTables.ptrTable[TABLE_HANDLE - 1] = objects;
	g_handleTables.sizeTable[TABLE_HANDLE - 1] = count * sizeof(ObjectRecord);
	g_regionMainObjectSlotEnd = main_end;
	g_regionStaticObjectSlotCount = static_count;
}

/* A fresh snapshot and capture, capture inactive, timing native, no tick open. */
static void Fresh(void) {
	XvtRenderSnapshot_Shutdown();
	XvtFlightTiming_EndSession();
	memset(g_testObjects, 0, sizeof g_testObjects);
	g_testObjects[0].objectType = 1;
	g_testObjects[0].objectSignature = 0x0A00;
	g_testObjects[2].objectType = 2;
	g_testObjects[2].objectSignature = 0x0A02;
	g_testObjects[3].objectType = 3;
	g_testObjects[3].objectSignature = 0x0A03;
	UseObjects(g_testObjects, 4, 3, 1);
	g_localTransientSlotStart = 0;
	g_localDebrisSlotEnd = 0;
	memset(g_players, 0, sizeof g_players);
	g_localPlayer = 0;
	g_players[0].currentTargetObjectIdx = -1;
	g_flightVpX = 0;
	g_flightVpY = 0;
	g_flightVpWidth = 320;
	g_flightVpHeight = 200;
	g_screenWidth = 640;
	g_screenHeight = 480;
	g_gameTime = 100;
	XvtRenderSnapshot_Init();
}

/* Fresh, with capture active and a tick open. */
static void FreshMission(void) {
	Fresh();
	XvtRenderCapture_BeginMission();
	XvtRenderSnapshot_BeginFrame();
}

static XvtRenderSnapshot* Writer(void) { return XvtRenderSnapshot_Writer(); }

/* Commits the open tick; returns the snapshot just committed. */
static const XvtRenderSnapshot* Commit(void) {
	XvtRenderSnapshot_Commit(g_gameTime, 1, 0);
	return XvtRenderSnapshot_Current();
}

/* Commits the open tick and opens the next; returns the snapshot just committed. */
static const XvtRenderSnapshot* NextTick(void) {
	const XvtRenderSnapshot* committed = Commit();
	XvtRenderSnapshot_BeginFrame();
	return committed;
}

/* One classic frame whose flip succeeds. */
static void PublishView(void) {
	XvtRenderCapture_CaptureView();
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(1);
}

static int HasObject(const XvtRenderSnapshot* s, unsigned slot, uint16_t signature) {
	for (uint32_t i = 0; i < s->object_count; ++i)
		if (s->objects[i].id.slot == slot && s->objects[i].id.signature == signature)
			return 1;
	return 0;
}

/* After Init there is no last view, and while capture is inactive no flight view is published. */
static void CheckInactive(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), -1);
	XvtRenderSnapshot_BeginFrame();
	PublishView();
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 0);
	const XvtRenderSnapshot* s = Commit();
	XVT_ASSERT_INT_EQ(s->flight_valid, 0);
	XVT_ASSERT_INT_EQ(s->camera.valid, 0);
	XVT_ASSERT_INT_EQ(s->object_count, 0);
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), -1);
}

/* A sealed view presented by a successful flip reaches the writer: each occupied slot, a valid camera,
 * and a raised flight frame serial; its game time becomes the last view tick. */
static void CheckPublishedView(void) {
	FreshMission();
	PublishView();
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 1);
	const XvtRenderSnapshot* s = NextTick();
	XVT_ASSERT_INT_EQ(s->flight_valid, 1);
	XVT_ASSERT_INT_EQ(s->camera.valid, 1);
	XVT_ASSERT_INT_EQ(s->object_count, 3);
	XVT_ASSERT_TRUE(HasObject(s, 0, 0x0A00));
	XVT_ASSERT_TRUE(HasObject(s, 2, 0x0A02));
	XVT_ASSERT_TRUE(HasObject(s, 3, 0x0A03));
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), 100);
	uint64_t serial = s->flight_frame_serial;

	g_gameTime = 104;
	PublishView();
	s = NextTick();
	XVT_ASSERT_TRUE(s->flight_frame_serial > serial);
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), 104);
}

/* Slots past the object table's capacity are not captured. */
static void CheckTableCapacity(void) {
	FreshMission();
	g_handleTables.sizeTable[TABLE_HANDLE - 1] = 3 * sizeof(ObjectRecord);
	PublishView();
	XVT_ASSERT_INT_EQ(Writer()->object_count, 2);
	XVT_ASSERT_TRUE(!HasObject(Writer(), 3, 0x0A03));
}

/* Returns 1 when one classic frame publishes a flight view into the writer. */
static int Publishes(void) {
	Writer()->flight_valid = 0;
	PublishView();
	return Writer()->flight_valid;
}

/* The view is left invalid when the table is missing or stale, a slot range is negative, the local player
 * index is out of range, or a viewport or screen size is 0. */
static void CheckRefusedViews(void) {
	FreshMission();
	XVT_ASSERT_TRUE(Publishes());
	g_objectTable = NULL;
	XVT_ASSERT_TRUE(!Publishes());
	UseObjects(g_testObjects, 4, 3, 1);
	g_handleTables.ptrTable[TABLE_HANDLE - 1] = &g_testObjects[1];
	XVT_ASSERT_TRUE(!Publishes());
	UseObjects(g_testObjects, 4, 3, 1);
	g_objectTableHandle = 0;
	XVT_ASSERT_TRUE(!Publishes());
	UseObjects(g_testObjects, 4, -1, 1);
	XVT_ASSERT_TRUE(!Publishes());
	UseObjects(g_testObjects, 4, 3, -1);
	XVT_ASSERT_TRUE(!Publishes());
	UseObjects(g_testObjects, 4, 3, 1);
	g_localPlayer = 8;
	XVT_ASSERT_TRUE(!Publishes());
	g_localPlayer = -1;
	XVT_ASSERT_TRUE(!Publishes());
	g_localPlayer = 0;
	XVT_ASSERT_TRUE(Publishes());

	uint16_t* sizes16[2] = { &g_flightVpWidth, &g_flightVpHeight };
	for (int i = 0; i < 2; ++i) {
		uint16_t kept = *sizes16[i];
		*sizes16[i] = 0;
		XVT_ASSERT_TRUE(!Publishes());
		*sizes16[i] = kept;
	}
	unsigned* sizes[2] = { &g_screenWidth, &g_screenHeight };
	for (int i = 0; i < 2; ++i) {
		unsigned kept = *sizes[i];
		*sizes[i] = 0;
		XVT_ASSERT_TRUE(!Publishes());
		*sizes[i] = kept;
	}
	XVT_ASSERT_TRUE(Publishes());
}

/* CaptureView sets the draw scope to world, or to map when the local player's map is open. */
static void CheckDrawScope(void) {
	FreshMission();
	XvtRenderDraw_Scope(XVT_SCOPE_COCKPIT);
	XvtRenderCapture_CaptureView();
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_WORLD);
	g_players[0].mapCameraState = 1;
	XvtRenderCapture_CaptureView();
	XVT_ASSERT_INT_EQ(XvtRenderDraw_ScopeCurrent(), XVT_SCOPE_MAP);
}

/* Objects past XVT_SNAP_OBJECTS and model types with an unsupported frame sequence or palette count as
 * dropped records in the writer when the view is presented. */
static void CheckDroppedRecords(void) {
	FreshMission();
	PublishView();
	uint32_t baseline = Writer()->dropped_records;
	NextTick();

	size_t count = XVT_SNAP_OBJECTS + 36;
	ObjectRecord* objects = calloc(count, sizeof *objects);
	XVT_ASSERT_TRUE(objects != NULL);
	for (size_t i = 0; i < count; ++i) {
		objects[i].objectType = 1;
		objects[i].objectSignature = (uint16_t)i;
	}
	UseObjects(objects, count, (int)count, 0);
	PublishView();
	XVT_ASSERT_INT_EQ(Writer()->object_count, XVT_SNAP_OBJECTS);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, baseline + 36);
	NextTick();

	/* Two model types with neither a frame sequence nor a palette get ones the capture does not know. */
	unsigned plain[2], found = 0;
	for (unsigned t = 0; t < XVT_SNAP_TYPES && found < 2; ++t)
		if (!g_objectTypeTable[t].textureFrameSequence && !g_objectTypeTable[t].palette)
			plain[found++] = t;
	XVT_ASSERT_INT_EQ(found, 2);
	static int16_t sequence[3] = { 1, 2, 3 };
	static uint8_t palette[16];
	g_objectTypeTable[plain[0]].textureFrameSequence = sequence;
	g_objectTypeTable[plain[1]].palette = palette;
	UseObjects(g_testObjects, 4, 3, 1);
	PublishView();
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, baseline + 2);
	g_objectTypeTable[plain[0]].textureFrameSequence = NULL;
	g_objectTypeTable[plain[1]].palette = NULL;
	free(objects);
	UseObjects(g_testObjects, 4, 3, 1);
}

/* A failed flip publishes nothing and leaves the sealed view for a later successful one; a later
 * successful flip in the same tick replaces the view, a failed one does not. */
static void CheckFlips(void) {
	FreshMission();
	XvtRenderCapture_CaptureView();
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(0);
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 0);
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 1);
	XVT_ASSERT_TRUE(HasObject(Writer(), 2, 0x0A02));

	g_testObjects[2].objectSignature = 0x0B02;
	XvtRenderCapture_CaptureView();
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(0);
	XVT_ASSERT_TRUE(HasObject(Writer(), 2, 0x0A02));
	XvtRenderCapture_CaptureView();
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_TRUE(HasObject(Writer(), 2, 0x0B02));
	XVT_ASSERT_TRUE(!HasObject(Writer(), 2, 0x0A02));
}

/* EndPresentation discards the pending view, sealed or not, and so does BeginFrame. */
static void CheckDiscardedViews(void) {
	FreshMission();
	XvtRenderCapture_CaptureView();
	XvtRenderCapture_SealView();
	XvtRenderCapture_EndPresentation();
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 0);

	XvtRenderCapture_CaptureView();
	XvtRenderCapture_SealView();
	XvtRenderCapture_BeginFrame();
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 0);
}

/* A presented view carries the HUD's target boxes into the writer; BeginClassicFrame begins a new HUD
 * frame, which clears them. */
static void CheckHudBoxes(void) {
	FreshMission();
	XvtRenderCapture_CaptureView();
	XvtRenderHud_TargetBox(0, 1, 100, 5);
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(Writer()->target_box_count, 1);

	XvtRenderCapture_BeginClassicFrame();
	PublishView();
	XVT_ASSERT_INT_EQ(Writer()->target_box_count, 0);
}

/* Hyperspace copies the streaks into the pending view, and nothing when there are more than
 * XVT_SNAP_STREAKS. */
static void CheckHyperspace(void) {
	FreshMission();
	const int x[3] = { 1, 2, 3 }, y[3] = { -4, -5, -6 }, z[3] = { 70, 80, 90 };
	const int width[3] = { 9, 8, 7 }, roll[3] = { 100, 200, 300 };
	XvtRenderCapture_CaptureView();
	XvtRenderCapture_Hyperspace(3, x, y, z, width, roll);
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(1);
	const XvtSnapHyperspace* h = &Writer()->hyperspace;
	XVT_ASSERT_INT_EQ(h->count, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(h->streaks[i].offset[0], x[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].offset[1], y[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].offset[2], z[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].half_width, width[i]);
		XVT_ASSERT_INT_EQ(h->streaks[i].roll, roll[i]);
	}

	XvtRenderCapture_CaptureView();
	XvtRenderCapture_Hyperspace(XVT_SNAP_STREAKS + 1, x, y, z, width, roll);
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(Writer()->hyperspace.count, 0);
}

/* With no view published in a tick, Commit carries the previous snapshot's view forward when it has the
 * same mission and world generation; otherwise the snapshot has no flight view. */
static void CheckCarryForward(void) {
	FreshMission();
	XvtRenderCapture_CaptureView();
	XvtRenderHud_TargetBox(2, 0, 50, 1);
	XvtRenderCapture_SealView();
	XvtRenderCapture_Presented(1);
	const XvtRenderSnapshot* published = NextTick();
	uint64_t mission = published->mission_generation;
	uint64_t world = published->world_generation;

	const XvtRenderSnapshot* carried = NextTick();
	XVT_ASSERT_INT_EQ(carried->flight_valid, 1);
	XVT_ASSERT_INT_EQ(carried->object_count, 3);
	XVT_ASSERT_TRUE(HasObject(carried, 2, 0x0A02));
	XVT_ASSERT_INT_EQ(carried->target_box_count, 1);
	XVT_ASSERT_INT_EQ(carried->mission_generation, mission);
	XVT_ASSERT_INT_EQ(carried->world_generation, world);

	XvtRenderCapture_WorldChanged();
	const XvtRenderSnapshot* after = NextTick();
	XVT_ASSERT_INT_EQ(after->flight_valid, 0);
	XVT_ASSERT_TRUE(after->world_generation > world);

	/* A new mission, then capture ends: nothing is carried. */
	PublishView();
	NextTick();
	XvtRenderCapture_EndMission();
	const XvtRenderSnapshot* ended = NextTick();
	XVT_ASSERT_INT_EQ(ended->flight_valid, 0);
	XVT_ASSERT_INT_EQ(ended->camera.valid, 0);
}

/* The mission generation rises on BeginMission; the world generation on every world change and on a view
 * whose game time went backward. WorldChanged forgets the last view time and clears the writer's flight
 * and camera validity. */
static void CheckGenerations(void) {
	FreshMission();
	const XvtRenderSnapshot* s = NextTick();
	uint64_t mission = s->mission_generation;
	uint64_t world = s->world_generation;

	XvtRenderCapture_BeginMission();
	s = NextTick();
	XVT_ASSERT_TRUE(s->mission_generation > mission);
	XVT_ASSERT_TRUE(s->world_generation > world);
	mission = s->mission_generation;
	world = s->world_generation;

	PublishView();
	s = NextTick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);
	g_gameTime = 50;
	PublishView();
	s = NextTick();
	XVT_ASSERT_TRUE(s->world_generation > world);
	XVT_ASSERT_INT_EQ(s->mission_generation, mission);
	world = s->world_generation;

	PublishView();
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 1);
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), 50);
	XvtRenderCapture_WorldChanged();
	XVT_ASSERT_INT_EQ(Writer()->flight_valid, 0);
	XVT_ASSERT_INT_EQ(Writer()->camera.valid, 0);
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), -1);
	s = NextTick();
	XVT_ASSERT_TRUE(s->world_generation > world);

	world = s->world_generation;
	XvtRenderCapture_EndMission();
	s = NextTick();
	XVT_ASSERT_TRUE(s->world_generation > world);
}

/* Network profile: a slot's pose that changed at the authoritative tick flags a correction, which
 * CompleteNetworkWorld applies as a world change. */
static void CheckNetworkCorrection(void) {
	FreshMission();
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_NETWORK_125);
	g_gameTime = 10;
	XvtRenderCapture_CompleteNetworkWorld();
	PublishView();
	const XvtRenderSnapshot* s = NextTick();
	uint64_t world = s->world_generation;

	/* Nothing moved: no correction. */
	XvtRenderCapture_CheckNetworkCorrection();
	XvtRenderCapture_CompleteNetworkWorld();
	s = NextTick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);

	/* A move seen at another game time is not compared. */
	g_testObjects[2].world_x += 5;
	g_gameTime = 11;
	XvtRenderCapture_CheckNetworkCorrection();
	g_gameTime = 10;
	XvtRenderCapture_CompleteNetworkWorld();
	s = NextTick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);

	/* The move is seen at the authoritative tick, which the presented view at time 10 set. */
	g_testObjects[2].world_x -= 5;
	XvtRenderCapture_CompleteNetworkWorld();
	PublishView();
	NextTick();
	g_testObjects[2].yaw = 0x1234;
	XvtRenderCapture_CheckNetworkCorrection();
	XvtRenderCapture_CompleteNetworkWorld();
	s = NextTick();
	XVT_ASSERT_TRUE(s->world_generation > world);
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), -1);
	XvtFlightTiming_EndSession();
}

/* Outside the network profile neither call changes the world. */
static void CheckNetworkOnly(void) {
	FreshMission();
	g_gameTime = 10;
	XvtRenderCapture_CompleteNetworkWorld();
	PublishView();
	const XvtRenderSnapshot* s = NextTick();
	uint64_t world = s->world_generation;
	g_testObjects[2].world_x += 5;
	XvtRenderCapture_CheckNetworkCorrection();
	XvtRenderCapture_CompleteNetworkWorld();
	s = NextTick();
	XVT_ASSERT_INT_EQ(s->world_generation, world);
	XVT_ASSERT_INT_EQ(XvtRenderCapture_LastViewTick(), 10);
}

/* A local transient slot is left out of the comparison. */
static void CheckNetworkSkipsLocalSlots(void) {
	FreshMission();
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_NETWORK_125);
	g_localTransientSlotStart = 1;
	g_localDebrisSlotEnd = 2;
	g_testObjects[1].objectType = 4;
	g_testObjects[1].objectSignature = 0x0A01;
	g_gameTime = 10;
	XvtRenderCapture_CompleteNetworkWorld();
	PublishView();
	uint64_t world = NextTick()->world_generation;
	g_testObjects[1].world_z += 1000;
	XvtRenderCapture_CheckNetworkCorrection();
	XvtRenderCapture_CompleteNetworkWorld();
	XVT_ASSERT_INT_EQ(NextTick()->world_generation, world);
	XvtFlightTiming_EndSession();
}

/* Init does not reset the network pose history: a pose recorded before it still flags a correction. */
static void CheckInitKeepsPoseHistory(void) {
	FreshMission();
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_NETWORK_125);
	g_gameTime = 10;
	XvtRenderCapture_CompleteNetworkWorld();
	PublishView();
	NextTick();
	XvtRenderCapture_Reset();
	uint64_t world = NextTick()->world_generation;
	g_testObjects[0].roll = 0x0400;
	XvtRenderCapture_CheckNetworkCorrection();
	XvtRenderCapture_CompleteNetworkWorld();
	XVT_ASSERT_TRUE(NextTick()->world_generation > world);
	XvtFlightTiming_EndSession();
}

/* Inside an overlay, a successful flip without a sealed view presents the flight scene; outside one it
 * does nothing. End never drops below zero. */
static void CheckOverlay(void) {
	FreshMission();
	uint64_t serial = NextTick()->presentation_serial;
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(NextTick()->presentation_serial, serial);

	XvtRenderCapture_BeginOverlay();
	XvtRenderCapture_BeginOverlay();
	XvtRenderCapture_EndOverlay();
	XvtRenderCapture_Presented(1);
	const XvtRenderSnapshot* s = NextTick();
	XVT_ASSERT_TRUE(s->presentation_serial > serial);
	XVT_ASSERT_INT_EQ(s->presented_scene, XVT_SCENE_FLIGHT);
	serial = s->presentation_serial;

	/* A failed flip does nothing, inside the overlay too. */
	XvtRenderCapture_Presented(0);
	XVT_ASSERT_INT_EQ(NextTick()->presentation_serial, serial);

	XvtRenderCapture_EndOverlay();
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(NextTick()->presentation_serial, serial);

	/* Ends past zero leave it at zero: one Begin and one End later the flip is outside again. */
	XvtRenderCapture_EndOverlay();
	XvtRenderCapture_EndOverlay();
	XvtRenderCapture_BeginOverlay();
	XvtRenderCapture_Presented(1);
	s = NextTick();
	XVT_ASSERT_TRUE(s->presentation_serial > serial);
	serial = s->presentation_serial;
	XvtRenderCapture_EndOverlay();
	XvtRenderCapture_Presented(1);
	XVT_ASSERT_INT_EQ(NextTick()->presentation_serial, serial);
}

/* FrontendPreview appends straight to the writer with a 640x480 camera; it is valid only when its handle
 * has an asset id. An empty rectangle or a bad local player index adds nothing; a full list counts a
 * dropped record. */
static void CheckFrontendPreview(void) {
	Fresh();
	XvtRenderSnapshot_BeginFrame();
	const float position[3] = { 1.5f, -2.5f, 300.0f };
	const float orientation[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	XvtRenderCapture_FrontendPreview(9, position, orientation, 2.5f, 3, 10, 20, 100, 80);
	XVT_ASSERT_INT_EQ(Writer()->preview_count, 1);
	const XvtSnapPreview* p = &Writer()->previews[0];
	XVT_ASSERT_INT_EQ(p->valid, 0);
	XVT_ASSERT_INT_EQ(p->camera.screen_width, 640);
	XVT_ASSERT_INT_EQ(p->camera.screen_height, 480);
	XVT_ASSERT_INT_EQ(p->destination.x, 10);
	XVT_ASSERT_INT_EQ(p->destination.y, 20);
	XVT_ASSERT_INT_EQ(p->destination.width, 100);
	XVT_ASSERT_INT_EQ(p->destination.height, 80);
	XVT_ASSERT_INT_EQ(p->node_switch, 3);
	for (int i = 0; i < 3; ++i)
		XVT_ASSERT_CLOSE(p->view_pos[i], position[i], 0, "a float copied as it is");
	for (int i = 0; i < 9; ++i)
		XVT_ASSERT_CLOSE(p->view_orient[i], orientation[i], 0, "a float copied as it is");
	XVT_ASSERT_CLOSE(p->model_scale, 2.5, 0, "a float copied as it is");

	XvtRenderCapture_FrontendPreview(9, position, orientation, 1, 0, 0, 0, 0, 80);
	XvtRenderCapture_FrontendPreview(9, position, orientation, 1, 0, 0, 0, 100, -1);
	g_localPlayer = 8;
	XvtRenderCapture_FrontendPreview(9, position, orientation, 1, 0, 0, 0, 100, 80);
	g_localPlayer = 0;
	XVT_ASSERT_INT_EQ(Writer()->preview_count, 1);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 0);

	for (int i = 1; i < XVT_SNAP_PREVIEWS; ++i)
		XvtRenderCapture_FrontendPreview(9, position, orientation, 1, 0, 0, 0, 100, 80);
	XVT_ASSERT_INT_EQ(Writer()->preview_count, XVT_SNAP_PREVIEWS);
	XvtRenderCapture_FrontendPreview(9, position, orientation, 1, 0, 0, 0, 100, 80);
	XVT_ASSERT_INT_EQ(Writer()->preview_count, XVT_SNAP_PREVIEWS);
	XVT_ASSERT_INT_EQ(Writer()->dropped_records, 1);

	/* Without an open tick nothing is added. */
	const XvtRenderSnapshot* s = Commit();
	XvtRenderCapture_FrontendPreview(9, position, orientation, 1, 0, 0, 0, 100, 80);
	XVT_ASSERT_INT_EQ(s->preview_count, XVT_SNAP_PREVIEWS);
	XVT_ASSERT_INT_EQ(s->dropped_records, 1);
}

int main(void) {
	CheckInactive();
	CheckPublishedView();
	CheckTableCapacity();
	CheckRefusedViews();
	CheckDrawScope();
	CheckDroppedRecords();
	CheckFlips();
	CheckDiscardedViews();
	CheckHudBoxes();
	CheckHyperspace();
	CheckCarryForward();
	CheckGenerations();
	CheckNetworkCorrection();
	CheckNetworkOnly();
	CheckNetworkSkipsLocalSlots();
	CheckInitKeepsPoseHistory();
	CheckOverlay();
	CheckFrontendPreview();
	XvtRenderSnapshot_Shutdown();
	return 0;
}

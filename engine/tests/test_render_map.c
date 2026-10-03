/* Checks the flight map capture (xvt_runtime/snapshot/render_map.h) against the promises in its header, on
 * a world this file builds itself: no game data is read. Each check starts from eight object slots (six
 * main, two static, the explosion slots ending at 4), one flight group, and local player 0 with the map
 * open, no camera focus and no target. No map icons are loaded.
 *
 * Not checked: the order endpoint, since the header does not say when a target's order "resolves", and
 * the label space running out, which XVT_SNAP_OBJECTS objects cannot reach: a label is at most a 20-byte
 * flight-group name, a space, three digits and a terminator, and 1664 of those fill 41600 of the 65535
 * bytes. */
#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/snapshot/render_map.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 8, MAIN_SLOTS = 6, EXPLOSION_SLOT_END = 4 };

static struct ObjectRecord g_testObjects[SLOTS];
static struct XvtSnapMap *g_map;

static void FreshWorld(void)
{
	memset(g_testObjects, 0, sizeof g_testObjects);
	for (int i = 0; i < SLOTS; ++i) {
		g_testObjects[i].objectSignature = (uint16_t)(0x100 + i);
		g_testObjects[i].flightGroupIdx = 0xFF;
	}
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = MAIN_SLOTS;
	g_regionStaticObjectSlotCount = SLOTS - MAIN_SLOTS;
	g_explosionObjectSlotEnd = EXPLOSION_SLOT_END;
	g_craftDataPoolCapacity = 0;
	memset(g_missionFlightGroups, 0, sizeof g_missionFlightGroups);
	memset(g_players, 0, sizeof g_players);
	g_localPlayer = 0;
	g_players[0].mapCameraState = 1;
	g_players[0].viewState.cameraFocusObjIdx = 0xFFFF;
	g_players[0].currentTargetObjectIdx = -1;
	g_flightIconFrames = NULL;
	g_flightIconFrameCount = 0;
	memset(g_map, 0, sizeof *g_map);
}

/* The captured record of the object in slot, as the capture would hand it over. */
static struct XvtSnapObject Captured(unsigned slot, unsigned genus)
{
	struct XvtSnapObject object;
	memset(&object, 0, sizeof object);
	object.id = (struct XvtSnapObjectId){
		(uint16_t)slot, g_testObjects[slot].objectSignature};
	object.genus = (uint8_t)genus;
	object.slot_class =
		slot >= MAIN_SLOTS ? XVT_SLOT_STATIC : XVT_SLOT_MAIN;
	return object;
}

/* Returns how many map objects one captured object gives: 1 when kept, 0 when not. */
static unsigned Kept(struct XvtSnapObject object)
{
	XvtRenderMap_Capture(g_map, &object, 1);
	XVT_ASSERT_INT_EQ(g_map->active, 1);
	return g_map->object_count;
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

/* With the local player's map closed, the map is left cleared and inactive. */
static void CheckClosedMap(void)
{
	FreshWorld();
	g_players[0].mapCameraState = 0;
	struct XvtSnapObject object = Captured(0, CRAFT_GENUS_STARFIGHTER);
	memset(g_map, 0xAB, sizeof *g_map);
	XvtRenderMap_Capture(g_map, &object, 1);
	XVT_ASSERT_INT_EQ(g_map->active, 0);
	XVT_ASSERT_TRUE(AllZero(g_map, sizeof *g_map));
}

/* Static objects are kept for genus up to platform or mine to satellite, wherever their slot. */
static void CheckStaticGenera(void)
{
	FreshWorld();
	for (unsigned genus = 0; genus <= CRAFT_GENUS_PEOPLE; ++genus) {
		unsigned expected = genus <= CRAFT_GENUS_PLATFORM ||
				    (genus >= CRAFT_GENUS_MINE &&
				     genus <= CRAFT_GENUS_SATELLITE);
		XVT_ASSERT_INT_EQ(Kept(Captured(MAIN_SLOTS, genus)), expected);
		XVT_ASSERT_INT_EQ(Kept(Captured(SLOTS - 1, genus)), expected);
	}
}

/* Other objects are kept below g_explosionObjectSlotEnd for genus up to platform, a projectile, small
 * debris or an explosion; at or past it, never. */
static void CheckOtherGenera(void)
{
	FreshWorld();
	for (unsigned genus = 0; genus <= CRAFT_GENUS_PEOPLE; ++genus) {
		unsigned expected = genus <= CRAFT_GENUS_PLATFORM ||
				    genus == CRAFT_GENUS_PLAYER_PROJECTILE ||
				    genus == CRAFT_GENUS_OTHER_PROJECTILE ||
				    genus == CRAFT_GENUS_SMALL_DEBRIS ||
				    genus == CRAFT_GENUS_EXPLOSION;
		XVT_ASSERT_INT_EQ(Kept(Captured(0, genus)), expected);
		XVT_ASSERT_INT_EQ(Kept(Captured(EXPLOSION_SLOT_END - 1, genus)),
				  expected);
		struct XvtSnapObject transient = Captured(1, genus);
		transient.slot_class = XVT_SLOT_LOCAL_TRANSIENT;
		XVT_ASSERT_INT_EQ(Kept(transient), expected);
		XVT_ASSERT_INT_EQ(Kept(Captured(EXPLOSION_SLOT_END, genus)), 0);
		XVT_ASSERT_INT_EQ(Kept(Captured(MAIN_SLOTS - 1, genus)), 0);
	}
}

/* Several objects in one capture: each kept one is counted once. */
static void CheckCount(void)
{
	FreshWorld();
	struct XvtSnapObject objects[5] = {
		Captured(0, CRAFT_GENUS_STARFIGHTER),
		Captured(1, CRAFT_GENUS_BACKDROP),
		Captured(2, CRAFT_GENUS_EXPLOSION),
		Captured(EXPLOSION_SLOT_END, CRAFT_GENUS_STARSHIP),
		Captured(MAIN_SLOTS, CRAFT_GENUS_MINE),
	};
	XvtRenderMap_Capture(g_map, objects, 5);
	XVT_ASSERT_INT_EQ(g_map->active, 1);
	XVT_ASSERT_INT_EQ(g_map->object_count, 3);
}

/* The one map object a kept captured object gives. */
static const struct XvtSnapMapObject *MapObject(struct XvtSnapObject object)
{
	XVT_ASSERT_INT_EQ(Kept(object), 1);
	return &g_map->objects[0];
}

/* The range from the camera focus: 0 at the focus's own position, never smaller farther away, and capped
 * at 9999. Without a valid focus slot there is no range. */
static void CheckRange(void)
{
	FreshWorld();
	g_testObjects[0].world_x = 1000;
	g_testObjects[0].world_y = -2000;
	g_testObjects[0].world_z = 3000;
	g_testObjects[1] = g_testObjects[0];
	g_testObjects[2] = g_testObjects[0];
	g_testObjects[2].world_x += 100000;
	g_testObjects[3] = g_testObjects[0];
	g_testObjects[3].world_x += 200000;
	g_testObjects[3].world_z -= 50000;
	g_testObjects[MAIN_SLOTS] = g_testObjects[0];
	g_testObjects[MAIN_SLOTS].world_y += 1000000000;

	g_players[0].viewState.cameraFocusObjIdx = 0;
	unsigned same =
		MapObject(Captured(1, CRAFT_GENUS_STARFIGHTER))->range_value;
	unsigned near =
		MapObject(Captured(2, CRAFT_GENUS_STARFIGHTER))->range_value;
	unsigned farther =
		MapObject(Captured(3, CRAFT_GENUS_STARFIGHTER))->range_value;
	unsigned far = MapObject(Captured(MAIN_SLOTS, CRAFT_GENUS_PLATFORM))
			       ->range_value;
	XVT_ASSERT_INT_EQ(same, 0);
	XVT_ASSERT_TRUE(near >= same);
	XVT_ASSERT_TRUE(farther >= near);
	XVT_ASSERT_INT_EQ(far, 9999);

	/* The slot total is not a valid focus slot. */
	g_players[0].viewState.cameraFocusObjIdx = SLOTS;
	XVT_ASSERT_INT_EQ(MapObject(Captured(MAIN_SLOTS, CRAFT_GENUS_PLATFORM))
				  ->range_value,
			  0);
}

/* The current target sets map->target to its captured id; when no captured object is the target, no
 * captured object's id is there. */
static void CheckTarget(void)
{
	FreshWorld();
	struct XvtSnapObject objects[3] = {
		Captured(1, CRAFT_GENUS_STARFIGHTER),
		Captured(2, CRAFT_GENUS_TRANSPORT),
		Captured(3, CRAFT_GENUS_FREIGHTER),
	};
	g_players[0].currentTargetObjectIdx = 2;
	XvtRenderMap_Capture(g_map, objects, 3);
	XVT_ASSERT_INT_EQ(g_map->target.slot, 2);
	XVT_ASSERT_INT_EQ(g_map->target.signature, objects[1].id.signature);

	g_players[0].currentTargetObjectIdx = 5;
	XvtRenderMap_Capture(g_map, objects, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_TRUE(g_map->target.slot != objects[i].id.slot);
	}
}

/* A kept object in a flight group gets that group's label. */
static void CheckLabel(void)
{
	FreshWorld();
	strcpy(g_missionFlightGroups[3].fg.name, "Gold");
	strcpy(g_missionFlightGroups[4].fg.name, "Red");
	g_testObjects[1].flightGroupIdx = 3;
	g_testObjects[2].flightGroupIdx = 4;
	const unsigned slots[2] = {1, 2};
	const char *names[2] = {"Gold", "Red"};
	for (int i = 0; i < 2; ++i) {
		const struct XvtSnapMapObject *object =
			MapObject(Captured(slots[i], CRAFT_GENUS_STARFIGHTER));
		XVT_ASSERT_INT_EQ(object->label_visible, 1);
		XVT_ASSERT_TRUE(object->label_offset < g_map->label_bytes);
		XVT_ASSERT_INT_EQ(strncmp(g_map->labels + object->label_offset,
					  names[i], strlen(names[i])),
				  0);
	}
}

/* Before map icons are loaded no object gets an icon. */
static void CheckNoIconsBeforeLoad(void)
{
	FreshWorld();
	XVT_ASSERT_INT_EQ(
		MapObject(Captured(0, CRAFT_GENUS_STARFIGHTER))->icon_frame, 0);
	XVT_ASSERT_INT_EQ(g_map->icon_asset_id, 0);
}

int main(void)
{
	g_map = malloc(sizeof *g_map);
	XVT_ASSERT_TRUE(g_map != NULL);
	CheckClosedMap();
	CheckStaticGenera();
	CheckOtherGenera();
	CheckCount();
	CheckRange();
	CheckTarget();
	CheckLabel();
	CheckNoIconsBeforeLoad();
	free(g_map);
	return 0;
}

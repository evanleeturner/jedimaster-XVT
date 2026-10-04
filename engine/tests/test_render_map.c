/* Checks the flight map capture (xvt_runtime/snapshot/render_map.h) against the
 * promises in its header, on a world this file builds itself: no game data is
 * read. Each check starts from eight object slots (six main, two static, the
 * explosion slots ending at 4), one flight group, and local player 0 with the
 * map open, no camera focus and no target. No map icons are loaded.
 *
 * Not checked: the order endpoint, since the header does not say when a
 * target's order "resolves", and the label space running out, which
 * XVT_SNAP_OBJECTS objects cannot reach: a label is at most a 20-byte
 * flight-group name, a space, three digits and a terminator, and 1664 of those
 * fill 41600 of the 65535 bytes. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/snapshot/render_map.h"

enum { SLOTS = 8, MAIN_SLOTS = 6, EXPLOSION_SLOT_END = 4 };

static struct object_record g_test_objects[SLOTS];
static struct xvt_snap_map *g_map;

static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	for (int i = 0; i < SLOTS; ++i) {
		g_test_objects[i].object_signature = (uint16_t)(0x100 + i);
		g_test_objects[i].flight_group_idx = 0xFF;
	}
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = MAIN_SLOTS;
	g_region_static_object_slot_count = SLOTS - MAIN_SLOTS;
	g_explosion_object_slot_end = EXPLOSION_SLOT_END;
	g_craft_data_pool_capacity = 0;
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_players, 0, sizeof g_players);
	g_local_player = 0;
	g_players[0].map_camera_state = 1;
	g_players[0].view_state.camera_focus_obj_idx = 0xFFFF;
	g_players[0].current_target_object_idx = -1;
	g_flight_icon_frames = NULL;
	g_flight_icon_frame_count = 0;
	memset(g_map, 0, sizeof *g_map);
}

/* The captured record of the object in slot, as the capture would hand it over. */
static struct xvt_snap_object captured(unsigned slot, unsigned genus)
{
	struct xvt_snap_object object;
	memset(&object, 0, sizeof object);
	object.id = (struct xvt_snap_object_id){
		(uint16_t)slot, g_test_objects[slot].object_signature};
	object.genus = (uint8_t)genus;
	object.slot_class =
		slot >= MAIN_SLOTS ? XVT_SLOT_STATIC : XVT_SLOT_MAIN;
	return object;
}

/* Returns how many map objects one captured object gives: 1 when kept, 0 when not. */
static unsigned kept(struct xvt_snap_object object)
{
	xvt_render_map_capture(g_map, &object, 1);
	XVT_ASSERT_INT_EQ(g_map->active, 1);
	return g_map->object_count;
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

/* With the local player's map closed, the map is left cleared and inactive. */
static void check_closed_map(void)
{
	fresh_world();
	g_players[0].map_camera_state = 0;
	struct xvt_snap_object object = captured(0, CRAFT_GENUS_STARFIGHTER);
	memset(g_map, 0xAB, sizeof *g_map);
	xvt_render_map_capture(g_map, &object, 1);
	XVT_ASSERT_INT_EQ(g_map->active, 0);
	XVT_ASSERT_TRUE(all_zero(g_map, sizeof *g_map));
}

/* Static objects are kept for genus up to platform or mine to satellite, wherever their slot. */
static void check_static_genera(void)
{
	fresh_world();
	for (unsigned genus = 0; genus <= CRAFT_GENUS_PEOPLE; ++genus) {
		unsigned expected = genus <= CRAFT_GENUS_PLATFORM ||
				    (genus >= CRAFT_GENUS_MINE &&
				     genus <= CRAFT_GENUS_SATELLITE);
		XVT_ASSERT_INT_EQ(kept(captured(MAIN_SLOTS, genus)), expected);
		XVT_ASSERT_INT_EQ(kept(captured(SLOTS - 1, genus)), expected);
	}
}

/* Other objects are kept below g_explosion_object_slot_end for genus up to
 * platform, a projectile, small debris or an explosion; at or past it,
 * never. */
static void check_other_genera(void)
{
	fresh_world();
	for (unsigned genus = 0; genus <= CRAFT_GENUS_PEOPLE; ++genus) {
		unsigned expected = genus <= CRAFT_GENUS_PLATFORM ||
				    genus == CRAFT_GENUS_PLAYER_PROJECTILE ||
				    genus == CRAFT_GENUS_OTHER_PROJECTILE ||
				    genus == CRAFT_GENUS_SMALL_DEBRIS ||
				    genus == CRAFT_GENUS_EXPLOSION;
		XVT_ASSERT_INT_EQ(kept(captured(0, genus)), expected);
		XVT_ASSERT_INT_EQ(kept(captured(EXPLOSION_SLOT_END - 1, genus)),
				  expected);
		struct xvt_snap_object transient = captured(1, genus);
		transient.slot_class = XVT_SLOT_LOCAL_TRANSIENT;
		XVT_ASSERT_INT_EQ(kept(transient), expected);
		XVT_ASSERT_INT_EQ(kept(captured(EXPLOSION_SLOT_END, genus)), 0);
		XVT_ASSERT_INT_EQ(kept(captured(MAIN_SLOTS - 1, genus)), 0);
	}
}

/* Several objects in one capture: each kept one is counted once. */
static void check_count(void)
{
	fresh_world();
	struct xvt_snap_object objects[5] = {
		captured(0, CRAFT_GENUS_STARFIGHTER),
		captured(1, CRAFT_GENUS_BACKDROP),
		captured(2, CRAFT_GENUS_EXPLOSION),
		captured(EXPLOSION_SLOT_END, CRAFT_GENUS_STARSHIP),
		captured(MAIN_SLOTS, CRAFT_GENUS_MINE),
	};
	xvt_render_map_capture(g_map, objects, 5);
	XVT_ASSERT_INT_EQ(g_map->active, 1);
	XVT_ASSERT_INT_EQ(g_map->object_count, 3);
}

/* The one map object a kept captured object gives. */
static const struct xvt_snap_map_object *
map_object(struct xvt_snap_object object)
{
	XVT_ASSERT_INT_EQ(kept(object), 1);
	return &g_map->objects[0];
}

/* The range from the camera focus: 0 at the focus's own position, never smaller
 * farther away, and capped at 9999. Without a valid focus slot there is no
 * range. */
static void check_range(void)
{
	fresh_world();
	g_test_objects[0].world_x = 1000;
	g_test_objects[0].world_y = -2000;
	g_test_objects[0].world_z = 3000;
	g_test_objects[1] = g_test_objects[0];
	g_test_objects[2] = g_test_objects[0];
	g_test_objects[2].world_x += 100000;
	g_test_objects[3] = g_test_objects[0];
	g_test_objects[3].world_x += 200000;
	g_test_objects[3].world_z -= 50000;
	g_test_objects[MAIN_SLOTS] = g_test_objects[0];
	g_test_objects[MAIN_SLOTS].world_y += 1000000000;

	g_players[0].view_state.camera_focus_obj_idx = 0;
	unsigned same =
		map_object(captured(1, CRAFT_GENUS_STARFIGHTER))->range_value;
	unsigned near =
		map_object(captured(2, CRAFT_GENUS_STARFIGHTER))->range_value;
	unsigned farther =
		map_object(captured(3, CRAFT_GENUS_STARFIGHTER))->range_value;
	unsigned far = map_object(captured(MAIN_SLOTS, CRAFT_GENUS_PLATFORM))
			       ->range_value;
	XVT_ASSERT_INT_EQ(same, 0);
	XVT_ASSERT_TRUE(near >= same);
	XVT_ASSERT_TRUE(farther >= near);
	XVT_ASSERT_INT_EQ(far, 9999);

	/* The slot total is not a valid focus slot. */
	g_players[0].view_state.camera_focus_obj_idx = SLOTS;
	XVT_ASSERT_INT_EQ(map_object(captured(MAIN_SLOTS, CRAFT_GENUS_PLATFORM))
				  ->range_value,
			  0);
}

/* The current target sets map->target to its captured id; when no captured object is the target, no
 * captured object's id is there. */
static void check_target(void)
{
	fresh_world();
	struct xvt_snap_object objects[3] = {
		captured(1, CRAFT_GENUS_STARFIGHTER),
		captured(2, CRAFT_GENUS_TRANSPORT),
		captured(3, CRAFT_GENUS_FREIGHTER),
	};
	g_players[0].current_target_object_idx = 2;
	xvt_render_map_capture(g_map, objects, 3);
	XVT_ASSERT_INT_EQ(g_map->target.slot, 2);
	XVT_ASSERT_INT_EQ(g_map->target.signature, objects[1].id.signature);

	g_players[0].current_target_object_idx = 5;
	xvt_render_map_capture(g_map, objects, 3);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_TRUE(g_map->target.slot != objects[i].id.slot);
	}
}

/* A kept object in a flight group gets that group's label. */
static void check_label(void)
{
	fresh_world();
	strcpy(g_mission_flight_groups[3].fg.name, "Gold");
	strcpy(g_mission_flight_groups[4].fg.name, "Red");
	g_test_objects[1].flight_group_idx = 3;
	g_test_objects[2].flight_group_idx = 4;
	const unsigned slots[2] = {1, 2};
	const char *names[2] = {"Gold", "Red"};
	for (int i = 0; i < 2; ++i) {
		const struct xvt_snap_map_object *object =
			map_object(captured(slots[i], CRAFT_GENUS_STARFIGHTER));
		XVT_ASSERT_INT_EQ(object->label_visible, 1);
		XVT_ASSERT_TRUE(object->label_offset < g_map->label_bytes);
		XVT_ASSERT_INT_EQ(strncmp(g_map->labels + object->label_offset,
					  names[i], strlen(names[i])),
				  0);
	}
}

/* Before map icons are loaded no object gets an icon. */
static void check_no_icons_before_load(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(
		map_object(captured(0, CRAFT_GENUS_STARFIGHTER))->icon_frame,
		0);
	XVT_ASSERT_INT_EQ(g_map->icon_asset_id, 0);
}

int main(void)
{
	g_map = malloc(sizeof *g_map);
	XVT_ASSERT_TRUE(g_map != NULL);
	check_closed_map();
	check_static_genera();
	check_other_genera();
	check_count();
	check_range();
	check_target();
	check_label();
	check_no_icons_before_load();
	free(g_map);
	return 0;
}

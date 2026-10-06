/* Tests for xvt/flight/object/damage.c, the damage page. Checks the step
 * through the local player's damaged systems in display order, on a craft
 * this file builds itself; no game data is read. The page's drawing and the
 * billboards over damaged hulls draw into the renderer and are not run
 * here. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"

enum { CRAFT_SLOT = 2, LOCAL_PLAYER = 0 };

static struct object_record g_test_objects[4];
static struct mobile_object g_test_mobiles[4];
static struct craft_data g_test_craft;

/* Display order, slot by slot: the systems in reverse, so system 9 is shown
 * first and system 0 last. */
static int system_at(int display_slot)
{
	return CRAFT_SUBSYSTEM_COUNT - 1 - display_slot;
}

/* The local player flies the craft in CRAFT_SLOT; its systems are shown in
 * reverse order and all have health 100 except those in damaged, which have
 * health 0. */
static void damaged_craft(const int *damaged, int count)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(&g_test_craft, 0, sizeof g_test_craft);
	g_object_table = g_test_objects;
	g_test_objects[CRAFT_SLOT].object_type = 1;
	g_test_objects[CRAFT_SLOT].mobj = &g_test_mobiles[CRAFT_SLOT];
	g_test_mobiles[CRAFT_SLOT].p_craft = &g_test_craft;
	g_local_player = LOCAL_PLAYER;
	g_players[LOCAL_PLAYER].object_index = CRAFT_SLOT;
	for (int slot = 0; slot < CRAFT_SUBSYSTEM_COUNT; ++slot) {
		g_test_craft.system_display_slot_by_system[system_at(slot)] =
			(uint8_t)slot;
	}
	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		g_test_craft.system_health[system] = 100;
	}
	for (int i = 0; i < count; ++i) {
		g_test_craft.system_health[damaged[i]] = 0;
	}
}

/* A system that is not damaged gives 0 either way. */
static void check_undamaged_current(void)
{
	static const int damaged[] = {3, 6};
	damaged_craft(damaged, 2);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(5, 1), 0);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(5, -1), 0);
}

/* Between damaged systems the step goes to the next damaged one in display
 * order, forward or back. Systems 7, 4 and 1 are damaged and are shown in
 * that order. */
static void check_steps_between_damaged(void)
{
	static const int damaged[] = {1, 4, 7};
	damaged_craft(damaged, 3);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(7, 1), 4);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(4, 1), 1);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(1, -1), 4);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(4, -1), 7);

	/* System 9, shown first, steps forward to system 5. */
	static const int first_shown[] = {5, 9};
	damaged_craft(first_shown, 2);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(9, 1), 5);
}

/* At the ends the step leaves the damaged systems: back from the first
 * damaged system it gives the last undamaged one in display order, and
 * forward from the last the first undamaged one. */
static void check_steps_past_the_ends(void)
{
	static const int damaged[] = {1, 4, 7};
	damaged_craft(damaged, 3);
	/* System 0 is shown last and is undamaged; system 9 first. */
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(7, -1), 0);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(1, 1), 9);

	/* Only system 9, shown first, is undamaged: back from system 8, the
	 * first damaged one, gives system 9. */
	static const int all_but_first[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
	damaged_craft(all_but_first, 9);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(8, -1), 9);
}

/* With every system damaged, back from the first gives the last system in
 * display order, and forward from the last gives the first damaged one. */
static void check_all_damaged(void)
{
	static const int damaged[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	damaged_craft(damaged, CRAFT_SUBSYSTEM_COUNT);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(9, -1), 0);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(0, 1), 9);
	XVT_ASSERT_INT_EQ(damage_find_adjacent_damaged_system(5, 1), 4);
}

int main(void)
{
	check_undamaged_current();
	check_steps_between_damaged();
	check_steps_past_the_ends();
	check_all_damaged();
	return 0;
}

/* Tests for xvt/flight/player/flight_player.c, the player's throttle steps and
 * the disabled-system test. Each check puts one craft in an object table this
 * file owns and gives it to a player; no game data is read. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/flight_player.h"
#include "xvt/flight/player/player.h"

enum {
	TEST_SLOTS = 8,
	TEST_CRAFT_SLOT = 3,
	TEST_PLAYER = 5,
	TEST_HEALTHY = 100,
};

static struct object_record g_test_objects[TEST_SLOTS];
static struct mobile_object g_test_mobiles[TEST_SLOTS];
static struct craft_data g_test_craft;

/* A craft in slot 3 flown by player 5, who is also the local player. Every
 * subsystem is installed and healthy, and each sits on the damage display row
 * of its own number. */
static struct craft_data *fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(&g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	g_object_table = g_test_objects;
	for (int i = 0; i < TEST_SLOTS; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
	}
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	g_test_objects[TEST_CRAFT_SLOT].object_type = 1;
	g_test_objects[TEST_CRAFT_SLOT].player_owner_idx = TEST_PLAYER;
	g_test_mobiles[TEST_CRAFT_SLOT].p_craft = &g_test_craft;
	g_players[TEST_PLAYER].object_index = TEST_CRAFT_SLOT;
	g_local_player = TEST_PLAYER;
	g_test_craft.system_flags = CRAFT_SUBSYSTEM_FLAGS_ALL;
	for (int system = 0; system < DAMAGE_SYSTEM_ID_COUNT; ++system) {
		g_test_craft.system_display_slot_by_system[system] =
			(uint8_t)system;
		g_test_craft.system_health[system] = TEST_HEALTHY;
	}
	return &g_test_craft;
}

/* A craft with every installed system working has none disabled; one whose
 * cannon system's health is 0 has one, whatever rows the damage display puts
 * the systems on. */
static void check_disabled_subsystem(void)
{
	struct craft_data *craft = fresh_world();
	XVT_ASSERT_INT_EQ(flight_player_has_disabled_subsystem(), 0);

	craft->system_health[DAMAGE_SYSTEM_03_CANNON_SYSTEM] = 0;
	XVT_ASSERT_INT_EQ(flight_player_has_disabled_subsystem(), 1);

	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		craft->system_display_slot_by_system[system] =
			(uint8_t)(CRAFT_SUBSYSTEM_COUNT - 1 - system);
	}
	XVT_ASSERT_INT_EQ(flight_player_has_disabled_subsystem(), 1);
}

/* A system with health 0 counts only when it is installed: its flag set in
 * system_flags. */
static void check_disabled_subsystem_not_installed(void)
{
	struct craft_data *craft = fresh_world();
	craft->system_health[DAMAGE_SYSTEM_03_CANNON_SYSTEM] = 0;
	craft->system_flags =
		CRAFT_SUBSYSTEM_FLAGS_ALL & ~CRAFT_SUBSYSTEM_FLAG_CANNONS;
	XVT_ASSERT_INT_EQ(flight_player_has_disabled_subsystem(), 0);

	craft->system_flags = CRAFT_SUBSYSTEM_FLAG_CANNONS;
	XVT_ASSERT_INT_EQ(flight_player_has_disabled_subsystem(), 1);
}

/* The answer is 0 when the local player has no craft, or when the object has
 * no craft record, whatever the craft's systems. */
static void check_disabled_subsystem_without_craft(void)
{
	struct craft_data *craft = fresh_world();
	craft->system_health[DAMAGE_SYSTEM_03_CANNON_SYSTEM] = 0;
	g_players[TEST_PLAYER].object_index = -1;
	XVT_ASSERT_INT_EQ(flight_player_has_disabled_subsystem(), 0);

	g_players[TEST_PLAYER].object_index = TEST_CRAFT_SLOT;
	g_test_mobiles[TEST_CRAFT_SLOT].p_craft = NULL;
	XVT_ASSERT_INT_EQ(flight_player_has_disabled_subsystem(), 0);
}

/* A step up adds to the throttle, and a step of 0 adds nothing; a sum that
 * would wrap past 0xFFFF holds there, the exact wrap to 0 included. */
static void check_throttle_increase(void)
{
	struct craft_data *craft = fresh_world();
	craft->throttle_speed = 0x1000;
	flight_player_increase_throttle_speed(0x100, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x1100);
	flight_player_increase_throttle_speed(0, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x1100);

	craft->throttle_speed = 0xFF00;
	flight_player_increase_throttle_speed(0xFF, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0xFFFF);

	craft->throttle_speed = 0xFF00;
	flight_player_increase_throttle_speed(0x100, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0xFFFF);

	craft->throttle_speed = 0xFF00;
	flight_player_increase_throttle_speed(0x200, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0xFFFF);
}

/* A step down takes from the throttle, and a step of 0 takes nothing; a
 * result that would wrap below 0 holds at 0. */
static void check_throttle_decrease(void)
{
	struct craft_data *craft = fresh_world();
	craft->throttle_speed = 0x1100;
	flight_player_decrease_throttle_speed(0x100, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x1000);
	flight_player_decrease_throttle_speed(0, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x1000);

	craft->throttle_speed = 0x100;
	flight_player_decrease_throttle_speed(0x100, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);

	craft->throttle_speed = 0x80;
	flight_player_decrease_throttle_speed(0x100, TEST_PLAYER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
}

int main(void)
{
	check_disabled_subsystem();
	check_disabled_subsystem_not_installed();
	check_disabled_subsystem_without_craft();
	check_throttle_increase();
	check_throttle_decrease();
	return 0;
}

/* Tests for the cockpit paths of xvt/flight/hud/hud.c that ask which computer
 * craft attack (ai_brain_attack_target): the attack light and the incoming
 * warhead warning (hud_update_threat_indicators) and the target's warhead
 * light (hud_draw_cmd_target_status_indicators). Each check builds its world
 * in an object table this file owns: the local player, player 0, flies the
 * craft in PLAYER_CRAFT; the computer craft in ATTACKER carries linked laser
 * cannons and one launcher of warheads and attacks nothing until a check aims
 * it; OTHER is a craft nobody attacks. The drawing goes to functions here that
 * draw nothing and the surface lock count stays above 0, so no screen is
 * touched; the values the cockpit shows are read back from
 * g_hud_element_state_cache, which every check starts unset. No game data is
 * read. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/render/renderer.h"

enum {
	CRAFT_SLOTS = 8,
	PLAYER_CRAFT = 1,
	ATTACKER = 2,
	OTHER = 3,
	TEST_XWING = 1,
	/* The cockpit elements, in instrument set 0. */
	ATTACK_LIGHT = 90,
	WARNING = 93,
	TARGET_WARHEADS = 100,
	/* A laser cannon's projectile type the attack light counts. */
	LASER = 0x8B,
	/* A lock past 944 ticks holds the warning steady; under it, it blinks. */
	LOCKED = 945,
	BUILDING = 300,
	/* A mission subsecond tick on which a blinking state is lit. */
	LIT_TICK = 59,
	SPRITE_COUNT = 265,
};

static struct object_record g_test_objects[CRAFT_SLOTS];
static struct mobile_object g_test_mobiles[CRAFT_SLOTS];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static uint8_t g_test_sprite[4];

/* The three activities of flying home, when a craft's AI target is its
 * mothership. */
static const uint8_t k_homeward[] = {
	AI_ACTIVITY_GOING_HOME,
	AI_ACTIVITY_GOING_HOME_EVADING,
	AI_ACTIVITY_ENTERING_HANGAR,
};

static void draw_nothing(uint8_t *rle_data, int x, int y, int end_marker,
			 int mirror)
{
	(void)rle_data;
	(void)x;
	(void)y;
	(void)end_marker;
	(void)mirror;
}

static void draw_no_char(uint8_t ch) { (void)ch; }

static void fill_nothing(void) {}

/* The world described at the top of this file, at a lit subsecond tick. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups[0]);
	memset(&g_mission_elapsed_clock, 0, sizeof g_mission_elapsed_clock);
	g_mission_elapsed_clock.subsecond_ticks = LIT_TICK;
	g_object_table = g_test_objects;
	for (int slot = 0; slot < CRAFT_SLOTS; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		g_test_craft[slot].ai_controller.target_obj_idx = UINT16_MAX;
		g_test_craft[slot].last_attacker_obj_idx = UINT16_MAX;
		g_test_craft[slot].beam_target_obj_idx = -1;
	}
	static const int filled[] = {PLAYER_CRAFT, ATTACKER, OTHER};
	for (size_t k = 0; k < sizeof filled / sizeof filled[0]; ++k) {
		g_test_objects[filled[k]].object_type = TEST_XWING;
		g_test_craft[filled[k]].working_subsystems = 0x03FF;
		g_test_craft[filled[k]].hull_max = 100;
	}
	for (int player = 0; player < 8; ++player) {
		g_players[player].object_index = -1;
		g_players[player].current_target_object_idx = -1;
	}
	g_local_player = 0;
	g_players[0].object_index = PLAYER_CRAFT;
	g_test_objects[PLAYER_CRAFT].player_owner_idx = 0;
	struct craft_data *attacker = &g_test_craft[ATTACKER];
	attacker->cannon_group_count = 1;
	attacker->laser_state.projectile_type_id[0] = LASER;
	attacker->laser_state.link_mode[0] = 1;
	attacker->warhead_launcher_count = 1;
	attacker->warhead_slot_type_ids[0] = 1;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_hud_instrument_set_base_index = 0;
	memset(g_hud_element_state_cache, 0xFF,
	       sizeof g_hud_element_state_cache);
	for (int sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
		g_hud_panel_sprite_data_by_index[sprite] = g_test_sprite;
	}
	for (int label = 0; label < 4; ++label) {
		g_str_threat_display_text[label] = "";
	}
	g_flight_blit_sprite_fn = draw_nothing;
	g_flight_draw_char_fn = draw_no_char;
	g_flight_fill_clip_rect_fn = fill_nothing;
	g_flight_render_to_frontend = 0;
	g_surface_lock_count = 2;
}

/* Gives ATTACKER the AI target target, the activity activity and the warhead
 * lock lock. */
static void aim(uint16_t target, uint8_t activity, int16_t lock)
{
	g_test_craft[ATTACKER].ai_controller.target_obj_idx = target;
	g_test_craft[ATTACKER].ai_controller.activity = activity;
	g_test_craft[ATTACKER].warhead_lock_ticks = lock;
}

/* The attack light and warning hud_update_threat_indicators shows for aim's
 * arguments, as attack * 10 + warning. */
static int threats_for(uint16_t target, uint8_t activity, int16_t lock)
{
	fresh_world();
	aim(target, activity, lock);
	hud_update_threat_indicators(0);
	return g_hud_element_state_cache[ATTACK_LIGHT] * 10 +
	       g_hud_element_state_cache[WARNING];
}

/* The target's warhead light hud_draw_cmd_target_status_indicators shows for
 * ATTACKER, the player's target, with aim's arguments. */
static int target_light_for(uint16_t target, uint8_t activity, int16_t lock)
{
	fresh_world();
	aim(target, activity, lock);
	g_players[0].current_target_object_idx = ATTACKER;
	hud_draw_cmd_target_status_indicators();
	return g_hud_element_state_cache[TARGET_WARHEADS];
}

/* A computer craft whose AI target is the player's craft lights the attack
 * light whatever its maneuver, and its lock sets the warning: 2 past 944
 * ticks, 1 on a lit tick while it builds, 0 at none. */
static void check_attacking_the_player(void)
{
	XVT_ASSERT_INT_EQ(threats_for(PLAYER_CRAFT, AI_ACTIVITY_NONE, 0), 10);
	XVT_ASSERT_INT_EQ(threats_for(PLAYER_CRAFT, AI_ACTIVITY_NONE, LOCKED),
			  12);
	XVT_ASSERT_INT_EQ(threats_for(PLAYER_CRAFT, AI_ACTIVITY_NONE, BUILDING),
			  11);
	XVT_ASSERT_INT_EQ(
		threats_for(PLAYER_CRAFT, AI_ACTIVITY_IN_FORMATION, LOCKED),
		12);
}

/* A craft aimed at another craft, aimed at nothing or flying home lights
 * neither the attack light nor the warning, whatever its lock. */
static void check_not_attacking_the_player(void)
{
	XVT_ASSERT_INT_EQ(threats_for(OTHER, AI_ACTIVITY_NONE, LOCKED), 0);
	XVT_ASSERT_INT_EQ(threats_for(UINT16_MAX, AI_ACTIVITY_NONE, LOCKED), 0);
	for (size_t k = 0; k < sizeof k_homeward; ++k) {
		XVT_ASSERT_INT_EQ(
			threats_for(PLAYER_CRAFT, k_homeward[k], LOCKED), 0);
	}
}

/* The player's target carries warheads: its warhead light shows 1 while it
 * attacks any craft and 2 on a lit tick while its lock builds; it shows 0
 * when the target attacks nothing or is flying home. */
static void check_target_warhead_light(void)
{
	XVT_ASSERT_INT_EQ(target_light_for(OTHER, AI_ACTIVITY_NONE, 0), 1);
	XVT_ASSERT_INT_EQ(target_light_for(PLAYER_CRAFT, AI_ACTIVITY_NONE, 0),
			  1);
	XVT_ASSERT_INT_EQ(target_light_for(OTHER, AI_ACTIVITY_NONE, BUILDING),
			  2);
	XVT_ASSERT_INT_EQ(
		target_light_for(UINT16_MAX, AI_ACTIVITY_NONE, BUILDING), 0);
	for (size_t k = 0; k < sizeof k_homeward; ++k) {
		XVT_ASSERT_INT_EQ(target_light_for(OTHER, k_homeward[k], 0), 0);
	}
}

int main(void)
{
	check_attacking_the_player();
	check_not_attacking_the_player();
	check_target_warhead_light();
	return 0;
}

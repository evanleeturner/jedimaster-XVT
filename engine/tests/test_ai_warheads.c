/* Tests for xvt/flight/ai_warheads.c: a computer craft's lock, counted by the
 * player's rule in laser_weaponsfire, and its warheads in the pilot
 * program's view. Each check builds its world in an object table this file
 * owns: the computer craft in LOCKER faces down the world's negative Y axis,
 * its side axis down negative X and its up axis down negative Z, each
 * component exactly -1 (as test_targeting.c's player), carries one launcher
 * of warheads, and has TARGET as its AI target; shots sit in SHOT_START and
 * up. Model sizes and the types' models are set by hand, and no type claims
 * mesh data, so a mesh's center is its craft's position; no game data is
 * read. A step is 8 ticks, the native step, unless a check says otherwise. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_warheads.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/ai/ai_wire.h"
#include "xvt_runtime/timing/flight_timing.h"

enum {
	/* More craft slots than the game's 32, to hold one past them. */
	CRAFT_SLOTS = 40,
	SHOT_START = CRAFT_SLOTS,
	SHOT_SLOTS = 258,
	SHOT_END = SHOT_START + SHOT_SLOTS,
	LOCKER = 1,
	TARGET = 4,
	OTHER = 5,
	PAST_CRAFT_SLOTS = 32,
	LOCKER_TYPE = 1,
	TARGET_TYPE = 2,
	LOCKER_MODEL = 5,
	TARGET_MODEL = 6,
	BOAT_MODEL = 7,
	FIRST_WARHEAD_SLOT = 2,
	MINUS_ONE_Q15 = -32768,
	/* The computer craft sits off the axes. */
	LOCKER_X = 123456,
	LOCKER_Y = -234560,
	LOCKER_Z = 345600,
	AHEAD = 50000,
	STEP = 8,
	LOCK_TICKS = 708,
	BOAT_LOCK_TICKS = 354,
	JEDI = 5,
	MISSILE = WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE,
	FLARE = COUNTERMEASURE_PROJECTILE_OBJECT_TYPE,
};

static struct object_record g_test_objects[SHOT_END];
static struct mobile_object g_test_mobiles[SHOT_END];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static struct warhead_guidance_state g_test_guidance[SHOT_SLOTS];
/* Each world's craft get signatures no earlier world gave, so the module's
 * kept locks of an earlier check never match. */
static uint16_t g_next_signature = 0x100;

/* Puts a craft of the target's type and genus forward, side and up of the
 * computer craft, in world units along its axes. */
static void place(int slot, int genus, int forward, int side, int up)
{
	g_test_objects[slot].object_type = TARGET_TYPE;
	g_test_objects[slot].genus_id = (uint8_t)genus;
	g_test_objects[slot].object_signature = ++g_next_signature;
	g_test_objects[slot].world_x = LOCKER_X - side;
	g_test_objects[slot].world_y = LOCKER_Y - forward;
	g_test_objects[slot].world_z = LOCKER_Z - up;
	g_test_craft[slot].model_index = TARGET_MODEL;
}

/* Makes the craft in slot a computer craft like LOCKER, at LOCKER's place,
 * with warheads left in its one launcher and target as its AI target. */
static void arm(int slot, int warheads, uint16_t target)
{
	g_test_objects[slot].object_type = LOCKER_TYPE;
	g_test_objects[slot].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_test_objects[slot].object_signature = ++g_next_signature;
	g_test_objects[slot].world_x = LOCKER_X;
	g_test_objects[slot].world_y = LOCKER_Y;
	g_test_objects[slot].world_z = LOCKER_Z;
	g_test_mobiles[slot].cached_fwd_y = MINUS_ONE_Q15;
	g_test_mobiles[slot].cached_side_x = MINUS_ONE_Q15;
	g_test_mobiles[slot].cached_up_z = MINUS_ONE_Q15;
	g_test_craft[slot].model_index = LOCKER_MODEL;
	g_test_craft[slot].warhead_launcher_count = 1;
	g_test_craft[slot].weapon_slots[FIRST_WARHEAD_SLOT].ammo_count =
		(int16_t)warheads;
	g_test_craft[slot].ai_controller.target_obj_idx = target;
}

/* LOCKER armed with 2 warheads, TARGET a starfighter AHEAD of it, OTHER a
 * starfighter ahead too; native timing. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups[0]);
	memset(g_players, 0, sizeof g_players);
	g_object_table = g_test_objects;
	for (int slot = 0; slot < SHOT_END; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		if (slot < CRAFT_SLOTS) {
			g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		} else {
			g_test_mobiles[slot].p_warhead_guidance =
				&g_test_guidance[slot - SHOT_START];
		}
	}
	for (int player = 0; player < 8; ++player) {
		g_players[player].object_index = -1;
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_projectile_object_slot_start = SHOT_START;
	g_projectile_object_slot_end = SHOT_END;
	g_object_type_table[LOCKER_TYPE].model_index = LOCKER_MODEL;
	g_object_type_table[TARGET_TYPE].model_index = TARGET_MODEL;
	g_object_type_table[CRAFT_SPECIES_MISSILE_BOAT].model_index =
		BOAT_MODEL;
	/* No model is loaded, so no type may claim mesh data. */
	g_object_type_table[LOCKER_TYPE].asset_flags = 0;
	g_object_type_table[TARGET_TYPE].asset_flags = 0;
	g_object_type_table[CRAFT_SPECIES_MISSILE_BOAT].asset_flags = 0;
	g_model_defs[LOCKER_MODEL].warhead_launcher_first_slot[0] =
		FIRST_WARHEAD_SLOT;
	g_model_defs[TARGET_MODEL].bound_size_x = 600;
	g_model_defs[TARGET_MODEL].bound_size_y = 600;
	g_model_defs[TARGET_MODEL].bound_size_z = 600;
	g_model_defs[TARGET_MODEL].bound_size_shift = 4;
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_NATIVE);
	g_elapsed_ticks = STEP;
	arm(LOCKER, 2, TARGET);
	place(TARGET, CRAFT_GENUS_STARFIGHTER, AHEAD, 0, 0);
	place(OTHER, CRAFT_GENUS_STARFIGHTER, AHEAD, 100, 0);
}

static int16_t lock_after_steps(int steps)
{
	for (int step = 0; step < steps; ++step) {
		ai_warheads_count_lock(LOCKER);
	}
	return g_test_craft[LOCKER].warhead_lock_ticks;
}

static struct ai_wire_craft_view view_of(uint16_t slot)
{
	struct ai_wire_craft_view view;
	memset(&view, 0, sizeof view);
	ai_warheads_view_craft(slot, &view);
	return view;
}

/* In aim, each step adds its ticks; the lock counts until 708 and is
 * declared from there, as a player's (laser.c's DEFAULT_LOCK_TICKS). */
static void check_lock_gains_in_aim(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(lock_after_steps(1), STEP);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_COUNTING);
	XVT_ASSERT_INT_EQ(lock_after_steps(87), 704);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_COUNTING);
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 712);
	struct ai_wire_craft_view view = view_of(LOCKER);
	XVT_ASSERT_INT_EQ(view.lock_state, AI_WIRE_LOCK_DECLARED);
	XVT_ASSERT_INT_EQ(view.lock_ticks, 712);
	XVT_ASSERT_INT_EQ(view.lock_needed, LOCK_TICKS);
	/* Exactly 708 is declared. */
	fresh_world();
	lock_after_steps(1);
	g_test_craft[LOCKER].warhead_lock_ticks = LOCK_TICKS - STEP;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), LOCK_TICKS);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_DECLARED);
}

/* A Missile Boat's lock is declared at 354 ticks, a player's Missile
 * Boat's. */
static void check_missile_boat_lock(void)
{
	fresh_world();
	g_test_objects[LOCKER].object_type = CRAFT_SPECIES_MISSILE_BOAT;
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_needed, BOAT_LOCK_TICKS);
	XVT_ASSERT_INT_EQ(lock_after_steps(44), 352);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_COUNTING);
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 360);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_DECLARED);
}

/* Chaff working on the target takes half of each step back; chaff with no
 * seconds left, or a flare, takes nothing. */
static void check_chaff_takes_half(void)
{
	fresh_world();
	struct craft_data *target = &g_test_craft[TARGET];
	target->cm_type_id = COUNTERMEASURE_TYPE_CHAFF;
	target->chaff_active_seconds = 3;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), STEP / 2);
	target->chaff_active_seconds = 0;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), STEP / 2 + STEP);
	target->cm_type_id = COUNTERMEASURE_TYPE_FLARE;
	target->chaff_active_seconds = 3;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), STEP / 2 + 2 * STEP);
}

/* Out of aim, a lock falls by half a step plus the step, to 0 at least,
 * and a declared lock is no longer declared. */
static void check_lock_falls_out_of_aim(void)
{
	fresh_world();
	lock_after_steps(89);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_DECLARED);
	place(TARGET, CRAFT_GENUS_STARFIGHTER, -AHEAD, 0, 0);
	/* The lock is kept for the craft and its target's slot, so moving the
	 * target keeps it. */
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 712 - STEP / 2 - STEP);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_NONE);
	g_test_craft[LOCKER].warhead_lock_ticks = 10;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 0);
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 0);
}

/* The lock range is 101,805 at a starfighter and 244,332 at a freighter,
 * starship or platform; a starship is measured at its mesh's center, which
 * is its position here (no model data). */
static void check_lock_range_by_genus(void)
{
	static const struct {
		int genus;
		int forward;
		int side;
		int16_t lock;
	} cases[] = {
		{CRAFT_GENUS_STARFIGHTER, 95000, 0, 100 + STEP},
		{CRAFT_GENUS_STARFIGHTER, 110000, 0, 100 - STEP / 2 - STEP},
		{CRAFT_GENUS_FREIGHTER, 110000, 0, 100 + STEP},
		{CRAFT_GENUS_STARSHIP, 110000, 0, 100 + STEP},
		{CRAFT_GENUS_PLATFORM, 235000, 0, 100 + STEP},
		{CRAFT_GENUS_STARSHIP, 255000, 0, 100 - STEP / 2 - STEP},
		/* Off the nose: inside the wide cone, outside the narrow one;
		 * then outside both. */
		{CRAFT_GENUS_STARFIGHTER, 50000, 20000, 100 + STEP},
		{CRAFT_GENUS_STARFIGHTER, 50000, 30000, 100 - STEP / 2 - STEP},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fresh_world();
		place(TARGET, cases[i].genus, cases[i].forward, cases[i].side,
		      0);
		lock_after_steps(1);
		g_test_craft[LOCKER].warhead_lock_ticks = 100;
		XVT_ASSERT_INT_EQ(lock_after_steps(1), cases[i].lock);
	}
}

/* No target, a target slot that is empty or no craft slot, or no warheads
 * left in any launcher: the lock is held at 0. The empty slot's record and
 * the shot past the craft slots lie dead ahead, so counting either as a
 * target would gain. */
static void check_lock_held_at_zero(void)
{
	static const uint16_t targets[] = {UINT16_MAX, 9, CRAFT_SLOTS};
	for (size_t i = 0; i < sizeof targets / sizeof targets[0]; ++i) {
		fresh_world();
		g_test_craft[LOCKER].ai_controller.target_obj_idx = targets[i];
		place(9, CRAFT_GENUS_STARFIGHTER, AHEAD, 0, 0);
		g_test_objects[9].object_type = 0;
		/* Slot CRAFT_SLOTS holds a shot: filled, but no craft slot. */
		g_test_objects[CRAFT_SLOTS].object_type = MISSILE;
		g_test_objects[CRAFT_SLOTS].world_x = LOCKER_X;
		g_test_objects[CRAFT_SLOTS].world_y = LOCKER_Y - AHEAD;
		g_test_objects[CRAFT_SLOTS].world_z = LOCKER_Z;
		g_test_craft[LOCKER].warhead_lock_ticks = 300;
		XVT_ASSERT_INT_EQ(lock_after_steps(1), 0);
	}
	fresh_world();
	lock_after_steps(89);
	g_test_craft[LOCKER].weapon_slots[FIRST_WARHEAD_SLOT].ammo_count = 0;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 0);
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_NONE);
	g_test_craft[LOCKER].weapon_slots[FIRST_WARHEAD_SLOT + 1].ammo_count =
		1;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), STEP);
}

/* A new AI target starts the count over, as a player's new target does
 * (player_set_target), and the view shows no lock until the next step. */
static void check_new_target_starts_over(void)
{
	fresh_world();
	lock_after_steps(1);
	g_test_craft[LOCKER].warhead_lock_ticks = 400;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 400 + STEP);
	g_test_craft[LOCKER].ai_controller.target_obj_idx = OTHER;
	XVT_ASSERT_INT_EQ(view_of(LOCKER).lock_state, AI_WIRE_LOCK_NONE);
	XVT_ASSERT_INT_EQ(lock_after_steps(1), STEP);
}

/* The count stops at 32,767 rather than wrap negative. */
static void check_lock_stops_at_ceiling(void)
{
	fresh_world();
	lock_after_steps(1);
	g_test_craft[LOCKER].warhead_lock_ticks = INT16_MAX - 3;
	XVT_ASSERT_INT_EQ(lock_after_steps(1), INT16_MAX);
	XVT_ASSERT_INT_EQ(lock_after_steps(1), INT16_MAX);
}

/* At unlocked timing a step is 1 tick, and a half carries its odd tick to
 * the next step in the same mode: under chaff, three steps gain 3 and take
 * back 0, 1 and 0. A step after a skipped serial drops the odd tick it
 * carried, and takes back 0 again. */
static void check_half_carries_odd_tick(void)
{
	fresh_world();
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 1;
	g_test_craft[TARGET].cm_type_id = COUNTERMEASURE_TYPE_CHAFF;
	g_test_craft[TARGET].chaff_active_seconds = 3;
	static const int16_t expected[] = {1, 1, 2};
	for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
		xvt_flight_timing_begin_advance(1);
		XVT_ASSERT_INT_EQ(lock_after_steps(1), expected[i]);
		xvt_flight_timing_end_advance();
	}
	xvt_flight_timing_begin_advance(1);
	xvt_flight_timing_end_advance();
	xvt_flight_timing_begin_advance(1);
	XVT_ASSERT_INT_EQ(lock_after_steps(1), 3);
	xvt_flight_timing_end_advance();
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_NATIVE);
}

/* The skill level is the craft's AI rank, which tells Jedi from Top Ace and
 * a Sith Lord (7) from the Apprentices of its group (6). */
static void check_view_skill_level(void)
{
	fresh_world();
	g_test_craft[LOCKER].ai_rank = JEDI;
	XVT_ASSERT_INT_EQ(view_of(LOCKER).skill_level, JEDI);
	g_test_craft[LOCKER].ai_rank = 7;
	XVT_ASSERT_INT_EQ(view_of(LOCKER).skill_level, 7);
}

/* A live shot (family 1) of the given type homing at strength tier on
 * target, in the given shot slot, with the given damage. */
static void place_shot(int slot, int type, int tier, uint16_t target,
		       unsigned int damage)
{
	g_test_objects[slot].object_type = (uint8_t)type;
	g_test_mobiles[slot].family = 1;
	g_test_mobiles[slot].damage_amount = damage;
	g_test_guidance[slot - SHOT_START].homing_tier = (uint8_t)tier;
	g_test_guidance[slot - SHOT_START].target_obj_idx = target;
}

/* A contact counts the homing warheads at it and sums their damage; a shot
 * that does not home, a shot at another craft, a flare and an empty slot
 * are not counted. */
static void check_view_contact_warheads_coming(void)
{
	fresh_world();
	place_shot(SHOT_START, MISSILE, 2, TARGET, 3200);
	place_shot(SHOT_START + 1, MISSILE, 0, TARGET, 3000);
	place_shot(SHOT_START + 2, MISSILE, 3, OTHER, 3000);
	place_shot(SHOT_START + 3, FLARE, 6, TARGET, 10);
	place_shot(SHOT_START + 4, 0, 6, TARGET, 10);
	place_shot(SHOT_END - 1, MISSILE, 6, TARGET, 3100);
	struct ai_wire_contact contact;
	memset(&contact, 0xFF, sizeof contact);
	ai_warheads_view_contact(TARGET, &contact);
	XVT_ASSERT_INT_EQ(contact.warheads_coming, 2);
	XVT_ASSERT_INT_EQ(contact.warheads_coming_damage, 6300);
	memset(&contact, 0xFF, sizeof contact);
	ai_warheads_view_contact(LOCKER, &contact);
	XVT_ASSERT_INT_EQ(contact.warheads_coming, 0);
	XVT_ASSERT_INT_EQ(contact.warheads_coming_damage, 0);
}

/* A warhead that has blown up (family 5) keeps its homing data for a
 * moment; a contact does not count it or its damage, and a live warhead
 * (family 1) still counts. */
static void check_view_contact_skips_blown_up_warheads(void)
{
	fresh_world();
	place_shot(SHOT_START, MISSILE, 2, TARGET, 3200);
	g_test_mobiles[SHOT_START].family = 5;
	place_shot(SHOT_START + 1, MISSILE, 3, TARGET, 3000);
	struct ai_wire_contact contact;
	memset(&contact, 0xFF, sizeof contact);
	ai_warheads_view_contact(TARGET, &contact);
	XVT_ASSERT_INT_EQ(contact.warheads_coming, 1);
	XVT_ASSERT_INT_EQ(contact.warheads_coming_damage, 3000);
}

/* The count stops at 255; the damage keeps summing. */
static void check_view_contact_count_cap(void)
{
	fresh_world();
	for (int slot = SHOT_START; slot < SHOT_END; ++slot) {
		place_shot(slot, MISSILE, 1, TARGET, 1);
	}
	struct ai_wire_contact contact;
	ai_warheads_view_contact(TARGET, &contact);
	XVT_ASSERT_INT_EQ(contact.warheads_coming, UINT8_MAX);
	XVT_ASSERT_INT_EQ(contact.warheads_coming_damage, SHOT_SLOTS);
}

/* Slot 32, past the game's 32 craft slots, is left alone. */
static void check_past_craft_slots(void)
{
	fresh_world();
	arm(PAST_CRAFT_SLOTS, 2, TARGET);
	ai_warheads_count_lock(PAST_CRAFT_SLOTS);
	XVT_ASSERT_INT_EQ(g_test_craft[PAST_CRAFT_SLOTS].warhead_lock_ticks, 0);
	XVT_ASSERT_INT_EQ(view_of(PAST_CRAFT_SLOTS).lock_state,
			  AI_WIRE_LOCK_NONE);
}

int main(void)
{
	check_lock_gains_in_aim();
	check_missile_boat_lock();
	check_chaff_takes_half();
	check_lock_falls_out_of_aim();
	check_lock_range_by_genus();
	check_lock_held_at_zero();
	check_new_target_starts_over();
	check_lock_stops_at_ceiling();
	check_half_carries_odd_tick();
	check_view_skill_level();
	check_view_contact_warheads_coming();
	check_view_contact_skips_blown_up_warheads();
	check_view_contact_count_cap();
	check_past_craft_slots();
	return 0;
}

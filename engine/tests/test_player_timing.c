/* Checks the per-player step remainders (xvt_runtime/timing/player_timing.h)
 * against the promises in its header, on a world this file builds itself: six
 * live objects with mobile records, slots 0 to 3 in the main region and slot 4
 * in the static region; player 0 (the local player) flies slot 0, whose craft
 * has working flight controls, and player 1 flies slot 1. No game data is read.
 * Each case starts from that world, cleared player timing and a new flight
 * timing session.
 *
 * Most checks watch one channel's carried remainder through Scale itself: Seed
 * clears a channel and leaves a carry of 3/4 on it, and Probe adds 1/4 more, so
 * Probe returns 1 exactly when the carry survived. */
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

enum { SLOTS = 6, MAIN_SLOTS = 4 };

static struct object_record g_test_objects[SLOTS];
static struct mobile_object g_test_mobiles[SLOTS];
static struct craft_data g_test_craft;

static void fresh_world(xvt_flight_timing_profile profile)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(&g_test_craft, 0, sizeof g_test_craft);
	for (int i = 0; i < SLOTS; ++i) {
		g_test_objects[i].object_type = 1;
		g_test_objects[i].object_signature = (uint16_t)(0x200 + i);
		g_test_objects[i].world_x = 1000 * i;
		g_test_objects[i].world_y = -100 * i;
		g_test_objects[i].world_z = 10 * i;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].prev_world_x = -7 - i;
		g_test_mobiles[i].prev_world_y = -8 - i;
		g_test_mobiles[i].prev_world_z = -9 - i;
	}
	g_test_mobiles[0].p_craft = &g_test_craft;
	g_test_craft.working_subsystems = CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS;
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = MAIN_SLOTS;
	g_region_static_object_slot_count = 1;
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
	}
	g_players[0].object_index = 0;
	g_players[0].current_target_object_idx = 2;
	g_players[1].object_index = 1;
	g_local_player = 0;
	g_flight_key_mods = 0;
	g_elapsed_ticks = 1;
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	xvt_flight_timing_begin_session(profile);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_init(SLOTS), 1);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SLOTS), 1);
	xvt_player_timing_reset();
}

static void seed(unsigned player, unsigned channel)
{
	xvt_player_timing_clear(player, channel);
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(player, channel, 3, 1, 4), 0);
}

static int probe(unsigned player, unsigned channel)
{
	return xvt_player_timing_scale(player, channel, 1, 1, 4);
}

/* Opens the next simulation step of `ticks` ticks. */
static void step(uint16_t ticks)
{
	xvt_flight_timing_end_advance();
	xvt_flight_timing_begin_advance(ticks);
}

static void check_scale(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* Truncated toward zero. */
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(0, XVT_PLAYER_YAW, -7, 1, 4),
			  -1);
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(0, XVT_PLAYER_PITCH, 7, 1, 4),
			  1);

	/* Ten short steps add up to one long one. */
	int sum = 0;
	int negative = 0;
	for (int i = 0; i < 10; ++i) {
		sum += xvt_player_timing_scale(0, XVT_PLAYER_ROLL, 7, 1, 4);
		negative +=
			xvt_player_timing_scale(1, XVT_PLAYER_ROLL, -7, 1, 4);
	}
	XVT_ASSERT_INT_EQ(
		sum, xvt_player_timing_scale(0, XVT_PLAYER_ZOOM, 7, 10, 4));
	XVT_ASSERT_INT_EQ(sum, 70 / 4);
	XVT_ASSERT_INT_EQ(negative, -70 / 4);

	/* A new sign drops the carry; zero is a sign of its own. */
	seed(0, XVT_PLAYER_DISTANCE);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_scale(0, XVT_PLAYER_DISTANCE, -1, 1, 4), 0);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_scale(0, XVT_PLAYER_DISTANCE, -3, 1, 4), -1);
	seed(0, XVT_PLAYER_SLEW_YAW);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_scale(0, XVT_PLAYER_SLEW_YAW, 0, 1, 4), 0);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_scale(0, XVT_PLAYER_SLEW_YAW, 1, 1, 4), 0);

	/* Clamped to int; 0 for a zero divisor or a channel past the enum. */
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(0, XVT_PLAYER_CAMERA_YAW,
						  INT_MAX, 4, 1),
			  INT_MAX);
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(0, XVT_PLAYER_CAMERA_YAW,
						  INT_MIN, 4, 1),
			  INT_MIN);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_scale(0, XVT_PLAYER_CAMERA_PITCH, 1000, 1, 0),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_scale(0, XVT_PLAYER_CHANNELS, 1000, 1, 1), 0);

	/* A player out of range gets the plain result with no carry. */
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(XVT_FLIGHT_PLAYERS,
						  XVT_PLAYER_YAW, 3, 1, 4),
			  0);
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(XVT_FLIGHT_PLAYERS,
						  XVT_PLAYER_YAW, 3, 1, 4),
			  0);
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(XVT_FLIGHT_PLAYERS,
						  XVT_PLAYER_YAW, 7, 3, 4),
			  5);
}

static void check_clear_and_reset(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	seed(0, XVT_PLAYER_YAW);
	seed(0, XVT_PLAYER_PITCH);
	xvt_player_timing_clear(0, XVT_PLAYER_YAW);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_PITCH), 1);

	seed(0, XVT_PLAYER_YAW);
	seed(1, XVT_PLAYER_YAW);
	seed(5, XVT_PLAYER_YAW);
	xvt_player_timing_reset();
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(probe(1, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(probe(5, XVT_PLAYER_YAW), 0);
}

static void check_entry_follows_object(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* The object in the main region changes signature: the entry is cleared. */
	seed(0, XVT_PLAYER_YAW);
	g_test_objects[0].object_signature = 0x999;
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 0);

	/* The player moves to another main slot: cleared. */
	seed(1, XVT_PLAYER_YAW);
	g_players[1].object_index = 2;
	XVT_ASSERT_INT_EQ(probe(1, XVT_PLAYER_YAW), 0);

	/* An object outside the main region, or none: the entry is kept. */
	g_players[2].object_index = MAIN_SLOTS;
	seed(2, XVT_PLAYER_YAW);
	g_test_objects[MAIN_SLOTS].object_signature = 0x999;
	XVT_ASSERT_INT_EQ(probe(2, XVT_PLAYER_YAW), 1);
	seed(3, XVT_PLAYER_YAW);
	XVT_ASSERT_INT_EQ(probe(3, XVT_PLAYER_YAW), 1);
}

static void check_reset_controls(void)
{
	static const unsigned camera[] = {XVT_PLAYER_CAMERA_YAW,
					  XVT_PLAYER_CAMERA_PITCH,
					  XVT_PLAYER_DISTANCE, XVT_PLAYER_ZOOM};
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	for (unsigned i = 0; i < 4; ++i) {
		seed(0, camera[i]);
	}
	seed(0, XVT_PLAYER_YAW);
	seed(0, XVT_PLAYER_SLEW_YAW);
	seed(1, XVT_PLAYER_CAMERA_YAW);
	xvt_player_timing_reset_controls();
	for (unsigned i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(probe(0, camera[i]), 0);
	}
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 1);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_SLEW_YAW), 1);
	/* Only the local player's. */
	XVT_ASSERT_INT_EQ(probe(1, XVT_PLAYER_CAMERA_YAW), 1);

	fresh_world(XVT_FLIGHT_TIMING_NATIVE);
	seed(0, XVT_PLAYER_ZOOM);
	xvt_player_timing_reset_controls();
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_ZOOM), 0);

	/* In NETWORK_125 it does nothing. */
	fresh_world(XVT_FLIGHT_TIMING_NETWORK_125);
	seed(0, XVT_PLAYER_CAMERA_YAW);
	xvt_player_timing_reset_controls();
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_CAMERA_YAW), 1);
}

/* Seeds player 0's yaw, pitch, roll and slew channels, calls BeginControls, and
 * checks whether the carry survived on each. */
static void expect_controls_keep(int kept)
{
	static const unsigned flight[] = {XVT_PLAYER_YAW, XVT_PLAYER_PITCH,
					  XVT_PLAYER_ROLL, XVT_PLAYER_SLEW_YAW,
					  XVT_PLAYER_SLEW_PITCH};
	for (unsigned i = 0; i < 5; ++i) {
		seed(0, flight[i]);
	}
	xvt_player_timing_begin_controls(0);
	for (unsigned i = 0; i < 5; ++i) {
		XVT_ASSERT_INT_EQ(probe(0, flight[i]), kept);
	}
}

static void check_begin_controls_mode(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* The first call clears; the same mode again keeps; another player is not touched. */
	seed(1, XVT_PLAYER_YAW);
	expect_controls_keep(0);
	expect_controls_keep(1);
	XVT_ASSERT_INT_EQ(probe(1, XVT_PLAYER_YAW), 1);

	/* Each part of the mode is a change: roll modifier held, ... */
	g_flight_key_mods = XVT_ROLL_MODIFIER;
	expect_controls_keep(0);
	expect_controls_keep(1);
	g_flight_key_mods = 0;
	expect_controls_keep(0);

	/* ...flight controls lost, ... */
	g_test_craft.working_subsystems = 0;
	expect_controls_keep(0);
	expect_controls_keep(1);
	g_test_craft.working_subsystems = CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS;
	expect_controls_keep(0);

	/* ...beam_effect_accum[1] set, which disables only while no chaff is active, ... */
	g_test_craft.beam_effect_accum[1] = 1;
	g_test_craft.chaff_active_seconds = 5;
	expect_controls_keep(1);
	g_test_craft.chaff_active_seconds = 0;
	expect_controls_keep(0);
	g_test_craft.beam_effect_accum[1] = 0;
	expect_controls_keep(0);

	/* ...input blocked, map view and hyperspace. */
	g_players[0].view_state.player_input_blocked = 1;
	expect_controls_keep(0);
	g_players[0].view_state.player_input_blocked = 0;
	expect_controls_keep(0);
	g_players[0].map_camera_state = 1;
	expect_controls_keep(0);
	g_players[0].map_camera_state = 0;
	expect_controls_keep(0);
	g_players[0].hyperspace_phase = 2;
	expect_controls_keep(0);
	g_players[0].hyperspace_phase = 0;
	expect_controls_keep(0);
	expect_controls_keep(1);
}

static void check_begin_controls_camera(void)
{
	static const unsigned camera[] = {XVT_PLAYER_CAMERA_YAW,
					  XVT_PLAYER_CAMERA_PITCH,
					  XVT_PLAYER_DISTANCE, XVT_PLAYER_ZOOM};
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_player_timing_begin_controls(0);

	/* A new camera focus clears the camera channels and keeps the others. */
	for (unsigned i = 0; i < 4; ++i) {
		seed(0, camera[i]);
	}
	seed(0, XVT_PLAYER_YAW);
	g_players[0].view_state.camera_focus_obj_idx = 3;
	xvt_player_timing_begin_controls(0);
	for (unsigned i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(probe(0, camera[i]), 0);
	}
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 1);

	/* The same focus keeps them, and so does a change of mode. */
	for (unsigned i = 0; i < 4; ++i) {
		seed(0, camera[i]);
	}
	xvt_player_timing_begin_controls(0);
	g_flight_key_mods = XVT_ROLL_MODIFIER;
	xvt_player_timing_begin_controls(0);
	for (unsigned i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(probe(0, camera[i]), 1);
	}
}

static void check_slew(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* Under 8: all of it, and the carry is dropped. */
	seed(0, XVT_PLAYER_SLEW_YAW);
	XVT_ASSERT_INT_EQ(xvt_player_timing_slew(0, XVT_PLAYER_SLEW_YAW, 5), 5);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_SLEW_YAW), 0);
	XVT_ASSERT_INT_EQ(xvt_player_timing_slew(0, XVT_PLAYER_SLEW_PITCH, -7),
			  -7);

	/* Otherwise 4 * max(1, |difference| / 29) per 8 ticks, with the difference's sign. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 8;
	XVT_ASSERT_INT_EQ(xvt_player_timing_slew(0, XVT_PLAYER_SLEW_YAW, 100),
			  4 * (100 / 29));
	XVT_ASSERT_INT_EQ(xvt_player_timing_slew(1, XVT_PLAYER_SLEW_YAW, -100),
			  -4 * (100 / 29));
	XVT_ASSERT_INT_EQ(xvt_player_timing_slew(1, XVT_PLAYER_SLEW_PITCH, 20),
			  4);
	g_elapsed_ticks = 16;
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_slew(0, XVT_PLAYER_CAMERA_YAW, 1000),
		2 * 4 * (1000 / 29));

	/* One tick at a time it is carried: eight one-tick steps move what one
	 * eight-tick step does. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 1;
	int sum = 0;
	for (int i = 0; i < 8; ++i) {
		sum += xvt_player_timing_slew(0, XVT_PLAYER_SLEW_PITCH, 100);
	}
	XVT_ASSERT_INT_EQ(sum, 4 * (100 / 29));

	/* A step that would reach the difference returns all of it and drops
	 * the carry: 3 ticks of 4 per 8 leave 4/8 carried; 64 more ticks would
	 * pass 10; afterwards 4/8 alone makes nothing. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 3;
	XVT_ASSERT_INT_EQ(xvt_player_timing_slew(0, XVT_PLAYER_SLEW_YAW, 10),
			  1);
	g_elapsed_ticks = 64;
	XVT_ASSERT_INT_EQ(xvt_player_timing_slew(0, XVT_PLAYER_SLEW_YAW, 10),
			  10);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_scale(0, XVT_PLAYER_SLEW_YAW, 4, 1, 8), 0);
}

/* Known failure slew_int_min: the header promises a step for any difference,
 * and INT_MIN's magnitude is 2^31, so with 8 ticks the step is -4 * (2^31 /
 * 29). The code takes abs(INT_MIN), which is undefined behavior, and the
 * sanitizer stops the program there. */
static void check_slew_most_negative(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 8;
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_slew(0, XVT_PLAYER_SLEW_YAW, INT_MIN),
		-4 * ((INT64_C(1) << 31) / 29));
}

static void check_lock_half_locked(void)
{
	fresh_world(XVT_FLIGHT_TIMING_NATIVE);
	g_elapsed_ticks = 7;
	for (int i = 0; i < 3; ++i) {
		step(7);
		XVT_ASSERT_INT_EQ(
			xvt_player_timing_lock_half(0, XVT_LOCK_HALF_NONE), 3);
		XVT_ASSERT_INT_EQ(
			xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF), 3);
		XVT_ASSERT_INT_EQ(
			xvt_player_timing_lock_half(XVT_FLIGHT_PLAYERS,
						    XVT_LOCK_HALF_TARGET_LOSS),
			3);
	}
}

static void check_lock_half_unlocked(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 3;
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_NONE),
			  0);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(XVT_FLIGHT_PLAYERS,
						      XVT_LOCK_HALF_CHAFF),
			  0);

	/* Consecutive steps carry the odd tick: four steps of 3 ticks give 12 / 2. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 3;
	unsigned sum = 0;
	for (int i = 0; i < 4; ++i) {
		step(3);
		sum += xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF);
	}
	XVT_ASSERT_INT_EQ(sum, 12 / 2);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(XVT_FLIGHT_PLAYERS,
						      XVT_LOCK_HALF_CHAFF),
			  0);
}

/* Starts a fresh unlocked world in which player 0's lock timing carries an odd
 * tick out of this step. */
static void odd_tick_carried(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 3;
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
}

static void check_lock_half_carry_dropped(void)
{
	/* Kept: the next step with everything the same returns (3 + 1) / 2. */
	odd_tick_carried();
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  2);

	/* Dropped: a step skipped, ... */
	odd_tick_carried();
	step(3);
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
	/* ...a second call in the same step, ... */
	odd_tick_carried();
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
	/* ...another mode, ... */
	odd_tick_carried();
	step(3);
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_lock_half(0, XVT_LOCK_HALF_TARGET_LOSS), 1);
	/* ...another target, ... */
	odd_tick_carried();
	g_players[0].current_target_object_idx = 3;
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
	/* ...another selected warhead, ... */
	odd_tick_carried();
	g_players[0].selected_weapon_bank = 1;
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
	/* ...another player object, ... */
	odd_tick_carried();
	g_test_objects[0].object_signature = 0x999;
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
	/* ...or mode 0 in between. */
	odd_tick_carried();
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_NONE),
			  0);
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
}

static void expect_position(const int32_t position[3], int32_t x, int32_t y,
			    int32_t z)
{
	XVT_ASSERT_INT_EQ(position[0], x);
	XVT_ASSERT_INT_EQ(position[1], y);
	XVT_ASSERT_INT_EQ(position[2], z);
}

static void move_object(unsigned slot, int dx)
{
	g_test_objects[slot].world_x += dx;
}

static void check_record_recovery(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_player_timing_begin_world();
	move_object(0, 50);

	/* Outside a reference step nothing is written. */
	step(1);
	int32_t position[3] = {1, 2, 3};
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 0);
	expect_position(position, 1, 2, 3);

	/* In one: the position BeginWorld recorded, then the object's position
	 * now; once per step. */
	step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);
	expect_position(position, 0, 0, 0);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 0);
	move_object(0, 25);
	step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);
	expect_position(position, 50, 0, 0);
	step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);
	expect_position(position, 75, 0, 0);

	/* A player out of range. */
	XVT_ASSERT_INT_EQ(
		xvt_player_timing_record_recovery(XVT_FLIGHT_PLAYERS, position),
		0);

	/* Locked flights: every step is a reference step. */
	fresh_world(XVT_FLIGHT_TIMING_NATIVE);
	xvt_player_timing_begin_world();
	step(1);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(1, position), 1);
	expect_position(position, 1000, -100, 10);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(1, position), 0);
	step(1);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(1, position), 1);
}

static void check_begin_world(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	seed(0, XVT_PLAYER_YAW);
	seed(2, XVT_PLAYER_YAW);
	/* Player 2 flies the static slot: not in the main region, so no recovery position. */
	g_players[2].object_index = MAIN_SLOTS;
	xvt_player_timing_begin_world();
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(probe(2, XVT_PLAYER_YAW), 0);

	step(XVT_REFERENCE_TICKS);
	/* Without a recovery position, the object's previous-step position is written. */
	int32_t position[3];
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(2, position), 1);
	expect_position(position, -7 - MAIN_SLOTS, -8 - MAIN_SLOTS,
			-9 - MAIN_SLOTS);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(1, position), 1);
	expect_position(position, 1000, -100, 10);
}

static void check_recover(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_player_timing_begin_world();
	xvt_player_timing_begin_controls(0);
	g_elapsed_ticks = 3;
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
	seed(0, XVT_PLAYER_YAW);
	seed(0, XVT_PLAYER_ZOOM);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH, 3, 1, 4),
		0);
	struct xvt_reference_motion_wire motion;
	xvt_reference_motion_encode(0, &motion);
	XVT_ASSERT_INT_EQ(motion.type, 1);

	move_object(0, 40);
	xvt_player_timing_recover(0);
	/* Channels, the object's step remainders and its reference motion are gone. */
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_ZOOM), 0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH, 1, 1, 4),
		0);
	xvt_reference_motion_encode(0, &motion);
	XVT_ASSERT_INT_EQ(motion.type, 0);

	/* Lock timing is kept: the odd tick carries into the next step. */
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  2);
	/* Control state is kept: the same mode is not a first call. */
	seed(0, XVT_PLAYER_YAW);
	xvt_player_timing_begin_controls(0);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_YAW), 1);

	/* The recovery position is where the object was at Recover. */
	move_object(0, 5);
	step(XVT_REFERENCE_TICKS);
	int32_t position[3];
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);
	expect_position(position, 40, 0, 0);
}

static int records_equal(const struct xvt_player_timing_wire *a,
			 const struct xvt_player_timing_wire *b)
{
	return memcmp(a, b, sizeof *a) == 0;
}

static struct xvt_player_timing_wire empty_record(unsigned player)
{
	struct xvt_player_timing_wire record;
	memset(&record, 0, sizeof record);
	record.player = (uint8_t)player;
	return record;
}

/* A well-formed record for player 0 in slot 0. */
static struct xvt_player_timing_wire good_record(void)
{
	struct xvt_player_timing_wire record = empty_record(0);
	record.valid = 1;
	xvt_wire_set16(record.slot, 0);
	xvt_wire_set16(record.signature, 0x200);
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		xvt_wire_set64(record.remainder[i],
			       (uint64_t)(int64_t)(i % 2 ? -(int)(10 * i)
							 : (int)(10 * i)));
		record.direction[i] = (int8_t)((int)(i % 3) - 1);
	}
	record.lock_mode = XVT_LOCK_HALF_CHAFF;
	record.lock_odd_tick = 1;
	record.control_valid = 1;
	xvt_wire_set32(record.control_mode, XVT_CONTROL_ROLL | XVT_CONTROL_MAP);
	xvt_wire_set16(record.lock_signature, 0x200);
	xvt_wire_set16(record.lock_target, 2);
	xvt_wire_set16(record.lock_target_signature, 0x202);
	xvt_wire_set16(record.lock_weapon, 1);
	xvt_wire_set64(record.lock_serial, 41);
	xvt_wire_set16(record.camera_focus, 3);
	return record;
}

static void check_encode_decode(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	const struct xvt_player_timing_wire good = good_record();
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&good, 1), 1);
	struct xvt_player_timing_wire out;
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &good));

	/* Carries built by Scale and lock timing survive a round trip into cleared state. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_player_timing_begin_controls(0);
	g_elapsed_ticks = 3;
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  1);
	seed(0, XVT_PLAYER_SLEW_PITCH);
	seed(0, XVT_PLAYER_DISTANCE);
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_INT_EQ(out.valid, 1);
	xvt_player_timing_reset();
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&out, 1), 1);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_SLEW_PITCH), 1);
	XVT_ASSERT_INT_EQ(probe(0, XVT_PLAYER_DISTANCE), 1);
	step(3);
	XVT_ASSERT_INT_EQ(xvt_player_timing_lock_half(0, XVT_LOCK_HALF_CHAFF),
			  2);

	/* Without apply it is judged, not installed. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&good, 0), 1);
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_INT_EQ(out.valid, 0);
}

static void check_recovery_not_shared(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_player_timing_begin_world();
	seed(0, XVT_PLAYER_YAW);
	struct xvt_player_timing_wire before;
	xvt_player_timing_encode(0, &before);

	/* The record does not carry the recovery position: moving it leaves the
	 * record as it was. */
	move_object(0, 30);
	step(XVT_REFERENCE_TICKS);
	int32_t position[3];
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);
	struct xvt_player_timing_wire after;
	xvt_player_timing_encode(0, &after);
	XVT_ASSERT_TRUE(records_equal(&before, &after));

	/* Decode leaves the recovery position: the next record is the one taken above. */
	const struct xvt_player_timing_wire good = good_record();
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&good, 1), 1);
	step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);
	expect_position(position, 30, 0, 0);
}

static void check_encode_empty(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	const struct xvt_player_timing_wire good = good_record();
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&good, 1), 1);
	struct xvt_player_timing_wire out;

	xvt_player_timing_encode(XVT_FLIGHT_PLAYERS, &out);
	struct xvt_player_timing_wire empty = empty_record(XVT_FLIGHT_PLAYERS);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));

	/* Player 5 has no object. */
	xvt_player_timing_encode(5, &out);
	empty = empty_record(5);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));

	/* Player 0's entry belongs to another object, its object is dead, it is
	 * outside the main region. */
	empty = empty_record(0);
	g_test_objects[0].object_signature = 0x999;
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	g_test_objects[0].object_signature = 0x200;
	g_test_objects[0].object_type = 0;
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	g_test_objects[0].object_type = 1;
	g_region_main_object_slot_end = 0;
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	g_region_main_object_slot_end = MAIN_SLOTS;
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &good));
}

static void expect_refused(const struct xvt_player_timing_wire *record)
{
	struct xvt_player_timing_wire before;
	xvt_player_timing_encode(0, &before);
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(record, 1), 0);
	struct xvt_player_timing_wire after;
	xvt_player_timing_encode(0, &after);
	XVT_ASSERT_TRUE(records_equal(&before, &after));
}

static void check_decode_refusals(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_player_timing_begin_controls(0);
	seed(0, XVT_PLAYER_YAW);
	const struct xvt_player_timing_wire good = good_record();

	struct xvt_player_timing_wire bad = good;
	bad.player = XVT_FLIGHT_PLAYERS;
	expect_refused(&bad);
	bad = good;
	bad.valid = 2;
	expect_refused(&bad);
	bad = good;
	bad.lock_odd_tick = 2;
	expect_refused(&bad);
	bad = good;
	bad.control_valid = 2;
	expect_refused(&bad);
	bad = good;
	bad.lock_mode = XVT_LOCK_HALF_TARGET_LOSS + 1;
	expect_refused(&bad);
	bad = good;
	xvt_wire_set32(bad.control_mode, XVT_CONTROL_MASK + 1);
	expect_refused(&bad);
	bad = good;
	xvt_wire_set16(bad.reserved, 1);
	expect_refused(&bad);
	bad = good;
	xvt_wire_set16(bad.reserved_tail, 1);
	expect_refused(&bad);

	/* Not valid: exactly the empty record. */
	bad = empty_record(0);
	xvt_wire_set16(bad.camera_focus, 1);
	expect_refused(&bad);

	/* Valid: a main-region slot, directions -1 to 1, remainders under one second of ticks. */
	bad = good;
	xvt_wire_set16(bad.slot, MAIN_SLOTS);
	expect_refused(&bad);
	bad = good;
	bad.direction[4] = 2;
	expect_refused(&bad);
	bad = good;
	bad.direction[8] = -2;
	expect_refused(&bad);
	bad = good;
	xvt_wire_set64(bad.remainder[0], SIMULATION_TICKS_PER_SECOND);
	expect_refused(&bad);
	bad = good;
	xvt_wire_set64(bad.remainder[8],
		       (uint64_t)-(int64_t)SIMULATION_TICKS_PER_SECOND);
	expect_refused(&bad);

	/* One short of the bounds is accepted, and so is the empty record. */
	bad = good;
	xvt_wire_set64(bad.remainder[0], SIMULATION_TICKS_PER_SECOND - 1);
	xvt_wire_set64(bad.remainder[8],
		       (uint64_t)(1 - (int64_t)SIMULATION_TICKS_PER_SECOND));
	xvt_wire_set16(bad.slot, MAIN_SLOTS - 1);
	bad.lock_mode = XVT_LOCK_HALF_TARGET_LOSS;
	xvt_wire_set32(bad.control_mode, XVT_CONTROL_MASK);
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&bad, 0), 1);
	bad = empty_record(7);
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&bad, 0), 1);
}

static void check_reset_shared(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_player_timing_begin_world();
	xvt_player_timing_begin_controls(0);
	seed(0, XVT_PLAYER_YAW);
	move_object(0, 60);
	step(XVT_REFERENCE_TICKS);
	int32_t position[3];
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);

	/* The shared state, the entry's object included, is gone: the record is empty. */
	xvt_player_timing_reset_shared();
	struct xvt_player_timing_wire out;
	struct xvt_player_timing_wire empty = empty_record(0);
	xvt_player_timing_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));

	/* Restoring the shared state from a record finds the recovery position still there. */
	const struct xvt_player_timing_wire good = good_record();
	XVT_ASSERT_INT_EQ(xvt_player_timing_decode(&good, 1), 1);
	step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_player_timing_record_recovery(0, position), 1);
	expect_position(position, 60, 0, 0);
}

int main(int argc, char **argv)
{
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "slew_int_min") == 0) {
			check_slew_most_negative();
		}
		return 0;
	}
	check_scale();
	check_clear_and_reset();
	check_entry_follows_object();
	check_reset_controls();
	check_begin_controls_mode();
	check_begin_controls_camera();
	check_slew();
	check_lock_half_locked();
	check_lock_half_unlocked();
	check_lock_half_carry_dropped();
	check_record_recovery();
	check_begin_world();
	check_recover();
	check_encode_decode();
	check_recovery_not_shared();
	check_encode_empty();
	check_decode_refusals();
	check_reset_shared();
	xvt_flight_integration_shutdown();
	xvt_reference_motion_shutdown();
	xvt_flight_timing_end_session();
	return 0;
}

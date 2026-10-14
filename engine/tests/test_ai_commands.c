/* Tests for xvt/flight/ai_commands.c, a player's commands to a computer
 * craft in its view, on worlds this file builds: SLOT_COUNT craft slots,
 * each with a craft record, whose command fields are set by hand as the
 * wingman keys set them. No game data is read. The rules are the 1997 AI's:
 * a candidate target it can no longer target is cleared
 * (paifight_scanfortargetorder), and the evade command, candidate
 * AI_TARGET_ABORT, is dropped once taken up (paiorder_evasiveorder). */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/ai_commands.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/ai/ai_wire.h"

enum {
	SLOT_COUNT = 4,
	/* The commanded craft, and two other craft in the world. */
	WINGMAN = 0,
	ENEMY = 2,
	OTHER = 3,
	/* Any object type but 0, the empty slot's. */
	TEST_CRAFT_TYPE = 1,
	/* A byte no field of a fresh view holds. */
	STALE = 0x5A,
	NONE = UINT16_MAX,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[SLOT_COUNT];
static struct craft_data g_test_craft[SLOT_COUNT];

/* A world of SLOT_COUNT craft, none commanded: no candidate, no avoided
 * target, no wait. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	g_object_table = g_test_objects;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		g_test_objects[slot].object_type = TEST_CRAFT_TYPE;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		g_test_craft[slot].ai_controller.candidate_target_idx = NONE;
		g_test_craft[slot].player_command_avoid_target_obj_idx = NONE;
	}
	g_flight_sim_side_effects_suppressed = 0;
}

static struct ai_controller *wingman_controller(void)
{
	return &g_test_craft[WINGMAN].ai_controller;
}

/* The wingman's view, written over a view of stale bytes. */
static struct ai_wire_craft_view view_of_wingman(void)
{
	struct ai_wire_craft_view view;
	memset(&view, STALE, sizeof view);
	ai_commands_view_craft(WINGMAN, &view);
	return view;
}

/* A craft no one commanded shows no command bits and no targets. */
static void check_view_no_commands(void)
{
	fresh_world();

	struct ai_wire_craft_view view = view_of_wingman();

	XVT_ASSERT_INT_EQ(view.commands, 0);
	XVT_ASSERT_INT_EQ(view.command_target, NONE);
	XVT_ASSERT_INT_EQ(view.avoid_target, NONE);
}

/* The wait mark shows as the wait bit alone. */
static void check_view_wait(void)
{
	fresh_world();
	wingman_controller()->wait_command = 1;

	struct ai_wire_craft_view view = view_of_wingman();

	XVT_ASSERT_INT_EQ(view.commands, AI_WIRE_COMMAND_WAIT);
	XVT_ASSERT_INT_EQ(view.command_target, NONE);
}

/* An attack order's target and an ignore order's target show as they are,
 * with no command bit. */
static void check_view_targets(void)
{
	fresh_world();
	wingman_controller()->candidate_target_idx = ENEMY;
	g_test_craft[WINGMAN].player_command_avoid_target_obj_idx = OTHER;

	struct ai_wire_craft_view view = view_of_wingman();

	XVT_ASSERT_INT_EQ(view.commands, 0);
	XVT_ASSERT_INT_EQ(view.command_target, ENEMY);
	XVT_ASSERT_INT_EQ(view.avoid_target, OTHER);
	XVT_ASSERT_INT_EQ(wingman_controller()->candidate_target_idx, ENEMY);
}

/* The evade command, candidate AI_TARGET_ABORT, shows as the evade bit with
 * no target, beside the wait bit; the view leaves the command standing. */
static void check_view_evade(void)
{
	fresh_world();
	wingman_controller()->candidate_target_idx = AI_TARGET_ABORT;
	wingman_controller()->wait_command = 1;

	struct ai_wire_craft_view view = view_of_wingman();

	XVT_ASSERT_INT_EQ(view.commands,
			  AI_WIRE_COMMAND_WAIT | AI_WIRE_COMMAND_EVADE);
	XVT_ASSERT_INT_EQ(view.command_target, NONE);
	XVT_ASSERT_INT_EQ(wingman_controller()->candidate_target_idx,
			  AI_TARGET_ABORT);
}

/* A target to attack whose slot is empty is cleared from the craft, and the
 * view shows none. */
static void check_view_clears_gone_target(void)
{
	fresh_world();
	wingman_controller()->candidate_target_idx = ENEMY;
	g_test_objects[ENEMY].object_type = 0;

	struct ai_wire_craft_view view = view_of_wingman();

	XVT_ASSERT_INT_EQ(view.command_target, NONE);
	XVT_ASSERT_INT_EQ(wingman_controller()->candidate_target_idx, NONE);
}

/* Taking up the evade command clears it and leaves the wait mark and the
 * avoided target alone. */
static void check_evaded(void)
{
	fresh_world();
	wingman_controller()->candidate_target_idx = AI_TARGET_ABORT;
	wingman_controller()->wait_command = 1;
	g_test_craft[WINGMAN].player_command_avoid_target_obj_idx = OTHER;

	ai_commands_evaded(WINGMAN);

	XVT_ASSERT_INT_EQ(wingman_controller()->candidate_target_idx, NONE);
	XVT_ASSERT_INT_EQ(wingman_controller()->wait_command, 1);
	XVT_ASSERT_INT_EQ(
		g_test_craft[WINGMAN].player_command_avoid_target_obj_idx,
		OTHER);
}

int main(void)
{
	check_view_no_commands();
	check_view_wait();
	check_view_targets();
	check_view_evade();
	check_view_clears_gone_target();
	check_evaded();
	return 0;
}

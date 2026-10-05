/* Checks that pai_setup_context_and_find_order_plan_on_target
 * (xvt/flight/ai/pai.c) puts g_pai_context back as it found it in the modern
 * build, on a world this file builds itself: two craft in two flight groups and
 * one plan. No game data is read. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"

static struct object_record g_test_objects[2];
static struct mobile_object g_test_mobiles[2];
static struct craft_data g_test_craft[2];
/* The plan's bytes: a target byte, then the maneuver byte setup reads. */
static uint8_t g_test_plan[4] = {255, 6, 0, 0};
static uint8_t g_marker_plan[4];
static struct craft_data g_marker_craft;
static struct ai_controller g_marker_controller;

/* Object 0 flies in flight group 0 with no leader; object 1 is the target, in
 * flight group 1. Every order of group 0 is order 0, whose leader plan name
 * index is 7, and its first target is flight group target_fg. */
static void build_world(uint16_t target_fg)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0,
	       2 * sizeof g_mission_flight_groups[0]);
	memset(g_plan_table, 0, sizeof g_plan_table);
	g_object_table = g_test_objects;
	for (int i = 0; i < 2; ++i) {
		g_test_objects[i].object_type = 1;
		g_test_objects[i].flight_group_idx = (uint8_t)i;
		g_test_objects[i].world_x = 1000 * (i + 1);
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].p_craft = &g_test_craft[i];
		g_test_craft[i].leader_obj_idx = UINT8_MAX;
	}
	strcpy(g_plan_table[0].name, "nullpln");
	g_plan_data_ptrs[0] = g_test_plan;
	g_order_leader_builtin_plan_name_index[0] = 7;
	for (int slot = 0; slot < 3; ++slot) {
		g_mission_flight_groups[0].fg.orders[slot].order = 0;
		g_mission_flight_groups[0].fg.orders[slot].target1_type = 1;
		g_mission_flight_groups[0].fg.orders[slot].target1 =
			(uint8_t)target_fg;
		g_mission_flight_groups[0].fg.orders[slot].target1_or_target2 =
			1;
	}
}

/* A context unlike anything setup writes, so a field it changes shows. */
static void mark_context(void)
{
	memset(&g_pai_context, 0, sizeof g_pai_context);
	g_pai_context.object_index = 1;
	g_pai_context.craft = &g_marker_craft;
	g_pai_context.controller = &g_marker_controller;
	g_pai_context.leader_object_index = 3;
	g_pai_context.leader_or_self_craft = &g_marker_craft;
	g_pai_context.craft_flight_group_index = 5;
	g_pai_context.order_slot = 2;
	g_pai_context.craft_position_x = -11;
	g_pai_context.craft_position_y = -12;
	g_pai_context.craft_position_z = -13;
	g_pai_context.skill_tier = 2;
	g_pai_context.initial_maneuver_id = 9;
	g_pai_context.plan_cursor = &g_marker_plan[1];
	g_pai_context.require_undisabled_target = 1;
	g_pai_context.variable_plan_id = 4;
	g_pai_context.target_search_flags = 0x21;
	g_pai_context.target_search_origin_x = 21;
	g_pai_context.target_search_origin_y = 22;
	g_pai_context.target_search_origin_z = 23;
}

static void assert_context_marked(void)
{
	XVT_ASSERT_INT_EQ(g_pai_context.object_index, 1);
	XVT_ASSERT_TRUE(g_pai_context.craft == &g_marker_craft);
	XVT_ASSERT_TRUE(g_pai_context.controller == &g_marker_controller);
	XVT_ASSERT_INT_EQ(g_pai_context.leader_object_index, 3);
	XVT_ASSERT_TRUE(g_pai_context.leader_or_self_craft == &g_marker_craft);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_flight_group_index, 5);
	XVT_ASSERT_INT_EQ(g_pai_context.order_slot, 2);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_position_x, -11);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_position_y, -12);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_position_z, -13);
	XVT_ASSERT_INT_EQ(g_pai_context.skill_tier, 2);
	XVT_ASSERT_INT_EQ(g_pai_context.initial_maneuver_id, 9);
	XVT_ASSERT_TRUE(g_pai_context.plan_cursor == &g_marker_plan[1]);
	XVT_ASSERT_INT_EQ(g_pai_context.require_undisabled_target, 1);
	XVT_ASSERT_INT_EQ(g_pai_context.variable_plan_id, 4);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_flags, 0x21);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_origin_x, 21);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_origin_y, 22);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_origin_z, 23);
}

static void check_found_plan_puts_context_back(void)
{
	build_world(1);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(0, 7, 1), 1);
	assert_context_marked();
}

static void check_no_plan_puts_context_back(void)
{
	/* The orders give the plan but name another flight group as target. */
	build_world(2);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(0, 7, 1), 0);
	assert_context_marked();

	/* No order gives the plan asked for. */
	build_world(1);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(0, 8, 1), 0);
	assert_context_marked();
}

static void check_no_object_leaves_context(void)
{
	build_world(1);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(-1, 7, 1), 0);
	assert_context_marked();
}

int main(void)
{
	check_found_plan_puts_context_back();
	check_no_plan_puts_context_back();
	check_no_object_leaves_context();
	return 0;
}

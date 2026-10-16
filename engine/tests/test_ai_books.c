/* Tests for xvt/flight/ai_books.c, the mission's books for computer craft,
 * on worlds this file builds: no game data is read. Each rule is checked
 * against the 1997 code it was ported from (orders 15, 38, 39 and 41, the
 * into-hyperspace maneuver and the hangar order), and each 1997 fault the
 * port does not keep is checked gone. The last case drives a jump through
 * the think loop with a fake pilot program and replays its last step from a
 * saved world. */
#include <SDL3/SDL_log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt_runtime/ai/ai_wire.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/world_state.h"

enum {
	SLOT_COUNT = 12,
	GROUP_COUNT = 6,
	/* The craft's group, its two motherships' groups and a cargo group. */
	TEST_GROUP = 2,
	CARRIER_GROUP = 3,
	ALTERNATE_GROUP = 4,
	CARGO_GROUP = 5,
	/* Models: a fighter with a hyperdrive, one without, and a carrier. */
	JUMP_MODEL = 1,
	NO_JUMP_MODEL = 2,
	CARRIER_MODEL = 3,
	TEST_INTERVAL = 59,
	TEST_TICK = 0x2000,
	ANSWER_CAPACITY = 512,
};

static struct object_record g_objects[SLOT_COUNT];
static struct mobile_object g_mobiles[SLOT_COUNT];
static struct craft_data g_craft[SLOT_COUNT];
/* Every in-flight message's text: a pane byte and a word. */
static char g_test_message[] = "\003message";

/* Empty slots whose mobile and craft records come from the pools the world
 * image saves, every slot in the active region, GROUP_COUNT flight groups
 * with no player whose first order is Attack, an order whose 1997 plan
 * kept every book, the clocks at 0, the three test models, and no
 * pilot program. */
static void fresh_world(void)
{
	memset(g_objects, 0, sizeof g_objects);
	memset(g_mobiles, 0, sizeof g_mobiles);
	memset(g_craft, 0, sizeof g_craft);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0,
	       GROUP_COUNT * sizeof g_mission_fg_stats[0]);
	memset(&g_mission_elapsed_clock, 0, sizeof g_mission_elapsed_clock);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(g_players, 0, sizeof g_players);
	for (int player = 0; player < 8; ++player) {
		g_players[player].object_index = -1;
	}
	g_mission_header.num_flight_groups = GROUP_COUNT;
	g_object_table = g_objects;
	g_mobile_object_pool_base = g_mobiles;
	g_craft_data_pool_base = g_craft;
	g_craft_data_pool_capacity = SLOT_COUNT;
	g_region_main_object_slot_end = SLOT_COUNT;
	g_region_static_object_slot_count = 0;
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	g_mobile_object_char_data_count = 0;
	g_projectile_object_slots_total = 0;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		g_objects[slot].mobj = &g_mobiles[slot];
		g_objects[slot].player_owner_idx = -1;
		g_mobiles[slot].p_craft = &g_craft[slot];
	}
	for (int group = 0; group < GROUP_COUNT; ++group) {
		g_mission_flight_groups[group].player_owner_idx = -1;
		g_mission_flight_groups[group].fg.orders[0].order =
			MISSION_ORDER_ATTACK;
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = SLOT_COUNT;
	g_game_time = TEST_TICK;
	g_flight_sim_side_effects_suppressed = 0;
	g_model_defs[JUMP_MODEL].has_hyperdrive = 1;
	g_model_defs[NO_JUMP_MODEL].has_hyperdrive = 0;
	g_model_defs[CARRIER_MODEL].hangar_points.outside.side = 0;
	g_model_defs[CARRIER_MODEL].hangar_points.outside.up = -300;
	g_model_defs[CARRIER_MODEL].hangar_points.outside.forward = 2000;
	g_model_defs[CARRIER_MODEL].hangar_points.inside.side = 100;
	g_model_defs[CARRIER_MODEL].hangar_points.inside.up = -200;
	g_model_defs[CARRIER_MODEL].hangar_points.inside.forward = 300;
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	ai_brain_set_exchange(NULL);
}

/* Makes slot a live computer craft of group: a fighter of JUMP_MODEL,
 * active, flying alone, able to move, carrying nothing, due to think. */
static struct craft_data *make_craft(int slot, int group)
{
	g_objects[slot].object_type = 5;
	g_objects[slot].object_signature = (uint16_t)(0x100 + slot);
	g_objects[slot].flight_group_idx = (uint16_t)group;
	g_objects[slot].genus_id = 1;
	struct craft_data *craft = &g_craft[slot];
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->leader_obj_idx = UINT8_MAX;
	craft->model_index = JUMP_MODEL;
	craft->ai_flight.max_speed_cache = 100;
	craft->carried_object_index = UINT16_MAX;
	craft->carrier_obj_idx = UINT16_MAX;
	craft->craft_ordinal = (uint8_t)(slot + 1);
	craft->ai_controller.think_interval = TEST_INTERVAL;
	return craft;
}

/* Makes slot the carrier leading group, a starship at x. */
static void make_carrier(int slot, int group, int x)
{
	make_craft(slot, group)->model_index = CARRIER_MODEL;
	g_objects[slot].genus_id = CRAFT_GENUS_STARSHIP;
	g_objects[slot].world_x = x;
}

/* The books' view of the craft in slot, every other field left 0xEE. */
static struct ai_wire_craft_view view_of(int slot)
{
	struct ai_wire_craft_view view;
	memset(&view, 0xEE, sizeof view);
	ai_books_view_craft((uint16_t)slot, &view);
	return view;
}

static int has_bit(int slot, unsigned int bit)
{
	return (view_of(slot).books & bit) != 0;
}

/* The engine's log lines, kept without Aeron's "xvt: " prefix. */
enum {
	LOG_LINE_CAPACITY = 64,
	LOG_LINE_SIZE = 200,
};

static char g_log_lines[LOG_LINE_CAPACITY][LOG_LINE_SIZE];
static int g_log_line_count;

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	static const char prefix[] = "xvt: ";
	(void)userdata;
	(void)category;
	(void)priority;
	if (g_log_line_count >= LOG_LINE_CAPACITY) {
		return;
	}
	if (strncmp(message, prefix, sizeof prefix - 1) == 0) {
		message += sizeof prefix - 1;
	}
	snprintf(g_log_lines[g_log_line_count++], LOG_LINE_SIZE, "%s", message);
}

/* How many kept lines start with start. */
static int lines_starting(const char *start)
{
	int found = 0;
	for (int line = 0; line < g_log_line_count; ++line) {
		found += strncmp(g_log_lines[line], start, strlen(start)) == 0;
	}
	return found;
}

static void clock_at(int hours, int minutes, int seconds)
{
	g_mission_elapsed_clock.hours = (uint8_t)hours;
	g_mission_elapsed_clock.minutes = (uint8_t)minutes;
	g_mission_elapsed_clock.seconds = (uint8_t)seconds;
}

static unsigned int count_of(int group, int outcome)
{
	return g_mission_fg_stats[group].outcome_count[outcome];
}

/* Order 38: the departure starts when the mission clock reaches the group's
 * departure time or its trigger holds, counted once as not yet departed, and
 * is due once the delay has passed; a craft that cannot move never starts,
 * nor one the program sent home or one that aborted. */
static void check_departure(void)
{
	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.departure_clock_min = 1;
	group->fg.departure_clock_sec = 30;
	group->fg.departure_delay_seconds = 20;
	clock_at(0, 1, 29);
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_DEPARTURE_DUE));
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 0);
	clock_at(0, 1, 30);
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_DEPARTURE_DUE));
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_clock_minutes, 1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_clock_seconds, 30);
	XVT_ASSERT_INT_EQ(
		count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_NOT_DEPARTED), 1);
	clock_at(0, 1, 49);
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_DEPARTURE_DUE));
	clock_at(0, 1, 50);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_DEPARTURE_DUE));
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 2);
	clock_at(0, 2, 10);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_DEPARTURE_DUE));
	XVT_ASSERT_INT_EQ(
		count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_NOT_DEPARTED), 1);

	/* The trigger: "never" or "always", in either order, with or. */
	fresh_world();
	craft = make_craft(1, TEST_GROUP);
	group->fg.departure_trigger.triggers[0].condition = MISSION_COND_NEVER;
	group->fg.departure_trigger.triggers[1].condition =
		MISSION_COND_NO_CONDITION;
	view_of(1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 0);
	group->fg.departure_trigger.triggers[1].condition =
		MISSION_COND_ALWAYS_TRUE;
	group->fg.departure_trigger.trigger1_or_trigger2 = GOAL_OPERATOR_STR_OR;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_DEPARTURE_DUE));

	fresh_world();
	craft = make_craft(1, TEST_GROUP);
	craft->ai_flight.max_speed_cache = 0;
	group->fg.departure_clock_sec = 1;
	clock_at(0, 0, 5);
	view_of(1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 0);
	craft->ai_flight.max_speed_cache = 100;
	craft->ai_controller.activity = AI_ACTIVITY_GOING_HOME;
	view_of(1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 0);
	craft->ai_controller.activity = AI_ACTIVITY_NONE;
	craft->ai_flight.mission_aborted_flag = 1;
	view_of(1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 0);
	craft->ai_flight.mission_aborted_flag = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_DEPARTURE_DUE));
}

/* Order 41: a follower takes its leader's departure, counted once, and its
 * order slot and orders-done mark. It now takes the departure clock too:
 * 1997 left it at 0:00:00, so a follower that came to lead withdrew at once
 * instead of after the delay. A follower keeps no departure clock of its
 * own. */
static void check_follower(void)
{
	fresh_world();
	struct craft_data *leader = make_craft(1, TEST_GROUP);
	struct craft_data *follower = make_craft(2, TEST_GROUP);
	follower->leader_obj_idx = 1;
	g_mission_flight_groups[TEST_GROUP].fg.departure_delay_seconds = 20;
	leader->ai_flight.depart_timer_flag = 1;
	leader->ai_flight.depart_clock_minutes = 1;
	leader->ai_flight.depart_clock_seconds = 30;
	leader->ai_controller.current_order_slot = 2;
	/* Slot 2 holds an order whose 1997 plan kept the departure. */
	g_mission_flight_groups[TEST_GROUP].fg.orders[2].order =
		MISSION_ORDER_ATTACK;
	clock_at(0, 1, 35);
	view_of(2);
	view_of(2);
	XVT_ASSERT_INT_EQ(follower->ai_flight.depart_timer_flag, 1);
	XVT_ASSERT_INT_EQ(follower->ai_flight.depart_clock_minutes, 1);
	XVT_ASSERT_INT_EQ(follower->ai_flight.depart_clock_seconds, 30);
	XVT_ASSERT_INT_EQ(follower->ai_controller.current_order_slot, 2);
	XVT_ASSERT_INT_EQ(
		count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_NOT_DEPARTED), 1);
	follower->leader_obj_idx = UINT8_MAX;
	clock_at(0, 1, 40);
	XVT_ASSERT_TRUE(!has_bit(2, AI_WIRE_BOOKS_DEPARTURE_DUE));
	clock_at(0, 1, 50);
	XVT_ASSERT_TRUE(has_bit(2, AI_WIRE_BOOKS_DEPARTURE_DUE));

	struct craft_data *other = make_craft(3, TEST_GROUP);
	other->leader_obj_idx = 1;
	leader->ai_flight.go_home_flag = 1;
	XVT_ASSERT_TRUE(has_bit(3, AI_WIRE_BOOKS_ORDERS_DONE));

	fresh_world();
	make_craft(1, TEST_GROUP);
	follower = make_craft(2, TEST_GROUP);
	follower->leader_obj_idx = 1;
	g_mission_flight_groups[TEST_GROUP].fg.departure_clock_min = 1;
	clock_at(0, 2, 0);
	view_of(2);
	XVT_ASSERT_INT_EQ(follower->ai_flight.depart_timer_flag, 0);
}

/* Sets the craft in slot so that abort trigger trigger just holds, or just
 * does not: all systems working, hull 100, shields 400 and 400, one launcher
 * with one warhead left, unless the trigger's own test is set across its
 * line. */
static void set_abort_state(int slot, int trigger, int holds)
{
	struct craft_data *craft = &g_craft[slot];
	craft->working_subsystems = (uint16_t)~0u;
	craft->hull_max = 100;
	craft->hull_damage = 0;
	craft->shield_energy[0] = 400;
	craft->shield_energy[1] = 400;
	craft->warhead_launcher_count = 1;
	craft->warhead_slot_type_ids[0] = 1;
	g_model_defs[JUMP_MODEL].warhead_launcher_first_slot[0] = 0;
	g_model_defs[JUMP_MODEL].warhead_launcher_last_slot[0] = 1;
	craft->weapon_slots[1].ammo_count = 1;
	uint16_t fraction = trigger == 6 ? 0x8000 : 0x4000;
	int limit = (uint16_t)math2_fraction(
		(uint16_t)craft_get_object_max_shield((uint16_t)slot),
		fraction);
	switch (trigger) {
	case 1:
		if (holds) {
			craft->working_subsystems &=
				(uint16_t)~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
		}
		break;
	case 2:
		if (holds) {
			craft->working_subsystems &=
				(uint16_t)~CRAFT_SUBSYSTEM_FLAG_CANNONS;
		}
		break;
	case 3:
		craft->weapon_slots[1].ammo_count = holds ? 0 : 1;
		break;
	case 4:
		craft->hull_damage = holds ? 50 : 49;
		break;
	case 5:
		craft->attacked_by_team[3] = (uint8_t)holds;
		break;
	case 6:
	case 7:
		craft->shield_energy[0] = holds ? limit : limit + 1;
		craft->shield_energy[1] = 0;
		break;
	case 8:
		craft->hull_damage = holds ? 25 : 24;
		break;
	default:
		craft->hull_damage = holds ? 75 : 74;
		break;
	}
}

/* Order 15: each of the nine abort triggers, just held and just not; a
 * freighter or starship with both banks empty; one abort counted once, a
 * departure under way undone; the group's player told a cannons abort's
 * reason as damaged and inoperative, not warheads out as in 1997; a craft
 * that cannot move never aborts. */
static void check_aborts(void)
{
	for (int trigger = 1; trigger <= 9; ++trigger) {
		for (int holds = 0; holds <= 1; ++holds) {
			fresh_world();
			struct craft_data *craft = make_craft(1, TEST_GROUP);
			g_mission_flight_groups[TEST_GROUP].fg.abort_trigger =
				(uint8_t)trigger;
			set_abort_state(1, trigger, holds);
			XVT_ASSERT_INT_EQ(has_bit(1, AI_WIRE_BOOKS_ABORTED),
					  holds);
			XVT_ASSERT_INT_EQ(craft->ai_flight.mission_aborted_flag,
					  holds);
			XVT_ASSERT_INT_EQ(
				count_of(TEST_GROUP,
					 FLIGHT_GROUP_OUTCOME_ABORTED),
				holds);
		}
	}

	fresh_world();
	make_craft(1, TEST_GROUP);
	g_mission_flight_groups[TEST_GROUP].fg.abort_trigger = 1;
	set_abort_state(1, 1, 0);
	g_craft[1].shield_energy[0] = 0;
	g_craft[1].shield_energy[1] = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ABORTED));
	g_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ABORTED));

	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	g_mission_flight_groups[TEST_GROUP].fg.abort_trigger = 2;
	g_mission_flight_groups[TEST_GROUP].fg.departure_delay_minutes = 1;
	g_mission_flight_groups[TEST_GROUP].player_owner_idx = 0;
	set_abort_state(1, 2, 1);
	craft->ai_flight.depart_timer_flag = 1;
	g_mission_fg_stats[TEST_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 1;
	g_msg_arg_table[2] = 0;
	view_of(1);
	view_of(1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 0);
	XVT_ASSERT_INT_EQ(
		count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_NOT_DEPARTED), 0);
	XVT_ASSERT_INT_EQ(count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_ABORTED),
			  1);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[2],
			  IFMSG_087_DAMAGED_AND_INOPERATIVE);

	fresh_world();
	craft = make_craft(1, TEST_GROUP);
	craft->ai_flight.max_speed_cache = 0;
	g_mission_flight_groups[TEST_GROUP].fg.abort_trigger = 4;
	set_abort_state(1, 4, 1);
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ABORTED));
}

/* The skip-to-order-4 trigger shown due only to a leader, and the order
 * action: marks the slot left, moves the slot, skips to order 4, and marks
 * the orders done (order 39) only when every order in slots 0 to 2 is done
 * (state 2), never after a skip or for a craft that cannot move. */
static void check_skip_and_orders(void)
{
	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.orders[0].order = MISSION_ORDER_ATTACK;
	group->fg.orders[1].order = 2;
	group->fg.orders[3].order = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_SKIP_DUE));
	group->fg.skip_to_order4.triggers[0].condition = MISSION_COND_NEVER;
	group->fg.skip_to_order4.triggers[1].condition =
		MISSION_COND_NO_CONDITION;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_SKIP_DUE));
	group->fg.skip_to_order4.triggers[1].condition =
		MISSION_COND_ALWAYS_TRUE;
	group->fg.skip_to_order4.trigger1_or_trigger2 = GOAL_OPERATOR_STR_OR;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_SKIP_DUE));
	make_craft(2, TEST_GROUP);
	craft->leader_obj_idx = 2;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_SKIP_DUE));
	craft->leader_obj_idx = UINT8_MAX;

	ai_books_apply_order(1, 1, 2);
	XVT_ASSERT_INT_EQ(
		craft->ai_controller.order_progress.completion_state[0], 2);
	XVT_ASSERT_INT_EQ(craft->ai_controller.current_order_slot, 1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.go_home_flag, 0);
	ai_books_apply_order(1, 1, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.go_home_flag, 1);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ORDERS_DONE));

	fresh_world();
	craft = make_craft(1, TEST_GROUP);
	group->fg.orders[0].order = 1;
	group->fg.orders[3].order = 1;
	ai_books_apply_order(1, 3, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.skipped_to_order4, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.current_order_slot, 3);
	XVT_ASSERT_INT_EQ(
		craft->ai_controller.order_progress.completion_state[0], 0);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_SKIPPED));
	craft->ai_controller.order_progress.completion_state[0] = 2;
	ai_books_apply_order(1, 3, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.go_home_flag, 0);

	fresh_world();
	craft = make_craft(1, TEST_GROUP);
	group->fg.orders[0].order = 1;
	group->fg.orders[1].order = 1;
	ai_books_apply_order(1, 1, 3);
	ai_books_apply_order(1, 1, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.go_home_flag, 0);

	fresh_world();
	craft = make_craft(1, TEST_GROUP);
	craft->ai_flight.max_speed_cache = 0;
	group->fg.orders[0].order = 1;
	ai_books_apply_order(1, 0, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.go_home_flag, 0);
}

/* The view's order fields and way home: codes and states of the four slots,
 * the current order's throttle as a craft throttle, variables and speed; may
 * jump only for an active craft with a hyperdrive whose group leaves by
 * hyperspace; by mothership for its group's departure method, or a captured
 * craft's; and mission point 13 times 256 with y negated. */
static void check_view_orders_and_home(void)
{
	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.orders[0].order = 1;
	group->fg.orders[0].throttle = 3;
	group->fg.orders[0].variable1 = 7;
	group->fg.orders[0].variable2 = 8;
	group->fg.orders[0].speed = 9;
	group->fg.orders[1].order = 26;
	craft->ai_controller.order_progress.completion_state[1] = 2;
	struct ai_wire_craft_view view = view_of(1);
	XVT_ASSERT_INT_EQ(view.order_codes[0], 1);
	XVT_ASSERT_INT_EQ(view.order_codes[1], 26);
	XVT_ASSERT_INT_EQ(view.order_codes[3], 0);
	XVT_ASSERT_INT_EQ(view.order_states[0], 0);
	XVT_ASSERT_INT_EQ(view.order_states[1], 2);
	XVT_ASSERT_INT_EQ(view.order_throttle,
			  g_order_throttle_to_craft_throttle_speed[3]);
	XVT_ASSERT_INT_EQ(view.order_variable1, 7);
	XVT_ASSERT_INT_EQ(view.order_variable2, 8);
	XVT_ASSERT_INT_EQ(view.order_speed, 9);
	/* No group leaves into this one, so all have returned. */
	XVT_ASSERT_INT_EQ(view.books, AI_WIRE_BOOKS_MAY_JUMP |
					      AI_WIRE_BOOKS_ALL_RETURNED |
					      AI_WIRE_BOOKS_ALL_LAUNCHED);
	XVT_ASSERT_INT_EQ(view.mothership, UINT16_MAX);
	XVT_ASSERT_INT_EQ(view.inside_hangar[0], 0);
	XVT_ASSERT_INT_EQ(view.home_point[2], 0);
	group->fg.orders[0].throttle = 12;
	XVT_ASSERT_INT_EQ(view_of(1).order_throttle, 0);

	craft->model_index = NO_JUMP_MODEL;
	XVT_ASSERT_INT_EQ(view_of(1).books, AI_WIRE_BOOKS_ALL_RETURNED |
						    AI_WIRE_BOOKS_ALL_LAUNCHED);
	craft->model_index = JUMP_MODEL;
	craft->object_kind = CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE;
	XVT_ASSERT_INT_EQ(view_of(1).books, AI_WIRE_BOOKS_ALL_RETURNED |
						    AI_WIRE_BOOKS_ALL_LAUNCHED);
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	/* No mothership here and none to come: it may jump (order 46). */
	group->fg.departure_method = 1;
	XVT_ASSERT_INT_EQ(view_of(1).books, AI_WIRE_BOOKS_BY_MOTHERSHIP |
						    AI_WIRE_BOOKS_MAY_JUMP |
						    AI_WIRE_BOOKS_ALL_RETURNED |
						    AI_WIRE_BOOKS_ALL_LAUNCHED);
	group->fg.departure_method = 0;
	craft->captured_by_flight_group = 1;
	group->fg.captured_depart_via_mothership = 1;
	XVT_ASSERT_INT_EQ(view_of(1).books, AI_WIRE_BOOKS_BY_MOTHERSHIP |
						    AI_WIRE_BOOKS_MAY_JUMP |
						    AI_WIRE_BOOKS_ALL_RETURNED |
						    AI_WIRE_BOOKS_ALL_LAUNCHED);
	craft->captured_by_flight_group = 0;

	group->fg.mission_point_enabled[13] = 1;
	group->fg.mission_point_x[13] = 10;
	group->fg.mission_point_y[13] = 20;
	group->fg.mission_point_z[13] = 30;
	view = view_of(1);
	XVT_ASSERT_TRUE((view.books & AI_WIRE_BOOKS_HOME_POINT) != 0);
	XVT_ASSERT_INT_EQ(view.home_point[0], 2560);
	XVT_ASSERT_INT_EQ(view.home_point[1], -5120);
	XVT_ASSERT_INT_EQ(view.home_point[2], 7680);
}

/* The books an order kept in 1997, with its departure started and not yet
 * due, its skip-to-order-4 trigger holding and, when aborting, its abort
 * trigger holding: BOOKS_STARTED while its departure runs, and its aborted
 * and skip-due bits. An abort ends a departure that started, as in 1997. */
enum { BOOKS_STARTED = 0x1000 };

static unsigned int books_kept_for(uint8_t order, int aborting)
{
	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.orders[0].order = order;
	group->fg.departure_clock_min = 1;
	group->fg.departure_delay_seconds = 20;
	clock_at(0, 1, 0);
	if (aborting) {
		group->fg.abort_trigger = 1;
		set_abort_state(1, 1, 1);
	}
	group->fg.skip_to_order4.triggers[0].condition = MISSION_COND_NEVER;
	group->fg.skip_to_order4.triggers[1].condition =
		MISSION_COND_ALWAYS_TRUE;
	group->fg.skip_to_order4.trigger1_or_trigger2 = GOAL_OPERATOR_STR_OR;
	unsigned int books = view_of(1).books;
	return (craft->ai_flight.depart_timer_flag != 0 ? BOOKS_STARTED : 0) |
	       (books & (AI_WIRE_BOOKS_ABORTED | AI_WIRE_BOOKS_SKIP_DUE));
}

/* The books each order's 1997 leader plan kept, read from the retail plan
 * file: Hold Steady and Go Home none; Attack all three; Rendezvous departure
 * and abort; Disabled the skip test alone; Kamikaze departure and the skip
 * test, never an abort; a code no order has none. */
static void check_books_kept_by_order(void)
{
	static const struct {
		uint8_t order;
		unsigned int flying;
		unsigned int aborting;
	} kept[] = {
		{MISSION_ORDER_HOLD_STEADY, 0, 0},
		{MISSION_ORDER_HOLD_STEADY_7, 0, 0},
		{MISSION_ORDER_GO_HOME, 0, 0},
		{MISSION_ORDER_ATTACK, BOOKS_STARTED | AI_WIRE_BOOKS_SKIP_DUE,
		 AI_WIRE_BOOKS_ABORTED},
		{MISSION_ORDER_RENDEZVOUS, BOOKS_STARTED,
		 AI_WIRE_BOOKS_ABORTED},
		{MISSION_ORDER_DISABLED, AI_WIRE_BOOKS_SKIP_DUE,
		 AI_WIRE_BOOKS_SKIP_DUE},
		{MISSION_ORDER_KAMIKAZE, BOOKS_STARTED | AI_WIRE_BOOKS_SKIP_DUE,
		 BOOKS_STARTED | AI_WIRE_BOOKS_SKIP_DUE},
		{40, 0, 0},
	};
	for (size_t row = 0; row < sizeof kept / sizeof kept[0]; ++row) {
		XVT_ASSERT_INT_EQ(books_kept_for(kept[row].order, 0),
				  kept[row].flying);
		XVT_ASSERT_INT_EQ(books_kept_for(kept[row].order, 1),
				  kept[row].aborting);
	}
}

/* The 1997 count of an order's targets left: a matching craft of another
 * group in the battle that may count, a static object whose type counts, a
 * matching group still to arrive or with waves to come; either target pair;
 * for a Disable order a target in working order only. */
static void check_targets_left(void)
{
	fresh_world();
	make_craft(1, TEST_GROUP);
	struct mission_order *order =
		&g_mission_flight_groups[TEST_GROUP].fg.orders[0];
	order->target1_type = 1;
	order->target1 = CARGO_GROUP;
	order->target1_or_target2 = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	struct craft_data *target = make_craft(2, CARGO_GROUP);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	target->object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	target->object_kind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	target->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	target->hull_damage = 2;
	target->hull_max = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	g_objects[2].player_owner_idx = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	g_objects[2].player_owner_idx = -1;
	target->hull_damage = 0;

	order->order = MISSION_ORDER_DISABLE;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	target->working_subsystems = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	order->order = MISSION_ORDER_ATTACK;

	order->target1_or_target2 = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	order->secondary_target_types[0] = 1;
	order->secondary_targets[0] = CARGO_GROUP;
	order->target3_or_target4 = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	order->secondary_targets[0] = TEST_GROUP;
	make_craft(3, TEST_GROUP);
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));

	order->secondary_targets[0] = ALTERNATE_GROUP;
	struct mission_fg_runtime_stats *later =
		&g_mission_fg_stats[ALTERNATE_GROUP];
	later->arrival_enabled = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	later->has_arrived = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	later->waves_remaining = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	later->arrival_enabled = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));

	enum { STATIC_SLOT = SLOT_COUNT - 1, STATIC_TYPE = 20 };
	g_region_main_object_slot_end = STATIC_SLOT;
	g_region_static_object_slot_count = 1;
	g_active_region_craft_object_slot_end = STATIC_SLOT;
	g_objects[STATIC_SLOT].object_type = STATIC_TYPE;
	g_objects[STATIC_SLOT].flight_group_idx = ALTERNATE_GROUP;
	uint8_t flags = g_object_type_table[STATIC_TYPE].behavior_flags;
	g_object_type_table[STATIC_TYPE].behavior_flags = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	g_object_type_table[STATIC_TYPE].behavior_flags = 2;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
	g_object_type_table[STATIC_TYPE].behavior_flags = flags;
}

/* The start point: the world position of the mission point the group's start
 * point reference names, point 0 or a variant, given with its bit once the
 * group has arrived; 0, 0, 0 and the bit clear before, or when the reference
 * names none of the group's 22 points. */
static void check_start_point(void)
{
	fresh_world();
	make_craft(1, TEST_GROUP);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.mission_point_x[0] = 3;
	group->fg.mission_point_y[0] = 4;
	group->fg.mission_point_z[0] = 5;
	group->fg.mission_point_x[2] = 7;
	group->fg.mission_point_y[2] = 8;
	group->fg.mission_point_z[2] = 9;
	group->fg.mission_point_x[21] = 11;
	group->fg.mission_point_y[21] = 12;
	group->fg.mission_point_z[21] = 13;
	struct mission_fg_runtime_stats *stats =
		&g_mission_fg_stats[TEST_GROUP];
	stats->current_mission_point_ref = 0x8002;
	struct ai_wire_craft_view view = view_of(1);
	XVT_ASSERT_INT_EQ(view.books & AI_WIRE_BOOKS_START_POINT, 0);
	for (int axis = 0; axis < 3; ++axis) {
		XVT_ASSERT_INT_EQ(view.start_point[axis], 0);
	}
	stats->has_arrived = 1;
	view = view_of(1);
	XVT_ASSERT_INT_EQ(view.books & AI_WIRE_BOOKS_START_POINT,
			  AI_WIRE_BOOKS_START_POINT);
	XVT_ASSERT_INT_EQ(view.start_point[0], 7 * 256);
	XVT_ASSERT_INT_EQ(view.start_point[1], -8 * 256);
	XVT_ASSERT_INT_EQ(view.start_point[2], 9 * 256);
	stats->current_mission_point_ref = 0x8000;
	view = view_of(1);
	XVT_ASSERT_INT_EQ(view.start_point[0], 3 * 256);
	XVT_ASSERT_INT_EQ(view.start_point[1], -4 * 256);
	XVT_ASSERT_INT_EQ(view.start_point[2], 5 * 256);
	/* The last of the 22 points is one; the next is none, and nor is an
	 * unset reference. */
	stats->current_mission_point_ref = 0x8015;
	view = view_of(1);
	XVT_ASSERT_INT_EQ(view.books & AI_WIRE_BOOKS_START_POINT,
			  AI_WIRE_BOOKS_START_POINT);
	XVT_ASSERT_INT_EQ(view.start_point[2], 13 * 256);
	stats->current_mission_point_ref = 0x8016;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_START_POINT));
	stats->current_mission_point_ref = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_START_POINT));
	/* Another group's arrival is not this group's. */
	stats->current_mission_point_ref = 0x8000;
	stats->has_arrived = 0;
	g_mission_fg_stats[CARGO_GROUP].has_arrived = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_START_POINT));
}

/* Targets now: bit n is set when the order in slot n has a target in the
 * battle now in either target pair, by the books' count; a group still to
 * arrive does not count, a target that may not count does not, and the other
 * bits are 0. */
static void check_targets_now(void)
{
	fresh_world();
	make_craft(1, TEST_GROUP);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	/* Slot 0: the cargo group, first pair. Slot 1: the alternate group,
	 * which is not here but still to arrive. Slot 2: the cargo group,
	 * second pair only. */
	group->fg.orders[0].target1_type = 1;
	group->fg.orders[0].target1 = CARGO_GROUP;
	group->fg.orders[0].target1_or_target2 = 1;
	group->fg.orders[1].target1_type = 1;
	group->fg.orders[1].target1 = ALTERNATE_GROUP;
	group->fg.orders[1].target1_or_target2 = 1;
	group->fg.orders[2].secondary_target_types[0] = 1;
	group->fg.orders[2].secondary_targets[0] = CARGO_GROUP;
	group->fg.orders[2].target3_or_target4 = 1;
	/* A fourth order is no bit of the three. */
	group->fg.orders[3] = group->fg.orders[0];
	g_mission_fg_stats[ALTERNATE_GROUP].arrival_enabled = 1;
	XVT_ASSERT_INT_EQ(view_of(1).targets_now, 0);
	struct craft_data *target = make_craft(2, CARGO_GROUP);
	XVT_ASSERT_INT_EQ(view_of(1).targets_now, 0x5);
	target->object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	XVT_ASSERT_INT_EQ(view_of(1).targets_now, 0);
	target->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	make_craft(3, ALTERNATE_GROUP);
	XVT_ASSERT_INT_EQ(view_of(1).targets_now, 0x7);
	/* The current slot does not matter. */
	g_craft[1].ai_controller.current_order_slot = 1;
	XVT_ASSERT_INT_EQ(view_of(1).targets_now, 0x7);
	g_objects[3].object_type = 0;
	XVT_ASSERT_INT_EQ(view_of(1).targets_now, 0x5);
	/* Later targets count in TARGETS_LEFT but not here. */
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_TARGETS_LEFT));
}

/* A static counts as a target when its slot holds an object whose type
 * carries behavior flag 2 and the books would count it, without a mobile
 * object to read. */
static void check_static_is_target(void)
{
	fresh_world();
	enum { STATIC_SLOT = SLOT_COUNT - 1, STATIC_TYPE = 20 };
	g_region_main_object_slot_end = STATIC_SLOT;
	g_region_static_object_slot_count = 1;
	g_objects[STATIC_SLOT].mobj = NULL;
	uint8_t flags = g_object_type_table[STATIC_TYPE].behavior_flags;
	g_object_type_table[STATIC_TYPE].behavior_flags = 2;
	XVT_ASSERT_TRUE(!ai_books_static_is_target(STATIC_SLOT));
	/* An empty slot is no static, whatever the flags of type 0 say. */
	g_object_type_table[0].behavior_flags = 2;
	XVT_ASSERT_TRUE(!ai_books_static_is_target(STATIC_SLOT));
	g_object_type_table[0].behavior_flags = 0;
	g_objects[STATIC_SLOT].object_type = STATIC_TYPE;
	XVT_ASSERT_TRUE(ai_books_static_is_target(STATIC_SLOT));
	g_object_type_table[STATIC_TYPE].behavior_flags = 0;
	XVT_ASSERT_TRUE(!ai_books_static_is_target(STATIC_SLOT));
	g_object_type_table[STATIC_TYPE].behavior_flags = 1;
	XVT_ASSERT_TRUE(!ai_books_static_is_target(STATIC_SLOT));
	g_object_type_table[STATIC_TYPE].behavior_flags = 3;
	XVT_ASSERT_TRUE(ai_books_static_is_target(STATIC_SLOT));
	g_object_type_table[STATIC_TYPE].behavior_flags = flags;
}

/* The all-returned book, the 1997 order 32's test: every other flight group
 * that leaves into the craft's group as its mothership has arrived, has no
 * wave left and has no craft in the battle. A group with arrival off and no
 * player, one that leaves another way or into another group, and the craft's
 * own group, are left out. */
static void check_all_returned(void)
{
	fresh_world();
	make_craft(1, TEST_GROUP);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	/* Group 5, the last, leaves into the craft's group but has not
	 * arrived. */
	struct mission_flight_group *leaver = &g_mission_flight_groups[5];
	struct mission_fg_runtime_stats *stats = &g_mission_fg_stats[5];
	leaver->fg.departure_method = 1;
	leaver->fg.departure_mothership = TEST_GROUP;
	stats->arrival_enabled = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	stats->has_arrived = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	stats->waves_remaining = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	stats->waves_remaining = 0;
	/* A craft of the group still in the battle, then out of the craft
	 * slots, then gone. */
	make_craft(2, 5);
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	g_active_region_craft_object_slot_end = 2;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	g_active_region_craft_object_slot_end = SLOT_COUNT;
	g_active_region_object_slot_start = 3;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	g_active_region_object_slot_start = 0;
	g_objects[2].object_type = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	/* Another group's craft does not matter. */
	make_craft(3, 4);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	/* Arrival off leaves the group out, unless a player owns it. */
	stats->has_arrived = 0;
	stats->arrival_enabled = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	leaver->player_owner_idx = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	leaver->player_owner_idx = -1;
	stats->arrival_enabled = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	/* Leaving another way, or into another group, leaves it out. */
	leaver->fg.departure_method = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	leaver->fg.departure_method = 1;
	leaver->fg.departure_mothership = ALTERNATE_GROUP;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	/* The craft's own group is left out even if it leaves into itself. */
	struct mission_flight_group *own = &g_mission_flight_groups[TEST_GROUP];
	own->fg.departure_method = 1;
	own->fg.departure_mothership = TEST_GROUP;
	g_mission_fg_stats[TEST_GROUP].arrival_enabled = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
	/* The group asked about is the craft's: group 4 asks, and group 5's
	 * not-yet-arrived departure into group 2 does not matter to it. */
	leaver->fg.departure_mothership = TEST_GROUP;
	XVT_ASSERT_TRUE(has_bit(3, AI_WIRE_BOOKS_ALL_RETURNED));
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_RETURNED));
}

/* The 1997 set-speed rule's full speed: the top speed, 128 here, plus an
 * eighth for each step the recharge levels' sum falls below 6, less an
 * eighth for each step above. */
static void check_full_speed(void)
{
	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	craft->ai_flight.max_speed_cache = 128;
	XVT_ASSERT_INT_EQ(view_of(1).full_speed, 224);
	craft->shield_recharge_level = 2;
	craft->beam_recharge_level = 2;
	craft->laser_recharge_level = 2;
	XVT_ASSERT_INT_EQ(view_of(1).full_speed, 128);
	craft->laser_recharge_level = 3;
	XVT_ASSERT_INT_EQ(view_of(1).full_speed, 112);
	craft->shield_recharge_level = 4;
	craft->beam_recharge_level = 4;
	craft->laser_recharge_level = 4;
	XVT_ASSERT_INT_EQ(view_of(1).full_speed, 32);
}

/* Order 46: a craft whose group leaves by mothership may jump once its
 * mothership is gone and every mothership group it could go home into has
 * arrived in full; not while one is still to come, nor with its mothership
 * here, nor without a hyperdrive. A captured craft's home point is point 12
 * when that point is on. */
static void check_mothership_gone(void)
{
	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.departure_method = 1;
	group->fg.departure_mothership = CARRIER_GROUP;
	uint16_t *carriers = g_mission_fg_stats[CARRIER_GROUP].outcome_count;
	carriers[FLIGHT_GROUP_OUTCOME_TOTAL] = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_MAY_JUMP));
	carriers[FLIGHT_GROUP_OUTCOME_ARRIVED] = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_MAY_JUMP));
	group->fg.alternate_mothership_used = 1;
	group->fg.alternate_mothership = ALTERNATE_GROUP;
	uint16_t *alternates =
		g_mission_fg_stats[ALTERNATE_GROUP].outcome_count;
	alternates[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_MAY_JUMP));
	group->fg.alternate_mothership_used = 0;
	make_carrier(3, CARRIER_GROUP, 50000);
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_MAY_JUMP));
	g_objects[3].object_type = 0;
	craft->model_index = NO_JUMP_MODEL;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_MAY_JUMP));
	craft->model_index = JUMP_MODEL;
	craft->captured_by_flight_group = 1;
	group->fg.captured_depart_via_mothership = 1;
	group->fg.captured_departure_mothership = ALTERNATE_GROUP;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_MAY_JUMP));
	alternates[FLIGHT_GROUP_OUTCOME_ARRIVED] = 2;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_MAY_JUMP));

	group->fg.mission_point_enabled[13] = 1;
	group->fg.mission_point_x[13] = 10;
	group->fg.mission_point_enabled[12] = 1;
	group->fg.mission_point_x[12] = 1;
	XVT_ASSERT_INT_EQ(view_of(1).home_point[0], 256);
	craft->captured_by_flight_group = 0;
	XVT_ASSERT_INT_EQ(view_of(1).home_point[0], 2560);
}

/* The hangar point at side, up and forward of the carrier in slot. */
static void hangar_point(int slot, int side, int up, int forward,
			 int32_t point[3])
{
	flight_geometry_local_to_world(&g_objects[slot], side, up, forward);
	point[0] = g_rotated_x + g_objects[slot].world_x;
	point[1] = g_rotated_y + g_objects[slot].world_y;
	point[2] = g_rotated_z + g_objects[slot].world_z;
}

/* The mothership, the 1997 fly-home order's choice for both flying home and
 * docking: the departure group's leader when the group leaves by
 * mothership, else the alternate's (1997's hangar order took the departure
 * group's even then); never a player's group; a captured craft only its
 * captors' when they leave that way. Its two hangar points, and at the
 * hangar within 512 of the inside one, 1,024 for a starship. */
static void check_mothership(void)
{
	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	g_objects[1].world_x = 20000;
	make_carrier(0, CARRIER_GROUP, 0);
	make_carrier(5, ALTERNATE_GROUP, 50000);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.departure_mothership = CARRIER_GROUP;
	group->fg.alternate_mothership = ALTERNATE_GROUP;
	group->fg.alternate_mothership_used = 1;
	group->fg.departure_method = 1;
	XVT_ASSERT_INT_EQ(view_of(1).mothership, 0);
	group->fg.departure_method = 0;
	XVT_ASSERT_INT_EQ(view_of(1).mothership, 5);
	group->fg.departure_method = 1;
	g_objects[0].object_type = 0;
	XVT_ASSERT_INT_EQ(view_of(1).mothership, 5);
	g_mission_flight_groups[ALTERNATE_GROUP].player_owner_idx = 0;
	XVT_ASSERT_INT_EQ(view_of(1).mothership, UINT16_MAX);
	g_objects[0].object_type = 5;
	craft->captured_by_flight_group = 1;
	group->fg.captured_departure_mothership = CARRIER_GROUP;
	XVT_ASSERT_INT_EQ(view_of(1).mothership, UINT16_MAX);
	group->fg.captured_depart_via_mothership = 1;
	XVT_ASSERT_INT_EQ(view_of(1).mothership, 0);
	craft->captured_by_flight_group = 0;

	int32_t outside[3];
	int32_t inside[3];
	hangar_point(0, 0, -300, 2000, outside);
	hangar_point(0, 100, -200, 300, inside);
	struct ai_wire_craft_view view = view_of(1);
	for (int axis = 0; axis < 3; ++axis) {
		XVT_ASSERT_INT_EQ(view.outside_hangar[axis], outside[axis]);
		XVT_ASSERT_INT_EQ(view.inside_hangar[axis], inside[axis]);
	}
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_HANGAR));
	g_objects[1].world_x = inside[0];
	g_objects[1].world_y = inside[1];
	g_objects[1].world_z = inside[2];
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_AT_HANGAR));
	g_objects[1].world_x = inside[0] + 600;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_HANGAR));
	g_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_AT_HANGAR));
	craft->object_kind = CRAFT_OBJECT_KIND_DISABLED;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_HANGAR));
}

/* Docking: the craft and the cargo it carries are counted, recorded with the
 * mothership's outcome and freed; a follower in formation elsewhere stays,
 * where 1997 took every formation follower of the group in with it. Nothing
 * happens when the mothership has gone. */
static void check_dock(void)
{
	fresh_world();
	make_carrier(0, CARRIER_GROUP, 0);
	make_craft(1, TEST_GROUP);
	struct craft_data *follower = make_craft(2, TEST_GROUP);
	follower->leader_obj_idx = 1;
	g_objects[2].world_x = 30000;
	make_craft(3, CARGO_GROUP);
	g_craft[1].carried_object_index = 3;
	g_mission_flight_groups[TEST_GROUP].fg.departure_method = 1;
	g_mission_flight_groups[TEST_GROUP].fg.departure_mothership =
		CARRIER_GROUP;
	ai_books_dock(1);
	XVT_ASSERT_INT_EQ(g_objects[1].object_type, 0);
	XVT_ASSERT_INT_EQ(count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_DEPARTED),
			  1);
	XVT_ASSERT_INT_EQ(
		count_of(TEST_GROUP,
			 FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT),
		1);
	XVT_ASSERT_INT_EQ(g_objects[2].object_type, 5);
	XVT_ASSERT_INT_EQ(g_objects[3].object_type, 0);
	XVT_ASSERT_INT_EQ(
		count_of(CARGO_GROUP,
			 FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION),
		1);
	XVT_ASSERT_INT_EQ(
		count_of(CARGO_GROUP,
			 FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT),
		1);

	fresh_world();
	make_carrier(5, ALTERNATE_GROUP, 0);
	make_craft(1, TEST_GROUP);
	g_mission_flight_groups[TEST_GROUP].fg.alternate_mothership =
		ALTERNATE_GROUP;
	g_mission_flight_groups[TEST_GROUP].fg.alternate_mothership_used = 1;
	ai_books_dock(1);
	XVT_ASSERT_INT_EQ(
		count_of(TEST_GROUP,
			 FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT),
		1);
	g_objects[5].object_type = 0;
	make_craft(2, TEST_GROUP);
	ai_books_dock(2);
	XVT_ASSERT_INT_EQ(g_objects[2].object_type, 5);
	XVT_ASSERT_INT_EQ(count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_DEPARTED),
			  1);
}

/* The 1997 departure counts by the craft's marks, each craft jumping out at
 * speed 3,600: orders done, aborted, departing (1 started, 2 due), skipped
 * to order 4. */
static void check_departure_counts(void)
{
	static const struct {
		int home, aborted, departing, skipped, departed, incomplete;
	} cases[] = {
		{0, 0, 0, 0, 1, 0}, {0, 1, 0, 0, 0, 0}, {1, 1, 0, 0, 1, 0},
		{0, 0, 1, 0, 0, 0}, {1, 0, 2, 0, 1, 0}, {0, 0, 0, 1, 0, 1},
		{1, 0, 0, 1, 0, 1}, {0, 1, 0, 1, 0, 0}, {0, 0, 2, 1, 0, 0},
	};
	for (size_t index = 0; index < sizeof cases / sizeof cases[0];
	     ++index) {
		fresh_world();
		struct craft_data *craft = make_craft(1, TEST_GROUP);
		craft->ai_flight.go_home_flag = (uint8_t)cases[index].home;
		craft->ai_flight.mission_aborted_flag =
			(uint8_t)cases[index].aborted;
		craft->ai_flight.depart_timer_flag =
			(uint8_t)cases[index].departing;
		craft->ai_controller.skipped_to_order4 =
			(uint8_t)cases[index].skipped;
		craft->object_kind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
		g_mobiles[1].speed = 3600;
		ai_books_finish_jumps();
		XVT_ASSERT_INT_EQ(g_objects[1].object_type, 0);
		XVT_ASSERT_INT_EQ(
			count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_DEPARTED),
			cases[index].departed);
		XVT_ASSERT_INT_EQ(
			count_of(
				TEST_GROUP,
				FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE),
			cases[index].incomplete);
		XVT_ASSERT_INT_EQ(
			count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_LEFT_REGION),
			1);
	}

	fresh_world();
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	craft->object_kind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
	g_mobiles[1].speed = 3599;
	ai_books_finish_jumps();
	XVT_ASSERT_INT_EQ(g_objects[1].object_type, 5);
	g_mobiles[1].speed = 3600;
	g_objects[1].player_owner_idx = 0;
	ai_books_finish_jumps();
	XVT_ASSERT_INT_EQ(g_objects[1].object_type, 5);

	/* A captured craft of team 2 from a team 1 group, carrying captured
	 * cargo of team 2. */
	fresh_world();
	craft = make_craft(1, TEST_GROUP);
	craft->captured_by_flight_group = 1;
	g_mobiles[1].team = 2;
	g_mission_flight_groups[TEST_GROUP].fg.team = 1;
	make_craft(3, CARGO_GROUP)->captured_by_flight_group = 1;
	g_mobiles[3].team = 2;
	craft->carried_object_index = 3;
	craft->object_kind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
	g_mobiles[1].speed = 3600;
	ai_books_finish_jumps();
	XVT_ASSERT_INT_EQ(count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_DEPARTED),
			  0);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[TEST_GROUP].team_captured_departed_count[2],
		1);
	for (int team = 0; team < 10; ++team) {
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[TEST_GROUP]
					  .team_uncaptured_lost[team],
				  team != 1 && team != 2);
	}
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[CARGO_GROUP].team_captured_departed_count[2],
		1);
	XVT_ASSERT_INT_EQ(
		count_of(CARGO_GROUP,
			 FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION),
		1);
	XVT_ASSERT_INT_EQ(g_objects[3].object_type, 0);
}

static struct {
	int calls;
	uint8_t actions[16];
	size_t action_bytes;
	int action_count;
} g_program;

static uint8_t *put_u16(uint8_t *at, unsigned int value)
{
	at[0] = (uint8_t)value;
	at[1] = (uint8_t)(value >> 8);
	return at + 2;
}

static uint8_t *put_u32(uint8_t *at, uint32_t value)
{
	return put_u16(put_u16(at, value & 0xFFFF), value >> 16);
}

static unsigned int get_u16(const uint8_t *at)
{
	return (unsigned int)at[0] | (unsigned int)at[1] << 8;
}

/* A pilot program that answers every craft asked about with an empty
 * notebook, the first with g_program's actions and the rest with none. */
static size_t fake_program(const uint8_t *question, size_t question_length,
			   uint8_t *answer, size_t answer_capacity)
{
	(void)question_length;
	XVT_ASSERT_TRUE(answer_capacity >= ANSWER_CAPACITY);
	++g_program.calls;
	int count = (int)get_u16(question + 16);
	uint8_t *at = put_u32(answer, 0);
	at = put_u32(at, AI_WIRE_MAGIC);
	at = put_u16(at, AI_WIRE_VERSION);
	at = put_u16(at, AI_WIRE_KIND_ANSWER);
	memcpy(at, question + 12, 4);
	at += 4;
	at = put_u16(at, (unsigned int)count);
	at = put_u16(at, 0);
	for (int craft = 0; craft < count; ++craft) {
		at = put_u16(at,
			     get_u16(question + AI_WIRE_HEAD_BYTES +
				     (size_t)craft * AI_WIRE_CRAFT_VIEW_BYTES));
		memset(at, 0, AI_NOTEBOOK_BYTES);
		at += AI_NOTEBOOK_BYTES;
		*at++ = (uint8_t)(craft == 0 ? g_program.action_count : 0);
		if (craft == 0) {
			memcpy(at, g_program.actions, g_program.action_bytes);
			at += g_program.action_bytes;
		}
	}
	put_u32(answer, (uint32_t)(at - answer - 4));
	return (size_t)(at - answer);
}

static void program_sends(int count, const uint8_t *actions, size_t bytes)
{
	g_program.action_count = count;
	memcpy(g_program.actions, actions, bytes);
	g_program.action_bytes = bytes;
}

/* A jump through the think loop: a depart the books do not allow refuses
 * the whole answer; an allowed one marks the craft entering hyperspace with
 * the 1997 timers, after which a steer no longer turns it; at speed 3,600
 * the next step counts and frees it. That step, run again from the world
 * saved before it, gives the same world checksum. */
static void check_jump_and_replay(void)
{
	static const uint8_t steer_depart[] = {
		AI_WIRE_ACTION_STEER, 0, 0x40, 0, 0, 0, 0,
		AI_WIRE_ACTION_DEPART};
	fresh_world();
	memset(&g_program, 0, sizeof g_program);
	struct craft_data *craft = make_craft(1, TEST_GROUP);
	craft->model_index = NO_JUMP_MODEL;
	ai_brain_set_exchange(fake_program);
	program_sends(2, steer_depart, sizeof steer_depart);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_program.calls, 1);
	XVT_ASSERT_INT_EQ(craft->object_kind, CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 0);

	craft->model_index = JUMP_MODEL;
	craft->ai_controller.think_timer = 0;
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_program.calls, 2);
	XVT_ASSERT_INT_EQ(craft->object_kind,
			  CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 1652);
	XVT_ASSERT_INT_EQ(craft->ai_controller.secondary_maneuver_timer, 944);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 0);
	program_sends(1, steer_depart, 7);
	craft->ai_controller.think_timer = 0;
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_program.calls, 3);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 0);

	g_mobiles[1].speed = 3600;
	size_t capacity = xvt_snapshot_calculate_size();
	uint8_t *image = calloc(1, capacity + 64);
	XVT_ASSERT_TRUE(image != NULL);
	uint8_t *saved_buffer = g_world_state_buffer;
	g_world_state_buffer = image;
	xvt_snapshot_save();
	int before = xvt_snapshot_live_checksum();
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_objects[1].object_type, 0);
	XVT_ASSERT_INT_EQ(count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_DEPARTED),
			  1);
	int after = xvt_snapshot_live_checksum();
	XVT_ASSERT_TRUE(after != before);
	xvt_snapshot_restore();
	XVT_ASSERT_INT_EQ(xvt_snapshot_live_checksum(), before);
	XVT_ASSERT_INT_EQ(g_objects[1].object_type, 5);
	XVT_ASSERT_INT_EQ(count_of(TEST_GROUP, FLIGHT_GROUP_OUTCOME_DEPARTED),
			  0);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(xvt_snapshot_live_checksum(), after);
	XVT_ASSERT_INT_EQ(g_program.calls, 3);
	g_world_state_buffer = saved_buffer;
	free(image);
	ai_brain_set_exchange(NULL);
}

/* SS Launch's test, the 1997 order 33: every other group that arrives from
 * the craft's group as its mothership has arrived with no waves left. A
 * group that has not arrived, or has a wave left, makes it no. Unlike the
 * return test, a craft of that group still in the battle does not. Left out:
 * a group whose arrival is off and that no player owns, one that arrives
 * another way or from another group, and the craft's own group. */
static void check_all_launched(void)
{
	fresh_world();
	make_craft(1, TEST_GROUP);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	struct mission_flight_group *launched = &g_mission_flight_groups[5];
	struct mission_fg_runtime_stats *stats = &g_mission_fg_stats[5];
	launched->fg.arrival_method = 1;
	launched->fg.arrival_mothership = TEST_GROUP;
	stats->arrival_enabled = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	stats->has_arrived = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	stats->waves_remaining = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	stats->waves_remaining = 0;
	/* Its craft still in the battle do not count. */
	make_craft(2, 5);
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	/* Departure fields are the return test's, not this one's. */
	stats->has_arrived = 0;
	launched->fg.departure_method = 1;
	launched->fg.departure_mothership = TEST_GROUP;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	/* Arrival off leaves the group out, unless a player owns it. */
	stats->arrival_enabled = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	launched->player_owner_idx = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	launched->player_owner_idx = -1;
	stats->arrival_enabled = 1;
	/* Arriving another way, or from another group, leaves it out. */
	launched->fg.arrival_method = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	launched->fg.arrival_method = 1;
	launched->fg.arrival_mothership = CARRIER_GROUP;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	/* The craft's own group is left out. */
	launched->fg.arrival_mothership = 5;
	g_objects[1].flight_group_idx = 5;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	g_objects[1].flight_group_idx = TEST_GROUP;
	/* The last of two groups launched counts as well as the first. */
	launched->fg.arrival_mothership = TEST_GROUP;
	g_mission_flight_groups[4].fg.arrival_method = 1;
	g_mission_flight_groups[4].fg.arrival_mothership = TEST_GROUP;
	g_mission_fg_stats[4].arrival_enabled = 1;
	stats->has_arrived = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
	g_mission_fg_stats[4].has_arrived = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_ALL_LAUNCHED));
}

/* Slot 1, group TEST_GROUP, a small craft (genus 1) of JUMP_MODEL, boards
 * slot 2, group CARGO_GROUP, of CARRIER_MODEL, at (1000, 2000, 3000) level
 * with zero yaw and roll, on a Capture order, aimed at it. The boarder's
 * docking-to heights are -20 and -60; the target's docking-from heights 100
 * and 300 and its forward offset 700. */
static void boarding_world(void)
{
	fresh_world();
	make_craft(1, TEST_GROUP);
	make_craft(2, CARGO_GROUP);
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_CAPTURE_CARGO;
	g_craft[1].ai_controller.target_obj_idx = 2;
	g_craft[2].model_index = CARRIER_MODEL;
	g_objects[2].world_x = 1000;
	g_objects[2].world_y = 2000;
	g_objects[2].world_z = 3000;
	g_objects[2].pitch = 0x4000;
	g_mobiles[2].orient_matrix_dirty = 1;
	g_model_defs[JUMP_MODEL].dock_to_up[0] = -20;
	g_model_defs[JUMP_MODEL].dock_to_up[1] = -60;
	g_model_defs[CARRIER_MODEL].dock_from_up[0] = 100;
	g_model_defs[CARRIER_MODEL].dock_from_up[1] = 300;
	g_model_defs[CARRIER_MODEL].dock_forward = 700;
}

/* The board point's offset from the target in world axes. The target is
 * level with zero yaw and roll, so its forward axis is world y and its up
 * axis world z; the Q15 product of a height with the axis (32767 over 32768)
 * cuts each of 700, 840, 1,180, 1,380 and 0x7000 down by 1. */
static void expect_point(int east, int north, int up)
{
	const struct ai_wire_craft_view view = view_of(1);
	XVT_ASSERT_INT_EQ(view.board_point[0], 1000 + east);
	XVT_ASSERT_INT_EQ(view.board_point[1], 2000 + north);
	XVT_ASSERT_INT_EQ(view.board_point[2], 3000 + up);
}

/* 1997's approach point above the target's hatch, by the genus of the target
 * and of the boarder: both small craft use the target's small docking
 * height less the boarder's, a big target with a small boarder adds the big
 * one, two big craft use the big heights; twice the big-height gap is added
 * to the first, and a sum below 0 falls back to 0x7000. */
static void check_board_point_heights(void)
{
	/* Target small: (100 + 20) + 2 x (300 + 60) = 840. */
	boarding_world();
	g_objects[2].genus_id = 1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_BOARD_POINT));
	expect_point(0, 699, 839);
	/* Target big, boarder small: (300 + 100 + 60) + 720 = 1180. */
	g_objects[2].genus_id = CRAFT_GENUS_STARSHIP;
	expect_point(0, 699, 1179);
	/* Both big: (2 x 300 + 60) + 720 = 1380. */
	g_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	expect_point(0, 699, 1379);
	/* Genus 2 is already big for the boarder as for the target. */
	g_objects[1].genus_id = 2;
	g_objects[2].genus_id = CRAFT_GENUS_STARSHIP;
	expect_point(0, 699, 1379);
	g_objects[1].genus_id = 1;
	/* Genus 2 is already big for the target. */
	g_objects[1].genus_id = 1;
	g_objects[2].genus_id = 2;
	expect_point(0, 699, 1179);
	/* A negative sum falls back to 0x7000. */
	g_objects[2].genus_id = 1;
	g_model_defs[CARRIER_MODEL].dock_from_up[0] = -5000;
	g_model_defs[CARRIER_MODEL].dock_from_up[1] = -100;
	expect_point(0, 699, 28670);
	/* A sum of exactly 0 is kept: (60 + 20) + 2 x (-100 + 60) = 0. */
	g_model_defs[CARRIER_MODEL].dock_from_up[0] = 60;
	expect_point(0, 699, 0);
}

/* The board point exists on exactly the eight boarding orders. */
static void check_board_point_orders(void)
{
	for (int code = 0; code < 40; ++code) {
		boarding_world();
		g_objects[2].genus_id = 1;
		g_mission_flight_groups[TEST_GROUP].fg.orders[0].order =
			(uint8_t)code;
		struct ai_wire_craft_view view = view_of(1);
		int boards =
			(code >= 12 && code <= 17) || code == 31 || code == 32;
		XVT_ASSERT_INT_EQ((view.books & AI_WIRE_BOOKS_BOARD_POINT) != 0,
				  boards);
		XVT_ASSERT_INT_EQ(view.board_point[2] != 0, boards);
	}
}

/* No board point unless the target is a craft in the active region with a
 * model: not none, a mission point reference, itself, a slot before the
 * region or at its end, an empty slot, or another family's object. */
static void check_board_point_targets(void)
{
	static const uint16_t targets[] = {UINT16_MAX, 0x8001, 1, 0, 8, 3};
	for (size_t row = 0; row < sizeof targets / sizeof targets[0]; ++row) {
		boarding_world();
		g_active_region_object_slot_start = 1;
		g_active_region_craft_object_slot_end = 8;
		make_craft(0, CARGO_GROUP);
		make_craft(8, CARGO_GROUP);
		g_craft[1].ai_controller.target_obj_idx = targets[row];
		struct ai_wire_craft_view view = view_of(1);
		XVT_ASSERT_INT_EQ(view.books & (AI_WIRE_BOOKS_BOARD_POINT |
						AI_WIRE_BOOKS_AT_BOARD_POINT),
				  0);
		for (int axis = 0; axis < 3; ++axis) {
			XVT_ASSERT_INT_EQ(view.board_point[axis], 0);
		}
	}
	/* The region's first slot is a target as any other. */
	boarding_world();
	g_active_region_object_slot_start = 2;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_BOARD_POINT));
	boarding_world();
	g_objects[2].object_type = 0;
	XVT_ASSERT_INT_EQ(view_of(1).books & AI_WIRE_BOOKS_BOARD_POINT, 0);
	boarding_world();
	g_mobiles[2].family = 1;
	XVT_ASSERT_INT_EQ(view_of(1).books & AI_WIRE_BOOKS_BOARD_POINT, 0);
}

/* At the board point: within 2,048 of the point by the rough polar
 * distance, 2,048 itself included, and the target not a player's craft that
 * moves. A moving player's craft that is not the local player's is not
 * asked; the local player's is asked once to set the throttle to 0 and
 * the bit stays clear until it stops. */
static void check_at_board_point(void)
{
	boarding_world();
	g_objects[2].genus_id = 1;
	/* The ask is a system pane message (type 3), which the game drops while
	 * system messages are off; the log line of the ask is what shows it was
	 * sent. */
	static char ask_text[] = "\003ask";
	g_str_in_flight_messages
		[IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU] =
			ask_text;
	g_system_message_display_enabled = 0;
	for (int slot = 0; slot < 11; ++slot) {
		g_ready_message_pane_queue[slot].state_or_message_id =
			UINT16_MAX;
	}
	g_log_line_count = 0;
	g_objects[1].world_x = 1000;
	g_objects[1].world_y = 2699;
	g_objects[1].world_z = 3839;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	g_objects[1].world_x = 1000 + 2048;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	g_objects[1].world_x = 1000 - 2048;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	g_objects[1].world_x = 1000 + 2049;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_BOARD_POINT));
	g_objects[1].world_x = 1000;
	/* A moving player's craft that is not the local player's. */
	g_local_player = 1;
	g_objects[2].player_owner_idx = 0;
	g_mobiles[2].speed = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_BOARD_POINT));
	XVT_ASSERT_INT_EQ(lines_starting("ai.board_asked_stop"), 0);
	/* An AI craft that moves is not asked to stop: only a player's is. */
	g_objects[2].player_owner_idx = -1;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	g_objects[2].player_owner_idx = 0;
	/* Stopped, it is at the point again. */
	g_mobiles[2].speed = 0;
	XVT_ASSERT_TRUE(has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	/* The local player's moving craft is asked, far from the point not. */
	g_mobiles[2].speed = 1;
	g_local_player = 0;
	g_objects[1].world_x = 1000 + 2049;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	XVT_ASSERT_INT_EQ(lines_starting("ai.board_asked_stop"), 0);
	g_objects[1].world_x = 1000;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	XVT_ASSERT_INT_EQ(lines_starting("ai.board_asked_stop"), 1);
	XVT_ASSERT_TRUE(strstr(g_log_lines[g_log_line_count - 1],
			       "object=1 target=2 slot=0") != NULL);
	/* Asked again while the ask is still queued: not asked twice. */
	g_ready_message_pane_queue[4].state_or_message_id =
		IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU;
	g_log_line_count = 0;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	XVT_ASSERT_INT_EQ(lines_starting("ai.board_asked_stop"), 0);
	/* A queued ask past the first ten slots is not seen. */
	g_ready_message_pane_queue[4].state_or_message_id = UINT16_MAX;
	g_ready_message_pane_queue[10].state_or_message_id =
		IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	XVT_ASSERT_INT_EQ(lines_starting("ai.board_asked_stop"), 1);
	/* In a predicted step nothing is asked. */
	g_ready_message_pane_queue[10].state_or_message_id = UINT16_MAX;
	g_log_line_count = 0;
	g_flight_sim_side_effects_suppressed = 1;
	XVT_ASSERT_TRUE(!has_bit(1, AI_WIRE_BOOKS_AT_BOARD_POINT));
	XVT_ASSERT_INT_EQ(lines_starting("ai.board_asked_stop"), 0);
	g_flight_sim_side_effects_suppressed = 0;
	g_local_player = 0;
}

/* The goals' progress and the craft it has docked with. */
static void check_order_progress_and_boarded(void)
{
	boarding_world();
	struct craft_data *craft = &g_craft[1];
	for (int slot = 0; slot < 4; ++slot) {
		craft->ai_controller.order_progress.goal_progress[slot] =
			(uint8_t)(slot + 5);
	}
	for (int slot = 0; slot < 10; ++slot) {
		craft->ai_flight.docked_target_signatures[slot] =
			(uint16_t)(0x300 + slot);
	}
	for (int count = 0; count <= 10; count += 5) {
		craft->ai_flight.docked_target_count = (uint8_t)count;
		struct ai_wire_craft_view view = view_of(1);
		XVT_ASSERT_INT_EQ(view.order_progress[0], 5);
		XVT_ASSERT_INT_EQ(view.order_progress[1], 6);
		XVT_ASSERT_INT_EQ(view.order_progress[2], 7);
		XVT_ASSERT_INT_EQ(view.order_progress[3], 8);
		XVT_ASSERT_INT_EQ(view.boarded_count, count);
		for (int slot = 0; slot < 10; ++slot) {
			XVT_ASSERT_INT_EQ(view.boarded[slot],
					  slot < count ? 0x300 + slot : 0);
		}
	}
	/* A count past the ten signatures the craft holds reads as ten. */
	craft->ai_flight.docked_target_count = 11;
	struct ai_wire_craft_view clamped = view_of(1);
	XVT_ASSERT_INT_EQ(clamped.boarded_count, 10);
	XVT_ASSERT_INT_EQ(clamped.boarded[9], 0x309);
	/* Without a board point they are still given. */
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].order = 7;
	craft->ai_flight.docked_target_count = 3;
	struct ai_wire_craft_view view = view_of(1);
	XVT_ASSERT_INT_EQ(view.order_progress[3], 8);
	XVT_ASSERT_INT_EQ(view.boarded[2], 0x302);
	XVT_ASSERT_INT_EQ(view.boarded[3], 0);
}

int main(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_departure();
	check_follower();
	check_aborts();
	check_skip_and_orders();
	check_view_orders_and_home();
	check_mothership();
	check_dock();
	check_departure_counts();
	check_jump_and_replay();
	check_books_kept_by_order();
	check_targets_left();
	check_all_returned();
	check_start_point();
	check_targets_now();
	check_static_is_target();
	check_full_speed();
	check_mothership_gone();
	check_all_launched();
	check_board_point_heights();
	check_board_point_orders();
	check_board_point_targets();
	check_at_board_point();
	check_order_progress_and_boarded();
	return 0;
}

/* Tests for xvt/flight/ai_brain.c's think loop: which craft are asked about,
 * the question's bytes, what an accepted answer changes, and that a refused
 * answer, or no exchange at all, changes nothing but the think timers. The
 * pilot program is a fake exchange here: it keeps each question and hands
 * back an answer this file builds byte by byte from the layout in
 * ai_wire.h. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/ai/ai_wire.h"

enum {
	SLOT_COUNT = 40,
	TEST_TICK = 0x01020304,
	TEST_GROUP = 2,
	TEST_INTERVAL = 59,
	/* Craft in one question: ai_brain.c's AI_BRAIN_QUESTION_CRAFT. */
	QUESTION_CRAFT = 32,
	QUESTION_CAPACITY = AI_WIRE_HEAD_BYTES +
			    QUESTION_CRAFT * AI_WIRE_CRAFT_VIEW_BYTES +
			    AI_WIRE_CONTACT_HEAD_BYTES +
			    AI_WIRE_CONTACTS_MAX * AI_WIRE_CONTACT_BYTES +
			    AI_WIRE_CONTACT_HEAD_BYTES +
			    AI_WIRE_STATICS_MAX * AI_WIRE_CONTACT_BYTES,
	/* Where a craft view's notebook starts. */
	VIEW_NOTEBOOK = AI_WIRE_CRAFT_VIEW_BYTES - AI_NOTEBOOK_BYTES,
	/* Where what a boarder sees starts, the 57 bytes before the notebook:
	 * board point 12, order progress 4, boarded count 1, boarded 20, dock
	 * phase 1, lifetime timer 2, order first pair mask 4, drop point 12,
	 * drop phase 1. */
	VIEW_BOARD_POINT = VIEW_NOTEBOOK - 57,
	VIEW_ORDER_PROGRESS = VIEW_BOARD_POINT + 12,
	VIEW_BOARDED_COUNT = VIEW_ORDER_PROGRESS + 4,
	VIEW_BOARDED = VIEW_BOARDED_COUNT + 1,
	VIEW_BOARD_PHASE = VIEW_BOARDED + 20,
	VIEW_LIFETIME = VIEW_BOARD_PHASE + 1,
	VIEW_FIRST_PAIR = VIEW_LIFETIME + 2,
	VIEW_DROP_POINT = VIEW_FIRST_PAIR + 4,
	VIEW_DROP_PHASE = VIEW_DROP_POINT + 12,
	/* Where its escorted craft starts, the 35 bytes before those:
	 * escorted 2, station 12, start point 12, targets now 1, static target
	 * mask 8. */
	VIEW_ESCORTED = VIEW_BOARD_POINT - 35,
	VIEW_STATION = VIEW_ESCORTED + 2,
	VIEW_START_POINT = VIEW_STATION + 12,
	VIEW_TARGETS_NOW = VIEW_START_POINT + 12,
	VIEW_STATIC_MASK = VIEW_TARGETS_NOW + 1,
	/* Where its maneuver timer starts, the 4 bytes before those. */
	VIEW_TIMER = VIEW_ESCORTED - 4,
	/* Where its lock starts, the 6 bytes before the timer: lock ticks,
	 * lock needed, lock state, skill level. */
	VIEW_LOCK = VIEW_TIMER - 6,
	/* Where its formation place starts, the 12 bytes before the lock. */
	VIEW_PLACE = VIEW_LOCK - 12,
	/* Where its commands start, the 5 bytes before the place. */
	VIEW_COMMANDS = VIEW_PLACE - 5,
	/* Where its waypoints start, the 97 bytes before the commands. */
	VIEW_WAYPOINTS = VIEW_COMMANDS - 97,
	/* Where its countermeasures start, the 72 bytes before the
	 * waypoints, and where the first threat's object index is. */
	VIEW_COUNTERMEASURES = VIEW_WAYPOINTS - 72,
	VIEW_FIRST_THREAT = VIEW_COUNTERMEASURES + 8,
	ANSWER_CAPACITY = AI_WIRE_HEAD_BYTES +
			  QUESTION_CRAFT * AI_WIRE_CRAFT_ANSWER_BYTES_MAX,
	MAX_CALLS = 2,
	/* The notebook byte 0 the echoing fake writes into every answer. */
	ECHO_MARK = 0xAB,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[SLOT_COUNT];
static struct craft_data g_test_craft[SLOT_COUNT];

/* What the fake exchange saw and what it answers: the canned bytes when
 * echo is 0, else an answer with no actions for whatever craft the question
 * names, each notebook a copy of the craft's with byte 0 set to
 * ECHO_MARK. */
static struct {
	int calls;
	uint8_t question[MAX_CALLS][QUESTION_CAPACITY];
	size_t question_length[MAX_CALLS];
	int echo;
	uint8_t answer[ANSWER_CAPACITY];
	size_t answer_length;
} g_fake;

static unsigned int get_u16(const uint8_t *at)
{
	return (unsigned int)at[0] | (unsigned int)at[1] << 8;
}

static uint32_t get_u32(const uint8_t *at)
{
	return (uint32_t)get_u16(at) | (uint32_t)get_u16(at + 2) << 16;
}

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

/* Writes an answer's length field and header for count craft at tick;
 * returns where the first craft answer goes. The length is set by
 * finish_answer. */
static uint8_t *start_answer(uint8_t *out, uint32_t tick, int count)
{
	uint8_t *at = put_u32(out, 0);
	at = put_u32(at, AI_WIRE_MAGIC);
	at = put_u16(at, AI_WIRE_VERSION);
	at = put_u16(at, AI_WIRE_KIND_ANSWER);
	at = put_u32(at, tick);
	at = put_u16(at, (unsigned int)count);
	return put_u16(at, 0);
}

static size_t finish_answer(uint8_t *out, const uint8_t *end)
{
	size_t length = (size_t)(end - out);
	put_u32(out, (uint32_t)(length - 4));
	return length;
}

/* Writes one craft answer's object index and a notebook whose byte i is
 * first + i, then the action count; returns where the actions go. */
static uint8_t *craft_answer(uint8_t *at, int object_index, int first,
			     int action_count)
{
	at = put_u16(at, (unsigned int)object_index);
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		*at++ = (uint8_t)(first + byte);
	}
	*at++ = (uint8_t)action_count;
	return at;
}

static size_t fake_exchange(const uint8_t *question, size_t question_length,
			    uint8_t *answer, size_t answer_capacity)
{
	XVT_ASSERT_TRUE(g_fake.calls < MAX_CALLS);
	XVT_ASSERT_TRUE(question_length <= QUESTION_CAPACITY);
	memcpy(g_fake.question[g_fake.calls], question, question_length);
	g_fake.question_length[g_fake.calls] = question_length;
	++g_fake.calls;
	if (!g_fake.echo) {
		XVT_ASSERT_TRUE(g_fake.answer_length <= answer_capacity);
		memcpy(answer, g_fake.answer, g_fake.answer_length);
		return g_fake.answer_length;
	}
	int count = (int)get_u16(question + 16);
	uint8_t *at = start_answer(answer, get_u32(question + 12), count);
	for (int craft = 0; craft < count; ++craft) {
		const uint8_t *view = question + AI_WIRE_HEAD_BYTES +
				      (size_t)craft * AI_WIRE_CRAFT_VIEW_BYTES;
		at = put_u16(at, get_u16(view));
		memcpy(at, view + VIEW_NOTEBOOK, AI_NOTEBOOK_BYTES);
		at[0] = ECHO_MARK;
		at += AI_NOTEBOOK_BYTES;
		*at++ = 0;
	}
	XVT_ASSERT_TRUE((size_t)(at - answer) <= answer_capacity);
	return finish_answer(answer, at);
}

/* Empty slots 0 to SLOT_COUNT - 1 with their mobile objects and craft
 * records, the active region's craft slots 0 to 8, no shot slots, the
 * clock at TEST_TICK, and the fake exchange set, canned with no answer. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(&g_fake, 0, sizeof g_fake);
	g_object_table = g_test_objects;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		g_test_objects[slot].player_owner_idx = -1;
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = 8;
	g_projectile_object_slot_start = 0;
	g_projectile_object_slot_end = 0;
	g_game_time = TEST_TICK;
	g_flight_sim_side_effects_suppressed = 0;
	ai_brain_set_exchange(fake_exchange);
}

/* Makes slot a computer craft due to think: think timer 0, think interval
 * TEST_INTERVAL, in flight group TEST_GROUP, leading its own flight. */
static struct craft_data *due_craft(int slot)
{
	g_test_objects[slot].object_type = 5;
	g_test_objects[slot].object_signature = (uint16_t)(0x100 + slot);
	g_test_objects[slot].flight_group_idx = TEST_GROUP;
	g_test_craft[slot].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_test_craft[slot].ai_controller.think_interval = TEST_INTERVAL;
	g_test_craft[slot].ai_controller.think_timer = 0;
	g_test_craft[slot].leader_obj_idx = UINT8_MAX;
	/* As spawn leaves them: carried by none, no player's command. */
	g_test_craft[slot].carrier_obj_idx = UINT16_MAX;
	g_test_craft[slot].ai_controller.candidate_target_idx = UINT16_MAX;
	g_test_craft[slot].player_command_avoid_target_obj_idx = UINT16_MAX;
	return &g_test_craft[slot];
}

/* The object index of craft view craft in question call. */
static int asked_object(int call, int craft)
{
	return (int)get_u16(g_fake.question[call] + AI_WIRE_HEAD_BYTES +
			    (size_t)craft * AI_WIRE_CRAFT_VIEW_BYTES);
}

/* With no exchange set the loop asks nothing and changes nothing, timers
 * included: today's game, before the pilot program runs. */
static void check_no_exchange_changes_nothing(void)
{
	fresh_world();
	due_craft(0);
	due_craft(1)->ai_controller.think_timer = -40;
	ai_brain_set_exchange(NULL);
	struct craft_data before[SLOT_COUNT];
	memcpy(before, g_test_craft, sizeof before);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_fake.calls, 0);
	XVT_ASSERT_TRUE(memcmp(before, g_test_craft, sizeof before) == 0);
}

/* The 1997 rule for which craft are due: only the active region's craft
 * slots; not an empty slot, an object of another family, a craft breaking up
 * or exploding, or one whose timer is above 0. A due craft a player flies is
 * not asked but its timer grows like an asked craft's; the others' timers
 * stay. */
static void check_who_is_asked(void)
{
	fresh_world();
	g_active_region_object_slot_start = 1;
	g_active_region_craft_object_slot_end = 9;
	due_craft(0); /* Below the region. */
	/* Slot 1 stays empty, its timer 0. */
	due_craft(2);
	due_craft(3);
	g_test_mobiles[3].family = 1;
	due_craft(4)->object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	due_craft(5)->object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	due_craft(6)->ai_controller.think_timer = 1;
	due_craft(7)->ai_controller.think_timer = -3;
	g_test_objects[7].player_owner_idx = 0;
	due_craft(8)->ai_controller.think_timer = -5;
	due_craft(9); /* At the region's end. */
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	XVT_ASSERT_INT_EQ(get_u16(g_fake.question[0] + 16), 2);
	XVT_ASSERT_INT_EQ(asked_object(0, 0), 2);
	XVT_ASSERT_INT_EQ(asked_object(0, 1), 8);
	static const int timer_after[10] = {0,
					    0,
					    TEST_INTERVAL,
					    0,
					    0,
					    0,
					    1,
					    TEST_INTERVAL - 3,
					    TEST_INTERVAL - 5,
					    0};
	for (int slot = 0; slot < 10; ++slot) {
		XVT_ASSERT_INT_EQ(g_test_craft[slot].ai_controller.think_timer,
				  timer_after[slot]);
	}
	/* Only the asked craft took the answer's notebook. */
	XVT_ASSERT_INT_EQ(g_test_craft[2].ai_notebook[0], ECHO_MARK);
	XVT_ASSERT_INT_EQ(g_test_craft[8].ai_notebook[0], ECHO_MARK);
	XVT_ASSERT_INT_EQ(g_test_craft[7].ai_notebook[0], 0);
}

/* The question about one craft: the header with the clock's tick, then each
 * view field from where the engine keeps it. The order code is the flight
 * group's order in the craft's current slot; the pitch is the craft
 * record's, which the steering turns, not the object's. */
static void check_question_bytes(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(3);
	struct object_record *object = &g_test_objects[3];
	struct ai_controller *controller = &craft->ai_controller;
	object->object_type = 0x21;
	object->object_signature = 0x4321;
	object->world_x = -0x123456;
	object->world_y = 0x234567;
	object->world_z = -65536;
	object->yaw = 0x1357;
	object->pitch = 0x0001;
	object->roll = 0x2468;
	g_test_mobiles[3].team = 3;
	g_test_mobiles[3].speed = 0x0190;
	g_mission_flight_groups[TEST_GROUP].fg.orders[1].order = 9;
	controller->current_order_slot = 1;
	controller->activity = AI_ACTIVITY_WAITING;
	controller->think_interval = 472;
	controller->saved_rand_seed = -2;
	craft->ai_skill = 0xC000;
	craft->throttle_speed = 0x8000;
	craft->pitch = 0x4100;
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		craft->ai_notebook[byte] = (uint8_t)(0x60 + byte);
	}
	/* Weapons: two cannon groups, a turret in weapon slot 2 of 4, one
	 * launcher of type 60 whose slots 5 and 6 hold 3 and 2, cooling down. */
	craft->cannon_group_count = 2;
	craft->laser_state.projectile_type_id[0] = 0x2A;
	craft->laser_state.projectile_type_id[1] = 0x2B;
	craft->laser_slot_count = 4;
	craft->weapon_slots[2].projectile_type_id = 2;
	craft->weapon_slots[5].projectile_type_id =
		2; /* past the laser slots */
	craft->warhead_launcher_count = 1;
	craft->warhead_slot_type_ids[0] = 60;
	craft->warhead_launcher_cooldown_ticks[0] = 300;
	craft->warhead_launcher_cooldown_ticks[1] = 9; /* no second launcher */
	g_model_defs[craft->model_index].warhead_launcher_first_slot[0] = 5;
	craft->weapon_slots[5].ammo_count = 3;
	craft->weapon_slots[6].ammo_count = 2;
	/* What only the contact shows. */
	object->genus_id = 4;
	int saved_size = g_object_type_table[0x21].max_bounds_extent;
	g_object_type_table[0x21].max_bounds_extent = 18449;
	controller->target_obj_idx = 7;
	craft->last_attacker_obj_idx = 5;
	craft->shield_energy[0] = 500;
	craft->shield_energy[1] = -1;
	craft->hull_damage = 40;
	craft->hull_max = 300;
	craft->working_subsystems = 1;
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *q = g_fake.question[0];
	XVT_ASSERT_INT_EQ(g_fake.question_length[0],
			  ai_wire_question_bytes(1, 1, 0));
	XVT_ASSERT_INT_EQ(get_u32(q), ai_wire_question_bytes(1, 1, 0) - 4);
	XVT_ASSERT_INT_EQ(get_u32(q + 4), AI_WIRE_MAGIC);
	XVT_ASSERT_INT_EQ(get_u16(q + 8), AI_WIRE_VERSION);
	XVT_ASSERT_INT_EQ(get_u16(q + 10), AI_WIRE_KIND_QUESTION);
	XVT_ASSERT_INT_EQ(get_u32(q + 12), TEST_TICK);
	XVT_ASSERT_INT_EQ(get_u16(q + 16), 1);
	XVT_ASSERT_INT_EQ(get_u16(q + 18), 0);
	const uint8_t *view = q + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(view), 3);
	XVT_ASSERT_INT_EQ(get_u16(view + 2), 0x4321);
	XVT_ASSERT_INT_EQ(view[4], 0x21);
	XVT_ASSERT_INT_EQ(view[5], TEST_GROUP);
	XVT_ASSERT_INT_EQ(view[6], 9);
	XVT_ASSERT_INT_EQ(view[7], 1);
	XVT_ASSERT_INT_EQ(view[8], AI_ACTIVITY_WAITING);
	XVT_ASSERT_INT_EQ(view[9], 3);
	XVT_ASSERT_INT_EQ(get_u16(view + 10), 0xC000);
	XVT_ASSERT_INT_EQ(get_u16(view + 12), 472);
	XVT_ASSERT_INT_EQ(get_u16(view + 14), 0x8000);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + 16), -0x123456);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + 20), 0x234567);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + 24), -65536);
	XVT_ASSERT_INT_EQ(get_u16(view + 28), 0x1357);
	XVT_ASSERT_INT_EQ(get_u16(view + 30), 0x4100);
	XVT_ASSERT_INT_EQ(get_u16(view + 32), 0x2468);
	XVT_ASSERT_INT_EQ(get_u16(view + 34), 0x0190);
	XVT_ASSERT_INT_EQ((int16_t)get_u16(view + 36), -2);
	XVT_ASSERT_INT_EQ(get_u32(view + 38), 0);
	XVT_ASSERT_INT_EQ(get_u16(view + 42), 1u << 2);
	XVT_ASSERT_INT_EQ(view[44], 2);
	XVT_ASSERT_INT_EQ(view[45], 1);
	XVT_ASSERT_INT_EQ(view[46], 0x2A);
	XVT_ASSERT_INT_EQ(view[47], 0x2B);
	XVT_ASSERT_INT_EQ(view[48], 60);
	XVT_ASSERT_INT_EQ(view[49], 0);
	XVT_ASSERT_INT_EQ(view[50], 5);
	XVT_ASSERT_INT_EQ(view[51], 0);
	XVT_ASSERT_INT_EQ(get_u16(view + 52), 300);
	XVT_ASSERT_INT_EQ(get_u16(view + 54), 0);
	/* No contact but itself, so no enemy; it leads its flight. */
	XVT_ASSERT_INT_EQ(get_u32(view + 56), 0);
	XVT_ASSERT_INT_EQ(get_u16(view + 60), 0xFFFF);
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		XVT_ASSERT_INT_EQ(view[VIEW_NOTEBOOK + byte], 0x60 + byte);
	}
	/* The one contact is the craft itself. */
	const uint8_t *head = view + AI_WIRE_CRAFT_VIEW_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(head), 1);
	XVT_ASSERT_INT_EQ(get_u16(head + 2), 0);
	const uint8_t *contact = head + AI_WIRE_CONTACT_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(contact), 3);
	XVT_ASSERT_INT_EQ(get_u16(contact + 2), 0x4321);
	XVT_ASSERT_INT_EQ(contact[4], 0x21);
	XVT_ASSERT_INT_EQ(contact[5], 4);
	XVT_ASSERT_INT_EQ(contact[6], 3);
	XVT_ASSERT_INT_EQ(contact[7], TEST_GROUP);
	XVT_ASSERT_INT_EQ(contact[8], CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(contact[9], 0);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(contact + 10), -0x123456);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(contact + 14), 0x234567);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(contact + 18), -65536);
	XVT_ASSERT_INT_EQ(get_u16(contact + 22), 0x1357);
	XVT_ASSERT_INT_EQ(get_u16(contact + 24), 0x4100);
	XVT_ASSERT_INT_EQ(get_u16(contact + 26), 0x0190);
	XVT_ASSERT_INT_EQ(get_u16(contact + 28), 7);
	XVT_ASSERT_INT_EQ(get_u16(contact + 30), 5);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(contact + 32), 500);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(contact + 36), -1);
	XVT_ASSERT_INT_EQ(get_u32(contact + 40), 40);
	XVT_ASSERT_INT_EQ(get_u32(contact + 44), 300);
	XVT_ASSERT_INT_EQ(contact[48], AI_ACTIVITY_WAITING);
	XVT_ASSERT_INT_EQ(get_u32(contact + 49), 18449);
	XVT_ASSERT_INT_EQ(contact[53], 0);
	XVT_ASSERT_INT_EQ(get_u32(contact + 54), 0);
	/* Never hit by any team, so no shooter. */
	XVT_ASSERT_INT_EQ(get_u16(contact + 58), UINT16_MAX);
	XVT_ASSERT_INT_EQ(AI_WIRE_CONTACT_BYTES, 88);
	g_object_type_table[0x21].max_bounds_extent = saved_size;
	g_model_defs[craft->model_index].warhead_launcher_first_slot[0] = 0;
}

/* The contacts are every craft in the active region's craft slots, empty
 * slots and other families skipped, a player's craft flagged with its
 * player's target; bit n of a craft's order target mask is set when contact
 * n matches its order's targets: here flight group TEST_GROUP + 1 (trigger
 * variable type 1), either of targets 1 and 2. */
static void check_contacts_and_order_targets(void)
{
	fresh_world();
	due_craft(1);
	due_craft(2)->ai_controller.think_timer = 5; /* not due */
	g_test_objects[2].flight_group_idx = TEST_GROUP + 1;
	due_craft(4)->ai_controller.think_timer = 5;
	g_test_objects[4].player_owner_idx = 1;
	g_players[1].current_target_object_idx = 2;
	g_test_craft[4].working_subsystems = 1;
	g_test_objects[5].object_type = 5;
	g_test_mobiles[5].family = 1; /* not a craft's */
	due_craft(6)->ai_controller.think_timer = 5;
	g_test_objects[6].flight_group_idx = TEST_GROUP + 1;
	struct mission_order *order =
		&g_mission_flight_groups[TEST_GROUP].fg.orders[0];
	order->target2_type = 1;
	order->target2 = TEST_GROUP + 1;
	order->target1_or_target2 = 1;
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *q = g_fake.question[0];
	XVT_ASSERT_INT_EQ(g_fake.question_length[0],
			  ai_wire_question_bytes(1, 4, 0));
	const uint8_t *view = q + AI_WIRE_HEAD_BYTES;
	/* Contacts 1, 2, 4, 6: bits 1 and 3 are flight group TEST_GROUP + 1. */
	XVT_ASSERT_INT_EQ(get_u32(view + 38), 0xA);
	const uint8_t *head = view + AI_WIRE_CRAFT_VIEW_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(head), 4);
	const uint8_t *contact = head + AI_WIRE_CONTACT_HEAD_BYTES;
	static const int expected_objects[4] = {1, 2, 4, 6};
	for (int index = 0; index < 4; ++index) {
		XVT_ASSERT_INT_EQ(
			get_u16(contact + index * AI_WIRE_CONTACT_BYTES),
			expected_objects[index]);
	}
	const uint8_t *player = contact + 2 * AI_WIRE_CONTACT_BYTES;
	/* Order 0, Hold Steady, parks it. */
	XVT_ASSERT_INT_EQ(player[9], AI_WIRE_CONTACT_PLAYER | 0x10);
	XVT_ASSERT_INT_EQ(get_u16(player + 28), 2);
	/* With "and", flight group TEST_GROUP + 1 must also match target 1's
	 * none (type 0), so nothing matches. */
	fresh_world();
	due_craft(1);
	due_craft(2)->ai_controller.think_timer = 5;
	g_test_objects[2].flight_group_idx = TEST_GROUP + 1;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].target2_type = 1;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].target2 =
		TEST_GROUP + 1;
	g_fake.echo = 1;
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(get_u32(g_fake.question[0] + AI_WIRE_HEAD_BYTES + 38),
			  0);
}

/* Bit n of a craft's enemy mask is set when contact n is on a team that the
 * craft's team's record does not mark allied. The asked craft, slot 3, is on
 * team 1 and follows slot 0; slot 1 (team 2) is hostile, slot 2 (team 3)
 * allied, slots 0 and 3 its own team, slot 4 (team 10) past the mission's ten
 * teams. A craft on team 10 has no enemy. */
static void check_enemy_mask_and_leader(void)
{
	static const uint8_t teams[5] = {1, 2, 3, 1, 10};
	for (int round = 0; round < 2; ++round) {
		fresh_world();
		memset(g_mission_teams, 0, sizeof g_mission_teams);
		g_mission_teams[1].allies[1] = 1;
		g_mission_teams[1].allies[3] = 1;
		for (int slot = 0; slot < 5; ++slot) {
			due_craft(slot)->ai_controller.think_timer = 5;
			g_test_mobiles[slot].team = teams[slot];
		}
		g_test_craft[3].ai_controller.think_timer = 0;
		g_test_craft[3].leader_obj_idx = 0;
		if (round == 1) {
			g_test_mobiles[3].team = 10;
		}
		g_fake.echo = 1;

		ai_brain_think_due_craft();

		XVT_ASSERT_INT_EQ(g_fake.calls, 1);
		const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
		XVT_ASSERT_INT_EQ(get_u16(view), 3);
		XVT_ASSERT_INT_EQ(get_u32(view + 56), round == 0 ? 0x2u : 0u);
		XVT_ASSERT_INT_EQ(get_u16(view + 60), 0);
	}
	memset(g_mission_teams, 0, sizeof g_mission_teams);
}

/* A flight whose leader is lost gets a new one before anyone is asked: slot
 * 0 led slots 1, 2, 3, 4 and 7 (readouts 4, 3, 1, 2 and 2) and is exploding.
 * Slot 3, the lowest readout, is breaking up and keeps its leader; slots 4 and
 * 7 tie, so slot 4 leads and 1, 2 and 7 follow it, the asked slot 1 says so.
 * Slot 5, of another group, follows slot 6, which lives, and is left alone. */
static void check_lost_leader_named(void)
{
	static const int readouts[8] = {1, 4, 3, 1, 2, 7, 1, 2};
	fresh_world();
	for (int slot = 0; slot < 8; ++slot) {
		due_craft(slot)->ai_controller.think_timer = 5;
		g_test_craft[slot].craft_index_in_group = readouts[slot];
		g_test_craft[slot].leader_obj_idx = 0;
	}
	g_test_craft[0].leader_obj_idx = UINT8_MAX;
	g_test_craft[0].object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	g_test_craft[3].object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	g_test_objects[5].flight_group_idx = TEST_GROUP + 1;
	g_test_objects[6].flight_group_idx = TEST_GROUP + 1;
	g_test_craft[5].leader_obj_idx = 6;
	g_test_craft[6].leader_obj_idx = UINT8_MAX;
	g_test_craft[1].ai_controller.think_timer = 0;
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	static const int expected[8] = {UINT8_MAX, 4, 4,	 0,
					UINT8_MAX, 6, UINT8_MAX, 4};
	for (int slot = 0; slot < 8; ++slot) {
		XVT_ASSERT_INT_EQ(g_test_craft[slot].leader_obj_idx,
				  expected[slot]);
	}
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	XVT_ASSERT_INT_EQ(get_u16(g_fake.question[0] + AI_WIRE_HEAD_BYTES + 60),
			  4);
}

/* An answer with every action: the notebook, the three steering goals with
 * their switches on at full rates, the throttle, the activity and the think
 * interval; the timer grows by the new interval. The pitch below its goal
 * turns upward (pitch_state 2), never through a loop. */
static void check_answer_applied(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(4);
	craft->pitch = 0x4000;
	craft->ai_flight.pitch_through_loop = 1;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	/* Two cannon groups, a turret in weapon slot 1, and a craft in slot 6
	 * to aim at. */
	craft->cannon_group_count = 2;
	craft->laser_slot_count = 2;
	craft->weapon_slots[1].projectile_type_id = 2;
	craft->turret_target_states[1].target_obj_idx = UINT16_MAX;
	due_craft(6)->ai_controller.think_timer = 5;
	at = craft_answer(at, 4, 0x80, 7);
	*at++ = AI_WIRE_ACTION_TARGET;
	at = put_u16(at, 6);
	*at++ = AI_WIRE_ACTION_LASERS;
	*at++ = 3;
	*at++ = 5;
	*at++ = 2;
	*at++ = 1;
	*at++ = AI_WIRE_ACTION_TURRETS;
	*at++ = 1;
	*at++ = 1;
	at = put_u16(at, 6);
	*at++ = 0;
	*at++ = AI_WIRE_ACTION_STEER;
	at = put_u16(at, 0x1111);
	at = put_u16(at, 0x5000);
	at = put_u16(at, 0x2222);
	*at++ = AI_WIRE_ACTION_THROTTLE;
	at = put_u16(at, 0x8000);
	*at++ = AI_WIRE_ACTION_ACTIVITY;
	*at++ = AI_ACTIVITY_WAITING;
	*at++ = AI_WIRE_ACTION_THINK_INTERVAL;
	at = put_u16(at, 29);
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		XVT_ASSERT_INT_EQ(craft->ai_notebook[byte], 0x80 + byte);
	}
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, 6);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_signature, 0x106);
	XVT_ASSERT_INT_EQ(craft->ai_controller.has_live_target, 1);
	XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[0], 3);
	XVT_ASSERT_INT_EQ(craft->laser_state.burst_remaining[0], 5);
	XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[1], 2);
	XVT_ASSERT_INT_EQ(craft->laser_state.burst_remaining[1], 1);
	XVT_ASSERT_INT_EQ(craft->turret_target_states[1].target_obj_idx, 6);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_xy_angle, 0x1111);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_step, -1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_z_angle, 0x5000);
	XVT_ASSERT_INT_EQ(craft->ai_flight.pitch_state, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.pitch_through_loop, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.pitch_step_scale, UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_roll, 0x2222);
	XVT_ASSERT_INT_EQ(craft->ai_flight.roll_state, 1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.roll_step, UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x8000);
	XVT_ASSERT_INT_EQ(craft->ai_controller.activity, AI_ACTIVITY_WAITING);
	XVT_ASSERT_INT_EQ(craft->ai_controller.think_interval, 29);
	XVT_ASSERT_INT_EQ(craft->ai_controller.think_timer, 29);
}

/* Answers the one due craft in slot 1, pitched at pitch, with a steer to
 * pitch goal and nothing else; returns its pitch_state after. */
static int pitch_state_after_steer(unsigned int pitch, unsigned int goal)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	craft->pitch = (uint16_t)pitch;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_STEER;
	at = put_u16(at, 0);
	at = put_u16(at, goal);
	at = put_u16(at, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_z_angle, goal);
	return craft->ai_flight.pitch_state;
}

/* A pitch above its goal turns downward (pitch_state 1), one on it is done
 * (3), one below turns upward (2). */
static void check_pitch_direction(void)
{
	XVT_ASSERT_INT_EQ(pitch_state_after_steer(0x5000, 0x4000), 1);
	XVT_ASSERT_INT_EQ(pitch_state_after_steer(0x4000, 0x4000), 3);
	XVT_ASSERT_INT_EQ(pitch_state_after_steer(0x3000, 0x4000), 2);
}

/* An answer with no actions changes the notebook only: the steering, the
 * throttle, the activity and the interval stay, and the timer grows by the
 * interval the craft had. */
static void check_notebook_only_answer(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(2);
	craft->ai_controller.target_xy_angle = 0x0777;
	craft->ai_controller.target_z_angle = 0x4000;
	craft->ai_controller.target_roll = 0x0888;
	craft->ai_flight.turn_state = 3;
	craft->ai_flight.pitch_state = 3;
	craft->ai_flight.roll_state = 4;
	craft->throttle_speed = 0x1234;
	craft->ai_controller.activity = AI_ACTIVITY_IN_FORMATION;
	struct craft_data before;
	memcpy(&before, craft, sizeof before);
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 2, 0x10, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		XVT_ASSERT_INT_EQ(craft->ai_notebook[byte], 0x10 + byte);
	}
	memcpy(before.ai_notebook, craft->ai_notebook,
	       sizeof before.ai_notebook);
	before.ai_controller.think_timer = TEST_INTERVAL;
	XVT_ASSERT_TRUE(memcmp(&before, craft, sizeof before) == 0);
}

/* An answer the reader refuses is applied to no craft, even one whose own
 * part was good: here the second craft's activity is past the last one. The
 * timers still grow, so the craft are asked again an interval later. */
static void check_refused_answer_applies_nothing(void)
{
	fresh_world();
	struct craft_data *first = due_craft(0);
	struct craft_data *second = due_craft(5);
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 2);
	at = craft_answer(at, 0, 0x20, 1);
	*at++ = AI_WIRE_ACTION_THROTTLE;
	at = put_u16(at, 0x9999);
	at = craft_answer(at, 5, 0x30, 1);
	*at++ = AI_WIRE_ACTION_ACTIVITY;
	*at++ = AI_ACTIVITY_COUNT;
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	XVT_ASSERT_INT_EQ(first->throttle_speed, 0);
	XVT_ASSERT_INT_EQ(second->ai_controller.activity, AI_ACTIVITY_NONE);
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		XVT_ASSERT_INT_EQ(first->ai_notebook[byte], 0);
		XVT_ASSERT_INT_EQ(second->ai_notebook[byte], 0);
	}
	XVT_ASSERT_INT_EQ(first->ai_controller.think_timer, TEST_INTERVAL);
	XVT_ASSERT_INT_EQ(second->ai_controller.think_timer, TEST_INTERVAL);
}

/* More craft due than one question holds: the first 32 in slot order go in
 * one question, the rest in a second, and every craft takes its answer. */
static void check_more_craft_than_one_question(void)
{
	fresh_world();
	g_active_region_craft_object_slot_end = SLOT_COUNT;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		due_craft(slot);
	}
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 2);
	XVT_ASSERT_INT_EQ(get_u16(g_fake.question[0] + 16), QUESTION_CRAFT);
	XVT_ASSERT_INT_EQ(get_u16(g_fake.question[1] + 16),
			  SLOT_COUNT - QUESTION_CRAFT);
	XVT_ASSERT_INT_EQ(asked_object(0, 0), 0);
	XVT_ASSERT_INT_EQ(asked_object(0, QUESTION_CRAFT - 1),
			  QUESTION_CRAFT - 1);
	XVT_ASSERT_INT_EQ(asked_object(1, 0), QUESTION_CRAFT);
	XVT_ASSERT_INT_EQ(asked_object(1, SLOT_COUNT - QUESTION_CRAFT - 1),
			  SLOT_COUNT - 1);
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		XVT_ASSERT_INT_EQ(g_test_craft[slot].ai_notebook[0], ECHO_MARK);
		XVT_ASSERT_INT_EQ(g_test_craft[slot].ai_controller.think_timer,
				  TEST_INTERVAL);
	}
}

/* Answers the one due craft in slot 1, whose launcher 0 has slots 2 and 3
 * with no warhead type (so nothing launches), with a target of none and a
 * warhead action; returns its launcher flags after. With no warhead in either
 * slot the wire refuses the whole answer: the target stays and no cooldown
 * is spent. */
static int launcher_flags_after(int paired, int inhibit, int first_ammo,
				int second_ammo)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	craft->ai_controller.target_obj_idx = 1;
	craft->ai_controller.has_live_target = 1;
	craft->warhead_launcher_count = 1;
	craft->warhead_launcher_flags[0] = 0x55;
	craft->weapon_fire_inhibit_timer = (int16_t)inhibit;
	g_model_defs[craft->model_index].warhead_launcher_first_slot[0] = 2;
	craft->weapon_slots[2].ammo_count = (uint8_t)first_ammo;
	craft->weapon_slots[3].ammo_count = (uint8_t)second_ammo;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 2);
	*at++ = AI_WIRE_ACTION_TARGET;
	at = put_u16(at, UINT16_MAX);
	*at++ = AI_WIRE_ACTION_WARHEAD;
	*at++ = 0;
	*at++ = (uint8_t)paired;
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	int empty = first_ammo + second_ammo == 0;
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx,
			  empty ? 1 : UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_signature, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.has_live_target, empty);
	/* laser_firewarheadsystem adds 472 ticks whether or not it fired. */
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_cooldown_ticks[0],
			  inhibit == 0 && !empty ? 472 : 0);
	g_model_defs[craft->model_index].warhead_launcher_first_slot[0] = 0;
	return (uint8_t)craft->warhead_launcher_flags[0];
}

/* The warhead action fires through laser_firewarheadsystem with the 1997
 * AI's launcher flags: both slots when paired, else the first, or the second
 * when it holds more; not at all while the craft's weapons are held, nor
 * from a launcher with no warhead left, whose answer is refused. */
static void check_warhead_action(void)
{
	XVT_ASSERT_INT_EQ(launcher_flags_after(1, 0, 1, 1), 3);
	XVT_ASSERT_INT_EQ(launcher_flags_after(0, 0, 1, 1), 1);
	XVT_ASSERT_INT_EQ(launcher_flags_after(0, 0, 1, 2), 0x81);
	XVT_ASSERT_INT_EQ(launcher_flags_after(0, 5, 1, 1), 0x55);
	XVT_ASSERT_INT_EQ(launcher_flags_after(1, 0, 0, 0), 0x55);
}

/* Answers the one due craft in slot 1, of the object kind and activity given
 * and at speed 900, with an arrival, after an activity action unless
 * new_activity is -1; returns its object kind after and sets *speed. */
static int kind_after_arrival(int kind, int activity, int new_activity,
			      int arriving, unsigned int sent,
			      unsigned int *speed)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	craft->object_kind = (craft_object_kind)kind;
	craft->ai_controller.activity = (uint8_t)activity;
	g_test_mobiles[1].speed = 900;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, new_activity == -1 ? 1 : 2);
	if (new_activity != -1) {
		*at++ = AI_WIRE_ACTION_ACTIVITY;
		*at++ = (uint8_t)new_activity;
	}
	*at++ = AI_WIRE_ACTION_ARRIVAL;
	*at++ = (uint8_t)arriving;
	at = put_u16(at, sent);
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	*speed = g_test_mobiles[1].speed;
	return craft->object_kind;
}

/* While a craft arrives from hyperspace, the arrival marks it, active or
 * arriving, arriving or active again, and sets its speed at once, even when
 * the same answer first ends the arriving activity; a disabled craft, or one
 * entering hyperspace, keeps its kind and speed. Outside an arrival, out of a
 * hangar too, it changes nothing: no speed is set at the game's levels. */
static void check_arrival_action(void)
{
	const int jumping = AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE;
	unsigned int speed = 0;
	XVT_ASSERT_INT_EQ(kind_after_arrival(CRAFT_OBJECT_KIND_ACTIVE, jumping,
					     -1, 1, 3600, &speed),
			  CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE);
	XVT_ASSERT_INT_EQ(speed, 3600);
	XVT_ASSERT_INT_EQ(
		kind_after_arrival(CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE,
				   jumping, -1, 1, 1800, &speed),
		CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE);
	XVT_ASSERT_INT_EQ(speed, 1800);
	XVT_ASSERT_INT_EQ(
		kind_after_arrival(CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE,
				   jumping, AI_ACTIVITY_NONE, 0, 250, &speed),
		CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(speed, 250);
	XVT_ASSERT_INT_EQ(kind_after_arrival(CRAFT_OBJECT_KIND_DISABLED,
					     jumping, -1, 1, 3600, &speed),
			  CRAFT_OBJECT_KIND_DISABLED);
	XVT_ASSERT_INT_EQ(speed, 900);
	XVT_ASSERT_INT_EQ(
		kind_after_arrival(CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE,
				   jumping, -1, 0, 0, &speed),
		CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE);
	XVT_ASSERT_INT_EQ(speed, 900);
	XVT_ASSERT_INT_EQ(kind_after_arrival(CRAFT_OBJECT_KIND_ACTIVE,
					     AI_ACTIVITY_NONE, -1, 1, 3600,
					     &speed),
			  CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(speed, 900);
	XVT_ASSERT_INT_EQ(kind_after_arrival(CRAFT_OBJECT_KIND_ACTIVE,
					     AI_ACTIVITY_LEAVING_HANGAR, -1, 0,
					     3600, &speed),
			  CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(speed, 900);
}

/* The view carries the craft's countermeasures and the homing shots aimed
 * at it, from ai_countermeasures_view_craft, and the countermeasure
 * action launches through ai_countermeasures_launch: here chaff, for a
 * missile in shot slot 31. */
static void check_countermeasure_action(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	struct warhead_guidance_state guidance = {0};
	craft->cm_type_id = COUNTERMEASURE_TYPE_CHAFF;
	craft->cm_ammo_count = 2;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES;
	g_projectile_object_slot_start = 30;
	g_projectile_object_slot_end = 32;
	g_test_objects[31].object_type = WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	g_test_mobiles[31].p_warhead_guidance = &guidance;
	g_test_mobiles[31].family = 1;
	guidance.target_obj_idx = 1;
	guidance.homing_tier = 2;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_COUNTERMEASURE;
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(view[VIEW_COUNTERMEASURES],
			  AI_WIRE_COUNTERMEASURE_CHAFF);
	XVT_ASSERT_INT_EQ(view[VIEW_COUNTERMEASURES + 1], 2);
	XVT_ASSERT_INT_EQ(view[VIEW_COUNTERMEASURES + 2], 1);
	XVT_ASSERT_INT_EQ(view[VIEW_COUNTERMEASURES + 3], 1);
	XVT_ASSERT_INT_EQ(get_u16(view + VIEW_FIRST_THREAT), 31);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 10);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 1);
	g_test_mobiles[31].p_warhead_guidance = NULL;
}

/* The view carries the craft's group's waypoints, from
 * ai_waypoints_view_craft: here mission point 4 on, point 5 off. */
static void check_view_waypoints(void)
{
	fresh_world();
	due_craft(1);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.mission_point_enabled[4] = 1;
	group->fg.mission_point_x[4] = 10;
	group->fg.mission_point_y[4] = 20;
	group->fg.mission_point_z[4] = 30;
	group->fg.mission_point_x[5] = 40;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(view[VIEW_WAYPOINTS], 0x01);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_WAYPOINTS + 1), 2560);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_WAYPOINTS + 5), 0);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_WAYPOINTS + 33), (uint32_t)-5120);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_WAYPOINTS + 65), 7680);
}

/* The view carries a player's commands to the craft, from
 * ai_commands_view_craft: here the wait mark, the evade command and an
 * avoided target; the evaded action clears the evade command through
 * ai_commands_evaded and leaves the rest. */
static void check_commands_and_evaded(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	craft->ai_controller.wait_command = 1;
	craft->ai_controller.candidate_target_idx = AI_TARGET_ABORT;
	craft->player_command_avoid_target_obj_idx = 2;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_EVADED;
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(view[VIEW_COMMANDS],
			  AI_WIRE_COMMAND_WAIT | AI_WIRE_COMMAND_EVADE);
	XVT_ASSERT_INT_EQ(get_u16(view + VIEW_COMMANDS + 1), UINT16_MAX);
	XVT_ASSERT_INT_EQ(get_u16(view + VIEW_COMMANDS + 3), 2);
	XVT_ASSERT_INT_EQ(craft->ai_controller.candidate_target_idx,
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->ai_controller.wait_command, 1);
	XVT_ASSERT_INT_EQ(craft->player_command_avoid_target_obj_idx, 2);
}

/* The view carries a follower's formation place, from
 * ai_formation_view_craft: craft 1 follows craft 2 in formation 0's place
 * 1 at separation 4, 499 out along the leader's side axis. Its answer ends
 * its leaving-hangar mark, so the group takes the mission's formation and
 * spacing through ai_formation_activity_changed. */
static void check_view_place_and_hangar_exit(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	craft->leader_obj_idx = 2;
	craft->craft_ordinal = 1;
	craft->ai_controller.activity = AI_ACTIVITY_LEAVING_HANGAR;
	g_test_objects[2].object_type = 5;
	g_test_objects[2].flight_group_idx = TEST_GROUP;
	g_test_objects[2].world_x = 1000;
	g_test_mobiles[2].orient_matrix_dirty = 1;
	g_test_craft[2].ai_controller.think_timer = TEST_INTERVAL;
	g_test_craft[2].leader_obj_idx = UINT8_MAX;
	g_test_craft[2].ai_flight.separation = 4;
	g_mission_flight_groups[TEST_GROUP].fg.formation = 3;
	g_mission_flight_groups[TEST_GROUP].fg.formation_spacing = 2;
	struct model_def kept = g_model_defs[craft->model_index];
	g_model_defs[craft->model_index].bound_size_x = 100;
	g_model_defs[craft->model_index].bound_size_y = 0;
	g_model_defs[craft->model_index].bound_size_z = 0;
	g_model_defs[craft->model_index].bound_size_shift = 0;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_ACTIVITY;
	*at++ = AI_ACTIVITY_NONE;
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	g_model_defs[craft->model_index] = kept;
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_PLACE), 1499);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_PLACE + 4), 0);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_PLACE + 8), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.activity, AI_ACTIVITY_NONE);
	for (int slot = 1; slot <= 2; ++slot) {
		XVT_ASSERT_INT_EQ(g_test_craft[slot].ai_flight.formation_type,
				  3);
		XVT_ASSERT_INT_EQ(g_test_craft[slot].ai_flight.separation, 2);
	}
}

/* The level the stat tables read: the group's level up to 5, the Jedi, and 5
 * for the Sith ranks 6 and 7 and for anything above. */
static void check_stat_level(void)
{
	for (int level = 0; level < 256; ++level) {
		XVT_ASSERT_INT_EQ(ai_brain_stat_level((uint8_t)level),
				  level > 5 ? 5 : level);
	}
	XVT_ASSERT_INT_EQ(AI_STAT_LEVEL_MAX, 5);
}

/* The view carries the craft's lock and skill level, from
 * ai_warheads_view_craft, and each contact the homing warheads at it, from
 * ai_warheads_view_contact: here a lock of 500 ticks no step has counted
 * yet, its AI rank 5, and a missile in shot slot 31 homing on it. */
static void check_view_lock_and_warheads_coming(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	struct warhead_guidance_state guidance = {0};
	craft->warhead_lock_ticks = 500;
	craft->ai_rank = 5;
	g_projectile_object_slot_start = 30;
	g_projectile_object_slot_end = 32;
	g_test_objects[31].object_type = WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	g_test_mobiles[31].p_warhead_guidance = &guidance;
	g_test_mobiles[31].family = 1;
	g_test_mobiles[31].damage_amount = 3225;
	guidance.target_obj_idx = 1;
	guidance.homing_tier = 2;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	g_test_mobiles[31].p_warhead_guidance = NULL;
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(view + VIEW_LOCK), 500);
	XVT_ASSERT_INT_EQ(view[VIEW_LOCK + 4], AI_WIRE_LOCK_NONE);
	XVT_ASSERT_INT_EQ(view[VIEW_LOCK + 5], 5);
	const uint8_t *contact =
		view + AI_WIRE_CRAFT_VIEW_BYTES + AI_WIRE_CONTACT_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(contact), 1);
	XVT_ASSERT_INT_EQ(contact[53], 1);
	XVT_ASSERT_INT_EQ(get_u32(contact + 54), 3225);
}

/* The contacts' flags and shooter, from the craft records; every craft is
 * on the group's order 0, Hold Steady, so every contact is also parked
 * (0x10). The active
 * region's craft slots are 1 to 11. Slot 2 has no working subsystem and was
 * hit by a team, its last shooter slot 1, the first craft slot; slot 3 was
 * hit, its shooter slot 11, the last; slot 4 was hit by team 9 alone, its
 * shooter slot 12, the first past the region; slot 5 by team 0, its shooter
 * slot 0, the last before it; slot 7 was hit, its shooter slot 6 is another
 * family; slot 8 was hit, its shooter slot 10 is empty; slot 1 was never
 * hit though its shooter index names craft slot 2. Slot 6 is no contact. */
static void check_view_contact_flags_and_shooter(void)
{
	static const struct {
		uint8_t flags;
		uint16_t hit_by;
	} expected[] = {
		{0x10, UINT16_MAX}, {0x16, 1},		{0x14, 11},
		{0x14, UINT16_MAX}, {0x14, UINT16_MAX}, {0x14, UINT16_MAX},
		{0x14, UINT16_MAX}, {0x10, UINT16_MAX}, {0x10, UINT16_MAX},
	};
	fresh_world();
	g_active_region_object_slot_start = 1;
	g_active_region_craft_object_slot_end = 12;
	for (int slot = 0; slot <= 12; ++slot) {
		g_test_craft[slot].working_subsystems = 1;
		g_test_objects[slot].object_type = slot == 10 ? 0 : 5;
		g_test_objects[slot].flight_group_idx = TEST_GROUP;
		g_test_craft[slot].leader_obj_idx = UINT8_MAX;
		g_test_craft[slot].ai_controller.think_interval = TEST_INTERVAL;
		g_test_craft[slot].ai_controller.think_timer =
			slot == 1 ? 0 : 5;
	}
	g_test_mobiles[6].family = 1;
	g_test_craft[2].working_subsystems = 0;
	g_test_craft[1].ai_flight.threat_obj_idx = 2;
	g_test_craft[2].attacked_by_team[1] = 1;
	g_test_craft[2].ai_flight.threat_obj_idx = 1;
	g_test_craft[3].attacked_by_team[1] = 1;
	g_test_craft[3].ai_flight.threat_obj_idx = 11;
	g_test_craft[4].attacked_by_team[9] = 1;
	g_test_craft[4].ai_flight.threat_obj_idx = 12;
	g_test_craft[5].attacked_by_team[0] = 3;
	g_test_craft[5].ai_flight.threat_obj_idx = 0;
	g_test_craft[7].attacked_by_team[2] = 1;
	g_test_craft[7].ai_flight.threat_obj_idx = 6;
	g_test_craft[8].attacked_by_team[3] = 1;
	g_test_craft[8].ai_flight.threat_obj_idx = 10;
	g_test_craft[9].ai_flight.threat_obj_idx = 1;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *head = g_fake.question[0] + AI_WIRE_HEAD_BYTES +
			      AI_WIRE_CRAFT_VIEW_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(head), 9);
	for (int contact = 0; contact < 9; ++contact) {
		const uint8_t *found = head + AI_WIRE_CONTACT_HEAD_BYTES +
				       contact * AI_WIRE_CONTACT_BYTES;
		XVT_ASSERT_INT_EQ(found[9], expected[contact].flags);
		XVT_ASSERT_INT_EQ(get_u16(found + 58),
				  expected[contact].hit_by);
	}
}

/* A craft with the maneuver timer 1,180 ticks left reports 1,180; one whose
 * timer ran below 0 reports 0. */
static uint32_t timer_in_view(int timer)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	craft->ai_controller.maneuver_timer = timer;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	return get_u32(g_fake.question[0] + AI_WIRE_HEAD_BYTES + VIEW_TIMER);
}

/* The maneuver timer in the view, and the action that sets it. */
static void check_maneuver_timer(void)
{
	XVT_ASSERT_INT_EQ(timer_in_view(1180), 1180);
	XVT_ASSERT_INT_EQ(timer_in_view(0), 0);
	XVT_ASSERT_INT_EQ(timer_in_view(-1), 0);
	XVT_ASSERT_INT_EQ(timer_in_view(INT_MIN), 0);
	XVT_ASSERT_INT_EQ(timer_in_view(INT_MAX), (uint32_t)INT_MAX);

	fresh_world();
	struct craft_data *craft = due_craft(1);
	craft->ai_controller.maneuver_timer = 33;
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_MANEUVER_TIMER;
	at = put_u32(at, 700);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 700);

	/* No action leaves the timer alone; 0 sets it to 0; the most sets it. */
	fresh_world();
	craft = due_craft(1);
	craft->ai_controller.maneuver_timer = 33;
	at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 33);
	fresh_world();
	craft = due_craft(1);
	craft->ai_controller.maneuver_timer = 33;
	at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_MANEUVER_TIMER;
	at = put_u32(at, 0);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 0);
	fresh_world();
	craft = due_craft(1);
	at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_MANEUVER_TIMER;
	at = put_u32(at, 0x7FFFFFFF);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, INT_MAX);
}

/* The ion switch of a turret sets its weapon slot's ammo_count, which the
 * laser code reads as an ion shot: 1 sets it, 0 clears it, and no other slot
 * changes. */
static void check_turret_ion(void)
{
	static const uint8_t ion[] = {1, 0};
	for (int pass = 0; pass < 2; ++pass) {
		fresh_world();
		struct craft_data *craft = due_craft(4);
		craft->laser_slot_count = 3;
		craft->weapon_slots[1].projectile_type_id = 2;
		craft->weapon_slots[2].projectile_type_id = 2;
		craft->weapon_slots[1].ammo_count = pass == 0 ? 0 : 1;
		craft->weapon_slots[2].ammo_count = 1;
		craft->weapon_slots[0].ammo_count = 5;
		uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
		at = craft_answer(at, 4, 0, 1);
		*at++ = AI_WIRE_ACTION_TURRETS;
		*at++ = 1;
		*at++ = 1;
		at = put_u16(at, UINT16_MAX);
		*at++ = ion[pass];
		g_fake.answer_length = finish_answer(g_fake.answer, at);
		ai_brain_think_due_craft();
		XVT_ASSERT_INT_EQ(craft->weapon_slots[1].ammo_count, ion[pass]);
		XVT_ASSERT_INT_EQ(craft->weapon_slots[2].ammo_count, 1);
		XVT_ASSERT_INT_EQ(craft->weapon_slots[0].ammo_count, 5);
	}
}

/* A computer craft attacks its AI target in any activity but the three of
 * flying home, where its target is its mothership; with no target it attacks
 * nothing. */
static void check_attack_target(void)
{
	static const struct {
		uint8_t activity;
		uint16_t attacks;
	} cases[] = {
		{AI_ACTIVITY_NONE, 7},
		{AI_ACTIVITY_IN_FORMATION, 7},
		{AI_ACTIVITY_LEAVING_HANGAR, 7},
		{AI_ACTIVITY_GOING_HOME, UINT16_MAX},
		{AI_ACTIVITY_GOING_HOME_EVADING, UINT16_MAX},
		{AI_ACTIVITY_ENTERING_HANGAR, UINT16_MAX},
	};
	static struct craft_data craft;
	memset(&craft, 0, sizeof craft);
	craft.ai_controller.target_obj_idx = 7;
	for (size_t k = 0; k < sizeof cases / sizeof cases[0]; ++k) {
		craft.ai_controller.activity = cases[k].activity;
		XVT_ASSERT_INT_EQ(ai_brain_attack_target(&craft),
				  cases[k].attacks);
	}
	craft.ai_controller.activity = AI_ACTIVITY_NONE;
	craft.ai_controller.target_obj_idx = UINT16_MAX;
	XVT_ASSERT_INT_EQ(ai_brain_attack_target(&craft), UINT16_MAX);
}

/* The statics after the contacts: each static slot whose object type carries
 * behavior flag 2, in slot order, in the contact form with the static flag
 * and the group's team; an empty slot or a flagless type is left out. The
 * craft's static target mask has a bit for each static its order's targets
 * match, and an answer may aim at a static. Static slots have no mobile
 * object. */
static void check_statics(void)
{
	enum {
		FIRST_STATIC = 30,
		STATIC_SLOTS = 5,
		FLAGGED_TYPE = 21,
		FLAGLESS_TYPE = 22,
		FLAGGED_GROUP = 3,
		OTHER_GROUP = 4,
	};
	fresh_world();
	g_region_main_object_slot_end = FIRST_STATIC;
	g_region_static_object_slot_count = STATIC_SLOTS;
	for (int slot = FIRST_STATIC; slot < FIRST_STATIC + STATIC_SLOTS;
	     ++slot) {
		g_test_objects[slot].mobj = NULL;
	}
	g_object_type_table[FLAGGED_TYPE].behavior_flags = 2;
	g_object_type_table[FLAGGED_TYPE].max_bounds_extent = 777;
	g_object_type_table[FLAGGED_TYPE].genus_id = 9;
	g_object_type_table[FLAGLESS_TYPE].behavior_flags = 1;
	g_mission_flight_groups[FLAGGED_GROUP].fg.team = 6;
	g_mission_flight_groups[OTHER_GROUP].fg.team = 8;
	struct mission_order *order =
		&g_mission_flight_groups[TEST_GROUP].fg.orders[0];
	order->target1_type = 1;
	order->target1 = FLAGGED_GROUP;
	order->target1_or_target2 = 1;
	/* Slot 30 is listed and matches; 31 has a flagless type; 32 is empty;
	 * 33 is listed but of another group, and disabled; 34 is listed and of
	 * the matching group too. */
	struct object_record *object = &g_test_objects[FIRST_STATIC];
	object[0] = (struct object_record){.object_signature = 0x77,
					   .genus_id = 0,
					   .object_type = FLAGGED_TYPE,
					   .world_x = -100,
					   .world_y = 200,
					   .world_z = -300,
					   .yaw = 0x1111,
					   .pitch = 0x2222,
					   .flight_group_idx = FLAGGED_GROUP,
					   .type_specific_word = 1023};
	object[1] = (struct object_record){.object_type = FLAGLESS_TYPE,
					   .flight_group_idx = FLAGGED_GROUP,
					   .type_specific_word = 1023};
	object[3] = (struct object_record){.object_signature = 0x78,
					   .object_type = FLAGGED_TYPE,
					   .flight_group_idx = OTHER_GROUP,
					   .type_specific_word = 0};
	object[4] = (struct object_record){.object_signature = 0x79,
					   .object_type = FLAGGED_TYPE,
					   .flight_group_idx = FLAGGED_GROUP,
					   .type_specific_word = 1023};
	due_craft(1);
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *q = g_fake.question[0];
	XVT_ASSERT_INT_EQ(g_fake.question_length[0],
			  ai_wire_question_bytes(1, 1, 3));
	const uint8_t *view = q + AI_WIRE_HEAD_BYTES;
	/* Statics 0 and 2, the ones of the order's group, in the mask. */
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_STATIC_MASK), 0x5);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_STATIC_MASK + 4), 0);
	const uint8_t *head = view + AI_WIRE_CRAFT_VIEW_BYTES +
			      AI_WIRE_CONTACT_HEAD_BYTES +
			      AI_WIRE_CONTACT_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(head), 3);
	XVT_ASSERT_INT_EQ(get_u16(head + 2), 0);
	const uint8_t *first = head + AI_WIRE_CONTACT_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(first), FIRST_STATIC);
	XVT_ASSERT_INT_EQ(get_u16(first + 2), 0x77);
	XVT_ASSERT_INT_EQ(first[4], FLAGGED_TYPE);
	XVT_ASSERT_INT_EQ(first[5], 9);
	XVT_ASSERT_INT_EQ(first[6], 6);
	XVT_ASSERT_INT_EQ(first[7], FLAGGED_GROUP);
	XVT_ASSERT_INT_EQ(first[8], 0);
	XVT_ASSERT_INT_EQ(first[9], 0x08);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(first + 10), -100);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(first + 14), 200);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(first + 18), -300);
	XVT_ASSERT_INT_EQ(get_u16(first + 22), 0x1111);
	XVT_ASSERT_INT_EQ(get_u16(first + 24), 0x2222);
	XVT_ASSERT_INT_EQ(get_u16(first + 26), 0);
	XVT_ASSERT_INT_EQ(get_u16(first + 28), 0xFFFF);
	XVT_ASSERT_INT_EQ(get_u16(first + 30), 0xFFFF);
	for (int byte = 32; byte < 49; ++byte) {
		XVT_ASSERT_INT_EQ(first[byte], 0);
	}
	XVT_ASSERT_INT_EQ(get_u32(first + 49), 777);
	XVT_ASSERT_INT_EQ(first[53], 0);
	XVT_ASSERT_INT_EQ(get_u32(first + 54), 0);
	XVT_ASSERT_INT_EQ(get_u16(first + 58), 0xFFFF);
	/* No boarder, and neither parked nor waiting for boarding. */
	XVT_ASSERT_INT_EQ(get_u16(first + 86), 0xFFFF);
	/* The second listed static is slot 33, of another group, disabled; the
	 * third is slot 34. */
	const uint8_t *second = first + AI_WIRE_CONTACT_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(second), FIRST_STATIC + 3);
	XVT_ASSERT_INT_EQ(second[5], 9);
	XVT_ASSERT_INT_EQ(second[6], 8);
	XVT_ASSERT_INT_EQ(second[9], 0x08 | 0x02);
	XVT_ASSERT_INT_EQ(get_u16(second + AI_WIRE_CONTACT_BYTES),
			  FIRST_STATIC + 4);

	/* An answer aims at a static, and its signature is the target's. */
	fresh_world();
	g_region_main_object_slot_end = FIRST_STATIC;
	g_region_static_object_slot_count = STATIC_SLOTS;
	g_test_objects[FIRST_STATIC].mobj = NULL;
	g_test_objects[FIRST_STATIC].object_type = FLAGGED_TYPE;
	g_test_objects[FIRST_STATIC].object_signature = 0x77;
	g_test_objects[FIRST_STATIC].type_specific_word = 1023;
	struct craft_data *craft = due_craft(1);
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0, 1);
	*at++ = AI_WIRE_ACTION_TARGET;
	at = put_u16(at, FIRST_STATIC);
	g_fake.answer_length = finish_answer(g_fake.answer, at);
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, FIRST_STATIC);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_signature, 0x77);
	g_region_main_object_slot_end = SLOT_COUNT;
	g_region_static_object_slot_count = 0;
}

/* The box round each contact's model and its roll. Type 0x31 has cached
 * bounds with fractional and negative corners, which are cut toward zero;
 * type 0x32 has values in the tables but is not cached; type 0x33 is cached
 * with the empty model's corners; types 0x35, 0x36 and 0x37 are cached with
 * only their z, x or y corners crossed; type 201 is past the tables. Each of
 * those sends zeros. Type 0x38 is cached with its corners meeting on every
 * axis, which is not empty and sends its box. A static sends its type's box and its roll as a
 * craft does. */
static void check_contact_box_and_roll(void)
{
	enum {
		CACHED_TYPE = 0x31,
		UNCACHED_TYPE = 0x32,
		EMPTY_TYPE = 0x33,
		CROSSED_Z_TYPE = 0x35,
		CROSSED_X_TYPE = 0x36,
		CROSSED_Y_TYPE = 0x37,
		MEETING_TYPE = 0x38,
		PAST_TABLE_TYPE = 201,
		TYPE_COUNT = 8,
		/* The box starts 28 bytes before the contact's end: 24 of box,
		 * the roll and the boarder. */
		BOX = AI_WIRE_CONTACT_BYTES - 28,
		FIRST_STATIC = 30,
	};
	static const int types[TYPE_COUNT] = {
		CACHED_TYPE,	UNCACHED_TYPE,	EMPTY_TYPE,	PAST_TABLE_TYPE,
		CROSSED_Z_TYPE, CROSSED_X_TYPE, CROSSED_Y_TYPE, MEETING_TYPE};
	static const int32_t expected[TYPE_COUNT][6] = {
		{-3000, -162468, -7, 9000, 165215, 12},
		{0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{7, -6, 5, 7, -6, 5}};
	fresh_world();
	g_active_region_craft_object_slot_end = TYPE_COUNT + 1;
	for (int slot = 1; slot <= TYPE_COUNT; ++slot) {
		due_craft(slot)->ai_controller.think_timer = 0;
		g_test_objects[slot].object_type = (uint8_t)types[slot - 1];
		g_test_objects[slot].roll = (uint16_t)(0xBEE0 + slot);
	}
	g_model_bounds_cached[CACHED_TYPE] = 1;
	g_model_bounds_min[CACHED_TYPE] =
		(struct opt_vector){-3000.9f, -162468.5f, -7.2f};
	g_model_bounds_max[CACHED_TYPE] =
		(struct opt_vector){9000.9f, 165215.5f, 12.7f};
	g_model_bounds_min[UNCACHED_TYPE] =
		(struct opt_vector){-1.0f, -2.0f, -3.0f};
	g_model_bounds_max[UNCACHED_TYPE] =
		(struct opt_vector){4.0f, 5.0f, 6.0f};
	g_model_bounds_cached[EMPTY_TYPE] = 1;
	g_model_bounds_min[EMPTY_TYPE] = (struct opt_vector){
		1073741800.0f, 1073741800.0f, 1073741800.0f};
	g_model_bounds_max[EMPTY_TYPE] = (struct opt_vector){
		-1073741800.0f, -1073741800.0f, -1073741800.0f};
	/* Only one pair of corners crossed each. */
	g_model_bounds_cached[CROSSED_Z_TYPE] = 1;
	g_model_bounds_min[CROSSED_Z_TYPE] =
		(struct opt_vector){-5.0f, -5.0f, 8.0f};
	g_model_bounds_max[CROSSED_Z_TYPE] =
		(struct opt_vector){5.0f, 5.0f, 2.0f};
	g_model_bounds_cached[CROSSED_X_TYPE] = 1;
	g_model_bounds_min[CROSSED_X_TYPE] =
		(struct opt_vector){8.0f, -5.0f, -5.0f};
	g_model_bounds_max[CROSSED_X_TYPE] =
		(struct opt_vector){2.0f, 5.0f, 5.0f};
	g_model_bounds_cached[CROSSED_Y_TYPE] = 1;
	g_model_bounds_min[CROSSED_Y_TYPE] =
		(struct opt_vector){-5.0f, 8.0f, -5.0f};
	g_model_bounds_max[CROSSED_Y_TYPE] =
		(struct opt_vector){5.0f, 2.0f, 5.0f};
	/* Corners that meet on every axis: a model of no size, which is not
	 * empty. */
	g_model_bounds_cached[MEETING_TYPE] = 1;
	g_model_bounds_min[MEETING_TYPE] =
		(struct opt_vector){7.0f, -6.0f, 5.0f};
	g_model_bounds_max[MEETING_TYPE] =
		(struct opt_vector){7.0f, -6.0f, 5.0f};
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *contact = g_fake.question[0] + AI_WIRE_HEAD_BYTES +
				 TYPE_COUNT * AI_WIRE_CRAFT_VIEW_BYTES +
				 AI_WIRE_CONTACT_HEAD_BYTES;
	for (int index = 0; index < TYPE_COUNT; ++index) {
		const uint8_t *found = contact + index * AI_WIRE_CONTACT_BYTES;
		XVT_ASSERT_INT_EQ(get_u16(found), index + 1);
		for (int value = 0; value < 6; ++value) {
			XVT_ASSERT_INT_EQ(
				(int32_t)get_u32(found + BOX + 4 * value),
				expected[index][value]);
		}
		XVT_ASSERT_INT_EQ(get_u16(found + BOX + 24), 0xBEE1 + index);
	}

	/* A static: slot 30, of the cached type, with a roll. */
	fresh_world();
	g_region_main_object_slot_end = FIRST_STATIC;
	g_region_static_object_slot_count = 1;
	g_test_objects[FIRST_STATIC].mobj = NULL;
	g_object_type_table[CACHED_TYPE].behavior_flags = 2;
	g_test_objects[FIRST_STATIC] =
		(struct object_record){.object_type = CACHED_TYPE,
				       .roll = 0x1234,
				       .flight_group_idx = 3,
				       .type_specific_word = 1023};
	struct mission_order *order =
		&g_mission_flight_groups[TEST_GROUP].fg.orders[0];
	order->target1_type = 1;
	order->target1 = 3;
	order->target1_or_target2 = 1;
	due_craft(1);
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	XVT_ASSERT_INT_EQ(g_fake.question_length[0],
			  ai_wire_question_bytes(1, 1, 1));
	const uint8_t *object =
		g_fake.question[0] + AI_WIRE_HEAD_BYTES +
		AI_WIRE_CRAFT_VIEW_BYTES + AI_WIRE_CONTACT_HEAD_BYTES +
		AI_WIRE_CONTACT_BYTES + AI_WIRE_CONTACT_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(object), FIRST_STATIC);
	for (int value = 0; value < 6; ++value) {
		XVT_ASSERT_INT_EQ((int32_t)get_u32(object + BOX + 4 * value),
				  expected[0][value]);
	}
	XVT_ASSERT_INT_EQ(get_u16(object + BOX + 24), 0x1234);

	for (int type = 0; type < TYPE_COUNT; ++type) {
		g_model_bounds_cached[types[type]] = 0;
	}
	g_object_type_table[CACHED_TYPE].behavior_flags = 0;
	g_region_main_object_slot_end = SLOT_COUNT;
	g_region_static_object_slot_count = 0;
}

/* The escorted craft, station, start point and targets now reach the view
 * from the engine's books: a craft on an Escort order whose target is the
 * flight group of another craft, with its group arrived. */
static void check_view_escort_start_and_targets_now(void)
{
	enum { TARGET_GROUP = 3, ESCORTED = 5 };
	fresh_world();
	g_mission_header.num_flight_groups = 6;
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.orders[1].order = MISSION_ORDER_ESCORT;
	group->fg.orders[1].variable1 = 27;
	group->fg.orders[1].target1_type = 1;
	group->fg.orders[1].target1 = TARGET_GROUP;
	group->fg.orders[1].target1_or_target2 = 1;
	group->fg.mission_point_x[1] = 3;
	group->fg.mission_point_y[1] = 4;
	group->fg.mission_point_z[1] = 5;
	g_mission_fg_stats[TEST_GROUP].has_arrived = 1;
	g_mission_fg_stats[TEST_GROUP].current_mission_point_ref = 0x8001;
	struct craft_data *craft = due_craft(1);
	craft->ai_controller.current_order_slot = 1;
	g_test_objects[ESCORTED].object_type = 5;
	g_test_objects[ESCORTED].flight_group_idx = TARGET_GROUP;
	g_test_objects[ESCORTED].world_x = 4000;
	g_test_objects[ESCORTED].world_y = 5000;
	g_test_objects[ESCORTED].world_z = 6000;
	g_test_craft[ESCORTED].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_test_craft[ESCORTED].leader_obj_idx = UINT8_MAX;
	g_test_craft[ESCORTED].ai_controller.think_timer = 99;
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(view + VIEW_ESCORTED), ESCORTED);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + VIEW_STATION), 4000);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + VIEW_STATION + 4), 5000);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + VIEW_STATION + 8), 6000);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + VIEW_START_POINT), 768);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + VIEW_START_POINT + 4), -1024);
	XVT_ASSERT_INT_EQ((int32_t)get_u32(view + VIEW_START_POINT + 8), 1280);
	XVT_ASSERT_INT_EQ(view[VIEW_TARGETS_NOW], 0x2);
	/* The books field, a u32, starts 77 bytes into the view. */
	unsigned int books = get_u32(view + 77);
	XVT_ASSERT_INT_EQ(books & AI_WIRE_BOOKS_STATION, AI_WIRE_BOOKS_STATION);
	XVT_ASSERT_INT_EQ(books & AI_WIRE_BOOKS_START_POINT,
			  AI_WIRE_BOOKS_START_POINT);
}

/* The contact's flag byte of the one craft in slot 1 whose group's order in
 * slot 0 is order_code. */
static unsigned int flags_on_order(int order_code)
{
	fresh_world();
	due_craft(1)->working_subsystems = 1;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].order =
		(uint8_t)order_code;
	g_fake.echo = 1;
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	return g_fake
		.question[0][AI_WIRE_HEAD_BYTES + AI_WIRE_CRAFT_VIEW_BYTES +
			     AI_WIRE_CONTACT_HEAD_BYTES + 9];
}

/* A contact is parked (0x10) on exactly the orders 0, 28, 30, 33, 34, 35,
 * 38 and 39, and awaiting (0x20) on exactly 5 and 6; no other of the 40
 * order codes sets either. */
static void check_contact_parked_and_awaiting(void)
{
	for (int code = 0; code < 40; ++code) {
		unsigned int parked = code == 0 || code == 28 || code == 30 ||
				      code == 33 || code == 34 || code == 35 ||
				      code == 38 || code == 39;
		unsigned int awaiting = code == 5 || code == 6;
		XVT_ASSERT_INT_EQ(flags_on_order(code),
				  (parked ? 0x10u : 0) |
					  (awaiting ? 0x20u : 0));
	}
}

/* The boarder of a contact: the craft that carries it first; else the
 * lowest-numbered other craft whose current order boards (12 to 17, 31, 32)
 * and whose target it is; 0xFFFF for none. Slot 1 is aimed at by slot 5
 * (Pick Up, 17), slot 3 (Give, 12) and slot 4 (Attack, which boards
 * nothing); slot 2 is aimed at by itself only; slot 6 (Repair, 32) aims at
 * slot 1 too but is in the region's last slot, past the walk's end. */
static void check_contact_boarder(void)
{
	fresh_world();
	for (int slot = 1; slot <= 6; ++slot) {
		due_craft(slot)->ai_controller.think_timer = 5;
		g_test_objects[slot].flight_group_idx = (uint16_t)(10 + slot);
	}
	g_active_region_craft_object_slot_end = 6;
	g_test_craft[1].ai_controller.think_timer = 0;
	static const uint8_t orders[7] = {0, 7, 17, 12, 7, 17, 32};
	for (int slot = 1; slot <= 6; ++slot) {
		g_mission_flight_groups[10 + slot].fg.orders[0].order =
			orders[slot];
		g_test_craft[slot].ai_controller.target_obj_idx = 1;
	}
	g_test_craft[2].ai_controller.target_obj_idx = 2;
	g_test_craft[1].ai_controller.target_obj_idx = UINT16_MAX;
	g_fake.echo = 1;
	ai_brain_think_due_craft();
	const uint8_t *head = g_fake.question[0] + AI_WIRE_HEAD_BYTES +
			      AI_WIRE_CRAFT_VIEW_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(head), 5);
	const uint8_t *first = head + AI_WIRE_CONTACT_HEAD_BYTES;
	/* Slot 1: slot 3 (Give) is the lowest of those aimed at it on a
	 * boarding order; slot 2 aims at itself, slot 4 attacks. */
	XVT_ASSERT_INT_EQ(get_u16(first + 86), 3);
	/* Slot 2 aims at itself, slot 3 is aimed at by none. */
	XVT_ASSERT_INT_EQ(get_u16(first + AI_WIRE_CONTACT_BYTES + 86),
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(get_u16(first + 2 * AI_WIRE_CONTACT_BYTES + 86),
			  UINT16_MAX);
	/* An empty slot, then an object of another family, in slot 0 on a boarding
	 * order and aimed at slot 1 are not boarders. */
	g_mission_flight_groups[0].fg.orders[0].order = 17;
	g_test_craft[0].ai_controller.target_obj_idx = 1;
	g_fake.calls = 0;
	g_test_craft[1].ai_controller.think_timer = 0;
	ai_brain_think_due_craft();
	first = g_fake.question[0] + AI_WIRE_HEAD_BYTES +
		AI_WIRE_CRAFT_VIEW_BYTES + AI_WIRE_CONTACT_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(first + 86), 3);
	g_test_objects[0].object_type = 5;
	g_test_mobiles[0].family = 1;
	g_fake.calls = 0;
	g_test_craft[1].ai_controller.think_timer = 0;
	ai_brain_think_due_craft();
	first = g_fake.question[0] + AI_WIRE_HEAD_BYTES +
		AI_WIRE_CRAFT_VIEW_BYTES + AI_WIRE_CONTACT_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(first + 86), 3);
	/* A craft carried by slot 5 has that boarder, before any aimed at it. */
	g_test_craft[1].carrier_obj_idx = 5;
	g_test_craft[1].ai_controller.think_timer = 0;
	g_fake.calls = 0;
	ai_brain_think_due_craft();
	first = g_fake.question[0] + AI_WIRE_HEAD_BYTES +
		AI_WIRE_CRAFT_VIEW_BYTES + AI_WIRE_CONTACT_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(get_u16(first + 86), 5);
}

/* The status line of each looking activity, of a disabled craft, and of the
 * three the pilot program sets for Drop Off, Self Destruct and Kamikaze. */
static void check_activity_status_lines(void)
{
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[13],
			  IFMSG_170_LOOKING_FOR_CRAFT_TO_BOARD);
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[14],
			  IFMSG_171_LOOKING_FOR_CRAFT_TO_CAPTURE);
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[15],
			  IFMSG_172_LOOKING_FOR_CRAFT_TO_PICK_UP);
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[AI_ACTIVITY_DISABLED],
			  IFMSG_176_AWAITING_BOARDING);
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[AI_ACTIVITY_BOARDING],
			  IFMSG_173_DOCKING);
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[16],
			  IFMSG_189_DEPLOYING_CRAFT);
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[17],
			  IFMSG_190_SELF_DESTRUCTING);
	XVT_ASSERT_INT_EQ(g_ai_activity_status_message_id[18],
			  IFMSG_191_KAMIKAZE_ATTACK);
	XVT_ASSERT_INT_EQ(AI_ACTIVITY_DEPLOYING_CRAFT, 16);
	XVT_ASSERT_INT_EQ(AI_ACTIVITY_SELF_DESTRUCTING, 17);
	XVT_ASSERT_INT_EQ(AI_ACTIVITY_KAMIKAZE_ATTACK, 18);
	XVT_ASSERT_INT_EQ(AI_ACTIVITY_COUNT, 19);
}

/* The self destruct action sets the lifetime timer of a craft whose timer
 * is 0 to units x 1,180 ticks (1 unit: 1,180; 55 units: 64,900, the most
 * the 16-bit timer holds of whole units) and keeps a timer already running. */
static void check_self_destruct_action(void)
{
	static const struct {
		uint16_t before;
		uint8_t units;
		uint16_t after;
	} cases[] = {{0, 1, 1180},
		     {0, 55, 64900},
		     {0, 3, 3540},
		     {7, 3, 7},
		     {65535, 1, 65535}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		fresh_world();
		due_craft(1);
		g_test_mobiles[1].lifetime_timer = cases[row].before;
		uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
		at = craft_answer(at, 1, 0, 1);
		*at++ = AI_WIRE_ACTION_SELF_DESTRUCT;
		*at++ = cases[row].units;
		g_fake.answer_length = finish_answer(g_fake.answer, at);
		ai_brain_think_due_craft();
		XVT_ASSERT_INT_EQ(g_test_mobiles[1].lifetime_timer,
				  cases[row].after);
	}
	/* Refused units apply nothing: 0 and 56 (56 x 1,180 wraps 16 bits). */
	for (int units = 0; units < 2; ++units) {
		fresh_world();
		due_craft(1);
		uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
		at = craft_answer(at, 1, 0, 1);
		*at++ = AI_WIRE_ACTION_SELF_DESTRUCT;
		*at++ = units == 0 ? 0 : 56;
		g_fake.answer_length = finish_answer(g_fake.answer, at);
		ai_brain_think_due_craft();
		XVT_ASSERT_INT_EQ(g_test_mobiles[1].lifetime_timer, 0);
	}
}

/* Slot 1 is due; slots 3, 4 and 5 are craft of flight groups 3, 4 and 5.
 * The order's first pair names group first (joined by "or" with nothing),
 * its second pair group second. */
static void pair_world(int first, int second)
{
	fresh_world();
	due_craft(1);
	for (int slot = 3; slot <= 5; ++slot) {
		due_craft(slot)->ai_controller.think_timer = 5;
		g_test_objects[slot].flight_group_idx = (uint16_t)slot;
	}
	struct mission_order *order =
		&g_mission_flight_groups[TEST_GROUP].fg.orders[0];
	order->target1_type = 1;
	order->target1 = (uint16_t)first;
	order->target1_or_target2 = 1;
	order->secondary_target_types[0] = 1;
	order->secondary_targets[0] = (uint16_t)second;
	order->target3_or_target4 = 1;
	g_fake.echo = 1;
}

/* The order first pair mask has the contacts that match the first pair of
 * targets alone; every one of them is also in the order target mask, which
 * has the second pair's too. Contacts are slots 1, 3, 4, 5: bits 0 to 3. */
static void check_order_first_pair_mask(void)
{
	static const struct {
		int first;
		int second;
		unsigned int first_mask;
		unsigned int all_mask;
	} cases[] = {{3, 4, 0x2, 0x6}, {4, 3, 0x4, 0x6}, {3, 3, 0x2, 0x2},
		     {5, 6, 0x8, 0x8}, {6, 5, 0x0, 0x8}, {6, 7, 0x0, 0x0}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		pair_world(cases[row].first, cases[row].second);
		ai_brain_think_due_craft();
		XVT_ASSERT_INT_EQ(g_fake.calls, 1);
		const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
		XVT_ASSERT_INT_EQ(get_u32(view + 38), cases[row].all_mask);
		XVT_ASSERT_INT_EQ(get_u32(view + VIEW_FIRST_PAIR),
				  cases[row].first_mask);
	}
}

/* The pairs' joins: a pair's two targets are joined by "or" when its flag is
 * 1, else by "and", which needs one object to match both. Contacts are slots
 * 1, 3, 4, 5 (groups 2, 3, 4, 5): bits 0 to 3. */
static void check_pair_joins(void)
{
	static const struct {
		int first_a;
		int first_b;
		int first_or;
		int second_a;
		int second_b;
		int second_or;
		unsigned int first_mask;
		unsigned int all_mask;
	} cases[] = {
		/* "and" with the same group twice matches that group. */
		{3, 3, 0, 9, 9, 0, 0x2, 0x2},
		{3, 4, 0, 9, 9, 0, 0x0, 0x0},
		{3, 4, 1, 9, 9, 0, 0x6, 0x6},
		{3, 5, 1, 9, 9, 1, 0xA, 0xA},
		/* The second pair: "and" with one group twice, with two groups,
		 * and "or" with two groups, each of its targets read. */
		{9, 9, 1, 4, 4, 0, 0x0, 0x4},
		{9, 9, 1, 4, 5, 0, 0x0, 0x0},
		{9, 9, 1, 4, 5, 1, 0x0, 0xC},
		{9, 9, 1, 5, 4, 1, 0x0, 0xC}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		pair_world(9, 9);
		struct mission_order *order =
			&g_mission_flight_groups[TEST_GROUP].fg.orders[0];
		order->target1 = (uint16_t)cases[row].first_a;
		order->target2_type = 1;
		order->target2 = (uint16_t)cases[row].first_b;
		order->target1_or_target2 = (uint8_t)cases[row].first_or;
		order->secondary_targets[0] = (uint16_t)cases[row].second_a;
		order->secondary_target_types[1] = 1;
		order->secondary_targets[1] = (uint16_t)cases[row].second_b;
		order->target3_or_target4 = (uint8_t)cases[row].second_or;
		ai_brain_think_due_craft();
		const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
		XVT_ASSERT_INT_EQ(get_u32(view + 38), cases[row].all_mask);
		XVT_ASSERT_INT_EQ(get_u32(view + VIEW_FIRST_PAIR),
				  cases[row].first_mask);
	}
}

/* A pair's second target of type 0 names nothing, and with "and" nothing
 * matches though the first target does: the second is read from its own
 * type, not the first's. */
static void check_second_target_type(void)
{
	pair_world(9, 4);
	struct mission_order *order =
		&g_mission_flight_groups[TEST_GROUP].fg.orders[0];
	order->secondary_targets[1] = 4;
	order->target3_or_target4 = 0;
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(get_u32(g_fake.question[0] + AI_WIRE_HEAD_BYTES + 38),
			  0);
}

/* The lifetime timer and the dock phase in the view: the object's timer in
 * ticks, 0 for none, and the dock's phase while the mode is BOARD. */
static void check_view_lifetime_and_phase(void)
{
	static const uint16_t timers[] = {0, 1, 3540, 7080, 65535};
	for (size_t row = 0; row < sizeof timers / sizeof timers[0]; ++row) {
		fresh_world();
		struct craft_data *craft = due_craft(1);
		g_test_mobiles[1].lifetime_timer = timers[row];
		g_fake.echo = 1;
		ai_brain_think_due_craft();
		const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
		XVT_ASSERT_INT_EQ(get_u16(view + VIEW_LIFETIME), timers[row]);
		XVT_ASSERT_INT_EQ(view[VIEW_BOARD_PHASE], 0);
		(void)craft;
	}
	/* A phase counts only in mode BOARD. */
	for (int phase = 0; phase <= 3; ++phase) {
		fresh_world();
		struct craft_data *craft = due_craft(1);
		craft->ai_controller.maneuver_phase = (uint8_t)phase;
		g_fake.echo = 1;
		ai_brain_think_due_craft();
		XVT_ASSERT_INT_EQ(g_fake.question[0][AI_WIRE_HEAD_BYTES +
						     VIEW_BOARD_PHASE],
				  0);
	}
}

/* Shift+B and the player's release ask whether an object matches the targets
 * of the order g_cur_craft works on: either pair of its current slot's, as
 * the order target mask has it; a static of the group matches as a craft
 * does; no object, an object of another group and a craft that is not in the
 * active region do not. */
static void check_order_targets_match_object(void)
{
	pair_world(3, 4);
	g_active_region_craft_object_slot_end = 8;
	g_test_objects[9].object_type = 5;
	g_test_objects[9].flight_group_idx = 3;
	g_test_objects[10].object_type = 5;
	g_test_objects[10].flight_group_idx = 6;
	g_cur_craft = &g_test_craft[1];
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(3), 1);
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(4), 1);
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(5), 0);
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(1), 0);
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(9), 1);
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(10), 0);
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(UINT16_MAX), 0);
	/* The craft's own current slot: slot 1's order is read, not slot 0's. */
	g_mission_flight_groups[TEST_GROUP].fg.orders[1] =
		g_mission_flight_groups[TEST_GROUP].fg.orders[0];
	g_mission_flight_groups[TEST_GROUP].fg.orders[1].target1 = 5;
	g_mission_flight_groups[TEST_GROUP].fg.orders[1].secondary_targets[0] =
		6;
	g_test_craft[1].ai_controller.current_order_slot = 1;
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(3), 0);
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(5), 1);
	/* A slot that is empty or holds another family's object is no holder,
	 * though its craft record is the one g_cur_craft names. */
	g_test_craft[1].ai_controller.current_order_slot = 0;
	g_test_mobiles[0].p_craft = &g_test_craft[1];
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(3), 1);
	g_test_objects[0].object_type = 5;
	g_test_mobiles[0].family = 1;
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(3), 1);
	g_test_objects[0].object_type = 0;
	g_test_mobiles[0].family = 0;
	g_test_mobiles[0].p_craft = &g_test_craft[0];
	g_test_craft[1].ai_controller.current_order_slot = 1;
	/* A craft outside the region, or none, matches nothing. */
	g_active_region_craft_object_slot_end = 1;
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(5), 0);
	g_active_region_craft_object_slot_end = 8;
	g_cur_craft = NULL;
	XVT_ASSERT_INT_EQ(ai_brain_order_targets_match_object(5), 0);
}

/* An answer with no board action starts no dock, though the craft is at the
 * board point of its target and the action's object would read 0: the target
 * is slot 0. */
static void check_no_board_without_the_action(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	due_craft(0)->ai_controller.think_timer = 5;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAGS_ALL;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	craft->ai_controller.target_obj_idx = 0;
	g_fake.echo = 1;
	ai_brain_think_due_craft();
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_mode,
			  AI_MANEUVER_MODE_NULL);
	/* The books did show it at its board point. */
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_TRUE((get_u32(view + 77) & AI_WIRE_BOOKS_AT_BOARD_POINT) !=
			0);
}

/* Slot 1 on Give at its target, slot 2, both at the origin, so the books
 * show it at its board point. */
static struct craft_data *give_world(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	due_craft(2)->ai_controller.think_timer = 5;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].variable1 = 2;
	craft->ai_controller.target_obj_idx = 2;
	craft->ai_controller.target_signature = 0x102;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAGS_ALL;
	static char name_long[] = "Craft";
	strcpy(g_model_defs[0].name, "Craft");
	g_model_defs[0].name_long = name_long;
	static char message[] = "\003message";
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = message;
	}
	g_players[g_local_player].object_index = -1;
	return craft;
}

/* Answers slot 1 with the action bytes given: a count and the bytes. */
static void answer_slot_one(const uint8_t *actions, size_t bytes,
			    int action_count)
{
	uint8_t *at = start_answer(g_fake.answer, TEST_TICK, 1);
	at = craft_answer(at, 1, 0x30, action_count);
	memcpy(at, actions, bytes);
	g_fake.answer_length = finish_answer(g_fake.answer, at + bytes);
	g_fake.calls = 0;
	g_test_craft[1].ai_controller.think_timer = 0;
	ai_brain_think_due_craft();
}

/* The board action starts the dock: throttle 0, phase align, mode BOARD. The
 * dock then steps on each think before the view is built, so the next question
 * shows the step's phase; while it docks the steer and the throttle are not
 * applied and the maneuver timer is refused with the whole answer; a board of
 * no object stops it. */
static void check_board_action(void)
{
	struct craft_data *craft = give_world();
	craft->throttle_speed = 0x8000;
	static const uint8_t board[] = {AI_WIRE_ACTION_BOARD, 2, 0};
	answer_slot_one(board, sizeof board, 1);
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_mode,
			  AI_MANEUVER_MODE_BOARD);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 1);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	/* The view of the first question showed the board point reached. */
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(view[VIEW_BOARD_PHASE], 0);
	/* Steer and throttle while it docks: the notebook is kept, the heading
	 * and throttle are not set. The align step ran first and found the
	 * craft on its point, so the second question shows the transfer. */
	craft->ai_controller.target_xy_angle = 0x77;
	static const uint8_t steer_throttle[] = {
		AI_WIRE_ACTION_STEER,	 0x11, 0x11, 0x00, 0x50, 0x22, 0x22,
		AI_WIRE_ACTION_THROTTLE, 0x00, 0x80};
	answer_slot_one(steer_throttle, sizeof steer_throttle, 2);
	view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(view[VIEW_BOARD_PHASE], 2);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 2);
	XVT_ASSERT_INT_EQ(craft->ai_notebook[0], 0x30);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_xy_angle, 0x77);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	/* The maneuver timer while it docks is refused and nothing applies. */
	craft->ai_notebook[0] = 0;
	static const uint8_t timer[] = {AI_WIRE_ACTION_MANEUVER_TIMER, 9, 0, 0,
					0};
	answer_slot_one(timer, sizeof timer, 1);
	XVT_ASSERT_INT_EQ(craft->ai_notebook[0], 0);
	/* A board of no object stops the dock in phase 2. */
	static const uint8_t stop[] = {AI_WIRE_ACTION_BOARD, 0xFF, 0xFF};
	answer_slot_one(stop, sizeof stop, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 0);
	/* The activity BOARDING on a boarding order is mode BOARD, phase 0; any
	 * other activity ends it. */
	static const uint8_t boarding[] = {AI_WIRE_ACTION_ACTIVITY,
					   AI_ACTIVITY_BOARDING};
	answer_slot_one(boarding, sizeof boarding, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_mode,
			  AI_MANEUVER_MODE_BOARD);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	static const uint8_t looking[] = {AI_WIRE_ACTION_ACTIVITY,
					  AI_ACTIVITY_LOOKING_TO_BOARD};
	answer_slot_one(looking, sizeof looking, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_mode,
			  AI_MANEUVER_MODE_NULL);
}

/* Slot 1 on Drop Off of group 2 (two craft to deliver), 1,000 east of the
 * origin, where the group's first place stands. */
static struct craft_data *drop_world(void)
{
	fresh_world();
	struct craft_data *craft = due_craft(1);
	g_mission_header.num_flight_groups = 4;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].order =
		MISSION_ORDER_DROP_OFF;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].variable2 = 2;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	g_mission_fg_stats[1].current_mission_point_ref = 0x8000;
	g_mission_fg_stats[1].outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] = 0;
	g_model_bounds_cached[g_craft_type_to_object_type[0]] = 1;
	g_model_bounds_cached[5] = 1;
	craft->throttle_speed = 0x8000;
	/* Within 2,048 of the drop point and far from the place itself. */
	g_test_objects[1].world_x = 1000;
	return craft;
}

/* The drop off action starts the drop: throttle 0, phase push, mode DROPOFF
 * (with the books showing the craft at its drop point). The drop then steps
 * on each think before the view is built, so the next question shows its
 * phase; while it drops the steer and the throttle are not applied and the
 * maneuver timer and a target are refused with the whole answer; a drop off
 * of 0 stops it. */
static void check_drop_off_action(void)
{
	struct craft_data *craft = drop_world();
	static const uint8_t start[] = {AI_WIRE_ACTION_DROP_OFF, 1};
	answer_slot_one(start, sizeof start, 1);
	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(view[VIEW_DROP_PHASE], 0);
	XVT_ASSERT_INT_EQ(get_u32(view + VIEW_DROP_POINT + 8), 932);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_mode,
			  AI_MANEUVER_MODE_DROPOFF);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 1);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	/* Steer and throttle while it drops: the notebook is kept, the heading
	 * and throttle are not set. */
	static const uint8_t steer_throttle[] = {
		AI_WIRE_ACTION_STEER,	 0x11, 0x11, 0x00, 0x50, 0x22, 0x22,
		AI_WIRE_ACTION_THROTTLE, 0x00, 0x80};
	answer_slot_one(steer_throttle, sizeof steer_throttle, 2);
	view = g_fake.question[0] + AI_WIRE_HEAD_BYTES;
	XVT_ASSERT_INT_EQ(view[VIEW_DROP_PHASE], 1);
	XVT_ASSERT_INT_EQ(craft->ai_notebook[0], 0x30);
	/* The heading is the drop's own, toward the place, not the 0x1111 the
	 * answer asked for, and the drop's step pushed the carrier 1,000 back
	 * to the place at the origin. */
	XVT_ASSERT_TRUE(craft->ai_controller.target_xy_angle != 0x1111);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, -1000);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	/* The maneuver timer and a target while it drops are refused, each
	 * with the notebook of its answer. */
	craft->ai_notebook[0] = 0;
	static const uint8_t timer[] = {AI_WIRE_ACTION_MANEUVER_TIMER, 9, 0, 0,
					0};
	answer_slot_one(timer, sizeof timer, 1);
	XVT_ASSERT_INT_EQ(craft->ai_notebook[0], 0);
	static const uint8_t target[] = {AI_WIRE_ACTION_TARGET, 0xFF, 0xFF};
	answer_slot_one(target, sizeof target, 1);
	XVT_ASSERT_INT_EQ(craft->ai_notebook[0], 0);
	/* A drop off of 0 stops it, keeping the places delivered. */
	craft->ai_controller.waypoint_index = 1;
	static const uint8_t stop[] = {AI_WIRE_ACTION_DROP_OFF, 0};
	answer_slot_one(stop, sizeof stop, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_mode,
			  AI_MANEUVER_MODE_NULL);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.waypoint_index, 1);
	/* A mode left at DROPOFF with no drop is cleared by the answer's sync. */
	craft->ai_controller.maneuver_mode = AI_MANEUVER_MODE_DROPOFF;
	craft->ai_controller.maneuver_phase = 0;
	static const uint8_t nothing[] = {AI_WIRE_ACTION_THINK_INTERVAL, 9, 0};
	answer_slot_one(nothing, sizeof nothing, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_mode,
			  AI_MANEUVER_MODE_NULL);
	/* The activity DEPLOYING_CRAFT is accepted and kept. */
	static const uint8_t deploying[] = {AI_WIRE_ACTION_ACTIVITY,
					    AI_ACTIVITY_DEPLOYING_CRAFT};
	answer_slot_one(deploying, sizeof deploying, 1);
	XVT_ASSERT_INT_EQ(craft->ai_controller.activity,
			  AI_ACTIVITY_DEPLOYING_CRAFT);
}

int main(void)
{
	check_stat_level();
	check_no_exchange_changes_nothing();
	check_who_is_asked();
	check_question_bytes();
	check_contacts_and_order_targets();
	check_enemy_mask_and_leader();
	check_lost_leader_named();
	check_warhead_action();
	check_countermeasure_action();
	check_view_waypoints();
	check_view_place_and_hangar_exit();
	check_view_lock_and_warheads_coming();
	check_commands_and_evaded();
	check_arrival_action();
	check_answer_applied();
	check_pitch_direction();
	check_notebook_only_answer();
	check_refused_answer_applies_nothing();
	check_more_craft_than_one_question();
	check_attack_target();
	check_view_contact_flags_and_shooter();
	check_maneuver_timer();
	check_turret_ion();
	check_statics();
	check_contact_box_and_roll();
	check_view_escort_start_and_targets_now();
	check_contact_parked_and_awaiting();
	check_contact_boarder();
	check_activity_status_lines();
	check_self_destruct_action();
	check_order_first_pair_mask();
	check_view_lifetime_and_phase();
	check_order_targets_match_object();
	check_board_action();
	check_drop_off_action();
	check_pair_joins();
	check_second_target_type();
	check_no_board_without_the_action();
	ai_brain_set_exchange(NULL);
	return 0;
}

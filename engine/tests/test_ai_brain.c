/* Tests for xvt/flight/ai_brain.c's think loop: which craft are asked about,
 * the question's bytes, what an accepted answer changes, and that a refused
 * answer, or no exchange at all, changes nothing but the think timers. The
 * pilot program is a fake exchange here: it keeps each question and hands
 * back an answer this file builds byte by byte from the layout in
 * ai_wire.h. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/ai/ai_wire.h"

enum {
	SLOT_COUNT = 40,
	TEST_TICK = 0x01020304,
	TEST_GROUP = 2,
	TEST_INTERVAL = 59,
	/* Craft in one question: ai_brain.c's AI_BRAIN_QUESTION_CRAFT. */
	QUESTION_CRAFT = 32,
	QUESTION_CAPACITY =
		AI_WIRE_HEAD_BYTES + QUESTION_CRAFT * AI_WIRE_CRAFT_VIEW_BYTES,
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
		memcpy(at, view + 38, AI_NOTEBOOK_BYTES);
		at[0] = ECHO_MARK;
		at += AI_NOTEBOOK_BYTES;
		*at++ = 0;
	}
	XVT_ASSERT_TRUE((size_t)(at - answer) <= answer_capacity);
	return finish_answer(answer, at);
}

/* Empty slots 0 to SLOT_COUNT - 1 with their mobile objects and craft
 * records, the active region's craft slots 0 to 8, the clock at TEST_TICK,
 * and the fake exchange set, canned with no answer. */
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
	g_game_time = TEST_TICK;
	g_flight_sim_side_effects_suppressed = 0;
	ai_brain_set_exchange(fake_exchange);
}

/* Makes slot a computer craft due to think: think timer 0, think interval
 * TEST_INTERVAL, in flight group TEST_GROUP. */
static struct craft_data *due_craft(int slot)
{
	g_test_objects[slot].object_type = 5;
	g_test_objects[slot].object_signature = (uint16_t)(0x100 + slot);
	g_test_objects[slot].flight_group_idx = TEST_GROUP;
	g_test_craft[slot].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_test_craft[slot].ai_controller.think_interval = TEST_INTERVAL;
	g_test_craft[slot].ai_controller.think_timer = 0;
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
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *q = g_fake.question[0];
	XVT_ASSERT_INT_EQ(g_fake.question_length[0], ai_wire_question_bytes(1));
	XVT_ASSERT_INT_EQ(get_u32(q), ai_wire_question_bytes(1) - 4);
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
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		XVT_ASSERT_INT_EQ(view[38 + byte], 0x60 + byte);
	}
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
	at = craft_answer(at, 4, 0x80, 4);
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

int main(void)
{
	check_no_exchange_changes_nothing();
	check_who_is_asked();
	check_question_bytes();
	check_answer_applied();
	check_pitch_direction();
	check_notebook_only_answer();
	check_refused_answer_applies_nothing();
	check_more_craft_than_one_question();
	ai_brain_set_exchange(NULL);
	return 0;
}

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
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
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
			    AI_WIRE_CONTACTS_MAX * AI_WIRE_CONTACT_BYTES,
	/* Where a craft view's notebook starts. */
	VIEW_NOTEBOOK = AI_WIRE_CRAFT_VIEW_BYTES - AI_NOTEBOOK_BYTES,
	/* Where its formation place starts, the 12 bytes before the
	 * notebook. */
	VIEW_PLACE = VIEW_NOTEBOOK - 12,
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
	/* As spawn leaves them: no player's command. */
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
	g_fake.echo = 1;

	ai_brain_think_due_craft();

	XVT_ASSERT_INT_EQ(g_fake.calls, 1);
	const uint8_t *q = g_fake.question[0];
	XVT_ASSERT_INT_EQ(g_fake.question_length[0],
			  ai_wire_question_bytes(1, 1));
	XVT_ASSERT_INT_EQ(get_u32(q), ai_wire_question_bytes(1, 1) - 4);
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
			  ai_wire_question_bytes(1, 4));
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
	XVT_ASSERT_INT_EQ(player[9], AI_WIRE_CONTACT_PLAYER);
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
 * warhead action; returns its launcher flags after. */
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
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_signature, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.has_live_target, 0);
	/* laser_firewarheadsystem adds 472 ticks whether or not it fired. */
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_cooldown_ticks[0],
			  inhibit == 0 ? 472 : 0);
	g_model_defs[craft->model_index].warhead_launcher_first_slot[0] = 0;
	return (uint8_t)craft->warhead_launcher_flags[0];
}

/* The warhead action fires through laser_firewarheadsystem with the 1997
 * AI's launcher flags: both slots when paired, else the first, or the second
 * when it holds more; not at all while the craft's weapons are held. */
static void check_warhead_action(void)
{
	XVT_ASSERT_INT_EQ(launcher_flags_after(1, 0, 0, 0), 3);
	XVT_ASSERT_INT_EQ(launcher_flags_after(0, 0, 1, 1), 1);
	XVT_ASSERT_INT_EQ(launcher_flags_after(0, 0, 1, 2), 0x81);
	XVT_ASSERT_INT_EQ(launcher_flags_after(0, 5, 0, 0), 0x55);
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
	check_commands_and_evaded();
	check_arrival_action();
	check_answer_applied();
	check_pitch_direction();
	check_notebook_only_answer();
	check_refused_answer_applies_nothing();
	check_more_craft_than_one_question();
	ai_brain_set_exchange(NULL);
	return 0;
}

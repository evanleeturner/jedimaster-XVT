/* Tests for xvt_runtime/ai/ai_wire.c: the question's bytes, written
 * out here by hand from the layout in ai_wire.h, and the answer reader on
 * answers this file builds byte by byte, one good and one per fault. No game
 * state is read. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/ai/ai_wire.h"

enum { TEST_TICK = 0x0A0B0C0D };

static struct ai_wire_craft_view view_with_distinct_bytes(void)
{
	struct ai_wire_craft_view view = {
		0x0102,
		0x0304,
		0x05,
		0x06,
		0x07,
		0x08,
		0x09,
		0x0A,
		0x0C0B,
		0x0E0D,
		0x100F,
		0x14131211,
		-2,
		0x1C1B1A19,
		0x1E1D,
		0x201F,
		0x2221,
		0x2423,
		-3,
		0x28272625,
		0x2A29,
		0x2B,
		0x2C,
		{0x2D, 0x2E},
		{0x2F, 0x30},
		{0x31, 0x32},
		{0x3433, 0x3635},
		0x3A393837,
		0x3C3B,
		{0x3D, 0x3E, 0x3F, 0x40},
		{0x41, 0x42, 0x43, 0x44},
		0x4645,
		0x47,
		0x48,
		0x49,
		0x9291,
		0x4B4A,
		0x4D4C,
		{0x51504F4E, 0x55545352, 0x59585756},
		{0x5D5C5B5A, 0x61605F5E, -6},
		{0x65646362, 0x69686766, 0x6D6C6B6A},
		0x6E,
		0x6F,
		0x70,
		0x71,
		0x7372,
		0x7574,
		{0x7776, 0x7978, 0x7B7A, 0x7D7C},
		{0x7E, 0x7F, 0x80, 0x81},
		{0x82, 0x83, 0x84, 0x85},
		{0x13121110, 0x17161514, 0x1B1A1918, 0x1F1E1D1C},
		{0x23222120, 0x27262524, 0x2B2A2928, -7},
		{0x2F2E2D2C, 0x33323130, 0x37363534, 0x3B3A3938},
		0x3C,
		{0x403F3E3D, 0x44434241, 0x48474645, 0x4C4B4A49, 0x504F4E4D,
		 0x54535251, 0x58575655, 0x5C5B5A59},
		{0x605F5E5D, 0x64636261, 0x68676665, 0x6C6B6A69, 0x706F6E6D,
		 0x74737271, 0x78777675, 0x7C7B7A79},
		{0x107F7E7D, 0x14131211, 0x18171615, 0x1C1B1A19, 0x201F1E1D,
		 0x24232221, 0x28272625, -8},
		0x29,
		0x2B2A,
		0x2D2C,
		{0x31302F2E, 0x35343332, -9},
		0x3736,
		0x3938,
		0x3A,
		0x3B,
		0x3F3E3D3C,
		{0}};
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		view.notebook[byte] = (uint8_t)(0x40 + byte);
	}
	return view;
}

static struct ai_wire_contact contact_with_distinct_bytes(void)
{
	struct ai_wire_contact contact = {
		0x4241, 0x4443,	    0x45,	0x46,	    0x47,
		0x48,	0x49,	    0x4A,	0x4E4D4C4B, 0x5251504F,
		-4,	0x5453,	    0x5655,	0x5857,	    0x5A59,
		0x5C5B, -5,	    0x605F5E5D, 0x64636261, 0x68676665,
		0x69,	0x6D6C6B6A, 0x6E,	0x7271706F, 0x7473};
	return contact;
}

static void check_question_bytes(void)
{
	static const uint8_t expected[AI_WIRE_HEAD_BYTES + 313] = {
		0xC9, 0x01, 0x00, 0x00, 0x4A, 0x4D, 0x50, 0x57, 0x0D, 0x00,
		0x01, 0x00, 0x0D, 0x0C, 0x0B, 0x0A, 0x01, 0x00, 0x00, 0x00,
		0x02, 0x01, 0x04, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
		0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
		0xFE, 0xFF, 0xFF, 0xFF, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
		0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0xFD, 0xFF, 0x25, 0x26,
		0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
		0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
		0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
		0x45, 0x46, 0x47, 0x48, 0x49, 0x91, 0x92, 0x4A, 0x4B, 0x4C,
		0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,
		0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60,
		0x61, 0xFA, 0xFF, 0xFF, 0xFF, 0x62, 0x63, 0x64, 0x65, 0x66,
		0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
		0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
		0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84,
		0x85, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,
		0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0xF9,
		0xFF, 0xFF, 0xFF, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
		0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
		0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
		0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
		0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
		0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64,
		0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E,
		0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
		0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x10, 0x11, 0x12,
		0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
		0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
		0x27, 0x28, 0xF8, 0xFF, 0xFF, 0xFF, 0x29, 0x2A, 0x2B, 0x2C,
		0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0xF7,
		0xFF, 0xFF, 0xFF, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
		0x3D, 0x3E, 0x3F};
	static const uint8_t expected_contacts[AI_WIRE_CONTACT_HEAD_BYTES +
					       AI_WIRE_CONTACT_BYTES] = {
		0x01, 0x00, 0x00, 0x00, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
		0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
		0x51, 0x52, 0xFC, 0xFF, 0xFF, 0xFF, 0x53, 0x54, 0x55, 0x56,
		0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0xFB, 0xFF, 0xFF, 0xFF,
		0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
		0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
		0x71, 0x72, 0x73, 0x74};
	struct ai_wire_craft_view view = view_with_distinct_bytes();
	struct ai_wire_contact contact = contact_with_distinct_bytes();
	struct ai_wire_question question = {TEST_TICK, 1, &view, 1, &contact};
	uint8_t out[AI_WIRE_HEAD_BYTES + AI_WIRE_CRAFT_VIEW_BYTES +
		    AI_WIRE_CONTACT_HEAD_BYTES + AI_WIRE_CONTACT_BYTES];
	XVT_ASSERT_INT_EQ(ai_wire_question_bytes(1, 1), sizeof out);
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out, &question),
			  sizeof out);
	for (size_t byte = 0; byte < sizeof expected; ++byte) {
		XVT_ASSERT_INT_EQ(out[byte], expected[byte]);
	}
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		XVT_ASSERT_INT_EQ(out[sizeof expected + (size_t)byte],
				  0x40 + byte);
	}
	const uint8_t *contacts =
		out + AI_WIRE_HEAD_BYTES + AI_WIRE_CRAFT_VIEW_BYTES;
	for (size_t byte = 0; byte < sizeof expected_contacts; ++byte) {
		XVT_ASSERT_INT_EQ(contacts[byte], expected_contacts[byte]);
	}
}

static void check_question_refusals(void)
{
	struct ai_wire_craft_view view = view_with_distinct_bytes();
	struct ai_wire_contact contact = contact_with_distinct_bytes();
	struct ai_wire_question question = {TEST_TICK, 1, &view, 1, &contact};
	uint8_t out[AI_WIRE_HEAD_BYTES + AI_WIRE_CRAFT_VIEW_BYTES +
		    AI_WIRE_CONTACT_HEAD_BYTES + AI_WIRE_CONTACT_BYTES];
	memset(out, 0xEE, sizeof out);
	XVT_ASSERT_INT_EQ(
		ai_wire_write_question(out, sizeof out - 1, &question), 0);
	XVT_ASSERT_INT_EQ(out[0], 0xEE);
	question.craft_count = -1;
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out, &question),
			  0);
	question.craft_count = UINT16_MAX + 1;
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out, &question),
			  0);
	question.craft_count = 0;
	question.contact_count = -1;
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out, &question),
			  0);
	question.contact_count = AI_WIRE_CONTACTS_MAX + 1;
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out, &question),
			  0);
	/* No craft due and no contacts: the header and the contact count. */
	question.contact_count = 0;
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out, &question),
			  AI_WIRE_HEAD_BYTES + AI_WIRE_CONTACT_HEAD_BYTES);
	XVT_ASSERT_INT_EQ(out[0],
			  AI_WIRE_HEAD_BYTES + AI_WIRE_CONTACT_HEAD_BYTES - 4);
}

/* An answer built byte by byte; finish_answer sets its length field. */
struct test_bytes {
	uint8_t data[512];
	size_t length;
};

static void add_u8(struct test_bytes *bytes, unsigned int value)
{
	bytes->data[bytes->length++] = (uint8_t)value;
}

static void add_u16(struct test_bytes *bytes, unsigned int value)
{
	add_u8(bytes, value & 0xFFu);
	add_u8(bytes, value >> 8);
}

static void add_u32(struct test_bytes *bytes, uint32_t value)
{
	add_u16(bytes, value & 0xFFFFu);
	add_u16(bytes, value >> 16);
}

static void start_answer(struct test_bytes *bytes, unsigned int count)
{
	bytes->length = 0;
	add_u32(bytes, 0);
	add_u32(bytes, AI_WIRE_MAGIC);
	add_u16(bytes, AI_WIRE_VERSION);
	add_u16(bytes, AI_WIRE_KIND_ANSWER);
	add_u32(bytes, TEST_TICK);
	add_u16(bytes, count);
	add_u16(bytes, 0);
}

/* A craft answer's object index and notebook, every byte fill; the caller
 * adds the action count and actions. */
static void add_craft(struct test_bytes *bytes, unsigned int object_index,
		      uint8_t fill)
{
	add_u16(bytes, object_index);
	memset(&bytes->data[bytes->length], fill, AI_NOTEBOOK_BYTES);
	bytes->length += AI_NOTEBOOK_BYTES;
}

static void finish_answer(struct test_bytes *bytes)
{
	uint32_t body = (uint32_t)bytes->length - 4;
	for (int byte = 0; byte < 4; ++byte) {
		bytes->data[byte] = (uint8_t)(body >> (8 * byte));
	}
}

/* Craft 7 has two cannon groups, one launcher ready, turrets in weapon
 * slots 3 and 5 and a flare ready, and leads, with orders in slots 0 and
 * 1, at its hangar;
 * craft 9 has none of these and follows craft 0. The contacts are the two
 * craft and object 12. */
static struct ai_wire_craft_view g_views[2] = {
	{.object_index = 7,
	 .turret_slot_mask = 1u << 3 | 1u << 5,
	 .cannon_group_count = 2,
	 .warhead_launcher_count = 1,
	 .warheads_left = {2, 0},
	 .order_codes = {1, 2, 0, 0},
	 .leader = UINT16_MAX,
	 .books = AI_WIRE_BOOKS_AT_HANGAR,
	 .countermeasure = AI_WIRE_COUNTERMEASURE_FLARE,
	 .countermeasures_left = 3,
	 .countermeasure_working = 1,
	 .commands = AI_WIRE_COMMAND_EVADE},
	{.object_index = 9}};
static struct ai_wire_contact g_contacts[3] = {
	{.object_index = 7}, {.object_index = 9}, {.object_index = 12}};
static struct ai_wire_question g_question = {TEST_TICK, 2, g_views, 3,
					     g_contacts};

/* Craft 7 sends every action kind but depart, last to first, aiming two
 * turrets, the first with ion shots; craft 9 sends none. */
static void good_answer(struct test_bytes *bytes)
{
	start_answer(bytes, 2);
	add_craft(bytes, 7, 0x5A);
	add_u8(bytes, 14);
	add_u8(bytes, AI_WIRE_ACTION_MANEUVER_TIMER);
	add_u32(bytes, 0x7FFFFFFF);
	add_u8(bytes, AI_WIRE_ACTION_EVADED);
	add_u8(bytes, AI_WIRE_ACTION_COUNTERMEASURE);
	add_u8(bytes, AI_WIRE_ACTION_DOCK);
	add_u8(bytes, AI_WIRE_ACTION_ORDER);
	add_u8(bytes, 1);
	add_u8(bytes, 2);
	add_u8(bytes, AI_WIRE_ACTION_ARRIVAL);
	add_u8(bytes, 1);
	add_u16(bytes, AI_WIRE_ARRIVAL_SPEED_MAX);
	add_u8(bytes, AI_WIRE_ACTION_TURRETS);
	add_u8(bytes, 2);
	add_u8(bytes, 5);
	add_u16(bytes, UINT16_MAX);
	add_u8(bytes, 1);
	add_u8(bytes, 3);
	add_u16(bytes, 12);
	add_u8(bytes, 0);
	add_u8(bytes, AI_WIRE_ACTION_WARHEAD);
	add_u8(bytes, 0);
	add_u8(bytes, 1);
	add_u8(bytes, AI_WIRE_ACTION_LASERS);
	add_u8(bytes, 3);
	add_u8(bytes, 4);
	add_u8(bytes, 1);
	add_u8(bytes, 2);
	add_u8(bytes, AI_WIRE_ACTION_TARGET);
	add_u16(bytes, 12);
	add_u8(bytes, AI_WIRE_ACTION_THINK_INTERVAL);
	add_u16(bytes, 59);
	add_u8(bytes, AI_WIRE_ACTION_ACTIVITY);
	add_u8(bytes, AI_ACTIVITY_DISABLED);
	add_u8(bytes, AI_WIRE_ACTION_THROTTLE);
	add_u16(bytes, 0x8000);
	add_u8(bytes, AI_WIRE_ACTION_STEER);
	add_u16(bytes, 0x1234);
	add_u16(bytes, 0x4000);
	add_u16(bytes, 0xFFFF);
	add_craft(bytes, 9, 0xA5);
	add_u8(bytes, 0);
	finish_answer(bytes);
}

static enum ai_wire_result read_two(const struct test_bytes *bytes,
				    struct ai_wire_craft_answer *answers)
{
	return ai_wire_read_answer(bytes->data, bytes->length, &g_question,
				   answers);
}

static void check_good_answer(void)
{
	struct test_bytes bytes;
	struct ai_wire_craft_answer answers[2];
	good_answer(&bytes);
	/* Craft 7's answer is the longest but for 14 turrets of 4 bytes. */
	XVT_ASSERT_INT_EQ(bytes.length, AI_WIRE_HEAD_BYTES +
						AI_WIRE_CRAFT_ANSWER_BYTES_MAX -
						14 * 4 + 3 + AI_NOTEBOOK_BYTES);
	XVT_ASSERT_INT_EQ(read_two(&bytes, answers), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(answers[0].object_index, 7);
	XVT_ASSERT_INT_EQ(answers[0].notebook[0], 0x5A);
	XVT_ASSERT_INT_EQ(answers[0].notebook[AI_NOTEBOOK_BYTES - 1], 0x5A);
	XVT_ASSERT_INT_EQ(answers[0].actions, 0xF7FE);
	XVT_ASSERT_INT_EQ(answers[0].order_slot, 1);
	XVT_ASSERT_INT_EQ(answers[0].order_state, 2);
	XVT_ASSERT_INT_EQ(answers[0].steer_yaw, 0x1234);
	XVT_ASSERT_INT_EQ(answers[0].steer_pitch, 0x4000);
	XVT_ASSERT_INT_EQ(answers[0].steer_roll, 0xFFFF);
	XVT_ASSERT_INT_EQ(answers[0].throttle, 0x8000);
	XVT_ASSERT_INT_EQ(answers[0].activity, AI_ACTIVITY_DISABLED);
	XVT_ASSERT_INT_EQ(answers[0].think_interval, 59);
	XVT_ASSERT_INT_EQ(answers[0].target, 12);
	XVT_ASSERT_INT_EQ(answers[0].laser_mode[0], 3);
	XVT_ASSERT_INT_EQ(answers[0].laser_burst[0], 4);
	XVT_ASSERT_INT_EQ(answers[0].laser_mode[1], 1);
	XVT_ASSERT_INT_EQ(answers[0].laser_burst[1], 2);
	XVT_ASSERT_INT_EQ(answers[0].warhead_launcher, 0);
	XVT_ASSERT_INT_EQ(answers[0].warhead_paired, 1);
	XVT_ASSERT_INT_EQ(answers[0].turret_count, 2);
	XVT_ASSERT_INT_EQ(answers[0].turret_slot[0], 5);
	XVT_ASSERT_INT_EQ(answers[0].turret_target[0], UINT16_MAX);
	XVT_ASSERT_INT_EQ(answers[0].turret_slot[1], 3);
	XVT_ASSERT_INT_EQ(answers[0].turret_target[1], 12);
	XVT_ASSERT_INT_EQ(answers[0].turret_ion[0], 1);
	XVT_ASSERT_INT_EQ(answers[0].turret_ion[1], 0);
	XVT_ASSERT_INT_EQ(answers[0].maneuver_timer, 2147483647u);
	XVT_ASSERT_INT_EQ(answers[0].arriving, 1);
	XVT_ASSERT_INT_EQ(answers[0].speed, 3600);
	XVT_ASSERT_INT_EQ(answers[1].object_index, 9);
	XVT_ASSERT_INT_EQ(answers[1].notebook[0], 0xA5);
	XVT_ASSERT_INT_EQ(answers[1].actions, 0);
	XVT_ASSERT_INT_EQ(answers[1].throttle, 0);
	XVT_ASSERT_INT_EQ(answers[1].arriving, 0);
	XVT_ASSERT_INT_EQ(answers[1].speed, 0);
}

/* The good answer with the byte at offset set to value. */
static enum ai_wire_result read_with_byte(size_t offset, uint8_t value)
{
	struct test_bytes bytes;
	struct ai_wire_craft_answer answers[2];
	good_answer(&bytes);
	bytes.data[offset] = value;
	return read_two(&bytes, answers);
}

static void check_header_faults(void)
{
	XVT_ASSERT_INT_EQ(read_with_byte(4, 0x4B), AI_WIRE_BAD_HEADER);
	XVT_ASSERT_INT_EQ(read_with_byte(8, 1), AI_WIRE_BAD_HEADER);
	XVT_ASSERT_INT_EQ(read_with_byte(10, AI_WIRE_KIND_QUESTION),
			  AI_WIRE_BAD_HEADER);
	XVT_ASSERT_INT_EQ(read_with_byte(18, 1), AI_WIRE_BAD_HEADER);
	XVT_ASSERT_INT_EQ(read_with_byte(12, 0x0E), AI_WIRE_WRONG_TICK);
	XVT_ASSERT_INT_EQ(read_with_byte(16, 3), AI_WIRE_WRONG_COUNT);
	XVT_ASSERT_INT_EQ(read_with_byte(20, 8), AI_WIRE_WRONG_CRAFT);
}

static void check_length_faults(void)
{
	struct test_bytes bytes;
	struct ai_wire_craft_answer answers[2];
	good_answer(&bytes);
	--bytes.length;
	XVT_ASSERT_INT_EQ(read_two(&bytes, answers), AI_WIRE_SHORT);
	/* The length field agrees, but the last craft's action count is
	 * missing. */
	finish_answer(&bytes);
	XVT_ASSERT_INT_EQ(read_two(&bytes, answers), AI_WIRE_SHORT);
	good_answer(&bytes);
	add_u8(&bytes, 0);
	XVT_ASSERT_INT_EQ(read_two(&bytes, answers), AI_WIRE_LONG);
	finish_answer(&bytes);
	XVT_ASSERT_INT_EQ(read_two(&bytes, answers), AI_WIRE_LONG);
	XVT_ASSERT_INT_EQ(
		ai_wire_read_answer(bytes.data, 3, &g_question, answers),
		AI_WIRE_SHORT);
}

/* One craft, object 7, sending the action bytes given. */
static enum ai_wire_result
read_actions(unsigned int count, const uint8_t *actions, size_t action_bytes)
{
	struct test_bytes bytes;
	struct ai_wire_craft_answer answer;
	start_answer(&bytes, 1);
	add_craft(&bytes, 7, 0);
	add_u8(&bytes, count);
	memcpy(&bytes.data[bytes.length], actions, action_bytes);
	bytes.length += action_bytes;
	finish_answer(&bytes);
	struct ai_wire_question question = g_question;
	question.craft_count = 1;
	return ai_wire_read_answer(bytes.data, bytes.length, &question,
				   &answer);
}

/* A craft 7 whose only launcher is cooling down, sending the action bytes
 * given. */
static enum ai_wire_result read_actions_cooling(const uint8_t *actions,
						size_t action_bytes)
{
	g_views[0].launcher_cooldown[0] = 1;
	enum ai_wire_result result = read_actions(1, actions, action_bytes);
	g_views[0].launcher_cooldown[0] = 0;
	return result;
}

/* Each new action kind against what the question showed: craft 7 has
 * cannon groups 0 and 1, launcher 0 and turrets in slots 3 and 5, and the
 * contacts are 7, 9 and 12. */
static void check_shown_faults(void)
{
	static const uint8_t target_none[] = {AI_WIRE_ACTION_TARGET, 0xFF,
					      0xFF};
	static const uint8_t target_9[] = {AI_WIRE_ACTION_TARGET, 9, 0};
	static const uint8_t target_11[] = {AI_WIRE_ACTION_TARGET, 11, 0};
	static const uint8_t lasers_ok[] = {AI_WIRE_ACTION_LASERS, 1, 1, 0, 0};
	static const uint8_t mode_4[] = {AI_WIRE_ACTION_LASERS, 4, 1, 0, 0};
	static const uint8_t mode_no_burst[] = {AI_WIRE_ACTION_LASERS, 1, 0, 0,
						0};
	static const uint8_t burst_no_mode[] = {AI_WIRE_ACTION_LASERS, 0, 5, 0,
						0};
	static const uint8_t warhead_ok[] = {AI_WIRE_ACTION_WARHEAD, 0, 1};
	static const uint8_t launcher_1[] = {AI_WIRE_ACTION_WARHEAD, 1, 0};
	static const uint8_t launcher_2[] = {AI_WIRE_ACTION_WARHEAD, 2, 0};
	static const uint8_t paired_2[] = {AI_WIRE_ACTION_WARHEAD, 0, 2};
	static const uint8_t turret_ok[] = {
		AI_WIRE_ACTION_TURRETS, 1, 3, 9, 0, 0};
	static const uint8_t turrets_0[] = {AI_WIRE_ACTION_TURRETS, 0};
	static const uint8_t turrets_17[] = {AI_WIRE_ACTION_TURRETS, 17};
	static const uint8_t not_turret[] = {
		AI_WIRE_ACTION_TURRETS, 1, 4, 0xFF, 0xFF, 0};
	static const uint8_t slot_16[] = {
		AI_WIRE_ACTION_TURRETS, 1, 16, 0xFF, 0xFF, 0};
	static const uint8_t slot_twice[] = {
		AI_WIRE_ACTION_TURRETS, 2, 3, 0xFF, 0xFF, 0, 3, 0xFF, 0xFF, 0};
	static const uint8_t turret_11[] = {
		AI_WIRE_ACTION_TURRETS, 1, 3, 11, 0, 0};
	XVT_ASSERT_INT_EQ(read_actions(1, target_none, sizeof target_none),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, target_9, sizeof target_9),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, target_11, sizeof target_11),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, lasers_ok, sizeof lasers_ok),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, mode_4, sizeof mode_4),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, mode_no_burst, sizeof mode_no_burst),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, burst_no_mode, sizeof burst_no_mode),
			  AI_WIRE_BAD_ACTION);
	g_views[0].cannon_group_count = 1;
	XVT_ASSERT_INT_EQ(read_actions(1, lasers_ok, sizeof lasers_ok),
			  AI_WIRE_OK);
	static const uint8_t group_1[] = {AI_WIRE_ACTION_LASERS, 0, 0, 1, 1};
	XVT_ASSERT_INT_EQ(read_actions(1, group_1, sizeof group_1),
			  AI_WIRE_BAD_ACTION);
	g_views[0].cannon_group_count = 2;
	XVT_ASSERT_INT_EQ(read_actions(1, group_1, sizeof group_1), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, warhead_ok, sizeof warhead_ok),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions_cooling(warhead_ok, sizeof warhead_ok),
			  AI_WIRE_BAD_ACTION);
	/* An empty launcher: the engine would spend 472 ticks of cooldown and
	 * fly nothing. */
	g_views[0].warheads_left[0] = 0;
	XVT_ASSERT_INT_EQ(read_actions(1, warhead_ok, sizeof warhead_ok),
			  AI_WIRE_BAD_ACTION);
	g_views[0].warheads_left[0] = 2;
	XVT_ASSERT_INT_EQ(read_actions(1, launcher_1, sizeof launcher_1),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, launcher_2, sizeof launcher_2),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, paired_2, sizeof paired_2),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, turret_ok, sizeof turret_ok),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, turrets_0, sizeof turrets_0),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, turrets_17, sizeof turrets_17),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, not_turret, sizeof not_turret),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, slot_16, sizeof slot_16),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, slot_twice, sizeof slot_twice),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, turret_11, sizeof turret_11),
			  AI_WIRE_BAD_ACTION);
	/* A turret cut off by the end of the message. */
	XVT_ASSERT_INT_EQ(read_actions(1, turret_ok, sizeof turret_ok - 1),
			  AI_WIRE_SHORT);
}

static void check_action_faults(void)
{
	static const uint8_t kind_zero[] = {0};
	static const uint8_t kind_end[] = {16};
	static const uint8_t twice[] = {AI_WIRE_ACTION_THROTTLE, 1, 0,
					AI_WIRE_ACTION_THROTTLE, 2, 0};
	static const uint8_t activity_end[] = {AI_WIRE_ACTION_ACTIVITY,
					       AI_ACTIVITY_COUNT};
	static const uint8_t activity_last[] = {AI_WIRE_ACTION_ACTIVITY,
						AI_ACTIVITY_COUNT - 1};
	/* The two activities only the engine sets. */
	static const uint8_t activity_arriving[] = {
		AI_WIRE_ACTION_ACTIVITY, AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE};
	static const uint8_t activity_leaving[] = {AI_WIRE_ACTION_ACTIVITY,
						   AI_ACTIVITY_LEAVING_HANGAR};
	static const uint8_t interval_zero[] = {AI_WIRE_ACTION_THINK_INTERVAL,
						0, 0};
	static const uint8_t interval_one[] = {AI_WIRE_ACTION_THINK_INTERVAL, 1,
					       0};
	/* Arriving 2; speed 3,601; arriving 0 at speed 3,600. */
	static const uint8_t arriving_two[] = {AI_WIRE_ACTION_ARRIVAL, 2, 0, 0};
	static const uint8_t too_fast[] = {AI_WIRE_ACTION_ARRIVAL, 1, 0x11,
					   0x0E};
	static const uint8_t fastest[] = {AI_WIRE_ACTION_ARRIVAL, 0, 0x10,
					  0x0E};
	XVT_ASSERT_INT_EQ(read_actions(1, kind_zero, sizeof kind_zero),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, kind_end, sizeof kind_end),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(2, twice, sizeof twice),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, activity_end, sizeof activity_end),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, activity_last, sizeof activity_last),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(
		read_actions(1, activity_arriving, sizeof activity_arriving),
		AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(
		read_actions(1, activity_leaving, sizeof activity_leaving),
		AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, interval_zero, sizeof interval_zero),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, interval_one, sizeof interval_one),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, arriving_two, sizeof arriving_two),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, too_fast, sizeof too_fast),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, fastest, sizeof fastest), AI_WIRE_OK);
	/* An action cut off by the end of the message. */
	XVT_ASSERT_INT_EQ(read_actions(1, twice, 2), AI_WIRE_SHORT);
}

/* The order, depart and dock actions against craft 7's view: an order to
 * its slot or to a slot with an order, to slot 3 only with the skip due,
 * none to another slot once skipped or from a follower, a state of 0, 2 or
 * 3; a depart only when it may jump, a dock only at its hangar, never
 * both. */
static void check_books_faults(void)
{
	static const uint8_t order_1[] = {AI_WIRE_ACTION_ORDER, 1, 2};
	static const uint8_t order_same[] = {AI_WIRE_ACTION_ORDER, 0, 3};
	static const uint8_t order_empty[] = {AI_WIRE_ACTION_ORDER, 2, 0};
	static const uint8_t order_3[] = {AI_WIRE_ACTION_ORDER, 3, 0};
	static const uint8_t order_4[] = {AI_WIRE_ACTION_ORDER, 4, 0};
	static const uint8_t state_1[] = {AI_WIRE_ACTION_ORDER, 1, 1};
	static const uint8_t depart[] = {AI_WIRE_ACTION_DEPART};
	static const uint8_t dock[] = {AI_WIRE_ACTION_DOCK};
	static const uint8_t both[] = {AI_WIRE_ACTION_DEPART,
				       AI_WIRE_ACTION_DOCK};
	struct ai_wire_craft_view *view = &g_views[0];
	XVT_ASSERT_INT_EQ(read_actions(1, order_1, sizeof order_1), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, order_same, sizeof order_same),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, order_empty, sizeof order_empty),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, order_3, sizeof order_3),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, order_4, sizeof order_4),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, state_1, sizeof state_1),
			  AI_WIRE_BAD_ACTION);
	view->books |= AI_WIRE_BOOKS_SKIP_DUE;
	XVT_ASSERT_INT_EQ(read_actions(1, order_3, sizeof order_3), AI_WIRE_OK);
	view->books = AI_WIRE_BOOKS_SKIPPED | AI_WIRE_BOOKS_AT_HANGAR;
	view->order_slot = 3;
	XVT_ASSERT_INT_EQ(read_actions(1, order_1, sizeof order_1),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, order_3, sizeof order_3), AI_WIRE_OK);
	view->order_slot = 0;
	view->books = AI_WIRE_BOOKS_AT_HANGAR;
	view->leader = 9;
	XVT_ASSERT_INT_EQ(read_actions(1, order_1, sizeof order_1),
			  AI_WIRE_BAD_ACTION);
	view->leader = UINT16_MAX;
	XVT_ASSERT_INT_EQ(read_actions(1, depart, sizeof depart),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, dock, sizeof dock), AI_WIRE_OK);
	view->books = AI_WIRE_BOOKS_MAY_JUMP;
	XVT_ASSERT_INT_EQ(read_actions(1, depart, sizeof depart), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, dock, sizeof dock),
			  AI_WIRE_BAD_ACTION);
	view->books |= AI_WIRE_BOOKS_AT_HANGAR;
	XVT_ASSERT_INT_EQ(read_actions(2, both, sizeof both),
			  AI_WIRE_BAD_ACTION);
	view->books = AI_WIRE_BOOKS_AT_HANGAR;
	/* An order cut off by the end of the message. */
	XVT_ASSERT_INT_EQ(read_actions(1, order_1, 2), AI_WIRE_SHORT);
}

/* A countermeasure goes through only while the view shows it ready: chaff
 * with no chaff seconds left, or a flare with its cooldown at 0, either
 * working with one left; nothing else launches. */
static void check_countermeasure_faults(void)
{
	static const uint8_t launch[] = {AI_WIRE_ACTION_COUNTERMEASURE};
	struct ai_wire_craft_view *view = &g_views[0];
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch), AI_WIRE_OK);
	view->countermeasure_cooldown = 1;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch),
			  AI_WIRE_BAD_ACTION);
	view->countermeasure_cooldown = 0;
	view->countermeasures_left = 0;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch),
			  AI_WIRE_BAD_ACTION);
	view->countermeasures_left = 3;
	view->countermeasure_working = 0;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch),
			  AI_WIRE_BAD_ACTION);
	view->countermeasure_working = 1;
	/* A flare's chaff seconds do not matter; chaff's cooldown does not. */
	view->chaff_seconds = 4;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch), AI_WIRE_OK);
	view->countermeasure = AI_WIRE_COUNTERMEASURE_CHAFF;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch),
			  AI_WIRE_BAD_ACTION);
	view->chaff_seconds = 0;
	view->countermeasure_cooldown = 1;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch), AI_WIRE_OK);
	view->countermeasure_cooldown = 0;
	view->countermeasure = AI_WIRE_COUNTERMEASURE_NONE;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch),
			  AI_WIRE_BAD_ACTION);
	/* A cluster mine, the engine's next type, is not one to launch. */
	view->countermeasure = 3;
	XVT_ASSERT_INT_EQ(read_actions(1, launch, sizeof launch),
			  AI_WIRE_BAD_ACTION);
	view->countermeasure = AI_WIRE_COUNTERMEASURE_FLARE;
}

/* An evaded goes through only while the view shows the evade command; the
 * wait command alone does not let it through. */
static void check_evaded_faults(void)
{
	static const uint8_t evaded[] = {AI_WIRE_ACTION_EVADED};
	struct ai_wire_craft_view *view = &g_views[0];
	XVT_ASSERT_INT_EQ(read_actions(1, evaded, sizeof evaded), AI_WIRE_OK);
	view->commands = AI_WIRE_COMMAND_WAIT;
	XVT_ASSERT_INT_EQ(read_actions(1, evaded, sizeof evaded),
			  AI_WIRE_BAD_ACTION);
	view->commands = 0;
	XVT_ASSERT_INT_EQ(read_actions(1, evaded, sizeof evaded),
			  AI_WIRE_BAD_ACTION);
	view->commands = AI_WIRE_COMMAND_EVADE;
}

/* A turret's ion switch is 0 or 1 and nothing else, and sets the answer's
 * ion byte. */
static void check_ion_faults(void)
{
	static const uint8_t ion_1[] = {AI_WIRE_ACTION_TURRETS, 1, 3, 9, 0, 1};
	static const uint8_t ion_2[] = {AI_WIRE_ACTION_TURRETS, 1, 3, 9, 0, 2};
	static const uint8_t ion_255[] = {
		AI_WIRE_ACTION_TURRETS, 1, 3, 9, 0, 255};
	static const uint8_t second_ion_2[] = {
		AI_WIRE_ACTION_TURRETS, 2, 3, 9, 0, 1, 5, 9, 0, 2};
	XVT_ASSERT_INT_EQ(read_actions(1, ion_1, sizeof ion_1), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, ion_2, sizeof ion_2),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, ion_255, sizeof ion_255),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, second_ion_2, sizeof second_ion_2),
			  AI_WIRE_BAD_ACTION);
	/* The ion byte cut off by the end of the message. */
	XVT_ASSERT_INT_EQ(read_actions(1, ion_1, sizeof ion_1 - 1),
			  AI_WIRE_SHORT);
}

/* The maneuver timer action is a u32 of 0 to 0x7FFFFFFF ticks, little
 * endian, read into the answer. */
static void check_maneuver_timer_faults(void)
{
	static const uint8_t zero[] = {AI_WIRE_ACTION_MANEUVER_TIMER, 0, 0, 0,
				       0};
	static const uint8_t most[] = {AI_WIRE_ACTION_MANEUVER_TIMER, 0xFF,
				       0xFF, 0xFF, 0x7F};
	static const uint8_t over[] = {AI_WIRE_ACTION_MANEUVER_TIMER, 0, 0, 0,
				       0x80};
	static const uint8_t ones[] = {AI_WIRE_ACTION_MANEUVER_TIMER, 0xFF,
				       0xFF, 0xFF, 0xFF};
	static const uint8_t ordered[] = {AI_WIRE_ACTION_MANEUVER_TIMER, 1, 2,
					  3, 4};
	XVT_ASSERT_INT_EQ(read_actions(1, zero, sizeof zero), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, most, sizeof most), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(read_actions(1, over, sizeof over),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, ones, sizeof ones),
			  AI_WIRE_BAD_ACTION);
	/* The timer cut off by the end of the message. */
	XVT_ASSERT_INT_EQ(read_actions(1, most, sizeof most - 1),
			  AI_WIRE_SHORT);
	struct test_bytes bytes;
	struct ai_wire_craft_answer answer;
	start_answer(&bytes, 1);
	add_craft(&bytes, 7, 0);
	add_u8(&bytes, 1);
	memcpy(&bytes.data[bytes.length], ordered, sizeof ordered);
	bytes.length += sizeof ordered;
	finish_answer(&bytes);
	struct ai_wire_question question = g_question;
	question.craft_count = 1;
	XVT_ASSERT_INT_EQ(ai_wire_read_answer(bytes.data, bytes.length,
					      &question, &answer),
			  AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(answer.maneuver_timer, 0x04030201);
	XVT_ASSERT_INT_EQ(answer.actions, 1u << 15);
}

int main(void)
{
	check_question_bytes();
	check_question_refusals();
	check_good_answer();
	check_header_faults();
	check_length_faults();
	check_action_faults();
	check_shown_faults();
	check_books_faults();
	check_countermeasure_faults();
	check_evaded_faults();
	check_ion_faults();
	check_maneuver_timer_faults();
	return 0;
}

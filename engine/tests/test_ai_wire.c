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
		0x0102, 0x0304, 0x05,	0x06,	0x07,	    0x08, 0x09,
		0x0A,	0x0C0B, 0x0E0D, 0x100F, 0x14131211, -2,	  0x1C1B1A19,
		0x1E1D, 0x201F, 0x2221, 0x2423, -3,	    {0}};
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		view.notebook[byte] = (uint8_t)(0x40 + byte);
	}
	return view;
}

static void check_question_bytes(void)
{
	static const uint8_t expected[AI_WIRE_HEAD_BYTES + 38] = {
		0x76, 0x00, 0x00, 0x00, 0x4A, 0x4D, 0x50, 0x57, 0x01, 0x00,
		0x01, 0x00, 0x0D, 0x0C, 0x0B, 0x0A, 0x01, 0x00, 0x00, 0x00,
		0x02, 0x01, 0x04, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
		0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
		0xFE, 0xFF, 0xFF, 0xFF, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
		0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0xFD, 0xFF};
	struct ai_wire_craft_view view = view_with_distinct_bytes();
	uint8_t out[AI_WIRE_HEAD_BYTES + AI_WIRE_CRAFT_VIEW_BYTES];
	XVT_ASSERT_INT_EQ(ai_wire_question_bytes(1), sizeof out);
	XVT_ASSERT_INT_EQ(
		ai_wire_write_question(out, sizeof out, TEST_TICK, &view, 1),
		sizeof out);
	for (size_t byte = 0; byte < sizeof expected; ++byte) {
		XVT_ASSERT_INT_EQ(out[byte], expected[byte]);
	}
	for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
		XVT_ASSERT_INT_EQ(out[sizeof expected + (size_t)byte],
				  0x40 + byte);
	}
}

static void check_question_refusals(void)
{
	struct ai_wire_craft_view view = view_with_distinct_bytes();
	uint8_t out[AI_WIRE_HEAD_BYTES + AI_WIRE_CRAFT_VIEW_BYTES];
	memset(out, 0xEE, sizeof out);
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out - 1, TEST_TICK,
						 &view, 1),
			  0);
	XVT_ASSERT_INT_EQ(out[0], 0xEE);
	XVT_ASSERT_INT_EQ(
		ai_wire_write_question(out, sizeof out, TEST_TICK, &view, -1),
		0);
	XVT_ASSERT_INT_EQ(ai_wire_write_question(out, sizeof out, TEST_TICK,
						 &view, UINT16_MAX + 1),
			  0);
	/* No craft due: the header alone. */
	XVT_ASSERT_INT_EQ(
		ai_wire_write_question(out, sizeof out, TEST_TICK, &view, 0),
		AI_WIRE_HEAD_BYTES);
	XVT_ASSERT_INT_EQ(out[0], AI_WIRE_HEAD_BYTES - 4);
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

static struct ai_wire_craft_view g_views[2] = {{.object_index = 7},
					       {.object_index = 9}};

/* Craft 7 sends every action kind, last to first; craft 9 sends none. */
static void good_answer(struct test_bytes *bytes)
{
	start_answer(bytes, 2);
	add_craft(bytes, 7, 0x5A);
	add_u8(bytes, 4);
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
	return ai_wire_read_answer(bytes->data, bytes->length, TEST_TICK,
				   g_views, 2, answers);
}

static void check_good_answer(void)
{
	struct test_bytes bytes;
	struct ai_wire_craft_answer answers[2];
	good_answer(&bytes);
	XVT_ASSERT_INT_EQ(bytes.length, AI_WIRE_HEAD_BYTES +
						AI_WIRE_CRAFT_ANSWER_BYTES_MAX +
						3 + AI_NOTEBOOK_BYTES);
	XVT_ASSERT_INT_EQ(read_two(&bytes, answers), AI_WIRE_OK);
	XVT_ASSERT_INT_EQ(answers[0].object_index, 7);
	XVT_ASSERT_INT_EQ(answers[0].notebook[0], 0x5A);
	XVT_ASSERT_INT_EQ(answers[0].notebook[AI_NOTEBOOK_BYTES - 1], 0x5A);
	XVT_ASSERT_INT_EQ(answers[0].actions, 0x1E);
	XVT_ASSERT_INT_EQ(answers[0].steer_yaw, 0x1234);
	XVT_ASSERT_INT_EQ(answers[0].steer_pitch, 0x4000);
	XVT_ASSERT_INT_EQ(answers[0].steer_roll, 0xFFFF);
	XVT_ASSERT_INT_EQ(answers[0].throttle, 0x8000);
	XVT_ASSERT_INT_EQ(answers[0].activity, AI_ACTIVITY_DISABLED);
	XVT_ASSERT_INT_EQ(answers[0].think_interval, 59);
	XVT_ASSERT_INT_EQ(answers[1].object_index, 9);
	XVT_ASSERT_INT_EQ(answers[1].notebook[0], 0xA5);
	XVT_ASSERT_INT_EQ(answers[1].actions, 0);
	XVT_ASSERT_INT_EQ(answers[1].throttle, 0);
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
	XVT_ASSERT_INT_EQ(read_with_byte(8, 2), AI_WIRE_BAD_HEADER);
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
	XVT_ASSERT_INT_EQ(ai_wire_read_answer(bytes.data, 3, TEST_TICK, g_views,
					      2, answers),
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
	return ai_wire_read_answer(bytes.data, bytes.length, TEST_TICK, g_views,
				   1, &answer);
}

static void check_action_faults(void)
{
	static const uint8_t kind_zero[] = {0};
	static const uint8_t kind_end[] = {AI_WIRE_ACTION_KIND_END};
	static const uint8_t twice[] = {AI_WIRE_ACTION_THROTTLE, 1, 0,
					AI_WIRE_ACTION_THROTTLE, 2, 0};
	static const uint8_t activity_end[] = {AI_WIRE_ACTION_ACTIVITY,
					       AI_ACTIVITY_COUNT};
	static const uint8_t activity_last[] = {AI_WIRE_ACTION_ACTIVITY,
						AI_ACTIVITY_COUNT - 1};
	static const uint8_t interval_zero[] = {AI_WIRE_ACTION_THINK_INTERVAL,
						0, 0};
	static const uint8_t interval_one[] = {AI_WIRE_ACTION_THINK_INTERVAL, 1,
					       0};
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
	XVT_ASSERT_INT_EQ(read_actions(1, interval_zero, sizeof interval_zero),
			  AI_WIRE_BAD_ACTION);
	XVT_ASSERT_INT_EQ(read_actions(1, interval_one, sizeof interval_one),
			  AI_WIRE_OK);
	/* An action cut off by the end of the message. */
	XVT_ASSERT_INT_EQ(read_actions(1, twice, 2), AI_WIRE_SHORT);
}

int main(void)
{
	check_question_bytes();
	check_question_refusals();
	check_good_answer();
	check_header_faults();
	check_length_faults();
	check_action_faults();
	return 0;
}

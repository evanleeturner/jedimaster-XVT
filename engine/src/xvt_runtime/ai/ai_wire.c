#include "xvt_runtime/ai/ai_wire.h"

#include <string.h>

static uint8_t *put_u16(uint8_t *at, unsigned int value)
{
	at[0] = (uint8_t)value;
	at[1] = (uint8_t)(value >> 8);
	return at + 2;
}

static uint8_t *put_u32(uint8_t *at, uint32_t value)
{
	at = put_u16(at, value & 0xFFFFu);
	return put_u16(at, value >> 16);
}

static uint8_t *put_craft_view(uint8_t *at,
			       const struct ai_wire_craft_view *view)
{
	at = put_u16(at, view->object_index);
	at = put_u16(at, view->object_signature);
	*at++ = view->object_type;
	*at++ = view->flight_group;
	*at++ = view->order_code;
	*at++ = view->order_slot;
	*at++ = view->activity;
	*at++ = view->team;
	at = put_u16(at, view->skill);
	at = put_u16(at, view->think_interval);
	at = put_u16(at, view->throttle);
	at = put_u32(at, (uint32_t)view->x);
	at = put_u32(at, (uint32_t)view->y);
	at = put_u32(at, (uint32_t)view->z);
	at = put_u16(at, view->yaw);
	at = put_u16(at, view->pitch);
	at = put_u16(at, view->roll);
	at = put_u16(at, view->speed);
	at = put_u16(at, (uint16_t)view->random_seed);
	memcpy(at, view->notebook, sizeof view->notebook);
	return at + sizeof view->notebook;
}

size_t ai_wire_question_bytes(int count)
{
	return AI_WIRE_HEAD_BYTES + (size_t)count * AI_WIRE_CRAFT_VIEW_BYTES;
}

size_t ai_wire_write_question(uint8_t *out, size_t capacity, uint32_t tick,
			      const struct ai_wire_craft_view *craft, int count)
{
	if (count < 0 || count > UINT16_MAX) {
		return 0;
	}
	size_t total = ai_wire_question_bytes(count);
	if (total > capacity) {
		return 0;
	}
	uint8_t *at = put_u32(out, (uint32_t)(total - 4));
	at = put_u32(at, AI_WIRE_MAGIC);
	at = put_u16(at, AI_WIRE_VERSION);
	at = put_u16(at, AI_WIRE_KIND_QUESTION);
	at = put_u32(at, tick);
	at = put_u16(at, (unsigned int)count);
	at = put_u16(at, 0);
	for (int index = 0; index < count; ++index) {
		at = put_craft_view(at, &craft[index]);
	}
	return total;
}

/* Reads from at up to end. A take past end sets short_read and gives 0. */
struct wire_reader {
	const uint8_t *at;
	const uint8_t *end;
	int short_read;
};

static int reader_has(struct wire_reader *reader, size_t bytes)
{
	if ((size_t)(reader->end - reader->at) < bytes) {
		reader->short_read = 1;
		reader->at = reader->end;
		return 0;
	}
	return 1;
}

static unsigned int take_u8(struct wire_reader *reader)
{
	if (!reader_has(reader, 1)) {
		return 0;
	}
	return *reader->at++;
}

static unsigned int take_u16(struct wire_reader *reader)
{
	if (!reader_has(reader, 2)) {
		return 0;
	}
	unsigned int value = reader->at[0] | (unsigned int)reader->at[1] << 8;
	reader->at += 2;
	return value;
}

static uint32_t take_u32(struct wire_reader *reader)
{
	uint32_t low = take_u16(reader);
	return low | (uint32_t)take_u16(reader) << 16;
}

static void take_bytes(struct wire_reader *reader, uint8_t *out, size_t bytes)
{
	if (reader_has(reader, bytes)) {
		memcpy(out, reader->at, bytes);
		reader->at += bytes;
	}
}

/* Reads one action into answer; returns 0 for a kind that is unknown or
 * already sent, or a value out of range. */
static int take_action(struct wire_reader *reader,
		       struct ai_wire_craft_answer *answer)
{
	unsigned int kind = take_u8(reader);
	if (kind == 0 || kind >= AI_WIRE_ACTION_KIND_END ||
	    (answer->actions & (1u << kind)) != 0) {
		return reader->short_read;
	}
	answer->actions |= 1u << kind;
	switch (kind) {
	case AI_WIRE_ACTION_STEER:
		answer->steer_yaw = (uint16_t)take_u16(reader);
		answer->steer_pitch = (uint16_t)take_u16(reader);
		answer->steer_roll = (uint16_t)take_u16(reader);
		return 1;
	case AI_WIRE_ACTION_THROTTLE:
		answer->throttle = (uint16_t)take_u16(reader);
		return 1;
	case AI_WIRE_ACTION_ACTIVITY:
		answer->activity = (uint8_t)take_u8(reader);
		return reader->short_read ||
		       answer->activity < AI_ACTIVITY_COUNT;
	default:
		answer->think_interval = (uint16_t)take_u16(reader);
		return reader->short_read || answer->think_interval != 0;
	}
}

static enum ai_wire_result
take_craft_answer(struct wire_reader *reader,
		  const struct ai_wire_craft_view *view,
		  struct ai_wire_craft_answer *answer)
{
	memset(answer, 0, sizeof *answer);
	answer->object_index = (uint16_t)take_u16(reader);
	if (!reader->short_read && answer->object_index != view->object_index) {
		return AI_WIRE_WRONG_CRAFT;
	}
	take_bytes(reader, answer->notebook, sizeof answer->notebook);
	unsigned int action_count = take_u8(reader);
	for (unsigned int action = 0; action < action_count; ++action) {
		if (!take_action(reader, answer)) {
			return AI_WIRE_BAD_ACTION;
		}
	}
	return reader->short_read ? AI_WIRE_SHORT : AI_WIRE_OK;
}

static enum ai_wire_result take_head(struct wire_reader *reader, uint32_t tick,
				     int count)
{
	uint32_t magic = take_u32(reader);
	unsigned int version = take_u16(reader);
	unsigned int kind = take_u16(reader);
	uint32_t answer_tick = take_u32(reader);
	unsigned int answer_count = take_u16(reader);
	unsigned int zero = take_u16(reader);
	if (reader->short_read) {
		return AI_WIRE_SHORT;
	}
	if (magic != AI_WIRE_MAGIC || version != AI_WIRE_VERSION ||
	    kind != AI_WIRE_KIND_ANSWER || zero != 0) {
		return AI_WIRE_BAD_HEADER;
	}
	if (answer_tick != tick) {
		return AI_WIRE_WRONG_TICK;
	}
	return answer_count == (unsigned int)count ? AI_WIRE_OK
						   : AI_WIRE_WRONG_COUNT;
}

enum ai_wire_result ai_wire_read_answer(const uint8_t *in, size_t length,
					uint32_t tick,
					const struct ai_wire_craft_view *craft,
					int count,
					struct ai_wire_craft_answer *answers)
{
	struct wire_reader reader = {in, in + length, 0};
	uint32_t body = take_u32(&reader);
	if (reader.short_read || body > length - 4) {
		return AI_WIRE_SHORT;
	}
	if (body < length - 4) {
		return AI_WIRE_LONG;
	}
	enum ai_wire_result result = take_head(&reader, tick, count);
	for (int index = 0; result == AI_WIRE_OK && index < count; ++index) {
		result = take_craft_answer(&reader, &craft[index],
					   &answers[index]);
	}
	if (result == AI_WIRE_OK && reader.at != reader.end) {
		return AI_WIRE_LONG;
	}
	return result;
}

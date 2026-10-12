#ifndef XVT_RUNTIME_AI_AI_WIRE_H
#define XVT_RUNTIME_AI_AI_WIRE_H

/* The byte form of the messages between the engine and the pilot program,
 * the separate program that decides what computer craft do. On each
 * reference step that has craft due to think, the engine writes one question
 * and reads one answer. Both are little-endian on every machine and carry no
 * pointers, so the machines of a network game send their programs the same
 * bytes. This file turns structs into bytes and back; it reads no game state.
 *
 * Every message starts with a u32, the count of bytes after it, then the
 * header: u32 AI_WIRE_MAGIC, u16 AI_WIRE_VERSION, u16 kind, u32 tick,
 * u16 craft count, u16 zero.
 *
 * A question (kind 1) holds one craft view per craft due, each
 * AI_WIRE_CRAFT_VIEW_BYTES long: u16 object index, u16 object signature,
 * u8 object type, u8 flight group, u8 order code, u8 order slot, u8 activity,
 * u8 team, u16 skill, u16 think interval, u16 throttle, i32 x, i32 y, i32 z,
 * u16 yaw, u16 pitch, u16 roll, u16 speed, i16 random seed, then the
 * notebook's AI_NOTEBOOK_BYTES.
 *
 * An answer (kind 2) holds one craft answer per craft, in the question's
 * order: u16 object index, the new notebook, u8 action count, then each
 * action as a u8 kind and its fields:
 *   1 steer: u16 yaw, u16 pitch, u16 roll to turn to
 *   2 throttle: u16, a fraction of 65,536
 *   3 activity: u8, an AI_ACTIVITY_ value
 *   4 think interval: u16 ticks, 1 or more
 * A craft answer names each action kind at most once. */

#include <stddef.h>
#include <stdint.h>

#include "xvt/flight/ai_state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ai_wire_constant {
	AI_WIRE_MAGIC = 0x57504D4A, /* "JMPW" */
	AI_WIRE_VERSION = 1,
	AI_WIRE_KIND_QUESTION = 1,
	AI_WIRE_KIND_ANSWER = 2,
	/* The length field and the header. */
	AI_WIRE_HEAD_BYTES = 20,
	AI_WIRE_CRAFT_VIEW_BYTES = 38 + AI_NOTEBOOK_BYTES,
	/* A craft answer with every action kind once. */
	AI_WIRE_CRAFT_ANSWER_BYTES_MAX = 18 + AI_NOTEBOOK_BYTES,
};

enum ai_wire_action_kind {
	AI_WIRE_ACTION_STEER = 1,
	AI_WIRE_ACTION_THROTTLE = 2,
	AI_WIRE_ACTION_ACTIVITY = 3,
	AI_WIRE_ACTION_THINK_INTERVAL = 4,
	AI_WIRE_ACTION_KIND_END = 5,
};

/* What the question tells the program about one craft due to think. */
struct ai_wire_craft_view {
	uint16_t object_index;
	uint16_t object_signature;
	uint8_t object_type;
	uint8_t flight_group;
	uint8_t order_code;
	uint8_t order_slot;
	uint8_t activity;
	uint8_t team;
	uint16_t skill;
	uint16_t think_interval;
	uint16_t throttle;
	int32_t x;
	int32_t y;
	int32_t z;
	uint16_t yaw;
	uint16_t pitch;
	uint16_t roll;
	uint16_t speed;
	int16_t random_seed;
	uint8_t notebook[AI_NOTEBOOK_BYTES];
};

/* One craft's answer as read. Bit (1 << kind) of actions is set for each
 * action kind the program sent; the fields of the other kinds are 0. */
struct ai_wire_craft_answer {
	uint16_t object_index;
	uint8_t notebook[AI_NOTEBOOK_BYTES];
	unsigned int actions;
	uint16_t steer_yaw;
	uint16_t steer_pitch;
	uint16_t steer_roll;
	uint16_t throttle;
	uint8_t activity;
	uint16_t think_interval;
};

/* Why an answer was refused. The reader stops at the first fault. */
enum ai_wire_result {
	AI_WIRE_OK = 0,
	/* Fewer bytes than the length field or the content needs. */
	AI_WIRE_SHORT,
	/* Bytes left over after the last craft answer. */
	AI_WIRE_LONG,
	/* The magic, version or kind is not an answer's, or the zero is not. */
	AI_WIRE_BAD_HEADER,
	AI_WIRE_WRONG_TICK,
	AI_WIRE_WRONG_COUNT,
	/* A craft answer's object index is not the question's craft there. */
	AI_WIRE_WRONG_CRAFT,
	/* An unknown action kind, a kind sent twice, an activity at or past
	 * AI_ACTIVITY_COUNT, or a think interval of 0. */
	AI_WIRE_BAD_ACTION,
};

/* Bytes in a question for count craft, the length field included. */
size_t ai_wire_question_bytes(int count);

/* Writes the question about count craft at tick into out. Returns the
 * bytes written, ai_wire_question_bytes(count), or 0 and writes nothing
 * when count is outside 0 to UINT16_MAX or capacity is short. */
size_t ai_wire_write_question(uint8_t *out, size_t capacity, uint32_t tick,
			      const struct ai_wire_craft_view *craft,
			      int count);

/* Reads the length bytes at in as the answer to the question about count
 * craft at tick. On AI_WIRE_OK answers[0] to answers[count - 1] hold the
 * craft answers in craft's order; on a fault their content is unspecified
 * and none may be applied. */
enum ai_wire_result ai_wire_read_answer(const uint8_t *in, size_t length,
					uint32_t tick,
					const struct ai_wire_craft_view *craft,
					int count,
					struct ai_wire_craft_answer *answers);

#ifdef __cplusplus
}
#endif

#endif

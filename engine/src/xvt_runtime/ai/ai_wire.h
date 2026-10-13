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
 * u16 yaw, u16 pitch, u16 roll, u16 speed, i16 random seed, u32 order target
 * mask (bit n set when contact n matches the targets of the craft's order),
 * u16 turret slot mask (bit n set when weapon slot n is a turret), u8 cannon
 * group count, u8 warhead launcher count, u8 cannon type[2] (each group's shot
 * object type), u8 warhead type[2], u8 warheads left[2], u16 launcher
 * cooldown[2] (ticks before each launcher may fire), then the notebook's
 * AI_NOTEBOOK_BYTES. Then u16 contact count, u16 zero, and one contact per
 * craft in the active region, at most AI_WIRE_CONTACTS_MAX, each
 * AI_WIRE_CONTACT_BYTES long: u16 object index, u16 object signature,
 * u8 object type, u8 genus, u8 team, u8 flight group, u8 object kind, u8 flags
 * (bit 0: a player flies it), i32 x, i32 y, i32 z, u16 yaw, u16 pitch,
 * u16 speed, u16 target, u16 last attacker (both 0xFFFF for none), i32 front
 * shield, i32 rear shield, u32 hull damage, u32 hull strength.
 *
 * An answer (kind 2) holds one craft answer per craft, in the question's
 * order: u16 object index, the new notebook, u8 action count, then each
 * action as a u8 kind and its fields:
 *   1 steer: u16 yaw, u16 pitch, u16 roll to turn to
 *   2 throttle: u16, a fraction of 65,536
 *   3 activity: u8, an AI_ACTIVITY_ value
 *   4 think interval: u16 ticks, 1 or more
 *   5 target: u16 object index, a contact's or 0xFFFF for none
 *   6 lasers: for cannon group 0 then 1, u8 link mode (0 holds fire, 1 to 3
 *     fire one cannon at a time, pairs, all) and u8 burst shots, 1 or more
 *     with a mode and 0 without; a group the view does not count holds 0, 0
 *   7 warhead: u8 launcher, one the view counts and shows ready (cooldown
 *     0), and u8 paired (1 fires both its slots at once, else 0)
 *   8 turrets: u8 count, 1 to 16, then per turret u8 weapon slot, one the
 *     view's turret mask names, each once, and u16 target, a contact's or
 *     0xFFFF for none
 * A craft answer names each action kind at most once, and names only what
 * the question showed. */

#include <stddef.h>
#include <stdint.h>

#include "xvt/flight/ai_state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ai_wire_constant {
	AI_WIRE_MAGIC = 0x57504D4A, /* "JMPW" */
	AI_WIRE_VERSION = 2,
	AI_WIRE_KIND_QUESTION = 1,
	AI_WIRE_KIND_ANSWER = 2,
	/* The length field and the header. */
	AI_WIRE_HEAD_BYTES = 20,
	AI_WIRE_CRAFT_VIEW_BYTES = 56 + AI_NOTEBOOK_BYTES,
	/* The contact count and its zero. */
	AI_WIRE_CONTACT_HEAD_BYTES = 4,
	AI_WIRE_CONTACT_BYTES = 48,
	/* The active region's craft slots; the order target mask has a bit for
	 * each. */
	AI_WIRE_CONTACTS_MAX = 32,
	AI_WIRE_CANNON_GROUPS = 2,
	AI_WIRE_WARHEAD_LAUNCHERS = 2,
	/* Weapon slots a craft has, and so turrets one answer may aim. */
	AI_WIRE_WEAPON_SLOTS = 16,
	/* A craft answer with every action kind once and all 16 turrets. */
	AI_WIRE_CRAFT_ANSWER_BYTES_MAX = 79 + AI_NOTEBOOK_BYTES,
};

enum ai_wire_action_kind {
	AI_WIRE_ACTION_STEER = 1,
	AI_WIRE_ACTION_THROTTLE = 2,
	AI_WIRE_ACTION_ACTIVITY = 3,
	AI_WIRE_ACTION_THINK_INTERVAL = 4,
	AI_WIRE_ACTION_TARGET = 5,
	AI_WIRE_ACTION_LASERS = 6,
	AI_WIRE_ACTION_WARHEAD = 7,
	AI_WIRE_ACTION_TURRETS = 8,
	AI_WIRE_ACTION_KIND_END = 9,
};

enum ai_wire_contact_flag {
	AI_WIRE_CONTACT_PLAYER = 1,
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
	uint32_t order_target_mask;
	uint16_t turret_slot_mask;
	uint8_t cannon_group_count;
	uint8_t warhead_launcher_count;
	uint8_t cannon_type[AI_WIRE_CANNON_GROUPS];
	uint8_t warhead_type[AI_WIRE_WARHEAD_LAUNCHERS];
	uint8_t warheads_left[AI_WIRE_WARHEAD_LAUNCHERS];
	uint16_t launcher_cooldown[AI_WIRE_WARHEAD_LAUNCHERS];
	uint8_t notebook[AI_NOTEBOOK_BYTES];
};

/* What the question tells the program about one craft in the active region,
 * due to think or not. Shields and hull are the craft record's own numbers;
 * target is the player's target for a craft a player flies. */
struct ai_wire_contact {
	uint16_t object_index;
	uint16_t object_signature;
	uint8_t object_type;
	uint8_t genus;
	uint8_t team;
	uint8_t flight_group;
	uint8_t object_kind;
	uint8_t flags;
	int32_t x;
	int32_t y;
	int32_t z;
	uint16_t yaw;
	uint16_t pitch;
	uint16_t speed;
	uint16_t target;
	uint16_t last_attacker;
	int32_t front_shield;
	int32_t rear_shield;
	uint32_t hull_damage;
	uint32_t hull_strength;
};

/* One question: the craft due to think and every contact, at one tick. */
struct ai_wire_question {
	uint32_t tick;
	int craft_count;
	const struct ai_wire_craft_view *craft;
	int contact_count;
	const struct ai_wire_contact *contacts;
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
	uint16_t target;
	uint8_t laser_mode[AI_WIRE_CANNON_GROUPS];
	uint8_t laser_burst[AI_WIRE_CANNON_GROUPS];
	uint8_t warhead_launcher;
	uint8_t warhead_paired;
	uint8_t turret_count;
	uint8_t turret_slot[AI_WIRE_WEAPON_SLOTS];
	uint16_t turret_target[AI_WIRE_WEAPON_SLOTS];
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
	 * AI_ACTIVITY_COUNT, a think interval of 0, or a target, cannon group,
	 * launcher or turret the question did not show. */
	AI_WIRE_BAD_ACTION,
};

/* Bytes in a question about craft_count craft with contact_count contacts,
 * the length field included. */
size_t ai_wire_question_bytes(int craft_count, int contact_count);

/* Writes the question into out. Returns the bytes written,
 * ai_wire_question_bytes of its counts, or 0 and writes nothing when the
 * craft count is outside 0 to UINT16_MAX, the contact count outside 0 to
 * AI_WIRE_CONTACTS_MAX, or capacity is short. */
size_t ai_wire_write_question(uint8_t *out, size_t capacity,
			      const struct ai_wire_question *question);

/* Reads the length bytes at in as the answer to the question. On AI_WIRE_OK
 * answers[0] to answers[craft_count - 1] hold the craft answers in the
 * question's order; on a fault their content is unspecified and none may be
 * applied. */
enum ai_wire_result ai_wire_read_answer(const uint8_t *in, size_t length,
					const struct ai_wire_question *question,
					struct ai_wire_craft_answer *answers);

#ifdef __cplusplus
}
#endif

#endif

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
 * cooldown[2] (ticks before each launcher may fire), u32 enemy mask (bit n
 * set when contact n is hostile to the craft by the mission's alliances),
 * u16 leader (the object index of the craft its flight follows, 0xFFFF when
 * it leads), then the mission's books: u8 order code[4] (each order slot's
 * order, 0 for none), u8 order state[4] (each slot's completion: 2 once its
 * goal was met, 3 once a boarding order had no target left, else 0), u16
 * order throttle (the current order's, as a craft throttle, a fraction of
 * 65,536), u8 order variable1, u8 order variable2, u8 order speed (the
 * current order's, as the mission holds them), u16 full speed (the speed
 * full throttle gives it, by the 1997 rule), u16 books (AI_WIRE_BOOKS_
 * bits), u16 mothership (the object index of the craft it goes home into,
 * 0xFFFF for none), i32 x, i32 y, i32 z of that craft's outside hangar point
 * and then of its inside one, and i32 x, i32 y, i32 z of its home point,
 * where it leaves by hyperspace: its group's mission point 13, or point 12
 * for a captured craft when that point is on (points not given are 0),
 * then its countermeasures: u8 countermeasure (an AI_WIRE_COUNTERMEASURE_
 * value, the engine's countermeasure type), u8 countermeasures left, u8
 * countermeasure working (1 while its countermeasure system works, else
 * 0), u8 threat count, u16 chaff seconds (simulated seconds of chaff
 * left), u16 countermeasure cooldown (ticks before another countermeasure
 * launches), then the homing shots aimed at it, the first threat count of
 * AI_WIRE_THREATS_MAX entries filled in projectile slot order and the rest
 * 0: u16 threat object[4] (each shot's object index), u8 threat type[4]
 * (its object type), u8 threat decoyed[4] (1 when a countermeasure
 * already homes on it), i32 threat x[4], i32 threat y[4], i32 threat z[4];
 * then its flight group's waypoints, mission points 4 to 11: u8 waypoint
 * mask (bit n set when point 4 + n is on), i32 waypoint x[8], i32 waypoint
 * y[8], i32 waypoint z[8] (each point's world position, 0 for a point
 * that is off); then a player's commands to it: u8 commands (bit 0 the
 * wait mark, bit 1 the evade command), u16 command target (the object a
 * player's attack or cover order gave it, 0xFFFF for none), u16 avoid
 * target (the object a player's ignore order told it to leave alone,
 * 0xFFFF for none); then its formation place: i32 x, i32 y, i32 z, the
 * world position of its place beside its leader, given with the books'
 * place bit (0 when not); then its warheads' lock: u16 lock ticks (its
 * count), u16 lock needed (the ticks it must reach to be declared), u8 lock
 * state (an AI_WIRE_LOCK_ value), and u8 skill level (its AI rank, 0 to 7,
 * where skill gives levels 4 and 5 one value), and u32 maneuver timer (the
 * ticks left on the craft's maneuver timer, 0 when none); then the
 * craft it escorts and its station: u16 escorted (the object index of the
 * craft it escorts, 0xFFFF for none), and i32 x, i32 y, i32 z of its station,
 * the world position it keeps beside that craft, both given with the books'
 * station bit (0xFFFF and 0, 0, 0 when not); then its group's start point:
 * i32 x, i32 y, i32 z, the world position of the mission point the group
 * arrived at, given with the books' start point bit (0 when not); then u8
 * targets now (bit n, 0 to 2, set when the order in slot n has a target in the
 * battle now, by the books' count of targets left without its later half; the
 * count ignores range and capacity; bits 3 to 7 are 0), and u64 static target
 * mask (bit n set when static n, in the statics after the contacts, matches
 * the targets of the craft's order); then the notebook's AI_NOTEBOOK_BYTES.
 * Then u16 contact count, u16 zero, and one contact per craft in the active
 * region, at most AI_WIRE_CONTACTS_MAX, each AI_WIRE_CONTACT_BYTES long: u16 object index,
 * u16 object signature, u8 object type, u8 genus, u8 team, u8 flight group,
 * u8 object kind, u8 flags (AI_WIRE_CONTACT_ bits: a player flies it, none of its
 * subsystems work, a team has hit it), i32 x, i32 y, i32 z,
 * u16 yaw, u16 pitch, u16 speed, u16 target, u16 last attacker (both 0xFFFF
 * for none), i32 front shield, i32 rear shield, u32 hull damage, u32 hull
 * strength, u8 activity (an AI_ACTIVITY_ value), u32 size (the longest side
 * of the box around the craft type's model, in world units), u8 warheads
 * coming (the homing warheads in flight at it, at most 255) u32
 * warheads coming damage (their damage summed) and u16 hit by (the object
 * index of the craft that last hit it, 0xFFFF for none, or when that object
 * is not a craft in the active region).
 *
 * After the last contact: u16 static count, u16 zero, and one static per
 * static object that counts as a target, at most AI_WIRE_STATICS_MAX, in
 * object slot order, each in the contact form (AI_WIRE_CONTACT_BYTES) with
 * AI_WIRE_CONTACT_STATIC in its flags. A static is a buoy, mine or other
 * fixed object with no craft record: its team is its flight group's, its
 * disabled flag is set once an ion hit took its working systems, and its
 * speed, shields, hull, activity and warhead fields are 0, its target, last attacker and hit by
 * 0xFFFF, its object kind 0. Its object index is its slot in the object
 * table, which no contact has.
 *
 * An answer (kind 2) holds one craft answer per craft, in the question's
 * order: u16 object index, the new notebook, u8 action count, then each
 * action as a u8 kind and its fields:
 *   1 steer: u16 yaw, u16 pitch, u16 roll to turn to
 *   2 throttle: u16, a fraction of 65,536
 *   3 activity: u8, an AI_ACTIVITY_ value other than arriving from
 *     hyperspace and leaving a hangar, which the engine alone sets
 *   4 think interval: u16 ticks, 1 or more
 *   5 target: u16 object index, a contact's or a static's, or 0xFFFF for none
 *   6 lasers: for cannon group 0 then 1, u8 link mode (0 holds fire, 1 to 3
 *     fire one cannon at a time, pairs, all) and u8 burst shots, 1 or more
 *     with a mode and 0 without; a group the view does not count holds 0, 0
 *   7 warhead: u8 launcher, one the view counts and shows ready (cooldown
 *     0), and u8 paired (1 fires both its slots at once, else 0)
 *   8 turrets: u8 count, 1 to 16, then per turret u8 weapon slot, one the
 *     view's turret mask names, each once, u16 target, a contact's or
 *     0xFFFF for none, and u8 ion, 1 to have that turret fire ion shots, 0
 *     to fire its usual ones
 *   9 arrival: u8 arriving and u16 speed, at most AI_WIRE_ARRIVAL_SPEED_MAX.
 *     Arriving 1 marks the craft arriving from hyperspace: while it is, the
 *     engine neither turns it nor changes its speed. Arriving 0 marks it
 *     active again. Either way the speed becomes the craft's speed at once.
 *     The engine applies it only to a craft that was arriving from
 *     hyperspace when the question was asked.
 *  10 order: u8 slot and u8 state. The craft works on order slot slot from
 *     now, and the slot the view shows it on takes completion state state
 *     when state is not 0. Only a craft the view shows leading or alone may
 *     send it (the engine moves a follower with its leader); state is 0, 2
 *     or 3; slot is the view's slot, one of 0 to 2 whose order code is not 0
 *     while the craft has not skipped to order 4, or 3 while the view's books
 *     show the skip due.
 *  11 depart: no fields. The craft starts out of the battle by hyperspace;
 *     only while the view's books show it may jump.
 *  12 dock: no fields. The craft enters its mothership's hangar and leaves
 *     the battle; only while the view's books show it at the hangar, and
 *     never with a depart in the same craft answer.
 *  13 countermeasure: no fields. The craft launches its countermeasure:
 *     chaff only while the view shows chaff, working, with one left and
 *     no chaff seconds; a flare only while it shows a flare, working,
 *     with one left and a cooldown of 0.
 *  14 evaded: no fields. The craft took up the evade command and dropped
 *     its target; the engine clears the command. Only while the view shows
 *     the evade command.
 *  15 maneuver timer: u32 ticks, 0 to 0x7FFFFFFF. The craft's maneuver
 *     timer is set to it; the engine counts it down.
 * A craft answer names each action kind at most once, and names only what
 * the question showed. While a craft enters hyperspace the engine does not
 * turn it, whatever the steer action says. */

#include <stddef.h>
#include <stdint.h>

#include "xvt/flight/ai_state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ai_wire_constant {
	AI_WIRE_MAGIC = 0x57504D4A, /* "JMPW" */
	AI_WIRE_VERSION = 14,
	AI_WIRE_KIND_QUESTION = 1,
	AI_WIRE_KIND_ANSWER = 2,
	/* The length field and the header. */
	AI_WIRE_HEAD_BYTES = 20,
	AI_WIRE_CRAFT_VIEW_BYTES = 348 + AI_NOTEBOOK_BYTES,
	/* A flight group's order slots, 0 to 3. */
	AI_WIRE_ORDER_SLOTS = 4,
	/* The x, y and z of a point. */
	AI_WIRE_POINT_AXES = 3,
	/* The contact count and its zero. */
	AI_WIRE_CONTACT_HEAD_BYTES = 4,
	AI_WIRE_CONTACT_BYTES = 60,
	/* The active region's craft slots; the order target mask has a bit for
	 * each. */
	AI_WIRE_CONTACTS_MAX = 32,
	/* The static object slots; the static target mask has a bit for each. */
	AI_WIRE_STATICS_MAX = 64,
	AI_WIRE_CANNON_GROUPS = 2,
	AI_WIRE_WARHEAD_LAUNCHERS = 2,
	/* Weapon slots a craft has, and so turrets one answer may aim. */
	AI_WIRE_WEAPON_SLOTS = 16,
	/* The homing shots aimed at a craft that its view can name. */
	AI_WIRE_THREATS_MAX = 4,
	/* A flight group's waypoints, mission points 4 to 11: the points the
	 * patrol orders fly in turn. */
	AI_WIRE_WAYPOINTS = 8,
	/* A craft answer with every action kind once, but depart or dock, never
	 * both, all 16 turrets with their ion bytes, and the maneuver
	 * timer. */
	AI_WIRE_CRAFT_ANSWER_BYTES_MAX = 110 + AI_NOTEBOOK_BYTES,
	/* The fastest speed an arrival may set: the 1997 AI's speed out of
	 * hyperspace. */
	AI_WIRE_ARRIVAL_SPEED_MAX = 3600,
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
	AI_WIRE_ACTION_ARRIVAL = 9,
	AI_WIRE_ACTION_ORDER = 10,
	AI_WIRE_ACTION_DEPART = 11,
	AI_WIRE_ACTION_DOCK = 12,
	AI_WIRE_ACTION_COUNTERMEASURE = 13,
	AI_WIRE_ACTION_EVADED = 14,
	AI_WIRE_ACTION_MANEUVER_TIMER = 15,
	AI_WIRE_ACTION_KIND_END = 16,
};

/* The bits of a craft view's commands: a player's wingman commands that
 * stand. */
enum ai_wire_commands {
	/* A wait command (Shift+W) holds the craft until a go command
	 * (Shift+G), an evade command or an attack or cover order. */
	AI_WIRE_COMMAND_WAIT = 0x01,
	/* An evade command (Shift+E): the craft drops its target. */
	AI_WIRE_COMMAND_EVADE = 0x02,
};

/* The bits of a craft view's books, as the engine keeps them by the 1997
 * rules. */
enum ai_wire_books {
	/* Every order the craft's group has in slots 0 to 2 is done, state 2,
	 * or its leader's are. */
	AI_WIRE_BOOKS_ORDERS_DONE = 0x001,
	/* The craft aborted the mission: its group's abort trigger held, or a
	 * player sent it home. */
	AI_WIRE_BOOKS_ABORTED = 0x002,
	/* Its group's departure came, by the clock or the trigger, and the
	 * group's departure delay has passed since. */
	AI_WIRE_BOOKS_DEPARTURE_DUE = 0x004,
	/* Its group's skip-to-order-4 trigger holds: it may move to slot 3. */
	AI_WIRE_BOOKS_SKIP_DUE = 0x008,
	/* It skipped to order 4. */
	AI_WIRE_BOOKS_SKIPPED = 0x010,
	/* It may depart: it is active, its model has a hyperdrive and its
	 * group leaves by hyperspace, or by a mothership that is gone with
	 * none of its groups still to arrive. */
	AI_WIRE_BOOKS_MAY_JUMP = 0x020,
	/* It may dock: it is active and within 512 world units of its
	 * mothership's inside hangar point by rough distance, 1,024 for a
	 * starship. */
	AI_WIRE_BOOKS_AT_HANGAR = 0x040,
	/* Its group leaves by mothership, not by hyperspace. */
	AI_WIRE_BOOKS_BY_MOTHERSHIP = 0x080,
	/* Its home point is on, so it is given. */
	AI_WIRE_BOOKS_HOME_POINT = 0x100,
	/* Its current order still has a target, in the battle or in a group
	 * still to arrive, by the 1997 count. */
	AI_WIRE_BOOKS_TARGETS_LEFT = 0x200,
	/* It follows a leader, so its formation place is given. */
	AI_WIRE_BOOKS_PLACE = 0x400,
	/* Every craft its mission asks it to bring home has returned. */
	AI_WIRE_BOOKS_ALL_RETURNED = 0x800,
	/* It escorts a craft, so its escorted craft and station are given. */
	AI_WIRE_BOOKS_STATION = 0x1000,
	/* Its group has arrived, so its start point is given. */
	AI_WIRE_BOOKS_START_POINT = 0x2000,
};

/* A craft view's countermeasure, the engine's countermeasure type; the
 * types after these the pilot program cannot launch. */
enum ai_wire_countermeasure {
	AI_WIRE_COUNTERMEASURE_NONE = 0,
	AI_WIRE_COUNTERMEASURE_CHAFF = 1,
	AI_WIRE_COUNTERMEASURE_FLARE = 2,
};

enum ai_wire_contact_flag {
	AI_WIRE_CONTACT_PLAYER = 0x01,
	/* None of its subsystems work. */
	AI_WIRE_CONTACT_DISABLED = 0x02,
	/* Some team has hit it. */
	AI_WIRE_CONTACT_ATTACKED = 0x04,
	/* A static object, not a craft; see the statics after the contacts. */
	AI_WIRE_CONTACT_STATIC = 0x08,
};

/* A craft's lock state in its view, as a player's missile_lock_state: no
 * lock, a lock counting, a lock declared. */
enum ai_wire_lock_state {
	AI_WIRE_LOCK_NONE = 0,
	AI_WIRE_LOCK_COUNTING = 1,
	AI_WIRE_LOCK_DECLARED = 2,
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
	uint32_t enemy_mask;
	uint16_t leader;
	uint8_t order_codes[AI_WIRE_ORDER_SLOTS];
	uint8_t order_states[AI_WIRE_ORDER_SLOTS];
	uint16_t order_throttle;
	uint8_t order_variable1;
	uint8_t order_variable2;
	uint8_t order_speed;
	uint16_t full_speed;
	uint16_t books;
	uint16_t mothership;
	int32_t outside_hangar[AI_WIRE_POINT_AXES];
	int32_t inside_hangar[AI_WIRE_POINT_AXES];
	int32_t home_point[AI_WIRE_POINT_AXES];
	uint8_t countermeasure;
	uint8_t countermeasures_left;
	uint8_t countermeasure_working;
	uint8_t threat_count;
	uint16_t chaff_seconds;
	uint16_t countermeasure_cooldown;
	uint16_t threat_object[AI_WIRE_THREATS_MAX];
	uint8_t threat_type[AI_WIRE_THREATS_MAX];
	uint8_t threat_decoyed[AI_WIRE_THREATS_MAX];
	int32_t threat_x[AI_WIRE_THREATS_MAX];
	int32_t threat_y[AI_WIRE_THREATS_MAX];
	int32_t threat_z[AI_WIRE_THREATS_MAX];
	uint8_t waypoint_mask;
	int32_t waypoint_x[AI_WIRE_WAYPOINTS];
	int32_t waypoint_y[AI_WIRE_WAYPOINTS];
	int32_t waypoint_z[AI_WIRE_WAYPOINTS];
	uint8_t commands;
	uint16_t command_target;
	uint16_t avoid_target;
	int32_t place[AI_WIRE_POINT_AXES];
	uint16_t lock_ticks;
	uint16_t lock_needed;
	uint8_t lock_state;
	uint8_t skill_level;
	uint32_t maneuver_timer;
	uint16_t escorted;
	int32_t station[AI_WIRE_POINT_AXES];
	int32_t start_point[AI_WIRE_POINT_AXES];
	uint8_t targets_now;
	uint64_t static_target_mask;
	uint8_t notebook[AI_NOTEBOOK_BYTES];
};

/* What the question tells the program about one craft in the active region,
 * due to think or not. Shields and hull are the craft record's own numbers;
 * target is the player's target for a craft a player flies; size is its
 * object type's max_bounds_extent, the number the flight code takes as the
 * craft's size. */
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
	uint8_t activity;
	uint32_t size;
	uint8_t warheads_coming;
	uint32_t warheads_coming_damage;
	uint16_t hit_by;
};

/* One question: the craft due to think and every contact, at one tick. */
struct ai_wire_question {
	uint32_t tick;
	int craft_count;
	const struct ai_wire_craft_view *craft;
	int contact_count;
	const struct ai_wire_contact *contacts;
	int static_count;
	const struct ai_wire_contact *statics;
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
	uint8_t turret_ion[AI_WIRE_WEAPON_SLOTS];
	uint8_t arriving;
	uint16_t speed;
	uint8_t order_slot;
	uint8_t order_state;
	uint32_t maneuver_timer;
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
	 * AI_ACTIVITY_COUNT or one the engine alone sets, a think interval of 0,
	 * an arrival's arriving past 1 or speed past AI_WIRE_ARRIVAL_SPEED_MAX,
	 * a target the question did not show as a contact or a static, a
	 * cannon group, launcher or turret it did not show, an order the view does not allow, a depart or dock its books
	 * do not show, or both, a countermeasure the view does not show
	 * ready, an evaded with no evade command shown, a turret ion past 1, or
	 * a maneuver timer past 0x7FFFFFFF. */
	AI_WIRE_BAD_ACTION,
};

/* Bytes in a question about craft_count craft with contact_count contacts and
 * static_count statics, the length field included. */
size_t ai_wire_question_bytes(int craft_count, int contact_count,
			      int static_count);

/* Writes the question into out. Returns the bytes written,
 * ai_wire_question_bytes of its counts, or 0 and writes nothing when the
 * craft count is outside 0 to UINT16_MAX, the contact count outside 0 to
 * AI_WIRE_CONTACTS_MAX, the static count outside 0 to AI_WIRE_STATICS_MAX, or
 * capacity is short. */
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

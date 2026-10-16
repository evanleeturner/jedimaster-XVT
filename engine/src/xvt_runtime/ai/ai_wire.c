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

static uint8_t *put_point(uint8_t *at, const int32_t point[AI_WIRE_POINT_AXES])
{
	for (int axis = 0; axis < AI_WIRE_POINT_AXES; ++axis) {
		at = put_u32(at, (uint32_t)point[axis]);
	}
	return at;
}

/* Writes the view's waypoint mask, then each waypoint's x, then each y, then
 * each z. */
static uint8_t *put_waypoints(uint8_t *at,
			      const struct ai_wire_craft_view *view)
{
	*at++ = view->waypoint_mask;
	for (int point = 0; point < AI_WIRE_WAYPOINTS; ++point) {
		at = put_u32(at, (uint32_t)view->waypoint_x[point]);
	}
	for (int point = 0; point < AI_WIRE_WAYPOINTS; ++point) {
		at = put_u32(at, (uint32_t)view->waypoint_y[point]);
	}
	for (int point = 0; point < AI_WIRE_WAYPOINTS; ++point) {
		at = put_u32(at, (uint32_t)view->waypoint_z[point]);
	}
	return at;
}

/* Writes the view's command bits, then its command target and its avoid
 * target. */
static uint8_t *put_commands(uint8_t *at, const struct ai_wire_craft_view *view)
{
	*at++ = view->commands;
	at = put_u16(at, view->command_target);
	return put_u16(at, view->avoid_target);
}

static uint8_t *put_u64(uint8_t *at, uint64_t value)
{
	at = put_u32(at, (uint32_t)(value & 0xFFFFFFFFu));
	return put_u32(at, (uint32_t)(value >> 32));
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
	at = put_u32(at, view->order_target_mask);
	at = put_u16(at, view->turret_slot_mask);
	*at++ = view->cannon_group_count;
	*at++ = view->warhead_launcher_count;
	memcpy(at, view->cannon_type, AI_WIRE_CANNON_GROUPS);
	at += AI_WIRE_CANNON_GROUPS;
	memcpy(at, view->warhead_type, AI_WIRE_WARHEAD_LAUNCHERS);
	at += AI_WIRE_WARHEAD_LAUNCHERS;
	memcpy(at, view->warheads_left, AI_WIRE_WARHEAD_LAUNCHERS);
	at += AI_WIRE_WARHEAD_LAUNCHERS;
	for (int launcher = 0; launcher < AI_WIRE_WARHEAD_LAUNCHERS;
	     ++launcher) {
		at = put_u16(at, view->launcher_cooldown[launcher]);
	}
	at = put_u32(at, view->enemy_mask);
	at = put_u16(at, view->leader);
	memcpy(at, view->order_codes, AI_WIRE_ORDER_SLOTS);
	at += AI_WIRE_ORDER_SLOTS;
	memcpy(at, view->order_states, AI_WIRE_ORDER_SLOTS);
	at += AI_WIRE_ORDER_SLOTS;
	at = put_u16(at, view->order_throttle);
	*at++ = view->order_variable1;
	*at++ = view->order_variable2;
	*at++ = view->order_speed;
	at = put_u16(at, view->full_speed);
	at = put_u32(at, view->books);
	at = put_u16(at, view->mothership);
	at = put_point(at, view->outside_hangar);
	at = put_point(at, view->inside_hangar);
	at = put_point(at, view->home_point);
	*at++ = view->countermeasure;
	*at++ = view->countermeasures_left;
	*at++ = view->countermeasure_working;
	*at++ = view->threat_count;
	at = put_u16(at, view->chaff_seconds);
	at = put_u16(at, view->countermeasure_cooldown);
	for (int threat = 0; threat < AI_WIRE_THREATS_MAX; ++threat) {
		at = put_u16(at, view->threat_object[threat]);
	}
	memcpy(at, view->threat_type, AI_WIRE_THREATS_MAX);
	at += AI_WIRE_THREATS_MAX;
	memcpy(at, view->threat_decoyed, AI_WIRE_THREATS_MAX);
	at += AI_WIRE_THREATS_MAX;
	for (int threat = 0; threat < AI_WIRE_THREATS_MAX; ++threat) {
		at = put_u32(at, (uint32_t)view->threat_x[threat]);
	}
	for (int threat = 0; threat < AI_WIRE_THREATS_MAX; ++threat) {
		at = put_u32(at, (uint32_t)view->threat_y[threat]);
	}
	for (int threat = 0; threat < AI_WIRE_THREATS_MAX; ++threat) {
		at = put_u32(at, (uint32_t)view->threat_z[threat]);
	}
	at = put_waypoints(at, view);
	at = put_commands(at, view);
	at = put_point(at, view->place);
	at = put_u16(at, view->lock_ticks);
	at = put_u16(at, view->lock_needed);
	*at++ = view->lock_state;
	*at++ = view->skill_level;
	at = put_u32(at, view->maneuver_timer);
	at = put_u16(at, view->escorted);
	at = put_point(at, view->station);
	at = put_point(at, view->start_point);
	*at++ = view->targets_now;
	at = put_u64(at, view->static_target_mask);
	at = put_point(at, view->board_point);
	memcpy(at, view->order_progress, AI_WIRE_ORDER_SLOTS);
	at += AI_WIRE_ORDER_SLOTS;
	*at++ = view->boarded_count;
	for (int slot = 0; slot < AI_WIRE_BOARDED_MAX; ++slot) {
		at = put_u16(at, view->boarded[slot]);
	}
	*at++ = view->board_phase;
	at = put_u16(at, view->lifetime_timer);
	at = put_u32(at, view->order_first_pair_mask);
	at = put_point(at, view->drop_point);
	*at++ = view->drop_phase;
	memcpy(at, view->notebook, sizeof view->notebook);
	return at + sizeof view->notebook;
}

static uint8_t *put_contact(uint8_t *at, const struct ai_wire_contact *contact)
{
	at = put_u16(at, contact->object_index);
	at = put_u16(at, contact->object_signature);
	*at++ = contact->object_type;
	*at++ = contact->genus;
	*at++ = contact->team;
	*at++ = contact->flight_group;
	*at++ = contact->object_kind;
	*at++ = contact->flags;
	at = put_u32(at, (uint32_t)contact->x);
	at = put_u32(at, (uint32_t)contact->y);
	at = put_u32(at, (uint32_t)contact->z);
	at = put_u16(at, contact->yaw);
	at = put_u16(at, contact->pitch);
	at = put_u16(at, contact->speed);
	at = put_u16(at, contact->target);
	at = put_u16(at, contact->last_attacker);
	at = put_u32(at, (uint32_t)contact->front_shield);
	at = put_u32(at, (uint32_t)contact->rear_shield);
	at = put_u32(at, contact->hull_damage);
	at = put_u32(at, contact->hull_strength);
	*at++ = contact->activity;
	at = put_u32(at, contact->size);
	*at++ = contact->warheads_coming;
	at = put_u32(at, contact->warheads_coming_damage);
	at = put_u16(at, contact->hit_by);
	at = put_point(at, contact->box_min);
	at = put_point(at, contact->box_max);
	at = put_u16(at, contact->roll);
	at = put_u16(at, contact->boarder);
	return at;
}

size_t ai_wire_question_bytes(int craft_count, int contact_count,
			      int static_count)
{
	size_t crafts = (size_t)craft_count * AI_WIRE_CRAFT_VIEW_BYTES;
	size_t contacts = (size_t)contact_count * AI_WIRE_CONTACT_BYTES;
	size_t statics = (size_t)static_count * AI_WIRE_CONTACT_BYTES;
	return AI_WIRE_HEAD_BYTES + crafts + AI_WIRE_CONTACT_HEAD_BYTES +
	       contacts + AI_WIRE_CONTACT_HEAD_BYTES + statics;
}

size_t ai_wire_write_question(uint8_t *out, size_t capacity,
			      const struct ai_wire_question *question)
{
	int count = question->craft_count;
	int contacts = question->contact_count;
	int statics = question->static_count;
	if (count < 0 || count > UINT16_MAX) {
		return 0;
	}
	if (contacts < 0 || contacts > AI_WIRE_CONTACTS_MAX) {
		return 0;
	}
	if (statics < 0 || statics > AI_WIRE_STATICS_MAX) {
		return 0;
	}
	size_t total = ai_wire_question_bytes(count, contacts, statics);
	if (total > capacity) {
		return 0;
	}
	uint8_t *at = put_u32(out, (uint32_t)(total - 4));
	at = put_u32(at, AI_WIRE_MAGIC);
	at = put_u16(at, AI_WIRE_VERSION);
	at = put_u16(at, AI_WIRE_KIND_QUESTION);
	at = put_u32(at, question->tick);
	at = put_u16(at, (unsigned int)count);
	at = put_u16(at, 0);
	for (int index = 0; index < count; ++index) {
		at = put_craft_view(at, &question->craft[index]);
	}
	at = put_u16(at, (unsigned int)contacts);
	at = put_u16(at, 0);
	for (int index = 0; index < contacts; ++index) {
		at = put_contact(at, &question->contacts[index]);
	}
	at = put_u16(at, (unsigned int)statics);
	at = put_u16(at, 0);
	for (int index = 0; index < statics; ++index) {
		at = put_contact(at, &question->statics[index]);
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

/* Whether object_index is 0xFFFF, no object, or one of the question's
 * contacts. */
static int is_contact_or_none(const struct ai_wire_question *question,
			      unsigned int object_index)
{
	if (object_index == UINT16_MAX) {
		return 1;
	}
	for (int index = 0; index < question->contact_count; ++index) {
		if (question->contacts[index].object_index == object_index) {
			return 1;
		}
	}
	return 0;
}

/* Whether object_index is one of the question's statics. */
static int is_static(const struct ai_wire_question *question,
		     unsigned int object_index)
{
	for (int index = 0; index < question->static_count; ++index) {
		if (question->statics[index].object_index == object_index) {
			return 1;
		}
	}
	return 0;
}

/* Reads the lasers action: per cannon group a link mode and a burst, a mode
 * 1 to 3 with 1 or more shots on a group the view counts, else 0 and 0. */
static int take_lasers(struct wire_reader *reader,
		       const struct ai_wire_craft_view *view,
		       struct ai_wire_craft_answer *answer)
{
	int good = 1;
	for (int group = 0; group < AI_WIRE_CANNON_GROUPS; ++group) {
		unsigned int mode = take_u8(reader);
		unsigned int burst = take_u8(reader);
		answer->laser_mode[group] = (uint8_t)mode;
		answer->laser_burst[group] = (uint8_t)burst;
		if (mode == 0) {
			good &= burst == 0;
		} else {
			good &= mode <= 3 && burst != 0 &&
				group < view->cannon_group_count;
		}
	}
	return reader->short_read || good;
}

/* Reads the warhead action: a launcher the view counts and shows ready with
 * a warhead left, and a paired flag of 0 or 1. */
static int take_warhead(struct wire_reader *reader,
			const struct ai_wire_craft_view *view,
			struct ai_wire_craft_answer *answer)
{
	unsigned int launcher = take_u8(reader);
	unsigned int paired = take_u8(reader);
	answer->warhead_launcher = (uint8_t)launcher;
	answer->warhead_paired = (uint8_t)paired;
	return reader->short_read ||
	       (launcher < AI_WIRE_WARHEAD_LAUNCHERS &&
		launcher < view->warhead_launcher_count &&
		view->launcher_cooldown[launcher] == 0 &&
		view->warheads_left[launcher] != 0 && paired <= 1);
}

/* Reads the turrets action: 1 to AI_WIRE_WEAPON_SLOTS turrets, each a slot
 * the view's turret mask names, once, a contact or no target, and an ion
 * switch of 0 or 1. */
static int take_turrets(struct wire_reader *reader,
			const struct ai_wire_question *question,
			const struct ai_wire_craft_view *view,
			struct ai_wire_craft_answer *answer)
{
	unsigned int count = take_u8(reader);
	if (count == 0 || count > AI_WIRE_WEAPON_SLOTS) {
		return reader->short_read;
	}
	unsigned int seen = 0;
	for (unsigned int turret = 0; turret < count; ++turret) {
		unsigned int slot = take_u8(reader);
		unsigned int target = take_u16(reader);
		unsigned int ion = take_u8(reader);
		if (reader->short_read) {
			return 1;
		}
		if (slot >= AI_WIRE_WEAPON_SLOTS ||
		    (view->turret_slot_mask & (1u << slot)) == 0 ||
		    (seen & (1u << slot)) != 0 ||
		    !is_contact_or_none(question, target) || ion > 1) {
			return 0;
		}
		seen |= 1u << slot;
		answer->turret_slot[turret] = (uint8_t)slot;
		answer->turret_target[turret] = (uint16_t)target;
		answer->turret_ion[turret] = (uint8_t)ion;
	}
	answer->turret_count = (uint8_t)count;
	return 1;
}

/* Reads the order action: for a craft the view shows leading or alone, a
 * slot the view allows and a state of 0, 2 or 3. */
static int take_order(struct wire_reader *reader,
		      const struct ai_wire_craft_view *view,
		      struct ai_wire_craft_answer *answer)
{
	unsigned int slot = take_u8(reader);
	unsigned int state = take_u8(reader);
	answer->order_slot = (uint8_t)slot;
	answer->order_state = (uint8_t)state;
	if (reader->short_read) {
		return 1;
	}
	int allowed = slot == view->order_slot;
	if (slot < AI_WIRE_ORDER_SLOTS - 1) {
		allowed |= view->order_codes[slot] != 0 &&
			   (view->books & AI_WIRE_BOOKS_SKIPPED) == 0;
	} else if (slot == AI_WIRE_ORDER_SLOTS - 1) {
		allowed |= (view->books & AI_WIRE_BOOKS_SKIP_DUE) != 0;
	}
	return allowed && view->leader == UINT16_MAX &&
	       (state == 0 || state == 2 || state == 3);
}

/* Whether the view shows the craft's countermeasure ready to launch: chaff
 * with no chaff seconds left, or a flare with its cooldown run out, either
 * working with one left. */
static int countermeasure_ready(const struct ai_wire_craft_view *view)
{
	if (view->countermeasure_working == 0 ||
	    view->countermeasures_left == 0) {
		return 0;
	}
	if (view->countermeasure == AI_WIRE_COUNTERMEASURE_CHAFF) {
		return view->chaff_seconds == 0;
	}
	return view->countermeasure == AI_WIRE_COUNTERMEASURE_FLARE &&
	       view->countermeasure_cooldown == 0;
}

/* Whether the mission order code boards a craft: Give to Pick Up (12 to 17),
 * SS Board (31) and Repair (32). */
static int order_boards(unsigned int order_code)
{
	return (order_code >= 12 && order_code <= 17) || order_code == 31 ||
	       order_code == 32;
}

/* Reads the board action: an object index to start a dock at, which the
 * view must show at its board point on a boarding order with no dock, a
 * contact; or 0xFFFF to stop a dock in its align or transfer phase. */
static int take_board(struct wire_reader *reader,
		      const struct ai_wire_question *question,
		      const struct ai_wire_craft_view *view,
		      struct ai_wire_craft_answer *answer)
{
	answer->board_object = (uint16_t)take_u16(reader);
	if (reader->short_read) {
		return 1;
	}
	if (answer->board_object == UINT16_MAX) {
		return view->board_phase == 1 || view->board_phase == 2;
	}
	return view->board_phase == 0 &&
	       (view->books & AI_WIRE_BOOKS_AT_BOARD_POINT) != 0 &&
	       order_boards(view->order_code) &&
	       is_contact_or_none(question, answer->board_object);
}

int ai_wire_engine_flies(const struct ai_wire_craft_view *view)
{
	return view->board_phase != 0 || view->drop_phase != 0;
}

/* Reads the drop off action: 1 starts the drop, only while the view shows
 * the craft at its drop point with no dock or drop running; 0 stops a drop
 * the view shows running. */
static int take_drop_off(struct wire_reader *reader,
			 const struct ai_wire_craft_view *view,
			 struct ai_wire_craft_answer *answer)
{
	answer->drop_off = (uint8_t)take_u8(reader);
	if (reader->short_read) {
		return 1;
	}
	if (answer->drop_off == 0) {
		return view->drop_phase != 0;
	}
	return answer->drop_off == 1 && !ai_wire_engine_flies(view) &&
	       (view->books & AI_WIRE_BOOKS_AT_DROP_POINT) != 0;
}

/* Reads one action into answer; returns 0 for a kind that is unknown or
 * already sent, a value out of range, or a thing the question did not
 * show. */
static int take_action(struct wire_reader *reader,
		       const struct ai_wire_question *question,
		       const struct ai_wire_craft_view *view,
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
		       (answer->activity < AI_ACTIVITY_COUNT &&
			answer->activity !=
				AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE &&
			answer->activity != AI_ACTIVITY_LEAVING_HANGAR);
	case AI_WIRE_ACTION_THINK_INTERVAL:
		answer->think_interval = (uint16_t)take_u16(reader);
		return reader->short_read || answer->think_interval != 0;
	case AI_WIRE_ACTION_TARGET:
		answer->target = (uint16_t)take_u16(reader);
		if (reader->short_read) {
			return 1;
		}
		return !ai_wire_engine_flies(view) &&
		       (is_contact_or_none(question, answer->target) ||
			is_static(question, answer->target));
	case AI_WIRE_ACTION_LASERS:
		return take_lasers(reader, view, answer);
	case AI_WIRE_ACTION_WARHEAD:
		return take_warhead(reader, view, answer);
	case AI_WIRE_ACTION_ARRIVAL:
		answer->arriving = (uint8_t)take_u8(reader);
		answer->speed = (uint16_t)take_u16(reader);
		return reader->short_read ||
		       (answer->arriving <= 1 &&
			answer->speed <= AI_WIRE_ARRIVAL_SPEED_MAX);
	case AI_WIRE_ACTION_ORDER:
		return take_order(reader, view, answer);
	case AI_WIRE_ACTION_DEPART:
		return (view->books & AI_WIRE_BOOKS_MAY_JUMP) != 0;
	case AI_WIRE_ACTION_DOCK:
		return (view->books & AI_WIRE_BOOKS_AT_HANGAR) != 0;
	case AI_WIRE_ACTION_COUNTERMEASURE:
		return countermeasure_ready(view);
	case AI_WIRE_ACTION_EVADED:
		return (view->commands & AI_WIRE_COMMAND_EVADE) != 0;
	case AI_WIRE_ACTION_MANEUVER_TIMER:
		answer->maneuver_timer = take_u32(reader);
		return reader->short_read ||
		       (answer->maneuver_timer <= 0x7FFFFFFFu &&
			!ai_wire_engine_flies(view));
	case AI_WIRE_ACTION_BOARD:
		return take_board(reader, question, view, answer);
	case AI_WIRE_ACTION_DROP_OFF:
		return take_drop_off(reader, view, answer);
	case AI_WIRE_ACTION_SELF_DESTRUCT:
		answer->self_destruct_units = (uint8_t)take_u8(reader);
		return reader->short_read ||
		       (answer->self_destruct_units != 0 &&
			answer->self_destruct_units <=
				AI_WIRE_SELF_DESTRUCT_UNITS_MAX);
	default:
		return take_turrets(reader, question, view, answer);
	}
}

static enum ai_wire_result
take_craft_answer(struct wire_reader *reader,
		  const struct ai_wire_question *question,
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
		if (!take_action(reader, question, view, answer)) {
			return AI_WIRE_BAD_ACTION;
		}
	}
	const unsigned int leaving =
		1u << AI_WIRE_ACTION_DEPART | 1u << AI_WIRE_ACTION_DOCK;
	const unsigned int board = 1u << AI_WIRE_ACTION_BOARD;
	const unsigned int drop = 1u << AI_WIRE_ACTION_DROP_OFF;
	if (!reader->short_read && (answer->actions & leaving) == leaving) {
		return AI_WIRE_BAD_ACTION;
	}
	if (!reader->short_read && (answer->actions & drop) != 0 &&
	    (answer->actions & (leaving | board)) != 0) {
		return AI_WIRE_BAD_ACTION;
	}
	if (!reader->short_read && (answer->actions & board) != 0 &&
	    ((answer->actions & leaving) != 0 ||
	     ((answer->actions & (1u << AI_WIRE_ACTION_TARGET)) != 0 &&
	      answer->board_object != UINT16_MAX &&
	      answer->board_object != answer->target))) {
		return AI_WIRE_BAD_ACTION;
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
					const struct ai_wire_question *question,
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
	enum ai_wire_result result =
		take_head(&reader, question->tick, question->craft_count);
	for (int index = 0;
	     result == AI_WIRE_OK && index < question->craft_count; ++index) {
		result = take_craft_answer(&reader, question,
					   &question->craft[index],
					   &answers[index]);
	}
	if (result == AI_WIRE_OK && reader.at != reader.end) {
		return AI_WIRE_LONG;
	}
	return result;
}

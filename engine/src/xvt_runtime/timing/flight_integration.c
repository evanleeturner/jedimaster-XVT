#include "xvt_runtime/timing/flight_integration.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/trig2.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/reference_motion.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct integration {
	int64_t position_remainder[3], remainder[XVT_INTEGRATE_COUNT];
	int8_t direction[XVT_INTEGRATE_COUNT];
	uint16_t signature, carried, target, target_signature;
	uint8_t type, family;
};

static struct integration *g_entries;
static size_t g_count;

void xvt_flight_integration_shutdown(void)
{
	free(g_entries);
	g_entries = NULL;
	g_count = 0;
}

int xvt_flight_integration_init(size_t count)
{
	xvt_flight_integration_shutdown();
	g_entries = calloc(count, sizeof *g_entries);
	if (!g_entries) {
		return 0;
	}
	g_count = count;
	return 1;
}

void xvt_flight_integration_reset_slot_and_motion(unsigned slot)
{
	if (slot < g_count) {
		memset(&g_entries[slot], 0, sizeof g_entries[slot]);
	}
	xvt_reference_motion_reset(slot);
}

static struct integration *xvt_flight_integration_sync_entry(unsigned slot)
{
	if (slot >= g_count) {
		return NULL;
	}
	struct integration *s = &g_entries[slot];
	const struct object_record *o = &g_object_table[slot];
	if (s->signature != o->object_signature || s->type != o->object_type ||
	    (o->mobj && s->family != o->mobj->family)) {
		memset(s, 0, sizeof *s);
		s->signature = o->object_signature;
		s->type = o->object_type;
		s->family = o->mobj ? o->mobj->family : 0;
		s->carried = UINT16_MAX;
		s->target = UINT16_MAX;
	}
	if (o->mobj) {
		if (!o->mobj->roll_impulse_rate) {
			s->remainder[XVT_INTEGRATE_SPIN_DECAY] =
				s->remainder[XVT_INTEGRATE_SPIN_ANGLE] = 0;
		}
		if (o->mobj->p_craft) {
			unsigned carried =
				o->mobj->p_craft->carried_object_index;
			if (carried != s->carried) {
				if (s->carried < g_count &&
				    s->carried != slot) {
					xvt_flight_integration_reset_slot_and_motion(
						s->carried);
				}
				if (carried < g_count && carried != slot) {
					xvt_flight_integration_reset_slot_and_motion(
						carried);
				}
				s->carried = carried;
			}
		}
		if (o->mobj->p_warhead_guidance) {
			const struct warhead_guidance_state *guidance =
				o->mobj->p_warhead_guidance;
			if (s->target != guidance->target_obj_idx ||
			    s->target_signature != guidance->target_signature ||
			    !guidance->homing_tier) {
				for (unsigned c = XVT_INTEGRATE_HOME_YAW;
				     c <= XVT_INTEGRATE_HOME_SPEED; ++c) {
					s->remainder[c] = 0;
					s->direction[c] = 0;
				}
				s->target = guidance->target_obj_idx;
				s->target_signature =
					guidance->target_signature;
			}
		}
	}
	return s;
}

void xvt_flight_integration_clear(unsigned slot, unsigned channel)
{
	struct integration *s = xvt_flight_integration_sync_entry(slot);
	if (s && channel < XVT_INTEGRATE_COUNT) {
		s->remainder[channel] = 0;
		s->direction[channel] = 0;
	}
}

static int64_t integrate(struct integration *s, unsigned channel,
			 int64_t numerator, int64_t divisor, int sign)
{
	if (!s) {
		return numerator / divisor;
	}
	if (s->direction[channel] != sign) {
		s->remainder[channel] = 0;
		s->direction[channel] = sign;
	}
	numerator += s->remainder[channel];
	s->remainder[channel] = numerator % divisor;
	return numerator / divisor;
}

int xvt_flight_integration_rate(unsigned slot, unsigned channel, int rate,
				unsigned elapsed, int divisor)
{
	if (channel >= XVT_INTEGRATE_COUNT || divisor <= 0) {
		return 0;
	}
	int64_t v = integrate(xvt_flight_integration_sync_entry(slot), channel,
			      (int64_t)rate * elapsed, divisor,
			      (rate > 0) - (rate < 0));
	return v > INT_MAX ? INT_MAX : v < INT_MIN ? INT_MIN : (int)v;
}

unsigned xvt_flight_integration_steer(unsigned slot, unsigned channel,
				      uint16_t rate, uint16_t accel,
				      uint16_t factor, int direction)
{
	if (channel >= XVT_INTEGRATE_COUNT) {
		return 0;
	}
	uint64_t a = accel == UINT16_MAX ? 65536u : accel;
	uint64_t f = factor == UINT16_MAX ? 65536u : factor;
	/* The complete product fits uint64_t even at the uint16_t elapsed limit. */
	uint64_t product = (uint64_t)rate * g_elapsed_ticks * a * f;
	uint64_t divisor = (uint64_t)SIMULATION_TICKS_PER_SECOND *
			   XVT_Q16_SCALE * XVT_Q16_SCALE;
	struct integration *s = xvt_flight_integration_sync_entry(slot);
	if (s && s->direction[channel] != direction) {
		s->remainder[channel] = 0;
		s->direction[channel] = direction;
	}
	uint64_t whole = product / divisor, remainder = product % divisor;
	if (s) {
		remainder += (uint64_t)s->remainder[channel];
		whole += remainder / divisor;
		s->remainder[channel] = (int64_t)(remainder % divisor);
	}
	return whole > UINT16_MAX ? UINT16_MAX : (unsigned)whole;
}

void xvt_flight_integration_move(unsigned slot)
{
	struct integration *s = xvt_flight_integration_sync_entry(slot);
	if (s) {
		const struct object_record *o = &g_object_table[slot];
		const int position[3] = {o->world_x, o->world_y, o->world_z};
		for (unsigned a = 0; a < 3; ++a) {
			if (position[a] <= -0x01000000 ||
			    position[a] >= 0x01000000) {
				s->position_remainder[a] = 0;
			}
		}
	}
	const struct mobile_object *m = g_object_table[slot].mobj;
	const int axes[3] = {m->move_x, m->move_y, m->move_z};
	int *outputs[3] = {&trig2_xmovedist, &trig2_ymovedist,
			   &trig2_zmovedist};
	int64_t speed = ((int64_t)4660 * m->speed + 128) >> 8;
	const int64_t divisor =
		(int64_t)SIMULATION_TICKS_PER_SECOND * XVT_Q15_SCALE;
	for (unsigned a = 0; a < 3; ++a) {
		int64_t numerator = speed * g_elapsed_ticks * axes[a] +
				    (s ? s->position_remainder[a] : 0);
		*outputs[a] = (int)(numerator / divisor);
		if (s) {
			s->position_remainder[a] = numerator % divisor;
		}
	}
}

void xvt_flight_integration_push(unsigned slot, unsigned axis, int *accum,
				 int cap, int *output)
{
	int rate = *accum < -cap ? -cap : *accum > cap ? cap : *accum;
	int step = xvt_flight_integration_rate(
		slot, XVT_INTEGRATE_PUSH_X + axis, rate, g_elapsed_ticks,
		SIMULATION_TICKS_PER_SECOND);
	if ((*accum > 0 && step > *accum) || (*accum < 0 && step < *accum)) {
		step = *accum;
	}
	*accum -= step;
	*output += step;
	if (!*accum) {
		xvt_flight_integration_clear(slot, XVT_INTEGRATE_PUSH_X + axis);
	}
}

void xvt_flight_integration_reset_shared(void)
{
	if (!g_entries) {
		return;
	}
	for (unsigned slot = 0; slot < g_count; ++slot) {
		if (slot >= (unsigned)g_local_transient_slot_start &&
		    slot < (unsigned)g_local_debris_slot_end) {
			continue;
		}
		memset(&g_entries[slot], 0, sizeof g_entries[slot]);
	}
}

typedef char xvt_integration_schema_channels
	[(XVT_INTEGRATE_COUNT == XVT_STATE_INTEGRATION_CHANNELS) ? 1 : -1];

void xvt_flight_integration_encode(unsigned slot,
				   struct xvt_integration_wire *out)
{
	memset(out, 0, sizeof *out);
	xvt_wire_set16(out->slot, slot);
	if (slot >= g_count) {
		return;
	}
	const struct integration *state = &g_entries[slot];
	const struct object_record *object = &g_object_table[slot];
	if (!object->object_type || state->type != object->object_type ||
	    state->signature != object->object_signature ||
	    (object->mobj && state->family != object->mobj->family)) {
		return;
	}
	xvt_wire_set16(out->signature, state->signature);
	out->type = state->type;
	out->family = state->family;
	xvt_wire_set16(out->carried_slot, state->carried);
	xvt_wire_set16(out->target_slot, state->target);
	xvt_wire_set16(out->target_signature, state->target_signature);
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis) {
		xvt_wire_set64(out->position_remainder[axis],
			       (uint64_t)state->position_remainder[axis]);
	}
	for (unsigned channel = 0; channel < XVT_INTEGRATE_COUNT; ++channel) {
		xvt_wire_set64(out->remainder[channel],
			       (uint64_t)state->remainder[channel]);
		out->direction[channel] = state->direction[channel];
	}
}

int xvt_flight_integration_decode(const struct xvt_integration_wire *record,
				  int apply)
{
	unsigned slot = xvt_wire_get16(record->slot);
	if (slot >= g_count) {
		return 0;
	}
	if (!record->type) {
		struct xvt_integration_wire empty = {0};
		xvt_wire_set16(empty.slot, slot);
		if (memcmp(record, &empty, sizeof empty)) {
			return 0;
		}
	}
	struct integration state = {0};
	state.signature = xvt_wire_get16(record->signature);
	state.type = record->type;
	state.family = record->family;
	state.carried = xvt_wire_get16(record->carried_slot);
	state.target = xvt_wire_get16(record->target_slot);
	state.target_signature = xvt_wire_get16(record->target_signature);
	if ((state.carried != UINT16_MAX && state.carried >= g_count) ||
	    (state.target != UINT16_MAX && state.target >= g_count)) {
		return 0;
	}
	const int64_t position_divisor =
		(int64_t)SIMULATION_TICKS_PER_SECOND * XVT_Q15_SCALE;
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis) {
		state.position_remainder[axis] = (int64_t)xvt_wire_get64(
			record->position_remainder[axis]);
		if (state.position_remainder[axis] <= -position_divisor ||
		    state.position_remainder[axis] >= position_divisor) {
			return 0;
		}
	}
	for (unsigned channel = 0; channel < XVT_INTEGRATE_COUNT; ++channel) {
		state.remainder[channel] =
			(int64_t)xvt_wire_get64(record->remainder[channel]);
		state.direction[channel] = record->direction[channel];
		if (state.direction[channel] < -1 ||
		    state.direction[channel] > 1) {
			return 0;
		}
		int64_t divisor =
			channel >= XVT_INTEGRATE_ROLL
				? (int64_t)SIMULATION_TICKS_PER_SECOND *
					  XVT_Q16_SCALE * XVT_Q16_SCALE
				: (int64_t)INT32_MAX + 1;
		if (state.remainder[channel] <= -divisor ||
		    state.remainder[channel] >= divisor) {
			return 0;
		}
	}
	if (apply) {
		g_entries[slot] = state;
	}
	return 1;
}

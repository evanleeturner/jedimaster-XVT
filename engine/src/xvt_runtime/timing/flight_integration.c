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

typedef struct Integration {
	int64_t position[3], remainder[XVT_INTEGRATE_COUNT];
	int8_t direction[XVT_INTEGRATE_COUNT];
	uint16_t signature, carried, target, target_signature;
	uint8_t type, state;
} Integration;

static Integration* g_entries;
static size_t g_count;

void XvtFlightIntegration_Shutdown(void) {
	free(g_entries);
	g_entries = NULL;
	g_count = 0;
}

int XvtFlightIntegration_Init(size_t count) {
	XvtFlightIntegration_Shutdown();
	g_entries = calloc(count, sizeof *g_entries);
	if (!g_entries)
		return 0;
	g_count = count;
	return 1;
}

void XvtFlightIntegration_Reset(unsigned slot) {
	if (slot < g_count)
		memset(&g_entries[slot], 0, sizeof g_entries[slot]);
	XvtReferenceMotion_Reset(slot);
}

static Integration* Entry(unsigned slot) {
	if (slot >= g_count)
		return NULL;
	Integration* s = &g_entries[slot];
	const ObjectRecord* o = &g_objectTable[slot];
	if (s->signature != o->objectSignature || s->type != o->objectType ||
		(o->mobj && s->state != o->mobj->state)) {
		memset(s, 0, sizeof *s);
		s->signature = o->objectSignature;
		s->type = o->objectType;
		s->state = o->mobj ? o->mobj->state : 0;
		s->carried = UINT16_MAX;
		s->target = UINT16_MAX;
	}
	if (o->mobj) {
		if (!o->mobj->rollImpulseRate) {
			s->remainder[XVT_INTEGRATE_SPIN_DECAY] = s->remainder[XVT_INTEGRATE_SPIN_ANGLE] = 0;
		}
		if (o->mobj->pCraft) {
			unsigned carried = o->mobj->pCraft->carriedObjectIndex;
			if (carried != s->carried) {
				if (s->carried < g_count && s->carried != slot)
					XvtFlightIntegration_Reset(s->carried);
				if (carried < g_count && carried != slot)
					XvtFlightIntegration_Reset(carried);
				s->carried = carried;
			}
		}
		if (o->mobj->pWarheadGuidance) {
			const WarheadGuidanceState* guidance = o->mobj->pWarheadGuidance;
			if (s->target != guidance->targetObjIdx || s->target_signature != guidance->targetSignature ||
				!guidance->homingTier) {
				for (unsigned c = XVT_INTEGRATE_HOME_YAW; c <= XVT_INTEGRATE_HOME_SPEED; ++c) {
					s->remainder[c] = 0;
					s->direction[c] = 0;
				}
				s->target = guidance->targetObjIdx;
				s->target_signature = guidance->targetSignature;
			}
		}
	}
	return s;
}

void XvtFlightIntegration_Clear(unsigned slot, unsigned channel) {
	Integration* s = Entry(slot);
	if (s && channel < XVT_INTEGRATE_COUNT) {
		s->remainder[channel] = 0;
		s->direction[channel] = 0;
	}
}

static int64_t Integrate(Integration* s, unsigned channel, int64_t numerator, int64_t divisor, int sign) {
	if (!s)
		return numerator / divisor;
	if (s->direction[channel] != sign) {
		s->remainder[channel] = 0;
		s->direction[channel] = sign;
	}
	numerator += s->remainder[channel];
	s->remainder[channel] = numerator % divisor;
	return numerator / divisor;
}

int XvtFlightIntegration_Rate(unsigned slot, unsigned channel, int rate, unsigned elapsed, int divisor) {
	if (channel >= XVT_INTEGRATE_COUNT || divisor <= 0)
		return 0;
	int64_t v = Integrate(Entry(slot), channel, (int64_t)rate * elapsed, divisor, (rate > 0) - (rate < 0));
	return v > INT_MAX ? INT_MAX : v < INT_MIN ? INT_MIN : (int)v;
}

unsigned XvtFlightIntegration_Steer(unsigned slot, unsigned channel, uint16_t rate, uint16_t accel,
									uint16_t factor, int direction) {
	if (channel >= XVT_INTEGRATE_COUNT)
		return 0;
	uint64_t a = accel == UINT16_MAX ? 65536u : accel;
	uint64_t f = factor == UINT16_MAX ? 65536u : factor;
	/* The complete product fits uint64_t even at the uint16_t elapsed limit. */
	uint64_t product = (uint64_t)rate * g_elapsedTicks * a * f;
	uint64_t divisor = (uint64_t)SIMULATION_TICKS_PER_SECOND * XVT_Q16_SCALE * XVT_Q16_SCALE;
	Integration* s = Entry(slot);
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

void XvtFlightIntegration_Move(unsigned slot) {
	Integration* s = Entry(slot);
	if (s) {
		const ObjectRecord* o = &g_objectTable[slot];
		const int position[3] = { o->world_x, o->world_y, o->world_z };
		for (unsigned a = 0; a < 3; ++a)
			if (position[a] <= -0x01000000 || position[a] >= 0x01000000)
				s->position[a] = 0;
	}
	const MobileObject* m = g_objectTable[slot].mobj;
	const int axes[3] = { m->moveX, m->moveY, m->moveZ };
	int* outputs[3] = { &trig2_xmovedist, &trig2_ymovedist, &trig2_zmovedist };
	int64_t speed = ((int64_t)4660 * m->speed + 128) >> 8;
	const int64_t divisor = (int64_t)SIMULATION_TICKS_PER_SECOND * XVT_Q15_SCALE;
	for (unsigned a = 0; a < 3; ++a) {
		int64_t numerator = speed * g_elapsedTicks * axes[a] + (s ? s->position[a] : 0);
		*outputs[a] = (int)(numerator / divisor);
		if (s)
			s->position[a] = numerator % divisor;
	}
}

void XvtFlightIntegration_Push(unsigned slot, unsigned axis, int* accum, int cap, int* output) {
	int rate = *accum < -cap ? -cap : *accum > cap ? cap : *accum;
	int step = XvtFlightIntegration_Rate(slot, XVT_INTEGRATE_PUSH_X + axis, rate, g_elapsedTicks, 236);
	if ((*accum > 0 && step > *accum) || (*accum < 0 && step < *accum))
		step = *accum;
	*accum -= step;
	*output += step;
	if (!*accum)
		XvtFlightIntegration_Clear(slot, XVT_INTEGRATE_PUSH_X + axis);
}

void XvtFlightIntegration_ResetShared(void) {
	if (!g_entries)
		return;
	for (unsigned slot = 0; slot < g_count; ++slot) {
		if (slot >= (unsigned)g_localTransientSlotStart && slot < (unsigned)g_localDebrisSlotEnd)
			continue;
		memset(&g_entries[slot], 0, sizeof g_entries[slot]);
	}
}

typedef char XvtIntegrationSchemaChannels[(XVT_INTEGRATE_COUNT == XVT_STATE_INTEGRATION_CHANNELS) ? 1 : -1];

void XvtFlightIntegration_Encode(unsigned slot, XvtIntegrationWire* out) {
	memset(out, 0, sizeof *out);
	XvtWire_Set16(out->slot, slot);
	if (slot >= g_count)
		return;
	const Integration* state = &g_entries[slot];
	const ObjectRecord* object = &g_objectTable[slot];
	if (!object->objectType || state->type != object->objectType ||
		state->signature != object->objectSignature || (object->mobj && state->state != object->mobj->state))
		return;
	XvtWire_Set16(out->signature, state->signature);
	out->type = state->type;
	out->state = state->state;
	XvtWire_Set16(out->carried_slot, state->carried);
	XvtWire_Set16(out->target_slot, state->target);
	XvtWire_Set16(out->target_signature, state->target_signature);
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis)
		XvtWire_Set64(out->position[axis], (uint64_t)state->position[axis]);
	for (unsigned channel = 0; channel < XVT_INTEGRATE_COUNT; ++channel) {
		XvtWire_Set64(out->remainder[channel], (uint64_t)state->remainder[channel]);
		out->direction[channel] = state->direction[channel];
	}
}

int XvtFlightIntegration_Decode(const XvtIntegrationWire* record, int apply) {
	unsigned slot = XvtWire_Get16(record->slot);
	if (slot >= g_count)
		return 0;
	if (!record->type) {
		XvtIntegrationWire empty = { 0 };
		XvtWire_Set16(empty.slot, slot);
		if (memcmp(record, &empty, sizeof empty))
			return 0;
	}
	Integration state = { 0 };
	state.signature = XvtWire_Get16(record->signature);
	state.type = record->type;
	state.state = record->state;
	state.carried = XvtWire_Get16(record->carried_slot);
	state.target = XvtWire_Get16(record->target_slot);
	state.target_signature = XvtWire_Get16(record->target_signature);
	if ((state.carried != UINT16_MAX && state.carried >= g_count) ||
		(state.target != UINT16_MAX && state.target >= g_count))
		return 0;
	const int64_t position_divisor = (int64_t)SIMULATION_TICKS_PER_SECOND * XVT_Q15_SCALE;
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis) {
		state.position[axis] = (int64_t)XvtWire_Get64(record->position[axis]);
		if (state.position[axis] <= -position_divisor || state.position[axis] >= position_divisor)
			return 0;
	}
	for (unsigned channel = 0; channel < XVT_INTEGRATE_COUNT; ++channel) {
		state.remainder[channel] = (int64_t)XvtWire_Get64(record->remainder[channel]);
		state.direction[channel] = record->direction[channel];
		if (state.direction[channel] < -1 || state.direction[channel] > 1)
			return 0;
		int64_t divisor = channel >= XVT_INTEGRATE_ROLL
							  ? (int64_t)SIMULATION_TICKS_PER_SECOND * XVT_Q16_SCALE * XVT_Q16_SCALE
							  : (int64_t)INT32_MAX + 1;
		if (state.remainder[channel] <= -divisor || state.remainder[channel] >= divisor)
			return 0;
	}
	if (apply)
		g_entries[slot] = state;
	return 1;
}

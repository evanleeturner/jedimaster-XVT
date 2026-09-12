#include "xvt_runtime/timing/reference_motion.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_timing.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct Motion {
	int32_t position[3], time, current_time;
	uint16_t signature;
	uint8_t type, valid, current_valid;
} Motion;

static Motion* g_motion;
static size_t g_count;

void XvtReferenceMotion_Shutdown(void) {
	free(g_motion);
	g_motion = NULL;
	g_count = 0;
}

int XvtReferenceMotion_Init(size_t count) {
	XvtReferenceMotion_Shutdown();
	g_motion = calloc(count, sizeof *g_motion);
	if (!g_motion)
		return 0;
	g_count = count;
	for (unsigned i = 0; i < count; ++i) {
		const ObjectRecord* o = &g_objectTable[i];
		if (!o->objectType)
			continue;
		g_motion[i].signature = o->objectSignature;
		g_motion[i].type = o->objectType;
		g_motion[i].position[0] = o->world_x;
		g_motion[i].position[1] = o->world_y;
		g_motion[i].position[2] = o->world_z;
		g_motion[i].time = g_gameTime;
		g_motion[i].valid = 1;
	}
	return 1;
}

void XvtReferenceMotion_Reset(unsigned slot) {
	if (slot < g_count)
		memset(&g_motion[slot], 0, sizeof g_motion[slot]);
}

static Motion* Entry(unsigned slot) {
	if (slot >= g_count || !g_objectTable)
		return NULL;
	Motion* m = &g_motion[slot];
	const ObjectRecord* o = &g_objectTable[slot];
	if (m->signature != o->objectSignature || m->type != o->objectType) {
		memset(m, 0, sizeof *m);
		m->signature = o->objectSignature;
		m->type = o->objectType;
	}
	return m;
}

void XvtReferenceMotion_Committed(unsigned slot, int timestamp) {
	if (!XvtFlightTiming_IsUnlocked())
		return;
	Motion* m = Entry(slot);
	if (m) {
		m->current_time = timestamp;
		m->current_valid = 1;
	}
}

void XvtReferenceMotion_CommitBoundary(void) {
	if (!XvtFlightTiming_IsUnlocked() || !XvtFlightTiming_ReferenceDue())
		return;
	for (unsigned i = 0; i < g_count; ++i) {
		Motion* m = Entry(i);
		const ObjectRecord* o = &g_objectTable[i];
		if (!o->objectType) {
			m->valid = m->current_valid = 0;
			continue;
		}
		m->position[0] = o->world_x;
		m->position[1] = o->world_y;
		m->position[2] = o->world_z;
		m->time = m->current_valid ? m->current_time : g_gameTime;
		m->valid = 1;
	}
}

void XvtReferenceMotion_Displacement(unsigned slot, int32_t delta[3]) {
	memset(delta, 0, 3 * sizeof *delta);
	Motion* m = Entry(slot);
	if (!m || !m->valid)
		return;
	const ObjectRecord* o = &g_objectTable[slot];
	int now = m->current_valid ? m->current_time : g_gameTime;
	int64_t interval = (int64_t)now - m->time;
	if (interval <= 0)
		return;
	const int32_t position[3] = { o->world_x, o->world_y, o->world_z };
	for (unsigned a = 0; a < 3; ++a) {
		int64_t d = ((int64_t)position[a] - m->position[a]) * 8 / interval;
		delta[a] = d < INT32_MIN ? INT32_MIN : d > INT32_MAX ? INT32_MAX : (int32_t)d;
	}
}

int32_t XvtReferenceMotion_Axis(unsigned slot, unsigned axis) {
	int32_t delta[3];
	XvtReferenceMotion_Displacement(slot, delta);
	return axis < 3 ? delta[axis] : 0;
}

void XvtReferenceMotion_ResetShared(void) {
	if (!g_motion)
		return;
	for (unsigned slot = 0; slot < g_count; ++slot) {
		if (slot >= (unsigned)g_localTransientSlotStart && slot < (unsigned)g_localDebrisSlotEnd)
			continue;
		memset(&g_motion[slot], 0, sizeof g_motion[slot]);
	}
}

void XvtReferenceMotion_Encode(unsigned slot, XvtReferenceMotionWire* out) {
	memset(out, 0, sizeof *out);
	XvtWire_Set16(out->slot, slot);
	if (slot >= g_count)
		return;
	const Motion* motion = &g_motion[slot];
	const ObjectRecord* object = &g_objectTable[slot];
	if (!object->objectType || motion->type != object->objectType ||
		motion->signature != object->objectSignature)
		return;
	XvtWire_Set16(out->signature, motion->signature);
	out->type = motion->type;
	out->flags =
		(motion->valid ? XVT_MOTION_VALID : 0) | (motion->current_valid ? XVT_MOTION_CURRENT_VALID : 0);
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis)
		XvtWire_Set32(out->position[axis], (uint32_t)motion->position[axis]);
	XvtWire_Set32(out->sample_tick, (uint32_t)motion->time);
	XvtWire_Set32(out->current_tick, (uint32_t)motion->current_time);
}

int XvtReferenceMotion_Decode(const XvtReferenceMotionWire* record, int apply) {
	unsigned slot = XvtWire_Get16(record->slot);
	if (slot >= g_count || record->flags & ~(XVT_MOTION_VALID | XVT_MOTION_CURRENT_VALID) ||
		XvtWire_Get16(record->reserved))
		return 0;
	if (!record->type) {
		XvtReferenceMotionWire empty = { 0 };
		XvtWire_Set16(empty.slot, slot);
		if (memcmp(record, &empty, sizeof empty))
			return 0;
	}
	Motion motion = { 0 };
	motion.signature = XvtWire_Get16(record->signature);
	motion.type = record->type;
	motion.valid = (record->flags & XVT_MOTION_VALID) != 0;
	motion.current_valid = (record->flags & XVT_MOTION_CURRENT_VALID) != 0;
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis)
		motion.position[axis] = (int32_t)XvtWire_Get32(record->position[axis]);
	motion.time = (int32_t)XvtWire_Get32(record->sample_tick);
	motion.current_time = (int32_t)XvtWire_Get32(record->current_tick);
	if (apply)
		g_motion[slot] = motion;
	return 1;
}

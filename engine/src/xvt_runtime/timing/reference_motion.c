#include "xvt_runtime/timing/reference_motion.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_timing.h"

struct motion {
	int32_t position[3];
	int32_t time;
	int32_t current_time;
	uint16_t signature;
	uint8_t type;
	uint8_t valid;
	uint8_t current_valid;
};

static struct motion *g_motion;
static size_t g_count;

void xvt_reference_motion_shutdown(void)
{
	free(g_motion);
	g_motion = NULL;
	g_count = 0;
}

int xvt_reference_motion_init(size_t count)
{
	xvt_reference_motion_shutdown();
	g_motion = calloc(count, sizeof *g_motion);
	if (!g_motion) {
		return 0;
	}
	g_count = count;
	for (unsigned i = 0; i < count; ++i) {
		const struct object_record *o = &g_object_table[i];
		if (!o->object_type) {
			continue;
		}
		g_motion[i].signature = o->object_signature;
		g_motion[i].type = o->object_type;
		g_motion[i].position[0] = o->world_x;
		g_motion[i].position[1] = o->world_y;
		g_motion[i].position[2] = o->world_z;
		g_motion[i].time = g_game_time;
		g_motion[i].valid = 1;
	}
	return 1;
}

void xvt_reference_motion_reset(unsigned slot)
{
	if (slot < g_count) {
		memset(&g_motion[slot], 0, sizeof g_motion[slot]);
	}
}

static struct motion *entry(unsigned slot)
{
	if (slot >= g_count || !g_object_table) {
		return NULL;
	}
	struct motion *m = &g_motion[slot];
	const struct object_record *o = &g_object_table[slot];
	if (m->signature != o->object_signature || m->type != o->object_type) {
		memset(m, 0, sizeof *m);
		m->signature = o->object_signature;
		m->type = o->object_type;
	}
	return m;
}

void xvt_reference_motion_committed(unsigned slot, int timestamp)
{
	if (!xvt_flight_timing_is_unlocked()) {
		return;
	}
	struct motion *m = entry(slot);
	if (m) {
		m->current_time = timestamp;
		m->current_valid = 1;
	}
}

void xvt_reference_motion_commit_boundary(void)
{
	if (!xvt_flight_timing_is_unlocked() ||
	    !xvt_flight_timing_reference_due()) {
		return;
	}
	for (unsigned i = 0; i < g_count; ++i) {
		struct motion *m = entry(i);
		const struct object_record *o = &g_object_table[i];
		if (!o->object_type) {
			m->valid = m->current_valid = 0;
			continue;
		}
		m->position[0] = o->world_x;
		m->position[1] = o->world_y;
		m->position[2] = o->world_z;
		m->time = m->current_valid ? m->current_time : g_game_time;
		m->valid = 1;
	}
}

void xvt_reference_motion_displacement(unsigned slot, int32_t delta[3])
{
	memset(delta, 0, 3 * sizeof *delta);
	struct motion *m = entry(slot);
	if (!m || !m->valid) {
		return;
	}
	const struct object_record *o = &g_object_table[slot];
	int now = m->current_valid ? m->current_time : g_game_time;
	int64_t interval = (int64_t)now - m->time;
	if (interval <= 0) {
		return;
	}
	const int32_t position[3] = {o->world_x, o->world_y, o->world_z};
	for (unsigned a = 0; a < 3; ++a) {
		int64_t d =
			((int64_t)position[a] - m->position[a]) * 8 / interval;
		delta[a] = d < INT32_MIN   ? INT32_MIN
			   : d > INT32_MAX ? INT32_MAX
					   : (int32_t)d;
	}
}

int32_t xvt_reference_motion_axis_displacement(unsigned slot, unsigned axis)
{
	int32_t delta[3];
	xvt_reference_motion_displacement(slot, delta);
	return axis < 3 ? delta[axis] : 0;
}

void xvt_reference_motion_reset_shared(void)
{
	if (!g_motion) {
		return;
	}
	for (unsigned slot = 0; slot < g_count; ++slot) {
		if (slot >= (unsigned)g_local_transient_slot_start &&
		    slot < (unsigned)g_local_debris_slot_end) {
			continue;
		}
		memset(&g_motion[slot], 0, sizeof g_motion[slot]);
	}
}

void xvt_reference_motion_encode(unsigned slot,
				 struct xvt_reference_motion_wire *out)
{
	memset(out, 0, sizeof *out);
	xvt_wire_set16(out->slot, slot);
	if (slot >= g_count) {
		return;
	}
	const struct motion *motion = &g_motion[slot];
	const struct object_record *object = &g_object_table[slot];
	if (!object->object_type || motion->type != object->object_type ||
	    motion->signature != object->object_signature) {
		return;
	}
	xvt_wire_set16(out->signature, motion->signature);
	out->type = motion->type;
	out->flags = (motion->valid ? XVT_MOTION_VALID : 0) |
		     (motion->current_valid ? XVT_MOTION_CURRENT_VALID : 0);
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis) {
		xvt_wire_set32(out->position[axis],
			       (uint32_t)motion->position[axis]);
	}
	xvt_wire_set32(out->sample_tick, (uint32_t)motion->time);
	xvt_wire_set32(out->current_tick, (uint32_t)motion->current_time);
}

int xvt_reference_motion_decode(const struct xvt_reference_motion_wire *record,
				int apply)
{
	unsigned slot = xvt_wire_get16(record->slot);
	if (slot >= g_count ||
	    record->flags & ~(XVT_MOTION_VALID | XVT_MOTION_CURRENT_VALID) ||
	    xvt_wire_get16(record->reserved)) {
		return 0;
	}
	if (!record->type) {
		struct xvt_reference_motion_wire empty = {0};
		xvt_wire_set16(empty.slot, slot);
		if (memcmp(record, &empty, sizeof empty)) {
			return 0;
		}
	}
	struct motion motion = {0};
	motion.signature = xvt_wire_get16(record->signature);
	motion.type = record->type;
	motion.valid = (record->flags & XVT_MOTION_VALID) != 0;
	motion.current_valid = (record->flags & XVT_MOTION_CURRENT_VALID) != 0;
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis) {
		motion.position[axis] =
			(int32_t)xvt_wire_get32(record->position[axis]);
	}
	motion.time = (int32_t)xvt_wire_get32(record->sample_tick);
	motion.current_time = (int32_t)xvt_wire_get32(record->current_tick);
	if (apply) {
		g_motion[slot] = motion;
	}
	return 1;
}

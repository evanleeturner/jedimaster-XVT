#include "xvt_runtime/runtime/flight_record.h"

#include <stddef.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_protocol.h"

/* The craft record of the object in slot; NULL when the slot is free or its
 * object has none. */
static const struct craft_data *xvt_flight_record_craft(int slot)
{
	const struct object_record *object = &g_object_table[slot];
	if (object->object_type == 0 || object->mobj == NULL) {
		return NULL;
	}
	return object->mobj->p_craft;
}

/* The object targeted by the player flying object; -1 when it has none or
 * no player flies object. */
static int xvt_flight_record_player_target(const struct object_record *object)
{
	int player = object->player_owner_idx;
	if (player < 0 || player >= XVT_FLIGHT_PLAYERS) {
		return -1;
	}
	return g_players[player].current_target_object_idx;
}

static void xvt_flight_record_track(int tick, int slot)
{
	const struct object_record *object = &g_object_table[slot];
	const struct mobile_object *mobj = object->mobj;
	const struct craft_data *craft = mobj->p_craft;
	XVT_LOG_DEBUG(
		"world.track tick=%d object=%d signature=%u type=%u fg=%u "
		"player=%d team=%u x=%d y=%d z=%d yaw=%u pitch=%u roll=%u "
		"speed=%u throttle=%u state=%u hull=%u hull_max=%u front=%d "
		"rear=%d target=%d ai_target=%u",
		tick, slot, object->object_signature, object->object_type,
		object->flight_group_idx, object->player_owner_idx, mobj->team,
		object->world_x, object->world_y, object->world_z, object->yaw,
		object->pitch, object->roll, mobj->speed, craft->throttle_speed,
		craft->object_kind, craft->hull_damage, craft->hull_max,
		craft->shield_energy[0], craft->shield_energy[1],
		xvt_flight_record_player_target(object),
		craft->ai_controller.target_obj_idx);
}

void xvt_flight_record_point(int tick)
{
	if (!xvt_log_enabled(AERON_LOG_DEBUG)) {
		return;
	}
	int tracked = 0;
	int unlinked = 0;
	for (int slot = g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		if (xvt_flight_record_craft(slot) != NULL) {
			++tracked;
		} else if (g_object_table[slot].object_type != 0) {
			++unlinked;
		}
	}
	XVT_LOG_DEBUG("record.point tick=%d craft=%d unlinked=%d", tick,
		      tracked, unlinked);
	for (int slot = g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		if (xvt_flight_record_craft(slot) != NULL) {
			xvt_flight_record_track(tick, slot);
		}
	}
}

/* The warhead in slot, or NULL when the slot holds no warhead: free, not a
 * shot type, or a shot type with warhead_class 0. */
static const struct object_record *xvt_flight_record_warhead(int slot)
{
	const struct object_record *object = &g_object_table[slot];
	int type_index =
		(int)object->object_type - PROJECTILE_OBJECT_TYPE_FIRST;
	if (type_index < 0 || type_index >= PROJECTILE_OBJECT_TYPE_COUNT ||
	    object->mobj == NULL ||
	    g_projectile_type_data.warhead_class[type_index] == 0) {
		return NULL;
	}
	return object;
}

void xvt_flight_record_warheads(int tick)
{
	if (!xvt_log_enabled(AERON_LOG_DEBUG)) {
		return;
	}
	for (int slot = g_projectile_object_slot_start;
	     slot < g_projectile_object_slot_end; ++slot) {
		const struct object_record *object =
			xvt_flight_record_warhead(slot);
		if (object == NULL) {
			continue;
		}
		const struct mobile_object *mobj = object->mobj;
		const struct warhead_guidance_state *guidance =
			mobj->p_warhead_guidance;
		/* Without a guidance record: no target, homing -1. */
		int target = guidance != NULL
				     ? (int)(int16_t)guidance->target_obj_idx
				     : -1;
		int homing = guidance != NULL ? (int)guidance->homing_tier : -1;
		XVT_LOG_DEBUG(
			"world.warhead_track tick=%d object=%d signature=%u "
			"type=%u source=%d target=%d homing=%d x=%d y=%d z=%d "
			"yaw=%u pitch=%u roll=%u speed=%u",
			tick, slot, object->object_signature,
			object->object_type, (int)mobj->source_obj_idx, target,
			homing, object->world_x, object->world_y,
			object->world_z, object->yaw, object->pitch,
			object->roll, mobj->speed);
	}
}

/* Whether a step from from_tick to to_tick crossed or landed on a multiple
 * of beat. */
static int xvt_flight_record_crossed(int from_tick, int to_tick, int beat)
{
	return from_tick >= 0 && from_tick < to_tick &&
	       from_tick / beat != to_tick / beat;
}

void xvt_flight_record_warhead_beat(int from_tick, int to_tick)
{
	if (xvt_flight_record_crossed(from_tick, to_tick,
				      XVT_WARHEAD_TRACK_TICKS)) {
		xvt_flight_record_warheads(to_tick);
	}
}

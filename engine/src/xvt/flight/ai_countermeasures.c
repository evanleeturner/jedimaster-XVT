/* A computer craft's countermeasures, ported from the end of the 1997 AI's
 * turret order (paifight_gunnerselfdefenseorder in paifight.c, deleted with
 * the 1997 AI): its search for the homing shots aimed at the craft and for a
 * countermeasure already homing on one, and its chaff and flare launches.
 * The rest of that code, the range at which a shot is answered, the
 * starfighter rule and which shot is answered first, moved to the pilot
 * program with the decision. */
#include "xvt/flight/ai_countermeasures.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/log/log.h"

enum {
	/* Simulated seconds of chaff one launch adds. */
	AI_COUNTERMEASURES_CHAFF_SECONDS = 10,
	/* A flight group status under which chaff uses no countermeasure. */
	AI_COUNTERMEASURES_UNLIMITED_STATUS = 21,
};

/* The guidance of the shot in slot when it is a homing shot, else NULL. */
static const struct warhead_guidance_state *homing_guidance(int slot)
{
	const struct object_record *shot = &g_object_table[slot];
	if (shot->object_type == 0) {
		return NULL;
	}
	const struct warhead_guidance_state *guidance =
		shot->mobj->p_warhead_guidance;
	if (guidance == NULL || guidance->homing_tier == 0) {
		return NULL;
	}
	return guidance;
}

/* Whether a countermeasure homes on the shot in threat_slot. */
static int is_decoyed(int threat_slot)
{
	for (int slot = g_projectile_object_slot_start;
	     slot < g_projectile_object_slot_end; ++slot) {
		if (g_object_table[slot].object_type !=
		    COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) {
			continue;
		}
		const struct warhead_guidance_state *guidance =
			homing_guidance(slot);
		if (guidance != NULL &&
		    guidance->target_obj_idx == threat_slot) {
			return 1;
		}
	}
	return 0;
}

void ai_countermeasures_view_craft(uint16_t object_index,
				   struct ai_wire_craft_view *view)
{
	const struct craft_data *craft =
		g_object_table[object_index].mobj->p_craft;
	view->countermeasure = (uint8_t)craft->cm_type_id;
	view->countermeasures_left = craft->cm_ammo_count;
	view->countermeasure_working =
		(craft->working_subsystems &
		 CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) != 0;
	view->chaff_seconds = craft->chaff_active_seconds;
	view->countermeasure_cooldown = craft->cm_fire_cooldown_timer;
	view->threat_count = 0;
	memset(view->threat_object, 0, sizeof view->threat_object);
	memset(view->threat_type, 0, sizeof view->threat_type);
	memset(view->threat_decoyed, 0, sizeof view->threat_decoyed);
	memset(view->threat_x, 0, sizeof view->threat_x);
	memset(view->threat_y, 0, sizeof view->threat_y);
	memset(view->threat_z, 0, sizeof view->threat_z);
	for (int slot = g_projectile_object_slot_start;
	     slot < g_projectile_object_slot_end &&
	     view->threat_count < AI_WIRE_THREATS_MAX;
	     ++slot) {
		const struct warhead_guidance_state *guidance =
			homing_guidance(slot);
		if (guidance == NULL ||
		    guidance->target_obj_idx != object_index ||
		    g_object_table[slot].mobj->family != 1) {
			continue;
		}
		const struct object_record *shot = &g_object_table[slot];
		int threat = view->threat_count++;
		view->threat_object[threat] = (uint16_t)slot;
		view->threat_type[threat] = shot->object_type;
		view->threat_decoyed[threat] = (uint8_t)is_decoyed(slot);
		view->threat_x[threat] = shot->world_x;
		view->threat_y[threat] = shot->world_y;
		view->threat_z[threat] = shot->world_z;
	}
}

void ai_countermeasures_launch(uint16_t object_index)
{
	const struct object_record *object = &g_object_table[object_index];
	struct craft_data *craft = object->mobj->p_craft;
	int launched = 0;
	if (craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE &&
	    (craft->working_subsystems &
	     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) != 0 &&
	    craft->cm_ammo_count != 0) {
		if (craft->cm_type_id == COUNTERMEASURE_TYPE_CHAFF &&
		    craft->chaff_active_seconds == 0) {
			const struct xvt_flight_group *group =
				&g_mission_flight_groups
					 [object->flight_group_idx]
						 .fg;
			craft->chaff_active_seconds +=
				AI_COUNTERMEASURES_CHAFF_SECONDS;
			if (group->status1 !=
				    AI_COUNTERMEASURES_UNLIMITED_STATUS &&
			    group->status2 !=
				    AI_COUNTERMEASURES_UNLIMITED_STATUS) {
				--craft->cm_ammo_count;
			}
			launched = 1;
		} else if (craft->cm_type_id == COUNTERMEASURE_TYPE_FLARE &&
			   craft->cm_fire_cooldown_timer == 0) {
			struct craft_data *kept = g_cur_craft;
			launched =
				laser_createcountermeasureprojectile(
					object_index,
					COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) >=
				0;
			g_cur_craft = kept;
		}
	}
	XVT_LOG_DEBUG(
		"ai.craft_countermeasure object=%d kind=%d launched=%d seconds=%u left=%u predicted=%d",
		(int)object_index, (int)craft->cm_type_id, launched,
		(unsigned)craft->chaff_active_seconds,
		(unsigned)craft->cm_ammo_count,
		g_flight_sim_side_effects_suppressed);
}

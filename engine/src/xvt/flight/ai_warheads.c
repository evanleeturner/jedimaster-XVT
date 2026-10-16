/* A computer craft's warheads (ai_warheads.h). The lock follows the player's
 * lock block in laser_weaponsfire and its half, xvt_player_timing_lock_half,
 * line for line; only the target (the craft's AI target) and the mesh it is
 * measured at (mesh 0, where the craft's homing shot steers) are the computer
 * craft's own. */

#include "xvt/flight/ai_warheads.h"

#include <stdint.h>
#include <string.h>

#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/targeting.h"
#include "xvt/math/trig2.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/timing/flight_timing.h"

enum {
	/* The player's lock rule, laser_weaponsfire. */
	AI_WARHEADS_FIGHTER_LOCK_RANGE = 101805,
	AI_WARHEADS_LARGE_CRAFT_LOCK_RANGE = 244332,
	AI_WARHEADS_LOCK_TICKS = 708,
	AI_WARHEADS_MISSILE_BOAT_LOCK_TICKS = 354,
	/* The modes of a half, as xvt_player_timing_lock_half takes them: under
	 * chaff, and while the lock falls. */
	AI_WARHEADS_HALF_CHAFF = 1,
	AI_WARHEADS_HALF_FALLING = 2,
	/* The mesh a computer craft's lock is measured at: its homing shot
	 * steers for mesh 0's center (warhead_guidance_state). */
	AI_WARHEADS_LOCK_MESH = 0,
	/* The game's craft slots (mission.c, CRAFT_SLOT_COUNT). */
	AI_WARHEADS_CRAFT_SLOTS = 32,
};

/* What a craft slot's lock was at its last step: the craft (by signature)
 * and target it was counted for, its state, and the half's odd tick with the
 * step and mode it was carried from, as a player's lock keeps them in
 * player_timing.c. */
struct ai_warheads_lock {
	uint64_t half_serial;
	uint16_t signature;
	uint16_t target;
	uint8_t state;
	uint8_t half_mode;
	uint8_t odd_tick;
};

static struct ai_warheads_lock g_ai_warheads_locks[AI_WARHEADS_CRAFT_SLOTS];

/* The craft slot's kept lock for this craft and target; a kept lock of
 * another craft or another target is cleared and the count starts over. */
static struct ai_warheads_lock *
kept_lock(uint16_t object_index, uint16_t target, struct craft_data *craft)
{
	struct ai_warheads_lock *lock = &g_ai_warheads_locks[object_index];
	uint16_t signature = g_object_table[object_index].object_signature;
	if (lock->signature != signature || lock->target != target) {
		memset(lock, 0, sizeof *lock);
		lock->signature = signature;
		lock->target = target;
		craft->warhead_lock_ticks = 0;
	}
	return lock;
}

/* Half the ticks that passed, as a player's lock takes it: at unlocked
 * timing the odd tick carries to the next step while the steps follow one
 * another in the same mode. */
static unsigned lock_half(struct ai_warheads_lock *lock, unsigned mode)
{
	if (!xvt_flight_timing_is_unlocked()) {
		return g_elapsed_ticks >> 1;
	}
	uint64_t serial = xvt_flight_timing_advance_serial();
	if (lock->half_serial + 1 != serial || lock->half_mode != mode) {
		lock->odd_tick = 0;
	}
	lock->half_serial = serial;
	lock->half_mode = (uint8_t)mode;
	unsigned elapsed = lock->odd_tick + g_elapsed_ticks;
	lock->odd_tick = (uint8_t)(elapsed % 2);
	return elapsed / 2;
}

/* The ticks the lock of the craft in object_index must reach. */
static int16_t lock_needed(uint16_t object_index)
{
	model_index missile_boat =
		get_model_index_from_type(CRAFT_SPECIES_MISSILE_BOAT);
	return get_model_index_from_type(
		       g_object_table[object_index].object_type) == missile_boat
		       ? AI_WARHEADS_MISSILE_BOAT_LOCK_TICKS
		       : AI_WARHEADS_LOCK_TICKS;
}

/* The warheads left in every launcher of craft. */
static int warheads_left(const struct craft_data *craft)
{
	int left = 0;
	for (int launcher = 0; launcher < craft->warhead_launcher_count &&
			       launcher < AI_WIRE_WARHEAD_LAUNCHERS;
	     ++launcher) {
		int first = g_model_defs[craft->model_index]
				    .warhead_launcher_first_slot[launcher];
		left += craft->weapon_slots[first].ammo_count +
			craft->weapon_slots[first + 1].ammo_count;
	}
	return left;
}

/* The target the lock is counted against: the craft's AI target when it
 * names a filled craft slot, else UINT16_MAX. */
static uint16_t lock_target(const struct craft_data *craft)
{
	uint16_t target = craft->ai_controller.target_obj_idx;
	if (target >= g_active_region_craft_object_slot_end ||
	    g_object_table[target].object_type == CRAFT_SPECIES_UNKNOWN) {
		return UINT16_MAX;
	}
	return target;
}

/* Whether target lies inside the lock range and the aim cone of the craft in
 * object_index: a starship or platform measured at its lock mesh's center,
 * anything else at its position. */
static int lock_in_aim(uint16_t object_index, uint16_t target)
{
	uint8_t genus = g_object_table[target].genus_id;
	int measured_at_mesh =
		genus == CRAFT_GENUS_STARSHIP || genus == CRAFT_GENUS_PLATFORM;
	if (measured_at_mesh) {
		object_direction_and_distance_to_mesh_center(
			object_index, target, AI_WARHEADS_LOCK_MESH);
	} else {
		flight_geometry_direction_between_refs(object_index, target);
	}
	unsigned int lock_range =
		measured_at_mesh || genus == CRAFT_GENUS_FREIGHTER
			? AI_WARHEADS_LARGE_CRAFT_LOCK_RANGE
			: AI_WARHEADS_FIGHTER_LOCK_RANGE;
	return (unsigned int)trig2_polardistance < lock_range &&
	       targeting_test_aim_cone_from(target, 0, object_index,
					    AI_WARHEADS_LOCK_MESH) != 0;
}

static void log_lock_lost(uint16_t object_index, uint16_t target,
			  const char *reason)
{
	XVT_LOG_DEBUG("ai.lock_lost object=%d target=%d reason=\"%s\" "
		      "predicted=%d",
		      (int)object_index, (int)(int16_t)target, reason,
		      g_flight_sim_side_effects_suppressed);
}

/* One step with the target in aim: the count gains the ticks, less half
 * under chaff, and is declared at the ticks the craft needs. */
static void lock_gain(uint16_t object_index, uint16_t target,
		      struct craft_data *craft, struct ai_warheads_lock *lock)
{
	int ticks = craft->warhead_lock_ticks + (int)g_elapsed_ticks;
	const struct craft_data *target_craft =
		g_object_table[target].mobj->p_craft;
	if (target_craft->cm_type_id == COUNTERMEASURE_TYPE_CHAFF &&
	    target_craft->chaff_active_seconds != 0) {
		ticks -= (int)lock_half(lock, AI_WARHEADS_HALF_CHAFF);
	}
	craft->warhead_lock_ticks =
		(int16_t)(ticks > INT16_MAX ? INT16_MAX : ticks);
	int16_t needed = lock_needed(object_index);
	if (craft->warhead_lock_ticks < needed) {
		lock->state = AI_WIRE_LOCK_COUNTING;
		return;
	}
	if (lock->state != AI_WIRE_LOCK_DECLARED) {
		XVT_LOG_DEBUG("ai.lock_gained object=%d target=%d ticks=%d "
			      "needed=%d predicted=%d",
			      (int)object_index, (int)target,
			      (int)craft->warhead_lock_ticks, (int)needed,
			      g_flight_sim_side_effects_suppressed);
	}
	lock->state = AI_WIRE_LOCK_DECLARED;
}

/* One step with the target out of aim: the count falls by half the ticks
 * plus the ticks, to 0 at least. */
static void lock_fall(uint16_t object_index, uint16_t target,
		      struct craft_data *craft, struct ai_warheads_lock *lock)
{
	if (lock->state == AI_WIRE_LOCK_DECLARED) {
		log_lock_lost(object_index, target, "out_of_aim");
	}
	if (craft->warhead_lock_ticks > 0) {
		int ticks = craft->warhead_lock_ticks -
			    (int)lock_half(lock, AI_WARHEADS_HALF_FALLING) -
			    (int)g_elapsed_ticks;
		craft->warhead_lock_ticks = (int16_t)(ticks < 0 ? 0 : ticks);
	}
	lock->state = AI_WIRE_LOCK_NONE;
}

void ai_warheads_count_lock(uint16_t object_index)
{
	if (object_index >= AI_WARHEADS_CRAFT_SLOTS) {
		return;
	}
	struct craft_data *craft = g_object_table[object_index].mobj->p_craft;
	uint16_t target = lock_target(craft);
	struct ai_warheads_lock *lock = kept_lock(object_index, target, craft);
	if (target == UINT16_MAX || warheads_left(craft) == 0) {
		if (lock->state == AI_WIRE_LOCK_DECLARED) {
			log_lock_lost(object_index, target,
				      target == UINT16_MAX ? "no_target"
							   : "no_rounds");
		}
		lock->state = AI_WIRE_LOCK_NONE;
		craft->warhead_lock_ticks = 0;
		return;
	}
	if (lock_in_aim(object_index, target)) {
		lock_gain(object_index, target, craft, lock);
	} else {
		lock_fall(object_index, target, craft, lock);
	}
}

void ai_warheads_view_craft(uint16_t object_index,
			    struct ai_wire_craft_view *view)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = object->mobj->p_craft;
	view->lock_ticks = (uint16_t)(craft->warhead_lock_ticks > 0
					      ? craft->warhead_lock_ticks
					      : 0);
	view->lock_needed = (uint16_t)lock_needed(object_index);
	view->lock_state = AI_WIRE_LOCK_NONE;
	if (object_index < AI_WARHEADS_CRAFT_SLOTS) {
		const struct ai_warheads_lock *lock =
			&g_ai_warheads_locks[object_index];
		if (lock->signature == object->object_signature &&
		    lock->target == lock_target(craft)) {
			view->lock_state = lock->state;
		}
	}
	view->skill_level = craft->ai_rank;
}

void ai_warheads_view_contact(uint16_t object_index,
			      struct ai_wire_contact *contact)
{
	contact->warheads_coming = 0;
	contact->warheads_coming_damage = 0;
	for (int slot = g_projectile_object_slot_start;
	     slot < g_projectile_object_slot_end; ++slot) {
		const struct object_record *shot = &g_object_table[slot];
		if (shot->object_type == 0 ||
		    shot->object_type ==
			    COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) {
			continue;
		}
		const struct warhead_guidance_state *guidance =
			shot->mobj->p_warhead_guidance;
		if (guidance == NULL || guidance->homing_tier == 0 ||
		    guidance->target_obj_idx != object_index ||
		    shot->mobj->family != 1) {
			continue;
		}
		if (contact->warheads_coming < UINT8_MAX) {
			++contact->warheads_coming;
		}
		contact->warheads_coming_damage += shot->mobj->damage_amount;
	}
}

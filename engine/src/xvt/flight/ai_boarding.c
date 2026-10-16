/* The dock of a computer craft on a boarding order, ported from the 1997
 * AI's board maneuver (paiman_boardmaneuver, paiman_transfer_object_to_ai_team
 * and paiorder_abortboardorder, in paiman.c and paiorder.c, deleted with the
 * 1997 AI). The pilot program flies the approach, the 1997 maneuver's phase 0;
 * from the board action the engine docks the craft: align (push onto the
 * target's docking point, turn to its attitude), transfer by the craft's
 * order, separate. Faults of the 1997 dock not kept: the tenth docked craft
 * was forgotten (the list now reads all ten entries and later docks overwrite
 * the last), Destroy set its countdown only on a craft that had one, and the
 * Give dock restored a player's cockpit displays on the player's own machine
 * alone and left the cockpit rebuild free to change the craft being worked
 * on. A mission point or a static object is no dock target. */
#include "xvt/flight/ai_boarding.h"

#include <stdint.h>

#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log.h"

enum {
	/* Genus ids below this are small craft for the docking offsets. */
	BOARDING_SMALL_GENUS_LIMIT = 2,
	/* The step of the turn to the target's attitude: half the model's rate,
	 * a fraction of 65,536. */
	BOARDING_ALIGNMENT_STEP = 0x8000,
	/* The three push distances left sum to less than this when the craft
	 * is on its docking point. */
	BOARDING_ALIGNMENT_DISTANCE = 16,
	/* Ticks of transfer for each unit of the order's variable1, the
	 * reload's step and its extension, the separation, and the up
	 * offset along the boarder's own axis the separation pushes to. */
	BOARDING_TICKS_PER_UNIT = 1180,
	BOARDING_RELOAD_STEP_TICKS = 472,
	BOARDING_RELOAD_CONTINUE_TICKS = 1416,
	BOARDING_SEPARATION_TICKS = 2360,
	BOARDING_SEPARATION_UP = 0x4000,
	/* The voice line of a dock start needs an order time of this many
	 * units. */
	BOARDING_MIN_VOICE_UNITS = 2,
	BOARDING_TEAMS = 10,
	BOARDING_HUD_FEATURES = 13,
	/* Signatures the docked list holds. */
	BOARDING_DOCKED_MAX = 10,
	/* The second warhead launcher always loads this warhead type. */
	BOARDING_LAUNCHER_SECONDARY = 1,
	BOARDING_SECONDARY_WARHEAD = 5,
	/* Entries in the ammo fraction table, and weapon slots a craft has. */
	BOARDING_WARHEAD_TYPES = 12,
	BOARDING_WEAPON_SLOTS = 16,
	BOARDING_WARHEADS_MIN = 1,
	BOARDING_WARHEADS_MAX = 99,
	BOARDING_FULL_CHARGE = 127,
	BOARDING_SYSTEM_HEALTH_FULL = 100,
	/* The mark a capturing flight group's index is joined to. */
	BOARDING_CAPTURE_OWNER_FLAG = 0x80,
	/* A destroyed craft's countdown: 15 to 30 seconds. */
	BOARDING_COUNTDOWN_RANDOM_MASK = 0x0F,
	BOARDING_COUNTDOWN_BASE_SECONDS = 15,
	/* The HUD's cached target is invalid and redrawn. */
	BOARDING_HUD_TARGET_INVALID = -3,
	/* Mission flight groups. */
	BOARDING_GROUPS = 48,
	BOARDING_CAPTOR_MASK = 0x7F,
};

static struct craft_data *boarding_craft(uint16_t object_index)
{
	return g_object_table[object_index].mobj->p_craft;
}

uint8_t ai_boarding_phase(uint16_t object_index)
{
	const struct ai_controller *controller =
		&boarding_craft(object_index)->ai_controller;
	if (controller->maneuver_mode != AI_MANEUVER_MODE_BOARD) {
		return AI_BOARDING_PHASE_NONE;
	}
	return controller->maneuver_phase <= AI_BOARDING_PHASE_SEPARATE
		       ? controller->maneuver_phase
		       : AI_BOARDING_PHASE_NONE;
}

/* Counts one more craft of flight group group with outcome, and marks the
 * outcome for the group's special cargo craft. */
static void boarding_count(uint16_t group, const struct craft_data *craft,
			   int outcome)
{
	++g_mission_fg_stats[group].outcome_count[outcome];
	if (g_mission_flight_groups[group].fg.special_cargo_craft ==
	    craft->craft_ordinal) {
		g_mission_fg_stats[group].special_cargo_outcome[outcome] = 1;
	}
}

/* Zeroes the craft's push, the way the dock moves it. */
static void boarding_stop_push(struct craft_data *craft)
{
	craft->push_accum_x = 0;
	craft->push_accum_y = 0;
	craft->push_accum_z = 0;
}

/* Ends the dock with no progress: phase 0, push and timers cleared. */
static void boarding_end(struct craft_data *craft)
{
	boarding_stop_push(craft);
	craft->ai_controller.maneuver_phase = AI_BOARDING_PHASE_NONE;
	craft->ai_controller.maneuver_timer = 0;
	craft->ai_controller.secondary_maneuver_timer = 0;
}

void ai_boarding_start(uint16_t object_index, uint16_t target)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	int32_t point[AI_WIRE_POINT_AXES];
	uint16_t ask;
	uint32_t bits = ai_books_board_bits(object_index, point, &ask);
	int started =
		ai_boarding_phase(object_index) == AI_BOARDING_PHASE_NONE &&
		(bits & AI_WIRE_BOOKS_AT_BOARD_POINT) != 0 &&
		controller->target_obj_idx == target;
	if (started) {
		controller->maneuver_mode = AI_MANEUVER_MODE_BOARD;
		boarding_end(craft);
		controller->maneuver_phase = AI_BOARDING_PHASE_ALIGN;
		craft->throttle_speed = 0;
	}
	XVT_LOG_DEBUG(
		"ai.craft_board object=%d target=%d started=%d bits=%u predicted=%d",
		(int)object_index, (int)target, started, (unsigned)bits,
		g_flight_sim_side_effects_suppressed);
}

void ai_boarding_stop(uint16_t object_index)
{
	uint8_t phase = ai_boarding_phase(object_index);
	if (phase != AI_BOARDING_PHASE_ALIGN &&
	    phase != AI_BOARDING_PHASE_TRANSFER) {
		return;
	}
	boarding_end(boarding_craft(object_index));
	XVT_LOG_DEBUG("ai.boarding_stopped object=%d phase=%d predicted=%d",
		      (int)object_index, (int)phase,
		      g_flight_sim_side_effects_suppressed);
}

void ai_boarding_sync_mode(uint16_t object_index)
{
	if (g_object_table[object_index].object_type == 0) {
		return;
	}
	struct ai_controller *controller =
		&boarding_craft(object_index)->ai_controller;
	int boarding =
		ai_boarding_phase(object_index) != AI_BOARDING_PHASE_NONE ||
		(controller->activity == AI_ACTIVITY_BOARDING &&
		 ai_books_order_boards(
			 mission_current_order_code(object_index)));
	if (boarding) {
		controller->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	} else if (controller->maneuver_mode == AI_MANEUVER_MODE_BOARD) {
		controller->maneuver_mode = AI_MANEUVER_MODE_NULL;
	}
}

/* Whether the dock must be abandoned, the 1997 abort order's tests: in the
 * align and transfer phases the target is gone (no object, an explosion; any
 * family but a craft's counts, as the dock reads the craft record), the
 * boarder has no working subsystem, or the target's signature is not the one
 * the craft chose; and a player's target that moves. Phase 3 never aborts. */
static int boarding_must_abort(uint16_t object_index, uint8_t phase)
{
	const struct craft_data *craft = boarding_craft(object_index);
	const uint16_t target = craft->ai_controller.target_obj_idx;
	if (phase == AI_BOARDING_PHASE_SEPARATE) {
		return 0;
	}
	if (target < g_active_region_object_slot_start ||
	    target >= g_active_region_craft_object_slot_end) {
		return 1;
	}
	const struct object_record *aimed = &g_object_table[target];
	return aimed->object_type == 0 || aimed->mobj->family != 0 ||
	       craft->working_subsystems == 0 ||
	       craft->ai_controller.target_signature !=
		       aimed->object_signature ||
	       (aimed->player_owner_idx != -1 && aimed->mobj->speed != 0);
}

/* Abandons the dock: no progress, the target forgotten. */
static void boarding_abandon(uint16_t object_index, uint8_t phase)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	XVT_LOG_DEBUG(
		"ai.boarding_abandoned object=%d target=%d phase=%d working=%04x predicted=%d",
		(int)object_index, (int)controller->target_obj_idx, (int)phase,
		(unsigned)craft->working_subsystems,
		g_flight_sim_side_effects_suppressed);
	boarding_end(craft);
	controller->target_obj_idx = UINT16_MAX;
	controller->target_signature = 0;
	controller->has_live_target = 0;
}

/* Writes the world point on the target's docking hatch the craft docks at into
 * the push accumulators, as the distance left to it. The offset along the
 * target's up axis depends on whether the target and the boarder are small
 * craft, in 16-bit arithmetic as the 1997 code did. */
static void boarding_push_to_hatch(uint16_t object_index, uint16_t target)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct object_record *aimed = &g_object_table[target];
	struct craft_data *craft = boarding_craft(object_index);
	const struct model_def *own = &g_model_defs[craft->model_index];
	const struct model_def *other =
		&g_model_defs[boarding_craft(target)->model_index];
	int16_t up;
	if (aimed->genus_id < BOARDING_SMALL_GENUS_LIMIT) {
		up = (int16_t)(other->dock_from_up[0] - own->dock_to_up[0]);
	} else if (object->genus_id < BOARDING_SMALL_GENUS_LIMIT) {
		up = (int16_t)(other->dock_from_up[0] - own->dock_to_up[1]);
	} else {
		up = (int16_t)(other->dock_from_up[1] - own->dock_to_up[1]);
	}
	flight_geometry_local_to_world_i16(aimed, 0, up, other->dock_forward);
	craft->push_accum_x = g_rotated_x + aimed->world_x - object->world_x;
	craft->push_accum_y = g_rotated_y + aimed->world_y - object->world_y;
	craft->push_accum_z = g_rotated_z + aimed->world_z - object->world_z;
}

/* Turns the craft toward the target's roll, heading and pitch at half the
 * model's rates, each only while it differs. The pitch test compares the two
 * objects' pitch, the 1997 test. */
static void boarding_turn_to(uint16_t object_index, uint16_t target)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct object_record *aimed = &g_object_table[target];
	struct craft_data *craft = boarding_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	struct ai_flight_state *flight = &craft->ai_flight;
	if (object->roll != aimed->roll) {
		flight->roll_state = 1;
		flight->roll_step = BOARDING_ALIGNMENT_STEP;
		controller->target_roll = aimed->roll;
	}
	if (object->yaw != aimed->yaw) {
		flight->turn_state = 2;
		flight->turn_step = (int16_t)BOARDING_ALIGNMENT_STEP;
		controller->target_xy_angle = aimed->yaw;
	}
	if (object->pitch != aimed->pitch) {
		controller->target_z_angle = aimed->pitch;
		flight->pitch_step_scale = BOARDING_ALIGNMENT_STEP;
		flight->pitch_through_loop = 0;
		flight->pitch_state =
			controller->target_z_angle > craft->pitch ? 2 : 1;
	}
}

/* The dock's start voice lines: the boarder's (friendly or hostile by the
 * order, only for a dock of two units or more) and the target's. */
static void boarding_speak_start(uint16_t object_index, uint16_t target,
				 int order, unsigned int units)
{
	if (g_flight_sim_side_effects_suppressed != 0) {
		return;
	}
	if (units >= BOARDING_MIN_VOICE_UNITS) {
		int hostile = order == MISSION_ORDER_BOARD_AND_TAKE_CARGO ||
			      order == MISSION_ORDER_BOARD_AND_CAPTURE_CARGO ||
			      order == MISSION_ORDER_BOARD_AND_DESTROY_CARGO;
		fsfx_speak_tactical_officer_event(
			TACTICAL_VOICE_STATUS,
			hostile ? TACTICAL_MSG_BOARDING_STARTED_HOSTILE
				: TACTICAL_MSG_BOARDING_STARTED_FRIENDLY,
			object_index, UINT16_MAX);
	}
	fsfx_speak_tactical_officer_event(TACTICAL_VOICE_STATUS,
					  TACTICAL_MSG_BOARDING_STARTED_TARGET,
					  target, UINT16_MAX);
}

/* The dock's start accounting, once for the boarder and once for the target:
 * the boarder's group counts a failed mission and the target's a completed
 * one, as the 1997 dock did before any transfer. */
static void boarding_account_start(uint16_t object_index, uint16_t target)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct craft_data *aimed = boarding_craft(target);
	uint16_t group = g_object_table[object_index].flight_group_idx;
	uint16_t aimed_group = g_object_table[target].flight_group_idx;
	if (craft->ai_flight.docking_accounting_done == 0) {
		craft->ai_flight.docking_accounting_done = 1;
		boarding_count(group, craft,
			       FLIGHT_GROUP_OUTCOME_FAILED_MISSION);
	}
	if (aimed->ai_flight.boarded_accounting_done == 0) {
		aimed->ai_flight.boarded_accounting_done = 1;
		boarding_count(aimed_group, aimed,
			       FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION);
	}
	XVT_LOG_DEBUG(
		"ai.docking_counted object=%d target=%d failed=%d completed=%d predicted=%d",
		(int)object_index, (int)target,
		(int)g_mission_fg_stats[group]
			.outcome_count[FLIGHT_GROUP_OUTCOME_FAILED_MISSION],
		(int)g_mission_fg_stats[aimed_group]
			.outcome_count[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION],
		g_flight_sim_side_effects_suppressed);
}

/* The dock's start message, "has docked with", to the local player. */
static void boarding_announce_start(uint16_t object_index, uint16_t target)
{
	msg_format_object_name(object_index, 1, g_flight_text_scratch_buffer);
	msg_add_message_ptr(0, g_flight_text_scratch_buffer);
	msg_format_object_name(target, 1,
			       g_flight_secondary_object_name_buffer);
	msg_add_message_ptr(1, g_flight_secondary_object_name_buffer);
	g_msg_sender_iff = (uint8_t)g_object_table[object_index].mobj->iff;
	msg_emit_in_flight_message(IFMSG_234_ARG_HAS_DOCKED_WITH_ARG,
				   g_local_player);
}

/* The align phase: pushes the craft onto the target's docking point, turns it
 * to the target's attitude, and when the three push distances left sum to less
 * than 16 begins the transfer: the timers (variable1 units of 1,180 ticks
 * and the reload's first step), the start accounting, the message and the
 * voice lines. */
static void boarding_align(uint16_t object_index)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	const uint16_t target = controller->target_obj_idx;
	boarding_push_to_hatch(object_index, target);
	boarding_turn_to(object_index, target);
	int left = (craft->push_accum_x < 0 ? -craft->push_accum_x
					    : craft->push_accum_x) +
		   (craft->push_accum_y < 0 ? -craft->push_accum_y
					    : craft->push_accum_y) +
		   (craft->push_accum_z < 0 ? -craft->push_accum_z
					    : craft->push_accum_z);
	if (left >= BOARDING_ALIGNMENT_DISTANCE) {
		return;
	}
	boarding_stop_push(craft);
	const uint16_t group = g_object_table[object_index].flight_group_idx;
	const int order = mission_current_order_code(object_index);
	const unsigned int units =
		g_mission_flight_groups[group]
			.fg.orders[controller->current_order_slot]
			.variable1;
	controller->maneuver_phase = AI_BOARDING_PHASE_TRANSFER;
	controller->maneuver_timer = (int)(BOARDING_TICKS_PER_UNIT * units);
	controller->secondary_maneuver_timer = SIMULATION_TICKS_PER_SECOND;
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"ai.boarding_started object=%d fg=%d target=%d target_fg=%d order=%d timer=%d tick=%d",
			(int)object_index, (int)group, (int)target,
			(int)g_object_table[target].flight_group_idx, order,
			controller->maneuver_timer, g_game_time);
	}
	boarding_account_start(object_index, target);
	boarding_announce_start(object_index, target);
	boarding_speak_start(object_index, target, order, units);
}

/* The most warheads the target's launcher slot is reloaded to: its model's
 * capacity at the warhead's ammo fraction, at least 1, doubled or halved by
 * its group's status, at least 1 again and at most 99. */
static unsigned int boarding_warhead_goal(uint16_t target, int launcher)
{
	const struct craft_data *aimed = boarding_craft(target);
	const struct mission_flight_group *group =
		&g_mission_flight_groups[g_object_table[target]
						 .flight_group_idx];
	unsigned int warhead = launcher == BOARDING_LAUNCHER_SECONDARY
				       ? BOARDING_SECONDARY_WARHEAD
				       : group->fg.warhead;
	if (warhead >= BOARDING_WARHEAD_TYPES) {
		warhead = 0;
	}
	uint16_t goal = (uint16_t)math2_fraction(
		g_model_defs[aimed->model_index]
			.warhead_launcher_capacity[launcher],
		g_warhead_ammo_fraction_q16[warhead]);
	if (goal == 0) {
		goal = BOARDING_WARHEADS_MIN;
	}
	if (group->fg.status1 == 1) {
		goal = (uint16_t)(goal * 2);
	} else if (group->fg.status1 == 2) {
		goal >>= 1;
	}
	if (goal == 0) {
		goal = BOARDING_WARHEADS_MIN;
	}
	return goal > BOARDING_WARHEADS_MAX ? BOARDING_WARHEADS_MAX : goal;
}

/* One reload step of a player's craft that a Give dock serves: one more
 * warhead in each launcher slot below its goal, every weapon charged, the
 * countermeasures full, and the first failed subsystem repaired. Returns 1
 * when anything changed. */
static int boarding_reload(uint16_t target)
{
	struct craft_data *aimed = boarding_craft(target);
	const struct model_def *model = &g_model_defs[aimed->model_index];
	int changed = 0;
	for (int launcher = 0; launcher < aimed->warhead_launcher_count;
	     ++launcher) {
		if (aimed->warhead_slot_type_ids[launcher] == 0) {
			continue;
		}
		unsigned int goal = boarding_warhead_goal(target, launcher);
		for (int slot = model->warhead_launcher_first_slot[launcher];
		     slot <= model->warhead_launcher_last_slot[launcher] &&
		     slot < BOARDING_WEAPON_SLOTS;
		     ++slot) {
			if (aimed->weapon_slots[slot].ammo_count < goal) {
				++aimed->weapon_slots[slot].ammo_count;
				changed = 1;
			}
			aimed->weapon_slots[slot].laser_charge =
				BOARDING_FULL_CHARGE;
		}
	}
	if (aimed->cm_type_id != COUNTERMEASURE_TYPE_NONE) {
		aimed->cm_ammo_count = model->countermeasure_count;
	}
	unsigned int mask = CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		if ((aimed->system_flags & mask) != 0 &&
		    (aimed->working_subsystems & mask) == 0) {
			aimed->working_subsystems |= (craft_subsystem_flag)mask;
			return 1;
		}
		mask *= 2;
	}
	return changed;
}

/* Copies a craft's special cargo name over another's. */
static void boarding_copy_cargo(struct craft_data *to,
				const struct craft_data *from)
{
	for (size_t index = 0; index < sizeof to->special_cargo_name; ++index) {
		to->special_cargo_name[index] = from->special_cargo_name[index];
	}
}

/* Gives a player's craft full system health and, when it is a craft whose
 * cockpit shows lost features, the features back. The state is changed on
 * every machine; only the local player's cockpit is drawn again, and the
 * craft the caller works on is kept across the redraw. */
static void boarding_restore_player(uint16_t object_index, uint16_t target)
{
	struct craft_data *aimed = boarding_craft(target);
	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		aimed->system_display_slot_by_system[system] = (uint8_t)system;
		aimed->system_health[system] = BOARDING_SYSTEM_HEALTH_FULL;
		aimed->system_repair_seconds[system] = 0;
	}
	int type = g_object_table[target].object_type;
	if (type != CRAFT_SPECIES_X_WING && type != CRAFT_SPECIES_Y_WING &&
	    type != CRAFT_SPECIES_A_WING && type != CRAFT_SPECIES_B_WING &&
	    type != CRAFT_SPECIES_Z_95_HEADHUNTER) {
		return;
	}
	struct craft_damage_stats *stats = &aimed->damage_stats;
	for (int feature = 0; feature < BOARDING_HUD_FEATURES; ++feature) {
		uint16_t bit = (uint16_t)(1u << feature);
		if ((stats->installed_hud_feature_mask & bit) != 0) {
			stats->active_hud_feature_mask |= bit;
		}
	}
	int player = g_object_table[target].player_owner_idx;
	XVT_LOG_DEBUG(
		"ai.hud_features_restored object=%d target=%d slot=%d mask=%04x local=%d predicted=%d",
		(int)object_index, (int)target, player,
		(unsigned)stats->active_hud_feature_mask,
		player == g_local_player, g_flight_sim_side_effects_suppressed);
	if (player == g_local_player) {
		struct craft_data *kept = g_cur_craft;
		flight_surface_lock();
		hud_rebuild_display_for_view_state(
			g_players[g_local_player].view_state.hud_state_live,
			g_local_player);
		flight_surface_unlock();
		g_cur_craft = kept;
	}
}

/* Gives a craft's side to the target, the 1997 transfer: when the teams
 * differ, the capture counts move (a craft taken back by its own group's team
 * is uncounted and its capture mark cleared; else it is counted for the new
 * team, moved from an earlier captor's, and marked with owner_flag and the
 * capturing flight group); always the target takes the boarder's iff and
 * team, its subsystems are restored and cleared of damage, its last attacker
 * forgotten and it is active. Its flight group's own team does not change. */
static void boarding_transfer_to_team(uint16_t object_index, uint16_t target,
				      uint8_t owner_flag)
{
	struct mobile_object *boarder = g_object_table[object_index].mobj;
	struct mobile_object *held = g_object_table[target].mobj;
	struct craft_data *aimed = held->p_craft;
	const uint16_t group = g_object_table[target].flight_group_idx;
	const uint8_t from = held->team;
	if (boarder->team != held->team) {
		uint8_t home = g_mission_flight_groups[group].fg.team;
		uint16_t (*counts)[BOARDING_GROUPS] =
			g_flight_mission_state.runtime
				.team_fg_inspected_captured_counts[1];
		if (boarder->team == home) {
			--g_mission_fg_stats[group]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED];
			if (g_mission_flight_groups[group]
				    .fg.special_cargo_craft ==
			    aimed->craft_ordinal) {
				g_mission_fg_stats[group].special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_CAPTURED] = 0;
			}
			aimed->captured_by_flight_group = 0;
		} else {
			uint8_t captor = aimed->captured_by_flight_group &
					 BOARDING_CAPTOR_MASK;
			if (held->team == home) {
				boarding_count(group, aimed,
					       FLIGHT_GROUP_OUTCOME_CAPTURED);
			} else if (captor < BOARDING_GROUPS &&
				   g_mission_flight_groups[captor].fg.team <
					   BOARDING_TEAMS) {
				--counts[g_mission_flight_groups[captor]
						 .fg.team][group];
			}
			if (boarder->team < BOARDING_TEAMS) {
				++counts[boarder->team][group];
			}
			aimed->captured_by_flight_group =
				(uint8_t)(owner_flag |
					  g_object_table[object_index]
						  .flight_group_idx);
		}
	}
	held->iff = boarder->iff;
	held->team = boarder->team;
	aimed->working_subsystems = aimed->system_flags;
	aimed->subsystem_damage = 0;
	aimed->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	aimed->last_attacker_obj_idx = UINT16_MAX;
	XVT_LOG_DEBUG(
		"ai.craft_changed_side object=%d target=%d fg=%d from=%d team=%d iff=%d captured=%d predicted=%d",
		(int)object_index, (int)target, (int)group, (int)from,
		(int)held->team, (int)held->iff,
		(int)aimed->captured_by_flight_group,
		g_flight_sim_side_effects_suppressed);
}

/* Speaks a tactical officer line, once, on this machine. */
static void boarding_speak(int message, uint16_t object_index)
{
	if (g_flight_sim_side_effects_suppressed == 0) {
		fsfx_speak_tactical_officer_event(TACTICAL_VOICE_STATUS,
						  message, object_index,
						  UINT16_MAX);
	}
}

/* The boarder's "docking operation complete" message. */
static void boarding_report_complete(uint16_t object_index)
{
	msg_emit_craft_message(object_index, boarding_craft(object_index),
			       IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
}

/* Give: the boarder's cargo goes to the target, the boarder's name cleared
 * once the order's variable2 boardings are done; a player's craft is
 * restored. */
static void boarding_give(uint16_t object_index, uint16_t target)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct craft_data *aimed = boarding_craft(target);
	const struct mission_order *order =
		&g_mission_flight_groups[g_object_table[object_index]
						 .flight_group_idx]
			 .fg.orders[craft->ai_controller.current_order_slot];
	boarding_copy_cargo(aimed, craft);
	aimed->boarding_state = 2;
	if (order->variable2 <=
	    craft->ai_controller.order_progress.goal_progress
			    [craft->ai_controller.current_order_slot] +
		    1) {
		craft->special_cargo_name[0] = 0;
	}
	craft->boarding_state = 1;
	if (g_object_table[target].player_owner_idx != -1) {
		boarding_restore_player(object_index, target);
	}
	boarding_report_complete(object_index);
	boarding_speak(TACTICAL_MSG_TRANSFER_COMPLETE, object_index);
}

/* Take: the target's cargo goes to the boarder. */
static void boarding_take(uint16_t object_index, uint16_t target)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct craft_data *aimed = boarding_craft(target);
	boarding_copy_cargo(craft, aimed);
	aimed->special_cargo_name[0] = 0;
	aimed->boarding_state = 1;
	craft->boarding_state = 2;
	boarding_report_complete(object_index);
	boarding_speak(TACTICAL_MSG_BOARDING_COMPLETE, object_index);
}

/* Exchange: the two cargo names swap. */
static void boarding_exchange(uint16_t object_index, uint16_t target)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct craft_data *aimed = boarding_craft(target);
	for (size_t index = 0; index < sizeof craft->special_cargo_name;
	     ++index) {
		char byte = craft->special_cargo_name[index];
		craft->special_cargo_name[index] =
			aimed->special_cargo_name[index];
		aimed->special_cargo_name[index] = byte;
	}
	aimed->boarding_state = 2;
	craft->boarding_state = 2;
	boarding_report_complete(object_index);
}

/* Capture: the target changes side. */
static void boarding_capture(uint16_t object_index, uint16_t target)
{
	struct craft_data *aimed = boarding_craft(target);
	boarding_transfer_to_team(object_index, target,
				  BOARDING_CAPTURE_OWNER_FLAG);
	if (aimed->captured_by_flight_group != 0) {
		msg_emit_craft_message(target, aimed,
				       IFMSG_139_HAS_BEEN_CAPTURED);
	}
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"ai.craft_captured object=%d target=%d team=%d captured=%d tick=%d",
			(int)object_index, (int)target,
			(int)g_object_table[target].mobj->team,
			(int)aimed->captured_by_flight_group, g_game_time);
	}
	boarding_speak(TACTICAL_MSG_BOARDING_COMPLETE, object_index);
	boarding_speak(TACTICAL_MSG_CAPTURED_TARGET, target);
}

/* Destroy: the boarder is credited with the kill now; the target gets a
 * countdown of 15 to 30 seconds when none runs (the 1997 test was inverted,
 * so a craft with no countdown got none), and blows itself up when it ends. */
static void boarding_destroy(uint16_t object_index, uint16_t target)
{
	struct mobile_object *held = g_object_table[target].mobj;
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"battle.killing_blow victim=%d by=%d attacker=%d weapon=\"boarding\" type=%d tick=%d",
			(int)target,
			g_object_table[object_index].player_owner_idx,
			(int)object_index,
			(int)g_object_table[object_index].object_type,
			g_game_time);
	}
	mission_credit_destruction_damage_contributors(object_index, target);
	if (held->lifetime_timer == 0) {
		held->lifetime_timer =
			(uint16_t)(SIMULATION_TICKS_PER_SECOND *
				   ((game_rand() &
				     BOARDING_COUNTDOWN_RANDOM_MASK) +
				    BOARDING_COUNTDOWN_BASE_SECONDS));
	}
	XVT_LOG_DEBUG(
		"ai.target_sabotaged object=%d target=%d life=%u predicted=%d",
		(int)object_index, (int)target, (unsigned)held->lifetime_timer,
		g_flight_sim_side_effects_suppressed);
	boarding_speak(TACTICAL_MSG_BOARDING_COMPLETE, object_index);
}

/* Pick Up: the target changes side, then the boarder carries it, its
 * subsystems down. */
static void boarding_pick_up(uint16_t object_index, uint16_t target)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct craft_data *aimed = boarding_craft(target);
	boarding_transfer_to_team(object_index, target,
				  BOARDING_CAPTURE_OWNER_FLAG);
	craft->carried_object_index = target;
	aimed->carrier_obj_idx = object_index;
	aimed->working_subsystems = 0;
	boarding_speak(TACTICAL_MSG_TRANSFER_COMPLETE, object_index);
}

/* SS Board: only the target's state. */
static void boarding_contact(uint16_t object_index, uint16_t target)
{
	boarding_craft(target)->boarding_state = 2;
	boarding_report_complete(object_index);
	boarding_speak(TACTICAL_MSG_BOARDING_COMPLETE, object_index);
}

/* Repair: the target's subsystems and damage restored. */
static void boarding_repair(uint16_t object_index, uint16_t target)
{
	struct craft_data *aimed = boarding_craft(target);
	aimed->working_subsystems = aimed->system_flags;
	aimed->subsystem_damage = 0;
	aimed->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	aimed->boarding_state = 3;
	boarding_report_complete(object_index);
	boarding_speak(TACTICAL_MSG_BOARDING_COMPLETE, object_index);
	boarding_speak(TACTICAL_MSG_REPAIRED_TARGET, target);
}

/* The transfer the craft's order does when the dock timer has run out. */
static void boarding_transfer(uint16_t object_index, uint16_t target)
{
	switch (mission_current_order_code(object_index)) {
	case MISSION_ORDER_BOARD_AND_GIVE_CARGO:
		boarding_give(object_index, target);
		break;
	case MISSION_ORDER_BOARD_AND_TAKE_CARGO:
		boarding_take(object_index, target);
		break;
	case MISSION_ORDER_BOARD_AND_EXCHANGE_CARGO:
		boarding_exchange(object_index, target);
		break;
	case MISSION_ORDER_BOARD_AND_CAPTURE_CARGO:
		boarding_capture(object_index, target);
		break;
	case MISSION_ORDER_BOARD_AND_DESTROY_CARGO:
		boarding_destroy(object_index, target);
		break;
	case MISSION_ORDER_PICK_UP:
		boarding_pick_up(object_index, target);
		break;
	case MISSION_ORDER_SS_BOARD:
		boarding_contact(object_index, target);
		break;
	case MISSION_ORDER_BOARD_TO_REPAIR:
		boarding_repair(object_index, target);
		break;
	default:
		break;
	}
}

/* The tail's inspection: a boarder of the target's iff inspects it for its
 * team once, counted for the target's group; a boarder of another iff only
 * raises a warning sound. */
static void boarding_inspect(uint16_t object_index, uint16_t target)
{
	struct mobile_object *boarder = g_object_table[object_index].mobj;
	struct craft_data *aimed = boarding_craft(target);
	const uint16_t group = g_object_table[target].flight_group_idx;
	if (boarder->iff != g_object_table[target].mobj->iff) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			fsfx_play_sound(FLIGHT_SOUND_GENERAL_WARNING, -1,
					g_local_player);
		}
		return;
	}
	if (boarder->team >= BOARDING_TEAMS ||
	    aimed->identified_order_by_team[boarder->team] != 0) {
		return;
	}
	uint8_t highest = 0;
	for (int team = 0; team < BOARDING_TEAMS; ++team) {
		if (highest < aimed->identified_order_by_team[team]) {
			highest = aimed->identified_order_by_team[team];
		}
	}
	aimed->identified_order_by_team[boarder->team] = (uint8_t)(highest + 1);
	boarding_count(group, aimed, FLIGHT_GROUP_OUTCOME_INSPECTED);
}

/* Writes the signature of the craft docked with into the docked list: a
 * new entry while there is room, else over the last (the tenth was forgotten
 * in 1997). */
static void boarding_record_docked(struct craft_data *craft, uint16_t signature)
{
	struct ai_flight_state *flight = &craft->ai_flight;
	if (flight->docked_target_count < BOARDING_DOCKED_MAX) {
		flight->docked_target_signatures
			[flight->docked_target_count++] = signature;
	} else {
		flight->docked_target_signatures[BOARDING_DOCKED_MAX - 1] =
			signature;
	}
}

/* The tail every transfer ends with: the inspection, one more boarding done,
 * the docked list and the DOCKED outcome (the first docking), the target's
 * times boarded and the BOARDED outcome, then the separation. */
static void boarding_finish_transfer(uint16_t object_index, uint16_t target)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct craft_data *aimed = boarding_craft(target);
	struct ai_controller *controller = &craft->ai_controller;
	const uint16_t group = g_object_table[object_index].flight_group_idx;
	boarding_inspect(object_index, target);
	++controller->order_progress
		  .goal_progress[controller->current_order_slot];
	boarding_record_docked(craft, g_object_table[target].object_signature);
	if (craft->ai_flight.docked_target_count == 1) {
		boarding_count(group, craft, FLIGHT_GROUP_OUTCOME_DOCKED);
	}
	++aimed->ai_flight.times_boarded;
	boarding_count(g_object_table[target].flight_group_idx, aimed,
		       FLIGHT_GROUP_OUTCOME_BOARDED);
	controller->maneuver_phase = AI_BOARDING_PHASE_SEPARATE;
	controller->maneuver_timer = BOARDING_SEPARATION_TICKS;
	if (g_players[g_local_player].current_target_object_idx == target) {
		g_hud_cached_target_object_idx = BOARDING_HUD_TARGET_INVALID;
	}
	XVT_LOG_DEBUG(
		"ai.boarding_done object=%d target=%d order=%d state=%d target_state=%d progress=%d docked=%d docked_count=%d boarded=%d predicted=%d",
		(int)object_index, (int)target,
		mission_current_order_code(object_index),
		(int)craft->boarding_state, (int)aimed->boarding_state,
		(int)controller->order_progress
			.goal_progress[controller->current_order_slot],
		(int)g_mission_fg_stats[group]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DOCKED],
		(int)craft->ai_flight.docked_target_count,
		(int)aimed->ai_flight.times_boarded,
		g_flight_sim_side_effects_suppressed);
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"ai.boarding_finished object=%d fg=%d target=%d order=%d progress=%d tick=%d",
			(int)object_index, (int)group, (int)target,
			mission_current_order_code(object_index),
			(int)controller->order_progress
				.goal_progress[controller->current_order_slot],
			g_game_time);
	}
}

/* The transfer phase: while its timer runs only a Give dock to a player's
 * craft works, a reload step each 472 ticks that extends the dock to 1,416
 * ticks after any change; when it has run out, the order's transfer and the
 * tail. */
static void boarding_transfer_phase(uint16_t object_index)
{
	struct ai_controller *controller =
		&boarding_craft(object_index)->ai_controller;
	const uint16_t target = controller->target_obj_idx;
	if (controller->maneuver_timer != 0) {
		if (mission_current_order_code(object_index) !=
			    MISSION_ORDER_BOARD_AND_GIVE_CARGO ||
		    g_object_table[target].player_owner_idx == -1 ||
		    controller->secondary_maneuver_timer != 0) {
			return;
		}
		int changed = boarding_reload(target);
		controller->secondary_maneuver_timer =
			BOARDING_RELOAD_STEP_TICKS;
		if (changed != 0) {
			controller->maneuver_timer =
				BOARDING_RELOAD_CONTINUE_TICKS;
		}
		XVT_LOG_DEBUG(
			"ai.reload_step object=%d target=%d slot=%d reloaded=%d timer=%d predicted=%d",
			(int)object_index, (int)target,
			g_object_table[target].player_owner_idx, changed,
			controller->maneuver_timer,
			g_flight_sim_side_effects_suppressed);
		return;
	}
	boarding_transfer(object_index, target);
	boarding_finish_transfer(object_index, target);
}

/* The separate phase: the craft is pushed to the target's position plus 0x4000
 * along its own up axis until the timer runs out, then the dock ends: the
 * target is forgotten, and a player's craft's command target with it. */
static void boarding_separate(uint16_t object_index)
{
	struct craft_data *craft = boarding_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	const uint16_t target = controller->target_obj_idx;
	const int gone = target < g_active_region_object_slot_start ||
			 target >= g_active_region_craft_object_slot_end ||
			 g_object_table[target].object_type == 0;
	if (controller->maneuver_timer == 0) {
		XVT_LOG_DEBUG(
			"ai.boarding_separated object=%d target=%d predicted=%d",
			(int)object_index, (int)target,
			g_flight_sim_side_effects_suppressed);
		if (!gone && g_object_table[target].player_owner_idx != -1) {
			controller->candidate_target_idx = UINT16_MAX;
		}
		boarding_end(craft);
		controller->target_obj_idx = UINT16_MAX;
		controller->target_signature = 0;
		controller->has_live_target = 0;
		return;
	}
	if (gone) {
		boarding_stop_push(craft);
		return;
	}
	const struct object_record *object = &g_object_table[object_index];
	const struct object_record *aimed = &g_object_table[target];
	flight_geometry_local_to_world_i16(object, 0, BOARDING_SEPARATION_UP,
					   0);
	craft->push_accum_x = g_rotated_x + aimed->world_x - object->world_x;
	craft->push_accum_y = g_rotated_y + aimed->world_y - object->world_y;
	craft->push_accum_z = g_rotated_z + aimed->world_z - object->world_z;
}

void ai_boarding_step(uint16_t object_index)
{
	uint8_t phase = ai_boarding_phase(object_index);
	if (phase == AI_BOARDING_PHASE_NONE) {
		return;
	}
	if (boarding_must_abort(object_index, phase)) {
		boarding_abandon(object_index, phase);
	} else if (phase == AI_BOARDING_PHASE_ALIGN) {
		boarding_align(object_index);
	} else if (phase == AI_BOARDING_PHASE_TRANSFER) {
		boarding_transfer_phase(object_index);
	} else {
		boarding_separate(object_index);
	}
}

#ifndef XVT_FLIGHT_AI_STATE_H
#define XVT_FLIGHT_AI_STATE_H

/* The computer pilot's part of each craft record: the order the craft
 * works on, its target and aim point, its maneuver and timers, and the
 * goals and rates the steering in flight.c turns it by. The pilots write
 * it; steering, collisions, the HUD and the world save read it. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes in each craft's AI notebook (struct craft_data ai_notebook).
 * The notebook rides the world save, both world checksums and a rewind, so a
 * new size is a new save format. */
enum { AI_NOTEBOOK_BYTES = 64 };

enum ai_targeting_constant {
	AI_TARGET_ABORT = 251,
	AI_DECOY_RANGE_SMALL = 0x8000,
	AI_DECOY_RANGE_LARGE = 0x10000,
	AI_TARGET_RANGE_MAX = 0x10000,
	AI_TURRET_TARGET_PENALTY = 0x8000,
};

struct ai_order_progress {
	/* Per order slot: 2 once its order's goal was met, 3 once a boarding
	 * order had no target left, else 0. The 1997 AI set it; spawn clears
	 * it and nothing sets it now. */
	uint8_t completion_state[4];
	/* Per order slot, progress toward the order's goal (laps of the
	 * waypoints, boardings, deliveries), compared with the order's
	 * variable1 or variable2. */
	uint8_t goal_progress[4];
};

/* Stored as uint8_t in the binary. Values index the maneuver dispatch tables. */
typedef uint8_t ai_maneuver_mode;

enum {
	AI_MANEUVER_MODE_NULL = 0,
	AI_MANEUVER_MODE_TURN_INSIDE = 1,
	AI_MANEUVER_MODE_SPLITS = 2,
	AI_MANEUVER_MODE_IMMELMANN = 3,
	AI_MANEUVER_MODE_SCISSORS = 4,
	AI_MANEUVER_MODE_RENDEZVOUS = 5,
	AI_MANEUVER_MODE_CRUISE = 6,
	AI_MANEUVER_MODE_HEAD_TOWARD_FULL = 7,
	AI_MANEUVER_MODE_RUN_AWAY = 8,
	AI_MANEUVER_MODE_HEAD_ON_ATTACK = 9,
	AI_MANEUVER_MODE_FOLLOW_LEADER = 10,
	AI_MANEUVER_MODE_SETUP_ATTACK = 11,
	AI_MANEUVER_MODE_ATTACK = 12,
	AI_MANEUVER_MODE_ZOOM = 13,
	AI_MANEUVER_MODE_DIVE = 14,
	AI_MANEUVER_MODE_SPLITS_DIVE = 15,
	AI_MANEUVER_MODE_SPEED_AWAY = 16,
	AI_MANEUVER_MODE_ESCORT = 17,
	AI_MANEUVER_MODE_BOARD = 18,
	AI_MANEUVER_MODE_AWAIT_BOARD = 19,
	AI_MANEUVER_MODE_HEAD_TOWARD = 20,
	AI_MANEUVER_MODE_INTO_HYPERSPACE = 21,
	AI_MANEUVER_MODE_OUT_OF_HYPERSPACE = 22,
	AI_MANEUVER_MODE_ROCKET_ATTACK = 23,
	AI_MANEUVER_MODE_TURN_AWAY = 24,
	AI_MANEUVER_MODE_STOP = 25,
	AI_MANEUVER_MODE_OUT_OF_HANGAR = 26,
	AI_MANEUVER_MODE_EVASIVE = 27,
	AI_MANEUVER_MODE_AVOID_STARSHIP = 28,
	AI_MANEUVER_MODE_WAIT = 29,
	AI_MANEUVER_MODE_DROPOFF = 30,
	AI_MANEUVER_MODE_KAMIKAZE = 31,
	AI_MANEUVER_MODE_AVOID_ATTACKER = 32,
	AI_MANEUVER_MODE_KAMIKAZE_COPY = 33,
	AI_MANEUVER_MODE_COUNT = 34,
};

/* What a craft is doing, as far as the engine must know: the HUD's status
 * line, collisions near hangars, the player's radio commands and the turret
 * mounts read it. Only the pilot program sets it; until it does, every craft's
 * is AI_ACTIVITY_NONE. Beside each, the 1997 plans it stands for. */
enum ai_activity {
	AI_ACTIVITY_NONE = 0, /* nullpln */
	AI_ACTIVITY_HOLDING_STATION =
		1,		      /* stationaryldrpln, stationaryflwpln */
	AI_ACTIVITY_IN_FORMATION = 2, /* formldr1pln, formflw1pln, form evade */
	AI_ACTIVITY_WAITING_FOR_GO =
		3,		 /* craftwaitforgopln, starshipwaitforgopln */
	AI_ACTIVITY_WAITING = 4, /* waitpln */
	AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE = 5, /* outofhyperspacepln */
	AI_ACTIVITY_LEAVING_BY_HYPERSPACE =
		6, /* intohyperspacepln, starshipintohyperpln */
	AI_ACTIVITY_LEAVING_HANGAR = 7,	 /* exithangarpln */
	AI_ACTIVITY_ENTERING_HANGAR = 8, /* enterhangarpln */
	AI_ACTIVITY_GOING_HOME =
		9, /* flyhomepln, followhomepln, starshipfollowhomepln */
	AI_ACTIVITY_GOING_HOME_EVADING =
		10,		   /* flyhomeevadepln, followhomeevadepln */
	AI_ACTIVITY_BOARDING = 11, /* board2pln, board3pln, boardtogivepln */
	AI_ACTIVITY_DISABLED = 12, /* disabledpln */
	/* Looking for a craft to board, to capture and to pick up: the status
	 * lines a boarder shows before it docks, one for each kind of boarding
	 * order; BOARDING is the dock itself. */
	AI_ACTIVITY_LOOKING_TO_BOARD = 13,
	AI_ACTIVITY_LOOKING_TO_CAPTURE = 14,
	AI_ACTIVITY_LOOKING_TO_PICK_UP = 15,
	AI_ACTIVITY_COUNT = 16,
};

struct ai_controller {
	/* Mission order slot, 0 to 3, the craft works on: 0 at spawn, moved by
	 * the order-switching orders, 3 after a skip to order 4. */
	uint8_t current_order_slot;
	/* Completion state and goal progress of each order slot. */
	struct ai_order_progress order_progress;
	/* 1 once the skip-to-order-4 trigger moved the craft to slot 3. */
	uint8_t skipped_to_order4;
	/* What the craft is doing, an AI_ACTIVITY_ value. */
	uint8_t activity;
	/* Held the 1997 AI's plan for the current order. */
	uint8_t unused0b;
	/* The mission point the craft is flying to; waypoints are points 4
	 * to 11. The drop-off order reuses it: it starts at 0 when the craft
	 * reaches the destination group, names the formation slot of the next
	 * craft to deliver, and counts the deliveries made. */
	uint8_t waypoint_index; /* 4 at spawn; past 11 it wraps to 4. */
	/* 1 while a player's wait command (Shift+W) holds the craft: set by
	 * Shift+W, cleared by Shift+G, Shift+E and the wingman attack and cover
	 * orders. The pilot program reads it and keeps the craft waiting. */
	uint8_t wait_command;
	/* Ticks between two AI thinks: the AI level's g_ai_think_interval_by_skill
	 * entry at spawn; a few orders and maneuvers set 29 or 59. */
	int think_interval;
	/* Ticks to the next AI think. Spawn spreads it by slot over one
	 * think_interval; flight_update_timers lowers it by the ticks that
	 * pass; at 0 or below ai_brain_think_due_craft asks the pilot program
	 * and adds think_interval. While no pilot program is set, nothing asks
	 * and it keeps falling. */
	int think_timer;
	/* The craft's own state of the game's random generator:
	 * collide_damagecraft swaps it into g_game_rand_feedback_state while it
	 * works on the craft, as the 1997 AI's think did. game_rand() XOR
	 * 0xBEEF at spawn. */
	int16_t saved_rand_seed;
	/* What the craft steers or fires at: an object index, 0x8000 plus a
	 * mission point of its flight group (0x8000 alone for the group's
	 * current point), or 0xFFFF for none. */
	uint16_t target_obj_idx;
	/* The target object's object_signature when it was chosen, to spot its
	 * slot holding another object later; 0 for a mission point. */
	uint16_t target_signature;
	/* Mesh of the target that warheads fired now aim at; laser.c copies it
	 * into each warhead's guidance. 0xFFFF at spawn. */
	uint16_t target_component;
	/* 1 when a target search or command set the target, 0 when an order
	 * pointed the craft at a mission point or at its leader's attacker.
	 * The 1997 AI read it only for the cannons of one disabling plan: with
	 * it set the ion cannons fired, their speed setting the aim ahead, and
	 * without it the others. Spawn clears it; nothing else uses it now. */
	uint8_t has_live_target;
	/* X of the world point the craft steers at: its target's position, a
	 * point ahead of it, a hangar or docking point, or a mission point. */
	int aim_point_x;
	int aim_point_y; /* Y of the aim point. */
	/* Z of the aim point; the climb test of the steering code compares it
	 * with the craft's Z. */
	int aim_point_z;
	/* Target a player's command gave the craft, which the target orders
	 * take up; 0xFFFF for none. The evade command (Shift+E) sets
	 * AI_TARGET_ABORT (251): the pilot program drops its target and clears
	 * it, as the 1997 AI did. */
	uint16_t candidate_target_idx;
	/* Flight group the craft escorts, set by the 1997 AI; 255 for none and
	 * at spawn, and nothing sets it now. */
	uint8_t escort_target_fg;
	/* Pitch the steering turns the craft to, in angle units, 0x4000 being
	 * level; 0x4000 at spawn. */
	uint16_t target_z_angle;
	/* Roll the steering turns the craft to; with roll_state 3 only its top
	 * bit counts, choosing the way the craft keeps rolling. */
	uint16_t target_roll;
	/* Heading the steering turns the craft to, in angle units. */
	uint16_t target_xy_angle;
	/* Maneuver the craft flies, an AI_MANEUVER_MODE_ value; the 1997 AI ran
	 * its step function on each think. AI_MANEUVER_MODE_NULL at spawn. */
	ai_maneuver_mode maneuver_mode;
	/* Step within the maneuver; 0 at spawn. */
	uint8_t maneuver_phase;
	/* Ticks left in the maneuver or its current step; flight_update_timers
	 * lowers it to 0. The HUD's target display shows it as the order
	 * time. */
	int maneuver_timer;
	/* Ticks to a maneuver's next sub-step, such as a weave or a course
	 * update; flight_update_timers lowers it to 0. */
	int16_t secondary_maneuver_timer;
};

struct ai_flight_state {
	/* Object whose shot last hit the craft, set by collide.c on each hit;
	 * 0xFFFF when a plan starts. */
	uint16_t threat_obj_idx;
	/* Object the craft last collided with, kept while the roll that gave it
	 * lasts; 0xFFFF otherwise and at spawn. */
	uint16_t impact_obj_idx;
	/* 1 once all the craft's orders are complete, or its leader's are; it
	 * lets the departure count as departed. */
	uint8_t go_home_flag;
	/* 1 once the craft has aborted the mission, by a player's radio command
	 * (the 1997 AI also aborted on the order's condition); the abort outcome
	 * is counted once. */
	uint8_t mission_aborted_flag;
	/* 1 once the flight group's departure had started for the craft, set
	 * by the 1997 AI for itself or from its leader; the delay counted from
	 * the departClock fields. Spawn clears it and nothing sets it now. */
	uint8_t depart_timer_flag;
	/* Mission clock hours when the departure started. */
	uint8_t depart_clock_hours;
	/* Mission clock minutes when the departure started. */
	uint8_t depart_clock_minutes;
	/* Mission clock seconds when the departure started. */
	uint8_t depart_clock_seconds;
	/* Warheads fired since the maneuver started; the 1997 AI stopped firing
	 * at its limit. Nothing uses it now. */
	uint8_t warheads_fired_this_maneuver;
	/* Hits taken since the maneuver started, counted by collide.c; the 1997
	 * AI broke off an attack at the model's reaction_threshold. */
	uint8_t hits_this_maneuver;
	/* 1 once a boarding of this craft counted its group's
	 * FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION outcome. */
	uint8_t boarded_accounting_done;
	uint8_t times_boarded; /* Times other craft have boarded this one. */
	/* 1 once this craft's first docking counted its group's
	 * FLIGHT_GROUP_OUTCOME_FAILED_MISSION outcome. */
	uint8_t docking_accounting_done;
	/* Signatures recorded in docked_target_signatures, at most 9 after a
	 * docking: a tenth docking writes slot 9, which no search reads, and
	 * later ones overwrite it. */
	uint8_t docked_target_count;
	/* Signatures of objects the craft has docked with, so it does not board
	 * them again. Slot 0 also keeps think_interval while the craft arrives
	 * from hyperspace. */
	uint16_t docked_target_signatures[10];
	/* The model's max_speed, copied at spawn: the base of the AI's speeds,
	 * and 0 for a craft that cannot move, which several orders skip. */
	int16_t max_speed_cache;
	/* Set to -1 at spawn; nothing reads it except the world snapshot's
	 * copy. */
	int16_t motion_scale;
	/* 1 while a climb runs, which the steering code ends at the aim point's
	 * Z by setting a level pitch. The 1997 AI's cruise set 1 and cleared
	 * it in the same think; nothing sets 1 now. */
	uint8_t climb_state;
	/* 1 while a dive pull-out runs, which
	 * flight_update_dive_pullout_pitch_target steps and ends with 2. Nothing
	 * sets it to 1, so that function never runs. */
	uint8_t dive_state;
	/* The model's pitch rate, in angle units per
	 * SIMULATION_TICKS_PER_SECOND ticks at a full step, copied at spawn. */
	int16_t pitch_rate;
	/* Fraction of 65,536 scaling the pitch step; -1, all of it, from spawn
	 * on. */
	int16_t pitch_accel;
	/* 0 no pitch; 1 lowers the pitch value toward target_z_angle; 2 raises
	 * it; 3 reached. */
	uint8_t pitch_state;
	/* 1 to pitch on past target_z_angle: when the pitch crosses 0 or 0x8000
	 * the steering flips yaw and roll by half a circle, clears this and
	 * turns back toward the target. */
	uint8_t pitch_through_loop;
	/* Fraction of 65,536 scaling the pitch step. */
	uint16_t pitch_step_scale;
	/* The model's roll rate, in angle units per SIMULATION_TICKS_PER_SECOND
	 * ticks at a full step, copied at spawn. */
	int16_t roll_rate;
	/* Fraction of 65,536 scaling the roll step; -1, all of it, from spawn
	 * on. */
	int16_t roll_accel;
	/* 0 no roll; 1 or 2 roll to target_roll (nothing sets 2); 3 keep
	 * rolling; 4 reached. In 0 and 4 the steering banks the craft into its
	 * turns. */
	uint8_t roll_state;
	/* Fraction of 65,536 scaling the roll step, which the steering
	 * doubles. */
	uint16_t roll_step;
	/* The model's yaw rate, in angle units per SIMULATION_TICKS_PER_SECOND
	 * ticks at a full step, copied at spawn. */
	int16_t turn_rate;
	/* Fraction of 65,536 scaling the turn step; -1, all of it, from spawn
	 * on. */
	int16_t turn_accel;
	/* 0 no turn; 1, 2 or 3 turn to target_xy_angle, 3 being set when the yaw
	 * gets there. */
	uint8_t turn_state;
	/* Fraction of 65,536 scaling the turn step, kept in 16 signed bits. */
	int16_t turn_step;
	/* Formation, 0 to 33, indexing g_form_pos_x, Y and Z: the flight group's
	 * at spawn, set again when the group leaves a hangar. */
	uint8_t formation_type;
	/* Formation spacing; a follower scales its g_formPos offsets by its
	 * leader's plus 1. The flight group's at spawn; going home sets 1. */
	uint8_t separation;
};

#ifdef __cplusplus
}
#endif

#endif

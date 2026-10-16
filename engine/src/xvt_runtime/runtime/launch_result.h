#ifndef XVT_RUNTIME_LAUNCH_RESULT_H
#define XVT_RUNTIME_LAUNCH_RESULT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The JSON result file that --launch writes when it ends. No state. */

/* The numbers the engine exits with after --launch. */
enum {
	XVT_LAUNCH_EXIT_FLOWN = 0,
	XVT_LAUNCH_EXIT_REFUSED = 3,
	XVT_LAUNCH_EXIT_NOT_REACHED = 4,
	XVT_LAUNCH_EXIT_FLIGHT_FAILED = 5
};

enum {
	XVT_LAUNCH_OUTCOME_NONE = 0,
	XVT_LAUNCH_OUTCOME_COMPLETED = 1,
	XVT_LAUNCH_OUTCOME_NOT_COMPLETED = 2
};

/* The battle's Sith block. With setting XVT_SITH_NONE the document always
 * shows flight_group -1, craft_type -1, no name, 0 counts and no why, whatever
 * the other members hold. */
struct xvt_launch_sith {
	int setting;	  /* enum xvt_sith_setting */
	int flight_group; /* the promoted group, -1 when none */
	char flight_name[21];
	int craft_type; /* -1 when none */
	int apprentices_flown;
	int apprentices_lost;
	int lord_flown;
	int lord_lost;
	char why[128];
};

/* The most players and teams a battle reports: the pilot record keeps 8
 * network players, and the game writes the results of teams 0 to 7 only. */
enum { XVT_LAUNCH_PLAYERS = 8, XVT_LAUNCH_TEAMS = 8 };

/* The sizes of the pilot's own tables: craft types, the ranks of human
 * pilots, the ranks of computer pilots, and flight groups. */
enum {
	XVT_LAUNCH_CRAFT_TYPES = 100,
	XVT_LAUNCH_PILOT_RANKS = 25,
	XVT_LAUNCH_AI_RANKS = 6,
	XVT_LAUNCH_FLIGHT_GROUPS = 48
};

/* One player of the battle, as the game's end of battle counted it. */
struct xvt_launch_player {
	int entry; /* the player's place in the pilot record, 0 to 7 */
	/* The player's place in the flight, -1 when no place in the flight
	 * holds the player; then team is -1 and the flight's own counts below
	 * (flight_group, shots, hits, warheads, warhead_hits) are 0. */
	int slot;
	char name
		[14]; /* the name the debriefing shows, 12 characters at most */
	int team;
	int flight_group;
	int rating; /* the player's rank at launch, as a number */
	int score;  /* the player's score plus the team's bonus */
	int kills;
	int shared;
	int assists;
	int losses;
	int shots; /* laser and ion shots */
	int hits;  /* laser and ion hits */
	int warheads;
	int warhead_hits;
	int has_left; /* 1 when the player left before the end */
};

/* One team of the battle. */
struct xvt_launch_team {
	int team;
	int completed; /* 1 when its primary goals were met and none prevented */
	int score;
	int kills;
	int shared;
	int losses;
	int seconds; /* when the team met its goals, 0 when it never did */
};

/* The local pilot's own record of the battle, the numbers its debriefing
 * shows: kills, shared kills and assists by the victim's craft type, by the
 * rank of a human victim and of a computer victim; deaths by the killer's
 * rank; kills of each player and of each flight group, and theirs of the
 * pilot. Other machines' pilots are not here: each machine keeps only its
 * own pilot's tables. */
struct xvt_launch_detail {
	int shots; /* laser and ion shots */
	int hits;
	int warheads;
	int warhead_hits;
	int losses;
	int losses_to_collisions;
	int losses_to_starships;
	int losses_to_mines;
	int friendlies_killed;
	int special_inspected;
	int kills_by_craft[XVT_LAUNCH_CRAFT_TYPES];
	int shared_by_craft[XVT_LAUNCH_CRAFT_TYPES];
	int assists_by_craft[XVT_LAUNCH_CRAFT_TYPES];
	int kills_on_pilot_rank[XVT_LAUNCH_PILOT_RANKS];
	int shared_on_pilot_rank[XVT_LAUNCH_PILOT_RANKS];
	int assists_on_pilot_rank[XVT_LAUNCH_PILOT_RANKS];
	int kills_on_ai_rank[XVT_LAUNCH_AI_RANKS];
	int shared_on_ai_rank[XVT_LAUNCH_AI_RANKS];
	int assists_on_ai_rank[XVT_LAUNCH_AI_RANKS];
	int killed_by_pilot_rank[XVT_LAUNCH_PILOT_RANKS];
	int killed_by_ai_rank[XVT_LAUNCH_AI_RANKS];
	int kills_on_player[XVT_LAUNCH_PLAYERS]; /* by pilot record entry */
	int shared_on_player[XVT_LAUNCH_PLAYERS];
	int kills_from_player[XVT_LAUNCH_PLAYERS];
	int shared_from_player[XVT_LAUNCH_PLAYERS];
	int kills_on_flight_group[XVT_LAUNCH_FLIGHT_GROUPS];
	int shared_on_flight_group[XVT_LAUNCH_FLIGHT_GROUPS];
	int kills_from_flight_group[XVT_LAUNCH_FLIGHT_GROUPS];
	int shared_from_flight_group[XVT_LAUNCH_FLIGHT_GROUPS];
};

struct xvt_launch_result {
	int exit_code;	    /* one of the XVT_LAUNCH_EXIT values */
	int outcome;	    /* one of the XVT_LAUNCH_OUTCOME values */
	int score;	    /* the pilot's score in the battle */
	int kills;	    /* full kills */
	int losses;	    /* craft the pilot lost */
	/* Seconds into the mission when the team met its goals; 0 when it never
	 * did, so a battle left before then has 0. */
	int goals_met_seconds;
	char why[256];	    /* empty after a battle that was flown */
	struct xvt_launch_sith sith; /* zeroed: setting none */
	/* The local pilot's place in the pilot record (the entry of one of
	 * players), -1 when it has none; zeroed means entry 0, so a battle not
	 * flown sets -1. */
	int local_entry;
	int player_count; /* 0 to XVT_LAUNCH_PLAYERS */
	struct xvt_launch_player players[XVT_LAUNCH_PLAYERS];
	int team_count; /* 0 to XVT_LAUNCH_TEAMS */
	struct xvt_launch_team teams[XVT_LAUNCH_TEAMS];
	struct xvt_launch_detail detail;
};

/* The longest document xvt_launch_result_format can make, terminator
 * included: every table full of the longest numbers fits. */
enum { XVT_LAUNCH_RESULT_MAX = 65536 };

/* Formats r as one JSON document on one line, then a newline, in out:
 * schema_version 4, status ("flown" for exit code 0, "refused" for 3,
 * "not_reached" for 4, "failed" for 5),
 * exit_code, outcome ("none", "completed" or "not_completed"), score, kills,
 * losses, goals_met_seconds, why, sith (an object: setting "none",
 * "apprentices" or "lord", flight_group, flight_name, craft_type,
 * apprentices_flown, apprentices_lost, lord_flown, lord_lost, why; every
 * member always present), local_entry, players (the first player_count, in
 * order: entry, slot, name, team, flight_group, rating, score, kills,
 * shared, assists, losses, shots, hits, warheads, warhead_hits, has_left as
 * true or false), teams (the first team_count: team, completed as true or
 * false, score, kills, shared, losses, seconds) and detail (shots, hits,
 * warheads, warhead_hits, losses, losses_to_collisions, losses_to_starships,
 * losses_to_mines, friendlies_killed, special_inspected, then lists that
 * hold only their rows with a number other than 0, lowest index first:
 * by_craft {craft_type, kills, shared, assists}, on_pilot_ranks and
 * on_ai_ranks {rank, kills, shared, assists}, killed_by_pilot_ranks and
 * killed_by_ai_ranks {rank, times}, on_players and from_players {entry,
 * kills, shared}, on_flight_groups and from_flight_groups {flight_group,
 * kills, shared}). The members version 3 had keep their meaning. Returns the
 * document's length, or 0 when it does not fit in size bytes with its
 * terminator, or r holds a number outside the codes above, a sith setting
 * that is not one of the three, or a count outside its range. */
size_t xvt_launch_result_format(const struct xvt_launch_result *r, char *out,
				size_t size);
/* Writes the document of r to path. Returns 1, or 0 when the file cannot be
 * written. */
int xvt_launch_result_write(const struct xvt_launch_result *r,
			    const char *path);

#ifdef __cplusplus
}
#endif

#endif

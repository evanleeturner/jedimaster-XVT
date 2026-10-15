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
};

/* Formats r as one JSON document on one line, then a newline, in out:
 * schema_version 3, status ("flown" for exit code 0, "refused" for 3,
 * "not_reached" for 4, "failed" for 5),
 * exit_code, outcome ("none", "completed" or "not_completed"), score, kills,
 * losses, goals_met_seconds, why and sith (an object: setting "none",
 * "apprentices" or "lord", flight_group, flight_name, craft_type,
 * apprentices_flown, apprentices_lost, lord_flown, lord_lost, why; every
 * member always present). Returns the document's length, or 0 when it
 * does not fit in size bytes with its terminator or r holds a number outside
 * the codes above or a sith setting that is not one of the three. */
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

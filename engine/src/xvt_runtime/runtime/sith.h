#ifndef XVT_RUNTIME_SITH_H
#define XVT_RUNTIME_SITH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Sith setting: whether a battle's enemy fighters include the two ranks
 * above Jedi, 6 Sith Apprentice and 7 Sith Lord. State: the setting, none at
 * start. The launch description sets it on the machine that is launched, the
 * host's options packet carries it to the other machines of a network
 * game, and the mission start reads it. */

enum xvt_sith_setting {
	/* No Sith: every flight group keeps the level the mission file and
	 * the difficulty rules gave it. */
	XVT_SITH_NONE = 0,
	/* The enemy's highest-ranked computer starfighter flight is promoted
	 * to level 6. */
	XVT_SITH_APPRENTICES = 1,
	/* The same flight is promoted to level 6 and the first craft of its
	 * first wave flies at level 7. */
	XVT_SITH_LORD = 2
};

/* Sets the setting; a value that is not one of enum xvt_sith_setting sets
 * XVT_SITH_NONE. */
void xvt_sith_set_setting(int setting);
/* The setting now. */
int xvt_sith_get_setting(void);
/* Sets the setting from a launch description's value: XVT_SITH_NONE, XVT_SITH_APPRENTICES,
 * XVT_SITH_LORD, or XVT_LAUNCH_SETTING_UNSET (-1) when the description
 * leaves it out, which sets XVT_SITH_NONE. */
void xvt_sith_set_from_description(int described);
/* The levels the promotion gives: 6 for the Sith Apprentice flight and 7 for
 * the Sith Lord's own craft. */
enum { XVT_SITH_APPRENTICE_LEVEL = 6, XVT_SITH_LORD_LEVEL = 7 };

/* Promotes one flight group by the setting, once at mission start after the
 * difficulty and melee rules. With the setting none it does nothing and logs
 * nothing. Otherwise it looks, among the flight groups no player can fly
 * (player_number 0) whose team is hostile to player 0's team (the test the
 * Hard difficulty uses) and whose craft type is a starfighter, for the one
 * with the highest AI level, the lowest index on a tie. That group's level
 * becomes 6 (if it was lower), and with XVT_SITH_LORD the first craft of its
 * first wave is to fly at level 7 (xvt_sith_craft_level). With no such group
 * nothing changes. The same mission file, settings and player 0 team give
 * the same choice on every machine: nothing here reads a random number or
 * the clock. */
void xvt_sith_promote_flight_groups(void);
/* The flight group whose first craft is the Sith Lord, or -1 for none. Set by
 * xvt_sith_promote_flight_groups, cleared by each call of it. */
int xvt_sith_lord_flight_group(void);
/* The level a craft spawns with: group_level, except
 * XVT_SITH_LORD_LEVEL for the first craft of the first wave of the Sith
 * Lord's flight group (craft_number 0, the group's count of craft spawned so
 * far). */
int xvt_sith_craft_level(int flight_group, int group_level, int craft_number);
/* The setting's word in logs and descriptions: "none", "apprentices" or
 * "lord"; "none" for any other value. */
const char *xvt_sith_setting_name(int setting);

/* What the battle's Sith setting did, for the battle result. */
struct xvt_sith_report {
	int setting;	       /* enum xvt_sith_setting */
	int flight_group;      /* the promoted group, -1 when none */
	int craft_type;	       /* its craft type, -1 when none */
	char flight_name[21];  /* its name, "" when none */
	int apprentices_flown; /* craft at level 6 that spawned */
	int apprentices_lost;  /* of those, destroyed */
	int lord_flown;	       /* 0 or 1 */
	int lord_lost;
	/* "" when a group was promoted or the setting is none, else the reason
	 * sith.none_to_promote logs. */
	char why[128];
};

/* Counts one craft destroyed, by the level it flew at: 6 adds to the
 * apprentices lost, 7 to the Lord lost, any other level nothing. Called by the
 * mission code when a craft's end is recorded as destroyed. A craft that left
 * by hyperspace or still flew at the end is not counted. */
void xvt_sith_note_craft_destroyed(int ai_rank);
/* Fills out for the battle now ending: the promotion's choice, the craft the
 * flight group spawned (its count of craft created, the Lord being the first
 * when the setting is lord) and the destroyed counts. With the setting none,
 * flight_group and craft_type are -1 and the counts 0. */
void xvt_sith_get_report(struct xvt_sith_report *out);
/* The tag a craft at ai_rank carries after its name on the cockpit's target
 * readouts: " Sith" at XVT_SITH_APPRENTICE_LEVEL, " Sith Lord" at
 * XVT_SITH_LORD_LEVEL, "" at any other value. The leading space is part of
 * the tag. */
const char *xvt_sith_rank_tag(int ai_rank);
/* Appends the tag of the target in object slot object_slot to
 * g_flight_text_scratch_buffer, where the cockpit has just formatted the
 * target's name. It appends only when the slot is a craft slot (below
 * g_active_region_craft_object_slot_end) holding a mobile object with a craft
 * record whose ai_rank has a tag; every other slot leaves the buffer
 * untouched. With log_change nonzero, which the readouts pass where they
 * draw the name afresh (a new target, or the cockpit redrawn after a view
 * change or a hit to the player's systems), an appended tag also logs
 * sith.target_tag. Returns 1 when a tag was appended, else 0. */
int xvt_sith_append_target_tag(unsigned int object_slot, int log_change);
/* Ends the flight: the setting returns to none and the Lord's group, the
 * promoted group and the counts are cleared, so a later battle in the same
 * process inherits nothing. */
void xvt_sith_end_flight(void);

#ifdef __cplusplus
}
#endif

#endif

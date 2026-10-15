#ifndef XVT_RUNTIME_SITH_H
#define XVT_RUNTIME_SITH_H

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
/* Ends the flight: the setting returns to none and the Lord's group is
 * cleared, so a later battle in the same process inherits nothing. */
void xvt_sith_end_flight(void);

#ifdef __cplusplus
}
#endif

#endif

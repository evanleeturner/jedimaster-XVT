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
/* The setting's word in logs and descriptions: "none", "apprentices" or
 * "lord"; "none" for any other value. */
const char *xvt_sith_setting_name(int setting);

#ifdef __cplusplus
}
#endif

#endif

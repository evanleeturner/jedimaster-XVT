#ifndef XVT_RUNTIME_LAUNCH_OPTIONS_H
#define XVT_RUNTIME_LAUNCH_OPTIONS_H

#include "xvt/frontend/config.h"
#include "xvt_runtime/runtime/launch_description.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game options a battle started from a description flies with. A solo
 * battle changes the player's own options, as its screens do. A network battle
 * needs the same options on every machine: the 1997 host chose them on its
 * screens and sent them to each joiner (GAME_OPTIONS), and a door battle has no
 * such screens, so each machine computes them from what every machine holds
 * alike, the game's shipped settings file and the description. Nothing here
 * reads or writes the game's state; the caller passes the options. */

/* Sets in game each setting that settings gives, leaving the unset ones. The
 * Sith setting is not a game option and is left to the caller. */
void xvt_launch_options_apply_settings(
	struct game_config *game, const struct xvt_launch_settings *settings);

/* Makes the changes a click on mission type directory's button makes to game
 * when the pilot's type was another: continue the sequence; easy cheat becomes
 * easy outside campaigns; random setup 2 becomes 0 outside battles, and any
 * random setup becomes 0 in campaigns. */
void xvt_launch_options_type_rules(struct game_config *game, int directory);

/* Sets game's options for a network description, the same on every machine,
 * in the order a 1997 host set them: the seventeen its lobby sent its joiners
 * (sixteen options and the seed) start from shipped, the options of the
 * shipped settings file; the type rules run as when the pilot's type was
 * another, whatever type each pilot chose last; the description's settings
 * then replace theirs, as the host's clicks after picking the type did; the
 * seed is the description's. Every other field of game stays as it was. */
void xvt_launch_options_network(
	struct game_config *game, const struct game_config *shipped,
	const struct xvt_launch_description *description);

#ifdef __cplusplus
}
#endif

#endif

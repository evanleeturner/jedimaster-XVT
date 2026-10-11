#ifndef XVT_RUNTIME_LAUNCH_DESCRIPTION_H
#define XVT_RUNTIME_LAUNCH_DESCRIPTION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The JSON description of one single-player battle that --launch reads, and
 * the checks on it. Nothing here touches the game's state: the text goes in,
 * a plain structure or a plain refusal sentence comes out. No state. */

enum {
	XVT_LAUNCH_NAME_CAPACITY = 14,
	XVT_LAUNCH_FILE_CAPACITY = 64,
	XVT_LAUNCH_PATH_CAPACITY = 512,
	XVT_LAUNCH_WHY_CAPACITY = 256
};

/* The side a pilot flies for, as the pilot record's faction number. */
enum {
	XVT_LAUNCH_SIDE_NONE = -1,
	XVT_LAUNCH_SIDE_REBEL = 0,
	XVT_LAUNCH_SIDE_IMPERIAL = 1
};

/* A setting the description left out keeps what the game's own settings file
 * holds; xvt_launch_setting_unset marks that. */
enum { XVT_LAUNCH_SETTING_UNSET = -1 };

struct xvt_launch_settings {
	/* Game order: 0 easy, 1 medium, 2 hard, 3 easy cheat. */
	int difficulty;
	int collisions;	   /* 0 or 1 */
	int craft_jumping; /* 0 or 1 */
	int random_setup;  /* 0 or 1 */
	/* The stored value: 0, 1 or 2 for 2, 3 or 4 wins. */
	int battle_length;
	/* Minutes 1 to 20, 0 for none, 255 for the mission's own. */
	int mission_time_limit;
	int continue_sequence; /* 0 restart, 1 continue */
};

struct xvt_launch_description {
	/* mission_directory_id: 0 training, 1 melee, 2 tournament, 3 combat,
	 * 4 battle, 5 campaign. */
	int directory_id;
	int side;
	char mission_file[XVT_LAUNCH_FILE_CAPACITY]; /* lowercase, no folder */
	char pilot[XVT_LAUNCH_NAME_CAPACITY];
	char result_path[XVT_LAUNCH_PATH_CAPACITY];
	int has_seed;
	unsigned int seed;
	struct xvt_launch_settings settings;
};

/* Reads and checks a description. The text must be a JSON object holding
 * exactly these members, none repeated: "schema_version" (1), "mode"
 * ("single_player"; anything else is refused), "game_type" (training, melee,
 * tournament, combat, battle or campaign), "side" (rebel, imperial or none:
 * training, combat and campaign need a side, the others take none),
 * "mission" ("<folder>/<file>", the folder being the game type's own and
 * the file letters, digits, underscore and dot), "pilot" (see
 * xvt_launch_name_check) and "result" (a path, at most 511 characters, or
 * absent). It may also hold "seed" (0 to 4294967295) and "settings" with any
 * of difficulty (easy, medium, hard, easy_cheat), collisions, craft_jumping,
 * random_setup (booleans), battle_length (2, 3 or 4), mission_time_limit
 * (none, default or 1 to 20) and continue_sequence (restart or continue).
 * Easy cheat is for a campaign only. On success fills out (settings not
 * given are XVT_LAUNCH_SETTING_UNSET, an absent result path is empty) and
 * returns 1. Otherwise returns 0 with a plain sentence in why, at most
 * XVT_LAUNCH_WHY_CAPACITY - 1 characters, and out holds only what was read
 * before the refusal. */
int xvt_launch_description_parse(const char *text,
				 struct xvt_launch_description *out, char *why,
				 size_t why_size);
/* Checks a pilot or game name. It must hold 1 to 12 letters, digits, spaces,
 * underscores, hyphens or dots, start and end with a letter or digit, and
 * hold none of the flight switch words (traincourse, nopilot, dinput, sfx,
 * music, voice, tickcounter, mipmaps, inprogress, newnet, nolauncher,
 * fullscreen, pageflip; found in any letter case, because the flight finds
 * them by substring anywhere in the launch line). Returns 1, or 0 with a plain
 * sentence in why naming the problem. */
int xvt_launch_name_check(const char *name, char *why, size_t why_size);
/* Checks text that goes into the launch line: a game name, a formal name. It
 * must hold no tilde, no control character and none of the switch words of
 * xvt_launch_name_check. Returns 1, or 0 with a plain sentence in why. */
int xvt_launch_text_check(const char *text, char *why, size_t why_size);
/* The folder name of a game type's missions ("train", "melee", ...), or NULL
 * for a number outside 0 to 5. */
const char *xvt_launch_directory_name(int directory_id);

#ifdef __cplusplus
}
#endif

#endif

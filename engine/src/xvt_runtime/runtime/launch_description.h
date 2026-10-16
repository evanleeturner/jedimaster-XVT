#ifndef XVT_RUNTIME_LAUNCH_DESCRIPTION_H
#define XVT_RUNTIME_LAUNCH_DESCRIPTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The JSON description of one battle that --launch reads, single player or
 * network, and the checks on it. Nothing here touches the game's state: the
 * text goes in, a plain structure or a plain refusal sentence comes out. No
 * state. */

enum {
	XVT_LAUNCH_NAME_CAPACITY = 14,
	XVT_LAUNCH_FILE_CAPACITY = 64,
	XVT_LAUNCH_PATH_CAPACITY = 512,
	/* "sha256//", 43 base64 digits, "=" and the terminating NUL. */
	XVT_LAUNCH_PIN_CAPACITY = 53,
	XVT_LAUNCH_WHY_CAPACITY = 256,
	/* Seats in a network battle, as many as the game's lobby holds. */
	XVT_LAUNCH_SEAT_CAPACITY = 8
};

/* This machine's part in a network battle. */
enum { XVT_LAUNCH_ROLE_HOST = 0, XVT_LAUNCH_ROLE_JOIN = 1 };

/* A network battle's application id, the fields of a GUID: the secret the
 * host's matchmaker issued for this one battle. Only a game holding it can
 * list the battle's room. */
struct xvt_launch_game_id {
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t data4[8];
};

/* The "network" part of a network description. */
struct xvt_launch_network {
	int role;
	/* The matchmaker's address, as the description gives it. */
	char lobby[XVT_LAUNCH_PATH_CAPACITY];
	/* The pin of the matchmaker's public key, as the matchmaker prints it;
	 * empty when the description gives none, and the system's certificate
	 * check is kept. */
	char lobby_pin[XVT_LAUNCH_PIN_CAPACITY];
	struct xvt_launch_game_id game;
	/* The pilots in seat order, the host's first; seat is the index of
	 * this machine's pilot among them. */
	int player_count;
	char players[XVT_LAUNCH_SEAT_CAPACITY][XVT_LAUNCH_NAME_CAPACITY];
	int seat;
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
	/* The Sith setting (sith.h): 0 none, 1 apprentices, 2 lord. */
	int sith;
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
	/* The AI program to start, from "ai_program": the Python that runs it
	 * and the program's path. Both are empty when it is absent. */
	char ai_python[XVT_LAUNCH_PATH_CAPACITY];
	char ai_path[XVT_LAUNCH_PATH_CAPACITY];
	struct xvt_launch_settings settings;
	/* 1 for "mode": "network", with network filled; 0 for single player,
	 * with network zeroed. */
	int is_network;
	struct xvt_launch_network network;
};

/* Reads and checks a description. The text must be a JSON object holding
 * exactly these members, none repeated: "schema_version" (1), "mode"
 * ("single_player" or "network"), "game_type" (training, melee,
 * tournament, combat, battle or campaign), "side" (rebel, imperial or none:
 * training, combat and campaign need a side, the others take none),
 * "mission" ("<folder>/<file>", the folder being the game type's own and
 * the file letters, digits, underscore and dot), "pilot" (see
 * xvt_launch_name_check) and "result" (a path, at most 511 characters, or
 * absent). It may also hold "seed" (0 to 4294967295), "ai_program" (an
 * object holding only "python" and "path", each 1 to 511 characters) and
 * "settings" with any
 * of difficulty (easy, medium, hard, easy_cheat), collisions, craft_jumping,
 * random_setup (booleans), battle_length (2, 3 or 4), mission_time_limit
 * (none, default or 1 to 20), continue_sequence (restart or continue) and
 * sith (none, apprentices or lord; absent counts as none).
 * Easy cheat is for a campaign only. A network description also holds
 * "network", an object holding exactly "role" (host or join), "lobby" (the
 * matchmaker's address: "https://" and at most 511 characters in all),
 * optionally "lobby_pin" (the matchmaker's key pin: "sha256//", 43 base64
 * digits and "="; given, the game trusts the matchmaker by that key alone),
 * "game" (the application id: 36 characters, lowercase hex digits in groups
 * of 8, 4, 4, 4 and 12 joined by hyphens, as the directory writes it) and
 * "players" (2 to 8 names, each passing xvt_launch_name_check, no two alike
 * in any letter case, "pilot" among them and first when the role is host);
 * its game type is melee or combat, its side is none (the seat decides the
 * side), and it needs "seed", which every machine must share. A single
 * player description holds no "network". On success fills out (settings not
 * given are XVT_LAUNCH_SETTING_UNSET, an absent result path or AI program is
 * empty) and
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
/* The setup fingerprint of a network description: a CRC-32C over the fields
 * every machine of one battle holds alike (game type, mission, seed, the
 * eight settings as read, the application id, the players in seat order),
 * never a machine's own (role, lobby, pilot, result path, AI program). Two
 * descriptions of one battle give one number; a difference in any of those
 * fields almost always gives another. It catches launchers that disagree,
 * not a forgery. */
uint32_t
xvt_launch_setup_fingerprint(const struct xvt_launch_description *description);

#ifdef __cplusplus
}
#endif

#endif

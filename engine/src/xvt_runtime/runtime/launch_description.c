#include "xvt_runtime/runtime/launch_description.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "xvt_runtime/runtime/flight_wire.h"

enum { MAX_NAME_LENGTH = 12, GAME_ID_LENGTH = 36, MIN_NETWORK_PLAYERS = 2 };

static const char *const g_directory_names[6] = {
	"train", "melee", "tourn", "combat", "battle", "campaign"};
static const char *const g_game_types[6] = {"training", "melee",  "tournament",
					    "combat",	"battle", "campaign"};
static const char *const g_switch_words[] = {
	"traincourse", "nopilot",     "dinput",	 "sfx",	       "music",
	"voice",       "tickcounter", "mipmaps", "inprogress", "newnet",
	"nolauncher",  "fullscreen",  "pageflip"};

const char *xvt_launch_directory_name(int directory_id)
{
	if (directory_id < 0 || directory_id > 5) {
		return NULL;
	}
	return g_directory_names[directory_id];
}

/* Writes a refusal sentence into why. */
static int refuse(char *why, size_t why_size, const char *format,
		  const char *detail)
{
	if (why && why_size > 0) {
		snprintf(why, why_size, format, detail);
	}
	return 0;
}

/* 1 when lowered text holds word. */
static int holds_word(const char *lowered, const char *word)
{
	return strstr(lowered, word) != NULL;
}

int xvt_launch_text_check(const char *text, char *why, size_t why_size)
{
	size_t length = strlen(text);
	char *lowered = malloc(length + 1);
	if (!lowered) {
		return refuse(why, why_size, "%s",
			      "There is not enough memory to check a name.");
	}
	int clean = 1;
	for (size_t index = 0; index < length; ++index) {
		unsigned char letter = (unsigned char)text[index];
		lowered[index] = (char)tolower(letter);
		if (letter == '~' || letter < ' ' || letter == 127) {
			clean = 0;
		}
	}
	lowered[length] = '\0';
	if (!clean) {
		free(lowered);
		return refuse(
			why, why_size, "%s",
			"A name holds a tilde or a control character, which the launch line cannot carry.");
	}
	for (size_t index = 0;
	     index < sizeof(g_switch_words) / sizeof(g_switch_words[0]);
	     ++index) {
		if (holds_word(lowered, g_switch_words[index])) {
			free(lowered);
			return refuse(
				why, why_size,
				"A name holds the flight switch word \"%s\", which would change how the game runs.",
				g_switch_words[index]);
		}
	}
	free(lowered);
	return 1;
}

int xvt_launch_name_check(const char *name, char *why, size_t why_size)
{
	if (!name || !name[0]) {
		return refuse(
			why, why_size, "%s",
			"The name is empty; it needs 1 to 12 characters.");
	}
	size_t length = strlen(name);
	if (length > MAX_NAME_LENGTH) {
		return refuse(why, why_size,
			      "The name \"%s\" is longer than 12 characters.",
			      name);
	}
	for (size_t index = 0; index < length; ++index) {
		unsigned char letter = (unsigned char)name[index];
		if (!isalnum(letter) && letter != ' ' && letter != '_' &&
		    letter != '-' && letter != '.') {
			return refuse(
				why, why_size,
				"The name \"%s\" holds a character other than letters, digits, space, underscore, hyphen and dot.",
				name);
		}
	}
	if (!isalnum((unsigned char)name[0]) ||
	    !isalnum((unsigned char)name[length - 1])) {
		return refuse(
			why, why_size,
			"The name \"%s\" must start and end with a letter or digit.",
			name);
	}
	return xvt_launch_text_check(name, why, why_size);
}

/* Returns the index of text in table, or -1. */
static int index_of(const char *const *table, int count, const char *text)
{
	for (int index = 0; index < count; ++index) {
		if (!strcmp(table[index], text)) {
			return index;
		}
	}
	return -1;
}

/* Returns the string member name of object, or NULL when it is absent or not a
 * string. */
static const char *string_member(const cJSON *object, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
	return cJSON_IsString(item) ? item->valuestring : NULL;
}

/* Reads "side" into out, checking it against the game type. */
static int read_side(const cJSON *root, struct xvt_launch_description *out,
		     char *why, size_t why_size)
{
	const char *side = string_member(root, "side");
	if (!side) {
		return refuse(
			why, why_size, "%s",
			"\"side\" is missing; it must be rebel, imperial or none.");
	}
	int needs_side = !out->is_network &&
			 (out->directory_id == 0 || out->directory_id == 3 ||
			  out->directory_id == 5);
	if (!strcmp(side, "rebel")) {
		out->side = XVT_LAUNCH_SIDE_REBEL;
	} else if (!strcmp(side, "imperial")) {
		out->side = XVT_LAUNCH_SIDE_IMPERIAL;
	} else if (strcmp(side, "none") != 0) {
		return refuse(
			why, why_size,
			"\"side\" is \"%s\"; it must be rebel, imperial or none.",
			side);
	}
	if (needs_side && out->side == XVT_LAUNCH_SIDE_NONE) {
		return refuse(why, why_size,
			      "A %s mission needs a side, rebel or imperial.",
			      g_game_types[out->directory_id]);
	}
	if (out->is_network && out->side != XVT_LAUNCH_SIDE_NONE) {
		return refuse(
			why, why_size, "%s",
			"A network battle takes no side; each seat's team decides it, so use \"none\".");
	}
	if (!needs_side && out->side != XVT_LAUNCH_SIDE_NONE) {
		return refuse(why, why_size,
			      "A %s mission takes no side; use \"none\".",
			      g_game_types[out->directory_id]);
	}
	return 1;
}

/* Reads "mission" into out: "<folder>/<file>" with the game type's folder. */
static int read_mission(const cJSON *root, struct xvt_launch_description *out,
			char *why, size_t why_size)
{
	const char *mission = string_member(root, "mission");
	if (!mission) {
		return refuse(
			why, why_size, "%s",
			"\"mission\" is missing; it must read folder/file.");
	}
	const char *folder = xvt_launch_directory_name(out->directory_id);
	size_t folder_length = strlen(folder);
	if (strncasecmp(mission, folder, folder_length) != 0 ||
	    mission[folder_length] != '/') {
		return refuse(
			why, why_size,
			"The mission \"%s\" is not in the folder of its game type.",
			mission);
	}
	const char *file = mission + folder_length + 1;
	size_t length = strlen(file);
	if (length == 0 || length >= sizeof(out->mission_file)) {
		return refuse(why, why_size,
			      "The mission \"%s\" has no usable file name.",
			      mission);
	}
	for (size_t index = 0; index < length; ++index) {
		unsigned char letter = (unsigned char)file[index];
		if (!isalnum(letter) && letter != '_' && letter != '.') {
			return refuse(
				why, why_size,
				"The mission \"%s\" has a character other than letters, digits, underscore and dot in its file name.",
				mission);
		}
		out->mission_file[index] = (char)tolower(letter);
	}
	return 1;
}

/* Reads a boolean setting named name into *value when present. */
static int read_boolean(const cJSON *settings, const char *name, int *value,
			char *why, size_t why_size)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(settings, name);
	if (!item) {
		return 1;
	}
	if (!cJSON_IsBool(item)) {
		return refuse(why, why_size,
			      "The setting \"%s\" must be true or false.",
			      name);
	}
	*value = cJSON_IsTrue(item) ? 1 : 0;
	return 1;
}

/* Reads the choice setting named name, one of names, into *value when
 * present, as its position in names. */
static int read_choice(const cJSON *settings, const char *name,
		       const char *const *names, int count, int *value,
		       char *why, size_t why_size)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(settings, name);
	if (!item) {
		return 1;
	}
	int found = cJSON_IsString(item)
			    ? index_of(names, count, item->valuestring)
			    : -1;
	if (found < 0) {
		return refuse(
			why, why_size,
			"The setting \"%s\" has a value the game does not know.",
			name);
	}
	*value = found;
	return 1;
}

/* Reads the numeric settings battle_length and mission_time_limit. */
static int read_numbers(const cJSON *settings, struct xvt_launch_settings *out,
			char *why, size_t why_size)
{
	const cJSON *length =
		cJSON_GetObjectItemCaseSensitive(settings, "battle_length");
	if (length) {
		if (!cJSON_IsNumber(length) || length->valuedouble < 2 ||
		    length->valuedouble > 4 ||
		    length->valuedouble != (int)length->valuedouble) {
			return refuse(
				why, why_size, "%s",
				"The setting \"battle_length\" must be 2, 3 or 4.");
		}
		out->battle_length = (int)length->valuedouble - 2;
	}
	const cJSON *limit = cJSON_GetObjectItemCaseSensitive(
		settings, "mission_time_limit");
	if (limit) {
		if (cJSON_IsString(limit) &&
		    !strcmp(limit->valuestring, "none")) {
			out->mission_time_limit = 0;
		} else if (cJSON_IsString(limit) &&
			   !strcmp(limit->valuestring, "default")) {
			out->mission_time_limit = 255;
		} else if (cJSON_IsNumber(limit) && limit->valuedouble >= 1 &&
			   limit->valuedouble <= 20 &&
			   limit->valuedouble == (int)limit->valuedouble) {
			out->mission_time_limit = (int)limit->valuedouble;
		} else {
			return refuse(
				why, why_size, "%s",
				"The setting \"mission_time_limit\" must be none, default or 1 to 20.");
		}
	}
	return 1;
}

/* Reads the members of "settings" into out and refuses an unknown one. */
static int read_settings(const cJSON *settings,
			 struct xvt_launch_description *out, char *why,
			 size_t why_size)
{
	static const char *const difficulties[] = {"easy", "medium", "hard",
						   "easy_cheat"};
	static const char *const sequences[] = {"restart", "continue"};
	static const char *const sith_choices[] = {"none", "apprentices",
						   "lord"};
	static const char *const known[] = {
		"difficulty",	     "collisions",    "craft_jumping",
		"random_setup",	     "battle_length", "mission_time_limit",
		"continue_sequence", "sith"};
	if (!cJSON_IsObject(settings)) {
		return refuse(why, why_size, "%s",
			      "\"settings\" must be an object.");
	}
	const cJSON *item;
	cJSON_ArrayForEach(item, settings)
	{
		if (index_of(known, 8, item->string) < 0) {
			return refuse(
				why, why_size,
				"The setting \"%s\" is not one the game takes.",
				item->string);
		}
	}
	struct xvt_launch_settings *set = &out->settings;
	if (!read_choice(settings, "difficulty", difficulties, 4,
			 &set->difficulty, why, why_size) ||
	    !read_choice(settings, "continue_sequence", sequences, 2,
			 &set->continue_sequence, why, why_size) ||
	    !read_choice(settings, "sith", sith_choices, 3, &set->sith, why,
			 why_size) ||
	    !read_boolean(settings, "collisions", &set->collisions, why,
			  why_size) ||
	    !read_boolean(settings, "craft_jumping", &set->craft_jumping, why,
			  why_size) ||
	    !read_boolean(settings, "random_setup", &set->random_setup, why,
			  why_size) ||
	    !read_numbers(settings, set, why, why_size)) {
		return 0;
	}
	if (set->difficulty == 3 && out->directory_id != 5) {
		return refuse(why, why_size, "%s",
			      "Easy cheat difficulty is for a campaign only.");
	}
	return 1;
}

/* 1 when item is a string of 1 to XVT_LAUNCH_PATH_CAPACITY - 1 characters. */
static int is_path(const cJSON *item)
{
	return cJSON_IsString(item) && item->valuestring[0] &&
	       strlen(item->valuestring) < XVT_LAUNCH_PATH_CAPACITY;
}

/* Reads "ai_program", an object holding only "python" and "path". */
static int read_ai_program(const cJSON *program,
			   struct xvt_launch_description *out, char *why,
			   size_t why_size)
{
	const cJSON *python =
		cJSON_IsObject(program)
			? cJSON_GetObjectItemCaseSensitive(program, "python")
			: NULL;
	const cJSON *path =
		cJSON_IsObject(program)
			? cJSON_GetObjectItemCaseSensitive(program, "path")
			: NULL;
	if (!is_path(python) || !is_path(path) ||
	    cJSON_GetArraySize(program) != 2) {
		return refuse(
			why, why_size, "%s",
			"\"ai_program\" must hold only \"python\" and \"path\", each 1 to 511 characters.");
	}
	strcpy(out->ai_python, python->valuestring);
	strcpy(out->ai_path, path->valuestring);
	return 1;
}

/* Reads "seed", "result" and "ai_program" into out when present. */
static int read_optionals(const cJSON *root, struct xvt_launch_description *out,
			  char *why, size_t why_size)
{
	const cJSON *seed = cJSON_GetObjectItemCaseSensitive(root, "seed");
	if (seed) {
		if (!cJSON_IsNumber(seed) || seed->valuedouble < 0 ||
		    seed->valuedouble > 4294967295.0 ||
		    seed->valuedouble !=
			    (double)(unsigned int)seed->valuedouble) {
			return refuse(
				why, why_size, "%s",
				"\"seed\" must be a whole number from 0 to 4294967295.");
		}
		out->has_seed = 1;
		out->seed = (unsigned int)seed->valuedouble;
	}
	const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
	if (result) {
		if (!cJSON_IsString(result) || !result->valuestring[0] ||
		    strlen(result->valuestring) >= sizeof(out->result_path)) {
			return refuse(
				why, why_size, "%s",
				"\"result\" must be a path of 1 to 511 characters.");
		}
		strcpy(out->result_path, result->valuestring);
	}
	const cJSON *program =
		cJSON_GetObjectItemCaseSensitive(root, "ai_program");
	return !program || read_ai_program(program, out, why, why_size);
}

/* The value of a lowercase hex digit, or -1 for any other character. */
static int hex_digit(char letter)
{
	if (letter >= '0' && letter <= '9') {
		return letter - '0';
	}
	if (letter >= 'a' && letter <= 'f') {
		return letter - 'a' + 10;
	}
	return -1;
}

/* Reads text, the directory's form of an application id, into id. Returns 1,
 * or 0 when the text is not 36 characters of lowercase hex digits in groups
 * of 8, 4, 4, 4 and 12 joined by hyphens. */
static int read_game_id(const char *text, struct xvt_launch_game_id *id)
{
	if (strlen(text) != GAME_ID_LENGTH) {
		return 0;
	}
	uint8_t bytes[16];
	int count = 0;
	for (int index = 0; index < GAME_ID_LENGTH; index += 2) {
		if (index == 8 || index == 13 || index == 18 || index == 23) {
			if (text[index] != '-') {
				return 0;
			}
			++index;
		}
		int high = hex_digit(text[index]);
		int low = hex_digit(text[index + 1]);
		if (high < 0 || low < 0) {
			return 0;
		}
		bytes[count++] = (uint8_t)(high * 16 + low);
	}
	id->data1 = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
		    (uint32_t)bytes[2] << 8 | bytes[3];
	id->data2 = (uint16_t)(bytes[4] << 8 | bytes[5]);
	id->data3 = (uint16_t)(bytes[6] << 8 | bytes[7]);
	memcpy(id->data4, bytes + 8, sizeof(id->data4));
	return 1;
}

/* Reads "players", the seat order, into network, and finds pilot's seat. */
static int read_players(const cJSON *players, const char *pilot,
			struct xvt_launch_network *network, char *why,
			size_t why_size)
{
	int count = cJSON_IsArray(players) ? cJSON_GetArraySize(players) : -1;
	if (count < MIN_NETWORK_PLAYERS || count > XVT_LAUNCH_SEAT_CAPACITY) {
		return refuse(
			why, why_size, "%s",
			"\"players\" must list 2 to 8 pilot names in seat order.");
	}
	network->seat = -1;
	const cJSON *item;
	cJSON_ArrayForEach(item, players)
	{
		if (!cJSON_IsString(item)) {
			return refuse(
				why, why_size, "%s",
				"Every entry of \"players\" must be a name.");
		}
		if (!xvt_launch_name_check(item->valuestring, why, why_size)) {
			return 0;
		}
		for (int seat = 0; seat < network->player_count; ++seat) {
			if (!strcasecmp(network->players[seat],
					item->valuestring)) {
				return refuse(
					why, why_size,
					"\"players\" names \"%s\" twice; each seat needs its own pilot.",
					item->valuestring);
			}
		}
		if (!strcasecmp(item->valuestring, pilot)) {
			network->seat = network->player_count;
		}
		strcpy(network->players[network->player_count++],
		       item->valuestring);
	}
	if (network->seat < 0) {
		return refuse(why, why_size,
			      "The pilot \"%s\" is not among \"players\".",
			      pilot);
	}
	if (network->role == XVT_LAUNCH_ROLE_HOST && network->seat != 0) {
		return refuse(
			why, why_size, "%s",
			"The host's pilot must be first in \"players\": the host takes the first seat.");
	}
	return 1;
}

/* Reads "network", an object holding exactly role, lobby, game and players,
 * into out->network. Needs out->pilot. */
static int read_network(const cJSON *network,
			struct xvt_launch_description *out, char *why,
			size_t why_size)
{
	static const char *const roles[] = {"host", "join"};
	static const char *const known[] = {"role", "lobby", "game", "players"};
	if (!cJSON_IsObject(network)) {
		return refuse(
			why, why_size, "%s",
			"A network description needs \"network\", an object.");
	}
	const cJSON *item;
	cJSON_ArrayForEach(item, network)
	{
		if (index_of(known, 4, item->string) < 0) {
			return refuse(
				why, why_size,
				"\"network\" holds \"%s\", which it does not take.",
				item->string);
		}
	}
	const char *role = string_member(network, "role");
	out->network.role = role ? index_of(roles, 2, role) : -1;
	if (out->network.role < 0) {
		return refuse(why, why_size, "%s",
			      "The network \"role\" must be host or join.");
	}
	const char *lobby = string_member(network, "lobby");
	if (!lobby || strncmp(lobby, "https://", 8) != 0 ||
	    strlen(lobby) >= sizeof(out->network.lobby)) {
		return refuse(
			why, why_size, "%s",
			"The network \"lobby\" must be an https:// address of at most 511 characters.");
	}
	strcpy(out->network.lobby, lobby);
	const char *game = string_member(network, "game");
	if (!game || !read_game_id(game, &out->network.game)) {
		return refuse(
			why, why_size, "%s",
			"The network \"game\" must be an application id: 36 lowercase hex digits and hyphens, grouped 8-4-4-4-12.");
	}
	return read_players(
		cJSON_GetObjectItemCaseSensitive(network, "players"),
		out->pilot, &out->network, why, why_size);
}

/* The rules a network description keeps beyond its "network" part: a game
 * type the door flies and a shared seed. */
static int check_network_battle(const struct xvt_launch_description *out,
				char *why, size_t why_size)
{
	if (out->directory_id != 1 && out->directory_id != 3) {
		return refuse(
			why, why_size,
			"A network battle is a melee or a combat engagement; \"%s\" is neither.",
			g_game_types[out->directory_id]);
	}
	if (!out->has_seed) {
		return refuse(
			why, why_size, "%s",
			"A network battle needs \"seed\": every machine must start from the same one.");
	}
	return 1;
}

/* Checks the members that must be there and nothing else is there, and reads
 * the mode into out->is_network. */
static int check_members(const cJSON *root, struct xvt_launch_description *out,
			 char *why, size_t why_size)
{
	static const char *const known[] = {
		"schema_version", "mode",     "game_type", "side",
		"mission",	  "pilot",    "result",	   "seed",
		"ai_program",	  "settings", "network"};
	const cJSON *item;
	cJSON_ArrayForEach(item, root)
	{
		if (index_of(known, 11, item->string) < 0) {
			return refuse(
				why, why_size,
				"The description holds \"%s\", which it does not take.",
				item->string);
		}
		for (const cJSON *earlier = root->child; earlier != item;
		     earlier = earlier->next) {
			if (!strcmp(earlier->string, item->string)) {
				return refuse(
					why, why_size,
					"The description holds \"%s\" twice.",
					item->string);
			}
		}
	}
	const cJSON *version =
		cJSON_GetObjectItemCaseSensitive(root, "schema_version");
	if (!cJSON_IsNumber(version) || version->valuedouble != 1) {
		return refuse(why, why_size, "%s",
			      "\"schema_version\" must be 1.");
	}
	const char *mode = string_member(root, "mode");
	if (mode && !strcmp(mode, "network")) {
		out->is_network = 1;
	} else if (!mode || strcmp(mode, "single_player") != 0) {
		return refuse(
			why, why_size, "%s",
			"\"mode\" must be \"single_player\" or \"network\".");
	}
	if (!out->is_network &&
	    cJSON_GetObjectItemCaseSensitive(root, "network")) {
		return refuse(
			why, why_size, "%s",
			"A single player description takes no \"network\".");
	}
	return 1;
}

/* Marks every setting as left alone. */
static void leave_settings_alone(struct xvt_launch_settings *set)
{
	set->difficulty = XVT_LAUNCH_SETTING_UNSET;
	set->collisions = XVT_LAUNCH_SETTING_UNSET;
	set->craft_jumping = XVT_LAUNCH_SETTING_UNSET;
	set->random_setup = XVT_LAUNCH_SETTING_UNSET;
	set->battle_length = XVT_LAUNCH_SETTING_UNSET;
	set->mission_time_limit = XVT_LAUNCH_SETTING_UNSET;
	set->continue_sequence = XVT_LAUNCH_SETTING_UNSET;
	set->sith = XVT_LAUNCH_SETTING_UNSET;
}

int xvt_launch_description_parse(const char *text,
				 struct xvt_launch_description *out, char *why,
				 size_t why_size)
{
	memset(out, 0, sizeof(*out));
	out->side = XVT_LAUNCH_SIDE_NONE;
	leave_settings_alone(&out->settings);
	cJSON *root = text ? cJSON_Parse(text) : NULL;
	if (!cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return refuse(why, why_size, "%s",
			      "The description is not a JSON object.");
	}
	int ok = check_members(root, out, why, why_size);
	const char *type = ok ? string_member(root, "game_type") : NULL;
	if (ok && !type) {
		ok = refuse(why, why_size, "%s", "\"game_type\" is missing.");
	}
	if (ok) {
		out->directory_id = index_of(g_game_types, 6, type);
		if (out->directory_id < 0) {
			ok = refuse(
				why, why_size,
				"The game type \"%s\" is not one of training, melee, tournament, combat, battle or campaign.",
				type);
		}
	}
	const char *pilot = ok ? string_member(root, "pilot") : NULL;
	if (ok && !pilot) {
		ok = refuse(why, why_size, "%s", "\"pilot\" is missing.");
	}
	ok = ok && xvt_launch_name_check(pilot, why, why_size) &&
	     read_side(root, out, why, why_size) &&
	     read_mission(root, out, why, why_size) &&
	     read_optionals(root, out, why, why_size);
	if (ok) {
		strcpy(out->pilot, pilot);
		const cJSON *settings =
			cJSON_GetObjectItemCaseSensitive(root, "settings");
		ok = !settings || read_settings(settings, out, why, why_size);
	}
	if (ok && out->is_network) {
		ok = check_network_battle(out, why, why_size) &&
		     read_network(
			     cJSON_GetObjectItemCaseSensitive(root, "network"),
			     out, why, why_size);
	}
	cJSON_Delete(root);
	return ok;
}

uint32_t
xvt_launch_setup_fingerprint(const struct xvt_launch_description *description)
{
	const struct xvt_launch_settings *set = &description->settings;
	const struct xvt_launch_game_id *game = &description->network.game;
	char text[512];
	int length = snprintf(
		text, sizeof(text),
		"%d|%s|%u|%d,%d,%d,%d,%d,%d,%d,%d|%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x|",
		description->directory_id, description->mission_file,
		description->seed, set->difficulty, set->collisions,
		set->craft_jumping, set->random_setup, set->battle_length,
		set->mission_time_limit, set->continue_sequence, set->sith,
		(unsigned)game->data1, (unsigned)game->data2,
		(unsigned)game->data3, game->data4[0], game->data4[1],
		game->data4[2], game->data4[3], game->data4[4], game->data4[5],
		game->data4[6], game->data4[7]);
	for (int seat = 0; seat < description->network.player_count; ++seat) {
		length += snprintf(text + length, sizeof(text) - (size_t)length,
				   "%s,", description->network.players[seat]);
	}
	return xvt_flight_wire_crc32c((const uint8_t *)text, (size_t)length);
}

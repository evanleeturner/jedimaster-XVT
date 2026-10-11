#include "xvt_runtime/runtime/launch_description.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

enum { MAX_NAME_LENGTH = 12 };

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
	int needs_side = out->directory_id == 0 || out->directory_id == 3 ||
			 out->directory_id == 5;
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
	static const char *const known[] = {
		"difficulty",	    "collisions",    "craft_jumping",
		"random_setup",	    "battle_length", "mission_time_limit",
		"continue_sequence"};
	if (!cJSON_IsObject(settings)) {
		return refuse(why, why_size, "%s",
			      "\"settings\" must be an object.");
	}
	const cJSON *item;
	cJSON_ArrayForEach(item, settings)
	{
		if (index_of(known, 7, item->string) < 0) {
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

/* Reads "seed" and "result" into out when present. */
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
	return 1;
}

/* Checks the members that must be there and nothing else is there. */
static int check_members(const cJSON *root, char *why, size_t why_size)
{
	static const char *const known[] = {
		"schema_version", "mode",   "game_type", "side",    "mission",
		"pilot",	  "result", "seed",	 "settings"};
	const cJSON *item;
	cJSON_ArrayForEach(item, root)
	{
		if (index_of(known, 9, item->string) < 0) {
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
	if (!mode || strcmp(mode, "single_player") != 0) {
		return refuse(
			why, why_size, "%s",
			"Only single player is supported: \"mode\" must be \"single_player\".");
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
	int ok = check_members(root, why, why_size);
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
	cJSON_Delete(root);
	return ok;
}

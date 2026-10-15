/* Checks the reading of a --launch description and the name checks
 * (xvt_runtime/runtime/launch_description.h) against the promises in its
 * header. The module holds no state and touches no game data: each case hands
 * it made-up JSON text. Every refusal the header lists has a case, and each
 * case checks the sentence names what is wrong, so a refusal for the wrong
 * reason is caught. */
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/runtime/launch_description.h"

enum { TEXT_CAPACITY = 1024 };

static struct xvt_launch_description g_out;
static char g_why[XVT_LAUNCH_WHY_CAPACITY];

/* Builds a description from a game type, a side, a mission path and the
 * members that follow them (pilot, seed, settings), the schema version and
 * the mode being right. Returns a buffer valid until the next call. */
static const char *doc(const char *game_type, const char *side,
		       const char *mission, const char *rest)
{
	static char text[TEXT_CAPACITY];
	snprintf(text, sizeof(text), "{\"schema_version\":1,");
	size_t used = strlen(text);
	snprintf(text + used, sizeof(text) - used,
		 "\"mode\":\"single_player\",");
	used = strlen(text);
	snprintf(text + used, sizeof(text) - used,
		 "\"game_type\":\"%s\",\"side\":\"%s\",", game_type, side);
	used = strlen(text);
	snprintf(text + used, sizeof(text) - used, "\"mission\":\"%s\",%s}",
		 mission, rest);
	return text;
}

/* Joins the parts into one buffer valid until the next call. */
static const char *join(const char *const *parts, size_t count)
{
	static char text[TEXT_CAPACITY];
	text[0] = '\0';
	for (size_t index = 0; index < count; ++index) {
		strcat(text, parts[index]);
	}
	return text;
}

#define JOIN(...)                                                              \
	join((const char *const[]){__VA_ARGS__},                               \
	     sizeof((const char *const[]){__VA_ARGS__}) / sizeof(char *))

/* The member of a description naming a valid pilot. */
#define PILOT "\"pilot\":\"Ace\""

/* Parses text; returns the parser's answer. */
static int parse(const char *text)
{
	g_why[0] = '\0';
	return xvt_launch_description_parse(text, &g_out, g_why, sizeof(g_why));
}

/* Checks text is refused with a sentence holding needle. */
static void refused(const char *text, const char *needle)
{
	XVT_ASSERT_INT_EQ(parse(text), 0);
	if (!strstr(g_why, needle)) {
		fprintf(stderr, "refusal \"%s\" lacks \"%s\"\n", g_why, needle);
		XVT_ASSERT_TRUE(0);
	}
}

static void check_valid_training(void)
{
	XVT_ASSERT_INT_EQ(
		parse(doc("training", "rebel", "train/1TA02BZ.tie", PILOT)), 1);
	XVT_ASSERT_INT_EQ(g_out.directory_id, 0);
	XVT_ASSERT_INT_EQ(g_out.side, XVT_LAUNCH_SIDE_REBEL);
	XVT_ASSERT_INT_EQ(strcmp(g_out.mission_file, "1ta02bz.tie"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_out.pilot, "Ace"), 0);
	XVT_ASSERT_INT_EQ(g_out.result_path[0], 0);
	XVT_ASSERT_INT_EQ(g_out.has_seed, 0);
	XVT_ASSERT_INT_EQ(g_out.settings.difficulty, XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(g_out.settings.collisions, XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(g_out.settings.craft_jumping,
			  XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(g_out.settings.random_setup,
			  XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(g_out.settings.battle_length,
			  XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(g_out.settings.mission_time_limit,
			  XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(g_out.settings.continue_sequence,
			  XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(g_out.settings.sith, XVT_LAUNCH_SETTING_UNSET);
}

static void check_every_game_type(void)
{
	static const struct {
		const char *type;
		const char *side;
		const char *folder;
		int directory;
		int side_value;
	} cases[] = {
		{"training", "imperial", "train", 0, XVT_LAUNCH_SIDE_IMPERIAL},
		{"melee", "none", "melee", 1, XVT_LAUNCH_SIDE_NONE},
		{"tournament", "none", "tourn", 2, XVT_LAUNCH_SIDE_NONE},
		{"combat", "rebel", "combat", 3, XVT_LAUNCH_SIDE_REBEL},
		{"battle", "none", "battle", 4, XVT_LAUNCH_SIDE_NONE},
		{"campaign", "imperial", "campaign", 5,
		 XVT_LAUNCH_SIDE_IMPERIAL}};
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]);
	     ++index) {
		char mission[64];
		snprintf(mission, sizeof(mission), "%s/a_1.lst",
			 cases[index].folder);
		XVT_ASSERT_INT_EQ(parse(doc(cases[index].type,
					    cases[index].side, mission, PILOT)),
				  1);
		XVT_ASSERT_INT_EQ(g_out.directory_id, cases[index].directory);
		XVT_ASSERT_INT_EQ(g_out.side, cases[index].side_value);
		const char *name =
			xvt_launch_directory_name(g_out.directory_id);
		XVT_ASSERT_TRUE(name != NULL);
		XVT_ASSERT_INT_EQ(strcmp(name, cases[index].folder), 0);
	}
	XVT_ASSERT_TRUE(xvt_launch_directory_name(6) == NULL);
	XVT_ASSERT_TRUE(xvt_launch_directory_name(-1) == NULL);
}

static void check_optional_members(void)
{
	const char *all_settings =
		JOIN(PILOT, ",\"seed\":4294967295",
		     ",\"result\":\"/logs/result.json\"",
		     ",\"settings\":{\"difficulty\":\"hard\"",
		     ",\"collisions\":false,\"craft_jumping\":true",
		     ",\"random_setup\":true,\"battle_length\":4",
		     ",\"mission_time_limit\":20",
		     ",\"continue_sequence\":\"restart\"}");
	XVT_ASSERT_INT_EQ(parse(doc("battle", "none", "battle/battle1.lst",
				    all_settings)),
			  1);
	XVT_ASSERT_INT_EQ(g_out.has_seed, 1);
	XVT_ASSERT_TRUE(g_out.seed == 4294967295u);
	XVT_ASSERT_INT_EQ(strcmp(g_out.result_path, "/logs/result.json"), 0);
	XVT_ASSERT_INT_EQ(g_out.settings.difficulty, 2);
	XVT_ASSERT_INT_EQ(g_out.settings.collisions, 0);
	XVT_ASSERT_INT_EQ(g_out.settings.craft_jumping, 1);
	XVT_ASSERT_INT_EQ(g_out.settings.random_setup, 1);
	XVT_ASSERT_INT_EQ(g_out.settings.battle_length, 2);
	XVT_ASSERT_INT_EQ(g_out.settings.mission_time_limit, 20);
	XVT_ASSERT_INT_EQ(g_out.settings.continue_sequence, 0);
	const char *cheat =
		JOIN(PILOT, ",\"settings\":{", "\"difficulty\":\"easy_cheat\"",
		     ",\"mission_time_limit\":\"none\"}");
	XVT_ASSERT_INT_EQ(
		parse(doc("campaign", "rebel", "campaign/c.lst", cheat)), 1);
	XVT_ASSERT_INT_EQ(g_out.settings.difficulty, 3);
	XVT_ASSERT_INT_EQ(g_out.settings.mission_time_limit, 0);
	const char *limit =
		JOIN(PILOT, ",\"settings\":{",
		     "\"mission_time_limit\":\"default\"}", ",\"seed\":0");
	XVT_ASSERT_INT_EQ(parse(doc("training", "rebel", "train/a.tie", limit)),
			  1);
	XVT_ASSERT_INT_EQ(g_out.settings.mission_time_limit, 255);
	XVT_ASSERT_INT_EQ(g_out.has_seed, 1);
	XVT_ASSERT_INT_EQ(g_out.seed, 0);
}

/* A refused description leaves the structure zeroed with no side. */
static void check_refusal_leaves_no_side(void)
{
	g_out.side = XVT_LAUNCH_SIDE_IMPERIAL;
	XVT_ASSERT_INT_EQ(parse("[1]"), 0);
	XVT_ASSERT_INT_EQ(g_out.side, XVT_LAUNCH_SIDE_NONE);
	XVT_ASSERT_INT_EQ(g_out.directory_id, 0);
	XVT_ASSERT_INT_EQ(g_out.pilot[0], 0);
	XVT_ASSERT_INT_EQ(g_out.mission_file[0], 0);
}

static void check_text_refusals(void)
{
	refused(NULL, "not a JSON object");
	refused("", "not a JSON object");
	refused("[1,2]", "not a JSON object");
	refused("{\"schema_version\":", "not a JSON object");
	refused("{\"schema_version\":2,\"mode\":\"single_player\"}",
		"schema_version");
	refused("{\"mode\":\"single_player\"}", "schema_version");
	refused("{\"schema_version\":1,\"mode\":\"multiplayer\"}",
		"Only single player");
	refused("{\"schema_version\":1}", "Only single player");
	const char *head = "{\"schema_version\":1,\"mode\":\"single_player\",";
	char text[TEXT_CAPACITY];
	snprintf(text, sizeof(text), "%s\"players\":2}", head);
	refused(text, "\"players\"");
	snprintf(text, sizeof(text), "%s\"mode\":\"single_player\"}", head);
	refused(text, "twice");
	snprintf(text, sizeof(text), "%s" PILOT "}", head);
	refused(text, "game_type");
	snprintf(text, sizeof(text), "%s\"game_type\":\"duel\"," PILOT "}",
		 head);
	refused(text, "\"duel\"");
	snprintf(text, sizeof(text), "%s\"game_type\":\"melee\"}", head);
	refused(text, "\"pilot\" is missing");
}

static void check_side_refusals(void)
{
	const char *head = "{\"schema_version\":1,\"mode\":\"single_player\",";
	char text[TEXT_CAPACITY];
	snprintf(text, sizeof(text), "%s\"game_type\":\"training\"," PILOT "}",
		 head);
	refused(text, "\"side\" is missing");
	refused(doc("training", "hutt", "train/a.tie", PILOT), "\"hutt\"");
	refused(doc("training", "none", "train/a.tie", PILOT), "needs a side");
	refused(doc("combat", "none", "combat/a.tie", PILOT), "needs a side");
	refused(doc("campaign", "none", "campaign/a.lst", PILOT),
		"needs a side");
	refused(doc("melee", "rebel", "melee/a.tie", PILOT), "takes no side");
	refused(doc("battle", "imperial", "battle/a.lst", PILOT),
		"takes no side");
}

static void check_mission_refusals(void)
{
	const char *head = "{\"schema_version\":1,\"mode\":\"single_player\",";
	char text[TEXT_CAPACITY];
	snprintf(text, sizeof(text),
		 "%s\"game_type\":\"melee\",\"side\":\"none\"," PILOT "}",
		 head);
	refused(text, "\"mission\" is missing");
	refused(doc("melee", "none", "train/a.tie", PILOT),
		"not in the folder");
	refused(doc("melee", "none", "melee\\\\a.tie", PILOT),
		"not in the folder");
	refused(doc("melee", "none", "melee/", PILOT), "no usable file name");
	refused(doc("melee", "none", "melee/a b.tie", PILOT),
		"character other than");
	refused(doc("melee", "none", "melee/../a.tie", PILOT),
		"character other than");
	char long_name[128];
	memset(long_name, 'a', sizeof(long_name));
	long_name[sizeof(long_name) - 1] = '\0';
	char mission[160];
	snprintf(mission, sizeof(mission), "melee/%s", long_name);
	refused(doc("melee", "none", mission, PILOT), "no usable file name");
}

/* Parses a training description with the given settings object text. */
static int with_settings(const char *settings, const char *game_type,
			 const char *side)
{
	char rest[TEXT_CAPACITY];
	snprintf(rest, sizeof(rest), PILOT ",\"settings\":%s", settings);
	return parse(doc(game_type, side, "train/a.tie", rest));
}

/* Checks a setting text is refused with a sentence holding needle. */
static void setting_refused(const char *settings, const char *needle)
{
	XVT_ASSERT_INT_EQ(with_settings(settings, "training", "rebel"), 0);
	if (!strstr(g_why, needle)) {
		fprintf(stderr, "refusal \"%s\" lacks \"%s\"\n", g_why, needle);
		XVT_ASSERT_TRUE(0);
	}
}

static void check_setting_refusals(void)
{
	setting_refused("{\"volume\":3}", "\"volume\"");
	setting_refused("{\"difficulty\":\"brutal\"}", "\"difficulty\"");
	setting_refused("{\"difficulty\":1}", "\"difficulty\"");
	setting_refused("{\"collisions\":\"on\"}", "true or false");
	setting_refused("{\"craft_jumping\":1}", "true or false");
	setting_refused("{\"random_setup\":null}", "true or false");
	setting_refused("{\"battle_length\":5}", "2, 3 or 4");
	setting_refused("{\"battle_length\":1}", "2, 3 or 4");
	setting_refused("{\"battle_length\":2.5}", "2, 3 or 4");
	setting_refused("{\"mission_time_limit\":21}", "1 to 20");
	setting_refused("{\"mission_time_limit\":0}", "1 to 20");
	setting_refused("{\"mission_time_limit\":\"soon\"}", "1 to 20");
	setting_refused("{\"continue_sequence\":\"maybe\"}",
			"\"continue_sequence\"");
	setting_refused("[]", "must be an object");
	XVT_ASSERT_INT_EQ(with_settings("{\"difficulty\":\"easy_cheat\"}",
					"training", "rebel"),
			  0);
	XVT_ASSERT_TRUE(strstr(g_why, "campaign only") != NULL);
	char rest[TEXT_CAPACITY];
	snprintf(rest, sizeof(rest),
		 PILOT ",\"settings\":{\"difficulty\":\"easy_cheat\"}");
	XVT_ASSERT_INT_EQ(parse(doc("melee", "none", "melee/a.tie", rest)), 0);
	XVT_ASSERT_TRUE(strstr(g_why, "campaign only") != NULL);
}

/* The sith setting takes none, apprentices or lord, as 0, 1 and 2; left out
 * it stays unset (the game reads that as none), and any other word, or a
 * value that is not a word, is refused as the difficulty's are. */
static void check_sith_setting(void)
{
	static const struct {
		const char *choice;
		int value;
	} cases[] = {{"none", 0}, {"apprentices", 1}, {"lord", 2}};
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]);
	     ++index) {
		char settings[64];
		snprintf(settings, sizeof(settings), "{\"sith\":\"%s\"}",
			 cases[index].choice);
		XVT_ASSERT_INT_EQ(with_settings(settings, "training", "rebel"),
				  1);
		XVT_ASSERT_INT_EQ(g_out.settings.sith, cases[index].value);
		XVT_ASSERT_INT_EQ(g_out.settings.difficulty,
				  XVT_LAUNCH_SETTING_UNSET);
	}
	XVT_ASSERT_INT_EQ(
		with_settings("{\"difficulty\":\"hard\"}", "training", "rebel"),
		1);
	XVT_ASSERT_INT_EQ(g_out.settings.sith, XVT_LAUNCH_SETTING_UNSET);
	setting_refused("{\"sith\":\"emperor\"}", "\"sith\"");
	setting_refused("{\"sith\":\"Lord\"}", "\"sith\"");
	setting_refused("{\"sith\":2}", "\"sith\"");
	setting_refused("{\"sith\":true}", "\"sith\"");
}

static void check_seed_and_result_refusals(void)
{
	char rest[TEXT_CAPACITY];
	static const char *const seeds[] = {"-1", "4294967296", "1.5", "\"7\""};
	for (size_t index = 0; index < sizeof(seeds) / sizeof(seeds[0]);
	     ++index) {
		snprintf(rest, sizeof(rest), PILOT ",\"seed\":%s",
			 seeds[index]);
		refused(doc("melee", "none", "melee/a.tie", rest), "\"seed\"");
	}
	snprintf(rest, sizeof(rest), PILOT ",\"result\":\"\"");
	refused(doc("melee", "none", "melee/a.tie", rest), "\"result\"");
	snprintf(rest, sizeof(rest), PILOT ",\"result\":5");
	refused(doc("melee", "none", "melee/a.tie", rest), "\"result\"");
	char long_path[600];
	memset(long_path, 'a', sizeof(long_path));
	long_path[sizeof(long_path) - 1] = '\0';
	char big[TEXT_CAPACITY];
	snprintf(big, sizeof(big), PILOT ",\"result\":\"%s\"", long_path);
	refused(doc("melee", "none", "melee/a.tie", big), "\"result\"");
}

/* "ai_program" fills both paths, which are empty when it is left out;
 * anything but an object of exactly two paths is refused. */
static void check_ai_program(void)
{
	char rest[TEXT_CAPACITY];
	XVT_ASSERT_INT_EQ(parse(doc("melee", "none", "melee/a.tie", PILOT)), 1);
	XVT_ASSERT_INT_EQ(g_out.ai_python[0], 0);
	XVT_ASSERT_INT_EQ(g_out.ai_path[0], 0);
	snprintf(rest, sizeof(rest),
		 PILOT ",\"ai_program\":{\"python\":\"/usr/bin/python3\","
		       "\"path\":\"/ai/jedimaster_ai\"}");
	XVT_ASSERT_INT_EQ(parse(doc("melee", "none", "melee/a.tie", rest)), 1);
	XVT_ASSERT_INT_EQ(strcmp(g_out.ai_python, "/usr/bin/python3"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_out.ai_path, "/ai/jedimaster_ai"), 0);
	static const char *const bad[] = {
		"\"python3\"",
		"{\"python\":\"p\"}",
		"{\"python\":\"p\",\"path\":\"\"}",
		"{\"python\":\"p\",\"path\":5}",
		"{\"python\":\"p\",\"script\":\"a\"}",
		"{\"python\":\"p\",\"path\":\"a\",\"args\":\"b\"}"};
	for (size_t index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
		snprintf(rest, sizeof(rest), PILOT ",\"ai_program\":%s",
			 bad[index]);
		refused(doc("melee", "none", "melee/a.tie", rest),
			"\"ai_program\"");
	}
	char long_path[600];
	memset(long_path, 'a', sizeof(long_path));
	long_path[sizeof(long_path) - 1] = '\0';
	char big[TEXT_CAPACITY];
	snprintf(big, sizeof(big),
		 PILOT ",\"ai_program\":{\"python\":\"p\",\"path\":\"%s\"}",
		 long_path);
	refused(doc("melee", "none", "melee/a.tie", big), "\"ai_program\"");
}

/* Checks a name is refused by the name check with a sentence holding needle. */
static void name_refused(const char *name, const char *needle)
{
	char why[XVT_LAUNCH_WHY_CAPACITY];
	XVT_ASSERT_INT_EQ(xvt_launch_name_check(name, why, sizeof(why)), 0);
	if (!strstr(why, needle)) {
		fprintf(stderr, "refusal \"%s\" lacks \"%s\"\n", why, needle);
		XVT_ASSERT_TRUE(0);
	}
}

static void check_name_checks(void)
{
	char why[XVT_LAUNCH_WHY_CAPACITY];
	XVT_ASSERT_INT_EQ(xvt_launch_name_check("Ace", why, sizeof(why)), 1);
	XVT_ASSERT_INT_EQ(xvt_launch_name_check("A", why, sizeof(why)), 1);
	XVT_ASSERT_INT_EQ(
		xvt_launch_name_check("Lukas S-1_x9", why, sizeof(why)), 1);
	XVT_ASSERT_INT_EQ(
		xvt_launch_name_check("123456789012", why, sizeof(why)), 1);
	XVT_ASSERT_INT_EQ(xvt_launch_name_check(NULL, why, sizeof(why)), 0);
	name_refused("", "empty");
	name_refused("1234567890123", "longer than 12");
	name_refused("a~b", "other than letters");
	name_refused("a\"b", "other than letters");
	name_refused("a\\b", "other than letters");
	name_refused("a\nb", "other than letters");
	name_refused(" Ace", "start and end");
	name_refused("Ace-", "start and end");
	name_refused("Ace.", "start and end");
}

/* Returns text with its lowercase letters raised to uppercase, in a static
 * buffer. */
static const char *upper(const char *text)
{
	static char raised[64];
	size_t at = 0;
	for (; text[at] && at + 1 < sizeof(raised); ++at) {
		raised[at] = (char)(text[at] >= 'a' && text[at] <= 'z'
					    ? text[at] - 32
					    : text[at]);
	}
	raised[at] = '\0';
	return raised;
}

/* Every flight switch word is refused inside a name, in any letter case. */
static void check_switch_words(void)
{
	static const char *const words[] = {
		"traincourse", "nopilot",     "dinput",	 "sfx",	       "music",
		"voice",       "tickcounter", "mipmaps", "inprogress", "newnet",
		"nolauncher",  "fullscreen",  "pageflip"};
	char why[XVT_LAUNCH_WHY_CAPACITY];
	for (size_t index = 0; index < sizeof(words) / sizeof(words[0]);
	     ++index) {
		char name[XVT_LAUNCH_NAME_CAPACITY + 16];
		snprintf(name, sizeof(name), "A%s", words[index]);
		XVT_ASSERT_INT_EQ(xvt_launch_text_check(name, why, sizeof(why)),
				  0);
		XVT_ASSERT_TRUE(strstr(why, words[index]) != NULL);
		XVT_ASSERT_INT_EQ(
			xvt_launch_text_check(upper(name), why, sizeof(why)),
			0);
		if (strlen(name) <= 12) {
			name_refused(name, "switch word");
		}
	}
	/* The case the flight falls for: a name holding a switch word. */
	XVT_ASSERT_INT_EQ(
		xvt_launch_text_check("Mr_nosfx_Smith", why, sizeof(why)), 0);
	XVT_ASSERT_TRUE(strstr(why, "sfx") != NULL);
	name_refused("Mr_nosfx_Smith", "longer than 12");
	name_refused("Mr nosfx", "switch word");
	refused(doc("melee", "none", "melee/a.tie", "\"pilot\":\"Mr nosfx\""),
		"switch word");
}

static void check_text_check(void)
{
	char why[XVT_LAUNCH_WHY_CAPACITY];
	XVT_ASSERT_INT_EQ(
		xvt_launch_text_check("Internet game.", why, sizeof(why)), 1);
	XVT_ASSERT_INT_EQ(xvt_launch_text_check("", why, sizeof(why)), 1);
	XVT_ASSERT_INT_EQ(xvt_launch_text_check("a~b", why, sizeof(why)), 0);
	XVT_ASSERT_TRUE(strstr(why, "tilde") != NULL);
	XVT_ASSERT_INT_EQ(xvt_launch_text_check("a\tb", why, sizeof(why)), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_text_check("a\x7f", why, sizeof(why)), 0);
	XVT_ASSERT_INT_EQ(
		xvt_launch_text_check("My Voice game", why, sizeof(why)), 0);
	/* A why buffer of one byte, or none, never overruns. */
	XVT_ASSERT_INT_EQ(xvt_launch_text_check("sfx", why, 1), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_text_check("sfx", NULL, 0), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_text_check("sfx", NULL, 10), 0);
}

int main(void)
{
	check_valid_training();
	check_every_game_type();
	check_optional_members();
	check_refusal_leaves_no_side();
	check_text_refusals();
	check_side_refusals();
	check_mission_refusals();
	check_setting_refusals();
	check_sith_setting();
	check_seed_and_result_refusals();
	check_ai_program();
	check_name_checks();
	check_switch_words();
	check_text_check();
	return 0;
}

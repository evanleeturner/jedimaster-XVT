/* Checks the settings documents (xvt_runtime/config/config.h) against the promises in the header: loading
 * the shipped defaults and the player's overrides, updating and saving them, the setters, and the game
 * options with their legacy text import. The shipped defaults are copied from the source tree; every user
 * file and legacy file is written here. Each check starts from a fresh fixture folder (config_fixture.h).
 *
 * Run as "test_config known-failure <check>" for a check that shows the code breaking its header; see
 * main. */
#define _XOPEN_SOURCE 700

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt/frontend/config.h"
#include "xvt/net/net.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/keyboard_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a refused call must leave as it was: the generation, both documents and the typed settings. */
struct State {
	uint64_t generation;
	AeronConfigFile *user;
	AeronConfigFile *resolved;
	int has_settings;
	struct XvtSettings settings;
};

static struct State g_state;

static void Snapshot(void)
{
	AeronConfigError detail;
	memset(&g_state, 0, sizeof g_state);
	g_state.generation = XvtConfig_Generation();
	g_state.user = Fixture_UserCopy();
	if (XvtConfig_ResolvedDocument()) {
		XVT_ASSERT_TRUE(
			AeronConfigFile_Clone(XvtConfig_ResolvedDocument(),
					      &g_state.resolved, &detail));
	}
	g_state.has_settings = XvtConfig_Settings() != NULL;
	if (g_state.has_settings) {
		memcpy(&g_state.settings, XvtConfig_Settings(),
		       sizeof g_state.settings);
	}
}

static void ExpectUnchanged(void)
{
	XVT_ASSERT_INT_EQ(XvtConfig_Generation(), g_state.generation);
	XVT_ASSERT_TRUE(
		Fixture_SameDocument(XvtConfig_UserDocument(), g_state.user));
	XVT_ASSERT_TRUE(Fixture_SameDocument(XvtConfig_ResolvedDocument(),
					     g_state.resolved));
	XVT_ASSERT_INT_EQ(XvtConfig_Settings() != NULL, g_state.has_settings);
	if (g_state.has_settings) {
		XVT_ASSERT_INT_EQ(memcmp(XvtConfig_Settings(),
					 &g_state.settings,
					 sizeof g_state.settings),
				  0);
	}
	AeronConfigFile_Destroy(g_state.user);
	AeronConfigFile_Destroy(g_state.resolved);
	memset(&g_state, 0, sizeof g_state);
}

/* A copy of the user overrides with one value set, for XvtConfig_UpdateUser. */
static AeronConfigFile *UserWithInt(const char *path, int64_t value)
{
	AeronConfigFile *candidate = Fixture_UserCopy();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_SetInt(candidate, path, value, &detail));
	return candidate;
}

/* Hands candidate to XvtConfig_UpdateUser without saving, expects it accepted, then destroys it. */
static void Accept(AeronConfigFile *candidate)
{
	char error[1024] = "";
	int accepted = XvtConfig_UpdateUser(candidate, 0, error, sizeof error);
	if (!accepted) {
		fprintf(stderr, "refused: %s\n", error);
	}
	XVT_ASSERT_INT_EQ(accepted, 1);
	AeronConfigFile_Destroy(candidate);
}

/* Two bindings that replace the whole keyboard when stored. */
static void TwoBindings(struct XvtKeyboardBindings *bindings)
{
	AeronKey a, b;
	XVT_ASSERT_TRUE(AeronKey_FromName("A", &a));
	XVT_ASSERT_TRUE(AeronKey_FromName("B", &b));
	memset(bindings, 0, sizeof *bindings);
	bindings->bindings[0] = (struct XvtKeyboardBinding){
		{.key = (uint16_t)b}, XVT_INPUT_ACTION_TARGET_NEXT};
	bindings->bindings[1] = (struct XvtKeyboardBinding){
		{.key = (uint16_t)a}, XVT_INPUT_ACTION_FIRE_WEAPON};
	bindings->count = 2;
}

static void CheckBeforeLoad(void)
{
	Fixture_Begin();
	char error[1024];
	static struct XvtKeyboardBindings bindings;
	TwoBindings(&bindings);
	static struct GameConfig game, before;
	memset(&game, 0x77, sizeof game);
	before = game;
	Fixture_WriteText("asset/legacy.txt", "difficulty 2\n");
	uint64_t generation = XvtConfig_Generation();

	XVT_ASSERT_TRUE(XvtConfig_UserDocument() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_ResolvedDocument() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_Settings() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_DefaultSettings() == NULL);
	XVT_ASSERT_INT_EQ(XvtConfig_CanResetToDefaults(), 0);
	/* Replace and every update need the shipped defaults; the setters are not enabled. */
	XVT_ASSERT_INT_EQ(XvtConfig_ResetToDefaults(error, sizeof error), 0);
	AeronConfigFile *candidate = Fixture_Yaml("version: 3\n");
	XVT_ASSERT_INT_EQ(
		XvtConfig_UpdateUser(candidate, 1, error, sizeof error), 0);
	AeronConfigFile_Destroy(candidate);
	XVT_ASSERT_TRUE(!XvtConfig_SetKeyboard(&bindings, error, sizeof error));
	XVT_ASSERT_TRUE(!XvtConfig_RestoreKeyboard(error, sizeof error));
	XVT_ASSERT_INT_EQ(
		XvtConfig_SetGameData("games", 1, error, sizeof error), 0);
	XVT_ASSERT_TRUE(!XvtConfig_SetFlightRate(true, error, sizeof error));
	XVT_ASSERT_TRUE(!XvtConfig_SetSkipIntro(true, error, sizeof error));
	XVT_ASSERT_INT_EQ(XvtConfig_Save(error, sizeof error), 0);
	XVT_ASSERT_INT_EQ(XvtConfig_Import("legacy.txt", error, sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(XvtConfig_Write(&game, error, sizeof error), 0);
	/* Apply fails and leaves the game options as they were. */
	XVT_ASSERT_INT_EQ(XvtConfig_Apply(&game, error, sizeof error), 0);
	XVT_ASSERT_INT_EQ(memcmp(&game, &before, sizeof game), 0);

	XVT_ASSERT_INT_EQ(XvtConfig_Generation(), generation);
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
	Fixture_End();
}

static void CheckLoadWithoutUserFile(void)
{
	Fixture_Begin();
	uint64_t generation = XvtConfig_Generation();
	Fixture_Load();
	/* Empty overrides start in memory; no file is written. */
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
	XVT_ASSERT_TRUE(XvtConfig_Generation() > generation);
	XVT_ASSERT_INT_EQ(XvtConfig_CanResetToDefaults(), 1);
	XVT_ASSERT_TRUE(XvtConfig_UserDocument() != NULL);
	XVT_ASSERT_TRUE(XvtConfig_ResolvedDocument() != NULL);
	const struct XvtSettings *settings = XvtConfig_Settings();
	const struct XvtSettings *defaults = XvtConfig_DefaultSettings();
	XVT_ASSERT_TRUE(settings != NULL && defaults != NULL);
	/* With no overrides, the settings are the shipped ones. */
	XVT_ASSERT_INT_EQ(settings->skip_intro, defaults->skip_intro);
	XVT_ASSERT_INT_EQ(settings->flight_unlocked, defaults->flight_unlocked);
	XVT_ASSERT_INT_EQ(settings->fullscreen, defaults->fullscreen);
	XVT_ASSERT_INT_EQ(strcmp(settings->game_data, defaults->game_data), 0);
	XVT_ASSERT_INT_EQ(settings->mouse.mouse_sensitivity,
			  defaults->mouse.mouse_sensitivity);
	XVT_ASSERT_INT_EQ(settings->render.msaa_samples,
			  defaults->render.msaa_samples);
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(&settings->keyboard,
						 &defaults->keyboard));
	Fixture_End();
}

static void CheckLoadUpgradesOldUserFile(void)
{
	/* Before version 3: controller settings and joystick buttons are dropped and the controller list
	 * starts empty; a user gamepad_defaults is dropped; version 2's keyboard bindings stay. */
	const char *version2 =
		"version: 2\n"
		"startup:\n"
		"  skip_intro: true\n"
		"input:\n"
		"  joystick: {deadzone: 3}\n"
		"  controllers: [{guid: \"00000000000000000000000000000001\", name: P, layout: gamepad}]\n"
		"  gamepad_defaults: {buttons: {}}\n"
		"  keyboard:\n"
		"    fire_weapon: \"Z\"\n"
		"game:\n"
		"  joybutton1: 5\n";
	Fixture_Begin();
	Fixture_WriteText("user/config.yaml", version2);
	Fixture_Load();
	const AeronConfigFile *user = XvtConfig_UserDocument();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "version", 0), 3);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.joystick"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "game.joybutton1"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.gamepad_defaults"));
	const AeronConfigNode *controllers =
		AeronConfigFile_GetNode(user, "input.controllers");
	XVT_ASSERT_INT_EQ(AeronConfigNode_Type(controllers),
			  AERON_CONFIG_SEQUENCE);
	XVT_ASSERT_INT_EQ(AeronConfigNode_SequenceCount(controllers), 0);
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->controller.count, 0);
	XVT_ASSERT_TRUE(XvtConfig_Settings()->skip_intro != 0);
	AeronKey z;
	XVT_ASSERT_TRUE(AeronKey_FromName("Z", &z));
	size_t found =
		XvtKeyboardMapping_Find(&XvtConfig_Settings()->keyboard,
					(AeronKeyChord){.key = (uint16_t)z});
	XVT_ASSERT_TRUE(found != SIZE_MAX);
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->keyboard.bindings[found].action,
			  XVT_INPUT_ACTION_FIRE_WEAPON);
	/* The upgrade is in memory only: the file is as it was written. */
	char *text = Fixture_ReadText("user/config.yaml");
	XVT_ASSERT_INT_EQ(strcmp(text, version2), 0);
	free(text);
	Fixture_End();

	/* Version 1's keyboard bindings are dropped. */
	Fixture_Begin();
	Fixture_WriteText(
		"user/config.yaml",
		"version: 1\ninput:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	Fixture_Load();
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(XvtConfig_UserDocument(),
					     "input.keyboard"));
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(
		&XvtConfig_Settings()->keyboard,
		&XvtConfig_DefaultSettings()->keyboard));
	Fixture_End();
}

/* Loads with text as USER/config.yaml (or a folder there when text is NULL) and expects the load refused,
 * the error naming the file, the defaults still loaded, and the file untouched; then Replace works. */
static void ExpectUserFileRefused(const char *text)
{
	Fixture_Begin();
	Fixture_Case(text ? text : "(a folder named config.yaml)");
	if (text) {
		Fixture_WriteText("user/config.yaml", text);
	} else {
		Fixture_MakeFolder("user/config.yaml");
	}
	char error[1024] = "";
	XVT_ASSERT_INT_EQ(XvtConfig_Load(g_fixtureVfs, error, sizeof error), 0);
	XVT_ASSERT_TRUE(strstr(error, "USER/config.yaml") != NULL);
	XVT_ASSERT_INT_EQ(XvtConfig_CanResetToDefaults(), 1);
	XVT_ASSERT_TRUE(XvtConfig_DefaultSettings() != NULL);
	XVT_ASSERT_TRUE(XvtConfig_Settings() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_UserDocument() == NULL);

	XVT_ASSERT_INT_EQ(XvtConfig_ResetToDefaults(error, sizeof error), 1);
	XVT_ASSERT_TRUE(XvtConfig_Settings() != NULL);
	if (text) {
		char *now = Fixture_ReadText("user/config.yaml");
		XVT_ASSERT_INT_EQ(strcmp(now, text), 0);
		free(now);
	} else {
		XVT_ASSERT_TRUE(Fixture_Exists("user/config.yaml"));
	}
	Fixture_Case(NULL);
	Fixture_End();
}

static void CheckLoadRefusesBadUserFile(void)
{
	/* Cannot be accepted: an unsupported version, a game option out of range, a document that is not a
	 * map. Cannot be read: broken YAML. Cannot be inspected: a folder where the file should be. */
	ExpectUserFileRefused("version: 9\n");
	ExpectUserFileRefused("version: 3\ngame:\n  difficulty: -1\n");
	ExpectUserFileRefused("- 1\n- 2\n");
	ExpectUserFileRefused("version: 3\nstartup: [\n");
	ExpectUserFileRefused(NULL);
}

/* Loads with text as the shipped defaults (removed when NULL) and expects nothing loaded. */
static void ExpectDefaultsRefused(const char *text)
{
	Fixture_Begin();
	Fixture_Case(text ? text : "(no shipped defaults)");
	if (text) {
		Fixture_WriteText("resource/config.yaml", text);
	} else {
		Fixture_Remove("resource/config.yaml");
	}
	char error[1024] = "";
	XVT_ASSERT_INT_EQ(XvtConfig_Load(g_fixtureVfs, error, sizeof error), 0);
	XVT_ASSERT_TRUE(error[0] != 0);
	XVT_ASSERT_INT_EQ(XvtConfig_CanResetToDefaults(), 0);
	XVT_ASSERT_TRUE(XvtConfig_DefaultSettings() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_Settings() == NULL);
	XVT_ASSERT_INT_EQ(XvtConfig_ResetToDefaults(error, sizeof error), 0);
	Fixture_Case(NULL);
	Fixture_End();
}

static void CheckLoadRefusesBadDefaults(void)
{
	/* The shipped defaults must be valid format 3. */
	ExpectDefaultsRefused("version: 2\n");
	ExpectDefaultsRefused("version: 3\n");
	ExpectDefaultsRefused(NULL);
}

static void CheckUpdateUserRefusals(void)
{
	static const char *const candidates[] = {
		"- 1\n",
		"startup:\n  skip_intro: true\n",
		"version: 0\n",
		"version: 4\n",
		"version: \"3\"\n",
		"version: 3\ngame:\n  difficulty: -1\n",
		"version: 3\ngame:\n  lastpilot: 7\n",
		"version: 3\nrender:\n  msaa_samples: 3\n",
		"version: 3\ninput:\n  keyboard:\n    fire_weapon: \"NoSuchKey\"\n",
	};
	Fixture_Begin();
	Fixture_Load();
	for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; ++i) {
		AeronConfigFile *candidate = Fixture_Yaml(candidates[i]);
		char error[1024] = "";
		Fixture_Case(candidates[i]);
		Snapshot();
		XVT_ASSERT_INT_EQ(
			XvtConfig_UpdateUser(candidate, 1, error, sizeof error),
			0);
		XVT_ASSERT_TRUE(error[0] != 0);
		ExpectUnchanged();
		XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
		Fixture_Case(NULL);
		AeronConfigFile_Destroy(candidate);
	}
	Fixture_End();
}

static void CheckUpdateUserAccepts(void)
{
	Fixture_Begin();
	Fixture_Load();
	const bool skip = XvtConfig_DefaultSettings()->skip_intro != 0;
	char text[256], error[1024] = "";

	/* A version-1 candidate is upgraded: its keyboard bindings and gamepad defaults are dropped, the
	 * controller list starts empty, and the version becomes 3. */
	snprintf(
		text, sizeof text,
		"version: 1\nstartup:\n  skip_intro: %s\ninput:\n  keyboard:\n    fire_weapon: \"Z\"\n"
		"  gamepad_defaults:\n    buttons: {}\n",
		skip ? "false" : "true");
	uint64_t generation = XvtConfig_Generation();
	AeronConfigFile *candidate = Fixture_Yaml(text);
	XVT_ASSERT_INT_EQ(
		XvtConfig_UpdateUser(candidate, 0, error, sizeof error), 1);
	AeronConfigFile_Destroy(candidate);
	XVT_ASSERT_TRUE(XvtConfig_Generation() > generation);
	const AeronConfigFile *user = XvtConfig_UserDocument();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "version", 0), 3);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.keyboard"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.gamepad_defaults"));
	XVT_ASSERT_INT_EQ(AeronConfigNode_SequenceCount(AeronConfigFile_GetNode(
				  user, "input.controllers")),
			  0);
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->skip_intro != 0, !skip);
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));

	/* With save the file is written, and reading it back gives the user overrides. A version-3
	 * candidate's gamepad defaults are dropped too. */
	snprintf(
		text, sizeof text,
		"version: 3\nstartup:\n  skip_intro: %s\ninput:\n  gamepad_defaults:\n    buttons: {}\n",
		skip ? "true" : "false");
	candidate = Fixture_Yaml(text);
	XVT_ASSERT_INT_EQ(
		XvtConfig_UpdateUser(candidate, 1, error, sizeof error), 1);
	AeronConfigFile_Destroy(candidate);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(XvtConfig_UserDocument(),
					     "input.gamepad_defaults"));
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->skip_intro != 0, skip);
	AeronConfigFile *saved = NULL;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_LoadYamlEx(g_fixtureVfs, AERON_VFS_ROOT_USER,
					   "config.yaml", &saved, &detail));
	XVT_ASSERT_TRUE(Fixture_SameDocument(saved, XvtConfig_UserDocument()));
	AeronConfigFile_Destroy(saved);
	Fixture_End();
}

static void CheckSkipIntroAndFlightRate(void)
{
	Fixture_Begin();
	Fixture_Load();
	char error[1024];
	const bool skip = XvtConfig_DefaultSettings()->skip_intro != 0;
	const bool unlocked = XvtConfig_DefaultSettings()->flight_unlocked != 0;

	/* A value other than the shipped one is set; memory only. */
	uint64_t generation = XvtConfig_Generation();
	XVT_ASSERT_TRUE(XvtConfig_SetSkipIntro(!skip, error, sizeof error));
	XVT_ASSERT_TRUE(XvtConfig_Generation() > generation);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetBool(XvtConfig_UserDocument(),
						  "startup.skip_intro", -1),
			  !skip);
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->skip_intro != 0, !skip);
	/* The shipped value removes the override. */
	XVT_ASSERT_TRUE(XvtConfig_SetSkipIntro(skip, error, sizeof error));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(XvtConfig_UserDocument(),
					     "startup.skip_intro"));
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->skip_intro != 0, skip);

	XVT_ASSERT_TRUE(
		XvtConfig_SetFlightRate(!unlocked, error, sizeof error));
	const char *rate = AeronConfigFile_GetString(XvtConfig_UserDocument(),
						     "flight.update_rate", "");
	XVT_ASSERT_INT_EQ(strcmp(rate, unlocked ? "native" : "unlocked"), 0);
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->flight_unlocked != 0,
			  !unlocked);
	XVT_ASSERT_TRUE(XvtConfig_SetFlightRate(unlocked, error, sizeof error));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(XvtConfig_UserDocument(),
					     "flight.update_rate"));
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->flight_unlocked != 0, unlocked);
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
	Fixture_End();
}

static void CheckKeyboardSetters(void)
{
	Fixture_Begin();
	Fixture_Load();
	char error[1024];
	static struct XvtKeyboardBindings bindings;
	TwoBindings(&bindings);

	/* The stored bindings replace the shipped ones; memory only. */
	XVT_ASSERT_TRUE(XvtConfig_SetKeyboard(&bindings, error, sizeof error));
	XVT_ASSERT_TRUE(AeronConfigFile_Has(XvtConfig_UserDocument(),
					    "input.keyboard"));
	XvtKeyboardMapping_Sort(&bindings);
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(
		&XvtConfig_Settings()->keyboard, &bindings));
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));

	/* A reserved key is refused before anything changes. */
	AeronKey escape;
	XVT_ASSERT_TRUE(AeronKey_FromName("Escape", &escape));
	static struct XvtKeyboardBindings bad;
	bad = bindings;
	bad.bindings[0].source.key = (uint16_t)escape;
	Snapshot();
	XVT_ASSERT_TRUE(!XvtConfig_SetKeyboard(&bad, error, sizeof error));
	ExpectUnchanged();

	/* Restoring removes the user's bindings, so the shipped ones apply; memory only. */
	XVT_ASSERT_TRUE(XvtConfig_RestoreKeyboard(error, sizeof error));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(XvtConfig_UserDocument(),
					     "input.keyboard"));
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(
		&XvtConfig_Settings()->keyboard,
		&XvtConfig_DefaultSettings()->keyboard));
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
	Fixture_End();
}

static void CheckSetGameData(void)
{
	Fixture_Begin();
	Fixture_Load();
	char error[1024];
	XVT_ASSERT_INT_EQ(
		XvtConfig_SetGameData("games/xvt", 0, error, sizeof error), 1);
	XVT_ASSERT_INT_EQ(strcmp(XvtConfig_Settings()->game_data, "games/xvt"),
			  0);
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(XvtConfig_UserDocument(),
						 "paths.game_data", ""),
		       "games/xvt"),
		0);
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));

	/* With save the user file is written, and the next load reads the path back. */
	XVT_ASSERT_INT_EQ(
		XvtConfig_SetGameData("games/other", 1, error, sizeof error),
		1);
	XVT_ASSERT_TRUE(Fixture_Exists("user/config.yaml"));
	Fixture_Load();
	XVT_ASSERT_INT_EQ(
		strcmp(XvtConfig_Settings()->game_data, "games/other"), 0);
	Fixture_End();
}

static void CheckApply(void)
{
	Fixture_Begin();
	Fixture_Load();
	AeronConfigFile *candidate =
		UserWithInt("game.single_player.screenres", 1);
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_SetInt(
		candidate, "game.single_player.windowsize", 2, &detail));
	XVT_ASSERT_TRUE(AeronConfigFile_SetInt(
		candidate, "game.multiplayer.screenres", 2, &detail));
	XVT_ASSERT_TRUE(AeronConfigFile_SetInt(
		candidate, "game.multiplayer.windowsize", 0, &detail));
	XVT_ASSERT_TRUE(AeronConfigFile_SetInt(candidate, "game.difficulty",
					       GAME_DIFFICULTY_HARD, &detail));
	XVT_ASSERT_TRUE(AeronConfigFile_SetInt(candidate, "game.random_seed",
					       4000000000, &detail));
	XVT_ASSERT_TRUE(AeronConfigFile_SetString(candidate, "game.lastpilot",
						  "Wedge", &detail));
	Accept(candidate);

	static struct GameConfig game;
	memset(&game, 0x77, sizeof game);
	char error[1024];
	XVT_ASSERT_INT_EQ(XvtConfig_Apply(&game, error, sizeof error), 1);
	XVT_ASSERT_INT_EQ(game.difficulty, GAME_DIFFICULTY_HARD);
	XVT_ASSERT_INT_EQ(game.randomSeed, 4000000000u);
	XVT_ASSERT_INT_EQ(strcmp(game.lastPilotName, "Wedge"), 0);
	/* A window size larger than its screen resolution is limited to it; a smaller one is kept. */
	XVT_ASSERT_INT_EQ(game.screenRes[0], 1);
	XVT_ASSERT_INT_EQ(game.windowSize[0], 1);
	XVT_ASSERT_INT_EQ(game.screenRes[1], 2);
	XVT_ASSERT_INT_EQ(game.windowSize[1], 0);
	/* The network type is TCP/IP and the IP address is cleared. */
	XVT_ASSERT_INT_EQ(game.networkType, NET_TRANSPORT_TCPIP);
	for (size_t i = 0; i < sizeof game.ipAddress; ++i) {
		XVT_ASSERT_INT_EQ(game.ipAddress[i], 0);
	}
	Fixture_End();
}

static void CheckWrite(void)
{
	Fixture_Begin();
	Fixture_Load();
	char error[1024];
	static struct GameConfig game, again, bad;
	XVT_ASSERT_INT_EQ(XvtConfig_Apply(&game, error, sizeof error), 1);
	/* An override that equals the shipped value, to see Write remove it. */
	Accept(UserWithInt("game.collisions", game.collisions));
	XVT_ASSERT_TRUE(AeronConfigFile_Has(XvtConfig_UserDocument(),
					    "game.collisions"));

	const uint8_t difficulty = game.difficulty == GAME_DIFFICULTY_HARD
					   ? GAME_DIFFICULTY_EASY
					   : GAME_DIFFICULTY_HARD;
	game.difficulty = difficulty;
	strcpy(game.password, "rogue");
	XVT_ASSERT_INT_EQ(XvtConfig_Write(&game, error, sizeof error), 1);
	const AeronConfigFile *user = XvtConfig_UserDocument();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.difficulty", -1),
			  difficulty);
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(user, "game.password", ""),
		       "rogue"),
		0);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "game.collisions"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "game.music"));
	/* It saves: the next load gives the options back. */
	XVT_ASSERT_TRUE(Fixture_Exists("user/config.yaml"));
	Fixture_Load();
	XVT_ASSERT_INT_EQ(XvtConfig_Apply(&again, error, sizeof error), 1);
	XVT_ASSERT_INT_EQ(again.difficulty, difficulty);
	XVT_ASSERT_INT_EQ(strcmp(again.password, "rogue"), 0);

	/* An unterminated string or an option out of range is refused, and nothing changes, on disk either. */
	char *saved = Fixture_ReadText("user/config.yaml");
	bad = again;
	memset(bad.password, 'x', sizeof bad.password);
	Snapshot();
	XVT_ASSERT_INT_EQ(XvtConfig_Write(&bad, error, sizeof error), 0);
	ExpectUnchanged();
	bad = again;
	bad.difficulty = 200;
	Snapshot();
	XVT_ASSERT_INT_EQ(XvtConfig_Write(&bad, error, sizeof error), 0);
	ExpectUnchanged();
	char *now = Fixture_ReadText("user/config.yaml");
	XVT_ASSERT_INT_EQ(strcmp(now, saved), 0);
	free(now);
	free(saved);
	Fixture_End();
}

static void CheckSave(void)
{
	Fixture_Begin();
	Fixture_Load();
	char error[1024];
	const bool skip = XvtConfig_DefaultSettings()->skip_intro != 0;
	XVT_ASSERT_TRUE(XvtConfig_SetSkipIntro(!skip, error, sizeof error));
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
	XVT_ASSERT_INT_EQ(XvtConfig_Save(error, sizeof error), 1);
	XVT_ASSERT_TRUE(Fixture_Exists("user/config.yaml"));
	/* A save then a load gives the setting back. */
	Fixture_Load();
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->skip_intro != 0, !skip);
	Fixture_End();
}

static void CheckShutdownKeepsGeneration(void)
{
	Fixture_Begin();
	Fixture_Load();
	char error[1024];
	uint64_t generation = XvtConfig_Generation();
	XvtConfig_Shutdown();
	XVT_ASSERT_INT_EQ(XvtConfig_Generation(), generation);
	XVT_ASSERT_TRUE(XvtConfig_Settings() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_UserDocument() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_ResolvedDocument() == NULL);
	XVT_ASSERT_TRUE(XvtConfig_DefaultSettings() == NULL);
	XVT_ASSERT_INT_EQ(XvtConfig_CanResetToDefaults(), 0);
	XVT_ASSERT_TRUE(!XvtConfig_SetSkipIntro(true, error, sizeof error));
	XVT_ASSERT_INT_EQ(XvtConfig_Save(error, sizeof error), 0);
	/* The next load works and counts on from where the generation was. */
	Fixture_Load();
	XVT_ASSERT_TRUE(XvtConfig_Generation() > generation);
	Fixture_End();
}

/* Imports text as asset/<name>; returns what Import returned. */
static int ImportText(const char *name, const char *text)
{
	char relative[128], error[1024] = "";
	snprintf(relative, sizeof relative, "asset/%s", name);
	Fixture_WriteText(relative, text);
	return XvtConfig_Import(name, error, sizeof error);
}

static void CheckImport(void)
{
	Fixture_Begin();
	Fixture_Load();
	/* Original option names are read and other lines ignored, a YAML path among them; then it saves. */
	XVT_ASSERT_INT_EQ(ImportText("legacy.txt", "difficulty 2\n"
						   "password rogue\n"
						   "not_an_option 5\n"
						   "game.difficulty 3\n"
						   "taunt1 Hello there\n"),
			  1);
	const AeronConfigFile *user = XvtConfig_UserDocument();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.difficulty", -1),
			  2);
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(user, "game.password", ""),
		       "rogue"),
		0);
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(user, "game.taunt1", ""),
		       "Hello there"),
		0);
	XVT_ASSERT_TRUE(Fixture_Exists("user/config.yaml"));
	Fixture_Load();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(XvtConfig_UserDocument(),
						 "game.difficulty", -1),
			  2);

	/* From a file named config.cfg, joystick buttons 124 to 229 move up by 4; from another name they do
	 * not. */
	const char *buttons =
		"joybutton1 124\njoybutton2 229\njoybutton3 123\njoybutton4 230\n";
	XVT_ASSERT_INT_EQ(ImportText("config.cfg", buttons), 1);
	user = XvtConfig_UserDocument();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton1", -1),
			  128);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton2", -1),
			  233);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton3", -1),
			  123);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton4", -1),
			  230);
	XVT_ASSERT_INT_EQ(ImportText("other.cfg", buttons), 1);
	user = XvtConfig_UserDocument();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton1", -1),
			  124);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton2", -1),
			  229);
	Fixture_End();
}

/* A line of length characters, all 'z', which names no option. */
static char *LongLine(size_t length, const char *ending)
{
	char *text = malloc(length + strlen(ending) + 32);
	XVT_ASSERT_TRUE(text != NULL);
	memset(text, 'z', length);
	strcpy(text + length, ending);
	return text;
}

static void CheckImportRefusals(void)
{
	static const char *const files[] = {
		"not_an_option 5\n", /* no name recognized */
		"difficulty two\n",  /* a value that is not a number */
		"difficulty -1\n",   /* out of range */
		"difficulty 4\n",    /* beyond the hardest difficulty */
		"difficulty 2\ndifficulty 3x\n", /* one bad value stops the earlier good one too */
		"difficulty 2\nmusic_volume many\n", /* the same, for another option */
	};
	Fixture_Begin();
	Fixture_Load();
	for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
		Fixture_Case(files[i]);
		Snapshot();
		XVT_ASSERT_INT_EQ(ImportText("legacy.txt", files[i]), 0);
		ExpectUnchanged();
		XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
		Fixture_Case(NULL);
	}
	/* A line of 511 characters is refused; one of 510 is read (and ignored, naming no option). */
	char *line = LongLine(511, "\ndifficulty 2\n");
	Snapshot();
	XVT_ASSERT_INT_EQ(ImportText("legacy.txt", line), 0);
	ExpectUnchanged();
	free(line);
	line = LongLine(510, "\ndifficulty 2\n");
	XVT_ASSERT_INT_EQ(ImportText("legacy.txt", line), 1);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(XvtConfig_UserDocument(),
						 "game.difficulty", -1),
			  2);
	free(line);
	/* A file that cannot be read. */
	Snapshot();
	char error[1024];
	XVT_ASSERT_INT_EQ(XvtConfig_Import("absent.txt", error, sizeof error),
			  0);
	ExpectUnchanged();
	Fixture_End();
}

/* Known failure. The header refuses a line longer than 510 characters. A line of exactly 510 characters
 * that ends in CR LF, as a DOS text file's lines do, is refused as well: the read that fills the 512-byte
 * line buffer stops after the CR, so the line looks 511 characters long with no newline. */
static void CheckImportCrLfLineOf510(void)
{
	Fixture_Begin();
	Fixture_Load();
	char *line = LongLine(510, "\r\ndifficulty 2\r\n");
	XVT_ASSERT_INT_EQ(ImportText("legacy.txt", line), 1);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(XvtConfig_UserDocument(),
						 "game.difficulty", -1),
			  2);
	free(line);
	Fixture_End();
}

/* With no arguments, runs every check that holds. "known-failure <name>" runs only that check, which shows
 * the code breaking its header; a name not listed here returns 0. */
int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "import_crlf_line_of_510") == 0) {
			CheckImportCrLfLineOf510();
		}
		return 0;
	}
	CheckBeforeLoad();
	CheckLoadWithoutUserFile();
	CheckLoadUpgradesOldUserFile();
	CheckLoadRefusesBadUserFile();
	CheckLoadRefusesBadDefaults();
	CheckUpdateUserRefusals();
	CheckUpdateUserAccepts();
	CheckSkipIntroAndFlightRate();
	CheckKeyboardSetters();
	CheckSetGameData();
	CheckApply();
	CheckWrite();
	CheckSave();
	CheckShutdownKeepsGeneration();
	CheckImport();
	CheckImportRefusals();
	return 0;
}

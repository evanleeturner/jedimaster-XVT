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
struct state {
	uint64_t generation;
	AeronConfigFile *user;
	AeronConfigFile *resolved;
	int has_settings;
	struct xvt_settings settings;
};

static struct state g_state;

static void snapshot(void)
{
	AeronConfigError detail;
	memset(&g_state, 0, sizeof g_state);
	g_state.generation = xvt_config_generation();
	g_state.user = fixture_user_copy();
	if (xvt_config_resolved_document()) {
		XVT_ASSERT_TRUE(
			AeronConfigFile_Clone(xvt_config_resolved_document(),
					      &g_state.resolved, &detail));
	}
	g_state.has_settings = xvt_config_settings() != NULL;
	if (g_state.has_settings) {
		memcpy(&g_state.settings, xvt_config_settings(),
		       sizeof g_state.settings);
	}
}

static void expect_unchanged(void)
{
	XVT_ASSERT_INT_EQ(xvt_config_generation(), g_state.generation);
	XVT_ASSERT_TRUE(fixture_same_document(xvt_config_user_document(),
					      g_state.user));
	XVT_ASSERT_TRUE(fixture_same_document(xvt_config_resolved_document(),
					      g_state.resolved));
	XVT_ASSERT_INT_EQ(xvt_config_settings() != NULL, g_state.has_settings);
	if (g_state.has_settings) {
		XVT_ASSERT_INT_EQ(memcmp(xvt_config_settings(),
					 &g_state.settings,
					 sizeof g_state.settings),
				  0);
	}
	AeronConfigFile_Destroy(g_state.user);
	AeronConfigFile_Destroy(g_state.resolved);
	memset(&g_state, 0, sizeof g_state);
}

/* A copy of the user overrides with one value set, for xvt_config_update_user. */
static AeronConfigFile *user_with_int(const char *path, int64_t value)
{
	AeronConfigFile *candidate = fixture_user_copy();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_SetInt(candidate, path, value, &detail));
	return candidate;
}

/* Hands candidate to xvt_config_update_user without saving, expects it accepted, then destroys it. */
static void accept(AeronConfigFile *candidate)
{
	char error[1024] = "";
	int accepted =
		xvt_config_update_user(candidate, 0, error, sizeof error);
	if (!accepted) {
		fprintf(stderr, "refused: %s\n", error);
	}
	XVT_ASSERT_INT_EQ(accepted, 1);
	AeronConfigFile_Destroy(candidate);
}

/* Two bindings that replace the whole keyboard when stored. */
static void two_bindings(struct xvt_keyboard_bindings *bindings)
{
	AeronKey a, b;
	XVT_ASSERT_TRUE(AeronKey_FromName("A", &a));
	XVT_ASSERT_TRUE(AeronKey_FromName("B", &b));
	memset(bindings, 0, sizeof *bindings);
	bindings->bindings[0] = (struct xvt_keyboard_binding){
		{.key = (uint16_t)b}, XVT_INPUT_ACTION_TARGET_NEXT};
	bindings->bindings[1] = (struct xvt_keyboard_binding){
		{.key = (uint16_t)a}, XVT_INPUT_ACTION_FIRE_WEAPON};
	bindings->count = 2;
}

static void check_before_load(void)
{
	fixture_begin();
	char error[1024];
	static struct xvt_keyboard_bindings bindings;
	two_bindings(&bindings);
	static struct game_config game, before;
	memset(&game, 0x77, sizeof game);
	before = game;
	fixture_write_text("asset/legacy.txt", "difficulty 2\n");
	uint64_t generation = xvt_config_generation();

	XVT_ASSERT_TRUE(xvt_config_user_document() == NULL);
	XVT_ASSERT_TRUE(xvt_config_resolved_document() == NULL);
	XVT_ASSERT_TRUE(xvt_config_settings() == NULL);
	XVT_ASSERT_TRUE(xvt_config_default_settings() == NULL);
	XVT_ASSERT_INT_EQ(xvt_config_can_reset_to_defaults(), 0);
	/* Replace and every update need the shipped defaults; the setters are not enabled. */
	XVT_ASSERT_INT_EQ(xvt_config_reset_to_defaults(error, sizeof error), 0);
	AeronConfigFile *candidate = fixture_yaml("version: 3\n");
	XVT_ASSERT_INT_EQ(
		xvt_config_update_user(candidate, 1, error, sizeof error), 0);
	AeronConfigFile_Destroy(candidate);
	XVT_ASSERT_TRUE(
		!xvt_config_set_keyboard(&bindings, error, sizeof error));
	XVT_ASSERT_TRUE(!xvt_config_restore_keyboard(error, sizeof error));
	XVT_ASSERT_INT_EQ(
		xvt_config_set_game_data("games", 1, error, sizeof error), 0);
	XVT_ASSERT_TRUE(!xvt_config_set_flight_rate(true, error, sizeof error));
	XVT_ASSERT_TRUE(!xvt_config_set_skip_intro(true, error, sizeof error));
	XVT_ASSERT_INT_EQ(xvt_config_save(error, sizeof error), 0);
	XVT_ASSERT_INT_EQ(xvt_config_import("legacy.txt", error, sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(xvt_config_write(&game, error, sizeof error), 0);
	/* Apply fails and leaves the game options as they were. */
	XVT_ASSERT_INT_EQ(xvt_config_apply(&game, error, sizeof error), 0);
	XVT_ASSERT_INT_EQ(memcmp(&game, &before, sizeof game), 0);

	XVT_ASSERT_INT_EQ(xvt_config_generation(), generation);
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
	fixture_end();
}

static void check_load_without_user_file(void)
{
	fixture_begin();
	uint64_t generation = xvt_config_generation();
	fixture_load();
	/* Empty overrides start in memory; no file is written. */
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
	XVT_ASSERT_TRUE(xvt_config_generation() > generation);
	XVT_ASSERT_INT_EQ(xvt_config_can_reset_to_defaults(), 1);
	XVT_ASSERT_TRUE(xvt_config_user_document() != NULL);
	XVT_ASSERT_TRUE(xvt_config_resolved_document() != NULL);
	const struct xvt_settings *settings = xvt_config_settings();
	const struct xvt_settings *defaults = xvt_config_default_settings();
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
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(&settings->keyboard,
						   &defaults->keyboard));
	fixture_end();
}

static void check_load_upgrades_old_user_file(void)
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
	fixture_begin();
	fixture_write_text("user/config.yaml", version2);
	fixture_load();
	const AeronConfigFile *user = xvt_config_user_document();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "version", 0), 3);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.joystick"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "game.joybutton1"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.gamepad_defaults"));
	const AeronConfigNode *controllers =
		AeronConfigFile_GetNode(user, "input.controllers");
	XVT_ASSERT_INT_EQ(AeronConfigNode_Type(controllers),
			  AERON_CONFIG_SEQUENCE);
	XVT_ASSERT_INT_EQ(AeronConfigNode_SequenceCount(controllers), 0);
	XVT_ASSERT_INT_EQ(xvt_config_settings()->controller.count, 0);
	XVT_ASSERT_TRUE(xvt_config_settings()->skip_intro != 0);
	AeronKey z;
	XVT_ASSERT_TRUE(AeronKey_FromName("Z", &z));
	size_t found =
		xvt_keyboard_mapping_find(&xvt_config_settings()->keyboard,
					  (AeronKeyChord){.key = (uint16_t)z});
	XVT_ASSERT_TRUE(found != SIZE_MAX);
	XVT_ASSERT_INT_EQ(
		xvt_config_settings()->keyboard.bindings[found].action,
		XVT_INPUT_ACTION_FIRE_WEAPON);
	/* The upgrade is in memory only: the file is as it was written. */
	char *text = fixture_read_text("user/config.yaml");
	XVT_ASSERT_INT_EQ(strcmp(text, version2), 0);
	free(text);
	fixture_end();

	/* Version 1's keyboard bindings are dropped. */
	fixture_begin();
	fixture_write_text(
		"user/config.yaml",
		"version: 1\ninput:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	fixture_load();
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(xvt_config_user_document(),
					     "input.keyboard"));
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(
		&xvt_config_settings()->keyboard,
		&xvt_config_default_settings()->keyboard));
	fixture_end();
}

/* Loads with text as USER/config.yaml (or a folder there when text is NULL) and expects the load refused,
 * the error naming the file, the defaults still loaded, and the file untouched; then Replace works. */
static void expect_user_file_refused(const char *text)
{
	fixture_begin();
	fixture_case(text ? text : "(a folder named config.yaml)");
	if (text) {
		fixture_write_text("user/config.yaml", text);
	} else {
		fixture_make_folder("user/config.yaml");
	}
	char error[1024] = "";
	XVT_ASSERT_INT_EQ(xvt_config_load(g_fixture_vfs, error, sizeof error),
			  0);
	XVT_ASSERT_TRUE(strstr(error, "USER/config.yaml") != NULL);
	XVT_ASSERT_INT_EQ(xvt_config_can_reset_to_defaults(), 1);
	XVT_ASSERT_TRUE(xvt_config_default_settings() != NULL);
	XVT_ASSERT_TRUE(xvt_config_settings() == NULL);
	XVT_ASSERT_TRUE(xvt_config_user_document() == NULL);

	XVT_ASSERT_INT_EQ(xvt_config_reset_to_defaults(error, sizeof error), 1);
	XVT_ASSERT_TRUE(xvt_config_settings() != NULL);
	if (text) {
		char *now = fixture_read_text("user/config.yaml");
		XVT_ASSERT_INT_EQ(strcmp(now, text), 0);
		free(now);
	} else {
		XVT_ASSERT_TRUE(fixture_exists("user/config.yaml"));
	}
	fixture_case(NULL);
	fixture_end();
}

static void check_load_refuses_bad_user_file(void)
{
	/* Cannot be accepted: an unsupported version, a game option out of range, a document that is not a
	 * map. Cannot be read: broken YAML. Cannot be inspected: a folder where the file should be. */
	expect_user_file_refused("version: 9\n");
	expect_user_file_refused("version: 3\ngame:\n  difficulty: -1\n");
	expect_user_file_refused("- 1\n- 2\n");
	expect_user_file_refused("version: 3\nstartup: [\n");
	expect_user_file_refused(NULL);
}

/* Loads with text as the shipped defaults (removed when NULL) and expects nothing loaded. */
static void expect_defaults_refused(const char *text)
{
	fixture_begin();
	fixture_case(text ? text : "(no shipped defaults)");
	if (text) {
		fixture_write_text("resource/config.yaml", text);
	} else {
		fixture_remove("resource/config.yaml");
	}
	char error[1024] = "";
	XVT_ASSERT_INT_EQ(xvt_config_load(g_fixture_vfs, error, sizeof error),
			  0);
	XVT_ASSERT_TRUE(error[0] != 0);
	XVT_ASSERT_INT_EQ(xvt_config_can_reset_to_defaults(), 0);
	XVT_ASSERT_TRUE(xvt_config_default_settings() == NULL);
	XVT_ASSERT_TRUE(xvt_config_settings() == NULL);
	XVT_ASSERT_INT_EQ(xvt_config_reset_to_defaults(error, sizeof error), 0);
	fixture_case(NULL);
	fixture_end();
}

static void check_load_refuses_bad_defaults(void)
{
	/* The shipped defaults must be valid format 3. */
	expect_defaults_refused("version: 2\n");
	expect_defaults_refused("version: 3\n");
	expect_defaults_refused(NULL);
}

static void check_update_user_refusals(void)
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
	fixture_begin();
	fixture_load();
	for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; ++i) {
		AeronConfigFile *candidate = fixture_yaml(candidates[i]);
		char error[1024] = "";
		fixture_case(candidates[i]);
		snapshot();
		XVT_ASSERT_INT_EQ(xvt_config_update_user(candidate, 1, error,
							 sizeof error),
				  0);
		XVT_ASSERT_TRUE(error[0] != 0);
		expect_unchanged();
		XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
		fixture_case(NULL);
		AeronConfigFile_Destroy(candidate);
	}
	fixture_end();
}

static void check_update_user_accepts(void)
{
	fixture_begin();
	fixture_load();
	const bool skip = xvt_config_default_settings()->skip_intro != 0;
	char text[256], error[1024] = "";

	/* A version-1 candidate is upgraded: its keyboard bindings and gamepad defaults are dropped, the
	 * controller list starts empty, and the version becomes 3. */
	snprintf(
		text, sizeof text,
		"version: 1\nstartup:\n  skip_intro: %s\ninput:\n  keyboard:\n    fire_weapon: \"Z\"\n"
		"  gamepad_defaults:\n    buttons: {}\n",
		skip ? "false" : "true");
	uint64_t generation = xvt_config_generation();
	AeronConfigFile *candidate = fixture_yaml(text);
	XVT_ASSERT_INT_EQ(
		xvt_config_update_user(candidate, 0, error, sizeof error), 1);
	AeronConfigFile_Destroy(candidate);
	XVT_ASSERT_TRUE(xvt_config_generation() > generation);
	const AeronConfigFile *user = xvt_config_user_document();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "version", 0), 3);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.keyboard"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.gamepad_defaults"));
	XVT_ASSERT_INT_EQ(AeronConfigNode_SequenceCount(AeronConfigFile_GetNode(
				  user, "input.controllers")),
			  0);
	XVT_ASSERT_INT_EQ(xvt_config_settings()->skip_intro != 0, !skip);
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));

	/* With save the file is written, and reading it back gives the user overrides. A version-3
	 * candidate's gamepad defaults are dropped too. */
	snprintf(
		text, sizeof text,
		"version: 3\nstartup:\n  skip_intro: %s\ninput:\n  gamepad_defaults:\n    buttons: {}\n",
		skip ? "true" : "false");
	candidate = fixture_yaml(text);
	XVT_ASSERT_INT_EQ(
		xvt_config_update_user(candidate, 1, error, sizeof error), 1);
	AeronConfigFile_Destroy(candidate);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(xvt_config_user_document(),
					     "input.gamepad_defaults"));
	XVT_ASSERT_INT_EQ(xvt_config_settings()->skip_intro != 0, skip);
	AeronConfigFile *saved = NULL;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_LoadYamlEx(g_fixture_vfs, AERON_VFS_ROOT_USER,
					   "config.yaml", &saved, &detail));
	XVT_ASSERT_TRUE(
		fixture_same_document(saved, xvt_config_user_document()));
	AeronConfigFile_Destroy(saved);
	fixture_end();
}

static void check_skip_intro_and_flight_rate(void)
{
	fixture_begin();
	fixture_load();
	char error[1024];
	const bool skip = xvt_config_default_settings()->skip_intro != 0;
	const bool unlocked =
		xvt_config_default_settings()->flight_unlocked != 0;

	/* A value other than the shipped one is set; memory only. */
	uint64_t generation = xvt_config_generation();
	XVT_ASSERT_TRUE(xvt_config_set_skip_intro(!skip, error, sizeof error));
	XVT_ASSERT_TRUE(xvt_config_generation() > generation);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetBool(xvt_config_user_document(),
						  "startup.skip_intro", -1),
			  !skip);
	XVT_ASSERT_INT_EQ(xvt_config_settings()->skip_intro != 0, !skip);
	/* The shipped value removes the override. */
	XVT_ASSERT_TRUE(xvt_config_set_skip_intro(skip, error, sizeof error));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(xvt_config_user_document(),
					     "startup.skip_intro"));
	XVT_ASSERT_INT_EQ(xvt_config_settings()->skip_intro != 0, skip);

	XVT_ASSERT_TRUE(
		xvt_config_set_flight_rate(!unlocked, error, sizeof error));
	const char *rate = AeronConfigFile_GetString(xvt_config_user_document(),
						     "flight.update_rate", "");
	XVT_ASSERT_INT_EQ(strcmp(rate, unlocked ? "native" : "unlocked"), 0);
	XVT_ASSERT_INT_EQ(xvt_config_settings()->flight_unlocked != 0,
			  !unlocked);
	XVT_ASSERT_TRUE(
		xvt_config_set_flight_rate(unlocked, error, sizeof error));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(xvt_config_user_document(),
					     "flight.update_rate"));
	XVT_ASSERT_INT_EQ(xvt_config_settings()->flight_unlocked != 0,
			  unlocked);
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
	fixture_end();
}

static void check_keyboard_setters(void)
{
	fixture_begin();
	fixture_load();
	char error[1024];
	static struct xvt_keyboard_bindings bindings;
	two_bindings(&bindings);

	/* The stored bindings replace the shipped ones; memory only. */
	XVT_ASSERT_TRUE(
		xvt_config_set_keyboard(&bindings, error, sizeof error));
	XVT_ASSERT_TRUE(AeronConfigFile_Has(xvt_config_user_document(),
					    "input.keyboard"));
	xvt_keyboard_mapping_sort(&bindings);
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(
		&xvt_config_settings()->keyboard, &bindings));
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));

	/* A reserved key is refused before anything changes. */
	AeronKey escape;
	XVT_ASSERT_TRUE(AeronKey_FromName("Escape", &escape));
	static struct xvt_keyboard_bindings bad;
	bad = bindings;
	bad.bindings[0].source.key = (uint16_t)escape;
	snapshot();
	XVT_ASSERT_TRUE(!xvt_config_set_keyboard(&bad, error, sizeof error));
	expect_unchanged();

	/* Restoring removes the user's bindings, so the shipped ones apply; memory only. */
	XVT_ASSERT_TRUE(xvt_config_restore_keyboard(error, sizeof error));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(xvt_config_user_document(),
					     "input.keyboard"));
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(
		&xvt_config_settings()->keyboard,
		&xvt_config_default_settings()->keyboard));
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
	fixture_end();
}

static void check_set_game_data(void)
{
	fixture_begin();
	fixture_load();
	char error[1024];
	XVT_ASSERT_INT_EQ(
		xvt_config_set_game_data("games/xvt", 0, error, sizeof error),
		1);
	XVT_ASSERT_INT_EQ(strcmp(xvt_config_settings()->game_data, "games/xvt"),
			  0);
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(xvt_config_user_document(),
						 "paths.game_data", ""),
		       "games/xvt"),
		0);
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));

	/* With save the user file is written, and the next load reads the path back. */
	XVT_ASSERT_INT_EQ(
		xvt_config_set_game_data("games/other", 1, error, sizeof error),
		1);
	XVT_ASSERT_TRUE(fixture_exists("user/config.yaml"));
	fixture_load();
	XVT_ASSERT_INT_EQ(
		strcmp(xvt_config_settings()->game_data, "games/other"), 0);
	fixture_end();
}

static void check_apply(void)
{
	fixture_begin();
	fixture_load();
	AeronConfigFile *candidate =
		user_with_int("game.single_player.screenres", 1);
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
	accept(candidate);

	static struct game_config game;
	memset(&game, 0x77, sizeof game);
	char error[1024];
	XVT_ASSERT_INT_EQ(xvt_config_apply(&game, error, sizeof error), 1);
	XVT_ASSERT_INT_EQ(game.difficulty, GAME_DIFFICULTY_HARD);
	XVT_ASSERT_INT_EQ(game.random_seed, 4000000000u);
	XVT_ASSERT_INT_EQ(strcmp(game.last_pilot_name, "Wedge"), 0);
	/* A window size larger than its screen resolution is limited to it; a smaller one is kept. */
	XVT_ASSERT_INT_EQ(game.screen_res[0], 1);
	XVT_ASSERT_INT_EQ(game.window_size[0], 1);
	XVT_ASSERT_INT_EQ(game.screen_res[1], 2);
	XVT_ASSERT_INT_EQ(game.window_size[1], 0);
	/* The network type is TCP/IP and the IP address is cleared. */
	XVT_ASSERT_INT_EQ(game.network_type, NET_TRANSPORT_TCPIP);
	for (size_t i = 0; i < sizeof game.ip_address; ++i) {
		XVT_ASSERT_INT_EQ(game.ip_address[i], 0);
	}
	fixture_end();
}

static void check_write(void)
{
	fixture_begin();
	fixture_load();
	char error[1024];
	static struct game_config game, again, bad;
	XVT_ASSERT_INT_EQ(xvt_config_apply(&game, error, sizeof error), 1);
	/* An override that equals the shipped value, to see Write remove it. */
	accept(user_with_int("game.collisions", game.collisions));
	XVT_ASSERT_TRUE(AeronConfigFile_Has(xvt_config_user_document(),
					    "game.collisions"));

	const uint8_t difficulty = game.difficulty == GAME_DIFFICULTY_HARD
					   ? GAME_DIFFICULTY_EASY
					   : GAME_DIFFICULTY_HARD;
	game.difficulty = difficulty;
	strcpy(game.password, "rogue");
	XVT_ASSERT_INT_EQ(xvt_config_write(&game, error, sizeof error), 1);
	const AeronConfigFile *user = xvt_config_user_document();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.difficulty", -1),
			  difficulty);
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(user, "game.password", ""),
		       "rogue"),
		0);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "game.collisions"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "game.music"));
	/* It saves: the next load gives the options back. */
	XVT_ASSERT_TRUE(fixture_exists("user/config.yaml"));
	fixture_load();
	XVT_ASSERT_INT_EQ(xvt_config_apply(&again, error, sizeof error), 1);
	XVT_ASSERT_INT_EQ(again.difficulty, difficulty);
	XVT_ASSERT_INT_EQ(strcmp(again.password, "rogue"), 0);

	/* An unterminated string or an option out of range is refused, and nothing changes, on disk either. */
	char *saved = fixture_read_text("user/config.yaml");
	bad = again;
	memset(bad.password, 'x', sizeof bad.password);
	snapshot();
	XVT_ASSERT_INT_EQ(xvt_config_write(&bad, error, sizeof error), 0);
	expect_unchanged();
	bad = again;
	bad.difficulty = 200;
	snapshot();
	XVT_ASSERT_INT_EQ(xvt_config_write(&bad, error, sizeof error), 0);
	expect_unchanged();
	char *now = fixture_read_text("user/config.yaml");
	XVT_ASSERT_INT_EQ(strcmp(now, saved), 0);
	free(now);
	free(saved);
	fixture_end();
}

static void check_save(void)
{
	fixture_begin();
	fixture_load();
	char error[1024];
	const bool skip = xvt_config_default_settings()->skip_intro != 0;
	XVT_ASSERT_TRUE(xvt_config_set_skip_intro(!skip, error, sizeof error));
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
	XVT_ASSERT_INT_EQ(xvt_config_save(error, sizeof error), 1);
	XVT_ASSERT_TRUE(fixture_exists("user/config.yaml"));
	/* A save then a load gives the setting back. */
	fixture_load();
	XVT_ASSERT_INT_EQ(xvt_config_settings()->skip_intro != 0, !skip);
	fixture_end();
}

static void check_shutdown_keeps_generation(void)
{
	fixture_begin();
	fixture_load();
	char error[1024];
	uint64_t generation = xvt_config_generation();
	xvt_config_shutdown();
	XVT_ASSERT_INT_EQ(xvt_config_generation(), generation);
	XVT_ASSERT_TRUE(xvt_config_settings() == NULL);
	XVT_ASSERT_TRUE(xvt_config_user_document() == NULL);
	XVT_ASSERT_TRUE(xvt_config_resolved_document() == NULL);
	XVT_ASSERT_TRUE(xvt_config_default_settings() == NULL);
	XVT_ASSERT_INT_EQ(xvt_config_can_reset_to_defaults(), 0);
	XVT_ASSERT_TRUE(!xvt_config_set_skip_intro(true, error, sizeof error));
	XVT_ASSERT_INT_EQ(xvt_config_save(error, sizeof error), 0);
	/* The next load works and counts on from where the generation was. */
	fixture_load();
	XVT_ASSERT_TRUE(xvt_config_generation() > generation);
	fixture_end();
}

/* Imports text as asset/<name>; returns what Import returned. */
static int import_text(const char *name, const char *text)
{
	char relative[128], error[1024] = "";
	snprintf(relative, sizeof relative, "asset/%s", name);
	fixture_write_text(relative, text);
	return xvt_config_import(name, error, sizeof error);
}

static void check_import(void)
{
	fixture_begin();
	fixture_load();
	/* Original option names are read and other lines ignored, a YAML path among them; then it saves. */
	XVT_ASSERT_INT_EQ(import_text("legacy.txt", "difficulty 2\n"
						    "password rogue\n"
						    "not_an_option 5\n"
						    "game.difficulty 3\n"
						    "taunt1 Hello there\n"),
			  1);
	const AeronConfigFile *user = xvt_config_user_document();
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
	XVT_ASSERT_TRUE(fixture_exists("user/config.yaml"));
	fixture_load();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(xvt_config_user_document(),
						 "game.difficulty", -1),
			  2);

	/* From a file named config.cfg, joystick buttons 124 to 229 move up by 4; from another name they do
	 * not. */
	const char *buttons =
		"joybutton1 124\njoybutton2 229\njoybutton3 123\njoybutton4 230\n";
	XVT_ASSERT_INT_EQ(import_text("config.cfg", buttons), 1);
	user = xvt_config_user_document();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton1", -1),
			  128);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton2", -1),
			  233);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton3", -1),
			  123);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton4", -1),
			  230);
	XVT_ASSERT_INT_EQ(import_text("other.cfg", buttons), 1);
	user = xvt_config_user_document();
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton1", -1),
			  124);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(user, "game.joybutton2", -1),
			  229);
	fixture_end();
}

/* A line of length characters, all 'z', which names no option. */
static char *long_line(size_t length, const char *ending)
{
	char *text = malloc(length + strlen(ending) + 32);
	XVT_ASSERT_TRUE(text != NULL);
	memset(text, 'z', length);
	strcpy(text + length, ending);
	return text;
}

static void check_import_refusals(void)
{
	static const char *const files[] = {
		"not_an_option 5\n", /* no name recognized */
		"difficulty two\n",  /* a value that is not a number */
		"difficulty -1\n",   /* out of range */
		"difficulty 4\n",    /* beyond the hardest difficulty */
		"difficulty 2\ndifficulty 3x\n", /* one bad value stops the earlier good one too */
		"difficulty 2\nmusic_volume many\n", /* the same, for another option */
	};
	fixture_begin();
	fixture_load();
	for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
		fixture_case(files[i]);
		snapshot();
		XVT_ASSERT_INT_EQ(import_text("legacy.txt", files[i]), 0);
		expect_unchanged();
		XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
		fixture_case(NULL);
	}
	/* A line of 511 characters is refused; one of 510 is read (and ignored, naming no option). */
	char *line = long_line(511, "\ndifficulty 2\n");
	snapshot();
	XVT_ASSERT_INT_EQ(import_text("legacy.txt", line), 0);
	expect_unchanged();
	free(line);
	line = long_line(510, "\ndifficulty 2\n");
	XVT_ASSERT_INT_EQ(import_text("legacy.txt", line), 1);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(xvt_config_user_document(),
						 "game.difficulty", -1),
			  2);
	free(line);
	/* A file that cannot be read. */
	snapshot();
	char error[1024];
	XVT_ASSERT_INT_EQ(xvt_config_import("absent.txt", error, sizeof error),
			  0);
	expect_unchanged();
	fixture_end();
}

/* Known failure. The header refuses a line longer than 510 characters. A line of exactly 510 characters
 * that ends in CR LF, as a DOS text file's lines do, is refused as well: the read that fills the 512-byte
 * line buffer stops after the CR, so the line looks 511 characters long with no newline. */
static void check_import_cr_lf_line_of510(void)
{
	fixture_begin();
	fixture_load();
	char *line = long_line(510, "\r\ndifficulty 2\r\n");
	XVT_ASSERT_INT_EQ(import_text("legacy.txt", line), 1);
	XVT_ASSERT_INT_EQ(AeronConfigFile_GetInt(xvt_config_user_document(),
						 "game.difficulty", -1),
			  2);
	free(line);
	fixture_end();
}

/* With no arguments, runs every check that holds. "known-failure <name>" runs only that check, which shows
 * the code breaking its header; a name not listed here returns 0. */
int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "import_crlf_line_of_510") == 0) {
			check_import_cr_lf_line_of510();
		}
		return 0;
	}
	check_before_load();
	check_load_without_user_file();
	check_load_upgrades_old_user_file();
	check_load_refuses_bad_user_file();
	check_load_refuses_bad_defaults();
	check_update_user_refusals();
	check_update_user_accepts();
	check_skip_intro_and_flight_rate();
	check_keyboard_setters();
	check_set_game_data();
	check_apply();
	check_write();
	check_save();
	check_shutdown_keeps_generation();
	check_import();
	check_import_refusals();
	return 0;
}

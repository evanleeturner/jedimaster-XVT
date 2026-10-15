/* Checks the options a launched battle flies with
 * (xvt_runtime/runtime/launch_options.h) against the promises in its header.
 * The network cases read a made-up melee description through the real parser
 * and take their shipped options from a made-up struct, each option a number
 * no other option holds, so an option read from the wrong field shows. The
 * player's own options start as 0x77 in every byte: none may survive into a
 * network battle's options, and every other field must. Mission types and
 * option values are the game's numbers: types 1 melee, 3 combat engagement,
 * 4 battle, 5 campaign; difficulty 0 easy, 2 hard, 3 easy cheat; sequence
 * 0 restart, 1 continue. */
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/runtime/launch_options.h"

static struct game_config g_game;
static struct game_config g_before;
static struct game_config g_shipped;

/* Reads a network melee description for Ace, seed 7, with settings as the
 * extra text before the network part ("" for none). */
static void read_network(struct xvt_launch_description *description,
			 const char *settings)
{
	char text[1024];
	snprintf(text, sizeof(text),
		 "{\"schema_version\":1,\"mode\":\"network\","
		 "\"game_type\":\"melee\",\"side\":\"none\","
		 "\"mission\":\"melee/m1.tie\",\"pilot\":\"Ace\",\"seed\":7,%s"
		 "\"network\":{\"role\":\"join\",\"lobby\":\"https://h\","
		 "\"game\":\"0123abcd-4567-89ef-0123-456789abcdef\","
		 "\"players\":[\"Rook\",\"Ace\"]}}",
		 settings);
	char why[XVT_LAUNCH_WHY_CAPACITY];
	int parsed = xvt_launch_description_parse(text, description, why,
						  sizeof(why));
	if (!parsed) {
		fprintf(stderr, "refused: %s\n", why);
	}
	XVT_ASSERT_INT_EQ(parsed, 1);
}

/* The made-up shipped options: sixteen distinct numbers and a seed. */
static void make_shipped(void)
{
	memset(&g_shipped, 0, sizeof(g_shipped));
	g_shipped.difficulty = 1;
	g_shipped.collisions = 11;
	g_shipped.craft_jumping = 12;
	g_shipped.random_setup = 13;
	g_shipped.battle_length_index = 14;
	g_shipped.in_progress_join = 15;
	g_shipped.craft_selection = 16;
	g_shipped.locate_players = 17;
	g_shipped.craft_waves = 18;
	g_shipped.mission_time_limit = 19;
	g_shipped.last_team_time_limit_minutes = 20;
	g_shipped.internet_play = 21;
	g_shipped.ai_opponents = 22;
	g_shipped.server_update_rate = 23;
	g_shipped.combat_balance = 24;
	g_shipped.continue_battle_or_campaign = 0;
	g_shipped.random_seed = 99;
}

static void check_apply_settings(void)
{
	struct xvt_launch_settings settings;
	settings.difficulty = XVT_LAUNCH_SETTING_UNSET;
	settings.collisions = XVT_LAUNCH_SETTING_UNSET;
	settings.craft_jumping = XVT_LAUNCH_SETTING_UNSET;
	settings.random_setup = XVT_LAUNCH_SETTING_UNSET;
	settings.battle_length = XVT_LAUNCH_SETTING_UNSET;
	settings.mission_time_limit = XVT_LAUNCH_SETTING_UNSET;
	settings.continue_sequence = XVT_LAUNCH_SETTING_UNSET;
	settings.sith = 2;
	/* Unset settings leave every option, and the Sith setting is not an
	 * option. */
	memset(&g_game, 0x77, sizeof(g_game));
	g_before = g_game;
	xvt_launch_options_apply_settings(&g_game, &settings);
	XVT_ASSERT_INT_EQ(memcmp(&g_game, &g_before, sizeof(g_game)), 0);
	/* Each set one lands in its own option and nowhere else. */
	settings.difficulty = 2;
	settings.collisions = 0;
	settings.craft_jumping = 1;
	settings.random_setup = 0;
	settings.battle_length = 2;
	settings.mission_time_limit = 15;
	settings.continue_sequence = 0;
	xvt_launch_options_apply_settings(&g_game, &settings);
	XVT_ASSERT_INT_EQ(g_game.difficulty, 2);
	XVT_ASSERT_INT_EQ(g_game.collisions, 0);
	XVT_ASSERT_INT_EQ(g_game.craft_jumping, 1);
	XVT_ASSERT_INT_EQ(g_game.random_setup, 0);
	XVT_ASSERT_INT_EQ(g_game.battle_length_index, 2);
	XVT_ASSERT_INT_EQ(g_game.mission_time_limit, 15);
	XVT_ASSERT_INT_EQ(g_game.continue_battle_or_campaign, 0);
	g_before.difficulty = 2;
	g_before.collisions = 0;
	g_before.craft_jumping = 1;
	g_before.random_setup = 0;
	g_before.battle_length_index = 2;
	g_before.mission_time_limit = 15;
	g_before.continue_battle_or_campaign = 0;
	XVT_ASSERT_INT_EQ(memcmp(&g_game, &g_before, sizeof(g_game)), 0);
}

/* Runs the type rules for directory on difficulty and random setup with the
 * sequence restarting; checks the sequence then continues and the two
 * options come out as expected. */
static void rules(int directory, int difficulty, int random_setup,
		  int want_difficulty, int want_random_setup)
{
	memset(&g_game, 0x77, sizeof(g_game));
	g_game.difficulty = (game_difficulty)difficulty;
	g_game.random_setup = (uint8_t)random_setup;
	g_game.continue_battle_or_campaign = 0;
	g_before = g_game;
	xvt_launch_options_type_rules(&g_game, directory);
	XVT_ASSERT_INT_EQ(g_game.continue_battle_or_campaign, 1);
	XVT_ASSERT_INT_EQ(g_game.difficulty, want_difficulty);
	XVT_ASSERT_INT_EQ(g_game.random_setup, want_random_setup);
	g_before.continue_battle_or_campaign = 1;
	g_before.difficulty = (game_difficulty)want_difficulty;
	g_before.random_setup = (uint8_t)want_random_setup;
	XVT_ASSERT_INT_EQ(memcmp(&g_game, &g_before, sizeof(g_game)), 0);
}

static void check_type_rules(void)
{
	/* Easy cheat becomes easy outside campaigns and stays in them. */
	rules(1, 3, 0, 0, 0);
	rules(4, 3, 0, 0, 0);
	rules(5, 3, 0, 3, 0);
	/* Other difficulties stay. */
	rules(1, 2, 0, 2, 0);
	/* Random setup 2 becomes 0 outside battles and stays in them. */
	rules(1, 0, 2, 0, 0);
	rules(3, 0, 2, 0, 0);
	rules(4, 0, 2, 0, 2);
	/* Any random setup becomes 0 in campaigns; 1 stays elsewhere. */
	rules(5, 0, 1, 0, 0);
	rules(1, 0, 1, 0, 1);
}

static void check_network_from_shipped(void)
{
	struct xvt_launch_description description;
	read_network(&description, "");
	make_shipped();
	memset(&g_game, 0x77, sizeof(g_game));
	g_before = g_game;
	xvt_launch_options_network(&g_game, &g_shipped, &description);
	/* Every option the description leaves out is the shipped one, ... */
	XVT_ASSERT_INT_EQ(g_game.difficulty, 1);
	XVT_ASSERT_INT_EQ(g_game.collisions, 11);
	XVT_ASSERT_INT_EQ(g_game.craft_jumping, 12);
	XVT_ASSERT_INT_EQ(g_game.random_setup, 13);
	XVT_ASSERT_INT_EQ(g_game.battle_length_index, 14);
	XVT_ASSERT_INT_EQ(g_game.in_progress_join, 15);
	XVT_ASSERT_INT_EQ(g_game.craft_selection, 16);
	XVT_ASSERT_INT_EQ(g_game.locate_players, 17);
	XVT_ASSERT_INT_EQ(g_game.craft_waves, 18);
	XVT_ASSERT_INT_EQ(g_game.mission_time_limit, 19);
	XVT_ASSERT_INT_EQ(g_game.last_team_time_limit_minutes, 20);
	XVT_ASSERT_INT_EQ(g_game.internet_play, 21);
	XVT_ASSERT_INT_EQ(g_game.ai_opponents, 22);
	XVT_ASSERT_INT_EQ(g_game.server_update_rate, 23);
	XVT_ASSERT_INT_EQ(g_game.combat_balance, 24);
	/* ... the sequence continues by the type rules though the shipped one
	 * restarts, and the seed is the description's, not the shipped one. */
	XVT_ASSERT_INT_EQ(g_game.continue_battle_or_campaign, 1);
	XVT_ASSERT_INT_EQ(g_game.random_seed, 7u);
	/* Every other field is the player's own. */
	struct game_config expected = g_before;
	expected.difficulty = 1;
	expected.collisions = 11;
	expected.craft_jumping = 12;
	expected.random_setup = 13;
	expected.battle_length_index = 14;
	expected.in_progress_join = 15;
	expected.craft_selection = 16;
	expected.locate_players = 17;
	expected.craft_waves = 18;
	expected.mission_time_limit = 19;
	expected.last_team_time_limit_minutes = 20;
	expected.internet_play = 21;
	expected.ai_opponents = 22;
	expected.server_update_rate = 23;
	expected.combat_balance = 24;
	expected.continue_battle_or_campaign = 1;
	expected.random_seed = 7;
	XVT_ASSERT_INT_EQ(memcmp(&g_game, &expected, sizeof(g_game)), 0);
}

static void check_network_settings_and_rules(void)
{
	struct xvt_launch_description description;
	/* The description's settings replace the shipped ones, after the type
	 * rules: its restart stands though the rules continue the sequence. */
	read_network(
		&description,
		"\"settings\":{\"difficulty\":\"hard\",\"collisions\":false,"
		"\"battle_length\":3,\"continue_sequence\":\"restart\"},");
	make_shipped();
	memset(&g_game, 0x77, sizeof(g_game));
	xvt_launch_options_network(&g_game, &g_shipped, &description);
	XVT_ASSERT_INT_EQ(g_game.difficulty, 2);
	XVT_ASSERT_INT_EQ(g_game.collisions, 0);
	XVT_ASSERT_INT_EQ(g_game.battle_length_index, 1);
	XVT_ASSERT_INT_EQ(g_game.craft_jumping, 12);
	XVT_ASSERT_INT_EQ(g_game.continue_battle_or_campaign, 0);
	/* A melee: shipped easy cheat becomes easy, shipped random setup 2
	 * becomes 0. */
	read_network(&description, "");
	g_shipped.difficulty = 3;
	g_shipped.random_setup = 2;
	xvt_launch_options_network(&g_game, &g_shipped, &description);
	XVT_ASSERT_INT_EQ(g_game.difficulty, 0);
	XVT_ASSERT_INT_EQ(g_game.random_setup, 0);
	/* Two machines whose players chose differently end alike. */
	static struct game_config other;
	memset(&other, 0x11, sizeof(other));
	xvt_launch_options_network(&other, &g_shipped, &description);
	XVT_ASSERT_INT_EQ(other.difficulty, g_game.difficulty);
	XVT_ASSERT_INT_EQ(other.random_setup, g_game.random_setup);
	XVT_ASSERT_INT_EQ(other.craft_waves, g_game.craft_waves);
	XVT_ASSERT_INT_EQ(other.random_seed, g_game.random_seed);
}

int main(void)
{
	check_apply_settings();
	check_type_rules();
	check_network_from_shipped();
	check_network_settings_and_rules();
	return 0;
}

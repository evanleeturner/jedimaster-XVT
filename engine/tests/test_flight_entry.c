/* Checks the flight entry steps in flight_entry.c against their promises in
 * xvt_runtime/runtime/flight_internal.h, for the parts that need no game session or display: Prepare's
 * refusals, the launch options it reads, the config it loads and the way it splits the command, and that
 * Cleanup hands rendering back to the frontend. Prepare loads the config, so each check that calls it with a
 * command works in a fresh temporary folder holding the shipped defaults, with the settings loaded from it
 * (config_fixture.h); the commands have fewer than 7 arguments, so Prepare returns 0 before it would open
 * the game session. No game data is read.
 *
 * Not checked here: a command of 7 arguments opens the game session, and CreateDevices opens the flight
 * display, DirectInput and the sound engine; they need the network session's peer and a window. */
#define _XOPEN_SOURCE 700
#include <string.h>

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/runtime/flight_internal.h"

static char g_command[512];

/* Runs Prepare on a writable copy of text, which must have fewer than 7 arguments. */
static void prepare(const char *text)
{
	XVT_ASSERT_TRUE(strlen(text) < sizeof g_command);
	strcpy(g_command, text);
	XVT_ASSERT_INT_EQ(xvt_flight_entry_prepare(g_command), 0);
}

static void set_all_options(int value)
{
	g_flight_conf_train_course = value;
	g_flight_conf_no_pilot = value;
	g_flight_conf_direct_input = value;
	g_flight_conf_sfx_enabled = (uint8_t)value;
	g_flight_conf_music_enabled = (uint8_t)value;
	g_flight_conf_voice_enabled = (uint8_t)value;
	g_flight_conf_tick_counter_enabled = (uint8_t)value;
	g_mipmapping_enabled = value;
	g_flight_in_progress_launch = value;
	g_flight_conf_new_net = value;
	g_flight_conf_no_launcher = value;
	g_flight_fullscreen = value;
	g_flight_page_flip = value;
}

static void check_null_command(void)
{
	XVT_ASSERT_INT_EQ(xvt_flight_entry_prepare(NULL), 0);
}

static void check_options_off(void)
{
	/* Every "no" form, and the plain options, inside one quoted name. */
	fixture_begin();
	fixture_load();
	set_all_options(1);
	g_flight_conf_train_course = 0;
	g_flight_conf_no_pilot = 0;
	g_flight_in_progress_launch = 0;
	g_flight_conf_new_net = 0;
	g_flight_conf_no_launcher = 0;
	prepare("mission ~traincourse nopilot nodinput nosfx nomusic novoice notickcounter nomipmaps inprogress "
		"newnet nolauncher nofullscreen nopageflip~");
	XVT_ASSERT_INT_EQ(g_flight_conf_train_course, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_no_pilot, 1);
	XVT_ASSERT_INT_EQ(g_flight_in_progress_launch, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_new_net, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_no_launcher, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_direct_input, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_sfx_enabled, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_music_enabled, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_voice_enabled, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_tick_counter_enabled, 0);
	XVT_ASSERT_INT_EQ(g_mipmapping_enabled, 0);
	XVT_ASSERT_INT_EQ(g_flight_fullscreen, 0);
	XVT_ASSERT_INT_EQ(g_flight_page_flip, 0);
	fixture_end();
}

static void check_options_on(void)
{
	/* The plain forms of the [no] options turn them on; plain options that are absent are off. */
	fixture_begin();
	fixture_load();
	set_all_options(0);
	g_flight_conf_train_course = 1;
	g_flight_conf_no_pilot = 1;
	g_flight_in_progress_launch = 1;
	g_flight_conf_new_net = 1;
	g_flight_conf_no_launcher = 1;
	prepare("mission ~dinput sfx music voice tickcounter mipmaps~");
	XVT_ASSERT_INT_EQ(g_flight_conf_direct_input, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_sfx_enabled, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_music_enabled, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_voice_enabled, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_tick_counter_enabled, 1);
	XVT_ASSERT_INT_EQ(g_mipmapping_enabled, 1);
	XVT_ASSERT_INT_EQ(g_flight_conf_train_course, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_no_pilot, 0);
	XVT_ASSERT_INT_EQ(g_flight_in_progress_launch, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_new_net, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_no_launcher, 0);
	set_all_options(0);
	prepare("mission fullscreen pageflip");
	XVT_ASSERT_INT_EQ(g_flight_fullscreen, 1);
	XVT_ASSERT_INT_EQ(g_flight_page_flip, 1);
	fixture_end();
}

static void check_options_anywhere(void)
{
	/* An option is found as a substring anywhere: inside a file name, and run together with another. */
	fixture_begin();
	fixture_load();
	set_all_options(1);
	prepare("missions/xnosfxy.tie novoicenomusic");
	XVT_ASSERT_INT_EQ(g_flight_conf_sfx_enabled, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_voice_enabled, 0);
	XVT_ASSERT_INT_EQ(g_flight_conf_music_enabled, 0);
	fixture_end();
}

static void check_split(void)
{
	/* Split in place at spaces, with ~ quoting one argument; six arguments are too few. */
	fixture_begin();
	fixture_load();
	prepare("first second ~third word~ fourth fifth sixth");
	const char *expected[] = {"first",  "second", "third word",
				  "fourth", "fifth",  "sixth"};
	for (int i = 0; i < 6; ++i) {
		const char *argument = g_flight_launch_args.arguments[i];
		XVT_ASSERT_TRUE(argument >= g_command &&
				argument < g_command + sizeof g_command);
		XVT_ASSERT_INT_EQ(strcmp(argument, expected[i]), 0);
	}
	fixture_end();
}

static void check_loads_config(void)
{
	/* Prepare loads the config: the game config is what the config load makes of the settings. */
	static struct game_config loaded;
	fixture_begin();
	fixture_load();
	memset(&g_game_config, 0xAB, sizeof g_game_config);
	prepare("mission");
	memcpy(&loaded, &g_game_config, sizeof loaded);
	memset(&g_game_config, 0xCD, sizeof g_game_config);
	config_load();
	XVT_ASSERT_INT_EQ(memcmp(&loaded, &g_game_config, sizeof loaded), 0);
	fixture_end();
}

static void check_cleanup(void)
{
	/* With nothing started, Cleanup still hands rendering back to the frontend. */
	fixture_begin();
	fixture_load();
	prepare("mission");
	g_flight_render_to_frontend = 0;
	xvt_flight_entry_cleanup();
	XVT_ASSERT_INT_EQ(g_flight_render_to_frontend, 1);
	fixture_end();
}

int main(void)
{
	check_null_command();
	check_options_off();
	check_options_on();
	check_options_anywhere();
	check_split();
	check_loads_config();
	check_cleanup();
	return 0;
}

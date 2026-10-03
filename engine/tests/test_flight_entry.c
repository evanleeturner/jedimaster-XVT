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
#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/runtime/flight_internal.h"

#include <string.h>

static char g_command[512];

/* Runs Prepare on a writable copy of text, which must have fewer than 7 arguments. */
static void Prepare(const char *text)
{
	XVT_ASSERT_TRUE(strlen(text) < sizeof g_command);
	strcpy(g_command, text);
	XVT_ASSERT_INT_EQ(XvtFlightEntry_Prepare(g_command), 0);
}

static void SetAllOptions(int value)
{
	g_flightConfTrainCourse = value;
	g_flightConfNoPilot = value;
	g_flightConfDirectInput = value;
	g_flightConfSfxEnabled = (uint8_t)value;
	g_flightConfMusicEnabled = (uint8_t)value;
	g_flightConfVoiceEnabled = (uint8_t)value;
	g_flightConfTickCounterEnabled = (uint8_t)value;
	g_mipmappingEnabled = value;
	g_flightInProgressLaunch = value;
	g_flightConfNewNet = value;
	g_flightConfNoLauncher = value;
	g_flightFullscreen = value;
	g_flightPageFlip = value;
}

static void CheckNullCommand(void)
{
	XVT_ASSERT_INT_EQ(XvtFlightEntry_Prepare(NULL), 0);
}

static void CheckOptionsOff(void)
{
	/* Every "no" form, and the plain options, inside one quoted name. */
	Fixture_Begin();
	Fixture_Load();
	SetAllOptions(1);
	g_flightConfTrainCourse = g_flightConfNoPilot =
		g_flightInProgressLaunch = 0;
	g_flightConfNewNet = g_flightConfNoLauncher = 0;
	Prepare("mission ~traincourse nopilot nodinput nosfx nomusic novoice notickcounter nomipmaps inprogress "
		"newnet nolauncher nofullscreen nopageflip~");
	XVT_ASSERT_INT_EQ(g_flightConfTrainCourse, 1);
	XVT_ASSERT_INT_EQ(g_flightConfNoPilot, 1);
	XVT_ASSERT_INT_EQ(g_flightInProgressLaunch, 1);
	XVT_ASSERT_INT_EQ(g_flightConfNewNet, 1);
	XVT_ASSERT_INT_EQ(g_flightConfNoLauncher, 1);
	XVT_ASSERT_INT_EQ(g_flightConfDirectInput, 0);
	XVT_ASSERT_INT_EQ(g_flightConfSfxEnabled, 0);
	XVT_ASSERT_INT_EQ(g_flightConfMusicEnabled, 0);
	XVT_ASSERT_INT_EQ(g_flightConfVoiceEnabled, 0);
	XVT_ASSERT_INT_EQ(g_flightConfTickCounterEnabled, 0);
	XVT_ASSERT_INT_EQ(g_mipmappingEnabled, 0);
	XVT_ASSERT_INT_EQ(g_flightFullscreen, 0);
	XVT_ASSERT_INT_EQ(g_flightPageFlip, 0);
	Fixture_End();
}

static void CheckOptionsOn(void)
{
	/* The plain forms of the [no] options turn them on; plain options that are absent are off. */
	Fixture_Begin();
	Fixture_Load();
	SetAllOptions(0);
	g_flightConfTrainCourse = g_flightConfNoPilot =
		g_flightInProgressLaunch = 1;
	g_flightConfNewNet = g_flightConfNoLauncher = 1;
	Prepare("mission ~dinput sfx music voice tickcounter mipmaps~");
	XVT_ASSERT_INT_EQ(g_flightConfDirectInput, 1);
	XVT_ASSERT_INT_EQ(g_flightConfSfxEnabled, 1);
	XVT_ASSERT_INT_EQ(g_flightConfMusicEnabled, 1);
	XVT_ASSERT_INT_EQ(g_flightConfVoiceEnabled, 1);
	XVT_ASSERT_INT_EQ(g_flightConfTickCounterEnabled, 1);
	XVT_ASSERT_INT_EQ(g_mipmappingEnabled, 1);
	XVT_ASSERT_INT_EQ(g_flightConfTrainCourse, 0);
	XVT_ASSERT_INT_EQ(g_flightConfNoPilot, 0);
	XVT_ASSERT_INT_EQ(g_flightInProgressLaunch, 0);
	XVT_ASSERT_INT_EQ(g_flightConfNewNet, 0);
	XVT_ASSERT_INT_EQ(g_flightConfNoLauncher, 0);
	SetAllOptions(0);
	Prepare("mission fullscreen pageflip");
	XVT_ASSERT_INT_EQ(g_flightFullscreen, 1);
	XVT_ASSERT_INT_EQ(g_flightPageFlip, 1);
	Fixture_End();
}

static void CheckOptionsAnywhere(void)
{
	/* An option is found as a substring anywhere: inside a file name, and run together with another. */
	Fixture_Begin();
	Fixture_Load();
	SetAllOptions(1);
	Prepare("missions/xnosfxy.tie novoicenomusic");
	XVT_ASSERT_INT_EQ(g_flightConfSfxEnabled, 0);
	XVT_ASSERT_INT_EQ(g_flightConfVoiceEnabled, 0);
	XVT_ASSERT_INT_EQ(g_flightConfMusicEnabled, 0);
	Fixture_End();
}

static void CheckSplit(void)
{
	/* Split in place at spaces, with ~ quoting one argument; six arguments are too few. */
	Fixture_Begin();
	Fixture_Load();
	Prepare("first second ~third word~ fourth fifth sixth");
	const char *expected[] = {"first",  "second", "third word",
				  "fourth", "fifth",  "sixth"};
	for (int i = 0; i < 6; ++i) {
		const char *argument = g_flightLaunchArgs.arguments[i];
		XVT_ASSERT_TRUE(argument >= g_command &&
				argument < g_command + sizeof g_command);
		XVT_ASSERT_INT_EQ(strcmp(argument, expected[i]), 0);
	}
	Fixture_End();
}

static void CheckLoadsConfig(void)
{
	/* Prepare loads the config: the game config is what the config load makes of the settings. */
	static GameConfig loaded;
	Fixture_Begin();
	Fixture_Load();
	memset(&g_gameConfig, 0xAB, sizeof g_gameConfig);
	Prepare("mission");
	memcpy(&loaded, &g_gameConfig, sizeof loaded);
	memset(&g_gameConfig, 0xCD, sizeof g_gameConfig);
	Config_Load();
	XVT_ASSERT_INT_EQ(memcmp(&loaded, &g_gameConfig, sizeof loaded), 0);
	Fixture_End();
}

static void CheckCleanup(void)
{
	/* With nothing started, Cleanup still hands rendering back to the frontend. */
	Fixture_Begin();
	Fixture_Load();
	Prepare("mission");
	g_flightRenderToFrontend = 0;
	XvtFlightEntry_Cleanup();
	XVT_ASSERT_INT_EQ(g_flightRenderToFrontend, 1);
	Fixture_End();
}

int main(void)
{
	CheckNullCommand();
	CheckOptionsOff();
	CheckOptionsOn();
	CheckOptionsAnywhere();
	CheckSplit();
	CheckLoadsConfig();
	CheckCleanup();
	return 0;
}

/* Checks the network session (xvt_runtime/runtime/network_session.h) against
 * the promises in its header, as far as they hold without a peer or a
 * directory: the player-name split, the refusals of Configure and of the Begin
 * calls, the status a setup reports, the lost mark, a close and its completion,
 * Reset and Shutdown, the door's directory standing in for the settings'
 * one, and whether the door's session is being set up. Only one setup runs a
 * step past its first phase: the last case's offline host for the door, whose
 * whole setup runs in this process on DirectPlay's own session, with no peer
 * and no directory request. Every case starts from Shutdown with no settings
 * loaded.
 *
 * Not checked here: the other setup phases past the close (a join's and an
 * online host's), admission, registration, the listing Service keeps and the
 * flight marks; they need a DirectPlay peer and a multiplayer directory. So the
 * door's setup is not seen ending at a joiner's admission. */
#include <string.h>

#include "test_assert.h"
#include "xvt/net/net_reliable.h"
#include "xvt_runtime/runtime/network_session.h"

static const GUID g_room = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};

static struct net_player_name_message g_message;

static xvt_network_session_state state(void)
{
	return xvt_network_session_get_status().state;
}

static AeronDplayDirectoryError network_session_error(void)
{
	return xvt_network_session_get_status().error;
}

static void fresh(void)
{
	xvt_network_session_shutdown();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_IDLE);
}

/* Completes the close every Begin starts with; no DirectPlay session is open in these tests. */
static void finish_close(void) { xvt_network_session_service(); }

static void check_copy_player_names(void)
{
	memset(&g_message, 0, sizeof g_message);
	memcpy(g_message.names, "Luke\0Skywalker", sizeof "Luke\0Skywalker");
	char short_name[16];
	char long_name[16];
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, sizeof short_name,
				  long_name, sizeof long_name),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(short_name, "Luke"), 0);
	XVT_ASSERT_INT_EQ(strcmp(long_name, "Skywalker"), 0);

	/* Truncated to fit, each terminated. */
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 3, long_name, 4),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(short_name, "Lu"), 0);
	XVT_ASSERT_INT_EQ(strcmp(long_name, "Sky"), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 1, long_name, 1),
			  1);
	XVT_ASSERT_INT_EQ(short_name[0], 0);
	XVT_ASSERT_INT_EQ(long_name[0], 0);

	/* NULL and zero-capacity arguments. */
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  NULL, short_name, 16, long_name, 16),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, NULL, 16, long_name, 16),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 16, NULL, 16),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 0, long_name, 16),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 16, long_name, 0),
			  0);

	/* A long name without its terminator, then no terminator at all. */
	memset(g_message.names, 'x', sizeof g_message.names);
	g_message.names[1] = 0;
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 16, long_name, 16),
			  0);
	memset(g_message.names, 'x', sizeof g_message.names);
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 16, long_name, 16),
			  0);

	/* Both terminators at the very end: an empty long name in the last byte. */
	memset(g_message.names, 'x', sizeof g_message.names);
	g_message.names[sizeof g_message.names - 2] = 0;
	g_message.names[sizeof g_message.names - 1] = 0;
	XVT_ASSERT_INT_EQ(xvt_network_session_copy_player_names(
				  &g_message, short_name, 16, long_name, 16),
			  1);
	XVT_ASSERT_INT_EQ(strlen(short_name), 15);
	XVT_ASSERT_INT_EQ(long_name[0], 0);
}

static void check_configure_needs_a_lobby(void)
{
	fresh();
	/* With no settings loaded the lobby URL is empty. */
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
}

static void check_door_directory(void)
{
	fresh();
	/* The door's address replaces the empty one of the settings: a plain
	 * http address reaches the directory's own check and is refused
	 * there. */
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door("http://plain.invalid",
						       "", &g_room),
			  1);
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);

	/* An address the directory cannot hold, or none, changes nothing. */
	char too_long[AERON_DPLAY_DIRECTORY_URL_CAPACITY + 1];
	memset(too_long, 'h', sizeof too_long);
	memcpy(too_long, "https://", 8);
	too_long[AERON_DPLAY_DIRECTORY_URL_CAPACITY] = 0;
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door(too_long, "", &g_room),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door(NULL, "", &g_room), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);

	/* No id goes back to the settings, as Shutdown does. */
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door(NULL, NULL, NULL), 1);
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door("http://plain.invalid",
						       "", &g_room),
			  1);
	xvt_network_session_shutdown();
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
}

/* The door's key pin reaches the directory's own check. A good pin and no pin
 * both configure a bare https address; a pin of any other form is refused
 * there, even under the address and id just configured, so a changed pin is
 * never answered from what was remembered. A pin longer than the directory
 * takes changes nothing. Configuring sends no request. */
static void check_door_pin(void)
{
	static const char good[] =
		"sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door("https://lobby.invalid",
						       good, &g_room),
			  1);
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(
		xvt_network_session_use_door("https://lobby.invalid",
					     "sha256//short=", &g_room),
		1);
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door("https://lobby.invalid",
						       NULL, &g_room),
			  1);
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);

	char too_long[AERON_DPLAY_DIRECTORY_PIN_CAPACITY + 1];
	memset(too_long, 'A', sizeof too_long);
	too_long[AERON_DPLAY_DIRECTORY_PIN_CAPACITY] = 0;
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door("https://other.invalid",
						       too_long, &g_room),
			  0);
	/* Nothing changed: the address, pin and id configured last stand. */
	XVT_ASSERT_INT_EQ(xvt_network_session_configure(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);
	xvt_network_session_shutdown();
}

static void check_begin_refusals(void)
{
	char too_long[64];
	memset(too_long, 'n', sizeof too_long);
	too_long[63] = 0;

	struct {
		const char *info;
		const char *player;
		const char *name;
	} hosts[] = {
		{NULL, "Luke", ""},
		{"\x02", NULL, ""},
		{"\x02", "Luke", NULL},
		{"0123456789abcdef", "Luke", ""},
		{"\x02", "0123456789abcdef", ""},
		{"\x02", "Luke", too_long},
	};

	for (unsigned i = 0; i < sizeof hosts / sizeof hosts[0]; ++i) {
		fresh();
		XVT_ASSERT_INT_EQ(xvt_network_session_begin_host(
					  hosts[i].info, hosts[i].player,
					  hosts[i].name, 0),
				  0);
		XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_FAILED);
		XVT_ASSERT_INT_EQ(network_session_error(),
				  AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	}

	/* A join needs a room as well. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_join("\x02", "Luke", NULL),
			  0);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_FAILED);
	XVT_ASSERT_INT_EQ(network_session_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_join("0123456789abcdef",
							 "Luke", &g_room),
			  0);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_FAILED);

	/* The longest arguments that fit are accepted: 15, 15 and 31 characters. */
	fresh();
	too_long[31] = 0;
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("0123456789abcde",
							 "0123456789abcde",
							 too_long, 0),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
}

static void check_begin_while_under_way(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);

	/* Another Begin while setup is under way returns -1 and does nothing:
	 * bad arguments do not fail it. */
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host(NULL, NULL, NULL, 0),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_join(NULL, NULL, NULL),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(network_session_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);

	/* A failed session can be started again. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_join("\x02", "Luke", NULL),
			  0);
	XVT_ASSERT_INT_EQ(
		xvt_network_session_begin_join("\x02", "Luke", &g_room),
		XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
}

static void check_tick(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), 0);

	/* A failed session ticks as 0. */
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host(NULL, "Luke", "", 0),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), 0);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_FAILED);

	/* While the previous session's close completes, setup is pending. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
}

static void check_lost(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
	xvt_network_session_host_lost();
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 1);
	xvt_network_session_host_lost();
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 1);

	/* Outside flight, the next Update fails a lost session. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	xvt_network_session_host_lost();
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), 0);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_FAILED);

	/* ...and so does the next Service. */
	fresh();
	XVT_ASSERT_INT_EQ(
		xvt_network_session_begin_join("\x02", "Luke", &g_room),
		XVT_NETWORK_PENDING);
	finish_close();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
	xvt_network_session_host_lost();
	xvt_network_session_service();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_FAILED);
}

static void check_close(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	xvt_network_session_host_lost();

	/* OnClose returns to idle with a close pending, and clears the lost mark. */
	xvt_network_session_on_close();
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), 0);

	/* Service finishes the close once DirectPlay is inactive. */
	finish_close();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_IDLE);

	/* Leave and Cancel shut the DirectPlay session down, which closes the session. */
	xvt_network_session_leave();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
	finish_close();
	xvt_network_session_cancel();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
	finish_close();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_IDLE);
}

static void check_leave_keeps_shutdown_clears(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host(NULL, "Luke", "", 0),
			  0);
	XVT_ASSERT_INT_EQ(network_session_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);

	/* Leave keeps the session state: the last failure's error stays. */
	xvt_network_session_leave();
	finish_close();
	XVT_ASSERT_INT_EQ(network_session_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);

	/* Shutdown forgets it. */
	xvt_network_session_host_lost();
	xvt_network_session_shutdown();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_IDLE);
	XVT_ASSERT_INT_EQ(network_session_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(xvt_network_session_is_lost(), 0);
}

static void check_admission_outside_admission(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_accept_admission(0, 0), 0);
	XVT_ASSERT_INT_EQ(
		xvt_network_session_begin_join("\x02", "Luke", &g_room),
		XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(xvt_network_session_accept_admission(0, 0), 0);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
}

static int door_is_opening(void)
{
	return xvt_network_session_door_is_opening();
}

static void check_door_is_opening(void)
{
	/* Without the door no setup counts, from the close to the next phase. */
	fresh();
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
	finish_close();
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);

	/* With the door: not while idle; from the close a Begin starts with,
	 * through its completion, into the next phase. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door("http://plain.invalid",
						       "", &g_room),
			  1);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(door_is_opening(), 1);
	finish_close();
	XVT_ASSERT_INT_EQ(door_is_opening(), 1);
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(door_is_opening(), 1);

	/* A close ends it: idle with the close pending. */
	xvt_network_session_on_close();
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
	finish_close();
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);

	/* So does a failure: a join the directory refuses the door's plain
	 * http address, and a Begin with a missing argument. */
	XVT_ASSERT_INT_EQ(
		xvt_network_session_begin_join("\x02", "Luke", &g_room),
		XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(door_is_opening(), 1);
	finish_close();
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), 0);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_FAILED);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host(NULL, "Luke", "", 0),
			  0);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);

	/* Shutdown forgets the door. */
	xvt_network_session_shutdown();
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
}

/* Runs last: it leaves DirectPlay created. */
static void check_door_host_setup_to_established(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_session_use_door("http://plain.invalid",
						       "", &g_room),
			  1);
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host("\x02", "Luke", "", 0),
			  XVT_NETWORK_PENDING);
	finish_close();
	/* The close, creating DirectPlay, opening, the player and the group
	 * each move to the next phase, which is still setup. */
	for (int step = 0; step < 5; ++step) {
		XVT_ASSERT_INT_EQ(xvt_network_session_update(),
				  XVT_NETWORK_PENDING);
		XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_PENDING);
		XVT_ASSERT_INT_EQ(door_is_opening(), 1);
	}
	/* The roster establishes the offline host, and the setup is over. */
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(state(), XVT_NETWORK_SESSION_ESTABLISHED);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_update(), 1);
	XVT_ASSERT_INT_EQ(door_is_opening(), 0);
}

int main(void)
{
	check_copy_player_names();
	check_configure_needs_a_lobby();
	check_door_directory();
	check_door_pin();
	check_begin_refusals();
	check_begin_while_under_way();
	check_tick();
	check_lost();
	check_close();
	check_leave_keeps_shutdown_clears();
	check_admission_outside_admission();
	check_door_is_opening();
	check_door_host_setup_to_established();
	xvt_network_session_shutdown();
	return 0;
}

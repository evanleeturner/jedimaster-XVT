/* Checks the filter that decides which of libjuice's own log messages reach
 * the log (aeron/src/compat/dplay_ice_log.h). libjuice's messages can carry
 * the session's ICE credentials, SDP and addresses; the filter passes only its
 * fixed reasons for a failing link. The refused messages below are libjuice's
 * own formats with values filled in, the ones that carry a secret first. */
#include <stddef.h>
#include <string.h>

#include "compat/dplay_ice_log.h"
#include "test_assert.h"

/* Each fixed reason passes whole, and DpIce_LogText returns the message
 * itself. */
static void fixed_reasons_pass(void)
{
	static const char *const reasons[] = {
		"Lost connectivity",
		"Connectivity timer expired",
		"Sending keepalive failed",
		"TURN allocation failed",
		"STUN server binding failed",
		"STUN entry 3: Consent expired for candidate pair",
		"STUN entry 12: Consent expired for candidate pair",
	};
	for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i) {
		XVT_ASSERT_TRUE(DpIce_LogText(reasons[i]) == reasons[i]);
	}
}

/* A debug build's "file.c:line: " prefix is skipped, and the text after it
 * is what passes. */
static void debug_prefix_is_skipped(void)
{
	const char *message = "agent.c:1126: Lost connectivity";
	const char *text = DpIce_LogText(message);
	XVT_ASSERT_TRUE(text != NULL);
	XVT_ASSERT_TRUE(strcmp(text, "Lost connectivity") == 0);
	message =
		"agent.c:1006: STUN entry 0: Consent expired for candidate pair";
	text = DpIce_LogText(message);
	XVT_ASSERT_TRUE(text != NULL);
	XVT_ASSERT_TRUE(strncmp(text, "STUN entry 0:", 13) == 0);
}

/* Messages that carry a credential, a name, SDP or an address never pass,
 * with or without the debug prefix. */
static void secrets_never_pass(void)
{
	static const char *const refused[] = {
		"STUN integrity check failed, password=\"hunter2\"",
		"agent.c:1309: STUN integrity check failed, password=\"hunter2\"",
		"STUN username invalid, username=\"ab12:cd34\"",
		"STUN local ufrag check failed, expected=\"ab12\", actual=\"cd34\"",
		"No credentials for username \"player\"",
		"Failed to parse remote SDP candidate: a=candidate:1 1 UDP 1 192.0.2.7 4000 typ host",
		"Received unexpected non-STUN datagram from 192.0.2.7:4000, ignoring",
		"Address resolution failed for relay.example:3478",
		"Changing state to failed",
	};
	for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
		XVT_ASSERT_TRUE(DpIce_LogText(refused[i]) == NULL);
	}
}

/* A fixed reason with anything added, or a malformed prefix, does not pass. */
static void near_misses_are_refused(void)
{
	static const char *const refused[] = {
		"Lost connectivity, password=\"hunter2\"",
		"Lost connectivity ",
		"lost connectivity",
		"STUN entry : Consent expired for candidate pair",
		"STUN entry 3: Consent expired for candidate pair 192.0.2.7",
		"STUN entry 3x: Consent expired for candidate pair",
		"agent.c:: Lost connectivity",
		"agent.c:12:Lost connectivity",
		".c:12: Lost connectivity",
		"",
	};
	for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
		XVT_ASSERT_TRUE(DpIce_LogText(refused[i]) == NULL);
	}
	XVT_ASSERT_TRUE(DpIce_LogText(NULL) == NULL);
}

int main(void)
{
	fixed_reasons_pass();
	debug_prefix_is_skipped();
	secrets_never_pass();
	near_misses_are_refused();
	return 0;
}

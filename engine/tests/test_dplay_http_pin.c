/* Checks the lobby pin of aeron/src/compat/dplay_http.c: the one form
 * DpHttp_Pin accepts, DpHttp_Create refusing any other, and, given a live
 * lobby, that the system's curl trusts the pinned key and nothing else.
 *
 * Without arguments it runs the form checks only. Given "PORT PIN GAME", the
 * port, the printed fingerprint and one game id of a "jedimaster matchmaker"
 * serving on 127.0.0.1 (tests/pin_check.py starts one), it also lists that
 * game's rooms three ways: with the printed pin (answered, status 200), with
 * the pin of another key (refused before any answer), and with no pin
 * (refused: no authority signed the matchmaker's own certificate). The
 * refusals count only because the pinned request reached the same lobby
 * first. It prints the curl version and TLS library it ran with. */
#ifndef _WIN32
/* nanosleep under -std=c99. */
#define _POSIX_C_SOURCE 200809L
#endif
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/time.h"
#include "compat/dplay_http.h"
#include "test_assert.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

enum { PIN_DIGITS = 43 };

static void pause_ms(unsigned ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	struct timespec wait = {(time_t)(ms / 1000u),
				(long)(ms % 1000u) * 1000000L};
	nanosleep(&wait, NULL);
#endif
}

/* Writes "sha256//", PIN_DIGITS copies of digit and "=" into out. */
static void make_pin(char out[AERON_DPLAY_DIRECTORY_PIN_CAPACITY], char digit)
{
	strcpy(out, "sha256//");
	memset(out + 8, digit, PIN_DIGITS);
	out[8 + PIN_DIGITS] = '=';
	out[8 + PIN_DIGITS + 1] = 0;
}

/* The one form: "sha256//", 43 base64 digits, "=". One pin, never a list,
 * and never base64url's '-' or '_'. */
static void pin_form(void)
{
	char pin[AERON_DPLAY_DIRECTORY_PIN_CAPACITY + 8];
	make_pin(pin, 'A');
	XVT_ASSERT_INT_EQ(strlen(pin), AERON_DPLAY_DIRECTORY_PIN_CAPACITY - 1);
	XVT_ASSERT_TRUE(DpHttp_Pin(pin));
	make_pin(pin, '/');
	XVT_ASSERT_TRUE(DpHttp_Pin(pin));
	XVT_ASSERT_TRUE(DpHttp_Pin(
		"sha256//Az09+/Az09+/Az09+/Az09+/Az09+/Az09+/Az09+/A="));

	XVT_ASSERT_TRUE(!DpHttp_Pin(""));
	make_pin(pin, 'A');
	pin[8 + PIN_DIGITS] = 'A';
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin)); /* no '=' */
	make_pin(pin, 'A');
	memmove(pin + 8 + PIN_DIGITS - 1, pin + 8 + PIN_DIGITS, 2);
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin)); /* 42 digits */
	make_pin(pin, 'A');
	memmove(pin + 9, pin + 8, PIN_DIGITS + 2);
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin)); /* 44 digits */
	make_pin(pin, 'A');
	pin[0] = 'S';
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin)); /* "Sha256//" */
	make_pin(pin, 'A');
	memcpy(pin, "sha1//AA", 8);
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin));
	make_pin(pin, 'A');
	pin[20] = '-';
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin));
	make_pin(pin, 'A');
	pin[20] = '_';
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin));
	make_pin(pin, 'A');
	strcat(pin, ";");
	XVT_ASSERT_TRUE(!DpHttp_Pin(pin)); /* the start of a list */

	/* A full buffer with no end is refused without reading past it; the
	 * sanitizers watch the read. */
	char *unended = malloc(AERON_DPLAY_DIRECTORY_PIN_CAPACITY);
	XVT_ASSERT_TRUE(unended != NULL);
	make_pin(pin, 'A');
	memcpy(unended, pin, AERON_DPLAY_DIRECTORY_PIN_CAPACITY - 1);
	unended[AERON_DPLAY_DIRECTORY_PIN_CAPACITY - 1] = '=';
	XVT_ASSERT_TRUE(!DpHttp_Pin(unended));
	free(unended);
}

/* A malformed pin is refused when the client is made, so no request ever
 * runs on a check the caller did not ask for; no pin and a good pin are
 * taken. */
static void create_takes_only_a_good_pin(void)
{
	char pin[AERON_DPLAY_DIRECTORY_PIN_CAPACITY];
	XVT_ASSERT_TRUE(DpHttp_Create("https://127.0.0.1:1", "sha256//x") ==
			NULL);
	make_pin(pin, 'A');
	pin[8 + PIN_DIGITS] = 'A';
	XVT_ASSERT_TRUE(DpHttp_Create("https://127.0.0.1:1", pin) == NULL);

	DpHttp *http = DpHttp_Create("https://127.0.0.1:1", "");
	XVT_ASSERT_TRUE(http != NULL);
	DpHttp_Destroy(http);
	make_pin(pin, 'A');
	http = DpHttp_Create("https://127.0.0.1:1", pin);
	XVT_ASSERT_TRUE(http != NULL);
	DpHttp_Destroy(http);
}

/* Lists game's rooms at origin with pin ("" for none) and waits for the
 * answer; the caller frees its body. */
static DpHttpResult list_rooms(const char *origin, const char *pin,
			       const char *game)
{
	DpHttp *http = DpHttp_Create(origin, pin);
	XVT_ASSERT_TRUE(http != NULL);
	static DpHttpRequest request;
	memset(&request, 0, sizeof(request));
	request.method = DP_HTTP_GET;
	snprintf(request.path, sizeof(request.path), "/v1/apps/%s/rooms", game);
	strcpy(request.token, "t");
	request.timeout_ms = 5000;
	uint64_t id = DpHttp_Submit(http, &request);
	XVT_ASSERT_TRUE(id != 0);
	DpHttpResult result;
	memset(&result, 0, sizeof(result));
	uint64_t deadline = Aeron_NowUs() + 15u * 1000000u;
	while (!DpHttp_Take(http, &result)) {
		XVT_ASSERT_TRUE(Aeron_NowUs() < deadline);
		pause_ms(10);
	}
	XVT_ASSERT_INT_EQ(result.id, id);
	DpHttp_Destroy(http);
	return result;
}

static void live_lobby(const char *port, const char *pin, const char *game)
{
	const curl_version_info_data *curl = curl_version_info(CURLVERSION_NOW);
	printf("curl %s, TLS %s\n", curl->version,
	       curl->ssl_version ? curl->ssl_version : "none");
	XVT_ASSERT_TRUE(DpHttp_Pin(pin));
	char origin[64];
	snprintf(origin, sizeof(origin), "https://127.0.0.1:%s", port);

	DpHttpResult pinned = list_rooms(origin, pin, game);
	printf("printed pin: status %ld, error %d\n", pinned.status,
	       (int)pinned.error);
	XVT_ASSERT_INT_EQ(pinned.error, AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(pinned.status, 200);
	XVT_ASSERT_TRUE(pinned.body && strstr(pinned.body, "\"rooms\""));
	free(pinned.body);

	/* One digit changed: still the form, another key. */
	char other[AERON_DPLAY_DIRECTORY_PIN_CAPACITY];
	strcpy(other, pin);
	other[8] = other[8] == 'A' ? 'B' : 'A';
	DpHttpResult wrong = list_rooms(origin, other, game);
	printf("another key's pin: status %ld, error %d\n", wrong.status,
	       (int)wrong.error);
	XVT_ASSERT_INT_EQ(wrong.error, AERON_DPLAY_DIRECTORY_ERROR_NETWORK);
	XVT_ASSERT_INT_EQ(wrong.status, 0);
	free(wrong.body);

	DpHttpResult unpinned = list_rooms(origin, "", game);
	printf("no pin: status %ld, error %d\n", unpinned.status,
	       (int)unpinned.error);
	XVT_ASSERT_INT_EQ(unpinned.error, AERON_DPLAY_DIRECTORY_ERROR_NETWORK);
	XVT_ASSERT_INT_EQ(unpinned.status, 0);
	free(unpinned.body);
}

int main(int argc, char **argv)
{
	if (argc != 1 && argc != 4) {
		fprintf(stderr, "usage: %s [PORT PIN GAME]\n", argv[0]);
		return 2;
	}
	pin_form();
	create_takes_only_a_good_pin();
	if (argc == 4) {
		live_lobby(argv[1], argv[2], argv[3]);
	}
	printf("ok\n");
	return 0;
}

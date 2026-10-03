/* Checks the log header: the level gate evaluates no argument when a line is off; DEBUG switches on at
 * run time in a build that defines NDEBUG, as a release build does (this file refuses to compile
 * without it); level names parse in any letter case; Aeron's "category: text" shape splits into event
 * and fields; a line formats exactly as the grammar says, cut cleanly when it does not fit; the home
 * folder in a path is written as ~ only where it is a whole folder at the start of a path; and a table
 * prints as whole hex words. Aeron's log funnel is replaced by a stub that records what it was handed. */
#include "test_assert.h"
#include "xvt_runtime/log/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if !defined(NDEBUG)
#error "test_log must be compiled with NDEBUG defined, as a release build is"
#endif

static int g_calls;
static AeronLogLevel g_lastLevel;
static char g_lastCategory[64];
static char g_lastMessage[256];

void Aeron_LogMessageV(AeronLogLevel level, const char *category,
		       const char *fmt, va_list args)
{
	g_calls++;
	g_lastLevel = level;
	snprintf(g_lastCategory, sizeof g_lastCategory, "%s",
		 category ? category : "");
	vsnprintf(g_lastMessage, sizeof g_lastMessage, fmt ? fmt : "", args);
}

static int g_evaluated;

static int Count(void) { return ++g_evaluated; }

static void CheckGate(void)
{
	XVT_ASSERT_INT_EQ(XvtLog_Level(), AERON_LOG_INFO);
	XVT_LOG_DEBUG("test.off n=%d", Count());
	XVT_ASSERT_INT_EQ(g_evaluated, 0);
	XVT_ASSERT_INT_EQ(g_calls, 0);

	XVT_LOG_INFO("test.on n=%d", Count());
	XVT_ASSERT_INT_EQ(g_evaluated, 1);
	XVT_ASSERT_INT_EQ(g_calls, 1);
	XVT_ASSERT_INT_EQ(g_lastLevel, AERON_LOG_INFO);
	XVT_ASSERT_TRUE(!strcmp(g_lastCategory, "xvt"));
	XVT_ASSERT_TRUE(!strcmp(g_lastMessage, "test.on n=1"));

	XvtLog_SetLevel(AERON_LOG_DEBUG);
	XVT_ASSERT_INT_EQ(XvtLog_Level(), AERON_LOG_DEBUG);
	XVT_LOG_DEBUG("test.off n=%d", Count());
	XVT_ASSERT_INT_EQ(g_evaluated, 2);
	XVT_ASSERT_INT_EQ(g_calls, 2);
	XVT_ASSERT_INT_EQ(g_lastLevel, AERON_LOG_DEBUG);
	XVT_ASSERT_TRUE(!strcmp(g_lastMessage, "test.off n=2"));

	XvtLog_SetLevel(AERON_LOG_ERROR);
	XVT_LOG_WARN("test.warn");
	XVT_ASSERT_INT_EQ(g_calls, 2);
	XVT_LOG_ERROR("test.error");
	XVT_ASSERT_INT_EQ(g_calls, 3);
	XVT_ASSERT_INT_EQ(g_lastLevel, AERON_LOG_ERROR);
	XVT_ASSERT_TRUE(!strcmp(g_lastMessage, "test.error"));
}

static void CheckParse(void)
{
	AeronLogLevel level = AERON_LOG_CRITICAL;
	XVT_ASSERT_TRUE(XvtLog_ParseLevel("debug", &level));
	XVT_ASSERT_INT_EQ(level, AERON_LOG_DEBUG);
	XVT_ASSERT_TRUE(XvtLog_ParseLevel("INFO", &level));
	XVT_ASSERT_INT_EQ(level, AERON_LOG_INFO);
	XVT_ASSERT_TRUE(XvtLog_ParseLevel("Warn", &level));
	XVT_ASSERT_INT_EQ(level, AERON_LOG_WARN);
	XVT_ASSERT_TRUE(XvtLog_ParseLevel("error", &level));
	XVT_ASSERT_INT_EQ(level, AERON_LOG_ERROR);
	XVT_ASSERT_TRUE(!XvtLog_ParseLevel("verbose", &level));
	XVT_ASSERT_TRUE(!XvtLog_ParseLevel("warning", &level));
	XVT_ASSERT_TRUE(!XvtLog_ParseLevel("", &level));
	XVT_ASSERT_TRUE(!XvtLog_ParseLevel(NULL, &level));
	XVT_ASSERT_INT_EQ(level, AERON_LOG_ERROR);
}

static void CheckSplit(void)
{
	const char *event;
	size_t length;
	const char *fields;
	const char *message = "xvt: input.queue_full queue=mouse";
	XVT_ASSERT_TRUE(XvtLog_SplitMessage(message, &event, &length, &fields));
	XVT_ASSERT_INT_EQ(length, 16);
	XVT_ASSERT_TRUE(!strncmp(event, "input.queue_full", 16));
	XVT_ASSERT_TRUE(!strcmp(fields, "queue=mouse"));

	XVT_ASSERT_TRUE(XvtLog_SplitMessage("xvt: app.ready", &event, &length,
					    &fields));
	XVT_ASSERT_INT_EQ(length, 9);
	XVT_ASSERT_TRUE(!strncmp(event, "app.ready", 9));
	XVT_ASSERT_TRUE(!strcmp(fields, ""));

	message = "aeron.scene: loaded 3 meshes: ok";
	XVT_ASSERT_TRUE(XvtLog_SplitMessage(message, &event, &length, &fields));
	XVT_ASSERT_INT_EQ(length, 11);
	XVT_ASSERT_TRUE(!strncmp(event, "aeron.scene", 11));
	XVT_ASSERT_TRUE(!strcmp(fields, "loaded 3 meshes: ok"));

	message = "[flight_gltf] missing: atlas";
	XVT_ASSERT_TRUE(
		!XvtLog_SplitMessage(message, &event, &length, &fields));
	XVT_ASSERT_TRUE(event == NULL);
	XVT_ASSERT_INT_EQ(length, 0);
	XVT_ASSERT_TRUE(fields == message);

	message = "no colon here";
	XVT_ASSERT_TRUE(
		!XvtLog_SplitMessage(message, &event, &length, &fields));
	XVT_ASSERT_TRUE(fields == message);

	XVT_ASSERT_TRUE(
		!XvtLog_SplitMessage(": leading", &event, &length, &fields));
	XVT_ASSERT_TRUE(
		!XvtLog_SplitMessage("xvt: ", &event, &length, &fields));
	XVT_ASSERT_TRUE(!XvtLog_SplitMessage(NULL, &event, &length, &fields));
	XVT_ASSERT_TRUE(!strcmp(fields, ""));
}

static void CheckFormat(void)
{
	char line[64];
	size_t written = XvtLog_FormatLine(line, sizeof line, 45296789u, 'I',
					   "app.start", 9, "version=\"1.0\"");
	XVT_ASSERT_TRUE(
		!strcmp(line, "12:34:56.789 I app.start version=\"1.0\"\n"));
	XVT_ASSERT_INT_EQ(written, strlen(line));

	written = XvtLog_FormatLine(line, sizeof line, 0, 'D', "app.ready", 9,
				    "");
	XVT_ASSERT_TRUE(!strcmp(line, "00:00:00.000 D app.ready\n"));
	XVT_ASSERT_INT_EQ(written, 25);

	/* One second past midnight after a full day wraps. */
	XvtLog_FormatLine(line, sizeof line, 86400000u + 1000u, 'W', "x.y", 3,
			  NULL);
	XVT_ASSERT_TRUE(!strcmp(line, "00:00:01.000 W x.y\n"));

	/* Line-breaking characters in the text become spaces. */
	XvtLog_FormatLine(line, sizeof line, 0, 'E', "x.y", 3,
			  "a=1\nb=2\tc=3\r");
	XVT_ASSERT_TRUE(!strcmp(line, "00:00:00.000 E x.y a=1 b=2 c=3 \n"));

	/* A line that does not fit keeps its newline and terminator. */
	written = XvtLog_FormatLine(line, 20, 45296789u, 'I', "app.start", 9,
				    "version=\"1.0\"");
	XVT_ASSERT_TRUE(!strcmp(line, "12:34:56.789 I app\n"));
	XVT_ASSERT_INT_EQ(written, 19);

	line[0] = 'x';
	XVT_ASSERT_INT_EQ(XvtLog_FormatLine(line, 1, 0, 'I', "x.y", 3, ""), 0);
	XVT_ASSERT_INT_EQ(line[0], 0);
	line[0] = 'x';
	XVT_ASSERT_INT_EQ(XvtLog_FormatLine(line, 0, 0, 'I', "x.y", 3, ""), 0);
	XVT_ASSERT_INT_EQ(line[0], 'x');
}

static void CheckShortenHome(void)
{
	static const char home[] = "/Users/ann";
	const size_t length = sizeof(home) - 1;
	char out[64];
	size_t written = XvtLog_ShortenHome(
		out, sizeof out, "root=\"/Users/ann/Games\"", home, length);
	XVT_ASSERT_TRUE(!strcmp(out, "root=\"~/Games\""));
	XVT_ASSERT_INT_EQ(written, strlen(out));

	/* Every copy at a path start is replaced, the whole text included. */
	XvtLog_ShortenHome(out, sizeof out,
			   "a=/Users/ann/x b=\"/Users/ann\" '/Users/ann'", home,
			   length);
	XVT_ASSERT_TRUE(!strcmp(out, "a=~/x b=\"~\" '~'"));
	XvtLog_ShortenHome(out, sizeof out, "/Users/ann", home, length);
	XVT_ASSERT_TRUE(!strcmp(out, "~"));

	/* A longer name, a deeper path, and other letter case stay whole. */
	XvtLog_ShortenHome(out, sizeof out,
			   "/Users/anna/x /old/Users/ann/x /USERS/ann/x", home,
			   length);
	XVT_ASSERT_TRUE(
		!strcmp(out, "/Users/anna/x /old/Users/ann/x /USERS/ann/x"));

	/* Windows separators. */
	XvtLog_ShortenHome(out, sizeof out, "path=\"C:\\Users\\ann\\AppData\"",
			   "C:\\Users\\ann", 12);
	XVT_ASSERT_TRUE(!strcmp(out, "path=\"~\\AppData\""));

	/* No usable home copies the text unchanged; a NULL text copies as "". */
	XvtLog_ShortenHome(out, sizeof out, "/Users/ann/x", NULL, length);
	XVT_ASSERT_TRUE(!strcmp(out, "/Users/ann/x"));
	XvtLog_ShortenHome(out, sizeof out, "/x/y", "/", 1);
	XVT_ASSERT_TRUE(!strcmp(out, "/x/y"));
	XVT_ASSERT_INT_EQ(
		XvtLog_ShortenHome(out, sizeof out, NULL, home, length), 0);
	XVT_ASSERT_INT_EQ(out[0], 0);

	/* A copy that does not fit is cut with its terminator. */
	written = XvtLog_ShortenHome(out, 4, "/Users/ann/Games", home, length);
	XVT_ASSERT_TRUE(!strcmp(out, "~/G"));
	XVT_ASSERT_INT_EQ(written, 3);
	out[0] = 'x';
	XVT_ASSERT_INT_EQ(
		XvtLog_ShortenHome(out, 0, "/Users/ann", home, length), 0);
	XVT_ASSERT_INT_EQ(out[0], 'x');
}

static void CheckHexList(void)
{
	static const unsigned values[] = {0x2au, 0xffffffffu, 0u};
	char out[32];
	size_t written = XvtLog_FormatHexList(out, sizeof out, values, 3);
	XVT_ASSERT_TRUE(!strcmp(out, "0000002a,ffffffff,00000000"));
	XVT_ASSERT_INT_EQ(written, 26);
	XVT_ASSERT_INT_EQ(XvtLog_FormatHexList(out, sizeof out, values, 0), 0);
	XVT_ASSERT_INT_EQ(out[0], 0);

	/* Only whole words: 18 bytes hold two words and the terminator, 17 hold one. */
	XVT_ASSERT_INT_EQ(XvtLog_FormatHexList(out, 18, values, 3), 17);
	XVT_ASSERT_TRUE(!strcmp(out, "0000002a,ffffffff"));
	XVT_ASSERT_INT_EQ(XvtLog_FormatHexList(out, 17, values, 3), 8);
	XVT_ASSERT_TRUE(!strcmp(out, "0000002a"));
	out[0] = 'x';
	XVT_ASSERT_INT_EQ(XvtLog_FormatHexList(out, 0, values, 3), 0);
	XVT_ASSERT_INT_EQ(out[0], 'x');
}

int main(void)
{
	CheckGate();
	CheckParse();
	CheckSplit();
	CheckFormat();
	CheckShortenHome();
	CheckHexList();
	return 0;
}

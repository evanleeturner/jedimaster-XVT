/* Tests for xvt_runtime/ai/ai_program.c with real child processes. The
 * stand-in AI program in the repository's ai/ folder answers questions this
 * file builds, and the engine's reader takes each answer whole. Short Python
 * programs given on the command line answer in pieces, exit, stay silent or
 * send a length too long; each fault must run the failure callback once and
 * leave every later exchange answering 0. XVT_TEST_PYTHON names the
 * interpreter and XVT_TEST_AI_PROGRAM_DIR the stand-in's folder. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/ai/ai_program.h"
#include "xvt_runtime/ai/ai_wire.h"

enum {
	TEST_TICK = 0x01020304,
	TEST_LIMIT_MS = 2000,
	TEST_SHORT_LIMIT_MS = 300,
	TEST_CRAFT_MAX = 32,
	/* A craft answer with no action: object index, notebook, count. */
	TEST_IDLE_ANSWER_BYTES = 2 + AI_NOTEBOOK_BYTES + 1,
};

static uint8_t g_question[AI_WIRE_HEAD_BYTES +
			  TEST_CRAFT_MAX * AI_WIRE_CRAFT_VIEW_BYTES];
static uint8_t g_answer[AI_WIRE_HEAD_BYTES +
			TEST_CRAFT_MAX * AI_WIRE_CRAFT_ANSWER_BYTES_MAX];
static struct ai_wire_craft_view g_views[TEST_CRAFT_MAX];
static struct ai_wire_craft_answer g_answers[TEST_CRAFT_MAX];
static int g_failures;
static char g_why[AI_PROGRAM_WHY_CAPACITY];

static void count_failure(const char *why)
{
	++g_failures;
	snprintf(g_why, sizeof(g_why), "%s", why);
}

/* Starts args with count_failure as the callback, failures counted from 0. */
static void start(const char *const *args, uint32_t limit_ms)
{
	char why[AI_PROGRAM_WHY_CAPACITY] = "";
	g_failures = 0;
	g_why[0] = '\0';
	int started = ai_program_start(args, limit_ms, count_failure, why,
				       sizeof(why));
	if (!started) {
		fprintf(stderr, "start refused: %s\n", why);
	}
	XVT_ASSERT_INT_EQ(started, 1);
}

static void start_python(const char *code, uint32_t limit_ms)
{
	const char *args[] = {XVT_TEST_PYTHON, "-c", code, NULL};
	start(args, limit_ms);
}

/* Writes into g_question a question about count craft at tick, each craft
 * with its own object index and notebook bytes; returns its length. */
static size_t question_about(int count, uint32_t tick)
{
	for (int craft = 0; craft < count; ++craft) {
		memset(&g_views[craft], 0, sizeof(g_views[craft]));
		g_views[craft].object_index = (uint16_t)(3 * craft + 5);
		g_views[craft].object_signature = (uint16_t)(0x100 + craft);
		g_views[craft].x = -1000 * craft;
		for (int byte = 0; byte < AI_NOTEBOOK_BYTES; ++byte) {
			g_views[craft].notebook[byte] =
				(uint8_t)(craft * 7 + byte);
		}
	}
	size_t length = ai_wire_write_question(g_question, sizeof(g_question),
					       tick, g_views, count);
	XVT_ASSERT_INT_EQ((int)length, (int)ai_wire_question_bytes(count));
	return length;
}

static size_t exchange_about_one_craft(void)
{
	size_t asked = question_about(1, TEST_TICK);
	return ai_program_exchange(g_question, asked, g_answer,
				   sizeof(g_answer));
}

/* The stand-in answers each craft with its own notebook and no action, and
 * the reader takes the answer whole, for a small question and a full one. */
static void check_stand_in_answers_with_nothing(void)
{
	const char *args[] = {XVT_TEST_PYTHON,
			      XVT_TEST_AI_PROGRAM_DIR "/jedimaster_ai", NULL};
	start(args, TEST_LIMIT_MS);
	static const int counts[] = {3, TEST_CRAFT_MAX};
	for (int round = 0; round < 2; ++round) {
		int count = counts[round];
		uint32_t tick = TEST_TICK + 8u * (uint32_t)round;
		size_t asked = question_about(count, tick);
		size_t length = ai_program_exchange(g_question, asked, g_answer,
						    sizeof(g_answer));
		XVT_ASSERT_INT_EQ((int)length,
				  AI_WIRE_HEAD_BYTES +
					  count * TEST_IDLE_ANSWER_BYTES);
		XVT_ASSERT_INT_EQ(ai_wire_read_answer(g_answer, length, tick,
						      g_views, count,
						      g_answers),
				  AI_WIRE_OK);
		for (int craft = 0; craft < count; ++craft) {
			XVT_ASSERT_INT_EQ(g_answers[craft].object_index,
					  g_views[craft].object_index);
			XVT_ASSERT_INT_EQ((int)g_answers[craft].actions, 0);
			XVT_ASSERT_INT_EQ(memcmp(g_answers[craft].notebook,
						 g_views[craft].notebook,
						 AI_NOTEBOOK_BYTES),
					  0);
		}
	}
	ai_program_stop();
	XVT_ASSERT_INT_EQ(g_failures, 0);
}

/* An answer that arrives in two pieces with a pause between comes back
 * whole, its length field first. */
static void check_answer_in_pieces(void)
{
	start_python("import sys, time\n"
		     "sys.stdin.buffer.read(4)\n"
		     "out = sys.stdout.buffer\n"
		     "out.write(b'\\x06\\x00'); out.flush(); time.sleep(0.05)\n"
		     "out.write(b'\\x00\\x00abcdef'); out.flush()\n"
		     "sys.stdin.buffer.read()\n",
		     TEST_LIMIT_MS);
	XVT_ASSERT_INT_EQ((int)exchange_about_one_craft(), 10);
	XVT_ASSERT_INT_EQ(memcmp(g_answer,
				 "\x06\x00\x00\x00"
				 "abcdef",
				 10),
			  0);
	ai_program_stop();
	XVT_ASSERT_INT_EQ(g_failures, 0);
}

/* A program that exits is a fault: the callback runs once, and later
 * exchanges answer 0 without running it again. */
static void check_exit_is_a_fault(void)
{
	start_python("import sys\nsys.exit(3)\n", TEST_LIMIT_MS);
	XVT_ASSERT_INT_EQ((int)exchange_about_one_craft(), 0);
	XVT_ASSERT_INT_EQ(g_failures, 1);
	XVT_ASSERT_TRUE(strstr(g_why, "The AI program stopped") != NULL);
	XVT_ASSERT_INT_EQ((int)exchange_about_one_craft(), 0);
	XVT_ASSERT_INT_EQ(g_failures, 1);
	ai_program_stop();
}

/* A program silent past the answer limit is a fault. */
static void check_silence_is_a_fault(void)
{
	start_python("import time\ntime.sleep(30)\n", TEST_SHORT_LIMIT_MS);
	XVT_ASSERT_INT_EQ((int)exchange_about_one_craft(), 0);
	XVT_ASSERT_INT_EQ(g_failures, 1);
	XVT_ASSERT_TRUE(strstr(g_why, "within 300 ms") != NULL);
}

/* A length field past what the answer buffer holds is a fault, found before
 * any byte after it is read. */
static void check_long_length_is_a_fault(void)
{
	start_python("import sys, time\n"
		     "sys.stdin.buffer.read(4)\n"
		     "sys.stdout.buffer.write(b'\\xf0\\xff\\xff\\xff')\n"
		     "sys.stdout.buffer.flush()\n"
		     "time.sleep(30)\n",
		     TEST_LIMIT_MS);
	XVT_ASSERT_INT_EQ((int)exchange_about_one_craft(), 0);
	XVT_ASSERT_INT_EQ(g_failures, 1);
	XVT_ASSERT_TRUE(strstr(g_why, "4294967284-byte answer") != NULL);
}

/* A program that cannot be started, and a second start while one runs, are
 * refused with a sentence; neither runs the callback. */
static void check_start_refusals(void)
{
	char why[AI_PROGRAM_WHY_CAPACITY] = "";
	const char *missing[] = {"/nonexistent/ai-program", NULL};
	g_failures = 0;
	XVT_ASSERT_INT_EQ(ai_program_start(missing, TEST_LIMIT_MS,
					   count_failure, why, sizeof(why)),
			  0);
	XVT_ASSERT_TRUE(strstr(why, "could not be started") != NULL);
	start_python("import sys\nsys.stdin.buffer.read()\n", TEST_LIMIT_MS);
	XVT_ASSERT_INT_EQ(ai_program_start(missing, TEST_LIMIT_MS,
					   count_failure, why, sizeof(why)),
			  0);
	XVT_ASSERT_TRUE(strstr(why, "already running") != NULL);
	ai_program_stop();
	XVT_ASSERT_INT_EQ(g_failures, 0);
}

/* With no program running an exchange answers 0. Stopping a program that
 * ignores its closed input kills it after the stop limit, and a new program
 * can start after it. */
static void check_stop(void)
{
	XVT_ASSERT_INT_EQ((int)exchange_about_one_craft(), 0);
	start_python("import time\ntime.sleep(30)\n", TEST_LIMIT_MS);
	ai_program_stop();
	start_python("import sys\nsys.stdin.buffer.read()\n", TEST_LIMIT_MS);
	ai_program_stop();
	XVT_ASSERT_INT_EQ(g_failures, 0);
}

int main(void)
{
	check_stand_in_answers_with_nothing();
	check_answer_in_pieces();
	check_exit_is_a_fault();
	check_silence_is_a_fault();
	check_long_length_is_a_fault();
	check_start_refusals();
	check_stop();
	return 0;
}

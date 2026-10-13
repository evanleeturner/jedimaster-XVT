/* Tests for xvt_runtime/ai/ai_flight.c with real child processes: which AI
 * program a flight runs, and that one failing during the flight ends it.
 * XVT_TEST_PYTHON names the interpreter and XVT_TEST_AI_PROGRAM_DIR the
 * stand-in's folder. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt_runtime/ai/ai_flight.h"
#include "xvt_runtime/ai/ai_program.h"

static struct xvt_settings g_settings;

static const struct xvt_settings *program_set(const char *python,
					      const char *program)
{
	memset(&g_settings, 0, sizeof(g_settings));
	snprintf(g_settings.ai_python, sizeof(g_settings.ai_python), "%s",
		 python);
	snprintf(g_settings.ai_program, sizeof(g_settings.ai_program), "%s",
		 program);
	return &g_settings;
}

/* Asks whatever program runs. Any bytes do: no program, or one that is gone,
 * answers 0 before reading them. */
static size_t exchange(void)
{
	uint8_t question[8] = {0};
	uint8_t answer[64];
	return ai_program_exchange(question, sizeof(question), answer,
				   sizeof(answer));
}

/* A flight the launch door started keeps the door's program: none starts. */
static void check_door_flight_starts_none(void)
{
	const struct xvt_settings *settings = program_set(
		XVT_TEST_PYTHON, XVT_TEST_AI_PROGRAM_DIR "/jedimaster_ai");
	XVT_ASSERT_INT_EQ(ai_flight_begin(settings, 1), AI_FLIGHT_DOOR);
	XVT_ASSERT_INT_EQ((int)exchange(), 0);
	ai_flight_end();
}

/* With no settings, or either half empty, computer craft sit. */
static void check_unset(void)
{
	XVT_ASSERT_INT_EQ(ai_flight_begin(NULL, 0), AI_FLIGHT_UNSET);
	XVT_ASSERT_INT_EQ(ai_flight_begin(program_set("", "ai"), 0),
			  AI_FLIGHT_UNSET);
	XVT_ASSERT_INT_EQ(ai_flight_begin(program_set(XVT_TEST_PYTHON, ""), 0),
			  AI_FLIGHT_UNSET);
	XVT_ASSERT_INT_EQ((int)exchange(), 0);
}

/* A Python that cannot be started leaves the craft sitting and the flight
 * flying. */
static void check_unstarted(void)
{
	g_flight_mission_state.mission_end_pending = 0;
	XVT_ASSERT_INT_EQ(
		ai_flight_begin(program_set("/nonexistent/python", "ai"), 0),
		AI_FLIGHT_UNSTARTED);
	XVT_ASSERT_INT_EQ((int)exchange(), 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	ai_flight_end();
}

/* The stand-in runs from a flight's start to its end; a second start ends
 * the first program before its own. */
static void check_started_then_stopped(void)
{
	g_flight_mission_state.mission_end_pending = 0;
	const struct xvt_settings *settings = program_set(
		XVT_TEST_PYTHON, XVT_TEST_AI_PROGRAM_DIR "/jedimaster_ai");
	XVT_ASSERT_INT_EQ(ai_flight_begin(settings, 0), AI_FLIGHT_STARTED);
	XVT_ASSERT_INT_EQ(ai_flight_begin(settings, 0), AI_FLIGHT_STARTED);
	ai_flight_end();
	XVT_ASSERT_INT_EQ((int)exchange(), 0);
	ai_flight_end();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
}

/* A program that dies during the flight ends the flight. */
static void check_failure_ends_the_flight(void)
{
	g_flight_mission_state.mission_end_pending = 0;
	XVT_ASSERT_INT_EQ(
		ai_flight_begin(program_set(XVT_TEST_PYTHON,
					    "/nonexistent/jedimaster_ai"),
				0),
		AI_FLIGHT_STARTED);
	XVT_ASSERT_INT_EQ((int)exchange(), 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 1);
	ai_flight_end();
	g_flight_mission_state.mission_end_pending = 0;
}

int main(void)
{
	ai_flight_set_log(0x1234abcd, "");
	check_door_flight_starts_none();
	check_unset();
	check_unstarted();
	check_started_then_stopped();
	check_failure_ends_the_flight();
	return 0;
}

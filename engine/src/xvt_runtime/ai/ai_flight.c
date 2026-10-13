#include "xvt_runtime/ai/ai_flight.h"

#include <stdio.h>

#include "xvt/flight/ai_brain.h"
#include "xvt/flight/flight.h"
#include "xvt_runtime/ai/ai_program.h"
#include "xvt_runtime/log/log.h"

static uint32_t g_run_id;
static char g_log_file[XVT_PATH_CAPACITY];
/* 1 while the program ai_flight_begin started runs. */
static int g_started;
/* 1 once this run has warned that no program is set. */
static int g_unset_warned;

void ai_flight_set_log(uint32_t run_id, const char *ai_log_file)
{
	g_run_id = run_id;
	snprintf(g_log_file, sizeof(g_log_file), "%s",
		 ai_log_file ? ai_log_file : "");
}

/* Ends the flight when its program fails, as a network fault does: no
 * computer craft would decide anything again, and a machine that flew on
 * without its program would part from the others in a network game. */
static void end_flight_on_failure(const char *why)
{
	XVT_LOG_INFO("ai.flight_ending why=\"%s\"", why);
	g_flight_mission_state.mission_end_pending = 1;
}

int ai_flight_begin(const struct xvt_settings *settings, int door_active)
{
	ai_flight_end();
	if (door_active) {
		return AI_FLIGHT_DOOR;
	}
	if (!settings || !settings->ai_python[0] || !settings->ai_program[0]) {
		if (!g_unset_warned) {
			XVT_LOG_WARN("ai.flight_program_unset");
			g_unset_warned = 1;
		}
		return AI_FLIGHT_UNSET;
	}
	const char *level = xvt_log_level_name(xvt_log_level());
	struct ai_program_command command;
	ai_program_fill_command(&command, settings->ai_python,
				settings->ai_program, g_run_id, level,
				g_log_file);
	XVT_LOG_INFO(
		"ai.flight_program path=\"%s\" run=%08x level=\"%s\" log_file=\"%s\"",
		settings->ai_program, (unsigned)g_run_id, level, g_log_file);
	char why[AI_PROGRAM_WHY_CAPACITY];
	if (!ai_program_start(command.args, AI_PROGRAM_ANSWER_LIMIT_MS,
			      end_flight_on_failure, why, sizeof(why))) {
		XVT_LOG_ERROR("ai.flight_unstarted why=\"%s\"", why);
		return AI_FLIGHT_UNSTARTED;
	}
	ai_brain_set_exchange(ai_program_exchange);
	g_started = 1;
	return AI_FLIGHT_STARTED;
}

void ai_flight_end(void)
{
	if (!g_started) {
		return;
	}
	g_started = 0;
	ai_brain_set_exchange(NULL);
	ai_program_stop();
}

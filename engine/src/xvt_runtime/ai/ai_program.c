/* The AI program as a child process, through SDL's process pipes, which SDL
 * opens non-blocking: a pipe that is not ready is looked at again after
 * AI_PROGRAM_POLL_WAIT_NS until the exchange's deadline. */
#include "xvt_runtime/ai/ai_program.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "xvt_runtime/log/log.h"

enum {
	AI_PROGRAM_POLL_WAIT_NS = 50000,
	/* The answer's u32 length field, which counts the bytes after it. */
	AI_PROGRAM_LENGTH_BYTES = 4,
};

static SDL_Process *g_process;
static uint32_t g_answer_limit_ms;
static ai_program_failure_fn g_on_failure;

/* Stops the program for good: kills it, logs why with its exit code, and
 * runs the failure callback with the sentence. */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static void ai_program_fault(const char *format, ...)
{
	char why[AI_PROGRAM_WHY_CAPACITY];
	va_list args;
	va_start(args, format);
	vsnprintf(why, sizeof why, format, args);
	va_end(args);
	int exit_code = INT_MIN;
	(void)SDL_KillProcess(g_process, true);
	(void)SDL_WaitProcess(g_process, true, &exit_code);
	SDL_DestroyProcess(g_process);
	g_process = NULL;
	XVT_LOG_ERROR("ai.program_failed exit=%d why=\"%s\"", exit_code, why);
	if (g_on_failure != NULL) {
		g_on_failure(why);
	}
}

/* After a write or read through stream that moved fewer bytes than asked:
 * returns 1 to try again, having waited when nothing moved, or 0 after a
 * fault when the pipe closed or the deadline passed. */
static int ai_program_may_retry(SDL_IOStream *stream, size_t moved,
				const char *pipe_name, uint64_t deadline_ns)
{
	if (moved == 0 && SDL_GetIOStatus(stream) != SDL_IO_STATUS_NOT_READY) {
		ai_program_fault("The AI program stopped: its %s closed.",
				 pipe_name);
		return 0;
	}
	if (SDL_GetTicksNS() >= deadline_ns) {
		ai_program_fault("The AI program did not answer within %u ms.",
				 (unsigned)g_answer_limit_ms);
		return 0;
	}
	if (moved == 0) {
		SDL_DelayNS(AI_PROGRAM_POLL_WAIT_NS);
	}
	return 1;
}

/* Writes count bytes to the program by the deadline. Returns 1, or 0 after
 * a fault. */
static int ai_program_write(const uint8_t *bytes, size_t count,
			    uint64_t deadline_ns)
{
	SDL_IOStream *input = SDL_GetProcessInput(g_process);
	size_t done = 0;
	while (done < count) {
		size_t moved = SDL_WriteIO(input, bytes + done, count - done);
		done += moved;
		if (done < count &&
		    !ai_program_may_retry(input, moved, "input", deadline_ns)) {
			return 0;
		}
	}
	return 1;
}

/* Reads count bytes from the program by the deadline. Returns 1, or 0 after
 * a fault. */
static int ai_program_read(uint8_t *bytes, size_t count, uint64_t deadline_ns)
{
	SDL_IOStream *output = SDL_GetProcessOutput(g_process);
	size_t done = 0;
	while (done < count) {
		size_t moved = SDL_ReadIO(output, bytes + done, count - done);
		done += moved;
		if (done < count &&
		    !ai_program_may_retry(output, moved, "output",
					  deadline_ns)) {
			return 0;
		}
	}
	return 1;
}

int ai_program_start(const char *const *args, uint32_t answer_limit_ms,
		     ai_program_failure_fn on_failure, char *why,
		     size_t why_capacity)
{
	if (g_process != NULL) {
		snprintf(why, why_capacity, "%s",
			 "An AI program is already running.");
		return 0;
	}
	g_process = SDL_CreateProcess(args, true);
	if (g_process == NULL) {
		snprintf(why, why_capacity,
			 "The AI program \"%s\" could not be started: %s",
			 args[0], SDL_GetError());
		XVT_LOG_ERROR(
			"ai.program_unstarted program=\"%s\" error=\"%s\"",
			args[0], SDL_GetError());
		return 0;
	}
	g_answer_limit_ms = answer_limit_ms;
	g_on_failure = on_failure;
	XVT_LOG_INFO("ai.program_started program=\"%s\" pid=%lld", args[0],
		     (long long)SDL_GetNumberProperty(
			     SDL_GetProcessProperties(g_process),
			     SDL_PROP_PROCESS_PID_NUMBER, 0));
	return 1;
}

size_t ai_program_exchange(const uint8_t *question, size_t question_length,
			   uint8_t *answer, size_t answer_capacity)
{
	if (g_process == NULL) {
		return 0;
	}
	uint64_t started_ns = SDL_GetTicksNS();
	uint64_t deadline_ns =
		started_ns + (uint64_t)g_answer_limit_ms * SDL_NS_PER_MS;
	uint8_t length_bytes[AI_PROGRAM_LENGTH_BYTES];
	if (!ai_program_write(question, question_length, deadline_ns) ||
	    !ai_program_read(length_bytes, sizeof length_bytes, deadline_ns)) {
		return 0;
	}
	uint64_t length = (uint64_t)length_bytes[0] |
			  (uint64_t)length_bytes[1] << 8 |
			  (uint64_t)length_bytes[2] << 16 |
			  (uint64_t)length_bytes[3] << 24;
	uint64_t whole = AI_PROGRAM_LENGTH_BYTES + length;
	if (whole > answer_capacity) {
		ai_program_fault(
			"The AI program sent a %llu-byte answer; the engine holds %llu.",
			(unsigned long long)whole,
			(unsigned long long)answer_capacity);
		return 0;
	}
	memcpy(answer, length_bytes, sizeof length_bytes);
	if (!ai_program_read(answer + AI_PROGRAM_LENGTH_BYTES, (size_t)length,
			     deadline_ns)) {
		return 0;
	}
	XVT_LOG_DEBUG(
		"ai.program_exchanged question_bytes=%d answer_bytes=%d wait_us=%lld",
		(int)question_length, (int)whole,
		(long long)((SDL_GetTicksNS() - started_ns) / SDL_NS_PER_US));
	return (size_t)whole;
}

void ai_program_stop(void)
{
	if (g_process == NULL) {
		return;
	}
	SDL_CloseIO(SDL_GetProcessInput(g_process));
	uint64_t deadline_ns =
		SDL_GetTicksNS() +
		(uint64_t)AI_PROGRAM_STOP_LIMIT_MS * SDL_NS_PER_MS;
	int exit_code = INT_MIN;
	bool exited = SDL_WaitProcess(g_process, false, &exit_code);
	while (!exited && SDL_GetTicksNS() < deadline_ns) {
		SDL_DelayNS(AI_PROGRAM_POLL_WAIT_NS);
		exited = SDL_WaitProcess(g_process, false, &exit_code);
	}
	if (!exited) {
		(void)SDL_KillProcess(g_process, true);
		(void)SDL_WaitProcess(g_process, true, &exit_code);
	}
	SDL_DestroyProcess(g_process);
	g_process = NULL;
	XVT_LOG_INFO("ai.program_stopped exit=%d killed=%d", exit_code,
		     !exited);
}

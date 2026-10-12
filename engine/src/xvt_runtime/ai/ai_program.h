#ifndef XVT_RUNTIME_AI_AI_PROGRAM_H
#define XVT_RUNTIME_AI_AI_PROGRAM_H

/* The AI program, the separate program that decides what computer craft do,
 * run as a child of the engine. It reads each question (ai_wire.h) on its
 * standard input and writes each answer on its standard output; its standard
 * error is the engine's. At most one runs at a time. Any fault (it exits,
 * sends a length the answer buffer cannot hold, or is silent past the answer
 * limit) stops it for good: it is killed, the failure callback runs once
 * with a plain sentence, and every later exchange returns 0. Closing its
 * standard input is the sign to exit. Nothing here reads game state. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ai_program_constant {
	/* The time one exchange may take, the question's writing included. */
	AI_PROGRAM_ANSWER_LIMIT_MS = 5000,
	/* The time a stopped program may take to exit before it is killed. */
	AI_PROGRAM_STOP_LIMIT_MS = 1000,
	AI_PROGRAM_WHY_CAPACITY = 256,
	/* A command line's entries, its ending NULL included: the Python, the
	 * program's path, and three options each with its value. */
	AI_PROGRAM_ARGS_CAPACITY = 9,
	/* Eight hex digits and the terminator. */
	AI_PROGRAM_RUN_ID_CAPACITY = 9,
};

/* The AI program's command line, for ai_program_start. args points into this
 * record and at the strings given to ai_program_fill_command, which must
 * outlive the start. */
struct ai_program_command {
	const char *args[AI_PROGRAM_ARGS_CAPACITY];
	char run_id[AI_PROGRAM_RUN_ID_CAPACITY];
};

/* Runs once, at the first fault, with why the program was stopped. */
typedef void (*ai_program_failure_fn)(const char *why);

/* Fills command with the AI program's command line: python, path,
 * "--run-id" and run_id as eight lowercase hex digits, "--log-level" and
 * level, and, unless log_file is "", "--log-file" and log_file; NULL ends the
 * list. The program's log then names the engine's run, at the engine's level,
 * in the file the engine chose (with no file, standard error only). */
void ai_program_fill_command(struct ai_program_command *command,
			     const char *python, const char *path,
			     uint32_t run_id, const char *level,
			     const char *log_file);

/* Starts the program args names: args[0] is the file to run, looked up on
 * PATH when it names no folder, and the list ends with NULL. Each later
 * exchange may take answer_limit_ms. Returns 1, or 0 with a plain sentence
 * in why when it could not be started or a program already runs. */
int ai_program_start(const char *const *args, uint32_t answer_limit_ms,
		     ai_program_failure_fn on_failure, char *why,
		     size_t why_capacity);

/* An ai_brain_exchange_fn: writes the question to the program and reads one
 * whole answer into answer, its length field included. Returns the answer's
 * length, or 0 when no program runs or at a fault. */
size_t ai_program_exchange(const uint8_t *question, size_t question_length,
			   uint8_t *answer, size_t answer_capacity);

/* Closes the program's standard input, waits up to AI_PROGRAM_STOP_LIMIT_MS
 * for it to exit, then kills it. Does nothing when no program runs. */
void ai_program_stop(void);

#ifdef __cplusplus
}
#endif

#endif

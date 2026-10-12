/* Aeron's log funnel for a test program built without Aeron, as the Windows
 * test job builds test_ai_program: every line goes to standard error as
 * "category: text", and the level starts at DEBUG, so the run prints each
 * exchange's wait (ai.program_exchanged wait_us=) beside its checks. */
#include <stdarg.h>
#include <stdio.h>

#include "xvt_runtime/log/log.h"

/* Runs before main: the test program sets no level, and the DEBUG lines are
 * what this build is for. */
__attribute__((constructor)) static void start_at_debug(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
}

/* drift-ok: camelcase -- Aeron's function, replaced to print lines */
void Aeron_LogMessageV(AeronLogLevel level, const char *category,
		       const char *fmt, va_list args)
{
	(void)level;
	fprintf(stderr, "%s: ", category ? category : "");
	vfprintf(stderr, fmt ? fmt : "", args);
	fputc('\n', stderr);
}

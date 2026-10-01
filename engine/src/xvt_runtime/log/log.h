#ifndef XVT_RUNTIME_LOG_LOG_H
#define XVT_RUNTIME_LOG_LOG_H

#include "aeron/log.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The program's log header. Every log line the new code writes goes through XVT_LOG_DEBUG, XVT_LOG_INFO,
 * XVT_LOG_WARN or XVT_LOG_ERROR, never through Aeron_Log* directly. A macro compares the level first and
 * evaluates its arguments only when the level is on, so a line that is switched off costs one compare
 * and formats nothing. The level is set once at start (XvtLog_SetLevel) and can be DEBUG in a release
 * build: the macros end in Aeron_LogMessageV, which Aeron keeps under NDEBUG.
 *
 * A line is an event id followed by key=value fields, never a sentence. The format string's first word
 * is the event id, a short two-part dotted name ("input.queue_full"); the rest is fields, one per word:
 * queue=mouse, count=%u, or name=\"%s\" for text, which is always double-quoted at the call site (a
 * quote inside the text is not escaped). The sentence for each event lives in events.json beside this
 * header, keyed by event id, with its level and field names. tools/log_catalog_check.py refuses a call
 * site whose event, level or fields disagree with that file, a catalog entry no site uses, and a call
 * that bypasses these macros. Log arguments must be pure: a side effect in an argument would run at some
 * levels and not at others.
 *
 * Aeron hands SDL "xvt: <event> <fields>". The output function the application installs
 * (src/xvt_app/log_sink.c) writes that as one line, HH:MM:SS.mmm L event key=value ..., with the UTC
 * time of day and a one-letter level: D, I, W, E or C. Aeron's own lines pass through the same function
 * with their category as the event and their text unchanged. State: the level in force. */

/* The Aeron category every macro writes under; the output function strips it. */
#define XVT_LOG_CATEGORY "xvt"

/* The level in force: a line is written when its level is at or above it. Set once at start; read
 * without a lock by every macro. */
extern AeronLogLevel g_xvtLogLevel;

/* Returns 1 when a line at level would be written, 0 when the macros skip it. */
static inline int XvtLog_Enabled(AeronLogLevel level) { return level >= g_xvtLogLevel; }

/* Formats fmt with its arguments and hands the text to Aeron at level, under XVT_LOG_CATEGORY. The macros
 * below are its intended callers and do the level check; this function does not. */
#if defined(_MSC_VER)
void XvtLog_Write(AeronLogLevel level, const char* fmt, ...);
#else
__attribute__((format(printf, 2, 3))) void XvtLog_Write(AeronLogLevel level, const char* fmt, ...);
#endif

/* Writes a line at level when the level is on. The first variable argument is the format string: the
 * event id, then the fields. Any further arguments are the fields' values, evaluated only when the line
 * is written. */
#define XVT_LOG_AT(level, ...)                                                                               \
	do {                                                                                                     \
		if (XvtLog_Enabled(level))                                                                           \
			XvtLog_Write((level), __VA_ARGS__);                                                              \
	} while (0)

#define XVT_LOG_DEBUG(...) XVT_LOG_AT(AERON_LOG_DEBUG, __VA_ARGS__)
#define XVT_LOG_INFO(...) XVT_LOG_AT(AERON_LOG_INFO, __VA_ARGS__)
#define XVT_LOG_WARN(...) XVT_LOG_AT(AERON_LOG_WARN, __VA_ARGS__)
#define XVT_LOG_ERROR(...) XVT_LOG_AT(AERON_LOG_ERROR, __VA_ARGS__)

/* Sets the level every macro compares against. Call once, before the first line, from the thread that
 * starts the program; the macros read it without a lock. */
void XvtLog_SetLevel(AeronLogLevel level);
/* Returns the level in force; INFO until SetLevel is called. */
AeronLogLevel XvtLog_Level(void);
/* Reads a level name, in any letter case: "debug", "info", "warn" or "error". Writes the level to out and
 * returns 1; returns 0, out untouched, for NULL or any other text. */
int XvtLog_ParseLevel(const char* name, AeronLogLevel* out);
/* Splits a message Aeron formatted into an event and its fields. "xvt: <event> <fields>" (a line from
 * these macros) gives the event word and the text after it; "<category>: <text>" (a line from Aeron, the
 * category holding no space) gives the category and the text. Writes event, its length and fields and
 * returns 1. Returns 0 for any other message, NULL included: event NULL, length 0, fields the whole
 * message ("" for NULL). */
int XvtLog_SplitMessage(const char* message, const char** event, size_t* event_length, const char** fields);
/* Writes one log line into out: the time of day as HH:MM:SS.mmm from ms_of_day (wrapped at 24 hours), a
 * space, the level letter, a space, the event (event_length bytes), then a space and the fields when
 * fields is nonempty, then a newline and a terminator. A newline, carriage return or tab inside the text
 * is written as a space, so one event is one line. A line that does not fit is cut so the newline and
 * terminator still fit. Capacity 0 writes nothing; capacity 1 writes only the terminator. Returns the
 * number of bytes written before the terminator. */
size_t XvtLog_FormatLine(char* out, size_t capacity, uint32_t ms_of_day, char level, const char* event,
						 size_t event_length, const char* fields);

#ifdef __cplusplus
}
#endif

#endif

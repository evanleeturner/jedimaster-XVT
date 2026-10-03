#include "xvt_runtime/log/log.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

AeronLogLevel g_xvt_log_level = AERON_LOG_INFO;

void xvt_log_write(AeronLogLevel level, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	Aeron_LogMessageV(level, XVT_LOG_CATEGORY, fmt, args);
	va_end(args);
}

void xvt_log_set_level(AeronLogLevel level) { g_xvt_log_level = level; }

AeronLogLevel xvt_log_level(void) { return g_xvt_log_level; }

static int xvt_log_equals_ignoring_case(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

int xvt_log_parse_level(const char *name, AeronLogLevel *out)
{
	static const struct {
		const char *name;
		AeronLogLevel level;
	} names[] = {{"debug", AERON_LOG_DEBUG},
		     {"info", AERON_LOG_INFO},
		     {"warn", AERON_LOG_WARN},
		     {"error", AERON_LOG_ERROR}};

	if (!name) {
		return 0;
	}
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (xvt_log_equals_ignoring_case(name, names[i].name)) {
			*out = names[i].level;
			return 1;
		}
	}
	return 0;
}

int xvt_log_split_message(const char *message, const char **event,
			  size_t *event_length, const char **fields)
{
	static const char prefix[] = XVT_LOG_CATEGORY ": ";
	const size_t prefix_length = sizeof(prefix) - 1;
	const char *colon;
	*event = NULL;
	*event_length = 0;
	*fields = message ? message : "";
	if (!message) {
		return 0;
	}
	if (!strncmp(message, prefix, prefix_length)) {
		const char *start = message + prefix_length;
		const char *end = start;
		while (*end && *end != ' ') {
			end++;
		}
		if (end == start) {
			return 0;
		}
		*event = start;
		*event_length = (size_t)(end - start);
		*fields = *end ? end + 1 : end;
		return 1;
	}
	colon = strstr(message, ": ");
	if (!colon || colon == message ||
	    memchr(message, ' ', (size_t)(colon - message))) {
		return 0;
	}
	*event = message;
	*event_length = (size_t)(colon - message);
	*fields = colon + 2;
	return 1;
}

/* Appends up to length bytes of text to out, never past limit, with line-breaking characters written as
 * spaces. Advances *used by what was written. */
static void xvt_log_append(char *out, size_t limit, size_t *used,
			   const char *text, size_t length)
{
	for (size_t i = 0; i < length && *used < limit; ++i) {
		char c = text[i];
		out[(*used)++] =
			(c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
	}
}

size_t xvt_log_format_line(char *out, size_t capacity, uint32_t ms_of_day,
			   char level, const char *event, size_t event_length,
			   const char *fields)
{
	char stamp[16];
	size_t used = 0;
	size_t limit;
	uint32_t ms;
	if (capacity == 0) {
		return 0;
	}
	if (capacity == 1) {
		out[0] = 0;
		return 0;
	}
	/* Two bytes stay reserved for the newline and the terminator. */
	limit = capacity - 2;
	ms = ms_of_day % 86400000u;
	snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u.%03u",
		 (unsigned)(ms / 3600000u), (unsigned)(ms / 60000u % 60u),
		 (unsigned)(ms / 1000u % 60u), (unsigned)(ms % 1000u));
	xvt_log_append(out, limit, &used, stamp, strlen(stamp));
	xvt_log_append(out, limit, &used, " ", 1);
	xvt_log_append(out, limit, &used, &level, 1);
	xvt_log_append(out, limit, &used, " ", 1);
	xvt_log_append(out, limit, &used, event ? event : "",
		       event ? event_length : 0);
	if (fields && fields[0]) {
		xvt_log_append(out, limit, &used, " ", 1);
		xvt_log_append(out, limit, &used, fields, strlen(fields));
	}
	out[used++] = '\n';
	out[used] = 0;
	return used;
}

static int xvt_log_is_path_start(char c)
{
	return c == ' ' || c == '"' || c == '\'' || c == '=';
}

static int xvt_log_is_path_break(char c)
{
	return c == 0 || c == '/' || c == '\\' || c == ' ' || c == '"' ||
	       c == '\'';
}

size_t xvt_log_shorten_home(char *out, size_t capacity, const char *text,
			    const char *home, size_t home_length)
{
	size_t used = 0;
	size_t i = 0;
	if (capacity == 0) {
		return 0;
	}
	if (!text) {
		text = "";
	}
	if (home_length < 2) {
		home = NULL;
	}
	while (text[i] && used + 1 < capacity) {
		/* strncmp returns 0 only when the text holds all home_length bytes, so the byte after them exists. */
		if (home && (i == 0 || xvt_log_is_path_start(text[i - 1])) &&
		    !strncmp(text + i, home, home_length) &&
		    xvt_log_is_path_break(text[i + home_length])) {
			out[used++] = '~';
			i += home_length;
			continue;
		}
		out[used++] = text[i++];
	}
	out[used] = 0;
	return used;
}

size_t xvt_log_format_hex_list(char *out, size_t capacity,
			       const unsigned *values, size_t count)
{
	size_t used = 0;
	if (capacity == 0) {
		return 0;
	}
	for (size_t i = 0; i < count; ++i) {
		char word[16];
		int length = snprintf(word, sizeof(word), "%s%08x",
				      i ? "," : "", values[i]);
		if (length < 0 || used + (size_t)length >= capacity) {
			break;
		}
		memcpy(out + used, word, (size_t)length);
		used += (size_t)length;
	}
	out[used] = 0;
	return used;
}

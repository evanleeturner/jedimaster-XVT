#ifndef AERON_COMPAT_DPLAY_ICE_LOG_H
#define AERON_COMPAT_DPLAY_ICE_LOG_H

#include <stddef.h>
#include <string.h>

/* libjuice's own log carries the session's SDP, its ICE credentials and the
 * players' addresses, so only the messages that say why a link failed are
 * passed on: fixed texts that carry no value but an entry number. A debug
 * build of libjuice writes "file.c:line: " first; that prefix is skipped.
 * Returns the text to log, inside message, or NULL when the message must not
 * be logged (NULL included). */
static inline const char* DpIce_LogText(const char* message) {
	static const char* const fixed[] = {
		"Lost connectivity",      "Connectivity timer expired", "Sending keepalive failed",
		"TURN allocation failed", "STUN server binding failed",
	};
	static const char consent[] = ": Consent expired for candidate pair";
	const char*       p         = message;
	const char*       digits;
	if (!message)
		return NULL;
	while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
		++p;
	if (p > message && strncmp(p, ".c:", 3) == 0) {
		digits = p + 3;
		p      = digits;
		while (*p >= '0' && *p <= '9')
			++p;
		if (p > digits && p[0] == ':' && p[1] == ' ')
			message = p + 2;
	}
	for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
		if (strcmp(message, fixed[i]) == 0)
			return message;
	if (strncmp(message, "STUN entry ", 11) != 0)
		return NULL;
	digits = message + 11;
	p      = digits;
	while (*p >= '0' && *p <= '9')
		++p;
	return p > digits && strcmp(p, consent) == 0 ? message : NULL;
}

#endif

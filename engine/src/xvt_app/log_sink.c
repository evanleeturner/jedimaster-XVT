#include "xvt_app/log_sink.h"

#include "xvt_runtime/log/log.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* Aeron formats a message into 1,024 bytes; the line adds the stamp, the level and the category. */
#define XVT_LOG_SINK_LINE_CAPACITY 1280
#define XVT_LOG_SINK_PATH_CAPACITY 1024

/* The home folder with no trailing separator, found once at install; empty when SDL cannot name it. */
static char g_logSinkHome[XVT_LOG_SINK_PATH_CAPACITY];
static size_t g_logSinkHomeLength;

static SDL_LogPriority XvtLogSink_Priority(AeronLogLevel level) {
	switch (level) {
		case AERON_LOG_DEBUG:
			return SDL_LOG_PRIORITY_DEBUG;
		case AERON_LOG_WARN:
			return SDL_LOG_PRIORITY_WARN;
		case AERON_LOG_ERROR:
			return SDL_LOG_PRIORITY_ERROR;
		default:
			return SDL_LOG_PRIORITY_INFO;
	}
}

static char XvtLogSink_Letter(SDL_LogPriority priority) {
	switch (priority) {
		case SDL_LOG_PRIORITY_TRACE:
		case SDL_LOG_PRIORITY_VERBOSE:
		case SDL_LOG_PRIORITY_DEBUG:
			return 'D';
		case SDL_LOG_PRIORITY_WARN:
			return 'W';
		case SDL_LOG_PRIORITY_ERROR:
			return 'E';
		case SDL_LOG_PRIORITY_CRITICAL:
			return 'C';
		default:
			return 'I';
	}
}

/* The UTC time of day in milliseconds; 0 when the clock cannot be read. */
static uint32_t XvtLogSink_MsOfDay(void) {
	SDL_Time now;
	SDL_DateTime stamp;
	if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &stamp, false))
		return 0;
	return (uint32_t)stamp.hour * 3600000u + (uint32_t)stamp.minute * 60000u +
		   (uint32_t)stamp.second * 1000u + (uint32_t)(stamp.nanosecond / 1000000);
}

/* Records the home folder for XvtLog_ShortenHome; a folder too long for the buffer is left unrecorded. */
static void XvtLogSink_FindHome(void) {
	const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
	size_t length = home ? strlen(home) : 0;
	while (length > 0 && (home[length - 1] == '/' || home[length - 1] == '\\'))
		length--;
	if (length >= sizeof(g_logSinkHome))
		length = 0;
	if (length)
		memcpy(g_logSinkHome, home, length);
	g_logSinkHome[length] = 0;
	g_logSinkHomeLength = length;
}

static void XvtLogSink_Write(void* userdata, int category, SDL_LogPriority priority, const char* message) {
	char line[XVT_LOG_SINK_LINE_CAPACITY];
	char shortened[XVT_LOG_SINK_LINE_CAPACITY];
	const char* event;
	size_t event_length;
	const char* fields;
	(void)userdata;
	if (!message)
		message = "";
	if (category != SDL_LOG_CATEGORY_APPLICATION ||
		!XvtLog_SplitMessage(message, &event, &event_length, &fields)) {
		event = "sdl";
		event_length = 3;
		fields = message;
	}
	XvtLog_ShortenHome(shortened, sizeof(shortened), fields, g_logSinkHome, g_logSinkHomeLength);
	XvtLog_FormatLine(line, sizeof(line), XvtLogSink_MsOfDay(), XvtLogSink_Letter(priority), event,
					  event_length, shortened);
	fputs(line, stderr);
}

static void XvtLogSink_WriteHeader(const XvtLaunchOptions* options) {
	AeronConfig config;
	SDL_Time now = 0;
	SDL_DateTime today = { 0 };
	char resources[XVT_LOG_SINK_PATH_CAPACITY];
	char shown[3][XVT_LOG_SINK_PATH_CAPACITY];
	char* user;
	char* cwd;
	XvtHostConfig_InitAeron(options, &config);
	if (SDL_GetCurrentTime(&now))
		SDL_TimeToDateTime(now, &today, false);
	fprintf(stderr, "= %s run %08x fmt 1 date %04d-%02d-%02d tz utc\n", config.app_name,
			(unsigned)((uint64_t)now & 0xffffffffu), today.year, today.month, today.day);
	if (!XvtHostConfig_ResolveResourceRoot(options, resources, sizeof(resources)))
		resources[0] = 0;
	user = SDL_GetPrefPath(config.org_name, config.app_name);
	cwd = SDL_GetCurrentDirectory();
	XvtLog_ShortenHome(shown[0], sizeof(shown[0]), user, g_logSinkHome, g_logSinkHomeLength);
	XvtLog_ShortenHome(shown[1], sizeof(shown[1]), resources, g_logSinkHome, g_logSinkHomeLength);
	XvtLog_ShortenHome(shown[2], sizeof(shown[2]), cwd, g_logSinkHome, g_logSinkHomeLength);
	fprintf(stderr, "= user \"%s\" res \"%s\" cwd \"%s\"\n", shown[0], shown[1], shown[2]);
	SDL_free(user);
	SDL_free(cwd);
}

int XvtLogSink_Install(const XvtLaunchOptions* options) {
	AeronLogLevel level = AERON_LOG_INFO;
	const char* name = options && options->log_level
						   ? options->log_level
						   : SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "OPENXVT_LOG_LEVEL");
	if (name && name[0] && !XvtLog_ParseLevel(name, &level)) {
		fprintf(stderr, "OpenXvT: the log level must be debug, info, warn or error, not '%s'\n", name);
		return 0;
	}
	XvtLog_SetLevel(level);
	XvtLogSink_FindHome();
	SDL_SetLogPriorities(XvtLogSink_Priority(level));
	SDL_SetLogOutputFunction(XvtLogSink_Write, NULL);
	XvtLogSink_WriteHeader(options);
	return 1;
}

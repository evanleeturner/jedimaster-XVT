#include "xvt_app/log_sink.h"

#include "xvt_app/log_file.h"
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
/* The run's log file; XVT_LOG_FILE_NONE when none could be opened, and lines then reach stderr only. */
static XvtLogFileHandle g_logSinkFile = XVT_LOG_FILE_NONE;

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

/* Writes one finished line to stderr and to the run's log file, each in one call. */
static void XvtLogSink_Emit(const char* line, size_t length) {
	fputs(line, stderr);
	XvtLogFile_Write(g_logSinkFile, line, length);
}

static void XvtLogSink_Write(void* userdata, int category, SDL_LogPriority priority, const char* message) {
	char line[XVT_LOG_SINK_LINE_CAPACITY];
	char shortened[XVT_LOG_SINK_LINE_CAPACITY];
	const char* event;
	size_t event_length;
	const char* fields;
	size_t length;
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
	length = XvtLog_FormatLine(line, sizeof(line), XvtLogSink_MsOfDay(), XvtLogSink_Letter(priority), event,
							   event_length, shortened);
	XvtLogSink_Emit(line, length);
}

/* The run id: the low 32 bits of the start time, shared by the header and the log file's name. */
static uint32_t XvtLogSink_RunId(SDL_Time start) { return (uint32_t)((uint64_t)start & 0xffffffffu); }

/* Writes a header line snprintf formatted into line, length being snprintf's result, to stderr and the log
 * file. A line cut by the buffer keeps its newline as its last byte. */
static void XvtLogSink_EmitHeaderLine(char* line, size_t capacity, int length) {
	size_t used;
	if (length <= 0)
		return;
	used = (size_t)length < capacity ? (size_t)length : capacity - 1;
	line[used - 1] = '\n';
	XvtLogSink_Emit(line, used);
}

static void XvtLogSink_WriteHeader(const XvtLaunchOptions* options, SDL_Time start) {
	AeronConfig config;
	SDL_DateTime today = { 0 };
	char resources[XVT_LOG_SINK_PATH_CAPACITY];
	char shown[3][XVT_LOG_SINK_PATH_CAPACITY];
	char line[3 * XVT_LOG_SINK_PATH_CAPACITY + 64];
	char* user;
	char* cwd;
	XvtHostConfig_InitAeron(options, &config);
	SDL_TimeToDateTime(start, &today, false);
	XvtLogSink_EmitHeaderLine(line, sizeof(line),
							  snprintf(line, sizeof(line), "= %s run %08x fmt 1 date %04d-%02d-%02d tz utc\n",
									   config.app_name, (unsigned)XvtLogSink_RunId(start), today.year,
									   today.month, today.day));
	if (!XvtHostConfig_ResolveResourceRoot(options, resources, sizeof(resources)))
		resources[0] = 0;
	user = SDL_GetPrefPath(config.org_name, config.app_name);
	cwd = SDL_GetCurrentDirectory();
	XvtLog_ShortenHome(shown[0], sizeof(shown[0]), user, g_logSinkHome, g_logSinkHomeLength);
	XvtLog_ShortenHome(shown[1], sizeof(shown[1]), resources, g_logSinkHome, g_logSinkHomeLength);
	XvtLog_ShortenHome(shown[2], sizeof(shown[2]), cwd, g_logSinkHome, g_logSinkHomeLength);
	XvtLogSink_EmitHeaderLine(
		line, sizeof(line),
		snprintf(line, sizeof(line), "= user \"%s\" res \"%s\" cwd \"%s\"\n", shown[0], shown[1], shown[2]));
	SDL_free(user);
	SDL_free(cwd);
}

/* Writes the default logs folder into out, the preferences folder's "logs" folder with a trailing
 * separator, and creates it. Returns 0, with the reason in error, when SDL names no preferences folder,
 * the path does not fit or the folder cannot be made. */
static int XvtLogSink_DefaultFolder(const XvtLaunchOptions* options, char* out, size_t capacity, char* error,
									size_t error_capacity) {
	AeronConfig config;
	char* preferences;
	size_t length;
	char separator;
	int written;
	XvtHostConfig_InitAeron(options, &config);
	preferences = SDL_GetPrefPath(config.org_name, config.app_name);
	if (!preferences) {
		snprintf(error, error_capacity, "no preferences folder: %s", SDL_GetError());
		return 0;
	}
	length = strlen(preferences);
	/* SDL ends the preferences path with the system's separator; the logs folder uses the same one. */
	separator = length ? preferences[length - 1] : '/';
	written = snprintf(out, capacity, "%slogs", preferences);
	SDL_free(preferences);
	if (written < 0 || (size_t)written + 1 >= capacity) {
		snprintf(error, error_capacity, "the logs folder path is too long");
		return 0;
	}
	if (!SDL_CreateDirectory(out)) {
		snprintf(error, error_capacity, "cannot make the logs folder: %s", SDL_GetError());
		return 0;
	}
	out[written] = separator;
	out[written + 1] = 0;
	return 1;
}

/* Removes the oldest run logs in folder (a path ending in a separator) so that XVT_LOG_FILE_KEEP remain once
 * this run's file is made. Only names of the run-log shape are touched; a file that cannot be removed is
 * left in place. */
static void XvtLogSink_Prune(const char* folder) {
	int count = 0;
	char** names = SDL_GlobDirectory(folder, "openxvt-*.log", 0, &count);
	size_t expired;
	if (!names)
		return;
	expired = XvtLogFile_SelectExpired((const char**)names, count > 0 ? (size_t)count : 0, XVT_LOG_FILE_KEEP);
	for (size_t i = 0; i < expired; ++i) {
		char path[XVT_LOG_SINK_PATH_CAPACITY];
		int written = snprintf(path, sizeof(path), "%s%s", folder, names[i]);
		if (written > 0 && (size_t)written < sizeof(path))
			SDL_RemovePath(path);
	}
	SDL_free(names);
}

/* Writes the run's log file path into out: the --log-file option, else the OPENXVT_LOG_FILE environment
 * variable when it is nonempty, else a new run name (log_file.h) in the default logs folder, after pruning
 * that folder. Returns 1, or 0 with the reason in error and whatever path is known in out. */
static int XvtLogSink_ChooseFile(const XvtLaunchOptions* options, SDL_Time start, char* out, size_t capacity,
								 char* error, size_t error_capacity) {
	const char* chosen = options && options->log_file
							 ? options->log_file
							 : SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "OPENXVT_LOG_FILE");
	char name[XVT_LOG_FILE_NAME_CAPACITY];
	SDL_DateTime when = { 0 };
	size_t length;
	out[0] = 0;
	if (chosen && chosen[0]) {
		if ((size_t)snprintf(out, capacity, "%s", chosen) < capacity)
			return 1;
		snprintf(error, error_capacity, "the path is too long");
		return 0;
	}
	if (!XvtLogSink_DefaultFolder(options, out, capacity, error, error_capacity))
		return 0;
	XvtLogSink_Prune(out);
	SDL_TimeToDateTime(start, &when, false);
	length = strlen(out);
	if (!XvtLogFile_FormatName(name, sizeof(name), when.year, when.month, when.day, when.hour, when.minute,
							   when.second, XvtLogSink_RunId(start)) ||
		length + strlen(name) >= capacity) {
		snprintf(error, error_capacity, "cannot name the log file");
		return 0;
	}
	memcpy(out + length, name, strlen(name) + 1);
	return 1;
}

int XvtLogSink_Install(const XvtLaunchOptions* options) {
	AeronLogLevel level = AERON_LOG_INFO;
	SDL_Time start = 0;
	char path[XVT_LOG_SINK_PATH_CAPACITY];
	char error[256] = { 0 };
	const char* name = options && options->log_level
						   ? options->log_level
						   : SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "OPENXVT_LOG_LEVEL");
	if (name && name[0] && !XvtLog_ParseLevel(name, &level)) {
		fprintf(stderr, "OpenXvT: the log level must be debug, info, warn or error, not '%s'\n", name);
		return 0;
	}
	XvtLog_SetLevel(level);
	XvtLogSink_FindHome();
	SDL_GetCurrentTime(&start);
	if (XvtLogSink_ChooseFile(options, start, path, sizeof(path), error, sizeof(error)))
		g_logSinkFile = XvtLogFile_Open(path, error, sizeof(error));
	SDL_SetLogPriorities(XvtLogSink_Priority(level));
	SDL_SetLogOutputFunction(XvtLogSink_Write, NULL);
	XvtLogSink_WriteHeader(options, start);
	if (g_logSinkFile != XVT_LOG_FILE_NONE)
		XVT_LOG_INFO("app.log_file path=\"%s\"", path);
	else
		XVT_LOG_WARN("app.log_file_failed path=\"%s\" error=\"%s\"", path, error);
	return 1;
}

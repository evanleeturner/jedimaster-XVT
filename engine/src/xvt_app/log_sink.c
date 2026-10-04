#include "xvt_app/log_sink.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "xvt_app/crash_note.h"
#include "xvt_app/log_file.h"
#include "xvt_runtime/log/log.h"

/* Aeron formats a message into 1,024 bytes; the line adds the stamp, the level and the category. */
#define XVT_LOG_SINK_LINE_CAPACITY 1280
#define XVT_LOG_SINK_PATH_CAPACITY 1024
/* Bytes read from the end of the previous run's log to judge how it ended. */
#define XVT_LOG_SINK_TAIL_CAPACITY 16384

/* The home folder with no trailing separator, found once at install; empty when
 * SDL cannot name it. */
static char g_log_sink_home[XVT_LOG_SINK_PATH_CAPACITY];
static size_t g_log_sink_home_length;
/* The run's log file; XVT_LOG_FILE_NONE when none could be opened, and lines
 * then reach stderr only. */
static xvt_log_file_handle g_log_sink_file = XVT_LOG_FILE_NONE;
static int g_log_sink_installed;

static SDL_LogPriority xvt_log_sink_priority(AeronLogLevel level)
{
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

static char xvt_log_sink_letter(SDL_LogPriority priority)
{
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
static uint32_t xvt_log_sink_ms_of_day(void)
{
	SDL_Time now;
	SDL_DateTime stamp;
	if (!SDL_GetCurrentTime(&now) ||
	    !SDL_TimeToDateTime(now, &stamp, false)) {
		return 0;
	}
	return (uint32_t)stamp.hour * 3600000u +
	       (uint32_t)stamp.minute * 60000u +
	       (uint32_t)stamp.second * 1000u +
	       (uint32_t)(stamp.nanosecond / 1000000);
}

/* Records the home folder for xvt_log_shorten_home; a folder too long for the
 * buffer is left unrecorded. */
static void xvt_log_sink_find_home(void)
{
	const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
	size_t length = home ? strlen(home) : 0;
	while (length > 0 &&
	       (home[length - 1] == '/' || home[length - 1] == '\\')) {
		length--;
	}
	if (length >= sizeof(g_log_sink_home)) {
		length = 0;
	}
	if (length) {
		memcpy(g_log_sink_home, home, length);
	}
	g_log_sink_home[length] = 0;
	g_log_sink_home_length = length;
}

/* Writes one finished line to stderr and to the run's log file, each in one call. */
static void xvt_log_sink_emit(const char *line, size_t length)
{
	fputs(line, stderr);
	xvt_log_file_write(g_log_sink_file, line, length);
}

static void xvt_log_sink_write(void *userdata, int category,
			       SDL_LogPriority priority, const char *message)
{
	(void)userdata;
	if (!message) {
		message = "";
	}
	const char *event;
	size_t event_length;
	const char *fields;
	if (category != SDL_LOG_CATEGORY_APPLICATION ||
	    !xvt_log_split_message(message, &event, &event_length, &fields)) {
		event = "sdl";
		event_length = 3;
		fields = message;
	}
	char shortened[XVT_LOG_SINK_LINE_CAPACITY];
	xvt_log_shorten_home(shortened, sizeof(shortened), fields,
			     g_log_sink_home, g_log_sink_home_length);
	char line[XVT_LOG_SINK_LINE_CAPACITY];
	size_t length = xvt_log_format_line(
		line, sizeof(line), xvt_log_sink_ms_of_day(),
		xvt_log_sink_letter(priority), event, event_length, shortened);
	xvt_log_sink_emit(line, length);
}

/* The run id: the low 32 bits of the start time, shared by the header and the log file's name. */
static uint32_t xvt_log_sink_run_id(SDL_Time start)
{
	return (uint32_t)((uint64_t)start & 0xffffffffu);
}

/* Writes a header line snprintf formatted into line, length being snprintf's
 * result, to stderr and the log file. A line cut by the buffer keeps its
 * newline as its last byte. */
static void xvt_log_sink_emit_header_line(char *line, size_t capacity,
					  int length)
{
	if (length <= 0) {
		return;
	}
	size_t used = (size_t)length < capacity ? (size_t)length : capacity - 1;
	line[used - 1] = '\n';
	xvt_log_sink_emit(line, used);
}

static void xvt_log_sink_write_header(const struct xvt_launch_options *options,
				      SDL_Time start)
{
	AeronConfig config;
	xvt_host_config_fill_aeron_config(options, &config);
	SDL_DateTime today = {0};
	SDL_TimeToDateTime(start, &today, false);
	char line[3 * XVT_LOG_SINK_PATH_CAPACITY + 64];
	xvt_log_sink_emit_header_line(
		line, sizeof(line),
		snprintf(line, sizeof(line),
			 "= %s run %08x fmt 1 date %04d-%02d-%02d tz utc\n",
			 config.app_name, (unsigned)xvt_log_sink_run_id(start),
			 today.year, today.month, today.day));
	char resources[XVT_LOG_SINK_PATH_CAPACITY];
	if (!xvt_host_config_resolve_resource_root(options, resources,
						   sizeof(resources))) {
		resources[0] = 0;
	}
	char *preferences = SDL_GetPrefPath(config.org_name, config.app_name);
	char *cwd = SDL_GetCurrentDirectory();
	char shown[3][XVT_LOG_SINK_PATH_CAPACITY];
	xvt_log_shorten_home(shown[0], sizeof(shown[0]), preferences,
			     g_log_sink_home, g_log_sink_home_length);
	xvt_log_shorten_home(shown[1], sizeof(shown[1]), resources,
			     g_log_sink_home, g_log_sink_home_length);
	xvt_log_shorten_home(shown[2], sizeof(shown[2]), cwd, g_log_sink_home,
			     g_log_sink_home_length);
	xvt_log_sink_emit_header_line(
		line, sizeof(line),
		snprintf(line, sizeof(line),
			 "= user \"%s\" res \"%s\" cwd \"%s\"\n", shown[0],
			 shown[1], shown[2]));
	SDL_free(preferences);
	SDL_free(cwd);
}

/* Writes the default logs folder into out, the preferences folder's "logs"
 * folder with a trailing separator, and creates it. Returns 0, with the reason
 * in error, when SDL names no preferences folder, the path does not fit or the
 * folder cannot be made. */
static int xvt_log_sink_default_folder(const struct xvt_launch_options *options,
				       char *out, size_t capacity, char *error,
				       size_t error_capacity)
{
	AeronConfig config;
	xvt_host_config_fill_aeron_config(options, &config);
	char *preferences = SDL_GetPrefPath(config.org_name, config.app_name);
	if (!preferences) {
		snprintf(error, error_capacity, "no preferences folder: %s",
			 SDL_GetError());
		return 0;
	}
	size_t length = strlen(preferences);
	/* SDL ends the preferences path with the system's separator; the logs
	 * folder uses the same one. */
	char separator = length ? preferences[length - 1] : '/';
	int written = snprintf(out, capacity, "%slogs", preferences);
	SDL_free(preferences);
	if (written < 0 || (size_t)written + 1 >= capacity) {
		snprintf(error, error_capacity,
			 "the logs folder path is too long");
		return 0;
	}
	if (!SDL_CreateDirectory(out)) {
		snprintf(error, error_capacity,
			 "cannot make the logs folder: %s", SDL_GetError());
		return 0;
	}
	out[written] = separator;
	out[written + 1] = 0;
	return 1;
}

/* Removes the oldest run logs in folder (a path ending in a separator) so that
 * XVT_LOG_FILE_KEEP remain once this run's file is made, and writes the path of
 * the newest one into newest ("" when there is none). Only names of the run-log
 * shape are touched; a file that cannot be removed is left in place. */
static void xvt_log_sink_prune(const char *folder, char *newest,
			       size_t capacity)
{
	int count = 0;
	char **names = SDL_GlobDirectory(folder, "openxvt-*.log", 0, &count);
	size_t total = count > 0 ? (size_t)count : 0;
	newest[0] = 0;
	if (!names) {
		return;
	}
	size_t expired = xvt_log_file_select_expired((const char **)names,
						     total, XVT_LOG_FILE_KEEP);
	const char *latest = NULL;
	for (size_t i = 0; i < total; ++i) {
		if (xvt_log_file_is_run_name(names[i])) {
			latest = names[i];
		}
	}
	if (latest && (size_t)snprintf(newest, capacity, "%s%s", folder,
				       latest) >= capacity) {
		newest[0] = 0;
	}
	for (size_t i = 0; i < expired; ++i) {
		char path[XVT_LOG_SINK_PATH_CAPACITY];
		int written =
			snprintf(path, sizeof(path), "%s%s", folder, names[i]);
		if (written > 0 && (size_t)written < sizeof(path)) {
			SDL_RemovePath(path);
		}
	}
	SDL_free(names);
}

/* Writes the run's log file path into out: the --log-file option, else the
 * OPENXVT_LOG_FILE environment variable when it is nonempty, else a new run
 * name (log_file.h) in the default logs folder, after pruning that folder.
 * Writes the previous run's log into previous: the chosen file itself when it
 * was named, the newest run log in the folder otherwise, "" when there is none.
 * Returns 1, or 0 with the reason in error and whatever path is known in out.
 * previous holds capacity bytes, as out does. */
static int xvt_log_sink_choose_file(const struct xvt_launch_options *options,
				    SDL_Time start, char *out, char *previous,
				    size_t capacity, char *error,
				    size_t error_capacity)
{
	const char *chosen =
		options && options->log_file
			? options->log_file
			: SDL_GetEnvironmentVariable(SDL_GetEnvironment(),
						     "OPENXVT_LOG_FILE");
	out[0] = 0;
	previous[0] = 0;
	if (chosen && chosen[0]) {
		if ((size_t)snprintf(out, capacity, "%s", chosen) < capacity) {
			memcpy(previous, out, strlen(out) + 1);
			return 1;
		}
		snprintf(error, error_capacity, "the path is too long");
		return 0;
	}
	if (!xvt_log_sink_default_folder(options, out, capacity, error,
					 error_capacity)) {
		return 0;
	}
	xvt_log_sink_prune(out, previous, capacity);
	SDL_DateTime when = {0};
	SDL_TimeToDateTime(start, &when, false);
	size_t length = strlen(out);
	char name[XVT_LOG_FILE_NAME_CAPACITY];
	if (!xvt_log_file_format_name(name, sizeof(name), when.year, when.month,
				      when.day, when.hour, when.minute,
				      when.second,
				      xvt_log_sink_run_id(start)) ||
	    length + strlen(name) >= capacity) {
		snprintf(error, error_capacity, "cannot name the log file");
		return 0;
	}
	memcpy(out + length, name, strlen(name) + 1);
	return 1;
}

/* Reads up to the last capacity - 1 bytes of the file at path into out,
 * terminated. Returns the bytes read: 0 when the file is missing, empty or
 * unreadable. */
static size_t xvt_log_sink_read_tail(const char *path, char *out,
				     size_t capacity)
{
	SDL_IOStream *stream = path[0] ? SDL_IOFromFile(path, "rb") : NULL;
	out[0] = 0;
	if (!stream) {
		return 0;
	}
	Sint64 size = SDL_GetIOSize(stream);
	size_t read = 0;
	if (size > 0 && SDL_SeekIO(stream,
				   size > (Sint64)(capacity - 1)
					   ? size - (Sint64)(capacity - 1)
					   : 0,
				   SDL_IO_SEEK_SET) >= 0) {
		read = SDL_ReadIO(stream, out, capacity - 1);
	}
	SDL_CloseIO(stream);
	out[read] = 0;
	return read;
}

/* Judges how the run that wrote the log at path ended (log_file.h), writing its
 * last event into last. No log, or an empty one, is NONE. */
static xvt_log_file_ending
xvt_log_sink_judge_previous(const char *path, char *last, size_t capacity)
{
	static char tail[XVT_LOG_SINK_TAIL_CAPACITY];
	size_t length = xvt_log_sink_read_tail(path, tail, sizeof(tail));
	return xvt_log_file_read_ending(tail, length, last, capacity);
}

int xvt_log_sink_install(const struct xvt_launch_options *options)
{
	AeronLogLevel level = AERON_LOG_INFO;
	const char *name =
		options && options->log_level
			? options->log_level
			: SDL_GetEnvironmentVariable(SDL_GetEnvironment(),
						     "OPENXVT_LOG_LEVEL");
	if (name && name[0] && !xvt_log_parse_level(name, &level)) {
		fprintf(stderr,
			"OpenXvT: the log level must be debug, info, warn or error, not '%s'\n",
			name);
		return 0;
	}
	xvt_log_set_level(level);
	xvt_log_sink_find_home();
	SDL_Time start = 0;
	SDL_GetCurrentTime(&start);
	char path[XVT_LOG_SINK_PATH_CAPACITY];
	char previous[XVT_LOG_SINK_PATH_CAPACITY];
	char error[256] = {0};
	int chosen =
		xvt_log_sink_choose_file(options, start, path, previous,
					 sizeof(path), error, sizeof(error));
	char last[64];
	/* The previous run's log is read before this run opens a file, which
	 * may be the same file. */
	xvt_log_file_ending previous_ending =
		xvt_log_sink_judge_previous(previous, last, sizeof(last));
	SDL_SetLogPriorities(xvt_log_sink_priority(level));
	SDL_SetLogOutputFunction(xvt_log_sink_write, NULL);
	if (chosen) {
		g_log_sink_file = xvt_log_file_open(path, error, sizeof(error));
	}
	xvt_crash_note_install(g_log_sink_file);
	xvt_log_sink_write_header(options, start);
	if (g_log_sink_file != XVT_LOG_FILE_NONE) {
		XVT_LOG_INFO("app.log_file path=\"%s\"", path);
	} else {
		XVT_LOG_WARN("app.log_file_failed path=\"%s\" error=\"%s\"",
			     path, error);
	}
	if (previous_ending == XVT_LOG_FILE_ENDING_CRASHED ||
	    previous_ending == XVT_LOG_FILE_ENDING_CUT) {
		XVT_LOG_WARN(
			"app.previous_run ended=\"%s\" last=\"%s\" file=\"%s\"",
			previous_ending == XVT_LOG_FILE_ENDING_CRASHED
				? "crash"
				: "without_stop",
			last, previous);
	}
	g_log_sink_installed = 1;
	return 1;
}

void xvt_log_sink_finish(int exit_code)
{
	if (!g_log_sink_installed) {
		return;
	}
	/* The stop line is written at every level, so a log without one is a
	 * run that did not end this way. */
	if (!xvt_log_enabled(AERON_LOG_INFO)) {
		xvt_log_set_level(AERON_LOG_INFO);
		SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
	}
	XVT_LOG_INFO("app.stop exit=%d", exit_code);
}

/* The build is strict C99; open's O_CLOEXEC is POSIX 2008. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "xvt_app/log_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/* "openxvt-" + YYYYMMDD + "-" + HHMMSS + "-" + run id + ".log": the pattern's characters, where '9'
 * stands for a decimal digit and 'f' for a lowercase hex digit. */
static const char g_run_name_pattern[] = "openxvt-99999999-999999-ffffffff.log";

int xvt_log_file_format_name(char *out, size_t capacity, int year, int month,
			     int day, int hour, int minute, int second,
			     uint32_t run_id)
{
	if (capacity == 0) {
		return 0;
	}
	out[0] = 0;
	if (capacity < XVT_LOG_FILE_NAME_CAPACITY || year < 0 || year > 9999 ||
	    month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 ||
	    hour > 23 || minute < 0 || minute > 59 || second < 0 ||
	    second > 59) {
		return 0;
	}
	snprintf(out, capacity, "openxvt-%04d%02d%02d-%02d%02d%02d-%08x.log",
		 year, month, day, hour, minute, second, (unsigned)run_id);
	return 1;
}

int xvt_log_file_is_run_name(const char *name)
{
	size_t i;
	if (!name) {
		return 0;
	}
	for (i = 0; g_run_name_pattern[i]; ++i) {
		char c = name[i];
		char want = g_run_name_pattern[i];
		int ok;
		if (want == '9') {
			ok = c >= '0' && c <= '9';
		} else if (want == 'f') {
			ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		} else {
			ok = c == want;
		}
		if (!ok) {
			return 0;
		}
	}
	return name[i] == 0;
}

static int xvt_log_file_compare_names(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

size_t xvt_log_file_select_expired(const char **names, size_t count,
				   size_t keep)
{
	size_t runs = 0;
	if (keep == 0) {
		keep = 1;
	}
	for (size_t i = 0; i < count; ++i) {
		if (xvt_log_file_is_run_name(names[i])) {
			const char *name = names[i];
			names[i] = names[runs];
			names[runs++] = name;
		}
	}
	if (runs > 1) {
		qsort(names, runs, sizeof(names[0]),
		      xvt_log_file_compare_names);
	}
	return runs > keep - 1 ? runs - (keep - 1) : 0;
}

/* Returns 1 when the length bytes at line start with the stamp "HH:MM:SS.mmm L ". */
static int xvt_log_file_has_stamp(const char *line, size_t length)
{
	static const char shape[] = "99:99:99.999 L ";
	if (length < sizeof(shape) - 1) {
		return 0;
	}
	for (size_t i = 0; i + 1 < sizeof(shape); ++i) {
		char c = line[i];
		if (shape[i] == '9'   ? !(c >= '0' && c <= '9')
		    : shape[i] == 'L' ? !(c >= 'A' && c <= 'Z')
				      : c != shape[i]) {
			return 0;
		}
	}
	return 1;
}

/* Returns 1 when the length bytes at line are a run's first header line, "= <program> run <id> fmt ...". */
static int xvt_log_file_is_run_header(const char *line, size_t length)
{
	size_t i = 2;
	if (length < 2 || line[0] != '=' || line[1] != ' ') {
		return 0;
	}
	while (i < length && line[i] != ' ') {
		++i;
	}
	return length - i > 5 && !strncmp(line + i, " run ", 5);
}

/* Returns 1 when the stamped line's event, after the 15-byte stamp, is exactly event. */
static int xvt_log_file_event_is(const char *line, size_t length,
				 const char *event)
{
	size_t event_length = strlen(event);
	return length >= 15 + event_length &&
	       !strncmp(line + 15, event, event_length) &&
	       (length == 15 + event_length || line[15 + event_length] == ' ');
}

xvt_log_file_ending xvt_log_file_read_ending(const char *tail, size_t length,
					     char *last_event, size_t capacity)
{
	const char *newest_run = tail;
	const char *last = NULL;
	size_t last_length = 0;
	int header = 0;
	int stopped = 0;
	int crashed = 0;
	/* The newest run starts after the last header line in the text. */
	for (const char *line = tail; line < tail + length;) {
		const char *end =
			memchr(line, '\n', (size_t)(tail + length - line));
		size_t line_length = end ? (size_t)(end - line)
					 : (size_t)(tail + length - line);
		if (xvt_log_file_is_run_header(line, line_length)) {
			newest_run = line;
			header = 1;
		}
		if (!end) {
			break;
		}
		line = end + 1;
	}
	for (const char *line = newest_run; line < tail + length;) {
		const char *end =
			memchr(line, '\n', (size_t)(tail + length - line));
		size_t line_length = end ? (size_t)(end - line)
					 : (size_t)(tail + length - line);
		if (xvt_log_file_has_stamp(line, line_length)) {
			last = line;
			last_length = line_length;
			stopped |= xvt_log_file_event_is(line, line_length,
							 "app.stop");
			crashed |= xvt_log_file_event_is(line, line_length,
							 "app.crash_signal") ||
				   xvt_log_file_event_is(line, line_length,
							 "app.crash_exception");
		}
		if (!end) {
			break;
		}
		line = end + 1;
	}
	if (capacity) {
		size_t used = 0;
		while (last && 15 + used < last_length &&
		       last[15 + used] != ' ' && used + 1 < capacity) {
			last_event[used] = last[15 + used];
			++used;
		}
		last_event[used] = 0;
	}
	if (crashed) {
		return XVT_LOG_FILE_ENDING_CRASHED;
	}
	if (stopped) {
		return XVT_LOG_FILE_ENDING_STOPPED;
	}
	return header || last ? XVT_LOG_FILE_ENDING_CUT
			      : XVT_LOG_FILE_ENDING_NONE;
}

#ifdef _WIN32

static void xvt_log_file_system_error(char *error, size_t error_capacity)
{
	if (error && error_capacity) {
		snprintf(error, error_capacity, "Windows error %lu",
			 (unsigned long)GetLastError());
	}
}

xvt_log_file_handle xvt_log_file_open(const char *path, char *error,
				      size_t error_capacity)
{
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path,
					 -1, NULL, 0);
	wchar_t *wide;
	HANDLE file;
	if (length <= 0) {
		xvt_log_file_system_error(error, error_capacity);
		return XVT_LOG_FILE_NONE;
	}
	wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
	if (!wide) {
		if (error && error_capacity) {
			snprintf(error, error_capacity, "out of memory");
		}
		return XVT_LOG_FILE_NONE;
	}
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide,
			    length);
	/* FILE_APPEND_DATA without FILE_WRITE_DATA makes every write land at the current end of the file. */
	file = CreateFileW(wide, FILE_APPEND_DATA,
			   FILE_SHARE_READ | FILE_SHARE_WRITE |
				   FILE_SHARE_DELETE,
			   NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		xvt_log_file_system_error(error, error_capacity);
	}
	free(wide);
	return file == INVALID_HANDLE_VALUE ? XVT_LOG_FILE_NONE
					    : (xvt_log_file_handle)file;
}

int xvt_log_file_write(xvt_log_file_handle file, const char *data,
		       size_t length)
{
	if (file == XVT_LOG_FILE_NONE) {
		return 0;
	}
	while (length > 0) {
		DWORD chunk =
			length > 0x40000000u ? 0x40000000u : (DWORD)length;
		DWORD written = 0;
		if (!WriteFile((HANDLE)file, data, chunk, &written, NULL) ||
		    written == 0) {
			return 0;
		}
		data += written;
		length -= written;
	}
	return 1;
}

void xvt_log_file_close(xvt_log_file_handle file)
{
	if (file != XVT_LOG_FILE_NONE) {
		CloseHandle((HANDLE)file);
	}
}

#else

xvt_log_file_handle xvt_log_file_open(const char *path, char *error,
				      size_t error_capacity)
{
	int file = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
	if (file < 0) {
		if (error && error_capacity) {
			snprintf(error, error_capacity, "%s", strerror(errno));
		}
		return XVT_LOG_FILE_NONE;
	}
	return (xvt_log_file_handle)file;
}

int xvt_log_file_write(xvt_log_file_handle file, const char *data,
		       size_t length)
{
	if (file == XVT_LOG_FILE_NONE) {
		return 0;
	}
	while (length > 0) {
		ssize_t written = write((int)file, data, length);
		if (written < 0 && errno == EINTR) {
			continue;
		}
		if (written <= 0) {
			return 0;
		}
		data += written;
		length -= (size_t)written;
	}
	return 1;
}

void xvt_log_file_close(xvt_log_file_handle file)
{
	if (file != XVT_LOG_FILE_NONE) {
		close((int)file);
	}
}

#endif

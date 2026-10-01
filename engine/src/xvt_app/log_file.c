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
static const char g_runNamePattern[] = "openxvt-99999999-999999-ffffffff.log";

int XvtLogFile_FormatName(char* out, size_t capacity, int year, int month, int day, int hour, int minute,
						  int second, uint32_t run_id) {
	if (capacity == 0)
		return 0;
	out[0] = 0;
	if (capacity < XVT_LOG_FILE_NAME_CAPACITY || year < 0 || year > 9999 || month < 1 || month > 12 ||
		day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
		second > 59)
		return 0;
	snprintf(out, capacity, "openxvt-%04d%02d%02d-%02d%02d%02d-%08x.log", year, month, day, hour, minute,
			 second, (unsigned)run_id);
	return 1;
}

int XvtLogFile_IsRunName(const char* name) {
	size_t i;
	if (!name)
		return 0;
	for (i = 0; g_runNamePattern[i]; ++i) {
		char c = name[i];
		char want = g_runNamePattern[i];
		int ok;
		if (want == '9')
			ok = c >= '0' && c <= '9';
		else if (want == 'f')
			ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		else
			ok = c == want;
		if (!ok)
			return 0;
	}
	return name[i] == 0;
}

static int XvtLogFile_CompareNames(const void* a, const void* b) {
	return strcmp(*(const char* const*)a, *(const char* const*)b);
}

size_t XvtLogFile_SelectExpired(const char** names, size_t count, size_t keep) {
	size_t runs = 0;
	if (keep == 0)
		keep = 1;
	for (size_t i = 0; i < count; ++i) {
		if (XvtLogFile_IsRunName(names[i])) {
			const char* name = names[i];
			names[i] = names[runs];
			names[runs++] = name;
		}
	}
	if (runs > 1)
		qsort(names, runs, sizeof(names[0]), XvtLogFile_CompareNames);
	return runs > keep - 1 ? runs - (keep - 1) : 0;
}

#ifdef _WIN32

static void XvtLogFile_SystemError(char* error, size_t error_capacity) {
	if (error && error_capacity)
		snprintf(error, error_capacity, "Windows error %lu", (unsigned long)GetLastError());
}

XvtLogFileHandle XvtLogFile_Open(const char* path, char* error, size_t error_capacity) {
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
	wchar_t* wide;
	HANDLE file;
	if (length <= 0) {
		XvtLogFile_SystemError(error, error_capacity);
		return XVT_LOG_FILE_NONE;
	}
	wide = (wchar_t*)malloc((size_t)length * sizeof(wchar_t));
	if (!wide) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "out of memory");
		return XVT_LOG_FILE_NONE;
	}
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length);
	/* FILE_APPEND_DATA without FILE_WRITE_DATA makes every write land at the current end of the file. */
	file = CreateFileW(wide, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
					   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		XvtLogFile_SystemError(error, error_capacity);
	free(wide);
	return file == INVALID_HANDLE_VALUE ? XVT_LOG_FILE_NONE : (XvtLogFileHandle)file;
}

int XvtLogFile_Write(XvtLogFileHandle file, const char* data, size_t length) {
	if (file == XVT_LOG_FILE_NONE)
		return 0;
	while (length > 0) {
		DWORD chunk = length > 0x40000000u ? 0x40000000u : (DWORD)length;
		DWORD written = 0;
		if (!WriteFile((HANDLE)file, data, chunk, &written, NULL) || written == 0)
			return 0;
		data += written;
		length -= written;
	}
	return 1;
}

void XvtLogFile_Close(XvtLogFileHandle file) {
	if (file != XVT_LOG_FILE_NONE)
		CloseHandle((HANDLE)file);
}

#else

XvtLogFileHandle XvtLogFile_Open(const char* path, char* error, size_t error_capacity) {
	int file = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
	if (file < 0) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "%s", strerror(errno));
		return XVT_LOG_FILE_NONE;
	}
	return (XvtLogFileHandle)file;
}

int XvtLogFile_Write(XvtLogFileHandle file, const char* data, size_t length) {
	if (file == XVT_LOG_FILE_NONE)
		return 0;
	while (length > 0) {
		ssize_t written = write((int)file, data, length);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return 0;
		data += written;
		length -= (size_t)written;
	}
	return 1;
}

void XvtLogFile_Close(XvtLogFileHandle file) {
	if (file != XVT_LOG_FILE_NONE)
		close((int)file);
}

#endif

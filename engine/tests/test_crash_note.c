/* Checks the crash note (xvt_app/crash_note.h) against the promises in its header: the line formatter, and
 * real crashes. Each crash case runs in a child process that installs the note, crashes on purpose, and
 * dies; the parent checks how the child ended (the same signal or exception code as without the note)
 * and what it wrote: one first line saying what happened, then frame lines, one of them in this program.
 * The program is built without the sanitizers, which catch these crashes themselves (tests/CMakeLists.txt).
 *
 * Linux and macOS: fork, crash, waitpid. Windows: the program starts itself again with "child <case>
 * <log path>" and reads the exit code, as no fork exists there. */
#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "test_assert.h"
#include "xvt_app/crash_note.h"
#include "xvt_app/log_file.h"

#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PROGRAM_NAME "test_crash_note.exe"
#else
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#define PROGRAM_NAME "test_crash_note"
#endif

/* Where frame 0 is the faulting instruction itself (crash_note.h), a fault in Crash puts frame 0 inside
 * Crash; elsewhere the handler's frames come first and frame 0 is not checked. */
#if (defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))) ||                                 \
	(defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)))
#define FAULT_IN_CRASH 1
#else
#define FAULT_IN_CRASH 0
#endif
/* Bytes from Crash's start that hold its faulting instructions: the function is a few comparisons long. */
#define CRASH_CODE_SPAN 512

/* Formats through XvtCrashNote_FormatLine with a variable argument list, as the note's writer does. */
static size_t Format(char* out, size_t capacity, uint32_t ms, const char* format, ...) {
	va_list args;
	va_start(args, format);
	size_t length = XvtCrashNote_FormatLine(out, capacity, ms, format, args);
	va_end(args);
	return length;
}

static void CheckFormatLine(void) {
	char line[256];
	size_t length = Format(line, sizeof line, 76955005u, "app.crash_signal signal=\"%s\" code=%d addr=%#llx",
						   "SIGSEGV", 1, 0ull);
	XVT_ASSERT_INT_EQ(strcmp(line, "21:22:35.005 C app.crash_signal signal=\"SIGSEGV\" code=1 addr=0x0\n"),
					  0);
	XVT_ASSERT_INT_EQ(length, strlen(line));

	Format(line, sizeof line, 0, "d=%d d=%d u=%u llu=%llu x=%#llx pct=%%", -7, INT_MIN, 4294967295u,
		   18446744073709551615ull, 0xdeadbeefull);
	XVT_ASSERT_INT_EQ(
		strcmp(
			line,
			"00:00:00.000 C d=-7 d=-2147483648 u=4294967295 llu=18446744073709551615 x=0xdeadbeef pct=%\n"),
		0);

	/* Text: line breaks and tabs become spaces; NULL is written as (null). */
	Format(line, sizeof line, 0, "a=\"%s\" b=\"%s\"", "one\ntwo\r\tthree", (const char*)NULL);
	XVT_ASSERT_INT_EQ(strcmp(line, "00:00:00.000 C a=\"one two  three\" b=\"(null)\"\n"), 0);

	/* The time of day wraps at 24 hours. */
	Format(line, sizeof line, 86400000u + 3723004u, "x");
	XVT_ASSERT_INT_EQ(strcmp(line, "01:02:03.004 C x\n"), 0);

	/* An unsupported conversion writes "?" and ends the text. */
	Format(line, sizeof line, 0, "a=%x b=%d", 5u, 6);
	XVT_ASSERT_INT_EQ(strcmp(line, "00:00:00.000 C a=?\n"), 0);

	/* A line too long is cut, keeping its newline and terminator. */
	length = Format(line, 20, 0, "long=%s", "abcdefghij");
	XVT_ASSERT_INT_EQ(length, 19);
	XVT_ASSERT_INT_EQ(strcmp(line, "00:00:00.000 C lon\n"), 0);
	strcpy(line, "x");
	XVT_ASSERT_INT_EQ(Format(line, 1, 0, "a"), 0);
	XVT_ASSERT_INT_EQ(line[0], 0);
	strcpy(line, "x");
	XVT_ASSERT_INT_EQ(Format(line, 0, 0, "a"), 0);
	XVT_ASSERT_INT_EQ(line[0], 'x');
}

/* Crashes the way case names; a case that does not crash returns. */
static void Crash(const char* name) {
	if (!strcmp(name, "segv")) {
		volatile int* volatile target = NULL;
		*target = 1;
	} else if (!strcmp(name, "abort")) {
		abort();
	} else if (!strcmp(name, "divide")) {
		/* Both operands unknown to the compiler: it turns 1 / x into a compare with no division in it. */
		volatile int numerator = 7;
		volatile int zero = 0;
		volatile int quotient = numerator / zero;
		(void)quotient;
	}
#ifndef _WIN32
	else if (!strcmp(name, "kill")) {
		kill(getpid(), SIGSEGV);
	}
#endif
}

/* Returns Crash's offset in this program, as a frame line would give it. */
static unsigned long long CrashOffset(void) {
	void (*function)(const char*) = Crash;
	void* address;
	memcpy(&address, &function, sizeof address);
#ifdef _WIN32
	return (unsigned long long)((uintptr_t)address - (uintptr_t)GetModuleHandleA(NULL));
#else
	Dl_info where;
	XVT_ASSERT_TRUE(dladdr(address, &where) != 0);
	return (unsigned long long)((uintptr_t)address - (uintptr_t)where.dli_fbase);
#endif
}

/* Installs the note on a new log file at path and crashes as name says. */
static void RunChild(const char* name, const char* path) {
	XvtLogFileHandle file = XvtLogFile_Open(path, NULL, 0);
	XVT_ASSERT_TRUE(file != XVT_LOG_FILE_NONE);
	XvtCrashNote_Install(file);
	Crash(name);
	/* A crash that the note swallowed would end here instead. */
	exit(77);
}

static char* ReadAll(const char* path) {
	FILE* file = fopen(path, "rb");
	XVT_ASSERT_TRUE(file != NULL);
	char* text = calloc(1, 1 << 16);
	XVT_ASSERT_TRUE(text != NULL);
	size_t length = fread(text, 1, (1 << 16) - 1, file);
	text[length] = 0;
	fclose(file);
	return text;
}

/* Checks a crashed child's log: the first line starts with first, every later line is a frame line with
 * n counting up from 0, at least two frames, one frame in this program, and, when in_crash is nonzero,
 * frame 0 inside Crash. */
static void CheckNote(const char* path, const char* first, int in_crash) {
	char* text = ReadAll(path);
	char* line = text;
	int frames = 0;
	int ours = 0;
	char* end = strchr(line, '\n');
	XVT_ASSERT_TRUE(end != NULL);
	*end = 0;
	if (strlen(line) < 15 || strncmp(line + 13, first, strlen(first))) {
		fprintf(stderr, "first line: %s\nwanted after the stamp: %s\n", line, first);
		XVT_ASSERT_TRUE(0);
	}
	for (line = end + 1; *line; line = end + 1) {
		char want[64];
		end = strchr(line, '\n');
		XVT_ASSERT_TRUE(end != NULL);
		*end = 0;
		snprintf(want, sizeof want, "C app.crash_frame n=%d module=\"", frames);
		XVT_ASSERT_TRUE(strlen(line) > 13 && !strncmp(line + 13, want, strlen(want)));
		if (strstr(line, "module=\"" PROGRAM_NAME "\" offset=0x"))
			ours++;
		if (frames == 0 && in_crash) {
			const char* offset = strstr(line, "module=\"" PROGRAM_NAME "\" offset=0x");
			unsigned long long at = offset ? strtoull(strstr(offset, "0x") + 2, NULL, 16) : 0;
			if (!offset || at < CrashOffset() || at >= CrashOffset() + CRASH_CODE_SPAN) {
				fprintf(stderr, "frame 0: %s\nwanted inside Crash, from offset 0x%llx\n", line,
						CrashOffset());
				XVT_ASSERT_TRUE(0);
			}
		}
		frames++;
	}
	XVT_ASSERT_TRUE(frames >= 2 && frames <= XVT_CRASH_NOTE_FRAMES);
	XVT_ASSERT_TRUE(ours >= 1);
	free(text);
}

#ifdef _WIN32

/* Starts this program as "child <name> <path>" and returns its exit code. */
static DWORD RunCase(const char* name, const char* path) {
	char program[MAX_PATH];
	char command[3 * MAX_PATH];
	STARTUPINFOA startup;
	PROCESS_INFORMATION process;
	DWORD code = 0;
	XVT_ASSERT_TRUE(GetModuleFileNameA(NULL, program, MAX_PATH) > 0);
	snprintf(command, sizeof command, "\"%s\" child %s \"%s\"", program, name, path);
	memset(&startup, 0, sizeof startup);
	startup.cb = sizeof startup;
	XVT_ASSERT_TRUE(CreateProcessA(program, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process));
	WaitForSingleObject(process.hProcess, INFINITE);
	XVT_ASSERT_TRUE(GetExitCodeProcess(process.hProcess, &code));
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return code;
}

static void CheckCrashes(void) {
	char folder[MAX_PATH];
	char path[MAX_PATH];
	XVT_ASSERT_TRUE(GetTempPathA(MAX_PATH, folder) > 0);
	snprintf(path, sizeof path, "%sopenxvt-crash-%lu.log", folder, (unsigned long)GetCurrentProcessId());

	DeleteFileA(path);
	XVT_ASSERT_INT_EQ(RunCase("segv", path), EXCEPTION_ACCESS_VIOLATION);
	CheckNote(path, "C app.crash_exception code=0xc0000005 addr=0x0", FAULT_IN_CRASH);
	DeleteFileA(path);
	XVT_ASSERT_INT_EQ(RunCase("divide", path), EXCEPTION_INT_DIVIDE_BY_ZERO);
	CheckNote(path, "C app.crash_exception code=0xc0000094 addr=0x", FAULT_IN_CRASH);
	DeleteFileA(path);
	/* The C runtime ends an aborted program with exit code 3. */
	XVT_ASSERT_INT_EQ(RunCase("abort", path), 3);
	CheckNote(path, "C app.crash_signal signal=\"SIGABRT\"", 0);
	DeleteFileA(path);
}

int main(int argc, char* argv[]) {
	if (argc == 4 && !strcmp(argv[1], "child")) {
		/* No error dialog: the parent reads the exit code. */
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		RunChild(argv[2], argv[3]);
	}
	CheckFormatLine();
	CheckCrashes();
	return 0;
}

#else

static void OnPrevious(int signal) {
	(void)signal;
	_exit(42);
}

/* Forks a child that crashes as name says, with its log at path and, when previous_signal is nonzero, a
 * handler of its own for that signal installed before the note; returns the child's wait status. */
static int RunCase(const char* name, const char* path, int previous_signal) {
	int status = 0;
	unlink(path);
	pid_t child = fork();
	XVT_ASSERT_TRUE(child >= 0);
	if (child == 0) {
		if (previous_signal) {
			struct sigaction action;
			memset(&action, 0, sizeof action);
			action.sa_handler = OnPrevious;
			sigemptyset(&action.sa_mask);
			sigaction(previous_signal, &action, NULL);
		}
		RunChild(name, path);
	}
	XVT_ASSERT_INT_EQ(waitpid(child, &status, 0), child);
	return status;
}

/* Checks the child died of signal and wrote a note that starts with first, frame 0 inside Crash when in_crash
 * is nonzero. */
static void CheckDied(int status, int signal, const char* path, const char* first, int in_crash) {
	if (!WIFSIGNALED(status) || WTERMSIG(status) != signal) {
		fprintf(stderr, "child status %d: exited %d, signal %d; wanted signal %d\n", status,
				WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFSIGNALED(status) ? WTERMSIG(status) : -1,
				signal);
		XVT_ASSERT_TRUE(0);
	}
	CheckNote(path, first, in_crash);
}

static void CheckCrashes(void) {
	char path[256];
	const char* folder = getenv("TMPDIR");
	snprintf(path, sizeof path, "%s/openxvt-crash-%ld.log", folder && folder[0] ? folder : "/tmp",
			 (long)getpid());

	/* A fault: the faulting address, then the frames; the program still dies of the same signal. */
	CheckDied(RunCase("segv", path, 0), SIGSEGV, path,
			  "C app.crash_signal signal=\"SIGSEGV\" code=1 addr=0x0", FAULT_IN_CRASH);
#if defined(__x86_64__) || defined(__i386__)
	/* Integer division by zero traps on x86 only; ARM processors return 0. */
	CheckDied(RunCase("divide", path, 0), SIGFPE, path, "C app.crash_signal signal=\"SIGFPE\" code=1 addr=0x",
			  FAULT_IN_CRASH);
#endif
	/* Sent signals do not repeat by themselves; the note sends them again. They carry no address: addr is 0,
	 * not the sender's ids that share its place. */
	CheckDied(RunCase("abort", path, 0), SIGABRT, path, "C app.crash_signal signal=\"SIGABRT\"", 0);
	CheckDied(RunCase("kill", path, 0), SIGSEGV, path,
			  "C app.crash_signal signal=\"SIGSEGV\" code=0 addr=0x0", 0);

	/* A handler installed before the note still runs after it: here it exits with 42. */
	int status = RunCase("segv", path, SIGSEGV);
	XVT_ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 42);
	CheckNote(path, "C app.crash_signal signal=\"SIGSEGV\"", FAULT_IN_CRASH);
	unlink(path);
}

int main(void) {
	CheckFormatLine();
	CheckCrashes();
	return 0;
}

#endif

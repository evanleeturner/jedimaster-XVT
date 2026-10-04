/* The build is strict C99: signal handling, dladdr and clock_gettime need the system's own extensions. */
#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "xvt_app/crash_note.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <ucontext.h>
#endif
#endif

/* Bytes of the stack the signal handler runs on, and the stack Windows keeps back for the filter. */
#define XVT_CRASH_NOTE_STACK 65536
#define XVT_CRASH_NOTE_LINE 512

static xvt_log_file_handle g_crash_note_file = XVT_LOG_FILE_NONE;
static int g_crash_note_installed;
/* Set by the first crash to be noted; a later one writes nothing. */
static volatile sig_atomic_t g_crash_note_written;

/* Appends c to out when there is room below limit. */
static void xvt_crash_note_put(char *out, size_t limit, size_t *used, char c)
{
	if (*used < limit) {
		out[(*used)++] = c;
	}
}

static void xvt_crash_note_put_text(char *out, size_t limit, size_t *used,
				    const char *text)
{
	if (!text) {
		text = "(null)";
	}
	for (; *text; ++text) {
		xvt_crash_note_put(
			out, limit, used,
			(*text == '\n' || *text == '\r' || *text == '\t')
				? ' '
				: *text);
	}
}

/* Appends value in base 10 or 16 (lowercase) with at least min_digits digits. */
static void xvt_crash_note_put_number(char *out, size_t limit, size_t *used,
				      unsigned long long value, unsigned base,
				      int min_digits)
{
	char digits[24];
	int count = 0;
	do {
		digits[count++] = "0123456789abcdef"[value % base];
		value /= base;
	} while (value);
	while (count < min_digits) {
		digits[count++] = '0';
	}
	while (count > 0) {
		xvt_crash_note_put(out, limit, used, digits[--count]);
	}
}

/* Returns 1 when text starts with prefix. */
static int xvt_crash_note_starts_with(const char *text, const char *prefix)
{
	while (*prefix) {
		if (*text++ != *prefix++) {
			return 0;
		}
	}
	return 1;
}

size_t xvt_crash_note_format_line(char *out, size_t capacity,
				  uint32_t ms_of_day, const char *format,
				  va_list args)
{
	if (capacity == 0) {
		return 0;
	}
	if (capacity == 1) {
		out[0] = 0;
		return 0;
	}
	/* Two bytes stay reserved for the newline and the terminator. */
	size_t limit = capacity - 2;
	uint32_t ms = ms_of_day % 86400000u;
	size_t used = 0;
	xvt_crash_note_put_number(out, limit, &used, ms / 3600000u, 10, 2);
	xvt_crash_note_put(out, limit, &used, ':');
	xvt_crash_note_put_number(out, limit, &used, ms / 60000u % 60u, 10, 2);
	xvt_crash_note_put(out, limit, &used, ':');
	xvt_crash_note_put_number(out, limit, &used, ms / 1000u % 60u, 10, 2);
	xvt_crash_note_put(out, limit, &used, '.');
	xvt_crash_note_put_number(out, limit, &used, ms % 1000u, 10, 3);
	xvt_crash_note_put_text(out, limit, &used, " C ");
	for (const char *p = format ? format : ""; *p;) {
		if (*p != '%') {
			xvt_crash_note_put(out, limit, &used, *p++);
			continue;
		}
		++p;
		if (*p == '%') {
			xvt_crash_note_put(out, limit, &used, '%');
			++p;
		} else if (*p == 's') {
			xvt_crash_note_put_text(out, limit, &used,
						va_arg(args, const char *));
			++p;
		} else if (*p == 'd') {
			int value = va_arg(args, int);
			if (value < 0) {
				xvt_crash_note_put(out, limit, &used, '-');
			}
			/* Negating in unsigned arithmetic keeps INT_MIN defined. */
			xvt_crash_note_put_number(
				out, limit, &used,
				value < 0 ? 0ull - (unsigned long long)value
					  : (unsigned long long)value,
				10, 1);
			++p;
		} else if (*p == 'u') {
			xvt_crash_note_put_number(out, limit, &used,
						  va_arg(args, unsigned), 10,
						  1);
			++p;
		} else if (xvt_crash_note_starts_with(p, "llu")) {
			xvt_crash_note_put_number(
				out, limit, &used,
				va_arg(args, unsigned long long), 10, 1);
			p += 3;
		} else if (xvt_crash_note_starts_with(p, "#llx")) {
			xvt_crash_note_put_text(out, limit, &used, "0x");
			xvt_crash_note_put_number(
				out, limit, &used,
				va_arg(args, unsigned long long), 16, 1);
			p += 4;
		} else {
			xvt_crash_note_put(out, limit, &used, '?');
			break;
		}
	}
	out[used++] = '\n';
	out[used] = 0;
	return used;
}

#ifdef _WIN32

static uint32_t xvt_crash_note_ms_of_day(void)
{
	FILETIME now;
	ULARGE_INTEGER filetime_100ns;
	GetSystemTimeAsFileTime(&now);
	filetime_100ns.LowPart = now.dwLowDateTime;
	filetime_100ns.HighPart = now.dwHighDateTime;
	/* FILETIME counts 100 ns steps from midnight UTC, 1 January 1601. */
	return (uint32_t)(filetime_100ns.QuadPart / 10000u % 86400000u);
}

static void xvt_crash_note_output(const char *line, size_t length)
{
	HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
	if (stderr_handle && stderr_handle != INVALID_HANDLE_VALUE) {
		xvt_log_file_write((xvt_log_file_handle)stderr_handle, line,
				   length);
	}
	xvt_log_file_write(g_crash_note_file, line, length);
}

#else

static uint32_t xvt_crash_note_ms_of_day(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
		return 0;
	}
	return (uint32_t)((unsigned long long)now.tv_sec % 86400u * 1000u +
			  (unsigned long long)now.tv_nsec / 1000000u);
}

static void xvt_crash_note_output(const char *line, size_t length)
{
	xvt_log_file_write((xvt_log_file_handle)STDERR_FILENO, line, length);
	xvt_log_file_write(g_crash_note_file, line, length);
}

#endif

void xvt_crash_note_writef(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	char line[XVT_CRASH_NOTE_LINE];
	size_t length = xvt_crash_note_format_line(
		line, sizeof(line), xvt_crash_note_ms_of_day(), format, args);
	va_end(args);
	xvt_crash_note_output(line, length);
}

#ifdef _WIN32

static LPTOP_LEVEL_EXCEPTION_FILTER g_crash_note_previous_filter;

/* Writes one frame line for a code address: the module holding it and the offset into that module. */
static void xvt_crash_note_write_frame(int n, uintptr_t address)
{
	HMODULE module = NULL;
	char name[128] = "?";
	uintptr_t offset = address;
	if (GetModuleHandleExW(
		    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		    (LPCWSTR)address, &module) &&
	    module) {
		wchar_t path[MAX_PATH];
		DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
		if (length > 0 && length < MAX_PATH) {
			const wchar_t *base = path;
			for (const wchar_t *c = path; *c; ++c) {
				if (*c == L'\\' || *c == L'/') {
					base = c + 1;
				}
			}
			if (!WideCharToMultiByte(CP_UTF8, 0, base, -1, name,
						 (int)sizeof(name), NULL,
						 NULL)) {
				strcpy(name, "?");
			}
		}
		offset = address - (uintptr_t)module;
	}
	XVT_LOG_CRASH("app.crash_frame n=%d module=\"%s\" offset=%#llx", n,
		      name, (unsigned long long)offset);
}

/* Writes the frames of the stack context belongs to, the faulting instruction first, by the unwind data
 * x86-64 code carries; other processors get no frame lines. */
static void xvt_crash_note_write_frames(const CONTEXT *start)
{
#if defined(_M_X64) || defined(__x86_64__)
	CONTEXT context = *start;
	for (int n = 0; n < XVT_CRASH_NOTE_FRAMES && context.Rip; ++n) {
		DWORD64 image_base = 0;
		PRUNTIME_FUNCTION function;
		xvt_crash_note_write_frame(n, (uintptr_t)context.Rip);
		function =
			RtlLookupFunctionEntry(context.Rip, &image_base, NULL);
		if (function) {
			PVOID handler_data = NULL;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base,
					 context.Rip, function, &context,
					 &handler_data, &establisher, NULL);
		} else {
			/* A function with no unwind data is a leaf: its return address is on top of the stack. */
			context.Rip = *(DWORD64 *)context.Rsp;
			context.Rsp += 8;
		}
	}
#else
	(void)start;
#endif
}

static LONG WINAPI xvt_crash_note_on_exception(EXCEPTION_POINTERS *info)
{
	if (!g_crash_note_written) {
		const EXCEPTION_RECORD *record = info->ExceptionRecord;
		unsigned long long address =
			(unsigned long long)(uintptr_t)record->ExceptionAddress;
		g_crash_note_written = 1;
		if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
		     record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
		    record->NumberParameters >= 2) {
			address = (unsigned long long)
					  record->ExceptionInformation[1];
		}
		XVT_LOG_CRASH("app.crash_exception code=%#llx addr=%#llx",
			      (unsigned long long)record->ExceptionCode,
			      address);
		xvt_crash_note_write_frames(info->ContextRecord);
	}
	return g_crash_note_previous_filter ? g_crash_note_previous_filter(info)
					    : EXCEPTION_CONTINUE_SEARCH;
}

/* abort() raises SIGABRT; the C runtime resets the action before calling this and ends the program after. */
static void xvt_crash_note_on_abort(int signal)
{
	(void)signal;
	if (!g_crash_note_written) {
		CONTEXT context;
		g_crash_note_written = 1;
		XVT_LOG_CRASH(
			"app.crash_signal signal=\"%s\" code=%d addr=%#llx",
			"SIGABRT", 0, 0ull);
		RtlCaptureContext(&context);
		xvt_crash_note_write_frames(&context);
	}
}

void xvt_crash_note_install(xvt_log_file_handle file)
{
	ULONG spare = XVT_CRASH_NOTE_STACK;
	g_crash_note_file = file;
	if (g_crash_note_installed) {
		return;
	}
	g_crash_note_installed = 1;
	SetThreadStackGuarantee(&spare);
	g_crash_note_previous_filter =
		SetUnhandledExceptionFilter(xvt_crash_note_on_exception);
	signal(SIGABRT, xvt_crash_note_on_abort);
}

#else

/* Returns the part of path after its last '/' or '\\'. */
static const char *xvt_crash_note_base_name(const char *path)
{
	const char *name = path;
	for (; *path; ++path) {
		if (*path == '/' || *path == '\\') {
			name = path + 1;
		}
	}
	return name;
}

static const int g_crash_note_signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL,
					   SIGABRT};
#define XVT_CRASH_NOTE_SIGNAL_COUNT                                            \
	(sizeof(g_crash_note_signals) / sizeof(g_crash_note_signals[0]))
static struct sigaction g_crash_note_previous[XVT_CRASH_NOTE_SIGNAL_COUNT];

static const char *xvt_crash_note_signal_name(int signal)
{
	switch (signal) {
	case SIGSEGV:
		return "SIGSEGV";
	case SIGBUS:
		return "SIGBUS";
	case SIGFPE:
		return "SIGFPE";
	case SIGILL:
		return "SIGILL";
	case SIGABRT:
		return "SIGABRT";
	default:
		return "?";
	}
}

/* Returns 1 when kill, raise or abort sent the signal, 0 when the processor raised it for an instruction,
 * which then runs again when the handler returns. */
static int xvt_crash_note_was_sent(const siginfo_t *info)
{
	if (!info) {
		return 1;
	}
	return info->si_code == SI_USER || info->si_code == SI_QUEUE ||
	       info->si_code <= 0;
}

/* The address of the instruction the signal interrupted, read from the handler's context; 0 where this
 * file does not know the context's layout. */
static uintptr_t xvt_crash_note_interrupted_at(const void *context)
{
#if defined(__linux__) && defined(__x86_64__)
	return context ? (uintptr_t)((const ucontext_t *)context)
				 ->uc_mcontext.gregs[REG_RIP]
		       : 0;
#elif defined(__linux__) && defined(__aarch64__)
	return context ? (uintptr_t)((const ucontext_t *)context)
				 ->uc_mcontext.pc
		       : 0;
#else
	(void)context;
	return 0;
#endif
}

/* Writes the stack's frames from the interrupted instruction at, when the stack holds it; otherwise from
 * the handler's own frames. */
static void xvt_crash_note_write_frames(uintptr_t at)
{
	void *frames[XVT_CRASH_NOTE_FRAMES + 8];
	int count = backtrace(frames, XVT_CRASH_NOTE_FRAMES + 8);
	int first = 0;
	for (int i = 0; at && i < count; ++i) {
		if ((uintptr_t)frames[i] == at) {
			first = i;
			break;
		}
	}
	for (int n = 0; n < XVT_CRASH_NOTE_FRAMES && first + n < count; ++n) {
		Dl_info where;
		const char *module = "?";
		uintptr_t offset = (uintptr_t)frames[first + n];
		if (dladdr(frames[first + n], &where) && where.dli_fname) {
			module = xvt_crash_note_base_name(where.dli_fname);
			offset -= (uintptr_t)where.dli_fbase;
		}
		XVT_LOG_CRASH("app.crash_frame n=%d module=\"%s\" offset=%#llx",
			      n, module, (unsigned long long)offset);
	}
}

static void xvt_crash_note_on_signal(int signal, siginfo_t *info, void *context)
{
	int saved_errno = errno;
	size_t slot = 0;
	while (slot + 1 < XVT_CRASH_NOTE_SIGNAL_COUNT &&
	       g_crash_note_signals[slot] != signal) {
		++slot;
	}
	if (!g_crash_note_written) {
		g_crash_note_written = 1;
		/* A sent signal carries the sender's process and user ids where a fault carries its address. */
		XVT_LOG_CRASH(
			"app.crash_signal signal=\"%s\" code=%d addr=%#llx",
			xvt_crash_note_signal_name(signal),
			info ? info->si_code : 0,
			xvt_crash_note_was_sent(info)
				? 0ull
				: (unsigned long long)(uintptr_t)info->si_addr);
		xvt_crash_note_write_frames(
			xvt_crash_note_interrupted_at(context));
	}
	/* A signal the program ignored before must still end it, or a fault would repeat forever. */
	if (g_crash_note_previous[slot].sa_handler == SIG_IGN &&
	    !(g_crash_note_previous[slot].sa_flags & SA_SIGINFO)) {
		g_crash_note_previous[slot].sa_handler = SIG_DFL;
	}
	sigaction(signal, &g_crash_note_previous[slot], NULL);
	/* The signal is blocked while this runs, so a resent one arrives as the handler returns. */
	if (xvt_crash_note_was_sent(info)) {
		raise(signal);
	}
	errno = saved_errno;
}

void xvt_crash_note_install(xvt_log_file_handle file)
{
	g_crash_note_file = file;
	if (g_crash_note_installed) {
		return;
	}
	g_crash_note_installed = 1;
	void *warm[1];
	/* The first backtrace call may load the unwinder and allocate; do it now, not in the handler. */
	backtrace(warm, 1);
	stack_t stack;
	memset(&stack, 0, sizeof(stack));
	stack.ss_sp = malloc(XVT_CRASH_NOTE_STACK);
	stack.ss_size = XVT_CRASH_NOTE_STACK;
	if (stack.ss_sp) {
		sigaltstack(&stack, NULL);
	}
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = xvt_crash_note_on_signal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	for (size_t i = 0; i < XVT_CRASH_NOTE_SIGNAL_COUNT; ++i) {
		sigaction(g_crash_note_signals[i], &action,
			  &g_crash_note_previous[i]);
	}
}

#endif

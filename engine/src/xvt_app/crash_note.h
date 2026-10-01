#ifndef XVT_APP_CRASH_NOTE_H
#define XVT_APP_CRASH_NOTE_H

#include "xvt_app/log_file.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The crash note: the last lines a crashing run writes, to stderr and to the run's log file, so a crash
 * leaves more than a log that stops. Without it the log of a crashed run simply ends; with it the log
 * ends with one line saying what happened, then one line per frame of the crashed thread's stack.
 *
 * On Linux and macOS a handler runs on SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT, on a stack of its own
 * for the thread that installed it, so a stack overflow on that thread is still reported. It writes
 *   HH:MM:SS.mmm C app.crash_signal signal="SIGSEGV" code=1 addr=0x0
 * (code is the signal's si_code; addr the faulting address it carries, 0 for a signal kill, raise or abort
 * sent, which carries none), then up to XVT_CRASH_NOTE_FRAMES
 *   HH:MM:SS.mmm C app.crash_frame n=0 module="OpenXvT" offset=0x6bae0
 * lines, the module being the file name of the executable or library holding the frame's address and the
 * offset its distance from that module's load address, which a symbolizer turns into a function with the
 * same build (addr2line -f -e OpenXvT 0x6bae0). Frame 0 is the instruction the signal interrupted, on Linux
 * x86-64 and ARM64; every later frame holds a return address, so look its offset up less one. Elsewhere the
 * frames start with the handler's own and the system's signal return. Then the previous action for that
 * signal is restored: a fault the processor raised repeats when the handler returns and reaches that
 * action, and a signal sent by kill or raise is sent again, so the run ends exactly as it would have
 * without the note: the same signal, exit status and core dump, and a sanitizer's own report still follows.
 *
 * On Windows an unhandled exception filter writes app.crash_exception (the exception code and, for an
 * access violation, the address read or written; otherwise the faulting instruction's address) and the
 * frames, walked from the faulting context on x86-64 (frame 0 the faulting instruction, as above), then
 * hands the exception to the filter that was there before, or lets Windows end the program as it would
 * have. SIGABRT, from abort(), is noted with app.crash_signal as on the other systems.
 *
 * The note takes no lock and allocates nothing while it writes: lines are formatted by
 * XvtCrashNote_FormatLine into a buffer on the stack and written with XvtLogFile_Write. Finding the frames
 * may still touch the system's loader, so the first line is written before the frames are looked for.
 * Only the first crash is noted; a second thread crashing at the same moment adds no lines. Stack
 * overflow on another thread is not reported: only the installing thread has the spare stack. No minidump
 * is written. State: the log file handle, the previous actions, and a flag set once a note is written. */

/* Most frames one note writes. */
#define XVT_CRASH_NOTE_FRAMES 32

/* Writes one crash line, built by XvtCrashNote_FormatLine at the current UTC time, to stderr and to the
 * log file given to XvtCrashNote_Install. Call sites use XVT_LOG_CRASH, which tools/log_catalog_check.py
 * holds to the catalog like the other log macros, at level C. */
#if defined(_MSC_VER)
void XvtCrashNote_Writef(const char* format, ...);
#else
__attribute__((format(printf, 1, 2))) void XvtCrashNote_Writef(const char* format, ...);
#endif
#define XVT_LOG_CRASH(...) XvtCrashNote_Writef(__VA_ARGS__)

/* Installs the handlers described above, writing to stderr and to file (XVT_LOG_FILE_NONE: stderr only).
 * Call once, from the thread that starts the program, after the log file is open. A second call changes
 * only the file. */
void XvtCrashNote_Install(XvtLogFileHandle file);

/* Writes one level-C log line into out, as XvtLog_FormatLine would: the time of day as HH:MM:SS.mmm from
 * ms_of_day (wrapped at 24 hours), " C ", the formatted text, a newline and a terminator. format is a
 * restricted printf format: %s (a NULL string writes "(null)"; a newline, carriage return or tab inside
 * it is written as a space), %d, %u, %llu, %#llx ("0x" and lowercase hex digits, "0x0" for zero) and %%.
 * Any other conversion writes "?" and ends the text. A line that does not fit is cut so the newline and
 * terminator still fit; capacity 0 writes nothing and capacity 1 only the terminator. Uses no locale, no
 * lock and no allocation, so a signal handler may call it. Returns the bytes written before the
 * terminator. */
size_t XvtCrashNote_FormatLine(char* out, size_t capacity, uint32_t ms_of_day, const char* format,
							   va_list args);

#ifdef __cplusplus
}
#endif

#endif

#ifndef XVT_APP_LOG_FILE_H
#define XVT_APP_LOG_FILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The run's log file: the name each run's file gets, which old files the logs folder keeps, and the one
 * handle every line is written through. A log written only to stderr is lost whenever nobody captures
 * stderr, which is always the case for the Windows build (a GUI program has no console) and for a player
 * who redirects only stdout. The file is written one line per system call, with no buffer in between, so
 * a crash or a kill loses no line written before it, and lines from different threads never interleave.
 * The open and write calls use the operating system directly (no SDL, no stdio buffer) so the crash
 * handler (crash_note.h) can write through the same handle. Folder listing and removal stay with the
 * log output function (log_sink.c), which has SDL. No state. */

/* Run logs the default logs folder keeps, this run's file included. */
#define XVT_LOG_FILE_KEEP 10
/* "openxvt-YYYYMMDD-HHMMSS-xxxxxxxx.log" and its terminator. */
#define XVT_LOG_FILE_NAME_CAPACITY 37

/* An open log file: a file descriptor, or a Windows HANDLE stored as an integer. */
typedef intptr_t XvtLogFileHandle;
#define XVT_LOG_FILE_NONE ((XvtLogFileHandle)(-1))

/* Writes a run log's name, "openxvt-YYYYMMDD-HHMMSS-xxxxxxxx.log", into out: the run's UTC start date and
 * time, then its run id as eight lowercase hex digits. Names sort in start order to the second. Returns 1;
 * returns 0, out holding "", when a date or time field is out of range (year 0 to 9999, month 1 to 12,
 * day 1 to 31, hour 0 to 23, minute and second 0 to 59) or capacity is under XVT_LOG_FILE_NAME_CAPACITY.
 * Capacity 0 writes nothing. */
int XvtLogFile_FormatName(char* out, size_t capacity, int year, int month, int day, int hour, int minute,
						  int second, uint32_t run_id);
/* Returns 1 when name has exactly the shape XvtLogFile_FormatName writes (digits where it writes digits,
 * lowercase hex for the run id), 0 for anything else, NULL included. Only such files are ever removed. */
int XvtLogFile_IsRunName(const char* name);
/* Reorders names so the run names (XvtLogFile_IsRunName) come first, oldest first, and the other names
 * after them in no particular order. Returns how many run names, counted from the front, to remove so that
 * keep remain once this run's new file is made: the number of run names less (keep - 1), or 0 when that is
 * not positive. keep 0 counts as 1. */
size_t XvtLogFile_SelectExpired(const char** names, size_t count, size_t keep);

/* How the newest run in a log ended, judged from the log's last lines. */
typedef enum XvtLogFileEnding {
	XVT_LOG_FILE_ENDING_NONE = 0, /* no run in the text: no header and no log line */
	XVT_LOG_FILE_ENDING_STOPPED,  /* an app.stop line: the run ended the normal way */
	XVT_LOG_FILE_ENDING_CRASHED,  /* a crash note (crash_note.h) */
	XVT_LOG_FILE_ENDING_CUT /* neither: killed, hung and ended from outside, lost power, or still running */
} XvtLogFileEnding;

/* Judges how the newest run in tail ended. tail is the end of a log, length bytes, and may start part way
 * through a line. Only the lines after its last header line ("= <program> run <id> fmt ...") count, so in
 * a log that several runs appended to only the newest is judged; with no header in tail, every line counts.
 * A line counts when it has the line grammar's stamp, "HH:MM:SS.mmm L ", which a line cut at the start of
 * tail lacks; the last line may lack its newline. A crash note anywhere among the counted lines means
 * CRASHED, even after an app.stop line; otherwise an app.stop line means STOPPED; otherwise a header or any
 * counted line means CUT, and nothing means NONE. Writes the event of the last counted line into last_event,
 * "" when there is none, cut to capacity (0 writes nothing). */
XvtLogFileEnding XvtLogFile_ReadEnding(const char* tail, size_t length, char* last_event, size_t capacity);

/* Opens path, UTF-8, for appending, and creates it when missing: readable and writable by the owner only
 * on POSIX systems, closed in child processes. The folder must exist. Returns the handle, or
 * XVT_LOG_FILE_NONE with the system's reason written into error (cut to error_capacity; NULL or capacity
 * 0 writes none). */
XvtLogFileHandle XvtLogFile_Open(const char* path, char* error, size_t error_capacity);
/* Writes length bytes of data at the end of file, retrying a short or interrupted write until every byte
 * is written or the system refuses. Allocates nothing and takes no lock, so a crash handler may call it.
 * Returns 1 when every byte was written, 0 otherwise or for XVT_LOG_FILE_NONE. */
int XvtLogFile_Write(XvtLogFileHandle file, const char* data, size_t length);
/* Closes file; XVT_LOG_FILE_NONE is ignored. The program keeps its run log open until it exits, so a
 * line written late in shutdown still lands; tests close what they open. */
void XvtLogFile_Close(XvtLogFileHandle file);

#ifdef __cplusplus
}
#endif

#endif

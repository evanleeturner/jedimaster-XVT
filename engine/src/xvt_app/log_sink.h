#ifndef XVT_APP_LOG_SINK_H
#define XVT_APP_LOG_SINK_H

#include "xvt_app/host_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The log output function: the one place log lines take their written form. Install chooses the level
 * (the --log-level option, else the OPENXVT_LOG_LEVEL environment variable, else info), sets it for the
 * log macros and for SDL's own filter so the two agree, points SDL's log output at this file's writer,
 * and writes two header lines that state the run's constants once:
 *   = OpenXvT run <id> fmt 1 date <YYYY-MM-DD> tz utc
 *   = user "<preferences folder>" res "<resource root>" cwd "<working directory>"
 * The run id is eight hex digits taken from the start time. Every later line is
 *   HH:MM:SS.mmm L event key=value ...
 * in UTC. Lines Aeron or SDL write pass through the same function; an SDL line outside the application
 * category gets the event "sdl". Every line and the header's three paths write the user's home folder as
 * ~ (XvtLog_ShortenHome), because on most systems that folder's name is the user's name and a log may be
 * pasted into a public bug report. The preferences folder is SDL's for the host's organization and
 * application names, and SDL creates it when it is missing, as Aeron's file system does later.
 *
 * Each line, header included, is written twice, each time in one call: to stderr, and to the run's log
 * file (log_file.h), so lines from different threads never interleave and a crash loses no line already
 * written. The file is the --log-file option, else the OPENXVT_LOG_FILE environment variable, appended
 * to; else a new file per run, named for the run's start, in the "logs" folder of the preferences folder,
 * where the oldest run logs are removed so the newest XVT_LOG_FILE_KEEP remain. The first line after the
 * header names the file (app.log_file), or says why there is none (app.log_file_failed) and the run goes
 * on with stderr only. The file holds DEBUG lines only when the level asks for them, as stderr does.
 * Install also installs the crash note (crash_note.h) on the same file, so a crash ends the log with what
 * happened and where. State: the home folder and the log file, found once at install; SDL holds the
 * output function, the log header the level. */

/* Installs the writer and opens the log file as described above, and returns 1. Returns 0, with nothing
 * changed, no file opened and a message on stderr, when the option or the environment variable names a
 * level that is not debug, info, warn or error in any letter case. An empty environment value counts as
 * unset, for the level and the file alike. */
int XvtLogSink_Install(const XvtLaunchOptions* options);

#ifdef __cplusplus
}
#endif

#endif

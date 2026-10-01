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
 * in UTC, written to stderr in one call, so lines from different threads never interleave. Lines Aeron
 * or SDL write pass through the same function; an SDL line outside the application category gets the
 * event "sdl". Every line and the header's three paths write the user's home folder as ~
 * (XvtLog_ShortenHome), because on most systems that folder's name is the user's name and a log may be
 * pasted into a public bug report. The preferences folder is SDL's for the host's organization and
 * application names, and SDL creates it when it is missing, as Aeron's file system does later. State:
 * the home folder, found once at install; SDL holds the output function, the log header the level. */

/* Installs the writer as described above and returns 1. Returns 0, with nothing changed and a message on
 * stderr, when the option or the environment variable names a level that is not debug, info, warn or
 * error in any letter case. An empty environment value counts as unset. */
int XvtLogSink_Install(const XvtLaunchOptions* options);

#ifdef __cplusplus
}
#endif

#endif

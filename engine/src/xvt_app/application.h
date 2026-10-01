#ifndef XVT_APP_APPLICATION_H
#define XVT_APP_APPLICATION_H

#include "xvt_app/host_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host program. Run is the whole lifetime: the windowless installation check, or the Aeron session
 * with setup, the settings menu, the modern renderer, CD music and the port, then the frame loop. Each
 * frame: Aeron begins the frame; while the menu is closed, newly connected gamepads are added to the
 * user's controller list in memory and handed to the mapping (a failure is logged as a warning when its
 * message differs from the previous frame's); the menu begins its frame; the debug shortcut toggles
 * Aeron's debug overlay unless the menu captures the keyboard, and its key is then blocked until
 * released; the renderer begins its frame; the port ticks; a
 * settings request from the game opens the menu; the renderer draws; the menu draws unless it opened
 * this frame; the frame is presented (a failure requests a fatal renderer error and ends the loop); then
 * the host sleeps until the sooner of the presentation interval (from Aeron's presentation rate, 60 Hz
 * when that is outside 1 to 1000) and the port's next wake. The loop ends when the port says quit.
 * State: the last controller discovery warning. */

/* First installs the log output function (log_sink.h); a log level that --log-level or the environment
 * names wrongly ends the run with 2 and a message on stderr, before anything else happens.
 * With --check-installation: resolves the resource root (failure: "cannot resolve application
 * resources" on stderr, 1), creates a VFS on it, binds storage and runs setup without a window; a
 * failure prints the setup error to stderr. Shuts config down, unbinds storage, destroys the VFS;
 * returns 0 on success, else 1. No window and no Aeron session.
 * Otherwise: logs app.start with the version, starts Aeron (failure: shut down, 1), binds
 * storage to its VFS and runs setup with the application UI; a cancelled setup exits 0, a failed one
 * logs the error on xvt.setup. Then, in order: the render snapshot store starts; the settings menu
 * starts (a failure, logged on xvt.settings, ends the run); the modern renderer starts (a failure ends
 * the run, unlogged here); CD music is configured from ASSET BalanceOfPower/MUSIC (a failure, logged on
 * xvt.setup, ends the run); skip-intro is set from the option or the setting; the port starts and, when
 * up, logs app.ready and runs the frame loop. Cleanup, in order: the menu flushes for exit and
 * shuts down, then the port, the renderer, the snapshot store and the UI; a nonzero port exit code
 * becomes the result; CD music, config, storage and Aeron shut down; logs app.stop with the exit code.
 * Returns the frame loop's exit code when it ran, 0 for a cancelled setup, 2 for a bad log level,
 * otherwise 1. */
int XvtApplication_Run(const XvtLaunchOptions* options);

#ifdef __cplusplus
}
#endif

#endif

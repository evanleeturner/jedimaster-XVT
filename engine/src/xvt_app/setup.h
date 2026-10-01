#ifndef XVT_APP_SETUP_H
#define XVT_APP_SETUP_H
#include "xvt_app/host_config.h"
#include "xvt_app/ui.h"

#ifdef __cplusplus
extern "C" {
#endif
/* What runs before the host starts: the configuration load, the installation check with its dialogs,
 * the legacy config import and the pilot import. State: the installation path Run mounted. */
typedef enum XvtSetupResult {
	XVT_SETUP_ERROR,
	XVT_SETUP_SUCCESS,
	XVT_SETUP_CANCELLED,
} XvtSetupResult;

/* A NULL UI keeps installation checks windowless. Otherwise setup initializes
 * the application-owned UI for use throughout the rest of the session. */
/* Needs bound storage. Makes USER lookups case-insensitive (failure: ERROR with error unwritten), loads the
 * configuration (a failure that cannot be replaced: ERROR), and with --reset-config replaces the user
 * overrides in memory (failure: ERROR), which forces the save below. With a UI, initializes it with the
 * loaded settings' font, or the defaults' font when the load failed (failure: ERROR). A failed load without a
 * UI is ERROR; with one, the recovery dialog runs, any result but SUCCESS is returned, and its success forces
 * the save. The candidate is --game-data, else the game_data setting; one of 1024 or more characters is
 * ERROR. With --setup, an empty candidate, or one ResolveInstallation rejects: without a UI or with
 * --game-data, ERROR (an empty candidate says to use --game-data with --save-config or the setup window);
 * otherwise the installation dialog runs with the candidate and any result but SUCCESS is returned; the
 * dialog saved the path, so the save below then happens only when --import-config was given, whatever asked
 * for it before. Mounts the selection as ASSET with case-insensitive lookup (failure: ERROR) and records it
 * as Installation. Imports the legacy config, then the pilot, when requested (either failure: ERROR). When a
 * save is due (--save-config, a reset, a recovery, or an import after the dialog), stores game_data and
 * writes the user file (failure: ERROR). With a UI, applies the fullscreen setting (failure: ERROR). Logs the
 * validated installation; SUCCESS. */
XvtSetupResult XvtSetup_Run(const XvtLaunchOptions* options, XvtAppUi* ui, char* error, size_t capacity);
/* Resolves an XvT root or its BalanceOfPower child; output changes only on success.
 * Input and output may share a buffer. */
/* Fails for a NULL or empty path, one of 1024 or more characters, or a NULL or zero-capacity resolved
 * ("Choose an XvT installation folder or its BalanceOfPower folder."). Backslashes become slashes and
 * trailing slashes are dropped, except on "/" and a drive root like "C:/". A scratch VFS (one that
 * cannot be created: "Could not check this installation. Try again.") probes the path: it must be
 * absolute (a leading slash, or a letter and ':'), mount as a case-insensitive ASSET root, hold readable
 * nonempty BalanceOfPower/fronttxt.txt, BalanceOfPower/frontres/top.lst,
 * BalanceOfPower/frontres/campawds.lst and sfx/sfx.lst, hold ivfiles/cal.opt, train/1ta01bf.tie and
 * wave/PBC/Pb1los07.wav under BalanceOfPower/ or directly, and cal.opt must load as a ship model. When
 * that fails and the last path part is "balanceofpower" in any letter case, the parent is probed
 * instead and its error stands. The messages name the offending file or folder. A path that passes but
 * does not fit resolved fails with "Game-data directory is too long.". */
int XvtSetup_ResolveInstallation(const char* path, char* resolved, size_t resolved_capacity, char* error,
								 size_t capacity);
/* The path Run last mounted, even when Run then failed; empty before any. Static storage. */
const char* XvtSetup_Installation(void);
#ifdef __cplusplus
}
#endif

#endif

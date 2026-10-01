#ifndef XVT_APP_SETUP_UI_H
#define XVT_APP_SETUP_UI_H

#include "aeron/scene/ui_file_picker.h"
#include "xvt_app/setup.h"

/* The setup dialogs, each run as its own frame loop before the host starts: configuration recovery
 * (keep the file and quit, or reset to defaults) and installation selection (a path field with a
 * directory picker, Quit and Continue). No state. */

/* Opens, or replaces, a directory picker titled "SELECT XvT INSTALLATION" that accepts only a folder
 * ResolveInstallation accepts, starting at path when it is nonempty. Returns the picker's result. */
int XvtSetupUi_OpenPicker(AeronUiFilePicker* picker, const char* path, char* error, size_t capacity);
/* A NULL path shows configuration recovery; otherwise Continue saves the
 * validated installation. Cancellation never saves the selection. */
/* With a path, creates a picker (failure: "Cannot create installation picker.", ERROR) and first validates a
 * nonempty path in place; a failure shows in the dialog. Shows the host cursor, then each frame until Aeron
 * requests quit or a fatal error: draws the recovery window (NULL path) or the installation window, then the
 * picker, whose selection is resolved into path in place; a frame that cannot be submitted or presented ends
 * the loop with ERROR ("Could not render the installation setup dialog."). The window ends the loop:
 * Continue, enabled once the path is valid, succeeds when the path resolves again and is saved as game_data;
 * Reset to defaults succeeds when the overrides are replaced in memory; Quit, Keep file and quit, and an
 * unconsumed cancel press end it CANCELLED. Each frame then waits about 16.7 ms. After the loop, a fatal host
 * error is ERROR ("Setup interrupted by a fatal host error.") and a quit request is CANCELLED. The picker is
 * destroyed; SUCCESS clears error. */
XvtSetupResult XvtSetupUi_Run(XvtAppUi* ui, char* path, size_t path_capacity, char* error, size_t capacity);

#endif

#ifndef XVT_APP_SETTINGS_H
#define XVT_APP_SETTINGS_H
#include "xvt_app/ui.h"
/* The in-game settings menu: one window with five tabbed pages (Game, Video, Controller, Keyboard,
 * Mouse), opened by the settings shortcut, a gamepad's Start button outside flight, or a request from the
 * game, and closed by Close, an unconsumed cancel or the same Start press. Closing commits the controller
 * and keyboard drafts, the installation page and the video options, then saves the user file; a failure
 * shows the error and keeps the menu open. While open, input is captured from the game and the port
 * pauses unless the network requires progress. One static menu; not thread-safe. */
typedef void (*xvt_settings_page_fn)(AeronUiContext *ui,
				     const AeronInputSnapshot *input);
/* Zeroes the menu, takes ui's context, installs the five page drawers, configures the video options with
 * the modern renderer's apply function and creates the installation page's picker. Returns the picker's
 * success; nothing else can fail. */
bool xvt_settings_menu_init(struct xvt_app_ui *ui, char *error,
			    size_t capacity);
/* Destroys the installation picker, cancels any keyboard or controller capture and zeroes the menu. */
void xvt_settings_menu_shutdown(void);
/* Nothing before a successful Init or after Shutdown. Flushes the video options as exiting (the
 * requested record, not the accepted one); while the menu is open, also commits the keyboard and
 * controller drafts and the installation page; then saves the user file. Each failure is logged on
 * xvt.settings and the rest still runs. */
void xvt_settings_menu_flush_for_exit(void);
/* Replaces the drawer of page 0 to 4 (Game, Video, Controller, Keyboard, Mouse); NULL draws nothing;
 * other pages are ignored. */
void xvt_settings_menu_set_page(int page, xvt_settings_page_fn draw);
/* true while the menu is shown; it does not open it. */
bool xvt_settings_menu_is_open(void);
/* true while open and the last Frame's UI reported a capture owning the whole frame. */
bool xvt_settings_menu_capture_owns_frame(void);
/* true while open and the UI's keyboard capture is active, its completion or cancellation frame
 * included. */
bool xvt_settings_menu_captures_keyboard(void);
/* Opens the menu when there is a context and it is closed: opens the installation page and the
 * controller and keyboard drafts from the current settings, clears the error, captures input from the
 * game, tells the port the settings are open and shows the host cursor. */
void xvt_settings_menu_show(void);
/* Asks an open menu to close at the next BeginFrame; nothing when closed. */
void xvt_settings_menu_request_close(void);
/* true from RequestClose until the close completes or an error is reported. */
bool xvt_settings_menu_close_requested(void);
/* Closes without committing: cancels the keyboard and controller captures and the installation picker,
 * clears the open, close-requested, controller-capture and exit-confirmation flags, and tells the port
 * the settings are closed. */
void xvt_settings_menu_complete_close(void);
/* Shows error in the window until the next Show, and cancels a pending close so the menu stays open. */
void xvt_settings_menu_report_error(const char *error);
/* Once per frame, before the port ticks. While open, adds newly connected gamepads to the controller
 * draft. Installs pending controller options and applies pending video options (a failure is reported).
 * A requested close flushes the video options, commits the controller and keyboard drafts, flushes the
 * installation page and saves the user file, then completes the close; the first failure is reported and
 * the menu stays open. With focus and no game dialog open, a fresh Start press on a gamepad that has a
 * gamepad model toggles the menu (closing an open one; opening a closed one only when no flight is
 * active), unless a close is pending or a controller capture, a keyboard capture or the installation
 * picker is active; with focus alone, a fresh settings shortcut opens a closed menu. Then starts the
 * input-capture frame
 * (captured while the menu was or is open, or without focus) and updates the controller mapping. On the
 * frame the menu closed, the host cursor is hidden when a flight is active and shown otherwise. Returns
 * true when the menu opened this frame. */
bool xvt_settings_menu_begin_frame(const AeronInputSnapshot *input);
/* Takes the port's settings request, if any, and shows the menu; true only when that opened it. */
bool xvt_settings_menu_consume_runtime_request(void);
/* Draws an open menu in a UI frame of input and seconds: the "OpenXvT SETTINGS" window with the page tabs
 * (leaving the Controller or Keyboard tab cancels its captures and modals), the current page, a note
 * while multiplayer continues, the reported error, Close (and on the Game page Exit Game, which confirms
 * before requesting quit), the Controller or Keyboard page's modals, then the installation picker.
 * Records whether the UI captured the whole frame; an unconsumed cancel requests a close. Submits the UI
 * layer. Nothing when closed or input is NULL. */
void xvt_settings_menu_frame(const AeronInputSnapshot *input, float seconds);
#endif

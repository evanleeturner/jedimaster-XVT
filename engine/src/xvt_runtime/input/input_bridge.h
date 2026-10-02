#ifndef XVT_RUNTIME_INPUT_BRIDGE_H
#define XVT_RUNTIME_INPUT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Feeds host input to the original game each host frame. The keyboard goes to the gameplay mapping,
 * to the game's raw keyboard for menus, or nowhere. The frontend gets key states, typed text as
 * Windows-1252, and the mouse in classic 640x480 coordinates. Controllers reach the frontend as one
 * WinMM joystick named "OpenXvT Controllers". */

typedef enum XvtKeyboardRoute {
	XVT_KEYBOARD_RAW,
	XVT_KEYBOARD_GAMEPLAY,
	XVT_KEYBOARD_BLOCKED
} XvtKeyboardRoute;

/* Picks the route: BLOCKED while the last Update suppressed input, the window lacks focus, input is
 * captured or the debug UI shows; GAMEPLAY in a loaded flight with no dialog, movie, resync or open
 * chat; RAW otherwise. A new route flushes the keyboard. Enables the keyboard mapping only for
 * GAMEPLAY. */
XvtKeyboardRoute XvtInput_ReconcileKeyboard(void);
/* After settings load: installs the controller and keyboard bindings, starts with the keyboard
 * suppressed, resets flight controls, and makes the controllers the game's joystick. Does nothing
 * before settings load. */
void XvtInput_Init(void);
/* A frontend frame: routes the keyboard, samples controllers and the joystick, then, unless suppress
 * or the route blocks, fills the frontend's key states (Windows virtual keys), typed Backspace, Tab,
 * Enter and Escape and text (characters outside Windows-1252 are dropped; a full 1024-byte ring drops
 * the oldest), and the mouse inside the classic view. Otherwise clears those and the mouse clicks. */
void XvtInput_Update(int suppress);
/* A flight frame: routes the keyboard and samples controllers and the joystick, suppressing the
 * joystick also without focus or while captured; drops typed characters. */
void XvtInput_UpdateFlight(int suppress);
/* Suspends the keyboard mapping, resets capture, shuts down the controller mapping and flight
 * controls, stops hiding Tab and detaches the joystick. */
void XvtInput_Shutdown(void);
/* 1 at most once per input frame, and only while the window has focus. */
int XvtInput_ConsumeKeyboardReacquire(void);
/* 1 when input is not captured, a render snapshot exists, no text is being entered in it or in a
 * dialog, and no flight chat is open. */
int XvtInput_RendererShortcutAllowed(void);
/* Host-frame frontend coordinates, including game-requested pointer warps. */
/* The frontend's mouse position, as the game last saw it. */
void XvtInput_FrontendCursorPosition(int* x, int* y);

#ifdef __cplusplus
}
#endif

#endif

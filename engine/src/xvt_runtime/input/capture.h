#ifndef XVT_RUNTIME_INPUT_CAPTURE_H
#define XVT_RUNTIME_INPUT_CAPTURE_H
#include "aeron/input.h"
#include <stdbool.h>
/* Who owns the keyboard and mouse: the game, or the host while input is captured (the settings menu,
 * or a window without focus). A key or mouse button held when ownership changes is blocked from the
 * game until it is released, so no press crosses the handover. Also runs mouse capture for mouse
 * flight and its Ctrl+Alt+M release chord. One global state; not thread-safe. */

/* Once per frame: unblocks keys and buttons that are up and were not just released, sets capture, and
 * while captured flushes the keyboard again. */
void XvtInput_BeginCaptureFrame(const AeronInputSnapshot* input, bool capture);
/* On a change only: blocks every key and button now held, ignores this frame's mouse motion, flushes
 * the keyboard, and clears the game's action key, axes, modifiers, mouse buttons and deltas, flight
 * controls and mouse flight state. Capturing also leaves relative mouse mode and suspends the
 * controller mapping. */
void XvtInput_SetCaptured(bool capture);
/* true while input is captured. */
bool XvtInput_IsCaptured(void);
/* true when not captured, the window has focus, and this is not the frame ownership changed. */
bool XvtInput_MouseMotionAllowed(void);
/* buttons without the blocked ones; 0 while captured. */
uint32_t XvtInput_FilterMouseButtons(uint32_t buttons);
/* Hides Tab from the game while suppress is true; the renderer's view mode sets it. */
void XvtInput_SuppressRendererTab(bool suppress);
/* Clears capture, every block, the Tab suppression and the mouse flight state and options, and leaves
 * relative mouse mode. */
void XvtInput_ResetCapture(void);
/* Suspends the keyboard mapping, blocks every held key, and flushes the raw keyboard. */
void XvtInput_FlushKeyboard(void);
/* Drains the DirectInput keyboard buffer and clears the game's shift, ctrl and alt state, its ready and
 * last key, and its character buffer. */
void XvtInput_FlushRawKeyboard(void);
/* Blocks every key held now until it is released. */
void XvtInput_BlockHeldKeys(void);
/* Blocks one key until it is released; a key out of range is ignored. */
void XvtInput_SuppressKey(int key);
/* Once per frame, after settings load: takes up changed mouse settings; resets mouse flight when a
 * flight starts or ends or its control context changes (ship, external camera, map); toggles the
 * release with Ctrl+Alt+M, and recaptures on a click inside the window; switches relative mouse mode to
 * match MouseFlightAllowed, logging and releasing when capture fails; shows the host cursor as needed;
 * then pumps mouse flight. */
void XvtInput_UpdateMouseCapture(const AeronInputSnapshot* input);
/* true when mouse flight is enabled, a flight runs loaded and unpaused, no dialog or debug UI shows,
 * mouse motion is allowed, and the mouse was neither released by the chord nor failed to capture. */
bool XvtInput_MouseFlightAllowed(void);
#endif

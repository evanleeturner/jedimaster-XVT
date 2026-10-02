#ifndef XVT_RUNTIME_INPUT_CONTROLLER_MAPPING_H
#define XVT_RUNTIME_INPUT_CONTROLLER_MAPPING_H
#include "xvt_runtime/input/controller_options.h"
/* Turns connected controllers into flight input, once per input frame. A saved model matches connected
 * controllers by GUID and kind. Its axes are read from one of them, its digital bindings from all. A
 * binding fires on press, but only after it was seen released, so a control held when a controller
 * appears cannot fire; it releases on release. Fire and the target/roll modifier are held buttons
 * (Modifiers). Other actions queue their flight key for ReadKey, only while a flight is active and not
 * loading, and chat send and cancel only while a chat is open. Releasing a numpad view key 1 to 9 queues
 * numpad 8, the forward view, and releasing numpad 0 queues it again. Escape outside chat and dialogs
 * asks the port for settings instead. One global state; not thread-safe. */

/* Clears all state and installs options; invalid options log a warning and leave none installed. */
void XvtControllerMapping_Init(const XvtControllerOptions* options);
/* Keeps options for the next ApplyPending; invalid options log a warning and are dropped. */
void XvtControllerMapping_SetOptions(const XvtControllerOptions* options);
/* Installs the options SetOptions kept, if any. Controllers whose model was removed or changed release
 * their actions; the rest keep their state and axis controller. */
void XvtControllerMapping_ApplyPending(void);
/* Samples one input frame; the same frame again does nothing. Suspends while the window lacks focus,
 * input is captured or the debug UI shows, and resumes otherwise. Releases controllers that went away,
 * then reads each model's axes from the controller it used last, else the lowest instance id, and the
 * bindings of every matching controller. On a controller's first frame, logs a warning for configured
 * controls it lacks. */
void XvtControllerMapping_Update(const AeronInputSnapshot* input);
/* Drops every controller state, held button, axis value and queued key without sending releases; the
 * mapping resumes at the next Update that allows it. */
void XvtControllerMapping_Suspend(void);
/* Clears all state, the installed options included. */
void XvtControllerMapping_Shutdown(void);
/* The installed options, not ones waiting for ApplyPending. */
const XvtControllerOptions* XvtControllerMapping_Options(void);
/* Yaw, pitch or roll from this frame, -127 to 127: 0 inside the deadzone, inverted as configured, and a
 * gamepad's pitch flipped once more, since its Y axis points down. 0 for throttle, an unbound axis, or while
 * suspended. */
int XvtControllerMapping_Axis(XvtInputAxis axis);
/* Bit 1 while fire is held, bit 2 while the target/roll modifier is held, on any controller. */
uint16_t XvtControllerMapping_Modifiers(void);
/* The next queued flight key code, or 0 when the queue is empty. A full queue of 255 keys drops the key
 * with a warning. */
uint16_t XvtControllerMapping_ReadKey(void);
/* Empties the key queue and held buttons without sending releases, and re-arms every binding, so a
 * control still held must be released before it fires again. */
void XvtControllerMapping_DropCommands(void);
/* The connected controller matching model's GUID and kind whose instance id is preferred, else the one
 * with the lowest instance id; NULL when none. */
const AeronControllerSnapshot* XvtControllerMapping_Resolve(const XvtControllerModel* model,
															const AeronInputSnapshot* input,
															uint32_t preferred);
/* The instance id of the controller whose axes that model uses, or 0. */
uint32_t XvtControllerMapping_AnalogInstance(const char* guid);
/* A throttle lever position, 0 to 65535, from a raw axis value: a full axis spans -32768 to 32767, a
 * gamepad trigger 0 to 32767; invert flips it; within 328 of either end it snaps to that end, and the
 * travel between is rescaled to the full range. */
uint16_t XvtControllerMapping_ThrottlePosition(int16_t raw, AeronControllerKind kind, int source,
											   bool invert);
/* Writes the last throttle position and a generation that changes when the throttle's controller or
 * binding changes or the mapping suspends, so a caller knows to reset its baseline. Returns true when a
 * throttle was read this frame. */
bool XvtControllerMapping_ThrottleSample(uint16_t* position, uint32_t* generation);
/* 1 when a configured model had a connected controller this frame, suspended or not. */
int XvtControllerMapping_IsModelConnected(void);
/* As Axis, with only the configured inversion: gamepad pitch is not flipped. */
int XvtControllerMapping_MenuAxis(XvtInputAxis axis);
/* Bit 1 for a gamepad's south button or a joystick's button 0, bit 2 for east or button 1, on any
 * matched controller this frame; a button held since its controller appeared stays hidden until
 * released. */
uint8_t XvtControllerMapping_MenuButtons(void);
/* The direction of the first matched controller that has one this frame: 1 up, 2 right, 4 down, 8 left,
 * from a gamepad's d-pad or a joystick's first hat, with the same hiding as MenuButtons. */
uint8_t XvtControllerMapping_MenuHat(void);
#endif

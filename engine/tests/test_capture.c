/* Checks keyboard and mouse ownership (xvt_runtime/input/capture.h) against the promises in its header:
 * what capturing clears, which keys and buttons stay blocked across a handover and when they unblock,
 * when mouse motion counts, Tab hiding, and the keyboard flushes. The test plays the host: it writes each
 * frame into Aeron's input snapshot, the one the module reads, and sets the game's input globals itself.
 * A key the game cannot see shows as suppressed in Aeron's compatibility layer, which is what the game
 * reads keys through. Every case starts from ResetCapture and an empty frame with focus.
 *
 * Not checked here: UpdateMouseCapture and the true side of MouseFlightAllowed need loaded settings and a
 * running flight, and relative mouse mode needs a window; DirectInput's buffer drain needs its device. */
#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "aeron/input.h"
#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/dinput.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/keyboard_mapping.h"

#include <string.h>

enum { ALL_BUTTONS = 0x1F };

static const int kA = AERON_KEY_A;
static const int kC = AERON_KEY_A + 2;
static const int kD = AERON_KEY_A + 3;
static const int kT = AERON_KEY_A + ('t' - 'a');

/* Aeron's snapshot is the host's frame; the test writes it as the host would. */
static AeronInputSnapshot* Host(void) { return (AeronInputSnapshot*)Aeron_InputSnapshot(); }

/* An empty frame with focus, numbered after the last one. */
static void NewFrame(void) {
	AeronInputSnapshot* host = Host();
	uint64_t frame = host->frame_id;
	memset(host, 0, sizeof *host);
	host->frame_id = frame + 1;
	host->has_focus = 1;
}

/* The next frame with the same keys and buttons held, and nothing just pressed or released. */
static void NextFrame(void) {
	AeronInputSnapshot* host = Host();
	++host->frame_id;
	memset(host->key_released, 0, sizeof host->key_released);
	host->mouse.released_buttons = 0;
}

static void Start(void) {
	XvtInput_ResetCapture();
	NewFrame();
}

static int SuppressedCount(void) {
	int count = 0;
	for (int key = 0; key < AERON_KEY_COUNT; ++key)
		count += AeronCompat_IsKeySuppressed(key) != 0;
	return count;
}

static void SetGameInput(void) {
	g_actionKey = 0x41;
	g_ctrlAxisX = 12;
	g_ctrlAxisY = -12;
	g_keyMods = 3;
	g_mouseButtons = 1;
	g_flightMouseDeltaX = 4;
	g_flightMouseDeltaY = -4;
	g_xvtControlRoll = 9;
}

/* A keyboard mapping that queues next target for T, enabled with no key held. */
static void KeyboardMappingWithT(void) {
	static XvtKeyboardBindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = (uint16_t)kT;
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	XvtKeyboardMapping_Install(&profile);
	XvtKeyboardMapping_Enable(true, Host());
}

static void PressT(void) {
	AeronKeyEvent event;
	memset(&event, 0, sizeof event);
	event.chord.key = (uint16_t)kT;
	event.down = 1;
	XvtKeyboardMapping_Event(&event, false);
}

static void CheckSuppressKey(void) {
	Start();
	XVT_ASSERT_INT_EQ(SuppressedCount(), 0);
	XvtInput_SuppressKey(kA);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kA), 1);
	XVT_ASSERT_INT_EQ(SuppressedCount(), 1);

	/* A key out of range is ignored. */
	XvtInput_SuppressKey(-1);
	XvtInput_SuppressKey(AERON_KEY_COUNT);
	XVT_ASSERT_INT_EQ(SuppressedCount(), 1);

	/* Blocked until released: held, then just released, then up. */
	NextFrame();
	Host()->key_down[kA] = 1;
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kA), 1);
	NextFrame();
	Host()->key_down[kA] = 0;
	Host()->key_released[kA] = 1;
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kA), 1);
	NextFrame();
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kA), 0);
}

static void CheckBlockHeldKeys(void) {
	Start();
	Host()->key_down[kA] = 1;
	XvtInput_BlockHeldKeys();
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kA), 1);
	XVT_ASSERT_INT_EQ(SuppressedCount(), 1);
	NextFrame();
	Host()->key_down[kA] = 0;
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kA), 0);
}

static void CheckCaptureClearsGameInput(void) {
	Start();
	SetGameInput();
	XvtInput_SetCaptured(true);
	XVT_ASSERT_INT_EQ(XvtInput_IsCaptured(), 1);
	XVT_ASSERT_INT_EQ(g_actionKey, 0);
	XVT_ASSERT_INT_EQ(g_ctrlAxisX, 0);
	XVT_ASSERT_INT_EQ(g_ctrlAxisY, 0);
	XVT_ASSERT_INT_EQ(g_keyMods, 0);
	XVT_ASSERT_INT_EQ(g_mouseButtons, 0);
	XVT_ASSERT_INT_EQ(g_flightMouseDeltaX, 0);
	XVT_ASSERT_INT_EQ(g_flightMouseDeltaY, 0);
	XVT_ASSERT_INT_EQ(g_xvtControlRoll, 0);

	/* Releasing is a change too, and clears again. */
	SetGameInput();
	XvtInput_SetCaptured(false);
	XVT_ASSERT_INT_EQ(XvtInput_IsCaptured(), 0);
	XVT_ASSERT_INT_EQ(g_actionKey, 0);
	XVT_ASSERT_INT_EQ(g_ctrlAxisX, 0);
	XVT_ASSERT_INT_EQ(g_keyMods, 0);

	/* No change does nothing: the game's input and the held key stay as they are. */
	SetGameInput();
	Host()->key_down[kD] = 1;
	XvtInput_SetCaptured(false);
	XVT_ASSERT_INT_EQ(g_actionKey, 0x41);
	XVT_ASSERT_INT_EQ(g_xvtControlRoll, 9);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kD), 0);
}

static void CheckHandoverBlocksHeldInput(void) {
	Start();
	/* While captured the host owns the keyboard and mouse: the game sees no key and no button. */
	XvtInput_BeginCaptureFrame(Host(), true);
	for (int key = 1; key < AERON_KEY_COUNT; ++key)
		if (AeronKey_Name((AeronKey)key)[0] != '\0')
			XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(key), 1);
	XVT_ASSERT_INT_EQ(XvtInput_FilterMouseButtons(ALL_BUTTONS), 0);
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 0);

	/* Handing back with C and the left button held blocks both, and this frame's motion. */
	NextFrame();
	Host()->key_down[kC] = 1;
	Host()->mouse.buttons = AERON_MOUSE_BUTTON_LEFT;
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(XvtInput_IsCaptured(), 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kC), 1);
	XVT_ASSERT_INT_EQ(SuppressedCount(), 1);
	XVT_ASSERT_INT_EQ(XvtInput_FilterMouseButtons(ALL_BUTTONS), ALL_BUTTONS & ~AERON_MOUSE_BUTTON_LEFT);
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 0);

	/* Still held next frame: still blocked, but motion counts again. */
	NextFrame();
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kC), 1);
	XVT_ASSERT_INT_EQ(XvtInput_FilterMouseButtons(ALL_BUTTONS), ALL_BUTTONS & ~AERON_MOUSE_BUTTON_LEFT);
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 1);

	/* Just released: still blocked. Up a frame later: both reach the game again. */
	NextFrame();
	Host()->key_down[kC] = 0;
	Host()->key_released[kC] = 1;
	Host()->mouse.buttons = 0;
	Host()->mouse.released_buttons = AERON_MOUSE_BUTTON_LEFT;
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kC), 1);
	XVT_ASSERT_INT_EQ(XvtInput_FilterMouseButtons(ALL_BUTTONS), ALL_BUTTONS & ~AERON_MOUSE_BUTTON_LEFT);
	NextFrame();
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kC), 0);
	XVT_ASSERT_INT_EQ(XvtInput_FilterMouseButtons(ALL_BUTTONS), ALL_BUTTONS);
}

static void CheckCaptureBlocksHeldInput(void) {
	/* D and the right button, held through capture and its release, stay blocked from the game. */
	Start();
	Host()->key_down[kD] = 1;
	Host()->mouse.buttons = AERON_MOUSE_BUTTON_RIGHT;
	XvtInput_SetCaptured(true);
	XvtInput_SetCaptured(false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kD), 1);
	XVT_ASSERT_INT_EQ(XvtInput_FilterMouseButtons(ALL_BUTTONS), ALL_BUTTONS & ~AERON_MOUSE_BUTTON_RIGHT);
}

static void CheckMouseMotionAllowed(void) {
	Start();
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 1);
	Host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 0);
	Host()->has_focus = 1;
	XvtInput_SetCaptured(true);
	NextFrame();
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 0);
	XvtInput_SetCaptured(false);
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 0);
	NextFrame();
	XVT_ASSERT_INT_EQ(XvtInput_MouseMotionAllowed(), 1);
}

static void CheckCaptureSuspendsControllers(void) {
	Start();
	XvtControllerOptions options;
	memset(&options, 0, sizeof options);
	XvtControllerModel* model = &options.models[0];
	memcpy(model->guid, "0123456789abcdef0123456789abcdea", sizeof model->guid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	XvtControllerOptions_ClearProfile(&model->profile, AERON_CONTROLLER_KIND_GAMEPAD);
	model->profile.mapping.axes[XVT_INPUT_AXIS_YAW].source = AERON_GAMEPAD_AXIS_LEFTX;
	options.count = 1;
	XvtControllerMapping_Init(&options);

	AeronControllerSnapshot* pad = &Host()->controllers[0];
	pad->connected = 1;
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	pad->instance_id = 5;
	memcpy(pad->guid, model->guid, sizeof pad->guid);
	pad->gamepad_available_axes = 1u << AERON_GAMEPAD_AXIS_LEFTX;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = 16384;
	XvtControllerMapping_Update(Host());
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW) != 0);

	XvtInput_SetCaptured(true);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), 0);
	XvtControllerMapping_Shutdown();
}

static void CheckRendererTab(void) {
	Start();
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 0);
	XvtInput_SuppressRendererTab(true);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 1);
	XVT_ASSERT_INT_EQ(SuppressedCount(), 1);

	/* It stays hidden across frames while suppress is true. */
	NextFrame();
	XvtInput_BeginCaptureFrame(Host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 1);
	XvtInput_SuppressRendererTab(false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 0);
}

static void CheckResetCapture(void) {
	Start();
	Host()->key_down[kA] = 1;
	Host()->mouse.buttons = AERON_MOUSE_BUTTON_LEFT;
	XvtInput_SetCaptured(true);
	XvtInput_SuppressRendererTab(true);
	XvtInput_SuppressKey(kD);

	XvtInput_ResetCapture();
	XVT_ASSERT_INT_EQ(XvtInput_IsCaptured(), 0);
	XVT_ASSERT_INT_EQ(SuppressedCount(), 0);
	XVT_ASSERT_INT_EQ(XvtInput_FilterMouseButtons(ALL_BUTTONS), ALL_BUTTONS);
}

static void PutChar(char c) {
	g_frontState.charRingBuffer[g_frontState.charWriteIdx] = c;
	g_frontState.charWriteIdx = (g_frontState.charWriteIdx + 1) % 1024;
}

static void SetRawKeyboard(void) {
	Keyboard_FlushCharBuffer();
	g_dinputShiftDown = g_dinputCtrlDown = g_dinputAltDown = 1;
	g_keyReady = 1;
	g_lastKeyCode = 0x1E;
	PutChar('x');
}

static void CheckFlushRawKeyboard(void) {
	Start();
	XVT_ASSERT_TRUE(g_dinputKeyboardDevice == NULL);
	SetRawKeyboard();
	XVT_ASSERT_TRUE(Keyboard_PeekChar() == 'x');
	XvtInput_FlushRawKeyboard();
	XVT_ASSERT_INT_EQ(g_dinputShiftDown, 0);
	XVT_ASSERT_INT_EQ(g_dinputCtrlDown, 0);
	XVT_ASSERT_INT_EQ(g_dinputAltDown, 0);
	XVT_ASSERT_INT_EQ(g_keyReady, 0);
	XVT_ASSERT_INT_EQ(g_lastKeyCode, 0);
	XVT_ASSERT_INT_EQ(Keyboard_PeekChar(), 0);
}

static void CheckFlushKeyboard(void) {
	Start();
	KeyboardMappingWithT();
	PressT();
	Host()->key_down[kD] = 1;
	SetRawKeyboard();

	XvtInput_FlushKeyboard();
	/* The mapping is suspended: its queue is empty and it takes no new presses. */
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	PressT();
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(kD), 1);
	XVT_ASSERT_INT_EQ(g_keyReady, 0);
	XVT_ASSERT_INT_EQ(g_lastKeyCode, 0);
	XVT_ASSERT_INT_EQ(Keyboard_PeekChar(), 0);
	XvtKeyboardMapping_Suspend();
}

static void CheckCaptureFlushesKeyboard(void) {
	/* Taking capture flushes the keyboard. */
	Start();
	KeyboardMappingWithT();
	PressT();
	SetRawKeyboard();
	XvtInput_SetCaptured(true);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	XVT_ASSERT_INT_EQ(g_keyReady, 0);
	XVT_ASSERT_INT_EQ(Keyboard_PeekChar(), 0);

	/* Each captured frame flushes it again. */
	NextFrame();
	SetRawKeyboard();
	XvtInput_BeginCaptureFrame(Host(), true);
	XVT_ASSERT_INT_EQ(g_keyReady, 0);
	XVT_ASSERT_INT_EQ(g_lastKeyCode, 0);
	XVT_ASSERT_INT_EQ(Keyboard_PeekChar(), 0);
	XvtKeyboardMapping_Suspend();
}

static void CheckMouseFlightNeedsFlight(void) {
	Start();
	XVT_ASSERT_INT_EQ(XvtInput_MouseFlightAllowed(), 0);
}

int main(void) {
	CheckSuppressKey();
	CheckBlockHeldKeys();
	CheckCaptureClearsGameInput();
	CheckHandoverBlocksHeldInput();
	CheckCaptureBlocksHeldInput();
	CheckMouseMotionAllowed();
	CheckCaptureSuspendsControllers();
	CheckRendererTab();
	CheckResetCapture();
	CheckFlushRawKeyboard();
	CheckFlushKeyboard();
	CheckCaptureFlushesKeyboard();
	CheckMouseFlightNeedsFlight();
	XvtInput_ResetCapture();
	return 0;
}

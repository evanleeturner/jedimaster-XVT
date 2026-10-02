/* Checks the host-to-game input bridge (xvt_runtime/input/input_bridge.h) against the promises in its
 * header: how the keyboard route is picked and what a new route does, the frontend's key states, typed
 * keys and Windows-1252 text, the frontend mouse, what a suppressed frame clears, keyboard reacquisition,
 * and Init and Shutdown. The test plays the host by writing each frame into Aeron's input snapshot, and
 * reads the frontend's state where the original game reads it. No flight runs and no settings are loaded.
 *
 * Not checked here: the GAMEPLAY route needs a loaded flight; RendererShortcutAllowed's true side needs a
 * committed render snapshot; the frontend joystick needs Init, which needs loaded settings. */
#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/input_bridge.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

#include <string.h>

/* Windows virtual-key codes. */
enum {
	VK_BACK = 0x08,
	VK_TAB = 0x09,
	VK_RETURN = 0x0D,
	VK_SHIFT = 0x10,
	VK_ESCAPE = 0x1B,
	VK_F1 = 0x70,
	VK_LSHIFT = 0xA0,
};

static AeronInputSnapshot *Host(void)
{
	return (AeronInputSnapshot *)Aeron_InputSnapshot();
}

/* An empty frame with focus, numbered after the last one. */
static void NewFrame(void)
{
	AeronInputSnapshot *host = Host();
	uint64_t frame = host->frame_id;
	memset(host, 0, sizeof *host);
	host->frame_id = frame + 1;
	host->has_focus = 1;
}

/* Nothing captured, blocked or mapped, an empty frontend, and an empty frame with focus. */
static void Start(void)
{
	XvtInput_ResetCapture();
	XvtKeyboardMapping_Suspend();
	XvtControllerMapping_Shutdown();
	Keyboard_FlushCharBuffer();
	memset(g_frontState.keyState, 0, sizeof g_frontState.keyState);
	g_frontState.mouseX = g_frontState.mouseY = 0;
	g_frontState.mouseLeftDown = g_frontState.mouseRightDown = 0;
	g_frontState.mouseLeftClickLatch = g_frontState.mouseRightClickLatch =
		0;
	NewFrame();
}

static void SetText(const char *text)
{
	size_t length = strlen(text);
	XVT_ASSERT_TRUE(length <= AERON_TEXT_INPUT_CAPACITY);
	memcpy(Host()->text, text, length);
	Host()->text_length = (uint32_t)length;
}

/* Reads the frontend's typed characters into out; returns how many there were. */
static size_t ReadTyped(unsigned char *out, size_t capacity)
{
	size_t count = 0;
	while (g_frontState.charReadIdx != g_frontState.charWriteIdx) {
		unsigned char c = (unsigned char)Keyboard_DequeueChar();
		if (count < capacity) {
			out[count] = c;
		}
		++count;
	}
	return count;
}

static void CheckRoute(void)
{
	Start();
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_RAW);

	Host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_BLOCKED);
	Host()->has_focus = 1;
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_RAW);

	XvtInput_SetCaptured(true);
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_BLOCKED);
	XvtInput_SetCaptured(false);
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_RAW);

	/* A suppressed Update blocks the route until an Update that is not. */
	XvtInput_Update(1);
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_BLOCKED);
	NewFrame();
	XvtInput_Update(0);
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_RAW);
}

static void CheckNewRouteFlushes(void)
{
	Start();
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_RAW);
	Host()->key_down[AERON_KEY_A] = 1;
	g_keyReady = 1;
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_A), 0);

	/* Losing focus is a new route: the held key is blocked and the raw keyboard flushed. */
	Host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_BLOCKED);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_A), 1);
	XVT_ASSERT_INT_EQ(g_keyReady, 0);
}

static void CheckMappingOnlyForGameplay(void)
{
	/* The route settles on RAW first, so the next call is no new route and flushes nothing. */
	Start();
	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_RAW);
	static XvtKeyboardBindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = AERON_KEY_A + ('t' - 'a');
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	XvtKeyboardMapping_Install(&profile);
	XvtKeyboardMapping_Enable(true, Host());

	XVT_ASSERT_INT_EQ(XvtInput_ReconcileKeyboard(), XVT_KEYBOARD_RAW);
	AeronKeyEvent press;
	memset(&press, 0, sizeof press);
	press.chord.key = AERON_KEY_A + ('t' - 'a');
	press.down = 1;
	XvtKeyboardMapping_Event(&press, false);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
}

static void CheckCanReacquireKeyboard(void)
{
	Start();
	XVT_ASSERT_INT_EQ(XvtInput_ConsumeKeyboardReacquire(), 1);
	XVT_ASSERT_INT_EQ(XvtInput_ConsumeKeyboardReacquire(), 0);
	NewFrame();
	Host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(XvtInput_ConsumeKeyboardReacquire(), 0);
	Host()->has_focus = 1;
	XVT_ASSERT_INT_EQ(XvtInput_ConsumeKeyboardReacquire(), 1);
	XVT_ASSERT_INT_EQ(XvtInput_ConsumeKeyboardReacquire(), 0);
}

static void CheckFrontendKeyStates(void)
{
	Start();
	Host()->key_down[AERON_KEY_A] = 1;
	Host()->key_down[AERON_KEY_1] = 1;
	Host()->key_down[AERON_KEY_1 + 9] = 1;
	Host()->key_down[AERON_KEY_F1] = 1;
	Host()->key_down[AERON_KEY_RETURN] = 1;
	Host()->key_down[AERON_KEY_LSHIFT] = 1;
	XvtInput_Update(0);
	XVT_ASSERT_TRUE(g_frontState.keyState['A'] & 0x80);
	XVT_ASSERT_TRUE(g_frontState.keyState['1'] & 0x80);
	XVT_ASSERT_TRUE(g_frontState.keyState['0'] & 0x80);
	XVT_ASSERT_TRUE(g_frontState.keyState[VK_F1] & 0x80);
	XVT_ASSERT_TRUE(g_frontState.keyState[VK_RETURN] & 0x80);
	XVT_ASSERT_TRUE(g_frontState.keyState[VK_LSHIFT] & 0x80);
	XVT_ASSERT_TRUE(g_frontState.keyState[VK_SHIFT] & 0x80);
	XVT_ASSERT_INT_EQ(g_frontState.keyState['B'], 0);
	XVT_ASSERT_INT_EQ(g_frontState.keyState['2'], 0);

	/* A key let go is up in the next frame's states. */
	NewFrame();
	XvtInput_Update(0);
	XVT_ASSERT_INT_EQ(g_frontState.keyState['A'], 0);
	XVT_ASSERT_INT_EQ(g_frontState.keyState[VK_SHIFT], 0);
}

static void CheckTypedControlKeys(void)
{
	Start();
	Host()->key_typed[AERON_KEY_BACKSPACE] = 2;
	Host()->key_typed[AERON_KEY_TAB] = 1;
	Host()->key_typed[AERON_KEY_RETURN] = 1;
	Host()->key_typed[AERON_KEY_ESCAPE] = 1;
	XvtInput_Update(0);
	unsigned char typed[16];
	size_t count = ReadTyped(typed, sizeof typed);
	XVT_ASSERT_INT_EQ(count, 5);
	int seen[256] = {0};
	for (size_t i = 0; i < count; ++i) {
		++seen[typed[i]];
	}
	XVT_ASSERT_INT_EQ(seen[VK_BACK], 2);
	XVT_ASSERT_INT_EQ(seen[VK_TAB], 1);
	XVT_ASSERT_INT_EQ(seen[VK_RETURN], 1);
	XVT_ASSERT_INT_EQ(seen[VK_ESCAPE], 1);
}

static void CheckTextWindows1252(void)
{
	Start();
	/* a, e acute (U+00E9), the euro sign (U+20AC, 0x80 in Windows-1252), a four-byte emoji, A with
	 * macron (U+0100, not in Windows-1252), then z. */
	SetText("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xC4\x80z");
	XvtInput_Update(0);
	unsigned char typed[16];
	size_t count = ReadTyped(typed, sizeof typed);
	XVT_ASSERT_INT_EQ(count, 4);
	XVT_ASSERT_INT_EQ(typed[0], 'a');
	XVT_ASSERT_INT_EQ(typed[1], 0xE9);
	XVT_ASSERT_INT_EQ(typed[2], 0x80);
	XVT_ASSERT_INT_EQ(typed[3], 'z');
}

static void CheckTextRingDropsOldest(void)
{
	Start();

	enum { TYPED = 1500, PER_FRAME = 750 };

	char text[PER_FRAME + 1];
	for (int frame = 0; frame < TYPED / PER_FRAME; ++frame) {
		for (int i = 0; i < PER_FRAME; ++i) {
			text[i] = (char)('A' + (frame * PER_FRAME + i) % 26);
		}
		text[PER_FRAME] = '\0';
		NewFrame();
		SetText(text);
		XvtInput_Update(0);
	}
	static unsigned char typed[TYPED];
	size_t count = ReadTyped(typed, sizeof typed);
	XVT_ASSERT_TRUE(count >= 1023 && count <= 1024);
	/* What is left is the newest characters, in order. */
	for (size_t i = 0; i < count; ++i) {
		XVT_ASSERT_INT_EQ(typed[i], 'A' + (TYPED - count + i) % 26);
	}
}

static void CheckFrontendMouse(void)
{
	Start();
	Host()->window_width = 640;
	Host()->window_height = 480;
	Host()->mouse.raw_x = 100;
	Host()->mouse.raw_y = 50;
	Host()->mouse.inside_content = 1;
	XvtInput_Update(0);
	int x = -1, y = -1;
	XvtInput_FrontendCursorPosition(&x, &y);
	XVT_ASSERT_INT_EQ(x, 100);
	XVT_ASSERT_INT_EQ(y, 50);

	/* A window twice the classic size: the same spot in classic coordinates. */
	NewFrame();
	Host()->window_width = 1280;
	Host()->window_height = 960;
	Host()->mouse.raw_x = 300;
	Host()->mouse.raw_y = 140;
	Host()->mouse.inside_content = 1;
	XvtInput_Update(0);
	XvtInput_FrontendCursorPosition(&x, &y);
	XVT_ASSERT_INT_EQ(x, 150);
	XVT_ASSERT_INT_EQ(y, 70);

	/* Outside the classic view the frontend keeps the last position. */
	NewFrame();
	Host()->window_width = 640;
	Host()->window_height = 480;
	Host()->mouse.raw_x = 10;
	Host()->mouse.raw_y = 10;
	Host()->mouse.inside_content = 0;
	XvtInput_Update(0);
	XvtInput_FrontendCursorPosition(&x, &y);
	XVT_ASSERT_INT_EQ(x, 150);
	XVT_ASSERT_INT_EQ(y, 70);
}

/* Held A, typed text, and pending clicks in the frontend. An empty frame first settles the route, since a
 * new route blocks keys already held. */
static void FillFrontend(void)
{
	XvtInput_Update(0);
	NewFrame();
	Host()->key_down[AERON_KEY_A] = 1;
	SetText("hi");
	XvtInput_Update(0);
	XVT_ASSERT_TRUE(g_frontState.keyState['A'] & 0x80);
	XVT_ASSERT_TRUE(Keyboard_PeekChar() == 'h');
	g_frontState.mouseLeftDown = g_frontState.mouseRightDown = 1;
	g_frontState.mouseLeftClickLatch = g_frontState.mouseRightClickLatch =
		1;
}

static void AssertFrontendCleared(void)
{
	for (int vk = 0; vk < 256; ++vk) {
		XVT_ASSERT_INT_EQ(g_frontState.keyState[vk], 0);
	}
	XVT_ASSERT_INT_EQ(Keyboard_PeekChar(), 0);
	XVT_ASSERT_INT_EQ(g_frontState.mouseLeftDown, 0);
	XVT_ASSERT_INT_EQ(g_frontState.mouseRightDown, 0);
	XVT_ASSERT_INT_EQ(g_frontState.mouseLeftClickLatch, 0);
	XVT_ASSERT_INT_EQ(g_frontState.mouseRightClickLatch, 0);
}

static void CheckSuppressedFrameClears(void)
{
	Start();
	FillFrontend();
	NewFrame();
	Host()->key_down[AERON_KEY_A] = 1;
	SetText("x");
	XvtInput_Update(1);
	AssertFrontendCleared();

	/* A blocked route clears the same way. */
	Start();
	FillFrontend();
	NewFrame();
	Host()->key_down[AERON_KEY_A] = 1;
	SetText("x");
	Host()->has_focus = 0;
	XvtInput_Update(0);
	AssertFrontendCleared();
}

/* A gamepad model and that gamepad connected in Aeron's snapshot. */
static void ConnectController(void)
{
	static XvtControllerOptions options;
	memset(&options, 0, sizeof options);
	XvtControllerModel *model = &options.models[0];
	memcpy(model->guid, "0123456789abcdef0123456789abcdea",
	       sizeof model->guid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	XvtControllerOptions_ClearProfile(&model->profile,
					  AERON_CONTROLLER_KIND_GAMEPAD);
	options.count = 1;
	XvtControllerMapping_Init(&options);
	AeronControllerSnapshot *pad = &Host()->controllers[0];
	pad->connected = 1;
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	pad->instance_id = 5;
	memcpy(pad->guid, model->guid, sizeof pad->guid);
}

static void CheckUpdateSamplesControllers(void)
{
	Start();
	ConnectController();
	XvtInput_Update(0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_IsModelConnected(), 1);

	Start();
	ConnectController();
	XvtInput_UpdateFlight(0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_IsModelConnected(), 1);
}

static void CheckUpdateFlightDropsText(void)
{
	Start();
	SetText("abc");
	XvtInput_Update(0);
	XVT_ASSERT_TRUE(Keyboard_PeekChar() == 'a');
	NewFrame();
	XvtInput_UpdateFlight(0);
	XVT_ASSERT_INT_EQ(Keyboard_PeekChar(), 0);
}

static void CheckInitWaitsForSettings(void)
{
	Start();
	XVT_ASSERT_TRUE(XvtConfig_Settings() == NULL);
	ConnectController();
	XvtInput_Init();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Options()->count, 1);
}

static void CheckShutdown(void)
{
	Start();
	ConnectController();
	XvtInput_SuppressRendererTab(true);
	XvtInput_SetCaptured(true);
	static XvtKeyboardBindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = AERON_KEY_A + ('t' - 'a');
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	XvtKeyboardMapping_Install(&profile);
	XvtKeyboardMapping_Enable(true, Host());
	AeronKeyEvent press;
	memset(&press, 0, sizeof press);
	press.chord.key = AERON_KEY_A + ('t' - 'a');
	press.down = 1;
	XvtKeyboardMapping_Event(&press, false);
	g_xvtControlRoll = 5;

	XvtInput_Shutdown();
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	XVT_ASSERT_INT_EQ(XvtInput_IsCaptured(), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Options()->count, 0);
	XVT_ASSERT_INT_EQ(g_xvtControlRoll, 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 0);
}

static void CheckRendererShortcutRefusals(void)
{
	Start();
	XVT_ASSERT_TRUE(XvtRenderSnapshot_Current() == NULL);
	XVT_ASSERT_INT_EQ(XvtInput_RendererShortcutAllowed(), 0);
	XvtInput_SetCaptured(true);
	XVT_ASSERT_INT_EQ(XvtInput_RendererShortcutAllowed(), 0);
	XvtInput_SetCaptured(false);
}

int main(void)
{
	CheckRoute();
	CheckNewRouteFlushes();
	CheckMappingOnlyForGameplay();
	CheckCanReacquireKeyboard();
	CheckFrontendKeyStates();
	CheckTypedControlKeys();
	CheckTextWindows1252();
	CheckTextRingDropsOldest();
	CheckFrontendMouse();
	CheckSuppressedFrameClears();
	CheckUpdateSamplesControllers();
	CheckUpdateFlightDropsText();
	CheckInitWaitsForSettings();
	CheckShutdown();
	CheckRendererShortcutRefusals();
	XvtInput_Shutdown();
	return 0;
}

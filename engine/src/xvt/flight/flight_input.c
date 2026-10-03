#include "xvt/flight/flight_input.h"
#ifdef XVT_MODERN
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/timing/flight_timing.h"
#endif

#include "xvt/frontend/config.h"
#include "xvt/input/dinput.h"
#include "xvt/input/input.h"
#include "xvt/input/joystick.h"
#include "xvt/input/mouse.h"
#include "xvt/util/time.h"

#ifndef XVT_MODERN
/* The Windows message record PeekMessageA and GetMessageA fill in the original
 * build's key reading. */
struct FlightInputWin32Message {
	void *window;	  /* Window the message is for; not read by name. */
	uint32_t message; /* Message number; not read by name. */
	uint32_t wParam;  /* First message argument; not read by name. */
	int32_t lParam;	  /* Second message argument; not read by name. */
	uint32_t time;	  /* Time the message was posted; not read by name. */
	int32_t pointX;	  /* Cursor X when posted; not read by name. */
	int32_t pointY;	  /* Cursor Y when posted; not read by name. */
};

__declspec(dllimport) int __stdcall
PeekMessageA(struct FlightInputWin32Message *message, void *hWnd,
	     unsigned int filterMin, unsigned int filterMax,
	     unsigned int removeMessage);
__declspec(dllimport) int __stdcall
GetMessageA(struct FlightInputWin32Message *message, void *hWnd,
	    unsigned int filterMin, unsigned int filterMax);
__declspec(dllimport) int __stdcall
TranslateMessage(const struct FlightInputWin32Message *message);
__declspec(dllimport) int32_t __stdcall
DispatchMessageA(const struct FlightInputWin32Message *message);
#endif

/* 1 when keys are read through DirectInput, 0 when through window messages; the
 * modern build reads keys through DirectInput either way. Starts at 1; at
 * flight start the launch option "nodinput" sets 0 and "dinput" or neither sets
 * 1 (Flight_Main in the original build, XvtFlightEntry_ReadLaunchSwitches in
 * the modern one), and it falls to 0 when DInput_Init fails (Flight_Main,
 * XvtFlightEntry_CreateDevices). */
// GLOBAL: XVT 0x527EB4
int g_flightConfDirectInput = 1;
/* 1 makes the window-message key reading look for a message with PeekMessageA
 * before waiting in GetMessageA. Nothing changes it from 0, so on that path
 * FlightInput_HasKeyReady waits for a message on every call; WinMouse_PollState
 * also reads it. */
// GLOBAL: XVT 0x66DDC0
int g_flightInputNonBlockingMsgPump = 0;
/* Key code FlightInput_GetNextKey returns on the window-message path. Nothing
 * in the engine stores a key in it; only XvtInput_FlushRawKeyboard writes it,
 * setting 0, in the modern build. */
// GLOBAL: XVT 0x66E1F4
uint8_t g_lastKeyCode = 0;
/* 1 when a key waits for FlightInput_GetNextKey on the window-message path.
 * Nothing in the engine sets it to 1: FlightInput_GetNextKey and, in the modern
 * build, XvtInput_FlushRawKeyboard set it to 0, so that path never sees a
 * key. */
// GLOBAL: XVT 0x66E708
int g_keyReady = 0;

/* Bit mask of the 20 joystick buttons FlightInput_Read last counted as held, so
 * a button gives its key once per press. Written only by FlightInput_Read and
 * FlightInput_ResetControlState, which sets 0. */
// GLOBAL: XVT 0x5505EC
int g_heldJoystickButtons;
/* The joystick throttle, 0 to 255, smoothed by FlightInput_Read, which moves it
 * a quarter of the way to each new reading; -1 until the first reading. Written
 * only by FlightInput_Read and FlightInput_ResetControlState, which sets -1. */
// GLOBAL: XVT 0x5505F0
int g_throttleSmoothed;
/* Keys a joystick throttle sends, indexed by 16 minus the smoothed throttle in
 * sixteenths (g_throttleSmoothed plus 8, divided by 16, clamped 0 to 16):
 * FLIGHT_KEY_BACKSLASH at index 0, the FLIGHT_KEY_THROTTLE keys and
 * FLIGHT_KEY_LEFT_BRACKET and FLIGHT_KEY_RIGHT_BRACKET between,
 * FLIGHT_KEY_BACKSPACE at index 16. FlightInput_Read sends one when the
 * sixteenth changes. Never written. */
// GLOBAL: XVT 0x51A878
uint8_t g_throttleKeyTable[17] = {0x5C, 0xDB, 0xDC, 0xDD, 0xDE, 0x5B,
				  0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0x5D,
				  0xE4, 0xE5, 0xE6, 0xE7, 0x08};
/* Per player, the input record FlightInput_Read applies when called with that
 * player's index: the input from that player's history for the step being
 * simulated, its key and button bits cleared when a step with side effects
 * suppressed replays a tick already simulated. Five functions write it:
 * Flight_AdvanceOneStep, and at flight start Flight_MainLoop, which clears it,
 * in the original build; XvtFlightSim_Advance, XvtFlightLoading_Globals, and
 * XvtFlightSim_UpdatePlayerStep, which clears its flags and throttle on a
 * pause, in the modern one. */
// GLOBAL: XVT 0x9A7B70
struct FlightInputFrameRecord g_replayInputs[8] = {{0}};
/* Stick X axis of the input being applied: the local joystick's on a local
 * read, else the replayed record's. FlightInput_LatchFlightControls turns it
 * into g_scaledInputYaw (times 120) when the mouse gives no yaw. Written by
 * FlightInput_Read and FlightInput_ClearAxesAndModifiers, and in the modern
 * build by XvtFlightControls_ReadLocal and XvtInput_SetCaptured. */
// GLOBAL: XVT 0xA00498
int16_t g_ctrlAxisX;
/* Stick Y axis, as g_ctrlAxisX; it becomes g_scaledInputPitch (times 50) when
 * the mouse gives no pitch. Same writers as g_ctrlAxisX. */
// GLOBAL: XVT 0xA004A0
int16_t g_ctrlAxisY;
/* Button bits of the input being applied: bit 0 fire, bit 1 target, set while a
 * joystick button mapped to code 156 or 157 is held, or from a replayed record
 * (its keyMods, low 2 bits). FlightInput_LatchFlightControls merges it into
 * g_flightKeyMods. Written by FlightInput_Read, FlightInput_ResetRuntimeState
 * and FlightInput_ClearAxesAndModifiers, and in the modern build by
 * XvtFlightControls_ReadLocal and XvtInput_SetCaptured. */
// GLOBAL: XVT 0xA08244
uint16_t g_keyMods;
/* What Input_DetectActiveJoystick returned, cut to 16 bits;
 * FlightInput_ResetRuntimeState sets it and sets g_joystickAvailable from it,
 * and nothing else reads it. */
// GLOBAL: XVT 0xA08130
uint16_t g_joystickDetectResultWord = 0;
/* Action key of the last FlightInput_Read (a FlightActionKey, 0 for none);
 * FlightInput_LatchFlightControls copies it to g_currentActionKey. Seven
 * functions write it: FlightInput_Read, Mission_InitFlightRuntimeState (0),
 * Flight_UpdatePlayerStep in the original build, and
 * XvtFlightControls_ReadLocal, XvtFlightControls_Recover, XvtFlightSim_Resume
 * and XvtInput_SetCaptured in the modern one. */
// GLOBAL: XVT 0x9A8C12
uint16_t g_actionKey;
/* Action key the current player step acts on, latched from g_actionKey by
 * FlightInput_LatchFlightControls; the only other writer,
 * Flight_ProcessPlayerActions, sets it to FLIGHT_KEY_NONE after some keys. Read
 * by the action handling, the chat input and the MFD pages. */
// GLOBAL: XVT 0x9D6930
uint16_t g_currentActionKey;
/* Button bits for the current player step: g_keyMods and the mouse buttons,
 * merged by FlightInput_LatchFlightControls. Bit 0, with bits 2 and 3 clear,
 * fires the weapon. Bit 1, with bits 2 and 3 clear, picks a target when
 * released within 59 ticks and, held longer, turns yaw input into roll; the
 * player step clears it here during those first 59 ticks. Three functions write
 * it: FlightInput_LatchFlightControls and the player step,
 * Flight_UpdatePlayerStep in the original build and
 * XvtFlightSim_UpdatePlayerStep in the modern one. */
// GLOBAL: XVT 0x9EC474
uint16_t g_flightKeyMods;
/* 1 when the mouse steers. Only FlightInput_ResetRuntimeState writes it,
 * setting 0, so the mouse paths in FlightInput_Read,
 * FlightInput_LatchFlightControls and the modern controls never run. */
// GLOBAL: XVT 0x9E95F0
uint16_t g_flightMouseEnabled;
/* 1 when FlightInput_ResetRuntimeState found a joystick
 * (Input_DetectActiveJoystick nonzero), else 0; only that function writes it.
 * FlightInput_Read polls the joystick only when it is set. */
// GLOBAL: XVT 0xA08242
uint16_t g_joystickAvailable;
/* Mouse movement in X since the last local read, clamped to -191 to 191;
 * FlightInput_LatchFlightControls turns it into yaw (shifted left 7). Written
 * by FlightInput_Read, and in the modern build by XvtFlightControls_ReadLocal
 * and XvtInput_SetCaptured. */
// GLOBAL: XVT 0x9A73E8
int16_t g_flightMouseDeltaX;
/* Mouse movement in Y, clamped to -127 to 127, turned into pitch (shifted left
 * 6); written as g_flightMouseDeltaX. */
// GLOBAL: XVT 0x9A73F2
int16_t g_flightMouseDeltaY;
/* Mouse X position from the last local read. Written by FlightInput_Read and,
 * through its address, XvtFlightControls_ReadLocal; nothing reads it. */
// GLOBAL: XVT 0x9A739C
int16_t g_flightMouseX = 0;
/* Mouse Y position, as g_flightMouseX; nothing reads it. */
// GLOBAL: XVT 0x9A739E
int16_t g_flightMouseY = 0;
/* Mouse button bits from the last local read; FlightInput_LatchFlightControls
 * merges them into g_flightKeyMods. Written by FlightInput_Read and
 * FlightInput_ResetRuntimeState, and in the modern build by
 * XvtFlightControls_ReadLocal and XvtInput_SetCaptured. */
// GLOBAL: XVT 0x9D8C06
uint16_t g_mouseButtons;
/* Yaw input for the current player step, signed: mouse X movement shifted left
 * 7, or with no mouse yaw, g_ctrlAxisX times 120. Set by
 * FlightInput_LatchFlightControls; the dead zones (FlightInput_ApplyDeadzone,
 * FlightInput_ReadAndApplyFlightDeadzone) zero it, and
 * Player_UpdateFlightControlsAndCamera also writes it. */
// GLOBAL: XVT 0x9ECC30
int16_t g_scaledInputYaw;
/* The size of g_scaledInputYaw, stored by Player_UpdateFlightControlsAndCamera,
 * its only writer; nothing reads it. */
// GLOBAL: XVT 0x999414
int16_t g_absScaledInputYaw = 0;
/* Pitch input for the current player step, signed: mouse Y movement shifted
 * left 6, or with no mouse pitch, g_ctrlAxisY times 50. Written by the same
 * functions as g_scaledInputYaw. */
// GLOBAL: XVT 0x9ECA24
int16_t g_scaledInputPitch;

/* Latches the input FlightInput_Read left for the current player step:
 * g_currentActionKey from g_actionKey, g_scaledInputYaw and g_scaledInputPitch
 * from the mouse movement when the mouse is on and, where that gives 0, from
 * g_ctrlAxisX times 120 and g_ctrlAxisY times 50, and g_flightKeyMods from
 * g_keyMods and the mouse buttons. Its test for no joystick,
 * (g_joystickAvailable | 1) == 0, can never pass, so the stick is always
 * consulted. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x411540
void FlightInput_LatchFlightControls(void)
{
	int16_t pitch;
	int16_t yaw;
	uint16_t mouseButtons;

	g_currentActionKey = g_actionKey;
	pitch = 0;
	mouseButtons = 0;
	yaw = 0;
	g_flightKeyMods = mouseButtons;
	g_scaledInputPitch = pitch;
	g_scaledInputYaw = yaw;
	if (g_flightMouseEnabled != 0) {
		yaw = (int16_t)((uint16_t)g_flightMouseDeltaX << 7);
		pitch = (int16_t)((uint16_t)g_flightMouseDeltaY << 6);
		mouseButtons = g_mouseButtons;
	}
	*(uint16_t *)&g_scaledInputYaw = (uint16_t)yaw;
	*(uint16_t *)&g_scaledInputPitch = (uint16_t)pitch;
	*(int16_t *)&g_flightKeyMods = (int16_t)mouseButtons;
	if ((uint16_t)(g_joystickAvailable | 1u) == 0) {
		return;
	}
	if (yaw == 0) {
		g_scaledInputYaw = (int16_t)(g_ctrlAxisX * 120);
	}
	*(uint16_t *)&g_scaledInputPitch = (uint16_t)pitch;
	if (pitch == 0) {
		g_scaledInputPitch = (int16_t)(g_ctrlAxisY * 50);
	}
	*(int16_t *)&g_flightKeyMods = (int16_t)(g_keyMods | mouseButtons);
}

/* Zeroes g_scaledInputYaw when its size is 64 or less and g_scaledInputPitch
 * when 24 or less. */
// FUNCTION: XVT 0x4115F0
void FlightInput_ApplyDeadzone(void)
{
	int16_t magnitude;

	magnitude = g_scaledInputYaw;
	if ((uint16_t)magnitude >= 0x8000u) {
		magnitude = -magnitude;
	}
	if (magnitude <= 64) {
		g_scaledInputYaw = 0;
	}

	magnitude = g_scaledInputPitch;
	if ((uint16_t)magnitude >= 0x8000u) {
		magnitude = -magnitude;
	}
	if (magnitude <= 24) {
		g_scaledInputPitch = 0;
	}
}

/* Reads input (FlightInput_Read), latches it (FlightInput_LatchFlightControls),
 * then zeroes g_scaledInputYaw when its size is 2,048 or less and
 * g_scaledInputPitch when 1,536 or less. Its only callers are
 * FlightInput_WaitForActionKeyRelease, FlightInput_WaitForPress and
 * FlightInput_ClearButtonsAndDebounce, which nothing calls. */
// FUNCTION: XVT 0x411630
void FlightInput_ReadAndApplyFlightDeadzone(int playerIdxOrSentinel)
{
	int16_t magnitude;

	FlightInput_Read(playerIdxOrSentinel);
	FlightInput_LatchFlightControls();
	magnitude = g_scaledInputYaw;
	if ((uint16_t)g_scaledInputYaw >= 0x8000u) {
		magnitude = -g_scaledInputYaw;
	}
	if (magnitude <= 2048) {
		g_scaledInputYaw = 0;
	}

	magnitude = g_scaledInputPitch;
	if ((uint16_t)g_scaledInputPitch >= 0x8000u) {
		magnitude = -g_scaledInputPitch;
	}
	if (magnitude <= 1536) {
		g_scaledInputPitch = 0;
	}
}

/* Reads local input until a read yields no action key. Nothing calls this. */
// FUNCTION: XVT 0x411680
void FlightInput_WaitForActionKeyRelease(void)
{
	FlightInput_ReadAndApplyFlightDeadzone(-2);
	while (g_currentActionKey != 0) {
		FlightInput_ReadAndApplyFlightDeadzone(-2);
	}
}

/* Reads local input until a key, a button bit (g_keyMods bits 0 to 3) or a
 * mouse button comes; when it was a button, reads on until all are released.
 * Nothing calls this. */
// FUNCTION: XVT 0x4116B0
void FlightInput_WaitForPress(void)
{
	uint16_t key;
	uint16_t keyMods;
	uint16_t mouseButtons;

	do {
		key = FlightInput_Read(-2);
		mouseButtons = g_mouseButtons;
	} while (key == 0 && (g_keyMods & 0xF) == 0 && mouseButtons == 0);

	keyMods = g_keyMods;
	if ((keyMods & 0xF) != 0 || mouseButtons != 0) {
		while ((keyMods & 0xF) != 0 || mouseButtons != 0) {
			FlightInput_ReadAndApplyFlightDeadzone(-2);
			keyMods = g_keyMods;
			mouseButtons = g_mouseButtons;
		}
	}
}

/* Reads local input until no button bit (g_keyMods bits 0 to 3) or mouse button
 * has been held for 2 ticks in a row. Nothing calls this. */
// FUNCTION: XVT 0x411710
void FlightInput_ClearButtonsAndDebounce(void)
{
	uint16_t keyMods;
	uint16_t mouseButtons;
	uint32_t clearTicks;

	keyMods = g_keyMods;
	mouseButtons = g_mouseButtons;
	clearTicks = 0;
	do {
		if ((keyMods & 0xF) != 0 || mouseButtons != 0) {
			while ((keyMods & 0xF) != 0 || mouseButtons != 0) {
				FlightInput_ReadAndApplyFlightDeadzone(-2);
				keyMods = g_keyMods;
				mouseButtons = g_mouseButtons;
			}
			clearTicks = 0;
			Time_ConsumeElapsedTicks();
		}
		FlightInput_ReadAndApplyFlightDeadzone(-2);
		clearTicks += Time_ConsumeElapsedTicks();
		mouseButtons = g_mouseButtons;
		keyMods = g_keyMods;
	} while (clearTicks < 2);
}

/* Resets input at flight start: clears g_mouseButtons, g_keyMods and
 * g_flightMouseEnabled, asks Input_DetectActiveJoystick for a joystick
 * (g_joystickDetectResultWord, g_joystickAvailable), and calls
 * FlightInput_ResetControlState. */
// FUNCTION: XVT 0x411780
void FlightInput_ResetRuntimeState(void)
{
	g_joystickAvailable = 0;
	g_flightMouseEnabled = 0;
	g_mouseButtons = 0;
	g_keyMods = 0;
	g_joystickDetectResultWord = (uint16_t)Input_DetectActiveJoystick();
	if (g_joystickDetectResultWord == 0) {
		g_joystickAvailable = 0;
	} else {
		g_joystickAvailable = 1;
	}
	FlightInput_ResetControlState();
	g_flightMouseEnabled = 0;
}

/* Sets g_ctrlAxisX, g_ctrlAxisY and g_keyMods to 0. Nothing calls this. */
// FUNCTION: XVT 0x4117D0
void FlightInput_ClearAxesAndModifiers(void)
{
	g_ctrlAxisY = 0;
	g_ctrlAxisX = 0;
	g_keyMods = 0;
}

/* Sets g_throttleSmoothed to -1 (no reading yet) and g_heldJoystickButtons to
 * 0; the modern build also calls XvtFlightControls_Reset. */
// FUNCTION: XVT 0x4117F0
void FlightInput_ResetControlState(void)
{
#ifdef XVT_MODERN
	XvtFlightControls_Reset();
#endif
	g_throttleSmoothed = -1;
	g_heldJoystickButtons = 0;
}

/* Reads one input and returns its action key (0 for none). With a negative
 * argument it reads the local devices: the modern build returns
 * XvtFlightControls_ReadLocal at once, and the rest of this path runs only in
 * the original build. That path polls the joystick when g_joystickAvailable and
 * the mouse when g_flightMouseEnabled, takes one waiting key, and maps the 20
 * joystick buttons through g_gameConfig.joyButtons: a newly pressed button
 * gives its key when no key came yet (else it is counted as not held, so it
 * gives its key on a later read); releasing a button mapped to keypad 0 gives
 * keypad 0, and one mapped to keypad 1 to 9 gives keypad 8, the same way;
 * buttons mapped to codes 156 and 157 set g_keyMods bits 0 and 1 while held.
 * With still no key, the smoothed throttle may give one from
 * g_throttleKeyTable. It writes g_actionKey, g_ctrlAxisX, g_ctrlAxisY,
 * g_keyMods, g_mouseButtons, the mouse globals, g_heldJoystickButtons and
 * g_throttleSmoothed; with the mouse off it stores mouse positions it never
 * set. With a player index it applies g_replayInputs for that player instead:
 * the axes, the key (into g_actionKey) and the low 2 bits of keyMods; the
 * modern build also sets g_xvtControlRoll and, under the network timing, zeroes
 * the mouse movement and buttons. */
// FUNCTION: XVT 0x411810
uint16_t FlightInput_Read(int playerIdxOrSentinel)
{
	int joystickButtons;
	uint16_t key;
	uint16_t mappedKey;
	unsigned int mappedKeyValue;
	int targetButtonHeld;
	int buttonIndex;
	int buttonBit;
	uint8_t buttonKey;
	int releasedKey;
	int previousThrottle;
	int throttleBucket;
	int throttleRaw;
	int fireButtonHeld;
	int combinedKeyMods;
	int axisX;
	int axisY;
	int16_t mouseX;
	int16_t mouseY;
	uint16_t mouseButtons;
	int16_t mouseDeltaX;
	int16_t mouseDeltaY;

	joystickButtons = 0;
	if (playerIdxOrSentinel < 0) {
#ifdef XVT_MODERN
		return XvtFlightControls_ReadLocal();
#endif
		mouseButtons = 0;
		throttleRaw = 0;
		mouseDeltaY = 0;
		axisY = 0;
		mouseDeltaX = 0;
		axisX = 0;
#ifdef XVT_MODERN
		mouseX = 0;
		mouseY = 0;
#endif
		if (g_joystickAvailable != 0) {
			joystickButtons = Joystick_PollScaledAxesIfActive(
				&axisX, &axisY, &throttleRaw, NULL);
		}
		if (g_flightMouseEnabled != 0) {
			mouseButtons = (uint16_t)Mouse_ReadPositionAndButtons(
				&mouseX, &mouseY);
			Mouse_ReadDelta(&mouseDeltaX, &mouseDeltaY);
			if (mouseDeltaX <= -192) {
				mouseDeltaX = -191;
			} else if (mouseDeltaX >= 192) {
				mouseDeltaX = 191;
			}
			if (mouseDeltaY <= -128) {
				mouseDeltaY = -127;
			} else if (mouseDeltaY >= 128) {
				mouseDeltaY = 127;
			}
		}

		key = 0;
		if (FlightInput_HasKeyReady() != 0) {
			key = FlightInput_GetNextKey();
		}
		targetButtonHeld = 0;
		buttonBit = 1;
		buttonIndex = 0;
		fireButtonHeld = 0;
		do {
			buttonKey = g_gameConfig.joyButtons[buttonIndex];
			if (buttonKey != 0) {
				if ((buttonBit & joystickButtons) != 0) {
					mappedKey = buttonKey;
					mappedKeyValue = mappedKey;
					switch (mappedKeyValue) {
					case 156:
						fireButtonHeld = 1;
						break;
					case 157:
						targetButtonHeld = 1;
						break;
					}
					if ((g_heldJoystickButtons &
					     buttonBit) == 0) {
						if (key == 0) {
							key = mappedKey;
						} else {
							joystickButtons &=
								~buttonBit;
						}
					}
				} else if ((g_heldJoystickButtons &
					    buttonBit) != 0) {
					releasedKey = buttonKey;
					if (releasedKey == 178) {
						releasedKey = 178;
					} else if (releasedKey >= 179 &&
						   releasedKey <= 187) {
						releasedKey = 186;
					} else {
						releasedKey = 0;
					}
					if (releasedKey != 0) {
						if (key == 0) {
							key = (uint16_t)
								releasedKey;
						} else {
							joystickButtons |=
								buttonBit;
						}
					}
				}
			}
			buttonBit *= 2;
			++buttonIndex;
		} while (buttonIndex < 20);
		combinedKeyMods = fireButtonHeld + 2 * targetButtonHeld;
		g_heldJoystickButtons = joystickButtons;

		if (key == 0) {
			throttleRaw = (int)(int8_t)throttleRaw;
			throttleRaw += 128;
			if (g_throttleSmoothed == -1) {
				g_throttleSmoothed = throttleRaw;
			} else {
				previousThrottle = g_throttleSmoothed;
				g_throttleSmoothed +=
					(throttleRaw - g_throttleSmoothed) / 4;
				previousThrottle = (previousThrottle + 8) / 16;
				throttleBucket = (g_throttleSmoothed + 8) / 16;
				if (throttleBucket != previousThrottle) {
					if (throttleBucket < 0) {
						throttleBucket = 0;
					}
					if (throttleBucket > 16) {
						throttleBucket = 16;
					}
					key = g_throttleKeyTable
						[16 - throttleBucket];
				}
			}
		}

		g_actionKey = key;
		g_flightMouseDeltaX = mouseDeltaX;
		g_flightMouseDeltaY = mouseDeltaY;
		g_ctrlAxisX = (int16_t)axisX;
		g_ctrlAxisY = (int16_t)axisY;
		g_keyMods = (uint16_t)combinedKeyMods;
		g_mouseButtons = mouseButtons;
		g_flightMouseX = mouseX;
		g_flightMouseY = mouseY;
		return key;
	} else {
		g_ctrlAxisX = g_replayInputs[playerIdxOrSentinel].axisX;
#ifdef XVT_MODERN
		g_xvtControlRoll = g_replayInputs[playerIdxOrSentinel].axisR;
		/* Recorded axes are the complete shared control sample. */
		if (XvtFlightTiming_IsNetwork125()) {
			g_flightMouseDeltaX = g_flightMouseDeltaY = 0;
			g_mouseButtons = 0;
		}
#endif
		g_ctrlAxisY = g_replayInputs[playerIdxOrSentinel].axisY;
		key = g_replayInputs[playerIdxOrSentinel].key;
		g_actionKey = key;
		g_keyMods = g_replayInputs[playerIdxOrSentinel].keyMods & 3;
		return key;
	}
}

/* Returns nonzero when a key press waits. The modern build returns 0 while the
 * input is captured (XvtInput_IsCaptured), else DInput_SkipToPendingKeyPress.
 * The original build asks DInput_SkipToPendingKeyPress when
 * g_flightConfDirectInput is set; otherwise it handles one window message,
 * waiting for one in GetMessageA unless g_flightInputNonBlockingMsgPump is set
 * and none is queued, and returns g_keyReady. */
// FUNCTION: XVT 0x4AA7F0
int FlightInput_HasKeyReady(void)
{
#ifdef XVT_MODERN
	return XvtInput_IsCaptured() ? 0 : DInput_SkipToPendingKeyPress();
#else
	struct FlightInputWin32Message message;

	if (g_flightConfDirectInput != 0) {
		return DInput_SkipToPendingKeyPress();
	}
	if (g_flightInputNonBlockingMsgPump == 0 ||
	    PeekMessageA(&message, 0, 0, 0, 0) != 0) {
		if (GetMessageA(&message, 0, 0, 0) == 0) {
			return g_keyReady;
		}
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}
	return g_keyReady;
#endif
}

/* Returns the next key press. The modern build returns 0 while the input is
 * captured, else DInput_GetKey. The original build returns DInput_GetKey when
 * g_flightConfDirectInput is set; otherwise it handles window messages until
 * g_keyReady is set, clears it and returns g_lastKeyCode, or returns
 * g_lastKeyCode when GetMessageA reports the quit message. */
// FUNCTION: XVT 0x4AA870
int FlightInput_GetNextKey(void)
{
#ifdef XVT_MODERN
	return XvtInput_IsCaptured() ? 0 : DInput_GetKey();
#else
	struct FlightInputWin32Message message;

	if (g_flightConfDirectInput != 0) {
		return DInput_GetKey();
	}
	while (1) {
		if (g_keyReady != 0) {
			g_keyReady = 0;
			return g_lastKeyCode;
		}
		if (g_flightInputNonBlockingMsgPump != 0 &&
		    PeekMessageA(&message, 0, 0, 0, 0) == 0) {
			continue;
		}
		if (GetMessageA(&message, 0, 0, 0) == 0) {
			return g_lastKeyCode;
		}
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}
#endif
}

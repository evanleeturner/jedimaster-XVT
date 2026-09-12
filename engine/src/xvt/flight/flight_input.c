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
struct FlightInputWin32Message {
	void* window;
	uint32_t message;
	uint32_t wParam;
	int32_t lParam;
	uint32_t time;
	int32_t pointX;
	int32_t pointY;
};

__declspec(dllimport) int __stdcall PeekMessageA(struct FlightInputWin32Message* message, void* hWnd,
												 unsigned int filterMin, unsigned int filterMax,
												 unsigned int removeMessage);
__declspec(dllimport) int __stdcall GetMessageA(struct FlightInputWin32Message* message, void* hWnd,
												unsigned int filterMin, unsigned int filterMax);
__declspec(dllimport) int __stdcall TranslateMessage(const struct FlightInputWin32Message* message);
__declspec(dllimport) int32_t __stdcall DispatchMessageA(const struct FlightInputWin32Message* message);
#endif

// GLOBAL: XVT 0x527EB4
int g_flightConfDirectInput = 1;
// GLOBAL: XVT 0x66DDC0
int g_flightInputNonBlockingMsgPump = 0;
// GLOBAL: XVT 0x66E1F4
uint8_t g_lastKeyCode = 0;
// GLOBAL: XVT 0x66E708
int g_keyReady = 0;

// GLOBAL: XVT 0x5505EC
int g_controlMask;
// GLOBAL: XVT 0x5505F0
int g_throttleSmoothed;
// GLOBAL: XVT 0x51A878
uint8_t g_throttleKeyTable[17] = { 0x5C, 0xDB, 0xDC, 0xDD, 0xDE, 0x5B, 0xDF, 0xE0, 0xE1,
								   0xE2, 0xE3, 0x5D, 0xE4, 0xE5, 0xE6, 0xE7, 0x08 };
// GLOBAL: XVT 0x9A7B70
FlightInputFrameRecord g_replayInputs[8] = { { 0 } };
// GLOBAL: XVT 0xA00498
int16_t g_ctrlAxisX;
// GLOBAL: XVT 0xA004A0
int16_t g_ctrlAxisY;
// GLOBAL: XVT 0xA08244
uint16_t g_keyMods;
// GLOBAL: XVT 0xA08130
uint16_t g_joystickDetectResultWord = 0;
// GLOBAL: XVT 0x9A8C12
uint16_t g_actionKey;
// GLOBAL: XVT 0x9D6930
uint16_t g_currentActionKey;
// GLOBAL: XVT 0x9EC474
uint16_t g_flightKeyMods;
// GLOBAL: XVT 0x9E95F0
uint16_t g_joystickEnabled;
// GLOBAL: XVT 0xA08242
uint16_t g_joystickAvailable;
// GLOBAL: XVT 0x9A73E8
int16_t g_flightMouseDeltaX;
// GLOBAL: XVT 0x9A73F2
int16_t g_flightMouseDeltaY;
// GLOBAL: XVT 0x9A739C
int16_t g_flightMouseX = 0;
// GLOBAL: XVT 0x9A739E
int16_t g_flightMouseY = 0;
// GLOBAL: XVT 0x9D8C06
uint16_t g_mouseButtons;
// GLOBAL: XVT 0x9ECC30
int16_t g_scaledInputYaw;
// GLOBAL: XVT 0x999414
int16_t g_absScaledInputYaw = 0;
// GLOBAL: XVT 0x9ECA24
int16_t g_scaledInputPitch;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x411540
void FlightInput_ScaleAxesForFlight(void) {
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
	if (g_joystickEnabled != 0) {
		yaw = (int16_t)((uint16_t)g_flightMouseDeltaX << 7);
		pitch = (int16_t)((uint16_t)g_flightMouseDeltaY << 6);
		mouseButtons = g_mouseButtons;
	}
	*(uint16_t*)&g_scaledInputYaw = (uint16_t)yaw;
	*(uint16_t*)&g_scaledInputPitch = (uint16_t)pitch;
	*(int16_t*)&g_flightKeyMods = (int16_t)mouseButtons;
	if ((uint16_t)(g_joystickAvailable | 1u) == 0)
		return;
	if (yaw == 0)
		g_scaledInputYaw = (int16_t)(g_ctrlAxisX * 120);
	*(uint16_t*)&g_scaledInputPitch = (uint16_t)pitch;
	if (pitch == 0)
		g_scaledInputPitch = (int16_t)(g_ctrlAxisY * 50);
	*(int16_t*)&g_flightKeyMods = (int16_t)(g_keyMods | mouseButtons);
}

// FUNCTION: XVT 0x4115F0
void FlightInput_ApplyDeadzone(void) {
	int16_t magnitude;

	magnitude = g_scaledInputYaw;
	if ((uint16_t)magnitude >= 0x8000u)
		magnitude = -magnitude;
	if (magnitude <= 64)
		g_scaledInputYaw = 0;

	magnitude = g_scaledInputPitch;
	if ((uint16_t)magnitude >= 0x8000u)
		magnitude = -magnitude;
	if (magnitude <= 24)
		g_scaledInputPitch = 0;
}

// FUNCTION: XVT 0x411630
void FlightInput_ReadAndApplyFlightDeadzone(int playerIdxOrSentinel) {
	int16_t magnitude;

	FlightInput_Read(playerIdxOrSentinel);
	FlightInput_ScaleAxesForFlight();
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

// FUNCTION: XVT 0x411680
void FlightInput_WaitForActionKeyRelease(void) {
	FlightInput_ReadAndApplyFlightDeadzone(-2);
	while (g_currentActionKey != 0) {
		FlightInput_ReadAndApplyFlightDeadzone(-2);
	}
}

// FUNCTION: XVT 0x4116B0
void FlightInput_WaitForPress(void) {
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

// FUNCTION: XVT 0x411710
void FlightInput_ClearButtonsAndDebounce(void) {
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
			Time_GetFrameDelta();
		}
		FlightInput_ReadAndApplyFlightDeadzone(-2);
		clearTicks += Time_GetFrameDelta();
		mouseButtons = g_mouseButtons;
		keyMods = g_keyMods;
	} while (clearTicks < 2);
}

// FUNCTION: XVT 0x411780
void FlightInput_ResetRuntimeState(void) {
	g_joystickAvailable = 0;
	g_joystickEnabled = 0;
	g_mouseButtons = 0;
	g_keyMods = 0;
	g_joystickDetectResultWord = (uint16_t)Input_DetectActiveJoystick();
	if (g_joystickDetectResultWord == 0) {
		g_joystickAvailable = 0;
	} else {
		g_joystickAvailable = 1;
	}
	FlightInput_ResetControlState();
	g_joystickEnabled = 0;
}

// FUNCTION: XVT 0x4117D0
void FlightInput_ClearAxesAndModifiers(void) {
	g_ctrlAxisY = 0;
	g_ctrlAxisX = 0;
	g_keyMods = 0;
}

// FUNCTION: XVT 0x4117F0
void FlightInput_ResetControlState(void) {
#ifdef XVT_MODERN
	XvtFlightControls_Reset();
#endif
	g_throttleSmoothed = -1;
	g_controlMask = 0;
}

// FUNCTION: XVT 0x411810
uint16_t FlightInput_Read(int playerIdxOrSentinel) {
	int joystickButtons;
	uint16_t key;
	uint16_t mappedKey;
	unsigned int mappedKeyValue;
	int keyModAlt3;
	int buttonIndex;
	int buttonBit;
	uint8_t buttonKey;
	int releasedKey;
	int previousThrottle;
	int throttleBucket;
	int throttleRaw;
	int keyModAlt2;
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
			joystickButtons = Joystick_PollRawAxesIfEnabled(&axisX, &axisY, &throttleRaw, NULL);
		}
		if (g_joystickEnabled != 0) {
			mouseButtons = (uint16_t)Mouse_ReadPositionAndButtons(&mouseX, &mouseY);
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
		keyModAlt3 = 0;
		buttonBit = 1;
		buttonIndex = 0;
		keyModAlt2 = 0;
		do {
			buttonKey = g_gameConfig.joyButtons[buttonIndex];
			if (buttonKey != 0) {
				if ((buttonBit & joystickButtons) != 0) {
					mappedKey = buttonKey;
					mappedKeyValue = mappedKey;
					switch (mappedKeyValue) {
						case 156:
							keyModAlt2 = 1;
							break;
						case 157:
							keyModAlt3 = 1;
							break;
					}
					if ((g_controlMask & buttonBit) == 0) {
						if (key == 0) {
							key = mappedKey;
						} else {
							joystickButtons &= ~buttonBit;
						}
					}
				} else if ((g_controlMask & buttonBit) != 0) {
					releasedKey = buttonKey;
					if (releasedKey == 178) {
						releasedKey = 178;
					} else if (releasedKey >= 179 && releasedKey <= 187) {
						releasedKey = 186;
					} else {
						releasedKey = 0;
					}
					if (releasedKey != 0) {
						if (key == 0) {
							key = (uint16_t)releasedKey;
						} else {
							joystickButtons |= buttonBit;
						}
					}
				}
			}
			buttonBit *= 2;
			++buttonIndex;
		} while (buttonIndex < 20);
		combinedKeyMods = keyModAlt2 + 2 * keyModAlt3;
		g_controlMask = joystickButtons;

		if (key == 0) {
			throttleRaw = (int)(int8_t)throttleRaw;
			throttleRaw += 128;
			if (g_throttleSmoothed == -1) {
				g_throttleSmoothed = throttleRaw;
			} else {
				previousThrottle = g_throttleSmoothed;
				g_throttleSmoothed += (throttleRaw - g_throttleSmoothed) / 4;
				previousThrottle = (previousThrottle + 8) / 16;
				throttleBucket = (g_throttleSmoothed + 8) / 16;
				if (throttleBucket != previousThrottle) {
					if (throttleBucket < 0) {
						throttleBucket = 0;
					}
					if (throttleBucket > 16) {
						throttleBucket = 16;
					}
					key = g_throttleKeyTable[16 - throttleBucket];
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

// FUNCTION: XVT 0x4AA7F0
int FlightInput_HasKeyReady(void) {
#ifdef XVT_MODERN
	return XvtInput_IsCaptured() ? 0 : DInput_HasKeyReady();
#else
	struct FlightInputWin32Message message;

	if (g_flightConfDirectInput != 0) {
		return DInput_HasKeyReady();
	}
	if (g_flightInputNonBlockingMsgPump == 0 || PeekMessageA(&message, 0, 0, 0, 0) != 0) {
		if (GetMessageA(&message, 0, 0, 0) == 0) {
			return g_keyReady;
		}
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}
	return g_keyReady;
#endif
}

// FUNCTION: XVT 0x4AA870
int FlightInput_GetNextKey(void) {
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
		if (g_flightInputNonBlockingMsgPump != 0 && PeekMessageA(&message, 0, 0, 0, 0) == 0) {
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

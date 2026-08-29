/* WinMM joystick API shim, filled from the game-registered controller source. */

#define AERON_WINMM_COMPAT_IMPLEMENTATION
#include "aeron/compat/host.h"
#include "aeron/compat/mmsystem.h"

#include <string.h>

enum { WINMM_JOY_AXIS_RANGE = 65535 };

static AeronWinmmJoystickSource g_joystick_source;
static void*                    g_joystick_source_user;

void AeronCompat_SetJoystickSource(AeronWinmmJoystickSource fn, void* user) {
	g_joystick_source      = fn;
	g_joystick_source_user = user;
}

uint32_t AERON_WINMMAPI joyGetNumDevs(void) {
	/* The games consume one explicitly selected logical controller. */
	return g_joystick_source ? 1u : 0u;
}

MMRESULT AERON_WINMMAPI joyGetDevCapsA(uint32_t uJoyID, JOYCAPSA* pjc, uint32_t cbjc) {
	AeronWinmmJoystickState state;

	if (!pjc || cbjc < sizeof(*pjc)) {
		return JOYERR_PARMS;
	}
	if (uJoyID != 0 || !g_joystick_source || !g_joystick_source(&state, g_joystick_source_user)) {
		return JOYERR_UNPLUGGED;
	}
	memset(pjc, 0, sizeof(*pjc));
	if (state.name) {
		strncpy(pjc->szPname, state.name, sizeof(pjc->szPname) - 1);
	}
	pjc->wXmax       = WINMM_JOY_AXIS_RANGE;
	pjc->wYmax       = WINMM_JOY_AXIS_RANGE;
	pjc->wZmax       = WINMM_JOY_AXIS_RANGE;
	pjc->wRmax       = WINMM_JOY_AXIS_RANGE;
	pjc->wNumButtons = state.button_count;
	pjc->wMaxButtons = state.button_count;
	pjc->wNumAxes    = 4;
	pjc->wMaxAxes    = 4;
	pjc->wCaps       = JOYCAPS_HASZ | JOYCAPS_HASR;
	if (state.has_pov) {
		pjc->wCaps |= JOYCAPS_HASPOV;
	}
	return JOYERR_NOERROR;
}

MMRESULT AERON_WINMMAPI joyGetPosEx(uint32_t uJoyID, JOYINFOEX* pji) {
	AeronWinmmJoystickState state;
	uint32_t                buttons;

	if (!pji) {
		return JOYERR_PARMS;
	}
	if (uJoyID != 0 || !g_joystick_source || !g_joystick_source(&state, g_joystick_source_user)) {
		return JOYERR_UNPLUGGED;
	}
	pji->dwXpos         = state.axes[0];
	pji->dwYpos         = state.axes[1];
	pji->dwZpos         = state.axes[2];
	pji->dwRpos         = state.axes[3];
	pji->dwUpos         = 0;
	pji->dwVpos         = 0;
	buttons             = state.buttons;
	pji->dwButtons      = buttons;
	pji->dwButtonNumber = 0;
	while (pji->dwButtonNumber < state.button_count && !(buttons & (1u << pji->dwButtonNumber))) {
		++pji->dwButtonNumber;
	}
	pji->dwButtonNumber = pji->dwButtonNumber < state.button_count ? pji->dwButtonNumber + 1 : 0;
	pji->dwPOV          = state.pov_direction < 0 ? JOY_POVCENTERED : (uint32_t)state.pov_direction * 9000u;
	pji->dwReserved1    = 0;
	pji->dwReserved2    = 0;
	return JOYERR_NOERROR;
}

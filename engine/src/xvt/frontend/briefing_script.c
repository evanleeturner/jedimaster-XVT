#include "xvt/frontend/briefing_script.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission.h"

#include <string.h>

// GLOBAL: XVT 0x52CF10
const int16_t g_briefingScriptOpcodeArgCounts[35] = {
	0, 0, 1, 0, 1, 1, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// GLOBAL: XVT 0x6691E6
int16_t g_briefingPlaybackActive = 0;
// GLOBAL: XVT 0x669218
int g_briefingLastNarratedTextBlockIdx = 0;
// GLOBAL: XVT 0x66921C
int g_briefingTextPageNumber = 0;
// GLOBAL: XVT 0x6696DA
int16_t g_briefingTextSlotActive[2] = { 0 };
// GLOBAL: XVT 0x6696DE
int16_t g_briefingTextSlotBlockIdx[2] = { 0 };
// GLOBAL: XVT 0x6696E2
int16_t g_briefingTextSlotsChanged = 0;
// GLOBAL: XVT 0x669778
int16_t g_briefingScriptPauseMarkerReached = 0;

// GLOBAL: XVT 0x669258
struct FrontendBriefingScript g_briefingScript;

// FUNCTION: XVT 0x4F6950
void BriefingScript_AdvanceOrResetAtEnd(int frameCounter) {
	(void)frameCounter;

	if (g_briefingPlaybackActive != 0) {
		BriefingMap_AnimateViewState();
		BriefingMap_UpdateScriptPlaybackAfterAnimation();
	}
}

// FUNCTION: XVT 0x4F7340
int16_t BriefingScript_InitDefaultScript(void) {
	g_briefingScript.durationTicks = 200;
	g_briefingScript.headerWord06 = 2;
	g_briefingScript.words[0] = 9999;
	g_briefingScript.words[1] = 34;
	g_briefingScript.currentTime = 0;
	g_briefingScript.cursorWordIndex = 0;
	g_briefingScript.headerWord08 = 0;
	return BriefingScript_ResetState();
}

// FUNCTION: XVT 0x4F7380
int16_t BriefingScript_ResetState(void) {
	int16_t index;

	g_briefingMapCenter.x = 0;
	g_briefingMapCenter.y = 0;
	g_briefingMapTargetCenter.x = 0;
	g_briefingMapTargetCenter.y = 0;
	g_briefingMapScale.x = 32;
	g_briefingMapScale.y = 32;
	g_briefingMapTargetScale.x = 32;
	g_briefingMapTargetScale.y = 32;
	for (index = 0; index < 2; ++index) {
		g_briefingTextSlotActive[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefingMapFgMarkerActive[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefingMapLabelActive[index] = 0;
	}
	g_briefingScript.currentTime = 0;
	g_briefingScript.cursorWordIndex = 0;
	return BriefingScript_AdvanceFrame(1);
}

// FUNCTION: XVT 0x4F7420
int16_t BriefingScript_AdvanceUntilTime(int16_t targetTime, int16_t initializeState) {
	if (g_briefingScript.currentTime - targetTime != 1) {
		if (targetTime < g_briefingScript.currentTime) {
			BriefingScript_ResetState();
		}
		while (targetTime >= g_briefingScript.currentTime) {
			BriefingScript_AdvanceFrame(initializeState);
		}
		return 1;
	}
	return 0;
}

// FUNCTION: XVT 0x4F7480
int16_t BriefingScript_AdvanceToNextVisibleLine(void) {
	int16_t textSlotActive;
	int16_t visibleTextTicks;
	int16_t opcode;
	int16_t slotIndex;
	int16_t currentTime;
	int16_t done;
	int16_t targetTime;
	int16_t targetOpcode;

	currentTime = g_briefingScript.currentTime;
	textSlotActive = 0;
	visibleTextTicks = 0;
	opcode = 0;
	done = 0;
	BriefingScript_ResetState();
	do {
		if (opcode == 34) {
			break;
		}
		opcode = g_briefingScript.words[g_briefingScript.cursorWordIndex + 1];
		if (g_briefingTextSlotsChanged != 0) {
			visibleTextTicks = 0;
			textSlotActive = 0;
		}
		for (slotIndex = 0; slotIndex < 2; ++slotIndex) {
			if (g_briefingTextSlotActive[slotIndex] != 0) {
				textSlotActive = 1;
			}
		}
		if (textSlotActive != 0) {
			++visibleTextTicks;
		}
		if ((g_briefingScriptPauseMarkerReached != 0 || visibleTextTicks == 1) &&
			currentTime <= g_briefingScript.currentTime) {
			done = 1;
		} else {
			BriefingScript_AdvanceFrame(1);
		}
	} while (done == 0);

	if (g_briefingScriptPauseMarkerReached != 0 || visibleTextTicks == 1) {
		targetTime = g_briefingScript.currentTime;
		targetOpcode = 0;
	} else {
		targetTime = g_briefingScript.words[g_briefingScript.cursorWordIndex];
		targetOpcode = g_briefingScript.words[g_briefingScript.cursorWordIndex + 1];
	}
	if (targetOpcode == 34) {
		g_briefingLastNarratedTextBlockIdx = 0;
		g_briefingTextPageNumber = 0;
		return BriefingScript_ResetState();
	}
	return BriefingScript_AdvanceUntilTime(targetTime, 0);
}

// FUNCTION: XVT 0x4F7590
int16_t BriefingScript_AdvanceFrame(int16_t initializeState) {
	int16_t cursorWordIndex;
	int16_t savedCursorWordIndex;
	int16_t eventTime;
	int16_t opcode;
	int16_t args[8];
	int16_t argumentCount;
	int16_t argumentIndex;
	int16_t slotIndex;
	int16_t iff;
	char labelText[40];

	cursorWordIndex = g_briefingScript.cursorWordIndex;
	savedCursorWordIndex = cursorWordIndex;
	eventTime = g_briefingScript.words[cursorWordIndex];
	g_briefingTextSlotsChanged = 0;
	g_briefingMapFgMarkersChanged = 0;
	g_briefingMapLabelsChanged = 0;
	g_briefingMapCenterDirty = 0;
	g_briefingMapScaleDirty = 0;
	g_briefingScriptPauseMarkerReached = 0;

	if (eventTime <= g_briefingScript.currentTime) {
		do {
			savedCursorWordIndex = cursorWordIndex;
			eventTime = g_briefingScript.words[cursorWordIndex++];
			opcode = g_briefingScript.words[cursorWordIndex++];
			argumentCount = g_briefingScriptOpcodeArgCounts[opcode];
			for (argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex) {
				args[argumentIndex] = g_briefingScript.words[cursorWordIndex++];
			}

			if (eventTime == g_briefingScript.currentTime) {
				switch (opcode) {
					case 1:
						g_briefingScriptPauseMarkerReached = 1;
						break;
					case 3:
						for (slotIndex = 0; slotIndex < 2; ++slotIndex) {
							g_briefingTextSlotActive[slotIndex] = 0;
						}
						g_briefingTextSlotsChanged = 1;
						break;
					case 4:
					case 5:
						slotIndex = opcode - 4;
						g_briefingTextSlotActive[slotIndex] = 1;
						g_briefingTextSlotBlockIdx[slotIndex] = args[0];
						break;
					case 6:
						if (eventTime == 0 || initializeState != 0) {
							g_briefingMapTargetCenter.x = args[0];
							g_briefingMapCenter.x = args[0];
							g_briefingMapTargetCenter.y = args[1];
							g_briefingMapCenter.y = args[1];
						} else {
							g_briefingMapTargetCenter.x = args[0];
							g_briefingMapTargetCenter.y = args[1];
						}
						g_briefingMapCenterDirty = 1;
						break;
					case 7:
						if (eventTime == 0 || initializeState != 0) {
							g_briefingMapTargetScale.x = args[0];
							g_briefingMapScale.x = args[0];
							g_briefingMapTargetScale.y = args[1];
							g_briefingMapScale.y = args[1];
						} else {
							g_briefingMapTargetScale.x = args[0];
							g_briefingMapTargetScale.y = args[1];
						}
						g_briefingMapScaleDirty = 1;
						break;
					case 8:
						for (slotIndex = 0; slotIndex < 8; ++slotIndex) {
							g_briefingMapFgMarkerActive[slotIndex] = 0;
						}
						g_briefingMapFgMarkersChanged = 1;
						break;
					case 9:
					case 10:
					case 11:
					case 12:
					case 13:
					case 14:
					case 15:
					case 16:
						if (initializeState == 0) {
							iff = g_frontendMission.flightGroups[args[0]].iff;
							if (iff > 2) {
								iff = 2;
							}
							if (iff == 1) {
								if (g_gameConfig.sfxDatapadEnabled != 0) {
									FrontendSound_PlayUISound("sfxTarget2", 1, 0, 127,
															  12 * g_gameConfig.sfxDatapadVolume, 63);
								}
							} else if (g_gameConfig.sfxDatapadEnabled != 0) {
								FrontendSound_PlayUISound("sfxTarget1", 1, 0, 127,
														  12 * g_gameConfig.sfxDatapadVolume, 63);
							}
						}
						slotIndex = opcode - 9;
						g_briefingMapFgMarkerActive[slotIndex] = 1;
						g_briefingMapFgMarkerAge[slotIndex] = initializeState == 0 ? 0 : 80;
						g_briefingMapFgMarkerIconIdx[slotIndex] = args[0];
						break;
					case 17:
						for (slotIndex = 0; slotIndex < 8; ++slotIndex) {
							g_briefingMapLabelActive[slotIndex] = 0;
						}
						g_briefingMapLabelsChanged = 1;
						break;
					case 18:
					case 19:
					case 20:
					case 21:
					case 22:
					case 23:
					case 24:
					case 25:
						if (initializeState == 0) {
							strcpy(labelText, g_briefingMapLabelTexts[args[0]]);
							if ((uint16_t)strlen(labelText) != 0 && g_gameConfig.sfxDatapadEnabled != 0) {
								FrontendSound_PlayUISound("sfxText", 1, 0, 127,
														  12 * g_gameConfig.sfxDatapadVolume, 63);
							}
						}
						slotIndex = opcode - 18;
						g_briefingMapLabelActive[slotIndex] = 1;
						g_briefingMapLabelAge[slotIndex] = initializeState == 0 ? 0 : 80;
						g_briefingMapLabelTextIdx[slotIndex] = args[0];
						g_briefingMapLabelX[slotIndex] = args[1];
						g_briefingMapLabelY[slotIndex] = args[2];
						g_briefingMapLabelStyle[slotIndex] = args[3];
						break;
					default:
						break;
				}
			}
		} while (eventTime <= g_briefingScript.currentTime);
	}

	++g_briefingScript.currentTime;
	g_briefingScript.cursorWordIndex = savedCursorWordIndex;
	return savedCursorWordIndex;
}

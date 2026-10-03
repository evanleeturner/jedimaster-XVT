#include "xvt/frontend/briefing_script.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission.h"

#include <string.h>

/* Argument words that follow each briefing script opcode, by opcode 0 to
 * 34. */
// GLOBAL: XVT 0x52CF10
const int16_t g_briefingScriptOpcodeArgCounts[35] = {
	0, 0, 1, 0, 1, 1, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0,
	4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* 1 while the briefing map plays: only then does
 * BriefingScript_AdvanceOrResetAtEnd move the view and step the script.
 * FrontendMission_InitForBriefing sets 1; the mission setup screen's Stop
 * and Play buttons set 0 and 1. */
// GLOBAL: XVT 0x6691E6
int16_t g_briefingPlaybackActive = 0;
/* Text block last counted as a page of narration:
 * BriefingMap_DrawViewportAndSelection stores slot 1's block here whenever it
 * differs, raising g_briefingTextPageNumber. Set to 0, with the page number,
 * whenever the briefing starts over: by FrontendMission_InitForBriefing,
 * BriefingMap_UpdateScriptPlaybackAfterAnimation,
 * BriefingScript_AdvanceToNextVisibleLine, and the mission setup screen's
 * Rewind button and its Forward button when that brings no new block. */
// GLOBAL: XVT 0x669218
int g_briefingLastNarratedTextBlockIdx = 0;
/* Page number drawn under the briefing map after the FRONTSTR_640_PAGE
 * string: narration blocks shown since the briefing last started over.
 * Raised by BriefingMap_DrawViewportAndSelection; set to 0 together with
 * g_briefingLastNarratedTextBlockIdx. */
// GLOBAL: XVT 0x66921C
int g_briefingTextPageNumber = 0;
/* Per text slot, 1 while it shows a text block: script opcodes 4 and 5 set
 * slot 0 and 1, opcode 3 clears both, and so do BriefingScript_ResetState and
 * FrontendMission_InitForBriefing. Only slot 1 is drawn, under the map;
 * BriefingScript_AdvanceToNextVisibleLine reads both. */
// GLOBAL: XVT 0x6696DA
int16_t g_briefingTextSlotActive[2] = {0};
/* Per text slot, the index in g_briefingTextBlocks of the block it shows: the
 * argument of opcode 4 or 5. Only BriefingScript_AdvanceFrame writes it. */
// GLOBAL: XVT 0x6696DE
int16_t g_briefingTextSlotBlockIdx[2] = {0};
/* 1 when the script frame just played held opcode 3, which cleared the text
 * slots; BriefingScript_AdvanceFrame sets 0 at the start of every frame. Read
 * by BriefingScript_AdvanceToNextVisibleLine. */
// GLOBAL: XVT 0x6696E2
int16_t g_briefingTextSlotsChanged = 0;
/* 1 when the script frame just played held opcode 1, a stopping point for
 * BriefingScript_AdvanceToNextVisibleLine, its one reader;
 * BriefingScript_AdvanceFrame sets 0 at the start of every frame. */
// GLOBAL: XVT 0x669778
int16_t g_briefingScriptPauseMarkerReached = 0;

/* The active briefing's script. FrontendMission_InitForBriefing puts in the
 * default from BriefingScript_InitDefaultScript, and
 * FrontendMission_LoadCurrentWithBriefing copies in the briefing of the
 * pilot's team from the mission file. BriefingScript_AdvanceFrame and
 * BriefingScript_ResetState move its play position. */
// GLOBAL: XVT 0x669258
struct FrontendBriefingScript g_briefingScript;

/* While g_briefingPlaybackActive is nonzero, moves the map view one step
 * toward its targets and plays one script frame with its sounds, or starts
 * the briefing over once its duration has passed
 * (BriefingMap_UpdateScriptPlaybackAfterAnimation). Ignores frameCounter. The
 * mission setup screen calls it every frame it shows the briefing map. */
// FUNCTION: XVT 0x4F6950
void BriefingScript_AdvanceOrResetAtEnd(int frameCounter)
{
	(void)frameCounter;

	if (g_briefingPlaybackActive != 0) {
		BriefingMap_AnimateViewState();
		BriefingMap_UpdateScriptPlaybackAfterAnimation();
	}
}

/* Makes g_briefingScript the default script: 200 frames long, headerWord06 2,
 * headerWord08 0, and one entry, opcode 34 at time 9999, which ends the
 * script. Only words 0 and 1 are written; the rest keep what they held. Then
 * starts it over through BriefingScript_ResetState and returns that
 * function's result. */
// FUNCTION: XVT 0x4F7340
int16_t BriefingScript_InitDefaultScript(void)
{
	g_briefingScript.durationFrames = 200;
	g_briefingScript.headerWord06 = 2;
	g_briefingScript.words[0] = 9999;
	g_briefingScript.words[1] = 34;
	g_briefingScript.currentFrame = 0;
	g_briefingScript.cursorWordIndex = 0;
	g_briefingScript.headerWord08 = 0;
	return BriefingScript_ResetState();
}

/* Starts the briefing over: map center and target center (0, 0), scale and
 * target scale 32 on both axes, both text slots, the 8 flight group markers
 * and the 8 labels off, and the script at frame 0 and word 0. Then plays
 * frame 0 through BriefingScript_AdvanceFrame(1), without sounds and with
 * markers and labels shown in full, and returns its word index. Leaves
 * g_briefingLastNarratedTextBlockIdx and g_briefingTextPageNumber alone. */
// FUNCTION: XVT 0x4F7380
int16_t BriefingScript_ResetState(void)
{
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
	g_briefingScript.currentFrame = 0;
	g_briefingScript.cursorWordIndex = 0;
	return BriefingScript_AdvanceFrame(1);
}

/* Plays the script through frame targetTime, passing initializeState to
 * BriefingScript_AdvanceFrame as its applyInstantly flag. Returns 0 and does
 * nothing when the current frame is targetTime + 1, so targetTime was the
 * last frame played. Otherwise starts over first when targetTime is behind
 * the current frame, plays frames until the current frame passes targetTime,
 * and returns 1. */
// FUNCTION: XVT 0x4F7420
int16_t BriefingScript_AdvanceUntilTime(int16_t targetTime,
					int16_t initializeState)
{
	if (g_briefingScript.currentFrame - targetTime != 1) {
		if (targetTime < g_briefingScript.currentFrame) {
			BriefingScript_ResetState();
		}
		while (targetTime >= g_briefingScript.currentFrame) {
			BriefingScript_AdvanceFrame(initializeState);
		}
		return 1;
	}
	return 0;
}

/* The briefing map's Forward button. Starts over and replays at once,
 * without sounds; once it is back at or past the frame it started from, it
 * stops after a frame that held a pause marker (opcode 1) or that first
 * showed a text slot since the slots last changed, plays the next frame with
 * sounds through BriefingScript_AdvanceUntilTime, and returns 1. When it
 * reaches the end entry (opcode 34) first, it sets
 * g_briefingLastNarratedTextBlockIdx and g_briefingTextPageNumber to 0,
 * starts over, and returns BriefingScript_ResetState's result. */
// FUNCTION: XVT 0x4F7480
int16_t BriefingScript_AdvanceToNextVisibleLine(void)
{
	int16_t textSlotActive;
	int16_t visibleTextFrames;
	int16_t opcode;
	int16_t slotIndex;
	int16_t startTime;
	int16_t done;
	int16_t targetTime;
	int16_t targetOpcode;

	startTime = g_briefingScript.currentFrame;
	textSlotActive = 0;
	visibleTextFrames = 0;
	opcode = 0;
	done = 0;
	BriefingScript_ResetState();
	do {
		if (opcode == 34) {
			break;
		}
		opcode = g_briefingScript
				 .words[g_briefingScript.cursorWordIndex + 1];
		if (g_briefingTextSlotsChanged != 0) {
			visibleTextFrames = 0;
			textSlotActive = 0;
		}
		for (slotIndex = 0; slotIndex < 2; ++slotIndex) {
			if (g_briefingTextSlotActive[slotIndex] != 0) {
				textSlotActive = 1;
			}
		}
		if (textSlotActive != 0) {
			++visibleTextFrames;
		}
		if ((g_briefingScriptPauseMarkerReached != 0 ||
		     visibleTextFrames == 1) &&
		    startTime <= g_briefingScript.currentFrame) {
			done = 1;
		} else {
			BriefingScript_AdvanceFrame(1);
		}
	} while (done == 0);

	if (g_briefingScriptPauseMarkerReached != 0 || visibleTextFrames == 1) {
		targetTime = g_briefingScript.currentFrame;
		targetOpcode = 0;
	} else {
		targetTime = g_briefingScript
				     .words[g_briefingScript.cursorWordIndex];
		targetOpcode =
			g_briefingScript
				.words[g_briefingScript.cursorWordIndex + 1];
	}
	if (targetOpcode == 34) {
		g_briefingLastNarratedTextBlockIdx = 0;
		g_briefingTextPageNumber = 0;
		return BriefingScript_ResetState();
	}
	return BriefingScript_AdvanceUntilTime(targetTime, 0);
}

/* Plays the script frame at g_briefingScript.currentFrame, raises
 * currentFrame by one, and returns the cursor's new word index. First sets
 * g_briefingTextSlotsChanged, g_briefingMapFgMarkersChanged,
 * g_briefingMapLabelsChanged, g_briefingMapCenterDirty,
 * g_briefingMapScaleDirty and g_briefingScriptPauseMarkerReached to 0. An
 * entry is a time word, an opcode word and the opcode's argument words, as
 * g_briefingScriptOpcodeArgCounts gives them. It reads entries from the cursor
 * while their time is not past the current frame, applies those whose time
 * equals it and skips earlier ones, and leaves the cursor on the first later
 * entry. Opcodes: 1 pause marker; 3 clears both text slots; 4 and 5 show text
 * block args[0] in slot 0 or 1; 6 and 7 set the map center or scale target to
 * (args[0], args[1]), and the current value too when the entry's time is 0 or
 * applyInstantly is set; 8 clears the flight group markers; 9 to 16 show
 * marker opcode - 9 on flight group args[0]; 17 clears the labels; 18 to 25
 * show label opcode - 18 with text args[0] at map point (args[1], args[2])
 * in shade ramp args[3]; any other opcode does nothing. A new marker or
 * label starts at age 0, or 80 with applyInstantly set. Without
 * applyInstantly, a marker plays "sfxTarget2" for an IFF 1 flight group and
 * "sfxTarget1" for others, and a label with text plays "sfxText", each only
 * with datapad sounds on, at 12 times the datapad volume. Checks no opcode,
 * argument or index range. */
// FUNCTION: XVT 0x4F7590
int16_t BriefingScript_AdvanceFrame(int16_t applyInstantly)
{
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

	if (eventTime <= g_briefingScript.currentFrame) {
		do {
			savedCursorWordIndex = cursorWordIndex;
			eventTime = g_briefingScript.words[cursorWordIndex++];
			opcode = g_briefingScript.words[cursorWordIndex++];
			argumentCount = g_briefingScriptOpcodeArgCounts[opcode];
			for (argumentIndex = 0; argumentIndex < argumentCount;
			     ++argumentIndex) {
				args[argumentIndex] =
					g_briefingScript
						.words[cursorWordIndex++];
			}

			if (eventTime == g_briefingScript.currentFrame) {
				switch (opcode) {
				case 1:
					g_briefingScriptPauseMarkerReached = 1;
					break;
				case 3:
					for (slotIndex = 0; slotIndex < 2;
					     ++slotIndex) {
						g_briefingTextSlotActive
							[slotIndex] = 0;
					}
					g_briefingTextSlotsChanged = 1;
					break;
				case 4:
				case 5:
					slotIndex = opcode - 4;
					g_briefingTextSlotActive[slotIndex] = 1;
					g_briefingTextSlotBlockIdx[slotIndex] =
						args[0];
					break;
				case 6:
					if (eventTime == 0 ||
					    applyInstantly != 0) {
						g_briefingMapTargetCenter.x =
							args[0];
						g_briefingMapCenter.x = args[0];
						g_briefingMapTargetCenter.y =
							args[1];
						g_briefingMapCenter.y = args[1];
					} else {
						g_briefingMapTargetCenter.x =
							args[0];
						g_briefingMapTargetCenter.y =
							args[1];
					}
					g_briefingMapCenterDirty = 1;
					break;
				case 7:
					if (eventTime == 0 ||
					    applyInstantly != 0) {
						g_briefingMapTargetScale.x =
							args[0];
						g_briefingMapScale.x = args[0];
						g_briefingMapTargetScale.y =
							args[1];
						g_briefingMapScale.y = args[1];
					} else {
						g_briefingMapTargetScale.x =
							args[0];
						g_briefingMapTargetScale.y =
							args[1];
					}
					g_briefingMapScaleDirty = 1;
					break;
				case 8:
					for (slotIndex = 0; slotIndex < 8;
					     ++slotIndex) {
						g_briefingMapFgMarkerActive
							[slotIndex] = 0;
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
					if (applyInstantly == 0) {
						iff = g_frontendMission
							      .flightGroups
								      [args[0]]
							      .iff;
						if (iff > 2) {
							iff = 2;
						}
						if (iff == 1) {
							if (g_gameConfig
								    .sfxDatapadEnabled !=
							    0) {
								FrontendSound_PlayUISound(
									"sfxTarget2",
									1, 0,
									127,
									12 * g_gameConfig
											.sfxDatapadVolume,
									63);
							}
						} else if (
							g_gameConfig
								.sfxDatapadEnabled !=
							0) {
							FrontendSound_PlayUISound(
								"sfxTarget1", 1,
								0, 127,
								12 * g_gameConfig
										.sfxDatapadVolume,
								63);
						}
					}
					slotIndex = opcode - 9;
					g_briefingMapFgMarkerActive[slotIndex] =
						1;
					g_briefingMapFgMarkerAge[slotIndex] =
						applyInstantly == 0 ? 0 : 80;
					g_briefingMapFgMarkerFlightGroupIdx
						[slotIndex] = args[0];
					break;
				case 17:
					for (slotIndex = 0; slotIndex < 8;
					     ++slotIndex) {
						g_briefingMapLabelActive
							[slotIndex] = 0;
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
					if (applyInstantly == 0) {
						strcpy(labelText,
						       g_briefingMapLabelTexts
							       [args[0]]);
						if ((uint16_t)strlen(
							    labelText) != 0 &&
						    g_gameConfig.sfxDatapadEnabled !=
							    0) {
							FrontendSound_PlayUISound(
								"sfxText", 1, 0,
								127,
								12 * g_gameConfig
										.sfxDatapadVolume,
								63);
						}
					}
					slotIndex = opcode - 18;
					g_briefingMapLabelActive[slotIndex] = 1;
					g_briefingMapLabelAge[slotIndex] =
						applyInstantly == 0 ? 0 : 80;
					g_briefingMapLabelTextIdx[slotIndex] =
						args[0];
					g_briefingMapLabelX[slotIndex] =
						args[1];
					g_briefingMapLabelY[slotIndex] =
						args[2];
					g_briefingMapLabelStyle[slotIndex] =
						args[3];
					break;
				default:
					break;
				}
			}
		} while (eventTime <= g_briefingScript.currentFrame);
	}

	++g_briefingScript.currentFrame;
	g_briefingScript.cursorWordIndex = savedCursorWordIndex;
	return savedCursorWordIndex;
}

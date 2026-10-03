#ifndef XVT_FRONTEND_BRIEFING_SCRIPT_H
#define XVT_FRONTEND_BRIEFING_SCRIPT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One briefing's script, read whole from the mission file;
 * BriefingScript_ResetState then sets currentFrame and cursorWordIndex. */
struct FrontendBriefingScript {
	/* Frames played before the briefing starts over. */
	int16_t durationFrames;
	int16_t currentFrame;	 /* Next frame to play, counted from 0. */
	int16_t cursorWordIndex; /* Index in words of the next entry to read. */
	/* Read with the script, set to 2 by BriefingScript_InitDefaultScript;
	 * nothing reads it. */
	int16_t headerWord06;
	/* Read with the script, set to 0 by BriefingScript_InitDefaultScript;
	 * nothing reads it. */
	int16_t headerWord08;
	/* Entries: a time in frames, an opcode, then the opcode's arguments. */
	int16_t words[400];
};

extern struct FrontendBriefingScript g_briefingScript;
extern int16_t g_briefingPlaybackActive;
extern int g_briefingLastNarratedTextBlockIdx;
extern int g_briefingTextPageNumber;
extern int16_t g_briefingTextSlotActive[2];
extern int16_t g_briefingTextSlotBlockIdx[2];
extern int16_t g_briefingTextSlotsChanged;
extern int16_t g_briefingScriptPauseMarkerReached;
extern const int16_t g_briefingScriptOpcodeArgCounts[35];

void BriefingScript_AdvanceOrResetAtEnd(int frameCounter);
int16_t BriefingScript_InitDefaultScript(void);
int16_t BriefingScript_ResetState(void);
int16_t BriefingScript_AdvanceUntilTime(int16_t targetTime,
					int16_t initializeState);
int16_t BriefingScript_AdvanceToNextVisibleLine(void);
int16_t BriefingScript_AdvanceFrame(int16_t applyInstantly);

#ifdef __cplusplus
}
#endif

#endif

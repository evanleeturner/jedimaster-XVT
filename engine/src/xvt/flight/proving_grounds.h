#ifndef XVT_FLIGHT_PROVING_GROUNDS_H
#define XVT_FLIGHT_PROVING_GROUNDS_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ProvingGroundsStatusLabelId {
	PROVING_STATUS_LEVEL = 0x0,
	PROVING_STATUS_SEGMENTS_LEFT = 0x1,
	PROVING_STATUS_SEGMENTS_DONE = 0x2,
	PROVING_STATUS_TARGETS_HIT = 0x3,
	PROVING_STATUS_SCORE = 0x4,
} ProvingGroundsStatusLabelId;

extern int g_provingGroundsScoreDecimalDivisors[9];
extern const char* g_provingGroundsStatusLabels[5];
extern int16_t g_provingGroundsLocalPlayerRollHistory[4];
extern int16_t g_provingGroundsLocalPlayerPitchHistory[4];
extern int16_t g_provingGroundsLocalPlayerYawHistory[4];
extern int g_provingGroundsLocalPlayerWorldZHistory[4];
extern int g_provingGroundsLocalPlayerWorldXHistory[4];
extern int g_provingGroundsLocalPlayerWorldYHistory[4];
extern uint16_t g_provingGroundsCurrentCheckpointObjIdx;

void ProvingGrounds_RecordLocalPlayerPoseHistory(void);
void ProvingGrounds_DrawCourseObject(uint16_t objectIndex);
void ProvingGrounds_InitCourseObjects(void);
void ProvingGrounds_StartLevel(uint16_t level);
void ProvingGrounds_UpdateCourse(void);
int ProvingGrounds_HasPlayerCrossedCheckpoint(uint16_t checkpointObjIdx);
void ProvingGrounds_DrawStatusPanel(int16_t x, int16_t y);
void ProvingGrounds_DrawScoreDecimal(int score, unsigned int width, unsigned int minDigits);
void ProvingGrounds_RenderTimeBonusFrame(void);

#ifdef __cplusplus
}
#endif

#endif

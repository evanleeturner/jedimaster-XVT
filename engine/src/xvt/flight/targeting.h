#ifndef XVT_FLIGHT_TARGETING_H
#define XVT_FLIGHT_TARGETING_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t Targeting_ScoreCandidate(uint16_t a1, int16_t a2, int a3);
extern uint16_t g_targetAngleScore;
void Targeting_DrawSceneObjectBoxes(void);
void Targeting_DrawObjectBox(uint16_t objectIdx, uint16_t componentIdx, uint8_t colorIndex);
int Targeting_GetObjectBoxExtent(unsigned int objectIdx);
void Targeting_ProjectObjectOrMissionPoint(unsigned int objOrMissionPointRef, uint16_t componentIdx,
										   int* outScreenX, int* outScreenY, int* outViewZ);
void Targeting_ComputeProjectedObjectExtent(uint16_t objectIdx, uint16_t* outWidth, uint16_t* outHeight,
											int cameraX, int cameraY, int cameraZ);

#ifdef __cplusplus
}
#endif

#endif

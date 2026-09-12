#ifndef XVT_RENDER_FLIGHT_LIGHT_H
#define XVT_RENDER_FLIGHT_LIGHT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ObjectPointLight {
	int x;
	int y;
	int z;
	int intensity;
};

extern int g_objectPointLightCount;
extern ObjectPointLight g_objectPointLights[10];

void FlightLight_ResetSoftwareFaceSampleCache(void);
float FlightLight_ComputeSoftwareFaceSampleIntensity(SceneFace* face, int screenX, int screenY,
													 float projectedDepth);
void FlightLight_SetupObjectLighting(ObjectRecord* object);
void FlightLight_SetupObjectLightingByIndex(unsigned int objectIndex);

#ifdef __cplusplus
}
#endif

#endif

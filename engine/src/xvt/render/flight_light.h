#ifndef XVT_RENDER_FLIGHT_LIGHT_H
#define XVT_RENDER_FLIGHT_LIGHT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ObjectPointLight {
	int x; /* Offset along the lit object's side axis, Q15 dot product. */
	int y; /* Offset along its forward axis, negated. */
	int z; /* Offset along its up axis. */
	int intensity; /* Brightness, the explosion's value times 8. */
};

extern int g_objectPointLightCount;
extern struct ObjectPointLight g_objectPointLights[10];

void FlightLight_ResetSoftwareFaceSampleCache(void);
float FlightLight_ComputeSoftwareFaceSampleIntensity(struct SceneFace *face,
						     int screenX, int screenY,
						     float reciprocalDepth);
void FlightLight_SetupObjectLighting(struct ObjectRecord *object);
void FlightLight_SetupObjectLightingByIndex(unsigned int objectIndex);

#ifdef __cplusplus
}
#endif

#endif

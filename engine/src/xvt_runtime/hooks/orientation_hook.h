#ifndef XVT_RUNTIME_ORIENTATION_HOOK_H
#define XVT_RUNTIME_ORIENTATION_HOOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XvtOrientationAngles {
	uint16_t yaw, pitch, roll;
} XvtOrientationAngles;

/* Apply local pitch/yaw using OpenXWA's gimbal-lock-safe rotation path. */
XvtOrientationAngles XvtOrientation_ApplyPitchYaw(XvtOrientationAngles current, int pitchDeltaQ16,
												  int negYawDeltaQ16);
/* Integer counterpart for deterministic network simulation. */
XvtOrientationAngles XvtOrientation_ApplyPitchYawFixed(XvtOrientationAngles current, int pitchDeltaQ16,
													   int negYawDeltaQ16);

#ifdef __cplusplus
}
#endif
#endif

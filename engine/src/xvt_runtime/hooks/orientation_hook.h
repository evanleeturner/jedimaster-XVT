#ifndef XVT_RUNTIME_ORIENTATION_HOOK_H
#define XVT_RUNTIME_ORIENTATION_HOOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turns an orientation by a pitch and a yaw about the craft's own axes, through a rotation matrix, so
 * pointing straight up or down does not lock the turn. Angles are 16-bit binary angles, 65536 to a turn,
 * and both deltas use the same unit. The modern USER_calcdeltapitch calls ApplyPitchYaw in place of the
 * original code. */

typedef struct XvtOrientationAngles {
	uint16_t yaw, pitch, roll;
} XvtOrientationAngles;

/* Apply local pitch/yaw using OpenXWA's gimbal-lock-safe rotation path. */
/* Applies the yaw change, given negated, then the pitch change, each about the current body axes. In
 * NETWORK_125 it returns ApplyPitchYawFixed's result. Otherwise it uses float math, and zero deltas are
 * not skipped: a call can rewrite the angles in an equivalent form, and the orientation can drift by
 * rounding. At straight up or down, yaw takes a fixed value and roll carries the heading. */
XvtOrientationAngles XvtOrientation_ApplyPitchYaw(XvtOrientationAngles current, int pitchDeltaQ16,
												  int negYawDeltaQ16);
/* Integer counterpart for deterministic network simulation. */
/* Integer math only, so every host gets the same bits. Returns current unchanged when both deltas are 0
 * modulo 65536; otherwise as ApplyPitchYaw. The two paths are not bit-identical: they agree on the
 * resulting orientation to within rounding, and near straight up or down they can return different
 * angles for it. */
XvtOrientationAngles XvtOrientation_ApplyPitchYawFixed(XvtOrientationAngles current, int pitchDeltaQ16,
													   int negYawDeltaQ16);

#ifdef __cplusplus
}
#endif
#endif

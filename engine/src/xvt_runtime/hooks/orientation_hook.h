#ifndef XVT_RUNTIME_ORIENTATION_HOOK_H
#define XVT_RUNTIME_ORIENTATION_HOOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turns an orientation by a pitch and a yaw about the craft's own axes, through a rotation matrix, so
 * pointing straight up or down does not lock the turn. Angles are 16-bit binary angles, 65536 to a turn,
 * and both deltas use the same unit. The modern player_apply_pitch_yaw_steps calls ApplyPitchYaw in place of the
 * original code. */

struct xvt_orientation_angles {
	uint16_t yaw;
	uint16_t pitch;
	uint16_t roll;
};

/* Apply local pitch/yaw using OpenXWA's gimbal-lock-safe rotation path. */
/* Applies the yaw change, given negated, then the pitch change, each about the current body axes. In
 * NETWORK_125 it returns ApplyPitchYawFixed's result. Otherwise it uses float math, and zero deltas are
 * not skipped: a call can rewrite the angles in an equivalent form, and the orientation can drift by
 * rounding. At straight up or down, yaw takes a fixed value and roll carries the heading. */
struct xvt_orientation_angles
xvt_orientation_apply_pitch_yaw(struct xvt_orientation_angles current,
				int pitch_delta_q16, int neg_yaw_delta_q16);
/* Integer counterpart for deterministic network simulation. */
/* Integer math only, so every host gets the same bits. Returns current unchanged when both deltas are 0
 * modulo 65536; otherwise as ApplyPitchYaw. The two paths are not bit-identical: they agree on the
 * resulting orientation to within rounding, and near straight up or down they can return different
 * angles for it. */
struct xvt_orientation_angles
xvt_orientation_apply_pitch_yaw_fixed(struct xvt_orientation_angles current,
				      int pitch_delta_q16,
				      int neg_yaw_delta_q16);

#ifdef __cplusplus
}
#endif
#endif

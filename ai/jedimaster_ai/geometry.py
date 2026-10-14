"""Directions and distances between craft, in the engine's units and integers.

Purpose:
    Where a craft must point to face another, how far away it is, and how far
    a heading is from another. Behaviors steer and aim with these.

The engine's conventions (``engine/src/xvt/math/trig2.c``, ``trig2_ctop``):

    yaw     65,536 to a full turn: 0 along +y, 0x4000 along +x
    pitch   0 straight up (+z), 0x4000 level, 0x8000 straight down

Invariants:
    - Integers only, so every machine and every Python gets the same numbers:
      the arctangent is a CORDIC with the 16 step angles below, and lengths
      are ``math.isqrt``. Within 4 units of the exact angle. The sine runs
      the same steps the other way, within 20 65,536ths, and is odd.
    - An offset of 0 has yaw 0x4000 and is level.
"""

from __future__ import annotations

import math

TURN = 0x10000
HALF_TURN = 0x8000
QUARTER_TURN = 0x4000
LEVEL = QUARTER_TURN
# atan(2**-i) in 65,536ths of a turn, rounded: the CORDIC's step angles.
_STEP_ANGLES = (8192, 4836, 2555, 1297, 651, 326, 163, 81, 41, 20, 10, 5, 3, 1, 1, 0)
# Inputs are widened by this many bits first, so short offsets keep their
# angle through the steps' shifts.
_WIDEN = 24


def arctangent(y: int, x: int) -> int:
    """Return the angle of (x, y) from +x toward +y, 0 to 65,535."""
    if x == 0 and y == 0:
        return 0
    angle = 0
    if x < 0:
        x, y, angle = -x, -y, HALF_TURN
    x <<= _WIDEN
    y <<= _WIDEN
    for step, step_angle in enumerate(_STEP_ANGLES):
        if y > 0:
            x, y = x + (y >> step), y - (x >> step)
            angle += step_angle
        else:
            x, y = x - (y >> step), y + (x >> step)
            angle -= step_angle
    return angle % TURN


def bearing(dx: int, dy: int, dz: int) -> tuple[int, int]:
    """Return the yaw and pitch that point along an offset."""
    yaw = (QUARTER_TURN - arctangent(dy, dx)) % TURN
    across = math.isqrt(dx * dx + dy * dy)
    pitch = (QUARTER_TURN - arctangent(dz, across)) % TURN
    return yaw, pitch


def distance(dx: int, dy: int, dz: int) -> int:
    """Return an offset's length, rounded down."""
    return math.isqrt(dx * dx + dy * dy + dz * dz)


def angle_between(first: int, second: int) -> int:
    """Return how far apart two angles are, either way round: 0 to 0x8000."""
    apart = (first - second) % TURN
    return TURN - apart if apart > HALF_TURN else apart


def signed(angle: int) -> int:
    """Return an angle as -0x8000 to 0x7FFF: negative counterclockwise."""
    return (angle + HALF_TURN) % TURN - HALF_TURN


# The CORDIC's gain over its 16 steps, inverted, in 65,536ths: a vector
# started this long ends 65,536 long.
_GAIN = 39797


def sine(angle: int) -> int:
    """Return the sine of an angle, in 65,536ths: 0 at 0 and a half turn,
    which the steps alone miss by 3."""
    angle = signed(angle)
    sign = -1 if angle < 0 else 1
    angle = abs(angle)
    if angle > QUARTER_TURN:
        angle = HALF_TURN - angle
    if angle == 0:
        return 0
    x, y = _GAIN << 8, 0
    for step, step_angle in enumerate(_STEP_ANGLES):
        if angle >= 0:
            x, y = x - (y >> step), y + (x >> step)
            angle -= step_angle
        else:
            x, y = x + (y >> step), y - (x >> step)
            angle += step_angle
    return sign * ((y + 0x80) >> 8)


def sweeps(nose: int, heading: int, toward: int) -> bool:
    """Return whether turning from nose to heading the shortest way swings
    the nose across toward, the angles alike yaws."""
    turn = signed(heading - nose)
    off = signed(toward - nose)
    return 0 < off < turn or turn < off < 0

"""Shared setup for the swerve fences: homing shots aimed at a craft.

``aimed`` places each shot at an offset from the craft's own position, so a
fence reads how far off the shot is, not where its craft sits.
"""

from __future__ import annotations

import dataclasses

from jedimaster_ai.world import CraftView
from jedimaster_ai.world import THREATS_MAX

# The object index of the first shot ``aimed`` lists; the next take SHOT + 1
# and on.
SHOT = 20
# The engine's object types of a proton torpedo and a concussion missile
# (laser.h).
TORPEDO = 143
MISSILE = 144


def aimed(
    view: CraftView, *offsets: tuple[int, int, int], kind: int = TORPEDO
) -> CraftView:
    """Return the view with a homing shot of kind aimed at it at each offset
    (x, y, z) from the craft, in the engine's slot order, none decoyed."""
    rows = [
        (SHOT + n, kind, view.x + dx, view.y + dy, view.z + dz)
        for n, (dx, dy, dz) in enumerate(offsets)
    ]
    rows += [(0, 0, 0, 0, 0)] * (THREATS_MAX - len(rows))
    objects, kinds, xs, ys, zs = (tuple(column) for column in zip(*rows, strict=True))
    return dataclasses.replace(
        view,
        threat_count=len(offsets),
        threat_object=objects,
        threat_type=kinds,
        threat_x=xs,
        threat_y=ys,
        threat_z=zs,
        threat_decoyed=(0,) * THREATS_MAX,
    )

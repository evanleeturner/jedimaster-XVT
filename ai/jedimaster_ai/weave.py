"""The weave: a craft with an enemy on its tail swerves from side to side.

Purpose:
    A behavior whose craft is chased starts a weave, then flies it at each
    think until it ends. Each swerve holds a heading SWERVE_YAW to one side
    of the weave's course (the craft's own when the weave began, or the one
    the behavior names, such as the way home), at a pitch the dice pick,
    and the next swerve takes the other side, so a chaser has to turn after
    it again and again. The craft rolls without stopping the way it swerves.
    The weave ends at the first think the behavior says the tail is
    clear, or once SWERVES_MAX swerves are flown; ended at its limit, the
    craft must fight for REST_TICKS before it may weave again.

    The blueprint is 1997's scissors maneuver (``paiman_scissorsmaneuver``,
    deleted with the 1997 AI): its swerve times, its roll, and its flare at a
    swerve for a pilot of AI level 3 or more on one draw in four (ruling 40).
    The 1997 maneuver ran 10 to 17 simulated seconds, 4 to 8 swerves, blind
    to whether the chaser was still there; this one is used less (ruling
    41): three swerves at most, ending when the tail is clear.

The state, STATE_BYTES little-endian bytes in the flying behavior's scratch:
u16 the course the weave began on, u16 the swerve's pitch, u8 its side
(SIDE_MINUS or SIDE_PLUS), u8 the swerves flown, the current one included (0
for no weave), u32 the tick the swerve ends, u32 the tick before which no
weave starts.

Invariants:
    - The behavior decides when the craft is chased; the weave never looks
      at the contacts.
    - Random choices come from the craft's dice in its notebook.
    - A flare is asked for only when the view shows one ready.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from . import dice
from . import geometry
from . import tactics
from .world import Countermeasure
from .world import CraftView
from .world import Steer

logger = logging.getLogger(__name__)

# 1997's swerve times in ticks: the first swerve 472 (two simulated
# seconds), each later one 472 and up to 235 more.
FIRST_SWERVE_TICKS = 472
SWERVE_SPREAD_TICKS = 236
# Ruling 41: three swerves at most, 1,416 to 1,886 ticks in all (5.7 to 7.5
# real seconds).
SWERVES_MAX = 3
# After a weave flown to its limit, the craft fights at least as long as the
# longest weave lasts, so a craft that cannot shake its chaser weaves at most
# half the time.
REST_TICKS = FIRST_SWERVE_TICKS + (SWERVES_MAX - 1) * (
    FIRST_SWERVE_TICKS + SWERVE_SPREAD_TICKS - 1
)

# The heading a swerve holds, either side of the weave's course: 0x6000
# between two swerves, which the slowest craft measured turns in 630 ticks
# (a quarter turn in 420), so a craft turns through most of each swerve, as
# 1997's did.
SWERVE_YAW = 0x3000
# A swerve's pitch is at most this far from level: the spread of the single
# jink the weave replaced.
SWERVE_PITCH_SPREAD = 0x1800
# The roll asked for at each think leads the craft's own by this much, the
# way it swerves, so it rolls without stopping as 1997's did; under half a
# turn, the engine rolls the short way, which is that way.
ROLL_LEAD = 0x4000
# 1997: a pilot of AI level 3 or more, skill tier 2, drops a flare at a
# swerve on one draw in FLARE_ODDS.
FLARE_TIER = 2
FLARE_ODDS = 4

SIDE_MINUS = 0
SIDE_PLUS = 1

_STATE = struct.Struct("<HHBBII")
STATE_BYTES = _STATE.size


@dataclasses.dataclass(frozen=True)
class Weave:
    """A weave's state, kept in the flying behavior's scratch."""

    course: int = 0
    pitch: int = 0
    side: int = SIDE_MINUS
    swerves: int = 0
    swerve_ends: int = 0
    rest_until: int = 0

    @classmethod
    def read(cls, raw: bytes) -> Weave:
        """Return the weave STATE_BYTES of raw hold."""
        return cls(*_STATE.unpack_from(raw))

    def write(self) -> bytes:
        """Return the weave as STATE_BYTES bytes."""
        return _STATE.pack(
            self.course,
            self.pitch,
            self.side,
            self.swerves,
            self.swerve_ends,
            self.rest_until,
        )

    @property
    def flying(self) -> bool:
        """Whether a weave is under way."""
        return self.swerves > 0


@dataclasses.dataclass(frozen=True)
class Move:
    """What a weaving craft does at one think: its steering, and whether it
    drops a flare."""

    steer: Steer
    flare: bool = False


def may_start(weave: Weave, tick: int) -> bool:
    """Return whether a craft may start a weave at this tick."""
    return not weave.flying and tick >= weave.rest_until


def start(
    view: CraftView,
    threat: int,
    weave: Weave,
    state: int,
    tick: int,
    course: int | None = None,
) -> tuple[Weave, Move, int]:
    """Return the weave a chased craft starts about a course, its present
    course when None, its first move, and the dice after them."""
    side, state = dice.roll(state, 2)
    pitch, state = _swerve_pitch(state)
    weave = Weave(
        course=view.yaw if course is None else course,
        pitch=pitch,
        side=side,
        swerves=1,
        swerve_ends=tick + FIRST_SWERVE_TICKS,
        rest_until=weave.rest_until,
    )
    logger.debug(
        "weave.start object=%d threat=%d course=%d side=%d pitch=%d ends=%d",
        view.object_index,
        threat,
        weave.course,
        weave.side,
        weave.pitch,
        weave.swerve_ends,
    )
    return weave, _move(view, weave), state


def fly(
    view: CraftView, weave: Weave, chased: bool, state: int, tick: int
) -> tuple[Weave, Move | None, int]:
    """Return the weave after this think, the craft's move, and the dice after
    them; the move is None when the weave ends at this think, the tail clear
    or its last swerve flown."""
    if not chased or (tick >= weave.swerve_ends and weave.swerves >= SWERVES_MAX):
        ended = Weave(rest_until=tick + REST_TICKS if chased else weave.rest_until)
        logger.debug(
            'weave.end object=%d reason="%s" swerves=%d rest_until=%d',
            view.object_index,
            "limit" if chased else "clear",
            weave.swerves,
            ended.rest_until,
        )
        return ended, None, state
    if tick < weave.swerve_ends:
        return weave, _move(view, weave), state
    longer, state = dice.roll(state, SWERVE_SPREAD_TICKS)
    pitch, state = _swerve_pitch(state)
    flare, state = _flare(view, state)
    weave = dataclasses.replace(
        weave,
        pitch=pitch,
        side=SIDE_PLUS - weave.side,
        swerves=weave.swerves + 1,
        swerve_ends=tick + FIRST_SWERVE_TICKS + longer,
    )
    logger.debug(
        "weave.swerve object=%d swerve=%d side=%d pitch=%d flare=%d ends=%d",
        view.object_index,
        weave.swerves,
        weave.side,
        weave.pitch,
        int(flare),
        weave.swerve_ends,
    )
    return weave, _move(view, weave, flare), state


def _swerve_pitch(state: int) -> tuple[int, int]:
    """Return a swerve's pitch by the dice, and the dice after it."""
    lift, state = dice.roll(state, 2 * SWERVE_PITCH_SPREAD + 1)
    return geometry.LEVEL + lift - SWERVE_PITCH_SPREAD, state


def _flare(view: CraftView, state: int) -> tuple[bool, int]:
    """Return whether the craft drops a flare at a swerve, and the dice after;
    as in 1997, only a pilot of FLARE_TIER draws."""
    if tactics.skill_tier(view.skill) < FLARE_TIER:
        return False, state
    draw, state = dice.roll(state, FLARE_ODDS)
    hit = draw == 0
    return (
        hit
        and view.countermeasure == Countermeasure.FLARE
        and view.countermeasure_ready
    ), state


def _move(view: CraftView, weave: Weave, flare: bool = False) -> Move:
    """Return the steering that holds the swerve's heading and pitch and keeps
    the craft rolling its way."""
    sign = 1 if weave.side == SIDE_PLUS else -1
    yaw = (weave.course + sign * SWERVE_YAW) % geometry.TURN
    roll = (view.roll + sign * ROLL_LEAD) % geometry.TURN
    return Move(Steer(yaw, weave.pitch, roll), flare)

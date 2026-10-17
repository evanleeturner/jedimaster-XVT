"""Lord: the rules a Sith Lord flies by and no other rank does.

Purpose:
    A craft at the Sith Lord's skill level (``tactics.SITH_LORD_LEVEL``, 7)
    flies as every fighting craft does, at its own skill tier, but it never
    retreats and it fires warheads unguided at any craft:

    - it fights every enemy after it, whatever its hull
      (``behaviors.attack.fights_back``): AI rulings 33 and 34, which send a
      craft below three quarters of its hull away evading, do not hold for it
    - its group's abort trigger never sends it home or stops its drop
      (``home_books``); its departure time and its orders done still do, so
      a mission's goals score as they did
    - it fires a warhead unguided at a small craft, a player's too, as well
      as at a freighter, starship or platform (``warheads.fires_unguided``,
      missiles idea E)
    - at a small craft its whole lead follows the target's turn: its
      cannons point where a shot meets the target on its predicted flight
      (``cannons.meet``), and it walks its shots in by its tracers (``aim``,
      AI rulings 220 and 231): each of its own tracers that passed the
      target since its last look shows how far behind, beside or below the
      target it went, and a share of the mean miss moves a correction kept
      in the target's own axes, so it turns with it; at a big ship it takes
      the plain lead every pilot takes (``cannons.aim``)

Invariants:
    - The rank is the view's alone (``CraftView.skill_level``, the level the
      engine promoted the craft to), so every machine agrees.
    - Pure: each answer depends only on the view, the question and what the
      caller kept. The abort book a Lord sets aside is logged at DEBUG each
      time, and each look of its aim.
    - Integers only, as ``geometry``.
"""

from __future__ import annotations

import dataclasses
import logging

from . import cannons
from . import geometry
from . import tactics
from .world import Books
from .world import Contact
from .world import CraftView
from .world import Question
from .world import Tracer

logger = logging.getLogger(__name__)


def is_lord(view: CraftView) -> bool:
    """Return whether the craft flies at the Sith Lord's skill level."""
    return view.skill_level == tactics.SITH_LORD_LEVEL


def home_books(view: CraftView, books: int) -> int:
    """Return those of the books that send the craft home: all of them, but
    the abort book for a Sith Lord, which never retreats."""
    if not is_lord(view) or not books & Books.ABORTED:
        return books
    logger.debug(
        "lord.stays object=%d books=%d",
        view.object_index,
        books,
    )
    return books & ~Books.ABORTED


# A look moves the correction by GAIN, a fraction, of its tracers' mean miss,
# each axis held within MAX_CORRECTION world units; a tracer counts only when
# its pass came within NEAR_PASS of the target, so shots at another craft
# count for nothing. Measured offline against scripted targets (AI ruling
# 240): a GAIN of 1/4 to 1/2 hit within 2% as often, a limit of 150 to 2,400
# and a NEAR_PASS of 512 to 4,096 within 1%, so these stay; a whole GAIN hit
# 6% less, and no correction at all 11% less when the program saw its
# target 8 ticks late. The flight may still move GAIN and MAX_CORRECTION (AI
# ruling 224).
GAIN = (1, 2)
MAX_CORRECTION = 600
NEAR_PASS = 0x800


@dataclasses.dataclass(frozen=True)
class Offset:
    """A distance in a target's own axes, in world units: ahead of it, to its
    right and above it, each negative the other way."""

    ahead: int = 0
    side: int = 0
    up: int = 0


def aim(
    view: CraftView,
    question: Question,
    target: Contact,
    plain: cannons.Aim,
    fix: Offset,
    since: int,
    disabling: bool = False,
) -> tuple[cannons.Aim, Offset]:
    """Return a Sith Lord's aim at the target and its correction after this
    look. At a freighter, starship or platform, or with no cannon shot it
    knows, the plain aim (``cannons.aim``) and the correction as kept;
    otherwise the yaw and pitch at the meeting point on the target's
    predicted flight (``cannons.meet``) moved by the correction, lined up
    there by its tier's cone and range (``cannons.fires_at``). The tracers
    that passed the target after tick ``since`` move the correction first
    (``correct``)."""
    if tactics.is_bulky(target):
        return plain, fix
    shot = cannons.firing_shot(view, disabling)
    meeting = cannons.meet(view, target, shot, turning=True)
    if meeting is None:
        return plain, fix
    misses = passes(view, question, target, since)
    fix = correct(fix, misses)
    yaw, pitch, _ = cannons.heading_at(target, meeting.ticks)
    dx, dy, dz = _to_world(fix, yaw, pitch)
    meeting = dataclasses.replace(
        meeting, dx=meeting.dx + dx, dy=meeting.dy + dy, dz=meeting.dz + dz
    )
    yaw, pitch = geometry.bearing(meeting.dx, meeting.dy, meeting.dz)
    tier = tactics.skill_tier(view)
    lined_up = cannons.fires_at(view, target, tier, plain.reach, yaw, pitch)
    _log_lead(view, target, meeting, misses, fix, lined_up)
    return dataclasses.replace(plain, yaw=yaw, pitch=pitch, lined_up=lined_up), fix


def passes(
    view: CraftView, question: Question, target: Contact, since: int
) -> list[Offset]:
    """Return how far each of the craft's own tracers missed the target, in
    the target's axes, for those whose closest pass came after tick ``since``
    and by the question's tick, within NEAR_PASS. Each flies straight and
    the target is taken on its heading, so a tracer counts at one look
    alone."""
    window = question.tick - since
    misses = []
    for tracer in question.tracers:
        if tracer.source != view.object_index:
            continue
        miss = _closest_pass(tracer, target, window)
        if miss is not None:
            misses.append(miss)
    return misses


def correct(fix: Offset, misses: list[Offset]) -> Offset:
    """Return the correction moved by GAIN of the misses' mean (``mean``),
    each axis held within MAX_CORRECTION; unchanged with no miss."""
    if not misses:
        return fix
    numerator, denominator = GAIN
    moved = mean(misses)
    ahead, side, up = (
        min(
            max(kept + _toward_zero(axis * numerator, denominator), -MAX_CORRECTION),
            MAX_CORRECTION,
        )
        for kept, axis in zip(
            (fix.ahead, fix.side, fix.up),
            (moved.ahead, moved.side, moved.up),
            strict=True,
        )
    )
    return Offset(ahead, side, up)


def mean(misses: list[Offset]) -> Offset:
    """Return the misses' mean on each axis, rounded toward 0; 0 with none."""
    if not misses:
        return Offset()
    count = len(misses)
    return Offset(
        _toward_zero(sum(miss.ahead for miss in misses), count),
        _toward_zero(sum(miss.side for miss in misses), count),
        _toward_zero(sum(miss.up for miss in misses), count),
    )


def _closest_pass(tracer: Tracer, target: Contact, window: int) -> Offset | None:
    """Return where the target lay from the tracer at their closest pass, in
    the target's axes, when that pass came within the last ``window`` ticks
    and NEAR_PASS; else None. Velocities are in 65,536ths of world units a
    simulated second, so the pass's tick is their dot products' ratio."""
    shot = _velocity(tracer.yaw, tracer.pitch, tracer.speed)
    craft = _velocity(target.yaw, target.pitch, target.speed)
    apart = (tracer.x - target.x, tracer.y - target.y, tracer.z - target.z)
    closing = tuple(a - b for a, b in zip(shot, craft, strict=True))
    square = sum(each * each for each in closing)
    if square == 0:
        return None
    scale = cannons.SIMULATED_SECOND_TICKS << 16
    # The pass's tick from now is -(apart . closing) * scale / square.
    toward = -sum(a * c for a, c in zip(apart, closing, strict=True)) * scale
    if not -window * square < toward <= 0:
        return None
    miss = tuple(
        -_toward_zero(a * square * scale + c * toward, square * scale)
        for a, c in zip(apart, closing, strict=True)
    )
    if geometry.distance(*miss) > NEAR_PASS:
        return None
    ahead, side, up = _axes(target.yaw, target.pitch)
    return Offset(_dot(miss, ahead), _dot(miss, side), _dot(miss, up))


def _velocity(yaw: int, pitch: int, speed: int) -> tuple[int, int, int]:
    pace = cannons.pace(speed)
    ux, uy, uz = geometry.forward(yaw, pitch)
    return ux * pace, uy * pace, uz * pace


def _axes(
    yaw: int, pitch: int
) -> tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]:
    """Return a heading's own axes, unit vectors in 65,536ths: ahead, to its
    right, level, and above it, square to both (roll aside)."""
    return (
        geometry.forward(yaw, pitch),
        geometry.forward(yaw + geometry.QUARTER_TURN, geometry.LEVEL),
        geometry.forward(yaw, pitch - geometry.QUARTER_TURN),
    )


def _to_world(fix: Offset, yaw: int, pitch: int) -> tuple[int, int, int]:
    """Return a correction in the target's axes, at a heading, as a world
    offset."""
    ahead, side, up = _axes(yaw, pitch)
    return tuple(
        _toward_zero(fix.ahead * a + fix.side * s + fix.up * u, 1 << 16)
        for a, s, u in zip(ahead, side, up, strict=True)
    )


def _dot(offset: tuple[int, int, int], unit: tuple[int, int, int]) -> int:
    return _toward_zero(sum(o * u for o, u in zip(offset, unit, strict=True)), 1 << 16)


def _toward_zero(numerator: int, denominator: int) -> int:
    quotient = abs(numerator) // denominator
    return -quotient if numerator < 0 else quotient


def _log_lead(
    view: CraftView,
    target: Contact,
    meeting: cannons.Meeting,
    misses: list[Offset],
    fix: Offset,
    lined_up: bool,
) -> None:
    """Log a Lord's look: its aim point in world units and the ticks a shot
    takes to it, the tracers that counted and their mean miss, the
    correction after it and whether it fires."""
    x, y, z = view.x + meeting.dx, view.y + meeting.dy, view.z + meeting.dz
    moved = mean(misses)
    logger.debug(
        "lord.lead object=%d target=%d x=%d y=%d z=%d ticks=%d tracers=%d "
        "miss_ahead=%d miss_side=%d miss_up=%d fix_ahead=%d fix_side=%d "
        "fix_up=%d fire=%d",
        view.object_index,
        target.object_index,
        x,
        y,
        z,
        meeting.ticks,
        len(misses),
        moved.ahead,
        moved.side,
        moved.up,
        fix.ahead,
        fix.side,
        fix.up,
        lined_up,
    )

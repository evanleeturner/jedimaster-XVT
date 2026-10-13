"""Attack: the dogfight a craft flies for the Attack order.

Purpose:
    A craft with the Attack order picks the nearest craft its order names,
    turns toward it, closes, and fires its cannons while the target sits
    inside its aim cone and firing range. Too close ahead, it breaks off a
    quarter turn aside and comes round again; with an enemy on its tail that
    targets it, it jinks to a random side. Skill sets the aim cone, the
    firing range, the burst, how soon it looks again for a nearer target and
    how long a jink lasts.

    The rules' shape follows Starshatter Open's fighter pilot (target choice,
    fire control, breaking off, jinking with times by skill), written new in
    this program's integer units; no Starshatter code is copied. The firing
    numbers are the game's own: the 1997 pilot's ranges, cone and bursts.

Phases, the notebook's phase byte:

    SEEK    no target: hold fire, keep course, look again each think
    ATTACK  steer at the target, fire when lined up
    BREAK   too close ahead: hold a heading a quarter turn aside
    EVADE   an enemy on the tail: hold a jink heading

The scratch bytes, little-endian: u16 target index, u16 its signature,
u16 yaw and u16 pitch a break or jink holds, u32 the tick to look again for
a nearer target; the rest zero.

Invariants:
    - The decision follows from the view, the question and the notebook;
      random choices come from the craft's dice in its notebook.
    - It names only what the question showed: its target is a contact, and
      it fires only the cannon groups the view counts.
    - It targets only craft the engine marks hostile (``tactics.can_fight``).
    - A fighting craft thinks every FIGHT_INTERVAL ticks, whatever its skill.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import dice
from .. import geometry
from .. import notebook
from .. import tactics
from ..world import Actions
from ..world import CANNON_GROUPS
from ..world import CannonGroup
from ..world import Contact
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import LinkMode
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import BehaviorCode
from . import Decision

logger = logging.getLogger(__name__)

CODE = BehaviorCode.ATTACK
SEEK = 0
ATTACK = 1
BREAK = 2
EVADE = 3

# Ticks between a fighting craft's thinks: 64 ms, at every skill.
FIGHT_INTERVAL = 16
FULL_THROTTLE = 0xFFFF
# Two thirds, to stay behind a target it has caught.
CLOSE_THROTTLE = 0xAAAA
# Tier 0 flies at seven tenths at most while it attacks.
ROOKIE_THROTTLE = 0xB333

# By skill tier, 0 to 2. The cone narrows with skill: a better pilot holds
# fire until the shot is good.
AIM_CONE = (0x800, 0x600, 0x400)
FIRE_RANGE = (0x6000, 0x8000, 0xA000)
BURST = (3, 4, 5)
LOOK_AGAIN_TICKS = (1500, 750, 250)
JINK_TICKS = (250, 200, 150)

# The 1997 pilot's range changes: a fast target flying the same way, or
# across, is harder to hit; a starship or platform is easier.
FAST_SPEED = 25
SAME_HEADING = 0x2000
SAME_HEADING_CUT = 0x4000
OBLIQUE_HEADING = 0x5000
OBLIQUE_CUT = 0x2000
LARGE_TARGET_BONUS = 0x6000
# Closer than these, all cannons fire together, then in pairs; farther, one
# at a time.
ALL_CANNONS_RANGE = 0x2000
PAIRS_RANGE = 0x4000

BREAK_RANGE = 0x1000
BREAK_CONE = 0x2000
BREAK_TICKS = 250
# An enemy within this range, more than this angle off the nose and
# targeting the craft, is on its tail.
TAIL_RANGE = 0x8000
TAIL_ANGLE = 0x6000
JINK_YAW_LEAST = 0x2000
JINK_YAW_SPREAD = 0x2000
JINK_PITCH_SPREAD = 0x1800

_SCRATCH = struct.Struct("<HHHHI")
_U32_MAX = 0xFFFFFFFF


@dataclasses.dataclass(frozen=True)
class Memory:
    """The scratch bytes' fields."""

    target: int = NO_OBJECT
    signature: int = 0
    yaw: int = 0
    pitch: int = geometry.LEVEL
    look_again: int = 0

    @classmethod
    def read(cls, scratch: bytes) -> Memory:
        """Return the memory scratch bytes hold."""
        return cls(*_SCRATCH.unpack_from(scratch))

    def write(self) -> bytes:
        """Return the memory as scratch bytes."""
        packed = _SCRATCH.pack(
            self.target, self.signature, self.yaw, self.pitch, self.look_again
        )
        return packed + bytes(notebook.SCRATCH_BYTES - _SCRATCH.size)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move in its dogfight."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    memory = Memory.read(book.scratch) if book.phase != SEEK else Memory()
    tier = tactics.skill_tier(view.skill)
    interval = None if view.think_interval == FIGHT_INTERVAL else FIGHT_INTERVAL

    if book.phase in (BREAK, EVADE):
        held = BREAK_TICKS if book.phase == BREAK else JINK_TICKS[tier]
        if tick - book.phase_tick < held:
            actions = Actions(
                steer=Steer(memory.yaw, memory.pitch, view.roll),
                throttle=FULL_THROTTLE,
                think_interval=interval,
                lasers=(HOLD_FIRE, HOLD_FIRE),
            )
            return Decision(book.encode(), actions)

    target, memory = _choose_target(view, question, memory, tier)
    if target is None:
        book = book if book.phase == SEEK else book.in_phase(SEEK, tick)
        actions = Actions(
            think_interval=interval, target=NO_OBJECT, lasers=(HOLD_FIRE, HOLD_FIRE)
        )
        return Decision(
            dataclasses.replace(book, scratch=bytes(len(book.scratch))).encode(),
            actions,
        )

    dx, dy, dz = tactics.offset(view, target)
    reach = geometry.distance(dx, dy, dz)
    yaw, pitch = geometry.bearing(dx, dy, dz)
    off_nose = geometry.angle_between(yaw, view.yaw)
    off_level = geometry.angle_between(pitch, view.pitch)
    lined_up = (
        off_nose < AIM_CONE[tier]
        and off_level < AIM_CONE[tier]
        and reach < fire_range(view, target, tier)
    )

    if reach < BREAK_RANGE and off_nose < BREAK_CONE:
        side, state = dice.roll(book.dice, 2)
        held_yaw = (
            view.yaw + (geometry.QUARTER_TURN if side else -geometry.QUARTER_TURN)
        ) % geometry.TURN
        logger.debug(
            "attack.break object=%d target=%d distance=%d yaw=%d",
            view.object_index,
            target.object_index,
            reach,
            held_yaw,
        )
        memory = dataclasses.replace(memory, yaw=held_yaw, pitch=geometry.LEVEL)
        return _hold(view, book, state, BREAK, tick, memory, target, interval)

    threat = tail_threat(view, question)
    if threat is not None and not lined_up:
        turn, state = dice.roll(book.dice, JINK_YAW_SPREAD)
        side, state = dice.roll(state, 2)
        lift, state = dice.roll(state, 2 * JINK_PITCH_SPREAD + 1)
        swing = JINK_YAW_LEAST + turn
        held_yaw = (view.yaw + (swing if side else -swing)) % geometry.TURN
        held_pitch = geometry.LEVEL + lift - JINK_PITCH_SPREAD
        logger.debug(
            "attack.evade object=%d threat=%d yaw=%d pitch=%d",
            view.object_index,
            threat.object_index,
            held_yaw,
            held_pitch,
        )
        memory = dataclasses.replace(memory, yaw=held_yaw, pitch=held_pitch)
        return _hold(view, book, state, EVADE, tick, memory, target, interval)

    if book.phase != ATTACK:
        book = book.in_phase(ATTACK, tick)
    caught = reach < ALL_CANNONS_RANGE and off_nose < AIM_CONE[tier]
    throttle = CLOSE_THROTTLE if caught else FULL_THROTTLE
    if tier == 0:
        throttle = min(throttle, ROOKIE_THROTTLE)
    actions = Actions(
        steer=Steer(yaw, pitch, view.roll),
        throttle=throttle,
        think_interval=interval,
        target=target.object_index,
        lasers=cannon_fire(view, reach, tier) if lined_up else (HOLD_FIRE, HOLD_FIRE),
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def fire_range(view: CraftView, target: Contact, tier: int) -> int:
    """Return how far the craft fires at a target, by the 1997 pilot's rules."""
    reach = FIRE_RANGE[tier]
    if target.speed > FAST_SPEED:
        apart = geometry.angle_between(view.yaw, target.yaw)
        if apart < SAME_HEADING:
            reach -= SAME_HEADING_CUT
        elif apart < OBLIQUE_HEADING:
            reach -= OBLIQUE_CUT
    if tactics.is_large(target):
        reach += LARGE_TARGET_BONUS
    return reach


def cannon_fire(
    view: CraftView, reach: int, tier: int
) -> tuple[CannonGroup, CannonGroup]:
    """Return a burst for each cannon group the craft has, by range and tier."""
    if reach < ALL_CANNONS_RANGE:
        mode = LinkMode.ALL
    elif reach < PAIRS_RANGE:
        mode = LinkMode.PAIRS
    else:
        mode = LinkMode.ONE_AT_A_TIME
    burst = CannonGroup(mode, BURST[tier])
    first, second = (
        burst if group < view.cannon_group_count else HOLD_FIRE
        for group in range(CANNON_GROUPS)
    )
    return first, second


def tail_threat(view: CraftView, question: Question) -> Contact | None:
    """Return the nearest enemy on the craft's tail that targets it, or None."""
    nearest = None
    for position, contact in enumerate(question.contacts):
        if contact.target != view.object_index:
            continue
        if not tactics.can_fight(view, position, contact):
            continue
        dx, dy, dz = tactics.offset(view, contact)
        reach = geometry.distance(dx, dy, dz)
        if reach >= TAIL_RANGE:
            continue
        yaw, _ = geometry.bearing(dx, dy, dz)
        if geometry.angle_between(yaw, view.yaw) <= TAIL_ANGLE:
            continue
        if nearest is None or (reach, contact.object_index) < nearest[0]:
            nearest = ((reach, contact.object_index), contact)
    return None if nearest is None else nearest[1]


def _choose_target(
    view: CraftView, question: Question, memory: Memory, tier: int
) -> tuple[Contact | None, Memory]:
    """Return the target to fight, kept or new, and the memory naming it."""
    targets = tactics.order_targets(view, question)
    kept = next(
        (
            contact
            for contact in targets
            if contact.object_index == memory.target
            and contact.object_signature == memory.signature
        ),
        None,
    )
    if kept is not None and question.tick < memory.look_again:
        return kept, memory
    if not targets:
        return None, Memory()
    nearest = min(
        targets,
        key=lambda contact: (
            geometry.distance(*tactics.offset(view, contact)),
            contact.object_index,
        ),
    )
    if kept is None or nearest.object_index != kept.object_index:
        logger.info(
            "attack.target object=%d target=%d distance=%d",
            view.object_index,
            nearest.object_index,
            geometry.distance(*tactics.offset(view, nearest)),
        )
    look_again = min(question.tick + LOOK_AGAIN_TICKS[tier], _U32_MAX)
    memory = dataclasses.replace(
        memory,
        target=nearest.object_index,
        signature=nearest.object_signature,
        look_again=look_again,
    )
    return nearest, memory


def _hold(
    view: CraftView,
    book: notebook.Notebook,
    state: int,
    phase: int,
    tick: int,
    memory: Memory,
    target: Contact,
    interval: int | None,
) -> Decision:
    """Return the decision that starts a break or a jink."""
    book = dataclasses.replace(
        book.in_phase(phase, tick), dice=state, scratch=memory.write()
    )
    actions = Actions(
        steer=Steer(memory.yaw, memory.pitch, view.roll),
        throttle=FULL_THROTTLE,
        think_interval=interval,
        target=target.object_index,
        lasers=(HOLD_FIRE, HOLD_FIRE),
    )
    return Decision(book.encode(), actions)
